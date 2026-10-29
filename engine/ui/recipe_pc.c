/*
 * engine/ui/recipe_pc.c -- the one-screen recipe for the PC boxes (ov170,
 * "BOX2"; docs/unified-view-recipes.md, section 11).
 *
 * What it shows
 * -------------
 *
 * Three panels, the "Desk": the party on the left (name, level and HP bar of
 * each of the six), the box in the middle (its name with arrows, the 6 x 5
 * grid with the game's Pokemon icons, a strip of the boxes when the canvas
 * is tall enough), and the details of the Pokemon under the mouse or the
 * keys' focus on the right with four buttons (SUMMARY, ITEM, MARK,
 * RELEASE).  The footer's hints are buttons too.
 *
 * Who owns what
 * -------------
 *
 * The recipe owns the interaction: the Pokemon in the hand, the focus, the
 * box list, the action menu and the marking panel.  The game owns the data
 * and its rules.  A Pokemon is picked up and put down, with a click, with a
 * drag, or with A; there is no action list in between.  A drop is applied
 * by the game's own code (BOX2_MAIN_HandleMonPickup checks it,
 * BOX2_MAIN_MoveCursorWithMon and BOX2_MAIN_UpdateBoxSlots do it) on the
 * game's thread, so the rules (a full box, Mail, the last Pokemon able to
 * battle, the Battle Box) and the mod events (box.deposit, box.withdraw)
 * stay the game's.  The reason a drop is refused is the game's own line
 * (message bank 10), shown before the drop when the target is hovered.
 *
 * Where the state comes from
 * --------------------------
 *
 * Everything shown is read from the game's own structs, nothing is copied:
 *   - the proc on the game's proc stack, overlay 170, its BoxTask (current
 *     box, state) and BoxParam (mode, the box save, the party);
 *   - the box Pokemon through BOX_SAVEDATA_GetPokemon and
 *     POKE_TOOL_GetBoxValue, the party through POKEPARTY_GetMember, the box
 *     names from the box save header;
 *   - the texts (species, moves, abilities, items, the modes' titles, the
 *     rules) from the game's message archive a/0/0/2 (banks 70, 203, 182,
 *     54, 9 and 10), decoded like the kit decodes its font;
 *   - where the game's own windows are (BGWINFRM_GetPos on BoxWork.winMgr:
 *     the action list and the party tray) and, for what has no struct (how
 *     many list buttons, the white message window, YES / NO), a few pixels
 *     of the bottom screen.
 *
 * What the game still does
 * ------------------------
 *
 * The summary, the held item and the release question stay the game's: the
 * recipe plays a short script on the game (the key cursor on the slot, A,
 * DOWN to the entry, A; for a party Pokemon the party tray opens first and
 * closes again afterwards), and the game's own list, message and YES / NO
 * are drawn as buttons where they were.  The box list, the action menu and
 * the marking panel are the recipe's own; marking is written through
 * POKE_TOOL_SetBoxValue.  In the item mode (3) the hand carries an item: a
 * drop trades the two Pokemon's items, written the same way.  Tuck in (5)
 * shows the DS screens, as does the game's own marking panel, box list or
 * wallpaper menu
 * should the game open one, so the player is never stuck.  EIDOS_PC_DEBUG
 * logs the task and the windows every 10 frames; EIDOS_PC_DRAG scripts a
 * mouse drag for checks.
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
extern int   BOX_SAVEDATA_CountPokemonInBox(void *gameData, u32 box) BW_PC_WEAK;
extern void  POKE_TOOL_SetBoxValue(void *boxMon, u32 paramId, PcPtr value) BW_PC_WEAK;
/* the box application's own functions (overlay 170, named by the build) */
extern int   ov170_BOX2_MAIN_HandleMonPickup(void *task, u32 a, u32 b) BW_PC_WEAK;
extern int   ov170_BOX2_MAIN_MoveCursorWithMon(void *task, u32 a, u32 b) BW_PC_WEAK;
extern void  ov170_BOX2_MAIN_UpdateBoxSlots(void *task) BW_PC_WEAK;
extern void  ov170_BOX2_SEQ_ShowAllSlotIcons(void *task) BW_PC_WEAK;
extern void  ov170_BOX2_SEQ_ShowPartyIcons(void *task) BW_PC_WEAK;

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
#define PC_MODE_ITEMS    3              /* BoxParam.mode: Move Items      */

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
    int taskmenu;           /* the yes/no menu's pointer (debug only: the
                             * game leaves it set after the menu closes) */
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
    int  item, ability, mark;
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
    m->item = (int)POKE_TOOL_GetBoxValue(bm, 0x06, 0);
    m->mark = (int)POKE_TOOL_GetBoxValue(bm, 0x0B, 0);
    memset(name, 0xFF, sizeof(name));
    (void)POKE_TOOL_GetBoxValue(bm, 0x74, (PcPtr)name);
    nick_utf8(name, m->nick, (int)sizeof(m->nick));
    if (pp != NULL && POKE_TOOL_GetPartyValue != NULL) {
        m->hp = (int)POKE_TOOL_GetPartyValue(pp, 0xA0, 0);
        m->maxhp = (int)POKE_TOOL_GetPartyValue(pp, 0xA1, 0);
    }
    if (full) {
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
/* Taps, keys and the game's own windows                                */
/* ------------------------------------------------------------------ */

#define PARTY_BTN_X   47        /* the PARTY PKMN button, bottom left      */
#define PARTY_BTN_Y   180
#define YESNO_X       222       /* the game's two-choice menu              */
#define YES_Y         109
#define NO_Y          133
#define BOX_ARROW_Y   28        /* the box arrows of the box scene         */
#define BOX_ARROW_L   16
#define BOX_ARROW_R   151

typedef struct PcWin {
    int list_shown;         /* the action list is on screen               */
    int list_n;             /* its buttons                                */
    int list_ids[6];        /* their message ids (bank 9)                 */
    int list_cur;           /* the button the game's red hand points at, -1 */
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
        /* the game's cursor: a red hand over the right of a button (it bobs) */
        w->list_cur = -1;
        for (i = 0; i < w->list_n && w->list_cur < 0; i++) {
            int cy = 148 - 24 * (w->list_n - 1 - i), hx, hy;

            for (hy = cy - 18; hy <= cy - 4 && w->list_cur < 0; hy += 2) {
                for (hx = 214; hx <= 230; hx += 2) {
                    if (px_near(hx, hy, 197, 0, 0, 24)) {
                        w->list_cur = i;
                        break;
                    }
                }
            }
        }
    }
    /* the white message window: its width is how far the white goes */
    w->msg = px_light(6, 168) && px_light(40, 168);
    w->msg_w = 0;
    if (w->msg) {
        int mx = 6;

        while (mx < 254 && px_light(mx, 168)) {
            mx++;
        }
        w->msg_w = mx + 3;          /* the window's frame is two pixels more */
        if (w->msg_w > 254) {
            w->msg_w = 254;
        }
    }
    w->yesno = !w->list_shown && w->msg && yn_px(YESNO_X, YES_Y) && yn_px(YESNO_X, NO_Y);
}

/* What this recipe has no picture for: Tuck in (5) and the
 * game's own marking panel, box list and wallpaper menu (states 85, 96,
 * 100), which this recipe never opens; they show the DS screens until the
 * game leaves them, so the player is never stuck. */
static int pc_unsupported(const PcGame *g)
{
    return g->mode < 0 || g->mode > 4 || g->state == 85 || g->state == 96 ||
           g->state == 100;
}

/* ------------------------------------------------------------------ */
/* A script of steps that drive the game's own screens                  */
/* ------------------------------------------------------------------ */

/*
 * What the recipe does not draw itself (the summary, the release question,
 * the held item) the game does.  A script is a short list of steps the
 * recipe plays on the game: put its key cursor on a slot, press A, wait for
 * the party tray or the action list, press DOWN to the entry, press A.  It
 * waits for the game to settle between steps (the same state for a few
 * frames), so it needs no timing of its own.  The platform runs faster
 * meanwhile (eidos_platform_fast_forward_lock).
 */
enum { SC_KEY = 1, SC_CUR, SC_TAP, SC_SETTLE, SC_TRAY_OPEN, SC_TRAY_SHUT, SC_LIST };

typedef struct PcStep { int kind, a, b; } PcStep;

#define PC_SCRIPT_MAX 40
static PcStep g_sc[PC_SCRIPT_MAX];
static int g_sc_n, g_sc_i;
static u32 g_sc_start;
static u32 g_sig_since;         /* the frame the game's windows last changed */
static u32 g_sig;
static int g_back;              /* 1: list to go, 2: wait for it back, then B */
static u32 g_back_at;
static u32 g_last_run;          /* the last frame this recipe ran on        */

static int pc_script_busy(void)
{
    return g_sc_i < g_sc_n;
}

static void sc_add(int kind, int a, int b)
{
    if (g_sc_n < PC_SCRIPT_MAX) {
        g_sc[g_sc_n].kind = kind;
        g_sc[g_sc_n].a = a;
        g_sc[g_sc_n].b = b;
        g_sc_n++;
    }
}

static void sc_key(u32 key)
{
    sc_add(SC_KEY, (int)key, 0);
    sc_add(SC_SETTLE, 14, 0);
}

/* Open the game's action list on a slot and take its entry `entry` (the
 * list's own order: MOVE first). */
static void pc_script_action(int slot, int entry)
{
    int i;

    g_sc_n = g_sc_i = 0;
    if (slot >= SLOT_PARTY0) {
        sc_add(SC_CUR, 33, 0);                  /* the PARTY PKMN button      */
        sc_key(BW_KEY_A);
        sc_add(SC_TRAY_OPEN, 0, 0);
        sc_add(SC_SETTLE, 12, 0);
        sc_add(SC_CUR, slot - SLOT_PARTY0, 0);
    } else {
        sc_add(SC_CUR, slot, 0);
    }
    sc_key(BW_KEY_A);
    sc_add(SC_LIST, 0, 0);
    sc_add(SC_SETTLE, 6, 0);
    for (i = 0; i < entry; i++) {
        sc_key(BW_KEY_DOWN);
    }
    sc_key(BW_KEY_A);
}

static void pc_script_close_tray(void)
{
    g_sc_n = g_sc_i = 0;
    sc_add(SC_TAP, PARTY_BTN_X, PARTY_BTN_Y);
    sc_add(SC_TRAY_SHUT, 0, 0);
    sc_add(SC_SETTLE, 8, 0);
}

