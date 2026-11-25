/*
 * engine/avatar/avatar.c -- the player's look (avatar.h): the look kept in
 * the settings, the player's palettes read from the cartridge, and the
 * recolour of one picture.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "avatar.h"
#include "avatar_int.h"
#include "port.h"
#include "mp.h"                     /* a session's players see the look */

/* engine/platform/settings.h */
extern void eidos_settings_avatar(int out[5]);
extern void eidos_settings_avatar_set(const int v[5]);

/* Where each set's palette is in the cartridge: the field forms' in a/0/4/9
 * (member 6 Hilbert walking, 7 Hilbert cycling, a shade different, and 9
 * Hilda walking; every field form of a body uses one of them), the
 * battle's in the trainer blocks' own palettes (8 members a block, the
 * palette last): a/0/7/3 blocks 0 and 1 the back with the throw, a/0/7/2
 * blocks 0 and 1 the front. */
static const struct { const char *arc; u32 member; int nclr; } k_src[AVATAR_SET_COUNT] = {
    { "a/0/4/9", 6, 0 }, { "a/0/4/9", 7, 0 }, { "a/0/4/9", 9, 0 },
    { "a/0/7/3", 0 * 8 + 7, 1 }, { "a/0/7/3", 1 * 8 + 7, 1 },
    { "a/0/7/2", 0 * 8 + 7, 1 }, { "a/0/7/2", 1 * 8 + 7, 1 },
};
static u16 g_pal[AVATAR_SET_COUNT][16];
static int g_init;                     /* 0 not tried, 1 read, -1 not there */
static u32 g_version = 1;

/* Whether the game running is one of the Multiplayer screen's: a session,
 * the new game of a mode, or a save under Your games (sdk/mp.h,
 * sdk/mod_menu.h, sdk/save_slots.h; weak: the tests link without them). */
extern u32 bw_mp_player_count(void) __attribute__((weak));
extern int bw_mod_menu_mode_running(void) __attribute__((weak));
extern int bw_slots_current_hidden(void) __attribute__((weak));

static int look_shown(void)
{
    static int last = -1;
    int on = (bw_mp_player_count != NULL && bw_mp_player_count() > 0) ||
             (bw_mod_menu_mode_running != NULL && bw_mod_menu_mode_running()) ||
             (bw_slots_current_hidden != NULL && bw_slots_current_hidden());

    if (on != last) {
        if (last >= 0) {
            g_version++;            /* every picture of the player is made again */
        }
        last = on;
    }
    return on;
}

u32 avatar_version(void)
{
    (void)look_shown();
    return g_version;
}

/* ------------------------------------------------------------------ */
/* The look                                                            */
/* ------------------------------------------------------------------ */

static AvatarLook clamp_look(const int v[5])
{
    AvatarLook l;

    l.body = (u8)(v[0] == 1 ? 1 : 0);
    l.hair = (u8)(v[1] > 0 && v[1] < AVATAR_HAIR_COUNT ? v[1] : 0);
    l.accent = (u8)(v[2] > 0 && v[2] < AVATAR_ACCENT_COUNT ? v[2] : 0);
    l.pants = (u8)(v[3] > 0 && v[3] < AVATAR_PANTS_COUNT ? v[3] : 0);
    l.skin = (u8)(v[4] > 0 && v[4] < AVATAR_SKIN_COUNT ? v[4] : 0);
    return l;
}

void avatar_look(AvatarLook *out)
{
    int v[5];

    eidos_settings_avatar(v);
    *out = clamp_look(v);
}

void avatar_look_now(AvatarLook *out)
{
    avatar_look(out);
    if (!look_shown()) {
        out->hair = out->accent = out->pants = out->skin = 0;
    }
}

void avatar_set_look(const AvatarLook *look)
{
    int v[5];

    v[0] = look->body;
    v[1] = look->hair;
    v[2] = look->accent;
    v[3] = look->pants;
    v[4] = look->skin;
    eidos_settings_avatar_set(v);
    g_version++;                    /* every recoloured picture is made again */
    bw_mp_look_changed();           /* the other players of a session see it */
}

void avatar_look_bytes(u8 out[5])
{
    AvatarLook l;

    avatar_look(&l);
    out[0] = l.body;
    out[1] = l.hair;
    out[2] = l.accent;
    out[3] = l.pants;
    out[4] = l.skin;
}

void avatar_look_from_bytes(const u8 in[5], AvatarLook *out)
{
    int v[5], i;

    for (i = 0; i < 5; i++) {
        v[i] = in[i];
    }
    *out = clamp_look(v);
}

/* ------------------------------------------------------------------ */
/* The palettes from the cartridge                                     */
/* ------------------------------------------------------------------ */

