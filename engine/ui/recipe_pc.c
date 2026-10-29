/*
 * engine/ui/recipe_pc.c -- the one-screen recipe for the PC boxes (ov170,
 * "BOX2"; docs/unified-view-recipes.md, section 11).
 *
 * What it shows
 * -------------
 *
 * Three columns: the party on the left (the kit's party rows with the
 * game's Pokemon icons), the box in the middle (the box name with its
 * arrows, the 6 x 5 grid with the game's icons, the cursor), and the
 * details of the Pokemon under the mouse or the game's cursor on the right
 * (name, level, types, ability, held item, the four moves).  A bar under
 * the columns carries the game's own message ("Dom is selected."); the
 * game's action list (DEPOSIT / SUMMARY / MARKING / RELEASE / CANCEL, and
 * the lists of the other modes) and its yes/no menu are drawn as buttons
 * where the game's own were.
 *
 * Where the state comes from
 * --------------------------
 *
 * Everything shown is read from the game's own structs, nothing is kept:
 *   - the proc on the game's proc stack, overlay 170, its BoxTask (cursor
 *     slot, current box, state) and BoxParam (mode, the game data, the
 *     party);
 *   - the box Pokemon through BOX_SAVEDATA_GetPokemon and
 *     POKE_TOOL_GetBoxValue, the party through POKEPARTY_GetMember, the box
 *     names from the box save header;
 *   - the texts (species, moves, abilities, items, the action lists, the
 *     modes' titles) from the game's message archive a/0/0/2 (banks 70,
 *     203, 182, 54, 9 and 10), decoded like the kit decodes its font;
 *   - where the game's windows are: the game's own window frame manager
 *     (BGWINFRM_GetPos on BoxWork.winMgr: the action list and the party
 *     tray), and, for what has no struct (how many list buttons, the white
 *     message window, YES / NO), a few pixels of the bottom screen.
 *
 * What a click does
 * -----------------
 *
 * The game keeps all the logic.  A click becomes a stylus tap at the centre
 * of the bottom-screen rectangle the game tests for that control (the box
 * grid, the party tray, the box arrows, the PARTY PKMN button, the buttons
 * of the action list, YES and NO); a drag is the stylus held while it moves
 * from slot to slot.  The game's tray (the party panel) is on the DS screen
 * only when the game has opened it, so a click on a party row first taps
 * the PARTY PKMN button and waits for the tray.
 *
 * What it leaves to the DS screens (the recipe does not match, so the two
 * screens are shown side by side and the mouse is the stylus): the item and
 * battle-box modes, the marking panel, the box list and the wallpaper menu,
 * and every sub screen (summary, bag, name entry), so the player is never
 * stuck.  EIDOS_PC_DEBUG logs the task and the windows every 10 frames;
 * EIDOS_PC_DRAG scripts a mouse drag for checks.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "ui.h"
#include "kit.h"
#include "dsmem.h"
#include "input.h"
#include "port.h"

typedef uintptr_t PcPtr;

#if defined(__GNUC__) || defined(__clang__)
#define BW_PC_WEAK __attribute__((weak))
#else
#define BW_PC_WEAK
#endif

/* ------------------------------------------------------------------ */
/* The game's own functions (arm9; the port links them by name)        */
/* ------------------------------------------------------------------ */

extern void *BOX_SAVEDATA_GetPokemon(void *gameData, u32 box, u32 slot) BW_PC_WEAK;
extern void *BOX_SAVEDATA_GetHeader(void *gameData) BW_PC_WEAK;
extern u32   POKE_TOOL_GetBoxValue(void *boxMon, u32 paramId, PcPtr arg) BW_PC_WEAK;
extern u32   POKE_TOOL_GetPartyValue(void *partyMon, u32 paramId, PcPtr arg) BW_PC_WEAK;
extern void *POKE_TOOL_CastToBoxParam(void *partyMon) BW_PC_WEAK;
extern void *POKEPARTY_GetMember(void *party, u32 pos) BW_PC_WEAK;
extern u32   POKEPARTY_GetCount(const void *party) BW_PC_WEAK;
extern void  BGWINFRM_GetPos(void *wk, u32 idx, s8 *x, s8 *y) BW_PC_WEAK;

/* ------------------------------------------------------------------ */
/* The game's structs, read at their offsets (ILP32, ov170/box2_types.h) */
/* ------------------------------------------------------------------ */

#define GS_ADDR_PTR      0x02146248u    /* _GameSystemGlobal.gameSystem   */
#define GS_PROCSYS       0x08
#define PS_CURRENT       0x08
#define PROC_OVERLAY     0x04
#define PROC_SEQ         0x08
#define PROC_WORK        0x14

#define BT_PARAMS        0x00           /* BoxTask                        */
#define BT_PROCSYS       0x04
#define BT_CURBOX        0x14
#define BT_BOXLIMIT      0x15
#define BT_CURSOR        0x16
#define BT_HELD          0x17
#define BT_HANDMODE      0x18
#define BT_PHASE         0x1A
#define BT_STATE         0x28
#define BT_WORK          0x2C

#define BP_GAMEDATA      0x04           /* BoxParam                       */
#define BP_PARTY         0x08
#define BP_MODE          0x24

#define BW_CURSORMOVE    0x70           /* BoxWork: the keys' cursor      */
#define BW_WINMGR        0x74           /* BoxWork                        */
#define CM_CUR           0x0D           /* CursorMove.cur                 */
#define BW_TASKMENU      0x1A0          /* the yes/no menu, 0 when closed */

#define OVERLAY_PC       170

#define SLOT_PARTY0      30             /* slots 0..29 box, 30..35 party  */
#define SLOT_COUNT       36

static u32 rd32(const u8 *p) { return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24); }
static u16 rd16(const u8 *p) { return (u16)((u32)p[0] | ((u32)p[1] << 8)); }

#ifdef BW_PC_TEST
/* The test (tests/test_ui_pc.c) includes this file and builds stand-ins for
 * the game's structs in its own memory: it names the cell that holds the
 * game system, and any non-null address is a pointer. */
extern u8 *g_pc_test_gs_cell;
static u8 *pc_gs_cell(void) { return g_pc_test_gs_cell; }
static int pc_identity(void) { return 1; }
static u8 *gptr(u32 v)
{
    return v != 0 ? (u8 *)bw_ds_try_ptr(v) : NULL;
}
#else
static u8 *pc_gs_cell(void) { return (u8 *)bw_ds_try_ptr(GS_ADDR_PTR); }
static int pc_identity(void) { return bw_ds_identity(); }

/* A game pointer (a host pointer on this identity-mapped build), or NULL
 * when it is not in main RAM. */
static u8 *gptr(u32 v)
{
    if (v < BW_DS_MAIN_BASE || v >= BW_DS_MAIN_BASE + BW_DS_MAIN_SIZE) {
        return NULL;
    }
    return (u8 *)(PcPtr)v;
}
#endif

typedef struct PcGame {
    u8 *task;               /* BoxTask                                    */
    u8 *params;             /* BoxParam                                   */
    u8 *work;               /* BoxWork                                    */
    u8 *winmgr;             /* BoxWork.winMgr                             */
    void *gd;               /* the game data the BOX_SAVEDATA calls take  */
    void *party;
    int mode;               /* BoxParam.mode                              */
    int cur_box;
    int box_limit;
    int cursor;             /* BoxTask.cursorSlot                         */
    int state;              /* BoxTask.state                              */
    int hand_mode;          /* BoxTask.handMode                           */
    int taskmenu;           /* a yes/no menu is open                      */
    u8 *cm;                 /* BoxWork.cursorMove: the keys' cursor       */
} PcGame;

static PcGame g_g;

/* The overlay-170 box application's task, when it is the proc on top of the
 * game's stack and no sub screen (summary, name entry, bag) runs inside. */
static int pc_read_game(PcGame *g)
{
    u8 *gsp = pc_gs_cell();
    u8 *gs, *ps, *proc, *task, *params, *nested;

    memset(g, 0, sizeof(*g));
    if (gsp == NULL || !pc_identity()) {     /* game pointers are host pointers */
        return 0;
    }
    gs = gptr(rd32(gsp));
    if (gs == NULL) {
        return 0;
    }
    ps = gptr(rd32(gs + GS_PROCSYS));
    if (ps == NULL) {
        return 0;
    }
    proc = gptr(rd32(ps + PS_CURRENT));
    if (proc == NULL || (s32)rd32(proc + PROC_OVERLAY) != OVERLAY_PC ||
        (s32)rd32(proc + PROC_SEQ) < 2) {
        return 0;
    }
    task = gptr(rd32(proc + PROC_WORK));
    if (task == NULL) {
        return 0;
    }
    params = gptr(rd32(task + BT_PARAMS));
    g->work = gptr(rd32(task + BT_WORK));
    if (params == NULL || g->work == NULL) {
        return 0;
    }
    /* a sub screen runs on the task's own proc stack */
    nested = gptr(rd32(task + BT_PROCSYS));
    if (nested != NULL && rd32(nested + PS_CURRENT) != 0) {
        return 0;
    }
    g->task = task;
    g->params = params;
    g->winmgr = gptr(rd32(g->work + BW_WINMGR));
    g->gd = gptr(rd32(params + BP_GAMEDATA));
    g->party = gptr(rd32(params + BP_PARTY));
    g->mode = (int)rd32(params + BP_MODE);
    g->cur_box = task[BT_CURBOX];
    g->box_limit = task[BT_BOXLIMIT];
    g->cursor = task[BT_CURSOR];
    g->state = (int)rd32(task + BT_STATE);
    g->hand_mode = task[BT_HANDMODE];
    g->taskmenu = rd32(g->work + BW_TASKMENU) != 0;
    g->cm = gptr(rd32(g->work + BW_CURSORMOVE));
    return g->winmgr != NULL && g->gd != NULL && g->party != NULL;
}