static void pc_script_run(const PcGame *g, const PcWin *w, u32 frame)
{
    PcStep *s;
    u32 sig = (u32)g->state | ((u32)w->tray << 12) | ((u32)w->ctray << 13) |
              ((u32)w->list_shown << 14) | ((u32)w->msg << 15) | ((u32)w->yesno << 16) |
              ((u32)g->hand_mode << 17);

    if (sig != g_sig) {
        g_sig = sig;
        g_sig_since = frame;
    }
    {
        u32 last = g_last_run;

        g_last_run = frame;
        if (g_back == 1 && frame > last + 3) {  /* the recipe was away (summary) */
            g_back = 2;
            g_sig_since = frame;
        }
    }
    if (!pc_script_busy()) {
        /* the game's screen (summary, bag) ends back in its action list:
         * leave the list so the player returns to the PC's own picture */
        if (g_back > 0 && frame > g_back_at + 900) {
            g_back = 0;
        } else if (g_back == 1 && !w->list_shown) {
            g_back = 2;
        } else if (g_back == 2 && w->list_shown && frame >= g_sig_since + 12) {
            eidos_ui_press(BW_KEY_B, 6);
            g_back = 0;
        }
        return;
    }
    if (frame > g_sc_start + 900) {                 /* the game never settled */
        g_sc_i = g_sc_n = 0;
        return;
    }
    s = &g_sc[g_sc_i];
    if (getenv("EIDOS_PC_DEBUG") != NULL && frame % 10 == 0) {
        fprintf(stderr, "pcsc f=%u i=%d/%d kind=%d a=%d since=%u sig=%x\n", frame, g_sc_i, g_sc_n, s->kind, s->a, g_sig_since, sig);
    }
    switch (s->kind) {
    case SC_KEY:
        eidos_ui_press((u32)s->a, 6);
        g_sig_since = frame;
        g_sc_i++;
        break;
    case SC_CUR:
        if (g->cm != NULL) {
            g->cm[CM_CUR] = (u8)s->a;
        }
        g_sc_i++;
        break;
    case SC_TAP:
        eidos_ui_tap(s->a, s->b, 6);
        g_sig_since = frame;
        g_sc_i++;
        break;
    case SC_SETTLE:
        if (frame >= g_sig_since + (u32)s->a) {
            g_sc_i++;
        }
        break;
    case SC_TRAY_OPEN:
        g_sc_i += w->tray != 0;
        break;
    case SC_TRAY_SHUT:
        g_sc_i += w->tray == 0;
        break;
    case SC_LIST:
        g_sc_i += w->list_shown != 0;
        break;
    default:
        g_sc_i = g_sc_n;
        break;
    }
}

/* ------------------------------------------------------------------ */
/* Changes through the game's own functions (on the game's thread)      */
/* ------------------------------------------------------------------ */

/*
 * A drop is applied by the game's own code, the code its drop calls: the
 * move is checked (BOX2_MAIN_HandleMonPickup: a full box, Mail, the last
 * Pokemon able to battle, the Battle Box's rules; the reason is left in the
 * work area's result byte) and filled into the game's move table
 * (BOX2_MAIN_MoveCursorWithMon), then BOX2_MAIN_UpdateBoxSlots does it, so
 * the mod events fire and a mod can still refuse.  The functions allocate
 * and draw, so they run on the game's thread (eidos_ui_game_call).
 * Slots: 0..29 the box, 30..35 the party, 36 + n the n-th box of the box
 * list (relative to the list's origin).  The game reads the box a Pokemon
 * was lifted from in BoxTask.heldSlot, which is set for the call.
 */
#define BW_RESULT 0xA173        /* BoxWork.result                          */
#define BW_SEQ_ENTRY 0x60       /* BoxWork.seq.entry                       */
#define BT_PREVBOX 0x19         /* BoxTask.prevBox: the box list's origin  */

enum { OP_PROBE = 1, OP_APPLY, OP_MARK, OP_ITEM };

typedef struct PcOp {
    volatile int pending;       /* queued, not yet run                    */
    volatile int done;          /* run, the result below is valid         */
    volatile int ok;
    volatile int code;          /* the work area's result byte            */
    int kind, a, b, src, tobox, value;
    u32 seq;
} PcOp;

static PcOp g_op;
static u8 *volatile g_task_ptr; /* the box task, for the game's thread    */

/* A Pokemon's box data: a party slot, or a slot of a box. */
static void *op_boxmon(u8 *t, int slot, int box)
{
    u8 *params = gptr(rd32(t + BT_PARAMS));
    void *pp;

    if (params == NULL) {
        return NULL;
    }
    if (slot >= SLOT_PARTY0) {
        pp = POKEPARTY_GetMember(gptr(rd32(params + BP_PARTY)), (u32)(slot - SLOT_PARTY0));
        return pp != NULL ? POKE_TOOL_CastToBoxParam(pp) : NULL;
    }
    return BOX_SAVEDATA_GetPokemon(gptr(rd32(params + BP_GAMEDATA)), (u32)box, (u32)slot);
}

static void pc_op_run(void *arg)
{
    u8 *t = g_task_ptr;
    u32 a = (u32)g_op.a, b = (u32)g_op.b;
    u8 saved;

    (void)arg;
    g_op.ok = 0;
    g_op.code = 0;
    if (t == NULL || ov170_BOX2_MAIN_HandleMonPickup == NULL || ov170_BOX2_MAIN_MoveCursorWithMon == NULL ||
        ov170_BOX2_MAIN_UpdateBoxSlots == NULL) {
        g_op.code = -1;
        g_op.done = 1;
        g_op.pending = 0;
        return;
    }
    {
        u8 *work = gptr(rd32(t + BT_WORK));

        saved = t[BT_HELD];
        if (g_op.kind == OP_MARK) {
            void *bm = op_boxmon(t, g_op.a, g_op.src);

            if (bm != NULL && POKE_TOOL_SetBoxValue != NULL) {
                POKE_TOOL_SetBoxValue(bm, 0x0B, (PcPtr)g_op.value);
                g_op.ok = 1;
            }
        } else if (g_op.kind == OP_ITEM) {
            /* the items of two Pokemon trade places (one may hold none) */
            void *from = op_boxmon(t, g_op.a, g_op.src), *to = op_boxmon(t, g_op.b, t[BT_CURBOX]);

            if (from != NULL && to != NULL && from != to && POKE_TOOL_SetBoxValue != NULL) {
                u32 fi = POKE_TOOL_GetBoxValue(from, 0x06, 0), ti = POKE_TOOL_GetBoxValue(to, 0x06, 0);

                POKE_TOOL_SetBoxValue(from, 0x06, (PcPtr)ti);
                POKE_TOOL_SetBoxValue(to, 0x06, (PcPtr)fi);
                g_op.ok = 1;
            }
        } else if (work != NULL) {
            t[BT_HELD] = (u8)g_op.src;
            if (g_op.tobox >= 0) {                  /* a box of the list      */
                u32 limit = t[BT_BOXLIMIT] != 0 ? t[BT_BOXLIMIT] : 1;

                b = 36u + ((u32)g_op.tobox + limit - t[BT_PREVBOX] % limit) % limit;
            }
            if (g_op.kind == OP_PROBE) {
                g_op.ok = ov170_BOX2_MAIN_HandleMonPickup(t, a, b) != 0;
            } else {
                /* the game keeps the move it carries out in a block it makes
                 * when a sequence starts; there is none at rest, so the
                 * call gets one of its own (BoxMove: 0xB8 bytes) */
                static u32 move[0x30];
                u32 *entry = (u32 *)(work + BW_SEQ_ENTRY), saved_entry = *entry;

                memset(move, 0, sizeof(move));
                *entry = (u32)(PcPtr)move;
                if (ov170_BOX2_MAIN_MoveCursorWithMon(t, a, b) != 0) {
                    ov170_BOX2_MAIN_UpdateBoxSlots(t);
                    /* the game's own screen follows: its icons are sprites
                     * the mover would have carried */
                    if (ov170_BOX2_SEQ_ShowAllSlotIcons != NULL) {
                        ov170_BOX2_SEQ_ShowAllSlotIcons(t);
                    }
                    if (ov170_BOX2_SEQ_ShowPartyIcons != NULL) {
                        ov170_BOX2_SEQ_ShowPartyIcons(t);
                    }
                    g_op.ok = 1;
                }
                *entry = saved_entry;
            }
            g_op.code = work[BW_RESULT];
            t[BT_HELD] = saved;
        }
    }
    g_op.done = 1;
    g_op.pending = 0;
}

static int pc_op_start(int kind, int a, int b, int src, int tobox, int value)
{
    if (g_op.pending) {
        return 0;
    }
    g_op.kind = kind;
    g_op.a = a;
    g_op.b = b;
    g_op.src = src;
    g_op.tobox = tobox;
    g_op.value = value;
    g_op.done = 0;
    g_op.pending = 1;
    g_op.seq++;
    if (!eidos_ui_game_call(pc_op_run, NULL)) {
        g_op.pending = 0;
        return 0;
    }
    return 1;
}

/* ------------------------------------------------------------------ */
/* Layout (option A: the party, the box, the details)                   */
/* ------------------------------------------------------------------ */

typedef struct PcLay {
    int cw, ch;
    int y0, y1;             /* the panels' vertical range                 */
    int px, pw, py, row_h, row_gap;
    int bx, bw, by, bh, gx, gy, pitch, head_y, strip_y, msg_y, show_strip;
    int dx, dw, dh;         /* the details panel                          */
    int chip_y;
    int hint_y;
} PcLay;

static int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

/*
 * Option A's proportions at any size: the party about a quarter of the
 * width, the box as large as the height allows, the details a little less
 * than a third.  The grid's pitch grows with the canvas (the 720p mock-up
 * has 24-pixel cells on a 427-pixel canvas; a larger canvas keeps the same
 * shares), and the party's six rows fill the column's height.
 */
static void pc_layout(PcLay *l, int cw, int ch)
{
    const int m = 6, gap = 5;
    int avail, ph, pw_want, dw_min, dw_max, spare;

    memset(l, 0, sizeof(*l));
    l->cw = cw;
    l->ch = ch;
    l->hint_y = ch - 17;
    l->y0 = 31;
    l->y1 = ch - 21;
    avail = cw - 2 * m - 2 * gap;
    l->bh = l->y1 - l->y0;
    pw_want = clampi(avail * 24 / 100, 104, 176);
    dw_min = clampi(avail * 27 / 100, 112, 220);
    dw_max = clampi(avail * 32 / 100, 112, 260);
    /* the grid: the width the party and the details leave, and the height
     * under the header, the message line and the box strip */
    l->pitch = (avail - pw_want - dw_min - 12) / 6;
    ph = (l->bh - 4 - 18 - 3 - 20 - 3 - 25) / 5;
    l->show_strip = ph >= 28;
    if (!l->show_strip) {
        ph = (l->bh - 4 - 18 - 3 - 20 - 3 - 6) / 5;
    }
    if (ph < l->pitch) {
        l->pitch = ph;
    }
    l->pitch = clampi(l->pitch, 20, 48);
    l->bw = 6 * l->pitch + 12;
    /* what is left goes to the details up to their share, then to both
     * side columns evenly */
    l->pw = pw_want;
    l->dw = avail - l->pw - l->bw;
    if (l->dw > dw_max) {
        spare = l->dw - dw_max;
        l->dw = dw_max;
        l->pw += spare / 2;
        l->dw += spare - spare / 2;
    }
    l->px = m;
    l->py = l->y0;
    l->bx = l->px + l->pw + gap;
    l->by = l->y0;
    l->head_y = l->by + 4;
    l->gx = l->bx + 6;
    l->gy = l->by + 4 + 18 + 3;
    l->strip_y = l->gy + 5 * l->pitch + 3;
    l->msg_y = l->by + l->bh - 22;           /* 6 pixels above the box's edge */
    l->dx = l->bx + l->bw + gap;
    l->dh = l->bh - 34;
    l->chip_y = l->y0 + l->dh + 4;
    /* the party's rows fill the column: taller rows, then the gaps */
    l->row_h = clampi((l->bh - 5 * 4) / 6, 30, 46);
    l->row_gap = clampi((l->bh - 6 * l->row_h) / 5, 0, 10);
}

static int pc_row_y(const PcLay *l, int i)
{
    return l->py + i * (l->row_h + l->row_gap);
}

static int in_rect(int x, int y, int rx, int ry, int rw, int rh)
{
    return x >= rx && y >= ry && x < rx + rw && y < ry + rh;
}

/* The slot under a canvas position, or -1. */
static int pc_slot_at(const PcLay *l, int mx, int my)
{
    int i;

    if (mx < 0 || my < 0) {
        return -1;
    }
    for (i = 0; i < 6; i++) {
        if (in_rect(mx, my, l->px, pc_row_y(l, i), l->pw, l->row_h)) {
            return SLOT_PARTY0 + i;
        }
    }
    if (in_rect(mx, my, l->gx, l->gy, 6 * l->pitch, 5 * l->pitch)) {
        return ((my - l->gy) / l->pitch) * 6 + (mx - l->gx) / l->pitch;
    }
    return -1;
}

