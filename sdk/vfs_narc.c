/*
 * sdk/vfs_narc.c -- member-level NARC overrides: the merged archive.
 *
 * Why this exists.  Every asset the game loads is a member of a NARC, and a
 * mod that adds one Pokemon or edits one map should not have to repack and
 * ship a whole archive (and then conflict with every other mod touching the
 * same archive).  So a mod may hold a *directory* at an archive's NitroFS
 * path, and each file in it named `<index>` or `<index>.<ext>` replaces that
 * member, or adds it past the archive's end (sdk/vfs.h has the whole model).
 *
 * Why a synthesised archive rather than a lookup on the read path.  The
 * readers that matter are matched code: func_02048E34, func_02049578 and the
 * rest read the NARC's own header, BTAF and GMIF through FS_SeekFile and
 * FS_ReadFile and do the arithmetic themselves, and the BwArcFile fields
 * (imageBase, fatOffset, count) they trust come from the same bytes.  The
 * one place that can make all of them agree without touching any of them is
 * the bytes: this file builds the NARC the mods describe, laid out exactly
 * the way the cartridge's archives are, and the FS shim (sdk/fs.c) serves it
 * as a memory file whenever the archive's path is opened, by path, by file
 * id, by ROM range or through the archive id table.  Nothing above the FS
 * shim can tell a merged archive from a shipped one.
 *
 * Merged images are cached per path, with a reference count held by the
 * open files reading them (the FS record stores the cache *index*, never a
 * pointer, per the 0x48-byte FSFile rule), and dropped on any change to the
 * roots or the mod list.
 *
 * Framing only: no member's contents are interpreted, and no game bytes are
 * in this file.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "port.h"
#include "vfs.h"
#include "plat_dir.h"
#include "vfs_msg.h"
#include "vfs_sdat.h"

#define FOURCC(a, b, c, d) \
    ((u32)(a) | ((u32)(b) << 8) | ((u32)(c) << 16) | ((u32)(d) << 24))

#define NARC_MAGIC FOURCC('N', 'A', 'R', 'C')
#define BTAF_MAGIC FOURCC('B', 'T', 'A', 'F')
#define BTNF_MAGIC FOURCC('B', 'T', 'N', 'F')
#define GMIF_MAGIC FOURCC('G', 'M', 'I', 'F')

#define NARC_HEADER_SIZE 0x10u
#define NARC_ALIGN       4u
#define NARC_PAD         0xFFu

static void put16(u8 *p, u32 v)
{
    p[0] = (u8)v;
    p[1] = (u8)(v >> 8);
}

static void put32(u8 *p, u32 v)
{
    p[0] = (u8)v;
    p[1] = (u8)(v >> 8);
    p[2] = (u8)(v >> 16);
    p[3] = (u8)(v >> 24);
}

/* ------------------------------------------------------------------ */
/* Packing                                                             */
/* ------------------------------------------------------------------ */

/* The nameless BTNF every archive of this game but one carries. */
static const u8 MINIMAL_BTNF[16] = {
    'B', 'T', 'N', 'F', 0x10, 0, 0, 0,  /* magic, size                    */
    4, 0, 0, 0,                         /* root: sub-table offset         */
    0, 0, 1, 0                          /* first file id 0, one directory */
};

int bw_narc_pack(const u8 *const *data, const u32 *sizes, u32 count,
                 const u8 *btnf, u32 btnf_size, u8 **out, u32 *out_size)
{
    u64 gmif_data = 0;
    u64 total;
    u32 btaf_size;
    u32 i;
    u32 at;
    u32 pos;
    u8 *img;

    *out = NULL;
    *out_size = 0;
    if (count > BW_NARC_MAX_MEMBERS || (count != 0 && sizes == NULL)) {
        return -1;
    }
    if (btnf == NULL) {
        btnf = MINIMAL_BTNF;
        btnf_size = sizeof(MINIMAL_BTNF);
    }
    if (btnf_size < 8 || (btnf_size % NARC_ALIGN) != 0 ||
        bw_rd32(btnf) != BTNF_MAGIC || bw_rd32(btnf + 4) != btnf_size) {
        return -1;
    }
    for (i = 0; i < count; i++) {
        if (sizes[i] != 0 && (data == NULL || data[i] == NULL)) {
            return -1;
        }
        gmif_data += ((u64)sizes[i] + NARC_ALIGN - 1) & ~(u64)(NARC_ALIGN - 1);
    }
    btaf_size = 12u + 8u * count;
    total = (u64)NARC_HEADER_SIZE + btaf_size + btnf_size + 8u + gmif_data;
    if (total > 0xFFFFFFFFu) {
        return -1;
    }
    img = (u8 *)malloc((size_t)total);
    if (img == NULL) {
        return -1;
    }

    put32(img + 0x00, NARC_MAGIC);
    put16(img + 0x04, 0xFFFEu);           /* byte order mark          */
    put16(img + 0x06, 0x0100u);           /* version                  */
    put32(img + 0x08, (u32)total);
    put16(img + 0x0C, NARC_HEADER_SIZE);
    put16(img + 0x0E, 3);                 /* chunk count              */

    at = NARC_HEADER_SIZE;
    put32(img + at + 0, BTAF_MAGIC);
    put32(img + at + 4, btaf_size);
    put16(img + at + 8, count);
    put16(img + at + 10, 0);
    pos = 0;
    for (i = 0; i < count; i++) {
        put32(img + at + 12 + i * 8, pos);
        put32(img + at + 16 + i * 8, pos + sizes[i]);
        pos += (sizes[i] + NARC_ALIGN - 1) & ~(NARC_ALIGN - 1);
    }
    at += btaf_size;

    memcpy(img + at, btnf, btnf_size);
    at += btnf_size;

    put32(img + at + 0, GMIF_MAGIC);
    put32(img + at + 4, (u32)(8u + gmif_data));
    at += 8;
    for (i = 0; i < count; i++) {
        u32 padded = (sizes[i] + NARC_ALIGN - 1) & ~(NARC_ALIGN - 1);

        if (sizes[i] != 0) {
            memcpy(img + at, data[i], sizes[i]);
        }
        memset(img + at + sizes[i], NARC_PAD, padded - sizes[i]);
        at += padded;
    }

    *out = img;
    *out_size = (u32)total;
    return 0;
}

