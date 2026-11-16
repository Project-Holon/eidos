/*
 * engine/avatar/avatar_color.c -- the colours of a look: the player
 * sprite's palette with a part's entries moved to the chosen hair, accent,
 * trousers or skin, plus one extra entry per palette index two parts
 * share (avatar.h, "How a look is drawn").
 *
 * The model, per part of a sprite set:
 *   idx    palette entries only that part uses: recoloured in place;
 *   split  entries the part shares with something else: the pixels the
 *          mask gives the part (avatar_mask.c) take an extra entry
 *          (16 + k) with the part's colour; the others keep theirs;
 *   main   the shade the choice's hue, saturation and lightness aim at.
 * A choice moves the part's main shade to its hue, saturation and
 * lightness as the walking sprite shows it; every other step keeps its
 * own distance in lightness from the main shade and its ratio of
 * saturation, so each sprite keeps its own light and dark steps.  Skin
 * keeps lightness ratios instead (shadow and blush stay as deep on dark
 * tones) and bends its darkest steps towards a floor above the outline.
 *
 * Only the choices' targets below are eidos's own numbers; every colour is
 * made from the palette of the player's own cartridge when the game runs.
 * The arithmetic follows the research model the masks and the choices were
 * reviewed with, Python's colorsys and its rounding included, so the
 * colours match the reviewed ones.
 */
#include <math.h>
#include <string.h>

#include "avatar.h"
#include "avatar_int.h"

static void hls_to_rgb(double h, double l, double s, double *r, double *g, double *b);

/* ------------------------------------------------------------------ */
/* The choices: (hue in degrees, saturation, lightness) of the main     */
/* shade as the walking sprite shows it.  Index 0 is the game's own.    */
/* ------------------------------------------------------------------ */

typedef struct Choice { const char *name; float h, s, l; } Choice;

static const Choice k_hair[AVATAR_HAIR_COUNT] = {
    { "Original", 0, 0, 0 },
    { "Black", 230, 0.12f, 0.13f },     { "Dark brown", 20, 0.40f, 0.19f },
    { "Auburn", 8, 0.55f, 0.30f },      { "Ginger", 18, 0.72f, 0.42f },
    { "Strawberry", 22, 0.62f, 0.58f }, { "Blonde", 44, 0.70f, 0.60f },
    { "Platinum", 48, 0.45f, 0.76f },   { "Silver", 220, 0.10f, 0.66f },
    { "White", 215, 0.12f, 0.85f },     { "Crimson", 352, 0.62f, 0.38f },
    { "Pink", 335, 0.62f, 0.62f },      { "Purple", 275, 0.40f, 0.38f },
    { "Navy", 225, 0.45f, 0.24f },      { "Blue", 210, 0.60f, 0.48f },
    { "Teal", 180, 0.45f, 0.36f },      { "Green", 135, 0.38f, 0.34f },
};
static const Choice k_accent[AVATAR_ACCENT_COUNT] = {
    { "Original", 0, 0, 0 },
    { "Red", 0, 0.78f, 0.64f },        { "Orange", 24, 0.85f, 0.58f },
    { "Gold", 44, 0.80f, 0.52f },      { "Yellow", 54, 0.85f, 0.62f },
    { "Lime", 88, 0.60f, 0.52f },      { "Green", 128, 0.55f, 0.45f },
    { "Teal", 176, 0.65f, 0.42f },     { "Cyan", 190, 0.70f, 0.58f },
    { "Blue", 215, 0.72f, 0.56f },     { "Indigo", 240, 0.55f, 0.50f },
    { "Purple", 272, 0.55f, 0.56f },   { "Magenta", 305, 0.65f, 0.58f },
    { "Pink", 332, 0.80f, 0.66f },     { "Charcoal", 230, 0.10f, 0.34f },
    { "Brown", 25, 0.45f, 0.40f },
};
static const Choice k_pants[AVATAR_PANTS_COUNT] = {
    { "Original", 0, 0, 0 },
    { "Black", 230, 0.10f, 0.17f },    { "Charcoal", 240, 0.19f, 0.42f },
    { "Grey", 220, 0.06f, 0.55f },     { "White", 220, 0.12f, 0.84f },
    { "Khaki", 38, 0.32f, 0.58f },     { "Brown", 22, 0.35f, 0.32f },
    { "Olive", 72, 0.30f, 0.36f },     { "Forest", 140, 0.35f, 0.30f },
    { "Navy", 225, 0.45f, 0.30f },     { "Denim", 214, 0.55f, 0.51f },
    { "Maroon", 350, 0.48f, 0.32f },   { "Plum", 285, 0.30f, 0.36f },
};
static const Choice k_skin[AVATAR_SKIN_COUNT] = {
    { "Original", 0, 0, 0 },
    { "Porcelain", 24, 0.80f, 0.91f }, { "Light", 24, 0.78f, 0.84f },
    { "Beige", 28, 0.62f, 0.76f },     { "Tan", 26, 0.52f, 0.66f },
    { "Olive", 36, 0.36f, 0.60f },     { "Golden", 30, 0.50f, 0.52f },
    { "Brown", 22, 0.45f, 0.42f },     { "Dark brown", 18, 0.42f, 0.33f },
    { "Deep", 15, 0.38f, 0.26f },      { "Very deep", 12, 0.32f, 0.21f },
};

