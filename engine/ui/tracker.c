/*
 * engine/ui/tracker.c -- the field's Tracker: what a mod tracks (its areas,
 * their checks, the items that matter) and where everyone is on the map.
 *
 * The data is the mod's (sdk/mod_tracker.h, tracker_set); this file reads
 * it and draws it as a panel over the field, opened from the top bar's
 * Tracker button.  Both tabs show the game's own Town Map picture
 * (a/0/8/5: the painting, bg 3, under the routes and places, bg 2):
 * - You: as PopTracker shows a pack's map, a square on each place with
 *   checks, coloured by how many are in reach, and a preview of the place
 *   the player is in (or the one chosen or pointed at) listing its areas
 *   and their checks; the items grouped under the map;
 * - Map: the player and the mods' Town Map markers (the other players of
 *   a world) at their places.
 * A zone finds its place the game's way: its parent zone, then the row of
 * the place table (a/0/8/6) whose first value is that zone.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ui.h"
#include "kit.h"
#include "assets.h"
#include "input.h"
#include "mod_tracker.h"
#include "mod_state.h"
#include "mod_v6.h"
#include "avatar.h"
#include "theme.h"

#if defined(__GNUC__) || defined(__clang__)
#define TR_WEAK __attribute__((weak))
#else
#define TR_WEAK
#endif

extern u32 ZONEDATA_GetParentZoneId(u32 id) TR_WEAK;
/* sdk/mod_state.c: the state calls (the player's zone) */
extern const BwModState *bw_mod_state_table(void) TR_WEAK;

/* ------------------------------------------------------------------ */
/* The mod's text                                                      */
/* ------------------------------------------------------------------ */

#define TR_AREAS  160
#define TR_CHECKS 2048
#define TR_ITEMS  160
#define TR_ZONES  8

enum { CHK_FOUND, CHK_OPEN, CHK_LOCKED };

typedef struct TrArea {
    char name[40];
    u16  zones[TR_ZONES];
    int  nzones;
    int  first, count;              /* its checks in g_chk                 */
    int  found;
} TrArea;

typedef struct TrCheck {
    char name[48];
    u8   state;
} TrCheck;

typedef struct TrItem {
    char group[20];
    char name[28];
    int  count;
} TrItem;

static TrArea  *g_area;             /* TR_AREAS, on the heap                */
static TrCheck *g_chk;              /* TR_CHECKS                            */
static TrItem  *g_item;             /* TR_ITEMS                             */
static int g_narea, g_nchk, g_nitem, g_found, g_total;
static u32 g_stamp = 0xFFFFFFFFu;

/* The next tab-separated word of a line into out; p moves past it. */
static void word(const char **p, char *out, size_t n)
{
    size_t k = 0;

    while (**p != '\0' && **p != '\t' && **p != '\n' && **p != '\r') {
        if (k + 1 < n) {
            out[k++] = **p;
        }
        (*p)++;
    }
    out[k] = '\0';
    if (**p == '\t') {
        (*p)++;
    }
}

static void parse(void)
{
    const char *p = bw_mod_tracker_text();

    if (g_area == NULL) {
        g_area = (TrArea *)calloc(TR_AREAS, sizeof *g_area);
        g_chk = (TrCheck *)calloc(TR_CHECKS, sizeof *g_chk);
        g_item = (TrItem *)calloc(TR_ITEMS, sizeof *g_item);
        if (g_area == NULL || g_chk == NULL || g_item == NULL) {
            return;
        }
    }
    g_narea = g_nchk = g_nitem = g_found = g_total = 0;
    while (*p != '\0') {
        char kind[8], a[48], b[48], c[48];

        word(&p, kind, sizeof kind);
        if (strcmp(kind, "area") == 0 && g_narea < TR_AREAS) {
            TrArea *ar = &g_area[g_narea++];
            const char *z;

            word(&p, a, sizeof a);
            word(&p, b, sizeof b);
            memset(ar, 0, sizeof *ar);
            snprintf(ar->name, sizeof ar->name, "%.39s", a);
            ar->first = g_nchk;
            for (z = b; *z != '\0' && ar->nzones < TR_ZONES;) {
                ar->zones[ar->nzones++] = (u16)strtoul(z, (char **)&z, 10);
                while (*z == ',' || *z == ' ') z++;
            }
        } else if (strcmp(kind, "check") == 0 && g_narea > 0 && g_nchk < TR_CHECKS) {
            TrCheck *ck = &g_chk[g_nchk++];

            word(&p, a, sizeof a);
            word(&p, b, sizeof b);
            snprintf(ck->name, sizeof ck->name, "%s", b);
            ck->state = strcmp(a, "found") == 0 ? CHK_FOUND : strcmp(a, "open") == 0 ? CHK_OPEN : CHK_LOCKED;
            g_area[g_narea - 1].count++;
            g_area[g_narea - 1].found += ck->state == CHK_FOUND;
        } else if (strcmp(kind, "item") == 0 && g_nitem < TR_ITEMS) {
            TrItem *it = &g_item[g_nitem++];

            word(&p, a, sizeof a);
            word(&p, b, sizeof b);
            word(&p, c, sizeof c);
            snprintf(it->group, sizeof it->group, "%.19s", a);
            it->count = atoi(b);
            snprintf(it->name, sizeof it->name, "%.27s", c);
        } else if (strcmp(kind, "count") == 0) {
            word(&p, a, sizeof a);
            word(&p, b, sizeof b);
            g_found = atoi(a);
            g_total = atoi(b);
        }
        while (*p != '\0' && *p != '\n') p++;
        if (*p == '\n') p++;
    }
    if (g_total == 0) {
        int i;

        for (i = 0; i < g_narea; i++) {
            g_found += g_area[i].found;
            g_total += g_area[i].count;
        }
    }
}

