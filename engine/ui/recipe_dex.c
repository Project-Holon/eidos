/*
 * engine/ui/recipe_dex.c -- the one-screen recipes for the Pokedex: the splash,
 * the species list and the entry pages (engine/ui/ui.h,
 * docs/unified-view-recipes.md, section 4).
 *
 * The Pokedex is one proc (overlay 212) that runs a private proc stack; the
 * screen on top of that stack is the splash (211), the list (209), the entry
 * (208) or the search (210, run_search).  It draws a red and black modal (the
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
#include "ui_lang.h"
#include "assets.h"
#include "port.h"
#include "input.h"
#include "textbox.h"
#include "theme.h"

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
extern int   ZUKAN_SAVE_IsCountedForNationalCompletion(u32 species) DEX_WEAK;
extern int   ZUKAN_SAVE_IsCountedForRegionalCompletion(u32 species) DEX_WEAK;
extern u32   ZUKAN_SAVE_HasSeenGenderForm(void *save, u16 species, u32 gender, u32 shiny, u32 form) DEX_WEAK;
extern void  ZUKAN_SAVE_UpdateViewFlags(void *save, u16 species, u32 gender, u32 shiny, u32 form) DEX_WEAK;
extern u32   ZUKAN_SAVE_GetViewMode(void *save) DEX_WEAK;
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

enum { DEX_NONE = 0, DEX_SPLASH, DEX_LIST, DEX_ENTRY, DEX_LOADING, DEX_SEARCH };

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
    /* search: its work (ZknSearchWork, ov210/zknsearch_types.h) */
    const u8 *sw;
    /* the opening screen: its work (ZukanSearchWork, ov211) */
    const u8 *splash;
    /* entry */
    int species, page;          /* page 1 info, 2 area, 3 voice, 4 form     */
    int index, total, number;
    const u16 *eids;
} DexNow;

static int g_skip;              /* the list drops the id list's first entry */

/* The number the game prints for a species (ZUKAN_LIST_PrintListRow, the
 * info page): in the Unova view its regional number, from the game's table
 * (member 0x29C of the personal archive: a u16 a species, 999 for one that
 * is not in the Unova Pokedex; Victini is 0), in the National view the
 * species number.  `fallback` when the view is not known. */
#define DEX_REGIONAL_ARC    "a/0/1/6"
#define DEX_REGIONAL_MEMBER 0x29Cu

static int dex_number(void *save, int species, int fallback)
{
    static u16 tab[650];
    static int state;           /* 0 not tried, 1 read, -1 failed */

    if (save == NULL || ZUKAN_SAVE_GetViewMode == NULL || species <= 0) {
        return fallback;
    }
    if (ZUKAN_SAVE_GetViewMode(save) != 0) {
        return species;
    }
    if (state == 0) {
        BwArcFile arc;
        u8 raw[sizeof tab];
        u32 i;

        state = -1;
        if (bw_arc_open(&arc, DEX_REGIONAL_ARC) == 0) {
            if (DEX_REGIONAL_MEMBER < arc.count && bw_arc_member_size(&arc, DEX_REGIONAL_MEMBER) == (long)sizeof raw
                && bw_arc_read_member(&arc, DEX_REGIONAL_MEMBER, raw, (u32)sizeof raw) == (long)sizeof raw) {
                for (i = 0; i < 650u; i++) {
                    tab[i] = (u16)(raw[i * 2u] | raw[i * 2u + 1u] << 8);
                }
                state = 1;
            }
            bw_arc_close(&arc);
        }
    }
    if (state != 1 || species >= 650 || tab[species] == 999) {
        return fallback;
    }
    return tab[species];
}

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
        v->splash = (const u8 *)rdptr(child, PR_WORK);
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
            if (ZUKAN_SAVE_GetFromGameData != NULL) {
                v->save = ZUKAN_SAVE_GetFromGameData((void *)rdptr(pw, ZD_GAMEDATA));
            }
            v->number = dex_number(v->save, v->species, v->index + (g_skip ? 0 : 1));
        }
    } else if (ov == 210) {
        v->sw = (const u8 *)rdptr(child, PR_WORK);
        v->kind = v->sw != NULL ? DEX_SEARCH : DEX_LOADING;
    } else {
        v->kind = DEX_NONE;
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
    int dev;                    /* a theme's device: two screens            */
    EidosUiRect s1, s2;         /* its screens (the Pokemon; the list or
                                   the entry's pages)                       */
} DexLayout;

/* A theme's device: how far it has slid open (0 closed .. 256 open, its
 * screens on), and whether it has been open since the Pokedex started. */
static int g_open = 256;
static int g_opened;
static int g_slide;             /* how far it has slid (0..200), in step with the game */
static int g_slid;              /* it is open */
static int g_lit_on;            /* its screens are coming on, or on */
static u32 g_lit_at;
static int g_auto_wait;         /* frames until the next touch on the opening screen */

