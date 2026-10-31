/*
 * engine/ui/recipe_party.c -- the one-screen recipes for the party list
 * (overlay 91) and the Pokemon summary (overlay 131)
 * (engine/ui/ui.h, docs/unified-view-recipes.md, sections 5 and 6).
 *
 * The game keeps the logic.  A recipe reads the game's own structs (the
 * proc on top of the in-game stack, its work area, the party and the
 * Pokemon) and draws them with the kit; a click becomes the touch the game
 * tests for that control (a tap at the centre of its bottom-screen
 * rectangle) or a button press.
 *
 * The game's structures are read by offset (the DS layout, which the
 * ILP32 host build keeps): the decomp's own headers name each field
 * (src/game/ov091/pokelist_types.h, ov091/plist_plate_types.h,
 * ov091/plist_menu_types.h, ov131/p_status_types.h, ov131/p_sta_skill.c,
 * ov131/p_sta_ribbon.c).  A host with wider pointers does not match.
 *
 * Text (names, descriptions) comes from the game's message archive
 * (a/0/0/2) through the port's file layer, so a mod that replaces a bank
 * shows in the interface too.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ui.h"
#include "overlay.h"
#include "kit.h"
#include "port.h"
#include "dsmem.h"
#include "input.h"

#define BW_WEAK __attribute__((weak))

/* ------------------------------------------------------------------ */
/* The game's own accessors (weak: a link without the game still links) */
/* ------------------------------------------------------------------ */

extern void *bw_dsref__GameSystemGlobal BW_WEAK;
extern u32   POKE_TOOL_GetPartyValue(void *mon, u32 paramId, u32 arg) BW_WEAK;
extern u32   POKE_TOOL_GetExpForLevel(u32 species, u32 form, u32 level) BW_WEAK;
extern void *POKEPARTY_GetMember(void *party, u32 pos) BW_WEAK;
extern u32   POKEPARTY_GetCount(const void *party) BW_WEAK;
extern u16  *STRBUF_GetChars(void *strbuf) BW_WEAK;
extern u16   STRBUF_GetLength(void *strbuf) BW_WEAK;
extern u8    APP_TASKMENU_GetCursorPos(const void *menu) BW_WEAK;
extern u32   WAZA_TOOL_GetParam(u32 move, u32 param) BW_WEAK;
extern u32   WAZA_TOOL_IsAlwaysHit(u16 move) BW_WEAK;
extern u8    MSGDATA_GetLanguage(void) BW_WEAK;

static int game_ready(void)
{
    return sizeof(void *) == 4 &&
           &bw_dsref__GameSystemGlobal != NULL && bw_dsref__GameSystemGlobal != NULL &&
           POKE_TOOL_GetPartyValue != NULL && POKEPARTY_GetMember != NULL &&
           POKEPARTY_GetCount != NULL;
}

/* ------------------------------------------------------------------ */
/* Reading structs by DS offset                                        */
/* ------------------------------------------------------------------ */

static void *rdp(const void *base, u32 off)
{
    return *(void *const *)((const u8 *)base + off);
}

static u32 rd32(const void *base, u32 off)
{
    return *(const u32 *)((const u8 *)base + off);
}

static int rds32(const void *base, u32 off)
{
    return *(const s32 *)((const u8 *)base + off);
}

static u32 rd16(const void *base, u32 off)
{
    return *(const u16 *)((const u8 *)base + off);
}

static u32 rd8(const void *base, u32 off)
{
    return *((const u8 *)base + off);
}

/*
 * The proc on top of the in-game stack (docs/unified-view-recipes.md, 0.2):
 * _GameSystemGlobal.gameSystem (+0), its procSys (+0x08), its current (+0x08).
 * Proc: overlayId 0x04, seq 0x08, procWork 0x10, work 0x14.
 */
static const void *top_proc(void)
{
    const void *gs, *ps;

    if (!game_ready()) {
        return NULL;
    }
    gs = *(void *const *)bw_dsref__GameSystemGlobal;
    if (gs == NULL) {
        return NULL;
    }
    ps = rdp(gs, 0x08);
    if (ps == NULL) {
        return NULL;
    }
    return rdp(ps, 0x08);
}

/* The evolution scene's work (ov195 Scene) while it is the app on top. */
#define EVO_OVERLAY     195
#define BTL_RETURN_OVERLAY 92
#define SC_STATE        0x028
#define SC_YESNO        0x09C
#define SC_TCB          0x0A0
#define SC_PLISTHOST    0x0B0
#define SC_PROCSYS      0x11C
#define SC_STRBUF_YES   0x094
#define SC_STRBUF_NO    0x098

static const void *evo_scene(void)
{
    const void *p = top_proc();

    /* after a battle the scene runs on the proc system of the battle's
     * return step (ov092 EventBattleReturnWork.procSys, +0x40) */
    if (p != NULL && rds32(p, 0x04) == BTL_RETURN_OVERLAY && rdp(p, 0x14) != NULL) {
        const void *ps = rdp(rdp(p, 0x14), 0x40);

        p = ps != NULL ? rdp(ps, 0x08) : NULL;
    }
    if (p == NULL || rds32(p, 0x04) != EVO_OVERLAY || rds32(p, 0x08) < 2) {
        return NULL;
    }
    return rdp(p, 0x14);
}

/* The work area of the proc when it runs the overlay, else NULL.  The
 * evolution scene runs the summary (to forget a move) on a proc system of
 * its own (Scene.proc), so a proc there counts as on top too. */
static const void *app_work(int overlay, const void **proc_out, const void **arg_out)
{
    const void *p = top_proc();
    const void *sc = evo_scene();
    int loaded = 0;

    if (sc != NULL && overlay != EVO_OVERLAY) {
        const void *ps = rdp(sc, SC_PROCSYS);

        p = ps != NULL ? rdp(ps, 0x08) : NULL;
        /* the scene loads the summary's overlay itself and pushes the
         * proc with no overlay (-1) */
        loaded = p != NULL && rds32(p, 0x04) == -1 && bw_overlay_is_loaded(overlay);
    }
    if (p == NULL || (rds32(p, 0x04) != overlay && !loaded) || rds32(p, 0x08) < 2) {
        return NULL;
    }
    if (proc_out != NULL) {
        *proc_out = p;
    }
    if (arg_out != NULL) {
        *arg_out = rdp(p, 0x10);
    }
    return rdp(p, 0x14);
}

/* ------------------------------------------------------------------ */
/* Text: the game's strings in UTF-8                                   */
/* ------------------------------------------------------------------ */

/* Append one code unit of the game's text as UTF-8; returns the new length. */
static int put_utf8(char *out, int n, int cap, u32 code)
{
    if (code < 0x80) {
        if (n + 1 < cap) out[n++] = (char)code;
    } else if (code < 0x800) {
        if (n + 2 < cap) {
            out[n++] = (char)(0xC0 | (code >> 6));
            out[n++] = (char)(0x80 | (code & 0x3F));
        }
    } else {
        if (n + 3 < cap) {
            out[n++] = (char)(0xE0 | (code >> 12));
            out[n++] = (char)(0x80 | ((code >> 6) & 0x3F));
            out[n++] = (char)(0x80 | (code & 0x3F));
        }
    }
    return n;
}

/*
 * The game's code units to UTF-8.  0xFFFF ends; 0xFFFE is a line break
 * (kept as a space unless nl); the private ranges (control codes, the
 * unexpanded placeholders) are dropped, with the code's arguments.
 */
static void units_to_utf8(const u16 *u, int count, char *out, int cap, int nl)
{
    int i, n = 0;

    for (i = 0; i < count; i++) {
        u32 c = u[i];

        if (c == 0xFFFFu) {
            break;
        }
        if (c == 0xFFFEu) {
            n = put_utf8(out, n, cap, nl ? '\n' : ' ');
            continue;
        }
        if (c == 0xF000u && i + 2 < count) {
            i += 2 + (int)(u[i + 2] & 0xFF);       /* code, argument count, arguments */
            if (i >= count) break;
            continue;
        }
        if (c >= 0xD800u && c <= 0xF8FFu) {
            continue;
        }
        n = put_utf8(out, n, cap, c);
    }
    out[n < cap ? n : cap - 1] = '\0';
}

/* ---- the message archive ---- */

#define BANK_PARTY      157     /* 0x9D: the party list's messages          */
#define BANK_SUMMARY    158     /* 0x9E: the summary's labels               */
#define BANK_NATURE      24
#define BANK_ITEM        54
#define BANK_ITEM_DESC   53
#define BANK_PLACE       89
#define BANK_SPECIES     70
#define BANK_RIBBON     170     /* 0xAA                                     */
#define BANK_ABILITY    182     /* 0xB6                                     */
#define BANK_ABILITY_D  183
#define BANK_MOVE       203     /* 0xCB                                     */
#define BANK_MOVE_DESC  202     /* 0xCA                                     */

#define BANK_MAX 12

typedef struct MsgCache {
    int bank;
    u8 *data;
    u32 size;
} MsgCache;

static BwArcFile g_msg_arc;
static int g_msg_arc_state;                 /* 0 not tried, 1 open, -1 failed */
static MsgCache g_msg[BANK_MAX];
static int g_msg_n;

static const MsgCache *msg_bank(int bank)
{
    int i;
    long n;
    u8 *buf;

    for (i = 0; i < g_msg_n; i++) {
        if (g_msg[i].bank == bank) {
            return g_msg[i].data != NULL ? &g_msg[i] : NULL;
        }
    }
    if (g_msg_n >= BANK_MAX) {
        return NULL;
    }
    if (g_msg_arc_state == 0) {
        g_msg_arc_state = bw_arc_open(&g_msg_arc, "a/0/0/2") == 0 ? 1 : -1;
    }
    g_msg[g_msg_n].bank = bank;
    g_msg[g_msg_n].data = NULL;
    g_msg[g_msg_n].size = 0;
    if (g_msg_arc_state == 1 && (u32)bank < g_msg_arc.count) {
        n = bw_arc_member_size(&g_msg_arc, (u32)bank);
        buf = n > 16 ? (u8 *)malloc((size_t)n) : NULL;
        if (buf != NULL && bw_arc_read_member(&g_msg_arc, (u32)bank, buf, (u32)n) == n) {
            g_msg[g_msg_n].data = buf;
            g_msg[g_msg_n].size = (u32)n;
        } else {
            free(buf);
        }
    }
    return g_msg[g_msg_n++].data != NULL ? &g_msg[g_msg_n - 1] : NULL;
}

/* Message `index` of `bank` as UTF-8 into out (empty when unavailable).
 * The header is u16 languages, u16 count, u32 size, u32 reserved, then one
 * u32 section offset per language; a section is u32 size, then 8 byte
 * entries (u32 offset, u16 length, u16 reserved); strings are u16 units
 * decrypted per message (arm9/msgdata.c). */
