/*
 * engine/g3d/texture.c -- the GX texture pixel formats.
 *
 * Provenance: the seven TEXIMAGE_PARAM format codes, their bit layouts
 * and the 4x4-compressed block encoding are public NDS hardware
 * documentation; aletheia's tools/assets/nsbmd.py decoded the same seven
 * independently for the asset lane and aletheia's docs/ASSETS.md records
 * the survey behind them (including that format 5 is absent from every
 * texture in this ROM, which is why this decoder is the one piece here
 * with no real instance behind it -- it is implemented from the
 * documentation and marked as unexercised rather than left out).
 *
 * Palette entries are BGR555, the same packing engine/g2d already
 * expands for the 2D palettes, with the same c * 255 / 31.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "g3d.h"

void g3d_bgr555(u16 v, u8 *r, u8 *g, u8 *b)
{
    *r = (u8)(((v      ) & 0x1Fu) * 255u / 31u);
    *g = (u8)(((v >>  5) & 0x1Fu) * 255u / 31u);
    *b = (u8)(((v >> 10) & 0x1Fu) * 255u / 31u);
}

void g3d_texture_free(G3dTexture *t)
{
    if (t == NULL) {
        return;
    }
    free(t->rgba);
    t->rgba = NULL;
    t->width = t->height = 0;
}

static u32 g3d_bits_per_pixel(u32 format)
{
    switch (format) {
    case G3D_TEXFMT_A3I5:   return 8;
    case G3D_TEXFMT_PAL4:   return 2;
    case G3D_TEXFMT_PAL16:  return 4;
    case G3D_TEXFMT_PAL256: return 8;
    case G3D_TEXFMT_A5I3:   return 8;
    case G3D_TEXFMT_DIRECT: return 16;
    default:                return 0;
    }
}

u32 g3d_texture_byte_size(u32 format, u32 width, u32 height)
{
    if (format == G3D_TEXFMT_COMP) {
        return (width / 4u) * (height / 4u) * 6u;
    }
    return width * height * g3d_bits_per_pixel(format) / 8u;
}

/* Palette colour `i`, or magenta when the index is past the end: an
 * out-of-range index is a decode that went wrong, and it should be
 * visible rather than silently black. */
static void pal_color(const u8 *pal, u32 pal_bytes, u32 i, u8 out[3])
{
    if (pal != NULL && i * 2u + 1u < pal_bytes) {
        g3d_bgr555((u16)(pal[i * 2] | (pal[i * 2 + 1] << 8)),
                   &out[0], &out[1], &out[2]);
    } else {
        out[0] = 255; out[1] = 0; out[2] = 255;
    }
}

static void blend(const u8 a[3], const u8 b[3], u32 wa, u32 wb, u8 out[3])
{
    u32 i;

    for (i = 0; i < 3; i++) {
        out[i] = (u8)((a[i] * wa + b[i] * wb) / (wa + wb));
    }
}

/*
 * Format 5, the 4x4-compressed texel format.  The region is the texel
 * words (one u32 per 4x4 block, sixteen two-bit local indices) followed
 * by the index words (one u16 per block: bits 0-13 a palette address in
 * halfword units, bits 14-15 the interpolation mode).  Both live inside
 * the one region the TEX0 header's texture-data bounds describe, which is
 * what aletheia's asset lane confirmed when it implemented the same
 * decoder; no real instance of this format exists anywhere in this ROM,
 * so this path is documentation-following code that no test exercises
 * against real data.
 */
static int decode_comp(u8 *out, u32 width, u32 height,
                       const u8 *texel, u32 texel_bytes,
                       const u8 *pal, u32 pal_bytes)
{
    u32 bw = width / 4u, bh = height / 4u;
    u32 blocks = bw * bh;
    const u8 *idxs;
    u32 bx, by;

    if (texel_bytes < blocks * 6u) {
        return -1;
    }
    idxs = texel + blocks * 4u;

    for (by = 0; by < bh; by++) {
        for (bx = 0; bx < bw; bx++) {
            u32 bi = by * bw + bx;
            u32 word = (u32)texel[bi * 4] | ((u32)texel[bi * 4 + 1] << 8) |
                       ((u32)texel[bi * 4 + 2] << 16) |
                       ((u32)texel[bi * 4 + 3] << 24);
            u32 iw = (u32)idxs[bi * 2] | ((u32)idxs[bi * 2 + 1] << 8);
            u32 addr = (iw & 0x3FFFu) * 2u;
            u32 mode = (iw >> 14) & 3u;
            u8 c[4][3];
            u8 alpha[4] = { 255, 255, 255, 255 };
            u32 tx, ty;

            pal_color(pal, pal_bytes, addr + 0, c[0]);
            pal_color(pal, pal_bytes, addr + 1, c[1]);
            switch (mode) {
            case 0:
                blend(c[0], c[1], 5, 3, c[2]);
                blend(c[0], c[1], 3, 5, c[3]);
                break;
            case 1:
                blend(c[0], c[1], 1, 1, c[2]);
                c[3][0] = c[3][1] = c[3][2] = 0;
                alpha[3] = 0;
                break;
            case 2:
                pal_color(pal, pal_bytes, addr + 2, c[2]);
                pal_color(pal, pal_bytes, addr + 3, c[3]);
                break;
            default:
                blend(c[0], c[1], 5, 3, c[2]);
                c[3][0] = c[3][1] = c[3][2] = 0;
                alpha[3] = 0;
                break;
            }
            for (ty = 0; ty < 4; ty++) {
                u32 py = by * 4u + ty;
                if (py >= height) {
                    continue;
                }
                for (tx = 0; tx < 4; tx++) {
                    u32 px = bx * 4u + tx;
                    u32 sel, o;
                    if (px >= width) {
                        continue;
                    }
                    sel = (word >> ((ty * 4u + tx) * 2u)) & 3u;
                    o = (py * width + px) * 4u;
                    out[o + 0] = c[sel][0];
                    out[o + 1] = c[sel][1];
                    out[o + 2] = c[sel][2];
                    out[o + 3] = alpha[sel];
                }
            }
        }
    }
    return 0;
}

