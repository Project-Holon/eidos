/*
 * sdk/langmap.c -- playing another cartridge than English Black
 * (sdk/langmap.h).
 *
 * A row of a map says that English Black's bytes [eng_start, eng_end) of a
 * region (the ARM9's main RAM, or one overlay) are at tgt_start in the other
 * cartridge.  Laying an image out copies each row's bytes from the
 * cartridge's place to English Black's; what no row covers stays zero.
 * The pointer words in the data still hold the cartridge's addresses: the
 * relocation (sdk/armdata.c) asks bw_lang_same_address whether a word is
 * the English address it expects, in the cartridge's layout.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "langmap.h"
#include "overlay.h"
#include "port.h"

typedef struct BwLangRow {
    int region;         /* -1 the ARM9's main RAM, else the overlay id */
    u32 eng_start, eng_end, tgt_start;
} BwLangRow;

typedef struct BwLangMap {
    const char *gamecode;
    const char *target;
    const BwLangRow *rows;
    u32 count;
} BwLangMap;

#include "langmap_table.h"

/* Whether row `r` is its region's own: an overlay's rows also list the
 * ARM9 and other overlays it refers to, which are not its bytes. */
static int own_row(const BwLangRow *r)
{
    const BwOverlayDesc *d;

    if (r->region < 0) {
        return 1;
    }
    d = bw_overlay_desc((u32)r->region);
    return d != NULL && r->eng_start >= d->ram_address
        && r->eng_end <= d->ram_address + d->ram_size + d->bss_size;
}

/* The map's rows as the layout uses them.  A run of an overlay's can reach
 * past the overlay, over the window it shares with others (ov094's starts
 * in ov093's place): its part inside the overlay is still the overlay's,
 * so the row is cut to the overlay's English extent, not dropped.  Made at
 * the first use after the overlay table is read. */
static BwLangRow *g_rows;
static u32 g_nrows;
static int g_rows_ovl;          /* made with the overlay table */

/* The game's language ids (src/game/version.h). */
#define LANG_ENGLISH 2
#define LANG_FRENCH  3
#define LANG_ITALIAN 4
#define LANG_GERMAN  5
#define LANG_SPANISH 7

/* The game reads its language here (src/game/version.h, PM_LANGUAGE_RUNTIME). */
int g_pm_language = LANG_ENGLISH;
/* ... and its version (VERSION_BLACK 1, VERSION_WHITE 2), the same way. */
int g_pm_version = 1;

static const BwLangMap *g_map;

/* The cartridge's own images, as they were read: the ARM9's and each
 * overlay's (bw_lang_target_ptr). */
static u8 *g_tgt_arm9;
static u32 g_tgt_arm9_size;
#define OVL_KEEP 512
static u8 g_whole_known[OVL_KEEP], g_whole_ok[OVL_KEEP];
static u32 g_whole_eng[OVL_KEEP], g_whole_tgt[OVL_KEEP], g_whole_size[OVL_KEEP];
static u8 *g_tgt_ovl[OVL_KEEP];
static u32 g_tgt_ovl_base[OVL_KEEP], g_tgt_ovl_size[OVL_KEEP];

static u8 *keep_copy(const u8 *src, u32 size)
{
    u8 *p = (u8 *)malloc(size ? size : 1);

    if (p != NULL) {
        memcpy(p, src, size);
    }
    return p;
}
static char g_code[5] = "IRBO";

#define ARM9_BASE 0x02004000u   /* arm9_dec.bin's first byte (BW_ARM9_LOAD_ADDRESS) */

