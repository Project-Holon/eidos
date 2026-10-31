/*
 * engine/platform/mod_options_proc.c -- the settings screen, drawn the way the
 * game draws its own Options screen.
 *
 * It is a game process (the {init, main, end} table the process manager runs,
 * src/matched/arm9/func_020315F0.c) written in port C that calls the game's own
 * graphics, window, text and sprite functions.  The set-up follows the Options
 * screen of overlay 68 (asm/overlays/ov068.s, func_021B9980 / func_021B9850,
 * and the matched func_021B9E28 for the sprites): the same VRAM banks, the same
 * six background layers, the same graphics archive (a/0/2/7, "arc 0x1B") with
 * the same members, bars 24 pixels apart, and the same sprite bank.  Nothing
 * of the game's art is in this file: it is loaded from the player's own data
 * at run time.
 *
 * The title menu starts it (stubs/ov/reimpl_ov088_func_021BA014.c) and it hands
 * back to the title menu when the player leaves.  What the screen shows and how
 * its buttons behave is engine/platform/settings_ui.c; this file only draws.
 *
 * The file holds one more thing of the port's own: the confirmation the save
 * handoff asks the player with, the one question the port asks outside its
 * own menu (the end of the file).
 *
 * Layout (docs/settings-design.md): the main screen is a list of six bars, the
 * sixth the grey one; the other screen is the dark grid with the help text of
 * the highlighted row.
 */

#include <stdio.h>
#include <string.h>

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef signed short s16;

/* The ARM9 matched files spell some functions sub_<addr>; the generated
 * header of overlay 88 (ovl/boot/include/ov088.h) renames the calls, and so
 * does this list (unless the build's gamespell.h already names the
 * function: its name wins).  The title menu calls all of these. */
#ifndef func_020056A0
#define func_020056A0 sub_020056A0
#endif
#ifndef func_020061E4
#define func_020061E4 sub_020061E4
#endif
#ifndef func_0201C440
#define func_0201C440 sub_0201C440
#endif
#ifndef func_0201C4C0
#define func_0201C4C0 sub_0201C4C0
#endif
#ifndef func_0201C724
#define func_0201C724 sub_0201C724
#endif
#ifndef func_0201D304
#define func_0201D304 sub_0201D304
#endif
#ifndef func_0203064C
#define func_0203064C sub_0203064C
#endif
#ifndef func_02030EAC
#define func_02030EAC sub_02030EAC
#endif
#ifndef func_0203101C
#define func_0203101C sub_0203101C
#endif
#ifndef func_020310C4
#define func_020310C4 sub_020310C4
#endif
#ifndef func_02031140
#define func_02031140 sub_02031140
#endif
#ifndef func_020315D4
#define func_020315D4 sub_020315D4
#endif
#ifndef func_020362DC
#define func_020362DC sub_020362DC
#endif
#ifndef func_0203633C
#define func_0203633C sub_0203633C
#endif
#ifndef func_02040588
#define func_02040588 sub_02040588
#endif
#ifndef func_020409B4
#define func_020409B4 sub_020409B4
#endif
#ifndef func_02041B6C
#define func_02041B6C sub_02041B6C
#endif
#ifndef func_0204335C
#define func_0204335C sub_0204335C
#endif
#ifndef func_020434DC
#define func_020434DC sub_020434DC
#endif
#ifndef func_02043534
#define func_02043534 sub_02043534
#endif
#ifndef func_02043598
#define func_02043598 sub_02043598
#endif
#ifndef func_020435F4
#define func_020435F4 sub_020435F4
#endif
#ifndef func_02043B5C
#define func_02043B5C sub_02043B5C
#endif
#ifndef func_02045088
#define func_02045088 sub_02045088
#endif
#ifndef func_020450C8
#define func_020450C8 sub_020450C8
#endif
#ifndef func_020450F0
#define func_020450F0 sub_020450F0
#endif
#ifndef func_020452E8
#define func_020452E8 sub_020452E8
#endif
#ifndef func_02045334
#define func_02045334 sub_02045334
#endif
#ifndef func_02045374
#define func_02045374 sub_02045374
#endif
#ifndef func_02045730
#define func_02045730 sub_02045730
#endif
#ifndef func_02045770
#define func_02045770 sub_02045770
#endif
#ifndef func_020457B0
#define func_020457B0 sub_020457B0
#endif
#ifndef func_02045808
#define func_02045808 sub_02045808
#endif
#ifndef func_020490F4
#define func_020490F4 sub_020490F4
#endif
#ifndef func_02049658
#define func_02049658 sub_02049658
#endif
#ifndef func_020498F4
#define func_020498F4 sub_020498F4
#endif
#ifndef func_02049B40
#define func_02049B40 sub_02049B40
#endif
#ifndef func_02049B68
#define func_02049B68 sub_02049B68
#endif
#ifndef func_0204A48C
#define func_0204A48C sub_0204A48C
#endif
#ifndef func_0204A5AC
#define func_0204A5AC sub_0204A5AC
#endif
#ifndef func_0204A6C8
#define func_0204A6C8 sub_0204A6C8
#endif
#ifndef func_0204A8D4
#define func_0204A8D4 sub_0204A8D4
#endif
#ifndef func_0204ABF0
#define func_0204ABF0 sub_0204ABF0
#endif
#ifndef func_0204ADA4
#define func_0204ADA4 sub_0204ADA4
#endif
#ifndef func_0204AF28
#define func_0204AF28 sub_0204AF28
#endif
#ifndef func_0204AFD8
#define func_0204AFD8 sub_0204AFD8
#endif
#ifndef func_0204B100
#define func_0204B100 sub_0204B100
#endif
#ifndef func_0204B1CC
#define func_0204B1CC sub_0204B1CC
#endif
#ifndef func_0204B270
#define func_0204B270 sub_0204B270
#endif
#ifndef func_0204B294
#define func_0204B294 sub_0204B294
#endif
#ifndef func_0204B3B4
#define func_0204B3B4 sub_0204B3B4
#endif
#ifndef func_0204B404
#define func_0204B404 sub_0204B404
#endif
#ifndef func_0204B454
#define func_0204B454 sub_0204B454
#endif
#ifndef func_0204B6F4
#define func_0204B6F4 sub_0204B6F4
#endif
#ifndef func_020434CC
#define func_020434CC sub_020434CC
#endif
#ifndef func_0204B92C
#define func_0204B92C sub_0204B92C
#endif