/* ------------------------------------------------------------------ */
/* Reading the base archive                                            */
/* ------------------------------------------------------------------ */

typedef struct Narc {
    const u8 *img;
    u32 size;
    u32 count;
    u32 fat;        /* offset of BTAF                     */
    u32 btnf;       /* offset of BTNF, or 0 when absent   */
    u32 btnf_size;
    u32 gmif;       /* offset of GMIF                     */
    u32 gmif_size;
} Narc;

static int parse_narc(const u8 *img, u32 size, Narc *n)
{
    u32 pos;
    int have_fat = 0;
    int have_img = 0;
    u32 i;

    memset(n, 0, sizeof(*n));
    if (size < NARC_HEADER_SIZE || bw_rd32(img) != NARC_MAGIC) {
        return -1;
    }
    n->img = img;
    n->size = size;
    pos = bw_rd16(img + 12);
    while (pos <= size && size - pos >= 8) {
        u32 magic = bw_rd32(img + pos);
        u32 csize = bw_rd32(img + pos + 4);

        if (csize < 8 || csize > size - pos) {
            break;
        }
        if (magic == BTAF_MAGIC && csize >= 12) {
            n->fat = pos;
            n->count = bw_rd16(img + pos + 8);
            if ((u64)12 + (u64)8 * n->count > csize) {
                return -1;
            }
            have_fat = 1;
        } else if (magic == BTNF_MAGIC) {
            n->btnf = pos;
            n->btnf_size = csize;
        } else if (magic == GMIF_MAGIC) {
            n->gmif = pos;
            n->gmif_size = csize;
            have_img = 1;
        }
        pos += csize;
    }
    if (!have_fat || !have_img) {
        return -1;
    }
    for (i = 0; i < n->count; i++) {
        u32 start = bw_rd32(img + n->fat + 12 + i * 8);
        u32 end = bw_rd32(img + n->fat + 16 + i * 8);

        if (end < start || end > n->gmif_size - 8) {
            return -1;
        }
    }
    return 0;
}

static u8 *read_whole(const char *path, u32 *size_out)
{
    FILE *fp = fopen(path, "rb");
    long end;
    u8 *buf;

    *size_out = 0;
    if (fp == NULL) {
        return NULL;
    }
    if (fseek(fp, 0, SEEK_END) != 0 || (end = ftell(fp)) < 0 ||
        (unsigned long)end > 0xFFFFFFFFul || fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        return NULL;
    }
    buf = (u8 *)malloc(end == 0 ? 1 : (size_t)end);
    if (buf == NULL) {
        fclose(fp);
        return NULL;
    }
    if (end != 0 && fread(buf, 1, (size_t)end, fp) != (size_t)end) {
        free(buf);
        fclose(fp);
        return NULL;
    }
    fclose(fp);
    *size_out = (u32)end;
    return buf;
}

u32 bw_narc_count(const u8 *img, u32 size)
{
    Narc n;

    return parse_narc(img, size, &n) == 0 ? n.count : 0xFFFFFFFFu;
}

int bw_narc_member(const u8 *img, u32 size, u32 index, const u8 **data, u32 *len)
{
    Narc n;
    u32 start, end;

    if (parse_narc(img, size, &n) != 0 || index >= n.count) {
        return -1;
    }
    start = bw_rd32(img + n.fat + 12 + index * 8);
    end = bw_rd32(img + n.fat + 16 + index * 8);
    *data = img + n.gmif + 8 + start;
    *len = end - start;
    return 0;
}

int bw_narc_restamp(const u8 *old, u32 old_size, const u8 *fresh, u32 fresh_size,
                    u8 **out, u32 *out_size)
{
    Narc o, f;
    u32 i, changed = 0;
    u64 grow = 0;
    u8 *img;
    u32 at, gmif_end;

    *out = NULL;
    *out_size = 0;
    if (parse_narc(old, old_size, &o) != 0 || parse_narc(fresh, fresh_size, &f) != 0) {
        return -1;
    }
    for (i = 0; i < o.count; i++) {
        const u8 *a = old + o.gmif + 8 + bw_rd32(old + o.fat + 12 + i * 8);
        u32 alen = bw_rd32(old + o.fat + 16 + i * 8) - bw_rd32(old + o.fat + 12 + i * 8);
        const u8 *b = NULL;
        u32 blen = 0;

        if (i < f.count) {
            b = fresh + f.gmif + 8 + bw_rd32(fresh + f.fat + 12 + i * 8);
            blen = bw_rd32(fresh + f.fat + 16 + i * 8) - bw_rd32(fresh + f.fat + 12 + i * 8);
        }
        if (alen == blen && (blen == 0 || memcmp(a, b, blen) == 0)) {
            continue;
        }
        changed++;
        grow += ((u64)blen + NARC_ALIGN - 1) & ~(u64)(NARC_ALIGN - 1);
    }
    if (changed == 0) {
        return 0;
    }
    /* The GMIF must be the last block, so its end is the image's end and
     * new member bytes go after it.  Every real archive is laid out so. */
    gmif_end = o.gmif + o.gmif_size;
    if (gmif_end != old_size || (u64)gmif_end + grow > 0xFFFFFFF0ull) {
        return -1;
    }
    img = (u8 *)malloc((size_t)(gmif_end + grow));
    if (img == NULL) {
        return -1;
    }
    memcpy(img, old, gmif_end);
    memset(img + gmif_end, NARC_PAD, (size_t)grow);
    at = gmif_end;
    for (i = 0; i < o.count; i++) {
        const u8 *a = old + o.gmif + 8 + bw_rd32(old + o.fat + 12 + i * 8);
        u32 alen = bw_rd32(old + o.fat + 16 + i * 8) - bw_rd32(old + o.fat + 12 + i * 8);
        const u8 *b = NULL;
        u32 blen = 0;
        u32 start;

        if (i < f.count) {
            b = fresh + f.gmif + 8 + bw_rd32(fresh + f.fat + 12 + i * 8);
            blen = bw_rd32(fresh + f.fat + 16 + i * 8) - bw_rd32(fresh + f.fat + 12 + i * 8);
        }
        if (alen == blen && (blen == 0 || memcmp(a, b, blen) == 0)) {
            continue;
        }
        start = at - (o.gmif + 8);
        if (blen != 0) {
            memcpy(img + at, b, blen);
        }
        put32(img + o.fat + 12 + i * 8, start);
        put32(img + o.fat + 16 + i * 8, start + blen);
        at += (blen + NARC_ALIGN - 1) & ~(NARC_ALIGN - 1);
    }
    put32(img + o.gmif + 4, at - o.gmif);
    put32(img + 8, at);
    *out = img;
    *out_size = at;
    return 1;
}

