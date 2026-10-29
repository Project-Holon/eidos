/*
 * engine/ui/recipe_battle.c -- the one-screen recipes for battle: commands, moves, target, battle Bag, switch
 * (engine/ui/ui.h, docs/unified-view-recipes.md, "Battle").
 *
 * What is here
 * ------------
 *
 *   battle_bag      the battle Bag (ov197): the four pockets with the last
 *                   used item as one big panel, the item list, the item
 *                   detail with USE
 *   battle_party    the battle party list (ov198): the switch list, the
 *                   Pokemon menu (SHIFT, SUMMARY, CHECK MOVES), and a small
 *                   summary and move list
 *   battle_action   FIGHT, POKEMON, BAG, RUN
 *   battle_move     the move menu (four moves, BACK)
 *   battle_target   the target choice of a double or triple battle
 *   battle_plain    messages and animations: the world, no interface
 *
 * Where the state comes from
 * --------------------------
 *
 * Every value is read from the game's own structs in the DS address space
 * (docs/unified-view-recipes.md, "Battle: common facts"): the battle effect
 * work gBtlvEff (ov094 0x0220AF20) holds the battle main, the main holds the
 * view core, the core the aux view and the aux view the input.  The Bag's and
 * the party list's own work objects are found through the task list of the
 * battle effect work: the work object's first word is the address of the
 * core's own embedded Bag or party host block, which names it.  Nothing is
 * called in an overlay: the recipe only reads memory, so it needs no
 * trampolines.  A pointer that does not point into the game's address space
 * stops the read and the scene falls back to the two DS screens.
 *
 * What the player's clicks do
 * ---------------------------
 *
 * A click becomes a tap at the centre of the bottom-screen rectangle the
 * game tests for that control (the tables in docs/unified-view-recipes.md).
 * The game ignores a tap while its screen is changing, so a tap is repeated
 * every few frames until the scene changes (the signature of the scene is
 * the screen, the page and the cursor), and then forgotten.  The keyboard and
 * the pad still reach the game; the drawn highlight follows the game's own
 * cursor, and the mouse hover wins while the mouse is over a control.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ui.h"
#include "kit.h"
#include "assets.h"
#include "settings.h"
#include "dsmem.h"
#include "input.h"
#include "overlay.h"
#include "port.h"

/* The game's own functions, resolved when the game is linked in. */
struct PokemonParam;
extern u32 POKE_TOOL_GetPartyValue(struct PokemonParam *pp, u32 paramId, u32 arg)
    __attribute__((weak));                                    /* 0x02017E1C */
extern u32 WAZA_TOOL_GetParam(u32 move, u32 param)
    __attribute__((weak));                                    /* 0x0201BD28 */
extern u32 POKE_TOOL_GetLevelMinExp(struct PokemonParam *pp)
    __attribute__((weak));
extern u32 POKE_TOOL_GetExpForLevel(u16 species, u16 form, u16 level)
    __attribute__((weak));

/* ------------------------------------------------------------------ */
/* Small helpers                                                       */
/* ------------------------------------------------------------------ */

static int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static u32 rd32(const u8 *p, u32 off)
{
    u32 v;

    memcpy(&v, p + off, 4);
    return v;
}

static u32 rd16(const u8 *p, u32 off)
{
    u16 v;

    memcpy(&v, p + off, 2);
    return v;
}

static int rds16(const u8 *p, u32 off)
{
    s16 v;

    memcpy(&v, p + off, 2);
    return v;
}

static u8 *rdp(const u8 *p, u32 off)
{
    return (u8 *)(uintptr_t)rd32(p, off);
}

/* A game pointer: inside the game's main RAM (the DS address space is
 * identity mapped in the port, so a game pointer is a host pointer). */
static int gptr_ok(const void *p)
{
    uintptr_t a = (uintptr_t)p;

    return a >= 0x02000000u && a < 0x02400000u && (a & 3u) == 0;
}

static int g_debug = -1;

static int debug_on(void)
{
    if (g_debug < 0) {
        g_debug = getenv("EIDOS_UI_BATTLE_DEBUG") != NULL;
    }
    return g_debug;
}

static void put_num(char *out, int cap, int v)
{
    snprintf(out, (size_t)cap, "%d", v);
}

/* ------------------------------------------------------------------ */
/* The game's text: message banks of a/0/0/2                           */
/* ------------------------------------------------------------------ */

enum {
    MSG_BAG_UI = 17,        /* the battle Bag's labels                     */
    MSG_PLIST_UI = 18,      /* the battle party list's texts               */
    MSG_DESC = 53,          /* item descriptions, one per item id          */
    MSG_ITEM = 54,          /* item names                                  */
    MSG_SPECIES = 70,       /* species names                               */
    MSG_MOVE = 203          /* move names                                  */
};

#define BANK_SLOTS 8
static struct { int member; u8 *data; u32 size; } g_bank[BANK_SLOTS];
static BwArcFile g_msg_arc;
static int g_msg_arc_state;

static const u8 *bank_get(int member, u32 *size)
{
    int i, free_slot = -1;
    long n;
    u8 *buf;

    for (i = 0; i < BANK_SLOTS; i++) {
        if (g_bank[i].member == member) {
            *size = g_bank[i].size;
            return g_bank[i].data;
        }
        if (g_bank[i].member == 0 && free_slot < 0) {
            free_slot = i;
        }
    }
    if (free_slot < 0) {
        return NULL;
    }
    if (g_msg_arc_state == 0) {
        g_msg_arc_state = bw_arc_open(&g_msg_arc, "a/0/0/2") == 0 ? 1 : -1;
    }
    g_bank[free_slot].member = member;          /* remember a failure too */
    if (g_msg_arc_state != 1 || (u32)member >= g_msg_arc.count) {
        return NULL;
    }
    n = bw_arc_member_size(&g_msg_arc, (u32)member);
    if (n <= 0) {
        return NULL;
    }
    buf = (u8 *)malloc((size_t)n);
    if (buf == NULL) {
        return NULL;
    }
    if (bw_arc_read_member(&g_msg_arc, (u32)member, buf, (u32)n) != n) {
        free(buf);
        return NULL;
    }
    g_bank[free_slot].data = buf;
    g_bank[free_slot].size = (u32)n;
    *size = (u32)n;
    return buf;
}

static int put_utf8(char *out, int at, int cap, u32 cp)
{
    if (cp < 0x80u) {
        if (at + 1 >= cap) return at;
        out[at++] = (char)cp;
    } else if (cp < 0x800u) {
        if (at + 2 >= cap) return at;
        out[at++] = (char)(0xC0u | (cp >> 6));
        out[at++] = (char)(0x80u | (cp & 0x3Fu));
    } else {
        if (at + 3 >= cap) return at;
        out[at++] = (char)(0xE0u | (cp >> 12));
        out[at++] = (char)(0x80u | ((cp >> 6) & 0x3Fu));
        out[at++] = (char)(0x80u | (cp & 0x3Fu));
    }
    return at;
}

/* Units of the game's text to UTF-8: control codes (0xF000 and its kind and
 * arguments) are dropped, a line break is a space. */
static void units_to_utf8_sub(const u16 *u, int n, char *out, int cap, const char *sub)
{
    int i = 0, at = 0;

    while (i < n && at < cap - 4) {
        u32 c = u[i++];

        if (c == 0xFFFFu) {
            break;
        }
        if (c == 0xF000u) {
            int args;

            if (i + 1 < n) {
                args = u[i + 1];
                i += 2 + args;
            }
            /* a tag stands for a word the caller names (a nickname) */
            while (sub != NULL && *sub != '\0' && at < cap - 4) {
                out[at++] = *sub++;
            }
            continue;
        }
        if (c == 0xFFFEu) {
            at = put_utf8(out, at, cap, ' ');
            continue;
        }
        if (c < 0x20u) {
            continue;
        }
        at = put_utf8(out, at, cap, c);
    }
    out[at] = '\0';
}

static void units_to_utf8(const u16 *u, int n, char *out, int cap)
{
    units_to_utf8_sub(u, n, out, cap, NULL);
}

/* One line of a message bank as UTF-8, a tag in it replaced by `sub`; false
 * (and "") when unavailable. */
static int msg_line_sub(int member, u32 idx, const char *sub, char *out, int cap)
{
    u32 size = 0, count, sect, off, chars, i;
    const u8 *b = bank_get(member, &size);
    u16 units[256];
    u16 key;

    out[0] = '\0';
    if (b == NULL || size < 20) {
        return 0;
    }
    count = rd16(b, 2);
    sect = rd32(b, 12);
    if (idx >= count || sect + 4 + 8ull * count > size) {
        return 0;
    }
    off = rd32(b, sect + 4 + 8 * idx);
    chars = rd16(b, sect + 4 + 8 * idx + 4);
    if (chars == 0 || chars > 256 || sect + off + 2ull * chars > size) {
        return 0;
    }
    key = (u16)((idx + 3) * 0x2983u);
    for (i = 0; i < chars; i++) {
        u16 v = (u16)rd16(b, sect + off + 2 * i);

        units[i] = (u16)(v ^ key);
        key = (u16)((key << 3) | (key >> 13));
    }
    units_to_utf8_sub(units, (int)chars, out, cap, sub);
    return 1;
}

static int msg_line(int member, u32 idx, char *out, int cap)
{
    return msg_line_sub(member, idx, NULL, out, cap);
}

/* A label from a bank with an English fallback. */
static const char *label(int member, u32 idx, const char *fallback, char *buf, int cap)
{
    if (msg_line(member, idx, buf, cap) && buf[0] != '\0') {
        return buf;
    }
    return fallback;
}

/* ------------------------------------------------------------------ */
/* The battle: where everything is                                     */
/* ------------------------------------------------------------------ */

typedef struct Bt {
    u8 *eff;            /* BtlvEff                                  */
    u8 *main;           /* BtlMain                                  */
    u8 *core;           /* BtlvCore                                 */
    u8 *aux;            /* BtlvAuxView, NULL before the menus       */
    u8 *in;             /* BtlvInput, NULL before the menus         */
    int screen;         /* BtlvInput.screen                         */
    int rule;           /* 0 single, 1 double, 2 triple, 3 rotation */
    u8 *bag;            /* BagWork, NULL when no Bag is up          */
    u8 *plist;          /* B_PlistWork, NULL when no list is up     */
    u32 frame;
} Bt;

#define DS_GBTLVEFF 0x0220AF20u

static Bt g_bt;
static int g_bt_ok;
static u32 g_bt_stamp = 0xFFFFFFFFu;

/* Walk the battle effect work's task list for the work object whose first
 * word is `first`. */
static u8 *find_task_work(const u8 *eff, u32 first)
{
    u8 *sys = rdp(eff, 0);
    u8 *t, *sentinel;
    int guard = 0;

    if (!gptr_ok(sys)) {
        return NULL;
    }
    sentinel = sys + 0x08;
    t = rdp(sentinel, 0x08);
    while (t != NULL && t != sentinel && guard++ < 96) {
        u8 *w;

        if (!gptr_ok(t)) {
            return NULL;
        }
        w = rdp(t, 0x10);
        if (gptr_ok(w) && rd32(w, 0) == first) {
            return w;
        }
        t = rdp(t, 0x08);
    }
    return NULL;
}

static void dbg_stage(const char *what, const void *p)
{
    static const char *last;
    static const void *last_p;

    if (debug_on() && (what != last || p != last_p)) {
        fprintf(stderr, "battle ui: %s %p\n", what, p);
        last = what;
        last_p = p;
    }
}

static int bt_read(Bt *b)
{
    u32 *slot;
    u8 *eff, *main, *core;
    u32 stamp = bw_input_frame_number();

    if (stamp == g_bt_stamp) {
        *b = g_bt;
        return g_bt_ok;
    }
    g_bt_stamp = stamp;
    g_bt_ok = 0;
    memset(&g_bt, 0, sizeof(g_bt));
    g_bt.frame = stamp;
    if (!bw_overlay_is_loaded(93) || !bw_overlay_is_loaded(94)) {
        goto done;
    }
    slot = (u32 *)bw_ds_try_ptr(DS_GBTLVEFF);
    if (slot == NULL) {
        dbg_stage("no gBtlvEff slot", NULL);
        goto done;
    }
    eff = (u8 *)(uintptr_t)*slot;
    if (!gptr_ok(eff)) {
        dbg_stage("gBtlvEff", eff);
        goto done;
    }
    main = rdp(eff, 0x1E4);                 /* setup.battle */
    if (!gptr_ok(main)) {
        dbg_stage("no main", main);
        goto done;
    }
    core = rdp(main, 0x004);                /* viewCore */
    if (!gptr_ok(core)) {
        dbg_stage("no core", core);
        goto done;
    }
    g_bt.eff = eff;
    g_bt.main = main;
    g_bt.core = core;
    g_bt.rule = (int)rd32(eff, 0x1BC);
    {
        u8 *aux = rdp(core, 0x180);

        if (gptr_ok(aux)) {
            u8 *in = rdp(aux, 0xB0);

            g_bt.aux = aux;
            if (gptr_ok(in)) {
                g_bt.in = in;
                g_bt.screen = (int)rd32(in, 0x58);
            }
        }
    }
    if (bw_overlay_is_loaded(197)) {
        g_bt.bag = find_task_work(eff, (u32)(uintptr_t)(core + 0xC8));
    }
    if (g_bt.bag == NULL && bw_overlay_is_loaded(198)) {
        g_bt.plist = find_task_work(eff, (u32)(uintptr_t)(core + 0x10C));
    }
    if (debug_on()) {
        static int last_screen = -2, last_bag = -1, last_pl = -1;
        int bag = g_bt.bag != NULL, pl = g_bt.plist != NULL;

        if (g_bt.screen != last_screen || bag != last_bag || pl != last_pl) {
            fprintf(stderr, "battle ui: frame %u screen %d rule %d bag %p plist %p step %08x sub %d\n",
                    (unsigned)stamp, g_bt.screen, g_bt.rule, (void *)g_bt.bag, (void *)g_bt.plist,
                    (unsigned)rd32(core, 0x14), core[0x174]);
            last_screen = g_bt.screen;
            last_bag = bag;
            last_pl = pl;
        }
    }
    g_bt_ok = 1;
done:
    *b = g_bt;
    return g_bt_ok;
}

/* The battle slot of a view position 2..7 (BTL_MAIN_GetSlotFromViewPos). */
static int view_slot(const Bt *b, int vp)
{
    u32 rule = (u32)b->rule;
    u32 own_first = b->main[0x46D];
    u32 parity = (u32)vp & 1u;
    u32 slot;

    if (vp < 2 || vp > 7) {
        return -1;
    }
    if (rule == 0) {
        slot = parity != 0 ? (own_first != 0 ? 0u : 1u) : own_first;
    } else {
        u32 own_par = own_first & 1u;

        slot = parity != 0 ? (own_par ^ 1u) + (u32)(vp - 3) : own_par + (u32)(vp - 2);
    }
    return slot > 5 ? -1 : (int)slot;
}

/* The Pokemon (a BtlPokeParam) at a view position 2..7, or NULL
 * (BTLV_CORE_GetPokeParamAtViewPos). */
