/*
 * tests/test_ui_pc.c -- the one-screen recipe for the PC boxes
 * (engine/ui/recipe_pc.c).
 *
 * The recipe reads the game's own structs by offset.  This test includes the
 * recipe's source (BW_PC_TEST makes it take the game system from a cell the
 * test names and any address for a pointer), builds stand-ins for the game's
 * structs and functions in memory (a game system with a proc stack, a box
 * task, a party, two boxes of Pokemon, the window frame manager, the bottom
 * screen's pixels, and the three functions that check and apply a drop), and
 * runs the recipe at several canvas sizes.  It checks
 *
 *   - the scene test (the modes it draws, the ones it leaves to the DS
 *     screens, a sub screen on top);
 *   - the layout (the party's six rows, the grid, the details and the chips
 *     fit at every size and do not overlap);
 *   - the slot under a canvas position;
 *   - the mouse alone: click to pick up and click to put down, a drag, a
 *     drop that is refused (the last Pokemon able to battle), putting back
 *     with the right button and by letting go off a target, the box arrows,
 *     the wheel, the box list;
 *   - the keys alone: A, the arrows, B, L and R, SELECT, X and its menu,
 *     the chips, the marking panel;
 *   - what the game is asked to do for the summary: the script of keys;
 *   - the game's own list and YES / NO, drawn as buttons that tap;
 *   - the text decoder (the message archive's cipher and control codes).
 *
 * The interface layer's hooks (hit regions, taps, presses, the keys it
 * takes, the game-thread queue) are the test's own.  UI_PC_OUT names a
 * directory for the pictures (BMP files).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BW_PC_TEST 1
#include "recipe_pc.c"
#include "port_test.h"
#include "ds_fixture.h"

u8 *g_pc_test_gs_cell;
const u8 *g_pc_test_lists[6];

/* ------------------------------------------------------------------ */
/* The interface layer's hooks                                         */
/* ------------------------------------------------------------------ */

static int g_mx = -1, g_my = -1, g_click;
static int g_tap_n, g_tap_x[64], g_tap_y[64];
static u32 g_press_log[64];
static int g_press_n;
static u32 g_keys_in;           /* the keys the player pressed this frame   */
static u32 g_taken;
static int g_right_click;
static int g_keys_lit;         /* the keys used last (the kit: until the mouse moves) */

int eidos_ui_hit(EidosUiRect r)
{
    int s = 0;

    if (g_mx >= r.x && g_my >= r.y && g_mx < r.x + r.w && g_my < r.y + r.h) {
        s |= EIDOS_UI_HOVER;
        if (g_click) {
            s |= EIDOS_UI_CLICK;
        }
    }
    return s;
}

int eidos_ui_wheel(EidosUiRect r)
{
    (void)r;
    return 0;
}

int eidos_ui_mouse_pos(int *x, int *y)
{
    *x = g_mx;
    *y = g_my;
    return g_mx >= 0;
}

void eidos_ui_tap(int x, int y, u32 frames)
{
    (void)frames;
    if (g_tap_n < 64) {
        g_tap_x[g_tap_n] = x;
        g_tap_y[g_tap_n] = y;
    }
    g_tap_n++;
}

int eidos_ui_right_click(void)
{
    return g_right_click;
}

int eidos_ui_keys_lit(void)
{
    return g_keys_lit;
}

void eidos_ui_want_text(int on)
{
    (void)on;
}

void eidos_ui_press(u32 keys, u32 frames)
{
    (void)frames;
    if (g_press_n < 64) {
        g_press_log[g_press_n] = keys;
    }
    g_press_n++;
}

static u32 g_sounds[64];
static int g_sound_n;

void eidos_ui_sound(u32 seq)
{
    if (g_sound_n < 64) {
        g_sounds[g_sound_n] = seq;
    }
    g_sound_n++;
}

static int heard(u32 seq)
{
    int i;

    for (i = 0; i < g_sound_n && i < 64; i++) {
        if (g_sounds[i] == seq) {
            return 1;
        }
    }
    return 0;
}

void eidos_ui_take_keys(u32 mask)
{
    g_taken |= mask;
}

u32 eidos_ui_keys_pressed(void)
{
    return g_keys_in;
}

/* The game thread is the test's own: a call runs at once. */
int eidos_ui_game_call(void (*fn)(void *arg), void *arg)
{
    fn(arg);
    return 1;
}

/* ------------------------------------------------------------------ */
/* The fake game                                                       */
/* ------------------------------------------------------------------ */

typedef struct FakeMon {
    int exists, egg, species, form, gender, level, item, ability, mark;
    int moves[4], type1, type2, hp, maxhp;
    u16 nick[12];
} FakeMon;

static FakeMon g_box[2][30];
static FakeMon g_party[6];
static int g_party_n;
static u8 g_header[0x3E0];
static s8 g_frame_x[12], g_frame_y[12];
static int g_applied;           /* UpdateBoxSlots calls                     */

u32 POKE_TOOL_GetBoxValue(void *boxMon, u32 paramId, PcPtr arg)
{
    FakeMon *m = (FakeMon *)boxMon;

    switch (paramId) {
    case 0xA9: return (u32)m->exists;
    case 0x05: return (u32)m->species;
    case 0x4C: return (u32)m->egg;
    case 0x6F: return (u32)m->form;
    case 0x6E: return (u32)m->gender;
    case 0x9E: return (u32)m->level;
    case 0x06: return (u32)m->item;
    case 0x0B: return (u32)m->mark;
    case 0x0A: return (u32)m->ability;
    case 0x36: case 0x37: case 0x38: case 0x39: return (u32)m->moves[paramId - 0x36];
    case 0xAE: return (u32)m->type1;
    case 0xAF: return (u32)m->type2;
    case 0x74:
        memcpy((void *)arg, m->nick, sizeof(m->nick));
        return 0;
    default: return 0;
    }
}

void POKE_TOOL_SetBoxValue(void *boxMon, u32 paramId, PcPtr value)
{
    if (paramId == 0x0B) {
        ((FakeMon *)boxMon)->mark = (int)value;
    } else if (paramId == 0x06) {
        ((FakeMon *)boxMon)->item = (int)value;
    }
}

u32 POKE_TOOL_GetPartyValue(void *partyMon, u32 paramId, PcPtr arg)
{
    FakeMon *m = (FakeMon *)partyMon;

    (void)arg;
    return paramId == 0xA0 ? (u32)m->hp : paramId == 0xA1 ? (u32)m->maxhp : 0;
}