/* ---- the game's functions (ARM9, all matched; see the symbol tables) ---- */
extern void func_0203064C(u32, u32, u32);
extern void *func_0203159C(void *, u32, u32);
extern void func_020315D4(void *);
extern void func_020306F0(u32);
extern void func_020312F8(int, const void *, void *);
extern void func_020312CC(int, const void *, void *);      /* PROCSYS_PushGlobal */
extern void func_02082BCC(void *, int, int);
extern void *func_02030734(u32, u32, u32, const char *, u32); /* GFL_HEAP_Alloc */
extern void func_020307B0(void *);                         /* GFL_HEAP_Free */
extern void func_02045A38(void *, u16 *, u32);             /* STRBUF_ToChars */

extern void func_0204335C(u32);
extern void func_020433E0(const void *);
extern void func_0204363C(u32);
extern void func_0204361C(void);
extern void func_020434CC(void);
extern void func_02043588(void);
extern void func_020434DC(u32, u32);
extern void func_02043598(u32, u32);
extern void func_0201DD60(void);
extern void func_0201DD78(u32, u32, u32);
extern void func_0207D534(volatile u16 *, int);
extern void func_02043534(u32);
extern void func_020435F4(u32);

extern void func_0203F8F4(u32);
extern void func_0203FC28(const void *);
extern void func_0203FCA0(int, const void *, int);
extern void func_020414C4(u32);
extern void func_020414EC(u32);
extern void func_020409B4(u32);
extern void func_02040588(u32, u32);
extern void func_02040C14(u32, u32, u32, u32);
extern void func_02041B6C(u32);
extern void func_020419E4(void);
extern void func_020450C8(void);
extern void func_0203F9B4(void);

extern void *func_020490F4(u32, u32);
extern void func_02049238(void *);
extern void func_02049B68(void *, u32, u32, u32, u32, u32);
extern void func_02049BDC(void *, u32, u32, u32, u32, u32, u32);
extern void func_02049B40(u32, u32, u32, u32, u32, u32);
extern void func_02049658(void *, u32, u32, u32, u32, u32, u32);
extern void func_020498F4(void *, u32, u32, u32, u32, u32, u32);

extern void func_02045088(u32);
extern u32 func_020450F0(u32, u32, u32, u32, u32, u32, u32);
extern void *func_02045770(u32);
extern void func_02043B5C(void *, u32);
extern void func_02043AF4(void *, u32, u32, u32, u16, u8);   /* BMP_FillRect */
extern void func_02045374(u32);
extern void func_02045334(u32);
extern u8 func_02045730(u32);
extern void func_020452E8(u32);
extern void *func_020457B0(u32, u32);
extern void func_02045994(void *, const u16 *, u32);
extern void func_02045808(void *);
extern u32 func_0201D7EC(int, int, int, int, u32);
extern void func_0201D83C(u32);
extern u32 func_0201C440(u32);
extern void func_0201C4C0(u32);
extern void func_0201C724(u32, void *, int, int, void *, u32, u32);
extern u32 func_0201D304(void *, u32, u32);

extern u32 func_0203101C(u32, u32, u32, u32);
extern void func_020310C4(u32);
extern void func_02030EAC(u32);
extern void func_02031140(u32);
extern u32 func_020056A0(void (*)(u32, u32), u32, u32);

extern u32 func_020362DC(void);
extern u32 func_0203633C(void);
extern void func_020061E4(u32);
extern void func_0204E720(u32, int, int, int);
extern int func_0204E804(void);

/* the sprite system's per-frame work (the screen has no sprites of its own,
 * but the game's objects, if any, are drawn and sent with these) */
extern void func_0204A600(void);
extern void func_0204A648(void);

/* ---- the port ---- */
extern void *bw_ds_ptr(u32);
extern void bw_io_write16(u32, u16);
extern void bw_io_write32(u32, u32);
extern u32 bw_io_read32(u32);
extern int bw_mod_title_return_ov(void);
extern const void *bw_mod_title_return_table(void);

extern void eidos_sui_enter(void);
extern int eidos_ui_options_proc_up;    /* engine/ui/recipe_settings.c */
extern void eidos_sui_leave(void);
extern int eidos_sui_input(unsigned pressed, unsigned held);
extern int eidos_sui_row_count(void);
extern void eidos_sui_row_text(int row, char *label, int nl, char *value, int nv);
extern int eidos_sui_cursor_row(void);
extern int eidos_sui_has_back(void);
extern int eidos_sui_tab(void);
extern int eidos_sui_tab_count(void);
extern const char *eidos_sui_tab_name(int i);
extern int eidos_sui_scroll(int *first, int *shown, int *total);
extern int eidos_sui_row_choices(int row, int *active, char names[3][20]);
extern int eidos_sui_row_adjustable(int row);
extern int eidos_sui_touch(int row, int zone);
extern int eidos_settings_touch(int *x, int *y);
extern int eidos_sui_help_count(void);
extern void eidos_sui_help_line(int i, char *out, int n);
extern int eidos_sui_keyboard(int *kind, int *max, char *text, int n);
extern void eidos_sui_keyboard_done(const char *text, int ok);
extern const char *eidos_sui_keyboard_prompt(void);

#define OPT_HEAP 0xb
#define OPT_HEAP_SIZE 0x80000u
#define OPT_ROWS 6              /* bars on the list screen */

/* DISPCNT / DISPSTAT style registers are written through the port's I/O. */
#define REG_DISPCNT_MAIN 0x04000000u
#define REG_DISPCNT_SUB  0x04001000u

/* VRAM banks of the Options screen (d_021BB598, overlay 68): the fields of
 * struct DispVram in src/matched/arm9/func_020433E0.c. */
static const u32 k_banks[12] = {
    1, 0, 4, 0, 2, 0, 0x100, 0, 0, 0, 0x200010, 0x200010
};

/* d_021BB540: the background system's mode, the same four words as the title's. */
static const u32 k_bgmode[4] = { 1, 0, 0, 0 };