/* <extract>/<rel>: the data root's parent, as sdk/narc.c finds arm9_dec.bin. */
static int extract_path(char *out, size_t cap, const char *rel)
{
    const char *root = bw_data_root();
    size_t n = root != NULL ? strlen(root) : 0;

    if (n + strlen(rel) + 2 > cap) {
        return -1;
    }
    memcpy(out, root != NULL ? root : "", n + 1);
    while (n > 0 && (out[n - 1] == '/' || out[n - 1] == '\\')) {
        n--;
    }
    while (n > 0 && out[n - 1] != '/' && out[n - 1] != '\\') {
        n--;
    }
    strcpy(out + n, rel);
    return 0;
}

static int read_at(const char *rel, u32 off, void *dst, u32 size)
{
    char path[1100];
    FILE *fp;
    int ok;

    if (extract_path(path, sizeof(path), rel) != 0 || (fp = fopen(path, "rb")) == NULL) {
        return -1;
    }
    ok = fseek(fp, (long)off, SEEK_SET) == 0 && fread(dst, 1, size, fp) == size;
    fclose(fp);
    return ok ? 0 : -1;
}

static void rows_ready(void)
{
    u32 i;
    int ovl = bw_overlay_desc(0) != NULL;

    if (g_rows != NULL && (g_rows_ovl || !ovl)) {
        return;
    }
    free(g_rows);
    g_nrows = 0;
    g_rows = (BwLangRow *)malloc((g_map->count ? g_map->count : 1) * sizeof(BwLangRow));
    if (g_rows == NULL) {
        return;
    }
    g_rows_ovl = ovl;
    for (i = 0; i < g_map->count; i++) {
        BwLangRow r = g_map->rows[i];

        if (r.region >= 0) {
            const BwOverlayDesc *d = bw_overlay_desc((u32)r.region);
            u32 lo, hi;

            if (d == NULL) {
                continue;
            }
            lo = d->ram_address;
            hi = d->ram_address + d->ram_size + d->bss_size;
            if (r.eng_end <= lo || r.eng_start >= hi) {
                continue;
            }
            if (r.eng_start < lo) {
                r.tgt_start += lo - r.eng_start;
                r.eng_start = lo;
            }
            if (r.eng_end > hi) {
                r.eng_end = hi;
            }
        }
        g_rows[g_nrows++] = r;
    }
}

void bw_lang_init(void)
{
    char code[4];
    u32 i;

    g_map = NULL;
    free(g_rows);
    g_rows = NULL;
    g_nrows = 0;
    memset(g_whole_known, 0, sizeof(g_whole_known));
    memset(g_whole_ok, 0, sizeof(g_whole_ok));
    g_pm_language = LANG_ENGLISH;
    g_pm_version = 1;
    memcpy(g_code, "IRBO", 5);
    if (read_at("header.bin", 0x0C, code, 4) != 0) {
        return;
    }
    memcpy(g_code, code, 4);
    g_code[4] = '\0';
    g_pm_version = code[2] == 'A' ? 2 : 1;      /* IRA*: White */
    switch (code[3]) {
    case 'F': g_pm_language = LANG_FRENCH; break;
    case 'D': g_pm_language = LANG_GERMAN; break;
    case 'I': g_pm_language = LANG_ITALIAN; break;
    case 'S': g_pm_language = LANG_SPANISH; break;
    default:  g_pm_language = LANG_ENGLISH; break;
    }
    for (i = 0; i < sizeof(bw_langmaps) / sizeof(bw_langmaps[0]); i++) {
        if (memcmp(bw_langmaps[i].gamecode, code, 4) == 0) {
            g_map = &bw_langmaps[i];
        }
    }
    if (g_map != NULL) {
        fprintf(stderr, "eidos: cartridge %s: laid out as English Black (%s map, %lu rows)\n",
                g_code, g_map->target, (unsigned long)g_map->count);
    }
}

const char *bw_lang_gamecode(void)
{
    return g_code;
}

int bw_lang_active(void)
{
    return g_map != NULL;
}

/* Lays one region out: every row of it copied from the cartridge's place
 * (`src`, its first byte at `src_base`) to English Black's (`dst`, at
 * `dst_base`). */
