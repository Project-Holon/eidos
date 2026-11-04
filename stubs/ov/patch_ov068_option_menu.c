/*
 * patches: src/game/ov068/option_menu.c OPTION_MENU_UpdateList OPTION_MENU_DrawList OPTION_MENU_ExitList
 *
 * The port's own settings in the game's Options screen (docs/settings-design.md).
 *
 * What this file owns
 * -------------------
 *
 * The Options screen (overlay 68) lists the game's five settings on the
 * bottom screen: a row label, then two or three choices, each with a
 * marker sprite, the chosen one bright and the others dim.  The port adds
 * pages to it.  The sixth bar under the list, empty in the game, is the
 * page bar: the page's name in the middle (GAME, DISPLAY, SOUND, EXTRAS) and a
 * double arrow at each end, the bag's pocket arrows drawn in this screen's
 * colours.  A touch on an arrow, or L and R, changes the page (it wraps),
 * and the arrow lights up for a moment as the bag's does.  GAME is the
 * game's own page, unchanged.  The other pages are drawn the way the game
 * draws its rows:
 *
 *   - the same row bars, the same label column and the same choice
 *     columns (three at 112/160/208, two at 112/184, in pixels);
 *   - the same marker sprites, moved to the port's choices;
 *   - the same colours: palette 0 for a chosen choice and a label, 3 for
 *     a choice not chosen, 7 and 12 for the same on the cursor's row;
 *   - the same help box on the top screen.
 *
 * A setting with many values (the pixel scale, the volume) is a stepper:
 * "<", the value, ">".  The value is the chosen choice; the arrows are
 * touch targets, and Left and Right step it as for any row.
 *
 * The game's settings are saved when the player confirms, as before.  The
 * port's settings apply at once and are saved at once (settings.ini), so
 * the discard prompt is the game's settings only.
 *
 * The cursor keeps the game's numbering: rows 0..4, then 5 (confirm) and
 * 6 (cancel), so the game's own buttons work on every page.  A page with
 * fewer rows skips the empty ones.
 *
 * With the game's own page the three functions are the game's own code
 * (copied below), apart from the page names.
 *
 * symbols:
 *   BMP_FillRect  0x02043AF4  arm
 *   BG_SYS_GetScrollY  0x02040804  arm
 *   BG_SYS_FillTileMap  0x020413B0  arm
 *   BG_SYS_MarkTileDirty  0x02041B6C  arm
 *   BG_SYS_SetPlaneVisible  0x02040588  arm
 *   CLACT_ChangeActorAnimSeq  0x0204B8A0  arm
 *   CLACT_AddActorSurfacePos  0x0204B404  arm
 *   KEY_GetTrg  0x020362DC  arm
 *   UI_GetInputMode  0x020355C4  arm
 *   UI_SetInputMode  0x020355D8  arm
 *   GFL_USE_PlaySeDefaultVolume  0x020061E4  thumb
 *   MI_CpuFill8  0x02082BCC  arm
 *   STRBUF_Create  0x020457B0  arm
 *   STRBUF_Delete  0x02045808  arm
 *   BMP_WIN_Create  0x020450F0  arm
 *   BMP_WIN_Delete  0x020452E8  arm
 *   BMP_WIN_GetBmp  0x02045770  arm
 *   BMP_WIN_TransferChars  0x02045334  arm
 *   BMP_WIN_MakeScreen  0x02045374  arm
 *   BMP_WIN_GetBgLayer  0x02045730  arm
 *   BMP_Clear  0x02043B5C  arm
 *   PRINTSYS_Print  0x0201C7A4  thumb
 *   PRINTSYS_PrintQue  0x0201C6FC  thumb
 *   PRINTSYS_PrintStreamDelete  0x0201CE48  thumb
 *   PRINTSYS_MeasureWidestLine  0x0201D304  thumb
 *   gOptionMenuLabels     0x021BB820  data  ov068  a4
 *   gOptionMenuChoices     0x021BB704  data  ov068  a4
 *   gOptionMenuSettingRows     0x021BB570  data  a4
 *   gOptionMenuRowPalettes     0x021BB538  data  a1
 */

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef signed short s16;

/* ------------------------------------------------------------------ */
/* The game's own records (src/game/ov068/option_menu_types.h)         */
/* ------------------------------------------------------------------ */

/* Copies of the records this file reads, the members in the same order
 * as the game's own header, so the host compiler gives the same offsets. */
struct ClactActor;
struct BmpWin;
struct Bmp;
struct GfFont;
struct Strbuf;
struct MsgBank;
struct PrintStream;
struct TaskList;
struct PrintSys;
struct Config;
struct OptionMenuScreen;
struct OptionMenuButtons;

enum {
    OPTION_INPUT_NONE = 0,
    OPTION_INPUT_DRAG = 1,
    OPTION_INPUT_TOUCH = 3,
    OPTION_INPUT_UP = 4,
    OPTION_INPUT_DOWN = 5,
    OPTION_INPUT_RIGHT = 6,
    OPTION_INPUT_LEFT = 7,
    OPTION_INPUT_CONFIRM = 8,
    OPTION_INPUT_CANCEL = 9,
    OPTION_INPUT_HOLD_UP = 10,
    OPTION_INPUT_HOLD_DOWN = 11,
    OPTION_INPUT_SHORTCUT_X = 12,
    OPTION_INPUT_SHORTCUT_Y = 13,
    OPTION_INPUT_KEY_ACTIVATE = 14
};

enum {
    OPTION_COUNT = 5,
    OPTION_CURSOR_CONFIRM = 5,
    OPTION_CURSOR_CANCEL = 6,
    OPTION_ACTOR_CHOICE_COUNT = 11,
    OPTION_WINDOW_LABEL_COUNT = 16
};

#define OPTION_SE_CURSOR 0x548

typedef struct OptionSettings {
    u8 values[OPTION_COUNT];
    u8 lastChanged;
    u16 pad6;
    struct Config *config;
} OptionSettings;

typedef struct OptionMenuList {
    u16 mainLayer;
    u16 subLayer;
    int cursor;
    struct ClactActor *choiceActors[OPTION_ACTOR_CHOICE_COUNT];
    int scrollY;
    u32 repeatCounter;
    u32 inputTrail;
    u32 pad40;
    OptionSettings settings;
    int redraw;
    int highlight;
} OptionMenuList;

typedef struct OptionMenuInput {
    int command;
    int startX;
    int startY;
    int dragX;
    int dragY;
    int lastX;
    int lastY;
    int touching;
} OptionMenuInput;