/* ------------------------------------------------------------------ */
/* Collecting the mods' members                                        */
/* ------------------------------------------------------------------ */

typedef struct Override {
    u32   index;
    u32   mod;      /* position in the load order, times two; +1 for the
                     * mod's memory layer, which wins over its own folder */
    char *path;     /* the member file, or NULL for a memory member */
    const u8 *mem;  /* a memory member's bytes (the layer owns them) */
    u32   mem_size;
    u32   lang;     /* a line file: 1 for `<bank>.<lang>.msg` (the game's
                     * language), applied after the mod's `<bank>.msg`; 2
                     * for lines made in memory (bw_vfs_lines_set) */
    const BwMsgLines *lines;    /* lines made in memory, path NULL */
} Override;

typedef struct Collect {
    Override   *list;
    u32         count;
    u32         cap;
    Override   *msg;        /* `<bank>.msg` line files, every mod's, in order */
    u32         msg_count;
    u32         msg_cap;
    u32         mod;
    const char *dir;
    int         error;
} Collect;

/*
 * `<digits>` or `<digits>.<ext>` with no further dot: 12, 0012, 12.bin,
 * 12.nclr.  Anything else (12.bin.bak, readme.txt, .DS_Store) is not a
 * member and is ignored.  Returns 1 for a member name, 0 for another name,
 * -1 for a member name whose index the format cannot hold.
 */
static int member_index(const char *name, u32 *index)
{
    const char *p = name;
    u32 v = 0;

    if (*p < '0' || *p > '9') {
        return 0;
    }
    while (*p >= '0' && *p <= '9') {
        if (v < BW_NARC_MAX_MEMBERS) {
            v = v * 10u + (u32)(*p - '0');
        }
        p++;
    }
    if (*p == '.') {
        if (p[1] == '\0' || strchr(p + 1, '.') != NULL) {
            return 0;
        }
    } else if (*p != '\0') {
        return 0;
    }
    if (v >= BW_NARC_MAX_MEMBERS) {
        return -1;
    }
    *index = v;
    return 1;
}

extern int bw_lang_language(void) __attribute__((weak));   /* sdk/langmap.c */

/* The game's language as a line file names it: `en`, `fr`, `de`, `it`,
 * `es` (src/game/version.h: 2, 3, 5, 4, 7). */
static const char *game_lang_code(void)
{
    switch (bw_lang_language != NULL ? bw_lang_language() : 2) {
    case 3: return "fr";
    case 4: return "it";
    case 5: return "de";
    case 7: return "es";
    }
    return "en";
}

/* `<digits>.msg`: lines for text bank `<digits>` (sdk/vfs_msg.h), in every
 * language; `<digits>.<lang>.msg` (`70.fr.msg`): lines for the game in that
 * language only, over the mod's `70.msg` line by line (*lang 1).  A file
 * for another language is not one (0). */
static int msg_index(const char *name, u32 *index, u32 *lang)
{
    size_t n = strlen(name);
    u32 v = 0;
    size_t i;

    *lang = 0;
    if (n < 5 || strcmp(name + n - 4, ".msg") != 0) {
        return 0;
    }
    n -= 4;
    if (n > 3 && name[n - 3] == '.') {
        const char *code = game_lang_code();

        if (name[n - 2] != code[0] || name[n - 1] != code[1]) {
            return 0;                   /* (another language's lines) */
        }
        *lang = 1;
        n -= 3;
    }
    for (i = 0; i < n; i++) {
        if (name[i] < '0' || name[i] > '9') {
            return 0;
        }
        if (v < BW_NARC_MAX_MEMBERS) {
            v = v * 10u + (u32)(name[i] - '0');
        }
    }
    if (v >= BW_NARC_MAX_MEMBERS) {
        return -1;
    }
    *index = v;
    return 1;
}

/* ------------------------------------------------------------------ */
/* The memory layers                                                   */
/* ------------------------------------------------------------------ */

/*
 * A mod's members held in memory (mod API v6, vfs_member_set;
 * docs/mods.md, "Members in memory").  One layer per mod root: it sits
 * where that mod's folder sits in the load order and wins over the
 * folder's own member files for the same index.
 */
typedef struct MemMember {
    char *root;     /* the mod's root, as bw_mod_root_at gives it */
    char *rel;      /* the archive's NitroFS path */
    u32   index;
    u8   *data;
    u32   size;
} MemMember;

static MemMember *g_mem;
static u32        g_mem_count;
static u32        g_mem_cap;

static int mem_find(const char *root, const char *rel, u32 index)
{
    u32 i;

    for (i = 0; i < g_mem_count; i++) {
        if (g_mem[i].index == index && strcmp(g_mem[i].rel, rel) == 0 &&
            strcmp(g_mem[i].root, root) == 0) {
            return (int)i;
        }
    }
    return -1;
}

static char *dup_str(const char *s)
{
    char *d = (char *)malloc(strlen(s) + 1);

    if (d != NULL) {
        strcpy(d, s);
    }
    return d;
}

/* The port's own layer (root "") holds a member of this path: it applies
 * with no mod loaded too (the port's sprites for the other players' looks,
 * engine/avatar/avatar_field.c). */
static int port_layer_has(const char *rel)
{
    u32 k;

    for (k = 0; rel != NULL && k < g_mem_count; k++) {
        if (g_mem[k].root[0] == '\0' && strcmp(g_mem[k].rel, rel) == 0) {
            return 1;
        }
    }
    return 0;
}