void *POKE_TOOL_CastToBoxParam(void *partyMon) { return partyMon; }
void *POKEPARTY_GetMember(void *party, u32 pos) { (void)party; return &g_party[pos]; }
u32 POKEPARTY_GetCount(const void *party) { (void)party; return (u32)g_party_n; }

void *BOX_SAVEDATA_GetPokemon(void *gameData, u32 box, u32 slot)
{
    (void)gameData;
    return &g_box[box & 1][slot];
}

void *BOX_SAVEDATA_GetHeader(void *gameData) { (void)gameData; return g_header; }

int BOX_SAVEDATA_CountPokemonInBox(void *gameData, u32 box)
{
    int i, n = 0;

    (void)gameData;
    for (i = 0; i < 30; i++) {
        n += g_box[box & 1][i].exists;
    }
    return n;
}

void BGWINFRM_GetPos(void *wk, u32 idx, s8 *x, s8 *y)
{
    (void)wk;
    *x = g_frame_x[idx];
    *y = g_frame_y[idx];
}

static unsigned char g_task[0x30];
static unsigned char g_work[0xA200];
static int g_pend_a, g_pend_b;

static FakeMon *slot_mon(int slot, int box)
{
    return slot >= SLOT_PARTY0 ? &g_party[slot - SLOT_PARTY0] : &g_box[box & 1][slot];
}

/* The game's rules, as far as the test needs them: a full box, the last
 * Pokemon able to battle. */
int ov170_BOX2_MAIN_HandleMonPickup(void *task, u32 a, u32 b)
{
    u8 *t = (u8 *)task;

    g_work[BW_RESULT] = 0;
    if (b >= 36) {
        u32 n = (b - 36 + t[0x19]) % t[0x15];

        if (n == t[BT_CURBOX] || BOX_SAVEDATA_CountPokemonInBox(NULL, n) == 30) {
            g_work[BW_RESULT] = 1;
            return 0;
        }
    }
    if (a >= SLOT_PARTY0 && g_party_n == 1 && (b < SLOT_PARTY0 ? !slot_mon((int)b, t[BT_CURBOX])->exists : b >= 36)) {
        g_work[BW_RESULT] = 3;
        return 0;
    }
    return 1;
}

int ov170_BOX2_MAIN_MoveCursorWithMon(void *task, u32 a, u32 b)
{
    if (ov170_BOX2_MAIN_HandleMonPickup(task, a, b) == 0) {
        g_pend_a = 0xFF;
        return 0;
    }
    g_pend_a = (int)a;
    g_pend_b = (int)b;
    return 1;
}

static void party_compact(void)
{
    int i, n = 0;

    for (i = 0; i < 6; i++) {
        if (g_party[i].exists) {
            g_party[n++] = g_party[i];
        }
    }
    for (i = n; i < 6; i++) {
        memset(&g_party[i], 0, sizeof(g_party[i]));
    }
    g_party_n = n;
}

void ov170_BOX2_MAIN_UpdateBoxSlots(void *task)
{
    u8 *t = (u8 *)task;
    FakeMon tmp;
    int a = g_pend_a, b = g_pend_b;

    g_applied++;
    if (b >= 36) {
        int n = (b - 36 + t[0x19]) % t[0x15], i;
        FakeMon *src = slot_mon(a, t[BT_HELD]);

        for (i = 0; i < 30; i++) {
            if (!g_box[n & 1][i].exists) {
                g_box[n & 1][i] = *src;
                memset(src, 0, sizeof(*src));
                break;
            }
        }
        party_compact();
        return;
    }
    if (b >= SLOT_PARTY0 && b - SLOT_PARTY0 >= g_party_n) {
        b = SLOT_PARTY0 + g_party_n;            /* an empty row: the end of the party */
    }
    tmp = *slot_mon(b, t[BT_CURBOX]);
    *slot_mon(b, t[BT_CURBOX]) = *slot_mon(a, t[BT_HELD]);
    *slot_mon(a, t[BT_HELD]) = tmp;
    party_compact();
}

static void w32(void *b, u32 off, u32 v) { memcpy((unsigned char *)b + off, &v, 4); }

static unsigned char g_gs[0x34], g_procsys[0x20], g_proc[0x20], g_params[0x40];
static unsigned char g_nested[0x20], g_winmgr[16], g_gd[16], g_partyobj[16];
static unsigned char g_bottom_px[256 * 192 * 4];

/* The game system with the box app on its stack. */
static void game_set(int overlay, int mode, int state)
{
    memset(g_gs, 0, sizeof(g_gs));
    memset(g_procsys, 0, sizeof(g_procsys));
    memset(g_proc, 0, sizeof(g_proc));
    memset(g_task, 0, sizeof(g_task));
    memset(g_params, 0, sizeof(g_params));
    memset(g_nested, 0, sizeof(g_nested));
    memset(g_work, 0, sizeof(g_work));
    wp(g_task, 0x00, g_params);
    wp(g_task, 0x04, g_nested);
    g_task[0x14] = 0;               /* box 1 */
    g_task[0x15] = 8;
    g_task[0x16] = 0;               /* cursor on slot 0 */
    g_task[0x17] = 0;
    w32(g_task, 0x28, (u32)state);
    wp(g_task, 0x2C, g_work);
    wp(g_params, 0x04, g_gd);
    wp(g_params, 0x08, g_partyobj);
    w32(g_params, 0x24, (u32)mode);
    wp(g_work, 0x74, g_winmgr);
    w32(g_proc, 0x04, (u32)overlay);
    w32(g_proc, 0x08, 2);
    wp(g_proc, 0x14, g_task);
    wp(g_procsys, 0x08, g_proc);
    wp(g_gs, 0x08, g_procsys);
    {
        static u32 cell;

        cell = fixture_address(g_gs);
        g_pc_test_gs_cell = (u8 *)&cell;
    }
}

static void put_nick(FakeMon *m, const char *s)
{
    int i;

    memset(m->nick, 0xFF, sizeof(m->nick));
    for (i = 0; s[i] != '\0' && i < 11; i++) {
        m->nick[i] = (u16)(unsigned char)s[i];
    }
}

