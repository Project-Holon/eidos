/*
 * sdk/mod_vfs.c -- members in memory and text entries, mod API version 6
 * (docs/mod-services.md, "Members in memory" and "Text entries").
 *
 * Members in memory
 * -----------------
 *
 * A mod changes a member of a NARC without a file: the bytes go to the
 * mod's memory layer (sdk/vfs.h, bw_vfs_mem_set), which sits where the
 * mod's folder sits in the load order.  The merged archive of that path is
 * dropped, so the next open sees the change.
 *
 * An archive the game already holds open reads through its handle, which
 * keeps the member count, the FAT offset and the image base from the open
 * and reads a member's FAT entry again on every load
 * (ARC_TOOL_ReadHandleMember).  For such an archive the port queues a swap:
 * between frames, on the game's thread, every file that has the archive
 * open gets the layout-stable image (sdk/fs.h, bw_fs_archive_swap), so no
 * read is half on the old image and half on the new one.  A member past
 * the end of an open archive cannot be added (the handle keeps the count).
 *
 * The limits: 16 MiB for one mod's layer, 32 MiB for every mod's together.
 *
 * Text entries
 * ------------
 *
 * A text bank is a member of a/0/0/2 (system text) or a/0/0/3 (story
 * text).  The text calls decode the bank as the game would read it now
 * (the layers included), and a set writes the whole bank back to the
 * mod's memory layer with the one entry changed.  A bank has one block of
 * lines, or two on a Japanese cartridge (kana and kanji); every block has
 * the same entry count, so an entry past the end grows every block.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "port.h"
#include "fs.h"
#include "vfs.h"
#include "vfs_msg.h"
#include "langmap.h"
#include "plat_save.h"
#include "mod_loader.h"
#include "mod_v6.h"

extern const char *bw_mod_savedata_namespace(void) BW_PORT_WEAK;

/* ------------------------------------------------------------------ */
/* The calling mod                                                     */
/* ------------------------------------------------------------------ */

static const char *calling_ns(void)
{
    return bw_mod_savedata_namespace != NULL ? bw_mod_savedata_namespace() : "";
}

static const char *calling_root(void)
{
    return bw_mod_loader_root_of(calling_ns());
}

static void say(const char *what, const char *archive, u32 index)
{
    const char *ns = calling_ns();

    fprintf(stderr, "eidos: mod %s: %s (%s member %u)\n",
            ns != NULL && ns[0] != '\0' ? ns : "?", what,
            archive != NULL ? archive : "?", index);
}

/* An archive name as a NitroFS path: no leading slash or "./". */
static int norm_archive(const char *archive, char *out, size_t size)
{
    size_t n;

    if (archive == NULL) {
        return -1;
    }
    while (archive[0] == '/' || (archive[0] == '.' && archive[1] == '/')) {
        archive += archive[0] == '/' ? 1 : 2;
    }
    n = strlen(archive);
    if (n == 0 || n >= size || strstr(archive, "..") != NULL ||
        strchr(archive, '\\') != NULL || strchr(archive, ':') != NULL) {
        return -1;
    }
    memcpy(out, archive, n + 1);
    return 0;
}

/* ------------------------------------------------------------------ */
/* The swaps queued for open archives                                  */
/* ------------------------------------------------------------------ */

#define PENDING_MAX 64

/* The paths are allocated, not static: the 32-bit program image has to end
 * well below the DS window (docs: the eidos32 image limit). */
static char *g_pending[PENDING_MAX];
static u32   g_pending_count;

static void queue_swap(const char *rel)
{
    u32 i;
    char *copy;

    for (i = 0; i < g_pending_count; i++) {
        if (strcmp(g_pending[i], rel) == 0) {
            return;
        }
    }
    if (g_pending_count == PENDING_MAX) {
        bw_mod_vfs_frame();     /* full: do the ones queued now */
    }
    copy = (char *)malloc(strlen(rel) + 1);
    if (copy == NULL) {
        return;
    }
    strcpy(copy, rel);
    g_pending[g_pending_count++] = copy;
}

