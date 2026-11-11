/*
 * engine/g3d/raster.c -- the software rasteriser.
 *
 * READ THIS BEFORE BELIEVING A PIXEL.  Everything above this file in
 * engine/g3d/ is checked against an oracle: the containers, the
 * dictionaries, the display-list interpreter and the vertex pipeline are
 * compared vertex for vertex against the OBJ aletheia's own asset tools
 * export.  This file has no oracle.  There is no DS frame in this
 * repository to compare a rendered picture against, and **nothing here
 * claims pixel-exactness with the hardware**.  What is below is one of
 * three things, and each rule says which:
 *
 *   [HW]  documented NDS hardware behaviour: the near-plane clip, the
 *         perspective divide, the 256x192 viewport, POLYGON_ATTR's
 *         culling bits and its 5-bit alpha, TEXIMAGE_PARAM's repeat,
 *         flip and clamp wrapping, and the modulate and decal texture
 *         blend formulas, which are defined on 6-bit colour channels and
 *         5-bit alpha rather than on the 8 bits a host buffer holds.
 *   [AP]  an approximation, named as one: the fill rule, the
 *         subpixel resolution, the per-span interpolation, the depth
 *         buffer's units, the shadow polygon mode, and the toon mode
 *         when no toon table is loaded (G3dTarget.toon).
 *   [PO]  a decision that belongs to the port rather than to either.
 *
 * The approximations worth knowing about:
 *
 *   [HW] Texture coordinates and colour are perspective correct: they
 *        are divided by w at the vertices, interpolated with 1/w, and
 *        divided back per pixel.  Polygons are clipped to the near, left,
 *        right, top and bottom planes before the divide.
 *   [AP] **The fill rule is a top-left rule at pixel centres**, in 20.4
 *        fixed point.  The DS's own edge inclusion is not documented
 *        precisely enough anywhere public to transcribe, so this is a
 *        conventional rule chosen to be watertight (no pixel drawn
 *        twice, no gap between shared edges) rather than to be the
 *        hardware's.
 *   [AP] **Depth is z/w mapped linearly onto a 23-bit range.**  The DS
 *        has both a z-buffer and a w-buffer mode selected per frame;
 *        this is the z one, and the exact quantisation is this engine's.
 *
 * [PO] A polygon whose POLYGON_ATTR word is zero is drawn double-sided
 * and opaque rather than culled.  That is not a hardware rule: it is
 * what a per-shape decode needs, because a shape's display list is
 * entered at a heuristically located offset (see gx.c and aletheia's
 * docs/ASSETS.md) which is usually *past* the model's own POLYGON_ATTR
 * command, so the attribute word really was never seen rather than
 * really being zero.  Culling everything in that case would render an
 * empty screen and call it correct.
 */

#include <stdlib.h>
#include <string.h>

#include "g2d.h"
#include "g3d.h"

#define SUBPIX 4          /* screen coordinates are 20.4 fixed point */
#define SUB_ONE (1 << SUBPIX)
#define SUB_HALF (SUB_ONE / 2)
#define DEPTH_MAX 0x7FFFFF

#define ATTR_DEFAULT (((u32)1 << 6) | ((u32)1 << 7) | ((u32)31 << 16))

/* ------------------------------------------------------------------ */
/* The target                                                          */
/* ------------------------------------------------------------------ */

int g3d_target_init(G3dTarget *t, u32 width, u32 height)
{
    memset(t, 0, sizeof(*t));
    if (width == 0 || height == 0 || width > 4096u || height > 4096u) {
        return -1;
    }
    t->rgba = (u8 *)calloc((size_t)width * height, 4);
    t->depth = (s32 *)malloc((size_t)width * height * sizeof(s32));
    t->pid = (u8 *)calloc((size_t)width * height, 1);
    t->pflag = (u8 *)calloc((size_t)width * height, 1);
    t->xid = (u8 *)calloc((size_t)width * height, 1);
    t->cov = (u8 *)calloc((size_t)width * height, 1);
    t->under = (u8 *)calloc((size_t)width * height, 4);
    if (t->rgba == NULL || t->depth == NULL || t->pid == NULL
        || t->pflag == NULL || t->xid == NULL || t->cov == NULL || t->under == NULL) {
        g3d_target_free(t);
        return -1;
    }
    t->width = width;
    t->height = height;
    g3d_target_clear(t, 0, 0, 0, 255);
    return 0;
}

void g3d_target_free(G3dTarget *t)
{
    if (t == NULL) {
        return;
    }
    free(t->rgba);
    free(t->depth);
    free(t->pid);
    free(t->pflag);
    free(t->xid);
    free(t->cov);
    free(t->under);
    memset(t, 0, sizeof(*t));
}

void g3d_target_clear(G3dTarget *t, u8 r, u8 g, u8 b, u8 a)
{
    u32 i, n = t->width * t->height;

    for (i = 0; i < n; i++) {
        t->rgba[i * 4 + 0] = r;
        t->rgba[i * 4 + 1] = g;
        t->rgba[i * 4 + 2] = b;
        t->rgba[i * 4 + 3] = a;
        t->depth[i] = G3D_DEPTH_FAR;
    }
    if (t->pid != NULL) {
        memset(t->pid, 0, n);
        memset(t->pflag, 0, n);
        memset(t->xid, 0, n);
        memset(t->cov, 31, n);
    }
}

/* ------------------------------------------------------------------ */
/* Clipping                                                            */
/* ------------------------------------------------------------------ */

/* A vertex carried through the clip and the interpolation.  Positions
 * are clip space until the divide; s and t stay in the sixteenths of a
 * texel the TEXCOORD command carried. */
typedef struct ClipV {
    fx32 x, y, z, w;
    s32  s, t;
    s32  r, g, b;
} ClipV;

#define CLIP_MAX 12

static void clip_lerp(ClipV *o, const ClipV *a, const ClipV *b, s64 num,
                      s64 den)
{
    /* o = a + (b - a) * num / den, with the division done once per field
     * in 64 bits so a large clip-space coordinate cannot overflow. */
    if (den == 0) {
        *o = *a;
        return;
    }
    o->x = (fx32)(a->x + ((s64)(b->x - a->x) * num) / den);
    o->y = (fx32)(a->y + ((s64)(b->y - a->y) * num) / den);
    o->z = (fx32)(a->z + ((s64)(b->z - a->z) * num) / den);
    o->w = (fx32)(a->w + ((s64)(b->w - a->w) * num) / den);
    o->s = (s32)(a->s + ((s64)(b->s - a->s) * num) / den);
    o->t = (s32)(a->t + ((s64)(b->t - a->t) * num) / den);
    o->r = (s32)(a->r + ((s64)(b->r - a->r) * num) / den);
    o->g = (s32)(a->g + ((s64)(b->g - a->g) * num) / den);
    o->b = (s32)(a->b + ((s64)(b->b - a->b) * num) / den);
}

