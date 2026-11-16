/*
 * engine/world/remade/plate.c -- a flat floor piece made a stepped plate (a
 * switch's pad: Nimbasa Gym's).
 *
 * The map draws the pad as level pictures lying on the floor, each ring a
 * little over the one under it, so the field's steep camera sees a picture.
 * A battle camera low over the floor sees a sheet.  Remade:
 *   - each draw named in `rise` is lifted that many units (its level faces
 *     left out and drawn again higher, in their own pixels);
 *   - every level face that looks up gets sides along its open edges (the
 *     edges no other face that looks up shares at its height), down to the
 *     model's lowest level, in the pixels along that edge stretched downward,
 *     a little darker: the plate's rim.
 * Its pictures, and a texture animation on them (a switch pressed or not),
 * stay the model's own.
 *
 * Settings: rise=B:H[:R0:R1]+.. (B the draw, 0 its first; H units up; R0..R1:
 * only its faces whose middle lies that far from the model's upright axis),
 * shade (0.8: the sides' darkening), y (the plate's foot at this model height:
 * the map holds a pad a few units over a see-through floor, which the resting
 * step stands nothing on).
 *
 * pad=H[,vband=V0:V1][,padmax=W:L]: a room's small floor pads instead (Mistralton
 * Gym's arrow plates before its cannons, drawn into the room's mesh at many
 * heights): only the level faces at least 10 by 40, no wider than W and no longer than L (13:46)
 * whose picture lies in the rows V0..V1 of their texture are taken; each is
 * lifted H and gets sides H high along its open edges, down to where it lay.
 * Nothing else of the mesh is touched.
 * relief=R (with pad): the pad is built pixel by pixel from its own picture,
 * in three heights: the pixels of its commonest brightness at H, the darker
 * ones (its rim, the shadows under its shapes) 1.5 lower, the lighter ones
 * (its bars, its arrow) R higher, with a side wherever a pixel stands over
 * its neighbour; see-through pixels (its rounded corners) are left out.
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "remade.h"

typedef struct {
    float p[3][3], uv[3][2];
    unsigned id;
    unsigned char rgb[3];
} Up;

static int same3(const float *a, const float *b)
{
    return fabsf(a[0] - b[0]) < 0.05f && fabsf(a[1] - b[1]) < 0.05f && fabsf(a[2] - b[2]) < 0.05f;
}

/* One pad in relief (relief=R).  t0: one of its two triangles (its plane and its picture's mapping); the uv box
 * [u0, u1] x [v0, v1] of both; base: the height it lay at; the three heights over it. */