/* The six layers (d_021BB614, overlay 68): the first word is the layer, then
 * the eight words func_0203FCA0 takes, then its mode. */
typedef struct LayerCfg {
    int layer;
    u32 cfg[8];
    int mode;
} LayerCfg;

static const LayerCfg k_layers[6] = {
    { 0, { 0, 0, 0x800, 0, 0x1000001, 0x8000, 0x100, 0 }, 0 },
    { 1, { 0, 0, 0x1000, 0, 0x2010002, 0x8000, 0x200, 0 }, 0 },
    { 2, { 0, 0, 0x1000, 0, 0x4030002, 0x8000, 0x300, 0 }, 0 },
    { 3, { 0, 0, 0x800, 0, 0x5050001, 0x8000, 0, 0 }, 0 },
    { 4, { 0, 0, 0x800, 0, 0x1000001, 0x8000, 0, 0 }, 0 },
    { 5, { 0, 0, 0x800, 0, 0x2010001, 0x8000, 0, 0 }, 0 },
};

/* Where the Options screen puts the choices of a row (d_021BB704 and the text
 * positions beside it): three choices at 112, 160 and 208, two at 112 and 184;
 * eidos's names are longer than SLOW, MID and FAST, so three sit at 100, 148 and
 * 196. */
static const int k_text3[3] = { 86, 126, 166 };
static const int k_text2[2] = { 100, 160 };
#define VALUE_X 100            /* where a value that is not a choice starts */

/*
 * The list's margins: the bars run from tile column BAR_L to BAR_R (the
 * game's art ran edge to edge), the row text sits inside them, and the
 * scrollbar has its own column right of the bars.
 */
#define BAR_L    1             /* the bars' first tile column */
#define BAR_R    28            /* their last */
#define ROW_X    (BAR_L + 1)   /* the row text's window */
#define ROW_W    (BAR_R - BAR_L - 1)
#define ROW_PX   (ROW_W * 8)   /* its width in pixels */
#define SCROLL_X (BAR_R + 1)   /* the scrollbar's column */
#define ARROW_W 14

typedef struct OptWork {
    u32 heap;
    u32 tcb;                    /* task list, run every frame */
    u32 vtask;                  /* the vblank task */
    u32 font, print;
    u32 win_head, win_row[OPT_ROWS], win_help, win_scroll;
    int built;
    int state_dirty;
    int ending;
    int tick;                   /* frames, for the pulse of the selected bar */
    int flash;                  /* frames left of the flash after a change */
    int touch_was;              /* the stylus was down on the frame before */
    struct KbArgs *kb;          /* the game's keyboard, while it is open */
    int marq;                   /* a value too long for its row: where it starts */
    int marq_tick;              /* frames to the next step (below 0: the first wait) */
    int marq_on;                /* the selected row's value moves */
    int marq_row;               /* the row it belongs to */
    int asked;                  /* the question before the first frame was asked */
} OptWork;

/* A value too long for the selected row moves along, one letter every
 * MARQ_STEP frames, after a wait of MARQ_WAIT frames; the others are cut. */
#define MARQ_STEP 10
#define MARQ_WAIT 45
#define MARQ_GAP  "   "

/*
 * The game's keyboard: the name entry screen (overlay 194), the one the
 * game opens for the player's name.  It takes the argument block of
 * src/game/ov010/namein_args.h; this is the same block.
 */
typedef struct KbArgs {
    int kind;                   /* 0 a player's name, 4 a plain text      */
    u16 species;                /* the player's gender for kind 0; 0xFFFF: none */
    u16 traits;
    void *mon;
    void *boxName;
    void *boxManager;
    int boxIndex;
    int maxLen;
    int canceled;
    void *name;                 /* the text: the start, then the answer   */
    void *saveIntr;
    void *pokedex;
} KbArgs;

#define NAMEIN_OVERLAY   0xC2u          /* ov194 */
#define NAMEIN_PROC_DATA 0x021F5580u    /* gNameInProcData */

/* Text colours, bank 15 of both screens. */
#define TC_TEXT   0xfu      /* white */
#define TC_SHADOW 0xeu
#define TC_DIM    0x2u      /* a choice that is not the active one */
#define TC_ACTIVE 0x3u      /* the active choice of a row */
#define TC_LIVE   0x4u      /* the same on the selected row: it pulses */
#define TC_HEAD   0x5u      /* the section title */

static OptWork *g_w;

/* ---- text ---- */

static void *ascii_obj(const char *t, int n)
{
    u16 buf[96];
    void *o;
    int i;

    for (i = 0; i < n && i < 94; i++) {
        buf[i] = (u8)t[i];
    }
    buf[i] = 0xFFFF;
    o = func_020457B0((u32)(i + 1), OPT_HEAP);
    func_02045994(o, buf, (u32)(i + 1));
    return o;
}

/* One text in a window: at x, or ending at `right` when right >= 0; cut at
 * the end until it fits `room` pixels.  Returns the width drawn. */
static int put_text(u32 win, const char *t, int x, int right, int room, int y,
                    u32 fg, u32 shadow)
{
    int n = 0, w;
    void *o;

    while (t[n]) {
        n++;
    }
    if (n == 0) {
        return 0;
    }
    o = ascii_obj(t, n);
    w = (int)func_0201D304(o, g_w->font, 0);
    while (w > room && n > 1) {
        func_02045808(o);
        n--;
        o = ascii_obj(t, n);
        w = (int)func_0201D304(o, g_w->font, 0);
    }
    if (right >= 0) {
        x = right - w;
    }
    /* the colours go with the print: letter, shadow and back, five bits each */
    func_0201C724(g_w->print, func_02045770(win), x, y, o, g_w->font,
                  (u16)((fg << 10) | (shadow << 5)));
    func_02045808(o);
    return w;
}

/* The width of a text in the screen's font. */
static int text_width(const char *t)
{
    int n = (int)strlen(t), w;
    void *o;

    if (n == 0) {
        return 0;
    }
    o = ascii_obj(t, n);
    w = (int)func_0201D304(o, g_w->font, 0);
    func_02045808(o);
    return w;
}

/* The text the selected row shows of a value wider than `room`: the value
 * from the moving start, then a gap and the value again. */