static u8 *poke_at_view(const Bt *b, int vp)
{
    u8 *main = b->main;
    int slot = view_slot(b, vp);
    int last = b->rule == 1 ? 5 : (b->rule == 2 ? 7 : 3);
    u32 cid, nth = 0, i, cnt;
    u8 *slotobj, *poke;

    if (slot < 0 || vp > last) {
        return NULL;
    }
    cid = main[0x428 + slot];
    if (cid > 3) {
        return NULL;
    }
    for (i = 0; i < (u32)slot; i++) {
        if (main[0x428 + i] == cid) {
            nth++;
        }
    }
    slotobj = main + 0xC8 + 4 + cid * 0x1Cu;
    cnt = slotobj[0x18];
    if (nth >= cnt || nth >= 6) {
        return NULL;
    }
    poke = rdp(slotobj, nth * 4);
    return gptr_ok(poke) ? poke : NULL;
}

/* ------------------------------------------------------------------ */
/* Names                                                               */
/* ------------------------------------------------------------------ */

typedef struct MonView {
    char name[40];
    int species, level, hp, maxhp, gender, form;
} MonView;

/* The nickname of a PokemonParam through the game's own accessor (a Strbuf
 * holds the characters). */
static int nickname(u8 *mon, char *out, int cap)
{
    struct {
        u16 capacity, length;
        u32 magic;
        u16 chars[40];
    } sb;
    int i;

    out[0] = '\0';
    if (POKE_TOOL_GetPartyValue == NULL || !gptr_ok(mon)) {
        return 0;
    }
    memset(&sb, 0, sizeof(sb));
    sb.capacity = 22;
    sb.length = 0;
    sb.magic = 0xB6F8D2ECu;
    for (i = 0; i < 40; i++) {
        sb.chars[i] = 0xFFFF;
    }
    POKE_TOOL_GetPartyValue((struct PokemonParam *)mon, 0x73, (u32)(uintptr_t)&sb);
    if (sb.length == 0 || sb.length > 22) {
        return 0;
    }
    units_to_utf8(sb.chars, sb.length, out, cap);
    return out[0] != '\0';
}

static void species_name(int species, char *out, int cap)
{
    if (!msg_line(MSG_SPECIES, (u32)species, out, cap)) {
        snprintf(out, (size_t)cap, "#%d", species);
    }
}

static void mon_view(u8 *poke, MonView *m)
{
    memset(m, 0, sizeof(*m));
    if (poke == NULL) {
        return;
    }
    m->species = (int)rd16(poke, 0x0C);
    m->maxhp = (int)rd16(poke, 0x0E);
    m->hp = (int)rd16(poke, 0x10);
    m->level = poke[0x18];
    m->gender = poke[0xFA];
    m->form = poke[0x141];
    if (!nickname(rdp(poke, 0x00), m->name, sizeof(m->name))) {
        species_name(m->species, m->name, sizeof(m->name));
    }
}

static const char *item_name(int item, char *buf, int cap)
{
    if (!msg_line(MSG_ITEM, (u32)item, buf, cap)) {
        snprintf(buf, (size_t)cap, "ITEM %d", item);
    }
    return buf;
}

/* ------------------------------------------------------------------ */
/* Taps that survive the game being busy                               */
/* ------------------------------------------------------------------ */

static int g_pend;
static int g_pend_x, g_pend_y;
static u32 g_pend_sig, g_pend_since, g_pend_last;

#define TAP_RETRY 10u
#define TAP_GIVE_UP 80u

/* The frame a battle menu scene last ran (tap_service), and how long the
 * plain screen waits after it (plain_match). */
static u32 g_menu_frame = 0xFFFF0000u;
#define PLAIN_AFTER_MENU 10

static void tap_request(int x, int y, u32 sig, u32 frame)
{
    if (debug_on()) {
        fprintf(stderr, "battle ui: tap %d,%d sig %08x frame %u\n", x, y, (unsigned)sig, (unsigned)frame);
    }
    g_pend = 1;
    g_pend_x = x;
    g_pend_y = y;
    g_pend_sig = sig;
    g_pend_since = frame;
    g_pend_last = frame;
    eidos_ui_tap(x, y, 2);
}

/* Called once per frame by every recipe with the scene's signature. */
static void tap_service(u32 sig, u32 frame)
{
    g_menu_frame = bw_input_frame_number();
    if (!g_pend) {
        return;
    }
    if (sig != g_pend_sig || frame - g_pend_since > TAP_GIVE_UP) {
        g_pend = 0;
        return;
    }
    if (frame - g_pend_last >= TAP_RETRY) {
        g_pend_last = frame;
        eidos_ui_tap(g_pend_x, g_pend_y, 2);
    }
}

static void tap_reset(void)
{
    g_pend = 0;
}

/* ------------------------------------------------------------------ */
/* Layout helpers                                                      */
/* ------------------------------------------------------------------ */

static void draw_wrapped_line(void *user, const char *start, int len);

typedef struct Wrap {
    EidosUiCtx *c;
    int x, y, lineh, left, font;
    u32 color;
    s32 shadow;
} Wrap;

static void draw_wrapped_line(void *user, const char *start, int len)
{
    Wrap *w = (Wrap *)user;
    char line[160];

    if (w->left <= 0) {
        return;
    }
    if (len > (int)sizeof(line) - 1) {
        len = (int)sizeof(line) - 1;
    }
    memcpy(line, start, (size_t)len);
    line[len] = '\0';
    ui_text(w->c, w->x, w->y, line, w->color, w->shadow, w->font, 1);
    w->y += w->lineh;
    w->left--;
}

static void draw_wrapped(EidosUiCtx *c, int x, int y, int width, const char *text, int font,
                         int lineh, int maxlines, u32 color, s32 shadow)
{
    Wrap w;

    w.c = c;
    w.x = x;
    w.y = y;
    w.lineh = lineh;
    w.left = maxlines;
    w.font = font;
    w.color = color;
    w.shadow = shadow;
    ui_wrap(text, width, font, draw_wrapped_line, &w);
}

static int hit(EidosUiCtx *c, int x, int y, int w, int h);

/* The key hints, a bar of width w at the bottom edge starting at x (ui_hints
 * takes only a bar from the left edge). Each hint presses its key. */
static void draw_hint_strip_h(EidosUiCtx *c, int x, int w, const char *const *pairs, int n,
                              const u32 *keys, int bh)
{
    int i, y = c->canvas->h - bh, at = x + 6;

    ui_fill(c, x, y, w, bh, UI_BODY2, 235);
    ui_fill(c, x, y, w, 1, UI_EDGE, 255);
    for (i = 0; i < n; i++) {
        const char *key = pairs[2 * i], *what = pairs[2 * i + 1];
        int kw = ui_keycap_w(key);
        int aw = ui_text_w(what, UI_FONT_SMALL);
        int st;

        if (kw < 13) {
            kw = 13;
        }
        st = hit(c, at - 2, y, kw + 4 + aw + 4, bh);
        if (st & EIDOS_UI_HOVER) {
            ui_fill(c, at - 2, y + 1, kw + 4 + aw + 4, bh - 2, UI_EDGE, 255);
        }
        if ((st & EIDOS_UI_CLICK) && keys != NULL && keys[i] != 0) {
            eidos_ui_press(keys[i], 2);
        }
        ui_keycap(c, at, y + (bh - 13) / 2, key);
        at += kw + 4;
        at = ui_text(c, at, y + (bh - 13) / 2 + ui_text_y(13, UI_FONT_SMALL), what, UI_LIGHT,
                     UI_EDGE,
                     UI_FONT_SMALL, 1) + 10;
    }
}

/* The key hints, a bar along the bottom edge (ui_hints' look). */
static void draw_hint_strip(EidosUiCtx *c, int x, int w, const char *const *pairs, int n,
                            const u32 *keys)
{
    draw_hint_strip_h(c, x, w, pairs, n, keys, 17);
}

/* A hit region that also draws nothing; returns the state. */
static int hit(EidosUiCtx *c, int x, int y, int w, int h)
{
    EidosUiRect r;

    (void)c;
    r.x = x;
    r.y = y;
    r.w = w;
    r.h = h;
    return eidos_ui_hit(r);
}

static int hit_wheel(EidosUiCtx *c, int x, int y, int w, int h)
{
    EidosUiRect r;

    (void)c;
    r.x = x;
    r.y = y;
    r.w = w;
    r.h = h;
    return eidos_ui_wheel(r);
}

/* ------------------------------------------------------------------ */
/* Plain battle                                                        */
/* ------------------------------------------------------------------ */

static int plain_match(void)
{
    Bt b;

    if (!bt_read(&b)) {
        return 0;
    }
    /* the screens the menus own are matched by their own recipes; the
     * choice, rotation and counter screens fall back */
    if (b.screen == 0) {
        /* Between two menus (a command chosen, the party opening) the
         * battle passes through its plain screen for a few frames: that
         * would show the game's own plates and box for a moment, so right
         * after a menu the plain screen waits, and the last view holds. */
        if (bw_input_frame_number() - g_menu_frame < PLAIN_AFTER_MENU) {
            return 0;
        }
        return 1;
    }
    return 0;
}

/*
 * The game's message, as the game prints it: the message window's bitmap
 * (BtlvCore.sceneUnit +0x17C, BtlvScu.msgWinChars +0x004, a struct Bmp of
 * 4-bit tiles), drawn letter by letter into the interface's own box.  The
 * commonest colour is the window's ground, the next the letters, the rest
 * their shadow.  Returns 0 when the window holds no text.
 */
/* One pixel of a bitmap of 4-bit tiles, bw pixels wide. */
static int msg_px(const u8 *px, int bw, int x, int y)
{
    u8 v = px[((y >> 3) * (bw >> 3) + (x >> 3)) * 32 + (y & 7) * 4 + ((x & 7) >> 1)];

    return (x & 1) != 0 ? v >> 4 : v & 15;
}

typedef struct Shell {
    int px, pw;         /* the own plates' column, bottom-right             */
    int top;            /* the y above them, where a sheet stands           */
    int n, own_vp[3];   /* the own plates, top to bottom (the chooser last) */
} Shell;

static void shell_layout(EidosUiCtx *c, const Bt *b, Shell *s);
static void shell_draw(EidosUiCtx *c, const Bt *b, const Shell *s, const char *prompt,
                       int own);

/*
 * Coloured move names.  The scene unit keeps the text it printed in a string
 * buffer (BtlvScu.textBuf, +0x034).  A move name in it is looked for in the
 * bitmap: the name set in the game's own font gives the exact letter pixels,
 * and where they all sit on ink pixels the name is found.  Only a name typed
 * so far (three letters or more) is coloured.
 */
#define MV_MAX   700
#define MV_NAME  20
#define MV_FONT  UI_FONT_LARGE
#define MV_FILL  1      /* the letters' colour in the window (GF_FONT_SetTextColor 1, 9, 12) */

static char g_mv_name[MV_MAX][MV_NAME];
static int g_mv_ready;

static int strbuf_text(const u8 *sb, char *out, int cap);

static void mv_names_load(void)
{
    int id, n = 0;

    if (g_mv_ready) {
        return;
    }
    for (id = 1; id < MV_MAX; id++) {
        g_mv_name[id][0] = '\0';
        if (msg_line(MSG_MOVE, (u32)id, g_mv_name[id], MV_NAME) && strlen(g_mv_name[id]) >= 3) {
            n++;
        } else {
            g_mv_name[id][0] = '\0';
        }
    }
    g_mv_ready = n > 100;
}

/* The longest move name inside the message text, as a move id (0: none). */
static int mv_find(const char *text)
{
    int id, best = 0;
    size_t bl = 0;

    for (id = 1; id < MV_MAX; id++) {
        size_t l = strlen(g_mv_name[id]);

        if (l > bl && strstr(text, g_mv_name[id]) != NULL) {
            best = id;
            bl = l;
        }
    }
    return best;
}

/* Does the name's first n letters, set in the game's font with the pen at
 * (ox, oy), sit on ink pixels?  Fills the letters' end x on a hit. */
static int mv_fit(const u8 *px, int bw, int bh, int ink, const UiGlyph *g, int n, int ox, int oy,
                  int *end)
{
    int i, pen = 0, gx, gy;

    for (i = 0; i < n; i++) {
        for (gy = 0; gy < g[i].cell_h; gy++) {
            for (gx = 0; gx < g[i].cell_w; gx++) {
                int k = gy * g[i].cell_w + gx;
                int v = (g[i].bitmap[k >> 2] >> (6 - 2 * (k & 3))) & 3;
                int bx = ox + pen + g[i].bearing + gx, by = oy + gy;

                if (v == 1 && (bx < 0 || bx >= bw || by >= bh || msg_px(px, bw, bx, by) != ink)) {
                    return 0;
                }
            }
        }
        pen += g[i].adv;
    }
    *end = ox + pen;
    return 1;
}

/* Find the move name (or its typed start) in the bitmap and mark the pixels
 * of its box in hit[]: 1 for the letter colour, 2 for the rest.  Returns
 * whether a name was found. */
static int mv_mark(const u8 *px, int bw, int bh, int ground, int ink, const char *name,
                   u8 (*hit)[256])
{
    UiGlyph g[MV_NAME];
    int n = 0, k, ox, oy, end, x, y;

    for (; name[n] != '\0' && n < MV_NAME; n++) {
        if ((u8)name[n] >= 0x80 || !ui_font_glyph(MV_FONT, (u32)(u8)name[n], &g[n])
            || g[n].bitmap == NULL) {
            return 0;
        }
    }
    for (k = n; k >= 3; k--) {
        for (oy = 0; oy + g[0].cell_h <= bh; oy++) {
            for (ox = -2; ox < bw; ox++) {
                if (mv_fit(px, bw, bh, ink, g, k, ox, oy, &end)) {
                    for (y = oy; y < oy + g[0].cell_h && y < bh; y++) {
                        for (x = ox < 0 ? 0 : ox; x <= end && x < bw; x++) {
                            int v = msg_px(px, bw, x, y);

                            hit[y][x] = v == ink ? 1 : v != ground ? 2 : 0;
                        }
                    }
                    return 1;
                }
            }
        }
    }
    return 0;
}