/* EIDOS_DEBUG_TRACKER=<file>: the tracker text from a file, as if a mod had
 * set it (the panel's look without a mod run). */
static void debug_text(void)
{
    static int done;
    const char *path = getenv("EIDOS_DEBUG_TRACKER");
    FILE *f;

    if (done || path == NULL || path[0] == '\0' || bw_mod_tracker_text()[0] != '\0') {
        return;
    }
    done = 1;
    if ((f = fopen(path, "rb")) != NULL) {
        char *buf = (char *)malloc(BW_MOD_TRACKER_MAX);   /* (the image has no room for it) */
        size_t n = buf != NULL ? fread(buf, 1, BW_MOD_TRACKER_MAX, f) : 0;

        fclose(f);
        if (buf != NULL) {
            (void)bw_mod_tracker_set(buf, (u32)n);
            free(buf);
        }
        fprintf(stderr, "eidos: EIDOS_DEBUG_TRACKER: %u bytes from %s\n", (unsigned)n, path);
    }
}

int ui_tracker_available(void)
{
    debug_text();
    return bw_mod_tracker_text()[0] != '\0';
}

/* ------------------------------------------------------------------ */
/* The Town Map picture and places                                     */
/* ------------------------------------------------------------------ */

#define MAP_W 256
#define MAP_H 168

static UiImage g_map;
static int     g_map_tried;
static u16    *g_places;            /* the place table: 27 u16 a row        */
static int     g_nplaces;

static u32 rd16(const u8 *p) { return (u32)p[0] | ((u32)p[1] << 8); }
static u32 rd32(const u8 *p) { return rd16(p) | (rd16(p + 2) << 16); }

/* A Nitro file's block by its magic: its payload (past the 8-byte head). */
static const u8 *block(const u8 *f, u32 size, const char *magic, u32 *bsize)
{
    u32 off, n, k;

    if (f == NULL || size < 16) {
        return NULL;
    }
    off = rd16(f + 12);
    n = rd16(f + 14);
    for (k = 0; k < n && off + 8 <= size; k++) {
        u32 s = rd32(f + off + 4);

        if (memcmp(f + off, magic, 4) == 0) {
            if (bsize != NULL) *bsize = s > 8 ? s - 8 : 0;
            return f + off + 8;
        }
        if (s == 0) break;
        off += s;
    }
    return NULL;
}

/* One 8 bpp background (tiles + screen) over the RGBA picture, colours
 * from the 256-entry palette. */
static void draw_bg(u8 *rgba, const u8 *chr_f, u32 chr_n, const u8 *scr_f, u32 scr_n, const u16 *pal)
{
    u32 cs, ss, i;
    const u8 *ch = block(chr_f, chr_n, "RAHC", &cs);
    const u8 *sc = block(scr_f, scr_n, "NRCS", &ss);
    u32 tiles_size, sw, ents;

    if (ch == NULL || sc == NULL) {
        return;
    }
    tiles_size = rd32(ch + 0x10);
    sw = rd16(sc);
    ents = rd32(sc + 8) / 2;
    for (i = 0; i < ents; i++) {
        u32 e = rd16(sc + 0xC + i * 2), t = e & 0x3FF, tx = (i % (sw / 8)) * 8, ty = (i / (sw / 8)) * 8;
        int hf = (e >> 10) & 1, vf = (e >> 11) & 1, x, y;

        if (ty >= MAP_H || tx >= MAP_W) {
            continue;
        }
        for (y = 0; y < 8; y++) {
            for (x = 0; x < 8; x++) {
                u32 at = t * 64u + (u32)y * 8u + (u32)x;
                u32 v = at < tiles_size ? ch[0x18 + at] : 0;
                int sx = (int)tx + (hf ? 7 - x : x), sy = (int)ty + (vf ? 7 - y : y);
                u16 c;
                u8 *o;

                if (v == 0 || sy >= MAP_H) {
                    continue;
                }
                c = pal[v];
                o = rgba + ((size_t)sy * MAP_W + (size_t)sx) * 4;
                o[0] = (u8)((c & 31) * 255 / 31);
                o[1] = (u8)(((c >> 5) & 31) * 255 / 31);
                o[2] = (u8)(((c >> 10) & 31) * 255 / 31);
                o[3] = 255;
            }
        }
    }
}

