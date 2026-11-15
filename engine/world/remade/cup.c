/*
 * engine/world/remade/cup.c -- a teacup drawn as a picture card (the cups on
 * the Striaton Gym's tables) made a cup.
 *
 * The card is a 16-pixel picture of a cup seen from above the table: the
 * rim's ellipse with the dark inside, the body narrowing to its foot, the
 * handle out of one side.  From any side but the field's the card is a line.
 * The cup: an eight-sided body, narrow at its foot and wide at its rim (two
 * bands, so it bellies a little); a rim; the inside sunk into it, dark; a
 * handle, a loop of three blocks out of the side the picture draws it on.
 * Every face takes one pixel of the picture: the body's light and shaded
 * white, the rim, the dark inside, the handle, so the cup keeps the game's
 * colours, a little darker by the way a face looks.  The picture's dark
 * outline is not drawn: it is the drawing's line, not part of the cup.
 *
 * Sizes are in picture pixels (the card's width over the picture's width):
 * from the picture, the body 11 pixels across at its rim (columns 0..10) and
 * 7 at its foot, about 6.5 high under a view that shows its rim as an
 * ellipse 5 pixels high; the handle 4 pixels out.  The cup stands on the
 * card's foot (or `y`), its middle behind the foot by its foot's radius,
 * across from the card's middle where the picture draws the body.
 *
 * Settings: y (the height it stands on), rt, rb, h, depth, rimw, hout, hy0,
 * hy1, hth, hw (sizes, pixels); bodyc (the body's middle column); side, low,
 * rim, in, handle, base (the pixel a part takes, COL:ROW of the texture).
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "remade.h"

#define PI 3.14159265358979323846

typedef struct {
    double c[3];                /* the cup's foot middle                        */
    double right[3], fwd[3];    /* across (the picture's right), toward the field camera */
    double s;                   /* a picture pixel, in map units                */
    double fu, fv;              /* the texture's repeat the card shows          */
    int tw, th;
    unsigned src;
    unsigned char rgb[3];
    int tex;
} Cup;

static void pick(const Cup *k, const char *opt, const char *name, int dc, int dr, double uv[2])
{
    char b[32];
    int col = dc, row = dr;

    if (rm_opt(opt, name, b, sizeof b) != NULL) {
        sscanf(b, "%d:%d", &col, &row);
    }
    uv[0] = k->fu + (col + 0.5) / k->tw;
    uv[1] = k->fv + (row + 0.5) / k->th;
}

/* A point of the cup: a across (pixels, the picture's right), up (pixels over the foot), f toward the field camera. */
static void at(const Cup *k, double a, double up, double f, double p[3])
{
    int i;

    for (i = 0; i < 3; i++) {
        p[i] = k->c[i] + (a * k->right[i] + f * k->fwd[i]) * k->s;
    }
    p[1] = k->c[1] + up * k->s;
}

/* One triangle, flat in its pixel, a little darker by the way it looks (as turned.c shades); it looks the way
 * (b - a) x (c - a) points. */
static void tri(const Cup *k, RmOut *out, const double a[3], const double b[3], const double c[3], const double uv[2])
{
    double e1[3], e2[3], n[3], f;
    unsigned char rgb[3];
    int i;

    for (i = 0; i < 3; i++) {
        e1[i] = b[i] - a[i];
        e2[i] = c[i] - a[i];
    }
    rm_cross(e1, e2, n);
    if (rm_norm(n) <= 1e-9) {
        return;
    }
    {
        const double nf = rm_dot(n, k->fwd);

        f = n[1] > 0.7 ? 1.0 : n[1] < -0.7 ? 0.55 : nf > 0.5 ? 1.0 : nf < -0.5 ? 0.78 : 0.86;
    }
    for (i = 0; i < 3; i++) {
        rgb[i] = (unsigned char)(k->rgb[i] * f);
    }
    rm_tri(out, k->src, -1, 1, rgb, a, b, c, uv, uv, uv, 0, 0);
}

static void quad(const Cup *k, RmOut *out, const double a[3], const double b[3], const double c[3], const double d[3],
                 const double uv[2])
{
    tri(k, out, a, b, c, uv);
    tri(k, out, a, c, d, uv);
}

/* A ring band of the eight-sided body between (r0, y0) and (r1, y1); `in`: its faces look in (or up, for a flat
 * ring from r0 in to r1 out). */
static void band(const Cup *k, RmOut *out, double r0, double y0, double r1, double y1, int in, const double uv[2])
{
    int i;

    for (i = 0; i < 8; i++) {
        const double t0 = (22.5 + 45.0 * i) * PI / 180.0, t1 = (22.5 + 45.0 * (i + 1)) * PI / 180.0;
        double a[3], b[3], c[3], d[3];

        at(k, r0 * cos(t0), y0, r0 * sin(t0), a);
        at(k, r0 * cos(t1), y0, r0 * sin(t1), b);
        at(k, r1 * cos(t1), y1, r1 * sin(t1), c);
        at(k, r1 * cos(t0), y1, r1 * sin(t0), d);
        if (in) {
            quad(k, out, a, b, c, d, uv);
        } else {
            quad(k, out, a, d, c, b, uv);
        }
    }
}