static const char *msg_text(int bank, u32 index, char *out, int cap, int nl)
{
    const MsgCache *m = msg_bank(bank);
    u32 langs, count, lang, sec, off, len, i;
    u16 key, units[256];
    const u8 *d;

    out[0] = '\0';
    if (m == NULL) {
        return out;
    }
    d = m->data;
    langs = rd16(d, 0);
    count = rd16(d, 2);
    lang = (MSGDATA_GetLanguage != NULL) ? MSGDATA_GetLanguage() : 0;
    if (lang >= langs) {
        lang = 0;
    }
    if (index >= count || 12 + 4 * (lang + 1) > m->size) {
        return out;
    }
    sec = rd32(d, 12 + 4 * lang);
    if (sec + 4 + 8 * (count) > m->size) {
        return out;
    }
    off = rd32(d, sec + 4 + 8 * index);
    len = rd16(d, sec + 4 + 8 * index + 4);
    if (len > 255) {
        len = 255;
    }
    if (sec + off + len * 2 > m->size) {
        return out;
    }
    key = (u16)((index + 3) * 0x2983u);
    for (i = 0; i < len; i++) {
        units[i] = (u16)(rd16(d, sec + off + i * 2) ^ key);
        key = (u16)(((key & 0xE000u) >> 13) | (u16)(key << 3));
    }
    units_to_utf8(units, (int)len, out, cap, nl);
    return out;
}

/* A string from a game Strbuf (the menu's labels, a message's text). */
static const char *strbuf_text(void *sb, char *out, int cap, int nl)
{
    out[0] = '\0';
    if (sb == NULL || STRBUF_GetChars == NULL || STRBUF_GetLength == NULL) {
        return out;
    }
    {
        u32 len = STRBUF_GetLength(sb);
        const u16 *u = STRBUF_GetChars(sb);

        if (u == NULL || len > 400) {
            return out;
        }
        units_to_utf8(u, (int)len, out, cap, nl);
    }
    return out;
}

/* ------------------------------------------------------------------ */
/* Pokemon values                                                      */
/* ------------------------------------------------------------------ */

enum {
    P_SPECIES = 0x05, P_ITEM = 0x06, P_OT_ID = 0x07, P_EXP = 0x08, P_FRIEND = 0x09,
    P_ABILITY = 0x0A, P_MOVE1 = 0x36, P_PP1 = 0x3A, P_MAXPP1 = 0x42, P_IS_EGG = 0x4C,
    P_GENDER = 0x6E, P_FORM = 0x6F, P_NATURE = 0x70, P_NICK = 0x74, P_OT_NAME = 0x8E,
    P_MET_LOC = 0x96, P_MET_LEVEL = 0x99, P_STATUS = 0x9D, P_LEVEL = 0x9E, P_HP = 0xA0,
    P_MAXHP = 0xA1, P_ATK = 0xA2, P_DEF = 0xA3, P_SPE = 0xA4, P_SPA = 0xA5, P_SPD = 0xA6,
    P_TYPE1 = 0xAE, P_TYPE2 = 0xAF, P_HAS_RIBBON_FIRST = 0x19
};

static u32 pv(void *mon, u32 id)
{
    return POKE_TOOL_GetPartyValue(mon, id, 0);
}

static void mon_nick(void *mon, char *out, int cap)
{
    u16 buf[32];

    memset(buf, 0xFF, sizeof buf);
    (void)POKE_TOOL_GetPartyValue(mon, P_NICK, (u32)(uintptr_t)buf);
    units_to_utf8(buf, 12, out, cap, 0);
}

static void mon_ot(void *mon, char *out, int cap)
{
    u16 buf[32];

    memset(buf, 0xFF, sizeof buf);
    (void)POKE_TOOL_GetPartyValue(mon, P_OT_NAME, (u32)(uintptr_t)buf);
    units_to_utf8(buf, 8, out, cap, 0);
}

/* The kit's status number (0 PSN, 1 PAR, 2 BRN, 3 SLP, 4 FRZ, 5 FNT), or
 * -1.  The party's status word: bits 0..2 sleep turns, 3 poison, 4 burn,
 * 5 freeze, 6 paralysis, 7 bad poison. */
static int mon_status(void *mon, u32 hp)
{
    u32 s = pv(mon, P_STATUS);

    if (hp == 0) return 5;
    if (s & 7u) return 3;
    if (s & 0x88u) return 0;
    if (s & 0x10u) return 2;
    if (s & 0x20u) return 4;
    if (s & 0x40u) return 1;
    return -1;
}

static int gender_of(void *mon)
{
    u32 g = pv(mon, P_GENDER);

    return g > 2 ? 2 : (int)g;
}

/* The game's type numbers have no "???" at 9 (the kit's table does): fire is 9. */
static int kit_type(int t)
{
    return t;      /* the kit takes the game's own type numbers */
}

/* The scale that fits an image of `w` into `room` (a whole number >= 1). */
static int fit_scale(int w, int room)
{
    int k = w > 0 ? room / w : 1;

    return k < 1 ? 1 : k;
}

/* ------------------------------------------------------------------ */
/* Shared drawing                                                      */
/* ------------------------------------------------------------------ */

#define FOOTER_H 17

static void modal_rect(const EidosUiCtx *c, int *x, int *y, int *w, int *h)
{
    int cw = c->canvas->w, ch = c->canvas->h;
    int mw = cw - cw / 10, mh = ch - FOOTER_H - 12;

    if (mw > 460) mw = 460;
    if (mw < 280) mw = cw - 8;
    if (mh > 216) mh = 216;
    if (mh < 150) mh = ch - FOOTER_H - 4;
    *w = mw;
    *h = mh;
    *x = (cw - mw) / 2;
    *y = (ch - FOOTER_H - mh) / 2;
}

static void draw_modal(EidosUiCtx *c, int x, int y, int w, int h)
{
    ui_panel(c, x, y, w, h, UI_CUT_TL | UI_CUT_BR, 9, UI_BODY, UI_NAVY2, 240);
}

/* A click (press and release) outside the modal, above the footer. */
static int click_outside(const EidosUiCtx *c, int x, int y, int w, int h)
{
    int cw = c->canvas->w, ch = c->canvas->h - FOOTER_H;
    EidosUiRect r[4];
    int i, hit = 0;

    r[0].x = 0;     r[0].y = 0;     r[0].w = cw;          r[0].h = y;
    r[1].x = 0;     r[1].y = y + h; r[1].w = cw;          r[1].h = ch - (y + h);
    r[2].x = 0;     r[2].y = y;     r[2].w = x;           r[2].h = h;
    r[3].x = x + w; r[3].y = y;     r[3].w = cw - (x + w); r[3].h = h;
    for (i = 0; i < 4; i++) {
        if (r[i].w > 0 && r[i].h > 0 && (eidos_ui_hit(r[i]) & EIDOS_UI_CLICK)) {
            hit = 1;
        }
    }
    return hit;
}

/* Footer: the key hints; each is a button. */
static void draw_hints(EidosUiCtx *c, const char *const *pairs, const u32 *keys, int n)
{
    ui_hints(c, c->canvas->h - FOOTER_H, c->canvas->w, pairs, n, keys);
}

#define ARROWS "\xE2\x86\x91\xE2\x86\x93\xE2\x86\x90\xE2\x86\x92"

/* ================================================================== */
/* The party list (overlay 91)                                         */
/* ================================================================== */

/* PokelistWork offsets (ov091/pokelist_types.h) */
#define PL_TOUCHMODE   0x008
#define PL_PHASE       0x00C
#define PL_INPUTSTAGE  0x00E
#define PL_SWITCH_EN   0x014
#define PL_RESTORE18   0x018
#define PL_SELECTED    0x02C
#define PL_SWAPSRC     0x030
#define PL_MSGWIN      0x150
#define PL_MENU        0x154
#define PL_PLATES      0x160
#define PL_ARG         0x280
/* PokelistArg */
#define PA_PARTY       0x00
#define PA_MODE        0x34
/* PlistPlate */
#define PP_MON         0x00
#define PP_HP          0x06
#define PP_ISEGG       0x0C
#define PP_ISEMPTY     0x14
/* PlistMenu */
#define PM_COUNT       0x00
#define PM_IDS         0x02
#define PM_ENTRIES     0x14
#define PM_TASKMENU    0x74
/* PlistMessage */
#define MS_KIND        0x00
#define MS_STREAM      0x0C
#define MS_STREAMTEXT  0x10

/* The game's touch centres of the six plates (DS pixels). */
static const int k_plate_tap[6][2] = {
    { 64, 32 }, { 192, 40 }, { 64, 80 }, { 192, 88 }, { 64, 128 }, { 192, 136 }
};

typedef struct PartyView {
    const void *work;
    const void *arg;
    int phase, stage, mode, selected, swap_src, touch_mode, switch_enabled;
    int count;                  /* party size                               */
} PartyView;

static int party_read(PartyView *v)
{
    const void *w = app_work(91, NULL, NULL);
    const void *arg;
    void *party;

    if (w == NULL) {
        return 0;
    }
    arg = rdp(w, PL_ARG);
    if (arg == NULL) {
        return 0;
    }
    party = rdp(arg, PA_PARTY);
    if (party == NULL) {
        return 0;
    }
    v->work = w;
    v->arg = arg;
    v->phase = (int)rd8(w, PL_PHASE);
    v->stage = (int)rd8(w, PL_INPUTSTAGE);
    v->mode = rds32(arg, PA_MODE);
    v->selected = rds32(w, PL_SELECTED);
    v->swap_src = rds32(w, PL_SWAPSRC);
    v->touch_mode = rds32(w, PL_TOUCHMODE);
    v->switch_enabled = rds32(w, PL_SWITCH_EN);
    v->count = (int)POKEPARTY_GetCount(party);
    return v->count >= 0 && v->count <= 6;
}

static int party_match(void)
{
    PartyView v;

    if (!party_read(&v)) {
        return 0;
    }
    /* The plain list and the use-an-item list; the list that teaches a
     * move, the battle order list and the like keep the DS screens. */
    if (v.mode != 0 && v.mode != 5 && v.mode != 8 && rds32(v.work, PL_RESTORE18) == 0) {
        return 0;
    }
    /* mode 8: the list back from the summary's "forget a move" after an
     * item taught one (a Rare Candy's level), with the game's message
     * ("... did not learn ...", "... forgot ..."); PL_RESTORE18 the same
     * on the way through mode 3 */
    return 1;
}

/* The mon of plate i (the plate follows the swap animation), or NULL. */
static void *plate_mon(const PartyView *v, int i)
{
    const void *pl = rdp(v->work, PL_PLATES + 4u * (u32)i);

    if (pl == NULL || rds32(pl, PP_ISEMPTY) != 0) {
        return NULL;
    }
    return rdp(pl, PP_MON);
}

static u32 plate_hp(const PartyView *v, int i, void *mon)
{
    const void *pl = rdp(v->work, PL_PLATES + 4u * (u32)i);

    if (pl != NULL && rds32(pl, PP_ISEMPTY) == 0 && rdp(pl, PP_MON) == mon) {
        return rd16(pl, PP_HP);
    }
    return pv(mon, P_HP);
}