static void map_load(void)
{
    u32 pn, n5, n7, n16, n17, in;
    u8 *pf, *c5, *c7, *s16, *s17, *info;
    const u8 *tt;
    u16 pal[256];
    u8 *rgba;
    int i;

    {
        static u32 gen;

        if (gen != eidos_ui_data_generation()) {
            gen = eidos_ui_data_generation();   /* the other version's data: its own map and places */
            free((void *)g_map.rgba);
            g_map.rgba = NULL;
            free(g_places);
            g_places = NULL;
            g_nplaces = 0;
            g_map_tried = 0;
        }
    }
    if (g_map_tried) {
        return;
    }
    g_map_tried = 1;
    pf = ui_arc_member(UI_ARC_TOWNMAP, 0, &pn);
    c5 = ui_arc_member(UI_ARC_TOWNMAP, 5, &n5);
    c7 = ui_arc_member(UI_ARC_TOWNMAP, 7, &n7);
    s16 = ui_arc_member(UI_ARC_TOWNMAP, 16, &n16);
    s17 = ui_arc_member(UI_ARC_TOWNMAP, 17, &n17);
    tt = block(pf, pn, "TTLP", NULL);
    rgba = (u8 *)calloc((size_t)MAP_W * MAP_H, 4);
    if (tt != NULL && rgba != NULL) {
        for (i = 0; i < 256; i++) {
            pal[i] = (u16)rd16(tt + 0x10 + i * 2);
        }
        draw_bg(rgba, c7, n7, s16, n16, pal);  /* the painting           */
        draw_bg(rgba, c5, n5, s17, n17, pal);  /* the routes and places  */
        g_map.w = MAP_W;
        g_map.h = MAP_H;
        g_map.rgba = rgba;
    } else {
        free(rgba);
    }
    free(pf); free(c5); free(c7); free(s16); free(s17);
    info = ui_arc_member(UI_ARC_TOWNINFO, 0, &in);
    if (info != NULL) {
        g_places = (u16 *)info;             /* little-endian u16 rows      */
        g_nplaces = (int)(in / 54u);
    }
}

/* Where a zone is on the map: 1 and its place's middle, or 0. */
static int zone_place(u32 zone, int *x, int *y)
{
    u32 z = ZONEDATA_GetParentZoneId != NULL ? ZONEDATA_GetParentZoneId(zone) : zone;
    int i;

    map_load();
    for (i = 0; g_places != NULL && i < g_nplaces; i++) {
        const u8 *r = (const u8 *)g_places + (size_t)i * 54u;

        if (rd16(r) == z || rd16(r) == zone) {
            *x = (int)rd16(r + 4);
            *y = (int)rd16(r + 6);
            return 1;
        }
    }
    return 0;
}

static int my_zone(void)
{
    const BwModState *st = bw_mod_state_table != NULL ? bw_mod_state_table() : NULL;

    return st != NULL && st->zone != NULL ? (int)st->zone() : -1;
}

/* The area the player is in: one listing the zone or its parent, else -1. */
static int area_here(void)
{
    int z = my_zone(), i, k;
    u32 parent;

    if (z < 0) {
        return -1;
    }
    parent = ZONEDATA_GetParentZoneId != NULL ? ZONEDATA_GetParentZoneId((u32)z) : (u32)z;
    for (i = 0; i < g_narea; i++) {
        for (k = 0; k < g_area[i].nzones; k++) {
            if (g_area[i].zones[k] == (u16)z) {
                return i;
            }
        }
    }
    for (i = 0; i < g_narea; i++) {
        for (k = 0; k < g_area[i].nzones; k++) {
            if (g_area[i].zones[k] == (u16)parent) {
                return i;
            }
        }
    }
    return -1;
}

/* ------------------------------------------------------------------ */
/* The panel                                                           */
/* ------------------------------------------------------------------ */

