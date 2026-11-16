/*
 * engine/avatar/avatar_mask.c -- which pixels of a shared palette entry
 * belong to a part, picture by picture (avatar.h, "How a look is drawn").
 *
 * The rules read the picture itself (where the cap is, where the shoes
 * are, what touches what), so they hold for every frame of every field
 * form.  They are the rules the recolour research reviewed frame by frame
 * (every texture of all nine field forms, the hair and skin leftovers
 * painted magenta to find any pixel a rule missed).
 *
 * Hilbert (field):
 *   - hair: entry 11 down to 4 rows below the cap; the dark 8 seen through
 *     the opening at the back of the cap (an 8 patch bordered only by cap
 *     white and grey); 8 beside that hair near the cap's lower edge, unless
 *     it touches skin;
 *   - trousers: entries 1, 2, 8 and 14 in the 3 rows above the shoe tops,
 *     minus pixels touching the bag; on the bicycle (and the save pose,
 *     which shares its palette) the legs share every entry with the bike,
 *     so the trousers stay;
 *   - skin: entry 11 that is not hair and touches skin (the hands' outline).
 * Hilda (field):
 *   - hair: grown from 1 and 2 at head height through 1, 2 and 8, at most
 *     9 rows below the cap and never through the cap's own outline; the
 *     long hair beside the face taken by place; dark pixels below the head
 *     that touch the pink bag or strap are the bag's edge.
 */
#include <stdlib.h>
#include <string.h>

#include "avatar_int.h"


#define M_HAIR  (AVATAR_PART_HAIR + 1)
#define M_PANTS (AVATAR_PART_PANTS + 1)
#define M_SKIN  (AVATAR_PART_SKIN + 1)
/* a pixel drawn in the hair's darkest shade (the hair's extra entry of index
 * 8): the cap's dark red where it shades Hilbert's hair */
#define M_HAIR_DARK AVATAR_MASK_HAIR_DARK

static const int N4[4][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
static const int N8[8][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 },
                              { 1, 1 }, { 1, -1 }, { -1, 1 }, { -1, -1 } };

typedef struct Pic {
    const u8 *r;
    int w, h;
    /* scratch on the heap (the 32-bit image has little room for statics) */
    int *comp, *order, *start, *queue;
    u8 *cap, *seen;
} Pic;

/* the value at (x, y); 0 outside, as the research's neighbour walk */
static int at(const Pic *p, int x, int y)
{
    return x >= 0 && y >= 0 && x < p->w && y < p->h ? p->r[y * p->w + x] : 0;
}

static int in_set(int v, u32 set)
{
    return v >= 0 && v < 32 && ((set >> v) & 1u);
}

#define BIT(i) (1u << (i))

/* how many 4-neighbours hold a value of the set */
static int count_nb4(const Pic *p, int x, int y, u32 set)
{
    int k, n = 0;

    for (k = 0; k < 4; k++) {
        n += in_set(at(p, x + N4[k][0], y + N4[k][1]), set);
    }
    return n;
}

/*
 * Connected components of the pixels whose value is in `set`, in the
 * order the research finds them (rows, then columns of the first pixel):
 * comp[] gets each pixel's component number (-1 none); answers how many.
 * order[] lists the pixels component by component; start[c] is where
 * component c begins in it, start[count] the end.
 */
static int comps(const Pic *p, u32 set, int eight, int *comp, int *order, int *start)
{
    int n = 0, used = 0, x, y, i, qh;
    const int (*nb)[2] = eight ? N8 : N4;
    int nn = eight ? 8 : 4;

    for (i = 0; i < p->w * p->h; i++) {
        comp[i] = -1;
    }
    for (y = 0; y < p->h; y++) {
        for (x = 0; x < p->w; x++) {
            if (!in_set(p->r[y * p->w + x], set) || comp[y * p->w + x] >= 0) {
                continue;
            }
            start[n] = used;
            comp[y * p->w + x] = n;
            order[used++] = y * p->w + x;
            for (qh = start[n]; qh < used; qh++) {
                int cx = order[qh] % p->w, cy = order[qh] / p->w, k;

                for (k = 0; k < nn; k++) {
                    int xx = cx + nb[k][0], yy = cy + nb[k][1];

                    if (xx >= 0 && yy >= 0 && xx < p->w && yy < p->h && comp[yy * p->w + xx] < 0 &&
                        in_set(p->r[yy * p->w + xx], set)) {
                        comp[yy * p->w + xx] = n;
                        order[used++] = yy * p->w + xx;
                    }
                }
            }
            n++;
        }
    }
    start[n] = used;
    return n;
}