/* The box strip's pip under a position (24 pips, 12 to a row), or -1. */
static int pc_pip_at(const PcLay *l, int limit, int mx, int my)
{
    int pw = (6 * l->pitch) / 12, i;

    if (!l->show_strip) {
        return -1;
    }
    for (i = 0; i < limit && i < 24; i++) {
        if (in_rect(mx, my, l->gx + (i % 12) * pw, l->strip_y + (i / 12) * 9, pw, 9)) {
            return i;
        }
    }
    return -1;
}

/* ------------------------------------------------------------------ */
/* Small drawing pieces                                                 */
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
    static const int idx[6] = { 37, 38, 39, 40, 41, 42 };

    if (mode < 0 || mode > 5) {
        return "";
    }
    return pc_text(TXT_PROMPT, (u32)idx[mode]);
}

static const UiImage *icon_of(const PcMon *m, int frame)
{
    if (!m->exists || m->egg || m->species <= 0) {
        return NULL;
    }
    return ui_mon_icon((u16)m->species, (u8)m->form, frame);
}

static void rect_outline(EidosUiCtx *c, int x, int y, int w, int h, u32 col, int a)
{
    ui_fill(c, x, y, w, 1, col, a);
    ui_fill(c, x, y + h - 1, w, 1, col, a);
    ui_fill(c, x, y, 1, h, col, a);
    ui_fill(c, x + w - 1, y, 1, h, col, a);
}

static void draw_cell(EidosUiCtx *c, int x, int y, int p, int hover, int selected)
{
    u32 edge = selected ? UI_SKY : 0x0B3A62u;

    ui_fill(c, x, y, p - 2, p - 2, hover ? 0x0E3A5Cu : 0x08243Eu, 235);
    rect_outline(c, x, y, p - 2, p - 2, edge, 255);
}

static void draw_chevron(EidosUiCtx *c, int x, int y, int dir)
{
    int i;

    for (i = 0; i < 5; i++) {
        int h = 9 - 2 * i;

        ui_fill(c, dir < 0 ? x + 5 - i : x + i, y + i, 1, h, UI_WHITE, 255);
    }
}

/* The six marks of the game: circle, triangle, square, heart, star, diamond. */
static void mark_shape(EidosUiCtx *c, int x, int y, int kind, u32 col)
{
    static const char *const pat[6][7] = {
        { "..###..", ".#####.", "#######", "#######", "#######", ".#####.", "..###.." },
        { "...#...", "..###..", "..###..", ".#####.", ".#####.", "#######", "#######" },
        { "#######", "#######", "#######", "#######", "#######", "#######", "#######" },
        { ".##.##.", "#######", "#######", "#######", ".#####.", "..###..", "...#..." },
        { "...#...", "...#...", "#######", ".#####.", "..###..", ".##.##.", ".#...#." },
        { "...#...", "..###..", ".#####.", "#######", ".#####.", "..###..", "...#..." } };
    int i, j;

    for (j = 0; j < 7; j++) {
        for (i = 0; i < 7; i++) {
            if (pat[kind][j][i] == '#') {
                ui_fill(c, x + i, y + j, 1, 1, col, 255);
            }
        }
    }
}

static void marks_small(EidosUiCtx *c, int x, int y, int bits)
{
    int i;

    for (i = 0; i < 6; i++) {
        mark_shape(c, x + i * 9, y, i, (bits >> i) & 1 ? UI_CYAN : UI_EDGE);
    }
}

static void chip(EidosUiCtx *c, int x, int y, int w, int h, const char *label, int sel, int enabled)
{
    ui_panel(c, x, y, w, h, UI_CUT_TL | UI_CUT_BR, 3, sel ? UI_CYAN2 : UI_BODY2,
             sel ? UI_CYAN_EDGE : UI_EDGE, 255);
    ui_text(c, x + (w - ui_text_w(label, UI_FONT_SMALL)) / 2, y + ui_text_y(h, UI_FONT_SMALL),
            label, enabled ? UI_WHITE : UI_EDGE2, enabled ? (s32)UI_EDGE : -1, UI_FONT_SMALL, 1);
}

static void item_dot(EidosUiCtx *c, int x, int y)
{
    ui_ellipse(c, x, y, 7, 7, UI_WHITE, UI_INK, 255);
    ui_fill(c, x + 1, y + 3, 5, 1, 0xDE184Au, 255);
}

/* The one line under the grid: the game's own words for what the move
 * would do or why it cannot be done. */
static void msg_line(EidosUiCtx *c, int x, int y, int w, const char *text, u32 col)
{
    /* cut like the panels around it */
    /* the large font, or the small one when the line is too long for it */
    int font = ui_text_w(text, UI_FONT_LARGE) <= w - 12 ? UI_FONT_LARGE : UI_FONT_SMALL;

    ui_panel(c, x, y, w, 16, UI_CUT_TL | UI_CUT_BR, 3, 0x06182Cu, UI_NAVY2, 255);
    ui_text(c, x + 6, y + ui_text_y(16, font), text, col, UI_EDGE, font, 1);
}

/* The game's own message ("Dom was released."), not its white window: the
 * dark letters of the bottom screen's message window, drawn white in the
 * box's message line (the game's font is the kit's, so it reads the same).
 * A two-line message shows its lines one above the other, the line grown. */
static void msg_game(EidosUiCtx *c, const PcLay *l, const PcWin *w)
{
    int x0 = l->bx + 6, wmax = l->bw - 12, sw = w->msg_w - 4, rows = 0, top = 25, bot = 0, xx, yy;

    if (sw > wmax - 8) {
        sw = wmax - 8;
    }
    /* the rows of the window that hold letters */
    for (yy = 0; yy < 26; yy++) {
        for (xx = 0; xx < sw; xx++) {
            const u8 *p = g_bottom + ((size_t)(166 + yy) * 256 + (size_t)(4 + xx)) * 4;

            if (p[0] + p[1] + p[2] < 3 * 110) {
                top = yy < top ? yy : top;
                bot = yy > bot ? yy : bot;
                break;
            }
        }
    }
    rows = bot >= top ? bot - top + 1 : 0;
    {
        int h = rows + 6 > 16 ? rows + 6 : 16, y = l->msg_y + 16 - h;

        ui_panel(c, x0, y, wmax, h, UI_CUT_TL | UI_CUT_BR, 3, 0x06182Cu, UI_NAVY2, 255);
        for (yy = 0; yy < rows; yy++) {
            for (xx = 0; xx < sw; xx++) {
                const u8 *p = g_bottom + ((size_t)(166 + top + yy) * 256 + (size_t)(4 + xx)) * 4;

                if (p[0] + p[1] + p[2] < 3 * 110) {
                    ui_fill(c, x0 + 6 + xx, y + 3 + yy, 1, 1, UI_WHITE, 255);
                }
            }
        }
    }
}

static void ghost(EidosUiCtx *c, int x, int y, const PcMon *m, int frame)
{
    const UiImage *ic = g_g.mode == PC_MODE_ITEMS ? ui_item_icon((u16)m->item)
                                                  : icon_of(m, (frame / 12) & 1);

    if (ic != NULL) {
        ui_fill(c, x - 14, y + 12, 28, 3, 0x000000u, 90);
        ui_image(c, x - ic->w / 2, y - ic->h / 2 - 4, ic, 1);
    }
}

/* A party row: the slot number, the icon, name, level and HP bar. */
static void party_chip(EidosUiCtx *c, int x, int y, int w, int h, int slot, const PcMon *m,
                       int sel, int hover, int dim, int frame)
{
    char lv[16];
    int nx;

    if (!m->exists) {
        char n[2];

        n[0] = (char)('1' + slot);
        n[1] = 0;
        ui_panel(c, x, y, w, h, UI_CUT_TL | UI_CUT_BR, 5, 0x06182Cu, UI_NAVY, hover ? 255 : 200);
        ui_text(c, x + 3, y + ui_text_y(h, UI_FONT_SMALL), n, UI_EDGE2, -1, UI_FONT_SMALL, 1);
        return;
    }
    ui_panel(c, x, y, w, h, UI_CUT_TL | UI_CUT_BR, 5, sel || hover ? UI_NAVY2 : UI_NAVY,
             sel ? UI_SKY : 0x084A73u, 235);
    ui_fill(c, x + 2, y + 2, 9, h - 4, 0x04182Cu, 255);
    {
        char n[2];

        n[0] = (char)('1' + slot);
        n[1] = '\0';
        ui_text(c, x + 3, y + ui_text_y(h, UI_FONT_SMALL), n, UI_LIGHT, UI_EDGE, UI_FONT_SMALL, 1);
    }
    if (!dim) {
        const UiImage *ic = icon_of(m, (frame / 24) & 1);

        if (ic != NULL) {
            ui_image(c, x + 11 + (22 - ic->w) / 2, y + (h - ic->h) / 2, ic, 1);
        }
    }
    if (h >= 40) {
        /* a tall row (a large canvas): the name in the large font */
        int top = y + (h - 36) / 2;

        nx = ui_text(c, x + 34, top, m->egg ? "Egg" : m->nick, UI_WHITE, UI_EDGE, UI_FONT_LARGE, 1);
        if (!m->egg) {
            ui_gender(c, nx + 2, top, m->gender, UI_FONT_LARGE);
            fmt_int(lv, "Lv.", m->level);
            ui_text(c, x + 34, top + 17, lv, UI_LIGHT, UI_EDGE, UI_FONT_SMALL, 1);
            ui_bar(c, x + 34, top + 29, w - 34 - 6, 5, m->hp, m->maxhp);
        }
        return;
    }
    {
        int top = y + (h - 30) / 2;

        nx = ui_text(c, x + 34, top + 1, m->egg ? "Egg" : m->nick, UI_WHITE, UI_EDGE, UI_FONT_SMALL, 1);
        if (!m->egg) {
            ui_gender(c, nx + 2, top + 1, m->gender, UI_FONT_SMALL);
            fmt_int(lv, "Lv.", m->level);
            ui_text(c, x + 34, top + 11, lv, UI_LIGHT, UI_EDGE, UI_FONT_SMALL, 1);
            ui_bar(c, x + 34, top + 22, w - 34 - 6, 4, m->hp, m->maxhp);
        }
    }
}

/* The details: name, level, icon with the types and marks, ability, item,
 * the moves; rows drop from the bottom when the panel is short. */