/* `s` cut to `maxw` pixels with "..." */
static const char *fit_text(char *buf, size_t n, const char *s, int maxw, int font)
{
    size_t k;

    if (ui_text_w(s, font) <= maxw || n < 5) {
        return s;
    }
    for (k = strlen(s) < n - 4 ? strlen(s) : n - 4; k > 0; k--) {
        if (((unsigned char)s[k] & 0xC0u) == 0x80u) {
            continue;
        }
        memcpy(buf, s, k);
        memcpy(buf + k, "...", 4);
        if (ui_text_w(buf, font) <= maxw) {
            return buf;
        }
    }
    return "...";
}

/* The areas by their place on the Town Map: PopTracker's squares, one a
 * place, with every area there (a town and its gym and houses share one).
 * An area with no place has a group of its own, off the map. */
#define TR_GROUP_AREAS 12

typedef struct TrGroup {
    int x, y;                       /* the place on the map; x < 0: none    */
    int area[TR_GROUP_AREAS];
    int n;
    int count, found, open;
} TrGroup;

static TrGroup *g_grp;              /* TR_AREAS                             */
static int g_ngrp;
static int g_grp_of[TR_AREAS];      /* each area's group                    */
static u32 g_grp_stamp = 0xFFFFFFFFu;

static void groups(void)
{
    int i, k;

    if (g_grp_stamp == g_stamp) {
        return;
    }
    g_grp_stamp = g_stamp;
    if (g_grp == NULL && (g_grp = (TrGroup *)calloc(TR_AREAS, sizeof *g_grp)) == NULL) {
        return;
    }
    g_ngrp = 0;
    for (i = 0; i < g_narea; i++) {
        const TrArea *a = &g_area[i];
        int px = -1, py = -1, gi = -1;

        for (k = 0; k < a->nzones && px < 0; k++) {
            if (!zone_place(a->zones[k], &px, &py)) {
                px = -1;
            }
        }
        for (k = 0; px >= 0 && k < g_ngrp; k++) {
            if (g_grp[k].x == px && g_grp[k].y == py && g_grp[k].n < TR_GROUP_AREAS) {
                gi = k;
                break;
            }
        }
        if (gi < 0) {
            gi = g_ngrp++;
            memset(&g_grp[gi], 0, sizeof g_grp[gi]);
            g_grp[gi].x = px;
            g_grp[gi].y = py;
        }
        g_grp[gi].area[g_grp[gi].n++] = i;
        g_grp[gi].count += a->count;
        g_grp[gi].found += a->found;
        for (k = 0; k < a->count; k++) {
            g_grp[gi].open += g_chk[a->first + k].state == CHK_OPEN;
        }
        g_grp_of[i] = gi;
    }
}

/* A square's colour, PopTracker's way: all in reach green, some orange,
 * none red, all found grey (a place with no checks is not drawn). */
#define SQ_ALL  0x3CC83Cu
#define SQ_SOME 0xF0A028u
#define SQ_NONE 0xD83C3Cu
#define SQ_DONE 0x6E6E6Eu

static u32 group_col(const TrGroup *g)
{
    int left = g->count - g->found;

    return left == 0 ? SQ_DONE : g->open == left ? SQ_ALL : g->open > 0 ? SQ_SOME : SQ_NONE;
}

static u32 check_col(int s)
{
    return s == CHK_FOUND ? SQ_DONE : s == CHK_OPEN ? SQ_ALL : SQ_NONE;
}

static int g_tab;                   /* 0 You, 1 Map                         */
static int g_sel = -1;              /* the area chosen (its group shown)    */
static int g_follow = 1;            /* the choice follows the player        */
static int g_map_pan = -10000;      /* the map's pan (pixels); below -9000: centre again */
static int g_pop_top;               /* the preview's first line             */
static int g_pop_grp = -1;          /* the group the preview showed         */
static int g_unfold;                /* the sheet unfolds over 8 frames      */

void ui_tracker_open(void)
{
    g_stamp = 0xFFFFFFFFu;
    g_follow = 1;
    g_map_pan = -10000;             /* centred on the player again */
    g_unfold = 0;                   /* it unfolds from the bar again */
    g_pop_top = 0;
}

/* A row or a tab in the style's own look (its Multiplayer screen parts:
 * kind 0 a card, kind 2 the big button); Kosmos's cut-corner panels when
 * the style has none. */
static void part(EidosUiCtx *c, int kind, int x, int y, int w, int h, int lit, int hover)
{
    if (ui_theme_active() != NULL && ui_theme_active()->mp_part != NULL) {
        ui_theme_active()->mp_part(c, kind, x, y, w, h, lit, hover, 1);
        return;
    }
    if (kind == 2) {
        ui_panel(c, x, y, w, h, UI_CUT_TL | UI_CUT_BR, 4, lit || hover ? UI_CYAN : UI_CYAN2, UI_CYAN, 255);
    } else {
        ui_panel(c, x, y, w, h, UI_CUT_TL | UI_CUT_BR, 3, lit ? 0x0A283Cu : hover ? 0x1C1C1Cu : UI_BODY,
                 lit ? UI_CYAN : UI_EDGE, 255);
    }
}