static const char *marquee(const char *value, int room, char *out, int n)
{
    int len = (int)strlen(value), start;

    if (text_width(value) <= room || len == 0) {
        return value;
    }
    g_w->marq_on = 1;
    start = g_w->marq % (len + (int)sizeof(MARQ_GAP) - 1);
    snprintf(out, (size_t)n, "%s" MARQ_GAP "%s", value, value);
    return out + start;
}

static void win_clear_and_commit_begin(u32 win)
{
    func_02043B5C(func_02045770(win), 0);
}

static void win_commit(u32 win)
{
    func_02045334(win);
    func_02045374(win);
    func_02041B6C(func_02045730(win));
}

/* The rows are in capitals, as the title menu's are. */
static void upper(char *t)
{
    for (; *t; t++) {
        if (*t >= 'a' && *t <= 'z') {
            *t = (char)(*t - 'a' + 'A');
        }
    }
}

/* The list screen: bar i holds row i + 1 of the model (row 0 is the header). */
static void draw_rows(void)
{
    char label[48], value[96], moving[200];
    int i, n = eidos_sui_row_count(), cr = eidos_sui_cursor_row();

    if (cr != g_w->marq_row) {
        /* another row: its value starts at the start */
        g_w->marq_row = cr;
        g_w->marq = 0;
        g_w->marq_tick = -MARQ_WAIT;
    }
    g_w->marq_on = 0;

    eidos_sui_row_text(0, label, sizeof label, value, sizeof value);
    upper(label);
    win_clear_and_commit_begin(g_w->win_head);
    put_text(g_w->win_head, label, BAR_L * 8 + 4, -1, 16 * 8 - 8, 0, TC_HEAD, TC_SHADOW);
    {
        /* the tabs, as the bag shows its pockets: one box each, the one
         * shown lit, with L and R on either side */
        int tabs = eidos_sui_tab_count(), cur = eidos_sui_tab(), t;
        int x = (BAR_R + 1) * 8 - 12 - tabs * 14 - 12;
        void *bmp = func_02045770(g_w->win_head);

        if (tabs == 0) {
            goto no_tabs;               /* the Multiplayer screen */
        }
        put_text(g_w->win_head, "L", x, -1, 12, 0, TC_TEXT, TC_SHADOW);
        x += 12;
        for (t = 0; t < tabs; t++) {
            func_02043AF4(bmp, (u32)(x + 1), 4, 10, 8,
                          (u8)(t == cur ? TC_LIVE : TC_DIM));
            x += 14;
        }
        put_text(g_w->win_head, "R", x + 2, -1, 12, 0, TC_TEXT, TC_SHADOW);
    }
no_tabs:
    win_commit(g_w->win_head);

    {
        /* the scrollbar: the track, and the part of the list on screen */
        int first = 0, shown = 1, total = 1;
        void *bmp = func_02045770(g_w->win_scroll);
        int h = 24 * OPT_ROWS;

        (void)eidos_sui_scroll(&first, &shown, &total);
        win_clear_and_commit_begin(g_w->win_scroll);
        if (total > shown) {
            int top = h * first / total;
            int len = h * shown / total;

            if (len < 8) {
                len = 8;
            }
            if (top + len > h) {
                top = h - len;
            }
            func_02043AF4(bmp, 3, 0, 2, (u16)h, (u8)TC_DIM);
            func_02043AF4(bmp, 1, (u32)top, 6, (u16)len, (u8)TC_TEXT);
        }
        win_commit(g_w->win_scroll);
    }

    /* Bars 0..4 are the option rows (model rows 1..); bar 5 is the grey
     * bar (Back) inside a section, and one more row on the main list. */
    for (i = 0; i < OPT_ROWS; i++) {
        u32 win = g_w->win_row[i];
        int grey = i == OPT_ROWS - 1 && eidos_sui_has_back();
        int mrow = grey ? n - 1 : i + 1;
        int have = grey ? 1 : mrow < n - (eidos_sui_has_back() ? 1 : 0);
        int sel = mrow == cr;
        char names[3][20];
        int active = 0, nch = 0, c;

        win_clear_and_commit_begin(win);
        if (have) {
            eidos_sui_row_text(mrow, label, sizeof label, value, sizeof value);
            upper(label);
            put_text(win, label, 8, -1, value[0] != 0 ? 100 : ROW_PX - 12, 4,
                     TC_TEXT, TC_SHADOW);
            if (!grey) {
                nch = eidos_sui_row_choices(mrow, &active, names);
            }
            if (nch > 0) {
                for (c = 0; c < nch; c++) {
                    /* the active choice in gold (it pulses on the selected
                     * row), the others grey */
                    upper(names[c]);
                    put_text(win, names[c], nch == 3 ? k_text3[c] : k_text2[c],
                             -1, 60, 4,
                             c != active ? TC_DIM : sel ? TC_LIVE : TC_ACTIVE,
                             TC_SHADOW);
                }
            } else if (value[0] != 0) {
                int adj = eidos_sui_row_adjustable(mrow), x = VALUE_X, vw;
                int room;
                const char *shown = value;

                upper(value);
                if (adj && sel) {
                    /* left and right change it: say so on the selected row */
                    put_text(win, "<", x, -1, 16, 4, TC_LIVE, TC_SHADOW);
                    x += ARROW_W;
                }
                room = ROW_PX - x - (adj && sel ? 20 : 4);
                if (sel) {
                    shown = marquee(value, room, moving, (int)sizeof moving);
                }
                vw = put_text(win, shown, x, -1, room, 4,
                              adj ? (sel ? TC_LIVE : TC_ACTIVE) : TC_TEXT, TC_SHADOW);
                if (adj && sel) {
                    put_text(win, ">", x + vw + 8, -1, 16, 4, TC_LIVE, TC_SHADOW);
                }
            }
        }
        win_commit(win);
    }
}

static void draw_help(void)
{
    char line[96];
    int i, n = eidos_sui_help_count();

    win_clear_and_commit_begin(g_w->win_help);
    for (i = 0; i < n && i < 2; i++) {
        eidos_sui_help_line(i, line, sizeof line);
        if (line[0] != 0) {
            put_text(g_w->win_help, line, 4, -1, 30 * 8 - 8, 2 + 16 * i, TC_TEXT, TC_SHADOW);
        }
    }
    win_commit(g_w->win_help);
}