/* A flat eight-sided disc at y, radius r, looking up (`up`) or down. */
static void disc(const Cup *k, RmOut *out, double r, double y, int up, const double uv[2])
{
    int i;
    double m[3];

    at(k, 0.0, y, 0.0, m);
    for (i = 0; i < 8; i++) {
        const double t0 = (22.5 + 45.0 * i) * PI / 180.0, t1 = (22.5 + 45.0 * (i + 1)) * PI / 180.0;
        double a[3], b[3];

        at(k, r * cos(t0), y, r * sin(t0), a);
        at(k, r * cos(t1), y, r * sin(t1), b);
        if (up) {
            tri(k, out, m, b, a, uv);
        } else {
            tri(k, out, m, a, b, uv);
        }
    }
}

/* A block: across a0..a1, up y0..y1, toward the camera f0..f1 (pixels). */
static void block(const Cup *k, RmOut *out, double a0, double a1, double y0, double y1, double f0, double f1, const double uv[2])
{
    double p[8][3];
    int i;
    static const int fc[6][4] = {
        { 0, 1, 3, 2 }, { 4, 6, 7, 5 },     /* (f0 side, f1 side) */
        { 0, 4, 5, 1 }, { 2, 3, 7, 6 },     /* (under, top) */
        { 0, 2, 6, 4 }, { 1, 5, 7, 3 },     /* (a0 end, a1 end) */
    };

    for (i = 0; i < 8; i++) {
        at(k, i & 1 ? a1 : a0, i & 2 ? y1 : y0, i & 4 ? f1 : f0, p[i]);
    }
    for (i = 0; i < 6; i++) {
        double c[3], e1[3], e2[3], n[3], mid[3], o[3];
        int j;

        /* (each face wound to look out of the block) */
        for (j = 0; j < 3; j++) {
            e1[j] = p[fc[i][1]][j] - p[fc[i][0]][j];
            e2[j] = p[fc[i][2]][j] - p[fc[i][0]][j];
            mid[j] = (p[0][j] + p[7][j]) / 2.0;
            c[j] = (p[fc[i][0]][j] + p[fc[i][2]][j]) / 2.0;
            o[j] = c[j] - mid[j];
        }
        rm_cross(e1, e2, n);
        if (rm_dot(n, o) >= 0.0) {
            quad(k, out, p[fc[i][0]], p[fc[i][1]], p[fc[i][2]], p[fc[i][3]], uv);
        } else {
            quad(k, out, p[fc[i][0]], p[fc[i][3]], p[fc[i][2]], p[fc[i][1]], uv);
        }
    }
}