/* The cap: the topmost component (4-connected) of the cap's colours with
 * at least 12 pixels; its pixels marked in `cap` (when given) and its
 * lowest row answered, -1 when there is none. */
static int find_cap(const Pic *p, u32 capset, u8 *cap)
{
    int *comp = p->comp, *order = p->order, *start = p->start;
    int n = comps(p, capset, 0, comp, order, start), c, best = -1, best_top = 1 << 30, i, bottom = -1;

    for (c = 0; c < n; c++) {
        int top = 1 << 30;

        if (start[c + 1] - start[c] < 12) {
            continue;
        }
        for (i = start[c]; i < start[c + 1]; i++) {
            top = order[i] / p->w < top ? order[i] / p->w : top;
        }
        if (top < best_top) {
            best_top = top;
            best = c;
        }
    }
    if (best < 0) {
        return -1;
    }
    for (i = start[best]; i < start[best + 1]; i++) {
        int y = order[i] / p->w;

        bottom = y > bottom ? y : bottom;
        if (cap != NULL) {
            cap[order[i]] = 1;
        }
    }
    return bottom;
}

static void field_hero(const Pic *p, u8 *m, int cycle)
{
    int *comp = p->comp, *order = p->order, *start = p->start;
    int cy1 = find_cap(p, BIT(3) | BIT(4) | BIT(13) | BIT(14) | BIT(9), NULL), x, y, n, c, i;

    if (cy1 < 0) {
        return;
    }
    for (y = 0; y < p->h; y++) {
        for (x = 0; x < p->w; x++) {
            if (at(p, x, y) == 11 && y <= cy1 + 4) {
                m[y * p->w + x] = M_HAIR;
            }
        }
    }
    /* the opening at the back of the cap: an 8 patch bordered only by cap
     * white and grey; the hair is seen through it */
    n = comps(p, BIT(8), 0, comp, order, start);
    for (c = 0; c < n; c++) {
        int ok = 1, any = 0;

        if (start[c + 1] - start[c] < 3) {
            continue;
        }
        for (i = start[c]; i < start[c + 1] && ok; i++) {
            int px = order[i] % p->w, py = order[i] / p->w, k;

            for (k = 0; k < 4; k++) {
                int xx = px + N4[k][0], yy = py + N4[k][1], v = at(p, xx, yy);
                int inside = xx >= 0 && yy >= 0 && xx < p->w && yy < p->h && comp[yy * p->w + xx] == c;

                if (inside) {
                    continue;
                }
                any = 1;
                if (v != 13 && v != 14 && v != 9) {
                    ok = 0;
                    break;
                }
            }
        }
        if (ok && any) {
            for (i = start[c]; i < start[c + 1]; i++) {
                m[order[i]] = M_HAIR;
            }
        }
    }
    /* the trousers: the 3 rows above the shoe tops, minus the bag's edge */
    if (!cycle) {
        int bottom = -1, ys0 = 1 << 30;

        for (y = 0; y < p->h; y++) {
            for (x = 0; x < p->w; x++) {
                if (at(p, x, y) != 0) {
                    bottom = y;
                    break;
                }
            }
        }
        for (y = bottom - 4; bottom >= 0 && y <= bottom; y++) {
            for (x = 0; x < p->w && y >= 0; x++) {
                int v = at(p, x, y);

                if ((v == 3 || v == 4) && y < ys0) {
                    ys0 = y;
                }
            }
        }
        if (ys0 != (1 << 30)) {
            int y0 = cy1 + 6 > ys0 - 3 ? cy1 + 6 : ys0 - 3;

            for (y = y0; y < ys0; y++) {
                for (x = 0; x < p->w; x++) {
                    int v = at(p, x, y);

                    if ((v == 1 || v == 2 || v == 8 || v == 14) && count_nb4(p, x, y, BIT(5) | BIT(9)) == 0) {
                        m[y * p->w + x] = M_PANTS;
                    }
                }
            }
        }
    }
    /* 8 beside the hair near the cap's lower edge, unless it touches skin
     * (the scan sees the pixels it marked before, as the research's does) */
    for (y = 0; y < p->h; y++) {
        for (x = 0; x < p->w; x++) {
            if (at(p, x, y) == 8 && y >= cy1 - 4 && y <= cy1 + 4) {
                int k, hair_n = 0;

                for (k = 0; k < 4; k++) {
                    int xx = x + N4[k][0], yy = y + N4[k][1];

                    if (xx >= 0 && yy >= 0 && xx < p->w && yy < p->h && m[yy * p->w + xx] == M_HAIR) {
                        hair_n++;
                    }
                }
                if (hair_n >= 1 && count_nb4(p, x, y, BIT(12) | BIT(10)) == 0) {
                    m[y * p->w + x] = M_HAIR;
                }
            }
        }
    }
    /* skin: 11 that is not hair and touches skin (the hands' outline) */
    for (y = 0; y < p->h; y++) {
        for (x = 0; x < p->w; x++) {
            if (at(p, x, y) == 11 && m[y * p->w + x] == 0 && count_nb4(p, x, y, BIT(12) | BIT(10)) > 0) {
                m[y * p->w + x] = M_SKIN;
            }
        }
    }
    /* the cap's dark red (3, 4) also shades the hair at the sides of the
     * head: a dot of at most 3 pixels, cut off from the cap, touching the
     * hair, by the cap's lower edge.  In the game's brown it reads as
     * shading; in an outfit colour it would be a spot in the hair, so it
     * takes the hair's darkest shade (M_HAIR_DARK) */
    n = comps(p, BIT(3) | BIT(4), 0, comp, order, start);
    for (c = 0; c < n; c++) {
        int touch = 0, near_cap = 1;

        if (start[c + 1] - start[c] > 3) {
            continue;
        }
        for (i = start[c]; i < start[c + 1]; i++) {
            int px = order[i] % p->w, py = order[i] / p->w, k;

            near_cap &= py >= cy1 - 4 && py <= cy1 + 4;
            for (k = 0; k < 4; k++) {
                int xx = px + N4[k][0], yy = py + N4[k][1];

                touch |= at(p, xx, yy) == 11 ||
                         (xx >= 0 && yy >= 0 && xx < p->w && yy < p->h && m[yy * p->w + xx] == M_HAIR);
            }
        }
        if (touch && near_cap) {
            for (i = start[c]; i < start[c + 1]; i++) {
                m[order[i]] = M_HAIR_DARK;
            }
        }
    }
}

