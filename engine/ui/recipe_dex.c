/*
 * engine/ui/recipe_dex.c -- the one-screen recipes for the Pokedex: the splash,
 * the species list and the entry pages (engine/ui/ui.h,
 * docs/unified-view-recipes.md, section 4).
 *
 * The Pokedex is one proc (overlay 212) that runs a private proc stack; the
 * screen on top of that stack is the splash (211), the list (209), the entry
 * (208) or the search (210).  The recipe matches the first three and lets the
 * search fall back to the two DS screens.  It draws a red and black modal (the
 * device's colours) over the blurred world and reads everything from the
 * game's own state:
 *
 *   - the list rows, the cursor and the counts from ZukanList (ov209) and the
 *     game's ZukanSave accessors;
 *   - the entry's species and page from ZukanDetailArgs (ov208);
 *   - names, categories, heights, weights and the entry text from the game's
 *     own message archive (a/0/0/2, the files the info widget of ov206 opens),
 *     read through the port's file layer so a mod that replaces a member shows
 *     here too;
 *   - the types from the personal data (a/0/1/6, the kit's archive slot);
 *   - the Pokemon's picture from its front sprite in the game's data (a/0/0/4,
 *     ui_mon_front_draw), playing its idle loop.
 *
 * A click becomes what the game expects: a tap on the bottom screen at the
 * centre of the touch rectangle the game tests, or a key press (the list's
 * cursor moves one row per UP or DOWN press, so selecting a far row is a series
 * of presses, one every few frames, while the game's list is idle).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ui.h"
#include "kit.h"
#include "assets.h"
#include "port.h"
#include "input.h"

#if defined(__GNUC__) || defined(__clang__)
#define DEX_WEAK __attribute__((weak))
#else
#define DEX_WEAK
#endif

/* ------------------------------------------------------------------ */
/* The game's objects                                                  */
/* ------------------------------------------------------------------ */

/* The game system global (src/game/arm9/gamesystem.c): its first word is the
 * running GameSystem (sdk/mp.c reads it the same way). */
extern void *bw_dsref__GameSystemGlobal DEX_WEAK;

/* The Pokedex save (src/game/arm9/zukan_save.c). */
extern void *ZUKAN_SAVE_GetFromGameData(void *gameData) DEX_WEAK;
extern u32   ZUKAN_SAVE_IsSeen(void *save, u16 species) DEX_WEAK;
extern u32   ZUKAN_SAVE_IsCaught(void *save, u16 species) DEX_WEAK;
extern u32   ZUKAN_SAVE_GetDexObtained(void *save) DEX_WEAK;
extern u32   ZUKAN_SAVE_IsNationalDexEnabled(void *save) DEX_WEAK;
extern u16   ZUKAN_SAVE_GetFormCount(u32 species) DEX_WEAK;
/* The message file's language section (src/game/arm9/msgdata.c). */
extern s32   MSGDATA_GetLanguageIndex(const void *file) DEX_WEAK;

/* Layouts (the DS's 32-bit ones; eidos builds ILP32, docs/unified-view-recipes.md
 * sections 0.2 and 4). */
#define GS_PROCSYS      0x08    /* GameSystem.procSys                        */
#define PS_CURRENT      0x08    /* ProcSys.current                           */
#define PR_OVERLAY      0x04    /* Proc.overlayId                            */
#define PR_SEQ          0x08    /* Proc.seq                                  */
#define PR_PROCWORK     0x10    /* Proc.procWork                             */
#define PR_WORK         0x14    /* Proc.work                                 */
#define ZW_PROC         0x04    /* ZukanWork.proc (the private ProcSys)      */
#define ZL_ARGS         0x000   /* ZukanList.args                            */
#define ZL_STATE        0x010   /* ZukanList.state: 4 is the idle input state */
#define ZL_FRAMELIST    0xC2C   /* ZukanList.frameList                       */
#define FL_CURSOR       0x5C    /* FrameList cursor (s16)                    */
#define FL_VISIBLE      0x5E    /* FrameList visibleCount (u16)              */
#define FL_SCROLLPOS    0x62    /* FrameList scrollPos (s16)                 */
#define ZA_SAVE         0x04    /* ZukanListArgs.save                        */
#define ZA_MODE         0x08    /* ZukanListArgs.mode (u16)                  */
#define ZA_COUNT        0x0A    /* ZukanListArgs.count (u16)                 */
#define ZA_IDS          0x0C    /* ZukanListArgs.ids                         */
#define ZD_GAMEDATA     0x00    /* ZukanDetailArgs.gameData                  */
#define ZD_MODE         0x04    /* ZukanDetailArgs.mode (1 info .. 4 form)   */
#define ZD_IDS          0x08    /* ZukanDetailArgs.idList                    */
#define ZD_COUNT        0x0C    /* ZukanDetailArgs.count (u16)               */
#define ZD_INDEX        0x0E    /* ZukanDetailArgs.index (u16)               */

static u32 rd32p(const void *base, u32 off)
{
    return *(const u32 *)((const u8 *)base + off);
}
static u32 rd16p(const void *base, u32 off)
{
    return *(const u16 *)((const u8 *)base + off);
}
static const void *rdptr(const void *base, u32 off)
{
    return *(const void *const *)((const u8 *)base + off);
}

/* ------------------------------------------------------------------ */
/* What the Pokedex shows now                                          */
/* ------------------------------------------------------------------ */

enum { DEX_NONE = 0, DEX_SPLASH, DEX_LIST, DEX_ENTRY, DEX_LOADING };

typedef struct DexNow {
    int kind;
    /* list */
    const u16 *ids;
    int count, mode;            /* mode 0 regional, 1 national              */
    int sel;                    /* cursor + scrollPos                       */
    int scroll;                 /* the game's scrollPos                     */
    int idle;                   /* the game's list takes keys now           */
    int visible;                /* rows the game's list shows (FrameList)   */
    void *fl;                   /* the game's FrameList, for the hover      */
    void *save;
    /* entry */
    int species, page;          /* page 1 info, 2 area, 3 voice, 4 form     */
    int index, total, number;
    const u16 *eids;
} DexNow;