void bw_mod_vfs_frame(void)
{
    u32 i;
    u32 n = g_pending_count;

    g_pending_count = 0;
    for (i = 0; i < n; i++) {
        if (bw_fs_archive_swap(g_pending[i]) < 0) {
            fprintf(stderr, "eidos: %s: the open archive kept its old members\n",
                    g_pending[i]);
        }
        free(g_pending[i]);
        g_pending[i] = NULL;
    }
}

u32 bw_mod_vfs_pending(void)
{
    return g_pending_count;
}

/* ------------------------------------------------------------------ */
/* Members                                                             */
/* ------------------------------------------------------------------ */

int bw_mod_vfs_member_set_for(const char *root, const char *archive, u32 index,
                              const void *data, u32 size)
{
    char rel[256];
    u32 open_count = 0;
    int old;
    u64 mine, all;

    if (root == NULL || norm_archive(archive, rel, sizeof(rel)) != 0 ||
        (data == NULL && size != 0)) {
        say("vfs_member_set: no such archive or no data", archive, index);
        return -1;
    }
    if (index >= BW_NARC_MAX_MEMBERS) {
        say("vfs_member_set: past the NARC member limit", archive, index);
        return -1;
    }
    if (bw_vfs_file_layer(rel) == BW_LAYER_NONE) {
        say("vfs_member_set: no such archive", archive, index);
        return -1;
    }
    if (bw_fs_archive_open(rel, &open_count) > 0 && index >= open_count) {
        say("vfs_member_set: the archive is open and the member is past its "
            "end; add it while the archive is closed", archive, index);
        return -1;
    }
    old = bw_vfs_mem_size(root, rel, index);
    mine = (u64)bw_vfs_mem_bytes(root) - (old > 0 ? (u32)old : 0u) + size;
    all = (u64)bw_vfs_mem_bytes(NULL) - (old > 0 ? (u32)old : 0u) + size;
    if (mine > BW_MOD_VFS_MOD_MAX || all > BW_MOD_VFS_TOTAL_MAX) {
        say(mine > BW_MOD_VFS_MOD_MAX
                ? "vfs_member_set: the mod's members in memory would pass 16 MiB"
                : "vfs_member_set: all mods' members in memory would pass 32 MiB",
            archive, index);
        return -1;
    }
    if (bw_vfs_mem_set(root, rel, index, data, size) != 0) {
        say("vfs_member_set: no memory", archive, index);
        return -1;
    }
    {
        /* Tests: EIDOS_VFS_MEM_DUMP=DIR writes each member a mod sets to
         * DIR/<archive>/<index> (a mod folder's member layout), so the same
         * bytes can be tried as files without the mod's code. */
        const char *dump = getenv("EIDOS_VFS_MEM_DUMP");

        if (dump != NULL && dump[0] != '\0') {
            char path[600];
            FILE *fp;

            snprintf(path, sizeof(path), "%s/%s", dump, rel);
            (void)bw_plat_mkdirs(path);
            snprintf(path, sizeof(path), "%s/%s/%u", dump, rel, (unsigned)index);
            fp = fopen(path, "wb");
            if (fp != NULL) {
                if (size != 0) {
                    (void)fwrite(data, 1, size, fp);
                }
                fclose(fp);
            }
        }
    }
    bw_vfs_flush_path(rel);
    queue_swap(rel);
    return 0;
}

int bw_mod_vfs_member_clear_for(const char *root, const char *archive, u32 index)
{
    char rel[256];
    u32 i;
    const char *r;
    u32 idx;

    if (root == NULL) {
        return -1;
    }
    if (archive == NULL || archive[0] == '\0') {
        /* every archive the mod holds members of */
        for (i = 0; bw_vfs_mem_list(i, &r, &idx); i++) {
            (void)idx;
            queue_swap(r);
            bw_vfs_flush_path(r);
        }
        return (int)bw_vfs_mem_clear(root, NULL, BW_VFS_MEM_ALL);
    }
    if (norm_archive(archive, rel, sizeof(rel)) != 0) {
        return -1;
    }
    i = bw_vfs_mem_clear(root, rel, index);
    if (i > 0) {
        bw_vfs_flush_path(rel);
        queue_swap(rel);
    }
    return (int)i;
}

