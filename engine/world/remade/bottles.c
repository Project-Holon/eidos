/*
 * engine/world/remade/bottles.c -- round things drawn as one leaning flat
 * picture: the Dreamyard's bottles (a green one standing, a brown one lying
 * behind it), as turned bodies.
 *
 * Settings (picture pixels; a pixel is a map unit when the picture is as
 * wide as its card):
 *   stand=C:R0:H+H+..  a standing thing about the upright through the middle
 *                      of pixel column C, from picture row R0 down: each
 *                      row's half width in pixels beside the middle one
 *   lie=R:C0:C1:W      a thing lying on the floor behind, about the level
 *                      line through the top of pixel row R, picture columns
 *                      C0..C1, continued W columns west with the first
 *                      column's pixels (the part the standing thing hides);
 *                      a column is as thick as its `own` pixels
 *   own=RRGGBB+..      the lying thing's colours
 *   line=RRGGBB+..     the picture's drawn outline and one-sided shade.  It
 *                      is the edge of a flat drawing, not a stripe down the
 *                      thing's sides: a side there takes the pixel next to
 *                      it, and a row that is all outline the row over it
 *
 * A row of the standing thing is a ring of 12 flat sides: the row's pixels
 * from its middle to its edge go once round a quarter, so the front shows
 * the picture as it is and the back the same from behind.  The lying thing
 * the same, turned: a column a ring, its pixels from the top one (the side
 * that faces up and away) to the bottom one; its ends are plain.  Under them
 * a see-through black shadow as the game draws a Pokemon's, on the floor.
 * All of it rests on the floor under the picture.
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "remade.h"

#define N 12
#define PI 3.14159265358979323846
#define MAXPX 64

typedef struct {
    RmOut *o;
    unsigned src;
    unsigned char rgb[3];
    const RmTex *pic;
    int kind_alpha;             /* 0: the body; else the shadow's alpha */
} Bo;

static const unsigned char *px(const RmTex *t, int c, int r)
{
    return t->rgba + ((size_t)r * (size_t)t->w + (size_t)c) * 4u;
}

/* A four-cornered face toward `outward`, in the picture's pixel (c, r): its
 * two triangles, those with an area. */
static void quad(const Bo *s, const double a[3], const double b[3], const double c[3], const double d[3], int pc,
                 int pr, const double outward[3])
{
    static const unsigned char black[3] = { 0, 0, 0 };
    const double *q[4] = { a, b, c, d };
    double e1[3], e2[3], n1[3], n2[3], uv[2];
    int t;

    uv[0] = (pc + 0.5) / s->pic->w;
    uv[1] = (pr + 0.5) / s->pic->h;
    rm_add(b, a, -1.0, e1);
    rm_add(c, a, -1.0, e2);
    rm_cross(e1, e2, n1);
    rm_add(d, a, -1.0, e1);
    rm_cross(e2, e1, n2);
    if (rm_dot(n1, outward) < 0.0 || rm_dot(n2, outward) < 0.0) {
        q[0] = d; q[1] = c; q[2] = b; q[3] = a;
    }
    for (t = 0; t < 2; t++) {
        const double *x = q[0], *y = q[1 + t], *z = q[2 + t];
        double n[3];

        rm_add(y, x, -1.0, e1);
        rm_add(z, x, -1.0, e2);
        rm_cross(e1, e2, n);
        if (sqrt(rm_dot(n, n)) > 1e-6) {
            if (s->kind_alpha) {
                rm_tri(s->o, s->src, -1, 0, black, x, y, z, uv, uv, uv, 2, s->kind_alpha);
            } else {
                rm_tri(s->o, s->src, -1, 1, s->rgb, x, y, z, uv, uv, uv, 2, 0);
            }
        }
    }
}

static int cell_cmp(const void *a, const void *b)
{
    const int *x = (const int *)a, *y = (const int *)b;

    return x[0] != y[0] ? x[0] - y[0] : x[1] - y[1];
}