/* ------------------------------------------------------------------ */
/* Debug log (EIDOS_PC_DEBUG): the task bytes every few frames          */
/* ------------------------------------------------------------------ */

static void pc_debug(const PcGame *g, u32 frame)
{
    static int on = -1;
    int i;

    if (on < 0) {
        on = getenv("EIDOS_PC_DEBUG") != NULL;
    }
    if (!on || frame % 10 != 0) {
        return;
    }
    {
        fprintf(stderr, "pcdbg f=%u mode=%d box=%d cur=%d kcur=%d state=%d tm=%d task:", frame,
                g->mode, g->cur_box, g->cursor, g->cm != NULL ? g->cm[CM_CUR] : -1, g->state,
                g->taskmenu);
    }
    for (i = 0x10; i < 0x2C; i++) {
        fprintf(stderr, " %02x", g->task[i]);
    }
    fprintf(stderr, " | seq: st=%u tm=%u | heldI=%d cx=%d cy=%d\n",
            (unsigned)rd16(g->work + 0x58 + 0x0C), (unsigned)rd16(g->work + 0x58 + 0x0E),
            (int)rd32(g->work + 0xA16C), (int)rd32(g->work + 0xA180),
            (int)rd32(g->work + 0xA184));
    fprintf(stderr, "pcdbg   work:");
    for (i = 0xA150; i < 0xA190; i++) {
        fprintf(stderr, "%s%02x", ((i - 0xA150) % 4 == 0) ? " " : "", g->work[i]);
    }
    fputc(10, stderr);
    if (BGWINFRM_GetPos != NULL && g->winmgr != NULL) {
        fprintf(stderr, "pcdbg   frames:");
        for (i = 0; i < 12; i++) {
            s8 x = 0, y = 0;

            BGWINFRM_GetPos(g->winmgr, (u32)i, &x, &y);
            fprintf(stderr, " %d=(%d,%d)", i, x, y);
        }
        fprintf(stderr, "\n");
    }
}

/* ------------------------------------------------------------------ */
/* The game's texts (message archive a/0/0/2)                           */
/* ------------------------------------------------------------------ */

enum { TXT_SPECIES, TXT_MOVE, TXT_ABILITY, TXT_ITEM, TXT_BOX, TXT_PROMPT, TXT_COUNT };

static const u32 k_bank_member[TXT_COUNT] = { 70, 203, 182, 54, 9, 10 };

typedef struct PcBank {
    u8   *data;
    u32   size;
    u32   count;
    char **cache;           /* one UTF-8 string per entry, made on first use */
    int   tried;
} PcBank;

static PcBank g_bank[TXT_COUNT];
static BwArcFile g_msg_arc;
static int g_msg_state;     /* 0 not tried, 1 open, -1 failed */

static int utf8_put(char *d, u32 cp)
{
    if (cp < 0x80) {
        d[0] = (char)cp;
        return 1;
    }
    if (cp < 0x800) {
        d[0] = (char)(0xC0 | (cp >> 6));
        d[1] = (char)(0x80 | (cp & 63));
        return 2;
    }
    d[0] = (char)(0xE0 | (cp >> 12));
    d[1] = (char)(0x80 | ((cp >> 6) & 63));
    d[2] = (char)(0x80 | (cp & 63));
    return 3;
}

static void bank_load(int b)
{
    PcBank *k = &g_bank[b];
    long n;

    k->tried = 1;
    if (g_msg_state == 0) {
        g_msg_state = bw_arc_open(&g_msg_arc, "a/0/0/2") == 0 ? 1 : -1;
    }
    if (g_msg_state != 1 || k_bank_member[b] >= g_msg_arc.count) {
        return;
    }
    n = bw_arc_member_size(&g_msg_arc, k_bank_member[b]);
    if (n < 20) {
        return;
    }
    k->data = (u8 *)malloc((size_t)n);
    if (k->data == NULL) {
        return;
    }
    if (bw_arc_read_member(&g_msg_arc, k_bank_member[b], k->data, (u32)n) != n) {
        free(k->data);
        k->data = NULL;
        return;
    }
    k->size = (u32)n;
    k->count = rd16(k->data + 2);
    k->cache = (char **)calloc(k->count != 0 ? k->count : 1, sizeof(char *));
}

static u16 rotl3(u16 k)
{
    return (u16)((k << 3) | (k >> 13));
}

/* Entry idx of the bank as UTF-8; "" when it is missing.  Control codes
 * (F000, kind, count, arguments) are left out, a line break is a space. */
static const char *pc_text(int b, u32 idx)
{
    PcBank *k = &g_bank[b];
    const u8 *e;
    u32 sect, off, len, i, o = 0;
    u16 key, nk;
    char *s;

    if (!k->tried) {
        bank_load(b);
    }
    if (k->data == NULL || k->cache == NULL || idx >= k->count) {
        return "";
    }
    if (k->cache[idx] != NULL) {
        return k->cache[idx];
    }
    sect = rd32(k->data + 12);
    e = k->data + sect + 4 + 8 * idx;
    off = sect + rd32(e);
    len = rd16(e + 4);
    if ((u64)off + 2ull * len > k->size) {
        return "";
    }
    s = (char *)malloc(len * 3 + 1);
    if (s == NULL) {
        return "";
    }
    key = (u16)((idx + 3u) * 0x2983u);
    for (i = 0; i < len; i++) {
        u16 u = (u16)(rd16(k->data + off + 2 * i) ^ key);

        key = rotl3(key);
        if (u == 0xFFFF) {
            break;
        }
        if (u == 0xF000 && i + 2 < len) {
            /* a control code: the kind, the argument count, the arguments */
            u16 kind_key = key, cnt_key = rotl3(key), n;
            u32 skip, j;

            (void)kind_key;
            n = (u16)(rd16(k->data + off + 2 * (i + 2)) ^ cnt_key);
            skip = 2 + n;
            for (j = 0; j < skip && i + 1 < len; j++) {
                key = rotl3(key);
                i++;
            }
            continue;
        }
        if (u == 0xFFFE) {
            s[o++] = ' ';
            continue;
        }
        o += (u32)utf8_put(s + o, u);
    }
    (void)nk;
    s[o] = '\0';
    k->cache[idx] = s;
    return s;
}

/* ------------------------------------------------------------------ */
/* The Pokemon                                                          */
/* ------------------------------------------------------------------ */

typedef struct PcMon {
    int  exists;
    int  egg;
    int  species, form, gender, level;
    int  hp, maxhp;         /* party only                                 */
    int  item, ability;
    int  moves[4];
    int  type1, type2;
    char nick[48];
} PcMon;

static void nick_utf8(const u16 *u, char *out, int cap)
{
    int i, o = 0;

    for (i = 0; i < 11 && u[i] != 0xFFFF && u[i] != 0 && o + 4 < cap; i++) {
        o += utf8_put(out + o, u[i]);
    }
    out[o] = '\0';
}

/* The Pokemon in a slot (0..29 of the shown box, 30..35 the party); full
 * non-zero reads everything the details need. */
