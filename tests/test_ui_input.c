/*
 * tests/test_ui_input.c -- the one-screen recipes for name entry and the
 * Xtransceiver (engine/ui/recipe_input.c).
 *
 * The recipes read the game's own structs by offset.  This test builds
 * stand-ins for those structs in memory (a game system with a proc stack,
 * a name entry screen with its page tables, a call list), runs each recipe
 * at several canvas sizes, checks that a click on a control taps the
 * bottom-screen position the game tests for it (docs/unified-view-recipes.md,
 * sections 13 and 14), and writes the pictures as BMP files when
 * UI_INPUT_OUT names a directory.
 *
 * The interface layer's hooks (hit regions, taps, presses) are the test's own.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kit.h"
#include "ui.h"
#include "port.h"
#include "port_test.h"
#include "ds_fixture.h"

extern const EidosUiRecipe *const ui_recipes_input[];
extern const u32 ui_recipes_input_count;

/* The game system's global: a pointer variable that holds the object's
 * address (the object's first word is the GameSystem). */
static void *g_gs_word;
void *bw_dsref__GameSystemGlobal = &g_gs_word;
void *bw_dsref_g_ProcSys;

/* ------------------------------------------------------------------ */
/* The interface layer's hooks                                         */
/* ------------------------------------------------------------------ */

static int g_mx = -1, g_my = -1, g_click;
static int g_tap_n, g_tap_x, g_tap_y;
static u32 g_press;
static EidosUiRect g_hit_rects[256];
static int g_hit_count, g_collect_hits;
static unsigned g_fixture_generation;