void rm_bottles(const RmBatch *b, unsigned nb, const RmTex *tex, unsigned ntex, const char *opt, RmOut *out)
{
    const double up[3] = { 0.0, 1.0, 0.0 };
    char sb[256], lb[64], cb[128];
    const char *stand = rm_opt(opt, "stand", sb, sizeof sb), *lie = rm_opt(opt, "lie", lb, sizeof lb);
    unsigned own[8], line[8], nown, nline, bi;

    nown = rm_colours(rm_opt(opt, "own", cb, sizeof cb), own, 8);
    nline = rm_colours(rm_opt(opt, "line", cb, sizeof cb), line, 8);
    for (bi = 0; bi < nb; bi++) {
        const RmBatch *bt = &b[bi];
        const RmTex *pic;
        double m[3][3], top[3], bot[3], ex[3], back[3], base0[3], unit, rmax = 0.0;
        int (*cells)[2] = NULL;
        unsigned ncell = 0, ccell = 0, first = out->ntri, i;
        int W, H, k, c, r;
        Bo s;

#define AT(cc, rr, o) rm_at(m, (double)(cc) / W, (double)(rr) / H, o)
#define SOLID(cc, rr) ((cc) >= 0 && (cc) < W && (rr) >= 0 && (rr) < H && px(pic, cc, rr)[3] >= 128)
#define MINE(cc, rr) (SOLID(cc, rr) && rm_has_colour(own, nown, px(pic, cc, rr)))
#define CELL(ix, iz) do { \
            if (ncell == ccell) { \
                int (*nw)[2] = (int (*)[2])realloc(cells, (ccell = ccell ? ccell * 2u : 256u) * sizeof *cells); \
                if (nw == NULL) { out->fail = 1; free(cells); return; } \
                cells = nw; \
            } \
            cells[ncell][0] = (ix); cells[ncell][1] = (iz); ncell++; \
        } while (0)

        if (bt->n != 2 || bt->tex < 0 || (unsigned)bt->tex >= ntex || tex[bt->tex].rgba == NULL || rm_plane(&bt->tri[0], m) != 0) {
            continue;
        }
        pic = &tex[bt->tex];
        W = pic->w;
        H = pic->h;
        if (W > MAXPX || H > MAXPX) {
            continue;
        }
        AT(W / 2.0, 0, top);
        AT(W / 2.0, H, bot);
        if (top[1] < bot[1]) {
            continue;                   /* the picture hangs head down: not handled */
        }
        {
            double a[3], e[3];

            AT(W, H, a);
            AT(0, H, e);
            rm_add(a, e, -1.0, ex);
            unit = sqrt(rm_dot(ex, ex)) / W;        /* a pixel in map units */
            rm_norm(ex);
        }
        rm_add(top, bot, -1.0, back);
        back[1] = 0.0;
        if (rm_norm(back) <= 0.0) {     /* level, the way the card leans: away from the field's camera */
            continue;
        }
        AT(0, H, base0);                /* the floor under the picture's lower left corner */
        s.o = out;
        s.src = bt->tri[0].id;
        s.rgb[0] = bt->rgb[0]; s.rgb[1] = bt->rgb[1]; s.rgb[2] = bt->rgb[2];
        s.pic = pic;
        s.kind_alpha = 0;
        if (stand != NULL) {
            int C = atoi(stand), R0, R1, half[MAXPX], nh = 0;
            const char *p = strchr(stand, ':');
            double foot[3];

            R0 = p != NULL ? atoi(p + 1) : 0;
            p = p != NULL ? strchr(p + 1, ':') : NULL;
            memset(half, 0, sizeof half);
            while (p != NULL && R0 + nh < H && R0 + nh < MAXPX) {
                half[R0 + nh++] = atoi(p + 1);
                p = strchr(p + 1, '+');
            }
            if (nh == 0 || C < 0 || C >= W || R0 < 0) {
                continue;
            }
            R1 = R0 + nh - 1;
            for (r = R0; r <= R1; r++) {
                rmax = half[r] + 0.5 > rmax ? half[r] + 0.5 : rmax;
            }
            rm_add(base0, ex, (C + 0.5) * unit, foot);
#define ROWP(rr, o) do { \
                o = (rr); \
                while (o > R0 && rm_has_colour(line, nline, px(pic, C, o))) { o--; } \
            } while (0)
#define RING(rr, kk, o) do { \
                double an_ = 2.0 * PI * ((kk) - 0.5) / N, rd_ = (half[rr] + 0.5) * unit; \
                int q_; \
                for (q_ = 0; q_ < 3; q_++) { \
                    (o)[q_] = foot[q_] + up[q_] * (R1 + 1 - (rr)) * unit + (ex[q_] * sin(an_) - back[q_] * cos(an_)) * rd_; \
                } \
            } while (0)
            for (r = R0; r <= R1; r++) {
                int nxt = r > R0 ? half[r - 1] : -1, rp;

                ROWP(r, rp);
                for (k = 0; k < N; k++) {
                    double phi = 2.0 * PI * k / N, a0[3], a1[3], d0[3], d1[3], outward[3];
                    int h0 = half[r] < half[rp] ? half[r] : half[rp], pc, q;

                    pc = C + (int)rint(rm_wave((double)k / N) * h0);
                    while (pc != C && rm_has_colour(line, nline, px(pic, pc, rp))) {
                        pc += pc < C ? 1 : -1;
                    }
                    RING(r, k, a0);
                    RING(r, k + 1, a1);
                    rm_add(a1, up, -unit, d1);
                    rm_add(a0, up, -unit, d0);
                    for (q = 0; q < 3; q++) {
                        outward[q] = ex[q] * sin(phi) - back[q] * cos(phi);
                    }
                    quad(&s, a0, a1, d1, d0, pc, rp, outward);
                }
                /* the step to the row above (a shoulder, or the underside of a wider row), and the top's lid */
                if (nxt < 0 || nxt != half[r]) {
                    int wide = (nxt < 0 || half[r] > nxt) ? r : r - 1, wp;
                    double dir[3] = { 0.0, wide == r ? 1.0 : -1.0, 0.0 };

                    ROWP(wide, wp);
                    for (k = 0; k < N; k++) {
                        double o0[3], o1[3], i0[3], i1[3];
                        int pc = C;

                        RING(r, k, o0);
                        RING(r, k + 1, o1);
                        if (nxt < 0) {
                            rm_add(foot, up, (R1 + 1 - r) * unit, i0);
                            rm_add(foot, up, (R1 + 1 - r) * unit, i1);
                        } else {
                            int h0 = half[wide] < half[wp] ? half[wide] : half[wp];

                            RING(r - 1, k, i0);
                            RING(r - 1, k + 1, i1);
                            i0[1] -= unit;
                            i1[1] -= unit;
                            pc = C + (int)rint(rm_wave((double)k / N) * h0);
                            while (pc != C && rm_has_colour(line, nline, px(pic, pc, wp))) {
                                pc += pc < C ? 1 : -1;
                            }
                        }
                        quad(&s, o0, o1, i1, i0, pc, wide, dir);
                    }
                }
            }
            for (r = -12; r < 12; r++) {
                for (c = -4; c < W + 4; c++) {
                    double dx = (c + 0.5 - (C + 1.5)) / (rmax + 1.5), dz = (r + 0.5 + 1.0) / (rmax + 0.5);

                    if (dx * dx + dz * dz <= 1.0) {
                        CELL(c, r);
                    }
                }
            }