/*
 * The selected bar: the game marks it with a sprite whose colours a palette
 * animation drives (func_021B9D68).  Here the bar's tile map goes to a
 * deeper copy of its palette instead: palette bank 9 of the main
 * background, made from bank 2 (the bars) with the red and green darkened.
 */
extern void func_020413B0(u32, u32, u32, u32, u32, u32);

/*
 * The colours.  The game's Options art is purple and green; the title menu this
 * screen is reached from is near-black with white borders and a teal
 * selection.  The art keeps its shapes and the palettes take the title menu's
 * colours.  Set directly in the palette memory after the game has loaded the
 * originals.
 */
static u16 rgb8(u32 r, u32 g, u32 b)
{
    return (u16)((r >> 3) | ((g >> 3) << 5) | ((b >> 3) << 10));
}

/* The bar palette (banks 0, 2 and 9 of the main background): index 4 is the
 * fill, 5 and 1 the inner lines, 3, 9 and 15 the border and its end ticks,
 * 6 and 14 the border's shading, 7, 8 and 10 the dark edge. */
static void theme_bar(u16 *bank, const u32 fill[3], const u32 line[3], const u32 shade[3])
{
    u32 f[3], d[3], l[3];
    int i;

    for (i = 0; i < 3; i++) {
        f[i] = fill[i];
        d[i] = fill[i] > 10u ? fill[i] - 10u : 0u;
        l[i] = fill[i] + 12u > 255u ? 255u : fill[i] + 12u;
    }
    bank[4] = rgb8(f[0], f[1], f[2]);
    bank[1] = rgb8(f[0], f[1], f[2]);
    bank[2] = rgb8(l[0], l[1], l[2]);
    bank[5] = rgb8(d[0], d[1], d[2]);
    bank[7] = rgb8(d[0], d[1], d[2]);
    bank[8] = rgb8(f[0], f[1], f[2]);
    bank[10] = rgb8(d[0], d[1], d[2]);
    bank[3] = rgb8(line[0], line[1], line[2]);
    bank[9] = rgb8(line[0], line[1], line[2]);
    bank[15] = rgb8(line[0], line[1], line[2]);
    bank[6] = rgb8(shade[0], shade[1], shade[2]);
    bank[14] = rgb8(shade[0], shade[1], shade[2]);
}

/* a to b, t in 0..16 */
static void mix3(u32 out[3], const u32 a[3], const u32 b[3], u32 t)
{
    int i;

    for (i = 0; i < 3; i++) {
        out[i] = (a[i] * (16u - t) + b[i] * t) / 16u;
    }
}

static const u32 k_teal[3] = { 8, 172, 160 }, k_teal_hi[3] = { 150, 255, 244 };
static const u32 k_sel[3] = { 10, 56, 62 }, k_sel_hi[3] = { 16, 84, 90 };
static const u32 k_flash[3] = { 120, 230, 220 };
static const u32 k_gold[3] = { 255, 204, 48 }, k_white[3] = { 255, 255, 255 };

static void make_palettes(void)
{
    static const u32 fill[3] = { 30, 31, 36 }, grey[3] = { 120, 122, 130 };
    static const u32 line[3] = { 238, 238, 238 };
    u16 *main_pal = (u16 *)bw_ds_ptr(0x05000000u);
    u16 *sub_pal = (u16 *)bw_ds_ptr(0x05000400u);
    int i;

    /* what shows through: the backdrop */
    main_pal[0] = rgb8(14, 14, 18);
    for (i = 0; i < 2; i++) {
        sub_pal[i] = sub_pal[16 + i] = rgb8(8, 8, 12);
    }
    theme_bar(main_pal + 0 * 16, fill, line, grey);     /* the grey bar */
    theme_bar(main_pal + 2 * 16, fill, line, grey);     /* the bars */
    theme_bar(main_pal + 9 * 16, k_sel, k_teal, k_teal);    /* the selected bar: animate() */
    /* bank 13 is for a bar with no row: all of it the backdrop's colour */
    for (i = 0; i < 16; i++) {
        main_pal[13 * 16 + i] = rgb8(14, 14, 18);
        main_pal[8 * 16 + i] = rgb8(14, 14, 18);
    }
    /* the grid of the other screen: faint crosses, the text goes over them */
    sub_pal[1 * 16 + 2] = rgb8(44, 46, 54);
    sub_pal[1 * 16 + 3] = rgb8(26, 28, 34);
    for (i = 0; i < 16; i++) {
        main_pal[15 * 16 + i] = 0;
        sub_pal[15 * 16 + i] = 0;
    }
    main_pal[15 * 16 + TC_TEXT] = sub_pal[15 * 16 + TC_TEXT] = rgb8(255, 255, 255);
    main_pal[15 * 16 + TC_SHADOW] = sub_pal[15 * 16 + TC_SHADOW] = rgb8(0, 0, 0);
    main_pal[15 * 16 + TC_DIM] = rgb8(124, 128, 140);
    main_pal[15 * 16 + TC_ACTIVE] = rgb8(k_gold[0], k_gold[1], k_gold[2]);
    main_pal[15 * 16 + TC_LIVE] = rgb8(k_gold[0], k_gold[1], k_gold[2]);
    main_pal[15 * 16 + TC_HEAD] = rgb8(k_teal_hi[0], k_teal_hi[1], k_teal_hi[2]);
}

/*
 * The moving parts, every frame: the selected bar's border and fill pulse
 * (a triangle wave of 48 frames), the active choice on it pulses between
 * gold and white, and a change of value flashes the bar for a few frames.
 */
static void animate(OptWork *w)
{
    u16 *main_pal = (u16 *)bw_ds_ptr(0x05000000u);
    u32 ph = (u32)w->tick % 48u, t = ph < 24u ? ph : 48u - ph, c[3], f[3];

    w->tick++;
    if (!w->built) {
        return;
    }
    t = t * 16u / 24u;
    mix3(c, k_teal, k_teal_hi, t);
    mix3(f, k_sel, k_sel_hi, t);
    if (w->flash > 0) {
        mix3(f, f, k_flash, (u32)w->flash * 16u / 10u);
        mix3(c, c, k_white, (u32)w->flash * 16u / 10u);
    }
    theme_bar(main_pal + 9 * 16, f, c, c);
    mix3(c, k_gold, k_white, w->flash > 0 ? 16u : t / 2u);
    main_pal[15 * 16 + TC_LIVE] = rgb8(c[0], c[1], c[2]);
    if (w->flash > 0) {
        w->flash--;
    }
}

