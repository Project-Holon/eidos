/*
 * engine/ui/item_show.c -- the key-item showcase of the one-screen field
 * (the mock-up "item_get": hyle recordings/item_get_mockup_1080p.mp4, made
 * by E:/dev/mockups/item_get/item_get_anim.py, whose timeline this follows
 * step for step).
 *
 * The game's own cut-in for a key item (ov021 FLD3D_CI, started by
 * SCRCMD_StartFld3dCiByTable) is drawn on the DS screen only and the
 * one-screen world hides it.  This is the same moment in the interface's
 * style, as one element from start to end: the flash, a light opening at
 * the player, the item growing out of it with an overshoot and two whole
 * turns on its upright axis, two bursts of stars, a gentle bob; then the
 * light squashes into a streak that runs down and becomes the item card's
 * panel while the item shrinks into the card's well, and the name and the
 * pocket slide in.  The card it ends as is the one recipe_field.c's
 * item_card draws over the "obtained" message, at the same place, so the
 * message opens under a card that was already there.
 *
 * Every size and place is in canvas pixels; f is the mock-up's frame (60
 * a second, 0 at the flash, the card whole at 136).
 */
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "item_show.h"
#include "port.h"

#define HL      0x008BACu       /* the interface's accent (the card's well) */
#define CYAN    0x00D5C5u
#define GOLD    0xFFD65Au
#define EDGE    0x464850u

static float clampf(float x, float a, float b) { return x < a ? a : x > b ? b : x; }
static float seg(float f, float a, float b) { return clampf((f - a) / (b - a), 0.f, 1.f); }
static float ease_out(float t) { t = 1.f - t; return 1.f - t * t * t; }
static float ease_io(float t) { t = clampf(t, 0.f, 1.f); return t * t * (3.f - 2.f * t); }
static float lerpf(float a, float b, float t) { return a + (b - a) * t; }

static float back_out(float t, float s)
{
    t -= 1.f;
    return t * t * ((s + 1.f) * t + s) + 1.f;
}

static u32 mix_rgb(u32 a, u32 b, float t)
{
    int i;
    u32 out = 0;

    for (i = 16; i >= 0; i -= 8) {
        float ca = (float)((a >> i) & 255u), cb = (float)((b >> i) & 255u);

        out |= (u32)(ca + (cb - ca) * t + 0.5f) << i;
    }
    return out;
}

static int in_tri(float px, float py, const float *t)
{
    float d1 = (px - t[2]) * (t[1] - t[3]) - (t[0] - t[2]) * (py - t[3]);
    float d2 = (px - t[4]) * (t[3] - t[5]) - (t[2] - t[4]) * (py - t[5]);
    float d3 = (px - t[0]) * (t[5] - t[1]) - (t[4] - t[0]) * (py - t[1]);
    int neg = d1 < 0.f || d2 < 0.f || d3 < 0.f, pos = d1 > 0.f || d2 > 0.f || d3 > 0.f;

    return !(neg && pos);
}

static void tri(EidosUiCtx *c, const float *t, u32 rgb, int a)
{
    float x0 = t[0], x1 = t[0], y0 = t[1], y1 = t[1];
    int i, x, y;

    for (i = 1; i < 3; i++) {
        if (t[i * 2] < x0) x0 = t[i * 2];
        if (t[i * 2] > x1) x1 = t[i * 2];
        if (t[i * 2 + 1] < y0) y0 = t[i * 2 + 1];
        if (t[i * 2 + 1] > y1) y1 = t[i * 2 + 1];
    }
    for (y = (int)floorf(y0); y <= (int)ceilf(y1); y++) {
        for (x = (int)floorf(x0); x <= (int)ceilf(x1); x++) {
            if (in_tri((float)x + .5f, (float)y + .5f, t)) {
                ui_pixel(c, x, y, rgb, a);
            }
        }
    }
}

/* Sixteen rays turning slowly, long white ones and short cyan ones, drawn
 * as light: strongest near the item and fading to nothing at their tips
 * (a flat ray over the world reads as a grey shard, not as light). */
