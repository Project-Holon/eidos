/*
 * engine/platform/pixup.c -- an edge-directed pixel-art upscaler for the
 * automatic HD textures (texpack.c).
 *
 * In the spirit of Hyllian's xBR (level 2), written for this project.  For
 * each texel E and each of its four corners, a 5x5 neighbourhood decides
 * whether an edge crosses that corner:
 *
 *        A1 B1 C1                  (the bottom-right corner shown;
 *     A0 A  B  C  C4                the other three are this one
 *     D0 D  E  F  F4                mirrored)
 *     G0 G  H  I  I4
 *        G5 H5 I5
 *
 *   edge  wd(E,C)+wd(E,G)+wd(I,F4)+wd(I,H5)+4wd(H,F)
 *       < wd(H,D)+wd(H,I5)+wd(F,I4)+wd(F,B)+4wd(E,I)
 *
 * and, when it does, how steep it is: a 45 degree cut through the side
 * midpoints, or a shallow (2:1) line running towards G or towards C.  The
 * corner takes the colour of the closer of F and H, with one output pixel
 * of anti-aliasing along the cut.  Flat areas and straight edges keep the
 * original texels exactly, so the palette and the style stay; only stairs
 * turn into lines and curves.
 *
 * Transparency: transparent texels are one colour, very far from any
 * opaque one, so outlines are smoothed as shapes; alpha stays hard (a cut
 * into a transparent neighbour is all or nothing), as the DS's alpha test
 * expects.  Texels wrap at the edges, since most textures tile.
 */

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "pixup.h"

typedef struct {
    float y, u, v;
    int a;                  /* 0 transparent, 1 opaque */
    unsigned char rgba[4];
} Px;

static void to_px(const unsigned char *p, Px *o)
{
    float r = p[0], g = p[1], b = p[2];
    o->a = p[3] >= 128;
    o->y = 0.299f * r + 0.587f * g + 0.114f * b;
    o->u = -0.169f * r - 0.331f * g + 0.5f * b;
    o->v = 0.5f * r - 0.419f * g - 0.081f * b;
    memcpy(o->rgba, p, 4);
}

static float wd(const Px *a, const Px *b)
{
    if (!a->a || !b->a) {
        return a->a == b->a ? 0.0f : 1000.0f;
    }
    return 48.0f * fabsf(a->y - b->y) + 7.0f * fabsf(a->u - b->u) + 6.0f * fabsf(a->v - b->v);
}

static int same(const Px *a, const Px *b)
{
    return wd(a, b) < 155.0f;
}

static float clamp01(float x)
{
    return x < 0 ? 0 : x > 1 ? 1 : x;
}

/* Coverage of the cut in a corner, for the output pixel centred at (u, v)
 * in the texel (0..1, corner at +u +v): line a*v + b*u > c, softened over
 * one output pixel (px texels). */
static float cover(float u, float v, float a, float b, float c, float px)
{
    float n = sqrtf(a * a + b * b);
    return clamp01(((a * v + b * u - c) / n) / px + 0.5f);
}

