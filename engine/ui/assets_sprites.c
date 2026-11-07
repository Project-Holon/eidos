/*
 * engine/ui/assets_sprites.c -- character sprites with walk frames.
 *
 * Where they are
 * --------------
 *
 * Two archives hold small 2D walking figures drawn with the console's
 * sprite hardware (the usual NITRO bundle: a character sheet NCGR, a cell
 * bank NCER that arranges hardware sprites into one picture, a cell
 * animation NANR that names the pictures of each pose, and a palette):
 *
 *   a/0/3/1  one trainer (a figure in a cap) with four directions, each a
 *            standing pose, a four frame walk and a four frame run.  One
 *            NCER (member 0), NANR (1), NCLR (4) and NCGR (5).
 *   a/0/3/2  32 smaller figures sharing one palette (member 0).  The
 *            members run: 16 NCGR, then 16 (NCER, NANR) pairs, then a
 *            second block of the same shape; the first block's figures have
 *            four standing poses, the second block's also a walk.  Each
 *            figure's own palette row is the palette bits of the first
 *            sprite of its first cell.
 *
 * The kit's `sprite` number: 0 is the trainer of a/0/3/1; 1 to 32 are the
 * figures of a/0/3/2 in member order.
 *
 * The formats
 * -----------
 *
 * NCGR (RGCN): the RAHC block's body holds the tile count in y and x (0xFFFF
 * for both when unspecified), the bit depth (3 is 4 bits per pixel), the
 * character format at +12 (1: the data is one linear picture, tile_x * 8
 * pixels wide; 0: 8x8 tiles in order), the data size at +16 and the data
 * offset at +20.  A cell's sprite names a tile by index: with a sheet of
 * unspecified width the tiles are plain 1D order and a sprite wider than
 * one tile takes its own width in tiles per row; with a real width the tile
 * index is a position in that width's grid of tiles and a sprite takes
 * `tile_x` per row.
 *
 * NCER (RECN): the KBEC block's body has the cell count and the cell record
 * type (1: 16 bytes per cell, with a bounding box; else 8), the offset of
 * the cell records, then (at +8) the mapping type: 0 to 3 mean a sprite's
 * tile number counts units of 32 << type bytes, that is, block number =
 * tile << type for a sheet of plain tiles.  (The trainer bank is type 2:
 * a 32x32 sprite is 16 tiles and its tile numbers step by 4.)  A cell record is the
 * sprite count (u16), an attribute (u16), the offset of its sprites relative
 * to the end of the cell records.  A sprite is three u16 (the console's
 * attribute words): y (8 bits, signed), shape; x (9 bits, signed), flips,
 * size; tile number, priority, palette row.  The shape and size give the
 * sprite's pixel size by the console's table.  As in the game's own
 * renderer, a flipped sprite's position is -(x + w) / -(y + h), and a
 * double-size affine sprite is moved by half its size.  Cells draw in
 * priority order, the first sprite of a cell on top.
 *
 * NANR (RNAN): the KNBA block's body has the sequence and element counts,
 * and at +4, +8, +12 the offsets of the sequence records (16 bytes each),
 * the element records (8 bytes: frame data offset, duration, 0xBEEF) and
 * the frame data.  A sequence's first element is its record's u32 at +12
 * divided by 8, and its length runs to the next sequence's first element.
 * A frame's data starts with the cell number (u16).  The LBAL block that
 * follows lists the sequences' names after one u32 per sequence.
 *
 * The kit draws every figure into one fixed canvas (the union of all the
 * bank's cells), so a figure does not jitter between frames.  The picture
 * origin is where the cells place their sprites: the figure's feet.
 */

#include <stdlib.h>
#include <string.h>

#include "assets.h"
#include "kit.h"

#define SPRITE_COUNT 34
#define SPRITE_HEROINE 33   /* a/0/3/1 members 2, 3, 6, 7: the title menu's figure for a girl */
#define DIRS 4
#define FRAMES 5            /* 0 standing, 1..4 the walk */

typedef struct Oam {
    int x, y, w, h;
    int hflip, vflip, tile, prio, pal;
} Oam;