static void world_build(void)
{
    int i;

    memset(g_box, 0, sizeof(g_box));
    memset(g_party, 0, sizeof(g_party));
    for (i = 0; i < 3; i++) {
        static const char *const names[3] = { "Saili", "Dom", "Buri-chu" };

        g_party[i].exists = 1;
        g_party[i].species = i == 0 ? 498 : i == 1 ? 504 : 506;
        g_party[i].level = 7 - i;
        g_party[i].hp = g_party[i].maxhp = 20 + i;
        g_party[i].moves[0] = 33;
        put_nick(&g_party[i], names[i]);
    }
    g_party_n = 3;
    g_box[0][7].exists = 1;
    g_box[0][7].species = 25;
    g_box[0][7].level = 12;
    put_nick(&g_box[0][7], "Pika");
    g_box[0][0].exists = 1;
    g_box[0][0].egg = 1;
    g_box[0][0].species = 25;
    put_nick(&g_box[0][0], "Egg");
    memset(g_header, 0xFF, sizeof(g_header));
    for (i = 0; i < 24; i++) {
        const char *n = "BOX ";
        int k;

        for (k = 0; k < 4; k++) {
            w32(g_header, 4 + (u32)i * 40 + (u32)k * 2, (u32)(unsigned char)n[k]);
            g_header[4 + i * 40 + k * 2 + 1] = 0;
        }
        g_header[4 + i * 40 + 8] = (unsigned char)('1' + i % 9);
        g_header[4 + i * 40 + 9] = 0;
        g_header[4 + i * 40 + 10] = 0xFF;
        g_header[4 + i * 40 + 11] = 0xFF;
    }
    g_applied = 0;
}

/* The windows the frame manager reports: the action list shown or not, the
 * tray open or not. */
static void frames_set(int list, int tray)
{
    int i;

    memset(g_frame_x, 0, sizeof(g_frame_x));
    memset(g_frame_y, 0, sizeof(g_frame_y));
    for (i = 0; i < 6; i++) {
        g_frame_x[i] = (s8)(list ? 0x15 : 32);
        g_frame_y[i] = (s8)(3 * i + 2);
    }
    g_frame_x[8] = 2;
    g_frame_y[8] = (s8)(tray ? 6 : 24);
}

/* The bottom screen: the action list's n buttons (dark grey at x = 248, the
 * stack grows up from the bottom), the white message window, YES / NO. */
static void screen_clear(void)
{
    int i;

    for (i = 0; i < 256 * 192; i++) {
        g_bottom_px[i * 4] = 139;
        g_bottom_px[i * 4 + 1] = 139;
        g_bottom_px[i * 4 + 2] = 139;
        g_bottom_px[i * 4 + 3] = 255;
    }
}

static void screen_px(int x, int y, int r, int g, int b)
{
    unsigned char *p = g_bottom_px + ((size_t)y * 256 + (size_t)x) * 4;

    p[0] = (unsigned char)r;
    p[1] = (unsigned char)g;
    p[2] = (unsigned char)b;
}

static void screen_list(int n)
{
    int i, x;

    for (i = 0; i < n; i++) {
        for (x = 244; x < 252; x++) {
            screen_px(x, 148 - 24 * i, 65, 65, 65);
        }
    }
}

static void screen_msg(int width)
{
    int x;

    for (x = 3; x < width; x++) {
        screen_px(x, 168, 255, 255, 255);
    }
}

static void screen_yesno(void)
{
    screen_px(222, 109, 8, 90, 139);
    screen_px(222, 133, 57, 57, 82);
}

/* ------------------------------------------------------------------ */
/* Running the recipe                                                  */
/* ------------------------------------------------------------------ */

static int g_fail;
#define CHECK(cond, msg)                                                    \
    do {                                                                    \
        bw_checks++;                                                        \
        if (!(cond)) {                                                      \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, (msg));          \
            g_fail = 1;                                                     \
        }                                                                   \
    } while (0)

static EidosUiCanvas g_cv;
static EidosUiCtx g_ctx;
static u32 g_frame;
static int g_win_w = 1920, g_win_h = 1080, g_win_k = 4;

static void run_frame(int mx, int my, int click, int down, int pressed, u32 keys)
{
    EidosUiFrame in;
    EidosUiView view;

    /* as the kit does: a click makes the mouse the input, a key the keys */
    if (click || down || pressed) {
        g_keys_lit = 0;
    } else if (keys != 0) {
        g_keys_lit = 1;
    }
    memset(&in, 0, sizeof(in));
    in.win_w = g_win_w;
    in.win_h = g_win_h;
    in.frame = ++g_frame;
    in.screen_rgba[1] = g_bottom_px;
    in.mouse.x = mx < 0 ? -1 : mx * g_win_k;
    in.mouse.y = my < 0 ? -1 : my * g_win_k;
    in.mouse.down = down;
    in.mouse.pressed = pressed;
    in.mouse.released = click;
    g_mx = mx;
    g_my = my;
    g_click = click;
    g_keys_in = keys;
    g_taken = 0;
    memset(&view, 0, sizeof(view));
    ui_begin(&g_ctx, &g_cv, g_win_w / g_win_k, g_win_h / g_win_k, g_win_k);
    if (recipe_pc.match()) {
        recipe_pc.run(&g_ctx, &view, &in);
    }
    g_right_click = 0;
}

static void idle(int n)
{
    while (n-- > 0) {
        run_frame(-1, -1, 0, 0, 0, 0);
    }
}

static void click_at(int x, int y)
{
    run_frame(x, y, 1, 0, 0, 0);
    idle(1);
}

static void key(u32 k)
{
    run_frame(-1, -1, 0, 0, 0, k);
    idle(2);
}

static void bmp_write(const char *path, const EidosUiCanvas *cv)
{
    FILE *f = fopen(path, "wb");
    int x, y;
    unsigned char hdr[54];
    u32 size = 54u + (u32)(cv->w * 3 + 3) / 4u * 4u * (u32)cv->h;

    if (f == NULL) {
        return;
    }
    memset(hdr, 0, sizeof(hdr));
    hdr[0] = 'B'; hdr[1] = 'M';
    memcpy(hdr + 2, &size, 4);
    hdr[10] = 54; hdr[14] = 40;
    memcpy(hdr + 18, &cv->w, 4);
    {
        int nh = -cv->h;

        memcpy(hdr + 22, &nh, 4);
    }
    hdr[26] = 1; hdr[28] = 24;
    fwrite(hdr, 1, sizeof(hdr), f);
    for (y = 0; y < cv->h; y++) {
        for (x = 0; x < cv->w; x++) {
            const unsigned char *p = cv->rgba + ((size_t)y * (size_t)cv->w + (size_t)x) * 4;
            unsigned char px3[3];

            px3[0] = p[2];
            px3[1] = p[1];
            px3[2] = p[0];
            fwrite(px3, 1, 3, f);
        }
        for (x = 0; x < (4 - (cv->w * 3) % 4) % 4; x++) {
            fputc(0, f);
        }
    }
    fclose(f);
}