/* The prompt line for the phase (the game prints it in its own window). */
static void party_prompt(const PartyView *v, void *sel_mon, char *out, int cap)
{
    char tmp[256];
    int idx = -1;

    out[0] = '\0';
    switch (v->phase) {
    case 6: {
        const void *menu = rdp(v->work, PL_MENU);
        u32 first = (menu != NULL && rd32(menu, PM_COUNT) & 0xFF) ? rd16(menu, PM_IDS) : 0;

        if (first == 0 || first == 3 || first == 6 || (first >= 16 && first <= 19)) {
            idx = 18;
        } else {
            idx = 19;
        }
        break;
    }
    case 3: idx = 11; break;
    case 2:
        idx = v->mode == 5 ? 13 : 9;
        break;
    default:
        break;
    }
    if (idx < 0) {
        return;
    }
    (void)msg_text(BANK_PARTY, (u32)idx, tmp, (int)sizeof tmp, 0);
    if (idx == 18 && sel_mon != NULL) {
        char nick[32];

        mon_nick(sel_mon, nick, (int)sizeof nick);
        /* "Do what with<nl>[name]?": the name takes the placeholder's place */
        snprintf(out, (size_t)cap, "Do what with %s?", nick);
        return;
    }
    snprintf(out, (size_t)cap, "%s", tmp);
}

static int g_party_hover = -1;

static void draw_wrapped(EidosUiCtx *c, int x, int y, int width, int lh, int max,
                         const char *text, int font, u32 color, s32 shadow);
static int game_message(EidosUiCtx *c, u32 since, int x, int y, int w, int h);

static void party_run(EidosUiCtx *c, EidosUiView *view, const EidosUiFrame *in)
{
    PartyView v;
    int mx, my, mw, mh;
    int i, rowh = 30, gap = 7, colw, x0, y0;
    int phase_pick, menu_open, msg_open, stream_msg;
    void *sel_mon;
    char prompt[160], right[16];
    const void *menu = NULL;
    int menu_n = 0;
    int click_slot = -1, back_click = 0;
    static const char *const hints[] = { "A", "Choose", "B", "Back", ARROWS, "Move" };
    static const u32 hint_keys[] = { BW_KEY_A, BW_KEY_B, 0 };

    view->mode = EIDOS_VIEW_WORLD;
    view->blur = 1;
    view->hold_world = 1;     /* an app: the field is not running */
    view->hide_2d = 1;
    view->zoom = 1.0f;

    if (!party_read(&v)) {
        return;
    }
    modal_rect(c, &mx, &my, &mw, &mh);
    draw_modal(c, mx, my, mw, mh);

    phase_pick = v.phase == 2 || v.phase == 3;
    /* phase 9: a menu opened with a callback (POKELIST_OpenMenuWithCallback:
     * the question after a Rare Candy, "forget a move?"), under its message */
    menu_open = v.phase == 6 || v.phase == 9;
    msg_open = v.phase == 7;
    if (menu_open) {
        menu = rdp(v.work, PL_MENU);
        menu_n = menu != NULL ? (int)rd8(menu, PM_COUNT) : 0;
        if (menu_n < 1 || menu_n > 8) {
            menu = NULL;
            menu_n = 0;
        }
    }
    sel_mon = (v.selected >= 0 && v.selected <= 5) ? plate_mon(&v, v.selected) : NULL;

    snprintf(right, sizeof right, "%d/6", v.count);
    ui_header(c, mx + 4, my + 4, mw - 8, "POK\xC3\x89MON", "PARTY", right, UI_SKY);

    /* The slots: two columns, three rows (the game's plate order). */
    colw = (mw - 8 * 2 - 6) / 2;
    x0 = mx + 8;
    y0 = my + 4 + 22 + 7;
    g_party_hover = -1;
    for (i = 0; i < 6; i++) {
        int col = i & 1, row = i >> 1;
        int x = x0 + col * (colw + 6), y = y0 + row * (rowh + gap);
        void *mon = plate_mon(&v, i);
        int sel = 0, st;
        EidosUiRect r;
        int hit = 0;

        r.x = x; r.y = y; r.w = colw; r.h = rowh;
        if (phase_pick && mon != NULL) {
            hit = eidos_ui_hit(r);
            if (hit & EIDOS_UI_HOVER) {
                g_party_hover = i;
                if (i != v.selected || v.touch_mode != 0) {
                    /* the game's cursor follows the mouse (and the key
                     * mode is on), so a key goes on from the Pokemon the
                     * mouse was on */
                    s32 slot = i;
                    s32 key_mode = 0;

                    memcpy((u8 *)v.work + PL_SELECTED, &slot, sizeof slot);
                    memcpy((u8 *)v.work + PL_TOUCHMODE, &key_mode, sizeof key_mode);
                }
            }
            if (hit & EIDOS_UI_CLICK) {
                click_slot = i;
            }
        }
        if (mon == NULL) {
            ui_party_row(c, x, y, colw, i, NULL, 0, 0, 0, 0, NULL, 0);
            continue;
        }
        if (v.phase == 3 && i == v.swap_src) {
            sel = 1;
        } else if (v.phase == 6 && i == v.selected) {
            sel = 1;
        } else if (v.touch_mode == 0 && i == v.selected && phase_pick && eidos_ui_keys_lit()) {
            sel = 1;
        } else if (i == g_party_hover) {
            sel = 1;
        }
        {
            char nick[40];
            u32 hp = plate_hp(&v, i, mon), maxhp = pv(mon, P_MAXHP);
            u16 species = (u16)pv(mon, P_SPECIES);
            int egg = pv(mon, P_IS_EGG) != 0;
            const UiImage *icon = ui_mon_icon(species, (u8)pv(mon, P_FORM),
                                              sel ? (int)((in->frame / 24) & 1) : 0);

            mon_nick(mon, nick, (int)sizeof nick);
            if (egg) {
                ui_party_row(c, x, y, colw, i, nick, 2, 0, 0, 0, icon, sel);
                /* an egg shows no level and no HP: cover the numbers */
                ui_fill(c, x + 40, y + 14, colw - 46, 14, sel ? UI_NAVY2 : UI_NAVY, 255);
                ui_fill(c, x + colw - 48, y + 2, 42, 12, sel ? UI_NAVY2 : UI_NAVY, 255);
            } else {
                int lvw;
                char lv[16];

                ui_party_row(c, x, y, colw, i, nick, gender_of(mon), (int)pv(mon, P_LEVEL),
                             (int)hp, (int)maxhp, icon, sel);
                st = mon_status(mon, hp);
                if (st >= 0) {
                    snprintf(lv, sizeof lv, "Lv.%d", (int)pv(mon, P_LEVEL));
                    lvw = ui_text_w(lv, UI_FONT_SMALL);
                    ui_status_badge(c, x + colw - 9 - lvw - 28, y + 3, st);
                }
                if (pv(mon, P_ITEM) != 0) {
                    /* the held item mark, at the Pokemon's feet */
                    ui_ellipse(c, x + 29, y + 19, 8, 8, UI_PINK, UI_INK, 255);
                    ui_fill(c, x + 30, y + 23, 6, 1, UI_WHITE, 255);
                }
            }
            if (v.phase == 3 && i == v.swap_src) {
                ui_text(c, x + colw - 78, y + 3, "MOVE", UI_CYAN, UI_NAVY, UI_FONT_SMALL, 1);
            }
        }
    }

    /* The message line and the menu. */
    {
        int by = y0 + 3 * (rowh + gap) + 1;
        int bh = my + mh - 6 - by;
        int msgw = mw - 16, menu_x = 0, menu_y = 0, menu_w = 0, menu_h = 0;

        if (v.switch_enabled && v.phase == 2) {
            msgw -= 52;
        }
        party_prompt(&v, sel_mon, prompt, (int)sizeof prompt);
        /* a message the game prints (after a Rare Candy, a move to learn):
         * as the battle shows its own, page by page as the game prints it */
        stream_msg = v.phase == 7 || v.phase == 9 || v.phase == 10 || v.phase == 11;
        if (menu_open && menu != NULL) {
            int ew = 104, eh = 17, n = menu_n;

            menu_w = ew + 8;
            menu_h = n * (eh + 2) + 8;
            menu_x = mx + mw - 8 - menu_w;
            menu_y = my + mh - 6 - menu_h;
            if (menu_y < y0 + 28) {
                menu_y = y0 + 28;
            }
            msgw = menu_x - (mx + 8) - 6;
        }
        if (stream_msg && bh >= 40) {
            EidosUiRect rr;

            rr.x = mx + 8; rr.y = my + mh - 6 - 40; rr.w = msgw; rr.h = 40;
            if (game_message(c, 0xFFFFFFFFu, rr.x, rr.y, rr.w, rr.h) && msg_open
                && (eidos_ui_hit(rr) & EIDOS_UI_CLICK)) {
                eidos_ui_press(BW_KEY_A, 3);
            }
        } else if (prompt[0] != '\0' && bh >= 18) {
            int ty, mh2 = bh < 30 ? bh : 30;

            ui_panel(c, mx + 8, my + mh - 6 - mh2, msgw, mh2, UI_CUT_TL | UI_CUT_BR, 5,
                     UI_BODY, UI_EDGE, 235);
            ty = ui_text_y(mh2, UI_FONT_LARGE);
            ui_text(c, mx + 8 + 8, my + mh - 6 - mh2 + ty, prompt, UI_WHITE, UI_GREY,
                    UI_FONT_LARGE, 1);
        }
        if (menu_open && menu != NULL) {
            const void *tm = rdp(menu, PM_TASKMENU);
            int cur = (tm != NULL && APP_TASKMENU_GetCursorPos != NULL)
                          ? (int)APP_TASKMENU_GetCursorPos(tm) : -1;
            int ey = menu_y + 4, ew = menu_w - 8;
            int cancel = menu_n - 1;        /* the game's cancel is the last entry */

            for (i = 0; i < menu_n; i++) {
                if (rd16(menu, PM_IDS + 2u * (u32)i) == 6) {
                    cancel = i;
                }
            }

            ui_panel(c, menu_x, menu_y, menu_w, menu_h, UI_CUT_TL | UI_CUT_BR, 6, UI_BODY2,
                     UI_SKY, 245);
            for (i = 0; i < menu_n; i++) {
                const void *e = (const u8 *)menu + PM_ENTRIES + 12u * (u32)i;
                char label[48];
                EidosUiRect rr;
                int hit, sel, ty;

                (void)strbuf_text(rdp(e, 0), label, (int)sizeof label, 0);
                rr.x = menu_x + 4; rr.y = ey; rr.w = ew; rr.h = 17;
                hit = eidos_ui_hit(rr);
                sel = (hit & EIDOS_UI_HOVER) || (i == cur && eidos_ui_keys_lit());
                if ((hit & EIDOS_UI_HOVER) && i != cur && v.stage == 1 && tm != NULL) {
                    /* the game's cursor follows the mouse, so a key goes
                     * on from the entry the mouse was on */
                    *((u8 *)tm + 0) = (u8)i;
                }
                ui_panel(c, rr.x, rr.y, rr.w, rr.h, UI_CUT_TL | UI_CUT_BR, 4,
                         sel ? UI_NAVY2 : UI_BODY, sel ? UI_SKY : UI_EDGE, 255);
                ty = ui_text_y(17, UI_FONT_LARGE);
                ui_text(c, rr.x + 8, rr.y + ty, label, UI_WHITE,
                        sel ? (s32)UI_NAVY : (s32)UI_EDGE, UI_FONT_LARGE, 1);
                if ((hit & EIDOS_UI_CLICK) && v.stage == 1) {
                    /* the game's own rectangle of entry i of n (docs 0.5) */
                    eidos_ui_tap(203, (24 - 3 * menu_n) * 8 + 24 * i + 12, 3);
                }
                if (i == cancel && eidos_ui_right_click() && v.stage == 1) {
                    eidos_ui_tap(203, (24 - 3 * menu_n) * 8 + 24 * i + 12, 3);
                }
                ey += 19;
            }
            if (click_outside(c, mx, my, mw, mh) && v.stage == 1) {
                /* a click outside the modal is B: the CANCEL entry */
                eidos_ui_tap(203, (24 - 3 * menu_n) * 8 + 24 * cancel + 12, 3);
            }
        }
    }

    /* The bottom screen's buttons as the game offers them: back (B), and the red cross that
     * leaves for the field at once (the game's "switch" command, selectedIndex 8). */
    if (phase_pick) {
        if (eidos_ui_right_click() || click_outside(c, mx, my, mw, mh)) {
            back_click = 1;
        }
        if (v.mode != 21 && back_click) {
            eidos_ui_tap(236, 180, 3);
        }
        if (click_slot < 0) {
            /* the start menu's party row: pick that Pokemon now */
            int want;

            if (eidos_ui_scene_age() >= 20 &&
                eidos_ui_take_intent(EIDOS_UI_INTENT_PARTY_PICK, &want) &&
                want >= 0 && want < 6) {
                click_slot = want;
            }
        }
        if (click_slot >= 0) {
            eidos_ui_tap(k_plate_tap[click_slot][0], k_plate_tap[click_slot][1], 3);
        }
        if (v.switch_enabled && v.phase == 2) {
            EidosUiRect rr;
            int sw = 46;

            rr.x = mx + mw - 8 - sw; rr.y = my + mh - 6 - 30 + 7; rr.w = sw; rr.h = 16;
            {
                int hit = eidos_ui_hit(rr);

                ui_panel(c, rr.x, rr.y, rr.w, rr.h, UI_CUT_TL | UI_CUT_BR, 4,
                         (hit & EIDOS_UI_HOVER) ? UI_NAVY2 : UI_BODY,
                         (hit & EIDOS_UI_HOVER) ? UI_SKY : UI_EDGE, 255);
                ui_text(c, rr.x + (sw - ui_text_w("EXIT", UI_FONT_SMALL)) / 2,
                        rr.y + ui_text_y(16, UI_FONT_SMALL), "EXIT", UI_WHITE, UI_EDGE,
                        UI_FONT_SMALL, 1);
                if (hit & EIDOS_UI_CLICK) {
                    eidos_ui_tap(212, 180, 3);
                }
            }
        }
    }
    if (msg_open && eidos_ui_right_click()) {
        eidos_ui_press(BW_KEY_A, 3);
    }
    draw_hints(c, hints, hint_keys, 3);
}

