/*
 * engine/world/remade/tower.c -- a tower on a block (the control tower): the
 * map has its south half; most of its parts go on round to the north, but
 * the faces that look north are not in the model, so from the sides and the
 * back it is open.
 *
 * What is made, from the model's own faces and texture pixels:
 *   halfturn=CX:CZ:R0:A0:A1[:Y0:Y1[:X0:X1[:TEX[:TOL[:CX0:CX1]]]]]+..  a round wing's faces past R0 from (CX, CZ), between
 *       the angles A0..A1, copied turned half round there (a back arc whose
 *       front is another part).
 *   mirror=X0:X1:Y0:Y1:ZC[:Z0:Z1[:W[:CX0:CX1]]]+..  the parts within that box are round about
 *       z = ZC (a balcony, a glass cab, its roof, lamps): their faces that
 *       look south are turned to look north, where the model has none (a face
 *       that lands on one of the model's own is left out).
 *   wall=SX:X0:X1:Z:Y0:Y1+..  a back wall at z = Z from X0 to X1, Y0 to Y1
 *       (a block's, a parapet's, a shaft's), in the texture of the side face
 *       at x = SX that reaches furthest back: its corner columns at each end,
 *       its plain column between, row by row, as the sides are.
 *   panel=SX:SZ:SY:X0:X1:Z:Y0:Y1[:ZTOP]+..  a back wall at z = Z in the picture of the
 *       side face at x = SX holding (SZ, SY), repeated along at its own width.
 *   panelx=SX:SZ:SY:X:Z0:Z1:Y0:Y1:DIR+..  an end wall at x = X looking along DIR
 *       (-1, 1), in the picture of the face at x = SX holding (SZ, SY).
 *   panelz=SZ:SX:SY:X0:X1:Z:Y0:Y1[:ZTOP[:UA:UB]]+..  as panel, the picture from a front face at
 *       z = SZ holding (SX, SY): the front's wall at its own scale.
 *   retex=X0:X1:Y0:Y1:ZMAX:SX:SY:DX[:S]+..  (S: read at SX + (x - middle) * S)  new faces in that box take the picture of the
 *       model face holding (SX, SY), read DX along x (a band's plain stretch).
 *   uclamp=X0:X1:Y0:Y1:ZMAX:UMIN:UMAX+..  new faces in that box: u held within
 *       UMIN..UMAX (a picture's edge column left out).
 *   shell=X0:X1:Y0:Y1:Z0:Z1:D:U+..  a wall the map has only as its inner lining: an
 *       outer skin D behind it, in the outer faces' texture column u = U, v by height.
 *   hide=X0:X1:Y0:Y1:Z0:Z1:TEX+..  the model's faces of texture TEX in that box left out.
 *   cutx=X0:X1:Z0:Z1+..  the model's faces wholly between Z0 and Z1 cut to X0..X1.
 *   decal=SZ:SX:SY:X0:X1:Y0:Y1:TX:TY:Z+..  a rectangle of a south face's picture laid on
 *       the back at z = Z, its middle at (TX, TY), looking north.
 *   under=Y+..  the underside at the height Y (an overhang, a balcony): the
 *       outline of the faces that end there, closed, looking down, in the
 *       colour of the face over it.
 *   props=Y0:SIZE  the small things standing above Y0 (roof furniture), each
 *       closed about its own middle; a single flat card (a leaf) both sides.
 *   twoback=Z  back walls the map has only from inside (upright, behind Z,
 *       looking south) shown from both sides.  seal=0: no dark backs (to find
 *       missing faces with pink.py).
 *   reside=FROM:TO:Y0:Y1  the side at x = TO (painted plain) takes the faces of
 *       the side at x = FROM between Y0 and Y1, mirrored across.
 *   rimwalls=Y0:X0:X1:Z0:Z1  a hole in a level floor (a roof pool): walls down
 *       to Y0 under its open edges in that box, in the hole's own wall colours.
 *   twofirst=1  the two pass before halfturn and mirror (their copies two-sided too).
 *   two=X0:X1:Y0:Y1+..  the faces in that box shown from both sides (a
 *       railing's bars, a parasol's cloth).
 *   snap=X:Y:Z:NZ+..  a corner of the model out of line moved to z = NZ first.
 *   clipz=Z  the model's faces that reach behind z = Z are cut there.
 *   mirror's box may end :Z0:Z1[:W], the faces whose middle lies between (and at
 *       least W wide in x).
 *   corner=W  the side's corner columns (map units, default 6); 0: no corner
 *       columns, the side's picture runs on along the back wall (boards).
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "remade.h"

typedef struct {
    const RmTri *t;
    const RmBatch *b;
    double n[3];
} Ft;

static void normal_of(const RmTri *t, double n[3])
{
    double e1[3], e2[3];
    int k;

    for (k = 0; k < 3; k++) {
        e1[k] = (double)t->p[1][k] - t->p[0][k];
        e2[k] = (double)t->p[2][k] - t->p[0][k];
    }
    rm_cross(e1, e2, n);
    if (rm_norm(n) <= 0.0) {
        n[0] = n[1] = n[2] = 0.0;
    }
}

/* Is the point on one of the model's faces that looks the same way (within `tol` of its plane)? */
static int on_face_tol(const Ft *f, unsigned nf, const double p[3], const double n[3], double tol)
{
    unsigned i;

    for (i = 0; i < nf; i++) {
        const RmTri *t = f[i].t;
        double d[3], e1[3], e2[3], w[3], c1[3], c2[3], c3[3], a0[3], a1[3], a2[3];
        int k;

        if (rm_dot(f[i].n, n) < 0.9) {
            continue;
        }
        for (k = 0; k < 3; k++) {
            d[k] = p[k] - t->p[0][k];
        }
        if (fabs(rm_dot(d, f[i].n)) > tol) {
            continue;
        }
        /* (inside: the point is on the inner side of all three edges) */
        for (k = 0; k < 3; k++) {
            e1[k] = (double)t->p[1][k] - t->p[0][k];
            e2[k] = (double)t->p[2][k] - t->p[1][k];
            w[k] = (double)t->p[0][k] - t->p[2][k];
            a0[k] = p[k] - t->p[0][k];
            a1[k] = p[k] - t->p[1][k];
            a2[k] = p[k] - t->p[2][k];
        }
        rm_cross(e1, a0, c1);
        rm_cross(e2, a1, c2);
        rm_cross(w, a2, c3);
        if (rm_dot(c1, f[i].n) >= -1e-3 && rm_dot(c2, f[i].n) >= -1e-3 && rm_dot(c3, f[i].n) >= -1e-3) {
            return 1;
        }
    }
    return 0;
}

/* within 0.1 of its plane */
static int on_face(const Ft *f, unsigned nf, const double p[3], const double n[3])
{
    return on_face_tol(f, nf, p, n, 0.1);
}

/* how a face's texture runs by z (a) and y (b) */
typedef struct { double u0, v0, ua, ub, va, vb, a0, b0; } Map;

static int map_of(const RmTri *t, Map *m)
{
    const double da1 = (double)t->p[1][2] - t->p[0][2], db1 = (double)t->p[1][1] - t->p[0][1];
    const double da2 = (double)t->p[2][2] - t->p[0][2], db2 = (double)t->p[2][1] - t->p[0][1];
    const double det = da1 * db2 - da2 * db1;
    const double du1 = (double)t->uv[1][0] - t->uv[0][0], du2 = (double)t->uv[2][0] - t->uv[0][0];
    const double dv1 = (double)t->uv[1][1] - t->uv[0][1], dv2 = (double)t->uv[2][1] - t->uv[0][1];

    if (fabs(det) < 1e-9) {
        return -1;
    }
    m->a0 = t->p[0][2];
    m->b0 = t->p[0][1];
    m->u0 = t->uv[0][0];
    m->v0 = t->uv[0][1];
    m->ua = (du1 * db2 - du2 * db1) / det;
    m->ub = (du2 * da1 - du1 * da2) / det;
    m->va = (dv1 * db2 - dv2 * db1) / det;
    m->vb = (dv2 * da1 - dv1 * da2) / det;
    return 0;
}

static void map_at(const Map *m, double z, double y, double uv[2])
{
    uv[0] = m->u0 + m->ua * (z - m->a0) + m->ub * (y - m->b0);
    uv[1] = m->v0 + m->va * (z - m->a0) + m->vb * (y - m->b0);
}

/* a new triangle cut to xlo <= x <= xhi (a copy that keeps one half of the back), as a fan; wholly outside, nothing */
static void emit_clipx(RmOut *out, unsigned id, int cull, const unsigned char rgb[3], const double p[3][3],
                       const double uv[3][2], double xlo, double xhi)
{
    double a[9][5], b[9][5];
    int n = 3, m, j, k, side;

    for (j = 0; j < 3; j++) {
        a[j][0] = p[j][0];
        a[j][1] = p[j][1];
        a[j][2] = p[j][2];
        a[j][3] = uv[j][0];
        a[j][4] = uv[j][1];
    }
    /* (Sutherland-Hodgman: x >= xlo, then x <= xhi) */
    for (side = 0; side < 2; side++) {
        const double lim = side == 0 ? xlo : xhi, sg = side == 0 ? 1.0 : -1.0;

        m = 0;
        for (j = 0; j < n; j++) {
            const double *u = a[j], *v = a[(j + 1) % n];
            const int uin = sg * (u[0] - lim) >= -1e-6, vin = sg * (v[0] - lim) >= -1e-6;

            if (uin) {
                memcpy(b[m++], u, sizeof b[0]);
            }
            if (uin != vin) {
                const double t = (lim - u[0]) / (v[0] - u[0]);

                for (k = 0; k < 5; k++) {
                    b[m][k] = u[k] + (v[k] - u[k]) * t;
                }
                m++;
            }
        }
        memcpy(a, b, sizeof a[0] * (size_t)m);
        n = m;
        if (n < 3) {
            return;
        }
    }
    for (j = 1; j + 1 < n; j++) {
        double pa[3], pb[3], pc[3], ua[2], ub[2], uc[2];

        for (k = 0; k < 3; k++) {
            pa[k] = a[0][k];
            pb[k] = a[j][k];
            pc[k] = a[j + 1][k];
        }
        ua[0] = a[0][3]; ua[1] = a[0][4];
        ub[0] = a[j][3]; ub[1] = a[j][4];
        uc[0] = a[j + 1][3]; uc[1] = a[j + 1][4];
        rm_tri(out, id, -1, cull, rgb, pa, pb, pc, ua, ub, uc, 0, 0);
    }
}

/* `two=X0:X1:Y0:Y1+..`: the faces in that box are shown from both sides (a railing's picture of bars, a parasol's
 * cloth: thin things the map draws from the front only) */
static void do_two(const Ft *f, unsigned nf, RmOut *out, char *buf)
{
    char *ep;
    unsigned i;

    for (ep = buf; ep != NULL && *ep != '\0'; ) {
        double bx[4];

        if (sscanf(ep, "%lf:%lf:%lf:%lf", &bx[0], &bx[1], &bx[2], &bx[3]) == 4) {
            for (i = 0; i < nf; i++) {
                int j, in = 1;

                for (j = 0; j < 3; j++) {
                    in &= f[i].t->p[j][0] >= bx[0] - 0.05 && f[i].t->p[j][0] <= bx[1] + 0.05
                          && f[i].t->p[j][1] >= bx[2] - 0.05 && f[i].t->p[j][1] <= bx[3] + 0.05;
                }
                if (in) {
                    rm_two(out, f[i].t->id);
                }
            }
        }
        ep = strchr(ep, '+');
        ep = ep != NULL ? ep + 1 : NULL;
    }
}

/* `halfturn=CX:CZ:R0:A0:A1`: a round wing whose back arc has no front arc to mirror (the front there is another part's
 * facade): its faces further than R0 from the upright through (CX, CZ) and between the angles A0..A1 (degrees, from
 * +x towards +z) are copied turned half round that upright, where the model has no face either way.  `:Y0:Y1[:X0:X1[:TEX]]`
 * after: only faces wholly within that height and x (a tower's shaft above a base with a door, without its sign), and
 * only faces in the picture TEX (a dome among other things; -1 any).  `:TOL` after: a copy is left out when it lies within
 * TOL of a face of the model's (default 0.1; a turned bevel can land on the model's own a little off its plane). */