static void details_panel(EidosUiCtx *c, int x, int y, int w, int h, const PcMon *m,
                          int item_focus, int frame)
{
    int ix = x + 5, iw = w - 10, iy = y + 4, well = 38, i;
    const UiImage *ic;

    ui_panel(c, x, y, w, h, UI_CUT_TL | UI_CUT_BR, 6, UI_BODY, UI_EDGE, 225);
    if (!m->exists) {
        ui_text(c, ix, iy, "Empty", UI_GREY, UI_EDGE, UI_FONT_LARGE, 1);
        return;
    }
    {
        char lv[16];
        int nx = ui_text(c, ix, iy, m->egg ? "Egg" : m->nick, UI_WHITE, UI_EDGE, UI_FONT_LARGE, 1);

        if (!m->egg) {
            ui_gender(c, nx + 3, iy, m->gender, UI_FONT_LARGE);
            fmt_int(lv, "Lv.", m->level);
            ui_text(c, x + w - 5 - ui_text_w(lv, UI_FONT_SMALL), iy + 2, lv, UI_LIGHT, UI_EDGE,
                    UI_FONT_SMALL, 1);
        }
    }
    iy += 17;
    ui_panel(c, ix, iy, well, well, UI_CUT_TL | UI_CUT_BR, 4, 0x0B1E34u, UI_EDGE, 255);
    ic = icon_of(m, (frame / 24) & 1);
    if (ic != NULL) {
        ui_image(c, ix + (well - ic->w) / 2, iy + (well - ic->h) / 2, ic, 1);
    }
    if (m->egg) {
        return;
    }
    {
        int tx = ix + well + 4, tw = iw - well - 4;

        if (tw > 46) {
            tw = 46;
        }
        if (tw >= 30) {
            ui_type_tag(c, tx, iy + 1, tw, m->type1);
            if (m->type2 != m->type1) {
                ui_type_tag(c, tx, iy + 1 + 14, tw, m->type2);
            }
        }
        marks_small(c, tx, iy + well - 8, m->mark);
    }
    iy += well + 3;
    ui_info_row(c, ix, iy, iw, "Ability", pc_text(TXT_ABILITY, (u32)m->ability));
    iy += 17;
    ui_info_row(c, ix, iy, iw, "Item", m->item > 0 ? pc_text(TXT_ITEM, (u32)m->item) : "None");
    if (item_focus) {
        rect_outline(c, ix - 1, iy - 1, iw + 2, 17, UI_CYAN, 255);
    }
    iy += 19;
    for (i = 0; i < 4; i++) {
        if (iy + 13 > y + h - 3) {
            break;
        }
        ui_panel(c, ix, iy, iw, 12, UI_CUT_TL | UI_CUT_BR, 2, UI_BODY2, UI_EDGE, 255);
        ui_text(c, ix + 4, iy + ui_text_y(12, UI_FONT_SMALL),
                m->moves[i] > 0 ? pc_text(TXT_MOVE, (u32)m->moves[i]) : "-----",
                m->moves[i] > 0 ? UI_WHITE : UI_EDGE2, UI_EDGE, UI_FONT_SMALL, 1);
        iy += 13;
    }
}

/* ------------------------------------------------------------------ */
/* What the player holds, where the focus is                            */
/* ------------------------------------------------------------------ */

/*
 * The recipe owns the interaction: it keeps the Pokemon in the hand, the
 * focus and the overlay itself, and asks the game to apply a drop (see
 * above).  Mouse and keys fill the same few actions (pick up, put down, put
 * back, change box, open an overlay), so every task is done with either.
 */
enum { FOC_BOX_PREV = 100, FOC_BOX_NAME, FOC_BOX_NEXT, FOC_CHIP0 = 110 };
enum { OV_NONE, OV_BOXES, OV_MENU, OV_MARK };
enum { ACT_SUMMARY, ACT_ITEM, ACT_MARK, ACT_RELEASE, ACT_COUNT };

static int g_hand = -1;         /* the slot carried; -1 nothing             */
static int g_hand_box;          /* the box it was lifted from               */
static PcMon g_hand_mon;
static int g_focus;             /* the keys' focus: a slot or a FOC_*       */
static int g_sel = 0;           /* the Pokemon the chips act on             */
static int g_press = -1;        /* a slot the button went down on           */
static int g_press_x, g_press_y;
static int g_dragging;          /* the Pokemon follows the mouse            */
static int g_ov, g_ov_cur, g_ov_slot;
static char g_msg[128];
static u32 g_msg_until;
static int g_msg_bad;
static int g_goto = -1;         /* the box the recipe is stepping to        */
static u32 g_busy_until;        /* a box step or a tap the game is still on */
static u32 g_vkey;              /* a key the player clicked on the screen   */
static u32 g_wheel_at;
static int g_apply;             /* a drop is sent, its answer not read      */
static int g_probe_key[4];      /* the probe answered: a, target, source box, box */
static int g_probe_have, g_probe_ok = 1, g_probe_code;
static int g_probe_asked[4];

static void pc_say(const char *text, int bad, u32 frame)
{
    snprintf(g_msg, sizeof(g_msg), "%s", text);
    g_msg_until = frame + 200;
    g_msg_bad = bad;
}

/* The game's own words for why a drop is refused (message bank 10). */
static const char *rule_text(int code)
{
    switch (code) {
    case 1:
        return pc_text(TXT_PROMPT, 12);     /* the box is full              */
    case 2:
        return pc_text(TXT_PROMPT, 22);     /* Mail                         */
    case 3:
        return pc_text(TXT_PROMPT, 6);      /* the last Pokemon able to battle */
    default:
        return "It cannot go there.";
    }
}

/* Mail stays with its Pokemon, as in the game.  Giratina, Arceus and
 * Genesect change form with their item, which the game's own item code does
 * and a plain write would not, so their items stay too. */
static int item_is_mail(int item)
{
    return item >= 137 && item <= 148;
}

static int item_fixed_mon(const PcMon *m)
{
    return m->species == 487 || m->species == 493 || m->species == 649;
}

/* Why an item cannot leave or reach a Pokemon, or NULL. */
static const char *item_rule(const PcMon *m)
{
    if (!m->exists) {
        return "Only a Pokemon can hold it.";
    }
    if (m->egg) {
        return pc_text(TXT_PROMPT, 31);     /* An Egg can't hold an item.    */
    }
    if (item_is_mail(m->item)) {
        return pc_text(TXT_PROMPT, 43);     /* Mail cannot be moved!         */
    }
    if (item_fixed_mon(m)) {
        return "Its item can't be moved here.";
    }
    return NULL;
}

static char g_item_to[48];      /* who takes the item being given           */

/* The game is idle in its box scene and nothing of ours is under way. */
static int pc_ready(const PcGame *g, const PcWin *w, u32 frame)
{
    return !pc_script_busy() && !g_op.pending && !w->tray && !w->list_shown && !w->yesno &&
           !w->msg && g->hand_mode == 0 && frame >= g_sig_since + 3 &&
           frame >= g_busy_until;
}

static void pc_reset(void)
{
    g_sc_n = g_sc_i = 0;
    g_hand = -1;
    g_focus = 0;
    g_sel = 0;
    g_press = -1;
    g_dragging = 0;
    g_ov = OV_NONE;
    g_msg[0] = '\0';
    g_goto = -1;
    g_busy_until = 0;
    g_vkey = 0;
    g_wheel_at = 0;
    g_apply = 0;
    g_probe_have = 0;
    g_probe_asked[0] = -1;
    g_op.pending = 0;
    g_task_ptr = NULL;
}

static void pc_pick(const PcGame *g, int slot, u32 frame)
{
    PcMon m;
    char line[96];

    if (!pc_mon(g, slot, 0, &m) || !m.exists) {
        return;
    }
    if (g->mode == PC_MODE_ITEMS) {
        const char *why = item_rule(&m);

        if (why == NULL && m.item <= 0) {
            snprintf(line, sizeof(line), "%s holds no item.", m.nick);
            why = line;
        }
        if (why != NULL) {
            pc_say(why, 1, frame);
            return;
        }
    }
    g_hand = slot;
    g_hand_box = g->cur_box;
    g_hand_mon = m;
    g_probe_have = 0;
    g_probe_asked[0] = -1;
    snprintf(line, sizeof(line), "Carrying %s.",
             g->mode == PC_MODE_ITEMS ? pc_text(TXT_ITEM, (u32)m.item) : m.egg ? "Egg" : m.nick);
    pc_say(line, 0, frame);
}

static void pc_putback(void)
{
    g_hand = -1;
    g_dragging = 0;
    g_probe_have = 0;
    g_msg[0] = '\0';
}

/* Put the Pokemon in the hand down on a slot, or into a box (slot < 0). */
static void pc_drop(const PcGame *g, const PcWin *w, int slot, int box, u32 frame)
{
    if (g_hand < 0) {
        return;
    }
    if (g->mode == PC_MODE_ITEMS) {
        PcMon t;
        const char *why;

        if (slot < 0) {                     /* a box: go there, still carrying */
            g_goto = box;
            return;
        }
        if (slot == g_hand && g->cur_box == g_hand_box) {
            pc_putback();
            return;
        }
        if (!pc_mon(g, slot, 0, &t)) {
            return;
        }
        why = item_rule(&t);
        if (why != NULL) {
            pc_say(why, 1, frame);
            return;
        }
        if (!pc_ready(g, w, frame)) {
            pc_say("Wait a moment.", 1, frame);
            return;
        }
        snprintf(g_item_to, sizeof(g_item_to), "%s", t.nick);
        if (pc_op_start(OP_ITEM, g_hand, slot, g_hand_box, -1, 0)) {
            g_apply = 1;
        }
        return;
    }
    if ((slot >= 0 && slot == g_hand && g->cur_box == g_hand_box) ||
        (slot < 0 && box == g->cur_box && g_hand < SLOT_PARTY0 && box == g_hand_box)) {
        pc_putback();
        return;
    }
    if (!pc_ready(g, w, frame)) {
        pc_say("Wait a moment.", 1, frame);
        return;
    }
    if (pc_op_start(OP_APPLY, g_hand, slot >= 0 ? slot : 0, g_hand_box, slot >= 0 ? -1 : box, 0)) {
        g_apply = 1;
    }
}

/* The answer of the game thread to a drop or a probe, read once. */
static void pc_poll_op(const PcGame *g, u32 frame)
{
    if (!g_op.done || g_op.pending) {
        return;
    }
    g_op.done = 0;
    if (g_op.kind == OP_ITEM && g_apply) {
        char line[96];

        g_apply = 0;
        if (g_op.ok) {
            snprintf(line, sizeof(line), "%s now holds %s.", g_item_to,
                     pc_text(TXT_ITEM, (u32)g_hand_mon.item));
            pc_say(line, 0, frame);
            g_hand = -1;
        } else {
            pc_say("The item could not be moved.", 1, frame);
        }
    } else if (g_op.kind == OP_APPLY && g_apply) {
        g_apply = 0;
        if (g_op.ok) {
            char line[96];

            snprintf(line, sizeof(line), "%s is stored.", g_hand_mon.egg ? "Egg" : g_hand_mon.nick);
            if (g_op.tobox < 0 && g_op.b >= SLOT_PARTY0) {
                snprintf(line, sizeof(line), "%s joins the party.",
                         g_hand_mon.egg ? "Egg" : g_hand_mon.nick);
            }
            pc_say(line, 0, frame);
            g_hand = -1;
            g_probe_have = 0;
        } else {
            pc_say(rule_text(g_op.code), 1, frame);
        }
    } else if (g_op.kind == OP_PROBE) {
        memcpy(g_probe_key, g_probe_asked, sizeof(g_probe_key));
        g_probe_ok = g_op.ok;
        g_probe_code = g_op.code;
        g_probe_have = 1;
    }
    (void)g;
}

/* Ask the game whether the Pokemon in the hand may go to the target. */
static void pc_probe(const PcGame *g, int slot, int box)
{
    int key[4];

    if (g_hand < 0 || g_op.pending || g_apply || g->mode == PC_MODE_ITEMS) {
        return;                             /* items: the rules are the recipe's */
    }
    key[0] = g_hand;
    key[1] = slot >= 0 ? slot : 100 + box;
    key[2] = g_hand_box;
    key[3] = g->cur_box;
    if (memcmp(key, g_probe_asked, sizeof(key)) == 0) {
        return;
    }
    if (slot >= 0 && slot == g_hand && g->cur_box == g_hand_box) {
        return;
    }
    memcpy(g_probe_asked, key, sizeof(key));
    (void)pc_op_start(OP_PROBE, g_hand, slot >= 0 ? slot : 0, g_hand_box, slot >= 0 ? -1 : box, 0);
}