static void party_reset(void)
{
    g_party_hover = -1;
}

static const EidosUiRecipe recipe_party_list = {
    "party list", party_match, party_run, party_reset, 0
};

/* ================================================================== */
/* The Pokemon summary (overlay 131)                                   */
/* ================================================================== */

/* PStatusWork offsets (ov131/p_status_types.h) */
#define SW_PARAM       0x008
#define SW_POS         0x018
#define SW_ISEGG       0x038
#define SW_HASRIBBON   0x040
#define SW_INPUTENABLE 0x044
#define SW_SEQ         0x050
#define SW_PAGE        0x058
#define SW_INFO        0x08C
#define SW_SKILL       0x090
#define SW_RIBBON      0x094
#define SW_TEMPPP      0x258
/* PStatusProcParam */
#define SP_PPD         0x00
#define SP_CONFIG      0x08
#define SP_PPT         0x0C
#define SP_MODE        0x0D
#define SP_MAX         0x0E
#define SP_NATIONAL    0x18
#define SP_NEWMOVE     0x14
#define SP_CANUSE      0x20
/* PStaSkillWork */
#define SK_ISMOVEHELD  0x14
#define SK_CURSOR      0x29
#define SK_HELD        0x2A
#define SK_SLOTCOUNT   0x2B
/* PStaInfoWork */
#define IW_DEXTABLE    0x08
/* PStaRibbonWork */
#define RB_COUNT       0x010
#define RB_OWNED       0x138

typedef struct SummaryView {
    const void *work, *param;
    void *mon;
    int pos, max, page, mode, egg, has_ribbon, input_enabled, seq, can_use;
    int cursor, slot_count;
} SummaryView;

static int summary_read(SummaryView *s)
{
    const void *w = app_work(131, NULL, NULL);
    const void *p;
    int ppt;
    void *mon;

    if (w == NULL) {
        return 0;
    }
    p = rdp(w, SW_PARAM);
    if (p == NULL) {
        return 0;
    }
    ppt = (int)rd8(p, SP_PPT);
    s->pos = (int)rd16(w, SW_POS);
    if (ppt == 0) {
        mon = rdp(p, SP_PPD);
    } else if (ppt == 1) {
        void *party = rdp(p, SP_PPD);

        mon = party != NULL ? POKEPARTY_GetMember(party, (u32)s->pos) : NULL;
    } else {
        mon = rdp(w, SW_TEMPPP);
    }
    if (mon == NULL) {
        return 0;
    }
    s->work = w;
    s->param = p;
    s->mon = mon;
    s->max = (int)rd8(p, SP_MAX);
    s->page = rds32(w, SW_PAGE);
    s->mode = (int)rd8(p, SP_MODE);
    s->egg = (int)rd32(w, SW_ISEGG) != 0;
    s->has_ribbon = rds32(w, SW_HASRIBBON) != 0;
    s->input_enabled = rds32(w, SW_INPUTENABLE) != 0;
    s->seq = rds32(w, SW_SEQ);
    s->can_use = rds32(p, SP_CANUSE) != 0;
    s->cursor = 0xFF;
    s->slot_count = 4;
    {
        const void *sk = rdp(w, SW_SKILL);

        if (sk != NULL) {
            s->cursor = (int)rd8(sk, SK_CURSOR);
            s->slot_count = (int)rd8(sk, SK_SLOTCOUNT);
        }
    }
    return 1;
}

static int summary_match(void)
{
    SummaryView s;

    return summary_read(&s) && s.page >= 0 && s.page <= 3;
}

/* The state the recipe keeps between frames. */
static u32 g_prev_buttons;
static int g_move_hover = -1;

/* Move data from the game's move table. */
typedef struct MoveInfo {
    int type, klass, power, accuracy, always;
} MoveInfo;

static void move_info(u32 move, MoveInfo *mi)
{
    mi->type = UI_TYPE_UNKNOWN;
    mi->klass = 0;
    mi->power = 0;
    mi->accuracy = 0;
    mi->always = 0;
    if (WAZA_TOOL_GetParam == NULL || move == 0) {
        return;
    }
    mi->type = kit_type((int)WAZA_TOOL_GetParam(move, 0));
    mi->klass = (int)WAZA_TOOL_GetParam(move, 2);
    mi->power = (int)WAZA_TOOL_GetParam(move, 3);
    mi->accuracy = (int)WAZA_TOOL_GetParam(move, 4);
    mi->always = WAZA_TOOL_IsAlwaysHit != NULL ? (int)WAZA_TOOL_IsAlwaysHit((u16)move) : 0;
}

/* The game's class (0 status, 1 physical, 2 special) in the kit's order. */
static int kit_class(int k)
{
    return k == 1 ? 0 : k == 2 ? 1 : 2;
}

typedef struct WrapOut {
    EidosUiCtx *c;
    int x, y, lh, n, max, font;
    u32 color;
    s32 shadow;
} WrapOut;

static void wrap_line(void *user, const char *s, int len)
{
    WrapOut *w = (WrapOut *)user;
    char tmp[200];

    if (w->n >= w->max) {
        return;
    }
    if (len > (int)sizeof tmp - 1) {
        len = (int)sizeof tmp - 1;
    }
    memcpy(tmp, s, (size_t)len);
    tmp[len] = '\0';
    ui_text(w->c, w->x, w->y + w->n * w->lh, tmp, w->color, w->shadow, w->font, 1);
    w->n++;
}

/* Wrapped text inside a width, at most `max` lines. */
static void draw_wrapped(EidosUiCtx *c, int x, int y, int width, int lh, int max,
                         const char *text, int font, u32 color, s32 shadow)
{
    WrapOut w;

    w.c = c; w.x = x; w.y = y; w.lh = lh; w.n = 0; w.max = max; w.font = font;
    w.color = color; w.shadow = shadow;
    (void)ui_wrap(text, width, font, wrap_line, &w);
}


/* ---- the left card: name, picture, types, number, OT, item ---- */