static int draw_battle_message(EidosUiCtx *c, const Bt *b, int x, int y, int w, int h)
{
    const u8 *scu = rdp(b->core, 0x17C);
    const u8 *bmp = scu != NULL ? rdp(scu, 0x004) : NULL;
    const u8 *px;
    int bw, bh, cnt[16] = { 0 }, ground = 0, ink = -1, i, yy, xx, any = 0;
    u8 hit[64][256];
    u32 mcol = 0, mdark = 0;

    if (bmp == NULL || (px = rdp(bmp, 0x00)) == NULL) {
        return 0;
    }
    bw = rd16(bmp, 0x04);
    bh = rd16(bmp, 0x06);
    if (bw <= 0 || bh <= 0 || bw > 256 || bh > 64) {
        return 0;
    }
    for (yy = 0; yy < bh; yy++) {
        for (xx = 0; xx < bw; xx++) {
            cnt[msg_px(px, bw, xx, yy)]++;
        }
    }
    for (i = 1; i < 16; i++) {
        if (cnt[i] > cnt[ground]) {
            ground = i;
        }
    }
    for (i = 0; i < 16; i++) {
        if (i != ground && cnt[i] > 0 && (ink < 0 || cnt[i] > cnt[ink])) {
            ink = i;
        }
    }
    if (ink < 0) {
        return 0;
    }
    memset(hit, 0, sizeof(hit));
    if (eidos_settings_get(EIDOS_OPT_MOVE_COLOURS) != 0) {
        /* the text lives in the scene unit's own buffer (entry messages) or
         * in the core's (the battle's messages) */
        const u8 *src[2] = { rdp(scu, 0x034), rdp(b->core, 0x0AC) };
        char text[128];

        mv_names_load();
        for (i = 0; i < 2; i++) {
            int id = strbuf_text(src[i], text, sizeof(text)) ? mv_find(text) : 0;

            if (id != 0 && mv_mark(px, bw, bh, ground, MV_FILL, g_mv_name[id], hit)) {
                int type = WAZA_TOOL_GetParam != NULL ? (int)WAZA_TOOL_GetParam((u32)id, 0) : 0;

                mcol = ui_type_color(type);
                mdark = ((mcol >> 17) & 0x7F) << 16 | ((mcol >> 9) & 0x7F) << 8
                        | ((mcol >> 1) & 0x7F);
                break;
            }
        }
    }
    ui_panel(c, x, y, w, h, UI_CUT_TL | UI_CUT_BR, 6, UI_BODY, UI_EDGE, 225);
    for (yy = 0; yy < bh && yy + 4 < h; yy++) {
        for (xx = 0; xx < bw && xx + 10 < w; xx++) {
            int v = msg_px(px, bw, xx, yy);

            if (v == ground) {
                continue;
            }
            ui_fill(c, x + 10 + xx, y + (h - bh) / 2 + yy, 1, 1,
                    hit[yy][xx] == 1 ? mcol : hit[yy][xx] == 2 ? mdark
                                                               : v == ink ? UI_WHITE : UI_GREY,
                    255);
            any = 1;
        }
    }
    return any;
}

/* ------------------------------------------------------------------ */
/* Level-up stat panel                                                 */
/* ------------------------------------------------------------------ */

/* The two windows the game opens after a level up: the gains per stat, then
 * the new totals (BTLV_SCU_StepBallWindowOpen and ...Refresh, which the game's
 * names call the ball window).  Both read BtlvScu.ballStates (+0x170): a
 * level word and six stats in the order HP, Attack, Defense, Sp. Atk,
 * Sp. Def, Speed.  The step the scene unit runs (BtlvScu.mainProc, +0x0E4)
 * says which window is up. */
extern int ov093_BTLV_SCU_StepBallWindowOpen(int *state, void *scu) __attribute__((weak));
extern int ov093_BTLV_SCU_StepBallWindowRefresh(int *state, void *scu) __attribute__((weak));
extern int ov093_BTLV_SCU_StepBallWindowClose(int *state, void *scu) __attribute__((weak));

enum { LV_NONE, LV_GAINS, LV_WAIT_TOTALS, LV_TOTALS };

/* Where shell_draw put each own plate last: the level-up rows grow out of the
 * plate of the Pokemon that levelled. */
static struct { const u8 *poke; int x, y, w; } g_plate[3];

static int g_lv_phase;

static int lv_is(u32 proc, int (*fn)(int *, void *))
{
    return fn != NULL && proc == (u32)(uintptr_t)fn;
}

/* Follow the scene unit's step; returns the phase to draw. */
static int levelup_phase(const Bt *b)
{
    const u8 *scu = b->core != NULL ? rdp(b->core, 0x17C) : NULL;
    u32 proc;

    if (scu == NULL) {
        g_lv_phase = LV_NONE;
        return LV_NONE;
    }
    proc = rd32(scu, 0xE4);
    if (lv_is(proc, ov093_BTLV_SCU_StepBallWindowOpen)) {
        g_lv_phase = LV_GAINS;
    } else if (lv_is(proc, ov093_BTLV_SCU_StepBallWindowRefresh)) {
        /* the step reads the new stats in its first run */
        if (g_lv_phase != LV_TOTALS) {
            g_lv_phase = (int)rd32(scu, 0xEC) >= 1 ? LV_TOTALS : LV_WAIT_TOTALS;
        }
    } else if (lv_is(proc, ov093_BTLV_SCU_StepBallWindowClose)) {
        g_lv_phase = LV_NONE;
    } else if (g_lv_phase == LV_WAIT_TOTALS) {
        g_lv_phase = LV_TOTALS;
    }
    return g_lv_phase == LV_WAIT_TOTALS ? LV_GAINS : g_lv_phase;
}

static void levelup_draw(EidosUiCtx *c, const Bt *b, int phase)
{
    static const char *const names[6] = { "HP", "Attack", "Defense", "Sp. Atk", "Sp. Def", "Speed" };
    const u8 *scu = rdp(b->core, 0x17C);
    const u8 *poke = scu != NULL ? rdp(scu, 0x16C) : NULL;
    int px = c->canvas->w - 176, py = c->canvas->h - 107, pw = 170;
    int rh = 13, h = 6 * rh + 8, y, i, on_field = 0, head = 0;
    int ty = ui_text_y(rh, UI_FONT_SMALL);

    for (i = 0; i < 3; i++) {
        if (g_plate[i].poke != NULL && g_plate[i].poke == poke) {
            px = g_plate[i].x;
            py = g_plate[i].y;
            pw = g_plate[i].w;
            on_field = 1;
        }
    }
    if (on_field) {
        /* one piece with the plate: the box ends on the plate's top edge */
        y = py - h + 1;
        ui_panel(c, px, y, pw, h, UI_CUT_TL, 6, UI_BODY, UI_EDGE, 225);
    } else {
        /* a Pokemon with no plate (benched, or Exp. Share): its own box
         * at the top left, with the name, gender and level */
        MonView m;
        char lv[16];
        int nx;

        mon_view(gptr_ok((u8 *)poke) ? (u8 *)poke : NULL, &m);
        px = 8;
        pw = 150;
        head = 17;
        h += head;
        y = 26;
        ui_panel(c, px, y, pw, h, UI_CUT_TL | UI_CUT_BR, 6, UI_BODY, UI_EDGE, 225);
        nx = ui_text(c, px + 10, y + 3, m.name, UI_WHITE, UI_EDGE, UI_FONT_LARGE, 1);
        ui_gender(c, nx + 3, y + 3, m.gender, UI_FONT_LARGE);
        snprintf(lv, sizeof(lv), "Lv.%d", rd16(scu, 0x170) & 0xFF);
        ui_text(c, px + pw - 10 - ui_text_w(lv, UI_FONT_SMALL), y + 5, lv, UI_LIGHT, UI_EDGE,
                UI_FONT_SMALL, 1);
        ui_fill(c, px + 8, y + head - 1, pw - 16, 1, UI_LIME, 255);
    }
    for (i = 0; i < 6; i++) {
        char val[16];
        int v = rd16(scu, 0x172 + i * 2);
        int ry = y + 5 + head + i * rh;

        snprintf(val, sizeof(val), phase == LV_GAINS ? "+%d" : "%d", v);
        ui_text(c, px + 10, ry + ty, names[i], UI_GREY, UI_EDGE, UI_FONT_SMALL, 1);
        ui_text(c, px + pw - 10 - ui_text_w(val, UI_FONT_SMALL), ry + ty, val,
                phase == LV_GAINS ? UI_HP_G : UI_WHITE, UI_EDGE, UI_FONT_SMALL, 1);
        if (i > 0) {
            ui_fill(c, px + 8, ry - 1, pw - 16, 1, UI_EDGE, 160);
        }
    }
}

/* The message box between two messages: the game clears its window for a
 * few frames between them (and through a move's animation); the box stays
 * that long, empty, and only then fades out, so it does not blink. */
#define MSG_HOLD_FRAMES 24
#define MSG_FADE_FRAMES 8
static u32 g_msg_frame;                 /* the last frame with text          */

static void plain_run(EidosUiCtx *c, EidosUiView *view, const EidosUiFrame *in)
{
    static const char *const hints[] = { "A", "Next" };
    static const u32 keys[] = { BW_KEY_A, BW_KEY_B };
    int w = c->canvas->w, h = c->canvas->h;
    int full;
    Bt b;

    view->mode = EIDOS_VIEW_WORLD;
    (void)in;
    tap_reset();
    /* The game's own plates and message box are not shown: the interface
     * draws the plates from the battle's state and the message from the
     * game's own window, so the screen looks the same as in the menus. */
    if (bt_read(&b) && b.core != NULL) {
        Shell sh;
        int mw, lv;
        u32 now = bw_input_frame_number(), since;

        view->hide_2d = 1;
        shell_layout(c, &b, &sh);
        mw = sh.px - 6 - 6;
        shell_draw(c, &b, &sh, NULL, 0);
        if (draw_battle_message(c, &b, 6, h - 17 - 6 - 40, mw, 40)) {
            g_msg_frame = now;
        } else if ((since = now - g_msg_frame) < MSG_HOLD_FRAMES + MSG_FADE_FRAMES) {
            int a = since < MSG_HOLD_FRAMES ? 225
                    : 225 * (int)(MSG_HOLD_FRAMES + MSG_FADE_FRAMES - since) / MSG_FADE_FRAMES;

            ui_panel(c, 6, h - 17 - 6 - 40, mw, 40, UI_CUT_TL | UI_CUT_BR, 6, UI_BODY, UI_EDGE, a);
        }
        lv = levelup_phase(&b);
        if (lv != LV_NONE) {
            levelup_draw(c, &b, lv);
        }
    } else {
        /* a frame the battle's state is not readable (a screen changing):
         * the last picture stays, and the game's own plates stay hidden */
        view->hide_2d = 1;
        eidos_ui_hold_canvas();
        return;
    }
    /* a click anywhere that is not the hints advances the message */
    full = hit(c, 0, 0, w, c->canvas->h - 17);
    if (full & EIDOS_UI_CLICK) {
        eidos_ui_press(BW_KEY_A, 2);
    }
    draw_hint_strip(c, 0, w, hints, 1, keys);
}

static const EidosUiRecipe recipe_plain = { "battle_plain", plain_match, plain_run, NULL, 0 };

/*
 * The game's own gauge of a view position (BtlvEff.gauge +0x1A4, BtlvGaugeWork
 * slots +0x40, 0x80 bytes each, indexed by the game's view position, which is this file's vp minus 2).  The gauge drains
 * and fills over many frames: the value on show is the animation's
 * accumulator (BTLV_GAUGE_StepValue), not the battle's final HP and EXP.  A
 * slot is on show once the game has put a Pokemon on it and shown it, so the
 * plates appear as each Pokemon is sent out.
 */
typedef struct GaugeView {
    int hp, maxhp, level;
    int exp, expmax;
} GaugeView;

#define GAUGE_SLOT_FLAGS 0x70
#define GF_HP_ANIM   (1u << 0)
#define GF_EXP_ANIM  (1u << 1)
#define GF_OCCUPIED  (1u << 3)
#define GF_VISIBLE   (1u << 29)
#define GF_HIDDEN    (1u << 30)

/* One animated value: acc is the accumulator, fixed point (<< 8) for a small
 * maximum.  down: the value falls, so a part of a step still counts. */
static int gauge_shown(int acc, int maxv, int tiles, int down, int fallback)
{
    if (acc == (int)0x80000000) {
        return fallback;
    }
    if (maxv < tiles * 8) {
        int v = acc >> 8;

        if (down && (acc & 0xFF) > 0) {
            v++;
        }
        return v;
    }
    return acc;
}

/* Returns 1 and fills `g` when the gauge shows view position vp; 0 when the
 * slot is empty or hidden.  Returns -1 when there is no gauge to read. */
static int gauge_read(const Bt *b, int vp, GaugeView *g)
{
    const u8 *work = rdp(b->eff, 0x1A4);
    const u8 *slot;
    u32 fl;
    int hp, maxhp, hpd, ex, exmax, exd;

    if (!gptr_ok(work) || vp < 2 || vp > 7) {
        return -1;
    }
    slot = work + 0x40 + (u32)(vp - 2) * 0x80u;
    fl = rd32(slot, GAUGE_SLOT_FLAGS);
    if ((fl & GF_OCCUPIED) == 0 || (fl & GF_VISIBLE) == 0 || (fl & GF_HIDDEN) != 0) {
        return 0;
    }
    hp = (int)rd32(slot, 0x48);
    maxhp = (int)rd32(slot, 0x4C);
    hpd = (int)rd32(slot, 0x68);
    ex = (int)rd32(slot, 0x54);
    exmax = (int)rd32(slot, 0x58);
    exd = (int)rd32(slot, 0x60);
    if ((fl & GF_HP_ANIM) != 0) {
        hp = gauge_shown((int)rd32(slot, 0x50), maxhp, 6, hpd > 0, hp);
    }
    if ((fl & GF_EXP_ANIM) != 0) {
        ex = gauge_shown((int)rd32(slot, 0x64), exmax, 10, exd > 0, ex);
    }
    g->hp = clampi(hp, 0, maxhp > 0 ? maxhp : 0);
    g->maxhp = maxhp;
    g->level = slot[0x6C];
    g->exp = clampi(ex, 0, exmax > 0 ? exmax : 0);
    g->expmax = exmax;
    return 1;
}

/* The experience bar of an own Pokemon: the part of this level done. */
static void own_exp(u8 *poke, int *num, int *den)
{
    u8 *mon = rdp(poke, 0);
    int level = poke[0x18];
    u32 lower, upper, exp = rd32(poke, 0x08);

    *num = 0;
    *den = 1;
    if (!gptr_ok(mon) || level >= 100 || POKE_TOOL_GetLevelMinExp == NULL ||
        POKE_TOOL_GetExpForLevel == NULL) {
        return;
    }
    lower = POKE_TOOL_GetLevelMinExp((struct PokemonParam *)mon);
    upper = POKE_TOOL_GetExpForLevel((u16)rd16(poke, 0x0C), (u16)poke[0x141], (u16)(level + 1));
    if (upper > lower && exp >= lower) {
        *num = (int)(exp - lower);
        *den = (int)(upper - lower);
    }
}

/*
 * The scene's own plates and prompt.  The own Pokemon's plate keeps the
 * bottom-right corner in every battle scene, the chooser's lowest; the
 * action and move sheets stand on it while the game asks, and the message
 * runs along the bottom to its left.  The menu scenes hide the game's 2D
 * layer and draw the plates again from the battle's state (the HP does not
 * move while a menu is open): the foes' plates top left.
 */
#define PLATE_H   32
#define PLATE_GAP 3

static void shell_layout(EidosUiCtx *c, const Bt *b, Shell *s)
{
    int w = c->canvas->w, h = c->canvas->h;
    int vp, i, n = 0, room;
    u8 *chooser = rdp(b->core, 0xBC);

    s->pw = clampi(w * 35 / 100, 150, 166);
    s->px = w - s->pw - 6;
    for (vp = 2; vp <= 6; vp += 2) {
        u8 *poke = poke_at_view(b, vp);
        MonView m;

        if (poke == NULL) {
            continue;
        }
        /* a shown gauge; or the Pokemon choosing a command, whose gauge the
         * game slides in a few frames after the menu: its plate is there
         * from the start, so the sheets above it do not jump */
        if (gauge_read(b, vp, &(GaugeView){ 0 }) != 0) {
            s->own_vp[n++] = vp;
        } else if (poke == chooser) {
            mon_view(poke, &m);
            if (m.hp > 0) {
                s->own_vp[n++] = vp;
            }
        }
    }
    /* the chooser last: it ends nearest the corner */
    for (i = 0; i < n; i++) {
        if (poke_at_view(b, s->own_vp[i]) == chooser && i != n - 1) {
            int t = s->own_vp[i];

            s->own_vp[i] = s->own_vp[n - 1];
            s->own_vp[n - 1] = t;
        }
    }
    room = (h - 17 - 6 - 120) / (PLATE_H + PLATE_GAP);
    if (room < 1) {
        room = 1;
    }
    if (n > room) {
        memmove(s->own_vp, s->own_vp + (n - room), (size_t)room * sizeof(int));
        n = room;
    }
    s->n = n;
    s->top = h - 17 - 6 - n * (PLATE_H + PLATE_GAP) + PLATE_GAP - 4;
}