static void do_halfturn(const Ft *f, unsigned nf, RmOut *out, const double h[13])
{
    const double pi = 3.14159265358979;
    unsigned i;

    for (i = 0; i < nf; i++) {
        const RmTri *t = f[i].t;
        double p[3][3], c[3] = { 0, 0, 0 }, mn[3], back[3], uv[3][2];
        int j, k, in = 1, s, hit = 0;

        if (h[9] >= 0.0 && f[i].b->tex != (int)h[9]) {
            continue;
        }
        for (j = 0; j < 3; j++) {
            const double dx = t->p[j][0] - h[0], dz = t->p[j][2] - h[1];
            double a = atan2(dz, dx) * 180.0 / pi;

            in &= sqrt(dx * dx + dz * dz) >= h[2] && a >= h[3] - 0.5 && a <= h[4] + 0.5;
            in &= t->p[j][1] >= h[5] - 0.01 && t->p[j][1] <= h[6] + 0.01;
            in &= t->p[j][0] >= h[7] - 0.01 && t->p[j][0] <= h[8] + 0.01;
        }
        if (!in) {
            continue;
        }
        for (j = 0; j < 3; j++) {
            p[j][0] = 2.0 * h[0] - t->p[j][0];
            p[j][1] = t->p[j][1];
            p[j][2] = 2.0 * h[1] - t->p[j][2];
            uv[j][0] = t->uv[j][0];
            uv[j][1] = t->uv[j][1];
            for (k = 0; k < 3; k++) {
                c[k] += p[j][k] / 3.0;
            }
        }
        mn[0] = -f[i].n[0];
        mn[1] = f[i].n[1];
        mn[2] = -f[i].n[2];
        for (k = 0; k < 3; k++) {
            back[k] = -mn[k];
        }
        for (s = 0; s < 4 && !hit; s++) {
            double q[3];

            for (k = 0; k < 3; k++) {
                q[k] = s == 0 ? c[k] : c[k] + (p[s - 1][k] - c[k]) * 0.66;
            }
            hit = on_face_tol(f, nf, q, mn, h[10]) || on_face_tol(f, nf, q, back, h[10]);
        }
        if (!hit) {
            /* (`:CX0:CX1` after TOL: the copy cut to that x range) */
            emit_clipx(out, t->id, f[i].b->cull, t->rgb, (const double (*)[3])p, (const double (*)[2])uv, h[11], h[12]);
        }
    }
}

/* a four-cornered face drawn from both sides, lit by the game, its corners turned to look along `outn` */
static void quad(RmOut *o, unsigned src, const unsigned char rgb[3], const double p[4][3], const double t[4][2],
                 const double outn[3])
{
    double e1[3], e2[3], n[3];
    unsigned first = o->ntri, i;
    int k;

    for (k = 0; k < 3; k++) {
        e1[k] = p[1][k] - p[0][k];
        e2[k] = p[2][k] - p[0][k];
    }
    rm_cross(e1, e2, n);
    if (rm_dot(n, outn) >= 0.0) {
        rm_tri(o, src, -1, 0, rgb, p[0], p[1], p[2], t[0], t[1], t[2], 0, 0);
        rm_tri(o, src, -1, 0, rgb, p[0], p[2], p[3], t[0], t[2], t[3], 0, 0);
    } else {
        rm_tri(o, src, -1, 0, rgb, p[0], p[2], p[1], t[0], t[2], t[1], 0, 0);
        rm_tri(o, src, -1, 0, rgb, p[0], p[3], p[2], t[0], t[3], t[2], 0, 0);
    }
    for (i = first; i < o->ntri; i++) {
        o->tri[i].lit = 1;
    }
}

static void do_mirror(const Ft *f, unsigned nf, RmOut *out, const double bx[10])
{
    unsigned i;

    for (i = 0; i < nf; i++) {
        const RmTri *t = f[i].t;
        double p[3][3], uv[3][2], c[3] = { 0, 0, 0 }, mn[3];
        int j, k, in = 1;

        /* (a face that looks south; or any face of a part that lies in the south half: a beam's top) */
        for (j = 0; j < 3; j++) {
            c[2] += t->p[j][2] / 3.0;
        }
        if (f[i].n[2] < 0.3 && c[2] <= bx[4] + 0.5) {
            continue;
        }
        c[2] = 0.0;
        for (j = 0; j < 3; j++) {
            in &= t->p[j][0] >= bx[0] - 0.05 && t->p[j][0] <= bx[1] + 0.05 && t->p[j][1] >= bx[2] - 0.05
                  && t->p[j][1] <= bx[3] + 0.05;
        }
        if (in && bx[7] > 0.0) {
            /* (`:W`: only faces at least W wide in x) */
            double xl = t->p[0][0], xh = t->p[0][0];

            for (j = 1; j < 3; j++) {
                xl = t->p[j][0] < xl ? t->p[j][0] : xl;
                xh = t->p[j][0] > xh ? t->p[j][0] : xh;
            }
            in = xh - xl >= bx[7];
        }
        if (in && bx[6] > bx[5]) {
            /* (`:Z0:Z1`: only faces whose middle lies between) */
            const double cz = ((double)t->p[0][2] + t->p[1][2] + t->p[2][2]) / 3.0;

            in = cz >= bx[5] && cz <= bx[6];
        }
        if (!in) {
            continue;
        }
        for (j = 0; j < 3; j++) {
            const int s = j == 0 ? 0 : 3 - j;     /* (mirrored: corners the other way round) */

            p[j][0] = t->p[s][0];
            p[j][1] = t->p[s][1];
            p[j][2] = 2.0 * bx[4] - t->p[s][2];
            uv[j][0] = t->uv[s][0];
            uv[j][1] = t->uv[s][1];
            for (k = 0; k < 3; k++) {
                c[k] += p[j][k] / 3.0;
            }
        }
        mn[0] = f[i].n[0];
        mn[1] = f[i].n[1];
        mn[2] = -f[i].n[2];
        {
            /* (the model has that face already, or part of it: its middle or a point two thirds of the way to a
             * corner on one of the model's faces; half on one, it would flicker with it) */
            int s, hit = 0;

            const double back[3] = { -mn[0], -mn[1], -mn[2] };

            /* (either way: a face the model has looking the other way in that plane is the wall's other side; the
             * copy would stand inside the wall) */
            for (s = 0; s < 4 && !hit; s++) {
                double q[3];

                for (k = 0; k < 3; k++) {
                    q[k] = s == 0 ? c[k] : c[k] + (p[s - 1][k] - c[k]) * 0.66;
                }
                hit = on_face(f, nf, q, mn) || on_face(f, nf, q, back);
            }
            if (hit) {
                continue;
            }
        }
        {
            /* (a face shown from both sides: its copy too) */
            int cull = f[i].b->cull;
            unsigned w;

            for (w = 0; w < out->ntwo; w++) {
                if (out->two[w] == t->id) {
                    cull = 0;
                    break;
                }
            }
            /* (`:CX0:CX1` after W: the copy cut to that x range) */
            emit_clipx(out, t->id, cull, t->rgb, (const double (*)[3])p, (const double (*)[2])uv, bx[8], bx[9]);
        }
    }
}

/* the texture place of the side at x = sx, at (z, y): from the side's own triangle there, among those over the heights
 * y0..y1 (a quad's two triangles may map the picture differently; one's mapping carried past its edge is wrong) */
static int side_uv(const Ft *f, unsigned nf, double sx, double z, double y, double y0, double y1, double uv[2])
{
    unsigned i;

    for (i = 0; i < nf; i++) {
        const RmTri *t = f[i].t;
        double ylo = 1e30, yhi = -1e30, den, w0, w1, w2;
        int j, at = 1;

        if (fabs(f[i].n[0]) < 0.9 || f[i].b->tex < 0) {
            continue;
        }
        for (j = 0; j < 3; j++) {
            at &= fabs(t->p[j][0] - sx) < 0.1;
            ylo = t->p[j][1] < ylo ? t->p[j][1] : ylo;
            yhi = t->p[j][1] > yhi ? t->p[j][1] : yhi;
        }
        if (!at || ylo < y0 - 0.05 || yhi > y1 + 0.05) {
            continue;
        }
        den = ((double)t->p[1][1] - t->p[2][1]) * ((double)t->p[0][2] - t->p[2][2])
              + ((double)t->p[2][2] - t->p[1][2]) * ((double)t->p[0][1] - t->p[2][1]);
        if (fabs(den) < 1e-9) {
            continue;
        }
        w0 = (((double)t->p[1][1] - t->p[2][1]) * (z - t->p[2][2]) + ((double)t->p[2][2] - t->p[1][2]) * (y - t->p[2][1])) / den;
        w1 = (((double)t->p[2][1] - t->p[0][1]) * (z - t->p[2][2]) + ((double)t->p[0][2] - t->p[2][2]) * (y - t->p[2][1])) / den;
        w2 = 1.0 - w0 - w1;
        if (w0 >= -1e-3 && w1 >= -1e-3 && w2 >= -1e-3) {
            uv[0] = w0 * t->uv[0][0] + w1 * t->uv[1][0] + w2 * t->uv[2][0];
            uv[1] = w0 * t->uv[0][1] + w1 * t->uv[1][1] + w2 * t->uv[2][1];
            return 1;
        }
    }
    return 0;
}

/* the back wall over the heights y0..y1 in one side face's texture */
static void wall_strip(RmOut *out, const Ft *f, unsigned nf, const Ft *sf, const double w[6], double cw, double zend,
                       double y0, double y1);

static void do_wall(const Ft *f, unsigned nf, RmOut *out, const double w[6], double cw)
{
    double best = 1e30;
    unsigned i;

    /* how far back the side at x = SX reaches over the wall's heights (lower down it may reach further: a wing) */
    for (i = 0; i < nf; i++) {
        const RmTri *t = f[i].t;
        double ylo = 1e30, yhi = -1e30;
        int j, at = 1;

        if (fabs(f[i].n[0]) < 0.9 || f[i].b->tex < 0) {
            continue;
        }
        for (j = 0; j < 3; j++) {
            at &= fabs(t->p[j][0] - w[0]) < 0.1;
            ylo = t->p[j][1] < ylo ? t->p[j][1] : ylo;
            yhi = t->p[j][1] > yhi ? t->p[j][1] : yhi;
        }
        if (!at || yhi < w[4] + 0.5 || ylo > w[5] - 0.5) {
            continue;
        }
        for (j = 0; j < 3; j++) {
            best = t->p[j][2] < best ? t->p[j][2] : best;
        }
    }
    /* each of its faces that reaches that far, band by band (a side made of bands, each in its own place of the
     * texture): the wall over that band's heights in that face's texture */
    for (i = 0; best < 1e29 && i < nf; i++) {
        const RmTri *t = f[i].t;
        double ylo = 1e30, yhi = -1e30, zlo = 1e30;
        int j, at = 1;

        if (fabs(f[i].n[0]) < 0.9 || f[i].b->tex < 0) {
            continue;
        }
        for (j = 0; j < 3; j++) {
            at &= fabs(t->p[j][0] - w[0]) < 0.1;
            zlo = t->p[j][2] < zlo ? t->p[j][2] : zlo;
            ylo = t->p[j][1] < ylo ? t->p[j][1] : ylo;
            yhi = t->p[j][1] > yhi ? t->p[j][1] : yhi;
        }
        ylo = ylo < w[4] ? w[4] : ylo;
        yhi = yhi > w[5] ? w[5] : yhi;
        if (!at || zlo > best + 0.2 || yhi - ylo < 0.05) {
            continue;
        }
        /* (the band's two triangles both reach back: one strip a band) */
        {
            unsigned q;
            int dup = 0;

            for (q = 0; q < i && !dup; q++) {
                double qlo = 1e30, qhi = -1e30, qz = 1e30;
                int at2 = 1;

                if (fabs(f[q].n[0]) < 0.9 || f[q].b->tex < 0) {
                    continue;
                }
                for (j = 0; j < 3; j++) {
                    at2 &= fabs(f[q].t->p[j][0] - w[0]) < 0.1;
                    qz = f[q].t->p[j][2] < qz ? f[q].t->p[j][2] : qz;
                    qlo = f[q].t->p[j][1] < qlo ? f[q].t->p[j][1] : qlo;
                    qhi = f[q].t->p[j][1] > qhi ? f[q].t->p[j][1] : qhi;
                }
                qlo = qlo < w[4] ? w[4] : qlo;
                qhi = qhi > w[5] ? w[5] : qhi;
                dup = at2 && qz <= best + 0.2 && fabs(qlo - ylo) < 0.05 && fabs(qhi - yhi) < 0.05;
            }
            if (!dup) {
                wall_strip(out, f, nf, &f[i], w, cw, best, ylo, yhi);
            }
        }
    }
}