static void relayout(int region, u8 *dst, u32 dst_base, u32 dst_size, const u8 *src, u32 src_base,
                     u32 src_size)
{
    u32 i, pe = dst_base, pt = src_base;

    /* What lies between two rows (the padding, an object the map did not
     * pair) is taken as it is where the gap is as long in both images,
     * and so is what follows the last row: the same bytes in the same
     * order, only moved. */
    for (i = 0; i <= g_nrows; i++) {
        const BwLangRow *r = i < g_nrows ? &g_rows[i] : NULL;
        u32 ge, gt, n;

        if (r != NULL && (r->region != region || r->eng_start < dst_base
                          || r->tgt_start < src_base || !own_row(r))) {
            continue;
        }
        ge = r != NULL ? r->eng_start : dst_base + dst_size;
        gt = r != NULL ? r->tgt_start : src_base + src_size;
        if (ge > pe && gt > pt && (r == NULL || ge - pe == gt - pt)
            && pe - dst_base < dst_size && pt - src_base < src_size) {
            n = ge - pe < gt - pt ? ge - pe : gt - pt;
            if (n > dst_size - (pe - dst_base)) n = dst_size - (pe - dst_base);
            if (n > src_size - (pt - src_base)) n = src_size - (pt - src_base);
            memcpy(dst + (pe - dst_base), src + (pt - src_base), n);
        }
        if (r != NULL && r->eng_end > pe) {
            pe = r->eng_end;
            pt = r->tgt_start + (r->eng_end - r->eng_start);
        }
    }
    for (i = 0; i < g_nrows; i++) {
        const BwLangRow *r = &g_rows[i];
        u32 n, d, s;

        if (r->region != region || r->eng_start < dst_base || r->tgt_start < src_base
            || !own_row(r)) {
            continue;
        }
        n = r->eng_end - r->eng_start;
        d = r->eng_start - dst_base;
        s = r->tgt_start - src_base;
        if (d >= dst_size || s >= src_size) {
            continue;           /* past the image: .bss, or past the file */
        }
        if (n > dst_size - d) n = dst_size - d;
        if (n > src_size - s) n = src_size - s;
        memcpy(dst + d, src + s, n);
    }
}

/* The words of a laid-out image that hold one of the cartridge's own
 * addresses take the English address of the same byte: the data's
 * pointers, listed or not (an identity-mapped build relocates only the
 * words the metadata lists, and the rest are read as they are). */
static int to_english_in(u32 word, int region, u32 *out);

/* An overlay the map has no rows of, the same length in both cartridges:
 * one run from its English place to its place in the cartridge's own
 * table (y9.bin).  0 when it is not such an overlay. */

static int whole_overlay(int id, u32 *eng, u32 *tgt, u32 *size)
{
    if (id < 0 || id >= OVL_KEEP) {
        return 0;
    }
    if (!g_whole_known[id]) {
        const BwOverlayDesc *d = bw_overlay_desc((u32)id);
        u8 e[16];
        u32 i, rows = 0;

        g_whole_known[id] = 1;
        for (i = 0; i < g_nrows; i++) {
            if (g_rows[i].region == id && own_row(&g_rows[i])) {
                rows++;
            }
        }
        if (d != NULL && rows == 0 && read_at("y9.bin", (u32)id * 32u, e, sizeof(e)) == 0) {
            u32 tb = (u32)e[4] | (u32)e[5] << 8 | (u32)e[6] << 16 | (u32)e[7] << 24;
            u32 ts = (u32)e[8] | (u32)e[9] << 8 | (u32)e[10] << 16 | (u32)e[11] << 24;
            u32 tz = (u32)e[12] | (u32)e[13] << 8 | (u32)e[14] << 16 | (u32)e[15] << 24;

            if (ts == d->ram_size && tz == d->bss_size) {
                g_whole_ok[id] = 1;
                g_whole_eng[id] = d->ram_address;
                g_whole_tgt[id] = tb;
                g_whole_size[id] = ts + tz;
            }
        }
    }
    if (!g_whole_ok[id]) {
        return 0;
    }
    *eng = g_whole_eng[id];
    *tgt = g_whole_tgt[id];
    *size = g_whole_size[id];
    return 1;
}