static void set_size(int w, int h, int k)
{
    g_win_w = w;
    g_win_h = h;
    g_win_k = k;
}

static void scene_start(int mode)
{
    world_build();
    game_set(170, mode, 11);
    frames_set(0, 0);
    screen_clear();
    pc_reset();
    g_press_n = 0;
    g_tap_n = 0;
    g_frame += 50;
    idle(6);
}

static void slot_center(const PcLay *l, int slot, int *x, int *y)
{
    if (slot >= SLOT_PARTY0) {
        *x = l->px + l->pw / 2;
        *y = pc_row_y(l, slot - SLOT_PARTY0) + l->row_h / 2;
    } else {
        *x = l->gx + (slot % 6) * l->pitch + l->pitch / 2;
        *y = l->gy + (slot / 6) * l->pitch + l->pitch / 2;
    }
}

/* ------------------------------------------------------------------ */
/* The checks                                                          */
/* ------------------------------------------------------------------ */

static const int k_sizes[][3] = {
    /* the interface's own scale: the window's height over 270 */
    /* (the PC rounds: 720p is drawn at 3, recipe_pc's round_scale) */
    { 1280, 720, 3 }, { 1920, 1080, 4 }, { 2560, 1440, 5 }, { 1280, 800, 3 },
    { 1024, 768, 3 }, { 2560, 1080, 4 }, { 960, 540, 2 }, { 800, 600, 2 },
};

static void test_scene(void)
{
    world_build();
    game_set(170, 0, 11);
    CHECK(recipe_pc.match() == 1, "deposit mode is drawn");
    game_set(170, 1, 11);
    CHECK(recipe_pc.match() == 1, "withdraw mode is drawn");
    game_set(170, 2, 11);
    CHECK(recipe_pc.match() == 1, "move mode is drawn");
    game_set(170, 4, 11);
    CHECK(recipe_pc.match() == 1, "the battle box is drawn");
    game_set(170, 3, 11);
    CHECK(recipe_pc.match() == 1, "the item mode is drawn");
    game_set(170, 5, 11);
    CHECK(recipe_pc.match() == 0, "tuck in is left to the DS screens");
    game_set(170, 0, 85);
    CHECK(recipe_pc.match() == 0, "the game's marking panel is left to the DS screens");
    game_set(91, 0, 11);
    CHECK(recipe_pc.match() == 0, "another overlay is not the PC");
    game_set(170, 0, 11);
    w32(g_nested, 0x08, 0x02123456u);   /* a sub screen runs inside */
    CHECK(recipe_pc.match() == 0, "a sub screen on the task's own stack is left to the DS screens");
}

static void test_layout(void)
{
    size_t s;

    for (s = 0; s < sizeof(k_sizes) / sizeof(k_sizes[0]); s++) {
        PcLay l;
        int cw = k_sizes[s][0] / k_sizes[s][2], ch = k_sizes[s][1] / k_sizes[s][2], i, slot, ci;

        pc_layout(&l, cw, ch);
        CHECK(l.pitch >= 20 && l.pitch <= 48, "the grid cell is a sane size");
        CHECK(l.px + l.pw <= l.bx, "the party column ends before the box");
        CHECK(l.bx + l.bw <= l.dx, "the box ends before the details");
        CHECK(l.dx + l.dw <= cw, "the details stay in the canvas");
        CHECK(l.dw >= 80, "the details are wide enough");
        CHECK(l.gx + 6 * l.pitch <= l.bx + l.bw, "the grid fits the box panel");
        CHECK(l.gy + 5 * l.pitch <= l.by + l.bh, "the grid fits the box panel (height)");
        CHECK(pc_row_y(&l, 5) + l.row_h <= l.hint_y, "the sixth party row is above the hints");
        CHECK(pc_row_y(&l, 0) >= 26, "the first party row is below the title");
        for (i = 1; i < 6; i++) {
            CHECK(pc_row_y(&l, i) >= pc_row_y(&l, i - 1) + l.row_h, "the party rows do not overlap");
        }
        CHECK(l.chip_y + 30 <= l.hint_y, "the chips are above the hints");
        CHECK(l.chip_y >= l.y0 + 60, "the details keep room");
        for (ci = 0; ci < ACT_COUNT; ci++) {
            int x, y, w, h = chip_geom(&l, ci, &x, &y, &w);

            CHECK(x >= l.dx && x + w <= l.dx + l.dw && y >= l.y0 && y + h <= l.hint_y, "a chip is in the details column");
        }
        for (slot = 0; slot < 30; slot++) {
            int cx, cy;

            slot_center(&l, slot, &cx, &cy);
            CHECK(pc_slot_at(&l, cx, cy) == slot, "the middle of a cell is its slot");
        }
        for (slot = 30; slot < 36; slot++) {
            int cx, cy;

            slot_center(&l, slot, &cx, &cy);
            CHECK(pc_slot_at(&l, cx, cy) == slot, "a party row is its slot");
        }
        CHECK(pc_slot_at(&l, -1, -1) == -1, "outside is no slot");
        CHECK(pc_slot_at(&l, l.dx + 3, l.y0 + 3) == -1, "the details are no slot");
    }
}