int bw_vfs_mem_set(const char *root, const char *rel, u32 index,
                   const void *data, u32 size)
{
    int at;
    u8 *copy;

    if (root == NULL || rel == NULL || index >= BW_NARC_MAX_MEMBERS ||
        (data == NULL && size != 0)) {
        return -1;
    }
    copy = (u8 *)malloc(size == 0 ? 1 : size);
    if (copy == NULL) {
        return -1;
    }
    if (size != 0) {
        memcpy(copy, data, size);
    }
    at = mem_find(root, rel, index);
    if (at >= 0) {
        free(g_mem[at].data);
        g_mem[at].data = copy;
        g_mem[at].size = size;
        return 0;
    }
    if (g_mem_count == g_mem_cap) {
        u32 cap = g_mem_cap == 0 ? 64 : g_mem_cap * 2;
        MemMember *grown = (MemMember *)realloc(g_mem, cap * sizeof(*grown));

        if (grown == NULL) {
            free(copy);
            return -1;
        }
        g_mem = grown;
        g_mem_cap = cap;
    }
    g_mem[g_mem_count].root = dup_str(root);
    g_mem[g_mem_count].rel = dup_str(rel);
    if (g_mem[g_mem_count].root == NULL || g_mem[g_mem_count].rel == NULL) {
        free(g_mem[g_mem_count].root);
        free(g_mem[g_mem_count].rel);
        free(copy);
        return -1;
    }
    g_mem[g_mem_count].index = index;
    g_mem[g_mem_count].data = copy;
    g_mem[g_mem_count].size = size;
    g_mem_count++;
    return 0;
}

u32 bw_vfs_mem_clear(const char *root, const char *rel, u32 index)
{
    u32 i = 0, removed = 0;

    while (i < g_mem_count) {
        MemMember *m = &g_mem[i];

        if ((root == NULL || strcmp(m->root, root) == 0) &&
            (rel == NULL || strcmp(m->rel, rel) == 0) &&
            (index == BW_VFS_MEM_ALL || m->index == index)) {
            free(m->root);
            free(m->rel);
            free(m->data);
            g_mem[i] = g_mem[g_mem_count - 1];
            g_mem_count--;
            removed++;
            continue;
        }
        i++;
    }
    return removed;
}

u32 bw_vfs_mem_bytes(const char *root)
{
    u32 i;
    u64 total = 0;

    for (i = 0; i < g_mem_count; i++) {
        if (root == NULL || strcmp(g_mem[i].root, root) == 0) {
            total += g_mem[i].size;
        }
    }
    return total > 0xFFFFFFFFull ? 0xFFFFFFFFu : (u32)total;
}

int bw_vfs_mem_size(const char *root, const char *rel, u32 index)
{
    int at = mem_find(root, rel, index);

    return at < 0 ? -1 : (int)g_mem[at].size;
}

u32 bw_vfs_mem_list(u32 n, const char **rel, u32 *index)
{
    if (n >= g_mem_count) {
        return 0;
    }
    if (rel != NULL) {
        *rel = g_mem[n].rel;
    }
    if (index != NULL) {
        *index = g_mem[n].index;
    }
    return 1;
}

static int add_entry(Override **list, u32 *count, u32 *cap, u32 index,
                     u32 mod, char *path)
{
    if (*count == *cap) {
        u32 grow = *cap == 0 ? 16 : *cap * 2;
        Override *grown = (Override *)realloc(*list, grow * sizeof(*grown));

        if (grown == NULL) {
            return -1;
        }
        *list = grown;
        *cap = grow;
    }
    (*list)[*count].index = index;
    (*list)[*count].mod = mod;
    (*list)[*count].path = path;
    (*list)[*count].mem = NULL;
    (*list)[*count].mem_size = 0;
    (*list)[*count].lang = 0;
    (*list)[*count].lines = NULL;
    (*count)++;
    return 0;
}

/* ------------------------------------------------------------------ */
/* Line files made in memory                                           */
/* ------------------------------------------------------------------ */

/*
 * A mod's lines made by the port for one bank (sdk/text_from.c: its move
 * and item lines in the game's language).  They apply as the mod's
 * `<bank>.<lang>.msg` would: after its own line files for the bank.
 */
typedef struct MemLines {
    char      *root;
    char      *rel;
    u32        bank;
    BwMsgLines lines;
} MemLines;

static MemLines *g_lines;
static u32 g_lines_count, g_lines_cap;

static int copy_lines(BwMsgLines *dst, const BwMsgLine *src, u32 count)
{
    u32 i;

    memset(dst, 0, sizeof(*dst));
    dst->lines = (BwMsgLine *)calloc(count == 0 ? 1 : count, sizeof(*dst->lines));
    if (dst->lines == NULL) {
        return -1;
    }
    dst->cap = count;
    for (i = 0; i < count; i++) {
        BwMsgLine *l = &dst->lines[i];

        l->index = src[i].index;
        l->len = src[i].len;
        l->units = NULL;
        if (src[i].len != 0) {
            l->units = (u16 *)malloc(src[i].len * sizeof(u16));
            if (l->units == NULL) {
                dst->count = i;
                bw_msg_lines_free(dst);
                return -1;
            }
            memcpy(l->units, src[i].units, src[i].len * sizeof(u16));
        }
        dst->count = i + 1;
    }
    return 0;
}