static int pc_mon(const PcGame *g, int slot, int full, PcMon *m)
{
    void *bm = NULL, *pp = NULL;
    u16 name[16];
    int i;

    memset(m, 0, sizeof(*m));
    if (POKE_TOOL_GetBoxValue == NULL) {
        return 0;
    }
    if (slot >= SLOT_PARTY0) {
        u32 idx = (u32)(slot - SLOT_PARTY0);

        if (POKEPARTY_GetCount == NULL || POKEPARTY_GetMember == NULL ||
            POKE_TOOL_CastToBoxParam == NULL || idx >= POKEPARTY_GetCount(g->party)) {
            return 0;
        }
        pp = POKEPARTY_GetMember(g->party, idx);
        bm = pp != NULL ? POKE_TOOL_CastToBoxParam(pp) : NULL;
    } else if (slot >= 0 && BOX_SAVEDATA_GetPokemon != NULL) {
        bm = BOX_SAVEDATA_GetPokemon(g->gd, (u32)g->cur_box, (u32)slot);
    }
    if (bm == NULL || POKE_TOOL_GetBoxValue(bm, 0xA9, 0) == 0) {      /* species exists */
        return 0;
    }
    m->exists = 1;
    m->species = (int)POKE_TOOL_GetBoxValue(bm, 0x05, 0);
    m->egg = POKE_TOOL_GetBoxValue(bm, 0x4C, 0) != 0;
    m->form = (int)POKE_TOOL_GetBoxValue(bm, 0x6F, 0);
    m->gender = (int)POKE_TOOL_GetBoxValue(bm, 0x6E, 0);
    m->level = (int)POKE_TOOL_GetBoxValue(bm, 0x9E, 0);
    memset(name, 0xFF, sizeof(name));
    (void)POKE_TOOL_GetBoxValue(bm, 0x74, (PcPtr)name);
    nick_utf8(name, m->nick, (int)sizeof(m->nick));
    if (pp != NULL && POKE_TOOL_GetPartyValue != NULL) {
        m->hp = (int)POKE_TOOL_GetPartyValue(pp, 0xA0, 0);
        m->maxhp = (int)POKE_TOOL_GetPartyValue(pp, 0xA1, 0);
    }
    if (full) {
        m->item = (int)POKE_TOOL_GetBoxValue(bm, 0x06, 0);
        m->ability = (int)POKE_TOOL_GetBoxValue(bm, 0x0A, 0);
        for (i = 0; i < 4; i++) {
            m->moves[i] = (int)POKE_TOOL_GetBoxValue(bm, 0x36u + (u32)i, 0);
        }
        m->type1 = (int)POKE_TOOL_GetBoxValue(bm, 0xAE, 0);
        m->type2 = (int)POKE_TOOL_GetBoxValue(bm, 0xAF, 0);
    }
    return 1;
}

/* The name of box `box`: the saved name (UTF-16 in the box header). */
static void pc_box_name(const PcGame *g, int box, char *out, int cap)
{
    u8 *h = BOX_SAVEDATA_GetHeader != NULL ? (u8 *)BOX_SAVEDATA_GetHeader(g->gd) : NULL;
    int i, o = 0;

    out[0] = '\0';
    if (h == NULL || box < 0 || box >= 24) {
        return;
    }
    for (i = 0; i < 20 && o + 4 < cap; i++) {
        u16 u = rd16(h + 4 + box * 40 + 2 * i);

        if (u == 0xFFFF || u == 0) {
            break;
        }
        o += utf8_put(out + o, u);
    }
    out[o] = '\0';
}

/* ------------------------------------------------------------------ */
/* The game's bottom screen as it is drawn (for what has no struct)     */
/* ------------------------------------------------------------------ */

static const u8 *g_bottom;      /* the 1x bottom screen, one frame old    */

static int px(int x, int y, int *r, int *gg, int *b)
{
    const u8 *p;

    if (g_bottom == NULL || x < 0 || y < 0 || x >= 256 || y >= 192) {
        return 0;
    }
    p = g_bottom + ((size_t)y * 256 + (size_t)x) * 4;
    *r = p[0];
    *gg = p[1];
    *b = p[2];
    return 1;
}

/* The pixel is this colour, give or take tol per channel. */
static int px_near(int x, int y, int cr, int cg, int cb, int tol)
{
    int r, g, b;

    return px(x, y, &r, &g, &b) && r >= cr - tol && r <= cr + tol && g >= cg - tol &&
           g <= cg + tol && b >= cb - tol && b <= cb + tol;
}

static int px_light(int x, int y)
{
    int r, g, b;

    return px(x, y, &r, &g, &b) && r > 235 && g > 235 && b > 235;
}

/* ------------------------------------------------------------------ */
/* Input: taps and a queue of delayed ones                              */
/* ------------------------------------------------------------------ */

typedef struct PcAct {
    u32 at;                 /* the frame it fires on                      */
    int kind;               /* 1 tap, 2 key                               */
    int x, y;
    u32 hold, keys;
} PcAct;

#define PC_QMAX 16
static PcAct g_q[PC_QMAX];
static int g_qn;
static u32 g_now;

static void pc_queue(u32 delay, int kind, int x, int y, u32 hold, u32 keys)
{
    if (g_qn < PC_QMAX) {
        PcAct *a = &g_q[g_qn++];

        a->at = g_now + delay;
        a->kind = kind;
        a->x = x;
        a->y = y;
        a->hold = hold;
        a->keys = keys;
    }
}

static void pc_queue_run(void)
{
    int i = 0;

    while (i < g_qn) {
        if (g_q[i].at <= g_now) {
            if (g_q[i].kind == 1) {
                eidos_ui_tap(g_q[i].x, g_q[i].y, g_q[i].hold);
            } else {
                eidos_ui_press(g_q[i].keys, g_q[i].hold);
            }
            memmove(&g_q[i], &g_q[i + 1], (size_t)(g_qn - i - 1) * sizeof(PcAct));
            g_qn--;
        } else {
            i++;
        }
    }
}

static void pc_tap(int x, int y)
{
    pc_queue(0, 1, x, y, 6, 0);
}

/* The bottom-screen positions the game tests. */
static void grid_pos(int slot, int *x, int *y)
{
    *x = 23 + 24 * (slot % 6);
    *y = 51 + 24 * (slot / 6);
}


/* ------------------------------------------------------------------ */
/* The state of the game's windows                                      */
/* ------------------------------------------------------------------ */

#define PARTY_BTN_X   47        /* the PARTY PKMN button, bottom left      */
#define PARTY_BTN_Y   180
#define YESNO_X       222       /* the game's two-choice menu              */
#define YES_Y         109
#define NO_Y          133

typedef struct PcWin {
    int list_shown;         /* the action list is on screen               */
    int list_n;             /* its buttons                                */
    int list_ids[6];        /* their message ids (bank 9)                 */
    int msg;                /* the white message bar is on screen         */
    int msg_w;              /* its right edge, bottom-screen pixels       */
    int yesno;              /* YES / NO is on screen                      */
    int tray;               /* the party tray is open                     */
    int ctray;              /* the narrow party tray (a Pokemon in hand)  */
} PcWin;

static PcWin g_w;

/* The action lists of the modes (ov170 data: {message id, flag} pairs at
 * the addresses BOX2_SEQ_StateItemMenu draws them from). */
static const u32 k_list_addr[6] = { 0x021EF950u, 0x021EF9B4u, 0x021EFA10u, 0, 0x021EF918u, 0x021EF8ECu };
static const int k_list_len[6] = { 5, 5, 6, 0, 4, 2 };

#ifdef BW_PC_TEST
extern const u8 *g_pc_test_lists[6];
static const u8 *pc_ov_data(int mode) { return g_pc_test_lists[mode]; }
#else
static const u8 *pc_ov_data(int mode) { return gptr(k_list_addr[mode]); }
#endif

/* A button of the two-choice menu: navy, or the lighter blue it has when it
 * is the one the cursor is on. */
static int yn_px(int x, int y)
{
    return px_near(x, y, 57, 57, 82, 14) || px_near(x, y, 8, 90, 139, 20);
}

static void pc_windows(const PcGame *g, PcWin *w)
{
    s8 x = 0, y = 0;
    int i;

    memset(w, 0, sizeof(*w));
    if (BGWINFRM_GetPos != NULL && g->winmgr != NULL) {
        BGWINFRM_GetPos(g->winmgr, 0, &x, &y);
        w->list_shown = x == 0x15;
        BGWINFRM_GetPos(g->winmgr, 8, &x, &y);
        w->tray = x == 2 && y == 6;
        w->ctray = x == 21 && y == 6;
    }
    if (w->list_shown) {
        /* the buttons stack from the bottom: count the dark ones */
        for (i = 0; i < 6; i++) {
            if (px_near(248, 148 - 24 * i, 65, 65, 65, 12)) {
                w->list_n++;
            } else {
                break;
            }
        }
        if (g->mode >= 0 && g->mode < 6 && k_list_len[g->mode] == w->list_n &&
            k_list_addr[g->mode] != 0) {
            const u8 *t = pc_ov_data(g->mode);

            for (i = 0; t != NULL && i < w->list_n; i++) {
                w->list_ids[i] = rd16(t + 4 * i);
            }
        } else if (w->list_n == 4) {
            /* the box menu the box name opens: SWITCH BOX, WALLPAPER, NAME, CANCEL */
            for (i = 0; i < 4; i++) {
                w->list_ids[i] = 31 + i;
            }
        }
    }
    /* the white message window: its width is how far the white goes */
    w->msg = px_light(6, 168) && px_light(40, 168);
    w->msg_w = 0;
    if (w->msg) {
        int x = 6;

        while (x < 254 && px_light(x, 168)) {
            x++;
        }
        w->msg_w = x + 3;           /* the window's frame is two pixels more */
        if (w->msg_w > 254) {
            w->msg_w = 254;
        }
    }
    w->yesno = !w->list_shown && w->msg && yn_px(YESNO_X, YES_Y) && yn_px(YESNO_X, NO_Y);
}