static void rays(EidosUiCtx *c, float cx, float cy, float r, float ang, float a)
{
    int i;

    if (a <= 0.f || r < 2.f) {
        return;
    }
    for (i = 0; i < 16; i++) {
        float t = ang + (float)i * 3.14159265f / 8.f;
        float rr = r * (i % 2 == 0 ? 1.f : .62f), w = i % 2 == 0 ? .07f : .05f;
        float p[6], x0, x1, y0, y1;
        u32 rgb = i % 2 == 0 ? UI_WHITE : CYAN;
        int k, x, y;

        p[0] = cx + cosf(t) * rr * .25f;
        p[1] = cy + sinf(t) * rr * .25f;
        p[2] = cx + cosf(t - w) * rr;
        p[3] = cy + sinf(t - w) * rr;
        p[4] = cx + cosf(t + w) * rr;
        p[5] = cy + sinf(t + w) * rr;
        x0 = x1 = p[0];
        y0 = y1 = p[1];
        for (k = 1; k < 3; k++) {
            if (p[k * 2] < x0) x0 = p[k * 2];
            if (p[k * 2] > x1) x1 = p[k * 2];
            if (p[k * 2 + 1] < y0) y0 = p[k * 2 + 1];
            if (p[k * 2 + 1] > y1) y1 = p[k * 2 + 1];
        }
        for (y = (int)floorf(y0); y <= (int)ceilf(y1); y++) {
            for (x = (int)floorf(x0); x <= (int)ceilf(x1); x++) {
                float px = (float)x + .5f, py = (float)y + .5f, d;

                if (!in_tri(px, py, p)) {
                    continue;
                }
                d = sqrtf((px - cx) * (px - cx) + (py - cy) * (py - cy)) / rr;
                d = clampf((d - .25f) / .75f, 0.f, 1.f);
                ui_pixel(c, x, y, rgb, (int)(a * 150.f * (1.f - d) * (1.f - d)));
            }
        }
    }
}

/* A four-point star: its arms as triangles, a white heart. */
static void star(EidosUiCtx *c, float cx, float cy, float r, u32 rgb, float a)
{
    float w, p[6];
    int i, hw;

    if (r < 1.f || a <= 0.f) {
        return;
    }
    w = r * .3f < 1.f ? 1.f : r * .3f;
    for (i = 0; i < 4; i++) {
        float ax = i == 0 ? 0.f : i == 1 ? r : i == 2 ? 0.f : -r;
        float ay = i == 0 ? -r : i == 1 ? 0.f : i == 2 ? r : 0.f;
        float px = ay != 0.f ? w : 0.f, py = ax != 0.f ? w : 0.f;

        p[0] = cx + ax;
        p[1] = cy + ay;
        p[2] = cx + px + (ax != 0.f ? 0.f : 0.f);
        p[3] = cy + py;
        p[4] = cx - px;
        p[5] = cy - py;
        tri(c, p, rgb, (int)(255.f * a));
    }
    hw = (int)(w / 2.f);
    ui_fill(c, (int)cx - hw, (int)cy - hw, hw * 2 + 1, hw * 2 + 1, UI_WHITE, (int)(255.f * a));
}

/* The item's icon at any size (a spin squashes it), mirrored when it shows
 * its back, lit white near the edge of a turn. */
static void icon_blit(EidosUiCtx *c, const UiImage *im, float cx, float cy, float w, float h,
                      int mirror, float lit)
{
    int x0 = (int)floorf(cx - w / 2.f + .5f), y0 = (int)floorf(cy - h / 2.f + .5f);
    int iw = (int)(w + .5f), ih = (int)(h + .5f), x, y;

    if (im == NULL || iw < 1 || ih < 1) {
        return;
    }
    for (y = 0; y < ih; y++) {
        int sy = y * im->h / ih;

        for (x = 0; x < iw; x++) {
            int sx = x * im->w / iw;
            const u8 *p;
            u32 rgb;

            if (mirror) {
                sx = im->w - 1 - sx;
            }
            p = im->rgba + ((size_t)sy * (size_t)im->w + (size_t)sx) * 4u;
            if (p[3] == 0) {
                continue;
            }
            rgb = ((u32)p[0] << 16) | ((u32)p[1] << 8) | p[2];
            if (lit > 0.f) {
                rgb = mix_rgb(rgb, UI_WHITE, lit);
            }
            ui_pixel(c, x0 + x, y0 + y, rgb, p[3]);
        }
    }
}

void item_card_draw(EidosUiCtx *c, int x, int y, int w, const UiImage *icon, const char *name,
                    const char *pocket)
{
    ui_panel(c, x, y, w, ITEM_CARD_H, UI_CUT_TL | UI_CUT_BR, 6, 0x000000u, EDGE, 240);
    ui_panel(c, x + 4, y + 3, 28, 28, UI_CUT_TL | UI_CUT_BR, 4, UI_BODY2, HL, 255);
    if (icon != NULL) {
        ui_image(c, x + 6, y + 5, icon, 1);
    }
    ui_text(c, x + 38, y + 2, name, UI_WHITE, EDGE, UI_FONT_LARGE, 1);
    ui_text(c, x + 38, y + 20, pocket, HL, 0x313131u, UI_FONT_SMALL, 1);
}

