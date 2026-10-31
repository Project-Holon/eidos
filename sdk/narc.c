/*
 * sdk/narc.c -- the port's NARC reader and the ARM9 rodata window.
 *
 * The data root and the mods that a NitroFS path resolves through live in
 * sdk/vfs.c, and member-level overrides in sdk/vfs_narc.c; this file opens
 * through bw_fs_open_rel like every other loader, so what it frames is the
 * merged archive when a mod changes members.
 *
 * A NARC is the container every `a/N/M/K` NitroFS file in this game is:
 *
 *     0x00  'NARC'  u16 bom  u16 version  u32 fileSize
 *     0x0C  u16 headerSize (0x10)  u16 chunkCount
 *     0x10  chunks, each  { u32 magic; u32 size; ... }, size-chained:
 *             'BTAF'  u32 size  u16 count  u16 reserved  then count
 *                     8-byte { u32 start; u32 end; } entries at +0x0C
 *             'BTNF'  u32 size  (filename table, unused here)
 *             'GMIF'  u32 size  then the concatenated sub-file image at +0x08
 *
 * The three numbers the matched readers want are exactly the chunk offsets:
 *
 *     fatOffset = offset of the BTAF chunk
 *     imageBase = offset of the GMIF chunk
 *     count     = BTAF's u16 at +0x08
 *
 * because src/matched/func_02049578.c computes a member's absolute position
 * as `imageBase + 8 + start` where `start` is the u32 it reads at
 * `fatOffset + 0xC + member * 8`.  This reader does not reimplement those
 * two lines: it fills in the three fields and then the matched code does the
 * arithmetic, which is the point of the whole exercise.
 *
 * Framing only.  No sub-file format is interpreted here, and no game bytes
 * are in this file.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "port.h"
#include "langmap.h"

/* The SDK file calls this file uses (sdk/fs.c), declared at file scope:
 * MSVC flags a block-scope extern function declaration (C4210). */
extern s32 FS_SeekFile(void *file, s32 offset, u32 origin);
extern s32 FS_ReadFile(void *file, void *dst, s32 len);
extern s32 FS_CloseFile(void *file);

#define FOURCC(a, b, c, d) \
    ((u32)(a) | ((u32)(b) << 8) | ((u32)(c) << 16) | ((u32)(d) << 24))

#define NARC_MAGIC FOURCC('N', 'A', 'R', 'C')
#define BTAF_MAGIC FOURCC('B', 'T', 'A', 'F')
#define GMIF_MAGIC FOURCC('G', 'M', 'I', 'F')

/* ------------------------------------------------------------------ */
/* ARM9 rodata window                                                  */
/* ------------------------------------------------------------------ */

static u8  *g_arm9;
static u32  g_arm9_size;
static int  g_arm9_tried;

static void load_arm9(void)
{
    char path[1100];
    const char *root = bw_data_root();
    size_t n = strlen(root);
    FILE *fp;
    long end;

    g_arm9_tried = 1;

    /*
     * arm9_dec.bin is the sibling of the data root: extract/data/.. is
     * extract/.  Strip one trailing path element rather than asking the
     * caller for a second root.
     */
    if (n + 32 > sizeof(path)) {
        return;
    }
    memcpy(path, root, n + 1);
    while (n > 0 && path[n - 1] != '/' && path[n - 1] != '\\') {
        n--;
    }
    if (n == 0) {
        strcpy(path, "arm9_dec.bin");
    } else {
        strcpy(path + n, "arm9_dec.bin");
    }

    fp = fopen(path, "rb");
    if (fp == NULL) {
        return;
    }
    if (fseek(fp, 0, SEEK_END) != 0 || (end = ftell(fp)) < 0) {
        fclose(fp);
        return;
    }
    rewind(fp);
    g_arm9 = (u8 *)malloc((size_t)end);
    if (g_arm9 == NULL) {
        fclose(fp);
        return;
    }
    if (fread(g_arm9, 1, (size_t)end, fp) != (size_t)end) {
        free(g_arm9);
        g_arm9 = NULL;
        fclose(fp);
        return;
    }
    fclose(fp);
    g_arm9_size = (u32)end;
    /* another cartridge than English Black: its image laid out as English
     * Black's (sdk/langmap.h) */
    bw_lang_init();
    if (bw_lang_active()) {
        u32 size = 0;
        u8 *eng = bw_lang_relayout_arm9(g_arm9, g_arm9_size, &size);

        if (eng != NULL) {
            free(g_arm9);
            g_arm9 = eng;
            g_arm9_size = size;
        }
    }
}