static void highlight(OptWork *w)
{
    int cr = eidos_sui_cursor_row(), bar, i;

    bar = eidos_sui_has_back() && cr == eidos_sui_row_count() - 1
          ? OPT_ROWS - 1 : cr - 1;
    if (!w->built) {
        make_palettes();
        w->built = 1;
    }
    for (i = 0; i < OPT_ROWS; i++) {
        /* bars 0..4 use palette bank 2, the grey bar bank 0 (the screen map);
         * a bar with no row takes the empty bank 13, so it is not drawn */
        int grey = i == OPT_ROWS - 1 && eidos_sui_has_back();
        u32 pal = grey ? 0u : 2u;
        int rows = eidos_sui_row_count() - 1 - (eidos_sui_has_back() ? 1 : 0);

        if (!grey && i >= rows) {
            pal = 13u;
        }
        if (i == bar) {
            pal = 9u;
        }
        func_020413B0(2, BAR_L, (u32)(2 + 3 * i), BAR_R - BAR_L + 1, 3, pal);
    }
    func_02041B6C(2);
}

/* ---- the screen ---- */

/*
 * The bars of the game's art run edge to edge.  Their map is rewritten so
 * they run from BAR_L to BAR_R: the caps keep their tiles, the middle is
 * the middle tile repeated, and the columns outside take the empty
 * palette bank, so nothing is drawn there.
 */
extern void *func_0204162C(int layer);         /* BG_SYS_GetBuf */

static void narrow_bars(void)
{
    u16 *map = (u16 *)func_0204162C(2);
    int i, r, x;

    if (map == NULL) {
        return;
    }
    for (i = 0; i < OPT_ROWS; i++) {
        for (r = 0; r < 3; r++) {
            u16 *row = map + (2 + 3 * i + r) * 32;
            u16 old[32];

            memcpy(old, row, sizeof old);
            for (x = 0; x < 32; x++) {
                u16 t;

                if (x < BAR_L || x > BAR_R) {
                    t = (u16)((old[2] & 0x0FFFu) | (13u << 12));
                } else if (x < BAR_L + 2) {
                    t = old[x - BAR_L];               /* the left cap */
                } else if (x > BAR_R - 2) {
                    t = old[31 - (BAR_R - x)];        /* the right cap */
                } else {
                    t = old[2];                       /* the middle */
                }
                row[x] = t;
            }
        }
    }
}

static void vblank_task(u32 task, u32 arg)
{
    (void)task;
    (void)arg;
    func_020419E4();
    func_0204A648();
}

static void gfx_build(OptWork *w)
{
    void *h;
    u32 heap = OPT_HEAP;
    int i;

    func_0203F8F4(heap);
    func_02045088(heap);
    func_0203FC28(k_bgmode);
    for (i = 0; i < 6; i++) {
        func_0203FCA0(k_layers[i].layer, k_layers[i].cfg, k_layers[i].mode);
        func_020414C4((u32)k_layers[i].layer);   /* func_020414AC: an asm body the boot cannot link, */
        func_020414EC((u32)k_layers[i].layer);   /* it is exactly these two calls */
        func_02040588((u32)k_layers[i].layer, 1);
    }
    func_02040588(3, 0);
    func_02040C14(4, 0, 1, 0);
    func_02040C14(3, 0, 1, 0);

    h = func_020490F4(0x1b, heap);
    func_02049B68(h, 1, 0, 0, 0, heap);                 /* main palette */
    func_02049658(h, 0, 2, 0, 0, 0, heap);              /* layer 2: the bars */
    func_020498F4(h, 2, 2, 0, 0, 0, heap);
    func_02049658(h, 0x10, 3, 1, 0, 0, heap);           /* layer 3: text tiles */
    narrow_bars();
    func_02049B68(h, 8, 4, 0, 0, heap);                 /* sub palette */
    func_02049658(h, 7, 5, 0, 0, 0, heap);              /* layer 5: the grid */
    func_020498F4(h, 9, 5, 0, 0, 0, heap);
    func_02049658(h, 0x10, 4, 1, 0, 0, heap);           /* layer 4: text tiles */
    func_02049238(h);

    w->win_help = func_020450F0(4, 1, 0x13, 0x1e, 4, 0xf, 1);
    w->win_head = func_020450F0(0, 0, 0, 0x1e, 3, 0xf, 1);   /* wide: the tabs on the right */
    w->win_scroll = func_020450F0(3, SCROLL_X, 2, 1, 3 * OPT_ROWS, 0xf, 1);
    for (i = 0; i < OPT_ROWS; i++) {
        w->win_row[i] = func_020450F0(3, ROW_X, (u32)(2 + 3 * i), ROW_W, 3, 0xf, 1);
    }
    for (i = 0; i < OPT_ROWS; i++) {
        func_02043B5C(func_02045770(w->win_row[i]), 0);
    }
    func_02043B5C(func_02045770(w->win_help), 0);
    func_02043B5C(func_02045770(w->win_head), 0);
    func_02043B5C(func_02045770(w->win_scroll), 0);

    w->font = func_0201D7EC(0x17, 0, 0, 0, heap);
    w->print = func_0201C440(heap);
    func_020409B4(0);
    func_020409B4(2);
    func_020409B4(3);
    func_020409B4(4);
    func_020409B4(5);
}

