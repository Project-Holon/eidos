/*
 * engine/ui/recipe_musical.c -- the one-screen recipe for the Pokemon
 * Musical's dress-up (#44, #50): overlay 133 (src/game/ov133/dup_fitting.c),
 * run by MUSICAL_DRESSUP_SYS (ov010/musical_dressup_sys.c).
 *
 * The screen
 * ----------
 *
 * The dressing room fills the window in the game's own pink pattern.  The
 * Pokemon stands in its heart of sparkles: the game's lower screen, drawn
 * as the world in a box (EidosUiView.box_*) at the canvas's scale, which
 * is the DS's own (eidos_ui_want_ds_scale_size), so a prop a Pokemon wears
 * shows on it as the game draws it.  Of that screen only the middle shows
 * (MU_ST_*): its lace border, its OK button and the rows under the heart
 * are covered with the room's pattern.  On the room lie the screen's
 * parts: the props the player owns (the game's own pictures, a/0/4/5), the
 * body part they are listed for (L and R, or a click), the game's line for
 * the prop under the cursor, and DONE.  Where they stand and how they are
 * drawn is the style's (UiThemeOps.dressup_*, theme.h): Kosmos's here, a
 * grid of tiles at the right; Plasma's a column of plates and a band of
 * chips; Harmonia's the DS's ring of props round the Pokemon.
 *
 * The controls
 * ------------
 *
 * The DS dresses by the stylus: a ring of props round the Pokemon, a prop
 * dragged from it onto the Pokemon.  Here A puts the prop under the cursor
 * on (when it fits two free places, the places show on the Pokemon and
 * LEFT / RIGHT pick one) or takes it off, START is done, and the game's
 * "Is this look OK?" is asked as a question of the style.  Every one is
 * also a click.  The game's ring and stylus are out of use meanwhile
 * (stubs/ov/patch_ov133_dup_fitting.c).
 *
 * How a prop is put on
 * --------------------
 *
 * Through the game's own functions, on the game's thread
 * (eidos_ui_game_call), in the order the stylus makes the game call them:
 * a loose prop is made (DUP_FITTING_AddFitElemById, what the game itself
 * calls for the props of the last outfit), held at the place
 * (DUP_FITTING_AttachElem snaps it there) and let go (DUP_FITTING_MoveElem
 * wears it).  Taking one off is the drag back to the ring
 * (DUP_FITTING_ReturnElem).  A prop that lies loose (the props of the last
 * outfit, which the DS drops round the Pokemon) is put back on at the place
 * the outfit had it, since this screen has no floor to pick it from.  YES
 * saves as the game's OK button does (DUP_FITTING_SaveOutfits) and starts
 * the game's own leaving.
 *
 * The game's records (the DS offsets; pointers are four bytes wherever the
 * overlays run): see the W_*, E_* and L_* names below.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ui.h"
#include "kit.h"
#include "assets.h"
#include "theme.h"
#include "choice_list.h"
#include "ui_lang.h"
#include "dsmem.h"
#include "input.h"
#include "textbox.h"
#include "overlay.h"
#include "g3d.h"

#if defined(__GNUC__) || defined(__clang__)
#define MU_WEAK __attribute__((weak))
#else
#define MU_WEAK
#endif

#define OV_DRESSUP 133u

/* DupFittingWork (ov133/dup_fitting_types.h) */
#define W_PARENT      0x0004u   /* DupFittingOwner: saveData 0x08                       */
#define W_STATE       0x000Cu   /* 1: the player dresses; 7, 8 the fade in; 10, 9 leaving */
#define W_CURSOR_X    0x001Cu
#define W_CURSOR_Y    0x0020u
#define W_DRAG        0x0030u
#define W_FRAME_INDEX 0x0034u   /* the place the held prop snaps to, 10: none            */
#define W_HELD        0x0038u
#define W_HOLD_STATE  0x003Cu
#define W_HOLD_KIND   0x0040u
#define W_HOLD_DX     0x0044u
#define W_HOLD_DY     0x0048u
#define W_COUNT       0x00A8u   /* u16: the props owned                                  */
#define W_FRAMES      0x00ACu   /* 20 bytes a prop: id u16, placed int 0x0C, new int 0x10 */
#define W_LIST_A      0x1820u   /* the props that lie loose                              */
#define W_LIST_B      0x1824u   /* the props worn                                        */
#define W_LIST_C      0x1828u   /* the props on their way back to the ring (4 at most)   */
#define W_ACTOR_X     0x18B0u
#define W_EXIT_TIMER  0x18C0u
#define W_RENDER      0x1880u   /* MusPokeDrawSlot: the places, 0x38 bytes each from 0x14 */
#define W_ELEMSET     0x1884u   /* McssActorPool: resource 0x0C (the props' data table at +4) */
/* DupFittingItemList */
#define L_COUNT       0x04u
#define L_MAX         0x06u
#define L_HEAD        0x08u
/* DupFittingItemElem */
#define E_DATA        0x04u     /* the prop's record in W_FRAMES                          */
#define E_RENDER      0x0Cu
#define E_POS_X       0x10u
#define E_POS_Y       0x14u
#define E_PLACE       0x20u
#define E_NEXT        0x30u

#define MU_PLACES     10        /* the places a Pokemon can wear a prop at (9 is never used) */
#define MU_PROPS      250
#define MU_PARTS      6         /* ear, head, face, body, waist, hand */

/* the layout, canvas pixels (the canvas is at the DS's scale) */
#define MU_ST_X0      26        /* the stage's columns and rows that show */
#define MU_ST_X1      230
#define MU_ST_Y1      158
#define MU_PW_MIN     138       /* the room the tiles have at the right */
#define MU_PW_MAX     181
#define MU_TILE       34
#define MU_GAP        4
#define MU_ROWS_NEED  (192 + 17)
#define MU_COLS_NEED  (240 + MU_PW_MIN + 6)

extern void *bw_dsref__GameSystemGlobal MU_WEAK;
/* engine/g2d/g2d.h: the main engine's screen at DS size, without the 3D */
extern int g2d_hw_main_graphics_dsmem(const u8 *rgba3d, u8 *out) MU_WEAK;
/* main-engine BGs (bit n) and OBJs (bit 4) left out of a compose */
extern void g2d_hw_hide_main_bgs(u32 mask) MU_WEAK;
/* sdk/ui_dressup.c: the one-screen dress-up draws (for the game-side patch) */
extern void bw_ui_dressup_mark(void) MU_WEAK;

/* the game's own functions (overlays 133 and 112, and the ARM9) */
extern void ov133_DUP_FITTING_AddFitElemById(void *work, u16 id) MU_WEAK;
extern void ov133_DUP_FITTING_FreeHeldElem(void *work) MU_WEAK;
extern void ov133_DUP_FITTING_AttachElem(void *work, int sound) MU_WEAK;
extern void ov133_DUP_FITTING_MoveElem(void *work) MU_WEAK;
extern void ov133_DUP_FITTING_ReturnElem(void *work, int unused) MU_WEAK;
extern void ov133_DUP_FITTING_SaveOutfits(void *work) MU_WEAK;
extern void ov133_DUP_FITTING_ITEM_RemoveElem(u8 *list, u8 *elem) MU_WEAK;
extern void ov133_DUP_FITTING_ITEM_InsertElem(u8 *list, u8 *elem) MU_WEAK;
extern void ov112_MUSICAL_MCSS_SetElemFlag(u8 *unused, void *elem, int flag) MU_WEAK;
extern u8 *MUSICAL_SAVE_GetOutfits(void *save) MU_WEAK;
extern void CLACT_SetActorDrawEnable(void *actor, u32 value) MU_WEAK;
extern void GFL_USE_PlaySeDefaultVolume(u32 se) MU_WEAK;

static u32 rd32(const void *p, u32 off)
{
    u32 v;

    memcpy(&v, (const u8 *)p + off, 4);
    return v;
}

static u32 rd16(const void *p, u32 off)
{
    u16 v;

    memcpy(&v, (const u8 *)p + off, 2);
    return v;
}

static void wr32(void *p, u32 off, u32 v)
{
    memcpy((u8 *)p + off, &v, 4);
}

static void *rdp(const void *p, u32 off)
{
    return (void *)(uintptr_t)rd32(p, off);
}

static int gptr_ok(const void *p)
{
    return p != NULL && bw_ds_game_ptr(p) && ((uintptr_t)p & 3u) == 0;
}

/* Every game function the screen calls is there (a build without the
 * overlays has none: the two screens show). */
static int game_ready(void)
{
    return ov133_DUP_FITTING_AddFitElemById != NULL && ov133_DUP_FITTING_FreeHeldElem != NULL
           && ov133_DUP_FITTING_AttachElem != NULL && ov133_DUP_FITTING_MoveElem != NULL
           && ov133_DUP_FITTING_ReturnElem != NULL && ov133_DUP_FITTING_SaveOutfits != NULL
           && ov133_DUP_FITTING_ITEM_RemoveElem != NULL && ov133_DUP_FITTING_ITEM_InsertElem != NULL
           && ov112_MUSICAL_MCSS_SetElemFlag != NULL && MUSICAL_SAVE_GetOutfits != NULL
           && CLACT_SetActorDrawEnable != NULL && GFL_USE_PlaySeDefaultVolume != NULL
           && bw_ui_dressup_mark != NULL;
}

/* The fitting's work, through the proc on top (GameSystem: procSys 0x08;
 * ProcSys: current 0x08; Proc: work 0x14; MusicalDressupWork: the fitting's
 * work 0x04), or NULL. */
static void *dressup_work(void)
{
    void *gs, *ps, *cur, *mw, *dw;

    if (!bw_overlay_is_loaded(OV_DRESSUP) || &bw_dsref__GameSystemGlobal == NULL
        || bw_dsref__GameSystemGlobal == NULL) {
        return NULL;
    }
    memcpy(&gs, bw_dsref__GameSystemGlobal, sizeof gs);
    if (!gptr_ok(gs) || !gptr_ok(ps = rdp(gs, 0x08)) || !gptr_ok(cur = rdp(ps, 0x08))
        || !gptr_ok(mw = rdp(cur, 0x14)) || !gptr_ok(dw = rdp(mw, 0x04))) {
        return NULL;
    }
    return dw;
}

/* ------------------------------------------------------------------ */
/* What the game has: the props, the places, what is worn              */
/* ------------------------------------------------------------------ */