/*
 * [HW] The near-plane clip.  A polygon is clipped against w >= WMIN and
 * z >= -w before the divide; the far plane and the four side planes are
 * left to the scanline loop's own screen clamp, which is the guard band
 * a hardware rasteriser has anyway.
 */
#define WMIN 16   /* 1/256 in fx32: small, but never a divide by zero */

/* The distance of a vertex inside plane `plane` (>= 0 is inside).  0 is
 * the w floor, 1 the near plane, 2 and 3 the left and right, 4 and 5 the
 * bottom and top.  [HW] the hardware clips a polygon to the view volume,
 * so a vertex far outside never reaches the divide; [AP] the far plane is
 * left to the depth clamp. */
static s64 plane_dist(const ClipV *a, int plane)
{
    switch (plane) {
    case 0:  return (s64)a->w - WMIN;
    case 1:  return (s64)a->z + a->w;
    case 2:  return (s64)a->x + a->w;
    case 3:  return (s64)a->w - a->x;
    case 4:  return (s64)a->y + a->w;
    default: return (s64)a->w - a->y;
    }
}

static u32 clip_plane(ClipV *out, const ClipV *in, u32 n, int plane)
{
    u32 i, m = 0;

    for (i = 0; i < n; i++) {
        const ClipV *a = &in[i];
        const ClipV *b = &in[(i + 1u) % n];
        s64 da = plane_dist(a, plane);
        s64 db = plane_dist(b, plane);
        int ia = da >= 0, ib = db >= 0;

        if (ia && m < CLIP_MAX) {
            out[m++] = *a;
        }
        if (ia != ib && m < CLIP_MAX) {
            clip_lerp(&out[m++], a, b, da, da - db);
        }
    }
    return m;
}

/* ------------------------------------------------------------------ */
/* Texture sampling                                                    */
/* ------------------------------------------------------------------ */

/*
 * [HW] TEXIMAGE_PARAM's wrapping.  With repeat clear the coordinate is
 * clamped to the edge; with repeat set it wraps by masking, and with
 * flip additionally set every other repetition is mirrored, which is why
 * the mask is over twice the size.  Every texture dimension is a power
 * of two, which is what makes both a mask rather than a modulo.
 */
static u32 wrap_coord(s32 v, u32 size, u32 repeat, u32 flip)
{
    u32 k;

    if (size == 0) {
        return 0;
    }
    if (!repeat) {
        if (v < 0) {
            return 0;
        }
        if ((u32)v >= size) {
            return size - 1u;
        }
        return (u32)v;
    }
    if (flip) {
        k = (u32)v & (size * 2u - 1u);
        if (k >= size) {
            k = size * 2u - 1u - k;
        }
        return k;
    }
    return (u32)v & (size - 1u);
}

static void sample_texture(const G3dTexture *tex, u32 teximage, s32 s16th,
                           s32 t16th, u8 out[4])
{
    s32 s = s16th >> SUBPIX;   /* sixteenths of a texel to whole texels */
    s32 t = t16th >> SUBPIX;
    u32 x = wrap_coord(s, tex->width, G3D_TEX_REPEAT_S(teximage),
                       G3D_TEX_FLIP_S(teximage));
    u32 y = wrap_coord(t, tex->height, G3D_TEX_REPEAT_T(teximage),
                       G3D_TEX_FLIP_T(teximage));
    const u8 *p = tex->rgba + ((size_t)y * tex->width + x) * 4u;

    out[0] = p[0];
    out[1] = p[1];
    out[2] = p[2];
    out[3] = p[3];
}

/* [HW] The modulate blend is defined on 6-bit colour channels and 5-bit
 * alpha: ((a + 1) * (b + 1) - 1) >> bits.  Doing it on the 8-bit
 * expansion instead rounds differently on essentially every pixel, which
 * is the same reason engine/g2d/window.c blends in 5 bits. */
static u8 modulate6(u8 a8, u8 b8)
{
    u32 a6 = (u32)a8 >> 2, b6 = (u32)b8 >> 2;
    u32 r6 = ((a6 + 1u) * (b6 + 1u) - 1u) >> 6;

    if (r6 > 63u) {
        r6 = 63u;
    }
    return (u8)(r6 * 255u / 63u);
}

static u32 modulate5(u32 a5, u32 b5)
{
    u32 r = ((a5 + 1u) * (b5 + 1u) - 1u) >> 5;

    return r > 31u ? 31u : r;
}

/* ------------------------------------------------------------------ */
/* One pixel                                                           */
/* ------------------------------------------------------------------ */

/*
 * One pixel of a polygon.  `cov5` is its coverage, 31 except at a span end
 * when the target records coverage (G3dTarget.aa).
 */