/* What this recipe has no picture for: the item and battle-box modes (3 to
 * 5) and, in modes 0 to 2, the marking panel (BoxTask.state 85), the box
 * list the box menu's SWITCH BOX opens (96) and the wallpaper menu (100);
 * they show the DS screens until the game leaves them.  Every other state
 * (the idle ones, the list, the question, the placing of a Pokemon) is
 * drawn from the data above, whatever its number. */
static int pc_unsupported(const PcGame *g)
{
    return g->mode < 0 || g->mode > 2 || g->state == 85 || g->state == 96 || g->state == 100;
}

/* ------------------------------------------------------------------ */
/* Layout                                                               */
/* ------------------------------------------------------------------ */

typedef struct PcLay {
    int cw, ch;
    int top, bottom;        /* the columns' vertical range                */
    EidosUiRect party, box, det, bar;
    int row_y[6], row_h;
    int pitch;              /* grid cell                                  */
    int narrow;             /* two columns: the details are under the grid */
    int gx, gy;             /* grid origin (top left of cell 0)           */
    int hint_y;
} PcLay;

static int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

static void pc_layout(PcLay *l, int cw, int ch)
{
    const int m = 6, gap = 5, bar_h = 28;
    int avail, boxw, pw, dw, pitch_w, pitch_h, i, colh, gapy;

    l->cw = cw;
    l->ch = ch;
    l->hint_y = ch - 17;
    l->top = 5 + 22 + 4;
    l->bar.h = bar_h;
    l->bar.y = l->hint_y - 3 - bar_h;
    l->bar.x = m;
    l->bar.w = cw - 2 * m;
    /* the message bar has its own strip when the party's six rows still fit
     * above it; a shorter canvas lets it cover the bottom of the columns */
    l->bottom = (l->hint_y - 3 - l->top >= 6 * 30 + bar_h + 6) ? l->bar.y - 4 : l->hint_y - 3;
    colh = l->bottom - l->top;
    avail = cw - 2 * m - 2 * gap;
    /* three columns when the canvas is wide enough; else the details sit
     * under the grid, inside the box's panel */
    l->narrow = avail < 128 + 110 + 6 * 22 + 12;
    l->party.x = m;
    l->party.y = l->top;
    l->party.h = colh;
    l->box.y = l->top;
    l->box.h = colh;
    if (!l->narrow) {
        pitch_w = (avail - 128 - 110 - 12) / 6;
        pitch_h = (colh - 24 - 6) / 5;
        l->pitch = clampi(pitch_w < pitch_h ? pitch_w : pitch_h, 22, 36);
        boxw = 6 * l->pitch + 12;
        pw = clampi((avail - boxw) * 46 / 100, 128, 160);
        dw = avail - boxw - pw;
        if (dw < 110) {
            dw = 110;
        }
        l->party.w = pw;
        l->box.x = l->party.x + pw + gap;
        l->box.w = boxw;
        l->det.x = l->box.x + boxw + gap;
        l->det.y = l->top;
        l->det.w = cw - m - l->det.x;
        l->det.h = colh;
        l->gx = l->box.x + 6;
    } else {
        pw = 128;
        l->party.w = pw;
        l->box.x = l->party.x + pw + gap;
        l->box.w = cw - m - l->box.x;
        pitch_w = (l->box.w - 12) / 6;
        pitch_h = (colh - 24 - 6 - 50) / 5;
        l->pitch = clampi(pitch_w < pitch_h ? pitch_w : pitch_h, 20, 32);
        l->gx = l->box.x + (l->box.w - 6 * l->pitch) / 2;
        l->det.x = l->box.x + 4;
        l->det.y = l->box.y + 24 + 5 * l->pitch + 4;
        l->det.w = l->box.w - 8;
        l->det.h = l->box.y + l->box.h - 4 - l->det.y;
    }
    l->gy = l->box.y + 24;
    l->row_h = 30;
    gapy = (colh - 6 * l->row_h) / 5;
    gapy = clampi(gapy, 0, 4);
    for (i = 0; i < 6; i++) {
        l->row_y[i] = l->party.y + i * (l->row_h + gapy);
    }
}

/* The slot under a canvas position, or -1. */
static int pc_slot_at(const PcLay *l, int mx, int my)
{
    int i;

    if (mx < 0 || my < 0) {
        return -1;
    }
    for (i = 0; i < 6; i++) {
        if (mx >= l->party.x && mx < l->party.x + l->party.w && my >= l->row_y[i] &&
            my < l->row_y[i] + l->row_h) {
            return SLOT_PARTY0 + i;
        }
    }
    if (mx >= l->gx && my >= l->gy && mx < l->gx + 6 * l->pitch && my < l->gy + 5 * l->pitch) {
        return ((my - l->gy) / l->pitch) * 6 + (mx - l->gx) / l->pitch;
    }
    return -1;
}

/* ------------------------------------------------------------------ */
/* Drawing                                                              */
/* ------------------------------------------------------------------ */

static void fmt_int(char *d, const char *pre, int v)
{
    char t[16];
    int n = 0, i, o = 0;

    while (*pre != '\0') {
        d[o++] = *pre++;
    }
    if (v < 0) {
        v = 0;
    }
    do {
        t[n++] = (char)('0' + v % 10);
        v /= 10;
    } while (v > 0 && n < 12);
    for (i = n - 1; i >= 0; i--) {
        d[o++] = t[i];
    }
    d[o] = '\0';
}

static const char *mode_title(int mode)
{
    static const int idx[6] = { 37, 38, 39, 39, 41, 40 };

    if (mode < 0 || mode > 5) {
        return "";
    }
    return pc_text(TXT_PROMPT, (u32)idx[mode]);
}

/* The game's type numbers (Black and White have no "???": Fire is 9, Dark 16;
 * message bank 199 has 17 names) as the kit's, whose list keeps the "???" at 9. */
static int kit_type(int t)
{
    return t;      /* the kit takes the game's own type numbers */
}

static const UiImage *icon_of(const PcMon *m, int frame)
{
    if (!m->exists || m->egg || m->species <= 0) {
        return NULL;
    }
    return ui_mon_icon((u16)m->species, (u8)m->form, frame);
}

static void draw_cell(EidosUiCtx *c, int x, int y, int p, int hover, int selected)
{
    u32 edge = selected ? UI_SKY : 0x0B3A62u;

    ui_fill(c, x, y, p - 2, p - 2, hover ? 0x0E3A5Cu : 0x08243Eu, 235);
    ui_fill(c, x, y, p - 2, 1, edge, 255);
    ui_fill(c, x, y + p - 3, p - 2, 1, edge, 255);
    ui_fill(c, x, y, 1, p - 2, edge, 255);
    ui_fill(c, x + p - 3, y, 1, p - 2, edge, 255);
}

static void draw_chevron(EidosUiCtx *c, int x, int y, int dir)
{
    int i;

    for (i = 0; i < 5; i++) {
        int h = 9 - 2 * i;

        ui_fill(c, dir < 0 ? x + 5 - i : x + i, y + i, 1, h, UI_WHITE, 255);
    }
}

static void draw_move_row(EidosUiCtx *c, int x, int y, int w, int h, int move)
{
    ui_panel(c, x, y, w, h, UI_CUT_TL | UI_CUT_BR, 3, UI_BODY2, UI_EDGE, 255);
    if (move > 0) {
        ui_text(c, x + 6, y + ui_text_y(h, UI_FONT_LARGE), pc_text(TXT_MOVE, (u32)move),
                UI_WHITE, UI_EDGE, UI_FONT_LARGE, 1);
    } else {
        ui_text(c, x + 6, y + ui_text_y(h, UI_FONT_LARGE), "-----", UI_EDGE2, -1,
                UI_FONT_LARGE, 1);
    }
}

static void draw_details_full(EidosUiCtx *c, const PcLay *l, const PcMon *m, int frame);

/* The details in a strip of 3 lines (a narrow canvas): name and level, the
 * types with the ability and the item, the four moves in two columns. */