typedef struct MuProp {
    u16 id;
    u16 fits;           /* the prop's data flags: the places it can be worn at */
    u8 group;           /* the body part it looks nice on (the game's text group) */
    u8 placed;          /* it is out of the ring: worn, or loose */
    u8 isnew;
    s8 worn_at;         /* the place it is worn at, or -1 */
} MuProp;

typedef struct MuPlace {
    int on;             /* this Pokemon has the place */
    int x, y;           /* where a prop worn there is, lower screen pixels */
    int prop;           /* the prop worn there (an index of g_prop), or -1 */
} MuPlace;

static MuProp g_prop[MU_PROPS];
static int g_nprop;
static MuPlace g_place[MU_PLACES];
static int g_loose, g_returning;

/* The body part of a place (MUSICAL_MCSS_GetIndex, then both ears and both
 * hands as one part each): 0 ear, 1 head, 2 face, 3 body, 4 waist, 5 hand;
 * -1 for the place the game never uses. */
static int place_part(int place)
{
    static const s8 k[MU_PLACES] = { 0, 0, 1, 2, 2, 3, 4, 5, 5, -1 };

    return place >= 0 && place < MU_PLACES ? k[place] : -1;
}

/* Whether a prop with these data flags can be worn at a place
 * (MUSICAL_MCSS_IsModeSupportedA). */
static int fits_place(u32 fits, int place)
{
    static const u8 k[MU_PLACES] = { 1, 1, 2, 4, 8, 16, 32, 64, 64, 0 };

    return place >= 0 && place < MU_PLACES && (fits & k[place]) != 0;
}

/* The part's word in the game's text (the bank the dress-up's help line
 * takes it from: DUP_FITTING_SetTextHelp). */
static const char *part_word(int part)
{
    static const u8 k[MU_PARTS] = { 0x10, 0x11, 0x17, 0x14, 0x15, 0x16 };

    return part >= 0 && part < MU_PARTS ? ui_game_text(2, 75, k[part]) : "";
}

static int list_count(const void *dw, u32 list_off)
{
    const void *l = rdp(dw, list_off);

    return gptr_ok(l) ? (int)rd16(l, L_COUNT) : 0;
}

static int prop_index(u32 id)
{
    int i;

    for (i = 0; i < g_nprop; i++) {
        if (g_prop[i].id == id) {
            return i;
        }
    }
    return -1;
}

static void model_read(const void *dw)
{
    const u8 *pool = rdp(dw, W_ELEMSET), *res, *table = NULL, *slot = rdp(dw, W_RENDER);
    const u8 *list, *e;
    int i, n = (int)rd16(dw, W_COUNT), guard;

    if (gptr_ok(pool) && gptr_ok(res = rdp(pool, 0x0C)) && gptr_ok(rdp(res, 0x04))) {
        table = rdp(res, 0x04);
    }
    g_nprop = 0;
    for (i = 0; i < n && i < MU_PROPS; i++) {
        const u8 *f = (const u8 *)dw + W_FRAMES + 20u * (u32)i;
        MuProp *p = &g_prop[g_nprop];

        p->id = (u16)rd16(f, 0);
        if (p->id >= 0xFFu) {
            continue;
        }
        p->placed = rd32(f, 0x0C) != 0;
        p->isnew = rd32(f, 0x10) == 1;
        p->fits = table != NULL ? (u16)rd16(table + 12u * p->id, 6) : 0;
        p->group = table != NULL ? table[12u * p->id + 8] : 0;
        p->worn_at = -1;
        g_nprop++;
    }
    for (i = 0; i < MU_PLACES; i++) {
        const u8 *q = gptr_ok(slot) ? slot + 0x14u + 0x38u * (u32)i : NULL;

        g_place[i].prop = -1;
        g_place[i].on = q != NULL && rd32(q, 0) == 1 && place_part(i) >= 0;
        if (g_place[i].on) {
            g_place[i].x = (int)((float)(s32)(rd32(q, 0x04) + rd32(q, 0x10)) / 4096.0f) + 128;
            g_place[i].y = (int)((float)(s32)(rd32(q, 0x08) + rd32(q, 0x14)) / 4096.0f) + 96;
        }
    }
    list = rdp(dw, W_LIST_B);
    for (e = gptr_ok(list) ? rdp(list, L_HEAD) : NULL, guard = 0; gptr_ok(e) && guard < 16; e = rdp(e, E_NEXT), guard++) {
        const u8 *data = rdp(e, E_DATA);
        int at = (int)rd32(e, E_PLACE), pi = gptr_ok(data) ? prop_index(rd16(data, 0)) : -1;

        if (pi >= 0 && at >= 0 && at < MU_PLACES) {
            g_prop[pi].worn_at = (s8)at;
            g_place[at].prop = pi;
        }
    }
    g_loose = list_count(dw, W_LIST_A);
    g_returning = list_count(dw, W_LIST_C);
    {
        /* EIDOS_DEBUG_DRESSUP_LOG=1 (docs/test-switches.md): what is worn
         * and where, whenever it changes */
        static int on = -1;
        static char last[160];
        char now[160];
        int m = 0;

        if (on < 0) {
            on = getenv("EIDOS_DEBUG_DRESSUP_LOG") != NULL;
        }
        if (on) {
            m += snprintf(now + m, sizeof now - (size_t)m, "state %u props %d loose %d back %d worn", (unsigned)rd32(dw, W_STATE),
                          g_nprop, g_loose, g_returning);
            for (i = 0; i < MU_PLACES && m < (int)sizeof now - 16; i++) {
                if (g_place[i].prop >= 0) {
                    m += snprintf(now + m, sizeof now - (size_t)m, " %u@%d", (unsigned)g_prop[g_place[i].prop].id, i);
                }
            }
            if (strcmp(now, last) != 0) {
                snprintf(last, sizeof last, "%s", now);
                fprintf(stderr, "eidos: dress-up: %s (input frame %u)\n", now, (unsigned)bw_input_frame_number());
            }
        }
    }
}

/* ------------------------------------------------------------------ */
/* On the game's thread: putting a prop on, taking it off, saving      */
/* ------------------------------------------------------------------ */

enum { ACT_NONE, ACT_WEAR, ACT_OFF, ACT_SAVE };

static volatile int g_act, g_act_id, g_act_place;   /* the one action that waits */
static volatile int g_act_done;                     /* counts the actions done */
static int g_tick_queued;
static int g_restored;          /* the last outfit's loose props were seen to */

/* The held prop is let go of: the state DUP_FITTING_UpdateCursor leaves
 * when the stylus lifts. */
static void held_clear(u8 *dw)
{
    wr32(dw, W_DRAG, 0);
    wr32(dw, W_HELD, 0);
    wr32(dw, W_HOLD_STATE, 0);
    wr32(dw, W_HOLD_KIND, 0);
    wr32(dw, W_FRAME_INDEX, 10);
}

static u8 *list_find(u8 *dw, u32 list_off, u32 id)
{
    u8 *list = rdp(dw, list_off), *e;
    int guard = 0;

    for (e = gptr_ok(list) ? rdp(list, L_HEAD) : NULL; gptr_ok(e) && guard < 160; e = rdp(e, E_NEXT), guard++) {
        const u8 *data = rdp(e, E_DATA);

        if (gptr_ok(data) && rd16(data, 0) == id) {
            return e;
        }
    }
    return NULL;
}

static u8 *list_worn_at_part(u8 *dw, int part)
{
    u8 *list = rdp(dw, W_LIST_B), *e;
    int guard = 0;

    /* (the game's own rule: one prop a place kind, DUP_FITTING_IsFrameOwned,
     * where places 3 and 4 are one kind and every other place its own) */
    for (e = gptr_ok(list) ? rdp(list, L_HEAD) : NULL; gptr_ok(e) && guard < 16; e = rdp(e, E_NEXT), guard++) {
        int at = (int)rd32(e, E_PLACE);

        if (at == part || ((at == 3 || at == 4) && (part == 3 || part == 4))) {
            return e;
        }
    }
    return NULL;
}

/* A prop that is worn or loose goes back to the ring: the drag onto the
 * ring, let go (DUP_FITTING_UpdateCursor's release, DUP_FITTING_ReturnElem).
 * It flies from where it is.  0 when the game's list of props on their way
 * back is full (four): the caller tries again in a later frame. */
static int game_return(u8 *dw, u8 *e, int worn)
{
    u8 *lc = rdp(dw, W_LIST_C);

    if (!gptr_ok(lc) || rd16(lc, L_COUNT) >= rd16(lc, L_MAX)) {
        return 0;
    }
    if (worn) {
        ov133_DUP_FITTING_ITEM_RemoveElem(rdp(dw, W_LIST_B), e);
        ov133_DUP_FITTING_ITEM_InsertElem(rdp(dw, W_LIST_A), e);
        /* off the Pokemon: it no longer turns with its place (DUP_FITTING_AttachElem
         * away from every place) */
        ov112_MUSICAL_MCSS_SetElemFlag(rdp(dw, W_ELEMSET), rdp(e, E_RENDER), 0);
    }
    wr32(dw, W_CURSOR_X, rd32(e, E_POS_X));
    wr32(dw, W_CURSOR_Y, rd32(e, E_POS_Y));
    wr32(dw, W_HOLD_DX, rd32(e, E_POS_X));
    wr32(dw, W_HOLD_DY, rd32(e, E_POS_Y));
    wr32(dw, W_HELD, (u32)(uintptr_t)e);
    wr32(dw, W_HOLD_STATE, 2);
    wr32(dw, W_HOLD_KIND, worn ? 3 : 2);
    wr32(dw, W_FRAME_INDEX, 10);
    ov133_DUP_FITTING_ReturnElem(dw, 0);
    held_clear(dw);
    return 1;
}

/* A loose prop is held at a place and let go: it is worn there
 * (DUP_FITTING_AttachElem while the stylus holds it at the place,
 * DUP_FITTING_MoveElem when it lifts).  1 when the game took the place. */