unsigned char *eidos_pixup(const unsigned char *src, int w, int h, int s)
{
    unsigned char *out;
    Px *px;
    int x, y, i, j, k;
    float pxsize = 1.0f / (float)s;

    if (src == NULL || w <= 0 || h <= 0 || s < 2) {
        return NULL;
    }
    out = (unsigned char *)malloc((size_t)w * s * h * s * 4);
    px = (Px *)malloc((size_t)w * h * sizeof *px);
    if (out == NULL || px == NULL) {
        free(out);
        free(px);
        return NULL;
    }
    for (i = 0; i < w * h; i++) {
        to_px(src + (size_t)i * 4, &px[i]);
    }

#define P(dx, dy) (&px[(((y + (dy) * sy) % h + h) % h) * w + (((x + (dx) * sx) % w + w) % w)])
    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++) {
            const Px *E = &px[y * w + x];
            /* per corner: the colour it takes and its line (0 none) */
            const Px *col[4];
            int kind[4];
            for (k = 0; k < 4; k++) {
                int sx = (k & 1) ? 1 : -1, sy = (k & 2) ? 1 : -1;
                const Px *B = P(0, -1), *C = P(1, -1), *D = P(-1, 0), *F = P(1, 0);
                const Px *G = P(-1, 1), *H = P(0, 1), *I = P(1, 1);
                const Px *F4 = P(2, 0), *I4 = P(2, 1), *H5 = P(0, 2), *I5 = P(1, 2);
                float e, ii;
                kind[k] = 0;
                col[k] = E;
                if (same(E, F) || same(E, H)) {
                    continue;
                }
                e = wd(E, C) + wd(E, G) + wd(I, F4) + wd(I, H5) + 4.0f * wd(H, F);
                ii = wd(H, D) + wd(H, I5) + wd(F, I4) + wd(F, B) + 4.0f * wd(E, I);
                if (!(e < ii)) {
                    continue;
                }
                /* level 1 restriction: not inside a checkerboard */
                if (same(F, B) && same(H, D)) {
                    continue;
                }
                kind[k] = 1;
                if (2.0f * wd(F, G) <= wd(H, C) && !same(E, G) && !same(D, G)) {
                    kind[k] = 2;        /* shallow, towards G */
                } else if (2.0f * wd(H, C) <= wd(F, G) && !same(E, C) && !same(B, C)) {
                    kind[k] = 3;        /* steep, towards C */
                }
                col[k] = wd(E, F) <= wd(E, H) ? F : H;
            }
            for (j = 0; j < s; j++) {
                for (i = 0; i < s; i++) {
                    float fu = ((float)i + 0.5f) / (float)s, fv = ((float)j + 0.5f) / (float)s;
                    float best = 0.0f;
                    const Px *pick = E;
                    unsigned char *o = out + (((size_t)y * s + j) * (size_t)w * s + (size_t)x * s + i) * 4;
                    for (k = 0; k < 4; k++) {
                        float u, v, m;
                        if (!kind[k]) {
                            continue;
                        }
                        u = (k & 1) ? fu : 1.0f - fu;
                        v = (k & 2) ? fv : 1.0f - fv;
                        if (kind[k] == 1) {
                            m = cover(u, v, 1.0f, 1.0f, 1.5f, pxsize);
                        } else if (kind[k] == 2) {
                            m = cover(u, v, 1.0f, 0.5f, 1.0f, pxsize);
                        } else {
                            m = cover(u, v, 0.5f, 1.0f, 1.0f, pxsize);
                        }
                        if (m > best) {
                            best = m;
                            pick = col[k];
                        }
                    }
                    if (best <= 0.0f) {
                        memcpy(o, E->rgba, 4);
                        continue;
                    }
                    if (!pick->a || !E->a) {
                        /* hard alpha against transparency */
                        memcpy(o, best >= 0.5f ? pick->rgba : E->rgba, 4);
                        continue;
                    }
                    o[0] = (unsigned char)(E->rgba[0] + (pick->rgba[0] - E->rgba[0]) * best + 0.5f);
                    o[1] = (unsigned char)(E->rgba[1] + (pick->rgba[1] - E->rgba[1]) * best + 0.5f);
                    o[2] = (unsigned char)(E->rgba[2] + (pick->rgba[2] - E->rgba[2]) * best + 0.5f);
                    o[3] = E->rgba[3];
                }
            }
        }
    }
#undef P
    free(px);
    return out;
}

unsigned char *eidos_pixup_half(const unsigned char *src, int w, int h, int s)
{
    unsigned char *up = eidos_pixup(src, w, h, s);
    int x, y, c;

    if (up == NULL) {
        return NULL;
    }
    for (y = 0; y < h * s; y++) {
        for (x = 0; x < w * s; x++) {
            unsigned char *u = up + ((size_t)y * w * s + x) * 4;
            const unsigned char *p = src + ((size_t)(y / s) * w + x / s) * 4;

            /* an edge texel blended with transparency keeps its own colour */
            if (p[3] == 0 || u[3] == 0) {
                continue;
            }
            for (c = 0; c < 3; c++) {
                u[c] = (unsigned char)((u[c] + p[c] + 1) / 2);
            }
        }
    }
    return up;
}
