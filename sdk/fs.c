/*
 * sdk/fs.c -- the FS module shim: the SDK's ROM file system onto the
 * host's stdio and the extracted tree.
 *
 * Why a shim and not the matched library.  aletheia matches all of TwlSDK's
 * libfs, but every path through it ends in the cartridge: FS_Init builds the
 * ROM archive over CARD_ReadRom, the archive's FAT and FNT live in the ROM
 * image, and a read is a CARD transfer the ARM7 completes.  docs/boot.md's
 * rule puts anything that waits on the cartridge in a shim.  What the shim
 * keeps from the SDK is its surface, its return values and the state the
 * game can observe (the default DMA channel, the size of the table cache);
 * what it replaces is where the bytes come from: the files tools/ndsrom.py's
 * `extract` wrote, resolved through sdk/vfs.c (mod overlay first).
 *
 * The SDK names, from aletheia's layout/symbols_sdk.txt (libfs.TWL.HYB.a):
 *
 *     020788AC  FS_InitFile              02078B44  FS_SeekFile
 *     020788D8  FS_ConvertPathToFileID   02078B70  FS_ReadFile
 *     02078964  FS_OpenFileDirect        02078C18  FS_OpenFile
 *     020789A4  FS_OpenFileFast          02078C28  FS_GetLength (*)
 *     020789FC  FS_OpenFileEx            0207A5C8  FS_LoadArchiveTables
 *     02078A80  FS_CloseFile             0207A940  FS_CreateFileFromMemory
 *     02078A94  FS_GetFileLength         0207AC64  FS_SetDefaultDMA
 *     02078AEC  FS_GetFilePosition       0207AD04  FS_Init
 *
 * (*) 0x02078C28 is not in symbols_sdk.txt; src/matched/arm9/func_02020E08.c
 * names it FS_GetLength, the SDK's compatibility wrapper over
 * FS_GetFileLength, and its 12 bytes sit between FS_OpenFile (the wrapper
 * over FS_OpenFileEx) and FS_ChangeDir, the third wrapper.
 *
 * The matched callers spell each one either by its SDK name or as
 * func_<address>; both are defined, the address spelling as a one-line
 * forwarder, so there is one implementation.  The overlay half of libfs is
 * sdk/overlay.c.
 *
 * The object.  The game's FSFile is 0x48 bytes (the matched `struct
 * ArcFile` starts with u8 file[0x48]; func_02020E08's FSFile is u8[0x48]).
 * The shim owns those bytes and lays its own record into them, under two
 * constraints: at most 0x48 bytes, and no more than 4-byte alignment,
 * because the game's structs are 4-aligned and a host FILE * wants 8.  So
 * the record stores an *index* into a table of open host files, never a
 * pointer (docs/building.md, portability rule 5), and the same struct works at
 * either pointer width.
 *
 * What one record can stand for:
 *
 *   - a host file, whole (bw_fs_open, FS_OpenFile by path, FS_OpenFileFast
 *     by file id);
 *   - a window of one (FS_OpenFileDirect: the SDK opens a ROM range
 *     [top, bottom), which the FAT turns into a file id and an offset);
 *   - a host buffer (FS_CreateFileFromMemory);
 *   - an archive some mod changes member by member: the merged NARC
 *     sdk/vfs_narc.c builds, served from memory under every open path
 *     (by path, by file id, by ROM range), so the matched readers see one
 *     consistent archive.  The slot keeps the merge cache's handle, an
 *     index, and releases it on close.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "port.h"
#include "dsmem.h"
#include "fs.h"
#include "vfs.h"

#define BW_FS_MAGIC     0x42574653u  /* 'BWFS' */
#define BW_FS_NO_ID     0xFFFFFFFFu

typedef struct BwFsRecord {
    u32 magic;
    u32 slot;      /* 1-based index into g_slots; 0 means closed           */
    u32 pos;       /* current offset inside the window                     */
    u32 size;      /* window length in bytes                               */
    u32 base;      /* window start inside the host file or buffer          */
    u32 file_id;   /* NitroFS file id, or BW_FS_NO_ID                      */
    u32 reserved[(BW_FSFILE_SIZE / 4) - 6];
} BwFsRecord;

/* The whole point of the index-not-pointer choice; check it, do not hope. */
typedef char bw_fs_record_fits[(sizeof(BwFsRecord) == BW_FSFILE_SIZE) ? 1 : -1];

typedef struct BwFsSlot {
    FILE     *fp;    /* a host file, or NULL for a memory file  */
    const u8 *mem;   /* a host buffer, or NULL for a host file  */
    u32       vfs;   /* bw_vfs_acquire handle, 0 when not merged */
    int       used;
} BwFsSlot;

/* The open files.  The DS has no limit (an FSFile is the caller's memory),
 * and one screen can hold many archives open at once (the Pokedex entry
 * keeps 35 message banks and more graphics archives open), so the table
 * grows as files open; a slot is reused once its file closes. */