#define HEROINE_FIELD_L 9

static void field_heroine(const Pic *p, u8 *m)
{
    u8 *cap = p->cap, *seen = p->seen;
    int *queue = p->queue;
    int cy1, x, y, qh = 0, qt = 0, k;

    memset(cap, 0, (size_t)(p->w * p->h));
    cy1 = find_cap(p, BIT(13) | BIT(14) | BIT(9), cap);
    if (cy1 < 0) {
        return;
    }
    memset(seen, 0, (size_t)(p->w * p->h));
    for (y = 0; y < p->h; y++) {
        for (x = 0; x < p->w; x++) {
            int v = at(p, x, y);

            if ((v == 1 || v == 2) && y <= cy1 + 1) {
                seen[y * p->w + x] = 1;
                queue[qt++] = y * p->w + x;
            }
        }
    }
    /* grown through 1, 2 and 8, never through the cap's own outline */
    while (qh < qt) {
        int i = queue[qh++], cx = i % p->w, cy = i / p->w;

        m[i] = M_HAIR;
        for (k = 0; k < 8; k++) {
            int xx = cx + N8[k][0], yy = cy + N8[k][1], v, j, by_cap = 0;

            if (xx < 0 || yy < 0 || xx >= p->w || yy >= p->h || seen[yy * p->w + xx]) {
                continue;
            }
            v = p->r[yy * p->w + xx];
            if ((v != 1 && v != 2 && v != 8) || yy > cy1 + HEROINE_FIELD_L) {
                continue;
            }
            for (j = 0; j < 4 && v == 8; j++) {
                int ax = xx + N4[j][0], ay = yy + N4[j][1];

                by_cap |= ax >= 0 && ay >= 0 && ax < p->w && ay < p->h && cap[ay * p->w + ax];
            }
            if (by_cap) {
                continue;           /* the cap's own outline */
            }
            seen[yy * p->w + xx] = 1;
            queue[qt++] = yy * p->w + xx;
        }
    }
    /* the long hair beside the face, cut off from the hair on top by the
     * cap's edge: taken by place, left or right of the face skin */
    {
        int fx0 = 1 << 30, fx1 = -1, ychin = -1, y0 = cy1 - 3 > 0 ? cy1 - 3 : 0;
        int y1 = cy1 + 9 < p->h ? cy1 + 9 : p->h;

        for (y = y0; y < y1; y++) {
            for (x = 0; x < p->w; x++) {
                if (at(p, x, y) == 12) {
                    fx0 = x < fx0 ? x : fx0;
                    fx1 = x > fx1 ? x : fx1;
                    ychin = y > ychin ? y : ychin;
                }
            }
        }
        if (fx1 >= 0) {
            int ya = cy1 - 2 > 0 ? cy1 - 2 : 0, yb = ychin + 4 < p->h ? ychin + 4 : p->h;

            for (y = ya; y < yb; y++) {
                for (x = 0; x < p->w; x++) {
                    int v = at(p, x, y), j, by_cap = 0;

                    if ((v != 1 && v != 2 && v != 8) || (x >= fx0 && x <= fx1) || m[y * p->w + x] != 0) {
                        continue;
                    }
                    for (j = 0; j < 4 && v == 8; j++) {
                        int ax = x + N4[j][0], ay = y + N4[j][1];

                        by_cap |= ax >= 0 && ay >= 0 && ax < p->w && ay < p->h && cap[ay * p->w + ax];
                    }
                    if (!by_cap) {
                        m[y * p->w + x] = M_HAIR;
                    }
                }
            }
        }
    }
    /* below the head, dark pixels that touch the pink bag or strap are the
     * bag's edge */
    for (y = 0; y < p->h; y++) {
        for (x = 0; x < p->w; x++) {
            if (m[y * p->w + x] != 0 && y > cy1 + 4 && count_nb4(p, x, y, BIT(5) | BIT(6) | BIT(7)) > 0) {
                m[y * p->w + x] = 0;
            }
        }
    }
}