static void shell_draw(EidosUiCtx *c, const Bt *b, const Shell *s, const char *prompt,
                       int own)
{
    int w = c->canvas->w, h = c->canvas->h;
    int fw = clampi(w * 34 / 100, 120, 170);
    int vp, y, i;

    (void)w;
    y = 6;
    for (vp = 3; vp <= 7; vp += 2) {
        u8 *poke = poke_at_view(b, vp);
        MonView m;
        GaugeView gv;
        int gs;

        if (poke == NULL) {
            continue;
        }
        mon_view(poke, &m);
        gs = gauge_read(b, vp, &gv);
        if (gs == 0) {
            continue;
        }
        if (gs > 0) {
            m.hp = gv.hp;
            m.maxhp = gv.maxhp;
            m.level = gv.level;
        }
        ui_plate(c, 6, y, fw, m.name, m.gender, m.level, m.hp, m.maxhp, 0, 0);
        y += 27;
    }
    memset(g_plate, 0, sizeof(g_plate));
    y = s->top + 4;
    for (i = 0; i < s->n; i++) {
        u8 *poke = poke_at_view(b, s->own_vp[i]);
        MonView m;
        int num, den;
        GaugeView gv;

        mon_view(poke, &m);
        own_exp(poke, &num, &den);
        if (gauge_read(b, s->own_vp[i], &gv) > 0) {
            m.hp = gv.hp;
            m.maxhp = gv.maxhp;
            m.level = gv.level;
            num = gv.exp;
            den = gv.expmax > 0 ? gv.expmax : 1;
            if (gv.expmax <= 0) {
                num = 0;
            }
        }
        ui_plate(c, s->px, y, s->pw, m.name, m.gender, m.level, m.hp, m.maxhp, num, den);
        if (i < 3) {
            g_plate[i].poke = poke;
            g_plate[i].x = s->px;
            g_plate[i].y = y;
            g_plate[i].w = s->pw;
        }
        y += PLATE_H + PLATE_GAP;
    }
    /* The prompt stands in the box of the game's messages (plain_run), and
     * while the game's own window holds text that text is shown: a crossfade
     * from a message to a menu then blends two equal pictures, not the
     * message and the prompt a line apart.  `own`: the interface's line even
     * then (a prompt the game does not print). */
    if (prompt != NULL) {
        int mw = s->px - 6 - 6, my = h - 17 - 6 - 40;

        if (own || !draw_battle_message(c, b, 6, my, mw, 40)) {
            ui_panel(c, 6, my, mw, 40, UI_CUT_TL | UI_CUT_BR, 6, UI_BODY, UI_EDGE, 225);
            ui_text(c, 16, my + ui_text_y(40, UI_FONT_LARGE), prompt, UI_WHITE, UI_GREY,
                    UI_FONT_LARGE, 1);
        }
    }
}

/* ------------------------------------------------------------------ */
/* The sheets' tiles                                                   */
/* ------------------------------------------------------------------ */

/* colour over the dark body at k/16 */
static u32 tint16(u32 col, int k)
{
    u32 r = (((col >> 16) & 255u) * (u32)k + ((UI_BODY >> 16) & 255u) * (u32)(16 - k)) / 16u;
    u32 g = (((col >> 8) & 255u) * (u32)k + ((UI_BODY >> 8) & 255u) * (u32)(16 - k)) / 16u;
    u32 bl = ((col & 255u) * (u32)k + (UI_BODY & 255u) * (u32)(16 - k)) / 16u;

    return (r << 16) | (g << 8) | bl;
}

static u32 dark3(u32 col)
{
    return ((((col >> 16) & 255u) / 3u) << 16) | ((((col >> 8) & 255u) / 3u) << 8) | ((col & 255u) / 3u);
}

/* A tile of a sheet: dark with a strip of its colour on the left; the
 * chosen one tinted with that colour and edged in it (raised a pixel by
 * the caller); a disabled one greyed. */
static void sheet_tile(EidosUiCtx *c, int x, int y, int w, int h, u32 col, int sel, int enabled)
{
    if (!enabled) {
        col = UI_EDGE2;
        sel = 0;
    }
    ui_panel(c, x, y, w, h, UI_CUT_TL | UI_CUT_BR, 5, sel ? tint16(col, 5) : UI_BODY,
             sel ? col : UI_EDGE, 240);
    {
        /* the strip, following the cut top-left corner */
        int r;

        for (r = 3; r < h - 3; r++) {
            int left = 3 + (r < 7 ? 7 - r : 0);

            if (left < 6) {
                ui_fill(c, x + left, y + r, 6 - left, 1, col, 255);
            }
        }
    }
}

static void action_tile(EidosUiCtx *c, int x, int y, int w, int h, const char *label, u32 col,
                        int sel, int enabled)
{
    int tw = ui_text_w(label, UI_FONT_MEDIUM);   /* the small font has no E acute */

    sheet_tile(c, x, y, w, h, col, sel, enabled);
    ui_text(c, x + (w - tw) / 2, y + ui_text_y(h, UI_FONT_MEDIUM), label,
            !enabled ? UI_EDGE2 : sel ? UI_WHITE : col, (s32)dark3(enabled ? col : UI_EDGE2),
            UI_FONT_MEDIUM, 1);
}

/* A move: the name (the medium font when the large one does not fit),
 * then its type in the type's colour and the PP. */
static void move_tile(EidosUiCtx *c, int x, int y, int w, int h, int type, const char *name,
                      int pp, int maxpp, int sel, int enabled)
{
    u32 col = ui_type_color(type);
    char s2[24];
    int font = ui_text_w(name, UI_FONT_LARGE) <= w - 9 - 5 ? UI_FONT_LARGE : UI_FONT_MEDIUM;

    sheet_tile(c, x, y, w, h, col, sel, enabled);
    ui_text(c, x + 9, y + (font == UI_FONT_LARGE ? 1 : 2), name, enabled ? UI_WHITE : UI_EDGE2,
            UI_GREY, font, 1);
    ui_text(c, x + 9, y + h - 11, ui_type_name(type), enabled ? col : UI_EDGE2,
            (s32)dark3(col), UI_FONT_SMALL, 1);
    snprintf(s2, sizeof(s2), "%d/%d", pp, maxpp);
    ui_text(c, x + w - 8 - ui_text_w(s2, UI_FONT_SMALL), y + h - 11, s2,
            !enabled ? UI_EDGE2 : sel ? UI_WHITE : UI_LIGHT, UI_EDGE, UI_FONT_SMALL, 1);
}

/* The arrows over a 2 x 2 sheet (entry = row * 2 + column): left and
 * right change the column, up and down the row; a disabled entry is not
 * a stop.  Returns the new entry, or -1. */
static int grid_step(const int *enabled, int cur)
{
    u32 k = eidos_ui_keys_pressed();
    int to;

    eidos_ui_take_keys(BW_KEY_UP | BW_KEY_DOWN | BW_KEY_LEFT | BW_KEY_RIGHT);
    if (cur < 0 || cur > 3) {
        cur = 0;
    }
    if (k & (BW_KEY_LEFT | BW_KEY_RIGHT)) {
        to = cur ^ 1;
    } else if (k & (BW_KEY_UP | BW_KEY_DOWN)) {
        to = cur ^ 2;
    } else {
        return -1;
    }
    return enabled[to] ? to : -1;
}

/* "What will X do?" for the Pokemon that is choosing. */
static const char *prompt_choosing(const Bt *b, char *buf, int cap)
{
    u8 *poke = rdp(b->core, 0xBC);
    MonView m;

    mon_view(gptr_ok(poke) ? poke : NULL, &m);
    if (!msg_line_sub(15, 69, m.name, buf, cap)) {
        snprintf(buf, (size_t)cap, "What will %s do?", m.name);
    }
    return buf;
}

/* The signature of a battle scene for tap_service. */
static u32 bt_sig(const Bt *b, u32 kind)
{
    u32 s = (kind << 24) | ((u32)b->screen << 16);

    if (b->in != NULL) {
        s ^= rd32(b->in, 0x68) & 0xFFFFu;
    }
    return s;
}

/* ------------------------------------------------------------------ */
/* Action menu                                                         */
/* ------------------------------------------------------------------ */

static int action_match(void)
{
    Bt b;

    return bt_read(&b) && b.in != NULL && b.screen == 1;
}

/* The entry clicked last and when: the game puts its cursor back on the
 * first entry when it takes a touch (BTLV_INPUT_ProcessButtonSelection), so
 * for a moment after a click the clicked entry is the one shown lit. */
static int g_click_cur = -1;
static int g_click_screen;
static u32 g_click_frame;

/* The game resets its own cursor to the first entry when a choice is taken
 * (BTLV_INPUT_ProcessButtonSelection), whether by a touch or by A, and keeps
 * it there while the screen changes.  The entry chosen stays lit until the
 * screen is another one. */
static void note_click(const Bt *b, int entry)
{
    g_click_cur = entry;
    g_click_screen = b->screen;
    g_click_frame = bw_input_frame_number();
}

/* The entry the game's cursor rests on when it opens screen 1, 2, 3 or 5
 * (BTLV_INPUT_UpdateWazaRow): the last choice made on that screen
 * (BtlvInput.lastChoice, +0x242, per party slot), or -1 for other screens. */
static int rest_cursor(const Bt *b)
{
    const u8 *lc = b->in + 0x242 + (rd32(b->in, 0x27C) % 3u) * 8;

    switch (b->screen) {
    case 1:
    case 2:
    case 5:
        return lc[b->screen] & 15;
    case 3:
        return ((lc[3] >> 4) == lc[2] ? lc[3] : 0) & 15;
    }
    return -1;
}

/* The game's cursor, steadied.  When a choice is taken or a screen is left
 * (BTLV_INPUT_ProcessKeyInput) the game zeroes its cursor and sets the redraw
 * bit, and only puts the cursor back on the last choice when the next screen
 * is up.  Until then the entry shown is the one the game will rest on: the
 * cursor as it was before, or the last choice of the screen just opened. */
static int game_cursor(const Bt *b)
{
    static int stable_screen = -1, stable_cur, last_screen;
    static u32 last_call;
    u32 f = rd32(b->in, 0x68), now = bw_input_frame_number();
    int cur = (int)((f >> 5) & 15u), rest;

    if (now - last_call > 3 || b->screen != last_screen) {
        stable_screen = -1;     /* another screen, or a bag or party screen was up in between */
    }
    last_call = now;
    last_screen = b->screen;
    rest = rest_cursor(b);
    if (rest < 0) {
        stable_screen = -1;
        return cur;
    }
    /* busy, animation tasks, redraw pending, input locked */
    if ((f & (0xFu | (1u << 13) | (1u << 31))) == 0) {
        stable_screen = b->screen;
        stable_cur = cur;
        return cur;
    }
    return stable_screen == b->screen ? stable_cur : rest;
}

static int input_cursor(const Bt *b)
{
    if (g_click_cur >= 0 && g_click_screen == b->screen &&
        bw_input_frame_number() - g_click_frame < 90) {
        return g_click_cur;
    }
    g_click_cur = -1;
    return game_cursor(b);
}

/* A taken with the keys: the entry under the game's cursor is the choice. */
static void note_key_choice(const Bt *b, int cur)
{
    if (g_click_cur < 0 && cur >= 0 && cur < 4 && (eidos_ui_keys_pressed() & BW_KEY_A) != 0) {
        note_click(b, cur);
    }
}

/* The game's key cursor is hidden after a touch, and the first key then
 * only shows it again (BtlvInput.cursorVisible, +0x274): with the mouse and
 * the keys mixed freely, the cursor is kept shown, so a key acts at once. */
static void keys_live(const Bt *b)
{
    u8 *vis = rdp(b->in, 0x274);

    if (vis != NULL && *vis == 0 && (eidos_ui_keys_pressed() & (BW_KEY_A | BW_KEY_B | BW_KEY_UP |
                                                               BW_KEY_DOWN | BW_KEY_LEFT |
                                                               BW_KEY_RIGHT)) != 0) {
        *vis = 1;
    }
}

/* Move the game's cursor to entry `to` (the same bits input_cursor reads):
 * the interface lays the entries out as a column, the game as a square, so
 * up and down are the interface's and the game's cursor follows them. */

static void set_input_cursor(const Bt *b, int to)
{
    u32 v = rd32(b->in, 0x68);

    eidos_ui_sound(0x552);     /* BTLV_INPUT_PlayCursorSe */

    v = (v & ~(15u << 5)) | ((u32)(to & 15) << 5);
    memcpy(b->in + 0x68, &v, 4);
    {
        u8 *vis = rdp(b->in, 0x274);

        if (vis != NULL) {
            *vis = 1;
        }
    }
    g_click_cur = -1;
}

/* Up and down along a column of entries in display order; disabled ones are
 * skipped.  Returns the new entry, or -1 when the keys did not move. */
static int column_step(const int *order, int n, const int *enabled, int cur)
{
    u32 k = eidos_ui_keys_pressed();
    int pos = 0, i, step;

    eidos_ui_take_keys(BW_KEY_UP | BW_KEY_DOWN | BW_KEY_LEFT | BW_KEY_RIGHT);
    if ((k & (BW_KEY_UP | BW_KEY_DOWN)) == 0) {
        return -1;
    }
    for (i = 0; i < n; i++) {
        if (order[i] == cur) {
            pos = i;
        }
    }
    step = (k & BW_KEY_UP) ? -1 : 1;
    for (i = 1; i < n; i++) {
        int p2 = pos + step * i;

        if (p2 < 0 || p2 >= n) {
            break;
        }
        if (enabled[order[p2]]) {
            return order[p2];
        }
    }
    return -1;
}

/* ------------------------------------------------------------------ */
/* Corners on the Pokemon themselves (double and triple battles)       */
/* ------------------------------------------------------------------ */

/* sdk/battle_view.c: where a Pokemon was drawn (linked into the game only) */
extern int bw_battle_view_mon_box(int pos, float box[4]) __attribute__((weak));

/* The canvas box of the Pokemon at view position vp, or 0. */
static int mon_rect(EidosUiCtx *c, int vp, EidosUiRect *r)
{
    float box[4];
    int cw = c->canvas->w, ch = c->canvas->h;

    if (bw_battle_view_mon_box == NULL || !bw_battle_view_mon_box(vp, box)) {
        return 0;
    }
    r->x = (int)(box[0] * (float)cw) - 2;
    r->y = (int)(box[1] * (float)ch) - 2;
    r->w = (int)(box[2] * (float)cw) + 2 - r->x;
    r->h = (int)(box[3] * (float)ch) + 2 - r->y;
    return r->w > 4 && r->h > 4;
}