int item_card_width(const char *name, const char *pocket)
{
    int w = ui_text_w(name, UI_FONT_LARGE);

    if (ui_text_w(pocket, UI_FONT_SMALL) > w) {
        w = ui_text_w(pocket, UI_FONT_SMALL);
    }
    return w + 50;
}

/* The card's words as they come in: from 8 pixels left, shown from the
 * left edge on as they arrive (the kit's text has no alpha). */
static void card_words(EidosUiCtx *c, int x, int y, int w, const char *name, const char *pocket,
                       float name_t, float pocket_t)
{
    EidosUiRect saved = c->clip;
    float tn = ease_out(name_t), tp = ease_out(pocket_t);

    if (name_t > 0.f) {
        ui_clip(c, x + 34, y, (int)((float)(w - 34) * tn) + 1, ITEM_CARD_H);
        ui_text(c, x + 38 - (int)(8.f * (1.f - tn)), y + 2, name, UI_WHITE, EDGE, UI_FONT_LARGE, 1);
        c->clip = saved;
    }
    if (pocket_t > 0.f) {
        ui_clip(c, x + 34, y, (int)((float)(w - 34) * tp) + 1, ITEM_CARD_H);
        ui_text(c, x + 38 - (int)(8.f * (1.f - tp)), y + 20, pocket, HL, 0x313131u, UI_FONT_SMALL, 1);
        c->clip = saved;
    }
}

typedef struct Star { float t0, a, sp, r; u32 rgb; } Star;

static int stars(Star *out)
{
    static const struct { int seed, n, t0; } bursts[2] = { { 1, 12, 18 }, { 4, 8, 40 } };
    int b, i, n = 0;

    for (b = 0; b < 2; b++) {
        for (i = 0; i < bursts[b].n; i++) {
            Star *s = &out[n++];

            s->t0 = (float)bursts[b].t0;
            s->a = (float)i / (float)bursts[b].n * 6.2831853f + (float)bursts[b].seed * .37f
                   + (float)(i % 3) * .21f;
            s->sp = 2.4f + (float)((i * 7 + bursts[b].seed) % 5) * .55f;
            s->r = (float)(5 + (i * 5 + bursts[b].seed) % 4);
            s->rgb = i % 3 == 0 ? GOLD : i % 3 == 1 ? UI_WHITE : CYAN;
        }
    }
    return n;
}