static u32 rd16(const u8 *p) { return (u32)p[0] | ((u32)p[1] << 8); }
static u32 rd32(const u8 *p) { return rd16(p) | (rd16(p + 2) << 16); }

/* The first palette of a BTX0 file (its TEX0 block's palette list). */
static int btx_palette0(const u8 *d, u32 n, u16 out[16])
{
    u32 t = 0, blocks, i, info, data, num, p, esize, off;

    if (n >= 4 && memcmp(d, "TEX0", 4) != 0) {
        if (n < 0x14) {
            return -1;
        }
        blocks = rd16(d + 0x0E);
        for (i = 0, t = 0; i < blocks && 0x10 + 4 * i + 4 <= n; i++) {
            u32 o = rd32(d + 0x10 + 4 * i);

            if (o + 4 <= n && memcmp(d + o, "TEX0", 4) == 0) {
                t = o;
                break;
            }
        }
        if (t == 0) {
            return -1;
        }
    }
    if (t + 0x3C > n) {
        return -1;
    }
    info = t + rd32(d + t + 0x34);
    data = t + rd32(d + t + 0x38);
    if (info + 12 > n) {
        return -1;
    }
    num = d[info + 1];
    p = info + 12 + num * 4;
    if (num == 0 || p + 8 > n) {
        return -1;
    }
    esize = rd16(d + p);
    (void)esize;
    off = rd16(d + p + 4) << 3;
    if (data + off + 32 > n) {
        return -1;
    }
    for (i = 0; i < 16; i++) {
        out[i] = (u16)rd16(d + data + off + i * 2);
    }
    return 0;
}

/* The first 16 colours of an NCLR palette file (its TTLP block). */
static int nclr_palette0(const u8 *d, u32 n, u16 out[16])
{
    u32 body, dsize, cofs, i;

    if (n < 0x28 || memcmp(d, "RLCN", 4) != 0 || memcmp(d + 0x10, "TTLP", 4) != 0) {
        return -1;
    }
    body = 0x18;
    dsize = rd32(d + body + 8);
    cofs = rd32(d + body + 12);
    if (dsize < 32 || body + cofs + 32 > n) {
        return -1;
    }
    for (i = 0; i < 16; i++) {
        out[i] = (u16)rd16(d + body + cofs + i * 2);
    }
    return 0;
}

int avatar_init(void)
{
    static u32 seen;
    BwArcFile arc;
    int s;

    if (seen != bw_data_generation()) {
        seen = bw_data_generation();    /* the other version's data: its own palettes */
        g_init = 0;
    }
    if (g_init != 0) {
        return g_init > 0 ? 0 : -1;
    }
    g_init = -1;
    for (s = 0; s < AVATAR_SET_COUNT; s++) {
        long size;
        u8 *buf;
        int ok;

        if (bw_arc_open(&arc, k_src[s].arc) != 0) {
            if (getenv("EIDOS_AVATAR_LOG") != NULL) {
                fprintf(stderr, "eidos: avatar: %s cannot be opened\n", k_src[s].arc);
            }
            return -1;
        }
        size = k_src[s].member < arc.count ? bw_arc_member_size(&arc, k_src[s].member) : -1;
        buf = size > 0 ? (u8 *)malloc((size_t)size) : NULL;
        ok = buf != NULL && bw_arc_read_member(&arc, k_src[s].member, buf, (u32)size) == size &&
             (k_src[s].nclr ? nclr_palette0(buf, (u32)size, g_pal[s]) : btx_palette0(buf, (u32)size, g_pal[s])) == 0;
        free(buf);
        bw_arc_close(&arc);
        if (!ok) {
            return -1;
        }
    }
    g_init = 1;
    return 0;
}

u32 avatar_choice_rgb(int body, int part, int id)
{
    int set = body == 1 ? AVATAR_SET_HEROINE_FIELD : AVATAR_SET_HERO_FIELD;
    u16 c;

    if (id > 0) {
        return avatar_choice_target_rgb(part, id);
    }
    if (avatar_init() != 0 || part < 0 || part >= AVATAR_PARTS) {
        return 0x808080u;
    }
    c = g_pal[set][avatar_set_spec(set)->part[part].main];
    return ((u32)((c & 31u) * 255u / 31u) << 16) | ((u32)(((c >> 5) & 31u) * 255u / 31u) << 8) |
           (u32)(((c >> 10) & 31u) * 255u / 31u);
}

/* Which of the player's sets a palette is: -1 none.  Entry 0, the
 * see-through colour, is never drawn: an other player's copy of the sprite
 * carries its tag there (avatar_field.c), so only entries 1 to 15 count. */