static void put_pixel(G3dTarget *t, u32 x, u32 y, s32 depth,
                      const u8 col[3], u32 alpha5, u32 attr, u32 cov5)
{
    size_t o = ((size_t)y * t->width + x);
    u8 *px = t->rgba + o * 4u;
    u8 fog = (u8)(G3D_ATTR_FOG(attr) ? G3D_PIX_FOG : 0u);

    /* [HW] the alpha test: with it enabled (DISP3DCNT bit 2) a pixel
     * whose alpha is not above ALPHA_TEST_REF is not drawn.  alpha_ref is
     * 0 when the test is off, which is the alpha-0 rule alone. */
    if (alpha5 == 0 || alpha5 <= t->alpha_ref) {
        return;
    }
    /* [HW] the depth test: strictly nearer by default (a second,
     * coincident polygon does not overwrite the first), and with
     * POLYGON_ATTR bit 14 equal within a tolerance, which is what lets a
     * decal drawn on a surface show.  [AP] the tolerance is the
     * hardware's 0x200 of 24 bits, halved onto this 23-bit range for the
     * z buffer and left as is for the w buffer. */
    if (G3D_ATTR_DEPTH_EQ(attr)) {
        s32 tol = t->wbuffer ? 0x200 : 0x100;
        s32 d = depth - t->depth[o];

        if (d < -tol || d > tol) {
            return;
        }
    } else if (depth >= t->depth[o]) {
        return;
    }
    if (alpha5 >= 31u) {
        if (t->pid != NULL) {
            /* An opaque pixel: its polygon ID, its fog flag, and, for a
             * partly covered span end, what it covers (anti-aliasing). */
            t->pid[o] = (u8)G3D_ATTR_ID(attr);
            t->pflag[o] = (u8)(G3D_PIX_OPAQUE | fog);
            t->xid[o] = 0;
            t->cov[o] = (u8)cov5;
            if (cov5 < 31u) {
                memcpy(t->under + o * 4u, px, 4u);
                t->pflag[o] |= G3D_PIX_EDGE;
            }
        }
        px[0] = col[0];
        px[1] = col[1];
        px[2] = col[2];
        px[3] = 255;
        t->depth[o] = depth;
        return;
    }
    /* [HW] a translucent pixel is not drawn over a translucent pixel of
     * the same polygon ID: overlapping faces of one see-through shape (a
     * glow made of a fan of rays, say) cover a pixel once, not once each. */
    if (t->xid != NULL) {
        u8 id = (u8)(0x40u | G3D_ATTR_ID(attr));

        if (t->xid[o] == id) {
            return;
        }
        t->xid[o] = id;
    }
    /* [HW] a translucent pixel over a pixel of alpha 0 (the clear plane
     * with a clear alpha of 0) is written as it is, its alpha included:
     * there is nothing to blend with, and the 2D engine blends the 3D
     * layer by that alpha.  Over anything else the colour is blended in
     * 5-bit alpha and the alpha is the larger of the two. */
    if (px[3] == 0) {
        px[0] = col[0];
        px[1] = col[1];
        px[2] = col[2];
        px[3] = (u8)(alpha5 * 255u / 31u);
    } else {
        u8 a8 = (u8)(alpha5 * 255u / 31u);

        px[0] = (u8)((col[0] * alpha5 + px[0] * (31u - alpha5)) / 31u);
        px[1] = (u8)((col[1] * alpha5 + px[1] * (31u - alpha5)) / 31u);
        px[2] = (u8)((col[2] * alpha5 + px[2] * (31u - alpha5)) / 31u);
        if (a8 > px[3]) {
            px[3] = a8;
        }
    }
    /* [HW] POLYGON_ATTR bit 11 lets a translucent polygon update the
     * depth buffer; [AP] otherwise it is not written, the default that
     * keeps a translucent surface from hiding what is behind it.  [AP]
     * the fog flag of a translucent pixel is the old flag ANDed with the
     * polygon's. */
    if (G3D_ATTR_XLU_DEPTH(attr)) {
        t->depth[o] = depth;
    }
    if (t->pflag != NULL) {
        t->pflag[o] = (u8)((t->pflag[o] & ~G3D_PIX_FOG) | (t->pflag[o] & fog));
    }
}

/* ------------------------------------------------------------------ */
/* The polygon                                                         */
/* ------------------------------------------------------------------ */

/* A vertex after the divide: screen position in 20.4, depth, and the
 * attributes premultiplied by q = 2^32 / w so they interpolate linearly
 * across the face and one division per pixel recovers the
 * perspective-correct value ([HW]: the hardware's texture and colour are
 * perspective correct). */
typedef struct ScreenV {
    s32 x, y;      /* 20.4 */
    s32 depth;
    s64 q;
    s64 s, t;      /* sixteenths of a texel, times q */
    s64 r, g, b;   /* 0..255, times q */
} ScreenV;

#define Q_ONE ((s64)1 << 32)

static s64 lerp64(s64 a, s64 b, s64 num, s64 den)
{
    return a + ((b - a) * num) / den;
}

static void interp(ScreenV *o, const ScreenV *a, const ScreenV *b,
                   s32 num, s32 den)
{
    if (den == 0) {
        *o = *a;
        return;
    }
    o->x     = (s32)lerp64(a->x, b->x, num, den);
    o->y     = (s32)lerp64(a->y, b->y, num, den);
    o->depth = (s32)lerp64(a->depth, b->depth, num, den);
    o->q     = lerp64(a->q, b->q, num, den);
    o->s     = lerp64(a->s, b->s, num, den);
    o->t     = lerp64(a->t, b->t, num, den);
    o->r     = lerp64(a->r, b->r, num, den);
    o->g     = lerp64(a->g, b->g, num, den);
    o->b     = lerp64(a->b, b->b, num, den);
}

G3dRasterStats g3d_raster_stats;

/* The statistics count once per polygon: a band other than the first
 * (g3d_raster_geom) leaves them alone. */
#define RSTAT(f) do { if (stats) { g3d_raster_stats.f++; } } while (0)