static void pc_box_step(const PcGame *g, const PcWin *w, int dir, u32 frame)
{
    if (!pc_ready(g, w, frame)) {
        return;
    }
    eidos_ui_tap(dir < 0 ? BOX_ARROW_L : BOX_ARROW_R, BOX_ARROW_Y, 6);
    g_busy_until = frame + 14;
}

static void pc_goto_box(int box)
{
    g_goto = box;
}

/* The actions a mode has: the Battle Box keeps no items and releases none;
 * the item mode's own list is the game's, which the scripts do not know, so
 * only the marks (the recipe's own) stay. */
static int act_allowed(const PcGame *g, int act)
{
    if (g->mode == PC_MODE_ITEMS) {
        return act == ACT_MARK;
    }
    return !(g->mode == 4 && (act == ACT_ITEM || act == ACT_RELEASE));
}

/* An action on a Pokemon the game does: its summary, its held item, its release. */
static void pc_act(const PcGame *g, const PcWin *w, int act, int slot, u32 frame)
{
    PcMon m;

    if (slot < 0 || !pc_mon(g, slot, 0, &m) || !m.exists) {
        return;
    }
    if (act == ACT_MARK) {
        g_ov = OV_MARK;
        g_ov_slot = slot;
        g_ov_cur = 0;
        return;
    }
    if (!pc_ready(g, w, frame)) {
        return;
    }
    if (!act_allowed(g, act)) {
        return;
    }
    pc_script_action(slot, act == ACT_SUMMARY ? 1 : act == ACT_ITEM ? 2 : 4);
    g_sc_start = frame;
    g_back = act == ACT_RELEASE ? 0 : 1;
    g_back_at = frame;
}

static int act_enabled(const PcGame *g, int act, int slot)
{
    PcMon m;

    if (slot < 0 || !pc_mon(g, slot, 0, &m) || !m.exists) {
        return 0;
    }
    return act_allowed(g, act);
}

/* ------------------------------------------------------------------ */
/* The mouse                                                            */
/* ------------------------------------------------------------------ */

typedef struct PcMouse {
    int x, y;               /* canvas pixels, -1 outside                  */
    int down, pressed;
    int up;                 /* the button went up (a click or a drop)     */
    int right;
    int wheel;
} PcMouse;

/*
 * EIDOS_PC_DRAG="frame:x1:y1:x2:y2:frames[,...]" (window pixels) scripts drags
 * for checks: the button goes down at x1,y1 on `frame`, moves in a line to
 * x2,y2 and goes up `frames` later.  The real mouse (or EIDOS_UI_MOUSE) is
 * used otherwise.
 */
static void pc_get_mouse(const EidosUiFrame *in, int k, int cw, int ch, PcMouse *m)
{
    static int loaded, have;
    static unsigned long d[8][6];
    int i, mx = -1, my = -1;
    EidosUiRect all;

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
            m->up = in->frame == sf + sd;
            return;
        }
    }
    (void)eidos_ui_mouse_pos(&mx, &my);
    m->x = mx;
    m->y = my;
    all.x = 0;
    all.y = 0;
    all.w = cw;
    all.h = ch;
    m->down = in->mouse.down;
    m->pressed = in->mouse.pressed;
    m->up = in->mouse.released || (eidos_ui_hit(all) & EIDOS_UI_CLICK) != 0;
    m->right = eidos_ui_right_click();
    m->wheel = in->mouse.wheel;
}

/* ------------------------------------------------------------------ */
/* Where things are drawn                                               */
/* ------------------------------------------------------------------ */

typedef struct PcBoxHead { int hx, hw, hy; } PcBoxHead;

static void head_geom(const PcLay *l, PcBoxHead *h)
{
    h->hx = l->bx + 6;
    h->hw = l->bw - 12;
    h->hy = l->head_y;
}

static int chip_geom(const PcLay *l, int i, int *x, int *y, int *w)
{
    int cwid = (l->dw - 3) / 2;

    *x = l->dx + (i % 2) * (cwid + 3);
    *y = l->chip_y + (i / 2) * 16;
    *w = cwid;
    return 14;
}

/* Where the action list and the two-choice menu are drawn (the box panel's
 * bottom right, above the message bar; the box panel's middle). */
static void list_geom(const PcLay *l, int n, int *lx, int *ly, int *lw, int *bh)
{
    *lw = 78;
    *bh = 18;
    *lx = l->bx + l->bw - *lw - 6;
    *ly = l->by + l->bh - 8 - n * (*bh + 2);
    if (*ly + n * (*bh + 2) > l->msg_y - 4) {
        *ly = l->msg_y - 4 - n * (*bh + 2);
    }
}

static void yesno_geom(const PcLay *l, int *bx, int *by0, int *bw, int *bh)
{
    *bw = 60;
    *bh = 20;
    *bx = l->bx + (l->bw - *bw) / 2;
    *by0 = l->by + l->bh / 2 - 24;
}

/* The box list overlay: tiles of the boxes. */
static void boxes_geom(const PcLay *l, int limit, int *x, int *y, int *w, int *h, int *cols,
                       int *mw, int *mh)
{
    int rows;

    *cols = 6;
    rows = (limit + *cols - 1) / *cols;
    *w = l->cw - 60 > 340 ? 340 : l->cw - 60;
    *x = (l->cw - *w) / 2;
    *y = 34;
    *h = l->ch - 70;
    *mw = (*w - 16 - (*cols - 1) * 4) / *cols;
    *mh = (*h - 34 - (rows - 1) * 4) / (rows > 0 ? rows : 1);
    if (*mh > 40) {
        *mh = 40;
    }
}

static void menu_geom(const PcLay *l, int slot, int *x, int *y, int *w, int *bh)
{
    *w = 86;
    *bh = 15;
    if (slot >= SLOT_PARTY0) {
        *x = l->px + l->pw - 20;
        *y = pc_row_y(l, slot - SLOT_PARTY0) - 4;
    } else {
        *x = l->gx + (slot % 6) * l->pitch + l->pitch - 6;
        *y = l->gy + (slot / 6) * l->pitch;
    }
    if (*x + *w + 6 > l->cw) {
        *x = l->cw - *w - 6;
    }
    if (*y + 5 * (*bh + 1) + 6 > l->hint_y) {
        *y = l->hint_y - 5 * (*bh + 1) - 6;
    }
}

/* ------------------------------------------------------------------ */
/* Focus with the keys                                                  */
/* ------------------------------------------------------------------ */

/* One step of the focus along the layout: the party column, the grid, the
 * box header above it, the four chips under the details. */
static void pc_nav(const PcGame *g, int key)
{
    int f = g_focus;

    if (f >= 0 && f < SLOT_PARTY0) {
        int r = f / 6, c = f % 6;

        if (key == BW_KEY_UP) {
            f = r > 0 ? f - 6 : (c < 2 ? FOC_BOX_PREV : c < 4 ? FOC_BOX_NAME : FOC_BOX_NEXT);
        } else if (key == BW_KEY_DOWN && r < 4) {
            f += 6;
        } else if (key == BW_KEY_LEFT) {
            f = c > 0 ? f - 1 : SLOT_PARTY0 + r;
        } else if (key == BW_KEY_RIGHT) {
            f = c < 5 ? f + 1 : FOC_CHIP0 + (r < 2 ? 0 : 2);
        }
    } else if (f >= SLOT_PARTY0 && f < SLOT_COUNT) {
        int p = f - SLOT_PARTY0;

        if (key == BW_KEY_UP && p > 0) {
            f--;
        } else if (key == BW_KEY_DOWN && p < 5) {
            f++;
        } else if (key == BW_KEY_RIGHT) {
            f = (p < 5 ? p : 4) * 6;
        }
    } else if (f >= FOC_BOX_PREV && f <= FOC_BOX_NEXT) {
        if (key == BW_KEY_LEFT && f > FOC_BOX_PREV) {
            f--;
        } else if (key == BW_KEY_RIGHT && f < FOC_BOX_NEXT) {
            f++;
        } else if (key == BW_KEY_DOWN) {
            f = f == FOC_BOX_PREV ? 0 : f == FOC_BOX_NAME ? 2 : 5;
        }
    } else if (f >= FOC_CHIP0 && f < FOC_CHIP0 + ACT_COUNT) {
        int ci = f - FOC_CHIP0;

        if (key == BW_KEY_LEFT) {
            f = ci % 2 == 1 ? f - 1 : (ci < 2 ? 5 : 17);
        } else if (key == BW_KEY_RIGHT && ci % 2 == 0) {
            f++;
        } else if (key == BW_KEY_DOWN && ci < 2) {
            f += 2;
        } else if (key == BW_KEY_UP && ci >= 2) {
            f -= 2;
        }
    }
    g_focus = f;
    (void)g;
}

/* The slot a Pokemon-related action acts on: the focus when it is a slot. */
static int focus_slot(void)
{
    return g_focus >= 0 && g_focus < SLOT_COUNT ? g_focus : -1;
}

/* ------------------------------------------------------------------ */
/* The overlays                                                         */
/* ------------------------------------------------------------------ */

static void mini_box(EidosUiCtx *c, int x, int y, int w, int h, const char *name, int n, int cur,
                     int target, int hover)
{
    char cnt[12];
    int i, rows = h >= 28 ? 3 : 0;

    ui_panel(c, x, y, w, h, UI_CUT_TL | UI_CUT_BR, 3,
             target ? UI_CYAN2 : (cur || hover ? UI_NAVY2 : UI_BODY2),
             target ? UI_CYAN : (cur ? UI_SKY : UI_EDGE), 255);
    {
        char nm[48];
        int k;

        snprintf(nm, sizeof(nm), "%s", name);
        k = (int)strlen(nm);
        while (k > 1 && ui_text_w(nm, UI_FONT_SMALL) > w - 22) {
            nm[--k] = '\0';
        }
        ui_text(c, x + 4, y + 2, nm, UI_WHITE, UI_EDGE, UI_FONT_SMALL, 1);
    }
    fmt_int(cnt, "", n);
    ui_text(c, x + w - 4 - ui_text_w(cnt, UI_FONT_SMALL), y + 2, cnt, n >= 30 ? UI_HP_Y : UI_LIGHT,
            UI_EDGE, UI_FONT_SMALL, 1);
    for (i = 0; rows != 0 && i < 30; i++) {
        int dx = x + 5 + (i % 6) * ((w - 10) / 6), dy = y + 14 + (i / 6) * 4;

        ui_fill(c, dx, dy, (w - 10) / 6 - 1, 3, i < n ? (target ? UI_WHITE : UI_CYAN) : 0x0B3A62u,
                255);
    }
}

static const char *const k_menu_label[5] = { "SUMMARY", "HELD ITEMS", "MARKING", "RELEASE", "CANCEL" };

/* ------------------------------------------------------------------ */
/* The footer: the hints are buttons                                    */
/* ------------------------------------------------------------------ */

/* Like the kit's hints bar, but a click is the key as the player's own
 * press (g_vkey), so the recipe handles it the way it handles a key. */
static void pc_hints(EidosUiCtx *c, int y, int w, const char *const *pairs, const u32 *keys, int n,
                     const PcMouse *m)
{
    int i, x = 6;

    ui_fill(c, 0, y, w, 17, UI_BODY2, 235);
    ui_fill(c, 0, y, w, 1, UI_EDGE, 255);
    for (i = 0; i < n; i++) {
        const char *key = pairs[2 * i], *what = pairs[2 * i + 1];
        int kw = ui_keycap_w(key), aw = ui_text_w(what, UI_FONT_SMALL), rw;

        if (kw < 13) {
            kw = 13;
        }
        rw = kw + 4 + aw + 4;
        if (m->x >= 0 && in_rect(m->x, m->y, x - 2, y, rw, 17)) {
            ui_fill(c, x - 2, y + 1, rw, 15, UI_EDGE, 255);
            if (m->up && keys[i] != 0) {
                g_vkey |= keys[i];
            }
        }
        ui_keycap(c, x, y + 2, key);
        x += kw + 4;
        x = ui_text(c, x, y + 2 + ui_text_y(13, UI_FONT_SMALL), what, UI_LIGHT, UI_EDGE,
                    UI_FONT_SMALL, 1) + 10;
    }
}

