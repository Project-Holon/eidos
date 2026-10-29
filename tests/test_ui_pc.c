/*
 * tests/test_ui_pc.c -- the one-screen recipe for the PC boxes
 * (engine/ui/recipe_pc.c).
 *
 * The recipe reads the game's own structs by offset.  This test includes the
 * recipe's source (BW_PC_TEST makes it take the game system from a cell the
 * test names and any address for a pointer), builds stand-ins for the game's
 * structs and functions in memory (a game system with a proc stack, a box
 * task, a party, two boxes of Pokemon, the window frame manager, the bottom
 * screen's pixels), and runs the recipe at several canvas sizes.  It checks
 *
 *   - the scene test (the modes it draws, the ones it leaves to the DS
 *     screens, a sub screen on top);
 *   - the layout (the party's six rows, the grid and the details fit, at
 *     every size, and do not overlap);
 *   - the slot under a canvas position, and the stylus position of a slot;
 *   - what a click makes: a tap at the rectangle the game tests (a box
 *     slot, a party row with the tray open and closed, the action list's
 *     buttons, YES and NO, the box arrows, the wheel);
 *   - a drag: the stylus follows the slot under the mouse;
 *   - the text decoder (the message archive's cipher and control codes).
 *
 * The interface layer's hooks (hit regions, taps, presses) are the test's own.
 * UI_PC_OUT names a directory for the pictures (BMP files).
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

static int g_mx = -1, g_my = -1, g_click, g_wheel;
static int g_tap_n, g_tap_x[64], g_tap_y[64];
static u32 g_press;

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
    if (g_mx >= r.x && g_my >= r.y && g_mx < r.x + r.w && g_my < r.y + r.h) {
        return g_wheel;
    }
    return 0;
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

static int g_right_click;

int eidos_ui_right_click(void)
{
    return g_right_click;
}

int eidos_ui_keys_lit(void)
{
    return 1;
}

void eidos_ui_want_text(int on)
{
    (void)on;
}

void eidos_ui_press(u32 keys, u32 frames)
{
    (void)frames;
    g_press |= keys;
}

/* ------------------------------------------------------------------ */
/* The fake game                                                       */
/* ------------------------------------------------------------------ */

typedef struct FakeMon {
    int exists, egg, species, form, gender, level, item, ability;
    int moves[4], type1, type2, hp, maxhp;
    u16 nick[12];
} FakeMon;

static FakeMon g_box[2][30];
static FakeMon g_party[6];
static int g_party_n;
static u8 g_header[0x3E0];
static s8 g_frame_x[12], g_frame_y[12];

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

void BGWINFRM_GetPos(void *wk, u32 idx, s8 *x, s8 *y)
{
    (void)wk;
    *x = g_frame_x[idx];
    *y = g_frame_y[idx];
}

static void w32(void *b, u32 off, u32 v) { memcpy((unsigned char *)b + off, &v, 4); }

static unsigned char g_gs[0x34], g_procsys[0x20], g_proc[0x20], g_task[0x30], g_params[0x40];
static unsigned char g_nested[0x20], g_work[0xA200], g_winmgr[16], g_gd[16], g_partyobj[16];
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
    wp(g_task, 0x00, g_params);
    wp(g_task, 0x04, g_nested);
    g_task[0x14] = 0;               /* box 1 */
    g_task[0x15] = 8;
    g_task[0x16] = 0;               /* cursor on slot 0 */
    g_task[0x17] = 0xFF;
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
    g_pc_test_gs_cell = (u8 *)g_gs;
    /* the gs cell is a pointer to the game system */
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

static void run_frame(int w, int h, int k, int mx, int my, int click, int wheel)
{
    EidosUiFrame in;
    EidosUiView view;

    memset(&in, 0, sizeof(in));
    in.win_w = w;
    in.win_h = h;
    in.frame = ++g_frame;
    in.screen_rgba[1] = g_bottom_px;
    in.mouse.x = mx * k;
    in.mouse.y = my * k;
    g_mx = mx;
    g_my = my;
    g_click = click;
    g_wheel = wheel;
    memset(&view, 0, sizeof(view));
    ui_begin(&g_ctx, &g_cv, w / k, h / k, k);
    if (recipe_pc.match()) {
        recipe_pc.run(&g_ctx, &view, &in);
    }
}