/* One polygon, drawn on rows ylo..yhi only. */
static void raster_band(G3dTarget *t, const G3dVertex *v, u32 n,
                        u32 attr, u32 teximage, const G3dTexture *tex,
                        s32 ylo, s32 yhi, int stats)
{
    ClipV in[CLIP_MAX], tmp[CLIP_MAX];
    ScreenV sv[CLIP_MAX];
    u32 i, m;
    u32 mode, alpha5;
    s64 area;
    int front;
    s32 ymin, ymax, y;

    RSTAT(submitted);
    if (n < 3u || n > 4u) {
        RSTAT(rej_count);
        return;
    }
    if (attr == 0) {
        attr = ATTR_DEFAULT;   /* [PO] see this file's header */
    }
    mode = G3D_ATTR_MODE(attr);
    alpha5 = G3D_ATTR_ALPHA(attr);
    if (alpha5 == 0) {
        /* [HW] alpha 0 is the wireframe mode; this engine does not draw
         * wireframes, and a polygon asking for one is skipped rather
         * than quietly filled. */
        RSTAT(rej_alpha0);
        return;
    }
    if (mode == G3D_POLY_MODE_SHADOW) {
        /* [AP] shadow volumes need the stencil-like shadow polygon
         * machinery and a second pass; nothing in the corpus this engine
         * has been pointed at uses them, and drawing them as ordinary
         * polygons would put black slabs in the picture. */
        RSTAT(rej_shadow);
        return;
    }

    for (i = 0; i < n; i++) {
        in[i].x = v[i].x; in[i].y = v[i].y; in[i].z = v[i].z; in[i].w = v[i].w;
        in[i].s = v[i].s; in[i].t = v[i].t;
        in[i].r = v[i].r; in[i].g = v[i].g; in[i].b = v[i].b;
    }
    m = n;
    for (i = 0; i < 6u; i++) {
        ClipV *src = (i & 1u) ? tmp : in;
        ClipV *dst = (i & 1u) ? in : tmp;

        m = clip_plane(dst, src, m, (int)i);
        if (m < 3u) {
            RSTAT(rej_clip);
            return;
        }
    }

    /* [HW] the perspective divide and the viewport transform. */
    for (i = 0; i < m; i++) {
        s64 w = in[i].w;
        s64 hx = (s64)t->width * SUB_ONE / 2;
        s64 hy = (s64)t->height * SUB_ONE / 2;
        s64 dz;

        if (w < WMIN) {
            w = WMIN;
        }
        sv[i].x = (s32)(((s64)in[i].x * hx) / w + hx);
        sv[i].y = (s32)(hy - ((s64)in[i].y * hy) / w);
        dz = (((s64)in[i].z + w) * DEPTH_MAX) / (2 * w);
        if (t->wbuffer) {
            /* [HW] SWAP_BUFFERS bit 1: the depth is w itself; [AP]
             * interpolated affinely across the face like z/w is. */
            dz = w;
            if (dz > (s64)G3D_DEPTH_FAR - 1) {
                dz = (s64)G3D_DEPTH_FAR - 1;
            }
        }
        if (dz < 0) {
            dz = 0;
        }
        if (!t->wbuffer && dz > DEPTH_MAX) {
            dz = DEPTH_MAX;
        }
        sv[i].depth = (s32)dz;
        sv[i].q = Q_ONE / w;
        sv[i].s = (s64)in[i].s * sv[i].q;
        sv[i].t = (s64)in[i].t * sv[i].q;
        sv[i].r = (s64)in[i].r * sv[i].q;
        sv[i].g = (s64)in[i].g * sv[i].q;
        sv[i].b = (s64)in[i].b * sv[i].q;
    }

    /* [HW] culling, on the projected winding.  Screen y runs downwards,
     * so a negative signed area is the front face. */
    area = 0;
    for (i = 0; i < m; i++) {
        const ScreenV *a = &sv[i];
        const ScreenV *b = &sv[(i + 1u) % m];

        area -= (s64)(b->x - a->x) * (b->y + a->y);
    }
    if (area == 0) {
        RSTAT(rej_degenerate);
        return;
    }
    front = area < 0;
    if (front && !G3D_ATTR_FRONT(attr)) {
        RSTAT(rej_cull);
        return;
    }
    if (!front && !G3D_ATTR_BACK(attr)) {
        RSTAT(rej_cull);
        return;
    }
    RSTAT(drawn);

    ymin = sv[0].y;
    ymax = sv[0].y;
    for (i = 1; i < m; i++) {
        if (sv[i].y < ymin) { ymin = sv[i].y; }
        if (sv[i].y > ymax) { ymax = sv[i].y; }
    }
    {
        s32 y0 = (ymin - SUB_HALF + SUB_ONE - 1) >> SUBPIX;
        s32 y1 = (ymax - SUB_HALF - 1) >> SUBPIX;

        if (y0 < ylo) { y0 = ylo; }
        if (y1 > yhi) { y1 = yhi; }

        for (y = y0; y <= y1; y++) {
            s32 yc = (y << SUBPIX) + SUB_HALF;   /* the pixel centre */
            ScreenV cross[2];
            u32 ncross = 0;
            ScreenV *L, *R;
            s32 x0, x1, x;
            s32 span;

            for (i = 0; i < m && ncross < 2u; i++) {
                const ScreenV *a = &sv[i];
                const ScreenV *b = &sv[(i + 1u) % m];
                int hit;

                /* [AP] the top-left rule: an edge covers a scanline on
                 * [y_top, y_bottom), so two faces sharing an edge cover
                 * every scanline exactly once between them. */
                if (a->y <= b->y) {
                    hit = (yc >= a->y && yc < b->y);
                } else {
                    hit = (yc >= b->y && yc < a->y);
                }
                if (!hit) {
                    continue;
                }
                /* Always from the upper end down, so the two faces that
                 * share an edge round its crossing identically and no
                 * pixel falls between them. */
                if (a->y <= b->y) {
                    interp(&cross[ncross++], a, b, yc - a->y, b->y - a->y);
                } else {
                    interp(&cross[ncross++], b, a, yc - b->y, a->y - b->y);
                }
            }
            if (ncross < 2u) {
                continue;
            }
            if (cross[0].x <= cross[1].x) {
                L = &cross[0]; R = &cross[1];
            } else {
                L = &cross[1]; R = &cross[0];
            }
            span = R->x - L->x;
            if (span <= 0) {
                continue;
            }
            x0 = (L->x - SUB_HALF + SUB_ONE - 1) >> SUBPIX;
            x1 = (R->x - SUB_HALF - 1) >> SUBPIX;
            if (x0 < 0) { x0 = 0; }
            if (x1 >= (s32)t->width) { x1 = (s32)t->width - 1; }

            for (x = x0; x <= x1; x++) {
                s32 xc = (x << SUBPIX) + SUB_HALF;
                ScreenV p;
                struct { s32 r, g, b, s, t, depth; } c;
                u8 col[3];
                u8 hl[3] = { 0, 0, 0 };
                u32 a5 = alpha5;
                u32 cov;

                /* [HW] perspective-correct interpolation: the
                 * attributes were divided by w at the vertices, so the
                 * span carries attribute/w and 1/w, and one division
                 * per pixel puts the attribute back. */
                interp(&p, L, R, xc - L->x, span);
                if (p.q <= 0) {
                    p.q = 1;
                }
                c.r = (s32)(p.r / p.q);
                c.g = (s32)(p.g / p.q);
                c.b = (s32)(p.b / p.q);
                c.s = (s32)(p.s / p.q);
                c.t = (s32)(p.t / p.q);
                c.depth = p.depth;
                if (t->wbuffer) {
                    s64 wv = Q_ONE / p.q;
                    c.depth = (s32)(wv > (s64)G3D_DEPTH_FAR - 1 ?
                                    (s64)G3D_DEPTH_FAR - 1 : wv);
                }

                if (c.r < 0) { c.r = 0; } if (c.r > 255) { c.r = 255; }
                if (c.g < 0) { c.g = 0; } if (c.g > 255) { c.g = 255; }
                if (c.b < 0) { c.b = 0; } if (c.b > 255) { c.b = 255; }

                /* [HW] toon and highlight shading, when the caller has
                 * loaded the toon table (engine/g3d/hw3d.c does, from
                 * the TOON_TABLE registers): the vertex colour's red
                 * channel indexes the 32-entry table.  Toon replaces the
                 * vertex colour with the entry; highlight keeps red as a
                 * grey level and adds the entry after the texture blend
                 * (below).  [AP] that the add saturates per channel at
                 * the 8-bit expansion rather than in 6 bits.  Without a
                 * table the mode falls back to modulate, as before. */
                if (mode == G3D_POLY_MODE_TOON && t->toon != NULL) {
                    const u8 *e = t->toon + ((u32)c.r >> 3) * 3u;
                    if (t->highlight) {
                        hl[0] = e[0]; hl[1] = e[1]; hl[2] = e[2];
                        c.g = c.r;
                        c.b = c.r;
                    } else {
                        c.r = e[0];
                        c.g = e[1];
                        c.b = e[2];
                    }
                }

                if (tex != NULL && tex->rgba != NULL) {
                    u8 tc[4];
                    sample_texture(tex, teximage, c.s, c.t, tc);
                    switch (mode) {
                    case G3D_POLY_MODE_DECAL: {
                        /* [HW] decal: the texture's own alpha selects
                         * between the texture and the vertex colour. */
                        u32 ta = (u32)tc[3] >> 3;
                        col[0] = (u8)((tc[0] * ta + c.r * (31u - ta)) / 31u);
                        col[1] = (u8)((tc[1] * ta + c.g * (31u - ta)) / 31u);
                        col[2] = (u8)((tc[2] * ta + c.b * (31u - ta)) / 31u);
                        break;
                    }
                    default:
                        /* [HW] modulate, and the texture half of toon
                         * and highlight, whose vertex colour was set up
                         * above.  [AP] with no toon table loaded the
                         * toon/highlight mode is plain modulate, because
                         * inventing a ramp would be a guess. */
                        col[0] = modulate6(tc[0], (u8)c.r);
                        col[1] = modulate6(tc[1], (u8)c.g);
                        col[2] = modulate6(tc[2], (u8)c.b);
                        a5 = modulate5((u32)tc[3] >> 3, alpha5);
                        break;
                    }
                } else {
                    col[0] = (u8)c.r;
                    col[1] = (u8)c.g;
                    col[2] = (u8)c.b;
                }
                if (hl[0] | hl[1] | hl[2]) {
                    u32 k;
                    for (k = 0; k < 3u; k++) {
                        u32 c = (u32)col[k] + hl[k];
                        col[k] = (u8)(c > 255u ? 255u : c);
                    }
                }
                cov = 31u;
                if (t->aa && a5 >= 31u && (x == x0 || x == x1)) {
                    /* [AP] the coverage of a span end: the part of the
                     * pixel's width inside [L, R), in 32nds less one.
                     * Vertical coverage (an edge crossing the pixel's
                     * height) is not measured. */
                    s32 lo = x << SUBPIX, hi = lo + SUB_ONE;
                    s32 c0 = L->x > lo ? L->x : lo;
                    s32 c1 = R->x < hi ? R->x : hi;
                    s32 span_px = c1 - c0;

                    if (span_px < SUB_ONE) {
                        cov = (u32)(span_px > 0 ? span_px : 0) * 32u / SUB_ONE;
                        cov = cov > 0 ? cov - 1u : 0u;
                    }
                }
                put_pixel(t, (u32)x, (u32)y, c.depth, col, a5, attr, cov);
            }
        }
    }
}