static BwFsSlot *g_slots;
static u32 g_nslots;

/* ------------------------------------------------------------------ */
/* Module state                                                         */
/* ------------------------------------------------------------------ */

static int g_fs_init;
static u32 g_default_dma = 0xFFFFFFFFu;   /* FS_DMA_NOT_USE until FS_Init */

u32 bw_fs_default_dma(void)
{
    return g_default_dma;
}

int bw_fs_initialised(void)
{
    return g_fs_init;
}

/*
 * The ROM archive object.  src/matched/arm9_sdk/dwc_startutilityex.c (the
 * matched FS_TryLoadTable, filed under the name libmatch gave it) passes
 * its address to FS_LoadArchiveTables; the SDK's fs_proc_rom.o names it
 * fsi_arc_rom.  The shim never reads it, so it is plain storage of the
 * SDK's size (FSArchive, 0x5C bytes; the next ARM9 object is sDispMode at
 * 0x02150E0C), defined here because the FS module is its owner.
 */
u32 d_02150DA8[0x5C / 4];

/* ------------------------------------------------------------------ */
/* The FAT and the FNT                                                  */
/* ------------------------------------------------------------------ */

static u8  *g_fat;
static u32  g_fat_size;
static u8  *g_fnt;
static u32  g_fnt_size;
static int  g_tables_tried;

static u8 *read_whole(const char *path, u32 *size_out)
{
    FILE *fp = fopen(path, "rb");
    long end;
    u8 *buf;

    *size_out = 0;
    if (fp == NULL) {
        return NULL;
    }
    if (fseek(fp, 0, SEEK_END) != 0 || (end = ftell(fp)) <= 0) {
        fclose(fp);
        return NULL;
    }
    rewind(fp);
    buf = (u8 *)malloc((size_t)end);
    if (buf == NULL) {
        fclose(fp);
        return NULL;
    }
    if (fread(buf, 1, (size_t)end, fp) != (size_t)end) {
        free(buf);
        fclose(fp);
        return NULL;
    }
    fclose(fp);
    *size_out = (u32)end;
    return buf;
}

/* extract/data/.. is extract/: the same rule sdk/narc.c uses for arm9_dec.bin. */
static int sibling_of_data_root(char *out, size_t cap, const char *name)
{
    const char *root = bw_data_root();
    size_t n = strlen(root);

    if (n + strlen(name) + 2 > cap) {
        return -1;
    }
    memcpy(out, root, n + 1);
    while (n > 0 && out[n - 1] != '/' && out[n - 1] != '\\') {
        n--;
    }
    strcpy(out + n, name);
    return 0;
}

static void free_tables(void)
{
    free(g_fat);
    free(g_fnt);
    g_fat = NULL;
    g_fnt = NULL;
    g_fat_size = 0;
    g_fnt_size = 0;
}

static void load_tables(void)
{
    char path[1100];

    g_tables_tried = 1;
    if (sibling_of_data_root(path, sizeof(path), "fat.bin") == 0) {
        g_fat = read_whole(path, &g_fat_size);
    }
    if (sibling_of_data_root(path, sizeof(path), "fnt.bin") == 0) {
        g_fnt = read_whole(path, &g_fnt_size);
    }
    /* A FAT without an FNT (or the reverse) is no file system at all. */
    if (g_fat == NULL || g_fnt == NULL || g_fnt_size < 8) {
        free_tables();
    }
}

static int tables(void)
{
    if (!g_tables_tried) {
        load_tables();
    }
    return g_fat != NULL && g_fnt != NULL;
}

int bw_fs_set_tables(const void *fat, u32 fat_size, const void *fnt, u32 fnt_size)
{
    free_tables();
    if (fat == NULL && fnt == NULL) {
        g_tables_tried = 0;
        return 0;
    }
    g_tables_tried = 1;
    if (fat == NULL || fnt == NULL || fat_size == 0 || fnt_size < 8) {
        return -1;
    }
    g_fat = (u8 *)malloc(fat_size);
    g_fnt = (u8 *)malloc(fnt_size);
    if (g_fat == NULL || g_fnt == NULL) {
        free_tables();
        return -1;
    }
    memcpy(g_fat, fat, fat_size);
    memcpy(g_fnt, fnt, fnt_size);
    g_fat_size = fat_size;
    g_fnt_size = fnt_size;
    return 0;
}

int bw_fs_tables_available(void)
{
    return tables();
}

u32 bw_fs_file_count(void)
{
    return tables() ? g_fat_size / 8u : 0u;
}

static int fat_entry(u32 id, u32 *top, u32 *bottom)
{
    if (!tables() || id >= g_fat_size / 8u) {
        return -1;
    }
    *top = bw_rd32(g_fat + id * 8u);
    *bottom = bw_rd32(g_fat + id * 8u + 4u);
    return *bottom >= *top ? 0 : -1;
}