/* A frame with a click, then what the click queued is sent. */
static void run_click(int w, int h, int k, int mx, int my)
{
    run_frame(w, h, k, mx, my, 1, 0);
    pc_queue_run();
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

/* ------------------------------------------------------------------ */
/* The checks                                                          */
/* ------------------------------------------------------------------ */

static const int k_sizes[][3] = {
    { 1280, 720, 3 }, { 1920, 1080, 4 }, { 1280, 800, 3 }, { 1024, 768, 3 },
    { 2560, 1080, 4 }, { 960, 540, 2 }, { 800, 600, 2 },
};

static void test_scene(void)
{
    world_build();
    game_set(170, 0, 11);
    CHECK(recipe_pc.match() == 1, "deposit mode is drawn");
    game_set(170, 1, 11);
    CHECK(recipe_pc.match() == 1, "withdraw mode is drawn");
    game_set(170, 2, 21);
    CHECK(recipe_pc.match() == 1, "move mode is drawn");
    game_set(170, 5, 11);
    CHECK(recipe_pc.match() == 0, "the item mode is left to the DS screens");
    game_set(170, 4, 11);
    CHECK(recipe_pc.match() == 0, "the battle box is left to the DS screens");
    game_set(170, 0, 85);
    CHECK(recipe_pc.match() == 0, "the marking panel is left to the DS screens");
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
        int cw = k_sizes[s][0] / k_sizes[s][2], ch = k_sizes[s][1] / k_sizes[s][2], i, slot;

        pc_layout(&l, cw, ch);
        CHECK(l.pitch >= 22 && l.pitch <= 36, "the grid cell is a sane size");
        CHECK(l.party.x + l.party.w <= l.box.x, "the party column ends before the box");
        if (!l.narrow) {
            CHECK(l.box.x + l.box.w <= l.det.x, "the box ends before the details");
            CHECK(l.det.x + l.det.w <= cw, "the details stay in the canvas");
            CHECK(l.det.w >= 100, "the details are wide enough");
        } else {
            CHECK(l.det.x >= l.box.x && l.det.x + l.det.w <= l.box.x + l.box.w,
                  "the details are inside the box panel");
            CHECK(l.det.y >= l.gy + 5 * l.pitch && l.det.h >= 44, "the details are under the grid, 44 high or more");
        }
        CHECK(l.box.x + l.box.w <= cw, "the box stays in the canvas");
        CHECK(l.gx + 6 * l.pitch <= l.box.x + l.box.w, "the grid fits the box panel");
        CHECK(l.gy + 5 * l.pitch <= l.box.y + l.box.h, "the grid fits the box panel (height)");
        CHECK(l.row_y[5] + l.row_h <= l.hint_y, "the sixth party row is above the hints");
        CHECK(l.row_y[0] >= l.top, "the first party row is below the title");
        for (i = 1; i < 6; i++) {
            CHECK(l.row_y[i] >= l.row_y[i - 1] + l.row_h, "the party rows do not overlap");
        }
        for (slot = 0; slot < 30; slot++) {
            int cx = l.gx + (slot % 6) * l.pitch + l.pitch / 2;
            int cy = l.gy + (slot / 6) * l.pitch + l.pitch / 2;

            CHECK(pc_slot_at(&l, cx, cy) == slot, "the middle of a cell is its slot");
        }
        for (slot = 0; slot < 6; slot++) {
            CHECK(pc_slot_at(&l, l.party.x + 4, l.row_y[slot] + 4) == SLOT_PARTY0 + slot,
                  "a party row is its slot");
        }
        CHECK(pc_slot_at(&l, -1, -1) == -1, "outside is no slot");
        CHECK(pc_slot_at(&l, l.det.x + 3, l.det.y + 3) == -1 || l.narrow, "the details are no slot");
    }
}