/* The view position of the Pokemon choosing now, or -1. */
static int chooser_vp(const Bt *b)
{
    u8 *poke = rdp(b->core, 0xBC);
    int vp;

    for (vp = 2; gptr_ok(poke) && vp <= 7; vp += 2) {
        if (poke_at_view(b, vp) == poke) {
            return vp;
        }
    }
    return -1;
}

/* Several Pokemon on a side: the one choosing has corners (drawn first, so
 * the interface stays over them). */
static void chooser_corners(EidosUiCtx *c, const Bt *b)
{
    EidosUiRect r;
    int vp = b->rule == 1 || b->rule == 2 ? chooser_vp(b) : -1;

    if (vp >= 0 && mon_rect(c, vp, &r)) {
        ui_cursor_corners(c, r.x, r.y, r.w, r.h, UI_CYAN);
    }
}

/* The arrows among boxes on screen: the nearest enabled one in the arrow's
 * direction (centres compared), first among those mostly that way (within
 * about 63 degrees of it), else in the whole half the arrow points to; -1
 * when there is none. */
static int spatial_step(const EidosUiRect *r, const int *en, int n, int cur, u32 k)
{
    int i, best = -1, pass;
    long bestd = 0;
    int cx, cy;

    k &= BW_KEY_UP | BW_KEY_DOWN | BW_KEY_LEFT | BW_KEY_RIGHT;
    if (cur < 0 || cur >= n || k == 0) {
        return -1;
    }
    cx = r[cur].x + r[cur].w / 2;
    cy = r[cur].y + r[cur].h / 2;
    for (pass = 0; pass < 2 && best < 0; pass++) {
        for (i = 0; i < n; i++) {
            int dx = r[i].x + r[i].w / 2 - cx, dy = r[i].y + r[i].h / 2 - cy;
            int horiz = (k & (BW_KEY_LEFT | BW_KEY_RIGHT)) != 0;
            int along = horiz ? abs(dx) : abs(dy), across = horiz ? abs(dy) : abs(dx);
            long d;

            if (i == cur || !en[i]) {
                continue;
            }
            if (((k & BW_KEY_LEFT) && dx >= 0) || ((k & BW_KEY_RIGHT) && dx <= 0) ||
                ((k & BW_KEY_UP) && dy >= 0) || ((k & BW_KEY_DOWN) && dy <= 0)) {
                continue;
            }
            if (pass == 0 && 2 * along < across) {
                continue;
            }
            /* along the arrow counts once, across it twice */
            d = (long)along + 2L * (long)across;
            if (best < 0 || d < bestd) {
                best = i;
                bestd = d;
            }
        }
    }
    return best;
}

static void action_run(EidosUiCtx *c, EidosUiView *view, const EidosUiFrame *in)
{
    static const char *const hints[] = { "A", "Select", "B", "Back" };
    static const u32 keys[] = { BW_KEY_A, BW_KEY_B };
    /* result order of the game: 0 FIGHT, 1 BAG, 2 POKEMON, 3 RUN; on the
     * sheet the same order, two by two */
    static const int tap_x[4] = { 127, 40, 215, 128 };
    static const int tap_y[4] = { 84, 167, 167, 171 };
    Bt b;
    Shell sh;
    int i, hovered = -1, cur, can_back, tw, th = 20, g = 3, x0, y0;
    const char *names[4];
    u32 colors[4] = { UI_FIGHT, UI_BAGY, UI_MON, UI_RUN };
    char run_label[16];
    char prompt_buf[96];
    int en[4];

    if (!bt_read(&b) || b.in == NULL) {
        return;
    }
    view->mode = EIDOS_VIEW_WORLD;
    view->hide_2d = 1;
    shell_layout(c, &b, &sh);
    chooser_corners(c, &b);
    keys_live(&b);
    cur = input_cursor(&b);
    note_key_choice(&b, cur);
    can_back = b.aux != NULL ? b.aux[0x8B] : 0;
    names[0] = "FIGHT";
    names[1] = "BAG";
    names[2] = "POK\xC3\x89MON";
    snprintf(run_label, sizeof(run_label), "%s", can_back ? "BACK" : "RUN");
    names[3] = run_label;
    for (i = 0; i < 4; i++) {
        en[i] = b.in[0x25A + i] != 0;
    }

    tw = (sh.pw - g) / 2;
    x0 = sh.px;
    y0 = sh.top - 2 * th - g;
    /* hit regions first so the hover is known when drawing */
    for (i = 0; i < 4; i++) {
        int st = hit(c, x0 + (i % 2) * (tw + g), y0 + (i / 2) * (th + g), tw, th);

        if (st & EIDOS_UI_HOVER) {
            hovered = i;
        }
        if ((st & EIDOS_UI_CLICK) && en[i]) {
            tap_request(tap_x[i], tap_y[i], bt_sig(&b, 1), in->frame);
            note_click(&b, i);
            cur = i;
        }
    }
    {
        int to = grid_step(en, cur);

        if (to >= 0) {
            set_input_cursor(&b, to);
            cur = to;
        }
    }
    for (i = 0; i < 4; i++) {
        int sel = g_click_cur >= 0 ? cur == i : hovered >= 0 ? hovered == i : cur == i;
        int x = x0 + (i % 2) * (tw + g), y = y0 + (i / 2) * (th + g);

        action_tile(c, x, y - (sel && en[i] ? 1 : 0), tw, th, names[i], colors[i],
                    sel && en[i], en[i]);
    }
    tap_service(bt_sig(&b, 1), in->frame);
    if (eidos_ui_right_click() && can_back) {
        eidos_ui_press(BW_KEY_B, 2);
    }
    prompt_choosing(&b, prompt_buf, sizeof(prompt_buf));
    shell_draw(c, &b, &sh, prompt_buf, 0);
    draw_hint_strip(c, 0, c->canvas->w, hints, can_back ? 2 : 1, keys);
}

static const EidosUiRecipe recipe_action = { "battle_action", action_match, action_run, NULL, 0 };


/* ------------------------------------------------------------------ */
/* Move menu                                                           */
/* ------------------------------------------------------------------ */

typedef struct MoveV {
    int id, pp, maxpp, type, category, power, accuracy;
} MoveV;

/* The i-th move of a battle Pokemon as the menu shows it: the battle's own
 * copy when a Transform or Mimic replaced the move (BattleMoveSlot.saved). */
/* The game's type numbers (Black and White drop the ??? type, so FIRE is 9)
 * as the kit numbers them (the older order, ??? at 9). */
static int kit_type(int t)
{
    return t;      /* the kit takes the game's own type numbers */
}

/* A move's name as the one-screen layout shows it: the spelling later
 * games use where Black and White squeezed it into 12 letters, else the
 * game's own text. */
static int move_name(int id, char *out, int cap)
{
    const char *modern = ui_move_name_modern(id);

    if (modern != NULL) {
        snprintf(out, (size_t)cap, "%s", modern);
        return 1;
    }
    if (!msg_line(MSG_MOVE, (u32)id, out, cap)) {
        snprintf(out, (size_t)cap, "MOVE %d", id);
        return 0;
    }
    return 1;
}

static void move_read(const u8 *poke, int i, MoveV *m)
{
    const u8 *slot = poke + 0x104 + i * 14;
    const u8 *use = slot[12] != 0 ? slot + 6 : slot;

    memset(m, 0, sizeof(*m));
    m->id = (int)rd16(use, 0);
    m->pp = use[2];
    m->maxpp = use[3];
    if (m->id != 0 && WAZA_TOOL_GetParam != NULL) {
        m->type = kit_type((int)WAZA_TOOL_GetParam((u32)m->id, 0));
        m->category = (int)WAZA_TOOL_GetParam((u32)m->id, 2);
        m->power = (int)WAZA_TOOL_GetParam((u32)m->id, 3);
        m->accuracy = (int)WAZA_TOOL_GetParam((u32)m->id, 4);
    }
}

static int move_match(void)
{
    Bt b;

    return bt_read(&b) && b.in != NULL && b.screen == 2;
}

static void move_run(EidosUiCtx *c, EidosUiView *view, const EidosUiFrame *in)
{
    static const char *const hints[] = { "A", "Use", "B", "Back" };
    static const u32 keys[] = { BW_KEY_A, BW_KEY_B };
    static const int tap_x[5] = { 64, 191, 64, 191, 215 };
    static const int tap_y[5] = { 56, 56, 104, 104, 167 };
    Bt b;
    Shell sh;
    int i, hovered = -1, cur, struggle, tw, th = 26, g = 3, x0, y0, info_y;
    u8 *poke;
    MoveV mv[4];
    char prompt_buf[96];

    if (!bt_read(&b) || b.in == NULL) {
        return;
    }
    view->hide_2d = 1;
    view->mode = EIDOS_VIEW_WORLD;
    shell_layout(c, &b, &sh);
    poke = rdp(b.core, 0xBC);
    if (!gptr_ok(poke)) {
        return;
    }
    chooser_corners(c, &b);
    keys_live(&b);
    cur = input_cursor(&b);
    note_key_choice(&b, cur);
    struggle = rd32(b.in, 0x220) == 1;
    for (i = 0; i < 4; i++) {
        move_read(poke, i, &mv[i]);
    }
    tw = (sh.pw - g) / 2;
    x0 = sh.px;
    y0 = sh.top - 2 * th - g;
    info_y = y0 - 13;
    if (struggle) {
        int st = hit(c, x0, sh.top - th, sh.pw, th);

        if (st & EIDOS_UI_CLICK) {
            tap_request(128, 80, bt_sig(&b, 2), in->frame);
        }
        move_tile(c, x0, sh.top - th - 1, sh.pw, th, 0, "Struggle", 1, 1, 1, 1);
    } else {
        int en[4];

        for (i = 0; i < 4; i++) {
            int usable = mv[i].id != 0 && mv[i].pp != 0 && b.in[0x25A + i] != 0;
            int st = hit(c, x0 + (i % 2) * (tw + g), y0 + (i / 2) * (th + g), tw, th);

            en[i] = mv[i].id != 0 && b.in[0x25A + i] != 0;
            if (st & EIDOS_UI_HOVER) {
                hovered = i;
            }
            if ((st & EIDOS_UI_CLICK) && usable) {
                tap_request(tap_x[i], tap_y[i], bt_sig(&b, 2), in->frame);
                note_click(&b, i);
                cur = i;
            }
        }
        {
            int to = grid_step(en, cur);

            if (to >= 0) {
                set_input_cursor(&b, to);
                cur = to;
            }
        }
        for (i = 0; i < 4; i++) {
            char name[40];
            int sel = g_click_cur < 0 && hovered >= 0 ? hovered == i : cur == i;
            int x = x0 + (i % 2) * (tw + g), y = y0 + (i / 2) * (th + g);

            if (mv[i].id == 0) {
                ui_panel(c, x, y, tw, th, UI_CUT_TL | UI_CUT_BR, 5, UI_BODY2, UI_EDGE, 160);
                continue;
            }
            move_name(mv[i].id, name, sizeof(name));
            move_tile(c, x, y - (sel ? 1 : 0), tw, th, mv[i].type, name, mv[i].pp,
                      mv[i].maxpp, sel, mv[i].pp != 0 && en[i]);
        }
        {
            int shown = hovered >= 0 ? hovered : (cur >= 0 && cur < 4 ? cur : 0);

            if (mv[shown].id != 0) {
                char info[48];

                ui_category_icon(c, x0 + 2, info_y,
                                 mv[shown].category == 2 ? 1 : mv[shown].category == 1 ? 0 : 2);
                snprintf(info, sizeof(info), "PWR %d  ACC %d", mv[shown].power, mv[shown].accuracy);
                if (mv[shown].power == 0) {
                    snprintf(info, sizeof(info), "ACC %d", mv[shown].accuracy);
                }
                ui_text(c, x0 + 22, info_y + ui_text_y(11, UI_FONT_SMALL), info, UI_LIGHT, UI_EDGE,
                        UI_FONT_SMALL, 1);
            }
        }
    }
    tap_service(bt_sig(&b, 2), in->frame);
    if (eidos_ui_right_click()) {
        eidos_ui_press(BW_KEY_B, 2);
    }
    prompt_choosing(&b, prompt_buf, sizeof(prompt_buf));
    shell_draw(c, &b, &sh, prompt_buf, 0);
    draw_hint_strip(c, 0, c->canvas->w, hints, 2, keys);
}

static const EidosUiRecipe recipe_move = { "battle_move", move_match, move_run, NULL, 0 };

/* ------------------------------------------------------------------ */
/* Target menu (double and triple battles)                             */
/* ------------------------------------------------------------------ */

static int target_match(void)
{
    Bt b;

    return bt_read(&b) && b.in != NULL && b.screen == 3;
}