int bw_fs_file_extent(u32 file_id, u32 *top, u32 *bottom)
{
    return fat_entry(file_id, top, bottom);
}

/*
 * FNT layout.  The main table is one 8-byte entry per directory, directory
 * id 0xF000 + index: { u32 sub-table offset; u16 first file id; u16 parent
 * directory id (the root's holds the directory count) }.  A sub-table is a
 * run of entries, each a length byte then the name: 0x01..0x7F a file of
 * that name length, 0x81..0xFF a directory of length & 0x7F followed by its
 * u16 id, 0x00 the end.  Files in one sub-table take consecutive ids from
 * the directory's first file id.
 */
static u32 fnt_dir_count(void)
{
    u32 n = bw_rd16(g_fnt + 6);

    if (n == 0 || n > 0x1000u || n * 8u > g_fnt_size) {
        return 0;
    }
    return n;
}

typedef struct FntCursor {
    u32 off;       /* next entry                          */
    u32 file_id;   /* id the next file entry will carry   */
} FntCursor;

static int fnt_open_dir(u32 dir_index, FntCursor *c)
{
    if (dir_index >= fnt_dir_count()) {
        return -1;
    }
    c->off = bw_rd32(g_fnt + dir_index * 8u);
    c->file_id = bw_rd16(g_fnt + dir_index * 8u + 4u);
    return c->off < g_fnt_size ? 0 : -1;
}

/*
 * One sub-table entry.  Returns 1 with the entry, 0 at the end, -1 on a
 * malformed table.  *is_dir is set; *id is the file id or the directory id.
 */
static int fnt_next(FntCursor *c, const u8 **name, u32 *len, int *is_dir, u32 *id)
{
    u32 t;

    if (c->off >= g_fnt_size) {
        return -1;
    }
    t = g_fnt[c->off];
    if (t == 0) {
        return 0;
    }
    if (t == 0x80) {
        return -1;
    }
    *len = t & 0x7Fu;
    if (c->off + 1u + *len + ((t & 0x80u) ? 2u : 0u) > g_fnt_size) {
        return -1;
    }
    *name = g_fnt + c->off + 1u;
    if (t & 0x80u) {
        *is_dir = 1;
        *id = bw_rd16(g_fnt + c->off + 1u + *len);
        c->off += 1u + *len + 2u;
    } else {
        *is_dir = 0;
        *id = c->file_id++;
        c->off += 1u + *len;
    }
    return 1;
}

/* Parent directory index of directory `dir_index` (not the root). */
static u32 fnt_parent(u32 dir_index)
{
    return (u32)bw_rd16(g_fnt + dir_index * 8u + 6u) - 0xF000u;
}

/* Name of directory `dir_index` inside its parent, by scanning the parent. */
static int fnt_dir_name(u32 dir_index, const u8 **name, u32 *len)
{
    FntCursor c;
    int is_dir;
    u32 id;

    if (dir_index == 0 || dir_index >= fnt_dir_count()) {
        return -1;
    }
    if (fnt_open_dir(fnt_parent(dir_index), &c) != 0) {
        return -1;
    }
    while (fnt_next(&c, name, len, &is_dir, &id) == 1) {
        if (is_dir && id == 0xF000u + dir_index) {
            return 0;
        }
    }
    return -1;
}

/* Prepend `len` bytes of `s` (and a '/' after them unless `slash` is 0) to
 * the path being built backwards in out[*at..]. */
static int prepend(char *out, u32 *at, const u8 *s, u32 len, int slash)
{
    u32 need = len + (slash ? 1u : 0u);

    if (need > *at) {
        return -1;
    }
    if (slash) {
        out[--(*at)] = '/';
    }
    *at -= len;
    memcpy(out + *at, s, len);
    return 0;
}

int bw_fs_path_of_id(u32 file_id, char *out, size_t out_size)
{
    char buf[512];
    u32 at = (u32)sizeof(buf) - 1u;
    u32 ndirs;
    u32 d;

    if (!tables() || out_size == 0) {
        return -1;
    }
    buf[at] = '\0';
    ndirs = fnt_dir_count();
    for (d = 0; d < ndirs; d++) {
        FntCursor c;
        const u8 *name;
        u32 len;
        int is_dir;
        u32 id;
        int r;

        if (fnt_open_dir(d, &c) != 0) {
            return -1;
        }
        while ((r = fnt_next(&c, &name, &len, &is_dir, &id)) == 1) {
            u32 up = d;
            u32 guard = 0;

            if (is_dir || id != file_id) {
                continue;
            }
            if (prepend(buf, &at, name, len, 0) != 0) {
                return -1;
            }
            while (up != 0) {
                const u8 *dn;
                u32 dl;

                if (fnt_dir_name(up, &dn, &dl) != 0 ||
                    prepend(buf, &at, dn, dl, 1) != 0 || ++guard > ndirs) {
                    return -1;
                }
                up = fnt_parent(up);
            }
            if ((size_t)(sizeof(buf) - at) > out_size) {
                return -1;
            }
            memcpy(out, buf + at, sizeof(buf) - at);
            return 0;
        }
        if (r < 0) {
            return -1;
        }
    }
    return -1;
}