typedef struct OptionMenuTextBox {
    struct MsgBank *msgBank;
    struct GfFont *font;
    struct PrintStream *stream;
    struct TaskList *taskList;
    struct BmpWin *window;
    struct Strbuf *buffer;
    u16 fillColor;
    u16 heapId;
    struct BmpWin *printWindow;
    u8 printing;
    u8 pad21[3];
    struct PrintSys *printSys;
    u32 mode;
    u32 waitState[3];
} OptionMenuTextBox;

typedef struct OptionMenuLabel {
    u32 messageId;
    u8 x;
    u8 y;
    u8 width;
    u8 height;
} OptionMenuLabel;

typedef struct OptionMenuChoice {
    u32 settingIndex;
    u32 value;
    u32 actorX;
    u32 actorY;
    s16 rectX;
    s16 rectY;
    s16 rectWidth;
    s16 rectHeight;
} OptionMenuChoice;

typedef struct OptionMenuSettingRow {
    u16 descriptionId;
    u16 choiceCount;
    u8 choices[4];
} OptionMenuSettingRow;

typedef struct OptionMenuPoint {
    int x;
    int y;
} OptionMenuPoint;

typedef struct ClPos {
    u16 x;
    u16 y;
} ClPos;

extern OptionMenuLabel gOptionMenuLabels[];
extern OptionMenuChoice gOptionMenuChoices[];
extern const OptionMenuSettingRow gOptionMenuSettingRows[];
extern const u8 gOptionMenuRowPalettes[];

/* The unit's own functions this file calls. */
int OPTION_MENU_GetInputCommand(const OptionMenuInput *input);
void OPTION_MENU_GetInputPoint(const OptionMenuInput *input, int which, OptionMenuPoint *out);
void OPTION_MENU_RestartScreenBlink(struct OptionMenuScreen *screen);
struct BmpWin *OPTION_MENU_GetScreenWindow(struct OptionMenuScreen *screen, int index);
int OPTION_MENU_GetRowAtTouch(OptionMenuList *list, const OptionMenuPoint *touch);
void OPTION_MENU_SelectChoiceAtTouch(OptionMenuList *list, const OptionMenuPoint *touch);
void OPTION_MENU_StepSelectedSetting(OptionMenuList *list, int delta);
int OPTION_MENU_TakeChangedSetting(OptionSettings *settings);
u32 OPTION_MENU_GetTextSpeedWait(const OptionSettings *settings);
void OPTION_MENU_PrintText(OptionMenuTextBox *box, u32 messageId, u32 wait, u32 useStream);
int OPTION_MENU_IsTextDone(OptionMenuTextBox *box);
void OPTION_MENU_ShiftInputTrail(OptionMenuList *list);
int OPTION_MENU_IsChoiceSelected(const OptionSettings *settings, int choiceIndex);
void OPTION_MENU_ScrollList(OptionMenuList *list, int delta, int min, int max);
void OPTION_MENU_DrawList(OptionMenuList *list, int highlight);

/* The game's own functions (src/game/arm9/). */
extern u32 BG_SYS_GetScrollY(u32 layer);
extern void BG_SYS_FillTileMap(u8 index, u8 x0, u8 y0, u8 w, u8 h, u8 pal);
extern void BG_SYS_MarkTileDirty(u32 bit);
extern void BG_SYS_SetPlaneVisible(unsigned char which, unsigned char enable);
extern void CLACT_ChangeActorAnimSeq(struct ClactActor *actor, u16 anim_seq);
extern void CLACT_AddActorSurfacePos(struct ClactActor *actor, const ClPos *pos, u32 which);
extern u32 KEY_GetTrg(void);
extern u8 UI_GetInputMode(void);
extern void UI_SetInputMode(u32 value);
extern void GFL_USE_PlaySeDefaultVolume(u32 id);
extern void MI_CpuFill8(void *dst, u32 value, u32 size);
extern struct Strbuf *STRBUF_Create(u32 capacity, u32 heapId);
extern void STRBUF_SetStringN(struct Strbuf *strbuf, const u16 *srcChars, u32 charCount);
extern void STRBUF_Delete(struct Strbuf *strbuf);
extern struct BmpWin *BMP_WIN_Create(u32 bgLayer, u32 posX, u32 posY, u8 width, u8 height, u8 palette, u8 allocDir);
extern u32 BMP_WIN_Delete(struct BmpWin *win);
extern struct Bmp *BMP_WIN_GetBmp(struct BmpWin *win);
extern void BMP_WIN_TransferChars(struct BmpWin *win);
extern void BMP_WIN_MakeScreen(struct BmpWin *win);
extern void BMP_WIN_ClearScreen(struct BmpWin *win);
extern u8 BMP_WIN_GetBgLayer(struct BmpWin *win);
extern void BMP_Clear(struct Bmp *bmp, u8 value);
extern void BMP_FillRect(struct Bmp *bmp, u32 x, u32 y, u32 w, u16 h, u8 value);
extern void PRINTSYS_Print(struct Bmp *bmp, s16 x, s16 y, struct Strbuf *str, struct GfFont *font);
extern void PRINTSYS_PrintQue(void *queue, struct Bmp *bmp, u32 x, s16 y, void *str, struct GfFont *font);
extern void PRINTSYS_PrintStreamDelete(struct PrintStream *stream);
extern u32 PRINTSYS_MeasureWidestLine(struct Strbuf *text, struct GfFont *font, u32 spacing);

/* The port's settings (engine/platform/settings.h), spelled out: the
 * overlay link header declares what the file itself does not. */
extern int eidos_settings_get(int id);
/* The one-screen layout (engine/ui/recipe_menus.c): the page on show, read
 * by its tabs, and the page a tab click asks for (-1: none), taken here at
 * the screen's next step as one page change. */
extern int eidos_ui_opt_page;
extern int eidos_ui_opt_page_request;
extern int eidos_settings_set(int id, int value);
extern int eidos_settings_commit(void);

/* The option ids this file uses (engine/platform/settings.h's enum). */
#define OPT_DISPLAY_MODE        0
#define OPT_DISPLAY_PIXEL_SCALE 1
#define OPT_DISPLAY_RESOLUTION  2
#define OPT_LAYOUT_ARRANGEMENT  6
#define OPT_LAYOUT_SWAP         7
#define OPT_SOUND_VOLUME        9
#define OPT_EXTRAS_INSTANT_TEXT 10
#define OPT_TRANSITIONS         16

/* ------------------------------------------------------------------ */
/* The pages                                                           */
/* ------------------------------------------------------------------ */

#define KEY_R 0x0100
#define KEY_L 0x0200