int bw_mod_vfs_member_set(const char *archive, u32 index, const void *data, u32 size)
{
    const char *root = calling_root();

    if (root == NULL) {
        say("vfs_member_set: only a loaded mod has a memory layer", archive, index);
        return -1;
    }
    return bw_mod_vfs_member_set_for(root, archive, index, data, size);
}

int bw_mod_vfs_member_clear(const char *archive, u32 index)
{
    const char *root = calling_root();

    return root == NULL ? -1 : bw_mod_vfs_member_clear_for(root, archive, index);
}

/* The archive as the next read sees it: 1 with a malloc'd image. */
static int archive_image(const char *rel, u8 **img, u32 *size)
{
    char host[1200];
    FILE *fp;
    long end;
    int rc = bw_vfs_build_merged(rel, img, size);

    if (rc != 0) {
        return rc;
    }
    if (bw_data_path(host, sizeof(host), rel) != 0 || (fp = fopen(host, "rb")) == NULL) {
        return -1;
    }
    if (fseek(fp, 0, SEEK_END) != 0 || (end = ftell(fp)) <= 0 || fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        return -1;
    }
    *img = (u8 *)malloc((size_t)end);
    if (*img == NULL || fread(*img, 1, (size_t)end, fp) != (size_t)end) {
        free(*img);
        *img = NULL;
        fclose(fp);
        return -1;
    }
    fclose(fp);
    *size = (u32)end;
    return 1;
}

/* One member as the next read sees it: its size, or -1.  The bytes are
 * copied up to `size`. */
static int member_bytes(const char *rel, u32 index, void *out, u32 size)
{
    u8 *img = NULL;
    u32 img_size = 0;
    const u8 *m;
    u32 len;
    int rc = -1;

    if (archive_image(rel, &img, &img_size) <= 0) {
        return -1;
    }
    if (bw_narc_member(img, img_size, index, &m, &len) == 0) {
        if (out != NULL && size != 0) {
            memcpy(out, m, len < size ? len : size);
        }
        rc = len > 0x7FFFFFFFu ? -1 : (int)len;
    }
    free(img);
    return rc;
}

int bw_mod_vfs_member_get(const char *archive, u32 index, void *out, u32 size)
{
    char rel[256];

    if (norm_archive(archive, rel, sizeof(rel)) != 0 || (out == NULL && size != 0)) {
        return -1;
    }
    return member_bytes(rel, index, out, size);
}

/* ------------------------------------------------------------------ */
/* Text banks                                                          */
/* ------------------------------------------------------------------ */

#define MSG_END 0xFFFFu

typedef struct Bank {
    u32 blocks;
    u32 count;
    u32 off[8];     /* each block's section offset */
} Bank;

static int bank_parse(const u8 *bank, u32 size, Bank *b)
{
    u32 i, k;

    if (bank == NULL || size < 12) {
        return -1;
    }
    b->blocks = bw_rd16(bank);
    b->count = bw_rd16(bank + 2);
    if (b->blocks == 0 || b->blocks > 8 || 12u + 4u * b->blocks > size) {
        return -1;
    }
    for (k = 0; k < b->blocks; k++) {
        u32 s = bw_rd32(bank + 12 + 4 * k);

        if (s < 12u + 4u * b->blocks || (u64)s + 4 + 8ull * b->count > size) {
            return -1;
        }
        for (i = 0; i < b->count; i++) {
            const u8 *e = bank + s + 4 + 8 * i;

            if ((u64)s + bw_rd32(e) + 2ull * bw_rd16(e + 4) > size) {
                return -1;
            }
        }
        b->off[k] = s;
    }
    return 0;
}