/* An address in the overlays' windows, from another image (the ARM9's
 * table of a resident overlay's functions): the English address when every
 * overlay that holds it agrees on one. */
static int to_english_any_overlay(u32 word, u32 *out)
{
    u32 i, found = 0, e = 0, span = 0xFFFFFFFFu;

    /* overlays share windows: where two disagree, the shorter run is the
     * more exact (a long run spans objects the overlay merely refers to) */
    for (i = 0; i < g_nrows; i++) {
        const BwLangRow *r = &g_rows[i];

        if (r->region >= 0 && own_row(r) && word >= r->tgt_start
            && word - r->tgt_start < r->eng_end - r->eng_start
            && r->eng_end - r->eng_start < span) {
            e = word - r->tgt_start + r->eng_start;
            span = r->eng_end - r->eng_start;
            found = 1;
        }
    }
    if (found) {
        *out = e;
        return 1;
    }
    for (i = 0; i < OVL_KEEP; i++) {
        u32 eb, tb, sz;

        if (whole_overlay((int)i, &eb, &tb, &sz) && word >= tb && word - tb < sz) {
            u32 x = word - tb + eb;

            if (found && x != e) {
                return 0;
            }
            e = x;
            found = 1;
        }
    }
    if (found) {
        *out = e;
    }
    return (int)found;
}

static void translate_words(u8 *img, u32 size, int region)
{
    u32 i;

    for (i = 0; i + 4 <= size; i += 4) {
        u32 v = (u32)img[i] | (u32)img[i + 1] << 8 | (u32)img[i + 2] << 16 | (u32)img[i + 3] << 24;
        u32 e;

        if (v < 0x02000000u || v >= 0x02400000u) {
            continue;
        }
        if ((region >= 0 && to_english_in(v, region, &e)) || to_english_in(v, -1, &e)
            || to_english_any_overlay(v, &e)) {
            if (e != v) {
                img[i] = (u8)e;
                img[i + 1] = (u8)(e >> 8);
                img[i + 2] = (u8)(e >> 16);
                img[i + 3] = (u8)(e >> 24);
            }
        }
    }
}

u8 *bw_lang_relayout_arm9(const u8 *image, u32 image_size, u32 *size)
{
    u32 i, end = 0;
    u8 *out;

    if (g_map == NULL) {
        return NULL;
    }
    rows_ready();
    /* as far as the English image's rows whose bytes are in this file go
     * (the rest is .bss and the autoloads' places, not in the file) */
    for (i = 0; i < g_nrows; i++) {
        const BwLangRow *r = &g_rows[i];

        if (r->region == -1 && r->eng_start >= ARM9_BASE && r->tgt_start >= ARM9_BASE
            && r->tgt_start - ARM9_BASE < image_size && r->eng_end > end) {
            end = r->eng_end;
        }
    }
    if (end <= ARM9_BASE) {
        return NULL;
    }
    end -= ARM9_BASE;
    if (end < image_size) {
        end = image_size;
    }
    out = (u8 *)calloc(1, end);
    if (out == NULL) {
        return NULL;
    }
    free(g_tgt_arm9);
    g_tgt_arm9 = keep_copy(image, image_size);
    g_tgt_arm9_size = g_tgt_arm9 != NULL ? image_size : 0;
    relayout(-1, out, ARM9_BASE, end, image, ARM9_BASE, image_size);
    translate_words(out, end, -1);
    *size = end;
    return out;
}