static void test_text(void)
{
    /* a bank of four entries: "Hi", "A" + a control code with 2 arguments + "B", a
     * line break between "x" and "y", and an empty one */
    static u8 bank[16 + 4 + 8 * 4 + 64];
    u16 plain[4][12] = {
        { 'H', 'i', 0xFFFF },
        { 'A', 0xF000, 0x0102, 2, 7, 9, 'B', 0xFFFF },
        { 'x', 0xFFFE, 'y', 0xFFFF },
        { 0xFFFF },
    };
    u32 lens[4] = { 3, 8, 4, 1 }, off = 4 + 8 * 4, i, j;

    memset(bank, 0, sizeof(bank));
    bank[0] = 1;
    bank[2] = 4;
    bank[12] = 16;
    for (i = 0; i < 4; i++) {
        u16 key_ = (u16)((i + 3u) * 0x2983u);
        u8 *e = bank + 16 + 4 + 8 * i;

        e[0] = (u8)off;
        e[1] = (u8)(off >> 8);
        e[4] = (u8)lens[i];
        for (j = 0; j < lens[i]; j++) {
            u16 u = (u16)(plain[i][j] ^ key_);

            bank[16 + off + 2 * j] = (u8)u;
            bank[16 + off + 2 * j + 1] = (u8)(u >> 8);
            key_ = rotl3(key_);
        }
        off += 2 * lens[i];
    }
    memset(&g_bank[TXT_SPECIES], 0, sizeof(PcBank));
    g_bank[TXT_SPECIES].data = bank;
    g_bank[TXT_SPECIES].size = (u32)sizeof(bank);
    g_bank[TXT_SPECIES].count = 4;
    g_bank[TXT_SPECIES].cache = (char **)calloc(4, sizeof(char *));
    g_bank[TXT_SPECIES].tried = 1;
    CHECK(strcmp(pc_text(TXT_SPECIES, 0), "Hi") == 0, "a plain entry");
    CHECK(strcmp(pc_text(TXT_SPECIES, 1), "AB") == 0, "a control code and its arguments are skipped");
    CHECK(strcmp(pc_text(TXT_SPECIES, 2), "x y") == 0, "a line break is a space");
    CHECK(strcmp(pc_text(TXT_SPECIES, 3), "") == 0, "an empty entry");
    CHECK(strcmp(pc_text(TXT_SPECIES, 9), "") == 0, "an entry past the end");
    free(g_bank[TXT_SPECIES].cache);
    memset(&g_bank[TXT_SPECIES], 0, sizeof(PcBank));
    g_bank[TXT_SPECIES].tried = 1;      /* the data root is not read in this test */
}

/* ------------------------------------------------------------------ */
/* The mouse alone                                                      */
/* ------------------------------------------------------------------ */

static void test_mouse(const char *outdir)
{
    PcLay l;
    int x7, y7, x20, y20, xp, yp, xa, ya;
    char path[512];

    set_size(1920, 1080, 4);
    scene_start(2);
    pc_layout(&l, g_win_w / g_win_k, g_win_h / g_win_k);
    slot_center(&l, 7, &x7, &y7);
    slot_center(&l, 20, &x20, &y20);
    slot_center(&l, SLOT_PARTY0 + 1, &xp, &yp);

    /* nothing is sent to the game by looking */
    g_tap_n = 0;
    idle(3);
    run_frame(x7, y7, 0, 0, 0, 0);
    CHECK(g_tap_n == 0 && g_press_n == 0, "hovering sends nothing to the game");
    CHECK((g_taken & BW_KEY_GAME) == BW_KEY_GAME, "the recipe takes the player's keys while it owns the screen");
    if (outdir != NULL) {
        snprintf(path, sizeof(path), "%s/pc_hover_%dx%d.bmp", outdir, g_win_w, g_win_h);
        bmp_write(path, &g_cv);
    }

    /* click to pick up, click to put down: Pika from slot 7 to slot 20 */
    click_at(x7, y7);
    CHECK(g_hand == 7, "a click on a Pokemon picks it up");
    CHECK(g_tap_n == 0, "picking up sends no tap to the game");
    run_frame(x20, y20, 0, 0, 0, 0);
    if (outdir != NULL) {
        snprintf(path, sizeof(path), "%s/pc_carry_%dx%d.bmp", outdir, g_win_w, g_win_h);
        bmp_write(path, &g_cv);
    }
    idle(2);
    click_at(x20, y20);
    idle(2);
    CHECK(g_hand < 0, "the Pokemon is out of the hand after the drop");
    CHECK(g_box[0][20].exists && g_box[0][20].species == 25 && !g_box[0][7].exists,
          "the game moved Pika from slot 7 to slot 20");
    CHECK(g_applied == 1, "the move was applied through the game's own function once");
    CHECK(g_tap_n == 0, "the drop sends no tap to the game");

    /* click on an empty slot with nothing in the hand does nothing */
    click_at(x7, y7);
    CHECK(g_hand < 0, "an empty slot is not picked up");

    /* a drag from the box onto the party's second row swaps the two */
    g_box[0][7] = g_box[0][20];
    memset(&g_box[0][20], 0, sizeof(g_box[0][20]));
    run_frame(x7, y7, 0, 1, 1, 0);
    run_frame(x7 + 6, y7, 0, 1, 0, 0);
    CHECK(g_hand == 7, "pressing and moving picks the Pokemon up");
    run_frame(xp, yp, 0, 1, 0, 0);
    idle(0);
    run_frame(xp, yp, 1, 0, 0, 0);
    idle(2);
    CHECK(g_hand < 0 && g_box[0][7].species == 504 && g_party[1].species == 25,
          "a drag onto party row 2 swaps Pika with Dom");

    /* a drag released off every target puts it back */
    slot_center(&l, 7, &xa, &ya);
    run_frame(xa, ya, 0, 1, 1, 0);
    run_frame(xa + 6, ya, 0, 1, 0, 0);
    run_frame(l.dx + 5, l.y0 + 5, 0, 1, 0, 0);
    run_frame(l.dx + 5, l.y0 + 5, 1, 0, 0, 0);
    idle(2);
    CHECK(g_hand < 0 && g_box[0][7].species == 504, "letting go off a target puts it back");

    /* right button puts it back */
    click_at(xa, ya);
    CHECK(g_hand == 7, "picked up again");
    g_right_click = 1;
    run_frame(xa, ya, 0, 0, 0, 0);
    CHECK(g_hand < 0, "the right button puts it back");

    /* the last Pokemon able to battle cannot leave: the game refuses */
    g_party_n = 1;
    memset(&g_party[1], 0, sizeof(g_party[1]) * 5);
    g_party[0].exists = 1;
    {
        int xe, ye;

        slot_center(&l, 12, &xe, &ye);
        click_at(xp - 0, l.py + l.row_h / 2);
        CHECK(g_hand == SLOT_PARTY0, "the only party member is picked up");
        run_frame(xe, ye, 0, 0, 0, 0);
        idle(3);
        if (outdir != NULL) {
            snprintf(path, sizeof(path), "%s/pc_refused_%dx%d.bmp", outdir, g_win_w, g_win_h);
            bmp_write(path, &g_cv);
        }
        CHECK(g_probe_have && !g_probe_ok && g_probe_code == 3, "the game's check says no before the drop");
        click_at(xe, ye);
        idle(2);
        CHECK(g_hand == SLOT_PARTY0 && g_party_n == 1 && !g_box[0][12].exists,
              "a refused drop leaves the Pokemon in the hand and the data alone");
        pc_putback();
    }
}