int bw_msg_bank_get(const u8 *bank, u32 size, u32 entry, u32 block, u16 *out, u32 max_units)
{
    Bank b;
    const u8 *e;
    u32 units, i, n;
    u16 *tmp;

    if (bank_parse(bank, size, &b) != 0 || block >= b.blocks || entry >= b.count) {
        return -1;
    }
    e = bank + b.off[block] + 4 + 8 * entry;
    units = bw_rd16(e + 4);
    tmp = (u16 *)malloc((units == 0 ? 1 : units) * sizeof(u16));
    if (tmp == NULL) {
        return -1;
    }
    for (i = 0; i < units; i++) {
        tmp[i] = bw_rd16(bank + b.off[block] + bw_rd32(e) + 2 * i);
    }
    bw_msg_cipher(tmp, units, entry);
    n = 0;
    while (n < units && tmp[n] != MSG_END) {
        n++;
    }
    if (out != NULL) {
        for (i = 0; i < n && i < max_units; i++) {
            out[i] = tmp[i];
        }
    }
    free(tmp);
    return (int)n;
}

int bw_msg_bank_set(const u8 *bank, u32 size, u32 entry, u32 block,
                    const u16 *units, u32 count, u8 **out, u32 *out_size)
{
    Bank b;
    u32 new_count, k, i;
    u64 total, sect_max = 0;
    u8 *img;
    u32 at;

    *out = NULL;
    *out_size = 0;
    if (bank_parse(bank, size, &b) != 0 || block >= b.blocks ||
        entry > BW_MSG_MAX_INDEX || count >= 0xFFFFu || (units == NULL && count != 0)) {
        return -1;
    }
    new_count = entry + 1 > b.count ? entry + 1 : b.count;
    /* the size: the header, then each block's section padded to 4 */
    total = 12u + 4u * b.blocks;
    total = (total + 3u) & ~3ull;
    for (k = 0; k < b.blocks; k++) {
        u64 s = 4u + 8ull * new_count;

        for (i = 0; i < new_count; i++) {
            if (k == block && i == entry) {
                s += 2ull * (count + 1);
            } else if (i < b.count) {
                s += 2ull * bw_rd16(bank + b.off[k] + 4 + 8 * i + 4);
            } else {
                s += 2;
            }
        }
        s = (s + 3u) & ~3ull;
        if (s > sect_max) {
            sect_max = s;
        }
        total += s;
    }
    if (total > 0x7FFFFFF0ull) {
        return -1;
    }
    img = (u8 *)calloc((size_t)total, 1);
    if (img == NULL) {
        return -1;
    }
    img[0] = (u8)b.blocks;
    img[1] = (u8)(b.blocks >> 8);
    img[2] = (u8)new_count;
    img[3] = (u8)(new_count >> 8);
    bw_wr32(img + 4, (u32)sect_max);
    bw_wr32(img + 8, size >= 12 ? bw_rd32(bank + 8) : 0);
    at = (u32)((12u + 4u * b.blocks + 3u) & ~3u);
    for (k = 0; k < b.blocks; k++) {
        u8 *sect = img + at;
        u32 off = 4 + 8 * new_count;
        u16 flags = b.count > 0 ? bw_rd16(bank + b.off[k] + 4 + 6) : 0;

        bw_wr32(img + 12 + 4 * k, at);
        for (i = 0; i < new_count; i++) {
            u8 *ent = sect + 4 + 8 * i;
            u32 n, j;
            u16 eflags = flags;

            if (i < b.count) {
                eflags = bw_rd16(bank + b.off[k] + 4 + 8 * i + 6);
            }
            if (k == block && i == entry) {
                u16 *tmp = (u16 *)malloc((count + 1) * sizeof(u16));

                if (tmp == NULL) {
                    free(img);
                    return -1;
                }
                if (count > 0) {
                    memcpy(tmp, units, count * sizeof(u16));
                }
                tmp[count] = MSG_END;
                bw_msg_cipher(tmp, count + 1, i);
                for (j = 0; j <= count; j++) {
                    bw_wr16(sect + off + 2 * j, tmp[j]);
                }
                free(tmp);
                n = count + 1;
            } else if (i < b.count) {
                const u8 *e = bank + b.off[k] + 4 + 8 * i;

                n = bw_rd16(e + 4);
                memcpy(sect + off, bank + b.off[k] + bw_rd32(e), 2u * n);
            } else {
                u16 t = MSG_END;

                bw_msg_cipher(&t, 1, i);
                bw_wr16(sect + off, t);
                n = 1;
            }
            bw_wr32(ent, off);
            bw_wr16(ent + 4, n);
            bw_wr16(ent + 6, eflags);
            off += 2 * n;
        }
        off = (off + 3u) & ~3u;
        bw_wr32(sect, off);
        at += off;
    }
    *out = img;
    *out_size = at;
    return 0;
}