static void draw_card(EidosUiCtx *c, const SummaryView *s, int x, int y, int w, int h,
                      const EidosUiFrame *in)
{
    char nick[40], tmp[64];
    int nx, species = (int)pv(s->mon, P_SPECIES), form = (int)pv(s->mon, P_FORM);
    const UiImage *img;
    int k, picx, picy, picw, ty, t1, t2, ry;
    int gender = gender_of(s->mon);

    ui_panel(c, x, y, w, h, UI_CUT_TL | UI_CUT_BR, 6, UI_BODY2, UI_EDGE, 255);
    mon_nick(s->mon, nick, (int)sizeof nick);
    ty = ui_text_y(16, UI_FONT_LARGE);
    nx = ui_text(c, x + 6, y + 3 + ty, nick, UI_WHITE, UI_GREY, UI_FONT_LARGE, 1);
    if (!s->egg) {
        char lv[16];

        ui_gender(c, nx + 3, y + 3 + ty, gender, UI_FONT_LARGE);
        snprintf(lv, sizeof lv, "Lv.%d", (int)pv(s->mon, P_LEVEL));
        ui_text(c, x + w - 6 - ui_text_w(lv, UI_FONT_SMALL), y + 3 + ui_text_y(16, UI_FONT_SMALL),
                lv, UI_LIGHT, UI_EDGE, UI_FONT_SMALL, 1);
    }

    /* the picture: the game's 3D model cannot be copied, so the party icon
     * grows by a whole number */
    img = ui_mon_icon((u16)species, (u8)form, (int)((in->frame / 24) & 1));
    picw = w - 12;
    k = fit_scale(img != NULL ? img->w : 32, picw);
    {
        int ih = img != NULL ? img->h : 32;
        int room_h = h - 22 - 2 - 3 - 16 - 2 * 17 - 4;     /* two rows stay */

        if (k * ih > room_h) {
            k = room_h / ih;
        }
        if (k < 1) k = 1;
    }
    picx = x + (w - (img != NULL ? img->w * k : 32)) / 2;
    picy = y + 22 + 2;
    ui_ellipse(c, x + w / 2 - 26, picy + (img != NULL ? img->h * k : 32) - 8, 52, 12,
               0x0A0A0Au, UI_NONE, 255);
    if (img != NULL) {
        ui_image(c, picx, picy, img, k);
    }
    ry = picy + (img != NULL ? img->h * k : 32) + 3;

    if (!s->egg) {
        t1 = kit_type((int)pv(s->mon, P_TYPE1));
        t2 = kit_type((int)pv(s->mon, P_TYPE2));
        if (getenv("EIDOS_UI_DEBUG") != NULL) {
            fprintf(stderr, "party: types %d %d species %d\n", t1, t2, species);
        }
        if (t2 != t1) {
            int tw = (w - 16) / 2;

            ui_type_tag(c, x + 6, ry, tw, t1);
            ui_type_tag(c, x + w - 6 - tw, ry, tw, t2);
        } else {
            ui_type_tag(c, x + (w - 60) / 2, ry, 60, t1);
        }
    }
    ry += 16;
    /* number, original trainer, held item */
    if (!s->egg) {
        u32 dex = (u32)species;
        const void *info = rdp(s->work, SW_INFO);

        if (rd32(s->param, SP_NATIONAL) == 0 && info != NULL) {
            const u16 *tab = (const u16 *)rdp(info, IW_DEXTABLE);

            if (tab != NULL && species < 650) {
                dex = tab[species];
            }
        }
        snprintf(tmp, sizeof tmp, "%03u", (unsigned)dex);
        (void)msg_text(BANK_SUMMARY, 8, nick, (int)sizeof nick, 0);
        if (ry + 17 * 3 <= y + h) {         /* the number only when all three rows fit */
            ui_info_row(c, x + 4, ry, w - 8, nick, tmp);
            ry += 17;
        }
    }
    mon_ot(s->mon, tmp, (int)sizeof tmp);
    (void)msg_text(BANK_SUMMARY, 13, nick, (int)sizeof nick, 0);
    ui_info_row(c, x + 4, ry, w - 8, nick, tmp);
    ry += 17;
    {
        u32 item = pv(s->mon, P_ITEM);

        (void)msg_text(BANK_SUMMARY, 4, nick, (int)sizeof nick, 0);
        if (item != 0) {
            (void)msg_text(BANK_ITEM, item, tmp, (int)sizeof tmp, 0);
        } else {
            (void)msg_text(BANK_SUMMARY, 6, tmp, (int)sizeof tmp, 0);
        }
        ui_info_row(c, x + 4, ry, w - 8, nick, tmp);
    }
}

/* ---- pages ---- */

static void draw_info_page(EidosUiCtx *c, const SummaryView *s, int x, int y, int w, int h)
{
    char tmp[128], lab[48], val[80];
    u32 level = pv(s->mon, P_LEVEL), exp = pv(s->mon, P_EXP);
    u32 species = pv(s->mon, P_SPECIES), form = pv(s->mon, P_FORM);
    u32 cur = 0, next = 0;
    int yy = y;

    ui_panel(c, x, y, w, h, UI_CUT_TL | UI_CUT_BR, 6, UI_BODY2, UI_EDGE, 255);
    if (s->egg) {
        draw_wrapped(c, x + 8, y + 8, w - 16, 16, 4,
                     "What will hatch from this Egg? It will take some time.",
                     UI_FONT_LARGE, UI_WHITE, UI_GREY);
        return;
    }
    yy = y + 6;
    (void)msg_text(BANK_SUMMARY, 10, lab, (int)sizeof lab, 0);
    (void)msg_text(BANK_SPECIES, species, val, (int)sizeof val, 0);
    ui_info_row(c, x + 6, yy, w - 12, lab, val);
    yy += 17;
    (void)msg_text(BANK_SUMMARY, 15, lab, (int)sizeof lab, 0);
    snprintf(val, sizeof val, "%05u", (unsigned)(pv(s->mon, P_OT_ID) & 0xFFFFu));
    ui_info_row(c, x + 6, yy, w - 12, lab, val);
    yy += 17;
    (void)msg_text(BANK_SUMMARY, 17, lab, (int)sizeof lab, 0);
    snprintf(val, sizeof val, "%u", (unsigned)exp);
    ui_info_row(c, x + 6, yy, w - 12, lab, val);
    yy += 17;
    if (POKE_TOOL_GetExpForLevel != NULL) {
        cur = POKE_TOOL_GetExpForLevel(species, form, level);
        next = POKE_TOOL_GetExpForLevel(species, form, level + 1);
    }
    (void)msg_text(BANK_SUMMARY, 19, lab, (int)sizeof lab, 0);
    snprintf(val, sizeof val, "%u", (unsigned)(next > exp ? next - exp : 0));
    ui_info_row(c, x + 6, yy, w - 12, lab, val);
    yy += 17;
    if (next > cur && level < 100) {
        ui_text(c, x + 8, yy + ui_text_y(10, UI_FONT_SMALL), "EXP", UI_SKY, UI_NAVY,
                UI_FONT_SMALL, 1);
        ui_bar(c, x + 34, yy + 3, w - 46, 5, (int)(exp > cur ? exp - cur : 0),
               (int)(next - cur));
    }
    yy += 14;
    /* nature, and where it was met */
    (void)msg_text(BANK_NATURE, pv(s->mon, P_NATURE), val, (int)sizeof val, 0);
    snprintf(tmp, sizeof tmp, "%s nature.", val);
    ui_text(c, x + 8, yy + ui_text_y(14, UI_FONT_LARGE), tmp, UI_WHITE, UI_GREY,
            UI_FONT_LARGE, 1);
    yy += 15;
    {
        u32 loc = pv(s->mon, P_MET_LOC);
        char place[64];

        (void)msg_text(BANK_PLACE, loc, place, (int)sizeof place, 0);
        if (place[0] != '\0') {
            snprintf(tmp, sizeof tmp, "Met at %s, Lv. %u.", place, (unsigned)pv(s->mon, P_MET_LEVEL));
            draw_wrapped(c, x + 8, yy + 2, w - 16, 14, 2, tmp, UI_FONT_MEDIUM, UI_LIGHT, UI_EDGE);
        }
    }
}

static const int k_slot_tap_y[5] = { 32, 64, 96, 128, 176 };

static void draw_moves(EidosUiCtx *c, const SummaryView *s, int x, int y, int w, int h,
                       int rowh, const EidosUiFrame *in)
{
    int i, n = s->slot_count > 4 ? 5 : 4;
    int shown_move = 0, hov = -1, yy = y;
    MoveInfo mi;
    char name[64], d[300], tmp[32];
    int detail_y;
    u32 newmove = rd16(s->param, SP_NEWMOVE);

    g_move_hover = -1;
    for (i = 0; i < n; i++) {
        u32 mv = i < 4 ? pv(s->mon, P_MOVE1 + (u32)i) : newmove;
        EidosUiRect r;
        int hit, sel;

        r.x = x; r.y = yy; r.w = w; r.h = rowh;
        sel = (!s->input_enabled && s->cursor == i);
        if (mv != 0) {
            MoveInfo m2;

            hit = eidos_ui_hit(r);
            if (hit & EIDOS_UI_HOVER) {
                hov = i;
            }
            if ((hit & EIDOS_UI_CLICK) && s->seq == 4) {
                eidos_ui_tap(76, k_slot_tap_y[i], 3);
            }
            move_info(mv, &m2);
            (void)msg_text(BANK_MOVE, mv, name, (int)sizeof name, 0);
            if (ui_move_name_modern((int)mv) != NULL) {
                snprintf(name, sizeof name, "%s", ui_move_name_modern((int)mv));
            }
            ui_move_row(c, x, yy, w, m2.type, name,
                        i < 4 ? (int)pv(s->mon, P_PP1 + (u32)i) : 0,
                        i < 4 ? (int)pv(s->mon, P_MAXPP1 + (u32)i) : 0, sel || hov == i);
            if (i == 4) {
                ui_text(c, x + w - 44, yy + ui_text_y(rowh, UI_FONT_SMALL), "NEW", UI_CYAN,
                        UI_NAVY, UI_FONT_SMALL, 1);
            }
        } else {
            ui_move_row(c, x, yy, w, 0, NULL, 0, 0, 0);
        }
        yy += rowh + 3;
    }
    g_move_hover = hov;

    /* the move under the pointer, else the chosen one, else the first */
    if (hov >= 0) {
        shown_move = hov;
    } else if (!s->input_enabled && s->cursor < 5) {
        shown_move = s->cursor;
    }
    {
        u32 mv = shown_move < 4 ? pv(s->mon, P_MOVE1 + (u32)shown_move) : newmove;

        detail_y = yy + 1;
        if (mv == 0 || detail_y + 30 > y + h) {
            return;
        }
        move_info(mv, &mi);
        ui_panel(c, x, detail_y, w, y + h - detail_y, UI_CUT_TL | UI_CUT_BR, 5, UI_BODY2,
                 UI_EDGE, 255);
        ui_category_icon(c, x + 5, detail_y + 4, kit_class(mi.klass));
        (void)msg_text(BANK_SUMMARY, 147, tmp, (int)sizeof tmp, 0);
        snprintf(name, sizeof name, "%s %s", tmp, mi.power <= 1 ? "--" : "");
        if (mi.power > 1) {
            snprintf(name, sizeof name, "%s %d", tmp, mi.power);
        }
        ui_text(c, x + 25, detail_y + 3 + ui_text_y(11, UI_FONT_SMALL), name, UI_WHITE,
                UI_EDGE, UI_FONT_SMALL, 1);
        (void)msg_text(BANK_SUMMARY, 148, tmp, (int)sizeof tmp, 0);
        if (mi.accuracy == 0 || mi.always) {
            snprintf(name, sizeof name, "%s --", tmp);
        } else {
            snprintf(name, sizeof name, "%s %d", tmp, mi.accuracy);
        }
        ui_text(c, x + w - 6 - ui_text_w(name, UI_FONT_SMALL),
                detail_y + 3 + ui_text_y(11, UI_FONT_SMALL), name, UI_WHITE, UI_EDGE,
                UI_FONT_SMALL, 1);
        {
            int ty = detail_y + 17, lines = (y + h - ty - 2) / 11;

            (void)msg_text(BANK_MOVE_DESC, mv, d, (int)sizeof d, 0);
            if (lines > 0) {
                draw_wrapped(c, x + 5, ty, w - 10, 11, lines, d, UI_FONT_SMALL, UI_LIGHT, UI_EDGE);
            }
        }
    }
    (void)in;
}