#define HEAP_ID   0x1c        /* the screen's heap (OPTION_MENU_Init) */
#define TEXT_MAX  96

enum { PAGE_GAME = 0, PAGE_DISPLAY, PAGE_SOUND, PAGE_EXTRAS, PAGE_COUNT };

enum { ROW_CHOICE = 0, ROW_STEP };

typedef struct PortRow {
    const char *label;
    const char *help;           /* two lines, '\n' between them */
    int option;                 /* the settings.h option id */
    int kind;                   /* ROW_CHOICE or ROW_STEP */
    int count;                  /* ROW_CHOICE: 2 or 3 choices */
    const char *names[3];
    int values[3];
    int min, max, step;         /* ROW_STEP */
    const char *zero;           /* ROW_STEP: the name of 0, or NULL */
    const char *unit;           /* ROW_STEP: "X" or "%" */
} PortRow;

static const PortRow k_display_rows[] = {
    { "WINDOW", "How the game window is shown.\nFull covers the whole screen.",
      OPT_DISPLAY_MODE, ROW_CHOICE, 3, { "WINDOW", "FULL", "BORDER" }, { 0, 1, 2 },
      0, 0, 0, 0, 0 },
    { "PIXEL SCALE", "The size of each DS pixel.\nAuto fills the window.",
      OPT_DISPLAY_PIXEL_SCALE, ROW_STEP, 0, { 0, 0, 0 }, { 0, 0, 0 },
      0, 10, 1, "AUTO", "X" },
    { "3D DETAIL", "The resolution of the 3D.\nAuto matches the window.",
      OPT_DISPLAY_RESOLUTION, ROW_STEP, 0, { 0, 0, 0 }, { 0, 0, 0 },
      0, 6, 1, "AUTO", "X" },
    { "SCREENS", "Stack the screens, put them side\nby side, or show one screen.",
      OPT_LAYOUT_ARRANGEMENT, ROW_CHOICE, 3, { "STACKED", "WIDE", "ONE" }, { 0, 1, 2 },
      0, 0, 0, 0, 0 },
    { "SWAP SCREENS", "Show the bottom screen first.", OPT_LAYOUT_SWAP,
      ROW_CHOICE, 2, { "ON", "OFF", 0 }, { 1, 0, 0 }, 0, 0, 0, 0, 0 },
};

static const PortRow k_sound_rows[] = {
    { "VOLUME", "The volume of the music and\nthe sound effects.",
      OPT_SOUND_VOLUME, ROW_STEP, 0, { 0, 0, 0 }, { 0, 0, 0 },
      0, 100, 5, 0, "%" },
};

static const PortRow k_extras_rows[] = {
    { "INSTANT TEXT", "Show each page of a message at\nonce, whatever the text speed.",
      OPT_EXTRAS_INSTANT_TEXT, ROW_CHOICE, 2, { "ON", "OFF", 0 }, { 1, 0, 0 },
      0, 0, 0, 0, 0 },
    { "TRANSITIONS", "Doors and map changes.\nSeamless never goes black.",
      OPT_TRANSITIONS, ROW_CHOICE, 3, { "ORIGINAL", "FAST", "SEAMLESS" }, { 0, 1, 2 },
      0, 0, 0, 0, 0 },
};

static const char *const k_page_names[PAGE_COUNT] = {
    "GAME", "DISPLAY", "SOUND", "EXTRAS"
};

/* The game's columns, in tiles and pixels (gOptionMenuLabels and
 * gOptionMenuChoices): three choices or two. */
static const u8  k_col3_tile[3]  = { 14, 20, 26 };
static const u8  k_col3_pad[3]   = { 7, 4, 2 };
static const int k_col3_actor[3] = { 120, 165, 211 };
static const int k_col3_rect[3]  = { 112, 160, 208 };
static const u8  k_col2_tile[2]  = { 15, 24 };
static const int k_col2_actor[2] = { 120, 192 };
static const int k_col2_rect[2]  = { 112, 184 };

#define ROW_TILE_Y(r)   (5 + 3 * (r))   /* a row's label and choices */
#define LABEL_TILE_X    1
#define LABEL_TILE_W    12
#define CHOICE3_TILE_W  6
#define CHOICE2_TILE_W  8
#define TAB_ROW         5               /* the sixth bar: the page bar */
#define ARROW_TILE_W    2
#define NAME_TILE_X     4
#define NAME_TILE_W     24
#define FLASH_FRAMES    8

enum { BAR_LEFT = 0, BAR_NAME, BAR_RIGHT, BAR_COUNT };
static const u8 k_bar_tile[BAR_COUNT] = { 2, NAME_TILE_X, 28 };

/*
 * The bag's pocket arrows (its touch bar, overlay 91), 16 x 12 pixels:
 * '#' is the arrow, '+' its outline.  They are drawn with the colour numbers
 * this screen's text shows in (15 the letter, 14 the shadow, read off a
 * render of every number under palettes 0 and 3), so the row colours tint
 * them as they tint the text.
 */
static const char *const k_arrow_left[12] = {
    ".....++.....++..",
    "....+##+...+##+.",
    "...+###+..+###+.",
    "..+###+..+###+..",
    ".+###+..+###+...",
    "+###+..+###+....",
    "+###+..+###+....",
    ".+###+..+###+...",
    "..+###+..+###+..",
    "...+###+..+###+.",
    "....+##+...+##+.",
    ".....++.....++..",
};

typedef struct PortWin {
    struct BmpWin *win;
    u8 x, y, w, h;
} PortWin;

/* One page's windows: a label and up to three choices per row. */
static struct {
    int page;
    struct OptionMenuScreen *screen;
    PortWin label[OPTION_COUNT];
    PortWin choice[OPTION_COUNT][3];
    PortWin bar[BAR_COUNT];     /* the page bar: arrow, name, arrow */
    int flash[2];               /* frames an arrow stays lit */
    int built;                  /* the page's windows exist */
    struct GfFont *font;        /* the screen's own font (OPTION_MENU_Init's) */
} g;

static const PortRow *page_rows(int page, int *n)
{
    switch (page) {
    case PAGE_DISPLAY:
        *n = (int)(sizeof k_display_rows / sizeof k_display_rows[0]);
        return k_display_rows;
    case PAGE_SOUND:
        *n = (int)(sizeof k_sound_rows / sizeof k_sound_rows[0]);
        return k_sound_rows;
    case PAGE_EXTRAS:
        *n = (int)(sizeof k_extras_rows / sizeof k_extras_rows[0]);
        return k_extras_rows;
    }
    *n = 0;
    return 0;
}