#undef RING
#undef ROWP
        }
        if (lie != NULL) {
            int R, C0 = 0, C1 = 0, Wst = 0, halfb[MAXPX + 16], *half = halfb + 16, rbig = 0, idist;
            double org[3], dn[3], fw[3], dist, mex[3] = { -ex[0], -ex[1], -ex[2] };

            if (sscanf(lie, "%d:%d:%d:%d", &R, &C0, &C1, &Wst) != 4 || C0 < 0 || C1 >= W || C1 < C0 || Wst < 0 || Wst > 16) {
                free(cells);
                continue;
            }
            for (c = C0; c <= C1; c++) {
                int h = 0;

                while (MINE(c, R + h) || MINE(c, R - 1 - h)) {
                    h++;
                }
                half[c] = h;
                rbig = h > rbig ? h : rbig;
            }
            for (c = C0 - Wst; c < C0; c++) {
                half[c] = half[C0];
            }
            dist = rmax + rbig;         /* pixels behind the picture's lower edge: the two things touch */
            for (k = 0; k < 3; k++) {
                org[k] = base0[k] + back[k] * dist * unit + up[k] * rbig * unit;
                dn[k] = -up[k] * 0.6 - back[k] * 0.8;   /* the picture's downward, as the field's camera sees it ... */
                fw[k] = up[k] * 0.8 - back[k] * 0.6;    /* ... and the way to that camera */
            }
#define RINGL(cc, hc, kk, o) do { \
                double an_ = 2.0 * PI * (kk) / N; \
                int q_; \
                for (q_ = 0; q_ < 3; q_++) { \
                    (o)[q_] = org[q_] + ex[q_] * (cc) * unit + (dn[q_] * sin(an_) + fw[q_] * cos(an_)) * (hc) * unit; \
                } \
            } while (0)
            for (c = C0 - Wst; c <= C1; c++) {
                int src = c > C0 ? c : C0, e;

                for (k = 0; k < N; k++) {
                    double phi = 2.0 * PI * (k + 0.5) / N, pq = rm_wave((k + 0.5) / N) * half[c];
                    double a0[3], a1[3], d0[3], d1[3], outward[3];
                    int q;

                    r = pq >= 0 ? R + (int)floor(pq) : R - 1 - (int)floor(-pq);
                    r = r > R + half[c] - 1 ? R + half[c] - 1 : r;
                    r = r < R - half[c] ? R - half[c] : r;
                    while (!MINE(src, r) && r != R) {
                        r += r < R ? 1 : -1;
                    }
                    RINGL(c, half[c], k, a0);
                    RINGL(c, half[c], k + 1, a1);
                    rm_add(a1, ex, unit, d1);
                    rm_add(a0, ex, unit, d0);
                    for (q = 0; q < 3; q++) {
                        outward[q] = dn[q] * sin(phi) + fw[q] * cos(phi);
                    }
                    quad(&s, a0, a1, d1, d0, src, r, outward);
                }
                /* its ends, and the steps to a thinner column: plain, the column's middle pixel */
                for (e = 0; e < 2; e++) {
                    int edge = e == 0 ? c : c + 1, other = e == 0 ? c - 1 : c + 1;
                    int has = other >= C0 - Wst && other <= C1, oh = has ? half[other] : 0;

                    if (has && oh >= half[c]) {
                        continue;
                    }
                    for (k = 0; k < N; k++) {
                        double o0[3], o1[3], i0[3], i1[3];

                        RINGL(edge, half[c], k, o0);
                        RINGL(edge, half[c], k + 1, o1);
                        if (!has) {
                            rm_add(org, ex, edge * unit, i0);
                            rm_add(org, ex, edge * unit, i1);
                        } else {
                            RINGL(edge, oh, k, i0);
                            RINGL(edge, oh, k + 1, i1);
                        }
                        quad(&s, o0, o1, i1, i0, src, R, e == 0 ? mex : ex);
                    }
                }
                idist = (int)dist;
                for (r = idist - rbig - 3; r < idist + rbig + 3; r++) {
                    if (fabs(r + 0.5 - (dist - 1.0)) <= half[c] + 1.0) {
                        CELL(c, r);
                        CELL(c + 1, r);
                    }
                }
            }
#undef RINGL
        }
        if (out->ntri == first) {
            free(cells);
            continue;
        }
        /* the shadow: the cells, each once, in their order */
        qsort(cells, ncell, sizeof *cells, cell_cmp);
        s.kind_alpha = 15;
        for (i = 0; i < ncell; i++) {
            double o[3], o1[3], o2[3], o3[3];

            if (i > 0 && cells[i][0] == cells[i - 1][0] && cells[i][1] == cells[i - 1][1]) {
                continue;
            }
            for (k = 0; k < 3; k++) {
                o[k] = base0[k] + ex[k] * cells[i][0] * unit + back[k] * cells[i][1] * unit + up[k] * 0.3;
            }
            rm_add(o, ex, unit, o1);
            rm_add(o1, back, unit, o2);
            rm_add(o, back, unit, o3);
            quad(&s, o, o1, o2, o3, 0, H - 1, up);
        }
        free(cells);
        rm_drop_all(out, bt);
#undef AT
#undef SOLID
#undef MINE
#undef CELL
    }
}