static void wall_strip(RmOut *out, const Ft *f, unsigned nf, const Ft *sf, const double w[6], double cw, double zend,
                       double y0, double y1)
{
    Map m;

    if (map_of(sf->t, &m) != 0) {
        return;
    }
    {
        const double outn[3] = { 0, 0, -1 };
        const double c = w[2] - w[1] > 3.0 * cw ? cw : (w[2] - w[1]) / 3.0;
        double p[4][3], t[4][2];
        int e;

        for (e = cw <= 0.0 ? 2 : 0; e < 3; e++) {
            /* (e 0, 1: the corner pieces at X1 and X0; e 2: the plain wall between) */
            const double xa = e == 0 ? w[2] : e == 1 ? w[1] : w[1] + c, xb = e == 0 ? w[2] - c : e == 1 ? w[1] + c : w[2] - c;
            unsigned q;

            p[0][0] = xa; p[0][1] = y0;
            p[1][0] = xb; p[1][1] = y0;
            p[2][0] = xb; p[2][1] = y1;
            p[3][0] = xa; p[3][1] = y1;
            for (q = 0; q < 4u; q++) {
                p[q][2] = w[3];
                /* (`corner=0`: the side's picture runs on along the wall, at its own scale: boards, their
                 * joints where the side has them) */
                const double zs = cw <= 0.0 ? zend + (p[q][0] - w[1]) : e < 2 ? zend + fabs(p[q][0] - xa) : zend + c - 0.3;
                const double ys = p[q][1] + (q < 2u ? 0.01 : -0.01);     /* (just inside the band) */

                if (!side_uv(f, nf, w[0], zs, ys, y0, y1, t[q])) {
                    map_at(&m, zs, p[q][1], t[q]);
                }
            }
            quad(out, sf->t->id, sf->b->rgb, p, t, outn);
        }
    }
}

/* `panel=SX:SZ:SY:X0:X1:Z:Y0:Y1+..`: a back wall in one picture of the building: the face of the side at x = SX that
 * holds the point (SZ, SY) is the picture (its texture place, as wide as that face is long); the wall at z = Z from
 * X0 to X1 and Y0 to Y1 wears it again and again along, at the face's own width, the picture's full height over the
 * wall's (a storey's panel, stretched as the building stretches it per storey). */
static void do_panel(const Ft *f, unsigned nf, RmOut *out, const double p[9], const RmTex *tex, unsigned ntex)
{
    const Ft *src = NULL;
    double u0 = 1e30, u1 = -1e30, v0 = 1e30, v1 = -1e30, len = 0.0, x;
    unsigned i;
    int j;

    for (i = 0; i < nf && src == NULL; i++) {
        const RmTri *t = f[i].t;
        double zlo = 1e30, zhi = -1e30, ylo = 1e30, yhi = -1e30;
        int at = 1;

        if (fabs(f[i].n[0]) < 0.9 || f[i].b->tex < 0) {
            continue;
        }
        for (j = 0; j < 3; j++) {
            at &= fabs(t->p[j][0] - p[0]) < 1.5;
            zlo = t->p[j][2] < zlo ? t->p[j][2] : zlo;
            zhi = t->p[j][2] > zhi ? t->p[j][2] : zhi;
            ylo = t->p[j][1] < ylo ? t->p[j][1] : ylo;
            yhi = t->p[j][1] > yhi ? t->p[j][1] : yhi;
        }
        if (at && p[1] >= zlo - 0.05 && p[1] <= zhi + 0.05 && p[2] >= ylo - 0.05 && p[2] <= yhi + 0.05) {
            src = &f[i];
            len = zhi - zlo;
        }
    }
    if (src == NULL || len < 0.5) {
        return;
    }
    for (j = 0; j < 3; j++) {
        u0 = src->t->uv[j][0] < u0 ? src->t->uv[j][0] : u0;
        u1 = src->t->uv[j][0] > u1 ? src->t->uv[j][0] : u1;
        v0 = src->t->uv[j][1] < v0 ? src->t->uv[j][1] : v0;
        v1 = src->t->uv[j][1] > v1 ? src->t->uv[j][1] : v1;
    }
    /* (a picture's see-through edge columns are not carried round: u0..u1 shrunk to the columns that are solid all
     * the way down) */
    if (src->b->tex >= 0 && (unsigned)src->b->tex < ntex && tex[src->b->tex].rgba != NULL) {
        const RmTex *tx = &tex[src->b->tex];
        const double fu0 = u0 - floor(u0);
        const int c0 = (int)floor(fu0 * tx->w), c1 = (int)ceil((fu0 + (u1 - u0)) * tx->w) - 1;
        const int r0 = (int)floor((v0 - floor(v0)) * tx->h), r1 = (int)ceil((v0 - floor(v0) + (v1 - v0)) * tx->h) - 1;
        int c, r, a = c0, z = c1;

        for (c = c0; c <= c1; c++) {
            int solid = 1;

            for (r = r0; r <= r1 && solid; r++) {
                solid = tx->rgba[(((size_t)(((r % tx->h) + tx->h) % tx->h)) * tx->w + (size_t)(((c % tx->w) + tx->w) % tx->w)) * 4u + 3u] > 127;
            }
            if (!solid && c == a) {
                a++;
            }
        }
        for (c = c1; c >= a; c--) {
            int solid = 1;

            for (r = r0; r <= r1 && solid; r++) {
                solid = tx->rgba[(((size_t)(((r % tx->h) + tx->h) % tx->h)) * tx->w + (size_t)(((c % tx->w) + tx->w) % tx->w)) * 4u + 3u] > 127;
            }
            if (solid) {
                break;
            }
            z--;
        }
        if (z >= a && (a > c0 || z < c1)) {
            u0 = floor(u0) + (double)a / tx->w + 0.5 / tx->w * 0.0;
            u1 = floor(u0) + (double)(z + 1) / tx->w;
        }
    }
    for (x = p[3]; x < p[4] - 0.05; x += len) {
        const double xe = x + len < p[4] ? x + len : p[4], ue = u0 + (u1 - u0) * (xe - x) / len;
        const double outn[3] = { 0, 0, -1 };
        double q[4][3], t[4][2];

        q[0][0] = x;  q[0][1] = p[6]; q[0][2] = p[5];
        q[1][0] = xe; q[1][1] = p[6]; q[1][2] = p[5];
        q[2][0] = xe; q[2][1] = p[7]; q[2][2] = p[8];   /* (p[8]: its top's depth, a wall that leans) */
        q[3][0] = x;  q[3][1] = p[7]; q[3][2] = p[8];
        /* (seen from behind, x runs right to left: the picture's start at the wall's high-x end of each piece) */
        t[0][0] = ue; t[0][1] = v1;
        t[1][0] = u0; t[1][1] = v1;
        t[2][0] = u0; t[2][1] = v0;
        t[3][0] = ue; t[3][1] = v0;
        quad(out, src->t->id, src->b->rgb, q, t, outn);
    }
}

/* `panelx=SX:SZ:SY:X:Z0:Z1:Y0:Y1:DIR+..`: an end wall at x = X (looking along DIR: -1 west, 1 east) from Z0 to Z1 and
 * Y0 to Y1, in the picture of the face at x = SX (within 1.5: a leaning side) that holds (SZ, SY): a band's open end,
 * capped as its other end is. */
static void do_panelx(const Ft *f, unsigned nf, RmOut *out, const double p[9])
{
    const Ft *src = NULL;
    double u0 = 1e30, u1 = -1e30, v0 = 1e30, v1 = -1e30;
    unsigned i;
    int j;

    for (i = 0; i < nf && src == NULL; i++) {
        const RmTri *t = f[i].t;
        double zlo = 1e30, zhi = -1e30, ylo = 1e30, yhi = -1e30;
        int at = 1;

        if (fabs(f[i].n[0]) < 0.9 || f[i].b->tex < 0) {
            continue;
        }
        for (j = 0; j < 3; j++) {
            at &= fabs(t->p[j][0] - p[0]) < 1.5;
            zlo = t->p[j][2] < zlo ? t->p[j][2] : zlo;
            zhi = t->p[j][2] > zhi ? t->p[j][2] : zhi;
            ylo = t->p[j][1] < ylo ? t->p[j][1] : ylo;
            yhi = t->p[j][1] > yhi ? t->p[j][1] : yhi;
        }
        if (at && p[1] >= zlo - 0.05 && p[1] <= zhi + 0.05 && p[2] >= ylo - 0.05 && p[2] <= yhi + 0.05) {
            src = &f[i];
        }
    }
    if (src == NULL) {
        return;
    }
    for (j = 0; j < 3; j++) {
        u0 = src->t->uv[j][0] < u0 ? src->t->uv[j][0] : u0;
        u1 = src->t->uv[j][0] > u1 ? src->t->uv[j][0] : u1;
        v0 = src->t->uv[j][1] < v0 ? src->t->uv[j][1] : v0;
        v1 = src->t->uv[j][1] > v1 ? src->t->uv[j][1] : v1;
    }
    {
        const double outn[3] = { p[8] < 0.0 ? -1.0 : 1.0, 0, 0 };
        const double um = u0 + (u1 - u0) * 0.5, uw = (u1 - u0) * 0.15;   /* (a narrow middle column: an end is short) */
        double q[4][3], t[4][2];

        q[0][0] = p[3]; q[0][1] = p[6]; q[0][2] = p[4];
        q[1][0] = p[3]; q[1][1] = p[6]; q[1][2] = p[5];
        q[2][0] = p[3]; q[2][1] = p[7]; q[2][2] = p[5];
        q[3][0] = p[3]; q[3][1] = p[7]; q[3][2] = p[4];
        t[0][0] = um - uw; t[0][1] = v1;
        t[1][0] = um + uw; t[1][1] = v1;
        t[2][0] = um + uw; t[2][1] = v0;
        t[3][0] = um - uw; t[3][1] = v0;
        quad(out, src->t->id, src->b->rgb, q, t, outn);
    }
}

/* `panelz=SZ:SX:SY:X0:X1:Z:Y0:Y1+..`: as `panel`, the picture taken from a face of the front (looking along z, at
 * z = SZ, holding (SX, SY)): a back in the front's own wall at the front's own scale, without its door, piece by piece
 * as wide as the front's pieces. */
static void do_panelz(const Ft *f, unsigned nf, RmOut *out, const double p[11])
{
    const Ft *src = NULL;
    double u0 = 1e30, u1 = -1e30, v0 = 1e30, v1 = -1e30, len = 0.0, x;
    unsigned i;
    int j;

    for (i = 0; i < nf && src == NULL; i++) {
        const RmTri *t = f[i].t;
        double xlo = 1e30, xhi = -1e30, ylo = 1e30, yhi = -1e30;
        int at = 1;

        if (fabs(f[i].n[2]) < 0.9 || f[i].b->tex < 0) {
            continue;
        }
        for (j = 0; j < 3; j++) {
            at &= fabs(t->p[j][2] - p[0]) < 2.1;   /* (a front panel may lean back a little) */
            xlo = t->p[j][0] < xlo ? t->p[j][0] : xlo;
            xhi = t->p[j][0] > xhi ? t->p[j][0] : xhi;
            ylo = t->p[j][1] < ylo ? t->p[j][1] : ylo;
            yhi = t->p[j][1] > yhi ? t->p[j][1] : yhi;
        }
        if (at && p[1] >= xlo - 0.05 && p[1] <= xhi + 0.05 && p[2] >= ylo - 0.05 && p[2] <= yhi + 0.05) {
            src = &f[i];
            len = xhi - xlo;
        }
    }
    if (src == NULL || len < 0.5) {
        return;
    }
    for (j = 0; j < 3; j++) {
        u0 = src->t->uv[j][0] < u0 ? src->t->uv[j][0] : u0;
        u1 = src->t->uv[j][0] > u1 ? src->t->uv[j][0] : u1;
        v0 = src->t->uv[j][1] < v0 ? src->t->uv[j][1] : v0;
        v1 = src->t->uv[j][1] > v1 ? src->t->uv[j][1] : v1;
    }
    /* (p[9]..p[10]: only that part of the picture across, as a fraction: a plain stretch of a panel) */
    if (p[10] > p[9]) {
        const double a0 = u0 + (u1 - u0) * p[9], a1 = u0 + (u1 - u0) * p[10];

        len *= p[10] - p[9];
        u0 = a0;
        u1 = a1;
    }
    for (x = p[3]; x < p[4] - 0.05; x += len) {
        const double xe = x + len < p[4] ? x + len : p[4], ue = u0 + (u1 - u0) * (xe - x) / len;
        const double outn[3] = { 0, 0, -1 };
        double q[4][3], t[4][2];

        q[0][0] = x;  q[0][1] = p[6]; q[0][2] = p[5];
        q[1][0] = xe; q[1][1] = p[6]; q[1][2] = p[5];
        q[2][0] = xe; q[2][1] = p[7]; q[2][2] = p[8];   /* (p[8]: its top's depth, a wall that leans) */
        q[3][0] = x;  q[3][1] = p[7]; q[3][2] = p[8];
        t[0][0] = ue; t[0][1] = v1;
        t[1][0] = u0; t[1][1] = v1;
        t[2][0] = u0; t[2][1] = v0;
        t[3][0] = ue; t[3][1] = v0;
        quad(out, src->t->id, src->b->rgb, q, t, outn);
    }
}