/* ------------------------------------------------------------------ */
/* Text                                                                */
/* ------------------------------------------------------------------ */

/* A string object from ASCII: code units, '\n' as the game's new line
 * (0xFFFE), then the end code. */
static struct Strbuf *make_text(const char *s)
{
    u16 units[TEXT_MAX + 1];
    struct Strbuf *str;
    int n = 0;

    while (*s != '\0' && n < TEXT_MAX) {
        units[n++] = *s == '\n' ? 0xFFFE : (u16)(unsigned char)*s;
        s++;
    }
    units[n++] = 0xFFFF;
    str = STRBUF_Create(TEXT_MAX + 1, HEAP_ID);
    STRBUF_SetStringN(str, units, (u32)n);
    return str;
}

static void step_text(const PortRow *row, int v, char *out)
{
    int i = 0, d = 1000;

    if (v == 0 && row->zero != 0) {
        const char *z = row->zero;

        while (*z != '\0') {
            out[i++] = *z++;
        }
        out[i] = '\0';
        return;
    }
    if (v < 0) {
        v = 0;
    }
    while (d > 1 && v < d) {
        d /= 10;
    }
    for (; d > 0; d /= 10) {
        out[i++] = (char)('0' + (v / d) % 10);
    }
    out[i++] = row->unit[0];
    out[i] = '\0';
}

/* A window on the list's layer with one text in it, as the game makes its
 * label windows (OPTION_MENU_CreateLabelWindows). */
static void make_win(PortWin *pw, int x, int y, int w, int h, const char *text,
                     int pad, struct GfFont *font)
{
    struct Strbuf *str = make_text(text);

    pw->x = (u8)x;
    pw->y = (u8)y;
    pw->w = (u8)w;
    pw->h = (u8)h;
    pw->win = BMP_WIN_Create(1, (u32)x, (u32)y, (u8)w, (u8)h, 0, 1);
    BMP_Clear(BMP_WIN_GetBmp(pw->win), 0);
    PRINTSYS_Print(BMP_WIN_GetBmp(pw->win), (s16)pad, 0, str, font);
    BMP_WIN_TransferChars(pw->win);
    BMP_WIN_MakeScreen(pw->win);
    BG_SYS_MarkTileDirty(BMP_WIN_GetBgLayer(pw->win));
    STRBUF_Delete(str);
}

static void drop_win(PortWin *pw)
{
    if (pw->win != 0) {
        BMP_WIN_ClearScreen(pw->win);
        BG_SYS_MarkTileDirty(BMP_WIN_GetBgLayer(pw->win));
        BMP_WIN_Delete(pw->win);
        pw->win = 0;
    }
}

static void color_win(const OptionMenuList *list, const PortWin *pw, int pal)
{
    if (pw->win != 0) {
        BG_SYS_FillTileMap((u8)list->mainLayer, pw->x, pw->y, pw->w, pw->h, (u8)pal);
    }
}

/* An arrow window: the bag's arrow, mirrored for the right one. */
static void make_arrow(PortWin *pw, int x, int right)
{
    struct Bmp *bmp;
    int r, c;

    pw->x = (u8)x;
    pw->y = (u8)ROW_TILE_Y(TAB_ROW);
    pw->w = ARROW_TILE_W;
    pw->h = 2;
    pw->win = BMP_WIN_Create(1, pw->x, pw->y, pw->w, pw->h, 0, 1);
    bmp = BMP_WIN_GetBmp(pw->win);
    BMP_Clear(bmp, 0);
    for (r = 0; r < 12; r++) {
        for (c = 0; c < 16; c++) {
            char ch = k_arrow_left[r][right ? 15 - c : c];

            if (ch != '.') {
                BMP_FillRect(bmp, (u32)c, (u32)(r + 2), 1, 1, ch == '#' ? 15 : 14);
            }
        }
    }
    BMP_WIN_TransferChars(pw->win);
    BMP_WIN_MakeScreen(pw->win);
    BG_SYS_MarkTileDirty(BMP_WIN_GetBgLayer(pw->win));
}

/* The page's name, centred in the bar. */
static void make_name(void)
{
    struct Strbuf *str = make_text(k_page_names[g.page]);
    int pad = (NAME_TILE_W * 8 - (int)PRINTSYS_MeasureWidestLine(str, g.font, 0)) / 2;

    STRBUF_Delete(str);
    drop_win(&g.bar[BAR_NAME]);
    make_win(&g.bar[BAR_NAME], NAME_TILE_X, ROW_TILE_Y(TAB_ROW), NAME_TILE_W, 2,
             k_page_names[g.page], pad < 0 ? 0 : pad, g.font);
}

/* The page bar: made with the screen's first frame, kept on every page. */
static void build_tabs(void)
{
    if (g.bar[BAR_NAME].win != 0) {
        return;
    }
    make_arrow(&g.bar[BAR_LEFT], k_bar_tile[BAR_LEFT], 0);
    make_name();
    make_arrow(&g.bar[BAR_RIGHT], k_bar_tile[BAR_RIGHT], 1);
    g.flash[0] = g.flash[1] = 0;
}

/* The name bright, as a chosen value is; the arrows dim, lit while pressed. */
static void color_tabs(const OptionMenuList *list)
{
    color_win(list, &g.bar[BAR_NAME], 0);
    color_win(list, &g.bar[BAR_LEFT], g.flash[0] > 0 ? 0 : 3);
    color_win(list, &g.bar[BAR_RIGHT], g.flash[1] > 0 ? 0 : 3);
}

/* The port page's windows: the value of a stepper is written into its
 * middle window, so a step rebuilds that window only. */
static void build_value(int r, const PortRow *row, struct GfFont *font)
{
    char text[16];

    step_text(row, eidos_settings_get(row->option), text);
    drop_win(&g.choice[r][1]);
    make_win(&g.choice[r][1], k_col3_tile[1], ROW_TILE_Y(r), CHOICE3_TILE_W, 2,
             text, k_col3_pad[1], font);
}