static void draw_skills_page(EidosUiCtx *c, const SummaryView *s, int x, int y, int w, int h,
                             const EidosUiFrame *in)
{
    static const u32 stat_id[5] = { P_ATK, P_DEF, P_SPA, P_SPD, P_SPE };
    int colw = w * 40 / 100, i, yy = y, maxstat = 1;
    u32 hp = pv(s->mon, P_HP), maxhp = pv(s->mon, P_MAXHP);
    char lab[32], val[32], d[300], ab[64];
    u32 ability;
    int abh;

    /* HP */
    ui_panel(c, x, yy, colw, 17, UI_CUT_TL | UI_CUT_BR, 4, UI_BODY2, UI_EDGE, 255);
    ui_text(c, x + 6, yy + ui_text_y(17, UI_FONT_LARGE), "HP", UI_LIME, UI_DEX3, UI_FONT_LARGE, 1);
    snprintf(val, sizeof val, "%u/%u", (unsigned)hp, (unsigned)maxhp);
    ui_bar(c, x + 26, yy + 6, colw - 32 - ui_text_w(val, UI_FONT_SMALL) - 4, 5, (int)hp, (int)maxhp);
    ui_text(c, x + colw - 6 - ui_text_w(val, UI_FONT_SMALL), yy + ui_text_y(17, UI_FONT_SMALL),
            val, UI_LIGHT, UI_EDGE, UI_FONT_SMALL, 1);
    yy += 19;
    for (i = 0; i < 5; i++) {
        if ((int)pv(s->mon, stat_id[i]) > maxstat) {
            maxstat = (int)pv(s->mon, stat_id[i]);
        }
    }
    for (i = 0; i < 5; i++) {
        u32 v = pv(s->mon, stat_id[i]);

        (void)msg_text(BANK_SUMMARY, 111 + (u32)i, lab, (int)sizeof lab, 0);
        ui_stat_row(c, x, yy, colw, lab, (int)v, (int)v, maxstat * 3 / 2, UI_SKY);
        yy += 17;
    }
    /* the ability, with its description */
    ability = pv(s->mon, P_ABILITY);
    abh = y + h - yy;
    if (abh >= 22) {
        ui_panel(c, x, yy, colw, abh, UI_CUT_TL | UI_CUT_BR, 5, UI_BODY2, UI_EDGE, 255);
        (void)msg_text(BANK_ABILITY, ability, ab, (int)sizeof ab, 0);
        ui_text(c, x + 6, yy + 2 + ui_text_y(13, UI_FONT_LARGE), ab, UI_WHITE, UI_GREY,
                UI_FONT_LARGE, 1);
        (void)msg_text(BANK_ABILITY_D, ability, d, (int)sizeof d, 0);
        if (abh >= 34) {
            draw_wrapped(c, x + 6, yy + 17, colw - 12, 11, (abh - 19) / 11, d, UI_FONT_SMALL,
                         UI_LIGHT, UI_EDGE);
        }
    }
    draw_moves(c, s, x + colw + 6, y, w - colw - 6, h, 19, in);
}

static void draw_forget_page(EidosUiCtx *c, const SummaryView *s, int x, int y, int w, int h,
                             const EidosUiFrame *in)
{
    /* choose a move to forget: the game's page 3, moves only */
    draw_moves(c, s, x, y, w, h, 19, in);
}

/* The ribbon table (ov131 data, 16 byte rows: parameter id, file, anim,
 * name message, description message, selector). */
#define RIBBON_TABLE_ADDR 0x021DC2F8u
#define RIBBON_ROWS 80

static const u8 *ribbon_table(void)
{
    const u8 *t = (const u8 *)bw_ds_try_ptr(RIBBON_TABLE_ADDR);
    int i;

    if (t == NULL) {
        return NULL;
    }
    /* a sanity check: every row names messages inside the ribbon bank */
    for (i = 0; i < RIBBON_ROWS; i += 9) {
        if (rd16(t, 16u * (u32)i + 8) > 200 || rd16(t, 16u * (u32)i + 10) > 200 ||
            rd32(t, 16u * (u32)i) > 0xB1) {
            return NULL;
        }
    }
    return t;
}

static int g_ribbon_sel;
static int g_ribbon_scroll;

static void draw_ribbon_page(EidosUiCtx *c, const SummaryView *s, int x, int y, int w, int h,
                             const EidosUiFrame *in)
{
    const void *rw = rdp(s->work, SW_RIBBON);
    const u8 *tab = ribbon_table();
    int n = rw != NULL ? (int)rd8(rw, RB_COUNT) : 0, i, rows, rowh = 17, listw;
    char name[80], d[300];
    EidosUiRect lr;

    ui_panel(c, x, y, w, h, UI_CUT_TL | UI_CUT_BR, 6, UI_BODY2, UI_EDGE, 255);
    if (n <= 0 || tab == NULL) {
        ui_text(c, x + 8, y + 8, "No ribbons.", UI_GREY, UI_EDGE, UI_FONT_LARGE, 1);
        return;
    }
    if (n > 81) n = 81;
    listw = w * 55 / 100;
    rows = (h - 8) / (rowh + 1);
    lr.x = x; lr.y = y; lr.w = listw; lr.h = h;
    {
        int wheel = eidos_ui_wheel(lr);

        g_ribbon_scroll -= wheel;
    }
    if (g_ribbon_sel >= n) g_ribbon_sel = 0;
    if (g_ribbon_sel < g_ribbon_scroll) g_ribbon_scroll = g_ribbon_sel;
    if (g_ribbon_sel >= g_ribbon_scroll + rows) g_ribbon_scroll = g_ribbon_sel - rows + 1;
    if (g_ribbon_scroll > n - rows) g_ribbon_scroll = n - rows;
    if (g_ribbon_scroll < 0) g_ribbon_scroll = 0;
    for (i = 0; i < rows && g_ribbon_scroll + i < n; i++) {
        int idx = g_ribbon_scroll + i;
        u32 no = rd8(rw, RB_OWNED + 8u * (u32)idx + 4);
        EidosUiRect r;
        int hit;

        r.x = x + 4; r.y = y + 4 + i * (rowh + 1); r.w = listw - 12; r.h = rowh;
        hit = eidos_ui_hit(r);
        if (hit & EIDOS_UI_CLICK) {
            g_ribbon_sel = idx;
        }
        ui_row(c, r.x, r.y, r.w, r.h, idx == g_ribbon_sel, (hit & EIDOS_UI_HOVER) != 0,
               &UI_THEME_PARTY);
        if (no < RIBBON_ROWS) {
            (void)msg_text(BANK_RIBBON, rd16(tab, 16u * no + 8), name, (int)sizeof name, 0);
            ui_text(c, r.x + 7, r.y + ui_text_y(rowh, UI_FONT_LARGE), name, UI_WHITE, UI_EDGE,
                    UI_FONT_LARGE, 1);
        }
    }
    ui_scrollbar(c, x + listw - 6, y + 4, h - 8, g_ribbon_scroll, rows, n, UI_SKY);
    {
        u32 no = rd8(rw, RB_OWNED + 8u * (u32)g_ribbon_sel + 4);

        if (no < RIBBON_ROWS) {
            int dx = x + listw + 6, dw = w - listw - 12;

            (void)msg_text(BANK_RIBBON, rd16(tab, 16u * no + 8), name, (int)sizeof name, 0);
            draw_wrapped(c, dx, y + 8, dw, 15, 2, name, UI_FONT_LARGE, UI_WHITE, UI_GREY);
            (void)msg_text(BANK_RIBBON, rd16(tab, 16u * no + 10), d, (int)sizeof d, 0);
            draw_wrapped(c, dx, y + 42, dw, 12, (h - 46) / 12, d, UI_FONT_MEDIUM, UI_LIGHT,
                         UI_EDGE);
        }
    }
    (void)in;
}

/* Choosing a move to forget for a new one (the summary's mode 2), laid out
 * as the battle's forget page (recipe_battle.c, pl_summary): the four moves
 * and the new one under them, the details of one, FORGET and BACK.  The
 * game's own page takes two steps (P_STA_SKILL_HandleKeysForget and
 * ...TouchForget): a move is picked (PStaSkillWork.isPickingForget, its slot
 * heldSlot, the cursor then on the new move), and A, or a touch on the new
 * move, confirms.  So a click on a move picks it (a touch of its slot), its
 * details show with FORGET, which is A; BACK and B put it back.  Before a
 * move is picked, B gives the new move up, as on the DS. */
#define SK_PICKING     0x18
#define SK_CONFIRMED   0x20

static int g_forget_next = -1;  /* a move to pick once the picked one is put back */

static int move_pp_max(u32 mv)
{
    return WAZA_TOOL_GetParam != NULL ? (int)WAZA_TOOL_GetParam(mv, 5) : 0;
}