static int imin(int a, int b) { return a < b ? a : b; }
static int imax(int a, int b) { return a > b ? a : b; }
static int iclamp(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

static void dex_layout(EidosUiCtx *c, DexLayout *L)
{
    int W = c->canvas->w, H = c->canvas->h - ui_bar_h(c);

    {
        int x = 0, y = 0, w = 0, h = 0;

        L->dev = 0;
        if (ui_app_window(c, "dex-screen1", &x, &y, &w, &h)) {
            /* the list and the header stand on the second screen */
            L->dev = 1;
            L->s1 = (EidosUiRect){ x, y, w, h };
            (void)ui_app_window(c, "dex-screen2", &x, &y, &w, &h);
            L->s2 = (EidosUiRect){ x, y, w, h };
            L->mx = x - 5;
            L->my = y - 4;
            L->mw = w + 10;
            L->mh = h + 9;
            L->bx = x;
            L->by = y + 26;
            L->bw = w;
            L->bh = y + h - L->by;
            return;
        }
    }
    L->mw = iclamp(W * 9 / 10, imin(W - 8, 300), 560);
    L->mh = iclamp(H * 9 / 10, imin(H - 8, 170), 320);
    L->mx = (W - L->mw) / 2;
    L->my = imax(2, (H - L->mh) / 2);
    (void)ui_app_window(c, "dex-size", &L->mx, &L->my, &L->mw, &L->mh);
    L->bx = L->mx + 5;
    L->by = L->my + 4 + 22 + 4;
    L->bw = L->mw - 10;
    L->bh = L->my + L->mh - 5 - L->by;
}

static void dex_modal(EidosUiCtx *c, DexLayout *L, const char *sub, const char *right)
{
    if (L->dev) {
        /* the device; its second screen's head when it is open (sub NULL:
         * no head) */
        int x = g_open, y = 0, w = 0, h = 0;

        (void)ui_app_window(c, "dex", &x, &y, &w, &h);
        if (g_open >= 256 && sub != NULL) {
            int hx = L->s2.x, hy = L->s2.y, hw = L->s2.w, hh = 22;

            (void)ui_app_window(c, "dex-head", &hx, &hy, &hw, &hh);
            ui_header(c, L->s2.x, L->s2.y, L->s2.w, sub, "", right, UI_DEX);
        }
        return;
    }
    {
        int x = L->mx, y = L->my, w = L->mw, h = L->mh;

        if (!ui_app_window(c, "dex", &x, &y, &w, &h)) {
            ui_panel(c, L->mx, L->my, L->mw, L->mh, UI_CUT_TL | UI_CUT_BR, 9, DEX_PANEL, UI_DEX2, 246);
            ui_panel(c, L->mx + 2, L->my + 2, L->mw - 4, L->mh - 4, UI_CUT_TL | UI_CUT_BR, 8,
                     DEX_PANEL, UI_DEX3, 255);
        }
    }
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
    int i, x = 6, y = c->canvas->h - ui_bar_h(c);

    if (!ui_hint_ground(c, 0, y, c->canvas->w, 17)) {
        ui_fill(c, 0, y, c->canvas->w, 17, UI_BODY2, 235);
        ui_fill(c, 0, y, c->canvas->w, 1, UI_DEX2, 255);
    }
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
    int W = c->canvas->w, H = c->canvas->h - ui_bar_h(c);
    EidosUiRect r[4] = {
        { 0, 0, W, L->my },
        { 0, L->my + L->mh, W, H - L->my - L->mh },
        { 0, L->my, L->mx, L->mh },
        { L->mx + L->mw, L->my, W - L->mx - L->mw, L->mh },
    };
    int i;

    if (L->dev) {
        return;                 /* the device fills the screen */
    }
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

    {
        int fx = x, fy = y, fw = w, fh = h;

        if (!ui_app_window(c, selected ? "dex-row-sel" : hover ? "dex-row-hot" : "dex-row", &fx, &fy, &fw, &fh)) {
            ui_panel(c, x, y, w, h, UI_CUT_TL | UI_CUT_BR, 4, fill, edge, 255);
        } else {
            x = fx;                     /* a theme's chosen row may stand apart */
        }
    }
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
static void dex_picture(EidosUiCtx *c, int species, int form, int shiny, int x, int y, int w, int h, int visible)
{
    int k;
    const UiImage *ic;

    ui_ellipse(c, x + w / 2 - w * 3 / 8, y + h - 18, w * 3 / 4, 14, UI_DEX2, UI_NONE, 255);
    ui_ellipse(c, x + w / 2 - w * 5 / 16, y + h - 15, w * 5 / 8, 9, UI_DEX3, UI_NONE, 255);
    if (!visible) {
        return;
    }
    if (form == 0 && ui_mon_front_anim((u16)species, shiny, 0, NULL, NULL) != NULL) {
        ui_mon_front_draw(c, (u16)species, shiny, bw_input_frame_number(), x, y, w, h - 8);
        return;
    }
    if (form != 0 && (ic = ui_mon_front((u16)species, (u8)form, shiny)) != NULL) {
        /* another form: its still picture */
        k = imax(1, imin(w / ic->w, (h - 8) / ic->h));
        ui_image(c, x + (w - ic->w * k) / 2, y + h - 8 - ic->h * k, ic, k);
        return;
    }
    ic = ui_mon_icon((u16)species, (u8)form, 0);
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

    {
        int fx = x, fy = y, fw = w, fh = h;

        if (!ui_app_window(c, "dex-panel", &fx, &fy, &fw, &fh)) {
            ui_panel(c, x, y, w, h, UI_CUT_TL | UI_CUT_BR, 6, DEX_PANEL2, UI_DEX2, 255);
        }
    }
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
        dex_picture(c, species, 0, 0, x + 6, py, w - 12, ph, state != 0);
    }
    ui_info_row(c, x + 6, y + h - 2 * (ih + 3) - 3, w - 12, "HT", ht);
    ui_info_row(c, x + 6, y + h - (ih + 3) - 1, w - 12, "WT", wt);
    (void)v;
}

/* The device's first screen: number, name, category, types, the picture
 * (of this form, shiny or not), height and weight. */
static void dex_face(EidosUiCtx *c, const EidosUiRect *r, int species, int number, int state, int form, int shiny)
{
    char s[8], name[64], cat[64], ht[40], wt[40];
    int t1 = UI_TYPE_UNKNOWN, t2 = UI_TYPE_UNKNOWN, ih = 15, x = r->x, y = r->y, w = r->w, h = r->h, py, ph;

    {
        int fx = x, fy = y, fw = w, fh = h;

        (void)ui_app_window(c, "dex-panel", &fx, &fy, &fw, &fh);
    }
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
        ui_type_tag(c, x + 8, y + 37, 48, t1);
        if (t2 != t1) {
            ui_type_tag(c, x + 8 + 52, y + 37, 48, t2);
        }
    } else {
        ui_type_tag(c, x + 8, y + 37, 48, UI_TYPE_UNKNOWN);
    }
    hw_strings(species, state == 2, ht, sizeof(ht), wt, sizeof(wt));
    py = y + 56;
    ph = h - 56 - 2 * (ih + 3) - 8;
    if (ph > 20) {
        dex_picture(c, species, form, shiny, x + 6, py, w - 12, ph, state != 0);
    }
    ui_info_row(c, x + 6, y + h - 2 * (ih + 3) - 4, w - 12, "HT", ht);
    ui_info_row(c, x + 6, y + h - (ih + 3) - 2, w - 12, "WT", wt);
}