static const Choice *choice_of(int part, int id)
{
    switch (part) {
    case AVATAR_PART_HAIR:   return id > 0 && id < AVATAR_HAIR_COUNT ? &k_hair[id] : NULL;
    case AVATAR_PART_ACCENT: return id > 0 && id < AVATAR_ACCENT_COUNT ? &k_accent[id] : NULL;
    case AVATAR_PART_PANTS:  return id > 0 && id < AVATAR_PANTS_COUNT ? &k_pants[id] : NULL;
    case AVATAR_PART_SKIN:   return id > 0 && id < AVATAR_SKIN_COUNT ? &k_skin[id] : NULL;
    default:                 return NULL;
    }
}

const char *avatar_choice_name(int part, int id)
{
    const Choice *c = choice_of(part, id);

    return c != NULL ? c->name : "Original";
}

/* The colour a choice aims at (0xRRGGBB), for the look's swatches; 0 for
 * the game's own (the caller shows the game's shade). */
u32 avatar_choice_target_rgb(int part, int id)
{
    const Choice *c = choice_of(part, id);
    double r, g, b;

    if (c == NULL) {
        return 0;
    }
    hls_to_rgb(c->h / 360.0, c->l, c->s, &r, &g, &b);
    return ((u32)(r * 255.0 + 0.5) << 16) | ((u32)(g * 255.0 + 0.5) << 8) | (u32)(b * 255.0 + 0.5);
}

int avatar_choice_count(int part)
{
    static const int n[AVATAR_PARTS] = { AVATAR_HAIR_COUNT, AVATAR_ACCENT_COUNT, AVATAR_PANTS_COUNT,
                                         AVATAR_SKIN_COUNT };

    return part >= 0 && part < AVATAR_PARTS ? n[part] : 0;
}

/* ------------------------------------------------------------------ */
/* The sets: which palette entries each part owns                       */
/* ------------------------------------------------------------------ */

#define NONE 0xFF