/* `retex=X0:X1:Y0:Y1:ZMAX:SX:SY:DX+..`: the new faces made so far wholly in that box (x, y, and z <= ZMAX: the back)
 * keep their shape but take their picture from the model's own face that holds (SX, SY) seen from the front (looking
 * south, its x and y), read DX further along x: a stretch of a band over a door gets the plain band beside it. */
static void do_retex(const Ft *f, unsigned nf, RmOut *out, unsigned first, const double r[9])
{
    const Ft *src = NULL;
    double m[2][3];             /* u, v = a x + b y + c, from the source's corners */
    unsigned i;
    int j;

    for (i = 0; i < nf && src == NULL; i++) {
        const RmTri *t = f[i].t;
        double den, w0, w1;

        if (f[i].n[2] < 0.3) {
            continue;
        }
        den = ((double)t->p[1][1] - t->p[2][1]) * ((double)t->p[0][0] - t->p[2][0])
              + ((double)t->p[2][0] - t->p[1][0]) * ((double)t->p[0][1] - t->p[2][1]);
        if (fabs(den) < 1e-9) {
            continue;
        }
        w0 = (((double)t->p[1][1] - t->p[2][1]) * (r[5] - t->p[2][0]) + ((double)t->p[2][0] - t->p[1][0]) * (r[6] - t->p[2][1])) / den;
        w1 = (((double)t->p[2][1] - t->p[0][1]) * (r[5] - t->p[2][0]) + ((double)t->p[0][0] - t->p[2][0]) * (r[6] - t->p[2][1])) / den;
        if (w0 >= -1e-3 && w1 >= -1e-3 && 1.0 - w0 - w1 >= -1e-3) {
            src = &f[i];
        }
    }
    if (src == NULL) {
        return;
    }
    {
        /* (the affine map from (x, y) to (u, v) over the source triangle) */
        const RmTri *t = src->t;
        const double x0 = t->p[0][0], y0 = t->p[0][1];
        const double x1 = t->p[1][0] - x0, y1 = t->p[1][1] - y0, x2 = t->p[2][0] - x0, y2 = t->p[2][1] - y0;
        const double det = x1 * y2 - x2 * y1;
        int k;

        if (fabs(det) < 1e-9) {
            return;
        }
        for (k = 0; k < 2; k++) {
            const double d1 = (double)t->uv[1][k] - t->uv[0][k], d2 = (double)t->uv[2][k] - t->uv[0][k];

            m[k][0] = (d1 * y2 - d2 * y1) / det;
            m[k][1] = (d2 * x1 - d1 * x2) / det;
            m[k][2] = t->uv[0][k] - m[k][0] * x0 - m[k][1] * y0;
        }
    }
    for (i = first; i < out->ntri; i++) {
        RmNew *n = &out->tri[i];
        int in = 1;

        for (j = 0; j < 3; j++) {
            in &= n->p[j][0] >= r[0] - 0.05 && n->p[j][0] <= r[1] + 0.05 && n->p[j][1] >= r[2] - 0.05
                  && n->p[j][1] <= r[3] + 0.05 && n->p[j][2] <= r[4];
        }
        if (!in) {
            continue;
        }
        /* (a copy keeps its x: the source read DX along from it) */
        for (j = 0; j < 3; j++) {
            const double x = r[8] > 0.0 ? r[5] + (n->p[j][0] - (r[0] + r[1]) / 2.0) * r[8] : n->p[j][0] + r[7];
            const double y = n->p[j][1];

            n->uv[j][0] = (float)(m[0][0] * x + m[0][1] * y + m[0][2]);
            n->uv[j][1] = (float)(m[1][0] * x + m[1][1] * y + m[1][2]);
        }
        n->src = src->t->id;
        n->tex = -1;
    }
}

/* `shell=X0:X1:Y0:Y1:Z0:Z1:D:U+..`: a wall the map has only as its inner lining (faces whose middle lies in that box):
 * an outer skin, each face copied looking the other way, D behind it, in the texture column u = U that the model's
 * own outer faces wear, its v at each corner the one those faces have at the same height (a cloth whose shade goes
 * by height only). */
static void do_shell(const Ft *f, unsigned nf, RmOut *out, const double s[8])
{
    double ys[512], vs[512];
    unsigned char rgb[3] = { 255, 255, 255 };
    unsigned i, np = 0;
    int j, k, got = 0;

    /* (the height -> v pairs of the outer faces in the column, and their colour) */
    for (i = 0; i < nf && np < 500u; i++) {
        const RmTri *t = f[i].t;
        int col = 1;

        for (j = 0; j < 3; j++) {
            col &= fabs(t->uv[j][0] - s[7]) < 0.02;
        }
        if (col && !got) {
            memcpy(rgb, t->rgb, 3);
            got = 1;
        }
        for (j = 0; col && j < 3 && np < 500u; j++) {
            ys[np] = t->p[j][1];
            vs[np] = t->uv[j][1];
            np++;
        }
    }
    if (np < 2u) {
        return;
    }
    for (i = 0; i < nf; i++) {
        const RmTri *t = f[i].t;
        double c[3] = { 0, 0, 0 }, p[3][3], uv[3][2];

        for (j = 0; j < 3; j++) {
            for (k = 0; k < 3; k++) {
                c[k] += t->p[j][k] / 3.0;
            }
        }
        if (c[0] < s[0] || c[0] > s[1] || c[1] < s[2] || c[1] > s[3] || c[2] < s[4] || c[2] > s[5]) {
            continue;
        }
        for (j = 0; j < 3; j++) {
            const int q = j == 0 ? 0 : 3 - j;       /* (corners the other way round: it looks the other way) */
            double best = 1e30, v = 0.0;
            unsigned a, b;

            for (k = 0; k < 3; k++) {
                p[j][k] = t->p[q][k] - f[i].n[k] * s[6];
            }
            /* (v at this height: between the two nearest heights of the column, one under, one over) */
            {
                double ylo = -1e30, yhi = 1e30, vlo = vs[0], vhi = vs[0];

                for (a = 0; a < np; a++) {
                    if (ys[a] <= p[j][1] && ys[a] > ylo) {
                        ylo = ys[a];
                        vlo = vs[a];
                    }
                    if (ys[a] >= p[j][1] && ys[a] < yhi) {
                        yhi = ys[a];
                        vhi = vs[a];
                    }
                }
                if (ylo < -1e29) {
                    v = vhi;
                } else if (yhi > 1e29) {
                    v = vlo;
                } else {
                    v = yhi - ylo > 1e-6 ? vlo + (vhi - vlo) * (p[j][1] - ylo) / (yhi - ylo) : vlo;
                }
            }
            (void)best;
            (void)b;
            uv[j][0] = s[7];
            uv[j][1] = v;
        }
        rm_tri(out, t->id, -1, 1, rgb, p[0], p[1], p[2], uv[0], uv[1], uv[2], 0, 0);
        out->tri[out->ntri - 1].lit = 1;
    }
}

/* `cutx=X0:X1:Z0:Z1`: the model's faces lying wholly between Z0 and Z1 in z are cut to X0..X1 in x (left out when
 * wholly outside): a shelf behind a doorway that runs on behind the thin wall beside it */
static void do_cutx(const Ft *f, unsigned nf, RmOut *out, const double c[4])
{
    unsigned i;

    for (i = 0; i < nf; i++) {
        const RmTri *t = f[i].t;
        double a[8][5], b[8][5];
        int j, k, na = 3, nb, s, inz = 1, out_ = 0;

        for (j = 0; j < 3; j++) {
            inz &= t->p[j][2] >= c[2] - 0.05 && t->p[j][2] <= c[3] + 0.05;
            out_ += t->p[j][0] < c[0] - 0.01 || t->p[j][0] > c[1] + 0.01;
            for (k = 0; k < 3; k++) {
                a[j][k] = t->p[j][k];
            }
            a[j][3] = t->uv[j][0];
            a[j][4] = t->uv[j][1];
        }
        if (!inz || out_ == 0) {
            continue;
        }
        rm_drop(out, t->id);
        for (s = 0; s < 2; s++) {
            const double lim = s == 0 ? c[0] : c[1], sg = s == 0 ? 1.0 : -1.0;

            nb = 0;
            for (j = 0; j < na; j++) {
                const double *p = a[j], *q = a[(j + 1) % na];
                const int pin = sg * (p[0] - lim) >= -1e-6, qin = sg * (q[0] - lim) >= -1e-6;

                if (pin && nb < 8) {
                    memcpy(b[nb++], p, sizeof b[0]);
                }
                if (pin != qin && nb < 8) {
                    const double u = (lim - p[0]) / (q[0] - p[0]);

                    for (k = 0; k < 5; k++) {
                        b[nb][k] = p[k] + (q[k] - p[k]) * u;
                    }
                    nb++;
                }
            }
            memcpy(a, b, sizeof a);
            na = nb;
        }
        for (j = 1; j + 1 < na; j++) {
            double pa[3], pb[3], pc[3], ua[2], ub[2], uc[2];

            for (k = 0; k < 3; k++) {
                pa[k] = a[0][k];
                pb[k] = a[j][k];
                pc[k] = a[j + 1][k];
            }
            ua[0] = a[0][3]; ua[1] = a[0][4];
            ub[0] = a[j][3]; ub[1] = a[j][4];
            uc[0] = a[j + 1][3]; uc[1] = a[j + 1][4];
            rm_tri(out, t->id, -1, f[i].b->cull, t->rgb, pa, pb, pc, ua, ub, uc, 0, 0);
        }
    }
}

/* `decal=SZ:SX:SY:X0:X1:Y0:Y1:TX:TY:Z+..`: a rectangle X0..X1, Y0..Y1 of the picture of the model's face that looks
 * south at z = SZ and holds (SX, SY), laid on the back at z = Z looking north, its middle at (TX, TY), the right way
 * round seen from behind: one of the house's own windows in a plain back wall. */
static void do_decal(const Ft *f, unsigned nf, RmOut *out, const double d[10])
{
    const Ft *src = NULL;
    double m[2][3];
    unsigned i;
    int j, k;

    for (i = 0; i < nf && src == NULL; i++) {
        const RmTri *t = f[i].t;
        double den, w0, w1;
        int at = 1;

        if (f[i].n[2] < 0.9 || f[i].b->tex < 0) {
            continue;
        }
        for (j = 0; j < 3; j++) {
            at &= fabs(t->p[j][2] - d[0]) < 0.1;
        }
        den = ((double)t->p[1][1] - t->p[2][1]) * ((double)t->p[0][0] - t->p[2][0])
              + ((double)t->p[2][0] - t->p[1][0]) * ((double)t->p[0][1] - t->p[2][1]);
        if (!at || fabs(den) < 1e-9) {
            continue;
        }
        w0 = (((double)t->p[1][1] - t->p[2][1]) * (d[1] - t->p[2][0]) + ((double)t->p[2][0] - t->p[1][0]) * (d[2] - t->p[2][1])) / den;
        w1 = (((double)t->p[2][1] - t->p[0][1]) * (d[1] - t->p[2][0]) + ((double)t->p[0][0] - t->p[2][0]) * (d[2] - t->p[2][1])) / den;
        if (w0 >= -1e-3 && w1 >= -1e-3 && 1.0 - w0 - w1 >= -1e-3) {
            src = &f[i];
        }
    }
    if (src == NULL) {
        return;
    }
    {
        const RmTri *t = src->t;
        const double x0 = t->p[0][0], y0 = t->p[0][1];
        const double x1 = t->p[1][0] - x0, y1 = t->p[1][1] - y0, x2 = t->p[2][0] - x0, y2 = t->p[2][1] - y0;
        const double det = x1 * y2 - x2 * y1;

        if (fabs(det) < 1e-9) {
            return;
        }
        for (k = 0; k < 2; k++) {
            const double d1 = (double)t->uv[1][k] - t->uv[0][k], d2 = (double)t->uv[2][k] - t->uv[0][k];

            m[k][0] = (d1 * y2 - d2 * y1) / det;
            m[k][1] = (d2 * x1 - d1 * x2) / det;
            m[k][2] = t->uv[0][k] - m[k][0] * x0 - m[k][1] * y0;
        }
    }
    {
        const double outn[3] = { 0, 0, -1 };
        const double cx = (d[3] + d[4]) / 2.0, cy = (d[5] + d[6]) / 2.0;
        double q[4][3], uv[4][2];
        const double sx[4] = { d[3], d[4], d[4], d[3] }, sy[4] = { d[5], d[5], d[6], d[6] };

        for (j = 0; j < 4; j++) {
            /* (seen from behind x runs the other way: the picture's left at the back's high x) */
            q[j][0] = d[7] - (sx[j] - cx);
            q[j][1] = d[8] + (sy[j] - cy);
            q[j][2] = d[9];
            uv[j][0] = m[0][0] * sx[j] + m[0][1] * sy[j] + m[0][2];
            uv[j][1] = m[1][0] * sx[j] + m[1][1] * sy[j] + m[1][2];
        }
        quad(out, src->t->id, src->t->rgb, q, uv, outn);
    }
}