void g3d_raster_polygon(G3dTarget *t, const G3dVertex *v, u32 n,
                        u32 attr, u32 teximage, const G3dTexture *tex)
{
    raster_band(t, v, n, attr, teximage, tex, 0, (s32)t->height - 1, 1);
}

/*
 * [HW] The order the hardware draws polygons in: every opaque polygon first
 * (in the order they were sent), then the translucent ones (alpha 1..30), which
 * in the automatic sort mode (SWAP_BUFFERS bit 0 clear) are sorted by the
 * screen Y of their top vertex, then X, and in manual sort mode keep the order
 * they were sent.  A translucent polygon sent before an opaque one behind it
 * therefore still blends with it.  [AP] "top" is the smallest projected Y of
 * the polygon's vertices, taken in the 256x192 viewport.
 */
typedef struct SortKey {
    u32 index;       /* the polygon's place in the order sent */
    u32 translucent;
    s32 ytop, xtop;
} SortKey;

/* a before b among the automatic mode's translucent polygons: the top
 * vertex's Y, then its X, then the order sent */
static int key_before(const SortKey *a, const SortKey *b)
{
    if (a->ytop != b->ytop) {
        return a->ytop < b->ytop;
    }
    if (a->xtop != b->xtop) {
        return a->xtop < b->xtop;
    }
    return a->index < b->index;
}

/* k[0..n) into that order, with n keys of room at tmp (a merge sort: runs of
 * 1, 2, 4... keys merged from one array into the other). */
static void sort_keys(SortKey *k, SortKey *tmp, u32 n)
{
    SortKey *src = k, *dst = tmp;
    u32 width, i;

    for (width = 1; width < n; width *= 2u) {
        for (i = 0; i < n; i += 2u * width) {
            u32 mid = i + width < n ? i + width : n;
            u32 end = i + 2u * width < n ? i + 2u * width : n;
            u32 l = i, r = mid, o = i;

            while (l < mid && r < end) {
                dst[o++] = key_before(&src[r], &src[l]) ? src[r++] : src[l++];
            }
            while (l < mid) {
                dst[o++] = src[l++];
            }
            while (r < end) {
                dst[o++] = src[r++];
            }
        }
        {
            SortKey *t = src;

            src = dst;
            dst = t;
        }
    }
    if (src != k) {
        memcpy(k, src, (size_t)n * sizeof(*k));
    }
}