/* ------------------------------------------------------------------ */
/* The battle's back (the MCSS texture, 256 x 128)                     */
/* ------------------------------------------------------------------ */

/* The body's cells (0, 5 and 6) lie left of x 192 in every back block;
 * the arms and hands (cells 1 to 4 and 7) start there.  Hair and trousers
 * are only ever on the body. */
#define BACK_BODY_X 192

#define M_ACCENT (AVATAR_PART_ACCENT + 1)

/* the extent of a component: its lowest and highest x and y */
static void comp_box(const Pic *p, const int *order, int a, int b, int *x0, int *y0, int *x1, int *y1)
{
    int i;

    *x0 = *y0 = 1 << 30;
    *x1 = *y1 = -1;
    for (i = a; i < b; i++) {
        int x = order[i] % p->w, y = order[i] / p->w;

        *x0 = x < *x0 ? x : *x0;
        *y0 = y < *y0 ? y : *y0;
        *x1 = x > *x1 ? x : *x1;
        *y1 = y > *y1 ? y : *y1;
    }
}

static void back_hero(const Pic *p, u8 *m)
{
    int *comp = p->comp, *order = p->order, *start = p->start;
    int n, c, i, x0, y0, x1, y1;

    /* the trousers: the 2/8 areas that reach the texture's bottom */
    n = comps(p, BIT(2) | BIT(8), 1, comp, order, start);
    for (c = 0; c < n; c++) {
        comp_box(p, order, start[c], start[c + 1], &x0, &y0, &x1, &y1);
        if (y1 >= p->h - 4) {
            for (i = start[c]; i < start[c + 1]; i++) {
                m[order[i]] = M_PANTS;
            }
        }
    }
    /* by hand: in the windup figure the bag touches the leg, left of the
     * outline column (same in blocks 0 and 4) */
    for (i = 0; i < p->w * p->h; i++) {
        int x = i % p->w, y = i / p->w;

        if (m[i] == M_PANTS && x >= 56 && ((y >= 80 && y < 93 && x <= 62) || (y >= 93 && y <= 112 && x <= 66))) {
            m[i] = 0;
        }
    }
    /* the hair: big 7/11 areas, and the hair seen through the opening at
     * the back of the cap (a small area touching no red band, bordered
     * mostly by the cap) */
    n = comps(p, BIT(7) | BIT(11), 1, comp, order, start);
    for (c = 0; c < n; c++) {
        int size = start[c + 1] - start[c], border = 0, capish = 0, red = 0;

        if (size < 5) {
            continue;
        }
        if (size < 40) {
            for (i = start[c]; i < start[c + 1]; i++) {
                int px = order[i] % p->w, py = order[i] / p->w, k;

                for (k = 0; k < 4; k++) {
                    int v = at(p, px + N4[k][0], py + N4[k][1]);

                    if (v == 7 || v == 11) {
                        continue;
                    }
                    border++;
                    capish += v == 13 || v == 8 || v == 9 || v == 10 || v == 12;
                    red |= v == 15;
                }
            }
            if (red || border == 0 || capish * 10 < border * 6) {
                continue;
            }
        }
        for (i = start[c]; i < start[c + 1]; i++) {
            m[order[i]] = M_HAIR;
        }
    }
}