static void target_run(EidosUiCtx *c, EidosUiView *view, const EidosUiFrame *in)
{
    static const char *const hints[] = { "A", "Choose", "B", "Back" };
    static const u32 keys[] = { BW_KEY_A, BW_KEY_B };
    /* the centre of each region, double battles (region = view position - 2) */
    static const int d_x[4] = { 64, 191, 191, 64 };
    static const int d_y[4] = { 108, 60, 108, 60 };
    /* triple battles */
    static const int t_x[6] = { 48, 208, 128, 128, 208, 48 };
    static const int t_y[6] = { 108, 60, 108, 60, 108, 60 };
    Bt b;
    Shell sh;
    int cw, x, y, i, rows = 0, back_y, hovered = -1, cur, corner, group;
    int vp_list[6], legal[6];
    char prompt_buf[96];
    const u8 *targets;
    int count;

    if (!bt_read(&b) || b.in == NULL || b.aux == NULL) {
        return;
    }
    view->hide_2d = 1;
    view->mode = EIDOS_VIEW_WORLD;
    shell_layout(c, &b, &sh);
    cw = sh.pw;
    x = sh.px;
    keys_live(&b);
    cur = input_cursor(&b);
    targets = b.aux + 0x80;
    count = targets[0];
    /* foes first, then the own side */
    {
        int vp;

        for (vp = 3; vp <= 7; vp += 2) {
            if (poke_at_view(&b, vp) != NULL) {
                vp_list[rows++] = vp;
            }
        }
        for (vp = 2; vp <= 6; vp += 2) {
            if (poke_at_view(&b, vp) != NULL) {
                vp_list[rows++] = vp;
            }
        }
    }
    /* BtlvTargetSet: count, altCount, altKind, slots[6]; a move that hits
     * several at once (Earthquake, Surf) lists them as the alternate set,
     * one choice for all of them */
    group = count == 0 && targets[1] != 0;
    for (i = 0; i < rows; i++) {
        int slot = view_slot(&b, vp_list[i]), j, n = group ? targets[1] : count;

        legal[i] = 0;
        for (j = 0; j < n && j < 6; j++) {
            if (targets[3 + j] == slot) {
                legal[i] = 1;
            }
        }
    }
    /* Every legal target drawn this frame: corners on the Pokemon
     * themselves stand for the list, the arrows go by where they stand and
     * a click on one picks it.  Drawn first, under the interface. */
    {
        EidosUiRect mr[6];
        int first = -1;

        corner = rows > 0;
        for (i = 0; i < rows; i++) {
            if (!mon_rect(c, vp_list[i], &mr[i])) {
                memset(&mr[i], 0, sizeof mr[i]);
                corner = legal[i] ? 0 : corner;
            }
            if (legal[i] && first < 0) {
                first = i;
            }
        }
        corner = corner && first >= 0;
        if (corner) {
            /* the target lit is the interface's own while the screen is up;
             * it starts on the game's choice, or on the first foe */
            static int sel_vp = -1;
            static u32 seen;
            int ci = -1, hov = -1, to, pick = -1;
            u32 k = eidos_ui_keys_pressed();

            if (in->frame - seen > 30u) {
                /* a new choice: the game's pick, a foe first */
                sel_vp = cur + 2;
                for (i = 0; (sel_vp & 1) == 0 && i < rows; i++) {
                    if (legal[i] && (vp_list[i] & 1) != 0) {
                        sel_vp = -1;
                    }
                }
            }
            seen = in->frame;
            for (i = 0; i < rows; i++) {
                if (legal[i] && vp_list[i] == sel_vp) {
                    ci = i;
                }
            }
            if (ci < 0) {
                ci = first;
                sel_vp = vp_list[ci];
            }
            /* the arrows move among the targets as they stand; A takes the
             * one lit, as a touch on it, so the game's own cursor is not
             * used */
            to = spatial_step(mr, legal, rows, ci, k);
            eidos_ui_take_keys(BW_KEY_UP | BW_KEY_DOWN | BW_KEY_LEFT | BW_KEY_RIGHT | BW_KEY_A);
            if (to >= 0) {
                eidos_ui_sound(0x552);     /* BTLV_INPUT_PlayCursorSe */
                sel_vp = vp_list[to];
                ci = to;
            }
            if (k & BW_KEY_A) {
                pick = ci;
            }
            /* the mouse: of the targets under it, the one whose centre is
             * nearest; hovering one selects it */
            {
                int mx = -1, my = -1, clicked = 0;
                long best = 0;

                (void)eidos_ui_mouse_pos(&mx, &my);
                for (i = 0; i < rows; i++) {
                    int st;
                    long dx, dy;

                    if (!legal[i]) {
                        continue;
                    }
                    st = hit(c, mr[i].x, mr[i].y, mr[i].w, mr[i].h);
                    clicked |= (st & EIDOS_UI_CLICK) != 0;
                    if ((st & EIDOS_UI_HOVER) == 0) {
                        continue;
                    }
                    dx = (long)(mr[i].x + mr[i].w / 2 - mx);
                    dy = (long)(mr[i].y + mr[i].h / 2 - my);
                    if (hov < 0 || dx * dx + dy * dy < best) {
                        hov = i;
                        best = dx * dx + dy * dy;
                    }
                }
                if (hov >= 0 && !eidos_ui_keys_lit() && hov != ci) {
                    sel_vp = vp_list[hov];
                    ci = hov;
                }
                if (clicked && hov >= 0) {
                    pick = hov;
                }
            }
            if (pick >= 0) {
                int vp = vp_list[pick];

                tap_request(b.rule == 2 ? t_x[vp - 2] : d_x[vp - 2],
                            b.rule == 2 ? t_y[vp - 2] : d_y[vp - 2], bt_sig(&b, 3), in->frame);
            }
            /* one lit at a time, a move that hits several too (taking any
             * of them takes the move) */
            ui_cursor_corners(c, mr[ci].x, mr[ci].y, mr[ci].w, mr[ci].h, UI_CYAN);
        }
    }
    /* The game lays the targets out as two rows (foes over the own side);
     * the list shows one column, so up and down walk the column and the
     * game's cursor follows them. */
    if (!corner) {
        int order[6], en[8] = { 0 };
        int to;

        for (i = 0; i < rows; i++) {
            order[i] = vp_list[i] - 2;
            en[vp_list[i] - 2] = legal[i];
        }
        /* the game's cursor starts hidden, on no legal target: the first
         * arrow lands on the first one of the column */
        if (cur < 0 || cur > 7 || !en[cur]) {
            u32 k = eidos_ui_keys_pressed() & (BW_KEY_UP | BW_KEY_DOWN);

            to = -1;
            for (i = 0; k != 0 && i < rows && to < 0; i++) {
                if (legal[i]) {
                    to = order[i];
                }
            }
            eidos_ui_take_keys(BW_KEY_UP | BW_KEY_DOWN | BW_KEY_LEFT | BW_KEY_RIGHT);
        } else {
            to = column_step(order, rows, en, cur);
        }
        if (to >= 0) {
            set_input_cursor(&b, to);
            cur = to;
        }
    }
    /* the targets stand on the plates, BACK nearest them */
    back_y = sh.top - 20;
    y = back_y - 3 - rows * 25;
    for (i = 0; !corner && i < rows; i++) {
        int ry = y + i * 25;
        int st = hit(c, x, ry, cw, 22);
        int vp = vp_list[i];
        int tx = b.rule == 2 ? t_x[vp - 2] : d_x[vp - 2];
        int ty = b.rule == 2 ? t_y[vp - 2] : d_y[vp - 2];
        u8 *poke = poke_at_view(&b, vp);
        MonView mvw;
        const UiImage *icon;
        int own = (vp & 1) == 0;
        int sel;

        if (st & EIDOS_UI_HOVER) {
            hovered = i;
        }
        if ((st & EIDOS_UI_CLICK) && legal[i]) {
            tap_request(tx, ty, bt_sig(&b, 3), in->frame);
        }
        mon_view(poke, &mvw);
        sel = hovered >= 0 ? hovered == i : cur == vp - 2;
        ui_panel(c, x, ry, cw, 22, UI_CUT_TL | UI_CUT_BR, 5, own ? UI_NAVY : UI_BODY,
                 sel && legal[i] ? UI_CURSOR : UI_EDGE, 235);
        icon = ui_mon_icon((u16)mvw.species, (u8)mvw.form, 0);
        if (icon != NULL) {
            ui_image(c, x + 4, ry + (22 - icon->h) / 2, icon, 1);
        }
        {
            int tx2 = ui_text(c, x + 30, ry + 2, mvw.name, legal[i] ? UI_WHITE : UI_EDGE2, UI_EDGE,
                              UI_FONT_LARGE, 1);

            ui_gender(c, tx2 + 2, ry + 2, mvw.gender, UI_FONT_LARGE);
        }
        ui_bar(c, x + 30, ry + 17, cw - 40, 3, mvw.hp, mvw.maxhp);
        if (!legal[i]) {
            ui_fill(c, x + 3, ry + 3, cw - 6, 16, UI_INK, 120);
        }
    }
    {
        int st = hit(c, x, back_y, cw, 20);
        int sel = (st & EIDOS_UI_HOVER) != 0;

        if (st & EIDOS_UI_CLICK) {
            tap_request(215, 167, bt_sig(&b, 3), in->frame);
        }
        ui_cmd_button(c, x, back_y, cw, 20, "BACK", UI_GREY, sel);
    }
    tap_service(bt_sig(&b, 3), in->frame);
    if (eidos_ui_right_click()) {
        eidos_ui_press(BW_KEY_B, 2);
    }
    {
        u32 raw = rd32(b.aux, 0x70) != 0 ? rd32(rdp(b.aux, 0x70), 0) : 0;
        char mname[40];

        if (!move_name((int)((raw >> 7) & 0xFFFFu), mname, sizeof(mname))) {
            snprintf(mname, sizeof(mname), "the move");
        }
        snprintf(prompt_buf, sizeof(prompt_buf), "Use %s on which?", mname);
    }
    shell_draw(c, &b, &sh, prompt_buf, 1);
    draw_hint_strip(c, 0, c->canvas->w, hints, 2, keys);
}

static const EidosUiRecipe recipe_target = { "battle_target", target_match, target_run, NULL, 0 };

/* ------------------------------------------------------------------ */
/* The modal frame shared by the Bag and the party list                */
/* ------------------------------------------------------------------ */

typedef struct Modal {
    int x, y, w, h;
} Modal;

static Modal modal_rect(const EidosUiCtx *c, int pw, int minw, int maxw, int ph, int minh,
                        int maxh)
{
    Modal m;
    int cw = c->canvas->w, ch = c->canvas->h;

    m.w = clampi(cw * pw / 100, minw, maxw);
    m.h = clampi(ch * ph / 100, minh, maxh);
    if (m.w > cw - 8) m.w = cw - 8;
    if (m.h > ch - 17 - 8) m.h = ch - 17 - 8;
    m.x = (cw - m.w) / 2;
    m.y = (ch - 17 - m.h) / 2;
    return m;
}

/* Right click, or a click outside the modal, is B. */
static void modal_back(EidosUiCtx *c, const Modal *m, const EidosUiFrame *in, int allowed)
{
    (void)in;
    int outside = hit(c, 0, 0, c->canvas->w, c->canvas->h - 17) & EIDOS_UI_CLICK;
    int inside = hit(c, m->x, m->y, m->w, m->h) & EIDOS_UI_HOVER;

    if (allowed && (eidos_ui_right_click() || (outside && !inside))) {
        eidos_ui_press(BW_KEY_B, 2);
    }
}

static void modal_frame(EidosUiCtx *c, const Modal *m, const char *title, const char *sub,
                        const char *right, u32 accent, u32 fill, u32 edge)
{
    ui_panel(c, m->x, m->y, m->w, m->h, UI_CUT_TL | UI_CUT_BR, 8, fill, edge, 245);
    ui_header(c, m->x + 3, m->y + 3, m->w - 6, title, sub, right, accent);
}

/* A small push button with a hit region; returns the state. */
static int push_button(EidosUiCtx *c, int x, int y, int w, int h, const char *text, u32 color,
                       int enabled)
{
    int st = hit(c, x, y, w, h);
    int sel = enabled && (st & EIDOS_UI_HOVER) != 0;

    ui_cmd_button(c, x, y, w, h, text, enabled ? color : UI_EDGE2, sel);
    return enabled ? st : 0;
}

/* ------------------------------------------------------------------ */
/* Battle Bag (ov197)                                                  */
/* ------------------------------------------------------------------ */

/* After a touch the game's cursor (a CursorMove: +0x2184 in the party list, +0x144
 * in the bag) waits in state 0 and
 * the first key only wakes it (CURSOR_MOVE_StartInput), so after a click that
 * opens the Pokemon menu the first key did nothing.  With the mouse and the
 * keys mixed freely, the cursor is kept awake: a key acts at once, in
 * whatever menu is open. */
static void cursor_wake(u8 *cm)
{
    if (gptr_ok(cm) && rd32(cm, 4) == 0 && cm[0x0C] == 1 &&
        (eidos_ui_keys_pressed() & (BW_KEY_A | BW_KEY_B | BW_KEY_UP | BW_KEY_DOWN | BW_KEY_LEFT |
                                    BW_KEY_RIGHT)) != 0) {
        u32 one = 1;

        memcpy(cm + 4, &one, 4);
        cm[0x0E] = 0xFF;
        cm[0x0F] = 0xFF;
    }
}

typedef struct BagV {
    u8 *p;
    int page;               /* 0 pockets, 1 list, 2 confirm               */
    int pocket;
    int cursor;             /* the game's cursor entry                    */
    int scroll;             /* page number of the list                    */
    int slot;               /* slot of the chosen item (0..5)             */
    int pages;              /* number of list pages of this pocket        */
    int count[4];
    int key_item, key_slot;
} BagV;

static void bag_read(u8 *p, BagV *v)
{
    u8 *menu = rdp(p, 0);
    u8 *cm = rdp(p, 0x144);
    int i;

    memset(v, 0, sizeof(*v));
    v->p = p;
    v->page = p[0x532];
    v->pocket = p[0x533] & 3;
    v->cursor = gptr_ok(cm) ? cm[0x0D] : 0;
    if (gptr_ok(menu)) {
        v->scroll = rds16(menu, 0x38 + 2 * (u32)v->pocket);
        v->slot = rds16(menu, 0x30 + 2 * (u32)v->pocket);
    }
    v->pages = p[0x539 + v->pocket] + 1;
    for (i = 0; i < 4; i++) {
        v->count[i] = p[0x535 + i];
    }
    v->key_item = (int)rd16(p, 0x540);
    v->key_slot = p[0x542] & 3;
}

static int bag_item(const BagV *v, int pocket, int n, int *qty)
{
    u32 off;

    if (n < 0 || n >= 36) {
        return 0;
    }
    off = 0x150 + (u32)((pocket * 36 + n) * 4);
    if (qty != NULL) {
        *qty = (int)rd16(v->p, off + 2);
    }
    return (int)rd16(v->p, off);
}

static int bag_qty_of(const BagV *v, int pocket, int item)
{
    int n, q;

    for (n = 0; n < 36; n++) {
        if (bag_item(v, pocket, n, &q) == item) {
            return q;
        }
    }
    return 0;
}

static int bag_match(void)
{
    Bt b;

    return bt_read(&b) && b.bag != NULL;
}

static const char *pocket_name(int pocket, char *buf, int cap)
{
    char a[40], d[40];

    switch (pocket) {
    case 0:
        if (snprintf(buf, (size_t)cap, "%s %s", label(MSG_BAG_UI, 22, "HP/PP", a, sizeof(a)),
                     label(MSG_BAG_UI, 23, "RESTORE", d, sizeof(d))) >= cap) {
            buf[cap - 1] = '\0';     /* cut to the buffer; the text stays terminated */
        }
        return buf;
    case 1:
        if (snprintf(buf, (size_t)cap, "%s %s", label(MSG_BAG_UI, 24, "STATUS", a, sizeof(a)),
                     label(MSG_BAG_UI, 25, "RESTORE", d, sizeof(d))) >= cap) {
            buf[cap - 1] = '\0';     /* cut to the buffer; the text stays terminated */
        }
        return buf;
    case 2:
        return label(MSG_BAG_UI, 26, "POK\xC3\x89 BALLS", buf, cap);
    default:
        return label(MSG_BAG_UI, 27, "BATTLE ITEMS", buf, cap);
    }
}

static int pocket_icon_item(int pocket)
{
    static const int items[4] = { 17, 18, 4, 57 };     /* POTION, ANTIDOTE, POKE BALL, X ATTACK */

    return items[pocket & 3];
}

static u32 bag_sig(const BagV *v)
{
    return (3u << 24) | ((u32)v->page << 16) | ((u32)v->pocket << 12) | ((u32)(v->scroll & 15) << 8) |
           ((u32)(v->p[0x530]) & 0xFFu);
}