static int name_eq(const u8 *a, u32 alen, const char *b, u32 blen)
{
    u32 i;

    if (alen != blen) {
        return 0;
    }
    for (i = 0; i < alen; i++) {
        int x = a[i];
        int y = (u8)b[i];

        if (x >= 'A' && x <= 'Z') {
            x += 'a' - 'A';
        }
        if (y >= 'A' && y <= 'Z') {
            y += 'a' - 'A';
        }
        if (x != y) {
            return 0;
        }
    }
    return 1;
}

static int is_sep(char c)
{
    return c == '/' || c == '\\';
}

int bw_fs_id_of_path(const char *path, u32 *file_id)
{
    u32 dir = 0;            /* directory index, root = 0 */
    const char *p = path;

    if (path == NULL || !tables()) {
        return -1;
    }
    if (strncmp(p, "rom:", 4) == 0) {
        p += 4;
    }
    for (;;) {
        const char *seg;
        const char *q;
        u32 seglen;
        int last;
        FntCursor c;
        const u8 *name;
        u32 len;
        int is_dir;
        u32 id;
        int found = 0;

        while (is_sep(*p)) {
            p++;
        }
        if (*p == '\0') {
            return -1;           /* the path names a directory */
        }
        seg = p;
        while (*p != '\0' && !is_sep(*p)) {
            p++;
        }
        seglen = (u32)(p - seg);
        q = p;
        while (is_sep(*q)) {
            q++;
        }
        last = (*q == '\0');

        if (seglen == 1 && seg[0] == '.') {
            if (last) {
                return -1;
            }
            continue;
        }
        if (seglen == 2 && seg[0] == '.' && seg[1] == '.') {
            if (dir != 0) {
                dir = fnt_parent(dir);
            }
            if (last) {
                return -1;
            }
            continue;
        }
        if (fnt_open_dir(dir, &c) != 0) {
            return -1;
        }
        while (fnt_next(&c, &name, &len, &is_dir, &id) == 1) {
            if (!name_eq(name, len, seg, seglen)) {
                continue;
            }
            if (last && !is_dir) {
                *file_id = id;
                return 0;
            }
            if (!last && is_dir) {
                dir = id - 0xF000u;
                found = 1;
                break;
            }
        }
        if (!found) {
            return -1;
        }
    }
}

/* ------------------------------------------------------------------ */
/* The file object                                                     */
/* ------------------------------------------------------------------ */

static BwFsRecord *record_of(void *file)
{
    BwFsRecord *r = (BwFsRecord *)file;

    if (r == NULL || r->magic != BW_FS_MAGIC || r->slot == 0 ||
        r->slot > g_nslots || !g_slots[r->slot - 1].used) {
        bw_panic("FS", 0, "operation on a file that is not open");
    }
    return r;
}

static int claim_slot(BwFsRecord *r, FILE *fp, const u8 *mem, u32 base, u32 size,
                      u32 file_id)
{
    u32 i;

    for (i = 0;; i++) {
        if (i == g_nslots) {
            u32 n = g_nslots != 0 ? g_nslots * 2u : 32u;
            BwFsSlot *grown = (BwFsSlot *)realloc(g_slots, (size_t)n * sizeof(*grown));

            if (grown == NULL) {
                return -1;
            }
            memset(grown + g_nslots, 0, (size_t)(n - g_nslots) * sizeof(*grown));
            g_slots = grown;
            g_nslots = n;
        }
        if (!g_slots[i].used) {
            g_slots[i].fp = fp;
            g_slots[i].mem = mem;
            g_slots[i].vfs = 0;
            g_slots[i].used = 1;
            memset(r, 0, sizeof(*r));
            r->magic = BW_FS_MAGIC;
            r->slot = i + 1;
            r->pos = 0;
            r->size = size;
            r->base = base;
            r->file_id = file_id;
            return 0;
        }
    }
    return -1;
}

/* Open host file `host_path` as the window [base, base + size); a size of
 * BW_FS_NO_ID means "to the end of the file". */