static void back_heroine(const Pic *p, u8 *m)
{
    int *comp = p->comp, *order = p->order, *start = p->start;
    int n, c, i, x0, y0, x1, y1;

    n = comps(p, BIT(6) | BIT(7), 1, comp, order, start);
    for (c = 0; c < n; c++) {
        int size = start[c + 1] - start[c], ok = size >= 60;

        if (!ok && size >= 4) {
            comp_box(p, order, start[c], start[c + 1], &x0, &y0, &x1, &y1);
            if (y1 < 80) {
                /* a strand above the hips that touches no pink */
                ok = 1;
                for (i = start[c]; i < start[c + 1] && ok; i++) {
                    int px = order[i] % p->w, py = order[i] / p->w, k;

                    for (k = 0; k < 8; k++) {
                        int v = at(p, px + N8[k][0], py + N8[k][1]);

                        if (v == 1 || v == 2) {
                            ok = 0;
                            break;
                        }
                    }
                }
            }
        }
        if (ok) {
            for (i = start[c]; i < start[c + 1]; i++) {
                m[order[i]] = M_HAIR;
            }
        }
    }
    /* by hand: the bag at the hip in the windup figure (blocks 1 and 5) */
    for (i = 0; i < p->w * p->h; i++) {
        int x = i % p->w, y = i / p->w;

        if (x >= 56 && x <= 79 && y >= 87 && y <= 110) {
            m[i] = 0;
        }
    }
    /* the grey shine bands inside the hair: a grey area whose border is
     * mostly hair */
    n = comps(p, BIT(10) | BIT(11) | BIT(12) | BIT(13), 1, comp, order, start);
    for (c = 0; c < n; c++) {
        int hair = 0, other = 0;

        for (i = start[c]; i < start[c + 1]; i++) {
            int px = order[i] % p->w, py = order[i] / p->w, k;

            for (k = 0; k < 4; k++) {
                int xx = px + N4[k][0], yy = py + N4[k][1], j;

                if (xx < 0 || yy < 0 || xx >= p->w || yy >= p->h) {
                    continue;
                }
                j = yy * p->w + xx;
                if (comp[j] == c || p->r[j] == 0) {
                    continue;
                }
                if (m[j] == M_HAIR) {
                    hair++;
                } else if (p->r[j] != 14) {
                    other++;
                }
            }
        }
        if (hair + other > 0 && hair * 10 >= 7 * (hair + other)) {
            for (i = start[c]; i < start[c + 1]; i++) {
                m[order[i]] = M_HAIR;
            }
        }
    }
}

/*
 * Hands drawn in shadow (the far hand of the throw, shaded fingers): grey
 * areas no part took, of 6 to 90 pixels and at least 3 each way, touching
 * no `never` colour, whose border is at least 70 % outline or skin.
 * `light`: at least 40 % of the area must be those shades (keeps bags and
 * straps out).  The limbs found are marked in `found`.
 */