int eidos_ui_hit(EidosUiRect r)
{
    int s = 0;

    if (g_collect_hits && r.w > 0 && r.h > 0 && g_hit_count < 256) {
        g_hit_rects[g_hit_count++] = r;
    }
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

void eidos_ui_tap(int x, int y, u32 frames)
{
    (void)frames;
    g_tap_n++;
    g_tap_x = x;
    g_tap_y = y;
}

static int g_right_click;

int eidos_ui_right_click(void)
{
    return g_right_click;
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

static void w32(void *b, u32 off, u32 v) { memcpy((unsigned char *)b + off, &v, 4); }
static void w16(void *b, u32 off, u32 v) { u16 x = (u16)v; memcpy((unsigned char *)b + off, &x, 2); }

static unsigned char g_gs[0x34], g_procsys[0x20], g_proc[0x20];

/* The game system with one app proc on its stack. */
static void game_set(int overlay, void *work)
{
    ++g_fixture_generation;
    memset(g_gs, 0, sizeof(g_gs));
    memset(g_procsys, 0, sizeof(g_procsys));
    memset(g_proc, 0, sizeof(g_proc));
    w32(g_proc, 0x04, (u32)overlay);
    w32(g_proc, 0x08, 2);
    wp(g_proc, 0x14, work);
    wp(g_procsys, 0x08, g_proc);
    wp(g_gs, 0x08, g_procsys);
    g_gs_word = g_gs;
}

/* Name entry. */
static unsigned char g_nw[0x2D4], g_args[0x2C];
static unsigned char g_m0[4 + 13 * 5 * 2], g_m1[4 + 13 * 5 * 2], g_m2[4 + 13 * 5 * 2];
static unsigned char g_m3[4 + 10 * 3 * 2], g_m4[4 + 10 * 3 * 2];
static u32 g_members[5];

static void grid_fill(unsigned char *m, int w, int h, const u16 *cells)
{
    int i;

    w16(m, 0, (u32)w);
    w16(m, 2, (u32)h);
    for (i = 0; i < w * h; i++) {
        w16(m, 4 + (u32)i * 2, cells[i]);
    }
}

static void name_build(int kind, int species, int page, int shift)
{
    static const char *const r0 = "ABCDEFGHIJ _,.KLMNOPQRST _'-UVWXYZ     \x6D\x6E";
    u16 c[13 * 5], q[30];
    int i, p;
    const char *rows[3] = { "qwertyuiop", "asdfghjkl*", "zxcvbnm-**" };

    (void)r0;
    for (p = 0; p < 3; p++) {
        memset(c, 0, sizeof(c));
        for (i = 0; i < 13 * 5; i++) {
            c[i] = 0x20;
        }
        if (p < 2) {
            for (i = 0; i < 26; i++) {
                int cell = i < 10 ? i : i < 20 ? 13 + (i - 10) : 26 + (i - 20);

                c[cell] = (u16)((p == 0 ? 'A' : 'a') + i);
            }
            c[11] = ','; c[12] = '.'; c[24] = '\''; c[25] = '-';
            c[37] = 0x246D; c[38] = 0x246E;
            for (i = 0; i < 10; i++) {
                c[52 + i] = (u16)('0' + i);
            }
        } else {
            static const u16 sym[] = { ',', '.', ':', ';', '!', '?', 0x20, 0x20, 0x20, 0x20, 0x246D, 0x246E, 0x20,
                                       0x201C, 0x201D, 0x2018, 0x2019, '(', ')', 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20 };

            for (i = 0; i < 26; i++) {
                c[i] = sym[i];
            }
            for (i = 0; i < 11; i++) {
                c[39 + i] = (u16)(0x2474 + i);
                c[52 + i] = (u16)(0x247A + i);
            }
        }
        grid_fill(p == 0 ? g_m0 : p == 1 ? g_m1 : g_m2, 13, 5, c);
    }
    for (p = 0; p < 2; p++) {
        for (i = 0; i < 30; i++) {
            q[i] = (u16)(p == 0 ? rows[i / 10][i % 10] : (rows[i / 10][i % 10] >= 'a' && rows[i / 10][i % 10] <= 'z'
                                                         ? rows[i / 10][i % 10] - 32 : rows[i / 10][i % 10]));
            if (q[i] == '*') {
                q[i] = 0x203B;
            }
        }
        grid_fill(p == 0 ? g_m3 : g_m4, 10, 3, q);
    }
    wp(g_members, 0, g_m0); wp(g_members, 4, g_m1); wp(g_members, 8, g_m2);
    wp(g_members, 12, g_m3); wp(g_members, 16, g_m4);

    memset(g_nw, 0, sizeof(g_nw));
    memset(g_args, 0, sizeof(g_args));
    w32(g_args, 0x00, (u32)kind);
    w32(g_args, 0x04, (u32)species);
    w32(g_args, 0x18, 10);
    wp(g_nw, 0x2C4, g_args);
    wp(g_nw, 0x22C, g_members);
    w16(g_nw, 0x68 + 0, 'S');
    w16(g_nw, 0x68 + 2, 'a');
    w16(g_nw, 0x68 + 4, 'i');
    w16(g_nw, 0x68 + 6, 0xFFFF);
    w16(g_nw, 0x68 + 0x1E, 3);
    w32(g_nw, 0x1FC, (u32)page);
    w32(g_nw, 0x200, (u32)shift);
    w32(g_nw, 0x1D4, 1);
    w32(g_nw, 0x1D8, 0);
    game_set(194, g_nw);
}

/* The call list. */
static unsigned char g_cw[0x1FC], g_call[0x408], g_peer[4][0x64];

static void name16(unsigned char *peer, const char *s)
{
    int i;

    for (i = 0; s[i] != '\0'; i++) {
        w16(peer, 0x1A + (u32)i * 2, (u32)(unsigned char)s[i]);
    }
    w16(peer, 0x1A + (u32)i * 2, 0xFFFF);
}

static void call_build(void)
{
    static const char *const names[4] = { "Cheren", "Bianca", "Mom", "Juniper" };
    static const int level[4] = { 0, 1, 7, 9 };
    int i;

    memset(g_cw, 0, sizeof(g_cw));
    memset(g_call, 0, sizeof(g_call));
    w32(g_cw, 0x08, 2);
    wp(g_cw, 0x1F0, g_call);
    for (i = 0; i < 15; i++) {
        g_call[0x40 + i * 0x1C + 0x18] = 0xFF;
    }
    for (i = 0; i < 4; i++) {
        unsigned char *e = g_call + 0x1E4 + i * 0x24;

        memset(g_peer[i], 0, 0x64);
        name16(g_peer[i], names[i]);
        w32(e, 0x00, 1);
        w32(e, 0x08, i == 1);
        w32(e, 0x0C, i != 3);
        wp(e, 0x14, g_peer[i]);
        e[0x18] = 3;
        e[0x1F] = (unsigned char)level[i];
        g_call[0x40 + i * 0x1C + 0x18] = (unsigned char)i;
    }
    game_set(172, g_cw);
}

/* ------------------------------------------------------------------ */
/* Running a recipe                                                    */
/* ------------------------------------------------------------------ */

static void clear_tap(void)
{
    g_tap_n = 0;
    g_press = 0;
}

static int run_recipe(int idx, int w, int h, int k, const char *out, EidosUiCanvas *cv)
{
    EidosUiCtx ctx;
    EidosUiView view;
    EidosUiFrame in;
    const EidosUiRecipe *r = ui_recipes_input[idx];

    memset(&view, 0, sizeof(view));
    memset(&in, 0, sizeof(in));
    memset(cv, 0, sizeof(*cv));
    ui_begin(&ctx, cv, w, h, k);
    if (!r->match()) {
        return 0;
    }
    in.win_w = w * k;
    in.win_h = h * k;
    r->run(&ctx, &view, &in);
    if (out != NULL) {
        FILE *f = fopen(out, "wb");
        int x, y, rowb = ((w * 3 + 3) / 4) * 4;
        unsigned char hd[54] = { 'B', 'M' };

        if (f != NULL) {
            u32 size = 54 + (u32)rowb * (u32)h;

            memcpy(hd + 2, &size, 4);
            hd[10] = 54; hd[14] = 40;
            memcpy(hd + 18, &w, 4);
            { int nh = -h; memcpy(hd + 22, &nh, 4); }
            hd[26] = 1; hd[28] = 24;
            fwrite(hd, 1, 54, f);
            for (y = 0; y < h; y++) {
                unsigned char *row = (unsigned char *)calloc(1, (size_t)rowb);

                for (x = 0; x < w; x++) {
                    const unsigned char *p = cv->rgba + ((size_t)y * (size_t)w + (size_t)x) * 4;
                    int a = p[3];

                    row[x * 3 + 0] = (unsigned char)((p[2] * a + 24 * (255 - a)) / 255);
                    row[x * 3 + 1] = (unsigned char)((p[1] * a + 32 * (255 - a)) / 255);
                    row[x * 3 + 2] = (unsigned char)((p[0] * a + 40 * (255 - a)) / 255);
                }
                fwrite(row, 1, (size_t)rowb, f);
                free(row);
            }
            fclose(f);
        }
    }
    return 1;
}

static int g_fail;

#define CHECK(cond, msg) do { bw_checks++; if (!(cond)) { \
    printf("FAIL: %s (line %d)\n", msg, __LINE__); \
    bw_failures++; g_fail = 1; } } while (0)