static void bag_run(EidosUiCtx *c, EidosUiView *view, const EidosUiFrame *in)
{
    static const char *const h0[] = { "A", "Select", "B", "Back" };
    static const char *const h2[] = { "A", "Use", "B", "Back" };
    static const u32 keys[] = { BW_KEY_A, BW_KEY_B };
    static const int list_x[6] = { 63, 191, 63, 191, 63, 191 };
    static const int list_y[6] = { 31, 31, 79, 79, 127, 127 };
    static const int pocket_x[4] = { 63, 63, 191, 191 };
    static const int pocket_y[4] = { 39, 111, 39, 111 };
    Bt b;
    BagV v;
    Modal m;
    EidosUiRect saved;
    char buf[64], desc[300];
    u32 sig;
    int i, ix, iy, iw, ih;

    if (!bt_read(&b) || b.bag == NULL) {
        return;
    }
    cursor_wake(rdp(b.bag, 0x144));
    bag_read(b.bag, &v);
    sig = bag_sig(&v);
    if (debug_on()) {
        static u32 last_sig;

        if (sig != last_sig) {
            fprintf(stderr, "battle ui: bag page %d pocket %d state %d cursor %d scroll %d slot %d frame %u\n",
                    v.page, v.pocket, v.p[0x530], v.cursor, v.scroll, v.slot, (unsigned)in->frame);
            last_sig = sig;
        }
    }
    view->mode = EIDOS_VIEW_WORLD;
    view->blur = 1;
    m = modal_rect(c, 80, 300, 440, 82, 180, 250);
    modal_frame(c, &m, "BAG", "BATTLE", "", UI_PINK, 0x3C0A1Au, UI_RED);
    ui_push(c, m.x, m.y, &saved);
    ix = 6;
    iy = 28;
    iw = m.w - 12;
    ih = m.h - iy - 6;

    if (v.page == 0) {
        /* the four pockets, column by column as the game lays them out */
        int pw = (iw - 6) / 2, ph = 30;
        int last_y, last_h;

        for (i = 0; i < 4; i++) {
            int px = ix + (i / 2) * (pw + 6);
            int py = iy + (i % 2) * (ph + 4);
            int st = hit(c, px, py, pw, ph);
            int sel = (st & EIDOS_UI_HOVER) ? 1 : (v.cursor == i && eidos_ui_keys_lit());
            const UiImage *icon = ui_item_icon((u16)pocket_icon_item(i));
            char cnt[16];

            if (st & EIDOS_UI_CLICK) {
                tap_request(pocket_x[i], pocket_y[i], sig, in->frame);
            }
            ui_panel(c, px, py, pw, ph, UI_CUT_TL | UI_CUT_BR, 5, sel ? UI_PINK2 : 0x3C0A1Au,
                     sel ? UI_WHITE : UI_RED, 255);
            ui_text(c, px + 8, py + ui_text_y(ph, UI_FONT_LARGE), pocket_name(i, buf, sizeof(buf)),
                    UI_WHITE, UI_RED2, UI_FONT_LARGE, 1);
            if (icon != NULL) {
                ui_image(c, px + pw - icon->w - 5, py + (ph - icon->h) / 2, icon, 1);
            }
            put_num(cnt, sizeof(cnt), v.count[i]);
            ui_text(c, px + pw - 32 - ui_text_w(cnt, UI_FONT_SMALL), py + ui_text_y(ph, UI_FONT_SMALL),
                    cnt, UI_LIGHT, UI_RED2, UI_FONT_SMALL, 1);
        }
        last_y = iy + 2 * (ph + 4);
        last_h = ih - (last_y - iy);
        {
            /* LAST ITEM USED: everything that is left, the item's icon centred */
            int lw = iw - 60;
            int st = hit(c, ix, last_y, lw, last_h);
            int sel = (st & EIDOS_UI_HOVER) ? 1 : (v.cursor == 4 && eidos_ui_keys_lit());
            const UiImage *icon = v.key_item != 0 ? ui_item_icon((u16)v.key_item) : NULL;
            char name[48];
            int usable = v.key_item != 0;

            if (usable && (st & EIDOS_UI_CLICK)) {
                tap_request(107, 171, sig, in->frame);
            }
            ui_panel(c, ix, last_y, lw, last_h, UI_CUT_TL | UI_CUT_BR, 6, sel && usable ? UI_PINK2 : 0x3C0A1Au,
                     sel ? UI_WHITE : UI_RED, 255);
            ui_text(c, ix + 10, last_y + 4,
                    label(MSG_BAG_UI, 8, "LAST USED ITEM", buf, sizeof(buf)), UI_PINK, UI_RED2,
                    UI_FONT_SMALL, 1);
            if (icon != NULL) {
                int room = last_h - 18, tw, total, gx, scale, ty;
                char q[24];

                scale = clampi(room / icon->h, 1, 4);
                item_name(v.key_item, name, sizeof(name));
                snprintf(q, sizeof(q), "\xC3\x97%d", bag_qty_of(&v, v.key_slot, v.key_item));
                tw = ui_text_w(name, UI_FONT_LARGE);
                total = icon->w * scale + 10 + tw;
                gx = ix + (lw - total) / 2;
                ty = last_y + 14 + (room - 15) / 2;
                ui_image(c, gx, last_y + 14 + (room - icon->h * scale) / 2, icon, scale);
                ui_text(c, gx + icon->w * scale + 10, ty, name, UI_WHITE, UI_RED2, UI_FONT_LARGE, 1);
                ui_text(c, ix + lw - 12 - ui_text_w(q, UI_FONT_LARGE), ty, q, UI_WHITE, UI_RED2,
                        UI_FONT_LARGE, 1);
            } else {
                /* no item used yet in this battle: say so, centred */
                const char *none = "No item used yet";

                ui_text(c, ix + (lw - ui_text_w(none, UI_FONT_LARGE)) / 2,
                        last_y + 14 + ui_text_y(last_h - 18, UI_FONT_LARGE), none, UI_GREY,
                        UI_RED2, UI_FONT_LARGE, 1);
            }
        }
    } else if (v.page == 1) {
        /* the item list: six slots, two columns, as the game lays them out */
        int colw = (iw - 6) / 2, rowh = 30;
        int detail_y, nav_y;
        int shown = v.cursor;
        int hov = -1;

        if (shown < 0 || shown > 5) {
            shown = 0;
        }
        for (i = 0; i < 6; i++) {
            int px = ix + (i % 2) * (colw + 6);
            int py = iy + (i / 2) * (rowh + 3);
            int st = hit(c, px, py, colw, rowh);
            int qty = 0, item = bag_item(&v, v.pocket, v.scroll * 6 + i, &qty);

            if (item == 0) {
                continue;
            }
            if (st & EIDOS_UI_HOVER) {
                hov = i;
            }
            if (st & EIDOS_UI_CLICK) {
                tap_request(list_x[i], list_y[i], sig, in->frame);
            }
        }
        if (hov >= 0) {
            shown = hov;
        }
        for (i = 0; i < 6; i++) {
            int px = ix + (i % 2) * (colw + 6);
            int py = iy + (i / 2) * (rowh + 3);
            int qty = 0, item = bag_item(&v, v.pocket, v.scroll * 6 + i, &qty);
            char name[48];

            if (item == 0) {
                ui_panel(c, px, py, colw, rowh, UI_CUT_TL | UI_CUT_BR, 4, 0x2A0814u, 0x5A1424u, 160);
                continue;
            }
            item_name(item, name, sizeof(name));
            ui_item_row(c, px, py, colw, ui_item_icon((u16)item), name, qty,
                        hov >= 0 ? hov == i : v.cursor == i);
        }
        detail_y = iy + 3 * (rowh + 3) + 1;
        nav_y = m.h - 6 - 18;
        {
            int qty = 0, item = bag_item(&v, v.pocket, v.scroll * 6 + shown, &qty);

            ui_panel(c, ix, detail_y, iw, nav_y - detail_y - 3, UI_CUT_TL | UI_CUT_BR, 4, 0x2A0814u, UI_RED, 255);
            if (item != 0 && msg_line(MSG_DESC, (u32)item, desc, sizeof(desc))) {
                draw_wrapped(c, ix + 6, detail_y + 3, iw - 12, desc, UI_FONT_SMALL, 11, 3, UI_LIGHT,
                             UI_RED2);
            }
        }
        /* page arrows and the page count */
        {
            char pg[24];
            int has = v.pages > 1;
            int st;

            st = push_button(c, ix, nav_y, 22, 18, "<", UI_PINK, has);
            if (st & EIDOS_UI_CLICK) {
                tap_request(19, 171, sig, in->frame);
            }
            st = push_button(c, ix + 26, nav_y, 22, 18, ">", UI_PINK, has);
            if (st & EIDOS_UI_CLICK) {
                tap_request(59, 171, sig, in->frame);
            }
            snprintf(pg, sizeof(pg), "%d/%d", v.scroll + 1, v.pages);
            ui_text(c, ix + 54, nav_y + ui_text_y(18, UI_FONT_LARGE), pg, UI_LIGHT, UI_RED2,
                    UI_FONT_LARGE, 1);
            if (has) {
                int wh = hit_wheel(c, 0, 0, m.w, m.h);

                if (wh > 0) {
                    tap_request(19, 171, sig, in->frame);
                } else if (wh < 0) {
                    tap_request(59, 171, sig, in->frame);
                }
            }
        }
    } else {
        /* confirm: the chosen item and USE */
        int qty = 0, item = bag_item(&v, v.pocket, v.scroll * 6 + v.slot, &qty);
        const UiImage *icon = item != 0 ? ui_item_icon((u16)item) : NULL;
        char name[48], q[24];
        int btn_y = m.h - 6 - 22, nx;

        ui_panel(c, ix, iy, iw, btn_y - iy - 4, UI_CUT_TL | UI_CUT_BR, 6, 0x3C0A1Au, UI_RED, 255);
        if (item != 0) {
            int scale = 2;
            int boxh = btn_y - iy - 4;

            item_name(item, name, sizeof(name));
            if (icon != NULL) {
                ui_image(c, ix + 12, iy + 12, icon, scale);
            }
            nx = ix + 12 + (icon != NULL ? icon->w * scale : 0) + 12;
            ui_text(c, nx, iy + 10, name, UI_WHITE, UI_RED2, UI_FONT_LARGE, 1);
            snprintf(q, sizeof(q), "\xC3\x97%d", qty);
            ui_text(c, ix + iw - 12 - ui_text_w(q, UI_FONT_LARGE), iy + 10, q, UI_WHITE, UI_RED2,
                    UI_FONT_LARGE, 1);
            if (msg_line(MSG_DESC, (u32)item, desc, sizeof(desc))) {
                draw_wrapped(c, nx, iy + 28, iw - (nx - ix) - 12, desc, UI_FONT_SMALL, 11,
                             (boxh - 30) / 11, UI_LIGHT, UI_RED2);
            }
        }
        {
            int bw2 = (iw - 6) / 2;
            int st = push_button(c, ix, btn_y, bw2, 22, label(MSG_BAG_UI, 31, "USE", buf, sizeof(buf)),
                                 UI_PINK, 1);

            if (st & EIDOS_UI_CLICK) {
                tap_request(107, 171, sig, in->frame);
            }
            if (v.cursor == 0 && (st & EIDOS_UI_HOVER) == 0 && eidos_ui_keys_lit()) {
                ui_cursor_corners(c, ix, btn_y, bw2, 22, UI_WHITE);
            }
            st = push_button(c, ix + bw2 + 6, btn_y, bw2, 22, "BACK", UI_GREY, 1);
            if (st & EIDOS_UI_CLICK) {
                tap_request(235, 171, sig, in->frame);
            }
            if (v.cursor == 1 && (st & EIDOS_UI_HOVER) == 0 && eidos_ui_keys_lit()) {
                ui_cursor_corners(c, ix + bw2 + 6, btn_y, bw2, 22, UI_WHITE);
            }
        }
    }
    ui_pop(c, m.x, m.y, &saved);
    if (v.page != 2) {
        /* BACK: bottom right, where the game has it, so the arrows reach it
         * from the same side (the game's cursor: 5 on the pockets, 6 on the
         * list) */
        int bx = m.x + m.w - 6 - 54;
        int by = m.y + m.h - 6 - 18;
        int bh = 18;
        int st = hit(c, bx, by, 54, bh);
        int keyed = eidos_ui_keys_lit() && v.cursor == (v.page == 0 ? 5 : 6);

        ui_cmd_button(c, bx, by, 54, bh, "BACK", UI_GREY, (st & EIDOS_UI_HOVER) != 0 || keyed);
        if (st & EIDOS_UI_CLICK) {
            tap_request(235, 171, sig, in->frame);
        }
    }
    tap_service(sig, in->frame);
    modal_back(c, &m, in, 1);
    draw_hint_strip(c, 0, c->canvas->w, v.page == 2 ? h2 : h0, 2, keys);
}

static const EidosUiRecipe recipe_bag = { "battle_bag", bag_match, bag_run, NULL, 0 };

/* ------------------------------------------------------------------ */
/* Battle party list (ov198)                                           */
/* ------------------------------------------------------------------ */

typedef struct PlV {
    u8 *p;
    u8 *host;
    int mode, list_type, cursor, row;
} PlV;

static void pl_read(u8 *p, PlV *v)
{
    u8 *cm = rdp(p, 0x2184);

    memset(v, 0, sizeof(*v));
    v->p = p;
    v->host = rdp(p, 0);
    v->mode = p[0x2180];
    v->list_type = gptr_ok(v->host) ? v->host[0x1F] : 0;
    v->cursor = gptr_ok(cm) ? cm[0x0D] : 0;
    v->row = gptr_ok(v->host) ? v->host[0x20] : 0;
}

/* The summary record behind a visible row, or NULL when the row is empty. */
static u8 *pl_row(const PlV *v, int r)
{
    int idx;
    u8 *s;

    if (r < 0 || r > 5) {
        return NULL;
    }
    idx = v->p[0x1CC + r];
    if (idx > 5) {
        return NULL;
    }
    s = v->p + 4 + idx * 0x4C;
    if (!gptr_ok(rdp(s, 0)) || rd16(s, 4) == 0) {
        return NULL;
    }
    return s;
}

static int pl_status_badge(const u8 *s)
{
    int anim = (s[0x17] >> 3) & 15;

    /* the game's status icon animation: 6 fainted, 8 none, 1..5 a condition;
     * the condition names are unverified: sleep, poison, burn, freeze and
     * paralysis in the order of the game's own icon map */
    switch (anim) {
    case 6: return 5;
    case 1: return 3;
    case 3: return 0;
    case 2: return 2;
    case 5: return 4;
    case 4: return 1;
    default: return -1;
    }
}

static int pl_match(void)
{
    Bt b;

    return bt_read(&b) && b.plist != NULL;
}

static u32 pl_sig(const PlV *v)
{
    return (4u << 24) | ((u32)v->mode << 16) | ((u32)(v->p[0x2178] & 0xFF) << 8) | (u32)v->row;
}