static void test_stylus(void)
{
    int x, y, slot;

    for (slot = 0; slot < 30; slot++) {
        CHECK(slot_stylus(0, slot, &x, &y) == 1, "a box slot always has a control");
        /* the game's grid: column ranges 12-35, 36-59 ..., rows 40-63, 64-87 ... */
        CHECK(x >= 12 + 24 * (slot % 6) && x <= 35 + 24 * (slot % 6), "x is in the game's column");
        CHECK(y >= 40 + 24 * (slot / 6) && y <= 63 + 24 * (slot / 6), "y is in the game's row");
    }
    CHECK(slot_stylus(1, 0, &x, &y) == 0 && slot_stylus(1, 3, &x, &y) == 0,
          "the box slots under the open tray (columns 0 to 3) have no control");
    CHECK(slot_stylus(1, 4, &x, &y) == 1 && slot_stylus(1, 5, &x, &y) == 1,
          "the box slots beside the open tray (columns 4 and 5) do");
    CHECK(slot_stylus(0, SLOT_PARTY0, &x, &y) == 0, "a party slot has no control while the tray is closed");
    CHECK(slot_stylus(1, SLOT_PARTY0 + 1, &x, &y) == 1 && x >= 66 && x <= 89 && y >= 64 && y <= 87,
          "party slot 2 is the second tray slot (66-89, 64-87)");
    CHECK(slot_stylus(1, SLOT_PARTY0 + 4, &x, &y) == 1 && x >= 30 && x <= 53 && y >= 120 && y <= 143,
          "party slot 5 is the fifth tray slot (30-53, 120-143)");
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
        u16 key = (u16)((i + 3u) * 0x2983u);
        u8 *e = bank + 16 + 4 + 8 * i;

        e[0] = (u8)off;
        e[1] = (u8)(off >> 8);
        e[4] = (u8)lens[i];
        for (j = 0; j < lens[i]; j++) {
            u16 u = (u16)(plain[i][j] ^ key);

            bank[16 + off + 2 * j] = (u8)u;
            bank[16 + off + 2 * j + 1] = (u8)(u >> 8);
            key = rotl3(key);
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

static int taps_have(int x, int y)
{
    int i;

    for (i = 0; i < g_tap_n && i < 64; i++) {
        if (g_tap_x[i] == x && g_tap_y[i] == y) {
            return 1;
        }
    }
    return 0;
}

static void run_plain(int w, int h, int k, int n)
{
    while (n-- > 0) {
        run_frame(w, h, k, -1, -1, 0, 0);
    }
}

static void flush_queue(void)
{
    int i;

    for (i = 0; i < 80; i++) {
        g_now++;
        pc_queue_run();
    }
}

static void test_clicks(const char *outdir)
{
    int w = 1920, h = 1080, k = 4;
    PcLay l;
    char path[512];
    int slot_x, slot_y, gx, gy;

    world_build();
    game_set(170, 2, 21);
    frames_set(0, 0);
    screen_clear();
    pc_layout(&l, w / k, h / k);

    /* a plain frame taps nothing */
    g_tap_n = 0;
    run_frame(w, h, k, -1, -1, 0, 0);
    CHECK(g_tap_n == 0, "no click, no tap");
    if (outdir != NULL) {
        snprintf(path, sizeof(path), "%s/pc_move_%dx%d.bmp", outdir, w, h);
        bmp_write(path, &g_cv);
    }

    /* a click on box slot 7 taps the game's cell */
    slot_x = l.gx + 1 * l.pitch + l.pitch / 2;
    slot_y = l.gy + 1 * l.pitch + l.pitch / 2;
    g_tap_n = 0;
    run_frame(w, h, k, slot_x, slot_y, 1, 0);
    flush_queue();
    grid_pos(7, &gx, &gy);
    CHECK(g_tap_n == 1 && taps_have(gx, gy), "a click on box slot 7 taps (47, 75)");

    /* a click on the party's second row with the tray closed: the PARTY button, then the tray slot */
    g_tap_n = 0;
    run_click(w, h, k, l.party.x + 10, l.row_y[1] + 10);
    run_plain(w, h, k, 24);
    CHECK(taps_have(PARTY_BTN_X, PARTY_BTN_Y), "the PARTY PKMN button is tapped first");
    CHECK(!taps_have(77, 75), "the tray slot is not tapped before the tray is open");
    frames_set(0, 1);                                   /* the tray opens */
    run_plain(w, h, k, 24);
    CHECK(taps_have(77, 75), "the tray slot is tapped after the tray has opened");

    /* with the tray open it is one tap */
    frames_set(0, 1);
    g_tap_n = 0;
    run_frame(w, h, k, l.party.x + 10, l.row_y[0] + 10, 1, 0);
    flush_queue();
    CHECK(g_tap_n == 1 && taps_have(41, 67), "an open tray: one tap on tray slot 1 (41, 67)");

    /* a box slot under the open tray: the move mode closes the tray first, the deposit mode ignores it */
    frames_set(0, 1);
    g_tap_n = 0;
    run_plain(w, h, k, 24);                                    /* settled */
    g_tap_n = 0;
    run_click(w, h, k, l.gx + 3, l.gy + 3);                    /* slot 0 */
    run_plain(w, h, k, 24);
    CHECK(taps_have(PARTY_BTN_X, PARTY_BTN_Y) && !taps_have(23, 51), "slot 0 under the tray: the button first");
    frames_set(0, 0);                                   /* the tray closes */
    run_plain(w, h, k, 24);
    CHECK(taps_have(23, 51), "slot 0 under the tray: then the slot");
    game_set(170, 0, 11);
    frames_set(0, 1);
    g_tap_n = 0;
    run_click(w, h, k, l.gx + 3, l.gy + 3);
    flush_queue();
    CHECK(g_tap_n == 0, "slot 0 under the deposit mode's tray: no tap");
    game_set(170, 2, 21);

    /* the box arrows and the wheel */
    frames_set(0, 0);
    g_tap_n = 0;
    run_frame(w, h, k, l.box.x + 6 + 8, l.box.y + 5 + 7, 1, 0);
    flush_queue();
    CHECK(g_tap_n == 1 && taps_have(16, 28), "the left arrow taps (16, 28)");
    g_tap_n = 0;
    run_frame(w, h, k, l.box.x + l.box.w - 6 - 8, l.box.y + 5 + 7, 1, 0);
    flush_queue();
    CHECK(g_tap_n == 1 && taps_have(151, 28), "the right arrow taps (151, 28)");
    g_frame += 100;
    g_wheel_at = 0;
    g_tap_n = 0;
    run_frame(w, h, k, slot_x, slot_y, 0, -1);
    flush_queue();
    CHECK(g_tap_n == 1 && taps_have(151, 28), "the wheel down is the next box");
    g_frame += 100;
    g_tap_n = 0;
    run_frame(w, h, k, slot_x, slot_y, 0, 1);
    flush_queue();
    CHECK(g_tap_n == 1 && taps_have(16, 28), "the wheel up is the previous box");

    /* the action list: six buttons, the second one (SUMMARY) is at y 124 - 24 * 3 = 52 + ... */
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
        g_tap_n = 0;
        run_frame(w, h, k, -1, -1, 0, 0);
        CHECK(g_w.list_shown == 1 && g_w.list_n == 6, "six list buttons are counted from the pixels");
        CHECK(g_w.list_ids[0] == 81 && g_w.list_ids[5] == 80, "the move mode's list ids come from the game's table");
        if (outdir != NULL) {
            snprintf(path, sizeof(path), "%s/pc_list_%dx%d.bmp", outdir, w, h);
            bmp_write(path, &g_cv);
        }
        /* click the second button: find it by sweeping the canvas for the click */
        {
            int found = 0, px_, py_;

            for (py_ = 0; py_ < h / k && !found; py_ += 2) {
                for (px_ = l.box.x; px_ < l.box.x + l.box.w && !found; px_ += 2) {
                    g_tap_n = 0;
                    run_click(w, h, k, px_, py_);
                    if (g_tap_n == 1 && g_tap_x[0] == 212 && g_tap_y[0] == 148 - 24 * 4) {
                        found = 1;
                    }
                }
            }
            if (!found) {
                int lx = l.box.x + l.box.w - 78 - 6, ly = l.box.y + l.box.h - 8 - 6 * 20;

                if (ly + 6 * 20 > l.bar.y - 6) {
                    ly = l.bar.y - 6 - 6 * 20;
                }
                g_tap_n = 0;
                run_click(w, h, k, lx + 10, ly + 25);
                printf("  list probe at (%d,%d): taps %d first (%d,%d) list_shown %d n %d%c", lx + 10, ly + 25,
                       g_tap_n, g_tap_x[0], g_tap_y[0], g_w.list_shown, g_w.list_n, 10);
            }
            CHECK(found, "the second list button taps (212, 52)");
        }
    }

    /* YES and NO */
    frames_set(0, 0);
    screen_clear();
    screen_msg(252);
    screen_yesno();
    run_frame(w, h, k, -1, -1, 0, 0);
    CHECK(g_w.yesno == 1, "the two-choice menu is found (the cursor's button is lighter)");
    {
        int found_yes = 0, found_no = 0, px_, py_;

        for (py_ = l.box.y; py_ < l.box.y + l.box.h; py_ += 2) {
            for (px_ = l.box.x; px_ < l.box.x + l.box.w; px_ += 2) {
                g_tap_n = 0;
                run_click(w, h, k, px_, py_);
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
        run_frame(w, h, k, -1, -1, 0, 0);
        snprintf(path, sizeof(path), "%s/pc_yesno_%dx%d.bmp", outdir, w, h);
        bmp_write(path, &g_cv);
    }
}

static void test_draw(const char *outdir)
{
    size_t s;
    char path[512];

    world_build();
    for (s = 0; s < sizeof(k_sizes) / sizeof(k_sizes[0]); s++) {
        int mode;

        for (mode = 0; mode < 3; mode++) {
            game_set(170, mode, 11);
            frames_set(0, mode == 0);
            screen_clear();
            g_tap_n = 0;
            g_task[0x16] = 7;                   /* the cursor on Pika */
            run_frame(k_sizes[s][0], k_sizes[s][1], k_sizes[s][2], -1, -1, 0, 0);
            CHECK(g_cv.w == k_sizes[s][0] / k_sizes[s][2], "the canvas is the window over k");
            if (outdir != NULL && mode == 2) {
                snprintf(path, sizeof(path), "%s/pc_%dx%d.bmp", outdir, k_sizes[s][0], k_sizes[s][1]);
                bmp_write(path, &g_cv);
            }
        }
    }
}

static char g_drag_env[96];

/* The drag script (EIDOS_PC_DRAG) must be in the environment before the first
 * frame of the recipe: it starts over box slot 7 and ends over slot 20. */
static void drag_env_set(void)
{
    const int w = 1920, h = 1080, k = 4;
    PcLay l;

    pc_layout(&l, w / k, h / k);
    snprintf(g_drag_env, sizeof(g_drag_env), "EIDOS_PC_DRAG=100000:%d:%d:%d:%d:20",
             (l.gx + l.pitch + l.pitch / 2) * k, (l.gy + l.pitch + l.pitch / 2) * k,
             (l.gx + 2 * l.pitch + l.pitch / 2) * k, (l.gy + 3 * l.pitch + l.pitch / 2) * k);
    putenv(g_drag_env);
}

static void test_drag(void)
{
    int w = 1920, h = 1080, k = 4, f, x7, y7, x20, y20, after;

    world_build();
    game_set(170, 2, 21);
    frames_set(0, 0);
    screen_clear();
    g_frame = 99990;
    g_tap_n = 0;
    for (f = 0; f < 440; f++) {     /* the pick-up waits up to 400 frames */
        run_frame(w, h, k, -1, -1, 0, 0);
        if (g_frame == 100022) {
            after = g_tap_n;
            (void)after;
        }
    }
    grid_pos(7, &x7, &y7);
    grid_pos(20, &x20, &y20);
    /* the move mode drags through the game's hand: a tap that picks the
     * Pokemon up, a tap that puts it down */
    /* (the test's game never puts it in the hand: the pick-up tap is sent
     * again a few times, then the drop goes) */
    CHECK(g_tap_n >= 2 && g_tap_n <= 4, "a move-mode drag is a pick-up tap and a drop tap");
    CHECK(g_tap_x[0] == x7 && g_tap_y[0] == y7, "the first tap is on the slot the drag began on");
    CHECK(g_tap_x[g_tap_n - 1] == x20 && g_tap_y[g_tap_n - 1] == y20,
          "the last tap is on the slot the drag ended on");
    /* the frames after the release add nothing */
    g_tap_n = 0;
    for (f = 0; f < 10; f++) {
        run_frame(w, h, k, -1, -1, 0, 0);
    }
    CHECK(g_tap_n == 0, "no stylus after the button went up");
}

int main(int argc, char **argv)
{
    const char *out = getenv("UI_PC_OUT");

    BW_TEST_SETUP(argc, argv);
    bw_ds_reset();
    drag_env_set();
    test_scene();
    test_layout();
    test_stylus();
    test_text();
    test_clicks(out);
    test_draw(out);
    test_drag();
    bw_failures += g_fail;
    BW_REPORT("test_ui_pc");
}