static void draw_details_compact(EidosUiCtx *c, const PcLay *l, const PcMon *m)
{
    EidosUiRect r = l->det;
    int x = r.x + 5, y = r.y + 3, w = r.w - 10, i;
    EidosUiRect saved;

    ui_panel(c, r.x, r.y, r.w, r.h, UI_CUT_TL | UI_CUT_BR, 4, UI_BODY2, UI_EDGE, 235);
    if (!m->exists) {
        ui_text(c, x, y, "Empty", UI_GREY, UI_EDGE, UI_FONT_LARGE, 1);
        return;
    }
    ui_push(c, 0, 0, &saved);
    ui_clip(c, r.x + 1, r.y + 1, r.w - 2, r.h - 2);
    {
        char lv[16];
        int nx = ui_text(c, x, y, m->egg ? "Egg" : m->nick, UI_WHITE, UI_EDGE, UI_FONT_LARGE, 1);

        if (!m->egg) {
            ui_gender(c, nx + 3, y, m->gender, UI_FONT_LARGE);
            fmt_int(lv, "Lv.", m->level);
            ui_text(c, r.x + r.w - 5 - ui_text_w(lv, UI_FONT_SMALL), y + 2, lv, UI_LIGHT, UI_EDGE,
                    UI_FONT_SMALL, 1);
        }
    }
    if (!m->egg) {
        int tx = x, ty = y + 17;
        char line[96];

        ui_type_tag(c, tx, ty, 36, kit_type(m->type1));
        tx += 39;
        if (m->type2 != m->type1) {
            ui_type_tag(c, tx, ty, 36, kit_type(m->type2));
            tx += 39;
        }
        snprintf(line, sizeof(line), "%s  %s", pc_text(TXT_ABILITY, (u32)m->ability),
                 m->item > 0 ? pc_text(TXT_ITEM, (u32)m->item) : "No item");
        ui_text(c, tx, ty + ui_text_y(13, UI_FONT_SMALL), line, UI_LIGHT, UI_EDGE, UI_FONT_SMALL, 1);
        for (i = 0; i < 4; i++) {
            int mx = x + (i % 2) * (w / 2), my = ty + 17 + (i / 2) * 12;

            ui_text(c, mx, my, m->moves[i] > 0 ? pc_text(TXT_MOVE, (u32)m->moves[i]) : "-----",
                    m->moves[i] > 0 ? UI_WHITE : UI_EDGE2, UI_EDGE, UI_FONT_SMALL, 1);
        }
    }
    ui_pop(c, 0, 0, &saved);
}

static void draw_details(EidosUiCtx *c, const PcLay *l, const PcMon *m, int frame)
{
    if (l->narrow) {
        draw_details_compact(c, l, m);
        return;
    }
    draw_details_full(c, l, m, frame);
}

static void draw_details_full(EidosUiCtx *c, const PcLay *l, const PcMon *m, int frame)
{
    EidosUiRect r = l->det;
    int x = r.x + 6, w = r.w - 12, y = r.y + 5, i, big, rowh, well;
    const UiImage *ic;

    ui_panel(c, r.x, r.y, r.w, r.h, UI_CUT_TL | UI_CUT_BR, 6, UI_BODY, UI_EDGE, 225);
    if (!m->exists) {
        ui_text(c, x, y + 2, "Empty", UI_GREY, UI_EDGE, UI_FONT_LARGE, 1);
        return;
    }
    {
        char lv[16];
        int nx;

        nx = ui_text(c, x, y, m->egg ? "Egg" : m->nick, UI_WHITE, UI_EDGE, UI_FONT_LARGE, 1);
        if (!m->egg) {
            ui_gender(c, nx + 3, y, m->gender, UI_FONT_LARGE);
            fmt_int(lv, "Lv.", m->level);
            ui_text(c, r.x + r.w - 6 - ui_text_w(lv, UI_FONT_SMALL), y + 2, lv, UI_LIGHT,
                    UI_EDGE, UI_FONT_SMALL, 1);
        }
    }
    y += 18;
    big = r.h >= 196;           /* room for the icon at twice the size */
    well = big ? 70 : 38;
    rowh = r.h >= 196 ? 17 : 15;
    ic = icon_of(m, (frame / 24) & 1);
    ui_panel(c, x, y, well, well, UI_CUT_TL | UI_CUT_BR, 4, 0x0B1E34u, UI_EDGE, 255);
    if (ic != NULL) {
        int k = big ? 2 : 1;

        ui_image(c, x + (well - ic->w * k) / 2, y + (well - ic->h * k) / 2, ic, k);
    }
    if (!m->egg && m->item > 0) {
        const UiImage *it = ui_item_icon((u16)m->item);

        if (it != NULL) {
            ui_image(c, x + well - it->w + 2, y + well - it->h + 2, it, 1);
        }
    }
    if (!m->egg) {
        int tx = x + well + 5, tw = w - well - 5, th = 13;

        tw = tw > 46 ? 46 : tw;
        if (tw >= 30) {
            ui_type_tag(c, tx, y + 4, tw, kit_type(m->type1));
            if (m->type2 != m->type1) {
                ui_type_tag(c, tx, y + 4 + th + 3, tw, kit_type(m->type2));
            }
        }
    }
    y += well + 5;
    if (m->egg) {
        return;
    }
    ui_info_row(c, x, y, w, "Ability", pc_text(TXT_ABILITY, (u32)m->ability));
    y += 17;
    ui_info_row(c, x, y, w, "Item", m->item > 0 ? pc_text(TXT_ITEM, (u32)m->item) : "None");
    y += 21;
    for (i = 0; i < 4; i++) {
        draw_move_row(c, x, y, w, rowh, m->moves[i]);
        y += rowh + 2;
    }
}

/* ------------------------------------------------------------------ */
/* The mouse (the stylus emulation) and the clicks                      */
/* ------------------------------------------------------------------ */

typedef struct PcMouse {
    int x, y;               /* canvas pixels, -1 outside                  */
    int down, pressed, released;
} PcMouse;

/*
 * EIDOS_PC_DRAG="frame:x1:y1:x2:y2:frames[,...]" (window pixels) scripts drags
 * for checks: the button goes down at x1,y1 on `frame`, moves in a line to
 * x2,y2 and goes up `frames` later.  The real mouse is used otherwise.
 */
static void pc_get_mouse(const EidosUiFrame *in, int k, PcMouse *m)
{
    static int loaded, have;
    static unsigned long d[8][6];
    int i;

    memset(m, 0, sizeof(*m));
    if (!loaded) {
        const char *s = getenv("EIDOS_PC_DRAG");

        loaded = 1;
        while (s != NULL && have < 8 &&
               sscanf(s, "%lu:%lu:%lu:%lu:%lu:%lu", &d[have][0], &d[have][1], &d[have][2],
                      &d[have][3], &d[have][4], &d[have][5]) == 6) {
            have++;
            s = strchr(s, ',');
            s = s != NULL ? s + 1 : NULL;
        }
    }
    for (i = 0; i < have; i++) {
        unsigned long sf = d[i][0], sd = d[i][5];

        if (in->frame >= sf && in->frame <= sf + sd) {
            unsigned long t = in->frame - sf;
            long x = (long)d[i][1] + ((long)d[i][3] - (long)d[i][1]) * (long)t / (long)(sd ? sd : 1);
            long y = (long)d[i][2] + ((long)d[i][4] - (long)d[i][2]) * (long)t / (long)(sd ? sd : 1);

            m->x = (int)(x / k);
            m->y = (int)(y / k);
            m->down = in->frame < sf + sd;
            m->pressed = in->frame == sf;
            m->released = in->frame == sf + sd;
            return;
        }
    }
    if (in->mouse.x < 0 || in->mouse.y < 0) {
        m->x = m->y = -1;
    } else {
        m->x = in->mouse.x / k;
        m->y = in->mouse.y / k;
    }
    m->down = in->mouse.down;
    m->pressed = in->mouse.pressed;
    m->released = in->mouse.released;
}

static int g_drag;              /* the stylus is held for a mouse drag    */
static int g_drag_src = -1;     /* the slot the drag began on             */
static int g_hand_drag;         /* a move-mode drag (pc_hand_drag)        */
static int g_hand_dest = -1;    /* where a release put it, still to tap   */
static u32 g_hand_until;        /* the pick-up's deadline                 */
static int g_hand_moved;        /* its MOVE button was tapped             */
static u32 g_hand_quiet;        /* the last frame a tap was still waiting */
static int g_hand_tries;        /* pick-up taps sent                      */
static u32 g_suppress;          /* clicks are ignored until this frame    */
static u32 g_wheel_at;          /* the frame of the last box change       */

/* Where the stylus goes for a slot; 0 when the game has no control for it
 * on screen now (a party slot while the tray is closed). */
static int slot_stylus(int tray, int slot, int *x, int *y)
{
    static const int tx[6] = { 41, 77, 41, 77, 41, 77 };
    static const int ty[6] = { 67, 75, 99, 107, 131, 139 };

    if (slot >= 0 && slot < SLOT_PARTY0) {
        if (tray && slot % 6 <= 3) {
            return 0;               /* under the open tray                */
        }
        grid_pos(slot, x, y);
        return 1;
    }
    if (slot >= SLOT_PARTY0 && slot < SLOT_COUNT && tray) {
        *x = tx[slot - SLOT_PARTY0];
        *y = ty[slot - SLOT_PARTY0];
        return 1;
    }
    return 0;
}