static void do_under(const Ft *f, unsigned nf, RmOut *out, double y)
{
    double pt[256][2], hull[257][2], uv[2] = { 0, 0 };
    unsigned np = 0, nh = 0, i, src = 0;
    const RmBatch *sb = NULL;
    int j, k;

    /* the corners at height y of the upright faces that end there (their feet) */
    for (i = 0; i < nf && np < 250u; i++) {
        const RmTri *t = f[i].t;

        if (fabs(f[i].n[1]) > 0.3) {
            continue;
        }
        for (j = 0; j < 3 && np < 250u; j++) {
            int low = 1;

            for (k = 0; k < 3; k++) {
                low &= t->p[k][1] >= t->p[j][1] - 0.05;
            }
            if (low && fabs(t->p[j][1] - y) < 0.05) {
                pt[np][0] = t->p[j][0];
                pt[np][1] = t->p[j][2];
                np++;
                if (sb == NULL) {
                    sb = f[i].b;
                    src = t->id;
                    uv[0] = t->uv[j][0];
                    uv[1] = t->uv[j][1];
                }
            }
        }
    }
    if (np < 3u || sb == NULL) {
        return;
    }
    /* their outline: the convex hull (monotone chain) */
    for (i = 1; i < np; i++) {
        double a0 = pt[i][0], a1 = pt[i][1];

        for (j = (int)i - 1; j >= 0 && (pt[j][0] > a0 || (pt[j][0] == a0 && pt[j][1] > a1)); j--) {
            pt[j + 1][0] = pt[j][0];
            pt[j + 1][1] = pt[j][1];
        }
        pt[j + 1][0] = a0;
        pt[j + 1][1] = a1;
    }
    for (k = 0; k < 2; k++) {
        const unsigned base = nh;

        for (i = 0; i < np; i++) {
            const unsigned q = k == 0 ? i : np - 1 - i;

            while (nh >= base + 2u) {
                const double cr = (hull[nh - 1][0] - hull[nh - 2][0]) * (pt[q][1] - hull[nh - 2][1])
                                  - (hull[nh - 1][1] - hull[nh - 2][1]) * (pt[q][0] - hull[nh - 2][0]);

                if (cr > 1e-6) {
                    break;
                }
                nh--;
            }
            hull[nh][0] = pt[q][0];
            hull[nh][1] = pt[q][1];
            nh++;
        }
        nh--;
    }
    {
        const double outn[3] = { 0, -1, 0 };
        unsigned first = out->ntri;

        for (i = 1; i + 1 < nh; i++) {
            const double a[3] = { hull[0][0], y, hull[0][1] }, b[3] = { hull[i][0], y, hull[i][1] };
            const double c[3] = { hull[i + 1][0], y, hull[i + 1][1] };
            double e1[3], e2[3], n[3];

            for (k = 0; k < 3; k++) {
                e1[k] = b[k] - a[k];
                e2[k] = c[k] - a[k];
            }
            rm_cross(e1, e2, n);
            if (rm_dot(n, outn) >= 0.0) {
                rm_tri(out, src, -1, 0, sb->rgb, a, b, c, uv, uv, uv, 0, 0);
            } else {
                rm_tri(out, src, -1, 0, sb->rgb, a, c, b, uv, uv, uv, 0, 0);
            }
        }
        for (i = first; i < out->ntri; i++) {
            out->tri[i].lit = 1;
        }
    }
}

/* `rimwalls=Y0:X0:X1:Z0:Z1`: a hole in a level floor (a pool in a roof) whose walls the map has only where the field's
 * camera looks into it: under each open edge of a level face in that box, a wall down to Y0, looking into the hole,
 * in the place of the texture the hole's own biggest wall uses (top row at the top, bottom row at the foot). */
static void do_rimwalls(const Ft *f, unsigned nf, RmOut *out, const double r[5])
{
    const Ft *ref = NULL;
    double best = 0.0, uvt[2] = { 0, 0 }, uvb[2] = { 0, 0 };
    unsigned i, j;
    int k;

    for (i = 0; i < nf; i++) {
        const RmTri *t = f[i].t;
        double e1[3], e2[3], n[3], a;
        int in = 1;

        if (fabs(f[i].n[1]) > 0.1) {
            continue;
        }
        for (j = 0; j < 3u; j++) {
            in &= t->p[j][0] >= r[1] - 0.05 && t->p[j][0] <= r[2] + 0.05 && t->p[j][2] >= r[3] - 0.05 && t->p[j][2] <= r[4] + 0.05;
        }
        for (k = 0; k < 3; k++) {
            e1[k] = (double)t->p[1][k] - t->p[0][k];
            e2[k] = (double)t->p[2][k] - t->p[0][k];
        }
        rm_cross(e1, e2, n);
        a = rm_norm(n);
        if (in && a > best) {
            best = a;
            ref = &f[i];
        }
    }
    if (ref == NULL) {
        return;
    }
    {
        /* (its column: the corners' u, its top and its foot's v) */
        double ytop = -1e30, ybot = 1e30;
        int nt = 0, nb = 0;

        for (j = 0; j < 3u; j++) {
            ytop = ref->t->p[j][1] > ytop ? ref->t->p[j][1] : ytop;
            ybot = ref->t->p[j][1] < ybot ? ref->t->p[j][1] : ybot;
        }
        for (j = 0; j < 3u; j++) {
            if (fabs(ref->t->p[j][1] - ytop) < 0.05) {
                uvt[0] += ref->t->uv[j][0];
                uvt[1] += ref->t->uv[j][1];
                nt++;
            } else if (fabs(ref->t->p[j][1] - ybot) < 0.05) {
                uvb[0] += ref->t->uv[j][0];
                uvb[1] += ref->t->uv[j][1];
                nb++;
            }
        }
        if (nt == 0 || nb == 0) {
            return;
        }
        uvt[0] /= nt; uvt[1] /= nt;
        uvb[0] /= nb; uvb[1] /= nb;
        uvb[0] = uvt[0];
    }
    for (i = 0; i < nf; i++) {
        const RmTri *t = f[i].t;
        double c[3] = { 0, 0, 0 };

        if (f[i].n[1] < 0.9) {
            continue;
        }
        for (j = 0; j < 3u; j++) {
            for (k = 0; k < 3; k++) {
                c[k] += t->p[j][k] / 3.0;
            }
        }
        for (j = 0; j < 3u; j++) {
            const float *a = t->p[j], *b = t->p[(j + 1) % 3u];
            double mid[3], nn[3], p[4][3], uv[4][2];
            unsigned o;
            int shared = 0, in;

            in = a[0] >= r[1] - 0.05 && a[0] <= r[2] + 0.05 && a[2] >= r[3] - 0.05 && a[2] <= r[4] + 0.05
                 && b[0] >= r[1] - 0.05 && b[0] <= r[2] + 0.05 && b[2] >= r[3] - 0.05 && b[2] <= r[4] + 0.05
                 && a[1] > r[0] + 0.5 && fabs(a[1] - b[1]) < 0.05;
            if (!in) {
                continue;
            }
            /* (open: no other face has this edge; a wall already under it shares it) */
            for (o = 0; o < nf && !shared; o++) {
                unsigned q;
                int ha = 0, hb = 0;

                if (o == i) {
                    continue;
                }
                for (q = 0; q < 3u; q++) {
                    ha |= fabs(f[o].t->p[q][0] - a[0]) < 0.05 && fabs(f[o].t->p[q][1] - a[1]) < 0.05 && fabs(f[o].t->p[q][2] - a[2]) < 0.05;
                    hb |= fabs(f[o].t->p[q][0] - b[0]) < 0.05 && fabs(f[o].t->p[q][1] - b[1]) < 0.05 && fabs(f[o].t->p[q][2] - b[2]) < 0.05;
                }
                shared = ha && hb;
            }
            if (shared) {
                continue;
            }
            for (k = 0; k < 3; k++) {
                mid[k] = (a[k] + b[k]) / 2.0;
            }
            /* (looking into the hole: away from the floor face) */
            nn[0] = -(b[2] - a[2]);
            nn[1] = 0.0;
            nn[2] = b[0] - a[0];
            if (nn[0] * (c[0] - mid[0]) + nn[2] * (c[2] - mid[2]) > 0.0) {
                nn[0] = -nn[0];
                nn[2] = -nn[2];
            }
            p[0][0] = a[0]; p[0][1] = a[1]; p[0][2] = a[2];
            p[1][0] = b[0]; p[1][1] = b[1]; p[1][2] = b[2];
            p[2][0] = b[0]; p[2][1] = r[0]; p[2][2] = b[2];
            p[3][0] = a[0]; p[3][1] = r[0]; p[3][2] = a[2];
            uv[0][0] = uvt[0]; uv[0][1] = uvt[1];
            uv[1][0] = uvt[0]; uv[1][1] = uvt[1];
            uv[2][0] = uvb[0]; uv[2][1] = uvb[1];
            uv[3][0] = uvb[0]; uv[3][1] = uvb[1];
            rm_norm(nn);
            quad(out, ref->t->id, ref->t->rgb, p, uv, nn);
        }
    }
}

/* `reside=FROM:TO:Y0:Y1`: a side wall the map painted plain while the other side wears the building's bands: the faces
 * of the side at x = TO between Y0 and Y1 give way to those of the side at x = FROM, mirrored across and cut to Y0..Y1,
 * in the light of the side they replace (one building, one pattern all round). */
static void do_reside(const Ft *f, unsigned nf, RmOut *out, const double r[4])
{
    const double xm = (r[0] + r[1]) / 2.0;
    unsigned char rgb[3] = { 255, 255, 255 };
    double zlo = 1e30, zhi = -1e30;       /* (the side it replaces: as long as that, no longer) */
    unsigned i;
    int got = 0, j, k;

    for (i = 0; i < nf; i++) {
        const RmTri *t = f[i].t;
        int at = 1, in = 1;

        for (j = 0; j < 3; j++) {
            at &= fabs(t->p[j][0] - r[1]) < 0.1;
            in &= t->p[j][1] >= r[2] - 0.05 && t->p[j][1] <= r[3] + 0.05;
        }
        if (at && in && fabs(f[i].n[0]) > 0.9) {
            if (!got) {
                memcpy(rgb, t->rgb, 3);
                got = 1;
            }
            for (j = 0; j < 3; j++) {
                zlo = t->p[j][2] < zlo ? t->p[j][2] : zlo;
                zhi = t->p[j][2] > zhi ? t->p[j][2] : zhi;
            }
            rm_drop(out, t->id);
        }
    }
    if (!got) {
        return;
    }
    for (i = 0; i < nf; i++) {
        const RmTri *t = f[i].t;
        double a[8][5], b[8][5];
        int at = 1, na = 3, nb, s;

        for (j = 0; j < 3; j++) {
            at &= fabs(t->p[j][0] - r[0]) < 0.1;
        }
        if (!at || fabs(f[i].n[0]) < 0.9) {
            continue;
        }
        for (j = 0; j < 3; j++) {
            for (k = 0; k < 3; k++) {
                a[j][k] = t->p[j][k];
            }
            a[j][3] = t->uv[j][0];
            a[j][4] = t->uv[j][1];
        }
        /* (cut to Y0..Y1 and to the replaced side's length: y >= Y0, y <= Y1, z >= its start, z <= its end) */
        for (s = 0; s < 4; s++) {
            const int ax = s < 2 ? 1 : 2;
            const double lim = s == 0 ? r[2] : s == 1 ? r[3] : s == 2 ? zlo : zhi, sg = s % 2 == 0 ? 1.0 : -1.0;

            nb = 0;
            for (j = 0; j < na; j++) {
                const double *p = a[j], *q = a[(j + 1) % na];
                const int pin = sg * (p[ax] - lim) >= -1e-6, qin = sg * (q[ax] - lim) >= -1e-6;

                if (pin && nb < 8) {
                    memcpy(b[nb++], p, sizeof b[0]);
                }
                if (pin != qin && nb < 8) {
                    const double u = (lim - p[ax]) / (q[ax] - p[ax]);

                    for (k = 0; k < 5; k++) {
                        b[nb][k] = p[k] + (q[k] - p[k]) * u;
                    }
                    nb++;
                }
            }
            memcpy(a, b, sizeof a);
            na = nb;
        }
        /* (mirrored across: corners the other way round) */
        for (j = 1; j + 1 < na; j++) {
            double pa[3], pb[3], pc[3], ua[2], ub[2], uc[2];

            for (k = 0; k < 3; k++) {
                pa[k] = a[0][k];
                pb[k] = a[j + 1][k];
                pc[k] = a[j][k];
            }
            pa[0] = 2.0 * xm - pa[0];
            pb[0] = 2.0 * xm - pb[0];
            pc[0] = 2.0 * xm - pc[0];
            ua[0] = a[0][3]; ua[1] = a[0][4];
            ub[0] = a[j + 1][3]; ub[1] = a[j + 1][4];
            uc[0] = a[j][3]; uc[1] = a[j][4];
            rm_tri(out, t->id, -1, f[i].b->cull, rgb, pa, pb, pc, ua, ub, uc, 0, 0);
        }
    }
}