static int open_window(void *file, const char *host_path, u32 base, u32 size,
                       u32 file_id)
{
    BwFsRecord *r = (BwFsRecord *)file;
    FILE *fp;
    long end;

    memset(r, 0, sizeof(*r));
    fp = fopen(host_path, "rb");
    if (fp == NULL) {
        return -1;
    }
    if (fseek(fp, 0, SEEK_END) != 0 || (end = ftell(fp)) < 0) {
        fclose(fp);
        return -1;
    }
    if (size == BW_FS_NO_ID) {
        if (base > (u32)end) {
            fclose(fp);
            return -1;
        }
        size = (u32)end - base;
    } else if (base > (u32)end || size > (u32)end - base) {
        fclose(fp);
        return -1;
    }
    if (claim_slot(r, fp, NULL, base, size, file_id) != 0) {
        fclose(fp);
        return -1;
    }
    return 0;
}

int bw_fs_open(void *file, const char *host_path)
{
    return open_window(file, host_path, 0, BW_FS_NO_ID, BW_FS_NO_ID);
}

/*
 * Open NitroFS path `rel` through the file layer as the window [base, base +
 * size) (BW_FS_NO_ID: to the end).  When a mod changes the archive member by
 * member the window is over the merged image; otherwise over the file
 * bw_data_path names (mod overlay first).
 */
static int open_rel_window(void *file, const char *rel, u32 base, u32 size,
                           u32 file_id)
{
    BwFsRecord *r = (BwFsRecord *)file;
    char host[1200];
    const u8 *data;
    u32 total;
    u32 handle;
    int merged;

    memset(r, 0, sizeof(*r));
    merged = bw_vfs_acquire(rel, &data, &total, &handle);
    if (merged < 0) {
        return -1;
    }
    if (merged > 0) {
        if (size == BW_FS_NO_ID) {
            size = base <= total ? total - base : 0;
        }
        if (base > total || size > total - base ||
            claim_slot(r, NULL, data, base, size, file_id) != 0) {
            bw_vfs_release(handle);
            memset(r, 0, sizeof(*r));
            return -1;
        }
        g_slots[r->slot - 1].vfs = handle;
        return 0;
    }
    if (bw_data_path(host, sizeof(host), rel) != 0) {
        return -1;
    }
    return open_window(file, host, base, size, file_id);
}

int bw_fs_open_rel(void *file, const char *rel)
{
    return open_rel_window(file, rel, 0, BW_FS_NO_ID, BW_FS_NO_ID);
}

static int open_id_window(void *file, u32 file_id, u32 base, u32 size)
{
    char rel[512];

    memset(file, 0, BW_FSFILE_SIZE);
    if (bw_fs_path_of_id(file_id, rel, sizeof(rel)) != 0) {
        return -1;
    }
    return open_rel_window(file, rel, base, size, file_id);
}

int bw_fs_open_id(void *file, u32 file_id)
{
    return open_id_window(file, file_id, 0, BW_FS_NO_ID);
}

int bw_fs_open_memory(void *file, const void *data, u32 size)
{
    BwFsRecord *r = (BwFsRecord *)file;

    memset(r, 0, sizeof(*r));
    if (data == NULL && size != 0) {
        return -1;
    }
    return claim_slot(r, NULL, (const u8 *)data, 0, size, BW_FS_NO_ID);
}

u32 bw_fs_size(void *file)
{
    return record_of(file)->size;
}

/* ------------------------------------------------------------------ */
/* The SDK surface: initialisation                                     */
/* ------------------------------------------------------------------ */

extern void bw_overlay_sdk_init(void);   /* sdk/overlay.c, FSi_InitOverlay */

/*
 * FS_Init(dma) at 0x0207AD04, called once by the boot (func_02005328:
 * FS_Init(1)).  The SDK initialises the archive list, the ROM archive over
 * the card and the overlay tables, and records the DMA channel.  The port
 * has no card: the archive is the extracted tree, so what remains is the
 * channel, the overlay registry's reset and the "initialised" state
 * FS_IsAvailable reports.  A second call is a no-op, as in the SDK.
 */
void FS_Init(u32 default_dma)
{
    if (g_fs_init) {
        return;
    }
    g_fs_init = 1;
    g_default_dma = default_dma;
    bw_overlay_sdk_init();
}

BOOL FS_IsAvailable(void)
{
    return g_fs_init ? TRUE : FALSE;
}

/* FS_SetDefaultDMA(dma) at 0x0207AC64: returns the previous channel. */
u32 FS_SetDefaultDMA(u32 dma)
{
    u32 old = g_default_dma;

    g_default_dma = dma;
    return old;
}

/*
 * FS_LoadArchiveTables(arc, mem, max_size) at 0x0207A5C8, reached at boot
 * through the matched FS_TryLoadTable (dwc_startutilityex.c), which
 * func_02005328 calls twice: once with (0, 0) to learn the size, once with
 * an arena block of that size.
 *
 * The answer is the SDK's own formula over the real tables,
 *     (fat_size + fnt_size + 32 + 31) & ~31,
 * so the arena block the boot takes is the size it is on the DS and every
 * later arena address keeps its DS distance from the start.  When the block
 * is big enough the FAT and then the FNT are copied into it at the first
 * 32-byte boundary, as the SDK does, so the memory holds the DS's bytes.
 * The shim reads its own copies afterwards; nothing in the matched code
 * reads the cache.  Without tables on disk the answer is 0 and nothing is
 * copied, which the boot tolerates (a zero-byte arena block).
 */