void g3d_geom_sort(G3dGeom *g, int manual)
{
    SortKey *k;
    G3dPoly *copy;
    u32 i, any = 0;

    if (g == NULL || g->n_polys < 2u) {
        return;
    }
    /* the keys, then room for as many again (the translucent ones, and the
     * merge sort's second array) */
    k = (SortKey *)malloc((size_t)g->n_polys * 3u * sizeof(*k));
    copy = (G3dPoly *)malloc((size_t)g->n_polys * sizeof(*copy));
    if (k == NULL || copy == NULL) {
        free(k);
        free(copy);
        return;
    }
    for (i = 0; i < g->n_polys; i++) {
        const G3dPoly *p = &g->polys[i];
        u32 a5 = G3D_ATTR_ALPHA(p->attr);
        u32 j;
        s32 ymin = 0x7FFFFFFF, xmin = 0x7FFFFFFF;

        k[i].index = i;
        /* [HW] translucent: alpha 1..30, or a texture with its own alpha
         * (A3I5 and A5I3) unless the polygon is a decal or a shadow */
        u32 fmt = (p->teximage >> 26) & 7u, pm = G3D_ATTR_MODE(p->attr);

        k[i].translucent = (p->attr != 0 && a5 > 0u && a5 < 31u) ? 1u : 0u;
        if (p->attr != 0 && a5 == 31u && (fmt == 1u || fmt == 6u)
            && pm != G3D_POLY_MODE_DECAL && pm != G3D_POLY_MODE_SHADOW) {
            k[i].translucent = 1u;
        }
        any |= k[i].translucent;
        for (j = 0; k[i].translucent && j < p->count && j < 4u; j++) {
            const G3dVertex *v = &g->verts[g->idx[p->first + j]];
            s64 w = v->w > 0 ? v->w : 1;
            s32 sy = (s32)(96 - ((s64)v->y * 96) / w);
            s32 sx = (s32)(128 + ((s64)v->x * 128) / w);

            if (sy < ymin || (sy == ymin && sx < xmin)) {
                ymin = sy;
                xmin = sx;
            }
        }
        k[i].ytop = ymin;
        k[i].xtop = xmin;
    }
    if (any) {
        /* The opaque polygons first, as sent, then the translucent ones: the
         * keys are in the order sent, so one pass splits them, and only the
         * automatic mode has anything to sort (its translucent polygons). */
        SortKey *tr = k + g->n_polys, *tmp = k + 2u * (size_t)g->n_polys;
        u32 n_op = 0, n_tr = 0;

        for (i = 0; i < g->n_polys; i++) {
            if (k[i].translucent) {
                tr[n_tr++] = k[i];
            } else {
                k[n_op++] = k[i];
            }
        }
        if (!manual) {
            sort_keys(tr, tmp, n_tr);
        }
        for (i = 0; i < n_op; i++) {
            copy[i] = g->polys[k[i].index];
        }
        for (i = 0; i < n_tr; i++) {
            copy[n_op + i] = g->polys[tr[i].index];
        }
        memcpy(g->polys, copy, (size_t)g->n_polys * sizeof(*copy));
    }
    free(k);
    free(copy);
}

/*
 * A frame's polygons, in bands of rows run on the compositor's worker pool
 * (engine/g2d/par.c).  Every pixel belongs to one band and each band draws
 * every polygon in order, so a pixel sees the same polygons in the same
 * order as a single pass: the frame is the same to the bit.  Only the
 * statistics are counted once, by the first band.
 */
#define BAND_MAX 8u
#define BAND_MIN_POLYS 64u

typedef struct BandJob {
    G3dTarget *t;
    const G3dGeom *g;
    G3dTexLookup lookup;
    void *user;
    const G3dTexture **tex;   /* per polygon, resolved before the bands */
    u32 bands;
} BandJob;

static void raster_rows(const BandJob *j, s32 ylo, s32 yhi, int stats)
{
    const G3dGeom *g = j->g;
    u32 i;

    for (i = 0; i < g->n_polys; i++) {
        const G3dPoly *poly = &g->polys[i];
        G3dVertex v[4];
        u32 k;

        if (poly->count < 3u || poly->count > 4u) {
            continue;
        }
        for (k = 0; k < poly->count; k++) {
            v[k] = g->verts[g->idx[poly->first + k]];
        }
        raster_band(j->t, v, poly->count, poly->attr, poly->teximage,
                    j->tex != NULL ? j->tex[i]
                    : j->lookup ? j->lookup(j->user, i) : NULL,
                    ylo, yhi, stats);
    }
}

static void band_task(void *ctx, unsigned index)
{
    const BandJob *j = (const BandJob *)ctx;
    s32 h = (s32)j->t->height;
    s32 ylo = (s32)(((u32)h * index) / j->bands);
    s32 yhi = (s32)(((u32)h * (index + 1u)) / j->bands) - 1;

    raster_rows(j, ylo, yhi, index == 0);
}

void g3d_raster_geom(G3dTarget *t, const G3dGeom *g,
                     G3dTexLookup lookup, void *user)
{
    BandJob j;
    u32 i;

    j.t = t;
    j.g = g;
    j.lookup = lookup;
    j.user = user;
    j.tex = NULL;
    j.bands = g2d_par_workers() + 1u;
    if (j.bands > BAND_MAX) {
        j.bands = BAND_MAX;
    }
    if (j.bands >= 2u && g->n_polys >= BAND_MIN_POLYS && t->height >= j.bands) {
        j.tex = (const G3dTexture **)malloc((size_t)g->n_polys * sizeof(*j.tex));
    }
    if (j.tex == NULL) {
        raster_rows(&j, 0, (s32)t->height - 1, 1);
        return;
    }
    /* The lookup decodes and caches on first use, which is not safe from
     * several threads, and a cache that grows may move what it returned
     * earlier: one pass fills the cache, a second takes the pointers. */
    for (i = 0; i < g->n_polys; i++) {
        j.tex[i] = lookup != NULL ? lookup(user, i) : NULL;
    }
    for (i = 0; lookup != NULL && i < g->n_polys; i++) {
        j.tex[i] = lookup(user, i);
    }
    g2d_par_run(j.bands, band_task, &j);
    free((void *)j.tex);
}

/* ------------------------------------------------------------------ */
/* One whole model                                                     */
/* ------------------------------------------------------------------ */

/* Draw one shape under one node: transform the shape's object-space
 * vertices by the node's world matrix and then the camera and rasterise
 * its polygons with the shape's texture.  `xf` is a scratch copy of the
 * geometry whose ox/oy/oz stay object space and whose x/y/z/w are rewritten
 * for this draw. */