static void dex_view(EidosUiView *view)
{
    view->mode = EIDOS_VIEW_WORLD;
    view->blur = 1;
    view->hold_world = 1;     /* an app: the field is not running */
    view->hide_2d = 1;
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
    snprintf(right, sizeof(right), ui_trf("SEEN %d  OWN %d"), seen, own);
    dex_layout(c, &L);
    if (L.dev) {
        /* a device that shows the counts itself (none on the list's head
         * then): SEEN and OWN as the list counts them, and the share owned
         * by the game's own completion rule (the species it leaves out of a
         * complete Pokedex are left out here) */
        int a = seen, b = own, part = 0, whole = 0;
        const char *dbg = getenv("EIDOS_DEBUG_DEX_OWN");

        for (i = 0; i < v->count; i++) {
            int sp = v->ids[i];
            int counted = v->mode == 1
                ? (ZUKAN_SAVE_IsCountedForNationalCompletion == NULL || ZUKAN_SAVE_IsCountedForNationalCompletion((u32)sp))
                : (ZUKAN_SAVE_IsCountedForRegionalCompletion == NULL || ZUKAN_SAVE_IsCountedForRegionalCompletion((u32)sp));

            if (counted) {
                whole++;
                part += dex_state(v, sp) == 2;
            }
        }
        if (dbg != NULL) {
            /* a test switch: this many owned (docs/test-switches.md) */
            b = iclamp(atoi(dbg), 0, v->count);
            a = imax(a, b);
            part = imin(b, whole);
        }
        if (ui_app_window(c, "dex-stats", &a, &b, &part, &whole)) {
            right[0] = '\0';
        }
    }
    if (L.dev && ui_text_w(right, UI_FONT_LARGE) + 80 > L.s2.w) {
        snprintf(right, sizeof(right), "%d / %d", seen, own);       /* a narrow screen */
    }
    dex_modal(c, &L, v->mode == 1 ? "NATIONAL" : "UNOVA", right);
    if (L.dev && g_open < 256) {
        dex_hints(c, NULL, 0);  /* the device is still sliding open */
        dex_view(view);
        return;
    }

    lw = iclamp(L.bw * 11 / 20, 200, L.bw - 140);
    lx = L.bx + (ui_theme_active_has_frame() ? 16 : 0);      /* room for a theme's leaning rows */
    rx = lx + lw + 6;
    rw = L.bx + L.bw - rx;
    listh = L.bh;
    vis = imax(1, (listh + gap) / (rowh + gap));
    if (vis > v->count) {
        vis = v->count;
    }
    if (L.dev) {
        /* the device: the list on its second screen, seven rows of it */
        lx = L.s2.x + 2;
        lw = L.s2.w - 2;
        rx = L.s1.x;
        rw = L.s1.w;
        rowh = iclamp((L.bh + gap) / 7 - gap, 22, 40);
        vis = imax(1, imin(v->count, (listh + gap) / (rowh + gap)));
    } else {
        int a = 0, b = 0, d = 0, e = 0;

        if (ui_app_window(c, "dex-flip", &a, &b, &d, &e)) {
            /* the DS's arrangement: the list at the right */
            rx = L.bx;
            lx = rx + rw + 6;
        }
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
        dex_row(c, lx, y, lw - 8, rowh, dex_number(v->save, species, row + 1), species, state,
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

        if (L.dev) {
            pr = L.s1;
            dex_face(c, &L.s1, sp, dex_number(v->save, sp, psel + 1), state, 0, 0);
        } else {
            dex_species_panel(c, v, rx, L.by, rw, L.bh, sp, dex_number(v->save, sp, psel + 1), state);
        }
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

/* ---- the entry on a theme's device: its pages on one screen ---- */

typedef struct DexVar { u8 form, gender, shiny; } DexVar;

static int g_view;              /* the form shown (an index into the seen ones) */
static int g_view_species = -1;
static u32 g_entry_prev;        /* the buttons held last frame */
static u32 g_cry_at;            /* the frame the cry last played */
static int g_cry_played;
static int g_park_wait;         /* frames until the next try for the AREA page */

/* The forms the player has seen, as the game's FORMS page lists them: a
 * species with forms by form, one without by gender; each normal and shiny. */
static int dex_variants(const DexNow *v, DexVar *out, int cap)
{
    int n = 0, nf, f, g, sh;

    nf = ZUKAN_SAVE_GetFormCount != NULL ? (int)ZUKAN_SAVE_GetFormCount((u32)v->species) : 1;
    nf = nf < 1 ? 1 : nf;
    if (v->save != NULL && ZUKAN_SAVE_HasSeenGenderForm != NULL) {
        for (sh = 0; sh < 2; sh++) {
            for (f = 0; f < nf; f++) {
                for (g = 0; g < 2; g++) {
                    if (n < cap && ZUKAN_SAVE_HasSeenGenderForm(v->save, (u16)v->species, (u32)g, (u32)sh, (u32)f) != 0) {
                        out[n].form = (u8)f;
                        out[n].gender = (u8)(nf >= 2 ? 2 : g);
                        out[n].shiny = (u8)sh;
                        n++;
                        if (nf >= 2) {
                            break;          /* a form counts once */
                        }
                    }
                }
            }
        }
    }
    if (n == 0) {
        out[0].form = 0;
        out[0].gender = 2;
        out[0].shiny = 0;
        n = 1;
    }
    return n;
}

/* The game's area map (its lower screen above the touch bar), fitted to
 * tw x th by averaging. */
#define MAP_SW 256
#define MAP_SH 168
static u8 g_map_px[MAP_SW * MAP_SH * 4];
static UiImage g_map_im;

static const UiImage *map_scaled(const u8 *scr, int tw, int th)
{
    int x, y;

    tw = iclamp(tw, 8, MAP_SW);
    th = iclamp(th, 8, MAP_SH);
    for (y = 0; y < th; y++) {
        int ya = y * MAP_SH / th, yb = imax(ya + 1, (y + 1) * MAP_SH / th);

        for (x = 0; x < tw; x++) {
            int xa = x * MAP_SW / tw, xb = imax(xa + 1, (x + 1) * MAP_SW / tw);
            int r = 0, g = 0, b = 0, n = 0, sx, sy;
            u8 *d = g_map_px + ((size_t)y * (size_t)tw + (size_t)x) * 4u;

            for (sy = ya; sy < yb; sy++) {
                for (sx = xa; sx < xb; sx++) {
                    const u8 *q = scr + ((size_t)sy * 256u + (size_t)sx) * 4u;

                    r += q[0];
                    g += q[1];
                    b += q[2];
                    n++;
                }
            }
            d[0] = (u8)(r / n);
            d[1] = (u8)(g / n);
            d[2] = (u8)(b / n);
            d[3] = 255;
        }
    }
    g_map_im.w = tw;
    g_map_im.h = th;
    g_map_im.rgba = g_map_px;
    return &g_map_im;
}

typedef struct WrapFit { EidosUiCtx *c; int x, y, lh, max_y, font, draw; } WrapFit;

static void wrap_fit(void *user, const char *start, int len)
{
    WrapFit *o = (WrapFit *)user;
    char buf[160];

    if (len >= (int)sizeof(buf)) {
        len = (int)sizeof(buf) - 1;
    }
    memcpy(buf, start, (size_t)len);
    buf[len] = '\0';
    if (o->draw && o->y + o->lh <= o->max_y) {
        ui_text(o->c, o->x, o->y, buf, UI_WHITE, UI_EDGE, o->font, 1);
    }
    o->y += o->lh;
}

/* A section of the entry's screen: the theme's box, its name on the head. */
#define SECT_HEAD 13
static void dex_sect(EidosUiCtx *c, const char *id, const char *label, int x, int y, int w, int h)
{
    int fx = x, fy = y, fw = w, fh = h;

    (void)ui_app_window(c, id, &fx, &fy, &fw, &fh);
    ui_text(c, x + 18, y + ui_text_y(SECT_HEAD, UI_FONT_SMALL), label, UI_WHITE, UI_EDGE, UI_FONT_SMALL, 1);
}

static void view_set(const DexNow *v, const DexVar *vars, int nv, int to)
{
    to = (to % nv + nv) % nv;
    if (to == g_view) {
        return;
    }
    g_view = to;
    eidos_ui_sound(0x548);
    /* the game's FORMS page keeps the form chosen the same way */
    if (v->save != NULL && ZUKAN_SAVE_UpdateViewFlags != NULL) {
        ZUKAN_SAVE_UpdateViewFlags(v->save, (u16)v->species, vars[to].gender == 1 ? 1u : 0u, vars[to].shiny, vars[to].form);
    }
}

static void run_entry_dev(EidosUiCtx *c, EidosUiView *view, const EidosUiFrame *in, const DexNow *v, DexLayout *L)
{
    DexVar vars[40];
    char text[600];
    int state = dex_state(v, v->species), nv, nh = 0, i;
    int x = L->s2.x + 3, w = L->s2.w - 6, y0 = L->s2.y + 3, y1 = L->s2.y + L->s2.h - 3;
    int rowh, yrow, tw, th, ymap, infoh, cryw, cell, have_text, font = UI_FONT_MEDIUM, lh = 14;
    DexHint hints[7];
    BwInputState st;
    u32 now, edge;

    dex_modal(c, L, NULL, NULL);
    nv = dex_variants(v, vars, 40);
    if (v->species != g_view_species) {
        g_view_species = v->species;
        g_view = 0;
    }
    if (g_view >= nv) {
        g_view = 0;
    }
    dex_face(c, &L->s1, v->species, v->number, state, vars[g_view].form, vars[g_view].shiny);

    /* the keys: the game's own page keys are not offered (its pages are one
     * screen here); up and down step through the Pokemon as its arrows do */
    bw_input_current(&st);
    now = in->buttons | st.buttons;
    edge = now & ~g_entry_prev;
    g_entry_prev = now;
    eidos_ui_take_keys(BW_KEY_LEFT | BW_KEY_RIGHT | BW_KEY_UP | BW_KEY_DOWN | BW_KEY_L | BW_KEY_R | BW_KEY_Y);
    if (edge & BW_KEY_UP) eidos_ui_tap(12, 180, 3);
    if (edge & BW_KEY_DOWN) eidos_ui_tap(36, 180, 3);
    if (edge & BW_KEY_R) view_set(v, vars, nv, g_view + 1);
    if (edge & BW_KEY_L) view_set(v, vars, nv, g_view - 1);
    /* the game stays on its AREA page: its lower screen is the map */
    if (g_park_wait > 0) {
        g_park_wait--;
    } else if (v->page != 2) {
        eidos_ui_tap(110, 180, 3);
        g_park_wait = 24;
    }

    /* from the foot up: the cry and the forms, the map, the text above.
     * The text has the lines it needs: in the medium letters when the whole
     * map fits under it, else in the small ones, the map smaller for it. */
    cell = L->s2.h >= 280 ? 34 : 29;
    rowh = SECT_HEAD + cell + 2;
    yrow = y1 - rowh;
    tw = imin(w - 6, MAP_SW);
    th = tw * MAP_SH / MAP_SW;
    have_text = state == 2 && msg_get(MSG_ENTRY, (u32)v->species, text, sizeof(text)) > 0;
    {
        WrapFit o;
        int room = yrow - 4 - (th + SECT_HEAD + 3) - 4 - y0, need;

        memset(&o, 0, sizeof o);
        o.lh = 14;
        o.font = UI_FONT_MEDIUM;
        need = SECT_HEAD + 6 + (have_text ? ui_wrap(text, w - 12, UI_FONT_MEDIUM, wrap_fit, &o) : 1) * 14;
        if (need > room) {
            font = UI_FONT_SMALL;
            lh = 11;
            need = SECT_HEAD + 6 + (have_text ? ui_wrap(text, w - 12, UI_FONT_SMALL, wrap_fit, &o) : 1) * 11;
            if (need > room) {
                th = imax(40, th - (need - room));
                tw = th * MAP_SW / MAP_SH;
            }
        }
    }
    ymap = yrow - 4 - (th + SECT_HEAD + 3);
    infoh = ymap - 4 - y0;

    dex_sect(c, "dex-sect-info", "INFO", x, y0, w, infoh);
    if (have_text) {
        WrapFit o;

        o.c = c;
        o.x = x + 6;
        o.max_y = y0 + infoh - 1;
        o.draw = 1;
        o.font = font;
        o.lh = lh;
        o.y = y0 + SECT_HEAD + 3;
        (void)ui_wrap(text, w - 12, font, wrap_fit, &o);
    } else {
        ui_text(c, x + 6, y0 + SECT_HEAD + 3, "No data yet.", UI_GREY, UI_EDGE, UI_FONT_MEDIUM, 1);
    }

    dex_sect(c, "dex-sect-area", "AREA", x, ymap, w, th + SECT_HEAD + 3);
    {
        int mx = x + (w - tw) / 2, my = ymap + SECT_HEAD + 1, px, py, pressed, released;

        if (v->page == 2 && in->screen_rgba[1] != NULL && tw >= 8 && th >= 8) {
            ui_image(c, mx, my, map_scaled(in->screen_rgba[1], tw, th), 1);
            /* a click on the map taps the game's screen at the same spot */
            (void)eidos_ui_pointer(&px, &py, &pressed, &released);
            if (pressed && px >= mx && py >= my && px < mx + tw && py < my + th) {
                eidos_ui_tap((px - mx) * MAP_SW / tw, (py - my) * MAP_SH / th, 3);
            }
        }
    }

    cryw = iclamp(w * 28 / 100, 44, 70);
    dex_sect(c, "dex-sect-cry", "CRY", x, yrow, cryw, rowh);
    {
        EidosUiRect r = { x, yrow, cryw, rowh };
        int hit = eidos_ui_hit(r);
        int playing = g_cry_played && in->frame - g_cry_at < 45u;
        int sx = x + cryw / 2 - 10, sy = yrow + SECT_HEAD + (rowh - SECT_HEAD) / 2 - 7;
        u32 col = UI_RAW(0xFFFFFFu);

        if ((hit & EIDOS_UI_CLICK) || (edge & BW_KEY_Y)) {
            eidos_ui_cry((u32)v->species);
            g_cry_at = in->frame;
            g_cry_played = 1;
            playing = 1;
        }
        /* a speaker: its box, its cone, the waves (they run while it plays;
         * the box lifts under the pointer) */
        sy -= (hit & EIDOS_UI_HOVER) ? 1 : 0;
        ui_fill(c, sx - 1, sy + 3, 6, 8, UI_RAW(0x101010u), 255);
        for (i = 0; i < 7; i++) {
            ui_fill(c, sx + 4 + i, sy + 3 - i, 1, 8 + 2 * i, UI_RAW(0x101010u), 255);
        }
        ui_fill(c, sx, sy + 4, 4, 6, col, 255);
        for (i = 0; i < 6; i++) {
            ui_fill(c, sx + 4 + i, sy + 4 - i, 1, 6 + 2 * i, col, 255);
        }
        for (i = 0; i < 3; i++) {
            int on = playing ? ((int)((in->frame - g_cry_at) / 5u) % 3) >= i : 1;

            ui_fill(c, sx + 13 + i * 3, sy + 4 - i * 2, 2, 6 + i * 4, UI_RAW(0x101010u), on ? 255 : 90);
            ui_fill(c, sx + 13 + i * 3, sy + 5 - i * 2, 1, 4 + i * 4, col, on ? 255 : 110);
        }
    }

    {
        int fx = x + cryw + 4, fw = w - cryw - 4, nvis, first, cy0 = yrow + SECT_HEAD + 1;

        dex_sect(c, "dex-sect-form", "FORMS", fx, yrow, fw, rowh);
        nvis = imax(1, (fw - 6) / (cell + 2));
        first = iclamp(g_view - nvis / 2, 0, imax(0, nv - nvis));
        for (i = 0; i < nvis && first + i < nv; i++) {
            const DexVar *dv = &vars[first + i];
            EidosUiRect r = { fx + 3 + i * (cell + 2), cy0, cell, cell };
            int hit = eidos_ui_hit(r), sel = first + i == g_view;
            const UiImage *ic = ui_mon_icon((u16)v->species, dv->form, 0);
            EidosUiRect saved;

            ui_fill(c, r.x, r.y, r.w, r.h, sel ? UI_DEX : (hit & EIDOS_UI_HOVER) ? UI_LIGHT : UI_EDGE2, 255);
            ui_fill(c, r.x + 1, r.y + 1, r.w - 2, r.h - 2, sel ? UI_DEX3 : UI_BODY2, 255);
            if (ic != NULL && state != 0) {
                ui_push(c, 0, 0, &saved);
                ui_clip(c, r.x + 1, r.y + 1, r.w - 2, r.h - 2);
                ui_image(c, r.x + (cell - ic->w) / 2, r.y + (cell - ic->h) / 2 - 2, ic, 1);
                ui_pop(c, 0, 0, &saved);
            }
            if (dv->gender < 2) {
                ui_gender(c, r.x + cell - 9, r.y + cell - 11, dv->gender, UI_FONT_SMALL);
            }
            if (dv->shiny) {
                /* a small star */
                ui_fill(c, r.x + 5, r.y + 3, 1, 5, UI_RAW(0xFFD838u), 255);
                ui_fill(c, r.x + 3, r.y + 5, 5, 1, UI_RAW(0xFFD838u), 255);
                ui_fill(c, r.x + 4, r.y + 4, 3, 3, UI_RAW(0xFFF0A0u), 255);
            }
            if (hit & EIDOS_UI_CLICK) {
                view_set(v, vars, nv, first + i);
            }
        }
        if (first + nvis < nv) {
            ui_text(c, fx + fw - 9, cy0 + cell / 2 - 5, "\xE2\x86\x92", UI_LIGHT, UI_EDGE, UI_FONT_SMALL, 1);
        }
    }

    hints[nh++] = (DexHint){ "B", "List", 0, 244, 180 };
    hints[nh++] = (DexHint){ "X", "Close", 0, 220, 180 };
    hints[nh++] = (DexHint){ "\xE2\x86\x91", "Previous", 0, 12, 180 };
    hints[nh++] = (DexHint){ "\xE2\x86\x93", "Next", 0, 36, 180 };
    hints[nh++] = (DexHint){ "Y", "Cry", BW_KEY_Y, 0, 0 };
    if (nv > 1) {
        hints[nh++] = (DexHint){ "L R", "Form", BW_KEY_R, 0, 0 };
    }
    dex_hints(c, hints, nh);
    dex_view(view);
}

static void run_entry(EidosUiCtx *c, EidosUiView *view, const EidosUiFrame *in,
                      const DexNow *v)
{
    static const char *const tabs[4] = { "INFO", "AREA", "CRY", "FORM" };
    /* the centres of the game's own INFO, AREA, CRY and FORM buttons on its
     * bottom bar (as drawn; issue #22: 128 was on AREA) */
    static const int tab_tap_x[4] = { 72, 110, 150, 191 };
    DexLayout L;
    char right[40], text[600];
    int state = dex_state(v, v->species), i, lw, rx, rw, tx, ty;
    DexHint hints[6];
    int nh = 0;

    snprintf(right, sizeof(right), ui_trf("No. %03d"), v->number);
    dex_layout(c, &L);
    if (L.dev) {
        run_entry_dev(c, view, in, v, &L);
        return;
    }
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
    if (L.dev) {
        /* the device opens by itself (dex_run) */
        dex_modal(c, &L, NULL, NULL);
        dex_hints(c, &hint, 0);
        dex_view(view);
        return;
    }
    dex_modal(c, &L, "", "");
    ui_text(c, L.bx + (L.bw - ui_text_w("POK\xC3\x89" "DEX", UI_FONT_LARGE) * 3) / 2,
            L.by + L.bh / 2 - 22, "POK\xC3\x89" "DEX", UI_DEX, UI_DEX3, UI_FONT_LARGE, 3);
    all.x = 0; all.y = 0; all.w = c->canvas->w; all.h = c->canvas->h - ui_bar_h(c);
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


/* ------------------------------------------------------------------ */
/* The search                                                          */
/* ------------------------------------------------------------------ */

/*
 * The Pokedex search (overlay 210, ov210/zknsearch_*.c; mock-up
 * "dex_search") in the Pokedex's window: the game's rows (ORDER, NAME,
 * TYPE, COLOR, FORM) as tiles with their values in the game's letters,
 * RESET and START, and the game's prompt as a system box.  A row opened
 * shows the game's own list of choices (the FrameList it fills) with the
 * marks the game puts on the chosen ones.  Every click taps the game's own
 * button or row where the game put it; the keys stay the game's.
 *
 * ZknSearchWork: bmpWin[16] 0x14C (8 bytes each: 0 the title, 1 the
 * prompt, 2..6 the row names, 7..10 the values of ORDER, NAME, TYPE and
 * COLOR, 11 START), frameList 0x1E0 (set while a row's list shows),
 * entries[50] 0x1E4 (Strbufs: the list's choices), cursorIndex 0x2C1 (the
 * row the keys are on).  FrameList: cursor 0x5C, visibleCount 0x5E,
 * scrollPos 0x62.  The lower screen: the rows at (192, 12 + 24 * row),
 * FORM at 116, RESET (26, 155), START (192, 155), back (244, 180); a
 * list's rows at (190, 12 + 24 * row), its page arrows (198, 179) and
 * (219, 179).
 */
extern u16 *STRBUF_GetChars(void *strbuf) DEX_WEAK;
extern u16  STRBUF_GetLength(void *strbuf) DEX_WEAK;

static const void *zs_win(const u8 *sw, int i)
{
    const u8 *win = (const u8 *)rdptr(sw, 0x14C + 8u * (u32)i);

    return win != NULL ? ui_bmpwin_bmp(win) : NULL;
}

static void zs_text(const u8 *sw, int i, char *out, int cap)
{
    u8 *sb = (u8 *)rdptr(sw, 0x1E4 + 4u * (u32)i);
    const u16 *u;
    int n = 0, k, len;

    out[0] = '\0';
    if (sb == NULL || STRBUF_GetChars == NULL || STRBUF_GetLength == NULL || (u = STRBUF_GetChars(sb)) == NULL) {
        return;
    }
    len = STRBUF_GetLength(sb);
    for (k = 0; k < len && u[k] != 0xFFFFu; k++) {
        if (u[k] == 0xF000u) {
            k += 2 + (k + 2 < len ? u[k + 2] : 0);
            continue;
        }
        if (u[k] >= 0xD800u && u[k] <= 0xF8FFu) {
            continue;
        }
        n = put_utf8(out, n, cap, u[k]);
    }
    out[n] = '\0';
}

/* The game's mark on the list row at DS y: a red tick in the box at the
 * row's left (x 128..142). */
static int zs_marked(const EidosUiFrame *in, int y)
{
    int x, yy, red = 0;

    if (in->screen_rgba[1] == NULL || y < 6 || y > 180) {
        return 0;
    }
    for (yy = y - 6; yy <= y + 6; yy++) {
        for (x = 128; x < 142; x++) {
            const u8 *p = in->screen_rgba[1] + ((size_t)yy * 256 + (size_t)x) * 4;

            red += p[0] > 180 && p[1] < 90 && p[2] < 90;
        }
    }
    return red > 6;
}

/* A battle tile; the hit state, 0x100 when it is lit. */
static int zs_tile(EidosUiCtx *c, int x, int y, int w, int h, int lit)
{
    EidosUiRect r;
    int st, rr, lift;

    r.x = x; r.y = y; r.w = w; r.h = h;
    st = eidos_ui_hit(r);
    lit = lit || (st & EIDOS_UI_HOVER);
    lift = lit ? -1 : 0;
    ui_panel(c, x, y + lift, w, h, UI_CUT_TL | UI_CUT_BR, 5, lit ? 0x05343Eu : UI_BODY, lit ? 0x008BACu : UI_EDGE, 242);
    for (rr = 3; rr < h - 3; rr++) {
        int left = 3 + (rr < 7 ? 7 - rr : 0);

        if (left < 6) {
            ui_fill(c, x + left, y + lift + rr, 6 - left, 1, lit ? 0x008BACu : UI_EDGE2, 255);
        }
    }
    return st | (lit ? 0x100 : 0);
}

static void run_search(EidosUiCtx *c, EidosUiView *view, const EidosUiFrame *in, const DexNow *v)
{
    static const DexHint hints[] = { { "A", "Choose", BW_KEY_A, 0, 0 }, { "B", "Back", 0, 244, 180 } };
    const u8 *sw = v->sw, *fl = (const u8 *)rdptr(sw, 0x1E0);
    DexLayout L;
    int i, x, y, w, rowh = 26, lit_row = eidos_ui_keys_lit() ? sw[0x2C1] : -1;
    int bw = 0, bh = 0;

    view->mode = EIDOS_VIEW_WORLD;
    view->blur = 1;
    view->hold_world = 1;
    view->hide_2d = 1;
    dex_layout(c, &L);
    dex_modal(c, &L, L.dev ? NULL : "", "");
    if (L.dev) {
        /* the device: the game's title on the second screen's head; on the
         * first a search glass and the game's prompt */
        int hx = L.s2.x, hy = L.s2.y, hw = L.s2.w, hh = 22;
        int gx = L.s1.x + L.s1.w / 2, gy = L.s1.y + L.s1.h * 36 / 100, r = iclamp(L.s1.h / 8, 16, 34), k;
        const void *pwin = zs_win(sw, 1);
        int pw2 = 0, ph2 = 0;

        (void)ui_app_window(c, "dex-head", &hx, &hy, &hw, &hh);
        if (ui_letters(NULL, zs_win(sw, 0), 0, 0, 0, 0, &bw, &bh)) {
            ui_letters(c, zs_win(sw, 0), L.s2.x + 6, L.s2.y + (24 - bh) / 2, UI_WHITE, UI_DEX3, NULL, NULL);
        }
        ui_ellipse(c, gx - r - 3, gy - r - 3, 2 * r + 6, 2 * r + 6, UI_RAW(0x101010u), UI_NONE, 255);
        ui_ellipse(c, gx - r, gy - r, 2 * r, 2 * r, UI_RAW(0xFFFFFFu), UI_NONE, 255);
        ui_ellipse(c, gx - r + 5, gy - r + 5, 2 * r - 10, 2 * r - 10, UI_RAW(0x101010u), UI_NONE, 255);
        ui_ellipse(c, gx - r + 7, gy - r + 7, 2 * r - 14, 2 * r - 14, UI_RAW(0x8CC8ECu), UI_NONE, 255);
        ui_ellipse(c, gx - r + 10, gy - r + 10, r * 6 / 10, r * 6 / 10, UI_RAW(0xDCF2FFu), UI_NONE, 255);
        for (k = 0; k < r; k++) {
            ui_fill(c, gx + r * 7 / 10 + k - 2, gy + r * 7 / 10 + k - 2, 9, 3, UI_RAW(0x101010u), 255);
        }
        for (k = 0; k < r - 2; k++) {
            ui_fill(c, gx + r * 7 / 10 + k, gy + r * 7 / 10 + k - 1, 5, 1, UI_RAW(0xFFFFFFu), 255);
        }
        if (pwin != NULL && ui_letters(NULL, pwin, 0, 0, 0, 0, &pw2, &ph2)) {
            ui_letters(c, pwin, L.s1.x + (L.s1.w - pw2) / 2, gy + 2 * r + 14, UI_WHITE, UI_EDGE, NULL, NULL);
        }
    } else if (ui_letters(NULL, zs_win(sw, 0), 0, 0, 0, 0, &bw, &bh)) {
        /* the game's title where the header's sub-title goes */
        ui_letters(c, zs_win(sw, 0), L.mx + 80, L.my + 8, UI_WHITE, UI_DEX3, NULL, NULL);
    }
    x = L.bx + 6;
    w = L.bw - 12;
    y = L.by + 2;
    if (fl == NULL) {
        /* the rows, the values at their right */
        static const int row_y[5] = { 12, 36, 60, 84, 116 };

        for (i = 0; i < 5; i++) {
            int st = zs_tile(c, x, y + i * (rowh + 3), w, rowh, i == lit_row);
            int dy = (st & 0x100) ? -1 : 0;
            int vw = 0, vh = 0;

            ui_letters(c, zs_win(sw, 2 + i), x + 12, y + i * (rowh + 3) + dy + 6, UI_WHITE, 0x313131u, NULL, NULL);
            if (i < 4 && ui_letters(NULL, zs_win(sw, 7 + i), 0, 0, 0, 0, &vw, &vh)) {
                ui_letters(c, zs_win(sw, 7 + i), x + w - 14 - vw, y + i * (rowh + 3) + dy + (rowh - vh) / 2,
                           UI_WHITE, 0x313131u, NULL, NULL);
            }
            if (st & EIDOS_UI_CLICK) {
                eidos_ui_tap(192, row_y[i], 3);
            }
        }
        y += 5 * (rowh + 3) + 2;
        {
            int half = (w - 4) / 2, st, dy, sw2 = 0, sh2 = 0;

            st = zs_tile(c, x, y, half, rowh, lit_row == 5);
            dy = (st & 0x100) ? -1 : 0;
            ui_text(c, x + 12, y + dy + ui_text_y(rowh, UI_FONT_LARGE), "RESET", UI_WHITE, 0x313131u, UI_FONT_LARGE, 1);
            if (st & EIDOS_UI_CLICK) {
                eidos_ui_tap(26, 155, 3);
            }
            st = zs_tile(c, x + half + 4, y, half, rowh, lit_row == 6);
            dy = (st & 0x100) ? -1 : 0;
            if (ui_letters(NULL, zs_win(sw, 11), 0, 0, 0, 0, &sw2, &sh2)) {
                ui_letters(c, zs_win(sw, 11), x + half + 16, y + dy + (rowh - sh2) / 2, UI_WHITE, 0x313131u, NULL, NULL);
            }
            if (st & EIDOS_UI_CLICK) {
                eidos_ui_tap(192, 155, 3);
            }
        }
    } else {
        /* a row's list: the game's choices, its marks, its cursor */
        int vis = (int)rd16p(fl, 0x5E), cur = (int)(s16)rd16p(fl, 0x5C), scroll = (int)(s16)rd16p(fl, 0x62);
        int total = 0, cols = 2, cw2 = (w - 4) / 2;
        char t[48];

        for (i = 0; i < 50 && rdptr(sw, 0x1E4 + 4u * (u32)i) != NULL; i++) {
            total++;
        }
        if (vis < 1 || vis > 8) vis = 7;
        for (i = 0; i < vis && scroll + i < total; i++) {
            int ry = y + (i / cols) * (rowh + 3), rx = x + (i % cols) * (cw2 + 4);
            int st = zs_tile(c, rx, ry, cw2, rowh, i == cur && eidos_ui_keys_lit());
            int dy = (st & 0x100) ? -1 : 0, mark = zs_marked(in, 12 + 24 * i);

            zs_text(sw, scroll + i, t, (int)sizeof t);
            ui_panel(c, rx + 12, ry + dy + 5, 12, 12, 0, 0, mark ? 0x008BACu : UI_BODY2, UI_EDGE2, 255);
            if (mark) {
                ui_fill(c, rx + 15, ry + dy + 10, 2, 2, UI_WHITE, 255);
                ui_fill(c, rx + 17, ry + dy + 12, 2, 2, UI_WHITE, 255);
                ui_fill(c, rx + 19, ry + dy + 10, 2, 2, UI_WHITE, 255);
                ui_fill(c, rx + 21, ry + dy + 8, 2, 2, UI_WHITE, 255);
            }
            ui_text(c, rx + 30, ry + dy + ui_text_y(rowh, UI_FONT_LARGE), t, UI_WHITE, 0x313131u, UI_FONT_LARGE, 1);
            if (st & EIDOS_UI_CLICK) {
                eidos_ui_tap(190, 12 + 24 * i, 3);
            }
        }
        if (total > vis) {
            int ay = y + ((vis + 1) / cols) * (rowh + 3);

            if (zs_tile(c, x, ay, cw2, rowh, 0) & EIDOS_UI_CLICK) {
                eidos_ui_tap(198, 179, 3);
            }
            ui_text(c, x + 12, ay + ui_text_y(rowh, UI_FONT_LARGE), "<<", UI_WHITE, 0x313131u, UI_FONT_LARGE, 1);
            if (zs_tile(c, x + cw2 + 4, ay, cw2, rowh, 0) & EIDOS_UI_CLICK) {
                eidos_ui_tap(219, 179, 3);
            }
            ui_text(c, x + cw2 + 16, ay + ui_text_y(rowh, UI_FONT_LARGE), ">>", UI_WHITE, 0x313131u, UI_FONT_LARGE, 1);
        }
    }
    /* the game's prompt */
    {
        const u8 *pw = (const u8 *)rdptr(sw, 0x14C + 8u);
        int ph = pw != NULL && !L.dev ? ui_textbox_h(pw) : 0;

        if (ph > 0) {
            ui_textbox_draw(c, UI_TEXTBOX_SYSTEM, L.bx, L.my + L.mh - 6 - ph, L.bw, pw, -1, -1);
        }
    }
    if (eidos_ui_right_click()) {
        eidos_ui_tap(244, 180, 3);
    }
    dex_hints(c, hints, 2);
}

static int g_last_kind = -1;    /* the dex screen shown last frame */

/* The splash opening onto the list: its body splits along the middle and
 * the two halves slide apart, the list behind them. */
#define DOOR_FRAMES 18
static int g_doors;             /* frames of the opening left              */
static int g_door_n = DOOR_FRAMES;
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
    int dev;

    if (!dex_probe(&v)) {
        return;
    }
    /* the splash, the list and an entry are one scene to the interface:
     * a change between them crossfades like a change of scene */
    {
        int a = 0, b = 0, d = 0, e = 0;

        dev = ui_app_window(c, "dex-screen1", &a, &b, &d, &e);
    }
    if (v.kind != g_last_kind) {
        if (g_from_splash && v.kind == DEX_LIST) {
            g_doors = dev ? 0 : DOOR_FRAMES;      /* the splash opens onto the list */
            g_door_n = DOOR_FRAMES;
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
    if (dev) {
        /*
         * A theme's device opens by itself, in step with the game's own
         * opening screen (ov211, ZukanSearchWork: timer 0x0A, state 0x0B,
         * procState 0x0C).  That screen waits for a touch (procState 4): it
         * gets one at once.  Its state 0 slides the DS's picture open for 26
         * frames and ends on a click: the device slides with it.  Its state
         * 1 then waits 32 frames: 26 of them are skipped.  From state 2 (its
         * second sound) the screens come on.  The interface draws through
         * the game's fades here.
         */
        int on = v.kind == DEX_LIST || v.kind == DEX_ENTRY || v.kind == DEX_SEARCH;

        eidos_ui_no_hold();
        if (v.kind == DEX_SPLASH && v.splash != NULL) {
            u32 ps = rd32p(v.splash, 0x0C);
            int st = v.splash[0x0B], tm = v.splash[0x0A];
            int a = 0, b = 0, d = 0, e = 0;

            if (!g_slid) {
                (void)ui_app_window(c, "dex-stats", &a, &b, &d, &e);       /* the lid: no counts yet */
            }
            if (ps == 4) {
                if (g_auto_wait > 0) {
                    g_auto_wait--;
                } else {
                    eidos_ui_tap(128, 96, 3);
                    g_auto_wait = 6;
                }
            } else if (ps == 5) {
                if (st == 0) {
                    g_slide = tm >= 26 ? 200 : tm * 200 / 26;
                } else {
                    g_slide = 200;
                    g_slid = 1;
                    if (st == 1 && tm < 26) {
                        ((u8 *)v.splash)[0x0A] = 26;
                    }
                    if (st >= 2 && !g_lit_on) {
                        g_lit_on = 1;
                        g_lit_at = in->frame;
                    }
                }
            }
        } else if (on && !g_lit_on) {
            /* a screen with no opening before it: open, the screens come on */
            g_slide = 200;
            g_slid = 1;
            g_lit_on = 1;
            g_lit_at = in->frame;
        }
        if (g_lit_on) {
            u32 e = in->frame - g_lit_at;

            g_open = e >= 10u ? 256 : 200 + (int)e * 56 / 10;
        } else {
            g_open = g_slide;
        }
        if (on) {
            g_opened = 1;
        }
    }
    switch (v.kind) {
    case DEX_LIST:
        run_list(c, view, in, &v);
        break;
    case DEX_ENTRY:
        run_entry(c, view, in, &v);
        break;
    case DEX_SEARCH:
        run_search(c, view, in, &v);
        break;
    default:
        run_splash(c, view, in, &v);
        break;
    }
    if (g_doors > 0 && v.kind == DEX_LIST) {
        /* the doors wait, shut, while the splash's picture is held through
         * the game's fade, and open once the list shows; they are the
         * change of screen, so no crossfade on top */
        if (!dev) {
            draw_doors(c, g_doors);
        }
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
    g_open = 256;
    g_opened = 0;
    g_slide = 0;
    g_slid = 0;
    g_lit_on = 0;
    g_auto_wait = 0;
    g_view_species = -1;
    g_entry_prev = 0;
    g_cry_played = 0;
    g_park_wait = 0;
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