static void shaded_limbs(const Pic *p, const u8 *m, const u8 *taken, u8 *found, u32 grey, u32 skin, u32 outline,
                         u32 never, u32 other, u32 light)
{
    int *comp = p->comp, *order = p->order, *start = p->start;
    int n = comps(p, grey, 1, comp, order, start), c, i;
    u8 *in_c = p->seen;

    for (c = 0; c < n; c++) {
        int cnt = 0, nl = 0, x0 = 1 << 30, y0 = 1 << 30, x1 = -1, y1 = -1, good = 0, bad = 0, stop = 0;

        for (i = start[c]; i < start[c + 1]; i++) {
            int j = order[i];

            in_c[j] = (u8)(m[j] == 0 && !taken[j]);
            if (in_c[j]) {
                int x = j % p->w, y = j / p->w;

                cnt++;
                nl += in_set(p->r[j], light);
                x0 = x < x0 ? x : x0;
                y0 = y < y0 ? y : y0;
                x1 = x > x1 ? x : x1;
                y1 = y > y1 ? y : y1;
            }
        }
        if (cnt >= 6 && cnt <= 90 && x1 - x0 >= 2 && y1 - y0 >= 2 && (light == 0 || nl * 10 >= cnt * 4)) {
            for (i = start[c]; i < start[c + 1]; i++) {
                int j = order[i], px = j % p->w, py = j / p->w, k;

                if (!in_c[j]) {
                    continue;
                }
                for (k = 0; k < 4; k++) {
                    int xx = px + N4[k][0], yy = py + N4[k][1], jj, v;

                    if (xx < 0 || yy < 0 || xx >= p->w || yy >= p->h) {
                        continue;
                    }
                    jj = yy * p->w + xx;
                    if (comp[jj] == c && in_c[jj]) {
                        continue;
                    }
                    v = p->r[jj];
                    if (in_set(v, never)) {
                        stop = 1;
                    } else if (in_set(v, skin) || in_set(v, outline) || taken[jj] || m[jj] == M_SKIN) {
                        good++;
                    } else if (v == 0) {
                        good++;
                    } else if (in_set(v, other) || in_set(v, grey)) {
                        bad++;
                    }
                }
            }
            if (!stop && good * 10 >= 7 * (good + bad)) {
                for (i = start[c]; i < start[c + 1]; i++) {
                    if (in_c[order[i]]) {
                        found[order[i]] = 1;
                    }
                }
            }
        }
        for (i = start[c]; i < start[c + 1]; i++) {
            in_c[order[i]] = 0;
        }
    }
}

static void back_hero_skin(const Pic *p, u8 *m)
{
    u8 *taken = p->cap;
    int i, n = p->w * p->h;

    /* 9, 10 and 12 within 2 pixels of skin 14, not next to white 13 */
    memset(taken, 0, (size_t)n);
    for (i = 0; i < n; i++) {
        int x = i % p->w, y = i / p->w, v = p->r[i], dx, dy, near = 0, k, white = 0;

        if ((v != 9 && v != 10 && v != 12) || m[i] != 0) {
            continue;
        }
        for (dy = -2; dy <= 2 && !near; dy++) {
            for (dx = -2; dx <= 2 && !near; dx++) {
                near = at(p, x + dx, y + dy) == 14;
            }
        }
        for (k = 0; k < 8; k++) {
            white += at(p, x + N8[k][0], y + N8[k][1]) == 13;
        }
        if (near && white == 0) {
            taken[i] = 1;
        }
    }
    /* the hands drawn in shadow (Hilbert's far hand also uses the
     * trousers' greys) */
    {
        u8 *found = (u8 *)calloc((size_t)n, 1);

        if (found != NULL) {
            shaded_limbs(p, m, taken, found, BIT(9) | BIT(10) | BIT(12) | BIT(2) | BIT(8), BIT(14), BIT(1), BIT(13),
                         BIT(3) | BIT(4) | BIT(5) | BIT(6) | BIT(7) | BIT(11), BIT(9) | BIT(10) | BIT(12));
            for (i = 0; i < n; i++) {
                taken[i] |= found[i];
            }
            free(found);
        }
    }
    for (i = 0; i < n; i++) {
        if (taken[i] && m[i] == 0) {
            m[i] = M_SKIN;
        }
    }
}