/* `clipz=Z[:X0:X1]`: the model's own faces that reach behind the back line z = Z (a flat picture inside a wall that
 * runs on past the house's back: the field never saw its end) are cut there; wholly behind, left out.  With X0:X1
 * only faces wholly between (the house, not a balcony that stands out past its back on purpose). */
static void do_clip(const Ft *f, unsigned nf, RmOut *out, double zc, double x0, double x1)
{
    unsigned i;

    for (i = 0; i < nf; i++) {
        const RmTri *t = f[i].t;
        double poly[6][5], pz[4][5];
        int j, k, n = 0, behind = 0, in = 1;

        for (j = 0; j < 3; j++) {
            in &= t->p[j][0] >= x0 - 0.05 && t->p[j][0] <= x1 + 0.05;
        }
        if (!in) {
            continue;
        }
        for (j = 0; j < 3; j++) {
            behind += t->p[j][2] < zc - 0.01;
            pz[j][0] = t->p[j][0];
            pz[j][1] = t->p[j][1];
            pz[j][2] = t->p[j][2];
            pz[j][3] = t->uv[j][0];
            pz[j][4] = t->uv[j][1];
        }
        if (behind == 0) {
            continue;
        }
        rm_drop(out, t->id);
        if (behind == 3) {
            continue;
        }
        /* (Sutherland-Hodgman against z >= zc) */
        for (j = 0; j < 3; j++) {
            const double *a = pz[j], *b = pz[(j + 1) % 3];
            const int ain = a[2] >= zc - 0.01, bin = b[2] >= zc - 0.01;

            if (ain) {
                memcpy(poly[n++], a, sizeof poly[0]);
            }
            if (ain != bin) {
                const double s = (zc - a[2]) / (b[2] - a[2]);

                for (k = 0; k < 5; k++) {
                    poly[n][k] = a[k] + (b[k] - a[k]) * s;
                }
                n++;
            }
        }
        for (j = 1; j + 1 < n; j++) {
            double pa[3], pb[3], pc[3], ua[2], ub[2], uc[2];

            for (k = 0; k < 3; k++) {
                pa[k] = poly[0][k];
                pb[k] = poly[j][k];
                pc[k] = poly[j + 1][k];
            }
            ua[0] = poly[0][3]; ua[1] = poly[0][4];
            ub[0] = poly[j][3]; ub[1] = poly[j][4];
            uc[0] = poly[j + 1][3]; uc[1] = poly[j + 1][4];
            rm_tri(out, t->id, -1, f[i].b->cull, t->rgb, pa, pb, pc, ua, ub, uc, 0, 0);
        }
    }
}

/* A new face of the furniture pass, kept so later faces can be tested against it and whole pieces copied with it. */
typedef struct {
    double p[3][3], uv[3][2], n[3];
    unsigned id;
    unsigned char rgb[3], cull;
} Nt;

/* Is the face (corners p, looking along n) already covered: does a point of it (its middle, or two thirds of the way
 * to a corner) lie on one of the faces f or the new faces nt that look the same way, within 0.05 of their plane? */
static int covered(const Ft *f, unsigned nf, const Nt *nt, unsigned nn, const double p[3][3], const double n[3])
{
    double c[3], q[4][3];
    unsigned i;
    int k, s;

    for (k = 0; k < 3; k++) {
        c[k] = (p[0][k] + p[1][k] + p[2][k]) / 3.0;
    }
    for (s = 0; s < 4; s++) {
        for (k = 0; k < 3; k++) {
            q[s][k] = s == 0 ? c[k] : c[k] + (p[s - 1][k] - c[k]) * 0.66;
        }
    }
    for (s = 0; s < 4; s++) {
        if (on_face(f, nf, q[s], n)) {
            return 1;
        }
        for (i = 0; i < nn; i++) {
            double e1[3], e2[3], w[3], a0[3], a1[3], a2[3], c1[3], c2[3], c3[3], d[3];

            if (rm_dot(nt[i].n, n) < 0.9) {
                continue;
            }
            for (k = 0; k < 3; k++) {
                d[k] = q[s][k] - nt[i].p[0][k];
                e1[k] = nt[i].p[1][k] - nt[i].p[0][k];
                e2[k] = nt[i].p[2][k] - nt[i].p[1][k];
                w[k] = nt[i].p[0][k] - nt[i].p[2][k];
                a0[k] = q[s][k] - nt[i].p[0][k];
                a1[k] = q[s][k] - nt[i].p[1][k];
                a2[k] = q[s][k] - nt[i].p[2][k];
            }
            if (fabs(rm_dot(d, nt[i].n)) > 0.05) {
                continue;
            }
            rm_cross(e1, a0, c1);
            rm_cross(e2, a1, c2);
            rm_cross(w, a2, c3);
            if (rm_dot(c1, nt[i].n) >= -1e-3 && rm_dot(c2, nt[i].n) >= -1e-3 && rm_dot(c3, nt[i].n) >= -1e-3) {
                return 1;
            }
        }
    }
    return 0;
}

/* a face turned about the upright through (cx, cz): mode 1 front to back, 2 side to side, 3 both (a half turn) */
static void turned(const double p[3][3], const double uv[3][2], const double n[3], int mode, double cx, double cz,
                   double q[3][3], double quv[3][2], double qn[3])
{
    int j;

    for (j = 0; j < 3; j++) {
        const int s = mode == 3 || j == 0 ? j : 3 - j;      /* (one mirror: corners the other way round) */

        q[j][0] = mode & 2 ? 2.0 * cx - p[s][0] : p[s][0];
        q[j][1] = p[s][1];
        q[j][2] = mode & 1 ? 2.0 * cz - p[s][2] : p[s][2];
        quv[j][0] = uv[s][0];
        quv[j][1] = uv[s][1];
    }
    qn[0] = mode & 2 ? -n[0] : n[0];
    qn[1] = n[1];
    qn[2] = mode & 1 ? -n[2] : n[2];
}

static void add_nt(RmOut *out, Nt *nt, unsigned *nn, unsigned cap, const double p[3][3], const double uv[3][2],
                   const double n[3], unsigned id, const unsigned char rgb[3], unsigned char cull)
{
    int j, k;

    if (*nn >= cap) {
        return;
    }
    for (j = 0; j < 3; j++) {
        for (k = 0; k < 3; k++) {
            nt[*nn].p[j][k] = p[j][k];
        }
        nt[*nn].uv[j][0] = uv[j][0];
        nt[*nn].uv[j][1] = uv[j][1];
        nt[*nn].n[j] = n[j];
        nt[*nn].rgb[j] = rgb[j];
    }
    nt[*nn].id = id;
    nt[*nn].cull = cull;
    (*nn)++;
    rm_tri(out, id, -1, cull, rgb, p[0], p[1], p[2], uv[0], uv[1], uv[2], 0, 0);
}

/* `props=Y0:SIZE`: the small things standing above Y0 (a stool, a planter on a bench; pieces of triangles that share
 * corners, smaller than SIZE across).  The map has only what the field's camera sees of them.
 *   - a piece that is one flat card (a leaf) is shown from both sides, with a copy turned half round where it stands
 *     (from behind a leaf, not the edge of a card that leans to the front);
 *   - every other piece is closed about its own middle: its faces turned front to back, side to side and both, where
 *     it has none (a planter's far side and end, a leg's back);
 *   - a thing (pieces whose boxes touch: a bench, its legs, the planter on it) that is not round gets a whole piece
 *     turned about the thing's middle where the thing has nothing at all (the bench's far legs).  A round thing (a
 *     stool turned on its spot) has all its legs. */