int bw_vfs_lines_set(const char *root, const char *rel, u32 bank, const BwMsgLine *lines, u32 count)
{
    MemLines *m = NULL;
    BwMsgLines made;
    u32 k;

    if (root == NULL || rel == NULL || bank >= BW_NARC_MAX_MEMBERS || (lines == NULL && count != 0) ||
        copy_lines(&made, lines, count) != 0) {
        return -1;
    }
    for (k = 0; k < g_lines_count; k++) {
        if (g_lines[k].bank == bank && strcmp(g_lines[k].root, root) == 0 && strcmp(g_lines[k].rel, rel) == 0) {
            m = &g_lines[k];
            bw_msg_lines_free(&m->lines);
            break;
        }
    }
    if (m == NULL) {
        if (g_lines_count == g_lines_cap) {
            u32 cap = g_lines_cap == 0 ? 8 : g_lines_cap * 2;
            MemLines *grown = (MemLines *)realloc(g_lines, cap * sizeof(*grown));

            if (grown == NULL) {
                bw_msg_lines_free(&made);
                return -1;
            }
            g_lines = grown;
            g_lines_cap = cap;
        }
        m = &g_lines[g_lines_count];
        m->root = dup_str(root);
        m->rel = dup_str(rel);
        if (m->root == NULL || m->rel == NULL) {
            free(m->root);
            free(m->rel);
            bw_msg_lines_free(&made);
            return -1;
        }
        m->bank = bank;
        g_lines_count++;
    }
    m->lines = made;
    bw_vfs_flush_path(rel);
    return 0;
}

void bw_vfs_lines_clear(void)
{
    u32 k;

    for (k = 0; k < g_lines_count; k++) {
        bw_vfs_flush_path(g_lines[k].rel);
        free(g_lines[k].root);
        free(g_lines[k].rel);
        bw_msg_lines_free(&g_lines[k].lines);
    }
    free(g_lines);
    g_lines = NULL;
    g_lines_count = g_lines_cap = 0;
}

static int collect_one(void *ctx, const char *name)
{
    Collect *c = (Collect *)ctx;
    u32 index;
    int kind;
    size_t need;
    char *path;
    int is_msg;
    u32 lang = 0;

    /* A line file is not a member: `70.msg` holds lines for bank 70, it is
     * not member 70 itself. */
    kind = msg_index(name, &index, &lang);
    is_msg = kind != 0;
    if (!is_msg) {
        kind = member_index(name, &index);
    }
    if (kind == 0) {
        return 0;
    }
    if (kind < 0) {
        fprintf(stderr, "vfs: %s/%s: member index past the NARC limit of %u\n",
                c->dir, name, BW_NARC_MAX_MEMBERS);
        c->error = 1;
        return 1;
    }
    need = strlen(c->dir) + 1 + strlen(name) + 1;
    path = (char *)malloc(need);
    if (path == NULL) {
        c->error = 1;
        return 1;
    }
    sprintf(path, "%s/%s", c->dir, name);
    if (bw_plat_path_kind(path) != BW_PATH_FILE) {
        free(path);      /* a subdirectory named like a member: not one */
        return 0;
    }
    if ((is_msg ? add_entry(&c->msg, &c->msg_count, &c->msg_cap, index, c->mod, path)
                : add_entry(&c->list, &c->count, &c->cap, index, c->mod, path)) != 0) {
        free(path);
        c->error = 1;
        return 1;
    }
    if (is_msg) {
        c->msg[c->msg_count - 1].lang = lang;
    }
    return 0;
}

static int cmp_override(const void *a, const void *b)
{
    const Override *x = (const Override *)a;
    const Override *y = (const Override *)b;

    if (x->index != y->index) {
        return x->index < y->index ? -1 : 1;
    }
    if (x->mod != y->mod) {
        return x->mod < y->mod ? -1 : 1;
    }
    if (x->lang != y->lang) {
        return x->lang < y->lang ? -1 : 1;      /* (a mod's language lines after its own) */
    }
    return 0;
}

static void free_collect(Collect *c)
{
    u32 i;

    for (i = 0; i < c->count; i++) {
        free(c->list[i].path);
    }
    free(c->list);
    for (i = 0; i < c->msg_count; i++) {
        free(c->msg[i].path);
    }
    free(c->msg);
    memset(c, 0, sizeof(*c));
}

/*
 * Gather the member files of every mod later than the file-granular winner
 * `layer`, then reduce them to one per index, the latest mod's.  Two files
 * for one index in the same mod is an error: neither is "later".
 * Returns 0 (c->count may be 0) or -1.
 */
static int collect(const char *rel, int layer, Collect *c)
{
    char dir[1200];
    u32 n = bw_mod_count();
    u32 i;
    u32 kept;

    memset(c, 0, sizeof(*c));
    /* The winner's own memory layer applies on top of its whole file, so
     * the walk starts at the winner itself (the data root is -1). */
    for (i = layer < 0 ? 0u : (u32)layer; i < n; i++) {
        const char *root = bw_mod_root_at(i);
        u32 k;

        if ((int)i > layer && strlen(root) + 1 + strlen(rel) + 1 <= sizeof(dir) &&
            snprintf(dir, sizeof(dir), "%s/%s", root, rel) < (int)sizeof(dir) &&
            bw_plat_path_kind(dir) == BW_PATH_DIR) {
            c->mod = i * 2u;
            c->dir = dir;
            (void)bw_plat_list_dir(dir, collect_one, c);
            if (c->error) {
                free_collect(c);
                return -1;
            }
        }
        /* the mod's memory layer, over its folder */
        for (k = 0; k < g_mem_count; k++) {
            if (strcmp(g_mem[k].rel, rel) != 0 || strcmp(g_mem[k].root, root) != 0) {
                continue;
            }
            if (add_entry(&c->list, &c->count, &c->cap, g_mem[k].index,
                          i * 2u + 1u, NULL) != 0) {
                free_collect(c);
                return -1;
            }
            c->list[c->count - 1].mem = g_mem[k].data;
            c->list[c->count - 1].mem_size = g_mem[k].size;
        }
        /* the lines the port made for the mod, after its line files */
        for (k = 0; (int)i > layer && k < g_lines_count; k++) {
            if (strcmp(g_lines[k].rel, rel) != 0 || strcmp(g_lines[k].root, root) != 0) {
                continue;
            }
            if (add_entry(&c->msg, &c->msg_count, &c->msg_cap, g_lines[k].bank, i * 2u, NULL) != 0) {
                free_collect(c);
                return -1;
            }
            c->msg[c->msg_count - 1].lang = 2;
            c->msg[c->msg_count - 1].lines = &g_lines[k].lines;
        }
    }
    /* The port's own layer (root ""), over every mod: what the port puts
     * in for a moment, the title of a mod's text input (sdk/mod_v6.c). */
    {
        u32 k;

        for (k = 0; k < g_mem_count; k++) {
            if (g_mem[k].root[0] != '\0' || strcmp(g_mem[k].rel, rel) != 0) {
                continue;
            }
            if (add_entry(&c->list, &c->count, &c->cap, g_mem[k].index,
                          n * 2u + 1u, NULL) != 0) {
                free_collect(c);
                return -1;
            }
            c->list[c->count - 1].mem = g_mem[k].data;
            c->list[c->count - 1].mem_size = g_mem[k].size;
        }
    }
    /* The line files go in load order per bank, a mod's `<bank>.<lang>.msg`
     * after its `<bank>.msg` (the merge applies them in this order; a
     * folder lists "70.fr.msg" before "70.msg"). */
    if (c->msg_count > 1) {
        qsort(c->msg, c->msg_count, sizeof(*c->msg), cmp_override);
    }
    if (c->count == 0) {
        return 0;
    }
    qsort(c->list, c->count, sizeof(*c->list), cmp_override);
    kept = 0;
    for (i = 0; i < c->count; i++) {
        if (i + 1 < c->count && c->list[i + 1].index == c->list[i].index) {
            if (c->list[i + 1].mod == c->list[i].mod) {
                fprintf(stderr, "vfs: %s and %s name the same member of %s\n",
                        c->list[i].path, c->list[i + 1].path, rel);
                free_collect(c);
                return -1;
            }
            free(c->list[i].path);        /* a later mod has this index */
            continue;
        }
        c->list[kept++] = c->list[i];
    }
    c->count = kept;
    return 0;
}