/* engine/avatar/avatar.h: the player's look.  Weak: the 3D module's own
 * tests link without it, and a picture is then the game's own. */
struct AvatarLook;
extern int avatar_recolor(const u8 *idx, int w, int h, const u16 *pal, int c0_clear,
                          const struct AvatarLook *look, u8 *rgba) __attribute__((weak));

/* A 16-colour texture in one of the player's palettes is a picture of the
 * player (a field form): drawn in the player's look.  Every path that
 * draws the game's textures decodes here: the 3D hardware's textures and
 * the one-screen world's own models and billboards. */
static void avatar_hook(u8 *rgba, u32 w, u32 h, const u8 *texel, const u8 *pal, u32 pal_bytes, int c0_clear)
{
    u8 *idx;
    u16 p16[16];
    u32 i;

    if (avatar_recolor == NULL || pal == NULL || pal_bytes < 32) {
        return;
    }
    for (i = 0; i < 16; i++) {
        p16[i] = (u16)(pal[i * 2] | (pal[i * 2 + 1] << 8));
    }
    if (w > 256 || h > 2048) {
        return;                 /* no picture of the player is wider */
    }
    idx = (u8 *)malloc(w * h);
    if (idx == NULL) {
        return;
    }
    for (i = 0; i < w * h; i++) {
        idx[i] = (u8)((texel[i / 2] >> ((i % 2) * 4)) & 15u);
    }
    (void)avatar_recolor(idx, (int)w, (int)h, p16, c0_clear, NULL, rgba);
    free(idx);
}

int g3d_texture_decode(G3dTexture *out, u32 format, u32 width, u32 height,
                       const u8 *texel, u32 texel_bytes,
                       const u8 *palette, u32 pal_bytes,
                       int color0_transparent)
{
    u32 n = width * height;
    u32 i;
    u8 *rgba;

    memset(out, 0, sizeof(*out));
    if (width == 0 || height == 0 || n > (1u << 22)) {
        return -1;
    }
    if (texel_bytes < g3d_texture_byte_size(format, width, height)) {
        return -1;
    }
    rgba = (u8 *)calloc(n, 4);
    if (rgba == NULL) {
        return -1;
    }

    switch (format) {
    case G3D_TEXFMT_DIRECT:
        for (i = 0; i < n; i++) {
            u16 v = (u16)(texel[i * 2] | (texel[i * 2 + 1] << 8));
            g3d_bgr555((u16)(v & 0x7FFFu), &rgba[i * 4], &rgba[i * 4 + 1],
                       &rgba[i * 4 + 2]);
            rgba[i * 4 + 3] = (v & 0x8000u) ? 255 : 0;
        }
        break;

    case G3D_TEXFMT_A3I5:
    case G3D_TEXFMT_A5I3:
        for (i = 0; i < n; i++) {
            u32 byte = texel[i];
            u32 idx, a;
            if (format == G3D_TEXFMT_A3I5) {
                idx = byte & 0x1Fu;
                a = ((byte >> 5) & 7u) * 255u / 7u;
            } else {
                idx = byte & 7u;
                a = ((byte >> 3) & 0x1Fu) * 255u / 31u;
            }
            pal_color(palette, pal_bytes, idx, &rgba[i * 4]);
            rgba[i * 4 + 3] = (u8)a;
        }
        break;

    case G3D_TEXFMT_PAL256:
        for (i = 0; i < n; i++) {
            u32 idx = texel[i];
            pal_color(palette, pal_bytes, idx, &rgba[i * 4]);
            rgba[i * 4 + 3] = (color0_transparent && idx == 0) ? 0 : 255;
        }
        break;

    case G3D_TEXFMT_PAL4:
    case G3D_TEXFMT_PAL16: {
        u32 bits = (format == G3D_TEXFMT_PAL4) ? 2u : 4u;
        u32 per_byte = 8u / bits;
        u32 mask = (1u << bits) - 1u;
        for (i = 0; i < n; i++) {
            u32 byte = texel[i / per_byte];
            u32 idx = (byte >> ((i % per_byte) * bits)) & mask;
            pal_color(palette, pal_bytes, idx, &rgba[i * 4]);
            rgba[i * 4 + 3] = (color0_transparent && idx == 0) ? 0 : 255;
        }
        if (format == G3D_TEXFMT_PAL16) {
            avatar_hook(rgba, width, height, texel, palette, pal_bytes, color0_transparent);
        }
        break;
    }

    case G3D_TEXFMT_COMP:
        if (decode_comp(rgba, width, height, texel, texel_bytes,
                        palette, pal_bytes) != 0) {
            free(rgba);
            return -1;
        }
        break;

    default:
        free(rgba);
        return -1;
    }

    out->width = width;
    out->height = height;
    out->format = format;
    out->rgba = rgba;
    return 0;
}