/* The keys' cursor (CursorMove.cur) counts in the table the screen uses:
 * with the party tray closed, 0..29 the box slots (30 the box name above
 * them, 33 the PARTY PKMN button below); with it open, 0..5 the party
 * rows (left column 0, 2, 4) and 6 its button.  -1: not a slot. */
static int key_slot(const PcGame *g, const PcWin *w)
{
    int k = g->cm != NULL ? g->cm[CM_CUR] : -1;

    if (w->tray) {
        return k >= 0 && k <= 5 ? SLOT_PARTY0 + k : -1;
    }
    if (k == 33) {
        return SLOT_PARTY0;     /* PARTY PKMN: A opens the tray on row 1   */
    }
    return k >= 0 && k < SLOT_PARTY0 ? k : -1;
}

/* The keys' cursor to a slot (the mouse is on it), so a key goes on from
 * there; a slot the current table has no entry for is left alone. */
static void key_slot_set(const PcGame *g, const PcWin *w, int slot)
{
    if (g->cm == NULL || slot < 0) {
        return;
    }
    if (w->tray && slot >= SLOT_PARTY0 && slot < SLOT_PARTY0 + 6) {
        g->cm[CM_CUR] = (u8)(slot - SLOT_PARTY0);
    } else if (!w->tray && slot < SLOT_PARTY0) {
        g->cm[CM_CUR] = (u8)slot;
    } else if (!w->tray) {
        g->cm[CM_CUR] = 33;     /* a party row: PARTY PKMN opens the tray */
    }
}

/* A click on a slot: a tap on its control.  A slot whose control needs the
 * party tray in the other state (a party row while it is closed, in the
 * move mode a box slot under it while it is open) waits: the PARTY PKMN
 * button toggles the tray, and the tap goes once the tray has settled in
 * the state the slot needs (pc_pending_run), however long the game takes. */
static int g_pend_slot = -1;    /* the slot still to tap                  */
static int g_pend_need;         /* the tray state it needs                */
static int g_pend_toggled;      /* the PARTY PKMN button was pressed      */
static u32 g_pend_at;           /* when it was (a retry after a while)    */
static u32 g_pend_until;        /* when to give up                        */
static int g_tray_last = -1, g_tray_for;

/* While the move mode's hand holds a Pokemon, the bottom screen changes:
 * the whole box shows and the party sits in a narrow tray on the right
 * (the same rows as the wide tray, two columns at x 192 and 232), and the
 * bottom-left button is BOX LIST.  Every slot is then one tap. */
static int slot_stylus_held(int slot, int *x, int *y)
{
    static const int ty[6] = { 67, 75, 99, 107, 131, 139 };

    if (slot >= 0 && slot < SLOT_PARTY0) {
        grid_pos(slot, x, y);
        return 1;
    }
    if (slot >= SLOT_PARTY0 && slot < SLOT_COUNT) {
        *x = (slot - SLOT_PARTY0) % 2 == 0 ? 192 : 232;
        *y = ty[slot - SLOT_PARTY0];
        return 1;
    }
    return 0;
}

static void pc_click_slot(const PcWin *w, int mode, int slot, u32 frame)
{
    int x, y, need = -1;

    if (frame < g_suppress) {
        return;
    }
    if (mode == 2 && g_g.hand_mode == 1) {
        /* a party row needs the narrow tray, which a Pokemon taken from a
         * box does not show at first (the box list stands there) */
        if (slot < SLOT_PARTY0 || w->ctray) {
            if (slot_stylus_held(slot, &x, &y)) {
                pc_queue(0, 1, x, y, 6, 0);
            }
            return;
        }
        g_pend_slot = slot;
        g_pend_need = 2;
        g_pend_toggled = 0;
        g_pend_until = frame + 300;
        return;
    }
    if (slot >= SLOT_PARTY0) {
        need = 1;
    } else if (slot % 6 <= 3) {
        need = 0;
    }
    if (need == 0 && w->tray && mode != 2) {
        return;                 /* the deposit mode keeps the tray open   */
    }
    if (need < 0 || (need == (w->tray != 0) && g_qn == 0 && g_pend_slot < 0)) {
        if (slot_stylus(w->tray, slot, &x, &y)) {
            pc_queue(0, 1, x, y, 6, 0);
        }
        return;
    }
    g_pend_slot = slot;
    g_pend_need = need;
    g_pend_toggled = 0;
    g_pend_until = frame + 300;
}

static void pc_pending_run(const PcWin *w, u32 frame)
{
    int x, y;
    /* the state the pending slot waits for: the wide tray open (1) or
     * closed (0), or the narrow tray shown (2, a Pokemon in hand) */
    int now = g_pend_need == 2 ? 2 * (w->ctray != 0) : (w->tray != 0);

    if (now == g_tray_last) {
        g_tray_for++;
    } else {
        g_tray_last = now;
        g_tray_for = 0;
    }
    if (g_pend_slot < 0 || g_qn > 0) {
        return;
    }
    if (frame > g_pend_until) {
        g_pend_slot = -1;
        return;
    }
    if (now == g_pend_need && g_tray_for >= 10) {
        if (g_pend_need == 2 ? slot_stylus_held(g_pend_slot, &x, &y)
                             : slot_stylus(w->tray, g_pend_slot, &x, &y)) {
            pc_queue(0, 1, x, y, 6, 0);
        }
        if (getenv("EIDOS_PC_DEBUG") != NULL) {
            fprintf(stderr, "pc: pending slot %d tapped at %u (tray %d)\n", g_pend_slot, frame, w->tray);
        }
        g_pend_slot = -1;
    } else if (now != g_pend_need && g_tray_for >= 10 &&
               (!g_pend_toggled || frame > g_pend_at + 90)) {
        pc_queue(0, 1, PARTY_BTN_X, PARTY_BTN_Y, 6, 0);
        g_pend_toggled = 1;
        g_pend_at = frame;
        if (getenv("EIDOS_PC_DEBUG") != NULL) {
            fprintf(stderr, "pc: pending slot %d: tray toggle at %u (tray %d)\n", g_pend_slot, frame, w->tray);
        }
    }
}

/* ------------------------------------------------------------------ */
/* The recipe                                                           */
/* ------------------------------------------------------------------ */

static void pc_reset(void)
{
    g_qn = 0;
    g_drag = 0;
    g_drag_src = -1;
    g_hand_drag = 0;
    g_hand_dest = -1;
    g_pend_slot = -1;
    g_tray_last = -1;
    g_suppress = 0;
    g_wheel_at = 0;
}

static int pc_match(void)
{
    if (!pc_read_game(&g_g)) {
        return 0;
    }
    pc_debug(&g_g, bw_input_frame_number());
    return !pc_unsupported(&g_g);
}

/* The stylus follows a mouse drag: down on a slot with a control, held at
 * the control of the slot under the mouse, up when the button is. */
/* The move mode's drag goes through the game's hand, the way its own
 * taps work: the press taps the Pokemon (a box Pokemon goes into the hand;
 * a party Pokemon opens the action list, whose MOVE button is then
 * tapped), the release taps the slot under the mouse, which puts it down.
 * pc_click_slot opens or closes the party tray first when the slot needs
 * it, so a Pokemon goes from the party to any box slot and back, which a
 * held stylus cannot do (the tray covers four columns of the box, and the
 * PARTY PKMN button cannot be pressed while the stylus holds a Pokemon).
 * g_hand_drag: 0 none, 1 picking up, 2 in the hand. */