static void draw_shape(G3dTarget *t, const G3dModel *m, G3dGeom *xf,
                       const G3dMtx *clip, u32 shape, u32 node,
                       G3dTexArchive *ar)
{
    const G3dShape *sh;
    const G3dMtx *nm;
    const G3dTexture *tex;
    u32 i;

    if (shape >= m->n_shapes) {
        return;
    }
    sh = &m->shapes[shape];
    nm = (node < m->n_nodes) ? &m->nodes[node].world : NULL;
    tex = (ar != NULL && sh->texture[0] != '\0')
              ? g3d_texarchive_get(ar, sh->texture)
              : NULL;

    for (i = 0; i < sh->n_verts; i++) {
        u32 vi = sh->first_vert + i;
        fx32 ox = xf->verts[vi].ox;
        fx32 oy = xf->verts[vi].oy;
        fx32 oz = xf->verts[vi].oz;
        fx32 mw;

        if (vi >= m->geom.n_verts) {
            break;
        }
        if (nm != NULL) {
            g3d_mtx_xform(nm, ox, oy, oz, &ox, &oy, &oz, &mw);
            (void)mw;
        }
        g3d_mtx_xform(clip, ox, oy, oz, &xf->verts[vi].x, &xf->verts[vi].y,
                      &xf->verts[vi].z, &xf->verts[vi].w);
    }
    for (i = 0; i < sh->n_polys; i++) {
        u32 pi = sh->first_poly + i;
        const G3dPoly *poly;
        G3dVertex v[4];
        u32 k;

        if (pi >= m->geom.n_polys) {
            break;
        }
        poly = &m->geom.polys[pi];
        if (poly->count < 3u || poly->count > 4u) {
            continue;
        }
        for (k = 0; k < poly->count; k++) {
            v[k] = xf->verts[m->geom.idx[poly->first + k]];
        }
        g3d_raster_polygon(t, v, poly->count, poly->attr, poly->teximage,
                           tex);
    }
}

void g3d_render_model(G3dTarget *t, const G3dModel *m,
                      const G3dMtx *view, const G3dMtx *proj,
                      G3dTexArchive *ar)
{
    G3dMtx clip;
    G3dGeom xf;
    u32 i, d;

    if (m->geom.n_verts == 0 || m->geom.n_polys == 0) {
        return;
    }
    g3d_mtx_mul(&clip, view, proj);

    xf = m->geom;
    xf.verts = (G3dVertex *)malloc((size_t)m->geom.n_verts * sizeof(G3dVertex));
    if (xf.verts == NULL) {
        return;
    }
    for (i = 0; i < m->geom.n_verts; i++) {
        xf.verts[i] = m->geom.verts[i];
    }
    /*
     * The render-command stream is the authority on what is drawn and under
     * which node; a shape instanced under several nodes is drawn once per
     * node with that node's (and its ancestors') matrix.  A model whose
     * stream carried no draw list falls back to every shape at its own
     * display-list origin, which is the behaviour this engine had before
     * the node dictionary was read.
     */
    if (m->n_draws > 0u) {
        for (d = 0; d < m->n_draws; d++) {
            draw_shape(t, m, &xf, &clip, m->draws[d].shape,
                       m->draws[d].node, ar);
        }
    } else {
        for (i = 0; i < m->n_shapes; i++) {
            draw_shape(t, m, &xf, &clip, i,
                       m->shapes[i].node == 0xFFFFFFFFu ? ~0u
                                                        : m->shapes[i].node,
                       ar);
        }
    }
    free(xf.verts);
}

/* ------------------------------------------------------------------ */
/* Meeting the 2D compositor                                           */
/* ------------------------------------------------------------------ */

void g3d_compose_over(const void *frame_desc, const G3dTarget *t,
                      u32 width, u32 height, u8 *rgba)
{
    const G2dFrameDesc *desc = (const G2dFrameDesc *)frame_desc;
    u32 x, y;

    for (y = 0; y < height; y++) {
        for (x = 0; x < width; x++) {
            u8 *out = rgba + ((size_t)y * width + x) * 4u;
            G2dFrameSample top;
            u32 n = g2d_frame_stack(desc, (s32)x, (s32)y, &top, 1u);

            if (n == 0) {
                /* the 2D frame shows its backdrop here, so the 3D layer
                 * behind it shows instead */
                if (x < t->width && y < t->height) {
                    const u8 *src = t->rgba + ((size_t)y * t->width + x) * 4u;
                    out[0] = src[0];
                    out[1] = src[1];
                    out[2] = src[2];
                    out[3] = 255;
                } else {
                    out[0] = out[1] = out[2] = 0;
                    out[3] = 255;
                }
            } else {
                G2dColor c = top.pal->colors[top.color_index];
                out[0] = c.r;
                out[1] = c.g;
                out[2] = c.b;
                out[3] = 255;
            }
        }
    }
}

/* ------------------------------------------------------------------ */
/* The rear passes: edge marking, fog, anti-aliasing                   */
/* ------------------------------------------------------------------ */

/*
 * [HW] Edge marking (DISP3DCNT bit 5): a pixel an opaque polygon drew is
 * an edge when one of its four neighbours holds a different polygon ID
 * and lies further away; it takes EDGE_COLOR[ID / 8].  A neighbour past
 * the screen border is the clear plane (its ID and depth).  [AP] the
 * colour replaces the pixel's outright, alpha kept, after the translucent
 * polygons were drawn over it.
 */