/* ------------------------------------------------------------------ */
/* The recipe                                                           */
/* ------------------------------------------------------------------ */

static int pc_match(void)
{
    if (!pc_read_game(&g_g)) {
        g_task_ptr = NULL;
        return 0;
    }
    g_task_ptr = g_g.task;
    pc_debug(&g_g, bw_input_frame_number());
    return !pc_unsupported(&g_g);
}

static int box_count(const PcGame *g, int box)
{
    return BOX_SAVEDATA_CountPokemonInBox != NULL ? BOX_SAVEDATA_CountPokemonInBox(g->gd, (u32)box) : 0;
}

static void box_title(const PcGame *g, int box, char *out, int cap)
{
    pc_box_name(g, box, out, cap);
    if (out[0] == '\0') {
        snprintf(out, (size_t)cap, "%s", pc_text(TXT_BOX, (u32)(6 + box)));
    }
}

/* The line under the grid. */
static const char *carry_line(const PcGame *g, int target, char *buf, int cap, int *bad)
{
    PcMon t;
    const char *me = g_hand_mon.egg ? "Egg" : g_hand_mon.nick;

    *bad = 0;
    if (g->mode == PC_MODE_ITEMS) {
        const char *it = pc_text(TXT_ITEM, (u32)g_hand_mon.item), *why;

        if (target < 0 || (target == g_hand && g->cur_box == g_hand_box) || !pc_mon(g, target, 0, &t)) {
            snprintf(buf, (size_t)cap, "Carrying %s.", it);
            return buf;
        }
        why = item_rule(&t);
        if (why != NULL) {
            *bad = 1;
            return why;
        }
        if (t.item > 0) {
            snprintf(buf, (size_t)cap, "%s will swap with %s's %s.", it, t.nick,
                     pc_text(TXT_ITEM, (u32)t.item));
        } else {
            snprintf(buf, (size_t)cap, "%s will hold %s.", t.nick, it);
        }
        return buf;
    }
    if (target >= 0 && g_probe_have && g_probe_key[0] == g_hand && g_probe_key[1] == target &&
        g_probe_key[3] == g->cur_box && !g_probe_ok) {
        *bad = 1;
        return rule_text(g_probe_code);
    }
    if (target >= 0 && pc_mon(g, target, 0, &t) && t.exists &&
        !(target == g_hand && g->cur_box == g_hand_box)) {
        snprintf(buf, (size_t)cap, "%s will swap with %s.", me, t.egg ? "Egg" : t.nick);
        return buf;
    }
    if (target >= 0 && !(target == g_hand && g->cur_box == g_hand_box)) {
        snprintf(buf, (size_t)cap, "%s goes there.", me);
        return buf;
    }
    snprintf(buf, (size_t)cap, "Carrying %s.", me);
    return buf;
}