static const char *text_archive(u32 archive)
{
    return archive == 2 ? "a/0/0/2" : archive == 3 ? "a/0/0/3" : NULL;
}

int bw_mod_text_get(u32 archive, u32 file, u32 entry, u32 block, u16 *out, u32 max_units)
{
    const char *rel = text_archive(archive);
    u8 *img = NULL, *bank;
    u32 img_size = 0;
    const u8 *m;
    u32 len;
    int rc = -1;

    if (rel == NULL || (out == NULL && max_units != 0) ||
        archive_image(rel, &img, &img_size) <= 0) {
        return -1;
    }
    if (bw_narc_member(img, img_size, file, &m, &len) == 0) {
        bank = (u8 *)m;
        rc = bw_msg_bank_get(bank, len, entry, block, out, max_units);
    }
    free(img);
    return rc;
}

int bw_mod_text_set_for(const char *root, u32 archive, u32 file, u32 entry, u32 block,
                        const u16 *units, u32 count)
{
    const char *rel = text_archive(archive);
    u8 *img = NULL, *bank = NULL;
    u32 img_size = 0, bank_size = 0;
    const u8 *m;
    u32 len;
    int rc = -1;

    if (rel == NULL || root == NULL || archive_image(rel, &img, &img_size) <= 0) {
        return -1;
    }
    if (bw_narc_member(img, img_size, file, &m, &len) != 0) {
        /* A new bank past the archive's end: no lines yet, as many blocks
         * as the archive's first bank has. */
        static u8 empty[32];
        u32 blocks = 1, k;

        if (bw_narc_member(img, img_size, 0, &m, &len) == 0 && len >= 2 &&
            bw_rd16(m) >= 1 && bw_rd16(m) <= 4) {
            blocks = bw_rd16(m);
        }
        memset(empty, 0, sizeof(empty));
        bw_wr16(empty, (u16)blocks);
        for (k = 0; k < blocks; k++) {
            bw_wr32(empty + 12 + 4 * k, 12 + 4 * blocks);
        }
        bw_wr32(empty + 12 + 4 * blocks, 4);   /* every block: one empty section */
        m = empty;
        len = 12 + 4 * blocks + 4;
    }
    if (bw_msg_bank_set(m, len, entry, block, units, count, &bank, &bank_size) == 0) {
        rc = bw_mod_vfs_member_set_for(root, rel, file, bank, bank_size);
    }
    free(bank);
    free(img);
    return rc;
}

int bw_mod_text_set(u32 archive, u32 file, u32 entry, u32 block, const u16 *units, u32 count)
{
    const char *root = calling_root();

    if (root == NULL) {
        return -1;
    }
    return bw_mod_text_set_for(root, archive, file, entry, block, units, count);
}

u32 bw_mod_text_language(void)
{
    return (u32)bw_lang_language();
}