static int set_of(const u16 *pal)
{
    int s;

    for (s = 0; s < AVATAR_SET_COUNT; s++) {
        if (memcmp(pal + 1, g_pal[s] + 1, 15 * sizeof(u16)) == 0) {
            return s;
        }
    }
    return -1;
}

void avatar_bump_version(void)
{
    g_version++;
}

/* ------------------------------------------------------------------ */
/* One picture                                                         */
/* ------------------------------------------------------------------ */

static void put_rgb(u8 *px, u16 c)
{
    px[0] = (u8)((c & 31u) * 255u / 31u);
    px[1] = (u8)(((c >> 5) & 31u) * 255u / 31u);
    px[2] = (u8)(((c >> 10) & 31u) * 255u / 31u);
}

int avatar_recolor(const u8 *idx, int w, int h, const u16 *pal, int c0_clear, const AvatarLook *look, u8 *rgba)
{
    AvatarLook mine;
    const AvatarSetSpec *spec;
    u16 ext[16 + 16];
    u8 *mask, part_of[16];
    int set, i, p, k;

    if (idx == NULL || pal == NULL || rgba == NULL || w <= 0 || h <= 0 || avatar_init() != 0) {
        return 0;
    }
    set = set_of(pal);
    if (getenv("EIDOS_AVATAR_LOG") != NULL) {
        static int said[AVATAR_SET_COUNT + 1];

        if (!said[set + 1]) {
            said[set + 1] = 1;
            fprintf(stderr, "eidos: avatar: a %dx%d picture, set %d (palette %04X %04X %04X; hero %04X %04X %04X)\n", w,
                    h, set, pal[1], pal[2], pal[3], g_pal[0][1], g_pal[0][2], g_pal[0][3]);
        }
    }
    if (set < 0) {
        return 0;
    }
    if (look == NULL) {
        /* an other player's copy: the look its tag stands for; else ours */
        if (pal[0] == g_pal[set][0] || !avatar_field_tag_look(pal[0], &mine)) {
            avatar_look_now(&mine);
        }
        look = &mine;
    }
    if (getenv("EIDOS_AVATAR_LOG") != NULL) {
        static int n;

        if (n++ < 6) {
            fprintf(stderr, "eidos: avatar: set %d %dx%d look %d %d %d %d\n", set, w, h, look->hair, look->accent,
                    look->pants, look->skin);
        }
    }
    if (look->hair == 0 && look->accent == 0 && look->pants == 0 && look->skin == 0) {
        return 0;                       /* the game's own colours */
    }
    spec = avatar_set_spec(set);
    /* the choices aim at the shades the walking sprite of the same body shows */
    (void)avatar_extended_palette(ext, pal,
                                  g_pal[set == AVATAR_SET_HEROINE_FIELD || set == AVATAR_SET_HEROINE_BACK ||
                                        set == AVATAR_SET_HEROINE_FRONT ? AVATAR_SET_HEROINE_FIELD
                                                                        : AVATAR_SET_HERO_FIELD],
                                  set, look);
    /* the entries a part owns alone: the palette is enough for them */
    memset(part_of, 0, sizeof part_of);
    for (p = 0; p < AVATAR_PARTS; p++) {
        for (k = 0; k < spec->part[p].nidx; k++) {
            part_of[spec->part[p].idx[k]] = (u8)(p + 1);
        }
    }
    mask = (u8 *)malloc((size_t)(w * h));
    if (mask == NULL) {
        return 0;
    }
    /* the field keeps a form's frames in one tall texture (the walk: 32 of
     * 32 x 32 in 32 x 1024): the masks read one frame at a time */
    {
        int fh = (h > w && h % w == 0) ? w : h, f;

        for (f = 0; f + fh <= h; f += fh) {
            avatar_mask(set, idx + f * w, w, fh, h / fh, mask + f * w);
        }
    }
    for (i = 0; i < w * h; i++) {
        int v = idx[i] & 15, slot = v;

        if (mask[i] == AVATAR_MASK_HAIR_DARK) {
            /* a dot that shades the hair: the hair's darkest shade */
            int s = avatar_split_slot(set, 8, AVATAR_PART_HAIR);

            slot = s >= 0 ? s : v;
        } else if (mask[i] != 0 && part_of[v] == 0) {
            int s = avatar_split_slot(set, v, mask[i] - 1);

            slot = s >= 0 ? s : v;
        }
        put_rgb(rgba + i * 4, ext[slot]);
        rgba[i * 4 + 3] = (u8)(v == 0 && c0_clear ? 0 : 255);
    }
    free(mask);
    return 1;
}