static int game_attach(u8 *dw, u8 *e, int place, int sound)
{
    const u8 *slot = rdp(dw, W_RENDER), *q;

    if (!gptr_ok(slot) || place < 0 || place >= MU_PLACES) {
        return 0;
    }
    q = slot + 0x14u + 0x38u * (u32)place;
    wr32(dw, W_CURSOR_X, (u32)((int)((float)(s32)(rd32(q, 0x04) + rd32(q, 0x10)) / 4096.0f) + 128));
    wr32(dw, W_CURSOR_Y, (u32)((int)((float)(s32)(rd32(q, 0x08) + rd32(q, 0x14)) / 4096.0f) + 96));
    wr32(dw, W_HOLD_DX, rd32(e, E_POS_X));
    wr32(dw, W_HOLD_DY, rd32(e, E_POS_Y));
    wr32(dw, W_HELD, (u32)(uintptr_t)e);
    wr32(dw, W_HOLD_STATE, 2);
    wr32(dw, W_HOLD_KIND, 2);
    wr32(dw, W_FRAME_INDEX, 10);
    ov133_DUP_FITTING_AttachElem(dw, sound);
    if ((int)rd32(dw, W_FRAME_INDEX) != place) {
        held_clear(dw);
        return 0;
    }
    ov133_DUP_FITTING_MoveElem(dw);
    held_clear(dw);
    return 1;
}

/* The props of the last outfit, which the game drops loose round the
 * Pokemon when the screen opens (DUP_FITTING_InitFrames): each goes back on
 * at the place the outfit had it, or to the ring when this Pokemon cannot
 * wear it there.  One a frame.  1 when none is left. */
static int game_restore(u8 *dw)
{
    u8 *la = rdp(dw, W_LIST_A), *e, *owner = rdp(dw, W_PARENT), *outfit;
    u32 id;
    int k, place = -1;

    if (!gptr_ok(la) || !gptr_ok(e = rdp(la, L_HEAD))) {
        return 1;
    }
    id = gptr_ok(rdp(e, E_DATA)) ? rd16(rdp(e, E_DATA), 0) : 0xFFFFu;
    outfit = gptr_ok(owner) && gptr_ok(rdp(owner, 0x08)) ? MUSICAL_SAVE_GetOutfits(rdp(owner, 0x08)) : NULL;
    for (k = 0; outfit != NULL && k < 8; k++) {
        if (outfit[k * 6] != 10 && rd16(outfit, (u32)k * 6u + 2u) == id) {
            place = outfit[k * 6];
        }
    }
    if (place >= 0 && place < MU_PLACES && list_worn_at_part(dw, place) == NULL && game_attach(dw, e, place, 0)) {
        return 0;
    }
    (void)game_return(dw, e, 0);
    return 0;
}

static void game_tick(void *arg)
{
    u8 *dw = dressup_work();
    int act = g_act;

    (void)arg;
    g_tick_queued = 0;
    if (dw == NULL || rd32(dw, W_STATE) != 1u) {
        return;                         /* (the fade in, the first visit's demo, leaving) */
    }
    if (!g_restored) {
        g_restored = game_restore(dw);
        return;
    }
    if (list_count(dw, W_LIST_A) > 0) {
        (void)game_restore(dw);         /* (a prop the game dropped loose: none is left lying) */
        return;
    }
    if (act == ACT_WEAR) {
        u8 *old = list_worn_at_part(dw, g_act_place), *e;

        if (old != NULL) {
            /* the place is taken: the prop there comes off first (the game
             * would throw it on the floor) */
            if (!game_return(dw, old, 1)) {
                return;                 /* (try again next frame) */
            }
        }
        if (list_find(dw, W_LIST_B, (u32)g_act_id) == NULL) {
            ov133_DUP_FITTING_AddFitElemById(dw, (u16)g_act_id);
            ov133_DUP_FITTING_FreeHeldElem(dw);     /* the ring's pictures follow, as after the game's own */
            e = list_find(dw, W_LIST_A, (u32)g_act_id);
            if (e != NULL && !game_attach(dw, e, g_act_place, 1)) {
                (void)game_return(dw, e, 0);
            }
        }
    } else if (act == ACT_OFF) {
        u8 *e = list_find(dw, W_LIST_B, (u32)g_act_id);

        if (e != NULL && !game_return(dw, e, 1)) {
            return;
        }
    } else if (act == ACT_SAVE) {
        /* the game's OK button on its "Is this look OK?" screen
         * (DUP_FITTING_HandleTouch, then DUP_FITTING_Main's state 3) */
        ov133_DUP_FITTING_SaveOutfits(dw);
        GFL_USE_PlaySeDefaultVolume(1358);
        wr32(dw, W_STATE, 10);
        dw[W_EXIT_TIMER] = 0;
        CLACT_SetActorDrawEnable(rdp(dw, W_ACTOR_X), 1);
    }
    if (act != ACT_NONE) {
        g_act = ACT_NONE;
        g_act_done++;
    }
}

static int act_free(void)
{
    return g_act == ACT_NONE;
}

static void act_ask(int act, int id, int place)
{
    if (g_act == ACT_NONE) {
        g_act_id = id;
        g_act_place = place;
        g_act = act;
    }
}

/* ------------------------------------------------------------------ */
/* The props' pictures                                                 */
/* ------------------------------------------------------------------ */

static UiImage g_pic[MU_PROPS];
static u8 g_pic_state[MU_PROPS];        /* 0 not tried, 1 there, 2 none */

/* The prop's picture (a/0/4/5: a member a prop, a texture file; the game
 * shows member 100 for a prop past the archive's props,
 * MUSICAL_MCSS_GetArcFileCount), cut to what is drawn in it. */
static const UiImage *prop_pic(u32 id)
{
    u32 size = 0, bs = 0;
    u8 *data;
    const u8 *blk;
    G3dTex0 t;
    G3dTexture tex;

    if (id >= MU_PROPS) {
        return NULL;
    }
    {
        static u32 gen;

        if (gen != eidos_ui_data_generation()) {
            u32 k;

            gen = eidos_ui_data_generation();   /* the other version's data: its own props */
            for (k = 0; k < MU_PROPS; k++) {
                if (g_pic_state[k] == 1) {
                    free((void *)g_pic[k].rgba);
                }
                g_pic[k].rgba = NULL;
                g_pic_state[k] = 0;
            }
        }
    }
    if (g_pic_state[id] != 0) {
        return g_pic_state[id] == 1 ? &g_pic[id] : NULL;
    }
    g_pic_state[id] = 2;
    data = ui_arc_member(UI_ARC_PROPS, id < 100u ? id : 100u, &size);
    if (data == NULL) {
        return NULL;
    }
    memset(&tex, 0, sizeof tex);
    blk = g3d_container_block(data, size, "TEX0", &bs);
    if (blk != NULL && g3d_tex0_parse(&t, blk, bs) == 0 && g3d_tex0_decode(&t, 0, &tex) == 0 && tex.rgba != NULL) {
        int w = (int)tex.width, h = (int)tex.height, x, y, x0 = w, y0 = h, x1 = -1, y1 = -1;

        for (y = 0; y < h; y++) {
            for (x = 0; x < w; x++) {
                if (tex.rgba[((size_t)y * (size_t)w + (size_t)x) * 4 + 3] != 0) {
                    x0 = x < x0 ? x : x0;
                    x1 = x > x1 ? x : x1;
                    y0 = y < y0 ? y : y0;
                    y1 = y > y1 ? y : y1;
                }
            }
        }
        if (x1 >= x0 && y1 >= y0) {
            int cw = x1 - x0 + 1, ch = y1 - y0 + 1;
            u8 *cut = (u8 *)malloc((size_t)cw * (size_t)ch * 4);

            if (cut != NULL) {
                for (y = 0; y < ch; y++) {
                    memcpy(cut + (size_t)y * (size_t)cw * 4, tex.rgba + (((size_t)(y0 + y) * (size_t)w) + (size_t)x0) * 4,
                           (size_t)cw * 4);
                }
                g_pic[id].w = cw;
                g_pic[id].h = ch;
                g_pic[id].rgba = cut;
                g_pic_state[id] = 1;
            }
        }
    }
    g3d_texture_free(&tex);
    free(data);
    return g_pic_state[id] == 1 ? &g_pic[id] : NULL;
}

/* A prop's picture in the middle of a tile; one wider or higher than the
 * tile's inside is drawn at half size. */
static void prop_draw(EidosUiCtx *c, int x, int y, int box, u32 id)
{
    const UiImage *im = prop_pic(id);

    if (im == NULL) {
        return;
    }
    if (im->w <= box && im->h <= box) {
        ui_image(c, x + (box - im->w) / 2, y + (box - im->h) / 2, im, 1);
    } else {
        int xx, yy, w = im->w / 2, h = im->h / 2;

        for (yy = 0; yy < h && yy < box; yy++) {
            for (xx = 0; xx < w && xx < box; xx++) {
                const u8 *p = im->rgba + (((size_t)yy * 2) * (size_t)im->w + (size_t)xx * 2) * 4;

                if (p[3] != 0) {
                    ui_pixel(c, x + (box - w) / 2 + xx, y + (box - h) / 2 + yy,
                             ((u32)p[0] << 16) | ((u32)p[1] << 8) | p[2], 255);
                }
            }
        }
    }
}

/* The same in a box of any shape (a style's row, a place on a ring). */
static void prop_draw_in(EidosUiCtx *c, const EidosUiRect *b, u32 id)
{
    int box = b->w < b->h ? b->w : b->h;

    prop_draw(c, b->x + (b->w - box) / 2, b->y + (b->h - box) / 2, box, id);
}

/* The word of the part a prop looks nice on (the game's text group of the
 * prop; DUP_FITTING_SetTextItem), in capitals. */
static const char *names_word(int group)
{
    static char word[7][24];
    const char *s = ui_game_text(2, 75, (u32)group + 16u);
    int n = 0;

    if (group < 0 || group > 6) {
        return "";
    }
    for (; s[n] != '\0' && n < 23; n++) {
        word[group][n] = s[n] >= 'a' && s[n] <= 'z' ? (char)(s[n] - 32) : s[n];
    }
    word[group][n] = '\0';
    return word[group];
}

/* ------------------------------------------------------------------ */
/* The room's pattern                                                  */
/* ------------------------------------------------------------------ */

/* The stage's pink pattern repeats every 64 pixels each way.  The game
 * shows the main engine (the one with the 3D) on the lower screen here, and
 * the 3D covers the whole stage, so the screen the interface gets
 * (EidosUiFrame.screen_rgba) holds the compositor's placeholder colour
 * there; the pattern comes from the main engine's own pattern layer
 * (pattern_make).  The buffer is taken from the heap on first use: the
 * 32-bit build's program image must stay small (the DS's memory is mapped
 * at its own address right above it). */
#define MU_SCREEN_BYTES ((size_t)256 * 192 * 4)
static u8 *g_pat;
static int g_pat_ok;