static void screen_build(OptWork *w)
{
    func_0207D534((volatile u16 *)bw_ds_ptr(0x0400006Cu), -16);
    func_0207D534((volatile u16 *)bw_ds_ptr(0x0400106Cu), -16);
    func_02043534(0);
    func_020435F4(0);
    bw_io_write16(0x04000050, 0);
    bw_io_write16(0x04001050, 0);
    bw_io_write32(REG_DISPCNT_MAIN, bw_io_read32(REG_DISPCNT_MAIN) & 0xFFFF1FF7u);
    bw_io_write32(REG_DISPCNT_SUB, bw_io_read32(REG_DISPCNT_SUB) & 0xFFFF1FF7u);
    func_0204335C(0);
    func_020433E0(k_banks);
    func_0204363C(0);
    func_0204361C();
    func_020434CC();
    func_02043588();
    func_0201DD60();
    func_0201DD78(0xf, 0xe, 0);
    w->tcb = func_0203101C(OPT_HEAP, OPT_HEAP, 4, 0x20);
    w->vtask = func_020056A0(vblank_task, 0, 0);
    gfx_build(w);
    draw_rows();
    draw_help();
    /* func_020434DC and func_02043598 return early when any bit of the mask is
     * already set, so the planes go on one at a time, as the game does. */
    func_02040588(1, 0);            /* layer 1 has no graphics */
    func_02040588(3, 1);            /* the text layer */
    func_02040588(4, 1);            /* the other screen's text layer */
    func_0204E720(3, 0x10, 0, 2);   /* fade in */
}

static void screen_free(OptWork *w)
{
    int i;

    func_020452E8(w->win_help);
    func_020452E8(w->win_head);
    func_020452E8(w->win_scroll);
    for (i = 0; i < OPT_ROWS; i++) {
        func_020452E8(w->win_row[i]);
    }
    func_0201C4C0(w->print);
    func_0201D83C(w->font);
    func_0204363C(1);
    func_0204361C();
    func_02030EAC(w->vtask);
    func_02031140(w->tcb);
    func_020450C8();
    func_0203F9B4();
}

/*
 * The stylus.  A touch that starts on a bar goes to the model with the row
 * and the place on it: one of the choices (each owns the span from its
 * text to the next one's), the arrows of a value, or the row itself.
 */
static int touch_input(OptWork *w)
{
    int x = 0, y = 0, down = eidos_settings_touch(&x, &y);
    int bar, mrow, n = eidos_sui_row_count(), zone = -1, nch, active;
    char names[3][20];

    if (!down || w->touch_was) {
        w->touch_was = down;
        return 0;
    }
    w->touch_was = 1;
    if (y < 16 || y >= 16 + 24 * OPT_ROWS) {
        return 0;
    }
    bar = (y - 16) / 24;
    x -= ROW_X * 8;
    {
        /* the sixth bar is the grey one inside a section, a row on the
         * main list */
        int grey = bar == OPT_ROWS - 1 && eidos_sui_has_back();

        mrow = grey ? n - 1 : bar + 1;
        if (!grey && mrow >= n - (eidos_sui_has_back() ? 1 : 0)) {
            return 0;       /* a bar with no row */
        }
        nch = !grey ? eidos_sui_row_choices(mrow, &active, names) : 0;
    }
    if (nch == 3) {
        zone = x < k_text3[0] - 6 ? -1 : x < k_text3[1] - 6 ? 0 : x < k_text3[2] - 6 ? 1 : 2;
    } else if (nch == 2) {
        zone = x < k_text2[0] - 6 ? -1 : x < k_text2[1] - 6 ? 0 : 1;
    } else if (eidos_sui_row_adjustable(mrow) && x >= VALUE_X - 8) {
        /* "<" is at VALUE_X; everything right of the value's start is ">" */
        zone = x < VALUE_X + ARROW_W + 4 ? -2 : -3;
    }
    return eidos_sui_touch(mrow, zone);
}

/* ---- the game's keyboard ---- */

/* The keyboard opens on the text the screen asked for; the screen's own
 * graphics are gone by now, and the keyboard is a process run before this
 * one goes on (PROCSYS_PushGlobal). */
static int kb_open(OptWork *w)
{
    KbArgs *a;
    char text[32];
    u16 buf[33];
    int kind = 4, max = 10, i;

    if (eidos_sui_keyboard(&kind, &max, text, (int)sizeof text) == 0) {
        return 0;
    }
    a = (KbArgs *)func_02030734(OPT_HEAP, (u32)sizeof(KbArgs), 0, "eidos", 0);
    if (a == NULL) {
        eidos_sui_keyboard_done(NULL, 0);
        return 0;
    }
    func_02082BCC(a, 0, (int)sizeof(KbArgs));
    a->kind = kind;
    a->species = kind == 0 ? 0 : 0xFFFFu;
    a->maxLen = max;
    for (i = 0; text[i] != '\0' && i < max && i < 32; i++) {
        buf[i] = (u8)text[i];
    }
    buf[i] = 0xFFFF;
    a->name = func_020457B0(0x20, OPT_HEAP);
    func_02045994(a->name, buf, (u32)(i + 1));
    w->kb = a;
    func_020312CC((int)NAMEIN_OVERLAY, bw_ds_ptr(NAMEIN_PROC_DATA), a);
    return 1;
}

/* The keyboard is closed: its text goes back to the screen. */
static void kb_close(OptWork *w)
{
    KbArgs *a = w->kb;
    u16 buf[33];
    char text[33];
    int i, n = 0;

    if (a == NULL) {
        return;
    }
    func_02082BCC(buf, 0, (int)sizeof buf);
    func_02045A38(a->name, buf, 32);
    for (i = 0; i < 32 && buf[i] != 0xFFFF && buf[i] != 0; i++) {
        u16 c = buf[i];

        if (c >= 0xFF01u && c <= 0xFF5Eu) {
            c = (u16)(c - 0xFEE0u);         /* the full-width forms */
        } else if (c == 0x3000u) {
            c = ' ';
        }
        if (c >= 0x20u && c < 0x7Fu) {
            text[n++] = (char)c;
        }
    }
    text[n] = '\0';
    eidos_sui_keyboard_done(text, !a->canceled);
    func_02045808(a->name);
    func_020307B0(a);
    w->kb = NULL;
}

/* ---- the process ---- */

static u32 opt_init(void *proc, u32 *seq, u32 pwork, u32 work)
{
    OptWork *w;

    (void)seq;
    (void)pwork;
    (void)work;
    func_0203064C(1, OPT_HEAP, OPT_HEAP_SIZE);
    w = (OptWork *)func_0203159C(proc, (u32)sizeof(OptWork), OPT_HEAP);
    func_02082BCC(w, 0, (int)sizeof(OptWork));
    w->heap = OPT_HEAP;
    g_w = w;
    eidos_ui_options_proc_up = 1;
    eidos_sui_enter();
    return 1;
}