static void do_props(const Ft *f, unsigned nf, RmOut *out, double y0, double size, unsigned modes)
{
    unsigned *grp = (unsigned *)malloc((size_t)(nf ? nf : 1) * sizeof *grp);
    unsigned *thing = (unsigned *)malloc((size_t)(nf ? nf : 1) * sizeof *thing);
    unsigned *cnt = (unsigned *)calloc(nf ? nf : 1, sizeof *cnt), *nt0 = (unsigned *)calloc(nf ? nf : 1, sizeof *nt0);
    unsigned *nt1 = (unsigned *)calloc(nf ? nf : 1, sizeof *nt1);
    unsigned char *use = (unsigned char *)calloc(nf ? nf : 1, 1);
    double (*lo)[3] = (double (*)[3])malloc((size_t)(nf ? nf : 1) * sizeof *lo);
    double (*hi)[3] = (double (*)[3])malloc((size_t)(nf ? nf : 1) * sizeof *hi);
    const unsigned cap = nf * 8u + 16u;
    Nt *nt = (Nt *)malloc((size_t)cap * sizeof *nt);
    Ft *pf = (Ft *)malloc((size_t)(nf ? nf : 1) * sizeof *pf);
    unsigned i, j, g, nn = 0;
    int k;

    if (grp == NULL || thing == NULL || cnt == NULL || nt0 == NULL || nt1 == NULL || use == NULL || lo == NULL
        || hi == NULL || nt == NULL || pf == NULL) {
        out->fail = 1;
        goto done;
    }
    /* (pieces: triangles of one draw that share a corner) */
    for (i = 0; i < nf; i++) {
        grp[i] = i;
        thing[i] = i;
    }
    for (i = 0; i < nf; i++) {
        for (j = i + 1; j < nf; j++) {
            int a, c, touch = 0;

            if (f[i].b != f[j].b) {
                continue;
            }
            for (a = 0; a < 3 && !touch; a++) {
                for (c = 0; c < 3 && !touch; c++) {
                    touch = fabs(f[i].t->p[a][0] - f[j].t->p[c][0]) < 0.01 && fabs(f[i].t->p[a][1] - f[j].t->p[c][1]) < 0.01
                            && fabs(f[i].t->p[a][2] - f[j].t->p[c][2]) < 0.01;
                }
            }
            if (touch) {
                unsigned ri = i, rj = j;

                while (grp[ri] != ri) {
                    ri = grp[ri];
                }
                while (grp[rj] != rj) {
                    rj = grp[rj];
                }
                grp[rj > ri ? rj : ri] = rj > ri ? ri : rj;
            }
        }
    }
    for (i = 0; i < nf; i++) {
        while (grp[grp[i]] != grp[i]) {
            grp[i] = grp[grp[i]];
        }
        grp[i] = grp[grp[i]];
    }
    for (g = 0; g < nf; g++) {
        for (k = 0; k < 3; k++) {
            lo[g][k] = 1e30;
            hi[g][k] = -1e30;
        }
    }
    for (i = 0; i < nf; i++) {
        g = grp[i];
        for (j = 0; j < 3u; j++) {
            for (k = 0; k < 3; k++) {
                lo[g][k] = f[i].t->p[j][k] < lo[g][k] ? f[i].t->p[j][k] : lo[g][k];
                hi[g][k] = f[i].t->p[j][k] > hi[g][k] ? f[i].t->p[j][k] : hi[g][k];
            }
        }
        cnt[g]++;
    }
    /* leaves; and each other piece closed about its own middle */
    for (g = 0; g < nf; g++) {
        const double cx = (lo[g][0] + hi[g][0]) / 2.0, cz = (lo[g][2] + hi[g][2]) / 2.0;
        unsigned np = 0;
        int flat = 1, mode;

        if (cnt[g] == 0 || lo[g][1] < y0 || (hi[g][0] - lo[g][0]) + (hi[g][2] - lo[g][2]) > size) {
            continue;
        }
        for (i = 0; i < nf; i++) {
            if (grp[i] == g) {
                flat &= rm_dot(f[i].n, f[g].n) > 0.99;
                pf[np++] = f[i];
            }
        }
        if (flat && np <= 4u) {
            for (i = 0; i < np; i++) {
                rm_two(out, pf[i].t->id);
                if (fabs(pf[i].n[2]) > 0.3) {
                    double p[3][3], uv[3][2], q[3][3], quv[3][2], qn[3];

                    for (j = 0; j < 3u; j++) {
                        for (k = 0; k < 3; k++) {
                            p[j][k] = pf[i].t->p[j][k];
                        }
                        uv[j][0] = pf[i].t->uv[j][0];
                        uv[j][1] = pf[i].t->uv[j][1];
                    }
                    turned(p, uv, pf[i].n, 3, cx, cz, q, quv, qn);
                    /* (a quarter unit out along its facing: a leaf turned round may land on a wall or post's face) */
                    for (j = 0; j < 3u; j++) {
                        for (k = 0; k < 3; k++) {
                            q[j][k] += qn[k] * 0.25;
                        }
                    }
                    rm_tri(out, pf[i].t->id, -1, 0, pf[i].t->rgb, q[0], q[1], q[2], quv[0], quv[1], quv[2], 0, 0);
                }
            }
            continue;
        }
        if (flat) {
            continue;                   /* (a flat piece of more than a card: a shadow, a mat; nothing to close) */
        }
        if (pf[0].b->cull == 0) {
            continue;                   /* (its draw is shown from both sides already (vines): no undrawn side) */
        }
        {
            /* (a piece standing turned on its spot (a lounger set at a slant, a stool turned): mirrored along x and
             * z it would cross itself; its faces are shown from both sides instead, thin slats and legs as they are) */
            unsigned up = 0, off = 0;

            for (i = 0; i < np; i++) {
                if (fabs(pf[i].n[1]) < 0.7) {
                    const double h = sqrt(pf[i].n[0] * pf[i].n[0] + pf[i].n[2] * pf[i].n[2]);

                    up++;
                    off += h > 1e-6 && fabs(pf[i].n[0]) / h < 0.97 && fabs(pf[i].n[2]) / h < 0.97;
                }
            }
            if (up > 0 && off * 2u > up) {
                for (i = 0; i < np; i++) {
                    rm_two(out, pf[i].t->id);
                }
                continue;
            }
        }
        use[g] = 1;
        nt0[g] = nn;
        for (mode = 1; mode <= 3; mode++) {
            if (!(modes & (1u << (mode - 1)))) {
                continue;       /* (`props=..:..:MODES`: 1 front to back, 2 side to side, 4 both) */
            }
            for (i = 0; i < np; i++) {
                double p[3][3], uv[3][2], q[3][3], quv[3][2], qn[3];

                for (j = 0; j < 3u; j++) {
                    for (k = 0; k < 3; k++) {
                        p[j][k] = pf[i].t->p[j][k];
                    }
                    uv[j][0] = pf[i].t->uv[j][0];
                    uv[j][1] = pf[i].t->uv[j][1];
                }
                turned(p, uv, pf[i].n, mode, cx, cz, q, quv, qn);
                {
                    /* (a face that closes a box-like piece lies on its outer box: its middle on one of the box's
                     * sides; a slab that leans (a chair's back) turned about its middle crosses itself instead) */
                    double c[3];
                    int on = 0;

                    for (k = 0; k < 3; k++) {
                        c[k] = (q[0][k] + q[1][k] + q[2][k]) / 3.0;
                    }
                    for (k = 0; k < 3 && !on; k++) {
                        const double tol = (hi[g][k] - lo[g][k]) * 0.15 > 0.6 ? (hi[g][k] - lo[g][k]) * 0.15 : 0.6;

                        on = fabs(c[k] - lo[g][k]) < tol || fabs(c[k] - hi[g][k]) < tol;
                    }
                    if (!on) {
                        continue;
                    }
                }
                if (!covered(f, nf, nt + nt0[g], nn - nt0[g], q, qn)) {   /* (any face of the model: a post's front and back can be pieces apart) */
                    add_nt(out, nt, &nn, cap, q, quv, qn, pf[i].t->id, pf[i].t->rgb, pf[i].b->cull);
                }
            }
        }
        nt1[g] = nn;
    }
    /* things: pieces whose boxes touch */
    for (g = 0; g < nf; g++) {
        for (j = g + 1; use[g] && j < nf; j++) {
            unsigned rg = g, rj = j;
            int touch = use[j];

            for (k = 0; touch && k < 3; k++) {
                touch = lo[g][k] <= hi[j][k] + 0.5 && lo[j][k] <= hi[g][k] + 0.5;
            }
            if (!touch) {
                continue;
            }
            while (thing[rg] != rg) {
                rg = thing[rg];
            }
            while (thing[rj] != rj) {
                rj = thing[rj];
            }
            thing[rj > rg ? rj : rg] = rj > rg ? rg : rj;
        }
    }
    for (g = 0; g < nf; g++) {
        unsigned r = g, m, seen = 0, dirs = 0, nplaced = 0;
        double tl[3] = { 1e30, 1e30, 1e30 }, th[3] = { -1e30, -1e30, -1e30 }, cx, cz, placed[64][4];
        int mode;

        if (!use[g]) {
            continue;
        }
        while (thing[r] != r) {
            r = thing[r];
        }
        if (r != g) {
            continue;                   /* (each thing once, at its first piece) */
        }
        for (m = 0; m < nf; m++) {
            unsigned rm = m;

            if (!use[m]) {
                continue;
            }
            while (thing[rm] != rm) {
                rm = thing[rm];
            }
            if (rm != r) {
                continue;
            }
            for (k = 0; k < 3; k++) {
                tl[k] = lo[m][k] < tl[k] ? lo[m][k] : tl[k];
                th[k] = hi[m][k] > th[k] ? hi[m][k] : th[k];
            }
            for (i = 0; i < nf; i++) {
                if (grp[i] == m && fabs(f[i].n[1]) < 0.5) {
                    const double a = atan2(f[i].n[2], f[i].n[0]);

                    seen |= 1u << ((unsigned)((a + 3.14159265358979) / (2.0 * 3.14159265358979) * 12.0) % 12u);
                }
            }
        }
        for (i = 0; i < 12u; i++) {
            dirs += (seen >> i) & 1u;
        }
        if (dirs >= 7u) {
            continue;                   /* (round: its legs are all there) */
        }
        cx = (tl[0] + th[0]) / 2.0;
        cz = (tl[2] + th[2]) / 2.0;
        for (m = 0; m < nf; m++) {
            unsigned rm = m;

            if (!use[m]) {
                continue;
            }
            while (thing[rm] != rm) {
                rm = thing[rm];
            }
            if (rm != r) {
                continue;
            }
            if (lo[m][1] > tl[1] + 0.5) {
                continue;               /* (only what stands on the thing's floor: legs; a backrest is one of a kind) */
            }
            for (mode = 1; mode <= 3; mode++) {
                if (!(modes & (1u << (mode - 1)))) {
                    continue;
                }
                /* the piece turned about the thing's middle: where the thing has no piece at all, it goes there */
                const double ml0 = mode & 2 ? 2.0 * cx - hi[m][0] : lo[m][0], mh0 = mode & 2 ? 2.0 * cx - lo[m][0] : hi[m][0];
                const double ml2 = mode & 1 ? 2.0 * cz - hi[m][2] : lo[m][2], mh2 = mode & 1 ? 2.0 * cz - lo[m][2] : hi[m][2];
                unsigned o;
                int free_ = 1;

                for (o = 0; o < nf && free_; o++) {
                    unsigned ro = o;

                    if (!use[o]) {
                        continue;
                    }
                    while (thing[ro] != ro) {
                        ro = thing[ro];
                    }
                    if (ro != r) {
                        continue;
                    }
                    free_ = !(ml0 < hi[o][0] - 0.2 && lo[o][0] < mh0 - 0.2 && ml2 < hi[o][2] - 0.2 && lo[o][2] < mh2 - 0.2
                              && lo[m][1] <= hi[o][1] + 0.05 && lo[o][1] <= hi[m][1] + 0.05);
                }
                for (o = 0; o < nplaced && free_; o++) {
                    free_ = !(ml0 < placed[o][1] - 0.2 && placed[o][0] < mh0 - 0.2 && ml2 < placed[o][3] - 0.2
                              && placed[o][2] < mh2 - 0.2);
                }
                if (!free_ || nplaced >= 64u) {
                    continue;
                }
                /* (a copy placed: the next turn of another piece does not go there too) */
                placed[nplaced][0] = ml0;
                placed[nplaced][1] = mh0;
                placed[nplaced][2] = ml2;
                placed[nplaced][3] = mh2;
                nplaced++;
                /* (its own faces and those the piece's closing added) */
                for (i = 0; i < nf; i++) {
                    double p[3][3], uv[3][2], q[3][3], quv[3][2], qn[3];

                    if (grp[i] != m) {
                        continue;
                    }
                    for (j = 0; j < 3u; j++) {
                        for (k = 0; k < 3; k++) {
                            p[j][k] = f[i].t->p[j][k];
                        }
                        uv[j][0] = f[i].t->uv[j][0];
                        uv[j][1] = f[i].t->uv[j][1];
                    }
                    turned(p, uv, f[i].n, mode, cx, cz, q, quv, qn);
                    if (!covered(f, nf, nt, nn, q, qn)) {
                        add_nt(out, nt, &nn, cap, q, quv, qn, f[i].t->id, f[i].t->rgb, f[i].b->cull);
                    }
                }
                for (i = nt0[m]; i < nt1[m]; i++) {
                    double q[3][3], quv[3][2], qn[3];
                    Nt src = nt[i];

                    turned(src.p, src.uv, src.n, mode, cx, cz, q, quv, qn);
                    if (!covered(f, nf, nt, nn, q, qn)) {
                        add_nt(out, nt, &nn, cap, q, quv, qn, src.id, src.rgb, src.cull);
                    }
                }
            }
        }
    }
done:
    free(grp); free(thing); free(cnt); free(nt0); free(nt1); free(use); free(lo); free(hi); free(nt); free(pf);
}