typedef struct Cell {
    int n;
    Oam *oam;
} Cell;

typedef struct Sheet {          /* one NCGR */
    const u8 *data;
    u32 size;
    int tx, ty, fmt;
} Sheet;

static UiImage *g_img[SPRITE_COUNT][DIRS][FRAMES];
static u8 *g_pal0, *g_pal1;
static u32 g_pal0_size, g_pal1_size;
static int g_pal_tried;
static int g_walk1_done, g_walk2_done, g_heroine_done;
static u8 *g_pal_f;
static u32 g_pal_f_size;

static u32 rd16(const u8 *p) { return (u32)p[0] | ((u32)p[1] << 8); }
static u32 rd32(const u8 *p) { return rd16(p) | (rd16(p + 2) << 16); }

void ui_sprites_reset(void)
{
    int a, b, c;

    for (a = 0; a < SPRITE_COUNT; a++) {
        for (b = 0; b < DIRS; b++) {
            for (c = 0; c < FRAMES; c++) {
                if (g_img[a][b][c] != NULL) {
                    free((void *)g_img[a][b][c]->rgba);
                    free(g_img[a][b][c]);
                    g_img[a][b][c] = NULL;
                }
            }
        }
    }
    free(g_pal0);
    free(g_pal1);
    g_pal0 = g_pal1 = NULL;
    free(g_pal_f);
    g_pal_f = NULL;
    g_pal_tried = 0;
    g_walk1_done = g_walk2_done = g_heroine_done = 0;
}

/* ------------------------------------------------------------------ */
/* Resource readers                                                    */
/* ------------------------------------------------------------------ */

static int block_at(const u8 *d, u32 size, const char *magic, u32 *body, u32 *bsize)
{
    u32 pos;

    if (size < 0x18) {
        return -1;
    }
    for (pos = rd16(d + 0x0C); pos + 8 <= size;) {
        u32 s = rd32(d + pos + 4);

        if (memcmp(d + pos, magic, 4) == 0) {
            *body = pos + 8;
            *bsize = s - 8;
            return (pos + s <= size) ? 0 : -1;
        }
        if (s < 8) {
            break;
        }
        pos += s;
    }
    return -1;
}

static int sheet_parse(Sheet *s, const u8 *d, u32 size)
{
    u32 body, bsize, dsize, dofs;

    if (size < 0x20 || memcmp(d, "RGCN", 4) != 0 ||
        block_at(d, size, "RAHC", &body, &bsize) != 0 || bsize < 24) {
        return -1;
    }
    s->ty = (int)rd16(d + body);
    s->tx = (int)rd16(d + body + 2);
    if (rd32(d + body + 4) != 3) {          /* 4 bits per pixel only */
        return -1;
    }
    s->fmt = (int)rd32(d + body + 12);
    dsize = rd32(d + body + 16);
    dofs = rd32(d + body + 20);
    if (body + dofs + dsize > size) {
        return -1;
    }
    s->data = d + body + dofs;
    s->size = dsize;
    return 0;
}

/* One pixel (0..15) of the 8x8 block `block` of the sheet. */
static int sheet_px(const Sheet *s, int block, int x, int y)
{
    u32 off;
    u8 b;

    if (s->tx == 0xFFFF || s->ty == 0xFFFF) {           /* plain 1D tiles   */
        off = (u32)block * 32u + (u32)(y * 4 + x / 2);
    } else if (s->fmt == 1) {                           /* a linear picture */
        int w = s->tx * 8;
        int bx = (block % s->tx) * 8, by = (block / s->tx) * 8;

        off = (u32)(((by + y) * w + bx + x) / 2);
        if (off >= s->size) {
            return 0;
        }
        b = s->data[off];
        return (((by + y) * w + bx + x) & 1) ? (b >> 4) : (b & 15);
    } else {                                            /* tiles in order   */
        off = (u32)block * 32u + (u32)(y * 4 + x / 2);
    }
    if (off >= s->size) {
        return 0;
    }
    b = s->data[off];
    return (x & 1) ? (b >> 4) : (b & 15);
}