static const AvatarSetSpec k_sets[AVATAR_SET_COUNT] = {
    /* hero field (and field_alt: the same roles) */
    { { { { NONE }, 0, { 11, 8 }, 2, 11 },
        { { 3, 4 }, 2, { NONE }, 0, 3 },
        { { NONE }, 0, { 1, 2, 8, 14 }, 4, 1 },
        { { 12, 10 }, 2, { 11 }, 1, 12 } }, 15, 8 },
    { { { { NONE }, 0, { 11, 8 }, 2, 11 },
        { { 3, 4 }, 2, { NONE }, 0, 3 },
        { { NONE }, 0, { 1, 2, 8, 14 }, 4, 1 },
        { { 12, 10 }, 2, { 11 }, 1, 12 } }, 15, 8 },
    /* heroine field */
    { { { { NONE }, 0, { 1, 2, 8 }, 3, 2 },
        { { 5, 6, 7 }, 3, { NONE }, 0, 5 },
        { { 3, 4 }, 2, { NONE }, 0, 3 },
        { { 12, 10, 11 }, 3, { NONE }, 0, 12 } }, 15, 8 },
    /* hero back: 1 the outline, 13 the cap's white and the Poke Ball stay */
    { { { { NONE }, 0, { 7, 11 }, 2, 7 },
        { { 15 }, 1, { NONE }, 0, 15 },
        { { NONE }, 0, { 2, 8 }, 2, 2 },
        { { 14 }, 1, { 9, 10, 12, 2, 8 }, 5, 14 } }, 1, AVATAR_NB_PANTS },
    /* heroine back: 14 the outline, 5 the vest and 8 the shirt stay */
    { { { { NONE }, 0, { 6, 7, 10, 11, 12, 13 }, 6, 6 },
        { { 1, 2 }, 2, { NONE }, 0, 1 },
        { { 3, 4 }, 2, { NONE }, 0, 3 },
        { { 9 }, 1, { 10, 11, 12, 13, 6, 7 }, 6, 9 } }, 14, 5 },
    /* hero front */
    { { { { NONE }, 0, { 7, 8 }, 2, 8 },
        { { 1, 2 }, 2, { NONE }, 0, 2 },
        { { NONE }, 0, { 13, 14 }, 2, 13 },
        { { 10, 9 }, 2, { 7, 8 }, 2, 10 } }, 15, AVATAR_NB_NONE },
    /* heroine front: 11 the outline; the lips (4, 5 in the mouth) stay */
    { { { { 2, 3 }, 2, { 1 }, 1, 2 },
        { { NONE }, 0, { 4, 5 }, 2, 5 },
        { { NONE }, 0, { 6, 12 }, 2, 6 },
        { { 10, 9, 7, 8 }, 4, { NONE }, 0, 10 } }, 11, 12 },
};

const AvatarSetSpec *avatar_set_spec(int set)
{
    return set >= 0 && set < AVATAR_SET_COUNT ? &k_sets[set] : NULL;
}

/* The field set whose part shades the choices aim at (the walking sprite). */
static int field_of(int set)
{
    return set == AVATAR_SET_HEROINE_FIELD || set == AVATAR_SET_HEROINE_BACK || set == AVATAR_SET_HEROINE_FRONT
           ? AVATAR_SET_HEROINE_FIELD : AVATAR_SET_HERO_FIELD;
}

int avatar_split_slot(int set, int index, int part)
{
    const AvatarSetSpec *s = avatar_set_spec(set);
    int p, k, n = 16;

    if (s == NULL) {
        return -1;
    }
    for (p = 0; p < AVATAR_PARTS; p++) {
        for (k = 0; k < s->part[p].nsplit; k++) {
            if (p == part && s->part[p].split[k] == index) {
                return n;
            }
            n++;
        }
    }
    return -1;
}

/* ------------------------------------------------------------------ */
/* Colour arithmetic: Python's colorsys, step for step                  */
/* ------------------------------------------------------------------ */

static double pmod1(double x)
{
    double r = fmod(x, 1.0);

    return r < 0 ? r + 1.0 : r;
}

static void rgb_to_hls(double r, double g, double b, double *h, double *l, double *s)
{
    double maxc = r > g ? (r > b ? r : b) : (g > b ? g : b);
    double minc = r < g ? (r < b ? r : b) : (g < b ? g : b);
    double sumc = maxc + minc, rangec = maxc - minc, rc, gc, bc, hh;

    *l = sumc / 2.0;
    if (minc == maxc) {
        *h = 0.0;
        *s = 0.0;
        return;
    }
    *s = *l <= 0.5 ? rangec / sumc : rangec / (2.0 - maxc - minc);
    rc = (maxc - r) / rangec;
    gc = (maxc - g) / rangec;
    bc = (maxc - b) / rangec;
    if (r == maxc) {
        hh = bc - gc;
    } else if (g == maxc) {
        hh = 2.0 + rc - bc;
    } else {
        hh = 4.0 + gc - rc;
    }
    *h = pmod1(hh / 6.0);
}