static void tab_button(EidosUiCtx *c, int x, int y, int w, int h, const char *label, int on, int which)
{
    EidosUiRect r;
    int st;

    r.x = x; r.y = y; r.w = w; r.h = h;
    st = eidos_ui_hit(r);
    if (on) {
        part(c, 2, x, y, w, h, 1, 0);       /* the tab shown: the style's button */
    } else {
        part(c, 0, x, y, w, h, 0, (st & EIDOS_UI_HOVER) != 0);
    }
    ui_text(c, x + (w - ui_text_w(label, UI_FONT_MEDIUM)) / 2, y + ui_text_y(h, UI_FONT_MEDIUM), label,
            on ? UI_WHITE : UI_GREY, -1, UI_FONT_MEDIUM, 1);
    if ((st & EIDOS_UI_CLICK) && g_tab != which) {
        g_tab = which;
        eidos_ui_sound(0x548);
    }
}

/* The preview of a place, as PopTracker's popup: each area there, its
 * checks under it with their marks (found grey and filled, in reach green,
 * not yet red).  Beside the square at (ax, ay), inside (vx, vy, vw, vh);
 * the wheel scrolls it when it is longer. */
static void preview(EidosUiCtx *c, int gi, int ax, int ay, int vx, int vy, int vw, int vh)
{
    const TrGroup *g = &g_grp[gi];
    const int rowh = 13, pw = 220;
    int lines = 0, i, k, ph, px, py, vis, line, ly;
    char buf[64], tmp[96];
    EidosUiRect r, saved;

    for (i = 0; i < g->n; i++) {
        lines += 1 + (g_area[g->area[i]].count > 0 ? g_area[g->area[i]].count : 1);
    }
    if (gi != g_pop_grp) {
        g_pop_grp = gi;
        g_pop_top = 0;
    }
    ph = 10 + lines * rowh;
    if (ph > vh - 8) ph = vh - 8;
    vis = (ph - 10) / rowh;
    px = ax + 10;
    if (px + pw > vx + vw - 4) px = ax - 10 - pw;
    if (px < vx + 4) px = vx + 4;
    py = ay - 12;
    if (py + ph > vy + vh - 4) py = vy + vh - 4 - ph;
    if (py < vy + 4) py = vy + 4;
    r.x = px; r.y = py; r.w = pw; r.h = ph;
    g_pop_top -= eidos_ui_wheel(r);
    if (g_pop_top > lines - vis) g_pop_top = lines - vis;
    if (g_pop_top < 0) g_pop_top = 0;
    ui_panel(c, px, py, pw, ph, UI_CUT_TL | UI_CUT_BR, 5, UI_BODY, UI_EDGE, 245);
    ui_push(c, 0, 0, &saved);
    ui_clip(c, px + 2, py + 4, pw - 4, ph - 8);
    line = 0;
    for (i = 0; i < g->n; i++) {
        const TrArea *a = &g_area[g->area[i]];

        for (k = -1; k < (a->count > 0 ? a->count : 1); k++, line++) {
            if (line < g_pop_top || line >= g_pop_top + vis) {
                continue;
            }
            ly = py + 5 + (line - g_pop_top) * rowh;
            if (k < 0) {
                /* the area: its name, how many found */
                snprintf(buf, sizeof buf, "%d/%d", a->found, a->count);
                ui_text(c, px + 8, ly, fit_text(tmp, sizeof tmp, a->name, pw - 60, UI_FONT_MEDIUM), UI_CYAN, -1,
                        UI_FONT_MEDIUM, 1);
                ui_text(c, px + pw - 12 - ui_text_w(buf, UI_FONT_SMALL), ly + 1, buf,
                        a->found == a->count && a->count > 0 ? SQ_DONE : UI_GREY, -1, UI_FONT_SMALL, 1);
            } else if (a->count == 0) {
                ui_text(c, px + 20, ly, "No checks here.", UI_GREY, -1, UI_FONT_SMALL, 1);
            } else {
                const TrCheck *ck = &g_chk[a->first + k];
                u32 col = check_col(ck->state);

                ui_fill(c, px + 12, ly + 3, 7, 7, UI_RAW(col), 255);
                if (ck->state != CHK_FOUND) {
                    ui_fill(c, px + 13, ly + 4, 5, 5, UI_BODY, 255);     /* a ring: not found yet */
                }
                ui_text(c, px + 24, ly, fit_text(tmp, sizeof tmp, ck->name, pw - 40, UI_FONT_MEDIUM),
                        ck->state == CHK_FOUND ? UI_GREY : UI_WHITE, -1, UI_FONT_MEDIUM, 1);
            }
        }
    }
    ui_pop(c, 0, 0, &saved);
    if (lines > vis) {
        ui_scrollbar(c, px + pw - 6, py + 5, ph - 10, g_pop_top, vis, lines, UI_CYAN);
    }
}