u32 FS_LoadArchiveTables(void *arc, void *mem, u32 max_size)
{
    u32 total;

    (void)arc;
    if (!tables()) {
        return 0;
    }
    total = (g_fat_size + g_fnt_size + 32u + 31u) & ~31u;
    if (mem != NULL && total <= max_size) {
        u8 *p = (u8 *)mem;
        u32 mis = (u32)((uintptr_t)mem & 31u);

        if (mis != 0) {
            p += 32u - mis;
        }
        memcpy(p, g_fat, g_fat_size);
        memcpy(p + g_fat_size, g_fnt, g_fnt_size);
    }
    return total;
}

/* ------------------------------------------------------------------ */
/* The SDK surface: opening                                            */
/* ------------------------------------------------------------------ */

/*
 * FS_InitFile at 0x020788AC: bind a fresh, closed file object.  The SDK
 * clears the object's list links and state; the port's object is its own
 * record, and a cleared one is a closed one, so a read before a successful
 * open panics in record_of() rather than reading a stale slot.
 */
void FS_InitFile(void *file)
{
    memset(file, 0, BW_FSFILE_SIZE);
}

/*
 * FS_OpenFileEx(file, path, mode) at 0x020789FC and FS_OpenFile(file, path)
 * at 0x02078C18, its read-only wrapper.  `path` is a host string: the
 * callers that reach these by name (func_02020DEC through the
 * sub_0200575C_Table of `void *`, ov107_func_021F0850) hand over a char
 * pointer.  The path is resolved through the FNT when there is one, so the
 * SDK's spellings ("rom:/a/0/0/2", ".." segments, any ASCII case) all name
 * the same file, and directly through the file layer otherwise.  Mode is
 * read only (FS_FILEMODE_R, 1); a write mode fails as it does on the ROM
 * archive.  Returns TRUE or FALSE; a failed open leaves the object closed.
 */
#define BW_FS_FILEMODE_R 0x1u
#define BW_FS_FILEMODE_W 0x2u

BOOL FS_OpenFileEx(void *file, const char *path, u32 mode)
{
    const char *p;
    u32 id;

    FS_InitFile(file);
    if (path == NULL || (mode & BW_FS_FILEMODE_W) != 0) {
        return FALSE;
    }
    if (bw_fs_id_of_path(path, &id) == 0) {
        return bw_fs_open_id(file, id) == 0 ? TRUE : FALSE;
    }
    p = path;
    if (strncmp(p, "rom:", 4) == 0) {
        p += 4;
    }
    while (*p == '/') {
        p++;
    }
    if (*p == '\0') {
        return FALSE;
    }
    return bw_fs_open_rel(file, p) == 0 ? TRUE : FALSE;
}

BOOL FS_OpenFile(void *file, const char *path)
{
    return FS_OpenFileEx(file, path, BW_FS_FILEMODE_R);
}

/*
 * FS_ConvertPathToFileID(id, path) at 0x020788D8 and FS_OpenFileFast(file,
 * id) at 0x020789A4.  The SDK's FSFileID is { FSArchive *arc; u32 file_id }.
 * The game's copy (src/matched/arm9/func_02073FB8.c) is { u32 a; u32 b },
 * passed by value, so the port fills it as two u32s: the ROM archive's DS
 * address (0x02150DA8, a tag the shim checks, never a host pointer) and the
 * NitroFS file id.
 */
typedef struct BwFsFileID {
    u32 arc;
    u32 file_id;
} BwFsFileID;

#define BW_FS_ROM_ARCHIVE 0x02150DA8u

BOOL FS_ConvertPathToFileID(BwFsFileID *out, const char *path)
{
    u32 id;

    if (out == NULL || bw_fs_id_of_path(path, &id) != 0) {
        return FALSE;
    }
    out->arc = BW_FS_ROM_ARCHIVE;
    out->file_id = id;
    return TRUE;
}

BOOL FS_OpenFileFast(void *file, BwFsFileID id)
{
    FS_InitFile(file);
    if (id.arc != BW_FS_ROM_ARCHIVE) {
        return FALSE;
    }
    return bw_fs_open_id(file, id.file_id) == 0 ? TRUE : FALSE;
}

/*
 * FS_OpenFileDirect(file, arc, top, bottom, id) at 0x02078964: open the ROM
 * range [top, bottom) as a file.  The FAT says which file holds it: `id`
 * when it is a real file id, otherwise the file whose extent contains the
 * range.  The host file is that id's extracted copy, and the window is the
 * range's offset inside it.
 *
 * A range that is the whole file opens the whole file the file layer
 * serves, however long: a replacement or merged archive is rarely the
 * cartridge copy's size, and the ROM range is only the game's name for "this
 * file".  A strict sub-range keeps its offsets, which are the cartridge
 * copy's; over a replaced or merged file they are only meaningful if the
 * mod kept that layout (no matched caller opens a sub-range of an archive).
 */