static void build_page(int page)
{
    struct GfFont *font = g.font;
    const PortRow *rows;
    int n, r, c;

    rows = page_rows(page, &n);
    for (r = 0; r < n; r++) {
        const PortRow *row = &rows[r];

        make_win(&g.label[r], LABEL_TILE_X, ROW_TILE_Y(r), LABEL_TILE_W, 2,
                 row->label, 0, font);
        if (row->kind == ROW_STEP) {
            make_win(&g.choice[r][0], k_col3_tile[0], ROW_TILE_Y(r), CHOICE3_TILE_W, 2,
                     "<", k_col3_pad[0], font);
            build_value(r, row, font);
            make_win(&g.choice[r][2], k_col3_tile[2], ROW_TILE_Y(r), CHOICE3_TILE_W, 2,
                     ">", k_col3_pad[2], font);
        } else if (row->count == 3) {
            for (c = 0; c < 3; c++) {
                make_win(&g.choice[r][c], k_col3_tile[c], ROW_TILE_Y(r), CHOICE3_TILE_W, 2,
                         row->names[c], k_col3_pad[c], font);
            }
        } else {
            for (c = 0; c < 2; c++) {
                make_win(&g.choice[r][c], k_col2_tile[c], ROW_TILE_Y(r), CHOICE2_TILE_W, 2,
                         row->names[c], 0, font);
            }
        }
    }
    g.built = 1;
}

static void drop_page(void)
{
    int r, c;

    for (r = 0; r < OPTION_COUNT; r++) {
        drop_win(&g.label[r]);
        for (c = 0; c < 3; c++) {
            drop_win(&g.choice[r][c]);
        }
    }
    g.built = 0;
}

/* The game's sixteen label windows: off the screen on a port page, back
 * on the game's. */
static void game_windows(int show)
{
    int i;

    if (g.screen == 0) {
        return;
    }
    for (i = 0; i < OPTION_WINDOW_LABEL_COUNT; i++) {
        struct BmpWin *w = OPTION_MENU_GetScreenWindow(g.screen, i);

        if (w == 0) {
            continue;
        }
        if (show) {
            BMP_WIN_MakeScreen(w);
        } else {
            BMP_WIN_ClearScreen(w);
        }
        BG_SYS_MarkTileDirty(BMP_WIN_GetBgLayer(w));
    }
}

/* The help box (the game's text box on the top screen), printed at once
 * as the game prints a row's help without the stream. */
static void port_help(OptionMenuTextBox *box, const char *text)
{
    u16 units[TEXT_MAX + 1];
    int n = 0;

    while (*text != '\0' && n < TEXT_MAX) {
        units[n++] = *text == '\n' ? 0xFFFE : (u16)(unsigned char)*text;
        text++;
    }
    units[n++] = 0xFFFF;
    BMP_Clear(BMP_WIN_GetBmp(box->window), (u8)box->fillColor);
    if (box->stream != 0) {
        PRINTSYS_PrintStreamDelete(box->stream);
        box->stream = 0;
    }
    STRBUF_SetStringN(box->buffer, units, (u32)n);
    PRINTSYS_PrintQue(box->printSys, BMP_WIN_GetBmp(box->printWindow), 0, 0,
                      box->buffer, box->font);
    box->printing = 1;
    box->mode = 2;
}

/* ------------------------------------------------------------------ */
/* A port page: draw                                                   */
/* ------------------------------------------------------------------ */

static int choice_selected(const PortRow *row, int c)
{
    if (row->kind == ROW_STEP) {
        return c == 1;
    }
    return eidos_settings_get(row->option) == row->values[c];
}

static void place_actor(struct ClactActor *actor, int x, int y, int seq)
{
    ClPos pos;

    pos.x = (u16)x;
    pos.y = (u16)y;
    CLACT_AddActorSurfacePos(actor, &pos, 0);
    CLACT_ChangeActorAnimSeq(actor, (u16)seq);
}

static void port_draw(OptionMenuList *list, int highlight)
{
    const PortRow *rows;
    int n, r, c, k, base, a = 0;

    if (list->cursor >= OPTION_COUNT) {
        highlight = 0;
    }
    list->highlight = highlight;
    rows = page_rows(g.page, &n);
    base = ((int)BG_SYS_GetScrollY(list->subLayer) + 0x14) / 0x18;
    BG_SYS_FillTileMap((u8)list->subLayer, 0, 0, 0x20, 0x20, 5);
    for (k = 0; k < OPTION_COUNT; k++) {
        BG_SYS_FillTileMap((u8)list->subLayer, 0, (u8)((base + k) * 3 + 2), 0x20, 3,
                           gOptionMenuRowPalettes[k]);
    }
    for (r = 0; r < n; r++) {
        const PortRow *row = &rows[r];
        int on = list->cursor == r && highlight;
        int cols = row->kind == ROW_STEP ? 3 : row->count;

        color_win(list, &g.label[r], on ? 7 : 0);
        for (c = 0; c < cols; c++) {
            int sel = choice_selected(row, c);

            color_win(list, &g.choice[r][c], on ? (sel ? 7 : 0xC) : (sel ? 0 : 3));
            /* a marker before each choice, as the game's rows have; a
             * stepper's arrows have none */
            if (row->kind == ROW_STEP && c != 1) {
                continue;
            }
            if (a < OPTION_ACTOR_CHOICE_COUNT) {
                int x = cols == 3 ? k_col3_actor[c] : k_col2_actor[c];

                place_actor(list->choiceActors[a++], x, list->scrollY + 24 * r,
                            on ? (sel ? 3 : 2) : (sel ? 0 : 1));
            }
        }
    }
    for (; a < OPTION_ACTOR_CHOICE_COUNT; a++) {
        place_actor(list->choiceActors[a], 300, 0, 1);  /* off the screen */
    }
    if (highlight) {
        BG_SYS_FillTileMap((u8)list->subLayer, 0, (u8)(list->cursor * 3 + 2), 0x20, 3, 7);
    }
    color_tabs(list);
    BG_SYS_MarkTileDirty(list->subLayer);
    BG_SYS_MarkTileDirty(list->mainLayer);
}

/* ------------------------------------------------------------------ */
/* A port page: input                                                  */
/* ------------------------------------------------------------------ */

static void port_change(int r, const PortRow *row, int c, int delta)
{
    int v = eidos_settings_get(row->option);

    if (row->kind == ROW_STEP) {
        v += delta * row->step;
        v = v < row->min ? row->min : (v > row->max ? row->max : v);
    } else if (c >= 0) {
        v = row->values[c];
    } else {
        for (c = 0; c < row->count; c++) {
            if (row->values[c] == v) {
                break;
            }
        }
        c = c >= row->count ? 0 : c + delta;
        c = c < 0 ? 0 : (c >= row->count ? row->count - 1 : c);
        v = row->values[c];
    }
    if (v == eidos_settings_get(row->option)) {
        return;
    }
    GFL_USE_PlaySeDefaultVolume(OPTION_SE_CURSOR);
    if (eidos_settings_set(row->option, v) == 0) {
        (void)eidos_settings_commit();
    }
    if (row->kind == ROW_STEP) {
        build_value(r, row, g.font);
    }
}