static u32 opt_main(void *proc, u32 *seq, u32 pwork, u32 work)
{
    OptWork *w = g_w;

    (void)proc;
    (void)pwork;
    (void)work;
    switch (*seq) {
    case 0:
        /* A question the screen asks before it first shows (the name, when
         * Multiplayer opens without one): the keyboard comes straight
         * after the title's fade-out, and the screen fades in after it. */
        if (!w->asked && (w->asked = 1, kb_open(w))) {
            *seq = 5;
            return 0;
        }
        screen_build(w);
        *seq = 1;
        break;
    case 1:
        highlight(w);               /* the palette has reached the screen by now */
        animate(w);
        if (func_0204E804() == 0) {
            *seq = 2;
        }
        break;
    case 2: {
        int r = eidos_sui_input(func_020362DC(), func_0203633C());

        r |= touch_input(w);
        if (r & 0x20) {
            w->flash = 10;
        }
        if (r & 0x10) {
            func_020061E4(0x548);
        }
        if (r & 0x20) {
            func_020061E4(0x54c);
        }
        if (r & 0x40) {
            func_020061E4(0x551);
        }
        if (r & 1) {
            draw_rows();
            draw_help();
        } else if (w->marq_on && ++w->marq_tick >= MARQ_STEP) {
            /* the selected row's long value moves on */
            w->marq_tick = 0;
            w->marq++;
            draw_rows();
        }
        highlight(w);
        animate(w);
        if (r & 2) {
            func_0204E720(3, 0, 0x10, 2);
            *seq = 3;
        } else if (r & 4) {
            /* the game's keyboard: the screen fades out and goes */
            func_0204E720(3, 0, 0x10, 2);
            *seq = 4;
        }
        break;
    }
    case 3:
        if (func_0204E804() == 0) {
            screen_free(w);
            return 1;
        }
        break;
    case 4:
        if (func_0204E804() == 0) {
            screen_free(w);
            *seq = kb_open(w) ? 5 : 0;
            return 0;               /* the screen's tasks are gone */
        }
        break;
    case 5:
        /* back from the keyboard: the screen is built again */
        kb_close(w);
        w->built = 0;               /* the palettes are made again too */
        *seq = 0;
        return 0;
    }
    func_020310C4(w->tcb);
    func_0204A600();
    return 0;
}

static u32 opt_end(void *proc, u32 *seq, u32 pwork, u32 work)
{
    (void)seq;
    (void)pwork;
    (void)work;
    eidos_sui_leave();
    func_020315D4(proc);
    func_020306F0(OPT_HEAP);
    g_w = NULL;
    eidos_ui_options_proc_up = 0;
    /* back to the title menu, loaded again like the game loads it */
    func_020312F8(bw_mod_title_return_ov(), bw_mod_title_return_table(), NULL);
    return 1;
}

typedef u32 (*ProcFn)(void *, u32 *, u32, u32);

static const struct {
    ProcFn init, main, end;
} k_proc = { opt_init, opt_main, opt_end };

const void *eidos_opt_proc_table(void)
{
    return &k_proc;
}

/* ------------------------------------------------------------------ */
/* The save handoff's confirmation                                     */
/* ------------------------------------------------------------------ */

/*
 * A save that arrives from a peer is the port's own: the player is asked
 * whether it is taken, and the save is installed only when the player
 * says yes (sdk/mod_handoff.h, "The port's own calls").  Without this
 * callback the handoff reports the save on stderr and drops it: nothing
 * is installed without the player's question.
 *
 * The question goes to the port's own text path: the game's own yes/no
 * window, the one a mod's question uses as well (sdk/mod_message.h).  A
 * question the port cannot ask answers no, so a save that arrives where
 * the player cannot be asked is not installed either.
 *
 * The SDK's headers are not included here: they carry the SDK's own
 * typedefs, and this file has its own (a second typedef of the same name
 * in one translation unit is a constraint violation in C99).
 */
extern int  bw_mod_message_confirm(const char *text);
extern void bw_mod_handoff_set_confirm(int (*fn)(u32 peer, const void *buf,
                                                  u32 size, void *user),
                                        void *user);

static int handoff_confirm(u32 peer, const void *buf, u32 size, void *user)
{
    char text[96];

    (void)buf;
    (void)size;
    (void)user;
    snprintf(text, sizeof text, "Peer %u sent a save. Take it?",
             (unsigned int)peer);
    return bw_mod_message_confirm(text);
}

/*
 * The port's own confirmation for the save handoff.  The boot calls this
 * where the port starts, beside bw_mod_handoff_set_reload(): the
 * confirmation is the port's, and the port installs it when it starts.
 */
void eidos_mod_handoff_install_confirm(void)
{
    bw_mod_handoff_set_confirm(handoff_confirm, NULL);
}

/* ------------------------------------------------------------------ */
/* The keyboard's question                                             */
/* ------------------------------------------------------------------ */

/*
 * The name entry asks its question by kind ("Your friend's name?" for the
 * plain kind); opened for a text of the port's own, it asks the port's
 * question instead.  The question is the one message the name entry reads
 * (MSGDATA_ReadString, for its message box), so while the port's keyboard
 * is open that read is answered here.  The link sends the game's reads
 * through this function (CMakeLists.txt, --wrap).
 */
#ifdef EIDOS_WRAP_MSGDATA
extern void __real_MSGDATA_ReadString(void *, u32, void *) __attribute__((weak));
void __wrap_MSGDATA_ReadString(void *bank, u32 id, void *dest)
{
    const char *q = (g_w != NULL && g_w->kb != NULL) ? eidos_sui_keyboard_prompt() : NULL;

    if (q != NULL && dest != NULL) {
        u16 buf[64];
        int i;

        for (i = 0; q[i] != '\0' && i < 63; i++) {
            buf[i] = (u8)q[i];
        }
        buf[i] = 0xFFFF;
        func_02045994(dest, buf, (u32)(i + 1));
        return;
    }
    __real_MSGDATA_ReadString(bank, id, dest);
}
#endif