static void pc_hand_drag(const PcLay *l, const PcWin *w, const PcGame *g, const PcMouse *m,
                         u32 frame)
{
    int s = pc_slot_at(l, m->x, m->y);

    if (m->pressed && s >= 0 && !w->list_shown && !w->yesno && g->hand_mode == 0 &&
        g_hand_drag == 0 && frame >= g_suppress) {
        PcMon pm;

        if (pc_mon(g, s, 0, &pm) && pm.exists) {
            pc_click_slot(w, 2, s, frame);  /* pick it up                 */
            g_hand_drag = 1;
            g_hand_until = frame + 400;
            g_hand_moved = 0;
            g_hand_quiet = frame;
            g_hand_tries = 1;
            g_drag_src = s;
            g_drag = 1;                     /* the mouse is down           */
            g_hand_dest = -1;
        }
    }
    if (g_hand_drag == 0) {
        return;
    }
    if (g_pend_slot >= 0 || g_qn > 0 || w->list_shown || g->hand_mode != 0) {
        g_hand_quiet = frame;
    } else if (g_hand_drag == 1 && !g_hand_moved && frame > g_hand_quiet + 60 &&
               g_hand_tries < 3) {
        /* the game let the tap go (it was still moving the tray): again */
        pc_click_slot(w, 2, g_drag_src, frame);
        g_hand_tries++;
        g_hand_quiet = frame;
    }
    if (g_hand_drag == 1 && g_pend_slot < 0 && g_qn == 0) {
        if (w->list_shown && !g_hand_moved) {
            /* the action list opens on its first button, MOVE: A takes it
             * at once, without waiting for the list to settle */
            pc_queue(0, 2, 0, 0, 4, BW_KEY_A);
            g_hand_moved = 1;
            if (getenv("EIDOS_PC_DEBUG") != NULL) {
                fprintf(stderr, "pc: hand drag MOVE at %u, list of %d\n", frame, w->list_n);
            }
        } else if (frame > g_hand_until) {
            g_hand_drag = 2;
        }
    }
    if (g_hand_drag == 1 && g->hand_mode == 1 && g_qn == 0) {
        /* in the hand; the game takes taps again once the hand has
         * settled */
        static int held_for;

        if (++held_for >= 10) {
            held_for = 0;
            g_hand_drag = 2;
        }
    }
    if (g_drag && (m->released || !m->down)) {
        g_drag = 0;
        g_hand_dest = s >= 0 && s != g_drag_src ? s : -1;
        g_suppress = frame + 4;             /* not also a click           */
        if (g_hand_dest < 0 && g_hand_drag == 2) {
            g_hand_drag = 0;                /* dropped where it began: held */
            g_drag_src = -1;
        }
    }
    if (!g_drag && g_hand_drag == 2 && g_qn == 0) {
        if (g_hand_dest >= 0) {
            pc_click_slot(w, 2, g_hand_dest, g_suppress); /* put it down  */
        }
        g_hand_drag = 0;
        g_hand_dest = -1;
        g_drag_src = -1;
    }
}

static void pc_drag(const PcLay *l, const PcWin *w, const PcMouse *m, u32 frame)
{
    int s = pc_slot_at(l, m->x, m->y), x = 128, y = 10;

    if (m->pressed && s >= 0 && !w->list_shown && !w->yesno && slot_stylus(w->tray, s, &x, &y)) {
        g_drag = 1;
        g_drag_src = s;
    }
    if (!g_drag) {
        return;
    }
    if (s < 0 || !slot_stylus(w->tray, s, &x, &y)) {
        x = 128;                    /* off any slot: a drop here is a cancel */
        y = 10;
    }
    eidos_ui_tap(x, y, 3);
    if (m->released || !m->down) {
        g_drag = 0;
        g_drag_src = -1;
        g_suppress = frame + 4;
    }
}

/* Where the action list and the two-choice menu are drawn (the box panel's
 * bottom right, above the message bar; the box panel's middle). */
static void list_geom(const PcLay *l, int n, int *lx, int *ly, int *lw, int *bh)
{
    *lw = 78;
    *bh = 18;
    *lx = l->box.x + l->box.w - *lw - 6;
    *ly = l->box.y + l->box.h - 8 - n * (*bh + 2);
    if (*ly + n * (*bh + 2) > l->bar.y - 6) {
        *ly = l->bar.y - 6 - n * (*bh + 2);
    }
}

static void yesno_geom(const PcLay *l, int *bx, int *by0, int *bw, int *bh)
{
    *bw = 60;
    *bh = 20;
    *bx = l->box.x + (l->box.w - *bw) / 2;
    *by0 = l->box.y + l->box.h / 2 - 24;
}