static int build(const RmBatch *bt, const RmTex *tx, const char *opt, RmOut *out)
{
    const RmTri *t = &bt->tri[0];
    double n[3], e1[3], e2[3], lo = 1e30, lohi[2] = { 1e30, 1e30 }, uL = 0, uR = 0, aL = 1e30, aR = -1e30, vmin = 1e30, umin = 1e30;
    double foot[3] = { 0, 0, 0 }, pw, sgn, bodyc, mid;
    unsigned i, j, nlow = 0;
    Cup k;
    int handle_right;

    memset(&k, 0, sizeof k);
    for (j = 0; j < 3; j++) {
        e1[j] = t->p[1][j] - t->p[0][j];
        e2[j] = t->p[2][j] - t->p[0][j];
    }
    rm_cross(e1, e2, n);
    n[1] = 0.0;
    if (rm_norm(n) < 1e-6) {
        return 0;
    }
    memcpy(k.fwd, n, sizeof n);
    k.right[0] = n[2];
    k.right[1] = 0.0;
    k.right[2] = -n[0];
    /* the card's corners: across, its two lowest (the foot), the texture's place */
    for (i = 0; i < bt->n; i++) {
        for (j = 0; j < 3u; j++) {
            const float *p = bt->tri[i].p[j];
            const double a = p[0] * k.right[0] + p[2] * k.right[2];

            if (a < aL) { aL = a; uL = bt->tri[i].uv[j][0]; }
            if (a > aR) { aR = a; uR = bt->tri[i].uv[j][0]; }
            lo = p[1] < lo ? p[1] : lo;
            umin = bt->tri[i].uv[j][0] < umin ? bt->tri[i].uv[j][0] : umin;
            vmin = bt->tri[i].uv[j][1] < vmin ? bt->tri[i].uv[j][1] : vmin;
        }
    }
    lohi[0] = lo;
    for (i = 0; i < bt->n; i++) {
        for (j = 0; j < 3u; j++) {
            const float *p = bt->tri[i].p[j];

            if (p[1] < lohi[0] + 0.01) {
                foot[0] += p[0]; foot[1] += p[1]; foot[2] += p[2];
                nlow++;
            }
        }
    }
    if (nlow == 0 || aR - aL < 1e-3) {
        return 0;
    }
    for (j = 0; j < 3u; j++) {
        foot[j] /= nlow;
    }
    k.tw = tx->w;
    k.th = tx->h;
    k.fu = floor(umin + 1e-6);
    k.fv = floor(vmin + 1e-6);
    pw = fabs(uR - uL) * tx->w;             /* the picture's width the card shows, pixels */
    if (pw < 2.0) {
        return 0;
    }
    k.s = (aR - aL) / pw;
    k.src = t->id;
    k.tex = bt->tex;
    memcpy(k.rgb, t->rgb, 3u);
    /* the picture's columns run across the card the way its u runs: the handle (the picture's right, columns over
     * the body) is on the card's right when u grows to the right */
    handle_right = uR > uL;
    sgn = handle_right ? 1.0 : -1.0;
    bodyc = rm_optf(opt, "bodyc", 5.5);
    mid = pw / 2.0;
    {
        const double rb = rm_optf(opt, "rb", 3.5), off = (bodyc - mid) * sgn;

        for (j = 0; j < 3u; j++) {
            k.c[j] = foot[j] + (off * k.right[j] - rb * k.fwd[j]) * k.s;
        }
        k.c[1] = rm_optf(opt, "y", foot[1]);
    }
    {
        const double rt = rm_optf(opt, "rt", 5.5), rb = rm_optf(opt, "rb", 3.5), h = rm_optf(opt, "h", 6.5);
        const double depth = rm_optf(opt, "depth", 4.0), rimw = rm_optf(opt, "rimw", 0.7);
        const double rm = rb + (rt - rb) * 0.7, ym = h * 0.45, ri = rt - rimw, rf = ri * 0.7;
        const double hout = rm_optf(opt, "hout", 3.5), hy0 = rm_optf(opt, "hy0", 1.8), hy1 = rm_optf(opt, "hy1", 5.6);
        const double hth = rm_optf(opt, "hth", 1.1), hw = rm_optf(opt, "hw", 1.2);
        double uv_side[2], uv_low[2], uv_rim[2], uv_in[2], uv_h[2], uv_base[2];
        double r_at0, r_at1, a0, a1;

        pick(&k, opt, "side", 5, 11, uv_side);
        pick(&k, opt, "low", 5, 13, uv_low);
        pick(&k, opt, "rim", 4, 6, uv_rim);
        pick(&k, opt, "in", 5, 8, uv_in);
        pick(&k, opt, "handle", 14, 8, uv_h);
        pick(&k, opt, "base", 6, 15, uv_base);
        band(&k, out, rb, 0.0, rm, ym, 0, uv_low);              /* the foot's band */
        band(&k, out, rm, ym, rt, h, 0, uv_side);               /* the body */
        band(&k, out, ri, h, rt, h, 1, uv_rim);                 /* the rim (flat: an annulus) */
        band(&k, out, rf, h - depth, ri, h, 1, uv_in);          /* the inside's wall */
        disc(&k, out, rf, h - depth, 1, uv_in);                 /* the inside's floor */
        disc(&k, out, rb, 0.0, 0, uv_base);                     /* the foot */
        /* the handle: two bars out of the side and the loop's outer bar (the body's radius where each meets it) */
        r_at0 = rb + (rm - rb) * (hy0 / ym);
        r_at1 = rm + (rt - rm) * ((hy1 - ym) / (h - ym));
        a0 = sgn * (r_at0 - 0.4);
        a1 = sgn * (r_at0 + hout);
        block(&k, out, a0 < a1 ? a0 : a1, a0 < a1 ? a1 : a0, hy0, hy0 + hth, -hw / 2.0, hw / 2.0, uv_h);
        a0 = sgn * (r_at1 - 0.4);
        a1 = sgn * (r_at0 + hout);
        block(&k, out, a0 < a1 ? a0 : a1, a0 < a1 ? a1 : a0, hy1 - hth, hy1, -hw / 2.0, hw / 2.0, uv_h);
        a0 = sgn * (r_at0 + hout - hth);
        a1 = sgn * (r_at0 + hout);
        block(&k, out, a0 < a1 ? a0 : a1, a0 < a1 ? a1 : a0, hy0 + hth, hy1 - hth, -hw / 2.0, hw / 2.0, uv_h);
    }
    return 1;
}

void rm_cup(const RmBatch *b, unsigned nb, const RmTex *tex, unsigned ntex, const char *opt, RmOut *out)
{
    unsigned bi;

    for (bi = 0; bi < nb; bi++) {
        const RmBatch *bt = &b[bi];

        if (bt->bb || bt->n < 2 || bt->tex < 0 || (unsigned)bt->tex >= ntex || tex[bt->tex].rgba == NULL) {
            continue;
        }
        if (build(bt, &tex[bt->tex], opt, out)) {
            rm_drop_all(out, bt);
        }
    }
}