void g3d_target_edge_mark(G3dTarget *t, const u16 edge[8], u32 clear_id,
                          s32 clear_depth)
{
    static const s32 k_dx[4] = { -1, 1, 0, 0 };
    static const s32 k_dy[4] = { 0, 0, -1, 1 };
    u8 *mark;
    u32 x, y, k;

    if (t->pid == NULL) {
        return;
    }
    mark = (u8 *)calloc((size_t)t->width * t->height, 1);
    if (mark == NULL) {
        return;
    }
    for (y = 0; y < t->height; y++) {
        for (x = 0; x < t->width; x++) {
            size_t o = (size_t)y * t->width + x;

            if ((t->pflag[o] & G3D_PIX_OPAQUE) == 0) {
                continue;
            }
            for (k = 0; k < 4u; k++) {
                s32 nx = (s32)x + k_dx[k], ny = (s32)y + k_dy[k];
                u32 nid;
                s32 nd;

                if (nx < 0 || ny < 0 || (u32)nx >= t->width
                    || (u32)ny >= t->height) {
                    nid = clear_id;
                    nd = clear_depth;
                } else {
                    size_t n = (size_t)(u32)ny * t->width + (u32)nx;

                    nid = t->pid[n];
                    nd = t->depth[n];
                }
                if (nid != t->pid[o] && t->depth[o] < nd) {
                    mark[o] = 1;
                    break;
                }
            }
        }
    }
    for (x = 0; x < t->width * t->height; x++) {
        if (mark[x]) {
            g3d_bgr555(edge[(t->pid[x] >> 3) & 7u], &t->rgba[x * 4u],
                       &t->rgba[x * 4u + 1u], &t->rgba[x * 4u + 2u]);
        }
    }
    free(mark);
}

/*
 * [HW] Fog (DISP3DCNT bit 7) on every pixel whose fog flag is set (an
 * opaque polygon with POLYGON_ATTR bit 15, or the clear plane with
 * CLEAR_COLOR bit 15).  The depth, in the hardware's 15-bit units, is
 * looked up in the 32-entry table: entry i sits at FOG_OFFSET +
 * (i + 1) * (0x400 >> shift), the density is entry 0 before the first and
 * entry 31 after the last and is interpolated linearly between two, and a
 * density of 127 is full fog (128).  Then per channel
 *     out = (fog * density + pixel * (128 - density)) / 128
 * and the same for the alpha; with DISP3DCNT bit 6 only the alpha.
 * [AP] the 15-bit depth is this rasteriser's 23-bit z shifted down by 8
 * (by 9 for a w-buffered frame, whose depth is w in 20.12), and the colour
 * is blended on the 8-bit expansion.
 */
void g3d_target_fog(G3dTarget *t, const u8 table[32], u32 color, u32 offset,
                    u32 shift, int alpha_only)
{
    u32 step = shift > 10u ? 0u : 0x400u >> shift;
    u8 fr, fg, fb;
    u32 fa = (color >> 16) & 0x1Fu;
    u32 i;

    if (t->pflag == NULL) {
        return;
    }
    g3d_bgr555((u16)(color & 0x7FFFu), &fr, &fg, &fb);
    offset &= 0x7FFFu;
    for (i = 0; i < t->width * t->height; i++) {
        s32 dd = t->depth[i];
        u32 d, den;
        u8 *px = t->rgba + (size_t)i * 4u;

        if ((t->pflag[i] & G3D_PIX_FOG) == 0) {
            continue;
        }
        if (dd < 0) {
            dd = 0;
        }
        d = (u32)dd >> (t->wbuffer ? 9 : 8);
        if (d > 0x7FFFu) {
            d = 0x7FFFu;
        }
        if (step == 0u) {
            den = d > offset ? table[31] : table[0];
        } else if (d <= offset + step) {
            den = table[0];
        } else if (d >= offset + step * 32u) {
            den = table[31];
        } else {
            u32 rel = d - offset - step;
            u32 k = rel / step, f = rel % step;

            den = ((u32)table[k] * (step - f) + (u32)table[k + 1u] * f) / step;
        }
        den &= 0x7Fu;
        if (den == 127u) {
            den = 128u;
        }
        if (!alpha_only) {
            px[0] = (u8)((fr * den + px[0] * (128u - den)) >> 7);
            px[1] = (u8)((fg * den + px[1] * (128u - den)) >> 7);
            px[2] = (u8)((fb * den + px[2] * (128u - den)) >> 7);
        }
        {
            u32 a5 = ((u32)px[3] * 31u + 127u) / 255u;

            a5 = (fa * den + a5 * (128u - den)) >> 7;
            px[3] = (u8)(a5 * 255u / 31u);
        }
    }
}

/*
 * [AP] Anti-aliasing (DISP3DCNT bit 4).  The hardware blends the edge
 * pixels of opaque polygons with what lies beneath by their coverage.  The
 * raster records, at each span end of an opaque polygon, the part of the
 * pixel's width the span covers (G3dTarget.cov) and what was there before
 * (G3dTarget.under); this pass blends those pixels that sit on a
 * silhouette, a four-neighbour drawn by no opaque polygon or by one with a
 * different polygon ID, as (pixel * (cov + 1) + beneath * (31 - cov)) / 32.
 * Over a beneath of alpha 0 the colour is kept and the coverage becomes
 * the alpha, so the 2D engine does the blend.  An edge shared by two
 * polygons of one ID is not blended, which keeps a mesh's inner edges
 * from showing the background.  Vertical coverage is not measured.
 */
void g3d_target_antialias(G3dTarget *t)
{
    static const s32 k_dx[4] = { -1, 1, 0, 0 };
    static const s32 k_dy[4] = { 0, 0, -1, 1 };
    u32 x, y, k;

    if (t->pflag == NULL) {
        return;
    }
    for (y = 0; y < t->height; y++) {
        for (x = 0; x < t->width; x++) {
            size_t o = (size_t)y * t->width + x;
            u32 c = t->cov[o];
            int sil = 0;
            u8 *px = t->rgba + o * 4u;
            const u8 *un = t->under + o * 4u;

            if ((t->pflag[o] & (G3D_PIX_OPAQUE | G3D_PIX_EDGE))
                != (G3D_PIX_OPAQUE | G3D_PIX_EDGE) || c >= 31u) {
                continue;
            }
            for (k = 0; k < 4u && !sil; k++) {
                s32 nx = (s32)x + k_dx[k], ny = (s32)y + k_dy[k];
                size_t n;

                if (nx < 0 || ny < 0 || (u32)nx >= t->width
                    || (u32)ny >= t->height) {
                    continue;
                }
                n = (size_t)(u32)ny * t->width + (u32)nx;
                if ((t->pflag[n] & G3D_PIX_OPAQUE) == 0
                    || t->pid[n] != t->pid[o]) {
                    sil = 1;
                }
            }
            if (!sil) {
                continue;
            }
            if (un[3] == 0) {
                px[3] = (u8)((c + 1u) * 255u / 32u);
                continue;
            }
            for (k = 0; k < 3u; k++) {
                px[k] = (u8)((px[k] * (c + 1u) + un[k] * (31u - c)) >> 5);
            }
        }
    }
}