static void back_heroine_skin(const Pic *p, u8 *m)
{
    int *comp = p->comp, *order = p->order, *start = p->start;
    int n = comps(p, BIT(10) | BIT(11) | BIT(12) | BIT(13), 1, comp, order, start), c, i, all = p->w * p->h;
    u8 *taken = p->cap, *in_c = p->seen;

    memset(taken, 0, (size_t)all);
    /* grey areas the hair did not take whose border has more skin (9) than
     * white shirt (8) */
    for (c = 0; c < n; c++) {
        int n9 = 0, n8 = 0, any = 0;

        for (i = start[c]; i < start[c + 1]; i++) {
            in_c[order[i]] = (u8)(m[order[i]] == 0);
            any |= in_c[order[i]];
        }
        for (i = start[c]; i < start[c + 1] && any; i++) {
            int j = order[i], px = j % p->w, py = j / p->w, k;

            if (!in_c[j]) {
                continue;
            }
            for (k = 0; k < 4; k++) {
                int xx = px + N4[k][0], yy = py + N4[k][1], jj;

                if (xx < 0 || yy < 0 || xx >= p->w || yy >= p->h) {
                    continue;
                }
                jj = yy * p->w + xx;
                if (comp[jj] == c && in_c[jj]) {
                    continue;
                }
                n9 += p->r[jj] == 9;
                n8 += p->r[jj] == 8;
            }
        }
        if (any && n9 >= 1 && n9 >= n8) {
            for (i = start[c]; i < start[c + 1]; i++) {
                if (in_c[order[i]]) {
                    taken[order[i]] = 1;
                }
            }
        }
        for (i = start[c]; i < start[c + 1]; i++) {
            in_c[order[i]] = 0;
        }
    }
    {
        u8 *found = (u8 *)calloc((size_t)all, 1);

        if (found != NULL) {
            shaded_limbs(p, m, taken, found, BIT(10) | BIT(11) | BIT(12) | BIT(13), BIT(9) | BIT(5) | BIT(6) | BIT(7),
                         BIT(14), BIT(8), BIT(3) | BIT(4) | BIT(1) | BIT(2), 0);
            for (i = 0; i < all; i++) {
                taken[i] |= found[i];
            }
            free(found);
        }
    }
    for (i = 0; i < all; i++) {
        if (taken[i] && m[i] == 0) {
            m[i] = M_SKIN;
        }
    }
}

/* ------------------------------------------------------------------ */
/* The battle's front (the picture at the texture's top left, 80 x 80)  */
/* ------------------------------------------------------------------ */

static void front_hero(const Pic *p, u8 *m)
{
    int *comp = p->comp, *order = p->order, *start = p->start;
    int n = comps(p, BIT(7) | BIT(8), 1, comp, order, start), c, i, x, y, y_hip = -1, y_shoe = 80;

    for (c = 0; c < n; c++) {
        int x0, y0, x1, y1;

        comp_box(p, order, start[c], start[c + 1], &x0, &y0, &x1, &y1);
        if (start[c + 1] - start[c] < 25 || y0 >= 30) {
            continue;
        }
        for (i = start[c]; i < start[c + 1]; i++) {
            int px = order[i] % p->w, py = order[i] / p->w;

            /* by hand: the hand raised to the cap (blocks 0 and 93) */
            if (px >= 22 && px <= 29 && py >= 14 && py <= 24) {
                continue;
            }
            if (count_nb4(p, px, py, BIT(9) | BIT(10)) < 2) {
                m[order[i]] = M_HAIR;
            }
        }
    }
    for (y = 0; y < p->h; y++) {
        for (x = 0; x < p->w; x++) {
            int v = at(p, x, y);

            if (v >= 3 && v <= 6 && y - 2 > y_hip) {
                y_hip = y - 2;          /* below the black shirt and the belt */
            }
            if ((v == 1 || v == 2) && y >= 60 && y < y_shoe) {
                y_shoe = y;
            }
        }
    }
    for (y = y_hip < 0 ? p->h : y_hip; y < y_shoe - 1 && y < p->h; y++) {
        for (x = 0; x < p->w; x++) {
            int v = at(p, x, y);

            /* by hand: the shoulder bag (and the device of block 93) at the
             * right hip, right of the outline column */
            if ((v == 13 || v == 14) && !((y >= 48 && y <= 50 && x >= 44) || (y >= 51 && y <= 54 && x >= 46))) {
                m[y * p->w + x] = M_PANTS;
            }
        }
    }
    /* skin: 7 and 8 that are not hair and touch skin */
    for (y = 0; y < p->h; y++) {
        for (x = 0; x < p->w; x++) {
            int v = at(p, x, y);

            if ((v == 7 || v == 8) && m[y * p->w + x] == 0 && count_nb4(p, x, y, BIT(9) | BIT(10)) > 0) {
                m[y * p->w + x] = M_SKIN;
            }
        }
    }
}