u8 *bw_lang_relayout_overlay(u32 id, u32 eng_base, u32 eng_size, const u8 *file,
                             u32 file_size, u32 *size)
{
    u8 entry[8];
    u32 tgt_base;
    u8 *out;

    if (g_map == NULL || eng_size == 0) {
        return NULL;
    }
    rows_ready();
    /* the overlay's place in the cartridge: its own table (y9.bin) */
    if (read_at("y9.bin", id * 32u, entry, sizeof(entry)) != 0) {
        return NULL;
    }
    tgt_base = (u32)entry[4] | (u32)entry[5] << 8 | (u32)entry[6] << 16 | (u32)entry[7] << 24;
    out = (u8 *)calloc(1, eng_size);
    if (out == NULL) {
        return NULL;
    }
    if (id < OVL_KEEP) {
        free(g_tgt_ovl[id]);
        g_tgt_ovl[id] = keep_copy(file, file_size);
        g_tgt_ovl_base[id] = tgt_base;
        g_tgt_ovl_size[id] = g_tgt_ovl[id] != NULL ? file_size : 0;
    }
    relayout((int)id, out, eng_base, eng_size, file, tgt_base, file_size);
    translate_words(out, eng_size, (int)id);
    *size = eng_size;
    return out;
}

int bw_lang_same_address(u32 english, u32 word)
{
    u32 i;

    if (g_map == NULL) {
        return english == word;
    }
    rows_ready();
    if (english == word) {
        return 1;
    }
    /* an English address can be in several overlays (they share windows):
     * any of them holding `word` there is the same object */
    for (i = 0; i < g_nrows; i++) {
        const BwLangRow *r = &g_rows[i];

        if (own_row(r) && english >= r->eng_start && english < r->eng_end
            && english - r->eng_start + r->tgt_start == word) {
            return 1;
        }
    }
    for (i = 0; i < OVL_KEEP; i++) {
        u32 eb, tb, sz;

        if (whole_overlay((int)i, &eb, &tb, &sz) && english >= eb && english - eb <= sz
            && english - eb + tb == word) {
            return 1;
        }
    }
    /* the end of an object (a pointer one past it) */
    for (i = 0; i < g_nrows; i++) {
        const BwLangRow *r = &g_rows[i];

        if (own_row(r) && english == r->eng_end
            && r->eng_end - r->eng_start + r->tgt_start == word) {
            return 1;
        }
    }
    return 0;
}

static int to_english_in(u32 word, int region, u32 *out)
{
    u32 i;

    for (i = 0; i < g_nrows; i++) {
        const BwLangRow *r = &g_rows[i];

        if (r->region == region && own_row(r) && word >= r->tgt_start
            && word - r->tgt_start < r->eng_end - r->eng_start) {
            *out = word - r->tgt_start + r->eng_start;
            return 1;
        }
    }
    {
        u32 eb, tb, sz;

        if (whole_overlay(region, &eb, &tb, &sz) && word >= tb && word - tb < sz) {
            *out = word - tb + eb;
            return 1;
        }
    }
    return 0;
}

int bw_lang_to_english(u32 word, int region, u32 *out)
{
    if (g_map == NULL) {
        return 0;
    }
    rows_ready();
    if (region >= 0 && to_english_in(word, region, out)) {
        return 1;
    }
    return to_english_in(word, -1, out);
}

void *bw_lang_target_ptr(u32 word, int region)
{
    if (g_map == NULL) {
        return NULL;
    }
    if (region >= 0 && region < OVL_KEEP && g_tgt_ovl[region] != NULL
        && word >= g_tgt_ovl_base[region] && word - g_tgt_ovl_base[region] < g_tgt_ovl_size[region]) {
        return g_tgt_ovl[region] + (word - g_tgt_ovl_base[region]);
    }
    if (g_tgt_arm9 != NULL && word >= ARM9_BASE && word - ARM9_BASE < g_tgt_arm9_size) {
        return g_tgt_arm9 + (word - ARM9_BASE);
    }
    return NULL;
}