/* The fade the world view gets (engine/ui/ui.c): the main engine's master
 * brightness, or its BLDY, down (dark > 0) or up (dark < 0), in 16ths.
 * From the registers as they are when the interface is drawn: the game's
 * frame is made from them right after, so a frame that is shown has the
 * stage and the room at the same step.  (The display's last frame,
 * g2d_gpu_frame, is one step behind at this point.) */
static int main_fade(void)
{
    u32 mb = bw_io_read16(0x0400006Cu), bld = (bw_io_read16(0x04000050u) >> 6) & 3u, f;

    if (((mb >> 14) & 3u) != 0u) {
        f = mb & 0x1Fu;
        f = f > 16u ? 16u : f;
        return ((mb >> 14) & 3u) == 2u ? (int)f : ((mb >> 14) & 3u) == 1u ? -(int)f : 0;
    }
    if (bld == 2u || bld == 3u) {
        f = bw_io_read16(0x04000054u) & 0x1Fu;
        f = f > 16u ? 16u : f;
        return bld == 3u ? (int)f : -(int)f;
    }
    return 0;
}

/* The room's pattern as the game has it this frame: the main engine's
 * screen with only its BG3 (the pattern's layer, DUP_FITTING_LoadBg; the
 * game scrolls it, DUP_FITTING_Main) -- the 3D off, BG1, BG2 and the OBJs
 * left out for this one render.  0 when it was made. */
static int pattern_make(void)
{
    int r;

    if (g2d_hw_main_graphics_dsmem == NULL || g2d_hw_hide_main_bgs == NULL) {
        return -1;
    }
    g2d_hw_hide_main_bgs(0x2u | 0x4u | 0x10u);
    r = g2d_hw_main_graphics_dsmem(NULL, g_pat);
    g2d_hw_hide_main_bgs(0);
    if (r == 0) {
        size_t i;

        g_pat_ok = 1;
        /* The colours as the display has them.  g2d_hw_main_graphics_dsmem
         * gives the engine's 6-bit stage, c6 * 255 / 63, and a palette
         * colour c5 is 2 * c5 + 1 there (g2d_c5to6); the display leaves a
         * screen with no master brightness as composed, c5 * 255 / 31
         * (engine/g2d/hwcompose.c).  The two differ by up to 2 of 255, and
         * the stage would show as a box in the room. */
        for (i = 0; i < MU_SCREEN_BYTES; i++) {
            if ((i & 3u) != 3u) {
                u32 c6 = ((u32)g_pat[i] * 63u + 127u) / 255u;

                g_pat[i] = (u8)((c6 >> 1) * 255u / 31u);
            }
        }
    }
    return r;
}

/* The pattern over the canvas rectangle (x, y, w, h), in phase with the
 * stage whose top-left is (ox, oy). */
static void pattern_fill(EidosUiCtx *c, int ox, int oy, int x, int y, int w, int h)
{
    int yy, xx;

    if (w <= 0 || h <= 0) {
        return;
    }
    for (yy = y; yy < y + h;) {
        int sy = ((yy - oy) % 64 + 64) % 64, bh = 192 - sy < y + h - yy ? 192 - sy : y + h - yy;

        for (xx = x; xx < x + w;) {
            int sx = ((xx - ox) % 64 + 64) % 64, bw = 256 - sx < x + w - xx ? 256 - sx : x + w - xx;

            ui_blit_screen(c, xx, yy, g_pat, sx, sy, bw, bh, 1, 0);
            xx += bw;
        }
        yy += bh;
    }
}

/* ------------------------------------------------------------------ */
/* The screen's own state                                              */
/* ------------------------------------------------------------------ */

enum { MODE_GRID, MODE_PLACE, MODE_ASK, MODE_LEAVE };

static int g_mode;
static int g_page;              /* 0: every prop; n: the nth body part this Pokemon has */
static int g_cur, g_top;        /* the grid's cursor (an entry of the page) and its first row */
static int g_cand[MU_PLACES], g_ncand, g_cand_cur, g_cand_prop;
static int g_ask_cur;
static void *g_seen_work;

/* the page's props, as indexes of g_prop: the ones that look nice on the
 * part first, as the game sorts its ring for a part (DUP_FITTING_FrameWeight) */
static int g_list[MU_PROPS], g_nlist;
static int g_parts[MU_PARTS], g_nparts;     /* the body parts this Pokemon has */

static int part_group_fits(int part, int group)
{
    /* MUSICAL_MCSS_IsModeSupportedC: the text group a part's props carry */
    static const s8 k[7] = { 0, 1, 2, 2, 3, 4, 5 };

    return group >= 0 && group < 7 && k[group] == part;
}

static int prop_fits_part(const MuProp *p, int part)
{
    int i;

    for (i = 0; i < MU_PLACES; i++) {
        if (g_place[i].on && place_part(i) == part && fits_place(p->fits, i)) {
            return 1;
        }
    }
    return 0;
}

/* This Pokemon has a place the prop can be worn at. */
static int prop_wearable(const MuProp *p)
{
    int i;

    for (i = 0; i < MU_PLACES; i++) {
        if (g_place[i].on && fits_place(p->fits, i)) {
            return 1;
        }
    }
    return 0;
}

static void pages_build(void)
{
    int i, pass;

    g_nparts = 0;
    for (i = 0; i < MU_PARTS; i++) {
        int j, on = 0;

        for (j = 0; j < MU_PLACES; j++) {
            on |= g_place[j].on && place_part(j) == i;
        }
        if (on) {
            g_parts[g_nparts++] = i;
        }
    }
    if (g_page > g_nparts) {
        g_page = 0;
    }
    g_nlist = 0;
    if (g_page == 0) {
        for (i = 0; i < g_nprop; i++) {
            g_list[g_nlist++] = i;
        }
    } else {
        int part = g_parts[g_page - 1];

        for (pass = 0; pass < 2; pass++) {
            for (i = 0; i < g_nprop; i++) {
                if (prop_fits_part(&g_prop[i], part) && (part_group_fits(part, g_prop[i].group) ? 0 : 1) == pass) {
                    g_list[g_nlist++] = i;
                }
            }
        }
    }
    if (g_cur >= g_nlist) {
        g_cur = g_nlist > 0 ? g_nlist - 1 : 0;
    }
}

/* The places the prop can go to from this page, of those this Pokemon has
 * and the prop fits: the page's part's.  On the first page, which names no
 * part: the places of the part the prop looks nice on (the game's own word
 * for the prop), or every place it fits when this Pokemon has none of
 * those. */
static int candidates(const MuProp *p, int *out)
{
    int i, n = 0, part = g_page > 0 ? g_parts[g_page - 1] : -1;

    if (part < 0) {
        for (i = 0; i < MU_PARTS; i++) {
            if (part_group_fits(i, p->group) && prop_fits_part(p, i)) {
                part = i;
            }
        }
    }
    for (i = 0; i < MU_PLACES; i++) {
        if (g_place[i].on && fits_place(p->fits, i) && (part < 0 || place_part(i) == part)) {
            out[n++] = i;
        }
    }
    return n;
}

/* A on a prop: off when it is worn; else on, at its one place, at its one
 * free place, or the player picks among them. */
static void prop_act(int pi)
{
    const MuProp *p = &g_prop[pi];
    int cand[MU_PLACES], n, i, nfree = 0, free_at = -1;

    if (!act_free()) {
        return;
    }
    if (p->worn_at >= 0) {
        act_ask(ACT_OFF, p->id, -1);
        eidos_ui_sound(0x54D);
        return;
    }
    n = candidates(p, cand);
    if (n == 0) {
        eidos_ui_sound(0x552);          /* (this Pokemon cannot wear it) */
        return;
    }
    for (i = 0; i < n; i++) {
        if (g_place[cand[i]].prop < 0) {
            nfree++;
            free_at = cand[i];
        }
    }
    if (n == 1 || nfree == 1) {
        act_ask(ACT_WEAR, p->id, n == 1 ? cand[0] : free_at);
        return;
    }
    memcpy(g_cand, cand, sizeof cand);
    g_ncand = n;
    g_cand_cur = 0;
    for (i = 0; i < n; i++) {
        if (g_place[cand[i]].prop < 0) {
            g_cand_cur = i;             /* (starts on a free one) */
            break;
        }
    }
    g_cand_prop = pi;
    g_mode = MODE_PLACE;
    eidos_ui_sound(0x54D);
}

/* ------------------------------------------------------------------ */
/* The recipe                                                          */
/* ------------------------------------------------------------------ */

static int musical_match(void)
{
    if (dressup_work() == NULL || !game_ready()) {
        return 0;
    }
    {
        /* Tests (docs/test-switches.md, EIDOS_DEBUG_DRESSUP_STYLUS=N): the
         * first N times the dress-up opens it is left to the game (the two
         * screens, the stylus), to compare what each way saves. */
        static int openings, skip = -1;
        static u32 last;
        u32 now = bw_input_frame_number();

        if (skip < 0) {
            skip = getenv("EIDOS_DEBUG_DRESSUP_STYLUS") != NULL ? atoi(getenv("EIDOS_DEBUG_DRESSUP_STYLUS")) : 0;
        }
        if (skip > 0) {
            if (now - last > 4u) {
                openings++;
            }
            last = now;
            if (openings <= skip) {
                return 0;
            }
        }
    }
    eidos_ui_want_ds_scale_size(MU_ROWS_NEED, MU_COLS_NEED);
    return 1;
}

typedef struct WrapPaint { EidosUiCtx *c; int x, y, n, max; u32 color; s32 shadow; } WrapPaint;

static void wrap_paint(void *user, const char *s, int len)
{
    WrapPaint *w = (WrapPaint *)user;
    char tmp[200];

    if (w->n >= w->max) {
        return;
    }
    len = len > (int)sizeof tmp - 1 ? (int)sizeof tmp - 1 : len;
    memcpy(tmp, s, (size_t)len);
    tmp[len] = '\0';
    ui_text(w->c, w->x, w->y + w->n * 12, tmp, w->color, w->shadow, UI_FONT_MEDIUM, 1);
    w->n++;
}

static void upper_ascii(const char *s, char *out, int max)
{
    int n = 0;

    for (; *s != '\0' && n < max - 1; s++) {
        out[n++] = *s >= 'a' && *s <= 'z' ? (char)(*s - 32) : *s;
    }
    out[n] = '\0';
}