static int click_at(int idx, int w, int h, int k, int x, int y)
{
    EidosUiCanvas cv;
    int rc;

    g_mx = x;
    g_my = y;
    g_click = 1;
    clear_tap();
    rc = run_recipe(idx, w, h, k, NULL, &cv);
    g_click = 0;
    g_mx = g_my = -1;
    free(cv.rgba);
    return rc;
}

/* Exercise each exposed control once per fixture and retain its DS tap.
 * Expected tap coordinates still come from the game's documented layout. */
static int taps_seen(int idx, int w, int h, int k, int tx, int ty)
{
    static unsigned generation;
    static int last_idx = -1, last_w, last_h, last_k, count;
    static int tap_x[256], tap_y[256];
    int i;

    if (generation != g_fixture_generation || last_idx != idx ||
        last_w != w || last_h != h || last_k != k) {
        EidosUiCanvas cv;

        generation = g_fixture_generation;
        last_idx = idx; last_w = w; last_h = h; last_k = k;
        count = g_hit_count = 0;
        g_collect_hits = 1;
        run_recipe(idx, w, h, k, NULL, &cv);
        g_collect_hits = 0;
        free(cv.rgba);
        for (i = 0; i < g_hit_count; ++i) {
            EidosUiRect r = g_hit_rects[i];
            click_at(idx, w, h, k, r.x + r.w / 2, r.y + r.h / 2);
            if (g_tap_n > 0) {
                tap_x[count] = g_tap_x;
                tap_y[count++] = g_tap_y;
            }
        }
    }
    for (i = 0; i < count; ++i)
        if (tap_x[i] == tx && tap_y[i] == ty) return 1;
    return 0;
}