BOOL FS_OpenFileDirect(void *file, void *arc, u32 top, u32 bottom, u32 id)
{
    u32 ftop;
    u32 fbot;
    u32 n;
    u32 i;

    (void)arc;
    FS_InitFile(file);
    if (bottom < top || !tables()) {
        return FALSE;
    }
    n = g_fat_size / 8u;
    if (id < n) {
        if (fat_entry(id, &ftop, &fbot) != 0 || top < ftop || bottom > fbot) {
            return FALSE;
        }
        return open_id_window(file, id, top - ftop,
                              (top == ftop && bottom == fbot) ? BW_FS_NO_ID
                                                              : bottom - top) == 0
                   ? TRUE : FALSE;
    }
    for (i = 0; i < n; i++) {
        if (fat_entry(i, &ftop, &fbot) == 0 && fbot > ftop &&
            ftop <= top && bottom <= fbot &&
            open_id_window(file, i, top - ftop,
                           (top == ftop && bottom == fbot) ? BW_FS_NO_ID
                                                           : bottom - top) == 0) {
            return TRUE;
        }
    }
    return FALSE;
}

/*
 * FS_CreateFileFromMemory(file, src, size) at 0x0207A940.  The live caller
 * is src/matched/arm9/func_0204913C.c, which hands over a host buffer
 * (`void *`).  unit_02048DDC.c's call passes a u32 and is on the arm taken
 * only when the archive table's +8 flag is set, which nothing in the
 * retail image does (that file's header); the definition follows the live
 * caller.  The buffer must outlive the file, as on the DS.
 */
BOOL FS_CreateFileFromMemory(void *file, void *src, u32 size)
{
    return bw_fs_open_memory(file, src, size) == 0 ? TRUE : FALSE;
}

/*
 * The open-by-path call at 0x02078C18 as src/matched/arm9/unit_02048DDC.c
 * spells it.  layout/symbols_sdk.txt now names 0x02078C18 FS_OpenFile; the
 * matched file still calls it CRYPTOi__BN_lshift1, the name libmatch.py's
 * first signature scan gave it.  Its call site is
 *
 *     FS_InitFile(arc);
 *     CRYPTOi__BN_lshift1(arc, g_02146A68.unk00[id]);
 *
 * where the second argument is an entry of the game's archive name table,
 * the ARM9 address of a string "a/0/0/0" .. "a/2/3/4" (aletheia's
 * docs/ASSETS.md, "the table"), held as a u32 at either pointer width
 * (sdk/game_globals.c, bw_arc_install_table).  So this spelling takes a DS
 * address, finds the string through the ARM9 window, and opens it like
 * FS_OpenFile.  Returns 1 on success and 0 on failure; an address outside
 * the image, an empty or unterminated string, or a missing file is 0.
 */
#define BW_FS_PATH_MAX 256u

u32 CRYPTOi__BN_lshift1(void *file, u32 path_address)
{
    char rel[BW_FS_PATH_MAX];
    const char *s;
    u32 i;

    FS_InitFile(file);
    if (!bw_ds_identity() && (path_address < BW_DS_MAIN_BASE ||
                              path_address - BW_DS_MAIN_BASE >= BW_DS_MAIN_SIZE)) {
        /* Movable blocks (the Vita, BW_DS_IDENTITY=0): the data scan
         * (sdk/armdata.c) has made the table's DS addresses host ones
         * already, so the entry is the string itself */
        return FS_OpenFile(file, (const char *)(uintptr_t)path_address) ? 1u : 0u;
    }
    for (i = 0; i < BW_FS_PATH_MAX; i++) {
        s = (const char *)bw_arm9_rodata(path_address + i, 1);
        if (s == NULL) {
            return 0;                   /* not an address in the image */
        }
        rel[i] = *s;
        if (*s == '\0') {
            break;
        }
    }
    if (i == BW_FS_PATH_MAX || i == 0) {
        return 0;                       /* unterminated, or empty */
    }
    return FS_OpenFile(file, rel) ? 1u : 0u;
}

/* ------------------------------------------------------------------ */
/* The SDK surface: reading                                            */
/* ------------------------------------------------------------------ */

/*
 * FS_SeekFile(file, offset, origin).  The matched callers only ever pass
 * origin 0, the SDK's FS_SEEK_SET; the other two are implemented anyway so a
 * later caller does not silently get the wrong one.  A position outside
 * [0, length] fails and leaves the position where it was.
 */
#define BW_FS_SEEK_SET 0
#define BW_FS_SEEK_CUR 1
#define BW_FS_SEEK_END 2