static void pc_run(EidosUiCtx *c, EidosUiView *view, const EidosUiFrame *in)
{
    PcLay l;
    const PcGame *g = &g_g;
    PcMon mon[SLOT_COUNT], det;
    PcMouse mouse;
    int cw = c->canvas->w, ch = c->canvas->h, k = c->canvas->k;
    int i, hover_slot = -1, sel_slot = -1, frame = (int)in->frame;
    int held_slot = -1, popup_hover = 0;
    PcWin *w = &g_w, shown;
    int busy;

    g_now = in->frame;
    g_bottom = in->screen_rgba[1];
    view->mode = EIDOS_VIEW_WORLD;
    view->world_screen = 0;
    view->zoom = 1.0f;
    view->blur = 1;
    view->hold_world = 1;     /* an app: the field is not running */
    view->hide_2d = 1;
    pc_windows(g, w);
    if (eidos_ui_keys_lit()) {
        sel_slot = g->cm != NULL ? key_slot(g, w) : (g->cursor < SLOT_COUNT ? g->cursor : -1);
    }
    pc_layout(&l, cw, ch);
    pc_get_mouse(in, k, &mouse);
    if (g->mode == 2 || g_hand_drag != 0) {
        pc_hand_drag(&l, w, g, &mouse, in->frame);
    } else {
        pc_drag(&l, w, &mouse, in->frame);
    }
    if (g_drag && g_drag_src >= 0 && g->cursor != g_drag_src) {
        held_slot = g_drag_src;                 /* carried by the stylus  */
    } else if (g->hand_mode == 1 && g->cursor < SLOT_COUNT) {
        held_slot = g->cursor;                  /* carried by the hand    */
    }
    if (eidos_ui_right_click()) {
        eidos_ui_press(BW_KEY_B, 6);
    }
    pc_pending_run(w, in->frame);
    pc_queue_run();
    /* While a move-mode drag is under way the recipe drives the game's own
     * steps (the tray, the action list's MOVE, the held layout): those
     * steps are not shown, so the screen stays still: the Pokemon follows
     * the mouse, then waits on the slot it was dropped on. */
    busy = g->mode == 2 && (g_hand_drag != 0 || g_hand_dest >= 0 ||
                            (g_pend_slot >= 0 && g_drag_src >= 0));
    if (busy) {
        shown = *w;
        shown.list_shown = 0;
        shown.list_n = 0;
        shown.msg = 0;
        shown.yesno = 0;
        w = &shown;
        if (g_drag_src >= 0) {
            held_slot = g_drag_src;
        }
    }

    /* a popup (the action list, YES / NO) takes the mouse from what is under it */
    {
        EidosUiRect pr;
        int lx, ly, lw, bh, bx, by0, bw;

        if (w->list_shown && w->list_n > 0) {
            list_geom(&l, w->list_n, &lx, &ly, &lw, &bh);
            pr.x = lx - 4; pr.y = ly - 4; pr.w = lw + 8; pr.h = w->list_n * (bh + 2) + 6;
            popup_hover |= (eidos_ui_hit(pr) & EIDOS_UI_HOVER) != 0;
        }
        if (w->yesno) {
            yesno_geom(&l, &bx, &by0, &bw, &bh);
            pr.x = bx - 6; pr.y = by0 - 6; pr.w = bw + 12; pr.h = 2 * (bh + 4) + 8;
            popup_hover |= (eidos_ui_hit(pr) & EIDOS_UI_HOVER) != 0;
        }
    }

    /* the header */
    ui_header(c, 6, 5, cw - 12, "POKEMON STORAGE", mode_title(g->mode), NULL, UI_SKY);

    /* the party */
    for (i = 0; i < 6; i++) {
        EidosUiRect rr;
        int slot = SLOT_PARTY0 + i, st;

        (void)pc_mon(g, slot, 0, &mon[slot]);
        rr.x = l.party.x;
        rr.y = l.row_y[i];
        rr.w = l.party.w;
        rr.h = l.row_h;
        st = popup_hover ? 0 : eidos_ui_hit(rr);
        if (st & EIDOS_UI_HOVER) {
            hover_slot = slot;
        }
        ui_party_row(c, rr.x, rr.y, rr.w, i,
                     mon[slot].exists ? (mon[slot].egg ? "Egg" : mon[slot].nick) : NULL,
                     mon[slot].gender, mon[slot].level, mon[slot].hp, mon[slot].maxhp,
                     slot == held_slot ? NULL : icon_of(&mon[slot], 0),
                     sel_slot == slot || (st & EIDOS_UI_HOVER));
        if ((st & EIDOS_UI_CLICK) && mon[slot].exists) {
            pc_click_slot(w, g->mode, slot, in->frame);
        }
    }

    /* the box */
    {
        char name[48];
        int p = l.pitch, gx = l.gx, gy = l.gy, row, col;

        ui_panel(c, l.box.x, l.box.y, l.box.w, l.box.h, UI_CUT_TL | UI_CUT_BR, 6, UI_BODY,
                 UI_SKY, 225);
        pc_box_name(g, g->cur_box, name, (int)sizeof(name));
        if (name[0] == '\0') {
            snprintf(name, sizeof(name), "%s", pc_text(TXT_BOX, (u32)(6 + g->cur_box)));
        }
        {
            int hx = l.box.x + 6, hw = l.box.w - 12, hy = l.box.y + 5;
            EidosUiRect la, ra, wr;
            int sl, sr, wheel;

            la.x = hx; la.y = hy; la.w = 16; la.h = 15;
            ra.x = hx + hw - 16; ra.y = hy; ra.w = 16; ra.h = 15;
            wr = l.box;
            sl = eidos_ui_hit(la);
            sr = eidos_ui_hit(ra);
            wheel = eidos_ui_wheel(wr);
            ui_panel(c, la.x, la.y, la.w, la.h, UI_CUT_TL | UI_CUT_BR, 3,
                     (sl & EIDOS_UI_HOVER) ? UI_NAVY2 : UI_NAVY, UI_SKY, 255);
            draw_chevron(c, la.x + 5, la.y + 3, -1);
            ui_panel(c, ra.x, ra.y, ra.w, ra.h, UI_CUT_TL | UI_CUT_BR, 3,
                     (sr & EIDOS_UI_HOVER) ? UI_NAVY2 : UI_NAVY, UI_SKY, 255);
            draw_chevron(c, ra.x + 5, ra.y + 3, 1);
            ui_panel(c, hx + 20, hy, hw - 40, 15, UI_CUT_TL | UI_CUT_BR, 3, UI_LIGHT, UI_GREY, 255);
            ui_text(c, hx + 20 + (hw - 40 - ui_text_w(name, UI_FONT_LARGE)) / 2,
                    hy + ui_text_y(15, UI_FONT_LARGE), name, UI_INK, -1, UI_FONT_LARGE, 1);
            if (in->frame >= g_suppress) {
                if (sl & EIDOS_UI_CLICK) {
                    pc_tap(16, 28);
                }
                if (sr & EIDOS_UI_CLICK) {
                    pc_tap(151, 28);
                }
                if (wheel != 0 && in->frame >= g_wheel_at + 24) {
                    g_wheel_at = in->frame;
                    pc_tap(wheel > 0 ? 16 : 151, 28);
                }
            }
        }
        for (row = 0; row < 5; row++) {
            for (col = 0; col < 6; col++) {
                int slot = row * 6 + col, cx = gx + col * p, cy = gy + row * p, st;
                EidosUiRect rr;
                const UiImage *ic;

                (void)pc_mon(g, slot, 0, &mon[slot]);
                rr.x = cx; rr.y = cy; rr.w = p; rr.h = p;
                st = popup_hover ? 0 : eidos_ui_hit(rr);
                if (st & EIDOS_UI_HOVER) {
                    hover_slot = slot;
                }
                draw_cell(c, cx, cy, p, st & EIDOS_UI_HOVER, sel_slot == slot);
                if (!mon[slot].exists) {
                    ui_ellipse(c, cx + p / 2 - 5, cy + p / 2 - 5, 9, 9, UI_NONE, 0x0B3A62u, 255);
                }
                ic = icon_of(&mon[slot], (frame / 24) & 1);
                if (ic != NULL && slot != held_slot) {
                    ui_image(c, cx + (p - 2 - ic->w) / 2, cy + (p - 2 - ic->h) / 2 - 1, ic, 1);
                } else if (mon[slot].egg) {
                    ui_text(c, cx + 3, cy + p / 2 - 6, "Egg", UI_WHITE, UI_EDGE, UI_FONT_SMALL, 1);
                }
                if (sel_slot == slot) {
                    ui_cursor_corners(c, cx - 1, cy - 1, p, p, UI_CYAN);
                }
                if (st & EIDOS_UI_CLICK) {
                    pc_click_slot(w, g->mode, slot, in->frame);
                }
            }
        }
    }

    /* the details */
    {
        /* the Pokemon under the mouse, but the chosen one while its list is open */
        int ds;

        if (hover_slot >= 0 && !w->list_shown && !w->yesno && !g_drag && g_hand_drag == 0 &&
            g->hand_mode == 0) {
            key_slot_set(g, w, hover_slot);     /* the keys go on from here */
        }
        ds = busy && held_slot >= 0 ? held_slot
           : (hover_slot >= 0 && !w->list_shown && !w->yesno)
                     ? hover_slot
                     : (sel_slot >= 0 && sel_slot < SLOT_COUNT ? sel_slot : -1);

        if (ds >= 0) {
            (void)pc_mon(g, ds, 1, &det);
        } else {
            memset(&det, 0, sizeof(det));
        }
        draw_details(c, &l, &det, frame);
    }

    /* the Pokemon in the hand follows the stylus */
    if (held_slot >= 0) {
        const UiImage *ic = icon_of(&mon[held_slot], (frame / 12) & 1);
        int hx = mouse.x, hy = mouse.y;

        if (busy && !g_drag && g_hand_dest >= 0) {
            /* dropped: on its slot until the game has put it there */
            int i2 = g_hand_dest;

            if (i2 < SLOT_PARTY0) {
                hx = l.gx + (i2 % 6) * l.pitch + l.pitch / 2;
                hy = l.gy + (i2 / 6) * l.pitch + l.pitch / 2;
            } else {
                hx = l.party.x + l.party.w / 2;
                hy = l.row_y[i2 - SLOT_PARTY0] + 10;
            }
        } else if (hx < 0) {
            /* no mouse: where the game's own hand is (bottom-screen pixels) */
            hx = l.gx + ((int)rd32(g->work + 0xA180) - 11) * l.pitch / 24;
            hy = l.gy + ((int)rd32(g->work + 0xA184) - 39) * l.pitch / 24;
        }
        if (ic != NULL) {
            ui_image(c, hx - ic->w / 2, hy - ic->h / 2, ic, 1);
        }
    }

    /* the action list */
    if (w->list_shown && w->list_n > 0) {
        int lw, bh, lx, ly, n = w->list_n;

        list_geom(&l, n, &lx, &ly, &lw, &bh);
        ui_panel(c, lx - 4, ly - 4, lw + 8, n * (bh + 2) + 6, UI_CUT_TL | UI_CUT_BR, 5, UI_BODY, UI_SKY, 245);
        for (i = 0; i < n; i++) {
            EidosUiRect rr;
            int st, by = ly + i * (bh + 2);
            const char *label = w->list_ids[i] > 0 ? pc_text(TXT_BOX, (u32)w->list_ids[i]) : "";

            rr.x = lx; rr.y = by; rr.w = lw; rr.h = bh;
            st = eidos_ui_hit(rr);
            ui_row(c, lx, by, lw, bh, 0, st & EIDOS_UI_HOVER, &UI_THEME_PARTY);
            ui_text(c, lx + 8, by + ui_text_y(bh, UI_FONT_LARGE), label, UI_WHITE, UI_EDGE, UI_FONT_LARGE, 1);
            if ((st & EIDOS_UI_CLICK) && in->frame >= g_suppress) {
                pc_tap(212, 148 - 24 * (n - 1 - i));
            }
        }
    }

    /* the game's two-choice menu */
    if (w->yesno) {
        static const char *const lab[2] = { "YES", "NO" };
        int bw, bh, bx, by0;

        yesno_geom(&l, &bx, &by0, &bw, &bh);

        ui_panel(c, bx - 6, by0 - 6, bw + 12, 2 * (bh + 4) + 8, UI_CUT_TL | UI_CUT_BR, 5, UI_BODY, UI_SKY, 245);
        for (i = 0; i < 2; i++) {
            EidosUiRect rr;
            int st, by = by0 + i * (bh + 4);

            rr.x = bx; rr.y = by; rr.w = bw; rr.h = bh;
            st = eidos_ui_hit(rr);
            ui_row(c, bx, by, bw, bh, 0, st & EIDOS_UI_HOVER, &UI_THEME_PARTY);
            ui_text(c, bx + (bw - ui_text_w(lab[i], UI_FONT_LARGE)) / 2, by + ui_text_y(bh, UI_FONT_LARGE),
                    lab[i], UI_WHITE, UI_EDGE, UI_FONT_LARGE, 1);
            if ((st & EIDOS_UI_CLICK) && in->frame >= g_suppress) {
                pc_tap(YESNO_X, i == 0 ? YES_Y : NO_Y);
            }
        }
    }

    /* the game's own message */
    if (w->msg && g_bottom != NULL && w->msg_w > 8) {
        int mw = w->msg_w - 2, mx = l.bar.x + (l.bar.w - mw) / 2;

        ui_panel(c, mx - 5, l.bar.y, mw + 10, l.bar.h, UI_CUT_TL | UI_CUT_BR, 5, UI_BODY, UI_SKY, 235);
        ui_blit_screen(c, mx, l.bar.y + 1, g_bottom, 2, 165, mw, 26, 1, 0);
    }

    {
        static const char *const hints[] = { "A", "Select", "B", "Back", "L", "Prev box", "R", "Next box", "X", "Close" };
        static const u32 keys[] = { BW_KEY_A, BW_KEY_B, BW_KEY_L, BW_KEY_R, BW_KEY_X };

        (void)ui_hints(c, l.hint_y, cw, hints, 5, keys);
    }
}

static const EidosUiRecipe recipe_pc = {
    "pc", pc_match, pc_run, pc_reset
};

const EidosUiRecipe *const ui_recipes_pc[] = { &recipe_pc };
const u32 ui_recipes_pc_count = 1;