void rm_tower(const RmBatch *b, unsigned nb, const RmTex *tex, unsigned ntex, const char *opt, RmOut *out)
{
    const double cw = rm_optf(opt, "corner", 6.0);
    unsigned bi, i, nf = 0, cap = 0;
    Ft *f;
    RmTri *fixed = NULL;
    unsigned nsnap = 0;
    char buf[512];
    const char *ep;

    for (bi = 0; bi < nb; bi++) {
        cap += b[bi].n;
    }
    f = (Ft *)malloc((size_t)(cap ? cap : 1) * sizeof *f);
    if (f == NULL) {
        out->fail = 1;
        return;
    }
    for (bi = 0; bi < nb; bi++) {
        for (i = 0; !b[bi].bb && i < b[bi].n; i++) {
            f[nf].t = &b[bi].tri[i];
            f[nf].b = &b[bi];
            normal_of(f[nf].t, f[nf].n);
            nf++;
        }
    }
    /* `snap=X:Y:Z:NZ+..`: a corner of the model at (X, Y, Z) that is out of line with its part (the map's own slip:
     * a post whose top corner stands 4 units behind the rest) goes to z = NZ: its triangles are left out and drawn
     * again with it moved, and every later pass sees the moved one */
    if (rm_opt(opt, "snap", buf, sizeof buf) != NULL && nf > 0) {
        fixed = (RmTri *)malloc((size_t)nf * sizeof *fixed);
        if (fixed == NULL) {
            free(f);
            out->fail = 1;
            return;
        }
        for (ep = buf; ep != NULL && *ep != '\0'; ) {
            double sp[4];

            if (sscanf(ep, "%lf:%lf:%lf:%lf", &sp[0], &sp[1], &sp[2], &sp[3]) == 4) {
                for (i = 0; i < nf; i++) {
                    int j, hit = -1;

                    for (j = 0; j < 3; j++) {
                        if (fabs(f[i].t->p[j][0] - sp[0]) < 0.05 && fabs(f[i].t->p[j][1] - sp[1]) < 0.05
                            && fabs(f[i].t->p[j][2] - sp[2]) < 0.05) {
                            hit = j;
                        }
                    }
                    if (hit < 0) {
                        continue;
                    }
                    if (f[i].t != &fixed[i]) {
                        fixed[i] = *f[i].t;
                        f[i].t = &fixed[i];
                    }
                    fixed[i].p[hit][2] = (float)sp[3];
                    normal_of(f[i].t, f[i].n);
                    nsnap++;
                }
            }
            ep = strchr(ep, '+');
            ep = ep != NULL ? ep + 1 : NULL;
        }
        for (i = 0; i < nf; i++) {
            if (f[i].t == &fixed[i]) {
                double pa[3][3], ua[3][2];
                int j, k;

                for (j = 0; j < 3; j++) {
                    for (k = 0; k < 3; k++) {
                        pa[j][k] = fixed[i].p[j][k];
                    }
                    ua[j][0] = fixed[i].uv[j][0];
                    ua[j][1] = fixed[i].uv[j][1];
                }
                rm_drop(out, fixed[i].id);
                rm_tri(out, fixed[i].id, -1, f[i].b->cull, fixed[i].rgb, pa[0], pa[1], pa[2], ua[0], ua[1], ua[2], 0, 0);
                out->tri[out->ntri - 1].lit = 1;
            }
        }
    }
    if (rm_opt(opt, "clipz", buf, sizeof buf) != NULL) {
        for (ep = buf; ep != NULL && *ep != '\0'; ) {
            double cz = 0.0, x0 = -1e30, x1 = 1e30;

            if (sscanf(ep, "%lf:%lf:%lf", &cz, &x0, &x1) >= 1) {
                do_clip(f, nf, out, cz, x0, x1);
            }
            ep = strchr(ep, '+');
            ep = ep != NULL ? ep + 1 : NULL;
        }
    }
    /* (`twofirst=1`: before halfturn and mirror, so their copies of two-sided faces are two-sided too) */
    if (rm_opt(opt, "twofirst", buf, sizeof buf) != NULL && atoi(buf) != 0 && rm_opt(opt, "two", buf, sizeof buf) != NULL) {
        do_two(f, nf, out, buf);
    }
    if (rm_opt(opt, "halfturn", buf, sizeof buf) != NULL) {
        for (ep = buf; ep != NULL && *ep != '\0'; ) {
            double h[13] = { 0, 0, 0, 0, 0, -1e30, 1e30, -1e30, 1e30, -1, 0.1, -1e30, 1e30 };

            if (sscanf(ep, "%lf:%lf:%lf:%lf:%lf:%lf:%lf:%lf:%lf:%lf:%lf:%lf:%lf", &h[0], &h[1], &h[2], &h[3], &h[4],
                       &h[5], &h[6], &h[7], &h[8], &h[9], &h[10], &h[11], &h[12]) >= 5) {
                do_halfturn(f, nf, out, h);
            }
            ep = strchr(ep, '+');
            ep = ep != NULL ? ep + 1 : NULL;
        }
    }
    if (rm_opt(opt, "cutx", buf, sizeof buf) != NULL) {
        for (ep = buf; ep != NULL && *ep != '\0'; ) {
            double cx4[4];

            if (sscanf(ep, "%lf:%lf:%lf:%lf", &cx4[0], &cx4[1], &cx4[2], &cx4[3]) == 4) {
                do_cutx(f, nf, out, cx4);
            }
            ep = strchr(ep, '+');
            ep = ep != NULL ? ep + 1 : NULL;
        }
    }
    if (rm_opt(opt, "mirror", buf, sizeof buf) != NULL) {
        for (ep = buf; ep != NULL && *ep != '\0'; ) {
            double bx[10] = { 0, 0, 0, 0, 0, 0, 0, 0, -1e30, 1e30 };

            if (sscanf(ep, "%lf:%lf:%lf:%lf:%lf:%lf:%lf:%lf:%lf:%lf", &bx[0], &bx[1], &bx[2], &bx[3], &bx[4], &bx[5],
                       &bx[6], &bx[7], &bx[8], &bx[9]) >= 5) {
                do_mirror(f, nf, out, bx);
            }
            ep = strchr(ep, '+');
            ep = ep != NULL ? ep + 1 : NULL;
        }
    }
    if (rm_opt(opt, "wall", buf, sizeof buf) != NULL) {
        for (ep = buf; ep != NULL && *ep != '\0'; ) {
            double w[6];

            if (sscanf(ep, "%lf:%lf:%lf:%lf:%lf:%lf", &w[0], &w[1], &w[2], &w[3], &w[4], &w[5]) == 6) {
                do_wall(f, nf, out, w, cw);
            }
            ep = strchr(ep, '+');
            ep = ep != NULL ? ep + 1 : NULL;
        }
    }
    if (rm_opt(opt, "reside", buf, sizeof buf) != NULL) {
        double r[4];

        if (sscanf(buf, "%lf:%lf:%lf:%lf", &r[0], &r[1], &r[2], &r[3]) == 4) {
            do_reside(f, nf, out, r);
        }
    }
    if (rm_opt(opt, "rimwalls", buf, sizeof buf) != NULL) {
        double r[5];

        if (sscanf(buf, "%lf:%lf:%lf:%lf:%lf", &r[0], &r[1], &r[2], &r[3], &r[4]) == 5) {
            do_rimwalls(f, nf, out, r);
        }
    }
    if (!(rm_opt(opt, "twofirst", buf, sizeof buf) != NULL && atoi(buf) != 0) && rm_opt(opt, "two", buf, sizeof buf) != NULL) {
        do_two(f, nf, out, buf);
    }
    if (rm_opt(opt, "props", buf, sizeof buf) != NULL) {
        double y0 = 0.0, size = 0.0, md = 7.0;

        if (sscanf(buf, "%lf:%lf:%lf", &y0, &size, &md) >= 2) {
            do_props(f, nf, out, y0, size, (unsigned)md);
        }
    }
    if (rm_opt(opt, "panel", buf, sizeof buf) != NULL) {
        for (ep = buf; ep != NULL && *ep != '\0'; ) {
            double pp[9];
            const int got = sscanf(ep, "%lf:%lf:%lf:%lf:%lf:%lf:%lf:%lf:%lf", &pp[0], &pp[1], &pp[2], &pp[3], &pp[4], &pp[5], &pp[6], &pp[7], &pp[8]);

            if (got >= 8) {
                if (got == 8) {
                    pp[8] = pp[5];
                }
                do_panel(f, nf, out, pp, tex, ntex);
            }
            ep = strchr(ep, '+');
            ep = ep != NULL ? ep + 1 : NULL;
        }
    }
    if (rm_opt(opt, "panelx", buf, sizeof buf) != NULL) {
        for (ep = buf; ep != NULL && *ep != '\0'; ) {
            double pp[9];

            if (sscanf(ep, "%lf:%lf:%lf:%lf:%lf:%lf:%lf:%lf:%lf", &pp[0], &pp[1], &pp[2], &pp[3], &pp[4], &pp[5], &pp[6], &pp[7], &pp[8]) == 9) {
                do_panelx(f, nf, out, pp);
            }
            ep = strchr(ep, '+');
            ep = ep != NULL ? ep + 1 : NULL;
        }
    }
    if (rm_opt(opt, "panelz", buf, sizeof buf) != NULL) {
        for (ep = buf; ep != NULL && *ep != '\0'; ) {
            double pp[11] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
            const int got = sscanf(ep, "%lf:%lf:%lf:%lf:%lf:%lf:%lf:%lf:%lf:%lf:%lf", &pp[0], &pp[1], &pp[2], &pp[3], &pp[4], &pp[5], &pp[6], &pp[7], &pp[8], &pp[9], &pp[10]);

            if (got >= 8) {
                if (got == 8) {
                    pp[8] = pp[5];
                }
                do_panelz(f, nf, out, pp);
            }
            ep = strchr(ep, '+');
            ep = ep != NULL ? ep + 1 : NULL;
        }
    }
    if (rm_opt(opt, "retex", buf, sizeof buf) != NULL) {
        for (ep = buf; ep != NULL && *ep != '\0'; ) {
            double rr[9] = { 0, 0, 0, 0, 0, 0, 0, 0, 0 };

            if (sscanf(ep, "%lf:%lf:%lf:%lf:%lf:%lf:%lf:%lf:%lf", &rr[0], &rr[1], &rr[2], &rr[3], &rr[4], &rr[5], &rr[6], &rr[7], &rr[8]) >= 8) {
                do_retex(f, nf, out, 0, rr);
            }
            ep = strchr(ep, '+');
            ep = ep != NULL ? ep + 1 : NULL;
        }
    }
    if (rm_opt(opt, "uclamp", buf, sizeof buf) != NULL) {
        /* `uclamp=X0:X1:Y0:Y1:ZMAX:UMIN:UMAX+..`: new faces in that box keep their picture, their corners' u held
         * within UMIN..UMAX: a picture's edge column (a door frame's corner) left out; v untouched, trims stay lined up */
        for (ep = buf; ep != NULL && *ep != '\0'; ) {
            double uc[7];

            if (sscanf(ep, "%lf:%lf:%lf:%lf:%lf:%lf:%lf", &uc[0], &uc[1], &uc[2], &uc[3], &uc[4], &uc[5], &uc[6]) == 7) {
                for (i = 0; i < out->ntri; i++) {
                    RmNew *nw = &out->tri[i];
                    int j, in = 1;

                    for (j = 0; j < 3; j++) {
                        in &= nw->p[j][0] >= uc[0] - 0.05 && nw->p[j][0] <= uc[1] + 0.05 && nw->p[j][1] >= uc[2] - 0.05
                              && nw->p[j][1] <= uc[3] + 0.05 && nw->p[j][2] <= uc[4];
                    }
                    for (j = 0; in && j < 3; j++) {
                        nw->uv[j][0] = nw->uv[j][0] < uc[5] ? (float)uc[5] : nw->uv[j][0] > uc[6] ? (float)uc[6] : nw->uv[j][0];
                    }
                }
            }
            ep = strchr(ep, '+');
            ep = ep != NULL ? ep + 1 : NULL;
        }
    }
    if (rm_opt(opt, "shell", buf, sizeof buf) != NULL) {
        for (ep = buf; ep != NULL && *ep != '\0'; ) {
            double sh[8];

            if (sscanf(ep, "%lf:%lf:%lf:%lf:%lf:%lf:%lf:%lf", &sh[0], &sh[1], &sh[2], &sh[3], &sh[4], &sh[5], &sh[6], &sh[7]) == 8) {
                do_shell(f, nf, out, sh);
            }
            ep = strchr(ep, '+');
            ep = ep != NULL ? ep + 1 : NULL;
        }
    }
    if (rm_opt(opt, "hide", buf, sizeof buf) != NULL) {
        /* `hide=X0:X1:Y0:Y1:Z0:Z1:TEX+..`: the model's faces of texture TEX whose middle lies in the box are left out */
        for (ep = buf; ep != NULL && *ep != '\0'; ) {
            double h[7];

            if (sscanf(ep, "%lf:%lf:%lf:%lf:%lf:%lf:%lf", &h[0], &h[1], &h[2], &h[3], &h[4], &h[5], &h[6]) == 7) {
                for (i = 0; i < nf; i++) {
                    const RmTri *t = f[i].t;
                    const double cx = ((double)t->p[0][0] + t->p[1][0] + t->p[2][0]) / 3.0;
                    const double cy = ((double)t->p[0][1] + t->p[1][1] + t->p[2][1]) / 3.0;
                    const double cz = ((double)t->p[0][2] + t->p[1][2] + t->p[2][2]) / 3.0;

                    if (f[i].b->tex == (int)h[6] && cx >= h[0] && cx <= h[1] && cy >= h[2] && cy <= h[3] && cz >= h[4] && cz <= h[5]) {
                        rm_drop(out, t->id);
                    }
                }
            }
            ep = strchr(ep, '+');
            ep = ep != NULL ? ep + 1 : NULL;
        }
    }
    if (rm_opt(opt, "decal", buf, sizeof buf) != NULL) {
        for (ep = buf; ep != NULL && *ep != '\0'; ) {
            double dc[10];

            if (sscanf(ep, "%lf:%lf:%lf:%lf:%lf:%lf:%lf:%lf:%lf:%lf", &dc[0], &dc[1], &dc[2], &dc[3], &dc[4], &dc[5], &dc[6], &dc[7], &dc[8], &dc[9]) == 10) {
                do_decal(f, nf, out, dc);
            }
            ep = strchr(ep, '+');
            ep = ep != NULL ? ep + 1 : NULL;
        }
    }
    if (rm_opt(opt, "under", buf, sizeof buf) != NULL) {
        for (ep = buf; ep != NULL && *ep != '\0'; ) {
            do_under(f, nf, out, atof(ep));
            ep = strchr(ep, '+');
            ep = ep != NULL ? ep + 1 : NULL;
        }
    }
    if (rm_optf(opt, "twoback", 1e30) < 1e29) {
        /* `twoback=Z`: a back wall the map has only from inside (an upright face behind Z that looks south, at the
         * field's camera over the lower roofs): shown from both sides, its outside is the back */
        const double zb = rm_optf(opt, "twoback", 0.0);

        for (i = 0; i < nf; i++) {
            int j, in = 1;

            for (j = 0; j < 3; j++) {
                in &= f[i].t->p[j][2] < zb;
            }
            if (in && fabs(f[i].n[1]) < 0.3 && f[i].n[2] > 0.7) {
                rm_two(out, f[i].t->id);
            }
        }
    }
    free(f);
    free(fixed);
    (void)nsnap;
    /* (`seal=0`: no dark backs: a missing face shows as an open side, for finding them) */
    if (rm_optf(opt, "seal", 1) != 0.0) {
        rm_seal(b, nb, out, 0.7);
    }
}