void bw_arm9_set_image(const void *image, u32 size)
{
    free(g_arm9);
    g_arm9 = NULL;
    g_arm9_size = 0;
    g_arm9_tried = 1;
    if (image == NULL || size == 0) {
        return;
    }
    g_arm9 = (u8 *)malloc(size);
    if (g_arm9 == NULL) {
        return;
    }
    memcpy(g_arm9, image, size);
    g_arm9_size = size;
}

const void *bw_arm9_rodata(u32 address, u32 size)
{
    u32 off;

    if (!g_arm9_tried) {
        load_arm9();
    }
    if (g_arm9 == NULL || address < BW_ARM9_LOAD_ADDRESS) {
        return NULL;
    }
    off = address - BW_ARM9_LOAD_ADDRESS;
    if (off > g_arm9_size || size > g_arm9_size - off) {
        return NULL;
    }
    return g_arm9 + off;
}

/* ------------------------------------------------------------------ */
/* NARC framing                                                        */
/* ------------------------------------------------------------------ */

static int read_at(void *file, u32 pos, void *dst, u32 size)
{
    if (!FS_SeekFile(file, (s32)pos, 0)) {
        return -1;
    }
    if (FS_ReadFile(file, dst, (s32)size) != (s32)size) {
        return -1;
    }
    return 0;
}

int bw_arc_open(BwArcFile *arc, const char *rel_path)
{
    u8 hdr[16];
    u32 total;
    u32 pos;
    int have_fat = 0;
    int have_img = 0;

    memset(arc, 0, sizeof(*arc));

    if (bw_fs_open_rel(arc->file, rel_path) != 0) {
        return -1;
    }
    total = bw_fs_size(arc->file);

    if (read_at(arc->file, 0, hdr, 16) != 0) {
        bw_arc_close(arc);
        return -1;
    }
    if (bw_rd32(hdr) != NARC_MAGIC) {
        bw_arc_close(arc);
        return -1;
    }

    pos = bw_rd16(hdr + 12);          /* headerSize, 0x10 in every file */
    while (pos + 8 <= total) {
        u8 chunk[12];
        u32 magic;
        u32 size;

        if (read_at(arc->file, pos, chunk, 12) != 0) {
            break;
        }
        magic = bw_rd32(chunk);
        size = bw_rd32(chunk + 4);
        if (size < 8 || size > total - pos) {
            break;
        }
        if (magic == BTAF_MAGIC) {
            arc->fatOffset = (u16)pos;
            arc->count = bw_rd16(chunk + 8);
            have_fat = 1;
        } else if (magic == GMIF_MAGIC) {
            arc->imageBase = pos;
            have_img = 1;
        }
        pos += size;
    }

    if (!have_fat || !have_img) {
        bw_arc_close(arc);
        return -1;
    }
    return 0;
}

void bw_arc_close(BwArcFile *arc)
{
    (void)FS_CloseFile(arc->file);
    arc->imageBase = 0;
    arc->fatOffset = 0;
    arc->count = 0;
}

static int member_extent(BwArcFile *arc, u32 member, u32 *start, u32 *end)
{
    u8 fat[8];

    if (member >= arc->count) {
        return -1;
    }
    if (read_at(arc->file, (u32)arc->fatOffset + 0x0C + member * 8, fat, 8) != 0) {
        return -1;
    }
    *start = bw_rd32(fat);
    *end = bw_rd32(fat + 4);
    if (*end < *start) {
        return -1;
    }
    return 0;
}

long bw_arc_member_size(BwArcFile *arc, u32 member)
{
    u32 start;
    u32 end;

    if (member_extent(arc, member, &start, &end) != 0) {
        return -1;
    }
    return (long)(end - start);
}

long bw_arc_read_member(BwArcFile *arc, u32 member, void *dst, u32 dst_size)
{
    u32 start;
    u32 end;

    if (member_extent(arc, member, &start, &end) != 0) {
        return -1;
    }
    if (end - start > dst_size) {
        return -1;
    }
    if (read_at(arc->file, arc->imageBase + 8 + start, dst, end - start) != 0) {
        return -1;
    }
    return (long)(end - start);
}