static const int k_oam_w[4][4] = { { 8, 16, 32, 64 }, { 16, 32, 32, 64 }, { 8, 8, 16, 32 }, { 8, 8, 8, 8 } };
static const int k_oam_h[4][4] = { { 8, 16, 32, 64 }, { 8, 8, 16, 32 }, { 16, 32, 32, 64 }, { 8, 8, 8, 8 } };

static int cells_parse(Cell **out, int *count, int *mapping, const u8 *d, u32 size)
{
    u32 body, bsize, ofs, n, info, i, arr;
    Cell *cells;

    if (size < 0x20 || memcmp(d, "RECN", 4) != 0 ||
        block_at(d, size, "KBEC", &body, &bsize) != 0) {
        return -1;
    }
    n = rd16(d + body);
    info = rd16(d + body + 2);
    ofs = rd32(d + body + 4);
    arr = ofs + n * (info == 1 ? 16u : 8u);
    if (n == 0 || n > 512 || arr > bsize) {
        return -1;
    }
    cells = (Cell *)calloc(n, sizeof *cells);
    if (cells == NULL) {
        return -1;
    }
    for (i = 0; i < n; i++) {
        const u8 *rec = d + body + ofs + i * (info == 1 ? 16u : 8u);
        u32 num = rd16(rec), oofs = rd32(rec + 4), j;

        if (arr + oofs + num * 6 > bsize) {
            num = 0;
        }
        cells[i].n = (int)num;
        cells[i].oam = (Oam *)calloc(num + 1, sizeof(Oam));
        if (cells[i].oam == NULL) {
            cells[i].n = 0;
            continue;
        }
        for (j = 0; j < num; j++) {
            const u8 *o = d + body + arr + oofs + j * 6;
            u32 a0 = rd16(o), a1 = rd16(o + 2), a2 = rd16(o + 4);
            Oam *m = &cells[i].oam[j];
            int y = (int)(a0 & 0xFF), x = (int)(a1 & 0x1FF);
            int rot = (int)((a0 >> 8) & 1);
            int shape = (int)((a0 >> 14) & 3), sz = (int)((a1 >> 14) & 3);

            m->y = y >= 128 ? y - 256 : y;
            m->x = x >= 256 ? x - 512 : x;
            m->w = k_oam_w[shape][sz];
            m->h = k_oam_h[shape][sz];
            m->hflip = rot ? 0 : (int)((a1 >> 12) & 1);
            m->vflip = rot ? 0 : (int)((a1 >> 13) & 1);
            m->tile = (int)(a2 & 0x3FF);
            m->prio = (int)((a2 >> 10) & 3);
            m->pal = (int)((a2 >> 12) & 15);
            if (((a0 >> 8) & 3) == 3) {                 /* double-size affine */
                m->x += m->w / 2;
                m->y += m->h / 2;
            }
            if (m->hflip) {
                m->x = -(m->x + m->w);
            }
            if (m->vflip) {
                m->y = -(m->y + m->h);
            }
        }
    }
    *out = cells;
    *count = (int)n;
    *mapping = (int)rd32(d + body + 8);
    return 0;
}

static void cells_free(Cell *cells, int n)
{
    int i;

    for (i = 0; i < n; i++) {
        free(cells[i].oam);
    }
    free(cells);
}