static void test_boxes(const char *outdir)
{
    PcLay l;
    char path[512];
    int x7, y7;

    set_size(1920, 1080, 4);
    scene_start(2);
    pc_layout(&l, g_win_w / g_win_k, g_win_h / g_win_k);
    slot_center(&l, 7, &x7, &y7);

    /* the arrows tap the game's arrows */
    g_tap_n = 0;
    click_at(l.bx + 6 + 8, l.head_y + 9);
    CHECK(g_tap_n == 1 && g_tap_x[0] == BOX_ARROW_L && g_tap_y[0] == BOX_ARROW_Y, "the left arrow taps the game's");
    idle(20);
    g_tap_n = 0;
    click_at(l.bx + l.bw - 6 - 8, l.head_y + 9);
    CHECK(g_tap_n == 1 && g_tap_x[0] == BOX_ARROW_R, "the right arrow taps the game's");
    idle(20);

    /* the wheel is a box step, at most one in 24 frames */
    g_tap_n = 0;
    {
        EidosUiFrame in;
        EidosUiView view;

        memset(&in, 0, sizeof(in));
        in.win_w = g_win_w;
        in.win_h = g_win_h;
        in.frame = ++g_frame;
        in.screen_rgba[1] = g_bottom_px;
        in.mouse.x = x7 * g_win_k;
        in.mouse.y = y7 * g_win_k;
        in.mouse.wheel = -1;
        g_mx = x7;
        g_my = y7;
        g_click = 0;
        g_keys_in = 0;
        ui_begin(&g_ctx, &g_cv, g_win_w / g_win_k, g_win_h / g_win_k, g_win_k);
        recipe_pc.match();
        recipe_pc.run(&g_ctx, &view, &in);
    }
    CHECK(g_tap_n == 1 && g_tap_x[0] == BOX_ARROW_R, "the wheel down is the next box");
    idle(30);

    /* the box list: click the name, click a tile = go to that box (stepped) */
    click_at(l.bx + l.bw / 2, l.head_y + 9);
    CHECK(g_ov == OV_BOXES, "the box name opens the box list");
    run_frame(-1, -1, 0, 0, 0, 0);
    if (outdir != NULL) {
        snprintf(path, sizeof(path), "%s/pc_boxlist_%dx%d.bmp", outdir, g_win_w, g_win_h);
        bmp_write(path, &g_cv);
    }
    {
        int bx, by, bw, bh, cols, mw, mh;

        boxes_geom(&l, 24, &bx, &by, &bw, &bh, &cols, &mw, &mh);
        g_tap_n = 0;
        click_at(bx + 8 + 2 * (mw + 4) + 3, by + 26 + 3);          /* box 3 */
        CHECK(g_ov == OV_NONE && g_goto == 2, "a tile of the list is a trip to that box");
        idle(20);
        CHECK(g_tap_n >= 1 && g_tap_x[0] == BOX_ARROW_R, "the trip steps toward it with the game's arrow");
        g_task[0x14] = 1;
        idle(20);
        g_task[0x14] = 2;
        idle(20);
        CHECK(g_goto < 0, "the trip ends on the box");
    }
}

/* ------------------------------------------------------------------ */
/* The keys alone                                                       */
/* ------------------------------------------------------------------ */