/* The items, by group: a chip each, lit when held; "+N" for the ones the
 * strip has no room for. */
static void items_strip(EidosUiCtx *c, int x, int y, int w, int h)
{
    const int pitch = 17;
    int rows = (h - 4) / pitch, ix = x + 8, row = 0, i;
    const char *group = NULL;
    char buf[64];

    ui_panel(c, x, y, w, h, UI_CUT_TL | UI_CUT_BR, 6, UI_BODY2, UI_EDGE, 240);
    for (i = 0; i < g_nitem; i++) {
        const TrItem *it = &g_item[i];
        int cw, gw = 0, more_w;

        snprintf(buf, sizeof buf, it->count > 1 ? "%s x%d" : "%s", it->name, it->count);
        cw = ui_text_w(buf, UI_FONT_SMALL) + 8;
        if (group == NULL || strcmp(group, it->group) != 0) {
            gw = ui_text_w(it->group, UI_FONT_SMALL) + 6 + (group != NULL ? 6 : 0);
        }
        /* the last row keeps room for "+N" */
        more_w = row == rows - 1 && i + 1 < g_nitem ? ui_text_w("+999", UI_FONT_SMALL) + 6 : 0;
        if (ix + gw + cw + more_w > x + w - 8) {
            if (row + 1 >= rows) {
                snprintf(buf, sizeof buf, "+%d", g_nitem - i);
                ui_text(c, ix + 2, y + 4 + row * pitch + ui_text_y(13, UI_FONT_SMALL), buf, UI_GREY, -1,
                        UI_FONT_SMALL, 1);
                return;
            }
            row++;
            ix = x + 8;
            gw = gw > 0 ? ui_text_w(it->group, UI_FONT_SMALL) + 6 : 0;
        }
        if (gw > 0) {
            /* the group's name, on the line of its first item */
            group = it->group;
            ui_text(c, ix + gw - ui_text_w(it->group, UI_FONT_SMALL) - 6, y + 4 + row * pitch + ui_text_y(13, UI_FONT_SMALL),
                    it->group, UI_CYAN, -1, UI_FONT_SMALL, 1);
            ix += gw;
        }
        ui_panel(c, ix, y + 4 + row * pitch, cw, 13, 0, 0, it->count > 0 ? 0x0A283Cu : 0x0C0E12u,
                 it->count > 0 ? UI_CYAN : UI_EDGE, 255);
        ui_text(c, ix + 4, y + 4 + row * pitch + ui_text_y(13, UI_FONT_SMALL), buf,
                it->count > 0 ? UI_WHITE : 0x707070u, -1, UI_FONT_SMALL, 1);
        ix += cw + 3;
    }
}

/* The Town Map in (x, y, w, h): as wide as the space allows, panned up and
 * down (the wheel, Up and Down on the Map tab), first centred on the
 * chosen place (You) or the player (Map).  You: the places' squares and
 * the chosen or pointed place's preview; Map: the other players too. */