static void front_heroine(const Pic *p, u8 *m)
{
    int x, y, y0 = -1, y1 = -1;

    for (y = 40; y < p->h; y++) {
        int n6 = 0;

        for (x = 0; x < p->w; x++) {
            n6 += at(p, x, y) == 6;
        }
        if (n6 >= 3) {
            y0 = y0 < 0 ? y : y0;
            y1 = y;
        }
    }
    if (y0 >= 0) {
        for (y = y0 - 1; y <= y1 + 1 && y < p->h; y++) {
            for (x = 0; x < p->w; x++) {
                int v = at(p, x, y);

                if (v == 6 || v == 12) {
                    m[y * p->w + x] = M_PANTS;
                }
            }
        }
    }
    for (y = 0; y < p->h; y++) {
        for (x = 0; x < p->w; x++) {
            int v = at(p, x, y);

            if (v == 4 || v == 5) {
                /* by hand: the open mouth keeps its colour (blocks 1 and 94) */
                if (!(x >= 33 && x <= 35 && y >= 25 && y <= 28)) {
                    m[y * p->w + x] = M_ACCENT;
                }
            } else if (v == 1 && count_nb4(p, x, y, BIT(9) | BIT(10) | BIT(7)) < 2) {
                m[y * p->w + x] = M_HAIR;
            }
        }
    }
}

/* ------------------------------------------------------------------ */

void avatar_mask(int set, const u8 *idx, int w, int h, int frames, u8 *mask)
{
    Pic p;
    u8 *crop = NULL, *cmask = NULL;
    int n, front = set == AVATAR_SET_HERO_FRONT || set == AVATAR_SET_HEROINE_FRONT;

    memset(mask, 0, (size_t)(w * h));
    if (w <= 0 || h <= 0 || w > 256 || h > 256) {
        return;
    }
    p.r = idx;
    p.w = w;
    p.h = h;
    if (front) {
        /* the front's picture alone: its top left 80 x 80 */
        int y, cw = w < 80 ? w : 80, ch = h < 80 ? h : 80;

        crop = (u8 *)malloc((size_t)(cw * ch));
        cmask = (u8 *)calloc((size_t)(cw * ch), 1);
        if (crop == NULL || cmask == NULL) {
            free(crop);
            free(cmask);
            return;
        }
        for (y = 0; y < ch; y++) {
            memcpy(crop + y * cw, idx + y * w, (size_t)cw);
        }
        p.r = crop;
        p.w = cw;
        p.h = ch;
    }
    n = p.w * p.h;
    p.comp = (int *)malloc(sizeof(int) * (size_t)(4 * n + 1) + 2u * (size_t)n);
    if (p.comp == NULL) {
        free(crop);
        free(cmask);
        return;
    }
    p.order = p.comp + n;
    p.start = p.order + n;
    p.queue = p.start + n + 1;
    p.cap = (u8 *)(p.queue + n);
    p.seen = p.cap + n;
    memset(p.seen, 0, (size_t)n);
    switch (set) {
    case AVATAR_SET_HEROINE_FIELD:
        field_heroine(&p, mask);
        break;
    case AVATAR_SET_HERO_FIELD:
    case AVATAR_SET_HERO_FIELD_ALT:
        field_hero(&p, mask, set == AVATAR_SET_HERO_FIELD_ALT && frames >= 8);   /* the bicycle, not the save pose */
        break;
    case AVATAR_SET_HERO_BACK:
    case AVATAR_SET_HEROINE_BACK: {
        int i;

        if (set == AVATAR_SET_HERO_BACK) {
            back_hero(&p, mask);
        } else {
            back_heroine(&p, mask);
        }
        for (i = 0; i < n; i++) {
            if (i % w >= BACK_BODY_X && mask[i] != M_SKIN) {
                mask[i] = 0;            /* hair and trousers are on the body */
            }
        }
        if (set == AVATAR_SET_HERO_BACK) {
            back_hero_skin(&p, mask);
        } else {
            back_heroine_skin(&p, mask);
        }
        break;
    }
    case AVATAR_SET_HERO_FRONT:
    case AVATAR_SET_HEROINE_FRONT: {
        int y;

        if (set == AVATAR_SET_HERO_FRONT) {
            front_hero(&p, cmask);
        } else {
            front_heroine(&p, cmask);
        }
        for (y = 0; y < p.h; y++) {
            memcpy(mask + y * w, cmask + y * p.w, (size_t)p.w);
        }
        break;
    }
    default:
        break;
    }
    free(p.comp);
    free(crop);
    free(cmask);
}