int bw_vfs_member_overrides(const char *rel)
{
    Collect c;
    int layer = bw_vfs_file_layer(rel);
    int n;

    if (layer == BW_LAYER_NONE || (bw_mod_count() == 0 && !port_layer_has(rel))) {
        return 0;
    }
    if (collect(rel, layer, &c) != 0) {
        return -1;
    }
    n = (int)(c.count + c.msg_count);
    free_collect(&c);
    return n;
}

/* ------------------------------------------------------------------ */
/* Merging                                                             */
/* ------------------------------------------------------------------ */

/*
 * The text bank a `.msg` merge starts from when the bank has no bytes yet
 * (an index past the archive's end): one language section, no lines.
 */
static const u8 k_empty_bank[20] = {
    1, 0, 0, 0,   4, 0, 0, 0,   0, 0, 0, 0,   16, 0, 0, 0,   4, 0, 0, 0
};

/*
 * Apply the `.msg` line files to the members in data[]/sizes[] (count of
 * them, sized for every bank index a line file names).  Each bank's lines
 * apply in load order on top of whatever the whole-member layers left.
 * The merged banks are kept in merged[] (count entries, NULL where none)
 * for the caller to free.  Returns 0 or -1.
 */
static int apply_msg(const char *rel, const Collect *c, const u8 **data,
                     u32 *sizes, u32 count, u8 **merged)
{
    u32 i;

    for (i = 0; i < c->msg_count; i++) {
        u32 idx = c->msg[i].index;
        BwMsgLines *lists[64];
        const BwMsgLines *const_lists[64];
        u32 n = 0, j;
        int rc;
        const u8 *base;
        u32 base_size;

        if (merged[idx] != NULL) {
            continue;                       /* done with an earlier entry */
        }
        /* every line file for this bank, in load order */
        for (j = i; j < c->msg_count; j++) {
            char err[256];
            u8 *text;
            u32 text_size;

            if (c->msg[j].index != idx) {
                continue;
            }
            if (n == sizeof(lists) / sizeof(lists[0])) {
                fprintf(stderr, "vfs: %s bank %u: too many line files\n", rel, idx);
                break;
            }
            lists[n] = (BwMsgLines *)calloc(1, sizeof(BwMsgLines));
            if (c->msg[j].lines != NULL) {
                /* lines made in memory */
                if (lists[n] == NULL ||
                    copy_lines(lists[n], c->msg[j].lines->lines, c->msg[j].lines->count) != 0) {
                    fprintf(stderr, "vfs: %s bank %u: no memory for the made lines\n", rel, idx);
                    free(lists[n]);
                    for (j = 0; j < n; j++) {
                        bw_msg_lines_free(lists[j]);
                        free(lists[j]);
                    }
                    return -1;
                }
                const_lists[n] = lists[n];
                n++;
                continue;
            }
            text = read_whole(c->msg[j].path, &text_size);
            if (lists[n] == NULL || text == NULL ||
                bw_msg_parse((const char *)text, text_size, lists[n], err,
                             sizeof(err)) != 0) {
                fprintf(stderr, "vfs: %s: %s\n", c->msg[j].path,
                        text == NULL ? "cannot read" : err);
                free(text);
                if (lists[n] != NULL) {
                    bw_msg_lines_free(lists[n]);
                    free(lists[n]);
                }
                for (j = 0; j < n; j++) {
                    bw_msg_lines_free(lists[j]);
                    free(lists[j]);
                }
                return -1;
            }
            free(text);
            const_lists[n] = lists[n];
            n++;
        }
        base = idx < count ? data[idx] : NULL;
        base_size = idx < count ? sizes[idx] : 0;
        if (base == NULL || base_size < sizeof(k_empty_bank)) {
            base = k_empty_bank;
            base_size = sizeof(k_empty_bank);
        }
        rc = bw_msg_merge(base, base_size, const_lists, n, &merged[idx], &sizes[idx]);
        for (j = 0; j < n; j++) {
            bw_msg_lines_free(lists[j]);
            free(lists[j]);
        }
        if (rc != 0) {
            fprintf(stderr, "vfs: %s: bank %u: the text lines do not merge\n", rel, idx);
            merged[idx] = NULL;
            return -1;
        }
        data[idx] = merged[idx];
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* The sound archive                                                   */
/* ------------------------------------------------------------------ */

#define SDAT_MODS_MAX 32

/*
 * An SDAT is not a NARC.  A mod holding a directory at its path adds
 * sequences, banks and wave archives through `sdat.txt` there
 * (sdk/vfs_sdat.h); the merge keeps every original byte and appends.
 * Returns 1 with *out set, 0 when no mod adds anything, or -1.
 */
static int build_sdat(const char *rel, int layer, u8 **out, u32 *out_size)
{
    char dir[1200], path[1400], base_path[1200];
    u32 n = bw_mod_count();
    u32 i, k, nadd = 0;
    BwSdatAdd adds[SDAT_MODS_MAX];
    BwSdatDecl decls[SDAT_MODS_MAX];
    BwSdatFile *files[SDAT_MODS_MAX];
    u8 *base = NULL;
    u32 base_size;
    int rc = -1;

    memset(decls, 0, sizeof(decls));
    memset(files, 0, sizeof(files));
    for (i = (u32)(layer + 1); i < n && nadd < SDAT_MODS_MAX; i++) {
        const char *root = bw_mod_root_at(i);
        u8 *text;
        u32 text_size;
        char err[256];

        if (strlen(root) + 1 + strlen(rel) + 16 > sizeof(dir)) {
            continue;
        }
        if (snprintf(dir, sizeof(dir), "%s/%s", root, rel) >= (int)sizeof(dir) ||
            snprintf(path, sizeof(path), "%s/sdat.txt", dir) >= (int)sizeof(path)) {
            continue;
        }
        if (bw_plat_path_kind(path) != BW_PATH_FILE) {
            continue;
        }
        text = read_whole(path, &text_size);
        if (text == NULL || bw_sdat_parse_decl((const char *)text, &decls[nadd],
                                               err, sizeof(err)) != 0) {
            fprintf(stderr, "vfs: %s: %s\n", path, text == NULL ? "cannot read" : err);
            free(text);
            goto done;
        }
        free(text);
        files[nadd] = (BwSdatFile *)calloc(decls[nadd].count + 1, sizeof(BwSdatFile));
        if (files[nadd] == NULL) {
            nadd++;
            goto done;
        }
        for (k = 0; k < decls[nadd].count; k++) {
            u32 sz = 0;
            char fp[1400];

            snprintf(fp, sizeof(fp), "%s/%s", dir, decls[nadd].entries[k].file);
            files[nadd][k].name = decls[nadd].entries[k].file;
            files[nadd][k].data = read_whole(fp, &sz);
            files[nadd][k].size = sz;
            if (files[nadd][k].data == NULL) {
                fprintf(stderr, "vfs: %s: cannot read\n", fp);
                nadd++;
                goto done;
            }
        }
        adds[nadd].mod_name = bw_mod_name_at(i);
        adds[nadd].decl = &decls[nadd];
        adds[nadd].files = files[nadd];
        adds[nadd].file_count = decls[nadd].count;
        nadd++;
    }
    if (nadd == 0) {
        return 0;
    }
    if (bw_data_path(base_path, sizeof(base_path), rel) != 0 ||
        (base = read_whole(base_path, &base_size)) == NULL) {
        fprintf(stderr, "vfs: %s: cannot read the sound archive\n", rel);
        goto done;
    }
    if (bw_vfs_sdat_merge(base, base_size, adds, nadd, out, out_size) != 0) {
        goto done;
    }
    rc = 1;

done:
    for (i = 0; i < nadd; i++) {
        if (files[i] != NULL) {
            for (k = 0; k < decls[i].count; k++) {
                free((void *)files[i][k].data);
            }
            free(files[i]);
        }
        bw_sdat_decl_free(&decls[i]);
    }
    free(base);
    return rc;
}

int bw_vfs_build_merged(const char *rel, u8 **out, u32 *out_size)
{
    char base_path[1200];
    Collect c;
    Narc narc;
    int layer;
    u8 *base = NULL;
    u32 base_size;
    const u8 **data = NULL;
    u32 *sizes = NULL;
    u8 **owned = NULL;
    u8 **msg_merged = NULL;
    u32 count = 0;
    u32 i;
    int rc = -1;

    *out = NULL;
    *out_size = 0;
    if (bw_mod_count() == 0 && !port_layer_has(rel)) {
        return 0;
    }
    /*
     * Members apply on top of a file: with no layer holding one, a mod
     * directory at this path is just a directory (a/0/1 holds the archives
     * a/0/1/0..9, whose names look like member indices).
     */
    layer = bw_vfs_file_layer(rel);
    if (layer == BW_LAYER_NONE) {
        return 0;
    }
    {
        size_t rl = strlen(rel);

        if (rl > 5 && strcmp(rel + rl - 5, ".sdat") == 0) {
            return build_sdat(rel, layer, out, out_size);
        }
    }
    if (collect(rel, layer, &c) != 0) {
        return -1;
    }
    if (c.count == 0 && c.msg_count == 0) {
        free_collect(&c);
        return 0;
    }

    if (bw_data_path(base_path, sizeof(base_path), rel) != 0 ||
        (base = read_whole(base_path, &base_size)) == NULL) {
        fprintf(stderr, "vfs: %s: cannot read the archive the members apply to\n", rel);
        goto done;
    }
    if (parse_narc(base, base_size, &narc) != 0) {
        fprintf(stderr, "vfs: %s: member overrides given, but %s is not a NARC\n",
                rel, base_path);
        goto done;
    }

    count = narc.count;
    if (c.count > 0 && c.list[c.count - 1].index + 1 > count) {
        count = c.list[c.count - 1].index + 1;
    }
    for (i = 0; i < c.msg_count; i++) {
        if (c.msg[i].index + 1 > count) {
            count = c.msg[i].index + 1;
        }
    }
    data = (const u8 **)calloc(count == 0 ? 1 : count, sizeof(*data));
    sizes = (u32 *)calloc(count == 0 ? 1 : count, sizeof(*sizes));
    owned = (u8 **)calloc(c.count == 0 ? 1 : c.count, sizeof(*owned));
    msg_merged = (u8 **)calloc(count == 0 ? 1 : count, sizeof(*msg_merged));
    if (data == NULL || sizes == NULL || owned == NULL || msg_merged == NULL) {
        goto done;
    }
    for (i = 0; i < narc.count; i++) {
        u32 start = bw_rd32(base + narc.fat + 12 + i * 8);
        u32 end = bw_rd32(base + narc.fat + 16 + i * 8);

        data[i] = base + narc.gmif + 8 + start;
        sizes[i] = end - start;
    }
    /* Indices past the base count that no mod fills stay empty members. */
    for (i = 0; i < c.count; i++) {
        u32 sz;

        if (c.list[i].path == NULL) {
            /* a memory member: the layer owns the bytes */
            data[c.list[i].index] = c.list[i].mem;
            sizes[c.list[i].index] = c.list[i].mem_size;
            continue;
        }
        owned[i] = read_whole(c.list[i].path, &sz);
        if (owned[i] == NULL) {
            fprintf(stderr, "vfs: %s: cannot read member file\n", c.list[i].path);
            goto done;
        }
        data[c.list[i].index] = owned[i];
        sizes[c.list[i].index] = sz;
    }
    /* The text lines, on top of every whole-member layer. */
    if (c.msg_count > 0 && apply_msg(rel, &c, data, sizes, count, msg_merged) != 0) {
        goto done;
    }
    if (bw_narc_pack(data, sizes, count,
                     narc.btnf != 0 ? base + narc.btnf : NULL, narc.btnf_size,
                     out, out_size) != 0) {
        fprintf(stderr, "vfs: %s: the merged archive does not fit the NARC format\n", rel);
        goto done;
    }
    rc = 1;

done:
    if (owned != NULL) {
        for (i = 0; i < c.count; i++) {
            free(owned[i]);
        }
    }
    free(owned);
    if (msg_merged != NULL) {
        for (i = 0; i < count; i++) {
            bw_msg_free(msg_merged[i]);
        }
    }
    free(msg_merged);
    free(sizes);
    free((void *)data);
    free(base);
    free_collect(&c);
    return rc;
}

/* ------------------------------------------------------------------ */
/* The cache the FS shim reads                                         */
/* ------------------------------------------------------------------ */

typedef struct Merged {
    char *rel;     /* NULL: a free entry                         */
    u8   *data;
    u32   size;
    u32   refs;    /* open files reading it                       */
    int   stale;   /* dropped by a flush, freed at the last close */
} Merged;

static Merged *g_merged;
static u32     g_merged_cap;

static void free_entry(Merged *m)
{
    free(m->rel);
    free(m->data);
    memset(m, 0, sizeof(*m));
}

void bw_vfs_flush(void)
{
    u32 i;

    for (i = 0; i < g_merged_cap; i++) {
        if (g_merged[i].rel == NULL) {
            continue;
        }
        if (g_merged[i].refs == 0) {
            free_entry(&g_merged[i]);
        } else {
            g_merged[i].stale = 1;
        }
    }
}

void bw_vfs_flush_path(const char *rel)
{
    u32 i;

    for (i = 0; i < g_merged_cap; i++) {
        if (g_merged[i].rel == NULL || strcmp(g_merged[i].rel, rel) != 0) {
            continue;
        }
        if (g_merged[i].refs == 0) {
            free_entry(&g_merged[i]);
        } else {
            g_merged[i].stale = 1;
        }
    }
}

static int new_entry(u32 *index);

/*
 * Take over an image built outside the cache (the layout-stable image of
 * an archive that is open, sdk/fs.c): it is held by `refs` open files and
 * freed at the last close.  It is stale from the start, so a new open
 * builds the archive again rather than share it.  Returns the handle, or 0.
 */
u32 bw_vfs_adopt(const char *rel, u8 *img, u32 size, u32 refs)
{
    u32 i;
    char *key = dup_str(rel);

    if (key == NULL || refs == 0 || new_entry(&i) != 0) {
        free(key);
        return 0;
    }
    g_merged[i].rel = key;
    g_merged[i].data = img;
    g_merged[i].size = size;
    g_merged[i].refs = refs;
    g_merged[i].stale = 1;
    return i + 1;
}

static int new_entry(u32 *index)
{
    u32 i;
    u32 cap;
    Merged *grown;

    for (i = 0; i < g_merged_cap; i++) {
        if (g_merged[i].rel == NULL) {
            *index = i;
            return 0;
        }
    }
    cap = g_merged_cap == 0 ? 8 : g_merged_cap * 2;
    grown = (Merged *)realloc(g_merged, cap * sizeof(*grown));
    if (grown == NULL) {
        return -1;
    }
    memset(grown + g_merged_cap, 0, (cap - g_merged_cap) * sizeof(*grown));
    *index = g_merged_cap;
    g_merged = grown;
    g_merged_cap = cap;
    return 0;
}

int bw_vfs_acquire(const char *rel, const u8 **data, u32 *size, u32 *handle)
{
    u32 i;
    u8 *img;
    u32 img_size;
    char *key;
    int rc;

    *data = NULL;
    *size = 0;
    *handle = 0;
    if (rel == NULL || (bw_mod_count() == 0 && !port_layer_has(rel))) {
        return 0;
    }
    for (i = 0; i < g_merged_cap; i++) {
        Merged *m = &g_merged[i];

        if (m->rel != NULL && !m->stale && strcmp(m->rel, rel) == 0) {
            m->refs++;
            *data = m->data;
            *size = m->size;
            *handle = i + 1;
            return 1;
        }
    }
    rc = bw_vfs_build_merged(rel, &img, &img_size);
    if (rc <= 0) {
        return rc;
    }
    key = (char *)malloc(strlen(rel) + 1);
    if (key == NULL || new_entry(&i) != 0) {
        free(key);
        free(img);
        return -1;
    }
    strcpy(key, rel);
    g_merged[i].rel = key;
    g_merged[i].data = img;
    g_merged[i].size = img_size;
    g_merged[i].refs = 1;
    g_merged[i].stale = 0;
    *data = img;
    *size = img_size;
    *handle = i + 1;
    return 1;
}

void bw_vfs_release(u32 handle)
{
    Merged *m;

    if (handle == 0 || handle > g_merged_cap) {
        return;
    }
    m = &g_merged[handle - 1];
    if (m->rel == NULL || m->refs == 0) {
        return;
    }
    m->refs--;
    if (m->refs == 0 && m->stale) {
        free_entry(m);
    }
}