static int g_skip;              /* the list drops the id list's first entry */

/* The Pokedex wrapper proc and its child, or NULL.  *child_overlay is the
 * overlay id of the screen on top of the private stack (0 when none). */
static const void *dex_wrapper(const void **child, int *child_overlay)
{
    const void *gs, *ps, *cur, *zw, *cps, *ccur;

    *child = NULL;
    *child_overlay = 0;
    if (&bw_dsref__GameSystemGlobal == NULL || bw_dsref__GameSystemGlobal == NULL) {
        return NULL;
    }
    gs = *(void *const *)bw_dsref__GameSystemGlobal;
    if (gs == NULL) {
        return NULL;
    }
    ps = rdptr(gs, GS_PROCSYS);
    cur = ps != NULL ? rdptr(ps, PS_CURRENT) : NULL;
    if (cur == NULL || (s32)rd32p(cur, PR_OVERLAY) != 212 || rd32p(cur, PR_SEQ) < 2) {
        return NULL;
    }
    zw = rdptr(cur, PR_WORK);
    if (zw == NULL) {
        return cur;
    }
    cps = rdptr(zw, ZW_PROC);
    ccur = cps != NULL ? rdptr(cps, PS_CURRENT) : NULL;
    if (ccur != NULL) {
        *child = ccur;
        *child_overlay = (int)rd32p(ccur, PR_OVERLAY);
    }
    return cur;
}

static int dex_probe(DexNow *v)
{
    const void *child, *w, *wrap, *zw;
    int ov;

    memset(v, 0, sizeof(*v));
    wrap = dex_wrapper(&child, &ov);
    zw = wrap != NULL ? rdptr(wrap, PR_WORK) : NULL;
    if (wrap == NULL) {
        return 0;
    }
    if (child == NULL || rd32p(child, PR_SEQ) < 2) {
        /* between two screens: an empty modal, never the DS screens */
        v->kind = DEX_LOADING;
        return 1;
    }
    if (ov == 211) {
        v->kind = DEX_SPLASH;
    } else if (ov == 209) {
        const void *args, *fl;

        w = rdptr(child, PR_WORK);
        args = w != NULL ? rdptr(w, ZL_ARGS) : NULL;
        fl = w != NULL ? rdptr(w, ZL_FRAMELIST) : NULL;
        if (args == NULL || fl == NULL || rdptr(args, ZA_IDS) == NULL) {
            v->kind = DEX_LOADING;
        } else {
            v->kind = DEX_LIST;
            v->ids = (const u16 *)rdptr(args, ZA_IDS);
            v->count = (int)rd16p(args, ZA_COUNT);
            if (v->count > 649) {
                v->count = 649;
            }
            v->mode = (int)rd16p(args, ZA_MODE);
            v->save = (void *)rdptr(args, ZA_SAVE);
            if (zw != NULL) {
                /* the wrapper's id list starts with an entry the regional
                 * list leaves out (ZUKAN_StartProc209) */
                g_skip = v->ids == (const u16 *)rdptr(zw, 0x0C) + 1;
            }
            v->scroll = (int)(s16)rd16p(fl, FL_SCROLLPOS);
            v->sel = v->scroll + (int)(s16)rd16p(fl, FL_CURSOR);
            v->idle = (int)rd32p(w, ZL_STATE) == 4;
            v->fl = (void *)fl;
            v->visible = (int)rd16p(fl, FL_VISIBLE);
            if (v->sel < 0 || v->sel >= v->count) {
                v->sel = v->sel < 0 ? 0 : v->count - 1;
            }
        }
    } else if (ov == 208) {
        const void *pw = rdptr(child, PR_PROCWORK);
        const u16 *ids;

        ids = pw != NULL ? (const u16 *)rdptr(pw, ZD_IDS) : NULL;
        if (pw == NULL || ids == NULL) {
            v->kind = DEX_LOADING;
        } else {
            v->kind = DEX_ENTRY;
            v->total = (int)rd16p(pw, ZD_COUNT);
            v->index = (int)rd16p(pw, ZD_INDEX);
            v->page = (int)rd32p(pw, ZD_MODE);
            if (v->index >= v->total) {
                v->index = 0;
            }
            v->species = v->total > 0 ? (int)ids[v->index] : 0;
            v->eids = ids;
            v->number = v->index + (g_skip ? 0 : 1);
            if (ZUKAN_SAVE_GetFromGameData != NULL) {
                v->save = ZUKAN_SAVE_GetFromGameData((void *)rdptr(pw, ZD_GAMEDATA));
            }
        }
    } else {
        v->kind = DEX_NONE;     /* the search (210): the DS screens */
        return 0;
    }
    return 1;
}

static int dex_state(const DexNow *v, int species)
{
    if (v->save == NULL || ZUKAN_SAVE_IsCaught == NULL || ZUKAN_SAVE_IsSeen == NULL ||
        species <= 0) {
        return 0;
    }
    if (ZUKAN_SAVE_IsCaught(v->save, (u16)species) == 1) {
        return 2;
    }
    return ZUKAN_SAVE_IsSeen(v->save, (u16)species) == 1 ? 1 : 0;
}

/* ------------------------------------------------------------------ */
/* The game's text: message files of a/0/0/2                           */
/* ------------------------------------------------------------------ */

/* The files the info widget opens (ov206 tbl_021BC1C4: [0] title, [1]
 * category, [2] height, [3] weight, [4] entry; gSpeciesNameBank is file
 * 0x46): each holds one string per species number. */