static double hls_v(double m1, double m2, double hue)
{
    hue = pmod1(hue);
    if (hue < 1.0 / 6.0) {
        return m1 + (m2 - m1) * hue * 6.0;
    }
    if (hue < 0.5) {
        return m2;
    }
    if (hue < 2.0 / 3.0) {
        return m1 + (m2 - m1) * (2.0 / 3.0 - hue) * 6.0;
    }
    return m1;
}

static void hls_to_rgb(double h, double l, double s, double *r, double *g, double *b)
{
    double m1, m2;

    if (s == 0.0) {
        *r = *g = *b = l;
        return;
    }
    m2 = l <= 0.5 ? l * (1.0 + s) : l + s - (l * s);
    m1 = 2.0 * l - m2;
    *r = hls_v(m1, m2, h + 1.0 / 3.0);
    *g = hls_v(m1, m2, h);
    *b = hls_v(m1, m2, h - 1.0 / 3.0);
}

static void hls555(u16 c, double *h, double *l, double *s)
{
    rgb_to_hls((c & 31) / 31.0, ((c >> 5) & 31) / 31.0, ((c >> 10) & 31) / 31.0, h, l, s);
}

/* round() as Python's: half to even */
static int round_even(double v)
{
    return (int)nearbyint(v);
}

static u16 to555(double r, double g, double b)
{
    int q[3], i;
    double v[3];

    v[0] = r; v[1] = g; v[2] = b;
    for (i = 0; i < 3; i++) {
        q[i] = round_even(v[i] * 31.0);
        q[i] = q[i] < 0 ? 0 : q[i] > 31 ? 31 : q[i];
    }
    return (u16)(q[0] | (q[1] << 5) | (q[2] << 10));
}

static double lightness(u16 c)
{
    double h, l, s;

    hls555(c, &h, &l, &s);
    return l;
}

#define LO 0.03
#define HI 0.97
#define SKIN_OUTLINE_GAP 0.09
#define DARK_TONE 0.45
#define SKIN_GAP 0.07

static double soft_clamp(double l)
{
    const double lo = 0.10, hi = 0.90;

    if (l < lo) {
        return lo * exp((l - lo) / lo);
    }
    if (l > hi) {
        return 1.0 - (1.0 - hi) * exp(-(l - hi) / (1.0 - hi));
    }
    return l;
}

static double remap_light(double l0, double l_ref, double l_t)
{
    double d = l0 - l_ref, room;

    if (d > 0) {
        room = (HI - l_t) / fmax(1e-6, HI - l_ref);
    } else {
        room = (l_t - LO) / fmax(1e-6, l_ref - LO);
    }
    return l_t + d * fmin(1.0, fmax(0.0, room));
}

/* One original colour c0 recoloured as a member of `part` of the set. */
static u16 recolour(u16 c0, const u16 *pal, const u16 *field_pal, int set, int part, const Choice *c,
                    double skin_k)
{
    const AvatarSetSpec *spec = avatar_set_spec(set), *ref = avatar_set_spec(field_of(set));
    double h0, l0, s0, h_m, l_m, s_main, hr, l_ref, sr, ratio, s, l, r, g, b;
    double h_t = c->h, s_t = c->s, l_t = c->l;

    hls555(field_pal[ref->part[part].main], &hr, &l_ref, &sr);
    hls555(pal[spec->part[part].main], &h_m, &l_m, &s_main);
    hls555(c0, &h0, &l0, &s0);
    ratio = s_main > 0.02 ? s0 / s_main : 1.0;
    ratio = fmin(1.4, fmax(0.6, ratio));
    s = fmin(1.0, s_t * ratio);
    if (part == AVATAR_PART_SKIN) {
        double dh = 0.0, ho, lo_, so, floor_, knee;

        if (s0 >= 0.25) {
            dh = pmod1(h0 - h_m + 0.5) - 0.5;
        }
        dh = fmax(-0.04, fmin(0.04, dh));
        s = fmin(1.0, s_t * fmin(1.4, fmax(0.3, s_main > 0.02 ? s0 / s_main : 1.0)));
        l = l0 * (l_t / fmax(1e-6, l_ref)) * skin_k;
        hls555(pal[spec->outline], &ho, &lo_, &so);
        floor_ = lo_ + SKIN_OUTLINE_GAP;
        knee = floor_ + 0.14;
        if (l < knee) {
            l = floor_ + (knee - floor_) * exp((l - knee) / (knee - floor_));
        }
        l = fmin(l, 0.95);
        hls_to_rgb(pmod1(h_t / 360.0 + dh), l, s, &r, &g, &b);
        return to555(r, g, b);
    }
    l = soft_clamp(remap_light(l0, l_ref, l_t));
    hls_to_rgb(h_t / 360.0, l, s, &r, &g, &b);
    return to555(r, g, b);
}