/* The cells of a named sequence (up to max); returns how many, 0 if none. */
static int nanr_sequence(const u8 *d, u32 size, const char *name, u16 *cells, int max)
{
    u32 body, bsize, nseq, nel, seq_ofs, el_ofs, el_end, i, lbody, lsize, start, end, k;
    const u8 *names;
    int idx = -1, n = 0;

    if (size < 0x20 || memcmp(d, "RNAN", 4) != 0 ||
        block_at(d, size, "KNBA", &body, &bsize) != 0) {
        return 0;
    }
    nseq = rd16(d + body);
    nel = rd16(d + body + 2);
    seq_ofs = rd32(d + body + 4);
    el_ofs = rd32(d + body + 8);
    el_end = rd32(d + body + 12);
    if (nseq == 0 || nseq > 64 || seq_ofs + nseq * 16 > bsize || el_ofs + nel * 8 > bsize ||
        el_end > bsize) {
        return 0;
    }
    if (block_at(d, size, "LBAL", &lbody, &lsize) != 0 || nseq * 4 > lsize) {
        return 0;
    }
    names = d + lbody + nseq * 4;
    for (i = 0; i < nseq; i++) {
        size_t room = (size_t)(d + lbody + lsize - names), l = 0;

        while (l < room && names[l] != 0) {
            l++;                        /* strnlen, which C99 lacks */
        }

        if (strcmp((const char *)names, name) == 0) {
            idx = (int)i;
        }
        names += l + 1;
    }
    if (idx < 0) {
        return 0;
    }
    start = rd32(d + body + seq_ofs + (u32)idx * 16 + 12) / 8;
    end = nel;
    for (i = 0; i < nseq; i++) {
        u32 s = rd32(d + body + seq_ofs + i * 16 + 12) / 8;

        if (s > start && s < end) {
            end = s;
        }
    }
    for (k = start; k < end && n < max; k++) {
        u32 fofs = rd32(d + body + el_ofs + k * 8);

        if (el_end + fofs + 2 > bsize) {
            break;
        }
        cells[n++] = (u16)rd16(d + body + el_end + fofs);
    }
    return n;
}

/* ------------------------------------------------------------------ */
/* Drawing                                                             */
/* ------------------------------------------------------------------ */

typedef struct Bank {
    const Sheet *sheet;
    Cell *cells;
    int ncells;
    int minx, miny, w, h;       /* the union of every cell's sprites       */
    const u16 *pal;             /* colours (BGR555), count in npal         */
    int npal, subpal;
    int mapping;                /* the cell bank's mapping type            */
} Bank;

static void bank_bounds(Bank *b)
{
    int minx = 1 << 20, miny = 1 << 20, maxx = -(1 << 20), maxy = -(1 << 20), i, j;

    for (i = 0; i < b->ncells; i++) {
        for (j = 0; j < b->cells[i].n; j++) {
            const Oam *o = &b->cells[i].oam[j];

            if (o->x < minx) {
                minx = o->x;
            }
            if (o->y < miny) {
                miny = o->y;
            }
            if (o->x + o->w > maxx) {
                maxx = o->x + o->w;
            }
            if (o->y + o->h > maxy) {
                maxy = o->y + o->h;
            }
        }
    }
    if (maxx <= minx || maxy <= miny) {
        minx = miny = 0;
        maxx = maxy = 1;
    }
    b->minx = minx;
    b->miny = miny;
    b->w = maxx - minx;
    b->h = maxy - miny;
}

static UiImage *draw_cell(const Bank *b, int cell)
{
    u8 *rgba = (u8 *)calloc((size_t)b->w * (size_t)b->h, 4);
    UiImage *im;
    int order[64], n, i, j, k;
    const Cell *c;

    if (rgba == NULL || cell < 0 || cell >= b->ncells) {
        free(rgba);
        return NULL;
    }
    c = &b->cells[cell];
    n = c->n < 64 ? c->n : 64;
    for (i = 0; i < n; i++) {
        order[i] = i;
    }
    /* back to front: a lower priority number is in front, a lower index is
     * in front within one priority, so paint the highest of both first */
    for (i = 0; i < n; i++) {
        for (j = i + 1; j < n; j++) {
            const Oam *a = &c->oam[order[i]], *d = &c->oam[order[j]];

            if (d->prio > a->prio || (d->prio == a->prio && order[j] > order[i])) {
                k = order[i];
                order[i] = order[j];
                order[j] = k;
            }
        }
    }
    for (i = 0; i < n; i++) {
        const Oam *o = &c->oam[order[i]];
        int tw = o->w / 8, ty, tx, y, x;
        int plain = b->sheet->tx == 0xFFFF;
        int stride = plain ? tw : b->sheet->tx;
        int first = plain && b->mapping < 4 ? o->tile << b->mapping : o->tile;

        for (ty = 0; ty < o->h / 8; ty++) {
            for (tx = 0; tx < tw; tx++) {
                int block = first + ty * stride + tx;

                for (y = 0; y < 8; y++) {
                    for (x = 0; x < 8; x++) {
                        int v = sheet_px(b->sheet, block, x, y);
                        int px = tx * 8 + x, py = ty * 8 + y, dx, dy, col;
                        u8 *d;

                        if (v == 0) {
                            continue;
                        }
                        if (o->hflip) {
                            px = o->w - 1 - px;
                        }
                        if (o->vflip) {
                            py = o->h - 1 - py;
                        }
                        dx = o->x - b->minx + px;
                        dy = o->y - b->miny + py;
                        if (dx < 0 || dy < 0 || dx >= b->w || dy >= b->h) {
                            continue;
                        }
                        col = b->subpal * 16 + v;
                        d = rgba + ((size_t)dy * (size_t)b->w + (size_t)dx) * 4;
                        if (col >= b->npal) {
                            col = v < b->npal ? v : 0;
                        }
                        d[0] = (u8)(((b->pal[col] & 31) * 255) / 31);
                        d[1] = (u8)((((b->pal[col] >> 5) & 31) * 255) / 31);
                        d[2] = (u8)((((b->pal[col] >> 10) & 31) * 255) / 31);
                        d[3] = 255;
                    }
                }
            }
        }
    }
    im = (UiImage *)malloc(sizeof *im);
    if (im == NULL) {
        free(rgba);
        return NULL;
    }
    im->w = b->w;
    im->h = b->h;
    im->rgba = rgba;
    return im;
}

