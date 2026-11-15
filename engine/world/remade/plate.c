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

void rm_plate(const RmBatch *b, unsigned nb, const RmTex *tex, unsigned ntex, const char *opt, RmOut *out)
{
    double rise[16][3], shift;
    unsigned nrise = 0, rb[16];
    const double shade = rm_optf(opt, "shade", 0.8);
    unsigned bi, i, j, n = 0, cap = 0;
    float floor_y = 1e30f;
    Up *up;
    char buf[256];

    (void)tex;
    (void)ntex;
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

            if (p0[1] - (floor_y + (float)shift) < 0.1f && p1[1] - (floor_y + (float)shift) < 0.1f) {
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