static void port_show_help(OptionMenuList *list, OptionMenuTextBox *help)
{
    const PortRow *rows;
    int n;

    rows = page_rows(g.page, &n);
    if (list->cursor >= 0 && list->cursor < n) {
        BG_SYS_SetPlaneVisible(4, 1);
        port_help(help, rows[list->cursor].help);
    } else {
        BG_SYS_SetPlaneVisible(4, 0);
    }
}

/* The row and choice under a touch point, as the game's rects are. */
static int port_touch(OptionMenuList *list, const OptionMenuPoint *t, int *col)
{
    const PortRow *rows;
    int n, r, c;

    rows = page_rows(g.page, &n);
    for (r = 0; r < n; r++) {
        int y = list->scrollY + 24 * r;
        int cols = rows[r].kind == ROW_STEP ? 3 : rows[r].count;

        if (t->y < y || t->y >= y + 24) {
            continue;
        }
        *col = -1;
        for (c = 0; c < cols; c++) {
            int x = cols == 3 ? k_col3_rect[c] : k_col2_rect[c];
            int w = cols == 3 ? 48 : 72;

            if (t->x >= x && t->x < x + w) {
                *col = c;
            }
        }
        return r;
    }
    return -1;
}

/* -1 or +1 for a touch on an arrow of the page bar (the left or the
 * right quarter of it, so the arrows are easy to hit), else 0.  The bar
 * moves with the list (OPTION_MENU_ScrollList), so its place is the list's
 * scroll plus its row. */
static int arrow_at(const OptionMenuList *list, const OptionMenuPoint *t)
{
    int y = list->scrollY + 24 * TAB_ROW;

    if (t->y < y || t->y >= y + 24) {
        return 0;
    }
    if (t->x < 64) {
        return -1;
    }
    if (t->x >= 192) {
        return 1;
    }
    return 0;
}

static void port_update(OptionMenuList *list, const OptionMenuInput *input,
                        OptionMenuTextBox *help, struct OptionMenuScreen *screen)
{
    const PortRow *rows;
    OptionMenuPoint touch;
    int n, r, col;
    int moved = 0;

    rows = page_rows(g.page, &n);
    switch (OPTION_MENU_GetInputCommand(input)) {
    case OPTION_INPUT_KEY_ACTIVATE:
        list->cursor = list->cursor > OPTION_CURSOR_CANCEL ? OPTION_CURSOR_CANCEL
                     : (list->cursor < 0 ? 0 : list->cursor);
        if (list->cursor >= n && list->cursor < OPTION_COUNT) {
            list->cursor = n - 1;
        }
        moved = 1;
        break;
    case OPTION_INPUT_HOLD_UP:
        if ((list->repeatCounter)++ < 10) {
            break;
        }
        /* fall through */
    case OPTION_INPUT_UP:
        list->repeatCounter = 0;
        list->cursor = list->cursor == 0 ? OPTION_CURSOR_CANCEL
                     : (list->cursor == OPTION_CURSOR_CONFIRM ? n - 1 : list->cursor - 1);
        GFL_USE_PlaySeDefaultVolume(OPTION_SE_CURSOR);
        OPTION_MENU_RestartScreenBlink(screen);
        moved = 1;
        break;
    case OPTION_INPUT_HOLD_DOWN:
        if ((list->repeatCounter)++ < 10) {
            break;
        }
        /* fall through */
    case OPTION_INPUT_DOWN:
        list->repeatCounter = 0;
        list->cursor = list->cursor == OPTION_CURSOR_CANCEL ? 0
                     : (list->cursor == n - 1 ? OPTION_CURSOR_CONFIRM : list->cursor + 1);
        GFL_USE_PlaySeDefaultVolume(OPTION_SE_CURSOR);
        OPTION_MENU_RestartScreenBlink(screen);
        moved = 1;
        break;
    case OPTION_INPUT_TOUCH:
        OPTION_MENU_GetInputPoint(input, 0, &touch);
        r = port_touch(list, &touch, &col);
        if (r < 0) {
            break;
        }
        list->cursor = r;
        if (col >= 0) {
            if (rows[r].kind == ROW_STEP) {
                if (col != 1) {
                    port_change(r, &rows[r], -1, col == 0 ? -1 : 1);
                }
            } else {
                port_change(r, &rows[r], col, 0);
            }
        }
        if (UI_GetInputMode() == 0) {
            UI_SetInputMode(1);
        }
        port_draw(list, 0);
        OPTION_MENU_RestartScreenBlink(screen);
        port_show_help(list, help);
        break;
    case OPTION_INPUT_RIGHT:
    case OPTION_INPUT_LEFT: {
        int d = OPTION_MENU_GetInputCommand(input) == OPTION_INPUT_RIGHT ? 1 : -1;

        if (list->cursor >= 0 && list->cursor < n) {
            port_change(list->cursor, &rows[list->cursor], -1, d);
            port_draw(list, 1);
            OPTION_MENU_RestartScreenBlink(screen);
        } else if (list->cursor == OPTION_CURSOR_CONFIRM && d > 0) {
            list->cursor = OPTION_CURSOR_CANCEL;
            GFL_USE_PlaySeDefaultVolume(OPTION_SE_CURSOR);
        } else if (list->cursor == OPTION_CURSOR_CANCEL && d < 0) {
            list->cursor = OPTION_CURSOR_CONFIRM;
            GFL_USE_PlaySeDefaultVolume(OPTION_SE_CURSOR);
        }
        break;
    }
    case OPTION_INPUT_SHORTCUT_X:
        port_draw(list, 1);
        break;
    case OPTION_INPUT_CONFIRM:
    case OPTION_INPUT_CANCEL:
        port_draw(list, 0);
        break;
    }
    if (moved) {
        port_draw(list, 1);
        port_show_help(list, help);
    }
    OPTION_MENU_IsTextDone(help);
    OPTION_MENU_ShiftInputTrail(list);
}

/* ------------------------------------------------------------------ */
/* Changing the page                                                   */
/* ------------------------------------------------------------------ */

static void set_page(OptionMenuList *list, OptionMenuTextBox *help, int page)
{
    int n;

    if (page == g.page) {
        return;
    }
    GFL_USE_PlaySeDefaultVolume(OPTION_SE_CURSOR);
    if (g.page == PAGE_GAME) {
        game_windows(0);
    } else {
        drop_page();
    }
    g.page = page;
    eidos_ui_opt_page = page;
    make_name();
    if (page == PAGE_GAME) {
        game_windows(1);
        /* the markers back where the game's rows have them */
        OPTION_MENU_ScrollList(list, 0, -192, 192);
        OPTION_MENU_DrawList(list, UI_GetInputMode() == 1 ? 0 : 1);
        list->redraw = 1;
        return;
    }
    build_page(page);
    (void)page_rows(page, &n);
    if (list->cursor >= n && list->cursor < OPTION_COUNT) {
        list->cursor = n - 1;
    }
    port_draw(list, UI_GetInputMode() == 1 ? 0 : 1);
    port_show_help(list, help);
}