/* The palette colours of an NCLR member (BGR555), a pointer into `d`. */
static const u16 *nclr_colors(const u8 *d, u32 size, int *count)
{
    u32 body, bsize, dsize, cofs;

    if (size < 0x20 || memcmp(d, "RLCN", 4) != 0 ||
        block_at(d, size, "TTLP", &body, &bsize) != 0 || bsize < 16) {
        return NULL;
    }
    dsize = rd32(d + body + 8);
    cofs = rd32(d + body + 12);
    if (body + cofs + dsize > size) {
        return NULL;
    }
    *count = (int)(dsize / 2);
    return (const u16 *)(const void *)(d + body + cofs);
}

/* Build the images of one figure: every direction, the standing pose and
 * the walk (when it has one). */
static void build_figure(int sprite, int ncer_m, int nanr_m, int ncgr_m, int arc,
                         const u8 *pal_data, u32 pal_size, int subpal_from_cell)
{
    u32 s1 = 0, s2 = 0, s3 = 0;
    u8 *ncer = ui_arc_member(arc, (u32)ncer_m, &s1);
    u8 *nanr = ui_arc_member(arc, (u32)nanr_m, &s2);
    u8 *ncgr = ui_arc_member(arc, (u32)ncgr_m, &s3);
    Sheet sheet;
    Bank bank;
    static const char *const stand[DIRS] = { "bottom", "top", "left", "right" };
    static const char *const walk[DIRS] = { "bottom_loop", "top_loop", "left_loop", "right_loop" };
    u16 seq[8];
    int d, f, npal = 0;
    u16 *pal_copy = NULL;
    const u16 *pc;

    memset(&bank, 0, sizeof bank);
    if (ncer == NULL || nanr == NULL || ncgr == NULL || sheet_parse(&sheet, ncgr, s3) != 0 ||
        cells_parse(&bank.cells, &bank.ncells, &bank.mapping, ncer, s1) != 0) {
        goto done;
    }
    pc = nclr_colors(pal_data, pal_size, &npal);
    if (pc == NULL || npal < 16) {
        goto done;
    }
    pal_copy = (u16 *)malloc((size_t)npal * sizeof(u16));
    if (pal_copy == NULL) {
        goto done;
    }
    memcpy(pal_copy, pc, (size_t)npal * sizeof(u16));
    bank.sheet = &sheet;
    bank.pal = pal_copy;
    bank.npal = npal;
    bank.subpal = (subpal_from_cell && bank.ncells > 0 && bank.cells[0].n > 0) ?
                  bank.cells[0].oam[0].pal : 0;
    bank_bounds(&bank);
    for (d = 0; d < DIRS; d++) {
        int n = nanr_sequence(nanr, s2, stand[d], seq, 8);

        g_img[sprite][d][0] = n > 0 ? draw_cell(&bank, seq[0]) : NULL;
        n = nanr_sequence(nanr, s2, walk[d], seq, 8);
        for (f = 0; f < 4 && f < n; f++) {
            g_img[sprite][d][1 + f] = draw_cell(&bank, seq[f]);
        }
    }
done:
    if (bank.cells != NULL) {
        cells_free(bank.cells, bank.ncells);
    }
    free(pal_copy);
    free(ncer);
    free(nanr);
    free(ncgr);
}