/* ------------------------------------------------------------------ */
/* Kosmos's dress-up (a style's own: UiThemeOps.dressup_*, theme.h)     */
/* ------------------------------------------------------------------ */

/*
 * One screen on the game's own room, which shows round every part (no part
 * stands in a window of its own, so the screen does not read as a stage
 * and a second screen beside it): the title across the top; the Pokemon in
 * the middle of the room that is left; the props dark tiles that lie on
 * the room at the right, the body part's label over them; the game's line
 * for the prop in a bar across the bottom, DONE at the bar's right end.
 */
#define MU_HEAD_H     21        /* the title bar */
#define MU_INFO_H     30        /* the line's bar: two lines of the medium font */
#define MU_DONE_W     56

static void def_layout(UiDressupLayout *l)
{
    int right = l->w - 6 - l->safe_r, pw = right - 240, by = l->h - 6 - MU_INFO_H, cols, rows, gy, slack;

    pw = pw < MU_PW_MIN ? MU_PW_MIN : pw > MU_PW_MAX ? MU_PW_MAX : pw;
    cols = (pw - 8 + MU_GAP) / (MU_TILE + MU_GAP);              /* (8: the scroll bar) */
    cols = cols < 1 ? 1 : cols;
    gy = MU_HEAD_H + 5 + 14 + 4;
    rows = (by - 4 - gy + MU_GAP) / (MU_TILE + MU_GAP);
    rows = rows < 1 ? 1 : rows;
    /* the label and the tiles stand in the middle of the room between the title and the bar */
    slack = by - 4 - (gy + rows * (MU_TILE + MU_GAP) - MU_GAP);
    gy += slack > 0 ? slack / 2 : 0;
    l->kind = 0;
    l->cols = cols;
    l->rows = rows;
    l->tile_w = l->tile_h = MU_TILE;
    l->gap = MU_GAP;
    l->grid.w = cols * (MU_TILE + MU_GAP) - MU_GAP;
    l->grid.h = rows * (MU_TILE + MU_GAP) - MU_GAP;
    l->grid.x = right - 8 - l->grid.w;
    l->grid.y = gy;
    l->part.x = l->grid.x;
    l->part.y = gy - 4 - 14;
    l->part.w = l->grid.w;
    l->part.h = 14;
    /* the Pokemon: the heart's middle (85 rows into the stage) between the
     * title and the bar, and between the window's left side and the tiles */
    l->stage_x = (l->grid.x - 6 - (MU_ST_X1 - MU_ST_X0)) / 2 - MU_ST_X0;
    l->stage_y = (MU_HEAD_H + by) / 2 - 85;
    l->stage_y = l->stage_y < 0 ? 0 : l->stage_y;
    /* (what shows of the stage ends at the title and at the bar: they fade
     * with the room, the stage fades by itself) */
    l->show_x0 = MU_ST_X0;
    l->show_x1 = MU_ST_X1;
    l->show_y0 = MU_HEAD_H - l->stage_y > 0 ? MU_HEAD_H - l->stage_y : 0;
    l->show_y1 = by - l->stage_y < MU_ST_Y1 ? by - l->stage_y : MU_ST_Y1;
    l->done.w = MU_DONE_W;
    l->done.h = MU_INFO_H;
    l->done.x = right - l->done.w;
    l->done.y = by;
    l->info.x = 6;
    l->info.y = by;
    l->info.w = (l->asking ? right : l->done.x - 4) - l->info.x;   /* (the question has the whole bar) */
    l->info.h = MU_INFO_H;
}

static void def_backdrop(EidosUiCtx *c, const UiDressupLayout *l, const char *part, const char *note)
{
    (void)part;
    (void)note;                 /* (the bar has it) */
    ui_header(c, 0, 0, l->w, ui_tr("MUSICAL"), ui_tr("DRESS UP"), NULL, UI_SKY);
}

/* The arrows of a body part's label: a small triangle at each end. */
static void part_arrows(EidosUiCtx *c, const EidosUiRect *r, u32 col)
{
    int yy, top = r->y + (r->h - 7) / 2;

    for (yy = 0; yy < 7; yy++) {
        int span = yy < 4 ? yy + 1 : 7 - yy;

        ui_fill(c, r->x + 10 - span, top + yy, span, 1, col, 255);
        ui_fill(c, r->x + r->w - 10, top + yy, span, 1, col, 255);
    }
}

static void def_part(EidosUiCtx *c, const EidosUiRect *r, const char *label, int sel, int hover)
{
    (void)sel;
    (void)hover;
    ui_panel(c, r->x, r->y, r->w, r->h, UI_CUT_TL | UI_CUT_BR, 3, UI_NAVY2, UI_CYAN2, 255);
    ui_text(c, r->x + (r->w - ui_text_w(label, UI_FONT_SMALL)) / 2, r->y + ui_text_y(r->h, UI_FONT_SMALL), label, UI_WHITE,
            UI_NAVY, UI_FONT_SMALL, 1);
    part_arrows(c, r, UI_WHITE);
}

static void def_tile(EidosUiCtx *c, const EidosUiRect *r, const char *name, const char *part, int state, EidosUiRect *pic)
{
    int lit = (state & UI_DRESSUP_LIT) != 0, yy;

    (void)name;
    (void)part;
    pic->x = r->x + 2;
    pic->y = r->y + 2;
    pic->w = r->w - 4;
    pic->h = r->h - 4;
    if (state & UI_DRESSUP_EMPTY) {
        return;                 /* (no tile where there is no prop: the room shows) */
    }
    ui_panel(c, r->x, r->y, r->w, r->h, UI_CUT_TL | UI_CUT_BR, 4, lit ? UI_NAVY2 : 0x1E222Cu, lit ? UI_SKY : 0x323C50u, 255);
    if (state & UI_DRESSUP_WORN) {
        for (yy = 0; yy < 9; yy++) {                    /* worn: a lit corner */
            ui_fill(c, r->x + r->w - 9 + yy, r->y + yy, 9 - yy, 1, UI_SKY, 255);
        }
    } else if (state & UI_DRESSUP_NEW) {
        ui_fill(c, r->x + 3, r->y + 3, 3, 3, UI_HP_Y, 255);     /* new: a dot */
    }
}

static void def_info(EidosUiCtx *c, const EidosUiRect *r, u32 *ink, s32 *shadow)
{
    ui_panel(c, r->x, r->y, r->w, r->h, UI_CUT_TL | UI_CUT_BR, 6, UI_BODY, UI_CYAN2, 244);
    *ink = UI_LIGHT;
    *shadow = (s32)UI_INK;
}

static void def_done(EidosUiCtx *c, const EidosUiRect *r, const char *label, int lit)
{
    int font = ui_text_w(label, UI_FONT_LARGE) <= r->w - 8 ? UI_FONT_LARGE : UI_FONT_SMALL;

    ui_panel(c, r->x, r->y, r->w, r->h, UI_CUT_TL | UI_CUT_BR, 6, lit ? UI_NAVY2 : UI_BODY, lit ? UI_SKY : UI_CYAN2, 244);
    ui_text(c, r->x + (r->w - ui_text_w(label, font)) / 2, r->y + ui_text_y(r->h, font), label, UI_WHITE, UI_INK, font, 1);
}

/* ------------------------------------------------------------------ */
/* The ring (UiDressupLayout.kind 1)                                    */
/* ------------------------------------------------------------------ */

/* sin(deg) * 256, deg any whole number (no libm in the interface). */
static int isin256(int deg)
{
    static const short q[19] = { 0, 22, 44, 66, 88, 108, 128, 147, 165, 181, 196, 210, 222, 232, 241, 247, 252, 255, 256 };
    int sign = 1, i, f, v;

    deg %= 360;
    if (deg < 0) {
        deg += 360;
    }
    if (deg >= 180) {
        deg -= 180;
        sign = -1;
    }
    if (deg > 90) {
        deg = 180 - deg;
    }
    i = deg / 5;
    f = deg % 5;
    v = i >= 18 ? 256 : q[i] + (q[i + 1] - q[i]) * f / 5;
    return sign * v;
}

static u32 isqrt32(u32 v)
{
    u32 r = 0, bit = 1u << 30;

    while (bit > v) {
        bit >>= 2;
    }
    while (bit != 0) {
        if (v >= r + bit) {
            v -= r + bit;
            r = (r >> 1) + bit;
        } else {
            r >>= 1;
        }
        bit >>= 2;
    }
    return r;
}

/*
 * A place of the ring: `f16` 16ths of a place from the front (the lowest
 * point; right is positive), `slots` places round the whole rim.  The
 * places lie at equal steps of the rim's length, not of its angle: on a
 * flat ellipse equal angles crowd the props at its two ends.  Out: the
 * point from the ring's middle, canvas pixels.
 */
static void ring_point(int rx, int ry, int f16, int slots, int *ox, int *oy)
{
    static int tx[73], ty[73], ta[73], have_rx = -1, have_ry = -1;     /* each 5 degrees: the point and the rim's length to it, 16ths of a pixel */
    int i, s, seg, total;

    if (have_rx != rx || have_ry != ry) {
        ta[0] = 0;
        for (i = 0; i <= 72; i++) {
            tx[i] = rx * isin256(i * 5) / 16;
            ty[i] = ry * isin256(i * 5 + 90) / 16;
            if (i > 0) {
                int dx = tx[i] - tx[i - 1], dy = ty[i] - ty[i - 1];

                ta[i] = ta[i - 1] + (int)isqrt32((u32)(dx * dx + dy * dy));
            }
        }
        have_rx = rx;
        have_ry = ry;
    }
    total = ta[72] > 0 ? ta[72] : 1;
    s = (int)((long long)total * f16 / (slots * 16));
    s = ((s % total) + total) % total;
    for (i = 0; i < 71 && ta[i + 1] <= s; i++) {
    }
    seg = ta[i + 1] - ta[i];
    seg = seg > 0 ? seg : 1;
    *ox = (tx[i] + (int)((long long)(tx[i + 1] - tx[i]) * (s - ta[i]) / seg) + 8) / 16;
    *oy = (ty[i] + (int)((long long)(ty[i + 1] - ty[i]) * (s - ta[i]) / seg) + 8) / 16;
}

static int g_ring_pos;          /* the ring's turn, 16ths of a place: it eases to the cursor */

/* A style's parts fade in and out with the room (the game's fade): the whole
 * canvas, the hint bar with it, but the part of the stage that shows, which
 * fades by itself (the game's master brightness is on its picture). */