/* L and R, and a touch on an arrow.  1 when the page changed. */
static int page_input(OptionMenuList *list, const OptionMenuInput *input,
                      OptionMenuTextBox *help)
{
    u32 trg = KEY_GetTrg();
    OptionMenuPoint touch;
    int d = 0;

    if (eidos_ui_opt_page_request >= 0) {
        /* a tab clicked in the one-screen layout: straight to its page */
        int p = eidos_ui_opt_page_request;

        eidos_ui_opt_page_request = -1;
        if (p >= 0 && p < PAGE_COUNT && p != g.page) {
            set_page(list, help, p);
            color_tabs(list);
            BG_SYS_MarkTileDirty(list->mainLayer);
            return 1;
        }
    }
    if (trg & KEY_R) {
        d = 1;
    } else if (trg & KEY_L) {
        d = -1;
    } else if (OPTION_MENU_GetInputCommand(input) == OPTION_INPUT_TOUCH) {
        OPTION_MENU_GetInputPoint(input, 0, &touch);
        d = arrow_at(list, &touch);
    }
    if (d == 0) {
        return 0;
    }
    g.flash[d < 0 ? 0 : 1] = FLASH_FRAMES;
    set_page(list, help, (g.page + PAGE_COUNT + d) % PAGE_COUNT);
    color_tabs(list);
    BG_SYS_MarkTileDirty(list->mainLayer);
    return 1;
}

/* An arrow goes dim again when its flash is over. */
static void flash_tick(OptionMenuList *list)
{
    int i, done = 0;

    for (i = 0; i < 2; i++) {
        if (g.flash[i] > 0 && --g.flash[i] == 0) {
            done = 1;
        }
    }
    if (done) {
        color_tabs(list);
        BG_SYS_MarkTileDirty(list->mainLayer);
    }
}

/* ------------------------------------------------------------------ */
/* The game's own code, with the pages around it                       */
/* ------------------------------------------------------------------ */

/* OPTION_MENU_UpdateList as the game has it (src/game/ov068/option_menu.c). */
static void game_update_list(OptionMenuList *list, const OptionMenuInput *input,
                             OptionMenuTextBox *helpText, struct OptionMenuScreen *screen)
{
    OptionMenuPoint touch;
    int command;
    int useStream;
    u32 wait;

    command = OPTION_MENU_GetInputCommand(input);
    BG_SYS_GetScrollY(list->subLayer);
    switch (command) {
    case OPTION_INPUT_KEY_ACTIVATE:
        list->cursor = list->cursor > OPTION_CURSOR_CANCEL ? OPTION_CURSOR_CANCEL : (list->cursor < 0 ? 0 : list->cursor);
        OPTION_MENU_DrawList(list, 1);
        list->redraw = 1;
        break;
    case OPTION_INPUT_HOLD_UP:
        if ((list->repeatCounter)++ < 10)
            break;
        /* fall through */
    case OPTION_INPUT_UP:
        list->repeatCounter = 0;
        list->cursor = list->cursor == 0 ? OPTION_CURSOR_CANCEL : list->cursor - 1;
        list->redraw = 1;
        GFL_USE_PlaySeDefaultVolume(OPTION_SE_CURSOR);
        OPTION_MENU_DrawList(list, 1);
        OPTION_MENU_RestartScreenBlink(screen);
        break;
    case OPTION_INPUT_HOLD_DOWN:
        if ((list->repeatCounter)++ < 10)
            break;
        /* fall through */
    case OPTION_INPUT_DOWN:
        list->repeatCounter = 0;
        if (list->cursor == OPTION_CURSOR_CANCEL)
            list->cursor = 0;
        else
            (list->cursor)++;
        list->redraw = 1;
        GFL_USE_PlaySeDefaultVolume(OPTION_SE_CURSOR);
        OPTION_MENU_DrawList(list, 1);
        OPTION_MENU_RestartScreenBlink(screen);
        break;
    case OPTION_INPUT_TOUCH:
        OPTION_MENU_GetInputPoint(input, 0, &touch);
        if (touch.y < 0x18 || touch.y > 0xA8)
            break;
        list->cursor = OPTION_MENU_GetRowAtTouch(list, &touch);
        OPTION_MENU_SelectChoiceAtTouch(list, &touch);
        OPTION_MENU_DrawList(list, 0);
        OPTION_MENU_RestartScreenBlink(screen);
        list->redraw = 1;
        if (UI_GetInputMode() == 0)
            UI_SetInputMode(1);
        break;
    case OPTION_INPUT_RIGHT:
        if (list->cursor < OPTION_COUNT) {
            GFL_USE_PlaySeDefaultVolume(OPTION_SE_CURSOR);
            OPTION_MENU_StepSelectedSetting(list, 1);
            OPTION_MENU_DrawList(list, 1);
            OPTION_MENU_RestartScreenBlink(screen);
        } else if (list->cursor == OPTION_CURSOR_CONFIRM) {
            (list->cursor)++;
            GFL_USE_PlaySeDefaultVolume(OPTION_SE_CURSOR);
            list->cursor = list->cursor > OPTION_CURSOR_CANCEL ? OPTION_CURSOR_CANCEL : (list->cursor < 0 ? 0 : list->cursor);
        }
        break;
    case OPTION_INPUT_LEFT:
        if (list->cursor < OPTION_COUNT) {
            GFL_USE_PlaySeDefaultVolume(OPTION_SE_CURSOR);
            OPTION_MENU_StepSelectedSetting(list, -1);
            OPTION_MENU_DrawList(list, 1);
            OPTION_MENU_RestartScreenBlink(screen);
        } else if (list->cursor == OPTION_CURSOR_CANCEL) {
            (list->cursor)--;
            GFL_USE_PlaySeDefaultVolume(OPTION_SE_CURSOR);
            list->cursor = list->cursor > OPTION_CURSOR_CANCEL ? OPTION_CURSOR_CANCEL : (list->cursor < 0 ? 0 : list->cursor);
        }
        break;
    case OPTION_INPUT_SHORTCUT_X:
        OPTION_MENU_DrawList(list, 1);
        break;
    case OPTION_INPUT_CONFIRM:
    case OPTION_INPUT_CANCEL:
        OPTION_MENU_DrawList(list, 0);
        break;
    }
    switch (OPTION_MENU_TakeChangedSetting(&list->settings)) {
    case 0:
    case 1:
    case 2:
        list->redraw = 1;
        break;
    }
    if (list->redraw) {
        if (list->cursor >= 0 && list->cursor < OPTION_COUNT) {
            if (list->cursor == 0) {
                useStream = 1;
                wait = OPTION_MENU_GetTextSpeedWait(&list->settings);
            } else {
                useStream = 0;
                wait = 0;
            }
            BG_SYS_SetPlaneVisible(4, 1);
            OPTION_MENU_PrintText(helpText, gOptionMenuSettingRows[list->cursor].descriptionId, wait, useStream);
        } else {
            BG_SYS_SetPlaneVisible(4, 0);
        }
        list->redraw = 0;
    }
    OPTION_MENU_IsTextDone(helpText);
    OPTION_MENU_ShiftInputTrail(list);
}