#define MSG_NAME      0x46
#define MSG_CATEGORY  0x104
#define MSG_HEIGHT    0xF5
#define MSG_WEIGHT    0x10C
#define MSG_ENTRY     0xEC

typedef struct MsgCache { u32 id; u8 *data; u32 size; } MsgCache;

static BwArcFile g_msg_arc;
static int       g_msg_state;       /* 0 not tried, 1 open, -1 failed        */
static MsgCache  g_msgs[8];
static int       g_msg_n;

static const MsgCache *msg_file(u32 id)
{
    int i;
    long n;
    u8 *buf;

    for (i = 0; i < g_msg_n; i++) {
        if (g_msgs[i].id == id) {
            return g_msgs[i].data != NULL ? &g_msgs[i] : NULL;
        }
    }
    if (g_msg_n >= 8) {
        return NULL;
    }
    if (g_msg_state == 0) {
        g_msg_state = bw_arc_open(&g_msg_arc, "a/0/0/2") == 0 ? 1 : -1;
    }
    g_msgs[g_msg_n].id = id;
    g_msgs[g_msg_n].data = NULL;
    g_msgs[g_msg_n].size = 0;
    if (g_msg_state == 1 && id < g_msg_arc.count &&
        (n = bw_arc_member_size(&g_msg_arc, id)) > 0 &&
        (buf = (u8 *)malloc((size_t)n)) != NULL) {
        if (bw_arc_read_member(&g_msg_arc, id, buf, (u32)n) == n) {
            g_msgs[g_msg_n].data = buf;
            g_msgs[g_msg_n].size = (u32)n;
        } else {
            free(buf);
        }
    }
    return g_msgs[g_msg_n++].data != NULL ? &g_msgs[g_msg_n - 1] : NULL;
}

static u32 le16(const u8 *p) { return (u32)p[0] | ((u32)p[1] << 8); }
static u32 le32(const u8 *p) { return le16(p) | (le16(p + 2) << 16); }

static int put_utf8(char *o, int at, int cap, u32 u)
{
    if (u < 0x80) {
        if (at + 1 >= cap) { return at; }
        o[at++] = (char)u;
    } else if (u < 0x800) {
        if (at + 2 >= cap) { return at; }
        o[at++] = (char)(0xC0 | (u >> 6));
        o[at++] = (char)(0x80 | (u & 63));
    } else {
        if (at + 3 >= cap) { return at; }
        o[at++] = (char)(0xE0 | (u >> 12));
        o[at++] = (char)(0x80 | ((u >> 6) & 63));
        o[at++] = (char)(0x80 | (u & 63));
    }
    return at;
}

/* One string of a message file as UTF-8 (the game's own decryption,
 * MSGDATA_DecryptUnits: a key per string index, rotated 3 bits a unit). */
static int msg_get(u32 file, u32 idx, char *out, int cap)
{
    const MsgCache *m = msg_file(file);
    const u8 *d, *sec, *e;
    u32 count, langs, li = 0, off, len, i, key;
    int at = 0;

    out[0] = '\0';
    if (m == NULL || m->size < 16) {
        return 0;
    }
    d = m->data;
    langs = le16(d);
    count = le16(d + 2);
    if (idx >= count || langs == 0) {
        return 0;
    }
    if (langs > 1 && MSGDATA_GetLanguageIndex != NULL) {
        s32 l = MSGDATA_GetLanguageIndex(d);

        li = l > 0 && (u32)l < langs ? (u32)l : 0;
    }
    if (12 + 4 * li + 4 > m->size) {
        return 0;
    }
    off = le32(d + 12 + 4 * li);
    if (off + 4 + (idx + 1) * 8 > m->size) {
        return 0;
    }
    sec = d + off;
    e = sec + 4 + idx * 8;
    off = le32(e);
    len = le16(e + 4);
    if ((u32)(sec - d) + off + len * 2 > m->size) {
        return 0;
    }
    key = (u32)(((idx + 3) * 0x2983u) & 0xFFFFu);
    for (i = 0; i < len; i++) {
        u32 u = le16(sec + off + i * 2) ^ key;

        key = ((key & 0xE000u) >> 13) | ((key << 3) & 0xFFFFu);
        if (u == 0xFFFFu) {
            break;
        }
        if (u == 0xFFFEu) {         /* the entry texts break their lines with this */
            u = ' ';
        }
        if (u == 0xE000u || u == 0x25BCu || u == 0x25BDu) {
            u = ' ';
        }
        at = put_utf8(out, at, cap, u);
    }
    out[at] = '\0';
    return at;
}

/* ------------------------------------------------------------------ */
/* Personal data: the types                                            */
/* ------------------------------------------------------------------ */

static void species_types(int species, int *t1, int *t2)
{
    u32 size = 0;
    u8 *p = ui_arc_member(UI_ARC_PERSONAL, (u32)species, &size);

    *t1 = *t2 = UI_TYPE_UNKNOWN;
    if (p != NULL && size >= 8) {
        /* the personal data numbers the types without the "???" gap */
        *t1 = p[6];
        *t2 = p[7];
    }
    free(p);
}

/* ------------------------------------------------------------------ */
/* Drawing                                                             */
/* ------------------------------------------------------------------ */

#define DEX_PANEL   0x0C0C0Eu
#define DEX_PANEL2  0x08080Au
#define DEX_ROWSEL  0x3C0812u

typedef struct DexLayout {
    int mx, my, mw, mh;         /* the modal                                */
    int bx, by, bw, bh;         /* the body under the header                */
} DexLayout;