static void pl_summary(EidosUiCtx *c, const PlV *v, int ix, int iy, int iw, int ih, u32 sig,
                       const EidosUiFrame *in)
{
    char buf[48], q[40];
    u8 *s = pl_row(v, v->row);
    int i, nav_y = iy + ih - 18;
    int st;

    if (s != NULL) {
        MonView mon;
        const UiImage *icon;
        int level = s[0x16] & 0x7F, hp = (int)rd16(s, 0x10), maxhp = (int)rd16(s, 0x12);
        int species = (int)rd16(s, 4), nx;

        memset(&mon, 0, sizeof(mon));
        if (!nickname(rdp(s, 0), mon.name, sizeof(mon.name))) {
            species_name(species, mon.name, sizeof(mon.name));
        }
        icon = ui_mon_icon((u16)species, s[0x15], 0);
        ui_panel(c, ix, iy, iw, nav_y - iy - 4, UI_CUT_TL | UI_CUT_BR, 5, UI_NAVY, 0x084A73u, 245);
        if (icon != NULL) {
            ui_image(c, ix + 8, iy + 6, icon, 1);
        }
        nx = ui_text(c, ix + 38, iy + 6, mon.name, UI_WHITE, UI_EDGE, UI_FONT_LARGE, 1);
        ui_gender(c, nx + 3, iy + 6, s[0x17] & 7, UI_FONT_LARGE);
        snprintf(q, sizeof(q), "Lv.%d", level);
        ui_text(c, ix + iw - 8 - ui_text_w(q, UI_FONT_LARGE), iy + 6, q, UI_LIGHT, UI_EDGE,
                UI_FONT_LARGE, 1);
        ui_bar(c, ix + 38, iy + 24, iw - 100, 4, hp, maxhp);
        snprintf(q, sizeof(q), "%d/%d", hp, maxhp);
        ui_text(c, ix + iw - 8 - ui_text_w(q, UI_FONT_SMALL), iy + 20, q, UI_LIGHT, UI_EDGE,
                UI_FONT_SMALL, 1);
        if (v->mode == 2) {
            static const char *const stat_names[5] = { "ATTACK", "DEFENSE", "SP. ATK", "SP. DEF", "SPEED" };
            static const u32 stat_off[5] = { 6, 8, 12, 14, 10 };
            int sw = (iw - 24) / 2;

            for (i = 0; i < 5; i++) {
                int sx = ix + 8 + (i % 2) * (sw + 8);
                int sy = iy + 36 + (i / 2) * 17;
                int val = (int)rd16(s, stat_off[i]);

                ui_stat_row(c, sx, sy, sw, stat_names[i], val, val, 200, UI_SKY);
            }
            {
                int item = (int)rd16(s, 0x1A);

                if (item != 0) {
                    item_name(item, buf, sizeof(buf));
                    ui_text(c, ix + 8, iy + 36 + 3 * 17, buf, UI_LIGHT, UI_EDGE, UI_FONT_LARGE, 1);
                }
            }
        } else {
            for (i = 0; i < 4; i++) {
                const u8 *mv = s + 0x2C + i * 8;
                int id = (int)rd16(mv, 0);
                int mx2 = ix + 8 + (i % 2) * ((iw - 24) / 2 + 8);
                int my2 = iy + 34 + (i / 2) * 26;
                int mw2 = (iw - 24) / 2;
                char name[40];
                int type = kit_type(mv[4]);

                if (id == 0) {
                    ui_move_row(c, mx2, my2, mw2, 0, NULL, 0, 0, 0);
                    continue;
                }
                if (!msg_line(MSG_MOVE, (u32)id, name, sizeof(name))) {
                    snprintf(name, sizeof(name), "MOVE %d", id);
                }
                ui_move_row(c, mx2, my2, mw2, type, name, mv[2], mv[3] ? mv[3] : mv[2], 0);
            }
        }
    }
    /* the footer: previous, next, the other page, BACK */
    st = push_button(c, ix, nav_y, 22, 18, "<", UI_SKY, 1);
    if (st & EIDOS_UI_CLICK) {
        tap_request(19, 171, sig, in->frame);
    }
    st = push_button(c, ix + 26, nav_y, 22, 18, ">", UI_SKY, 1);
    if (st & EIDOS_UI_CLICK) {
        tap_request(59, 171, sig, in->frame);
    }
    if (v->mode == 2 || v->mode == 3) {
        const char *t = v->mode == 2 ? label(MSG_PLIST_UI, 25, "CHECK MOVES", buf, sizeof(buf))
                                     : label(MSG_PLIST_UI, 24, "SUMMARY", buf, sizeof(buf));

        st = push_button(c, ix + 56, nav_y, 100, 18, t, UI_SKY, 1);
        if (st & EIDOS_UI_CLICK) {
            tap_request(147, 171, sig, in->frame);
        }
    }
    st = push_button(c, ix + iw - 54, nav_y, 54, 18, "BACK", UI_GREY, 1);
    if (st & EIDOS_UI_CLICK) {
        tap_request(235, 171, sig, in->frame);
    }
}

static void pl_run(EidosUiCtx *c, EidosUiView *view, const EidosUiFrame *in)
{
    static const char *const h0[] = { "A", "Select", "B", "Back" };
    static const u32 keys[] = { BW_KEY_A, BW_KEY_B };
    static const int row_x[6] = { 63, 191, 63, 191, 63, 191 };
    static const int row_y[6] = { 23, 31, 71, 79, 119, 127 };
    Bt b;
    PlV v;
    Modal m;
    EidosUiRect saved;
    char buf[80], msg[96];
    u32 sig;
    int i, ix, iy, iw, ih;
    const char *sub;
    int msg_id;

    if (!bt_read(&b) || b.plist == NULL) {
        return;
    }
    cursor_wake(rdp(b.plist, 0x2184));
    pl_read(b.plist, &v);
    sig = pl_sig(&v);
    view->mode = EIDOS_VIEW_WORLD;
    view->blur = 1;
    m = modal_rect(c, 84, 320, 460, 84, 190, 260);
    sub = v.list_type == 3 ? "ITEM" : "SWITCH";
    modal_frame(c, &m, "POK\xC3\x89MON", sub, "", UI_PINK, UI_BODY2, UI_EDGE);
    ui_push(c, m.x, m.y, &saved);
    ix = 6;
    iy = 28;
    iw = m.w - 12;
    ih = m.h - iy - 6;

    if (v.mode >= 2 && v.mode <= 4) {
        pl_summary(c, &v, ix, iy, iw, ih, sig, in);
    } else {
        int colw = (iw - 6) / 2, rowh = 30, gap = 4;
        int msg_y = iy + 3 * (rowh + gap) + 2;
        int msg_h = ih - (msg_y - iy) - 22;
        int menu_open = v.mode == 1;
        int hov = -1;

        for (i = 0; i < 6; i++) {
            int px = ix + (i % 2) * (colw + 6);
            int py = iy + (i / 2) * (rowh + gap);
            int st = menu_open ? 0 : hit(c, px, py, colw, rowh);

            if (pl_row(&v, i) == NULL) {
                continue;
            }
            if (st & EIDOS_UI_HOVER) {
                hov = i;
            }
            if (st & EIDOS_UI_CLICK) {
                tap_request(row_x[i], row_y[i], sig, in->frame);
            }
        }
        for (i = 0; i < 6; i++) {
            int px = ix + (i % 2) * (colw + 6);
            int py = iy + (i / 2) * (rowh + gap);
            u8 *s = pl_row(&v, i);
            char name[40];
            int sel;
            const UiImage *icon;
            int badge;

            if (s == NULL) {
                ui_party_row(c, px, py, colw, i, NULL, 0, 0, 0, 0, NULL, 0);
                continue;
            }
            if ((s[0x16] >> 7) != 0) {
                snprintf(name, sizeof(name), "EGG");
            } else if (!nickname(rdp(s, 0), name, sizeof(name))) {
                species_name((int)rd16(s, 4), name, sizeof(name));
            }
            sel = menu_open ? (v.row == i) : (hov >= 0 ? hov == i : v.cursor == i);
            icon = ui_mon_icon((u16)rd16(s, 4), s[0x15], 0);
            ui_party_row(c, px, py, colw, i, name, s[0x17] & 7, s[0x16] & 0x7F, (int)rd16(s, 0x10),
                         (int)rd16(s, 0x12), icon, sel);
            badge = pl_status_badge(s);
            if (badge >= 0) {
                ui_status_badge(c, px + colw - 30, py + 14, badge);
            }
        }
        /* the game's own question */
        msg_id = v.list_type == 3 ? 7 : v.list_type == 2 ? 9 : v.list_type == 8 ? 10 : 6;
        snprintf(msg, sizeof(msg), "%s", label(MSG_PLIST_UI, (u32)msg_id, "Choose a Pok\xC3\xA9mon.",
                                              buf, sizeof(buf)));
        ui_panel(c, ix, msg_y, iw, msg_h, UI_CUT_TL | UI_CUT_BR, 5, UI_BODY, UI_EDGE, 235);
        ui_text(c, ix + 10, msg_y + ui_text_y(msg_h, UI_FONT_LARGE), msg, UI_WHITE, UI_GREY,
                UI_FONT_LARGE, 1);
        {
            int st = push_button(c, ix + iw - 54, iy + ih - 18, 54, 18, "BACK", UI_GREY, !menu_open);

            if (st & EIDOS_UI_CLICK) {
                tap_request(235, 171, sig, in->frame);
            }
            /* the game's cursor 6 is BACK */
            if (!menu_open && v.cursor == 6 && (st & EIDOS_UI_HOVER) == 0 && eidos_ui_keys_lit()) {
                ui_cursor_corners(c, ix + iw - 54, iy + ih - 18, 54, 18, UI_WHITE);
            }
        }
        if (menu_open) {
            /* the Pokemon menu: SHIFT (or why not), SUMMARY, CHECK MOVES, BACK */
            int mw = 150, mh = 4 * 28 + 4;
            int mx = ix + (iw - mw) / 2, my = iy + (ih - mh) / 2;
            u8 *s = pl_row(&v, v.row);
            const char *first;
            char b1[40], b2[40], b3[40], b4[40];
            static const int tx2[4] = { 127, 51, 155, 235 };
            static const int ty2[4] = { 77, 171, 171, 171 };
            const char *names[4];
            int st;

            /* The game lays the menu out as SHIFT over SUMMARY, CHECK MOVES and
             * CANCEL side by side; the interface shows a column, so up and down
             * are the interface's and the game's cursor follows them. */
            {
                static const int order[4] = { 0, 1, 2, 3 };
                static const int all[4] = { 1, 1, 1, 1 };
                u8 *cm = rdp(v.p, 0x2184);
                int to = column_step(order, 4, all, v.cursor);

                if (to >= 0 && gptr_ok(cm)) {
                    eidos_ui_sound(0x552);
                    cm[0x0E] = cm[0x0D];
                    cm[0x0D] = (u8)to;
                    v.cursor = to;
                }
            }
            ui_fill(c, 0, 0, m.w, m.h, UI_INK, 120);
            ui_panel(c, mx, my, mw, mh, UI_CUT_TL | UI_CUT_BR, 6, UI_BODY2, UI_SKY, 250);
            if (s != NULL && (int)rd16(s, 0x10) == 0) {
                first = label(MSG_PLIST_UI, 26, "CANNOT BATTLE", b1, sizeof(b1));
            } else if (s != NULL && v.row < (int)v.host[0x22]) {
                first = label(MSG_PLIST_UI, 27, "IN BATTLE", b1, sizeof(b1));
            } else {
                first = label(MSG_PLIST_UI, 21, "SHIFT", b1, sizeof(b1));
            }
            names[0] = first;
            names[1] = label(MSG_PLIST_UI, 24, "SUMMARY", b2, sizeof(b2));
            names[2] = label(MSG_PLIST_UI, 25, "CHECK MOVES", b3, sizeof(b3));
            names[3] = label(MSG_PLIST_UI, 69, "CANCEL", b4, sizeof(b4));
            for (i = 0; i < 4; i++) {
                st = push_button(c, mx + 6, my + 4 + i * 28, mw - 12, 24, names[i],
                                 i == 0 ? UI_MON : (i == 3 ? UI_GREY : UI_SKY), 1);
                if (st & EIDOS_UI_CLICK) {
                    tap_request(tx2[i], ty2[i], sig, in->frame);
                }
                if (v.cursor == i && (st & EIDOS_UI_HOVER) == 0 && eidos_ui_keys_lit()) {
                    ui_cursor_corners(c, mx + 6, my + 4 + i * 28, mw - 12, 24, UI_WHITE);
                }
            }
        }
    }
    ui_pop(c, m.x, m.y, &saved);
    tap_service(sig, in->frame);
    modal_back(c, &m, in, 1);
    draw_hint_strip(c, 0, c->canvas->w, h0, 2, keys);
}

static const EidosUiRecipe recipe_party = { "battle_party", pl_match, pl_run, NULL, 0 };

/* ------------------------------------------------------------------ */
/* Yes / no choice (screen 4: the shift prompt, the move-learn prompt) */
/* ------------------------------------------------------------------ */

/* The text of a game string buffer (u16 characters behind an 8 byte head). */
static int strbuf_text(const u8 *sb, char *out, int cap)
{
    u32 len;

    out[0] = '\0';
    if (!gptr_ok(sb)) {
        return 0;
    }
    len = rd16(sb, 2);
    if (len == 0 || len > 120 || rd32(sb, 4) != 0xB6F8D2ECu) {
        return 0;
    }
    {
        u16 chars[121];
        u32 i;

        for (i = 0; i < len; i++) {
            chars[i] = (u16)rd16(sb, 8 + 2 * i);
        }
        units_to_utf8(chars, (int)len, out, cap);
    }
    return out[0] != '\0';
}

static int choice_match(void)
{
    Bt b;

    return bt_read(&b) && b.in != NULL && b.screen == 4;
}

static void choice_run(EidosUiCtx *c, EidosUiView *view, const EidosUiFrame *in)
{
    static const char *const hints[] = { "A", "Select", "B", "No" };
    static const u32 keys[] = { BW_KEY_A, BW_KEY_B };
    static const int tap_y[2] = { 56, 104 };
    Bt b;
    Shell sh;
    char text[2][64], question[160];
    int cw, x, y, i, hovered = -1, cur;

    if (!bt_read(&b) || b.in == NULL) {
        return;
    }
    view->mode = EIDOS_VIEW_WORLD;
    view->hide_2d = 1;
    /* the two answers: the choice's own strings (the core's task work) */
    if (!strbuf_text(rdp(b.core, 0x2C), text[0], sizeof(text[0]))) {
        snprintf(text[0], sizeof(text[0]), "YES");
    }
    if (!strbuf_text(rdp(b.core, 0x30), text[1], sizeof(text[1]))) {
        snprintf(text[1], sizeof(text[1]), "NO");
    }
    if (!strbuf_text(rdp(b.core, 0xAC), question, sizeof(question))) {
        question[0] = '\0';
    }
    shell_layout(c, &b, &sh);
    cw = sh.pw;
    x = sh.px;
    y = sh.top - 2 * 28 - 3;
    keys_live(&b);
    cur = input_cursor(&b);
    for (i = 0; i < 2; i++) {
        int st = hit(c, x, y + i * 31, cw, 28);

        if (st & EIDOS_UI_HOVER) {
            hovered = i;
        }
        if (st & EIDOS_UI_CLICK) {
            tap_request(127, tap_y[i], bt_sig(&b, 4), in->frame);
        }
    }
    for (i = 0; i < 2; i++) {
        int sel = hovered >= 0 ? hovered == i : cur == i;

        ui_cmd_button(c, x, y + i * 31, cw, 28, text[i], i == 0 ? UI_MON : UI_GREY, sel);
    }
    tap_service(bt_sig(&b, 4), in->frame);
    if (eidos_ui_right_click()) {
        eidos_ui_press(BW_KEY_B, 2);
    }
    shell_draw(c, &b, &sh, question, 0);
    draw_hint_strip(c, 0, c->canvas->w, hints, 2, keys);
}

static const EidosUiRecipe recipe_choice = { "battle_choice", choice_match, choice_run, NULL, 0 };

/* ------------------------------------------------------------------ */
/* The group                                                           */
/* ------------------------------------------------------------------ */

const EidosUiRecipe *const ui_recipes_battle[] = {
    &recipe_bag, &recipe_party, &recipe_action, &recipe_move, &recipe_target, &recipe_choice, &recipe_plain
};
const u32 ui_recipes_battle_count = 7;