static void map_view(EidosUiCtx *c, int x, int y, int w, int h, u32 frame, int you)
{
    EidosUiRect saved, area;
    int scale, mx, my, vy, vh, full, px, py, i, hover = -1, sel_grp;
    int sq;

    map_load();
    if (g_map.rgba == NULL) {
        ui_text(c, x + 10, y + 10, "The map could not be read.", UI_GREY, -1, UI_FONT_MEDIUM, 1);
        return;
    }
    groups();
    sel_grp = g_sel >= 0 && g_sel < g_narea ? g_grp_of[g_sel] : -1;
    scale = (w - 8) / MAP_W;
    scale = scale < 1 ? 1 : scale;
    sq = 4 + 2 * scale;             /* PopTracker's 12 on a 512 map: 8 at twice */
    full = MAP_H * scale;
    vh = full < h - 8 ? full : h - 8;
    mx = x + (w - MAP_W * scale) / 2;
    vy = y + 4 + (h - 8 - vh) / 2;
    if (g_map_pan < -9000) {
        g_map_pan = 0;
        if (you && sel_grp >= 0 && g_grp[sel_grp].x >= 0) {
            g_map_pan = g_grp[sel_grp].y * scale - vh / 2;
        } else if (my_zone() >= 0 && zone_place((u32)my_zone(), &px, &py)) {
            g_map_pan = py * scale - vh / 2;
        }
    }
    area.x = mx; area.y = vy; area.w = MAP_W * scale; area.h = vh;
    g_map_pan -= eidos_ui_wheel(area) * 16;
    if (g_map_pan > full - vh) g_map_pan = full - vh;
    if (g_map_pan < 0) g_map_pan = 0;
    my = vy - g_map_pan;
    ui_panel(c, mx - 4, vy - 4, MAP_W * scale + 8, vh + 8, UI_CUT_TL | UI_CUT_BR, 6, UI_BODY2, UI_EDGE, 240);
    ui_push(c, 0, 0, &saved);
    ui_clip(c, mx, vy, MAP_W * scale, vh);
    ui_image(c, mx, my, &g_map, scale);

    /* the player: their own figure, at their place */
    if (my_zone() >= 0 && zone_place((u32)my_zone(), &px, &py)) {
        AvatarLook look;
        const UiImage *fig;
        int sx = mx + px * scale, sy = my + py * scale;

        avatar_look_now(&look);
        fig = ui_overworld_look(&look, 0, (int)((frame / 12) & 3));
        if (fig != NULL) {
            ui_image(c, sx - fig->w / 2, sy - fig->h + 4 - (you ? sq / 2 : 0), fig, 1);
        } else {
            ui_fill(c, sx - 3, sy - 3, 7, 7, UI_CYAN, 255);
        }
    }
    /* the places' squares (You only): a dark rim, the colour; the chosen one ringed */
    for (i = 0; i < g_ngrp; i++) {
        const TrGroup *g = &g_grp[i];
        int sx, sy, st;
        EidosUiRect r;

        if (!you || g->x < 0 || g->count == 0) {
            continue;
        }
        sx = mx + g->x * scale - sq / 2;
        sy = my + g->y * scale - sq / 2;
        if (sy + sq < vy || sy > vy + vh) {
            continue;
        }
        if (i == sel_grp) {
            ui_fill(c, sx - 2, sy - 2, sq + 4, sq + 4, UI_WHITE, 255);
        }
        ui_fill(c, sx, sy, sq, sq, UI_RAW(0x101010u), 255);
        ui_fill(c, sx + 1, sy + 1, sq - 2, sq - 2, UI_RAW(group_col(g)), 255);
        r.x = sx - 1; r.y = sy - 1; r.w = sq + 2; r.h = sq + 2;
        if (r.y < vy) { r.h -= vy - r.y; r.y = vy; }
        if (r.y + r.h > vy + vh) r.h = vy + vh - r.y;
        st = r.h > 0 ? eidos_ui_hit(r) : 0;
        if (st & EIDOS_UI_HOVER) {
            hover = i;
        }
        if (st & EIDOS_UI_CLICK) {
            g_sel = g->area[0];
            g_follow = 0;
            g_tab = 0;
            eidos_ui_sound(0x548);
        }
    }
    if (!you) {
        /* the other players (the mods' Town Map markers): an amber pin, a name */
        for (i = 0;; i++) {
            const BwTownmapMarker *m = bw_mod_townmap_marker_at((u32)i);

            if (m == NULL) {
                break;
            }
            if (zone_place(m->zone, &px, &py)) {
                int sx = mx + px * scale, sy = my + py * scale;

                ui_fill(c, sx - 3, sy - 3, 7, 7, UI_RAW(0x101010u), 255);
                ui_fill(c, sx - 2, sy - 2, 5, 5, UI_RAW(0xF2B33Du), 255);
                ui_text(c, sx + 6, sy - 6, m->name, UI_WHITE, UI_EDGE, UI_FONT_SMALL, 1);
            }
        }
    }
    ui_pop(c, 0, 0, &saved);

    /* the preview: the place pointed at, else the chosen one */
    {
        int gi = hover >= 0 ? hover : you ? sel_grp : -1;

        if (gi >= 0) {
            const TrGroup *g = &g_grp[gi];
            int ax = g->x >= 0 ? mx + g->x * scale : mx, ay = g->x >= 0 ? my + g->y * scale : vy;

            preview(c, gi, ax, ay, mx, vy, MAP_W * scale, vh);
        }
    }
}

/* The sheet under the top bar (its bottom edge at `top`); 1 when it
 * closes (B). */