static void forget_view(EidosUiCtx *c, const SummaryView *s, int mx, int my, int mw, int mh)
{
    const void *sk = rdp(s->work, SW_SKILL);
    int picking = sk != NULL && rds32(sk, SK_PICKING) != 0;
    int held = sk != NULL ? (int)rd8(sk, SK_HELD) : 5;
    int busy = sk == NULL || rds32(sk, SK_CONFIRMED) != 0 || s->seq != 4;
    int ix = mx + 8, iy = my + 4 + 22 + 6, iw = mw - 16;
    int nav_y = my + mh - 6 - 18, i, hov = -1, shown, st;
    int mw2 = (iw - 24) / 2;
    u32 newmove = rd16(s->param, SP_NEWMOVE);
    char nick[40], q[48];
    static const char *const hints_pick[] = { "A", "Forget", "B", "Back" };
    static const char *const hints_list[] = { "A", "Choose", "B", "Give up" };
    static const u32 keys[] = { BW_KEY_A, BW_KEY_B };

    if (held > 3) {
        picking = 0;
    }
    /* a move clicked while another was picked: once that one is back */
    if (g_forget_next >= 0 && !picking && !busy) {
        eidos_ui_tap(76, k_slot_tap_y[g_forget_next], 3);
        g_forget_next = -1;
    }
    ui_panel(c, ix, iy, iw, nav_y - iy - 4, UI_CUT_TL | UI_CUT_BR, 5, UI_NAVY, 0x084A73u, 245);
    {
        const UiImage *icon = ui_mon_icon((u16)pv(s->mon, P_SPECIES), (u8)pv(s->mon, P_FORM), 0);
        u32 hp = pv(s->mon, P_HP), maxhp = pv(s->mon, P_MAXHP);
        int nx;

        if (icon != NULL) {
            ui_image(c, ix + 8, iy + 6, icon, 1);
        }
        mon_nick(s->mon, nick, (int)sizeof nick);
        nx = ui_text(c, ix + 38, iy + 6, nick, UI_WHITE, UI_EDGE, UI_FONT_LARGE, 1);
        ui_gender(c, nx + 3, iy + 6, gender_of(s->mon), UI_FONT_LARGE);
        snprintf(q, sizeof q, "Lv.%d", (int)pv(s->mon, P_LEVEL));
        ui_text(c, ix + iw - 8 - ui_text_w(q, UI_FONT_LARGE), iy + 6, q, UI_LIGHT, UI_EDGE,
                UI_FONT_LARGE, 1);
        ui_bar(c, ix + 38, iy + 24, iw - 100, 4, (int)hp, (int)maxhp);
        snprintf(q, sizeof q, "%u/%u", (unsigned)hp, (unsigned)maxhp);
        ui_text(c, ix + iw - 8 - ui_text_w(q, UI_FONT_SMALL), iy + 20, q, UI_LIGHT, UI_EDGE,
                UI_FONT_SMALL, 1);
    }
    for (i = 0; i < 5; i++) {
        u32 mv = i < 4 ? pv(s->mon, P_MOVE1 + (u32)i) : newmove;
        int x2 = i < 4 ? ix + 8 + (i % 2) * (mw2 + 8) : ix + 8 + (iw - 16 - mw2) / 2;
        int y2 = iy + 34 + (i / 2) * 26;
        EidosUiRect r;
        MoveInfo m;
        char name[64];
        int hit, sel;

        if (mv == 0) {
            ui_move_row(c, x2, y2, mw2, 0, NULL, 0, 0, 0);
            continue;
        }
        r.x = x2; r.y = y2; r.w = mw2; r.h = 22;
        hit = busy ? 0 : eidos_ui_hit(r);
        if (hit & EIDOS_UI_HOVER) {
            hov = i;
        }
        if (hit & EIDOS_UI_CLICK) {
            if (i == 4) {
                if (picking) {
                    eidos_ui_press(BW_KEY_B, 3);        /* the new move stays: back */
                }
            } else if (!picking) {
                eidos_ui_tap(76, k_slot_tap_y[i], 3);
            } else if (i != held) {
                eidos_ui_press(BW_KEY_B, 3);
                g_forget_next = i;
            }
        }
        move_info(mv, &m);
        (void)msg_text(BANK_MOVE, mv, name, (int)sizeof name, 0);
        if (ui_move_name_modern((int)mv) != NULL) {
            snprintf(name, sizeof name, "%s", ui_move_name_modern((int)mv));
        }
        sel = (picking && i == held) || hov == i ||
              (!picking && s->cursor == i && eidos_ui_keys_lit());
        ui_move_row(c, x2, y2, mw2, m.type, name,
                    i < 4 ? (int)pv(s->mon, P_PP1 + (u32)i) : move_pp_max(mv),
                    i < 4 ? (int)pv(s->mon, P_MAXPP1 + (u32)i) : move_pp_max(mv), sel);
        if (i == 4) {
            ui_text(c, x2 + mw2 + 8, y2 + ui_text_y(22, UI_FONT_SMALL), "NEW", UI_CYAN, UI_NAVY,
                    UI_FONT_SMALL, 1);
        }
    }
    /* the details: the move under the pointer, else the picked one, else
     * the one under the game's cursor */
    shown = hov >= 0 ? hov : picking ? held : (s->cursor < 5 ? s->cursor : 4);
    {
        u32 mv = shown < 4 ? pv(s->mon, P_MOVE1 + (u32)shown) : newmove;
        int dy = iy + 34 + 3 * 26, dh = nav_y - 4 - 4 - dy;
        MoveInfo m;
        char lab[24], info[48], d[300];

        if (mv != 0 && dh >= 24) {
            move_info(mv, &m);
            ui_panel(c, ix + 8, dy, iw - 16, dh, UI_CUT_TL | UI_CUT_BR, 5, UI_BODY2, UI_EDGE, 255);
            ui_category_icon(c, ix + 13, dy + 4, kit_class(m.klass));
            (void)msg_text(BANK_SUMMARY, 147, lab, (int)sizeof lab, 0);
            if (m.power > 1) {
                snprintf(info, sizeof info, "%s %d", lab, m.power);
            } else {
                snprintf(info, sizeof info, "%s --", lab);
            }
            ui_text(c, ix + 33, dy + 3 + ui_text_y(11, UI_FONT_SMALL), info, UI_WHITE, UI_EDGE,
                    UI_FONT_SMALL, 1);
            (void)msg_text(BANK_SUMMARY, 148, lab, (int)sizeof lab, 0);
            if (m.accuracy == 0 || m.always) {
                snprintf(info, sizeof info, "%s --", lab);
            } else {
                snprintf(info, sizeof info, "%s %d", lab, m.accuracy);
            }
            ui_text(c, ix + iw - 14 - ui_text_w(info, UI_FONT_SMALL), dy + 3 + ui_text_y(11, UI_FONT_SMALL),
                    info, UI_WHITE, UI_EDGE, UI_FONT_SMALL, 1);
            (void)msg_text(BANK_MOVE_DESC, mv, d, (int)sizeof d, 0);
            draw_wrapped(c, ix + 13, dy + 16, iw - 26, 10, (dh - 17) / 10, d, UI_FONT_SMALL, UI_LIGHT,
                         UI_EDGE);
        }
    }
    /* FORGET while a move is picked; BACK puts it back, or GIVE UP gives
     * the new move up when none is */
    if (picking) {
        EidosUiRect r = { ix, nav_y, 140, 18 };

        st = busy ? 0 : eidos_ui_hit(r);
        ui_cmd_button(c, r.x, r.y, r.w, r.h, "FORGET", UI_PINK, (st & EIDOS_UI_HOVER) != 0);
        if (st & EIDOS_UI_CLICK) {
            eidos_ui_press(BW_KEY_A, 3);
        }
    }
    {
        int bw = picking ? 54 : 80;
        EidosUiRect r = { ix + iw - bw, nav_y, bw, 18 };

        st = busy ? 0 : eidos_ui_hit(r);
        ui_cmd_button(c, r.x, r.y, r.w, r.h, picking ? "BACK" : "GIVE UP", UI_GREY,
                      (st & EIDOS_UI_HOVER) != 0);
        if (((st & EIDOS_UI_CLICK) || eidos_ui_right_click()) && !busy) {
            eidos_ui_press(BW_KEY_B, 3);
        }
    }
    draw_hints(c, picking ? hints_pick : hints_list, keys, 2);
}

/* The Pokemon after `dir` that the game would step to (it skips empty
 * entries and, on the ribbon page, Pokemon without ribbons); the screen's
 * own up and down handle it, so the recipe only presses the keys. */

static void summary_run(EidosUiCtx *c, EidosUiView *view, const EidosUiFrame *in)
{
    SummaryView s;
    int mx, my, mw, mh, cx, cy, cw, ch, cardw, tabx, taby;
    int page_idx;
    char right[24], sub[24];
    static const char *const hints_n[] = {
        "L R", "Page", ARROWS, "Pok\xC3\xA9mon", "B", "Back"
    };
    static const u32 keys_n[] = { BW_KEY_RIGHT, BW_KEY_DOWN, BW_KEY_B };
    int back = 0;

    view->mode = EIDOS_VIEW_WORLD;
    view->blur = 1;
    view->hold_world = 1;     /* an app: the field is not running */
    view->hide_2d = 1;
    view->zoom = 1.0f;

    if (!summary_read(&s)) {
        return;
    }
    /* L and R turn the pages (the game's own keys for it are left and right) */
    {
        BwInputState st;
        u32 now, edge;

        bw_input_current(&st);
        now = in->buttons | st.buttons;
        edge = now & ~g_prev_buttons;
        g_prev_buttons = now;
        if (s.seq == 4 && s.input_enabled && s.mode != 2) {
            if (edge & BW_KEY_R) eidos_ui_press(BW_KEY_RIGHT, 2);
            if (edge & BW_KEY_L) eidos_ui_press(BW_KEY_LEFT, 2);
        }
    }

    modal_rect(c, &mx, &my, &mw, &mh);
    draw_modal(c, mx, my, mw, mh);

    page_idx = s.page;
    snprintf(sub, sizeof sub, "%s",
             s.mode == 2 ? "FORGET A MOVE" : page_idx == 0 ? "INFO" : page_idx == 1 ? "SKILLS" :
             page_idx == 2 ? "RIBBONS" : "MOVES");
    right[0] = '\0';
    if (s.max > 1) {
        snprintf(right, sizeof right, "%d/%d", s.pos + 1, s.max);
    }
    ui_header(c, mx + 4, my + 4, mw - 8, "SUMMARY", sub, "", UI_SKY);
    if (s.mode == 2) {
        forget_view(c, &s, mx, my, mw, mh);
        return;
    }

    /* the Pokemon arrows, on the title bar */
    if (s.max > 1 && s.mode != 2) {
        int ax = mx + mw - 8 - 44, ay = my + 4 + 3;
        EidosUiRect rp, rn;
        int hp, hn;

        rp.x = ax; rp.y = ay; rp.w = 20; rp.h = 15;
        rn.x = ax + 22; rn.y = ay; rn.w = 20; rn.h = 15;
        hp = eidos_ui_hit(rp);
        hn = eidos_ui_hit(rn);
        ui_fill(c, rp.x, rp.y, rp.w, rp.h, (hp & EIDOS_UI_HOVER) ? UI_EDGE : UI_BODY2, 255);
        ui_fill(c, rn.x, rn.y, rn.w, rn.h, (hn & EIDOS_UI_HOVER) ? UI_EDGE : UI_BODY2, 255);
        ui_text(c, rp.x + 5, rp.y + ui_text_y(15, UI_FONT_SMALL), "\xE2\x86\x91", UI_WHITE,
                UI_EDGE, UI_FONT_SMALL, 1);
        ui_text(c, rn.x + 5, rn.y + ui_text_y(15, UI_FONT_SMALL), "\xE2\x86\x93", UI_WHITE,
                UI_EDGE, UI_FONT_SMALL, 1);
        ui_text(c, ax - 6 - ui_text_w(right, UI_FONT_LARGE), my + 4 + ui_text_y(19, UI_FONT_LARGE),
                right, UI_LIGHT, UI_EDGE, UI_FONT_LARGE, 1);
        if (s.seq == 4 && s.input_enabled && (hp & EIDOS_UI_CLICK)) eidos_ui_tap(156, 180, 3);
        if (s.seq == 4 && s.input_enabled && (hn & EIDOS_UI_CLICK)) eidos_ui_tap(180, 180, 3);
        if (s.seq == 4 && s.input_enabled && s.page != 2 && eidos_ui_wheel((EidosUiRect){ mx, my, mw, mh }) != 0) {
            /* the wheel steps through the Pokemon */
            int wh = eidos_ui_wheel((EidosUiRect){ mx, my, mw, mh });

            if (wh > 0) eidos_ui_tap(156, 180, 3);
            else if (wh < 0) eidos_ui_tap(180, 180, 3);
        }
    }

    /* the tabs: the game's three pages, shown as it offers them */
    tabx = mx + 8;
    taby = my + 4 + 22 + 3;
    if (s.mode != 2) {
        static const char *const names[3] = { "INFO", "SKILLS", "RIBBONS" };
        static const int tap_x[3] = { 20, 60, 100 };
        int t;

        for (t = 0; t < 3; t++) {
            int tw = ui_text_w(names[t], UI_FONT_SMALL) + 16;
            EidosUiRect r;
            int hit, avail = 1;

            if (t == 1 && s.egg) avail = 0;
            if (t == 2 && (s.egg || !s.has_ribbon)) avail = 0;
            if (!avail) {
                continue;
            }
            r.x = tabx; r.y = taby; r.w = tw; r.h = 14;
            hit = eidos_ui_hit(r);
            ui_tab(c, tabx, taby, names[t], t == s.page || (hit & EIDOS_UI_HOVER),
                   t == s.page ? UI_SKY : UI_EDGE2, t == s.page ? UI_NAVY2 : UI_NAVY);
            if ((hit & EIDOS_UI_CLICK) && s.seq == 4 && s.input_enabled && t != s.page) {
                eidos_ui_tap(tap_x[t], 180, 3);
            }
            tabx += tw + 3;
        }
    }

    /* the content */
    cx = mx + 8;
    cy = my + 4 + 22 + 3 + 14 + 4;
    cw = mw - 16;
    ch = my + mh - 6 - cy;
    cardw = cw * 28 / 100;
    if (cardw < 100) cardw = 100;
    if (cardw > 130) cardw = 130;
    draw_card(c, &s, cx, cy, cardw, ch, in);
    {
        int px = cx + cardw + 6, pw = cw - cardw - 6;

        if (s.page == 0) {
            draw_info_page(c, &s, px, cy, pw, ch);
        } else if (s.page == 1) {
            draw_skills_page(c, &s, px, cy, pw, ch, in);
        } else if (s.page == 2) {
            draw_ribbon_page(c, &s, px, cy, pw, ch, in);
        } else {
            draw_forget_page(c, &s, px, cy, pw, ch, in);
        }
    }

    /* B: a right click, or a click outside the modal */
    if (eidos_ui_right_click() || click_outside(c, mx, my, mw, mh)) {
        back = 1;
    }
    if (back && s.seq == 4) {
        eidos_ui_tap(244, 180, 3);
    }
    draw_hints(c, hints_n, keys_n, 3);
}