static void test_keys(const char *outdir)
{
    char path[512];

    set_size(1920, 1080, 4);
    scene_start(2);
    g_keys_lit = 1;
    g_focus = 7;

    /* A picks Pika up, the arrows move the focus, A puts it down */
    g_sound_n = 0;
    key(BW_KEY_A);
    CHECK(g_hand == 7, "A picks the focused Pokemon up");
    CHECK(heard(0x666u), "picking up plays the game's pick-up sound");
    g_sound_n = 0;
    key(BW_KEY_RIGHT);
    key(BW_KEY_DOWN);
    CHECK(g_focus == 14, "the arrows move the focus");
    CHECK(heard(0x548u), "a step plays the cursor sound");
    if (outdir != NULL) {
        run_frame(-1, -1, 0, 0, 0, 0);
        snprintf(path, sizeof(path), "%s/pc_keys_carry_%dx%d.bmp", outdir, g_win_w, g_win_h);
        bmp_write(path, &g_cv);
    }
    key(BW_KEY_A);
    idle(3);
    CHECK(g_hand < 0 && g_box[0][14].species == 25 && !g_box[0][7].exists, "A puts it down on the focused slot");
    CHECK(heard(0x667u), "putting it down plays the put-down sound");

    /* B puts it back; B with an empty hand is the game's B (it leaves) */
    g_focus = 14;
    key(BW_KEY_A);
    CHECK(g_hand == 14, "picked up");
    key(BW_KEY_B);
    CHECK(g_hand < 0 && g_press_n == 0, "B with a Pokemon in the hand puts it back");
    key(BW_KEY_B);
    CHECK(g_press_n == 1 && g_press_log[0] == BW_KEY_B, "B with an empty hand is the game's B");

    /* Left from the first column enters the party, Right goes back */
    g_focus = 12;
    key(BW_KEY_LEFT);
    CHECK(g_focus == SLOT_PARTY0 + 2, "Left from column 0 enters the party row of that grid row");
    key(BW_KEY_RIGHT);
    CHECK(g_focus == 12, "Right from the party returns to the grid");
    g_focus = 5;
    key(BW_KEY_UP);
    CHECK(g_focus == FOC_BOX_NEXT, "Up from the top row enters the box header");
    key(BW_KEY_LEFT);
    CHECK(g_focus == FOC_BOX_NAME, "Left moves along the header");
    key(BW_KEY_DOWN);
    CHECK(g_focus == 2, "Down from the name returns to the grid");

    /* L and R step the box */
    g_tap_n = 0;
    key(BW_KEY_R);
    CHECK(g_tap_n == 1 && g_tap_x[0] == BOX_ARROW_R, "R is the next box");
    idle(20);
    g_tap_n = 0;
    key(BW_KEY_L);
    CHECK(g_tap_n == 1 && g_tap_x[0] == BOX_ARROW_L, "L is the previous box");
    idle(20);

    /* a Pokemon to the party by keys: carry from the grid, Left into the party, A */
    g_focus = 14;
    key(BW_KEY_A);
    key(BW_KEY_LEFT);
    key(BW_KEY_LEFT);
    key(BW_KEY_LEFT);
    key(BW_KEY_LEFT);
    key(BW_KEY_LEFT);
    key(BW_KEY_LEFT);
    CHECK(g_focus == SLOT_PARTY0 + 2, "the focus is on a party row");
    key(BW_KEY_DOWN);
    key(BW_KEY_DOWN);
    CHECK(g_focus == SLOT_PARTY0 + 4, "the party rows can be reached, empty ones too");
    key(BW_KEY_A);
    idle(3);
    CHECK(g_hand < 0 && g_party_n == 4 && g_party[3].species == 25, "dropping on an empty party row adds it to the party");

    /* X opens the action menu, Down and A: SELECT opens the box list */
    g_focus = 7 + 0;
    g_box[0][7].exists = 1;
    g_box[0][7].species = 25;
    key(BW_KEY_X);
    CHECK(g_ov == OV_MENU && g_ov_slot == 7, "X opens the action menu on the focused Pokemon");
    run_frame(-1, -1, 0, 0, 0, 0);
    if (outdir != NULL) {
        snprintf(path, sizeof(path), "%s/pc_menu_%dx%d.bmp", outdir, g_win_w, g_win_h);
        bmp_write(path, &g_cv);
    }
    key(BW_KEY_DOWN);
    key(BW_KEY_DOWN);
    key(BW_KEY_A);
    CHECK(g_ov == OV_MARK && g_ov_slot == 7, "the menu's third entry is the marking panel");
    key(BW_KEY_RIGHT);
    key(BW_KEY_RIGHT);
    key(BW_KEY_A);
    idle(2);
    CHECK(g_box[0][7].mark == 4, "A on the third mark sets it through the game");
    run_frame(-1, -1, 0, 0, 0, 0);
    if (outdir != NULL) {
        snprintf(path, sizeof(path), "%s/pc_mark_%dx%d.bmp", outdir, g_win_w, g_win_h);
        bmp_write(path, &g_cv);
    }
    key(BW_KEY_B);
    CHECK(g_ov == OV_NONE, "B closes the marking panel");
    key(BW_KEY_SELECT);
    CHECK(g_ov == OV_BOXES, "SELECT opens the box list");
    key(BW_KEY_B);
    CHECK(g_ov == OV_NONE, "B closes the box list");

    /* the chips by keys: Right from the grid's last column, Down, A (MARK) */
    g_focus = 11;
    key(BW_KEY_RIGHT);
    CHECK(g_focus == FOC_CHIP0, "Right from the last column enters the chips");
    key(BW_KEY_DOWN);
    CHECK(g_focus == FOC_CHIP0 + 2, "Down moves to the second row of chips");
    g_sel = 7;
    key(BW_KEY_A);
    CHECK(g_ov == OV_MARK, "the MARK chip opens the marking panel");
    key(BW_KEY_B);
    key(BW_KEY_LEFT);
    CHECK(g_focus >= 0 && g_focus < 30, "Left from the chips returns to the grid");
}

/* The item mode: the hand carries an item, a drop trades two items. */
static void test_items(const char *outdir)
{
    char path[512];

    set_size(1920, 1080, 4);
    scene_start(3);
    g_keys_lit = 1;
    g_box[0][7].item = 155;                     /* Pika holds an Oran Berry */
    g_party[1].item = 234;                      /* Dom holds Leftovers      */

    g_focus = 8;
    key(BW_KEY_A);
    CHECK(g_hand < 0, "an empty slot gives no item");
    g_focus = SLOT_PARTY0;
    key(BW_KEY_A);
    CHECK(g_hand < 0, "a Pokemon holding nothing gives no item");
    g_focus = 7;
    key(BW_KEY_A);
    CHECK(g_hand == 7, "A takes the item of the focused Pokemon");
    CHECK(g_box[0][7].item == 155, "taking it changes nothing yet");
    g_focus = 0;
    key(BW_KEY_A);
    idle(2);
    CHECK(g_hand == 7 && g_box[0][7].item == 155, "an Egg takes no item");

    /* give it to Saili (holds nothing) */
    g_focus = SLOT_PARTY0;
    run_frame(-1, -1, 0, 0, 0, 0);
    if (outdir != NULL) {
        snprintf(path, sizeof(path), "%s/pc_items_%dx%d.bmp", outdir, g_win_w, g_win_h);
        bmp_write(path, &g_cv);
    }
    key(BW_KEY_A);
    idle(3);
    CHECK(g_hand < 0 && g_party[0].item == 155 && g_box[0][7].item == 0, "a drop on a Pokemon gives it the item");

    /* swap: Saili's berry onto Dom, who holds Leftovers */
    g_focus = SLOT_PARTY0;
    key(BW_KEY_A);
    g_focus = SLOT_PARTY0 + 1;
    key(BW_KEY_A);
    idle(3);
    CHECK(g_party[1].item == 155 && g_party[0].item == 234, "a drop on a holder trades the two items");

    /* Mail stays */
    g_party[2].item = 137;
    g_focus = SLOT_PARTY0 + 2;
    key(BW_KEY_A);
    CHECK(g_hand < 0, "Mail can't be taken");
    g_focus = SLOT_PARTY0;
    key(BW_KEY_A);
    g_focus = SLOT_PARTY0 + 2;
    key(BW_KEY_A);
    idle(3);
    CHECK(g_party[2].item == 137 && g_party[0].item == 234, "nor swapped away");
    key(BW_KEY_B);
    CHECK(g_hand < 0, "B puts the item back");

    /* only the marks of the actions */
    CHECK(act_enabled(&g_g, ACT_SUMMARY, 7) == 0 && act_enabled(&g_g, ACT_ITEM, 7) == 0 &&
          act_enabled(&g_g, ACT_RELEASE, 7) == 0 && act_enabled(&g_g, ACT_MARK, 7) == 1,
          "the item mode keeps the marks only");
}