void item_show_draw(EidosUiCtx *c, float f, float live, int cx, int cy, int card_x, int card_y, int card_w,
                    const UiImage *icon, const char *name, const char *pocket)
{
    const int cw = c->canvas->w, ch = c->canvas->h;
    const float wx = (float)(card_x + 18), wy = (float)(card_y + 17);   /* the card's well */
    float m = ease_io(seg(f, 104, 120));                                /* into the card   */
    float bob = live >= 34.f ? sinf((live - 34.f) * .16f) * 1.5f * (1.f - m) : 0.f;
    float ox = lerpf((float)cx, wx, m), oy = lerpf((float)cy + bob, wy, m);
    float dim = .35f * seg(f, 4, 16) * (1.f - seg(f, 112, 130));
    float g_open = back_out(seg(f, 6, 22), 1.6f), ga, p_t, wb, s_in, spin, sx, scale, fl;
    static Star st[24];
    static int nst;
    int i;

    if (nst == 0) {
        nst = stars(st);
    }
    if (dim > 0.f) {
        ui_fill(c, 0, 0, cw, ch, 0x000000u, (int)(255.f * dim));
    }

    /* the light: rays open with the item and fade as it leaves for the
     * card (no glow behind it: the item alone is the focus) */
    ga = clampf(g_open, 0.f, 1.f);
    rays(c, (float)cx, (float)cy, 70.f * g_open, live * .012f, ga * (1.f - seg(f, 98, 110)));

    /* the card's panel grows out of the well, left to right as the well
     * arrives, a bright edge leading it; its words follow */
    p_t = ease_out(seg(f, 116, 128));
    if (p_t > 0.f) {
        EidosUiRect saved = c->clip;
        int pw = (int)((float)card_w * p_t);
        float pa = p_t * 1.6f > 1.f ? 1.f : p_t * 1.6f;

        if (pw < 30) {
            pw = 30;
        }
        ui_clip(c, card_x, card_y, pw, ITEM_CARD_H);
        ui_panel(c, card_x, card_y, card_w, ITEM_CARD_H, UI_CUT_TL | UI_CUT_BR, 6, 0x000000u, EDGE,
                 (int)(240.f * pa));
        c->clip = saved;
        if (p_t < 1.f) {
            ui_fill(c, card_x + pw - 1, card_y + 3, 1, ITEM_CARD_H - 6, CYAN, (int)(255.f * (1.f - p_t)));
        }
        card_words(c, card_x, card_y, card_w, name, pocket, seg(f, 119, 129), seg(f, 122, 132));
    }

    /* the well: none while the item is the focus; it forms around the
     * item in the last part of its way down, as the card's own well */
    wb = ease_out(seg(m, .55f, 1.f));
    if (wb > 0.f) {
        int size = (int)(28.f + 10.f * (1.f - wb) + .5f);

        ui_panel(c, (int)(ox - (float)size / 2.f + .5f), (int)(oy - (float)size / 2.f + .5f), size, size,
                 UI_CUT_TL | UI_CUT_BR, 4, UI_BODY2, HL, (int)(255.f * wb));
    }

    /* the item: grows out of the light with one gentle overshoot that ends
     * with its spin, two whole turns, so it ends facing front by itself */
    s_in = back_out(seg(f, 8, 36), 1.7f);
    spin = seg(f, 8, 36);
    sx = cosf(ease_out(spin) * 4.f * 3.14159265f);
    scale = lerpf(3.f * s_in, 1.f, m);
    if (scale > .05f && icon != NULL) {
        float lit = (fabsf(sx) < .5f && spin < 1.f) ? (200.f / 255.f) * (1.f - fabsf(sx) / .5f) : 0.f;

        if (m >= 1.f) {
            /* at rest in the card: exactly the card's own icon */
            ui_image(c, card_x + 6, card_y + 5, icon, 1);
        } else {
            icon_blit(c, icon, ox, oy, (float)icon->w * scale * fabsf(sx), (float)icon->h * scale,
                      sx < 0.f, lit);
        }
    }

    /* the stars: two bursts flying out and slowing, twinkling as they go */
    for (i = 0; i < nst; i++) {
        float t = f - st[i].t0, dist, tw;

        if (t < 0.f || t > 46.f) {
            continue;
        }
        dist = st[i].sp * (1.f - expf(-t / 9.f)) * 14.f;
        tw = .6f + .4f * sinf(t * .9f + st[i].a * 3.f);
        star(c, (float)cx + cosf(st[i].a) * dist, (float)cy + sinf(st[i].a) * dist * .85f + t * .12f,
             st[i].r * tw * (1.f - t / 60.f), st[i].rgb, 1.f - t / 46.f);
    }

    /* the flash: the whole window, white, then easing out */
    fl = f < 5.f ? 1.f : 1.f - ease_out(seg(f, 5, 16));
    if (fl > 0.f) {
        ui_fill(c, 0, 0, cw, ch, UI_WHITE, (int)(255.f * fl));
    }
}

/* ------------------------------------------------------------------ */
/* The badges' pictures                                                */
/* ------------------------------------------------------------------ */

/*
 * A badge as the Trainer Card's badge case draws it (its full shine): the
 * badge viewer's graphics, archive a/0/4/0: cell bank member 45 (cell i is
 * badge i, Trio to Legend, each a 32 x 64 object or two), characters
 * member 27 (4 bits, one-dimensional), palette member 2 (the brightest of
 * the polish stages; each object names its 16-colour bank).  Decoded once,
 * 64 x 64 with the cell's origin in the middle.
 */
#define BADGE_PX 64

static UiImage g_badge[8];
static u8 g_badge_px[8][BADGE_PX * BADGE_PX * 4];
static int g_badge_state;                   /* 0 not tried, 1 ok, -1 failed */

static u8 *arc_load(const char *path, u32 member, u32 *size)
{
    BwArcFile f;
    long n;
    u8 *buf = NULL;

    if (bw_arc_open(&f, path) != 0) {
        return NULL;
    }
    n = bw_arc_member_size(&f, member);
    if (n > 0 && (buf = (u8 *)malloc((size_t)n)) != NULL &&
        bw_arc_read_member(&f, member, buf, (u32)n) != n) {
        free(buf);
        buf = NULL;
    }
    bw_arc_close(&f);
    if (buf != NULL) {
        *size = (u32)n;
    }
    return buf;
}

static const u8 *find_magic(const u8 *p, u32 n, const char *m)
{
    u32 i;

    for (i = 0; i + 4 <= n; i++) {
        if (memcmp(p + i, m, 4) == 0) {
            return p + i;
        }
    }
    return NULL;
}

static u32 le16(const u8 *p) { return (u32)p[0] | (u32)p[1] << 8; }
static u32 le32(const u8 *p) { return le16(p) | le16(p + 2) << 16; }