static void summary_reset(void)
{
    g_prev_buttons = 0;
    g_ribbon_sel = 0;
    g_ribbon_scroll = 0;
    g_move_hover = -1;
    g_forget_next = -1;
}

static const EidosUiRecipe recipe_summary = {
    "pokemon summary", summary_match, summary_run, summary_reset, 0
};

/* ------------------------------------------------------------------ */
/* The evolution scene (overlay 195, SHINKA_DEMO)                      */
/* ------------------------------------------------------------------ */

/* The game draws it on the top screen alone (the Pokemon on its glowing
 * stage, the message box under it): the window shows the scene across its
 * width, as a battle, with the message in the interface's box.  A click is
 * A (the messages go on), a right click B (the game's own: it stops the
 * evolution). */
static int evolution_match(void)
{
    return bw_overlay_is_loaded(EVO_OVERLAY);
}

/* recipe_battle.c: the battle's party list, opened by the evolution scene */
int eidos_ui_plist_outside(EidosUiCtx *c, EidosUiView *view, const EidosUiFrame *in, void *sys,
                           const void *host);

/* YesNoMenu (ov067): its step and the answer under the key cursor */
#define YN_STATE        0x98
#define YN_CURSOR       0xC0

/* The scene's question when the new form learns a move with four known
 * (state 22: "Should a move be deleted...?"): the game's two buttons on
 * the bottom screen (YESNO_MENU, yes over rows 0 to 87, no under them) as
 * two buttons over the message box.  The keys work as on the DS. */
static void evolution_yesno(EidosUiCtx *c, const void *sc, int box_x, int box_y, int box_w)
{
    const void *yn = rdp(sc, SC_YESNO);
    int i, bw = 150, bh = 18;
    char label[48];

    if (yn == NULL || rds32(yn, YN_STATE) != 2) {
        return;
    }
    for (i = 0; i < 2; i++) {
        EidosUiRect r;
        int st, sel;

        r.x = box_x + box_w - bw;
        r.y = box_y - 6 - (2 - i) * (bh + 4);
        r.w = bw;
        r.h = bh;
        (void)strbuf_text(rdp(sc, i == 0 ? SC_STRBUF_YES : SC_STRBUF_NO), label, (int)sizeof label, 0);
        st = eidos_ui_hit(r);
        sel = (st & EIDOS_UI_HOVER) != 0 || (eidos_ui_keys_lit() && rds32(yn, YN_CURSOR) == i);
        ui_cmd_button(c, r.x, r.y, r.w, r.h, label, i == 0 ? UI_PINK : UI_GREY, sel);
        if (st & EIDOS_UI_CLICK) {
            eidos_ui_tap(128, i == 0 ? 44 : 140, 3);
        }
    }
}

/* engine/platform/text_instant.c: the window the game printed into last */
struct BmpWin;
struct BmpWin *eidos_text_last_window(u32 *count);
extern void *BMP_WIN_GetBmp(void *win) __attribute__((weak));
extern u8 BMP_WIN_GetBgLayer(void *win) __attribute__((weak));

/* A pixel of a 4-bit tiled message bitmap. */
static int evo_px(const u8 *px, int bw, int x, int y)
{
    u8 v = px[((y >> 3) * (bw >> 3) + (x >> 3)) * 32 + (y & 7) * 4 + ((x & 7) >> 1)];

    return (x & 1) != 0 ? v >> 4 : v & 15;
}

/* The game's message as the battle shows its own (recipe_battle.c,
 * draw_battle_message): the window the game printed a message into last,
 * its letters at the DS size in the interface's box.  The window is read as
 * the game draws it, so a message comes page by page and letter by letter
 * as on the DS.  Only while the game shows it (its BG on: DISPCNT bits 8 to
 * 11 of the screen the window is on; the text stays in the bitmap after the
 * box is hidden), and only a window printed into after `since`.  Returns
 * whether a message is on show. */
static int game_message(EidosUiCtx *c, u32 since, int x, int y, int w, int h)
{
    u32 count;
    void *win = eidos_text_last_window(&count);
    const u8 *bmp, *px;
    int bw, bh, cnt[16] = { 0 }, ground = 0, ink = -1, xx, yy, i;

    if (win == NULL || count == since || BMP_WIN_GetBmp == NULL || BMP_WIN_GetBgLayer == NULL) {
        return 0;
    }
    {
        u32 layer = BMP_WIN_GetBgLayer(win);
        u32 dispcnt = bw_io_read32(layer < 4u ? 0x04000000u : 0x04001000u);

        if ((dispcnt & (0x100u << (layer & 3u))) == 0) {
            return 0;
        }
    }
    bmp = (const u8 *)BMP_WIN_GetBmp(win);
    if (bmp == NULL || (px = rdp(bmp, 0x00)) == NULL) {
        return 0;
    }
    bw = (int)rd16(bmp, 0x04);
    bh = (int)rd16(bmp, 0x06);
    if (bw <= 0 || bh <= 0 || bw > 256 || bh > 64) {
        return 0;
    }
    for (yy = 0; yy < bh; yy++) {
        for (xx = 0; xx < bw; xx++) {
            cnt[evo_px(px, bw, xx, yy)]++;
        }
    }
    for (i = 1; i < 16; i++) {
        if (cnt[i] > cnt[ground]) {
            ground = i;
        }
    }
    for (i = 0; i < 16; i++) {
        if (i != ground && cnt[i] > 0 && (ink < 0 || cnt[i] > cnt[ink])) {
            ink = i;                    /* the letters; the rest is their shadow */
        }
    }
    if (ink < 0) {
        return 0;                       /* an empty window: no message */
    }
    ui_panel(c, x, y, w, h, UI_CUT_TL | UI_CUT_BR, 6, UI_BODY, UI_EDGE, 225);
    for (yy = 0; yy < bh && yy + 4 < h; yy++) {
        for (xx = 0; xx < bw && xx + 10 < w; xx++) {
            int v = evo_px(px, bw, xx, yy);

            if (v != ground) {
                ui_fill(c, x + 10 + xx, y + (h - bh) / 2 + yy, 1, 1, v == ink ? UI_WHITE : UI_GREY, 255);
            }
        }
    }
    return 1;
}

static u32 g_evo_since;
static int g_evo_seen;

static void evolution_reset(void)
{
    g_evo_seen = 0;
}

static void evolution_run(EidosUiCtx *c, EidosUiView *view, const EidosUiFrame *in)
{
    EidosUiRect all;
    const void *sc = evo_scene();

    /* When the evolution teaches a move the game asks on the bottom screen:
     * FORGET or KEEP OLD MOVES (drawn here), then the moves, in the battle's
     * party list after a battle (its page, below) or in the summary after a
     * Rare Candy (the summary's recipe). */
    if (sc != NULL && rds32(sc, SC_STATE) == 28 &&
        eidos_ui_plist_outside(c, view, in, rdp(sc, SC_TCB), (const u8 *)sc + SC_PLISTHOST)) {
        view->hide_2d = 1;          /* the scene's message box under it */
        return;
    }
    /* the scene across the window, as a battle: the game's message box
     * (its 2D) is not shown, its message is in the interface's box, where
     * the battle has its own */
    view->mode = EIDOS_VIEW_WORLD;
    view->world_screen = 0;
    view->hide_2d = 1;
    if (!g_evo_seen) {
        (void)eidos_text_last_window(&g_evo_since);  /* printed before the scene: not its */
        g_evo_seen = 1;
    }
    {
        int cw = c->canvas->w, ch = c->canvas->h, bw = cw * 2 / 3;
        static const char *const hints[] = { "A", "Next" };
        static const u32 keys[] = { BW_KEY_A };
        static const char *const hints_yn[] = { "A", "Choose", ARROWS, "Move" };
        static const u32 keys_yn[] = { BW_KEY_A, 0 };
        int state = sc != NULL ? rds32(sc, SC_STATE) : 0;

        /* not while the list opens and closes (states 27 to 30): the
         * question stays in the window, under the game's fade */
        if (state < 27 || state > 30) {
            (void)game_message(c, g_evo_since, (cw - bw) / 2, ch - FOOTER_H - 6 - 40, bw, 40);
        }
        if (state == 22) {
            evolution_yesno(c, sc, (cw - bw) / 2, ch - FOOTER_H - 6 - 40, bw);
            draw_hints(c, hints_yn, keys_yn, 2);
            return;
        }
        draw_hints(c, hints, keys, 1);
    }
    all.x = 0;
    all.y = 0;
    all.w = c->canvas->w;
    all.h = c->canvas->h;
    if (eidos_ui_hit(all) & EIDOS_UI_CLICK) {
        eidos_ui_press(BW_KEY_A, 2);
    } else if (eidos_ui_right_click()) {
        eidos_ui_press(BW_KEY_B, 2);
    }
}

static const EidosUiRecipe recipe_evolution = {
    "evolution", evolution_match, evolution_run, evolution_reset, 0
};

/* ------------------------------------------------------------------ */

const EidosUiRecipe *const ui_recipes_party[] = { &recipe_summary, &recipe_party_list, &recipe_evolution };
const u32 ui_recipes_party_count = 3;