/* Every entry a part owns: (slot in the extended palette, original index). */
static int part_entries(int set, int part, int slots[8], int orig[8])
{
    const AvatarPartSpec *p = &avatar_set_spec(set)->part[part];
    int n = 0, k;

    for (k = 0; k < p->nidx; k++) {
        slots[n] = p->idx[k];
        orig[n++] = p->idx[k];
    }
    for (k = 0; k < p->nsplit; k++) {
        slots[n] = avatar_split_slot(set, p->split[k], part);
        orig[n++] = p->split[k];
    }
    return n;
}

/* RGB555 rounding can merge two close steps: from dark to light, a step
 * is lifted by one unit per channel until it is lighter than the one below. */
static void separate_steps(u16 *out, const u16 *pal, int set, int part)
{
    int slots[8], orig[8], n = part_entries(set, part, slots, orig), i, j, t;
    double prev_l = -1.0;
    int have_prev = 0;
    u16 prev_orig = 0, prev_new = 0;

    /* sorted by the original's lightness (stable, as Python's sorted) */
    for (i = 1; i < n; i++) {
        for (j = i; j > 0 && lightness(pal[orig[j - 1]]) > lightness(pal[orig[j]]); j--) {
            t = slots[j]; slots[j] = slots[j - 1]; slots[j - 1] = t;
            t = orig[j]; orig[j] = orig[j - 1]; orig[j - 1] = t;
        }
    }
    for (i = 0; i < n; i++) {
        int slot = slots[i], k;

        if (have_prev && pal[orig[i]] == prev_orig) {
            out[slot] = prev_new;       /* the same original, the same new colour */
            continue;
        }
        for (k = 0; k < 8; k++) {
            u16 v = out[slot];
            int r = v & 31, g = (v >> 5) & 31, b = (v >> 10) & 31;

            if (lightness(v) > prev_l + 1e-6) {
                break;
            }
            r = r + 1 > 31 ? 31 : r + 1;
            g = g + 1 > 31 ? 31 : g + 1;
            b = b + 1 > 31 ? 31 : b + 1;
            out[slot] = (u16)(r | (g << 5) | (b << 10));
        }
        prev_l = lightness(out[slot]);
        prev_orig = pal[orig[i]];
        prev_new = out[slot];
        have_prev = 1;
    }
}

static void build_skin(u16 *out, const u16 *pal, const u16 *field_pal, int set, const Choice *c, double k)
{
    const AvatarPartSpec *p = &avatar_set_spec(set)->part[AVATAR_PART_SKIN];
    int i;

    for (i = 0; i < p->nidx; i++) {
        out[p->idx[i]] = recolour(pal[p->idx[i]], pal, field_pal, set, AVATAR_PART_SKIN, c, k);
    }
    for (i = 0; i < p->nsplit; i++) {
        out[avatar_split_slot(set, p->split[i], AVATAR_PART_SKIN)] =
            recolour(pal[p->split[i]], pal, field_pal, set, AVATAR_PART_SKIN, c, k);
    }
}