/* The member numbers of a/0/3/2's figures: runs of NCGR, then as many
 * (NCER, NANR) pairs. */
static int walk2_figures(int ncgr[32], int ncer[32], int nanr[32])
{
    u32 members, i, count = 0;
    u32 size;
    u8 *m;
    int kind[256];

    /* classify every member by its magic, reading only the first bytes */
    members = 0;
    for (i = 0; i < 256; i++) {
        m = ui_arc_member(UI_ARC_WALK2, i, &size);
        if (m == NULL) {
            break;
        }
        kind[i] = memcmp(m, "RGCN", 4) == 0 ? 1 : memcmp(m, "RECN", 4) == 0 ? 2 :
                  memcmp(m, "RNAN", 4) == 0 ? 3 : 0;
        free(m);
        members++;
    }
    for (i = 0; i < members && count < 32;) {
        u32 g0, run = 0, p;

        if (kind[i] != 1) {
            i++;
            continue;
        }
        g0 = i;
        while (i < members && kind[i] == 1) {
            run++;
            i++;
        }
        for (p = 0; p < run && i + 1 < members && kind[i] == 2 && kind[i + 1] == 3 &&
                    count < 32; p++) {
            ncgr[count] = (int)(g0 + p);
            ncer[count] = (int)i;
            nanr[count] = (int)(i + 1);
            count++;
            i += 2;
        }
    }
    return (int)count;
}

static void sprites_ensure(int sprite)
{
    if (!g_pal_tried) {
        g_pal_tried = 1;
        g_pal0 = ui_arc_member(UI_ARC_WALK1, 4, &g_pal0_size);
        g_pal1 = ui_arc_member(UI_ARC_WALK2, 0, &g_pal1_size);
        g_walk1_done = g_walk2_done = 0;
    }
    if (sprite == SPRITE_HEROINE) {
        if (!g_heroine_done) {
            g_heroine_done = 1;
            g_pal_f = ui_arc_member(UI_ARC_WALK1, 6, &g_pal_f_size);
            if (g_pal_f != NULL) {
                build_figure(SPRITE_HEROINE, 2, 3, 7, UI_ARC_WALK1, g_pal_f, g_pal_f_size, 0);
            }
        }
    } else if (sprite == 0 && !g_walk1_done) {
        g_walk1_done = 1;
        if (g_pal0 != NULL) {
            build_figure(0, 0, 1, 5, UI_ARC_WALK1, g_pal0, g_pal0_size, 0);
        }
    } else if (sprite > 0 && sprite < SPRITE_HEROINE && !g_walk2_done) {
        int ncgr[32], ncer[32], nanr[32], n, i;

        g_walk2_done = 1;
        n = walk2_figures(ncgr, ncer, nanr);
        for (i = 0; i < n && g_pal1 != NULL; i++) {
            build_figure(1 + i, ncer[i], nanr[i], ncgr[i], UI_ARC_WALK2, g_pal1, g_pal1_size, 1);
        }
    }
}

const UiImage *ui_overworld(u16 sprite, int dir, int frame)
{
    const UiImage *im;

    if (sprite >= SPRITE_COUNT || dir < 0 || dir >= DIRS) {
        return NULL;
    }
    sprites_ensure(sprite);
    if (frame < 0) {
        return g_img[sprite][dir][0];
    }
    im = g_img[sprite][dir][1 + (frame & 3)];
    return im != NULL ? im : g_img[sprite][dir][0];
}