int ui_tracker_run(EidosUiCtx *c, const EidosUiFrame *in, int cw, int ch, int top)
{
    static const char *const hints_you[] = { "B", "Back", "L R", "Tab", "Up Down", "Place", "A", "Here" };
    static const char *const hints_map[] = { "B", "Back", "L R", "Tab", "Up Down", "Move" };
    int mw = cw - 24, mh, mx, my, here, closed = 0, tab0 = g_tab;
    u32 k = eidos_ui_keys_pressed();
    char sub[96];

    debug_text();
    if (bw_mod_tracker_stamp() != g_stamp) {
        g_stamp = bw_mod_tracker_stamp();
        parse();
        if (g_sel >= g_narea) g_sel = -1;
    }
    groups();
    here = area_here();
    if (g_follow && here >= 0 && g_sel != here) {
        g_sel = here;
        g_map_pan = -10000;                 /* the map follows too */
    }
    if (g_sel < 0 && g_narea > 0) {
        g_sel = 0;
    }
    /* the keys are the panel's while it is up */
    eidos_ui_take_keys(BW_KEY_UP | BW_KEY_DOWN | BW_KEY_LEFT | BW_KEY_RIGHT | BW_KEY_A | BW_KEY_B | BW_KEY_X |
                       BW_KEY_Y | BW_KEY_L | BW_KEY_R | BW_KEY_START | BW_KEY_SELECT);
    if (k & (BW_KEY_L | BW_KEY_R | BW_KEY_LEFT | BW_KEY_RIGHT)) {
        g_tab = g_tab ? 0 : 1;
        eidos_ui_sound(0x548);
    }
    if (g_tab == 0 && (k & (BW_KEY_UP | BW_KEY_DOWN)) && g_ngrp > 0 && g_sel >= 0) {
        /* the next place with checks, in the mod's order */
        int gi = g_grp_of[g_sel], n;

        for (n = 0; n < g_ngrp; n++) {
            gi = (gi + ((k & BW_KEY_UP) ? g_ngrp - 1 : 1)) % g_ngrp;
            if (g_grp[gi].count > 0) {
                break;
            }
        }
        g_sel = g_grp[gi].area[0];
        g_follow = 0;
        g_map_pan = -10000;
        eidos_ui_sound(0x548);
    }
    if (g_tab == 0 && (k & BW_KEY_A)) {
        g_follow = 1;                       /* back to where the player is */
        g_map_pan = -10000;
        eidos_ui_sound(0x548);
    }
    if (g_tab == 1 && (k & (BW_KEY_UP | BW_KEY_DOWN))) {
        g_map_pan += (k & BW_KEY_UP) ? -24 : 24;   /* clamped as it is drawn */
    }
    if (k & BW_KEY_B) {
        closed = 1;
        eidos_ui_sound(0x551);
    }

    /* a sheet unfolding from the top bar, as the C-Gear's does, wide and
     * tall: the field stays under the bar and around the sheet */
    if (mw > 600) mw = 600;
    mh = ch - top - ui_bar_h(c) - 6;
    if (mh > 340) mh = 340;
    mx = (cw - mw) / 2;
    my = top;
    {
        int full = mh, shown, fx = mx, fy = my, fw = mw, fh, themed;
        EidosUiRect saved;

        if (g_unfold < 8) {
            g_unfold++;
        }
        shown = full * g_unfold / 8;
        fh = shown;
        themed = ui_theme_active_has_frame() && ui_app_window(c, "sheet", &fx, &fy, &fw, &fh);
        if (!themed) {
            ui_panel(c, mx, my, mw, shown, UI_CUT_BL | UI_CUT_BR, 6, UI_BODY, UI_EDGE, 240);
            ui_fill(c, mx + 6, my + shown - 1, mw - 12, 1, UI_CYAN, 255);
        }
        ui_push(c, 0, 0, &saved);
        ui_clip(c, mx, my, mw, shown);
        snprintf(sub, sizeof sub, "%d of %d checks found", g_found, g_total);
        ui_text(c, mx + 10, my + 7, "TRACKER", UI_CYAN, -1, UI_FONT_SMALL, 1);
        ui_text(c, mx + 14 + ui_text_w("TRACKER", UI_FONT_SMALL), my + 7, sub, UI_GREY, -1, UI_FONT_SMALL, 1);
        tab_button(c, mx + mw - 10 - 120, my + 4, 56, 15, "You", g_tab == 0, 0);
        tab_button(c, mx + mw - 10 - 60, my + 4, 56, 15, "Map", g_tab == 1, 1);
        if (g_tab == 0) {
            /* PopTracker's way: the map with the places' squares, the
             * items under it */
            int items_h = g_nitem > 0 ? 42 : 0;

            map_view(c, mx + 8, my + 24, mw - 16, full - 32 - (items_h ? items_h + 6 : 0), in->frame, 1);
            if (items_h > 0) {
                items_strip(c, mx + 8, my + full - 8 - items_h, mw - 16, items_h);
            }
        } else {
            map_view(c, mx + 8, my + 24, mw - 16, full - 32, in->frame, 0);
        }
        ui_pop(c, 0, 0, &saved);
    }
    if (g_tab != tab0) {
        g_map_pan = -10000;                 /* each tab centres its own way */
    }
    ui_hints(c, ch - ui_bar_h(c), cw, g_tab ? hints_map : hints_you, g_tab ? 3 : 4, NULL);
    return closed;
}