static int imin(int a, int b) { return a < b ? a : b; }
static int imax(int a, int b) { return a > b ? a : b; }
static int iclamp(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

static void dex_layout(EidosUiCtx *c, DexLayout *L)
{
    int W = c->canvas->w, H = c->canvas->h - 17;

    L->mw = iclamp(W * 9 / 10, imin(W - 8, 300), 560);
    L->mh = iclamp(H * 9 / 10, imin(H - 8, 170), 320);
    L->mx = (W - L->mw) / 2;
    L->my = imax(2, (H - L->mh) / 2);
    L->bx = L->mx + 5;
    L->by = L->my + 4 + 22 + 4;
    L->bw = L->mw - 10;
    L->bh = L->my + L->mh - 5 - L->by;
}

static void dex_modal(EidosUiCtx *c, DexLayout *L, const char *sub, const char *right)
{
    ui_panel(c, L->mx, L->my, L->mw, L->mh, UI_CUT_TL | UI_CUT_BR, 9, DEX_PANEL, UI_DEX2, 246);
    ui_panel(c, L->mx + 2, L->my + 2, L->mw - 4, L->mh - 4, UI_CUT_TL | UI_CUT_BR, 8,
             DEX_PANEL, UI_DEX3, 255);
    ui_header(c, L->mx + 4, L->my + 4, L->mw - 8, "POK\xC3\x89" "DEX", sub, right, UI_DEX);
}

/* The key hints: each is a button; the click taps the game's own button or
 * presses the key. */
typedef struct DexHint {
    const char *key, *what;
    u32 keys;                   /* BW_KEY_* to press, or 0                  */
    int tap_x, tap_y;           /* or a tap at this DS position             */
} DexHint;

static void dex_hints(EidosUiCtx *c, const DexHint *h, int n)
{
    int i, x = 6, y = c->canvas->h - 17;

    ui_fill(c, 0, y, c->canvas->w, 17, UI_BODY2, 235);
    ui_fill(c, 0, y, c->canvas->w, 1, UI_DEX2, 255);
    for (i = 0; i < n; i++) {
        int kw = ui_keycap_w(h[i].key);
        int aw = ui_text_w(h[i].what, UI_FONT_SMALL);
        EidosUiRect r = { x - 2, y, kw + 4 + aw + 4, 17 };
        int st = eidos_ui_hit(r);

        if (st & EIDOS_UI_HOVER) {
            ui_fill(c, x - 2, y + 1, r.w, 15, UI_EDGE, 255);
        }
        if (st & EIDOS_UI_CLICK) {
            if (h[i].keys != 0) {
                eidos_ui_press(h[i].keys, 2);
            } else {
                eidos_ui_tap(h[i].tap_x, h[i].tap_y, 3);
            }
        }
        ui_keycap(c, x, y + 2, h[i].key);
        x += kw + 4;
        x = ui_text(c, x, y + 2 + ui_text_y(13, UI_FONT_SMALL), h[i].what, UI_LIGHT, UI_EDGE,
                    UI_FONT_SMALL, 1) + 10;
    }
}

/* A click outside the modal is B (the tap at the game's back button). */
static void outside_back(EidosUiCtx *c, const DexLayout *L, int tx, int ty)
{
    int W = c->canvas->w, H = c->canvas->h - 17;
    EidosUiRect r[4] = {
        { 0, 0, W, L->my },
        { 0, L->my + L->mh, W, H - L->my - L->mh },
        { 0, L->my, L->mx, L->mh },
        { L->mx + L->mw, L->my, W - L->mx - L->mw, L->mh },
    };
    int i;

    for (i = 0; i < 4; i++) {
        if (r[i].w > 0 && r[i].h > 0 && (eidos_ui_hit(r[i]) & EIDOS_UI_CLICK)) {
            eidos_ui_tap(tx, ty, 3);
        }
    }
}

static void num3(char *s, int n)
{
    s[0] = (char)('0' + (n / 100) % 10);
    s[1] = (char)('0' + (n / 10) % 10);
    s[2] = (char)('0' + n % 10);
    s[3] = '\0';
}

/* One list row: mark, number, icon, name. */
static void dex_row(EidosUiCtx *c, int x, int y, int w, int h, int number, int species,
                    int state, int selected, int hover)
{
    char s[8], name[64];
    u32 fill = selected ? DEX_ROWSEL : hover ? 0x241018u : UI_BODY2;
    u32 edge = selected ? UI_DEX : UI_EDGE;
    int ty = ui_text_y(h, UI_FONT_LARGE);
    const UiImage *ic;

    ui_panel(c, x, y, w, h, UI_CUT_TL | UI_CUT_BR, 4, fill, edge, 255);
    if (state == 2) {
        u8 st = 0;

        ui_party_balls(c, x + 6, y + (h - 12) / 2, &st, 1);
    } else if (state == 1) {
        ui_ellipse(c, x + 6, y + (h - 10) / 2, 10, 10, UI_NONE, UI_LIGHT, 255);
    }
    num3(s, number);
    ui_text(c, x + 22, y + ty, s, selected ? UI_PINK : UI_LIGHT, UI_EDGE, UI_FONT_LARGE, 1);
    if (state != 0 && (ic = ui_mon_icon((u16)species, 0, 0)) != NULL) {
        ui_image(c, x + 46, y + (h - ic->h) / 2, ic, 1);
    }
    if (state != 0 && msg_get(MSG_NAME, (u32)species, name, sizeof(name)) > 0) {
        ui_text(c, x + 82, y + ty, name, UI_WHITE, UI_EDGE, UI_FONT_LARGE, 1);
    } else {
        ui_text(c, x + 82, y + ty, "?????", UI_GREY, UI_EDGE, UI_FONT_LARGE, 1);
    }
}

/* The picture block: the shadow, then the Pokemon's front sprite playing
 * its idle loop (the game's own sprite data, ui_mon_front_draw), or its
 * icon, scaled, when the sprite cannot be read. */
static void dex_picture(EidosUiCtx *c, int species, int x, int y, int w, int h, int visible)
{
    int k;
    const UiImage *ic;

    ui_ellipse(c, x + w / 2 - w * 3 / 8, y + h - 18, w * 3 / 4, 14, UI_DEX2, UI_NONE, 255);
    ui_ellipse(c, x + w / 2 - w * 5 / 16, y + h - 15, w * 5 / 8, 9, UI_DEX3, UI_NONE, 255);
    if (!visible) {
        return;
    }
    if (ui_mon_front_anim((u16)species, 0, 0, NULL, NULL) != NULL) {
        ui_mon_front_draw(c, (u16)species, 0, bw_input_frame_number(), x, y, w, h - 8);
        return;
    }
    ic = ui_mon_icon((u16)species, 0, 0);
    if (ic != NULL) {
        k = imax(1, imin(w / ic->w, (h - 8) / ic->h));
        ui_image(c, x + (w - ic->w * k) / 2, y + h - 8 - ic->h * k, ic, k);
    }
}

static void hw_strings(int species, int caught, char *ht, int htn, char *wt, int wtn)
{
    if (caught && msg_get(MSG_HEIGHT, (u32)species, ht, htn) > 0 &&
        msg_get(MSG_WEIGHT, (u32)species, wt, wtn) > 0) {
        return;
    }
    snprintf(ht, (size_t)htn, "\?\?\?'\?\?\"");
    snprintf(wt, (size_t)wtn, "????.? lbs.");
}

/* The species block: number, name, category, types, picture, height and
 * weight, into the panel (x, y, w, h). */
static void dex_species_panel(EidosUiCtx *c, const DexNow *v,
                              int x, int y, int w, int h, int species, int number,
                              int state)
{
    char s[8], name[64], cat[64], ht[40], wt[40];
    int t1 = UI_TYPE_UNKNOWN, t2 = UI_TYPE_UNKNOWN, tx, ih = 15, py, ph;

    ui_panel(c, x, y, w, h, UI_CUT_TL | UI_CUT_BR, 6, DEX_PANEL2, UI_DEX2, 255);
    num3(s, number);
    ui_text(c, x + 8, y + 5, s, UI_PINK, UI_EDGE, UI_FONT_LARGE, 1);
    if (state != 0 && msg_get(MSG_NAME, (u32)species, name, sizeof(name)) > 0) {
        ui_text(c, x + 8 + 30, y + 5, name, UI_WHITE, UI_EDGE, UI_FONT_LARGE, 1);
    } else {
        ui_text(c, x + 8 + 30, y + 5, "?????", UI_GREY, UI_EDGE, UI_FONT_LARGE, 1);
    }
    if (state == 2 && msg_get(MSG_CATEGORY, (u32)species, cat, sizeof(cat)) > 0) {
        ui_text(c, x + 8, y + 21, cat, UI_LIGHT, UI_EDGE, UI_FONT_MEDIUM, 1);
    } else {
        ui_text(c, x + 8, y + 21, "????? Pok\xC3\xA9mon", UI_GREY, UI_EDGE, UI_FONT_MEDIUM, 1);
    }
    if (state == 2) {
        species_types(species, &t1, &t2);
        tx = x + w - 8 - 48;
        if (t2 != t1) {
            ui_type_tag(c, tx, y + 6, 48, t2);
            tx -= 52;
        }
        ui_type_tag(c, tx, y + 6, 48, t1);
    } else {
        ui_type_tag(c, x + w - 8 - 48, y + 6, 48, UI_TYPE_UNKNOWN);
    }
    hw_strings(species, state == 2, ht, sizeof(ht), wt, sizeof(wt));
    py = y + 38;
    ph = h - 38 - 2 * (ih + 3) - 6;
    if (ph > 20) {
        dex_picture(c, species, x + 6, py, w - 12, ph, state != 0);
    }
    ui_info_row(c, x + 6, y + h - 2 * (ih + 3) - 3, w - 12, "HT", ht);
    ui_info_row(c, x + 6, y + h - (ih + 3) - 1, w - 12, "WT", wt);
    (void)v;
}

/* ------------------------------------------------------------------ */
/* The list                                                            */
/* ------------------------------------------------------------------ */

static int g_target = -1;       /* the row the player asked for              */
static int g_press_wait;        /* frames until the next cursor key          */
static int g_first;             /* the first row drawn                       */
static int g_open_pending;      /* open the selected row when it arrives     */
static int g_game_sel = -1;     /* the row whose picture the game shows      */
static int g_hover_sel = -1;    /* the row the hover moved the cursor to     */

/* Open the species under the game's cursor: the tap on its row (the game's
 * hit codes 1 to 7 open at once, where the A key is first spent on bringing
 * the key cursor back after a touch). */
static void list_open(const DexNow *v)
{
    int row = iclamp(v->sel - v->scroll, 0, 6);

    eidos_ui_tap(111, 24 * row + 12, 3);
}

/* A press that moves the game's list cursor one row toward g_target. */
static void list_steer(const DexNow *v)
{
    if (g_press_wait > 0) {
        g_press_wait--;
        return;
    }
    if (g_target < 0) {
        return;
    }
    if (v->sel == g_target) {
        g_target = -1;
        if (g_open_pending) {
            g_open_pending = 0;
            list_open(v);
            g_press_wait = 6;
        }
        return;
    }
    if (!v->idle) {
        return;
    }
    {
        int d = g_target - v->sel;

        /* LEFT and RIGHT page the game's list; UP and DOWN step one row */
        eidos_ui_press(d >= 8 ? BW_KEY_RIGHT : d <= -8 ? BW_KEY_LEFT :
                       d > 0 ? BW_KEY_DOWN : BW_KEY_UP, 2);
        g_press_wait = d >= 8 || d <= -8 ? 8 : 3;
    }
}

static void run_list(EidosUiCtx *c, EidosUiView *view, const EidosUiFrame *in,
                     const DexNow *v)
{
    DexLayout L;
    char right[40];
    int i, seen = 0, own = 0, rowh = 36, gap = 2, vis, lw, lx, rx, rw, listh, sbx;
    EidosUiRect lr;
    int wh;
    DexHint hints[6];
    int nh = 0;

    for (i = 0; i < v->count; i++) {
        int s = dex_state(v, v->ids[i]);

        seen += s != 0;
        own += s == 2;
    }
    snprintf(right, sizeof(right), "SEEN %d  OWN %d", seen, own);
    dex_layout(c, &L);
    dex_modal(c, &L, v->mode == 1 ? "NATIONAL" : "UNOVA", right);

    lw = iclamp(L.bw * 11 / 20, 200, L.bw - 140);
    lx = L.bx;
    rx = lx + lw + 6;
    rw = L.bx + L.bw - rx;
    listh = L.bh;
    vis = imax(1, (listh + gap) / (rowh + gap));
    if (vis > v->count) {
        vis = v->count;
    }
    sbx = lx + lw - 5;

    /* a cursor the hover moved has no picture on the game's screen yet: the
     * panel stays on the row the game shows until a key moves on */
    if (v->sel != g_hover_sel) {
        g_hover_sel = -1;
        g_game_sel = v->sel;
    }

    /* the viewport follows the cursor */
    if (v->sel < g_first) {
        g_first = v->sel;
    }
    if (v->sel >= g_first + vis) {
        g_first = v->sel - vis + 1;
    }
    g_first = iclamp(g_first, 0, imax(0, v->count - vis));

    lr.x = lx; lr.y = L.by; lr.w = lw; lr.h = listh;
    wh = eidos_ui_wheel(lr);
    if (wh != 0) {
        int base = g_target >= 0 ? g_target : v->sel;

        g_target = iclamp(base - wh * 3, 0, v->count - 1);
        g_open_pending = 0;
    }

    for (i = 0; i < vis; i++) {
        int row = g_first + i, species = v->ids[row];
        int y = L.by + i * (rowh + gap), state = dex_state(v, species);
        EidosUiRect r = { lx, y, lw - 8, rowh };
        int st = eidos_ui_hit(r);

        if ((st & EIDOS_UI_HOVER) && g_target < 0 && v->idle && row != v->sel &&
            row - v->scroll >= 0 && row - v->scroll < v->visible) {
            /* the game's cursor follows the mouse (inside the rows the game
             * shows, so the list never scrolls): a key goes on from the row
             * the mouse was on */
            *(s16 *)((u8 *)v->fl + FL_CURSOR) = (s16)(row - v->scroll);
            g_hover_sel = row;
        }
        dex_row(c, lx, y, lw - 8, rowh, row + 1, species, state,
                row == v->sel && eidos_ui_keys_lit(), (st & EIDOS_UI_HOVER) != 0);
        if (st & EIDOS_UI_CLICK) {
            if (row == v->sel) {
                list_open(v);
            } else {
                g_target = row;
                g_open_pending = 0;
            }
        }
    }
    ui_scrollbar(c, sbx, L.by, listh, g_first, vis, v->count, UI_DEX);
    {
        /* the scroll bar is a column of slices: a click jumps the list there,
         * and the held button drags across them */
        int n = 32, j;

        for (j = 0; j < n && v->count > 1; j++) {
            EidosUiRect sr = { sbx - 3, L.by + j * listh / n, 11, listh / n + 1 };
            int st = eidos_ui_hit(sr);

            if ((st & EIDOS_UI_CLICK) || ((st & EIDOS_UI_HOVER) && in->mouse.down &&
                                           in->mouse.x >= (sbx - 3) * c->canvas->k)) {
                g_target = iclamp((j * 2 + 1) * (v->count - 1) / (n * 2), 0, v->count - 1);
                g_open_pending = 0;
            }
        }
    }
    if (in->buttons & (BW_KEY_UP | BW_KEY_DOWN | BW_KEY_LEFT | BW_KEY_RIGHT | BW_KEY_A)) {
        g_target = -1;          /* the player's own keys drive the list */
    }
    outside_back(c, &L, 243, 179);

    {
        int psel = g_hover_sel >= 0 ? g_game_sel : v->sel;
        int sp = v->ids[psel], state = dex_state(v, sp);
        EidosUiRect pr = { rx, L.by, rw, L.bh };

        dex_species_panel(c, v, rx, L.by, rw, L.bh, sp, psel + 1, state);
        if (eidos_ui_hit(pr) & EIDOS_UI_CLICK) {
            if (state != 0) {
                list_open(v);
            }
        }
    }

    hints[nh++] = (DexHint){ "A", "Info", 0, 111, 24 * iclamp(v->sel - v->scroll, 0, 6) + 12 };
    hints[nh++] = (DexHint){ "B", "Back", 0, 243, 179 };
    hints[nh++] = (DexHint){ "X", "Close", 0, 219, 179 };
    hints[nh++] = (DexHint){ "\xE2\x86\x91\xE2\x86\x93", "Move", 0, 0, 0 };
    if (v->save != NULL && ZUKAN_SAVE_GetDexObtained != NULL &&
        ZUKAN_SAVE_GetDexObtained(v->save) != 0) {
        hints[nh++] = (DexHint){ "START", "Search", 0, 35, 179 };
    }
    if (v->save != NULL && ZUKAN_SAVE_IsNationalDexEnabled != NULL &&
        ZUKAN_SAVE_IsNationalDexEnabled(v->save) != 0 && v->mode == 0) {
        hints[nh++] = (DexHint){ "SELECT", "Order", 0, 103, 179 };
    }
    dex_hints(c, hints, nh);
    list_steer(v);

    view->mode = EIDOS_VIEW_WORLD;
    view->blur = 1;
    view->hold_world = 1;     /* an app: the field is not running */
    view->hide_2d = 1;
}

/* ------------------------------------------------------------------ */
/* The entry                                                           */
/* ------------------------------------------------------------------ */

typedef struct WrapOut { EidosUiCtx *c; int x, y, lh; int max_y; } WrapOut;

static void wrap_line(void *user, const char *start, int len)
{
    WrapOut *o = (WrapOut *)user;
    char buf[160];

    if (len >= (int)sizeof(buf)) {
        len = (int)sizeof(buf) - 1;
    }
    memcpy(buf, start, (size_t)len);
    buf[len] = '\0';
    if (o->y + o->lh <= o->max_y) {
        ui_text(o->c, o->x, o->y, buf, UI_WHITE, UI_EDGE, UI_FONT_MEDIUM, 1);
    }
    o->y += o->lh;
}

static void run_entry(EidosUiCtx *c, EidosUiView *view, const EidosUiFrame *in,
                      const DexNow *v)
{
    static const char *const tabs[4] = { "INFO", "AREA", "CRY", "FORM" };
    static const int tab_tap_x[4] = { 64, 96, 128, 160 };
    DexLayout L;
    char right[40], text[600];
    int state = dex_state(v, v->species), i, lw, rx, rw, tx, ty;
    DexHint hints[6];
    int nh = 0;

    snprintf(right, sizeof(right), "No. %03d", v->number);
    dex_layout(c, &L);
    dex_modal(c, &L, "ENTRY", right);

    lw = iclamp(L.bw * 2 / 5, 130, L.bw - 120);
    dex_species_panel(c, v, L.bx, L.by, lw, L.bh, v->species, v->number, state);
    rx = L.bx + lw + 6;
    rw = L.bx + L.bw - rx;
    ui_panel(c, rx, L.by, rw, L.bh, UI_CUT_TL | UI_CUT_BR, 6, DEX_PANEL2, UI_DEX2, 255);

    tx = rx + 6;
    ty = L.by + 5;
    for (i = 0; i < 4; i++) {
        int tw = ui_text_w(tabs[i], UI_FONT_SMALL) + 16;
        int off = i == 3 && (ZUKAN_SAVE_GetFormCount == NULL ||
                             ZUKAN_SAVE_GetFormCount((u32)v->species) <= 1);
        EidosUiRect r = { tx, ty, tw, 14 };
        int st = off ? 0 : eidos_ui_hit(r);

        ui_tab(c, tx, ty, tabs[i], v->page == i + 1, UI_DEX, UI_DEX2);
        if (off) {
            ui_fill(c, tx + 3, ty + 3, tw - 6, 8, UI_BODY2, 160);
        } else if ((st & EIDOS_UI_HOVER) && v->page != i + 1) {
            ui_fill(c, tx + 4, ty + 12, tw - 8, 1, UI_DEX, 255);
        }
        if ((st & EIDOS_UI_CLICK) && v->page != i + 1) {
            eidos_ui_tap(tab_tap_x[i], 180, 3);
        }
        tx += tw + 4;
    }
    (void)tx;
    ui_fill(c, rx + 4, ty + 18, rw - 8, 1, UI_DEX2, 255);

    if (v->page == 1) {
        WrapOut o;

        if (state == 2 && msg_get(MSG_ENTRY, (u32)v->species, text, sizeof(text)) > 0) {
            o.c = c; o.x = rx + 8; o.y = ty + 24; o.lh = 15; o.max_y = L.by + L.bh - 4;
            ui_wrap(text, rw - 16, UI_FONT_MEDIUM, wrap_line, &o);
        } else {
            ui_text(c, rx + 8, ty + 24, "No data yet.", UI_GREY, UI_EDGE, UI_FONT_MEDIUM, 1);
        }
    } else {
        /* area, voice and form pages: the game draws them on its bottom
         * screen (a map, a wave, the forms); show that picture as drawn and
         * turn a click on it into the tap at the same spot */
        const u8 *scr = in->screen_rgba[1];
        int aw = rw - 8, ah = L.by + L.bh - (ty + 22) - 4;
        int sw = imin(256, aw), sh = imin(158, ah);
        int sx = (256 - sw) / 2, sy = 10, k = imax(1, imin(aw / sw, ah / sh));
        int px = rx + (rw - sw * k) / 2, py = ty + 22, gx, gy;

        if (scr != NULL) {
            ui_blit_screen(c, px, py, scr, sx, sy, sw, sh, k, 0);
            /* the picture is a grid of 8 by 8 DS pixel cells; a click on a
             * cell taps the game's screen at its centre */
            for (gy = 0; gy * 8 < sh; gy++) {
                for (gx = 0; gx * 8 < sw; gx++) {
                    EidosUiRect r = { px + gx * 8 * k, py + gy * 8 * k, 8 * k, 8 * k };

                    if (eidos_ui_hit(r) & EIDOS_UI_CLICK) {
                        eidos_ui_tap(sx + gx * 8 + 4, sy + gy * 8 + 4, 3);
                    }
                }
            }
        }
    }

    outside_back(c, &L, 244, 180);

    hints[nh++] = (DexHint){ "B", "List", 0, 244, 180 };
    hints[nh++] = (DexHint){ "X", "Close", 0, 220, 180 };
    hints[nh++] = (DexHint){ "\xE2\x86\x91", "Previous", 0, 12, 180 };
    hints[nh++] = (DexHint){ "\xE2\x86\x93", "Next", 0, 36, 180 };
    hints[nh++] = (DexHint){ "\xE2\x86\x90\xE2\x86\x92", "Page", 0, 0, 0 };
    dex_hints(c, hints, nh);

    view->mode = EIDOS_VIEW_WORLD;
    view->blur = 1;
    view->hold_world = 1;     /* an app: the field is not running */
    view->hide_2d = 1;
}

/* ------------------------------------------------------------------ */
/* The splash and the gaps                                             */
/* ------------------------------------------------------------------ */

static void run_splash(EidosUiCtx *c, EidosUiView *view, const EidosUiFrame *in,
                       const DexNow *v)
{
    DexLayout L;
    EidosUiRect all;
    DexHint hint = { "A", "Open", 0, 128, 96 };

    (void)in;
    dex_layout(c, &L);
    dex_modal(c, &L, "", "");
    ui_text(c, L.bx + (L.bw - ui_text_w("POK\xC3\x89" "DEX", UI_FONT_LARGE) * 3) / 2,
            L.by + L.bh / 2 - 22, "POK\xC3\x89" "DEX", UI_DEX, UI_DEX3, UI_FONT_LARGE, 3);
    all.x = 0; all.y = 0; all.w = c->canvas->w; all.h = c->canvas->h - 17;
    if (v->kind == DEX_SPLASH && (eidos_ui_hit(all) & EIDOS_UI_CLICK)) {
        eidos_ui_tap(128, 96, 3);
    }
    dex_hints(c, &hint, v->kind == DEX_SPLASH ? 1 : 0);
    view->mode = EIDOS_VIEW_WORLD;
    view->blur = 1;
    view->hold_world = 1;     /* an app: the field is not running */
    view->hide_2d = 1;
}

/* ------------------------------------------------------------------ */
/* The recipe                                                          */
/* ------------------------------------------------------------------ */

static int g_last_kind = -1;    /* the dex screen shown last frame */

/* The splash opening onto the list: its body splits along the middle and
 * the two halves slide apart, the list behind them. */
#define DOOR_FRAMES 18
static int g_doors;             /* frames of the opening left              */
static int g_from_splash;       /* the splash was the last screen shown    */

static void draw_doors(EidosUiCtx *c, int left)
{
    DexLayout L;
    int half, off, i, t;
    EidosUiRect saved;

    dex_layout(c, &L);
    half = L.bh / 2;
    /* eased out: fast at first, settling */
    t = (DOOR_FRAMES - left) * 256 / DOOR_FRAMES;
    t = 256 - (256 - t) * (256 - t) / 256;
    off = (half + 2) * t / 256;
    for (i = 0; i < 2; i++) {
        int dy = i == 0 ? -off : off;
        int y0 = i == 0 ? L.by : L.by + half, h = i == 0 ? half : L.bh - half;

        ui_push(c, 0, 0, &saved);
        ui_clip(c, L.bx, L.by, L.bw, L.bh);
        ui_fill(c, L.bx, y0 + dy, L.bw, h, DEX_PANEL, 255);
        ui_fill(c, L.bx, (i == 0 ? y0 + h - 1 : y0) + dy, L.bw, 1, UI_DEX, 255);
        /* the splash's title, cut along the middle with its half */
        {
            int top = y0 + dy > L.by ? y0 + dy : L.by, bot = y0 + dy + h;

            ui_clip(c, L.bx, top, L.bw, bot > top ? bot - top : 0);
        }
        ui_text(c, L.bx + (L.bw - ui_text_w("POK\xC3\x89" "DEX", UI_FONT_LARGE) * 3) / 2,
                L.by + L.bh / 2 - 22 + dy, "POK\xC3\x89" "DEX", UI_DEX, UI_DEX3,
                UI_FONT_LARGE, 3);
        ui_pop(c, 0, 0, &saved);
    }
}

static int dex_match(void)
{
    DexNow v;

    return dex_probe(&v);
}

static void dex_run(EidosUiCtx *c, EidosUiView *view, const EidosUiFrame *in)
{
    DexNow v;

    if (!dex_probe(&v)) {
        return;
    }
    /* the splash, the list and an entry are one scene to the interface:
     * a change between them crossfades like a change of scene */
    if (v.kind != g_last_kind) {
        if (g_from_splash && v.kind == DEX_LIST) {
            g_doors = DOOR_FRAMES;      /* the splash opens onto the list */
        } else if (g_last_kind != -1) {
            eidos_ui_crossfade();
        }
        g_last_kind = v.kind;
    }
    /* the splash, then the game's fade between it and the list */
    if (v.kind == DEX_SPLASH) {
        g_from_splash = 1;
    } else if (v.kind == DEX_LIST || v.kind == DEX_ENTRY) {
        if (g_doors == 0 && v.kind != DEX_LIST) {
            g_from_splash = 0;
        }
    }
    /* a right click is B, a click outside the modal is B */
    if (eidos_ui_right_click() && (v.kind == DEX_LIST || v.kind == DEX_ENTRY)) {
        eidos_ui_tap(v.kind == DEX_LIST ? 243 : 244, v.kind == DEX_LIST ? 179 : 180, 3);
    }
    switch (v.kind) {
    case DEX_LIST:
        run_list(c, view, in, &v);
        break;
    case DEX_ENTRY:
        run_entry(c, view, in, &v);
        break;
    default:
        run_splash(c, view, in, &v);
        break;
    }
    if (g_doors > 0 && v.kind == DEX_LIST) {
        /* the doors wait, shut, while the splash's picture is held through
         * the game's fade, and open once the list shows; they are the
         * change of screen, so no crossfade on top */
        draw_doors(c, g_doors);
        eidos_ui_own_reveal();
        if (!eidos_ui_holding()) {
            g_doors--;
        }
        g_from_splash = 0;
    } else {
        g_doors = 0;
    }
}

static void dex_reset(void)
{
    g_last_kind = -1;
    g_doors = 0;
    g_from_splash = 0;
    g_target = -1;
    g_press_wait = 0;
    g_first = 0;
    g_open_pending = 0;
}

/* For other recipes (kit.h): the species' name and types. */
int ui_species_name(int species, char *out, int cap)
{
    return msg_get(MSG_NAME, (u32)species, out, cap);
}

void ui_species_types(int species, int *t1, int *t2)
{
    species_types(species, t1, t2);
}

static const EidosUiRecipe recipe_dex = { "pokedex", dex_match, dex_run, dex_reset, 0 };

const EidosUiRecipe *const ui_recipes_dex[] = { &recipe_dex };
const u32 ui_recipes_dex_count = 1;