static void pad_relief(const RmTri *t0, const RmTex *tx, double u0, double u1, double v0, double v1, double h_low,
                       double h_mid, double h_high, double shade, RmOut *out)
{
    double m[3][3];
    int W = tx->w, H = tx->h, x0, x1, y0, y1, nx, ny, x, y, k;
    unsigned char *lev;
    int hist[256], mode = 0;
    unsigned char side_rgb[3];

    if (tx->rgba == NULL || W <= 0 || H <= 0 || rm_plane(t0, m) != 0) {
        return;
    }
    x0 = (int)floor(u0 * W + 0.01);
    x1 = (int)ceil(u1 * W - 0.01);
    y0 = (int)floor(v0 * H + 0.01);
    y1 = (int)ceil(v1 * H - 0.01);
    nx = x1 - x0;
    ny = y1 - y0;
    if (nx <= 0 || ny <= 0 || nx > 128 || ny > 128) {
        return;
    }
    lev = (unsigned char *)calloc((size_t)nx * (size_t)ny, 1);
    if (lev == NULL) {
        out->fail = 1;
        return;
    }
    memset(hist, 0, sizeof hist);
#define PX(X, Y) (tx->rgba + ((size_t)((((Y) % H) + H) % H) * (size_t)W + (size_t)((((X) % W) + W) % W)) * 4u)
#define LUM(P) (((P)[0] * 299 + (P)[1] * 587 + (P)[2] * 114) / 1000)
    for (y = 0; y < ny; y++) {
        for (x = 0; x < nx; x++) {
            const unsigned char *q = PX(x0 + x, y0 + y);

            if (q[3] >= 128) {
                hist[LUM(q)]++;
            }
        }
    }
    for (k = 1; k < 256; k++) {
        mode = hist[k] > hist[mode] ? k : mode;
    }
    /* each pixel's level: 0 none, 1 low, 2 the pad's own, 3 high */
    for (y = 0; y < ny; y++) {
        for (x = 0; x < nx; x++) {
            const unsigned char *q = PX(x0 + x, y0 + y);
            int l = LUM(q);

            lev[y * nx + x] = (unsigned char)(q[3] < 128 ? 0 : l < mode - 10 ? 1 : l > mode + 10 ? 3 : 2);
        }
    }
    for (k = 0; k < 3; k++) {
        side_rgb[k] = (unsigned char)(t0->rgb[k] * shade);
    }
    {
        const double hs[4] = { 0.0, h_low, h_mid, h_high };
        const double up[3] = { 0.0, 1.0, 0.0 };

        for (y = 0; y < ny; y++) {
            for (x = 0; x < nx;) {
                int l = lev[y * nx + x], xe = x + 1, c;

                /* a top: the run of pixels of one level along the row, in its own pixels */
                while (xe < nx && lev[y * nx + xe] == l) {
                    xe++;
                }
                if (l != 0) {
                    double q[4][3], uv[4][2], e1[3], e2[3], n[3];
                    const double uu[2] = { (double)(x0 + x) / W, (double)(x0 + xe) / W };
                    const double vv[2] = { (double)(y0 + y) / H, (double)(y0 + y + 1) / H };

                    for (c = 0; c < 4; c++) {
                        uv[c][0] = uu[c == 1 || c == 2];
                        uv[c][1] = vv[c >= 2];
                        rm_at(m, uv[c][0], uv[c][1], q[c]);
                        q[c][1] += hs[l];
                    }
                    for (k = 0; k < 3; k++) {
                        e1[k] = q[1][k] - q[0][k];
                        e2[k] = q[2][k] - q[0][k];
                    }
                    rm_cross(e1, e2, n);
                    if (rm_dot(n, up) >= 0.0) {
                        rm_tri(out, t0->id, -1, 1, t0->rgb, q[0], q[1], q[2], uv[0], uv[1], uv[2], 0, 0);
                        rm_tri(out, t0->id, -1, 1, t0->rgb, q[0], q[2], q[3], uv[0], uv[2], uv[3], 0, 0);
                    } else {
                        rm_tri(out, t0->id, -1, 1, t0->rgb, q[0], q[2], q[1], uv[0], uv[2], uv[1], 0, 0);
                        rm_tri(out, t0->id, -1, 1, t0->rgb, q[0], q[3], q[2], uv[0], uv[3], uv[2], 0, 0);
                    }
                }
                x = xe;
            }
        }
        /* the sides: each edge between two pixels where one stands over the other (or over nothing), from the
         * higher one's top down to the lower one's (the floor where there is none), in the higher one's own pixel */
        for (y = 0; y < ny; y++) {
            for (x = 0; x < nx; x++) {
                static const int dn[4][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
                int l = lev[y * nx + x], d;

                if (l == 0) {
                    continue;
                }
                for (d = 0; d < 4; d++) {
                    int ax = x + dn[d][0], ay = y + dn[d][1];
                    int o = ax < 0 || ay < 0 || ax >= nx || ay >= ny ? 0 : lev[ay * nx + ax];
                    double q[4][3], uvc[2], ctr[3], e1[3], e2[3], n[3], outw[3], ua, ub, va, vb;

                    if (hs[o] >= hs[l] - 1e-6) {
                        continue;
                    }
                    /* the shared edge, in the picture: the pixel's side toward the neighbour */
                    ua = (double)(x0 + x + (dn[d][0] > 0)) / W;
                    ub = (double)(x0 + x + (dn[d][0] >= 0)) / W;
                    va = (double)(y0 + y + (dn[d][1] > 0)) / H;
                    vb = (double)(y0 + y + (dn[d][1] >= 0)) / H;
                    if (dn[d][0] != 0) {
                        va = (double)(y0 + y) / H;
                        vb = (double)(y0 + y + 1) / H;
                        ub = ua;
                    } else {
                        ua = (double)(x0 + x) / W;
                        ub = (double)(x0 + x + 1) / W;
                        vb = va;
                    }
                    rm_at(m, ua, va, q[0]);
                    rm_at(m, ub, vb, q[1]);
                    memcpy(q[2], q[1], sizeof q[2]);
                    memcpy(q[3], q[0], sizeof q[3]);
                    q[0][1] += hs[l];
                    q[1][1] += hs[l];
                    q[2][1] += hs[o];
                    q[3][1] += hs[o];
                    uvc[0] = ((double)(x0 + x) + 0.5) / W;
                    uvc[1] = ((double)(y0 + y) + 0.5) / H;
                    rm_at(m, uvc[0], uvc[1], ctr);
                    for (k = 0; k < 3; k++) {
                        outw[k] = 0.5 * (q[0][k] + q[1][k]) - ctr[k];
                        e1[k] = q[1][k] - q[0][k];
                        e2[k] = q[2][k] - q[0][k];
                    }
                    outw[1] = 0.0;
                    rm_cross(e1, e2, n);
                    if (rm_dot(n, outw) >= 0.0) {
                        rm_tri(out, t0->id, -1, 1, side_rgb, q[0], q[1], q[2], uvc, uvc, uvc, 0, 0);
                        rm_tri(out, t0->id, -1, 1, side_rgb, q[0], q[2], q[3], uvc, uvc, uvc, 0, 0);
                    } else {
                        rm_tri(out, t0->id, -1, 1, side_rgb, q[0], q[2], q[1], uvc, uvc, uvc, 0, 0);
                        rm_tri(out, t0->id, -1, 1, side_rgb, q[0], q[3], q[2], uvc, uvc, uvc, 0, 0);
                    }
                }
            }
        }
    }
#undef PX
#undef LUM
    free(lev);
}

void rm_plate(const RmBatch *b, unsigned nb, const RmTex *tex, unsigned ntex, const char *opt, RmOut *out)
{
    double rise[16][3], shift;
    unsigned nrise = 0, rb[16];
    const double shade = rm_optf(opt, "shade", 0.8);
    const double pad = rm_optf(opt, "pad", 0.0), relief = rm_optf(opt, "relief", 0.0);
    double vband[2] = { 0.0, 1.0 }, padmax[2] = { 13.0, 46.0 };
    unsigned bi, i, j, n = 0, cap = 0;
    float floor_y = 1e30f;
    Up *up;
    char buf[256];

    (void)tex;
    (void)ntex;
    if (rm_opt(opt, "vband", buf, sizeof buf) != NULL) {
        (void)sscanf(buf, "%lf:%lf", &vband[0], &vband[1]);
    }
    if (rm_opt(opt, "padmax", buf, sizeof buf) != NULL) {
        (void)sscanf(buf, "%lf:%lf", &padmax[0], &padmax[1]);
    }
    if (rm_opt(opt, "rise", buf, sizeof buf) != NULL) {
        const char *p = buf;

        while (p != NULL && *p != '\0') {
            unsigned d;
            double h, r0 = 0.0, r1 = 1e30;

            if (sscanf(p, "%u:%lf:%lf:%lf", &d, &h, &r0, &r1) >= 2 && nrise < 16u) {
                rb[nrise] = d;
                rise[nrise][0] = h;
                rise[nrise][1] = r0;
                rise[nrise][2] = r1;
                nrise++;
            }
            p = strchr(p, '+');
            p = p != NULL ? p + 1 : NULL;
        }
    }
    for (bi = 0; bi < nb; bi++) {
        cap += b[bi].n;
        for (i = 0; !b[bi].bb && i < b[bi].n; i++) {
            for (j = 0; j < 3u; j++) {
                floor_y = b[bi].tri[i].p[j][1] < floor_y ? b[bi].tri[i].p[j][1] : floor_y;
            }
        }
    }
    shift = rm_optf(opt, "y", 1e30) < 1e29 ? rm_optf(opt, "y", 0.0) - floor_y : 0.0;
    up = (Up *)calloc(cap ? cap : 1u, sizeof *up);
    if (up == NULL) {
        out->fail = 1;
        return;
    }
    /* the faces that look up, lifted where `rise` says (drawn again higher) */
    for (bi = 0; bi < nb; bi++) {
        for (i = 0; !b[bi].bb && i < b[bi].n; i++) {
            const RmTri *t = &b[bi].tri[i];
            double e1[3], e2[3], nn[3];
            int k;

            for (k = 0; k < 3; k++) {
                e1[k] = (double)t->p[1][k] - t->p[0][k];
                e2[k] = (double)t->p[2][k] - t->p[0][k];
            }
            rm_cross(e1, e2, nn);
            if (rm_norm(nn) < 1e-9 || nn[1] < 0.9) {
                continue;
            }
            if (pad > 0.0) {
                /* a pad's face: small, level, its picture within the rows asked for */
                double lo[2] = { 1e30, 1e30 }, hi[2] = { -1e30, -1e30 }, v0 = 1e30, w, l;
                int in = 1;

                for (j = 0; j < 3u; j++) {
                    lo[0] = fmin(lo[0], t->p[j][0]); hi[0] = fmax(hi[0], t->p[j][0]);
                    lo[1] = fmin(lo[1], t->p[j][2]); hi[1] = fmax(hi[1], t->p[j][2]);
                    v0 = fmin(v0, t->uv[j][1]);
                    in &= fabs((double)t->p[j][1] - t->p[0][1]) < 0.01;
                }
                w = fmin(hi[0] - lo[0], hi[1] - lo[1]);
                l = fmax(hi[0] - lo[0], hi[1] - lo[1]);
                v0 = floor(v0 + 1e-4);
                for (j = 0; j < 3u; j++) {
                    in &= t->uv[j][1] - v0 >= vband[0] - 0.02 && t->uv[j][1] - v0 <= vband[1] + 0.02;
                }
                /* (a pad is one quad of its own size: its triangle spans all of it, at least 10 by 40) */
                if (!in || w > padmax[0] || l > padmax[1] || w < 10.0 || l < 40.0) {
                    continue;
                }
                if (relief > 0.0 && b[bi].tex >= 0 && (unsigned)b[bi].tex < ntex) {
                    /* in relief: the pad's two triangles together (the one whose box this one shares), once */
                    double u0 = 1e30, u1 = -1e30, w0 = 1e30, w1 = -1e30;
                    unsigned o2, c2;
                    int first = 1;

                    for (o2 = 0; o2 < b[bi].n; o2++) {
                        const RmTri *t2 = &b[bi].tri[o2];
                        double lo2[2] = { 1e30, 1e30 }, hi2[2] = { -1e30, -1e30 };

                        for (c2 = 0; c2 < 3u; c2++) {
                            lo2[0] = fmin(lo2[0], t2->p[c2][0]); hi2[0] = fmax(hi2[0], t2->p[c2][0]);
                            lo2[1] = fmin(lo2[1], t2->p[c2][2]); hi2[1] = fmax(hi2[1], t2->p[c2][2]);
                        }
                        if (fabs(lo2[0] - lo[0]) > 0.05 || fabs(hi2[0] - hi[0]) > 0.05 || fabs(lo2[1] - lo[1]) > 0.05
                            || fabs(hi2[1] - hi[1]) > 0.05 || fabs((double)t2->p[0][1] - t->p[0][1]) > 0.05) {
                            continue;
                        }
                        if (o2 < i) {
                            first = 0;          /* (its twin came first and built the pad) */
                        }
                        for (c2 = 0; c2 < 3u; c2++) {
                            u0 = fmin(u0, t2->uv[c2][0]); u1 = fmax(u1, t2->uv[c2][0]);
                            w0 = fmin(w0, t2->uv[c2][1]); w1 = fmax(w1, t2->uv[c2][1]);
                        }
                    }
                    rm_drop(out, t->id);
                    if (first) {
                        pad_relief(t, &tex[b[bi].tex], u0, u1, w0, w1, pad > 1.5 ? pad - 1.5 : 0.5 * pad, pad,
                                   pad + relief, shade, out);
                    }
                    continue;
                }
            }
            memcpy(up[n].p, t->p, sizeof up[n].p);
            memcpy(up[n].uv, t->uv, sizeof up[n].uv);
            memcpy(up[n].rgb, t->rgb, 3u);
            up[n].id = t->id;
            double lift = 0.0;
            {
                const double mx = ((double)t->p[0][0] + t->p[1][0] + t->p[2][0]) / 3.0;
                const double mz = ((double)t->p[0][2] + t->p[1][2] + t->p[2][2]) / 3.0;
                const double r = sqrt(mx * mx + mz * mz);
                unsigned q2;

                for (q2 = 0; q2 < nrise; q2++) {
                    if (rb[q2] == bi && r >= rise[q2][1] && r <= rise[q2][2]) {
                        lift = rise[q2][0];
                    }
                }
            }
            lift += shift;
            if (pad > 0.0) {
                lift = pad;
            }
            if (lift != 0.0) {
                double q[3][3], uv[3][2];

                for (j = 0; j < 3u; j++) {
                    up[n].p[j][1] += (float)lift;
                    for (k = 0; k < 3; k++) {
                        q[j][k] = up[n].p[j][k];
                    }
                    uv[j][0] = t->uv[j][0];
                    uv[j][1] = t->uv[j][1];
                }
                rm_drop(out, t->id);
                rm_tri(out, t->id, -1, 1, t->rgb, q[0], q[1], q[2], uv[0], uv[1], uv[2], 0, 0);
            }
            n++;
        }
    }
    /* each open edge of a face that looks up: its side, down to the floor */
    for (i = 0; i < n; i++) {
        int a;

        for (a = 0; a < 3; a++) {
            const float *p0 = up[i].p[a], *p1 = up[i].p[(a + 1) % 3];
            int shared = 0;

            if (pad <= 0.0 && p0[1] - (floor_y + (float)shift) < 0.1f && p1[1] - (floor_y + (float)shift) < 0.1f) {
                continue;                       /* (on the floor: no side) */
            }
            for (j = 0; j < n && !shared; j++) {
                int m, s0 = 0, s1 = 0;

                if (j == i) {
                    continue;
                }
                for (m = 0; m < 3; m++) {
                    s0 |= same3(up[j].p[m], p0);
                    s1 |= same3(up[j].p[m], p1);
                }
                shared = s0 && s1;
            }
            if (shared) {
                continue;
            }
            {
                double q[4][3], uv[4][2], e1[3], e2[3], fn[3], out_n[3], c[3] = { 0, 0, 0 };
                unsigned char rgb[3];
                int k;

                for (k = 0; k < 3; k++) {
                    c[k] = ((double)up[i].p[0][k] + up[i].p[1][k] + up[i].p[2][k]) / 3.0;
                    q[0][k] = p0[k];
                    q[1][k] = p1[k];
                    q[2][k] = p1[k];
                    q[3][k] = p0[k];
                    rgb[k] = (unsigned char)(up[i].rgb[k] * shade);
                }
                q[2][1] = q[3][1] = floor_y + (float)shift;
                if (pad > 0.0) {
                    q[2][1] = p1[1] - pad;      /* (a pad: down to where it lay) */
                    q[3][1] = p0[1] - pad;
                }
                uv[0][0] = uv[3][0] = up[i].uv[a][0];
                uv[0][1] = uv[3][1] = up[i].uv[a][1];
                uv[1][0] = uv[2][0] = up[i].uv[(a + 1) % 3][0];
                uv[1][1] = uv[2][1] = up[i].uv[(a + 1) % 3][1];
                /* (looking out of the face: across the edge, away from its middle) */
                out_n[0] = -((double)p1[2] - p0[2]);
                out_n[1] = 0.0;
                out_n[2] = (double)p1[0] - p0[0];
                if (((double)p0[0] - c[0]) * out_n[0] + ((double)p0[2] - c[2]) * out_n[2] < 0.0) {
                    out_n[0] = -out_n[0];
                    out_n[2] = -out_n[2];
                }
                for (k = 0; k < 3; k++) {
                    e1[k] = q[1][k] - q[0][k];
                    e2[k] = q[2][k] - q[0][k];
                }
                rm_cross(e1, e2, fn);
                if (rm_dot(fn, out_n) >= 0.0) {
                    rm_tri(out, up[i].id, -1, 1, rgb, q[0], q[1], q[2], uv[0], uv[1], uv[2], 0, 0);
                    rm_tri(out, up[i].id, -1, 1, rgb, q[0], q[2], q[3], uv[0], uv[2], uv[3], 0, 0);
                } else {
                    rm_tri(out, up[i].id, -1, 1, rgb, q[0], q[2], q[1], uv[0], uv[2], uv[1], 0, 0);
                    rm_tri(out, up[i].id, -1, 1, rgb, q[0], q[3], q[2], uv[0], uv[3], uv[2], 0, 0);
                }
            }
        }
    }
    free(up);
}
