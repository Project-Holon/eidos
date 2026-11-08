/*
 * sdk/ui_text.c -- what the one-screen text boxes (engine/ui/textbox.c)
 * need from the field's talk balloons (stubs/ov/patch_ov021_talkmsgwin.c).
 *
 * The balloons are overlay 21's TalkMsgWinSys: a text window on a BG and a
 * balloon with a tail drawn as a 3D polygon.  While the interface draws the
 * text boxes itself, the game's balloon polygons are left out
 * (bw_ui_text_own), and the screen point each balloon's tail aims at, the
 * one the game works out every frame, is kept here so the interface's
 * balloon points its tail at the same speaker.
 */

#include <stddef.h>

#include "types.h"

#define TALK_MAX 32

typedef struct TalkTarget {
    const void *win;
    int x, y;
    int side;           /* the game's tail side (TALKMSGWIN_TAIL_SIDE_*) */
    u32 stamp;
} TalkTarget;

static TalkTarget g_targets[TALK_MAX];
static volatile int g_own;
static u32 g_stamp;

/* The interface draws the text boxes (1) or the game does (0). */
void bw_ui_text_set_own(int on)
{
    g_own = on != 0;
}

int bw_ui_text_own(void)
{
    return g_own;
}

/* The game worked out where window `win`'s tail aims (DS screen pixels)
 * and which side of the window it leaves (TALKMSGWIN_CalcTail). */
void bw_ui_talk_target(const void *win, int x, int y, int side)
{
    int i, slot = -1, oldest = 0;

    g_stamp++;
    for (i = 0; i < TALK_MAX; i++) {
        if (g_targets[i].win == win) {
            slot = i;
            break;
        }
        if (g_targets[i].stamp < g_targets[oldest].stamp) {
            oldest = i;
        }
    }
    if (slot < 0) {
        slot = oldest;
    }
    g_targets[slot].win = win;
    g_targets[slot].x = x;
    g_targets[slot].y = y;
    g_targets[slot].side = side;
    g_targets[slot].stamp = g_stamp;
}

/* Where window `win`'s tail aims and the side it leaves: 1 when the game
 * has said. */
int bw_ui_talk_target_get(const void *win, int *x, int *y, int *side)
{
    int i;

    for (i = 0; i < TALK_MAX; i++) {
        if (g_targets[i].win == win && win != NULL) {
            *x = g_targets[i].x;
            *y = g_targets[i].y;
            *side = g_targets[i].side;
            return 1;
        }
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* The field's framed windows (stubs/ov/patch_ov021_field_msgbg.c)      */
/* ------------------------------------------------------------------ */

#define FIELD_WIN_MAX 16

static const void *g_field_wins[FIELD_WIN_MAX];

void bw_ui_field_win_add(const void *bmpWin)
{
    int i;

    for (i = 0; i < FIELD_WIN_MAX; i++) {
        if (g_field_wins[i] == NULL || g_field_wins[i] == bmpWin) {
            g_field_wins[i] = bmpWin;
            return;
        }
    }
}

void bw_ui_field_win_remove(const void *bmpWin)
{
    int i;

    for (i = 0; i < FIELD_WIN_MAX; i++) {
        if (g_field_wins[i] == bmpWin) {
            g_field_wins[i] = NULL;
        }
    }
}

/* The open windows, in the order made; returns how many. */
int bw_ui_field_wins(const void **out, int max)
{
    int i, n = 0;

    for (i = 0; i < FIELD_WIN_MAX && n < max; i++) {
        if (g_field_wins[i] != NULL) {
            out[n++] = g_field_wins[i];
        }
    }
    return n;
}

/* ------------------------------------------------------------------ */
/* The item a script just gave (stubs/ov/patch_ov021_scrcmd_trainer_audio_item.c) */
/* ------------------------------------------------------------------ */

static volatile u32 g_item_got, g_item_count, g_item_serial;

void bw_ui_item_got(u32 item, u32 count, int added)
{
    if (added) {
        g_item_got = item;
        g_item_count = count;
        g_item_serial++;
    }
}

/* The last item a script put in the Bag and a number that changes with
 * every new one (0 when none yet). */
u32 bw_ui_item_last(u32 *count, u32 *serial)
{
    if (count != NULL) *count = g_item_count;
    if (serial != NULL) *serial = g_item_serial;
    return g_item_got;
}

/* ------------------------------------------------------------------ */
/* The key item a cut-in shows (stubs/ov/patch_ov021_fld_wipe_3dobj.c)  */
/* ------------------------------------------------------------------ */

static volatile u32 g_cutin_item, g_cutin_serial;

void bw_ui_cutin_item(u32 item)
{
    g_cutin_item = item;
    g_cutin_serial++;
}

/* The key item the last cut-in showed and a number that changes with every
 * new one (0 when none yet). */
u32 bw_ui_cutin_last(u32 *serial)
{
    if (serial != NULL) *serial = g_cutin_serial;
    return g_cutin_item;
}

/* ------------------------------------------------------------------ */
/* The battle gauges' status icons (stubs/ov/patch_ov094_btlv_gauge.c)  */
/* ------------------------------------------------------------------ */

static volatile int g_gauge_status[8];

void bw_ui_gauge_status(int slot, int status)
{
    if (slot >= 0 && slot < 8) {
        g_gauge_status[slot] = status;
    }
}

/* The condition gauge `slot` (0..7, the battle's view positions 2..9)
 * shows: 0 none, 1 paralysis, 2 sleep, 3 freeze, 4 burn, 5 poison, 6 bad
 * poison. */
int bw_ui_gauge_status_of(int slot)
{
    return slot >= 0 && slot < 8 ? g_gauge_status[slot] : 0;
}

/* ------------------------------------------------------------------ */
/* The field's open list (stubs/ov/patch_ov021_field_msgbg.c)          */
/* ------------------------------------------------------------------ */

extern u32 bw_input_frame_number(void);

static const void *volatile g_menu_list, *volatile g_menu_win;
static volatile u32 g_menu_frame;

void bw_ui_field_menu_seen(const void *menuList, const void *bmpWin)
{
    g_menu_list = menuList;
    g_menu_win = bmpWin;
    g_menu_frame = bw_input_frame_number();
}

/* The list the game ran in the last two frames, or 0 (the game runs an
 * open list every frame). */
int bw_ui_field_menu_now(const void **menuList, const void **bmpWin)
{
    if (g_menu_list == NULL || bw_input_frame_number() - g_menu_frame > 2u) {
        return 0;
    }
    *menuList = g_menu_list;
    *bmpWin = g_menu_win;
    return 1;
}

/* ------------------------------------------------------------------ */
/* The name entry's work (stubs/ov/patch_ov194_namein_main.c)          */
/* ------------------------------------------------------------------ */

static const void *volatile g_namein_work;
static volatile u32 g_namein_frame;

void bw_ui_namein_seen(const void *work)
{
    g_namein_work = work;
    g_namein_frame = bw_input_frame_number();
}

/* The name entry's work (NameinGraphicWork) when its main step ran in the
 * last two frames, or 0. */
const void *bw_ui_namein_now(void)
{
    if (g_namein_work == NULL || bw_input_frame_number() - g_namein_frame > 2u) {
        return NULL;
    }
    return g_namein_work;
}