static void pc_run(EidosUiCtx *c, EidosUiView *view, const EidosUiFrame *in)
{
    PcLay l;
    const PcGame *g = &g_g;
    PcMon mon[SLOT_COUNT], det;
    PcMouse mouse;
    PcWin *w = &g_w;
    PcBoxHead hd;
    int cw = c->canvas->w, ch = c->canvas->h, k = c->canvas->k;
    int i, hover = -1, hover_pip = -1, popup, limit, frame = (int)in->frame, mouse_in;
    int over_prev = 0, over_next = 0, over_name = 0, hover_chip = -1;
    u32 keys, vk;
    char boxname[48], line[128];

    view->mode = EIDOS_VIEW_WORLD;
    view->world_screen = 0;
    view->zoom = 1.0f;
    view->blur = 1;
    view->hold_world = 1;     /* an app: the field is not running */
    view->hide_2d = 1;
    g_bottom = in->screen_rgba[1];
    pc_windows(g, w);
    pc_script_run(g, w, in->frame);
    pc_poll_op(g, in->frame);
    pc_layout(&l, cw, ch);
    pc_get_mouse(in, k, cw, ch, &mouse);
    head_geom(&l, &hd);
    limit = g->box_limit > 0 ? g->box_limit : 24;
    popup = w->list_shown || w->yesno || w->msg;
    mouse_in = mouse.x >= 0 && mouse.y >= 0;

    /* the keys: the recipe's own while nothing of the game's is open */
    vk = g_vkey;
    g_vkey = 0;
    keys = eidos_ui_keys_pressed() | vk;
    if (popup) {
        if (vk != 0) {
            eidos_ui_press(vk & BW_KEY_GAME, 3);
        }
        keys = 0;
    } else {
        eidos_ui_take_keys(BW_KEY_GAME);
        if (pc_script_busy()) {
            keys = 0;
        }
    }
    if (!popup && !pc_script_busy() && w->tray && g_hand < 0 && in->frame >= g_sig_since + 20) {
        pc_script_close_tray();
        g_sc_start = in->frame;
    }

    /* what is under the mouse */
    if (mouse_in && !popup && g_ov == OV_NONE) {
        hover = pc_slot_at(&l, mouse.x, mouse.y);
        hover_pip = pc_pip_at(&l, limit, mouse.x, mouse.y);
        over_prev = in_rect(mouse.x, mouse.y, hd.hx, hd.hy, 16, 18);
        over_next = in_rect(mouse.x, mouse.y, hd.hx + hd.hw - 16, hd.hy, 16, 18);
        over_name = in_rect(mouse.x, mouse.y, hd.hx + 19, hd.hy, hd.hw - 38, 18);
        for (i = 0; i < ACT_COUNT; i++) {
            int cx, cy, cwid, chh = chip_geom(&l, i, &cx, &cy, &cwid);

            if (in_rect(mouse.x, mouse.y, cx, cy, cwid, chh)) {
                hover_chip = i;
            }
        }
    }
    if (hover >= 0) {
        g_focus = hover;
        g_sel = hover;
    } else if (focus_slot() >= 0) {
        g_sel = focus_slot();
    }

    /* the mouse */
    if (!popup && g_ov == OV_NONE && !pc_script_busy()) {
        if (mouse.right) {
            if (g_hand >= 0) {
                pc_putback();
            } else {
                keys |= BW_KEY_B;
            }
        }
        if (mouse.wheel != 0 && mouse_in && in_rect(mouse.x, mouse.y, l.bx, l.by, l.bw, l.bh) &&
            in->frame >= g_wheel_at + 24) {
            g_wheel_at = in->frame;
            pc_box_step(g, w, mouse.wheel > 0 ? -1 : 1, in->frame);
        }
        if (mouse.pressed) {
            g_press = -1;
            if (hover >= 0 && g_hand < 0 && pc_mon(g, hover, 0, &det) && det.exists) {
                g_press = hover;
                g_press_x = mouse.x;
                g_press_y = mouse.y;
            }
        }
        if (mouse.down && g_press >= 0 && !g_dragging && g_hand < 0) {
            int dx = mouse.x - g_press_x, dy = mouse.y - g_press_y;

            if (dx * dx + dy * dy >= 16) {
                pc_pick(g, g_press, in->frame);
                g_dragging = g_hand >= 0;
            }
        }
        if (g_hand >= 0 && mouse_in && (over_prev || over_next) && in->frame % 24 == 0) {
            pc_box_step(g, w, over_prev ? -1 : 1, in->frame);
        }
        if (mouse.up) {
            if (g_dragging) {
                g_dragging = 0;
                if (hover >= 0) {
                    pc_drop(g, w, hover, 0, in->frame);
                } else if (hover_pip >= 0) {
                    pc_drop(g, w, -1, hover_pip, in->frame);
                } else if (!over_prev && !over_next) {
                    pc_putback();
                }
            } else if (mouse_in) {
                if (over_prev || over_next) {
                    pc_box_step(g, w, over_prev ? -1 : 1, in->frame);
                } else if (over_name) {
                    g_ov = OV_BOXES;
                    g_ov_cur = g->cur_box;
                } else if (hover_chip >= 0) {
                    if (act_enabled(g, hover_chip, g_sel)) {
                        pc_act(g, w, hover_chip, g_sel, in->frame);
                    }
                } else if (hover_pip >= 0) {
                    if (g_hand >= 0) {
                        pc_drop(g, w, -1, hover_pip, in->frame);
                    } else {
                        pc_goto_box(hover_pip);
                    }
                } else if (hover >= 0) {
                    if (g_hand < 0) {
                        pc_pick(g, hover, in->frame);
                    } else {
                        pc_drop(g, w, hover, 0, in->frame);
                    }
                } else if (g_hand >= 0 && in_rect(mouse.x, mouse.y, l.px, l.y0, cw, l.y1 - l.y0)) {
                    pc_putback();
                }
            }
            g_press = -1;
        }
    }
    if (g_hand < 0) {
        g_dragging = 0;
    }

    /* going to a box the player named: one step at a time */
    if (g_goto >= 0) {
        if (g_goto == g->cur_box) {
            g_goto = -1;
        } else if (pc_ready(g, w, in->frame)) {
            int fwd = (g_goto - g->cur_box + limit) % limit;

            pc_box_step(g, w, fwd <= limit / 2 ? 1 : -1, in->frame);
        }
    }

    /* the keys */
    if (g_ov == OV_BOXES) {
        int step = 0;

        if (keys & BW_KEY_LEFT) { step = -1; }
        if (keys & BW_KEY_RIGHT) { step = 1; }
        if (keys & BW_KEY_UP) { step = -6; }
        if (keys & BW_KEY_DOWN) { step = 6; }
        if (step != 0 && g_ov_cur + step >= 0 && g_ov_cur + step < limit) {
            g_ov_cur += step;
        }
        if (keys & BW_KEY_A) {
            if (g_hand >= 0) {
                pc_drop(g, w, -1, g_ov_cur, in->frame);
            } else {
                pc_goto_box(g_ov_cur);
            }
            g_ov = OV_NONE;
        } else if (keys & (BW_KEY_B | BW_KEY_SELECT)) {
            g_ov = OV_NONE;
        }
    } else if (g_ov == OV_MENU) {
        if ((keys & BW_KEY_UP) && g_ov_cur > 0) { g_ov_cur--; }
        if ((keys & BW_KEY_DOWN) && g_ov_cur < 4) { g_ov_cur++; }
        if (keys & BW_KEY_A) {
            int pick = g_ov_cur;

            g_ov = OV_NONE;
            if (pick < 4 && act_enabled(g, pick, g_ov_slot)) {
                pc_act(g, w, pick == 0 ? ACT_SUMMARY : pick == 1 ? ACT_ITEM : pick == 2 ? ACT_MARK
                       : ACT_RELEASE, g_ov_slot, in->frame);
            }
        } else if (keys & (BW_KEY_B | BW_KEY_X)) {
            g_ov = OV_NONE;
        }
    } else if (g_ov == OV_MARK) {
        if ((keys & BW_KEY_LEFT) && g_ov_cur > 0) { g_ov_cur--; }
        if ((keys & BW_KEY_RIGHT) && g_ov_cur < 5) { g_ov_cur++; }
        if (keys & BW_KEY_A) {
            PcMon m;

            if (pc_mon(g, g_ov_slot, 0, &m) && m.exists && !g_op.pending) {
                (void)pc_op_start(OP_MARK, g_ov_slot, 0, g->cur_box, -1, m.mark ^ (1 << g_ov_cur));
            }
        } else if (keys & (BW_KEY_B | BW_KEY_X)) {
            g_ov = OV_NONE;
        }
    } else if (keys != 0) {
        int dir = (keys & BW_KEY_UP) ? BW_KEY_UP : (keys & BW_KEY_DOWN) ? BW_KEY_DOWN
                : (keys & BW_KEY_LEFT) ? BW_KEY_LEFT : (keys & BW_KEY_RIGHT) ? BW_KEY_RIGHT : 0;

        if (g_focus < 0) {
            g_focus = 0;
        }
        if (keys & BW_KEY_L) { pc_box_step(g, w, -1, in->frame); }
        if (keys & BW_KEY_R) { pc_box_step(g, w, 1, in->frame); }
        if (keys & BW_KEY_SELECT) {
            g_ov = OV_BOXES;
            g_ov_cur = g->cur_box;
        }
        if ((keys & BW_KEY_X) && focus_slot() >= 0 && act_enabled(g, ACT_MARK, focus_slot())) {
            g_ov = OV_MENU;
            g_ov_slot = focus_slot();
            g_ov_cur = 0;
        }
        if ((keys & BW_KEY_Y) && focus_slot() >= 0) {
            pc_act(g, w, ACT_SUMMARY, focus_slot(), in->frame);
        }
        if (dir != 0) {
            pc_nav(g, dir);
        }
        if (keys & BW_KEY_A) {
            if (focus_slot() >= 0) {
                if (g_hand < 0) {
                    pc_pick(g, focus_slot(), in->frame);
                } else {
                    pc_drop(g, w, focus_slot(), 0, in->frame);
                }
            } else if (g_focus == FOC_BOX_PREV || g_focus == FOC_BOX_NEXT) {
                pc_box_step(g, w, g_focus == FOC_BOX_PREV ? -1 : 1, in->frame);
            } else if (g_focus == FOC_BOX_NAME) {
                g_ov = OV_BOXES;
                g_ov_cur = g->cur_box;
            } else if (g_focus >= FOC_CHIP0 && g_focus < FOC_CHIP0 + ACT_COUNT &&
                       act_enabled(g, g_focus - FOC_CHIP0, g_sel)) {
                pc_act(g, w, g_focus - FOC_CHIP0, g_sel, in->frame);
            }
        }
        if (keys & BW_KEY_B) {
            if (g_hand >= 0) {
                pc_putback();
            } else {
                eidos_ui_press(BW_KEY_B, 3);        /* the game closes the PC */
            }
        }
    }

    /* the rules for the target of a carried Pokemon */
    if (g_hand >= 0 && !popup) {
        int tgt = hover >= 0 ? hover : (!mouse_in || eidos_ui_keys_lit()) ? focus_slot() : -1;

        if (g_ov == OV_BOXES) {
            pc_probe(g, -1, g_ov_cur);
        } else if (hover_pip >= 0) {
            pc_probe(g, -1, hover_pip);
        } else if (tgt >= 0) {
            pc_probe(g, tgt, 0);
        }
    }

    /* ------------------------------ drawing ------------------------------ */
    box_title(g, g->cur_box, boxname, (int)sizeof(boxname));
    /* the box's own bar names it and counts its Pokemon, and the strip
     * under the grid shows every box: no box counter in the header */
    ui_header(c, 6, 5, cw - 12, "POKEMON STORAGE", mode_title(g->mode), NULL, UI_SKY);

    {
        int carry_from_box = g_hand >= 0 && g_hand < SLOT_PARTY0 && g->cur_box == g_hand_box;
        int tgt = hover >= 0 ? hover : (!mouse_in || eidos_ui_keys_lit()) ? focus_slot() : -1;
        int ds = hover >= 0 ? hover : (g_focus >= 0 && g_focus < SLOT_COUNT ? g_focus : g_sel);
        int keys_on = eidos_ui_keys_lit() && g_ov == OV_NONE && !popup;
        int probe_bad = g_probe_have && !g_probe_ok;
        int items = g->mode == PC_MODE_ITEMS;

        /* the party */
        for (i = 0; i < 6; i++) {
            int slot = SLOT_PARTY0 + i, y = pc_row_y(&l, i), is_t = g_hand >= 0 && tgt == slot;

            (void)pc_mon(g, slot, 0, &mon[slot]);
            party_chip(c, l.px, y, l.pw, l.row_h, i, &mon[slot], keys_on && g_focus == slot,
                       hover == slot, g_hand == slot && !items, frame);
            if (mon[slot].exists && mon[slot].item > 0 && g_hand != slot) {
                item_dot(c, l.px + 26, y + l.row_h / 2 + 4);
            }
            if (is_t) {
                ui_cursor_corners(c, l.px - 1, y - 1, l.pw + 2, l.row_h + 2,
                                  probe_bad ? UI_HP_R : UI_LIME);
            }
        }

        /* the box */
        ui_panel(c, l.bx, l.by, l.bw, l.bh, UI_CUT_TL | UI_CUT_BR, 6, UI_BODY, UI_SKY, 225);
        {
            char cnt[24];

            ui_panel(c, hd.hx, hd.hy, 16, 18, UI_CUT_TL | UI_CUT_BR, 3,
                     over_prev || (keys_on && g_focus == FOC_BOX_PREV) ? UI_NAVY2 : UI_NAVY, UI_SKY, 255);
            draw_chevron(c, hd.hx + 5, hd.hy + 4, -1);
            ui_panel(c, hd.hx + hd.hw - 16, hd.hy, 16, 18, UI_CUT_TL | UI_CUT_BR, 3,
                     over_next || (keys_on && g_focus == FOC_BOX_NEXT) ? UI_NAVY2 : UI_NAVY, UI_SKY, 255);
            draw_chevron(c, hd.hx + hd.hw - 16 + 5, hd.hy + 4, 1);
            ui_panel(c, hd.hx + 19, hd.hy, hd.hw - 38, 18, UI_CUT_TL | UI_CUT_BR, 3,
                     over_name || (keys_on && g_focus == FOC_BOX_NAME) ? UI_WHITE : UI_LIGHT, UI_GREY, 255);
            ui_text(c, hd.hx + 24, hd.hy + ui_text_y(18, UI_FONT_LARGE), boxname, UI_INK, -1,
                    UI_FONT_LARGE, 1);
            fmt_int(cnt, "", box_count(g, g->cur_box));
            strcat(cnt, "/30");
            ui_text(c, hd.hx + hd.hw - 19 - 5 - ui_text_w(cnt, UI_FONT_SMALL),
                    hd.hy + ui_text_y(18, UI_FONT_SMALL), cnt, 0x404040u, -1, UI_FONT_SMALL, 1);
            if (keys_on && g_focus >= FOC_BOX_PREV && g_focus <= FOC_BOX_NEXT) {
                int fx = g_focus == FOC_BOX_PREV ? hd.hx : g_focus == FOC_BOX_NEXT ? hd.hx + hd.hw - 16
                                                                                    : hd.hx + 19;
                int fw = g_focus == FOC_BOX_NAME ? hd.hw - 38 : 16;

                ui_cursor_corners(c, fx - 1, hd.hy - 1, fw + 2, 20, UI_CYAN);
            }
        }
        for (i = 0; i < 30; i++) {
            int row = i / 6, col = i % 6, cx = l.gx + col * l.pitch, cy = l.gy + row * l.pitch;
            int p = l.pitch;
            const UiImage *ic;
            int hide = carry_from_box && g_hand == i, hide_mon = hide && !items;

            (void)pc_mon(g, i, 0, &mon[i]);
            draw_cell(c, cx, cy, p, hover == i, 0);
            if (!mon[i].exists) {
                ui_ellipse(c, cx + p / 2 - 5, cy + p / 2 - 5, 9, 9, UI_NONE, 0x0B3A62u, 255);
            }
            ic = icon_of(&mon[i], (frame / 24) & 1);
            if (ic != NULL && !hide_mon) {
                ui_image(c, cx + (p - 2 - ic->w) / 2, cy + (p - 2 - ic->h) / 2 - 1, ic, 1);
            } else if (mon[i].egg && !hide_mon) {
                ui_text(c, cx + 3, cy + p / 2 - 6, "Egg", UI_WHITE, UI_EDGE, UI_FONT_SMALL, 1);
            }
            if (mon[i].exists && mon[i].item > 0 && !hide) {
                item_dot(c, cx + p - 10, cy + p - 10);
            }
            if (g_hand >= 0 && tgt == i) {
                ui_cursor_corners(c, cx - 1, cy - 1, p, p, probe_bad ? UI_HP_R : UI_LIME);
            } else if (keys_on && g_focus == i) {
                ui_cursor_corners(c, cx - 1, cy - 1, p, p, UI_CYAN);
            }
        }
        if (l.show_strip) {
            int pw = (6 * l.pitch) / 12;

            /* every box, as a gauge of how full it is (yellow when full);
             * the box on show is outlined */
            for (i = 0; i < limit && i < 24; i++) {
                int x = l.gx + (i % 12) * pw, yy = l.strip_y + (i / 12) * 9, n = box_count(g, i);
                int fw = n > 0 ? 1 + (pw - 4) * (n > 30 ? 30 : n) / 30 : 0;

                ui_fill(c, x, yy, pw - 1, 7, i == g->cur_box ? 0x0E3A5Cu : 0x08243Eu, 255);
                if (fw > 0) {
                    ui_fill(c, x + 1, yy + 1, fw, 5, n >= 30 ? UI_HP_Y : UI_CYAN2, 255);
                }
                if (i == g->cur_box) {
                    rect_outline(c, x - 1, yy - 1, pw + 1, 9, UI_WHITE, 255);
                }
                if (i == hover_pip && g_hand >= 0) {
                    rect_outline(c, x - 1, yy - 1, pw + 1, 9, UI_LIME, 255);
                }
            }
        }
        /* the message line: the game's own message, or the recipe's */
        if (w->msg && g_bottom != NULL && w->msg_w > 8 && !pc_script_busy()) {
            msg_game(c, &l, w);
        } else if (g_hand >= 0) {
            int bad = 0;
            const char *t = carry_line(g, tgt, line, (int)sizeof(line), &bad);

            msg_line(c, l.bx + 6, l.msg_y, l.bw - 12, t, bad ? UI_HP_R : UI_WHITE);
        } else if (g_msg[0] != '\0' && in->frame < g_msg_until) {
            msg_line(c, l.bx + 6, l.msg_y, l.bw - 12, g_msg, g_msg_bad ? UI_HP_R : UI_WHITE);
        } else {
            msg_line(c, l.bx + 6, l.msg_y, l.bw - 12, "", UI_WHITE);   /* the line stays */
        }

        /* the details of the Pokemon under the mouse or the focus */
        if (ds >= 0 && ds < SLOT_COUNT) {
            (void)pc_mon(g, ds, 1, &det);
        } else {
            memset(&det, 0, sizeof(det));
        }
        details_panel(c, l.dx, l.y0, l.dw, l.dh, &det, 0, frame);
        for (i = 0; i < ACT_COUNT; i++) {
            static const char *const lab[ACT_COUNT] = { "SUMMARY", "ITEM", "MARK", "RELEASE" };
            int cx, cy, cwid, chh = chip_geom(&l, i, &cx, &cy, &cwid);
            int en = act_enabled(g, i, g_sel);

            chip(c, cx, cy, cwid, chh, lab[i], hover_chip == i && en, en);
            if (keys_on && g_focus == FOC_CHIP0 + i) {
                ui_cursor_corners(c, cx - 1, cy - 1, cwid + 2, chh + 2, UI_CYAN);
            }
        }

        /* the Pokemon in the hand follows the mouse, or sits on the target */
        if (g_hand >= 0) {
            int hx = mouse.x, hy = mouse.y;

            if (!g_dragging || !mouse_in) {
                if (tgt >= 0 && tgt < SLOT_PARTY0) {
                    hx = l.gx + (tgt % 6) * l.pitch + l.pitch / 2;
                    hy = l.gy + (tgt / 6) * l.pitch + l.pitch / 2 - 2;
                } else if (tgt >= SLOT_PARTY0) {
                    hx = l.px + l.pw - 22;
                    hy = pc_row_y(&l, tgt - SLOT_PARTY0) + l.row_h / 2;
                } else {
                    hx = l.gx + 3 * l.pitch;
                    hy = l.gy + 2 * l.pitch;
                }
            }
            ghost(c, hx, hy, &g_hand_mon, frame);
        }
    }

    /* the game's own windows: its action list and its two-choice menu */
    /* (not while a script walks it: the summary or the item screen opens
     * straight from the button, without the list flashing up) */
    if (w->list_shown && w->list_n > 0 && !pc_script_busy()) {
        int lw, bh, lx, ly, n = w->list_n;

        list_geom(&l, n, &lx, &ly, &lw, &bh);
        ui_panel(c, lx - 4, ly - 4, lw + 8, n * (bh + 2) + 6, UI_CUT_TL | UI_CUT_BR, 5, UI_BODY, UI_SKY, 245);
        for (i = 0; i < n; i++) {
            int by = ly + i * (bh + 2), hov = mouse_in && in_rect(mouse.x, mouse.y, lx, by, lw, bh);
            const char *label = w->list_ids[i] > 0 ? pc_text(TXT_BOX, (u32)w->list_ids[i]) : "";

            ui_row(c, lx, by, lw, bh, i == w->list_cur, hov, &UI_THEME_PARTY);
            ui_text(c, lx + 8, by + ui_text_y(bh, UI_FONT_LARGE), label, UI_WHITE, UI_EDGE, UI_FONT_LARGE, 1);
            if (hov && mouse.up && !pc_script_busy()) {
                eidos_ui_tap(212, 148 - 24 * (n - 1 - i), 6);
            }
        }
    }
    if (w->msg && !w->list_shown && !w->yesno && mouse.up && mouse_in && !pc_script_busy()) {
        /* a plain message of the game ("X was released."): a click goes on */
        eidos_ui_press(BW_KEY_A, 6);
    }
    if (w->yesno) {
        static const char *const lab[2] = { "YES", "NO" };
        int bw, bh, bx, by0;

        yesno_geom(&l, &bx, &by0, &bw, &bh);
        ui_panel(c, bx - 6, by0 - 6, bw + 12, 2 * (bh + 4) + 8, UI_CUT_TL | UI_CUT_BR, 5, UI_BODY, UI_SKY, 245);
        for (i = 0; i < 2; i++) {
            int by = by0 + i * (bh + 4), hov = mouse_in && in_rect(mouse.x, mouse.y, bx, by, bw, bh);

            /* the game's cursor is the lighter blue button */
            ui_row(c, bx, by, bw, bh, px_near(YESNO_X, i == 0 ? YES_Y : NO_Y, 8, 90, 139, 20), hov,
                   &UI_THEME_PARTY);
            ui_text(c, bx + (bw - ui_text_w(lab[i], UI_FONT_LARGE)) / 2, by + ui_text_y(bh, UI_FONT_LARGE),
                    lab[i], UI_WHITE, UI_EDGE, UI_FONT_LARGE, 1);
            if (hov && mouse.up) {
                eidos_ui_tap(YESNO_X, i == 0 ? YES_Y : NO_Y, 6);
            }
        }
    }

    /* the overlays */
    if (g_ov == OV_BOXES) {
        int x, y, bw, bh, cols, mw, mh, rows = (limit + 5) / 6;

        boxes_geom(&l, limit, &x, &y, &bw, &bh, &cols, &mw, &mh);
        ui_fill(c, 0, 0, cw, ch - 17, 0x000000u, 150);
        ui_panel(c, x, y, bw, bh, UI_CUT_TL | UI_CUT_BR, 8, UI_BODY, UI_SKY, 250);
        ui_text(c, x + 8, y + 5, g_hand >= 0 && g->mode != PC_MODE_ITEMS ? "Place it in which Box?"
                                                                       : "Jump to which Box?",
                UI_WHITE, UI_EDGE, UI_FONT_LARGE, 1);
        for (i = 0; i < limit; i++) {
            int tx = x + 8 + (i % cols) * (mw + 4), ty = y + 26 + (i / cols) * (mh + 4);
            int hov = mouse_in && in_rect(mouse.x, mouse.y, tx, ty, mw, mh);
            char nm[48];

            box_title(g, i, nm, (int)sizeof(nm));
            if (hov && mouse.pressed == 0 && mouse.up) {
                if (g_hand >= 0) {
                    pc_drop(g, w, -1, i, in->frame);
                } else {
                    pc_goto_box(i);
                }
                g_ov = OV_NONE;
            }
            mini_box(c, tx, ty, mw, mh, nm, box_count(g, i), i == g->cur_box,
                     (g_hand >= 0 || 1) && (i == g_ov_cur), hov);
            if (hov) {
                g_ov_cur = i;
            }
        }
        (void)rows;
        if (g_hand >= 0) {
            ghost(c, x + bw - 20, y + 14, &g_hand_mon, frame);
        }
        if (mouse.up && mouse_in && !in_rect(mouse.x, mouse.y, x, y, bw, bh)) {
            g_ov = OV_NONE;
        }
    } else if (g_ov == OV_MENU) {
        int mx, my, mw, bh;

        menu_geom(&l, g_ov_slot, &mx, &my, &mw, &bh);
        ui_fill(c, 0, 0, cw, ch - 17, 0x000000u, 150);
        ui_panel(c, mx - 3, my - 3, mw + 6, 5 * (bh + 1) + 5, UI_CUT_TL | UI_CUT_BR, 5, UI_BODY, UI_SKY, 248);
        for (i = 0; i < 5; i++) {
            int by = my + i * (bh + 1), hov = mouse_in && in_rect(mouse.x, mouse.y, mx, by, mw, bh);
            int en = i == 4 || act_enabled(g, i == 0 ? ACT_SUMMARY : i == 1 ? ACT_ITEM : i == 2 ? ACT_MARK
                                           : ACT_RELEASE, g_ov_slot);

            if (hov) {
                g_ov_cur = i;
            }
            ui_row(c, mx, by, mw, bh, i == g_ov_cur, hov, &UI_THEME_PARTY);
            ui_text(c, mx + 7, by + ui_text_y(bh, UI_FONT_LARGE), k_menu_label[i], en ? UI_WHITE : UI_EDGE2,
                    UI_EDGE, UI_FONT_LARGE, 1);
            if (hov && mouse.up) {
                g_ov = OV_NONE;
                if (i < 4 && en) {
                    pc_act(g, w, i == 0 ? ACT_SUMMARY : i == 1 ? ACT_ITEM : i == 2 ? ACT_MARK : ACT_RELEASE,
                           g_ov_slot, in->frame);
                }
            }
        }
        if (mouse.up && mouse_in && g_ov == OV_MENU && !in_rect(mouse.x, mouse.y, mx - 3, my - 3, mw + 6, 5 * (bh + 1) + 5)) {
            g_ov = OV_NONE;
        }
    } else if (g_ov == OV_MARK) {
        static const char *const nm[6] = { "Circle", "Triangle", "Square", "Heart", "Star", "Diamond" };
        PcMon mm;
        int mw = 6 * 20 + 12, mh = 56, mx, my;

        (void)pc_mon(g, g_ov_slot, 0, &mm);
        mx = clampi(cw / 2 - mw / 2, 0, cw - mw);
        my = clampi(l.gy + 2 * l.pitch - 8, 20, l.hint_y - mh - 4);
        ui_fill(c, 0, 0, cw, ch - 17, 0x000000u, 150);
        ui_panel(c, mx, my, mw, mh, UI_CUT_TL | UI_CUT_BR, 6, UI_BODY, UI_SKY, 250);
        ui_text(c, mx + 6, my + 3, "MARKING", UI_WHITE, UI_EDGE, UI_FONT_LARGE, 1);
        for (i = 0; i < 6; i++) {
            int on = (mm.mark >> i) & 1, bx = mx + 6 + i * 20;
            int hov = mouse_in && in_rect(mouse.x, mouse.y, bx, my + 20, 18, 18);

            if (hov) {
                g_ov_cur = i;
            }
            ui_panel(c, bx, my + 20, 18, 18, UI_CUT_TL | UI_CUT_BR, 3, on ? UI_CYAN2 : UI_BODY2,
                     i == g_ov_cur ? UI_CYAN_EDGE : UI_EDGE, 255);
            mark_shape(c, bx + 5, my + 25, i, on ? UI_WHITE : UI_EDGE2);
            if (hov && mouse.up && !g_op.pending) {
                (void)pc_op_start(OP_MARK, g_ov_slot, 0, g->cur_box, -1, mm.mark ^ (1 << i));
            }
        }
        ui_text(c, mx + 6, my + 41, nm[g_ov_cur], UI_GREY, UI_EDGE, UI_FONT_SMALL, 1);
        if (mouse.up && mouse_in && !in_rect(mouse.x, mouse.y, mx, my, mw, mh)) {
            g_ov = OV_NONE;
        }
    }

    {
        static const char *const h_main[] = { "A", "Pick up", "B", "Leave", "X", "Menu", "Y", "Summary",
                                              "L", "Prev", "R", "Next", "SELECT", "Boxes" };
        static const u32 k_main[] = { BW_KEY_A, BW_KEY_B, BW_KEY_X, BW_KEY_Y, BW_KEY_L, BW_KEY_R, BW_KEY_SELECT };
        static const char *const h_carry[] = { "A", "Put down", "B", "Put back", "L", "Prev", "R", "Next",
                                               "SELECT", "Boxes" };
        static const u32 k_carry[] = { BW_KEY_A, BW_KEY_B, BW_KEY_L, BW_KEY_R, BW_KEY_SELECT };
        static const char *const h_pop[] = { "A", "Select", "B", "Back" };
        static const u32 k_pop[] = { BW_KEY_A, BW_KEY_B };
        static const char *const h_ov[] = { "A", "Choose", "B", "Close" };
        static const u32 k_ov[] = { BW_KEY_A, BW_KEY_B };

        if (popup) {
            pc_hints(c, l.hint_y, cw, h_pop, k_pop, 2, &mouse);
        } else if (g_ov != OV_NONE) {
            pc_hints(c, l.hint_y, cw, h_ov, k_ov, 2, &mouse);
        } else if (g_hand >= 0) {
            pc_hints(c, l.hint_y, cw, h_carry, k_carry, 5, &mouse);
        } else if (g->mode == PC_MODE_ITEMS) {
            static const char *const h_items[] = { "A", "Take item", "B", "Leave", "X", "Menu",
                                                   "L", "Prev", "R", "Next", "SELECT", "Boxes" };
            static const u32 k_items[] = { BW_KEY_A, BW_KEY_B, BW_KEY_X, BW_KEY_L, BW_KEY_R, BW_KEY_SELECT };

            pc_hints(c, l.hint_y, cw, h_items, k_items, 6, &mouse);
        } else {
            pc_hints(c, l.hint_y, cw, h_main, k_main, 7, &mouse);
        }
    }
}

static const EidosUiRecipe recipe_pc = {
    "pc", pc_match, pc_run, pc_reset, 1
};

const EidosUiRecipe *const ui_recipes_pc[] = { &recipe_pc };
const u32 ui_recipes_pc_count = 1;