/* A dark skin tone is kept off the large dark part it touches most (the
 * set's dark lining or vest): a monotone map of the main shade's lightness
 * around that part's, the whole ramp scaled to reach it. */
static void skin_contrast(u16 *out, const u16 *pal, const u16 *field_pal, int set, const Choice *c)
{
    const AvatarSetSpec *spec = avatar_set_spec(set);
    int main = spec->part[AVATAR_PART_SKIN].main, it;
    double d, m, lo, top = DARK_TONE, want, k, got;

    if (spec->dark_neighbour == AVATAR_NB_NONE) {
        return;
    }
    if (spec->dark_neighbour == AVATAR_NB_PANTS) {
        /* the look's own trousers: their main shade, in place or extra */
        const AvatarPartSpec *ps = &spec->part[AVATAR_PART_PANTS];
        int k, slot = -1;

        for (k = 0; k < ps->nidx; k++) {
            slot = ps->idx[k] == ps->main ? ps->main : slot;
        }
        if (slot < 0) {
            slot = avatar_split_slot(set, ps->main, AVATAR_PART_PANTS);
        }
        d = lightness(out[slot]);
    } else {
        d = lightness(out[spec->dark_neighbour]);
    }
    m = lightness(out[main]);
    lo = lightness(pal[spec->outline]) + SKIN_OUTLINE_GAP;
    if (m >= top || d >= top) {
        return;
    }
    if (m < d) {
        want = m > lo ? lo + (m - lo) * fmax(0.0, (d - SKIN_GAP) - lo) / fmax(1e-6, d - lo) : m;
        want = fmin(want, d - SKIN_GAP);
    } else {
        want = (d + SKIN_GAP) + (m - d) * (top - (d + SKIN_GAP)) / fmax(1e-6, top - d);
    }
    if (fabs(want - m) < 0.004) {
        return;
    }
    k = want / fmax(1e-6, m);
    for (it = 0; it < 6; it++) {
        build_skin(out, pal, field_pal, set, c, k);
        got = lightness(out[main]);
        if (fabs(got - want) < 0.004) {
            break;
        }
        k *= want / fmax(1e-6, got);
        k = fmax(0.2, fmin(4.0, k));
    }
    separate_steps(out, pal, set, AVATAR_PART_SKIN);
}

int avatar_extended_palette(u16 *out, const u16 *pal, const u16 *field_pal, int set, const AvatarLook *look)
{
    const AvatarSetSpec *spec = avatar_set_spec(set);
    int ids[AVATAR_PARTS], p, i, n = 16;

    if (spec == NULL) {
        return 0;
    }
    ids[AVATAR_PART_HAIR] = look->hair;
    ids[AVATAR_PART_ACCENT] = look->accent;
    ids[AVATAR_PART_PANTS] = look->pants;
    ids[AVATAR_PART_SKIN] = look->skin;
    memcpy(out, pal, 16 * sizeof(u16));
    for (p = 0; p < AVATAR_PARTS; p++) {
        n += spec->part[p].nsplit;
    }
    for (i = 16; i < n; i++) {
        out[i] = 0;
    }
    for (p = 0; p < AVATAR_PARTS; p++) {
        const Choice *c = choice_of(p, ids[p]);
        const AvatarPartSpec *ps = &spec->part[p];

        for (i = 0; i < ps->nidx; i++) {
            if (c != NULL) {
                out[ps->idx[i]] = recolour(pal[ps->idx[i]], pal, field_pal, set, p, c, 1.0);
            }
        }
        for (i = 0; i < ps->nsplit; i++) {
            int slot = avatar_split_slot(set, ps->split[i], p);

            out[slot] = c != NULL ? recolour(pal[ps->split[i]], pal, field_pal, set, p, c, 1.0)
                                  : pal[ps->split[i]];
        }
        if (c != NULL) {
            separate_steps(out, pal, set, p);
        }
    }
    {
        const Choice *c = choice_of(AVATAR_PART_SKIN, ids[AVATAR_PART_SKIN]);

        if (c != NULL) {
            skin_contrast(out, pal, field_pal, set, c);
        }
    }
    return n;
}