BOOL FS_SeekFile(void *file, s32 offset, u32 origin)
{
    BwFsRecord *r = record_of(file);
    long base;

    switch (origin) {
    case BW_FS_SEEK_SET: base = 0;            break;
    case BW_FS_SEEK_CUR: base = (long)r->pos;  break;
    case BW_FS_SEEK_END: base = (long)r->size; break;
    default:
        return FALSE;
    }
    if (base + offset < 0 || (u32)(base + offset) > r->size) {
        return FALSE;
    }
    r->pos = (u32)(base + offset);
    return TRUE;
}

/* FS_ReadFile: bytes read (short at the end of the window), or -1. */
s32 FS_ReadFile(void *file, void *dst, s32 len)
{
    BwFsRecord *r = record_of(file);
    const BwFsSlot *s = &g_slots[r->slot - 1];
    size_t got;

    if (len < 0) {
        return -1;
    }
    if ((u32)len > r->size - r->pos) {
        len = (s32)(r->size - r->pos);
    }
    if (len == 0) {
        return 0;
    }
    if (s->fp == NULL) {
        memcpy(dst, s->mem + r->base + r->pos, (size_t)len);
        got = (size_t)len;
    } else {
        if (fseek(s->fp, (long)(r->base + r->pos), SEEK_SET) != 0) {
            return -1;
        }
        got = fread(dst, 1, (size_t)len, s->fp);
    }
    r->pos += (u32)got;
    return (s32)got;
}

/* FS_GetFileLength at 0x02078A94, and FS_GetLength (0x02078C28), its wrapper. */
u32 FS_GetFileLength(void *file)
{
    return record_of(file)->size;
}

u32 FS_GetLength(void *file)
{
    return FS_GetFileLength(file);
}

/* FS_GetFilePosition at 0x02078AEC. */
u32 FS_GetFilePosition(void *file)
{
    return record_of(file)->pos;
}

/* FS_CloseFile: TRUE, or FALSE for an object that is not open. */
BOOL FS_CloseFile(void *file)
{
    BwFsRecord *r = (BwFsRecord *)file;
    BwFsSlot *s;

    if (r == NULL || r->magic != BW_FS_MAGIC || r->slot == 0 ||
        r->slot > g_nslots) {
        return FALSE;
    }
    s = &g_slots[r->slot - 1];
    if (s->fp != NULL) {
        fclose(s->fp);
    }
    if (s->vfs != 0) {
        bw_vfs_release(s->vfs);
    }
    s->vfs = 0;
    s->fp = NULL;
    s->mem = NULL;
    s->used = 0;
    r->magic = 0;
    r->slot = 0;
    return TRUE;
}

/* ------------------------------------------------------------------ */
/* Address aliases the matched sources declare                          */
/* ------------------------------------------------------------------ */

/*
 * The matched C calls most of the above as func_<address> (the matcher's
 * spelling where the relocation had no name): src/matched/arm9/
 * func_02048D00.c, func_02049578.c, func_0204944C.c, func_020461F4.c,
 * func_02020DEC.c, func_02073FB8.c and the boot's dwc_startutilityex.c among
 * them.  Each forwarder returns what the SDK function returns, so a caller
 * that uses the result (func_020461F4 tests FS_OpenFile's) gets it and one
 * that declares void ignores it.
 */
void func_020788AC(void *file)
{
    FS_InitFile(file);
}

BOOL func_020788D8(BwFsFileID *out, const char *path)
{
    return FS_ConvertPathToFileID(out, path);
}

BOOL func_02078964(void *file, void *arc, u32 top, u32 bottom, u32 id)
{
    return FS_OpenFileDirect(file, arc, top, bottom, id);
}

BOOL func_020789A4(void *file, BwFsFileID id)
{
    return FS_OpenFileFast(file, id);
}

BOOL func_020789FC(void *file, const char *path, u32 mode)
{
    return FS_OpenFileEx(file, path, mode);
}

BOOL func_02078A80(void *file)
{
    return FS_CloseFile(file);
}

u32 func_02078A94(void *file)
{
    return FS_GetFileLength(file);
}

u32 func_02078AEC(void *file)
{
    return FS_GetFilePosition(file);
}

BOOL func_02078B44(void *file, s32 offset, u32 origin)
{
    return FS_SeekFile(file, offset, origin);
}

s32 func_02078B70(void *file, void *dst, s32 len)
{
    return FS_ReadFile(file, dst, len);
}

BOOL func_02078C18(void *file, const char *path)
{
    return FS_OpenFile(file, path);
}

u32 func_02078C28(void *file)
{
    return FS_GetLength(file);
}

u32 func_0207A5C8(void *arc, void *mem, u32 max_size)
{
    return FS_LoadArchiveTables(arc, mem, max_size);
}

void func_0207AD04(u32 default_dma)
{
    FS_Init(default_dma);
}