static void parts_fade(EidosUiCtx *c, int fade, int w, int h, int x0, int y0, int x1, int y1)
{
    u32 col = fade < 0 ? UI_WHITE : UI_INK;
    int a = (fade < 0 ? -fade : fade) * 255 / 16;

    if (fade == 0) {
        return;
    }
    ui_fill(c, 0, 0, w, y0, col, a);
    ui_fill(c, 0, y0, x0, y1 - y0, col, a);
    ui_fill(c, x1, y0, w - x1, y1 - y0, col, a);
    ui_fill(c, 0, y1, w, h - y1, col, a);
}

/* ------------------------------------------------------------------ */
/* The recipe                                                          */
/* ------------------------------------------------------------------ */

static void musical_run(EidosUiCtx *c, EidosUiView *view, const EidosUiFrame *in)
{
    static const char *const hints_grid[] = { "A", "Wear", "L R", "Body part", "START", "Done" };
    static const char *const hints_off[] = { "A", "Take off", "L R", "Body part", "START", "Done" };
    static const u32 keys_grid[] = { BW_KEY_A, BW_KEY_R, BW_KEY_START };
    static const char *const hints_place[] = { "LEFT RIGHT", "Place", "A", "Wear", "B", "Back" };
    static const u32 keys_place[] = { BW_KEY_RIGHT, BW_KEY_A, BW_KEY_B };
    static const char *const hints_ask[] = { "A", "Choose", "B", "No" };
    static const u32 keys_ask[] = { BW_KEY_A, BW_KEY_B };
    const UiThemeOps *th = ui_theme_active();
    int own = th->dressup_layout != NULL && th->dressup_backdrop != NULL && th->dressup_part != NULL
              && th->dressup_tile != NULL && th->dressup_info != NULL && th->dressup_done != NULL;
    /* the six parts: the style's own, or Kosmos's */
    void (*f_layout)(UiDressupLayout *) = own ? th->dressup_layout : def_layout;
    void (*f_backdrop)(EidosUiCtx *, const UiDressupLayout *, const char *, const char *) = own ? th->dressup_backdrop : def_backdrop;
    void (*f_part)(EidosUiCtx *, const EidosUiRect *, const char *, int, int) = own ? th->dressup_part : def_part;
    void (*f_tile)(EidosUiCtx *, const EidosUiRect *, const char *, const char *, int, EidosUiRect *) = own ? th->dressup_tile : def_tile;
    void (*f_info)(EidosUiCtx *, const EidosUiRect *, u32 *, s32 *) = own ? th->dressup_info : def_info;
    void (*f_done)(EidosUiCtx *, const EidosUiRect *, const char *, int) = own ? th->dressup_done : def_done;
    u8 *dw = dressup_work();
    int cw = c->canvas->w, ch = c->canvas->h, k = c->canvas->k, bar = ui_bar_h(c);
    int sx, sy, x0, y0, x1, y1, state, live, fade, pat_now, leaving;
    u32 keys = eidos_ui_keys_pressed();
    UiDressupLayout l;
    EidosUiRect r;

    if (dw == NULL) {
        return;
    }
    bw_ui_dressup_mark();
    {
        /* the screen opened: another work, or the recipe did not run for a
         * while (the game may give the next dress-up the same memory) */
        static u32 last_frame;

        if (g_seen_work != dw || in->frame - last_frame > 4u) {
            g_seen_work = dw;
            g_mode = MODE_GRID;
            g_page = g_cur = g_top = 0;
            g_ring_pos = 0;
            g_restored = 0;
            g_act = ACT_NONE;
            if (getenv("EIDOS_DEBUG_DRESSUP_LOG") != NULL && MUSICAL_SAVE_GetOutfits != NULL) {
                const u8 *owner = rdp(dw, W_PARENT);
                const u8 *o = gptr_ok(owner) && gptr_ok(rdp(owner, 0x08)) ? MUSICAL_SAVE_GetOutfits(rdp(owner, 0x08)) : NULL;
                char hex[8 * 6 * 2 + 16];
                int j, m = 0;

                for (j = 0; o != NULL && j < 48; j++) {
                    m += snprintf(hex + m, sizeof hex - (size_t)m, "%02X%s", o[j], j % 6 == 5 ? " " : "");
                }
                hex[m] = '\0';
                fprintf(stderr, "eidos: dress-up: opened, the saved outfit (8 records: place, -, prop id, turn): %s\n", hex);
            }
        }
        last_frame = in->frame;
    }
    state = (int)rd32(dw, W_STATE);
    model_read(dw);
    pages_build();
    if (!g_tick_queued && eidos_ui_game_call(game_tick, NULL)) {
        g_tick_queued = 1;
    }
    live = state == 1 && g_restored && g_loose == 0 && act_free();
    eidos_ui_take_keys(BW_KEY_GAME);

    /* the places: the style's, or Kosmos's */
    memset(&l, 0, sizeof l);
    l.w = cw;
    l.h = ch - bar;
    l.safe_r = c->safe_r;
    l.nparts = g_nparts + 1;
    /* the question and the leaving: the Pokemon stays where it stands, the
     * props go (the look is what is asked about) */
    leaving = g_mode == MODE_LEAVE || state == 10 || state == 9;
    l.asking = g_mode == MODE_ASK || leaving;
    l.at = g_nlist > 0 ? g_cur + 1 : 0;
    l.total = g_nlist;
    f_layout(&l);
    /* The stage's box is whole inside the window (the canvas may be a pixel
     * wider than the window: its size is rounded up), and what shows of it
     * is the place the layout asked for, less the stage's lace border. */
    sx = l.stage_x > in->win_w / k - 256 ? in->win_w / k - 256 : l.stage_x;
    sy = l.stage_y > in->win_h / k - 192 ? in->win_h / k - 192 : l.stage_y;
    sx = sx < 0 ? 0 : sx;
    sy = sy < 0 ? 0 : sy;
    x0 = l.stage_x + l.show_x0;
    y0 = l.stage_y + l.show_y0;
    x1 = l.stage_x + l.show_x1;
    y1 = l.stage_y + l.show_y1;
    x0 = x0 < sx + MU_ST_X0 ? sx + MU_ST_X0 : x0;
    x1 = x1 > sx + MU_ST_X1 ? sx + MU_ST_X1 : x1;
    y0 = y0 < sy ? sy : y0;
    y1 = y1 > sy + MU_ST_Y1 ? sy + MU_ST_Y1 : y1;

    {
        /* EIDOS_DEBUG_DRESSUP_LOG: where the parts stand, each time that
         * changes (canvas pixels: a scripted pointer's places) */
        static int log_on = -1, was[10];
        int now[10] = { sx, sy, l.grid.x, l.grid.y, l.part.x, l.part.y, l.done.x, l.info.x, l.info.w, g_mode };

        if (log_on < 0) {
            log_on = getenv("EIDOS_DEBUG_DRESSUP_LOG") != NULL;
        }
        if (log_on && memcmp(was, now, sizeof now) != 0) {
            int i;

            memcpy(was, now, sizeof now);
            fprintf(stderr, "eidos: dress-up: layout: canvas %dx%d mode %d stage %d,%d grid %d,%d tile %dx%d gap %d cols %d rows %d"
                            " part %d,%d %dx%d tabs %d (%d pages) info %d,%d %dx%d done %d,%d %dx%d\n",
                    cw, ch, g_mode, sx, sy, l.grid.x, l.grid.y, l.tile_w, l.tile_h, l.gap, l.cols, l.rows, l.part.x, l.part.y,
                    l.part.w, l.part.h, l.part_tabs, g_nparts + 1, l.info.x, l.info.y, l.info.w, l.info.h, l.done.x, l.done.y,
                    l.done.w, l.done.h);
            if (l.kind == 1) {
                for (i = -(l.ring_n / 2); i <= l.ring_n / 2; i++) {
                    int px, py;

                    ring_point(l.ring_rx, l.ring_ry, i * 16, l.ring_n + 3, &px, &py);
                    fprintf(stderr, "eidos: dress-up: layout: ring place %d at %d,%d\n", i, l.ring_cx + px, l.ring_cy + py);
                }
            }
            for (i = 0; g_mode == MODE_PLACE && i < g_ncand; i++) {
                fprintf(stderr, "eidos: dress-up: layout: body place %d at %d,%d\n", g_cand[i], sx + g_place[g_cand[i]].x,
                        sy + g_place[g_cand[i]].y);
            }
        }
    }

    /* the stage: the game's lower screen as the world, in its box */
    view->mode = EIDOS_VIEW_WORLD;
    view->world_screen = 1;
    view->ds_only = 1;
    view->lower_only = 1;
    view->box_x = sx * k;
    view->box_y = sy * k;
    view->box_w = 256 * k;
    view->box_h = 192 * k;

    /* the room: the pattern over everything but the part of the stage that shows */
    if (g_pat == NULL) {
        g_pat = (u8 *)calloc(1, MU_SCREEN_BYTES);
    }
    fade = main_fade();
    pat_now = g_pat != NULL && pattern_make() == 0;
    if (pat_now) {
        /* (the pattern at full strength: parts_fade fades the room and
         * all that lies on it, once) */
        pattern_fill(c, sx, sy, 0, 0, cw, y0);
        pattern_fill(c, sx, sy, 0, y0, x0, y1 - y0);
        pattern_fill(c, sx, sy, x1, y0, cw - x1, y1 - y0);
        pattern_fill(c, sx, sy, 0, y1, cw, ch - y1);
    } else {
        /* the game's screen is not up yet, or no more (its fade from and
         * to white, the display off): the room is the fade's colour, as
         * the stage is, and the rest waits */
        u32 col = fade < 0 ? UI_WHITE : UI_INK;

        ui_fill(c, 0, 0, cw, sy, col, 255);
        ui_fill(c, 0, sy, sx, 192, col, 255);
        ui_fill(c, sx + 256, sy, cw - (sx + 256), 192, col, 255);
        ui_fill(c, 0, sy + 192, cw, ch - (sy + 192), col, 255);
        ui_hints(c, ch - bar, cw, NULL, 0, NULL);
        ui_fill(c, 0, ch - bar, cw, bar, col, 255);     /* (the hint bar too: the whole window is the fade's colour) */
        return;
    }

    if (leaving) {
        f_backdrop(c, &l, "", NULL);
        ui_hints(c, ch - bar, cw, NULL, 0, NULL);
        parts_fade(c, fade, cw, ch, x0, y0, x1, y1);
        return;
    }

    if (g_mode == MODE_ASK) {
        /* the game's question (its bank 33, entry 1) where the style has
         * the prop's line, the answers the style's list over its right end */
        static const char *const yn[2] = { "YES", "NO" };
        const char *q = ui_game_text(2, 33, 1);
        int bw, bh, bx, by, lw = 64, lx, ly, i, hov = -1, cstyle = l.white ? UI_CHOICE_WHITE : UI_CHOICE_SYSTEM;
        u32 qink = UI_WHITE;
        s32 qsh = (s32)UI_EDGE;

        f_backdrop(c, &l, "", NULL);
        if (l.info.w > 0) {
            bx = l.info.x; by = l.info.y; bw = l.info.w; bh = l.info.h;
            f_info(c, &l.info, &qink, &qsh);
        } else {
            u32 qfill = 0, qedge = 0, ink = UI_WHITE, sh = UI_EDGE;

            bw = 228; bh = 30; bx = (cw - bw) / 2; by = ch - bar - 6 - bh;
            if (th->textbox != NULL) {
                th->textbox(c, UI_TEXTBOX_SYSTEM | 16, bx, by, bw, bh, &qfill, &qedge, &ink, &sh);
                qink = ink;
                qsh = (s32)sh;
            } else {
                ui_textbox_frame(c, UI_TEXTBOX_SYSTEM, bx, by, bw, bh);
            }
        }
        ui_text(c, bx + 14, by + ui_text_y(bh, UI_FONT_LARGE), q[0] != '\0' ? q : "Is this look OK?", qink, qsh,
                UI_FONT_LARGE, 1);
        lx = bx + bw - lw;
        ly = by - 4 - (2 * 18 + 8);
        ui_textbox_frame(c, l.white ? UI_TEXTBOX_NARRATION : UI_TEXTBOX_SYSTEM, lx, ly, lw, 2 * 18 + 8);
        if (keys & (BW_KEY_UP | BW_KEY_DOWN)) {
            g_ask_cur ^= 1;
            eidos_ui_sound(0x548);
        }
        for (i = 0; i < 2; i++) {
            r.x = lx + 3; r.y = ly + 4 + i * 18; r.w = lw - 6; r.h = 18;
            if (eidos_ui_hit(r) & EIDOS_UI_HOVER) {
                hov = i;
            }
        }
        for (i = 0; i < 2; i++) {
            int st, pick = 0;

            r.x = lx + 3; r.y = ly + 4 + i * 18; r.w = lw - 6; r.h = 18;
            st = eidos_ui_hit(r);
            if (th->choice_row != NULL) {
                th->choice_row(c, cstyle, r.x, r.y, r.w, r.h, ui_tr(yn[i]), hov >= 0 ? hov == i : g_ask_cur == i);
            } else {
                int lit = hov >= 0 ? hov == i : g_ask_cur == i;

                if (lit) {
                    ui_panel(c, r.x, r.y - 1, r.w, r.h, UI_CUT_TL | UI_CUT_BR, 5, 0x05343Eu, UI_CYAN2, 250);
                }
                ui_text(c, r.x + 10, r.y - (lit ? 1 : 0) + ui_text_y(r.h, UI_FONT_LARGE), ui_tr(yn[i]),
                        lit ? UI_WHITE : UI_LIGHT, 0x464850u, UI_FONT_LARGE, 1);
            }
            pick = (st & EIDOS_UI_CLICK) != 0 || ((keys & BW_KEY_A) != 0 && hov < 0 && g_ask_cur == i);
            if ((keys & BW_KEY_B) != 0 || eidos_ui_right_click()) {
                pick = i == 1;
            }
            if (pick && act_free()) {
                if (i == 0) {
                    act_ask(ACT_SAVE, 0, 0);
                    g_mode = MODE_LEAVE;
                } else {
                    g_mode = MODE_GRID;
                    eidos_ui_sound(0x552);
                }
                break;
            }
        }
        ui_hints(c, ch - bar, cw, hints_ask, 2, keys_ask);
        return;
    }

    /* the props */
    {
        int i, n, hov = -1, step = 0;
        char part[48], names[MU_PARTS + 1][24];
        const MuProp *sel;
        const char *note = g_mode == MODE_PLACE ? ui_tr("Choose where to put it.") : NULL;

        /* the body part: L and R, the label's arrows or its tabs (the page
         * turns before anything of it is drawn) */
        snprintf(names[0], sizeof names[0], "%s", ui_tr("ALL"));
        for (i = 0; i < g_nparts && i < MU_PARTS; i++) {
            upper_ascii(part_word(g_parts[i]), names[i + 1], (int)sizeof names[0]);
        }
        if (g_mode == MODE_GRID) {
            if (keys & BW_KEY_L) {
                step = -1;
            }
            if (keys & BW_KEY_R) {
                step = 1;
            }
            if (l.part_tabs) {
                for (i = 0; i <= g_nparts; i++) {
                    r.w = (l.part.w - g_nparts * 3) / (g_nparts + 1);
                    r.x = l.part.x + i * (r.w + 3);
                    r.y = l.part.y;
                    r.h = l.part.h;
                    if ((eidos_ui_hit(r) & EIDOS_UI_CLICK) != 0 && i != g_page) {
                        step = i - g_page;
                    }
                }
            } else {
                EidosUiRect la = { l.part.x, l.part.y, 18, l.part.h }, ra = { l.part.x + l.part.w - 18, l.part.y, 18, l.part.h };

                if (eidos_ui_hit(la) & EIDOS_UI_CLICK) {
                    step = -1;
                }
                if (eidos_ui_hit(ra) & EIDOS_UI_CLICK) {
                    step = 1;
                }
            }
        }
        if (step != 0) {
            g_page = ((g_page + step) % (g_nparts + 1) + g_nparts + 1) % (g_nparts + 1);
            g_cur = g_top = 0;
            g_ring_pos = 0;
            eidos_ui_sound(0x548);
            pages_build();
        }
        n = g_nlist;
        snprintf(part, sizeof part, "%s", names[g_page <= g_nparts ? g_page : 0]);

        l.at = n > 0 ? g_cur + 1 : 0;
        l.total = n;
        f_backdrop(c, &l, part, note);
        if (l.part_tabs) {
            for (i = 0; i <= g_nparts; i++) {
                r.w = (l.part.w - g_nparts * 3) / (g_nparts + 1);
                r.x = l.part.x + i * (r.w + 3);
                r.y = l.part.y;
                r.h = l.part.h;
                f_part(c, &r, names[i], i == g_page, (eidos_ui_hit(r) & EIDOS_UI_HOVER) != 0 && g_mode == MODE_GRID);
            }
        } else {
            f_part(c, &l.part, part, 1, 0);
        }

        if (l.kind == 0) {
            /* the grid: rows that scroll */
            int cols = l.cols < 1 ? 1 : l.cols, rows = l.rows < 1 ? 1 : l.rows, tw = l.tile_w + l.gap, thh = l.tile_h + l.gap;
            int total = (n + cols - 1) / cols, wh;
            EidosUiRect grid = { l.grid.x, l.grid.y, cols * tw, rows * thh };

            if (g_mode == MODE_GRID && n > 0) {
                int was = g_cur;

                if ((keys & BW_KEY_RIGHT) && g_cur + 1 < n) g_cur++;
                if ((keys & BW_KEY_LEFT) && g_cur > 0) g_cur--;
                if ((keys & BW_KEY_DOWN) && g_cur + cols < n) g_cur += cols;
                if ((keys & BW_KEY_UP) && g_cur - cols >= 0) g_cur -= cols;
                if (g_cur != was) {
                    eidos_ui_sound(0x548);
                }
            }
            wh = g_mode == MODE_GRID ? eidos_ui_wheel(grid) : 0;
            if (wh != 0) {
                g_top += wh > 0 ? -1 : 1;
                g_top = g_top > total - rows ? total - rows : g_top;
                g_top = g_top < 0 ? 0 : g_top;
                /* (the cursor stays on a row that shows) */
                while (g_cur / cols < g_top && g_cur + cols < n) g_cur += cols;
                while (g_cur / cols >= g_top + rows && g_cur - cols >= 0) g_cur -= cols;
            }
            if (g_cur / cols < g_top) g_top = g_cur / cols;
            if (g_cur / cols >= g_top + rows) g_top = g_cur / cols - rows + 1;
            if (total > rows) {
                ui_scrollbar(c, l.bar_x > 0 ? l.bar_x : l.grid.x + cols * tw - l.gap + 3, l.grid.y, rows * thh - l.gap, g_top, rows,
                             total, UI_SKY);
            }
            for (i = 0; i < cols * rows; i++) {
                int at = g_top * cols + i;

                r.x = l.grid.x + (i % cols) * tw; r.y = l.grid.y + (i / cols) * thh; r.w = l.tile_w; r.h = l.tile_h;
                if (at < n && g_mode == MODE_GRID && (eidos_ui_hit(r) & EIDOS_UI_HOVER)) {
                    hov = at;
                }
            }
            if (hov >= 0) {
                g_cur = hov;            /* (the cursor follows the pointer) */
            }
            for (i = 0; i < cols * rows; i++) {
                int at = g_top * cols + i, st = 0;
                EidosUiRect pic;
                const MuProp *p;

                r.x = l.grid.x + (i % cols) * tw; r.y = l.grid.y + (i / cols) * thh; r.w = l.tile_w; r.h = l.tile_h;
                if (at >= n) {
                    f_tile(c, &r, NULL, NULL, UI_DRESSUP_EMPTY, &pic);
                    continue;
                }
                p = &g_prop[g_list[at]];
                st |= at == g_cur && (hov >= 0 || eidos_ui_keys_lit()) && g_mode == MODE_GRID ? UI_DRESSUP_LIT : 0;
                st |= p->worn_at >= 0 ? UI_DRESSUP_WORN : 0;
                st |= p->isnew ? UI_DRESSUP_NEW : 0;
                /* (this Pokemon has no place for it, a hand's prop on a
                 * Pokemon with no hand; or a place is being chosen) */
                st |= !prop_wearable(p) || g_mode == MODE_PLACE ? UI_DRESSUP_DIM : 0;
                f_tile(c, &r, ui_game_text(2, 79, (u32)p->id + 3u), names_word(p->group), st, &pic);
                prop_draw_in(c, &pic, p->id);
                if (st & UI_DRESSUP_DIM) {
                    ui_fill(c, pic.x, pic.y, pic.w, pic.h, UI_INK, own ? 110 : 150);
                }
                if (g_mode == MODE_GRID && live && (eidos_ui_hit(r) & EIDOS_UI_CLICK)) {
                    g_cur = at;
                    prop_act(g_list[at]);
                }
            }
        } else {
            /* the ring: the props round the Pokemon, the cursor's in front
             * (the DS's ring; LEFT and RIGHT turn it) */
            int vis = l.ring_n < 3 ? 3 : l.ring_n, half, slots = vis + 3, base, lo, hi, pass, wh;
            EidosUiRect all = { l.ring_cx - l.ring_rx - 20, l.ring_cy - l.ring_ry - 20, 2 * l.ring_rx + 40, 2 * l.ring_ry + 40 };

            if (g_mode == MODE_GRID && n > 0) {
                wh = eidos_ui_wheel(all);
                if ((keys & (BW_KEY_RIGHT | BW_KEY_DOWN)) != 0 || wh < 0) {
                    g_cur = (g_cur + 1) % n;
                    eidos_ui_sound(0x548);
                }
                if ((keys & (BW_KEY_LEFT | BW_KEY_UP)) != 0 || wh > 0) {
                    g_cur = (g_cur + n - 1) % n;
                    eidos_ui_sound(0x548);
                }
            }
            if (n > 0) {
                /* the ring eases to the cursor, the short way round */
                int span = n * 16, d = g_cur * 16 - g_ring_pos;

                if (d > span / 2) d -= span;
                if (d < -span / 2) d += span;
                g_ring_pos += d == 0 ? 0 : (d > 0 ? (d + 2) / 3 : (d - 2) / 3);
                g_ring_pos = ((g_ring_pos % span) + span) % span;
            }
            half = vis / 2;
            lo = n >= vis ? -half : -((n - 1) / 2);
            hi = n >= vis ? half : n / 2;
            base = (g_ring_pos + 8) / 16;
            /* from the back to the front, so the front ones lie over the others */
            for (pass = half; pass >= 0 && n > 0; pass--) {
                int side;

                for (side = -1; side <= 1; side += 2) {
                    int kk = side * pass, idx, f, px, py, size, st = 0;
                    EidosUiRect pic;
                    const MuProp *p;

                    if ((pass == 0 && side > 0) || kk < lo || kk > hi) {
                        continue;
                    }
                    idx = ((base + kk) % n + n) % n;
                    f = (base + kk) * 16 - g_ring_pos;         /* 16ths of a place from the front */
                    if (f > n * 8) f -= n * 16;
                    if (f < -n * 8) f += n * 16;
                    ring_point(l.ring_rx, l.ring_ry, f, slots, &px, &py);
                    size = 24 + 12 * (py + l.ring_ry) / (2 * l.ring_ry > 0 ? 2 * l.ring_ry : 1);    /* 36 in front, 24 at the back */
                    r.w = r.h = size;
                    r.x = l.ring_cx + px - size / 2;
                    r.y = l.ring_cy + py - size / 2;
                    p = &g_prop[g_list[idx]];
                    /* (the cursor and the mark of a new prop come when the
                     * room is up, as the hints do) */
                    st |= idx == g_cur && g_mode == MODE_GRID && fade == 0 ? UI_DRESSUP_LIT : 0;
                    st |= p->worn_at >= 0 ? UI_DRESSUP_WORN : 0;
                    st |= p->isnew && fade == 0 ? UI_DRESSUP_NEW : 0;
                    st |= !prop_wearable(p) || g_mode == MODE_PLACE ? UI_DRESSUP_DIM : 0;
                    f_tile(c, &r, ui_game_text(2, 79, (u32)p->id + 3u), names_word(p->group), st, &pic);
                    prop_draw_in(c, &pic, p->id);
                    if (fade != 0) {
                        /* the ring lies over the stage, which fades by
                         * itself: there a prop of the ring (a round tile,
                         * the ellipse of its rectangle) takes the fade
                         * here; outside it parts_fade does */
                        EidosUiRect saved;

                        ui_push(c, 0, 0, &saved);
                        ui_clip(c, x0, y0, x1 - x0, y1 - y0);
                        ui_ellipse(c, r.x, r.y, r.w, r.h, fade < 0 ? UI_WHITE : UI_INK, fade < 0 ? UI_WHITE : UI_INK,
                                   (fade < 0 ? -fade : fade) * 255 / 16);
                        ui_pop(c, 0, 0, &saved);
                    }
                    if (g_mode == MODE_GRID && live && (eidos_ui_hit(r) & EIDOS_UI_CLICK)) {
                        if (idx == g_cur) {
                            prop_act(g_list[idx]);      /* the front one: on, or off */
                        } else {
                            g_cur = idx;                /* another: it comes to the front */
                            eidos_ui_sound(0x548);
                        }
                    }
                }
            }
        }
        if (g_mode == MODE_GRID && live && n > 0 && (keys & BW_KEY_A) != 0 && hov < 0) {
            prop_act(g_list[g_cur]);
            keys &= ~(u32)BW_KEY_A;     /* (this A is spent: it does not also pick a place) */
        }

        /* the prop under the cursor: the game's own line for it */
        sel = n > 0 ? &g_prop[g_list[g_cur]] : NULL;
        if (l.info.w > 0) {
            char line[300], flat[300];
            const char *slots[2];
            WrapPaint wp;
            u32 ink = UI_LIGHT;
            s32 shadow = (s32)UI_INK;
            int j, m = 0;

            f_info(c, &l.info, &ink, &shadow);
            line[0] = '\0';
            if (note != NULL) {
                snprintf(line, sizeof line, "%s", note);
            } else if (sel != NULL) {
                /* DUP_FITTING_SetTextItem: entry 3 of the dress-up's bank, its
                 * two words the prop's name and the part it looks nice on */
                slots[0] = ui_game_text(2, 79, (u32)sel->id + 3u);
                slots[1] = ui_game_text(2, 75, (u32)sel->group + 16u);
                ui_game_text_fill(2, 33, 3, slots, 2, line, (int)sizeof line);
            }
            for (j = 0; line[j] != '\0' && m < (int)sizeof flat - 1; j++) {
                flat[m++] = line[j] == '\n' ? ' ' : line[j];
            }
            flat[m] = '\0';
            wp.c = c; wp.x = l.info.x + 6; wp.y = l.info.y + 5; wp.n = 0; wp.max = (l.info.h - 6) / 12;
            wp.color = ink; wp.shadow = shadow;
            ui_wrap(flat, l.info.w - 12, UI_FONT_MEDIUM, wrap_paint, &wp);
        }

        /* DONE (START) */
        {
            int st = eidos_ui_hit(l.done), over = (st & EIDOS_UI_HOVER) != 0 && g_mode == MODE_GRID;

            f_done(c, &l.done, ui_tr("DONE"), over);
            if (g_mode == MODE_GRID && live && ((st & EIDOS_UI_CLICK) != 0 || (keys & BW_KEY_START) != 0)) {
                g_mode = MODE_ASK;
                g_ask_cur = 0;
                eidos_ui_sound(0x54D);
            }
        }
        if (g_mode == MODE_PLACE) {
            /* the places the prop fits, marked on the Pokemon */
            int pick = -1;

            if ((keys & (BW_KEY_RIGHT | BW_KEY_DOWN)) != 0) {
                g_cand_cur = (g_cand_cur + 1) % g_ncand;
                eidos_ui_sound(0x548);
            }
            if ((keys & (BW_KEY_LEFT | BW_KEY_UP)) != 0) {
                g_cand_cur = (g_cand_cur + g_ncand - 1) % g_ncand;
                eidos_ui_sound(0x548);
            }
            for (i = 0; i < g_ncand; i++) {
                const MuPlace *p = &g_place[g_cand[i]];
                int st;

                r.x = sx + p->x - 11; r.y = sy + p->y - 11; r.w = r.h = 22;
                st = eidos_ui_hit(r);
                if (st & EIDOS_UI_HOVER) {
                    g_cand_cur = i;
                }
                if (st & EIDOS_UI_CLICK) {
                    pick = i;
                }
            }
            for (i = 0; i < g_ncand; i++) {
                const MuPlace *p = &g_place[g_cand[i]];

                /* light marks, so the Pokemon and what it wears stay in
                 * view: the chosen place in the cursor's corners, the
                 * others a dot (rings covered a worn prop under them) */
                if (i == g_cand_cur) {
                    ui_cursor_corners(c, sx + p->x - 11, sy + p->y - 11, 22, 22, UI_SKY);
                } else {
                    ui_fill(c, sx + p->x - 2, sy + p->y - 2, 5, 5, UI_INK, 255);
                    ui_fill(c, sx + p->x - 1, sy + p->y - 1, 3, 3, UI_WHITE, 255);
                }
            }
            if ((keys & BW_KEY_A) != 0 && pick < 0) {
                pick = g_cand_cur;
            }
            if (pick >= 0 && act_free() && g_cand_prop < g_nprop) {
                act_ask(ACT_WEAR, g_prop[g_cand_prop].id, g_cand[pick]);
                g_mode = MODE_GRID;
            } else if ((keys & BW_KEY_B) != 0 || eidos_ui_right_click()) {
                g_mode = MODE_GRID;
                eidos_ui_sound(0x552);
            }
            ui_hints(c, ch - bar, cw, hints_place, 3, keys_place);
        } else {
            if (fade != 0) {
                ui_hints(c, ch - bar, cw, NULL, 0, NULL);       /* (the keys come with the room) */
            } else if (sel != NULL && sel->worn_at < 0 && !prop_wearable(sel)) {
                ui_hints(c, ch - bar, cw, hints_grid + 2, 2, keys_grid + 1);    /* (no Wear: it has no place) */
            } else {
                ui_hints(c, ch - bar, cw, sel != NULL && sel->worn_at >= 0 ? hints_off : hints_grid, 3, keys_grid);
            }
        }
        /* the room, the parts on it and the hint bar fade as one with the stage */
        parts_fade(c, fade, cw, ch, x0, y0, x1, y1);
    }
}

static void musical_reset(void)
{
    g_pat_ok = 0;
    g_seen_work = NULL;
    g_mode = MODE_GRID;
}

static const EidosUiRecipe recipe_musical = {
    "musical_dressup", musical_match, musical_run, musical_reset, 0
};

const EidosUiRecipe *const ui_recipes_musical[] = { &recipe_musical };
const u32 ui_recipes_musical_count = 1;