void OPTION_MENU_UpdateList(OptionMenuList *list, const OptionMenuInput *input,
                            OptionMenuTextBox *helpText, struct OptionMenuScreen *screen,
                            struct OptionMenuButtons *buttons)
{
    (void)buttons;
    g.screen = screen;
    g.font = helpText->font;
    if (g.bar[BAR_NAME].win == 0) {
        build_tabs();
        color_tabs(list);
        BG_SYS_MarkTileDirty(list->mainLayer);
    }
    flash_tick(list);
    if (page_input(list, input, helpText)) {
        OPTION_MENU_IsTextDone(helpText);
        OPTION_MENU_ShiftInputTrail(list);
        return;
    }
    if (g.page == PAGE_GAME) {
        game_update_list(list, input, helpText, screen);
    } else {
        port_update(list, input, helpText, screen);
    }
}

/* OPTION_MENU_DrawList as the game has it, then the page names. */
static void game_draw_list(OptionMenuList *list, int highlight)
{
    int base;
    u32 k;
    int i;

    if (list->cursor >= OPTION_COUNT)
        highlight = 0;
    list->highlight = highlight;
    base = ((int)BG_SYS_GetScrollY(list->subLayer) + 0x14) / 0x18;
    BG_SYS_FillTileMap((u8)(list->subLayer), 0, 0, 0x20, 0x20, 5);
    i = 0;
    for (k = 0; k < OPTION_COUNT; k++)
        BG_SYS_FillTileMap((u8)(list->subLayer), (u8)i, (u8)((base + (int)k) * 3 + 2), 0x20, 3, gOptionMenuRowPalettes[k]);
    for (i = 0; i < OPTION_ACTOR_CHOICE_COUNT; i++) {
        if ((u32)list->cursor == gOptionMenuChoices[i].settingIndex && highlight) {
            if (OPTION_MENU_IsChoiceSelected(&list->settings, i))
                BG_SYS_FillTileMap((u8)(list->mainLayer), gOptionMenuLabels[i].x, gOptionMenuLabels[i].y, gOptionMenuLabels[i].width, gOptionMenuLabels[i].height, 7);
            else
                BG_SYS_FillTileMap((u8)(list->mainLayer), gOptionMenuLabels[i].x, gOptionMenuLabels[i].y, gOptionMenuLabels[i].width, gOptionMenuLabels[i].height, 0xC);
        } else {
            if (OPTION_MENU_IsChoiceSelected(&list->settings, i))
                BG_SYS_FillTileMap((u8)(list->mainLayer), gOptionMenuLabels[i].x, gOptionMenuLabels[i].y, gOptionMenuLabels[i].width, gOptionMenuLabels[i].height, 0);
            else
                BG_SYS_FillTileMap((u8)(list->mainLayer), gOptionMenuLabels[i].x, gOptionMenuLabels[i].y, gOptionMenuLabels[i].width, gOptionMenuLabels[i].height, 3);
        }
    }
    for (i = OPTION_ACTOR_CHOICE_COUNT; i < OPTION_WINDOW_LABEL_COUNT; i++) {
        if (i - OPTION_ACTOR_CHOICE_COUNT == list->cursor && highlight)
            BG_SYS_FillTileMap((u8)(list->mainLayer), gOptionMenuLabels[i].x, gOptionMenuLabels[i].y, gOptionMenuLabels[i].width, gOptionMenuLabels[i].height, 7);
        else
            BG_SYS_FillTileMap((u8)(list->mainLayer), gOptionMenuLabels[i].x, gOptionMenuLabels[i].y, gOptionMenuLabels[i].width, gOptionMenuLabels[i].height, 0);
    }
    if (highlight)
        BG_SYS_FillTileMap((u8)(list->subLayer), 0, (u8)(list->cursor * 3 + 2), 0x20, 3, 7);
    BG_SYS_MarkTileDirty((u8)(list->subLayer));
    BG_SYS_MarkTileDirty((u8)(list->mainLayer));
    for (i = 0; i < OPTION_ACTOR_CHOICE_COUNT; i++) {
        if ((u32)list->cursor == gOptionMenuChoices[i].settingIndex && highlight) {
            if (OPTION_MENU_IsChoiceSelected(&list->settings, i))
                CLACT_ChangeActorAnimSeq(list->choiceActors[i], 3);
            else
                CLACT_ChangeActorAnimSeq(list->choiceActors[i], 2);
        } else {
            if (OPTION_MENU_IsChoiceSelected(&list->settings, i))
                CLACT_ChangeActorAnimSeq(list->choiceActors[i], 0);
            else
                CLACT_ChangeActorAnimSeq(list->choiceActors[i], 1);
        }
    }
}

void OPTION_MENU_DrawList(OptionMenuList *list, int highlight)
{
    if (g.page == PAGE_GAME) {
        game_draw_list(list, highlight);
        if (g.bar[BAR_NAME].win != 0) {
            color_tabs(list);
        }
    } else {
        port_draw(list, highlight);
    }
}

/* OPTION_MENU_ExitList, with the port's windows deleted first: the screen's
 * heap is checked for leaks when it is deleted (OPTION_MENU_Exit). */
void OPTION_MENU_ExitList(OptionMenuList *list)
{
    int i;

    drop_page();
    for (i = 0; i < BAR_COUNT; i++) {
        drop_win(&g.bar[i]);
    }
    g.page = PAGE_GAME;
    eidos_ui_opt_page = PAGE_GAME;
    eidos_ui_opt_page_request = -1;
    g.screen = 0;
    MI_CpuFill8(list, 0, sizeof(OptionMenuList));
}