/* ------------------------------------------------------------------ */
/* What the game does                                                   */
/* ------------------------------------------------------------------ */

static void test_script(void)
{
    int i, a_pressed = 0, down_pressed = 0;

    set_size(1920, 1080, 4);
    scene_start(2);
    g_focus = 7;
    g_sel = 7;
    g_press_n = 0;
    {
        PcLay l;
        int x, y, w, h;

        pc_layout(&l, g_win_w / g_win_k, g_win_h / g_win_k);
        h = chip_geom(&l, ACT_SUMMARY, &x, &y, &w);
        click_at(x + w / 2, y + h / 2);
    }
    CHECK(pc_script_busy(), "the SUMMARY chip starts a script on the game");
    /* the game's list shows once the script has pressed A; the script
     * waits for the game to settle after each key (about 50 frames in all) */
    for (i = 0; i < 70; i++) {
        run_frame(-1, -1, 0, 0, 0, 0);
        if (i == 12) {
            frames_set(1, 0);
            screen_list(6);
        }
    }
    for (i = 0; i < g_press_n && i < 64; i++) {
        a_pressed += g_press_log[i] == BW_KEY_A;
        down_pressed += g_press_log[i] == BW_KEY_DOWN;
    }
    CHECK(a_pressed == 2 && down_pressed == 1, "the script presses A, DOWN once (SUMMARY is the second entry), A");
    CHECK(!pc_script_busy(), "the script is over");

    /* a Pokemon of the party: the tray opens first */
    scene_start(2);
    g_sel = SLOT_PARTY0;
    g_press_n = 0;
    pc_act(&g_g, &g_w, ACT_SUMMARY, SLOT_PARTY0, g_frame);
    CHECK(pc_script_busy() && g_sc[0].kind == SC_CUR && g_sc[0].a == 33, "a party Pokemon: the script starts on the party button");
}

/* ------------------------------------------------------------------ */
/* The game's own windows                                               */
/* ------------------------------------------------------------------ */

static void test_popups(const char *outdir)
{
    PcLay l;
    char path[512];
    int found, px_, py_;

    set_size(1920, 1080, 4);
    scene_start(2);
    pc_layout(&l, g_win_w / g_win_k, g_win_h / g_win_k);

    /* the game's list: six buttons, the second one taps the game's second */
    frames_set(1, 0);
    screen_clear();
    screen_list(6);
    screen_msg(160);
    {
        static u8 t6[6 * 4];
        static const u16 ids[6] = { 81, 74, 75, 76, 77, 80 };
        int i;

        for (i = 0; i < 6; i++) {
            t6[4 * i] = (u8)ids[i];
            t6[4 * i + 1] = (u8)(ids[i] >> 8);
        }
        g_pc_test_lists[2] = t6;
    }
    idle(2);
    CHECK(g_w.list_shown == 1 && g_w.list_n == 6, "six list buttons are counted from the pixels");
    CHECK(g_w.list_ids[0] == 81 && g_w.list_ids[5] == 80, "the list ids come from the game's table");
    if (outdir != NULL) {
        snprintf(path, sizeof(path), "%s/pc_list_%dx%d.bmp", outdir, g_win_w, g_win_h);
        bmp_write(path, &g_cv);
    }
    CHECK((g_taken & BW_KEY_GAME) == 0, "the game's list gets the player's keys");
    found = 0;
    for (py_ = l.by; py_ < l.by + l.bh && !found; py_ += 5) {
        for (px_ = l.bx; px_ < l.bx + l.bw && !found; px_ += 5) {
            g_tap_n = 0;
            click_at(px_, py_);
            if (g_tap_n == 1 && g_tap_x[0] == 212 && g_tap_y[0] == 148 - 24 * 4) {
                found = 1;
            }
        }
    }
    CHECK(found, "the second list button taps (212, 52)");

    /* YES and NO */
    frames_set(0, 0);
    screen_clear();
    screen_msg(252);
    screen_yesno();
    idle(2);
    CHECK(g_w.yesno == 1, "the two-choice menu is found (the cursor's button is lighter)");
    {
        int found_yes = 0, found_no = 0;

        for (py_ = l.by; py_ < l.by + l.bh; py_ += 5) {
            for (px_ = l.bx; px_ < l.bx + l.bw; px_ += 5) {
                g_tap_n = 0;
                click_at(px_, py_);
                if (g_tap_n == 1 && g_tap_x[0] == YESNO_X && g_tap_y[0] == YES_Y) {
                    found_yes = 1;
                }
                if (g_tap_n == 1 && g_tap_x[0] == YESNO_X && g_tap_y[0] == NO_Y) {
                    found_no = 1;
                }
            }
        }
        CHECK(found_yes && found_no, "YES taps (222, 109) and NO taps (222, 133)");
    }
    if (outdir != NULL) {
        run_frame(-1, -1, 0, 0, 0, 0);
        snprintf(path, sizeof(path), "%s/pc_yesno_%dx%d.bmp", outdir, g_win_w, g_win_h);
        bmp_write(path, &g_cv);
    }
}

static void test_draw(const char *outdir)
{
    size_t s;
    char path[512];

    for (s = 0; s < sizeof(k_sizes) / sizeof(k_sizes[0]); s++) {
        int mode;

        set_size(k_sizes[s][0], k_sizes[s][1], k_sizes[s][2]);
        for (mode = 0; mode < 5; mode++) {
            scene_start(mode);
            g_focus = 7;
            run_frame(-1, -1, 0, 0, 0, 0);
            CHECK(g_cv.w == g_win_w / g_win_k, "the canvas is the window over k");
            if (outdir != NULL && mode == 2) {
                snprintf(path, sizeof(path), "%s/pc_%dx%d.bmp", outdir, g_win_w, g_win_h);
                bmp_write(path, &g_cv);
            }
        }
    }
}

int main(int argc, char **argv)
{
    const char *out = getenv("UI_PC_OUT");

    BW_TEST_SETUP(argc, argv);
    bw_ds_reset();
    test_scene();
    test_layout();
    test_text();
    test_mouse(out);
    test_boxes(out);
    test_keys(out);
    test_items(out);
    test_script();
    test_popups(out);
    test_draw(out);
    bw_failures += g_fail;
    BW_REPORT("test_ui_pc");
}