static void badges_load(void)
{
    static const int k_size[3][4][2] = {
        { { 8, 8 }, { 16, 16 }, { 32, 32 }, { 64, 64 } },
        { { 16, 8 }, { 32, 8 }, { 32, 16 }, { 64, 32 } },
        { { 8, 16 }, { 8, 32 }, { 16, 32 }, { 32, 64 } },
    };
    u32 nc = 0, np = 0, ne = 0;
    u8 *chr = arc_load("a/0/4/0", 27, &nc), *pal = arc_load("a/0/4/0", 2, &np);
    u8 *cer = arc_load("a/0/4/0", 45, &ne);
    const u8 *rahc, *ttlp, *kbec, *tiles, *colours, *cells, *oams;
    u32 tsize, ncol, ncell, csize;
    int b;

    g_badge_state = -1;
    if (chr == NULL || pal == NULL || cer == NULL ||
        (rahc = find_magic(chr, nc, "RAHC")) == NULL || (ttlp = find_magic(pal, np, "TTLP")) == NULL ||
        (kbec = find_magic(cer, ne, "KBEC")) == NULL) {
        goto done;
    }
    tsize = le32(rahc + 0x18);
    tiles = rahc + 0x20;
    if ((u32)(tiles - chr) + tsize > nc) {
        goto done;
    }
    colours = ttlp + 0x18;
    ncol = (np - (u32)(colours - pal)) / 2u;
    ncell = le16(kbec + 8);
    csize = le16(kbec + 10) == 1 ? 16u : 8u;
    cells = kbec + 8 + le32(kbec + 12);
    oams = cells + ncell * csize;
    if (ncell < 8) {
        goto done;
    }
    for (b = 0; b < 8; b++) {
        u32 noam = le16(cells + (u32)b * csize), ooff = le32(cells + (u32)b * csize + 4), o;
        u8 *out = g_badge_px[b];

        memset(out, 0, sizeof g_badge_px[b]);
        for (o = 0; o < noam && o < 8; o++) {
            const u8 *a = oams + ooff + o * 6u;
            u32 a0 = le16(a), a1 = le16(a + 2), a2 = le16(a + 4);
            int y = (int)(a0 & 0xFF), x = (int)(a1 & 0x1FF);
            int shape = (int)((a0 >> 14) & 3), sz = (int)((a1 >> 14) & 3), w, h, tx, ty, xx, yy;
            u32 tile = a2 & 0x3FF, bank = (a2 >> 12) & 15;

            if (shape > 2) {
                continue;
            }
            if (y >= 128) y -= 256;
            if (x >= 256) x -= 512;
            w = k_size[shape][sz][0];
            h = k_size[shape][sz][1];
            for (ty = 0; ty < h / 8; ty++) {
                for (tx = 0; tx < w / 8; tx++) {
                    u32 t = tile + (u32)(ty * (w / 8) + tx);

                    if (t * 32u + 32u > tsize) {
                        continue;
                    }
                    for (yy = 0; yy < 8; yy++) {
                        for (xx = 0; xx < 8; xx++) {
                            u32 byte = tiles[t * 32u + (u32)yy * 4u + (u32)(xx >> 1)];
                            u32 v = (xx & 1) ? byte >> 4 : byte & 15u, ci = bank * 16u + v, c;
                            int X = BADGE_PX / 2 + x + tx * 8 + xx, Y = BADGE_PX / 2 + y + ty * 8 + yy;
                            u8 *q;

                            if (v == 0 || ci >= ncol || X < 0 || Y < 0 || X >= BADGE_PX || Y >= BADGE_PX) {
                                continue;
                            }
                            c = le16(colours + ci * 2u);
                            q = out + ((size_t)Y * BADGE_PX + (size_t)X) * 4u;
                            q[0] = (u8)((c & 31u) << 3 | (c & 31u) >> 2);
                            q[1] = (u8)(((c >> 5) & 31u) << 3 | ((c >> 5) & 31u) >> 2);
                            q[2] = (u8)(((c >> 10) & 31u) << 3 | ((c >> 10) & 31u) >> 2);
                            q[3] = 255;
                        }
                    }
                }
            }
        }
        g_badge[b].w = BADGE_PX;
        g_badge[b].h = BADGE_PX;
        g_badge[b].rgba = out;
    }
    g_badge_state = 1;
done:
    free(chr);
    free(pal);
    free(cer);
}

const UiImage *ui_badge_image(int badge)
{
    if (badge < 0 || badge > 7) {
        return NULL;
    }
    if (g_badge_state == 0) {
        badges_load();
    }
    return g_badge_state > 0 ? &g_badge[badge] : NULL;
}