static int test_name(const char *outdir)
{
    EidosUiCanvas cv;
    int sizes[3][2] = { { 426, 240 }, { 320, 240 }, { 480, 270 } };
    int s, page;
    char path[512];

    g_fail = 0;
    for (page = 0; page < 4; page++) {
        for (s = 0; s < 3; s++) {
            name_build(1, 498, page, 0);
            snprintf(path, sizeof(path), "%s/name_p%d_%dx%d.bmp", outdir != NULL ? outdir : ".", page,
                     sizes[s][0], sizes[s][1]);
            CHECK(run_recipe(0, sizes[s][0], sizes[s][1], 1, outdir != NULL ? path : NULL, &cv) == 1,
                  "name entry matches");
            free(cv.rgba);
        }
    }
    /* every character of page 0 can be clicked, and taps its own cell */
    {
        int cx, cy, hits = 0, total = 0;
        static const struct { int cx, cy; } cells[] = {
            { 0, 0 }, { 9, 0 }, { 11, 0 }, { 0, 1 }, { 9, 1 }, { 12, 1 }, { 5, 2 }, { 11, 2 }, { 12, 2 },
            { 0, 4 }, { 9, 4 } };

        name_build(1, 498, 0, 0);
        for (cx = 0; cx < (int)(sizeof(cells) / sizeof(cells[0])); cx++) {
            cy = cx;
            total++;
            hits += taps_seen(0, 426, 240, 1, 16 * cells[cy].cx + 32, 24 * cells[cy].cy + 52);
        }
        printf("name entry: %d of %d sampled keys tap their own cell\n", hits, total);
        CHECK(hits == total, "page 0 keys tap their cells");
    }
    /* the page tabs, delete, space and ok */
    name_build(1, 498, 0, 0);
    CHECK(taps_seen(0, 426, 240, 1, 36 + 24 * 3, 172), "qwerty tab taps (108, 172)");
    CHECK(taps_seen(0, 426, 240, 1, 148, 172), "delete taps (148, 172)");
    CHECK(taps_seen(0, 426, 240, 1, 204, 172), "ok taps (204, 172)");
    CHECK(taps_seen(0, 426, 240, 1, 16 * 10 + 32, 52), "space taps a blank cell");
    name_build(1, 498, 3, 0);
    CHECK(taps_seen(0, 426, 240, 1, 8 * (5 + 2 * 4) + 8, 8 * 8 + 12), "qwerty t");
    CHECK(taps_seen(0, 426, 240, 1, 8 * (7 + 2 * 6) + 8, 8 * 16 + 12), "qwerty m");
    CHECK(taps_seen(0, 426, 240, 1, 168, 172), "qwerty space bar");
    CHECK(taps_seen(0, 426, 240, 1, 40, 140), "qwerty shift");
    CHECK(taps_seen(0, 426, 240, 1, 216, 76), "qwerty delete");
    CHECK(taps_seen(0, 426, 240, 1, 212, 108), "qwerty ok");
    /* a scene that is not name entry does not match */
    game_set(70, g_nw);
    CHECK(ui_recipes_input[0]->match() == 0, "bag is not name entry");
    return g_fail;
}

static int test_xtrans(const char *outdir)
{
    EidosUiCanvas cv;
    char path[512];
    int s;
    int sizes[2][2] = { { 426, 240 }, { 480, 270 } };

    g_fail = 0;
    call_build();
    for (s = 0; s < 2; s++) {
        snprintf(path, sizeof(path), "%s/xtrans_%dx%d.bmp", outdir != NULL ? outdir : ".", sizes[s][0],
                 sizes[s][1]);
        CHECK(run_recipe(1, sizes[s][0], sizes[s][1], 1, outdir != NULL ? path : NULL, &cv) == 1,
              "xtransceiver matches the call list");
        free(cv.rgba);
    }
    /* row r taps (128, r * 32 - scroll + 16) when the contact is in range */
    CHECK(taps_seen(1, 426, 240, 1, 128, 16), "Cheren row taps (128, 16)");
    CHECK(taps_seen(1, 426, 240, 1, 128, 48), "Bianca row taps (128, 48)");
    CHECK(!taps_seen(1, 426, 240, 1, 128, 80), "Mom is out of range: no tap");
    /* another screen of the overlay is not the call list */
    w32(g_cw, 0x08, 1);
    CHECK(ui_recipes_input[1]->match() == 0, "the talk screen is left to the fallback");
    return g_fail;
}

int main(int argc, char **argv)
{
    const char *out = getenv("UI_INPUT_OUT");
    int rc = 0;

    BW_TEST_SETUP(argc, argv);
    bw_ds_reset();
    rc |= test_name(out);
    rc |= test_xtrans(out);
    if (rc == 0) {
        printf("test_ui_input: ok\n");
    }
    BW_REPORT("test_ui_input");
}
