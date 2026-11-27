/*
 * tools/review/eidos_review.c -- the theme review app: every device's view
 * of one screen of the one-screen interface, in one style, side by side,
 * with comments kept in a file.
 *
 *   top      the style (one at a time) and the screen type (Start, Field,
 *            Menu, Bag, Battle, PC and trades)
 *   left     the states of that type (each picture the tour takes)
 *   middle   the state on every device: its window size, its safe area,
 *            touch on phones and tablets, the console's button icons
 *   right    the comments on the state: on the whole screen or on the view
 *            picked
 *
 * A view is the game's own picture of that state on that device: a hidden
 * eidos_boot at the device's window size plays the state's steps (the
 * screens file) and takes the picture itself (the test script's `shot`),
 * then ends.  Pictures are kept in the work folder, so a state is computed
 * once per build; at most --jobs games run at once.  Double-click a view to
 * see it large; there, Live opens it in a game stopped on that state
 * (script step `freeze`, shown through the embed mapping of
 * engine/platform/embed_shm.h) where the pointer works.
 *
 *     eidos_review --screens FILE --settings FILE --work DIR [--exe PATH] [--jobs N]
 *
 * Tests: --shot PATH --after SECS [--style NAME] [--type NAME] [--state NAME]
 * draws the window hidden SECS seconds after the start (computing the state
 * meanwhile), writes it as a BMP and exits.  Windows only (the processes).
 */

#define SDL_MAIN_HANDLED
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "embed_shm.h"

typedef uint8_t u8;
typedef uint32_t u32;

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <direct.h>
#include <process.h>
#endif

#define MAX_STATES  1024
#define MAX_STEPS   200
#define MAX_JOBS    8
#define MAX_TYPES   12

/* ------------------------------------------------------------------ */
/* Look                                                                */
/* ------------------------------------------------------------------ */

#define C_BG      0x15171Cu
#define C_PANEL   0x1D2027u
#define C_PANEL2  0x252932u
#define C_LINE    0x323743u
#define C_TEXT    0xE6E8ECu
#define C_MUTED   0x9AA1AEu
#define C_ACCENT  0x4A94FFu
#define C_PICK    0xFFD24Au
#define C_GOOD    0x63DE31u
#define C_WARN    0xFFBD29u
#define C_BAD     0xFF5231u

/* The text: Segoe UI at the size the window wants (bigger on a 1440p
 * window), drawn by the system, anti-aliased. */
static int g_text_px = 15;
#define FK        (g_text_px / 7)       /* layout steps that follow the text size */

/* ------------------------------------------------------------------ */
/* Devices                                                             */
/* ------------------------------------------------------------------ */

/*
 * Each device as it holds the game: its window in pixels (landscape), the
 * part the system covers on each side (a notch, rounded corners, the home
 * indicator; from the devices' published safe areas, landscape, rounded),
 * touch first, or a pad of a family (padglyph.h UI_PAD_*, -1 the keyboard).
 */
typedef struct Device {
    const char *group;
    const char *name;
    int w, h;
    int safe_l, safe_t, safe_r, safe_b;
    int touch;
    int pad;
    int shown;          /* in the default view: every Switch, iOS, Android and iPad view, and of the rest the
                         * ones that look clearly different (another shape, size class or button set) */
} Device;

static const Device k_devices[] = {
    { "PC", "720p", 1280, 720, 0, 0, 0, 0, 0, -1, 1 },
    { "PC", "768p", 1366, 768, 0, 0, 0, 0, 0, -1, 0 },
    { "PC", "900p", 1600, 900, 0, 0, 0, 0, 0, -1, 0 },
    { "PC", "1080p", 1920, 1080, 0, 0, 0, 0, 0, -1, 1 },
    { "PC", "1440p", 2560, 1440, 0, 0, 0, 0, 0, -1, 0 },
    { "PC", "4K", 3840, 2160, 0, 0, 0, 0, 0, -1, 0 },
    { "PC", "16:10 1200p", 1920, 1200, 0, 0, 0, 0, 0, -1, 1 },
    { "PC", "16:10 1600p", 2560, 1600, 0, 0, 0, 0, 0, -1, 0 },
    { "PC", "4:3 1200p", 1600, 1200, 0, 0, 0, 0, 0, -1, 1 },
    { "PC", "21:9 1440p", 3440, 1440, 0, 0, 0, 0, 0, -1, 1 },
    { "PC", "32:9 1080p", 3840, 1080, 0, 0, 0, 0, 0, -1, 1 },
    { "Handheld", "Steam Deck", 1280, 800, 0, 0, 0, 0, 0, 7, 1 },
    { "Handheld", "Switch handheld", 1280, 720, 0, 0, 0, 0, 0, 5, 1 },
    { "Handheld", "Switch docked", 1920, 1080, 0, 0, 0, 0, 0, 5, 1 },
    { "Handheld", "Vita", 960, 544, 0, 0, 0, 0, 0, 3, 1 },
    { "Handheld", "ROG Ally", 1920, 1080, 0, 0, 0, 0, 0, 1, 0 },
    { "Phone", "iPhone SE", 1334, 750, 0, 0, 0, 0, 1, -1, 1 },
    { "Phone", "iPhone 15", 2556, 1179, 177, 0, 177, 63, 1, -1, 1 },
    { "Phone", "iPhone 15 Pro Max", 2796, 1290, 177, 0, 177, 63, 1, -1, 1 },
    { "Phone", "Pixel 8", 2400, 1080, 100, 0, 0, 0, 1, -1, 1 },
    { "Phone", "Galaxy S24", 2340, 1080, 90, 0, 0, 0, 1, -1, 1 },
    { "Phone", "Xperia 21:9", 2520, 1080, 0, 0, 0, 0, 1, -1, 1 },
    { "Phone", "Galaxy Fold inner", 2176, 1812, 0, 0, 0, 0, 1, -1, 1 },
    { "Tablet", "iPad", 2360, 1640, 0, 0, 0, 40, 1, -1, 1 },
    { "Tablet", "iPad Pro 12.9", 2732, 2048, 0, 0, 0, 40, 1, -1, 1 },
    { "Tablet", "iPad mini", 2266, 1488, 0, 0, 0, 40, 1, -1, 1 },
    { "Tablet", "Galaxy Tab S9", 2560, 1600, 0, 0, 0, 0, 1, -1, 1 },
};
#define DEVICE_COUNT ((int)(sizeof k_devices / sizeof k_devices[0]))

static const char *const k_groups[] = { "PC", "Handheld", "Phone", "Tablet" };
#define GROUP_COUNT 4
static const char *const k_styles[] = { "kosmos", "plasma", "harmonia" };
static const char *const k_style_names[] = { "Kosmos", "Plasma", "Harmonia" };

/* ------------------------------------------------------------------ */
/* States                                                              */
/* ------------------------------------------------------------------ */

typedef struct State {
    char type[24];
    char name[40];
    int style;
    char *env[8];
    int nenv;
    char *steps[MAX_STEPS];
    int nsteps;
} State;

static State g_states[MAX_STATES];
static int g_nstates;
static char g_types[MAX_TYPES][24];
static int g_ntypes;
static char g_save[600], g_warp[64], g_demo[600];

static char *dup_line(const char *s)
{
    size_t n = strlen(s);
    char *d = (char *)malloc(n + 1);

    if (d != NULL) {
        memcpy(d, s, n + 1);
    }
    return d;
}

static void chomp(char *s)
{
    size_t n = strlen(s);

    while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r')) {
        s[--n] = '\0';
    }
}

static int style_of(const char *s)
{
    int i;

    for (i = 0; i < 3; i++) {
        if (strcmp(s, k_styles[i]) == 0) {
            return i;
        }
    }
    return -1;
}

static int load_states(const char *path)
{
    FILE *f = fopen(path, "rb");
    char line[1024];
    State *cur = NULL;

    if (f == NULL) {
        return -1;
    }
    while (fgets(line, sizeof line, f) != NULL) {
        chomp(line);
        if (strncmp(line, "save ", 5) == 0) {
            snprintf(g_save, sizeof g_save, "%s", line + 5);
        } else if (strncmp(line, "warp ", 5) == 0) {
            snprintf(g_warp, sizeof g_warp, "%s", line + 5);
        } else if (strncmp(line, "demo ", 5) == 0) {
            snprintf(g_demo, sizeof g_demo, "%s", line + 5);
        } else if (strncmp(line, "state ", 6) == 0 && g_nstates < MAX_STATES) {
            char style[16];
            int i, known = 0, nf;

            cur = &g_states[g_nstates];
            memset(cur, 0, sizeof *cur);
            nf = sscanf(line + 6, "%23s %39s %15s", cur->type, cur->name, style);
            if (nf < 3 || (cur->style = style_of(style)) < 0) {
                cur = NULL;
                continue;
            }
            for (i = 0; cur->type[i] != '\0'; i++) {
                if (cur->type[i] == '_') {
                    cur->type[i] = ' ';
                }
            }
            g_nstates++;
            for (i = 0; i < g_ntypes; i++) {
                known |= strcmp(g_types[i], cur->type) == 0;
            }
            if (!known && g_ntypes < MAX_TYPES) {
                snprintf(g_types[g_ntypes++], sizeof g_types[0], "%s", cur->type);
            }
        } else if (strncmp(line, "env ", 4) == 0 && cur != NULL && cur->nenv < 8) {
            cur->env[cur->nenv++] = dup_line(line + 4);
        } else if (strncmp(line, "step ", 5) == 0 && cur != NULL && cur->nsteps < MAX_STEPS) {
            cur->steps[cur->nsteps++] = dup_line(line + 5);
        } else if (strcmp(line, "end") == 0) {
            cur = NULL;
        }
    }
    fclose(f);
    return g_nstates > 0 ? 0 : -1;
}

/* ------------------------------------------------------------------ */
/* The app                                                             */
/* ------------------------------------------------------------------ */

typedef struct Tile {
    SDL_Texture *tex;       /* the view's picture, NULL until computed */
    int pw, ph;
    int failed;
    SDL_FRect box;          /* where it was drawn last */
} Tile;

typedef struct Job {
    int state, device;
    intptr_t proc;
    uint32_t started;
} Job;

static struct {
    SDL_Window *win;
    SDL_Renderer *r;
    int W, H;
    char settings[600], work[600], exe[600];
    int jobs_max;
    int style, type, state;     /* the chosen style, type index, state index (into g_states) */
    int group_off[GROUP_COUNT]; /* device groups hidden */
    Tile tiles[DEVICE_COUNT];   /* the views of the chosen state */
    int pick;                   /* the view picked, -1 none */
    int zoom;                   /* the picked view large */
    int list_top;               /* the states list's first row */
    int all_devices;            /* every device, not only the default ones */
    /* the queue of views to compute and the games computing */
    int queue[DEVICE_COUNT * 4][2];
    int nqueue;
    Job jobs[MAX_JOBS];
    /* input */
    float mx, my;
    int pressed, dbl, wheel;
    int typing;
    char text[600];
    char note[200];
    uint32_t note_at;
    /* comments on the chosen state */
    char comments[40][320];
    int ncomments;
    /* live: the picked view in a stopped game */
#ifdef _WIN32
    HANDLE live_map;
#endif
    EidosEmbedShm *live;
    intptr_t live_proc;
    SDL_Texture *live_tex;
    int live_w, live_h;
    uint32_t live_frame;
} A;

static void say(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(A.note, sizeof A.note, fmt, ap);
    va_end(ap);
    A.note_at = SDL_GetTicks();
    fprintf(stderr, "eidos_review: %s\n", A.note);
}

static int hover(float x, float y, float w, float h)
{
    return A.mx >= x && A.my >= y && A.mx < x + w && A.my < y + h;
}

static int clicked(float x, float y, float w, float h)
{
    if (A.pressed && hover(x, y, w, h)) {
        A.pressed = 0;
        return 1;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Drawing: flat shapes, and text the system draws                      */
/* ------------------------------------------------------------------ */

static void fill(int x, int y, int w, int h, u32 rgb, int a)
{
    SDL_FRect r = { (float)x, (float)y, (float)w, (float)h };

    if (w <= 0 || h <= 0) {
        return;
    }
    SDL_SetRenderDrawBlendMode(A.r, a < 255 ? SDL_BLENDMODE_BLEND : SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(A.r, (u8)(rgb >> 16), (u8)(rgb >> 8), (u8)rgb, (u8)a);
    SDL_RenderFillRect(A.r, &r);
}

static void outline(int x, int y, int w, int h, u32 rgb)
{
    SDL_FRect r = { (float)x + 0.5f, (float)y + 0.5f, (float)w - 1.0f, (float)h - 1.0f };

    SDL_SetRenderDrawBlendMode(A.r, SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(A.r, (u8)(rgb >> 16), (u8)(rgb >> 8), (u8)rgb, 255);
    SDL_RenderRect(A.r, &r);
}

/* A box: its fill and a one-pixel edge. */
static void panel(int x, int y, int w, int h, u32 fill_rgb, u32 edge)
{
    fill(x, y, w, h, fill_rgb, 255);
    outline(x, y, w, h, edge);
}

#ifdef _WIN32
/*
 * One line of text as a texture: drawn white on black by GDI in Segoe UI,
 * its grey taken as the alpha (anti-aliased), kept in a small cache by
 * string, size and weight, and tinted when drawn.
 */
typedef struct TextTex {
    char s[200];
    int px, bold;
    SDL_Texture *tex;
    int w, h;
    uint32_t used;
} TextTex;

static TextTex g_tt[600];
static HDC g_dc;
static HFONT g_font[2][64];
static uint32_t g_tt_clock;

static HFONT font_of(int px, int bold)
{
    if (px < 8) {
        px = 8;
    }
    if (px > 63) {
        px = 63;
    }
    if (g_font[bold][px] == NULL) {
        g_font[bold][px] = CreateFontW(-px, 0, 0, 0, bold ? FW_SEMIBOLD : FW_NORMAL, FALSE, FALSE, FALSE,
                                       DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
                                       DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
    }
    return g_font[bold][px];
}

static void text_extent(const char *s, int px, int bold, int *w, int *h)
{
    WCHAR ws[256];
    int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, ws, 256);
    SIZE sz = { 0, 0 };

    if (g_dc == NULL) {
        g_dc = CreateCompatibleDC(NULL);
    }
    SelectObject(g_dc, font_of(px, bold));
    if (n > 1) {
        GetTextExtentPoint32W(g_dc, ws, n - 1, &sz);
    }
    *w = sz.cx;
    *h = sz.cy > 0 ? sz.cy : px + px / 3;
}

static TextTex *text_tex(const char *s, int px, int bold)
{
    int i, oldest = 0, w, h, x, y;
    TextTex *t;
    WCHAR ws[256];
    int n;
    BITMAPINFO bi;
    void *bits = NULL;
    HBITMAP bmp;
    u8 *rgba;

    for (i = 0; i < (int)(sizeof g_tt / sizeof g_tt[0]); i++) {
        if (g_tt[i].tex != NULL && g_tt[i].px == px && g_tt[i].bold == bold && strcmp(g_tt[i].s, s) == 0) {
            g_tt[i].used = ++g_tt_clock;
            return &g_tt[i];
        }
        if (g_tt[i].used < g_tt[oldest].used) {
            oldest = i;
        }
    }
    text_extent(s, px, bold, &w, &h);
    if (w <= 0 || h <= 0) {
        return NULL;
    }
    n = MultiByteToWideChar(CP_UTF8, 0, s, -1, ws, 256);
    memset(&bi, 0, sizeof bi);
    bi.bmiHeader.biSize = sizeof bi.bmiHeader;
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    bmp = CreateDIBSection(g_dc, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (bmp == NULL || bits == NULL) {
        return NULL;
    }
    SelectObject(g_dc, bmp);
    memset(bits, 0, (size_t)w * (size_t)h * 4u);
    SelectObject(g_dc, font_of(px, bold));
    SetBkMode(g_dc, TRANSPARENT);
    SetTextColor(g_dc, RGB(255, 255, 255));
    TextOutW(g_dc, 0, 0, ws, n > 1 ? n - 1 : 0);
    GdiFlush();
    rgba = (u8 *)malloc((size_t)w * (size_t)h * 4u);
    if (rgba == NULL) {
        DeleteObject(bmp);
        return NULL;
    }
    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++) {
            const u8 *p = (const u8 *)bits + ((size_t)y * (size_t)w + (size_t)x) * 4u;
            u8 *q = rgba + ((size_t)y * (size_t)w + (size_t)x) * 4u;
            u8 a = p[0] > p[1] ? (p[0] > p[2] ? p[0] : p[2]) : (p[1] > p[2] ? p[1] : p[2]);

            q[0] = q[1] = q[2] = 255;
            q[3] = a;
        }
    }
    DeleteObject(bmp);
    t = &g_tt[oldest];
    if (t->tex != NULL) {
        SDL_DestroyTexture(t->tex);
    }
    t->tex = SDL_CreateTexture(A.r, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, w, h);
    if (t->tex != NULL) {
        SDL_UpdateTexture(t->tex, NULL, rgba, w * 4);
        SDL_SetTextureBlendMode(t->tex, SDL_BLENDMODE_BLEND);
    }
    free(rgba);
    snprintf(t->s, sizeof t->s, "%s", s);
    t->px = px;
    t->bold = bold;
    t->w = w;
    t->h = h;
    t->used = ++g_tt_clock;
    return t->tex != NULL ? t : NULL;
}

static int text_at(int x, int y, u32 col, const char *s, int px, int bold)
{
    TextTex *t = s[0] != '\0' ? text_tex(s, px, bold) : NULL;
    SDL_FRect r;

    if (t == NULL) {
        return 0;
    }
    r.x = (float)x;
    r.y = (float)y;
    r.w = (float)t->w;
    r.h = (float)t->h;
    SDL_SetTextureColorMod(t->tex, (u8)(col >> 16), (u8)(col >> 8), (u8)col);
    SDL_RenderTexture(A.r, t->tex, NULL, &r);
    return t->w;
}

static int text_w(const char *s)
{
    int w, h;

    text_extent(s, g_text_px, 0, &w, &h);
    return w;
}
#else
static int text_at(int x, int y, u32 col, const char *s, int px, int bold)
{
    (void)px;
    (void)bold;
    SDL_SetRenderDrawColor(A.r, (u8)(col >> 16), (u8)(col >> 8), (u8)col, 255);
    SDL_RenderDebugText(A.r, (float)x, (float)y, s);
    return (int)strlen(s) * 8;
}

static int text_w(const char *s)
{
    return (int)strlen(s) * 8;
}
#endif

static int text(int x, int y, u32 col, const char *s)
{
    return text_at(x, y, col, s, g_text_px, 0);
}

static int text_bold(int x, int y, u32 col, const char *s)
{
    return text_at(x, y, col, s, g_text_px + 1, 1);
}

#define LINE_H (g_text_px + g_text_px / 3)

/* A button: a flat box and its label; returns its width. */
static int button(int x, int y, const char *label, int on, int *hit)
{
    int w = text_w(label) + 2 * g_text_px, h = LINE_H + 10;
    int over = hover((float)x, (float)y, (float)w, (float)h);

    panel(x, y, w, h, on ? 0x2B4E86u : over ? 0x2C313Bu : C_PANEL2, on ? 0x4A7FD0u : over ? 0x4A5263u : C_LINE);
    text(x + g_text_px, y + 5, on ? 0xFFFFFFu : C_TEXT, label);
    *hit = clicked((float)x, (float)y, (float)w, (float)h);
    return w;
}

static void device_label(const Device *d, char *out, size_t n)
{
    snprintf(out, n, "%s  %dx%d", d->name, d->w, d->h);
}

/* ------------------------------------------------------------------ */
/* Files                                                               */
/* ------------------------------------------------------------------ */

#ifdef _WIN32
static void make_dirs(const char *path)
{
    char p[800];
    size_t i;

    snprintf(p, sizeof p, "%s", path);
    for (i = 1; p[i] != '\0'; i++) {
        if (p[i] == '/' || p[i] == '\\') {
            char ch = p[i];

            p[i] = '\0';
            _mkdir(p);
            p[i] = ch;
        }
    }
    _mkdir(p);
}

static void copy_file(const char *from, const char *to)
{
    CopyFileA(from, to, FALSE);
}
#endif

static void cache_dir(int state, char *out, size_t n)
{
    snprintf(out, n, "%s/cache/%s/%s", A.work, k_styles[g_states[state].style], g_states[state].name);
}

static void cache_path(int state, int device, char *out, size_t n)
{
    char dir[700];

    cache_dir(state, dir, sizeof dir);
    snprintf(out, n, "%s/d%02d.bmp", dir, device);
}

/* ------------------------------------------------------------------ */
/* Computing a view: a hidden game at the device's size takes the picture */
/* ------------------------------------------------------------------ */

#ifdef _WIN32
static char **env_with(char *const *add, int nadd)
{
    int n = 0, i, k = 0;
    char **out;

    while (_environ[n] != NULL) {
        n++;
    }
    out = (char **)calloc((size_t)(n + nadd + 1), sizeof *out);
    if (out == NULL) {
        return NULL;
    }
    for (i = 0; i < n; i++) {
        int j, clash = 0;

        for (j = 0; j < nadd; j++) {
            const char *eq = strchr(add[j], '=');
            size_t len = eq != NULL ? (size_t)(eq - add[j]) + 1 : 0;

            clash |= len > 0 && _strnicmp(_environ[i], add[j], len) == 0;
        }
        if (!clash) {
            out[k++] = _environ[i];
        }
    }
    for (i = 0; i < nadd; i++) {
        out[k++] = add[i];
    }
    out[k] = NULL;
    return out;
}

/*
 * A game for state s on device d in folder `dir`: its profile (the
 * settings and the test save), its script (the state's steps, then `last`),
 * and the device's switches.  `hidden`: its own hidden window at the
 * device's size, pictures to `shots`; else shown through the embed mapping
 * `embed`.  The process, or 0.
 */
static intptr_t start_game(int s, int d, const char *dir, const char *last, const char *shots, const char *embed)
{
    const State *st = &g_states[s];
    const Device *dev = &k_devices[d];
    char prof[760], saves[780], path[800], script[800], bin[700];
    char buf[28][760];
    char *add[28];
    int nadd = 0, i;
    char **envp;
    const char *argv[16];
    intptr_t proc;
    FILE *f;

    snprintf(prof, sizeof prof, "%s/prof", dir);
    snprintf(saves, sizeof saves, "%s/saves", prof);
    make_dirs(saves);
    snprintf(path, sizeof path, "%s/settings.ini", prof);
    copy_file(A.settings, path);
    snprintf(path, sizeof path, "%s/pokeblack.sav", saves);
    copy_file(g_save, path);
    snprintf(script, sizeof script, "%s/script.txt", dir);
    f = fopen(script, "wb");
    if (f == NULL) {
        return 0;
    }
    for (i = 0; i < st->nsteps; i++) {
        fprintf(f, "%s\n", st->steps[i]);
    }
    fprintf(f, "%s\n", last);
    fclose(f);
#define ADD(...) do { if (nadd < 28) { snprintf(buf[nadd], sizeof buf[0], __VA_ARGS__); add[nadd] = buf[nadd]; nadd++; } } while (0)
    ADD("EIDOS_UI_THEME=%s", k_styles[st->style]);
    ADD("EIDOS_UI_SCRIPT=%s", script);
    ADD("EIDOS_FLASH_WATCH=0");
    ADD("EIDOS_NO_AUDIO=1");
    ADD("EIDOS_NO_LINK=1");
    ADD("EIDOS_DEBUG_WARP=%s", g_warp);
    ADD("EIDOS_DEMO_DIR=%s", g_demo);
    ADD("EIDOS_TOUCH_DEVICE=%d", dev->touch);
    ADD("EIDOS_SAFE_AREA=%d %d %d %d", dev->safe_l, dev->safe_t, dev->safe_r, dev->safe_b);
    if (dev->pad >= 0) {
        ADD("EIDOS_PAD_FAMILY=%d", dev->pad);
    } else {
        ADD("EIDOS_PAD_FAMILY=");
    }
    if (embed != NULL) {
        ADD("EIDOS_EMBED=%s", embed);
        ADD("EIDOS_WINDOW_HIDDEN=");
        ADD("EIDOS_WINDOW_SIZE=");
    } else {
        ADD("EIDOS_EMBED=");
        ADD("EIDOS_WINDOW_HIDDEN=1");
        ADD("EIDOS_WINDOW_SIZE=%dx%d", dev->w, dev->h);
    }
    for (i = 0; i < st->nenv; i++) {
        if (strncmp(st->env[i], "NO_WARP=", 8) == 0) {
            ADD("EIDOS_DEBUG_WARP=");
        } else if (strncmp(st->env[i], "SAVE=", 5) == 0) {
            snprintf(path, sizeof path, "%s/pokeblack.sav", saves);
            copy_file(st->env[i] + 5, path);
        } else {
            ADD("%s", st->env[i]);
        }
    }
#undef ADD
    envp = env_with(add, nadd);
    snprintf(bin, sizeof bin, "%s/bin", A.work);
    snprintf(path, sizeof path, "%s/bin/eidos_boot.exe", A.work);
    i = 0;
    argv[i++] = "\"eidos_boot.exe\"";
    argv[i++] = "--profile-dir";
    argv[i++] = prof;
    argv[i++] = "--window";
    argv[i++] = "--layout";
    argv[i++] = "one";
    argv[i++] = "--frames";
    argv[i++] = embed != NULL ? "1000000000" : "60000";
    if (shots != NULL) {
        argv[i++] = "--screenshot-dir";
        argv[i++] = shots;
        argv[i++] = "--watchdog";
        argv[i++] = "60";
    }
    argv[i] = NULL;
    {
        char here[700];

        _getcwd(here, sizeof here);
        _chdir(bin);
        proc = _spawnve(_P_NOWAIT, path, argv, (const char *const *)envp);
        _chdir(here);
    }
    free(envp);
    return proc > 0 ? proc : 0;
}

static int proc_done(intptr_t proc)
{
    return proc <= 0 || WaitForSingleObject((HANDLE)proc, 0) != WAIT_TIMEOUT;
}

static void proc_end(intptr_t proc)
{
    if (proc > 0) {
        if (WaitForSingleObject((HANDLE)proc, 1500) == WAIT_TIMEOUT) {
            TerminateProcess((HANDLE)proc, 0);
        }
        CloseHandle((HANDLE)proc);
    }
}
#endif

static void load_tile(int d)
{
    char path[800];
    SDL_Surface *sf;
    Tile *t = &A.tiles[d];

    if (t->tex != NULL) {
        SDL_DestroyTexture(t->tex);
        t->tex = NULL;
    }
    cache_path(A.state, d, path, sizeof path);
    sf = SDL_LoadBMP(path);
    if (sf != NULL) {
        t->tex = SDL_CreateTextureFromSurface(A.r, sf);
        t->pw = sf->w;
        t->ph = sf->h;
        if (t->tex != NULL) {
            SDL_SetTextureScaleMode(t->tex, SDL_SCALEMODE_LINEAR);
        }
        SDL_DestroySurface(sf);
    }
}

static int queued(int s, int d)
{
    int i;

    for (i = 0; i < A.nqueue; i++) {
        if (A.queue[i][0] == s && A.queue[i][1] == d) {
            return 1;
        }
    }
    for (i = 0; i < A.jobs_max; i++) {
        if (A.jobs[i].proc > 0 && A.jobs[i].state == s && A.jobs[i].device == d) {
            return 1;
        }
    }
    return 0;
}

static int cached(int s, int d)
{
    char path[800];
    FILE *f;

    cache_path(s, d, path, sizeof path);
    f = fopen(path, "rb");
    if (f != NULL) {
        fclose(f);
        return 1;
    }
    return 0;
}

static int shown_device(int d);

/* The views of state s on show and not computed yet, first in the queue. */
static void want_state(int s)
{
    int d, n = 0, keep[DEVICE_COUNT * 4][2], nk = 0, i;

    for (d = 0; d < DEVICE_COUNT; d++) {
        if (shown_device(d) && !cached(s, d) && !queued(s, d) && n < DEVICE_COUNT) {
            keep[nk][0] = s;
            keep[nk][1] = d;
            nk++;
            n++;
        }
    }
    for (i = 0; i < A.nqueue && nk < DEVICE_COUNT * 4; i++) {
        keep[nk][0] = A.queue[i][0];
        keep[nk][1] = A.queue[i][1];
        nk++;
    }
    memcpy(A.queue, keep, (size_t)nk * sizeof keep[0]);
    A.nqueue = nk;
}

static void run_jobs(void)
{
#ifdef _WIN32
    int i;

    for (i = 0; i < A.jobs_max; i++) {
        Job *j = &A.jobs[i];

        if (j->proc > 0 && proc_done(j->proc)) {
            char path[800];

            proc_end(j->proc);
            j->proc = 0;
            cache_path(j->state, j->device, path, sizeof path);
            if (j->state == A.state) {
                load_tile(j->device);
                if (A.tiles[j->device].tex == NULL) {
                    A.tiles[j->device].failed = 1;
                }
            }
        }
        if (j->proc == 0 && A.nqueue > 0) {
            char dir[700], shots[700], last[120];

            j->state = A.queue[0][0];
            j->device = A.queue[0][1];
            memmove(A.queue, A.queue + 1, (size_t)(A.nqueue - 1) * sizeof A.queue[0]);
            A.nqueue--;
            snprintf(dir, sizeof dir, "%s/jobs/j%d", A.work, i);
            cache_dir(j->state, shots, sizeof shots);
            make_dirs(shots);
            snprintf(last, sizeof last, "frames 10\nshot d%02d\nframes 4\nquit", j->device);
            j->proc = start_game(j->state, j->device, dir, last, shots, NULL);
            j->started = SDL_GetTicks();
        }
    }
#endif
}

static int jobs_running(void)
{
    int i, n = 0;

    for (i = 0; i < A.jobs_max; i++) {
        n += A.jobs[i].proc > 0;
    }
    return n;
}

/* ------------------------------------------------------------------ */
/* Live: the picked view in a game stopped on the state                 */
/* ------------------------------------------------------------------ */

static void live_stop(void)
{
#ifdef _WIN32
    if (A.live != NULL) {
        A.live->quit = 1;
    }
    proc_end(A.live_proc);
    A.live_proc = 0;
    if (A.live != NULL) {
        UnmapViewOfFile(A.live);
        A.live = NULL;
    }
    if (A.live_map != NULL) {
        CloseHandle(A.live_map);
        A.live_map = NULL;
    }
#endif
    if (A.live_tex != NULL) {
        SDL_DestroyTexture(A.live_tex);
        A.live_tex = NULL;
    }
    A.live_w = A.live_h = 0;
    A.live_frame = 0;
}

static void live_start(int d)
{
#ifdef _WIN32
    char name[96], dir[700];

    live_stop();
    snprintf(name, sizeof name, "Local\\eidos_review_live_%lu", (unsigned long)GetCurrentProcessId());
    A.live_map = CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, (DWORD)EIDOS_EMBED_BYTES, name);
    A.live = A.live_map != NULL ? (EidosEmbedShm *)MapViewOfFile(A.live_map, FILE_MAP_ALL_ACCESS, 0, 0, 0) : NULL;
    if (A.live == NULL) {
        say("No memory for the live view");
        live_stop();
        return;
    }
    memset(A.live, 0, sizeof *A.live);
    A.live->magic = EIDOS_EMBED_MAGIC;
    A.live->size = (uint32_t)EIDOS_EMBED_BYTES;
    A.live->want_w = (uint32_t)k_devices[d].w;
    A.live->want_h = (uint32_t)k_devices[d].h;
    A.live->mx = A.live->my = -1;
    snprintf(dir, sizeof dir, "%s/jobs/live", A.work);
    A.live_proc = start_game(A.state, d, dir, "freeze", NULL, name);
    say("Live: the game goes to the state and stops there (the pointer works)");
#else
    (void)d;
#endif
}

/* ------------------------------------------------------------------ */
/* Comments                                                            */
/* ------------------------------------------------------------------ */

static void json_str(FILE *f, const char *s)
{
    fputc('"', f);
    for (; *s != '\0'; s++) {
        unsigned char ch = (unsigned char)*s;

        if (ch == '"' || ch == '\\') {
            fprintf(f, "\\%c", ch);
        } else if (ch < 0x20) {
            fprintf(f, "\\u%04x", ch);
        } else {
            fputc(ch, f);
        }
    }
    fputc('"', f);
}

/* A string field of one of our JSON lines (flat objects), or "". */
static void json_field(const char *line, const char *key, char *out, size_t n)
{
    char pat[40];
    const char *p;
    size_t k = 0;

    out[0] = '\0';
    snprintf(pat, sizeof pat, "\"%s\": \"", key);
    p = strstr(line, pat);
    if (p == NULL) {
        return;
    }
    p += strlen(pat);
    while (*p != '\0' && *p != '"' && k + 1 < n) {
        if (*p == '\\' && p[1] != '\0') {
            p++;
        }
        out[k++] = *p++;
    }
    out[k] = '\0';
}

static void read_comments(void)
{
    char path[700], line[4096];
    FILE *f;
    const State *st = &g_states[A.state];

    A.ncomments = 0;
    snprintf(path, sizeof path, "%s/comments.jsonl", A.work);
    f = fopen(path, "rb");
    if (f == NULL) {
        return;
    }
    while (fgets(line, sizeof line, f) != NULL && A.ncomments < 38) {
        char style[16], state[48], view[80], txt[240], status[16], reply[240];

        json_field(line, "style", style, sizeof style);
        json_field(line, "state", state, sizeof state);
        if (strcmp(style, k_styles[st->style]) != 0 || strcmp(state, st->name) != 0) {
            continue;
        }
        json_field(line, "view", view, sizeof view);
        json_field(line, "text", txt, sizeof txt);
        json_field(line, "status", status, sizeof status);
        json_field(line, "reply", reply, sizeof reply);
        snprintf(A.comments[A.ncomments++], sizeof A.comments[0], "%s|%s|%s", status, view, txt);
        if (reply[0] != '\0') {
            snprintf(A.comments[A.ncomments++], sizeof A.comments[0], "reply||%s", reply);
        }
    }
    fclose(f);
}

static void save_comment(int whole)
{
    char path[700], stamp[32], shot[800], view[120];
    FILE *f;
    time_t now = time(NULL);
    const State *st = &g_states[A.state];

    if (A.text[0] == '\0') {
        return;
    }
    if (!whole && A.pick < 0) {
        say("Pick a view first (click it), or comment on the whole screen");
        return;
    }
    strftime(stamp, sizeof stamp, "%Y-%m-%d %H:%M:%S", localtime(&now));
    shot[0] = '\0';
    if (whole) {
        snprintf(view, sizeof view, "whole screen");
    } else {
        device_label(&k_devices[A.pick], view, sizeof view);
        cache_path(A.state, A.pick, shot, sizeof shot);
    }
    snprintf(path, sizeof path, "%s/comments.jsonl", A.work);
    f = fopen(path, "ab");
    if (f == NULL) {
        say("Could not write %s", path);
        return;
    }
    fputs("{\"time\": ", f);
    json_str(f, stamp);
    fputs(", \"style\": ", f);
    json_str(f, k_styles[st->style]);
    fputs(", \"type\": ", f);
    json_str(f, st->type);
    fputs(", \"state\": ", f);
    json_str(f, st->name);
    fputs(", \"view\": ", f);
    json_str(f, view);
    fputs(", \"text\": ", f);
    json_str(f, A.text);
    fputs(", \"shot\": ", f);
    json_str(f, shot);
    fputs(", \"status\": \"open\"}\n", f);
    fclose(f);
    A.text[0] = '\0';
    say("Comment saved");
    read_comments();
}

/* ------------------------------------------------------------------ */
/* Choosing                                                            */
/* ------------------------------------------------------------------ */

/* The states of the chosen style and type, in the screens file's order. */
static int states_shown(int *out, int max)
{
    int i, n = 0;

    for (i = 0; i < g_nstates && n < max; i++) {
        if (g_states[i].style == A.style && strcmp(g_states[i].type, g_types[A.type]) == 0) {
            out[n++] = i;
        }
    }
    return n;
}

static void choose_state(int s)
{
    int d;

    A.state = s;
    A.pick = -1;
    A.zoom = 0;
    live_stop();
    for (d = 0; d < DEVICE_COUNT; d++) {
        A.tiles[d].failed = 0;
        load_tile(d);
    }
    want_state(s);
    read_comments();
}

/* The same state's name in another style, else the type's first state. */
static void choose_style_or_type(void)
{
    int list[MAX_STATES], n = states_shown(list, MAX_STATES), i;
    const char *name = g_states[A.state].name;

    A.list_top = 0;
    for (i = 0; i < n; i++) {
        if (strcmp(g_states[list[i]].name, name) == 0) {
            choose_state(list[i]);
            return;
        }
    }
    if (n > 0) {
        choose_state(list[0]);
    }
}

/* ------------------------------------------------------------------ */
/* Drawing                                                             */
/* ------------------------------------------------------------------ */

#define TOP_H   (LINE_H * 2 + 26)
#define LEFT_W  (FK * 130)
#define RIGHT_W (FK * 190)

static void draw_top(void)
{
    int x = 12, y = 8, i, hit, w;

    fill(0, 0, A.W, TOP_H, C_PANEL, 255);
    fill(0, TOP_H - 1, A.W, 1, C_LINE, 255);
    text(x, y + 5, C_MUTED, "Style");
    x += text_w("Style") + 10;
    for (i = 0; i < 3; i++) {
        w = button(x, y, k_style_names[i], A.style == i, &hit);
        if (hit && A.style != i) {
            A.style = i;
            choose_style_or_type();
        }
        x += w + 6;
    }
    x += 24;
    text(x, y + 5, C_MUTED, "Screen");
    x += text_w("Screen") + 10;
    for (i = 0; i < g_ntypes; i++) {
        w = button(x, y, g_types[i], A.type == i, &hit);
        if (hit && A.type != i) {
            A.type = i;
            choose_style_or_type();
        }
        x += w + 6;
    }
    /* second row: the device groups, the queue */
    x = 12;
    y += LINE_H + 14;
    text(x, y + 5, C_MUTED, "Devices");
    x += text_w("Devices") + 10;
    for (i = 0; i < GROUP_COUNT; i++) {
        w = button(x, y, k_groups[i], !A.group_off[i], &hit);
        if (hit) {
            A.group_off[i] = !A.group_off[i];
            want_state(A.state);
        }
        x += w + 6;
    }
    x += 18;
    w = button(x, y, A.all_devices ? "All devices" : "Distinct devices", A.all_devices, &hit);
    if (hit) {
        A.all_devices = !A.all_devices;
        want_state(A.state);
    }
    x += w + 6;
    {
        char s[120];
        int left = A.nqueue + jobs_running();

        if (left > 0) {
            snprintf(s, sizeof s, "Computing: %d views left (%d games at once)", left, A.jobs_max);
        } else {
            snprintf(s, sizeof s, "All views of this screen are computed");
        }
        text(x + 24, y + 5, left > 0 ? C_WARN : C_MUTED, s);
        x += 24 + text_w(s) + 16;
        w = button(x, y, "Compute this type", 0, &hit);
        if (hit) {
            int list[MAX_STATES], n = states_shown(list, MAX_STATES), k;

            for (k = n - 1; k >= 0; k--) {
                want_state(list[k]);
            }
            want_state(A.state);
            say("Queued every state of %s in %s", g_types[A.type], k_style_names[A.style]);
        }
        x += w + 6;
        w = button(x, y, "Compute again", 0, &hit);
        if (hit) {
            int d;
            char path[800];

            for (d = 0; d < DEVICE_COUNT; d++) {
                cache_path(A.state, d, path, sizeof path);
                remove(path);
            }
            choose_state(A.state);
        }
    }
}

static void draw_left(void)
{
    int list[MAX_STATES], n = states_shown(list, MAX_STATES), i, y = TOP_H + 10, rows;

    fill(0, TOP_H, LEFT_W, A.H - TOP_H, C_PANEL, 255);
    rows = (A.H - TOP_H - 20) / (LINE_H + 8);
    if (hover(0, (float)TOP_H, (float)LEFT_W, (float)(A.H - TOP_H)) && A.wheel != 0) {
        A.list_top -= A.wheel * 3;
    }
    if (A.list_top > n - rows) {
        A.list_top = n - rows;
    }
    if (A.list_top < 0) {
        A.list_top = 0;
    }
    for (i = A.list_top; i < n && i < A.list_top + rows; i++) {
        int s = list[i], on = s == A.state;
        float rx = 8, rw = (float)LEFT_W - 16, rh = (float)LINE_H + 6;

        if (on || hover(rx, (float)y, rw, rh)) {
            fill((int)rx, y, (int)rw, (int)rh, on ? 0x2E4A7Au : C_PANEL2, 255);
        }
        text(16, y + 3, on ? 0xFFFFFFu : C_TEXT, g_states[s].name);
        if (clicked(rx, (float)y, rw, rh) && !on) {
            choose_state(s);
        }
        y += LINE_H + 8;
    }
}

static int shown_device(int d)
{
    int g;

    for (g = 0; g < GROUP_COUNT; g++) {
        if (strcmp(k_devices[d].group, k_groups[g]) == 0) {
            return !A.group_off[g] && (A.all_devices || k_devices[d].shown);
        }
    }
    return 1;
}

/* The views over the middle area: the column count that shows them largest. */
static void layout_tiles(int ax, int ay, int aw, int ah)
{
    int n = 0, d, cols, best_cols = 1, k;
    float best = 0.0f;

    for (d = 0; d < DEVICE_COUNT; d++) {
        n += shown_device(d);
    }
    if (n == 0) {
        return;
    }
    for (cols = 1; cols <= n; cols++) {
        int rows = (n + cols - 1) / cols;
        float tw = (float)aw / cols, th = (float)ah / rows - (LINE_H + 10), s = 1e9f;

        for (d = 0; d < DEVICE_COUNT; d++) {
            if (shown_device(d)) {
                s = SDL_min(s, SDL_min((tw - 10) / k_devices[d].w, th / k_devices[d].h));
            }
        }
        if (s > best) {
            best = s;
            best_cols = cols;
        }
    }
    {
        int rows = (n + best_cols - 1) / best_cols;
        float tw = (float)aw / best_cols, th = (float)ah / rows;

        k = 0;
        for (d = 0; d < DEVICE_COUNT; d++) {
            Tile *t = &A.tiles[d];
            int c, r;
            float s;

            t->box.w = 0;
            if (!shown_device(d)) {
                continue;
            }
            c = k % best_cols;
            r = k / best_cols;
            k++;
            s = SDL_min((tw - 10) / k_devices[d].w, (th - LINE_H - 10) / k_devices[d].h);
            t->box.w = k_devices[d].w * s;
            t->box.h = k_devices[d].h * s;
            t->box.x = ax + c * tw + (tw - t->box.w) / 2;
            t->box.y = ay + r * th + (th - LINE_H - 10 - t->box.h) / 2;
        }
    }
}

static void draw_tiles_ui(void)
{
    int d;

    for (d = 0; d < DEVICE_COUNT; d++) {
        Tile *t = &A.tiles[d];
        char label[96];
        int x = (int)t->box.x, y = (int)t->box.y, w = (int)t->box.w, h = (int)t->box.h;

        if (t->box.w <= 0) {
            continue;
        }
        if (t->tex == NULL) {
            fill(x, y, w, h, 0x0C0D10u, 255);
            text(x + 8, y + 8, t->failed ? C_BAD : C_MUTED, t->failed ? "No picture (the run failed)" : "Computing...");
        }
        if (A.pick == d) {
            fill(x - 3, y - 3, w + 6, 3, C_PICK, 255);
            fill(x - 3, y + h, w + 6, 3, C_PICK, 255);
            fill(x - 3, y, 3, h, C_PICK, 255);
            fill(x + w, y, 3, h, C_PICK, 255);
        } else if (hover(t->box.x, t->box.y, t->box.w, t->box.h)) {
            fill(x - 1, y - 1, w + 2, 1, C_ACCENT, 255);
            fill(x - 1, y + h, w + 2, 1, C_ACCENT, 255);
            fill(x - 1, y, 1, h, C_ACCENT, 255);
            fill(x + w, y, 1, h, C_ACCENT, 255);
        }
        device_label(&k_devices[d], label, sizeof label);
        if (text_w(label) > w) {
            snprintf(label, sizeof label, "%s", k_devices[d].name);     /* (the name alone when the size does not fit) */
        }
        text(x, y + h + 6, A.pick == d ? C_PICK : C_MUTED, label);
        if (hover(t->box.x, t->box.y, t->box.w, t->box.h) && A.pressed) {
            A.pick = d;
            if (A.dbl) {
                A.zoom = 1;
            }
            A.pressed = 0;
        }
    }
}

/* `s` word-wrapped to `w` pixels from (x, y), at most `max_rows` rows (0:
 * no limit) and never below `bottom`: the height used.  *last_x gets the
 * end of the last row (the caret's place), when not NULL. */
static int wrapped(int x, int y, int w, u32 col, const char *s, int max_rows, int bottom, int *last_x)
{
    int rows = 0, h = 0;
    const char *p = s;

    if (last_x != NULL) {
        *last_x = x;
    }
    while (*p != '\0' && (max_rows == 0 || rows < max_rows) && y + h + LINE_H <= bottom) {
        char line[400];
        size_t n = 0, fit = 0, fit_word = 0;

        /* the longest start of p that fits, cut at a space when there is one */
        while (p[n] != '\0' && n + 1 < sizeof line) {
            line[n] = p[n];
            line[n + 1] = '\0';
            if (text_w(line) > w) {
                break;
            }
            n++;
            fit = n;
            if (p[n] == ' ' || p[n] == '\0') {
                fit_word = n;
            }
        }
        if (fit == 0) {
            fit = 1;
        }
        if (p[fit] != '\0' && fit_word > 0) {
            fit = fit_word;
        }
        memcpy(line, p, fit);
        line[fit] = '\0';
        text(x, y + h, col, line);
        if (last_x != NULL) {
            *last_x = x + text_w(line);
        }
        p += fit;
        while (*p == ' ') {
            p++;
        }
        rows++;
        h += LINE_H;
    }
    return h;
}

static void draw_right(void)
{
    int x = A.W - RIGHT_W, y = TOP_H + 14, i, hit, w, pad = g_text_px;
    int inner = RIGHT_W - 2 * pad;
    const State *st = &g_states[A.state];
    char head[160];

    fill(x, TOP_H, RIGHT_W, A.H - TOP_H, C_PANEL, 255);
    fill(x, TOP_H, 1, A.H - TOP_H, C_LINE, 255);
    x += pad;
    snprintf(head, sizeof head, "%s, %s", st->name, k_style_names[st->style]);
    text_bold(x, y, 0xFFFFFFu, head);
    y += LINE_H + 6;
    if (A.pick >= 0) {
        char v[96];

        device_label(&k_devices[A.pick], v, sizeof v);
        snprintf(head, sizeof head, "View: %s", v);
    } else {
        snprintf(head, sizeof head, "No view picked: click one");
    }
    text(x, y, A.pick >= 0 ? C_PICK : C_MUTED, head);
    y += LINE_H + 12;
    /* the comment being written */
    {
        int bh = LINE_H * 4 + 16, end_x = x + 10;

        panel(x, y, inner, bh, 0x101217u, A.typing ? C_ACCENT : C_LINE);
        if (A.text[0] == '\0' && !A.typing) {
            text(x + 10, y + 8, C_MUTED, "Write a comment...");
        } else {
            int used = wrapped(x + 10, y + 8, inner - 20, C_TEXT, A.text, 4, y + bh, &end_x);

            if (A.typing && (SDL_GetTicks() / 500) % 2 == 0) {
                int cy = y + 8 + (used > 0 ? used - LINE_H : 0);

                fill(end_x + 1, cy + 2, 2, LINE_H - 4, C_PICK, 255);
            }
        }
        if (clicked((float)x, (float)y, (float)inner, (float)bh)) {
            A.typing = 1;
            SDL_StartTextInput(A.win);
        }
        y += bh + 10;
    }
    w = button(x, y, "Comment on this view", A.pick >= 0, &hit);
    if (hit) {
        save_comment(0);
    }
    w += 8;
    {
        int w2 = button(x + w, y, "On the whole screen", 0, &hit);

        (void)w2;
        if (hit) {
            save_comment(1);
        }
    }
    y += LINE_H + 18;
    text(x, y, C_MUTED, "Enter: on the view.  Shift+Enter: the whole screen.");
    y += LINE_H + 16;
    fill(x, y, inner, 1, C_LINE, 255);
    y += 14;
    text_bold(x, y, 0xFFFFFFu, "Comments");
    y += LINE_H + 10;
    if (A.ncomments == 0) {
        text(x, y, C_MUTED, "None on this screen yet.");
    }
    for (i = A.ncomments - 1; i >= 0 && y < A.H - LINE_H * 2; i--) {
        char status[24], view[100], body[260];
        const char *p = A.comments[i], *q1 = strchr(p, '|'), *q2 = q1 != NULL ? strchr(q1 + 1, '|') : NULL;
        u32 col;

        if (q1 == NULL || q2 == NULL) {
            continue;
        }
        snprintf(status, sizeof status, "%.*s", (int)(q1 - p), p);
        snprintf(view, sizeof view, "%.*s", (int)(q2 - q1 - 1), q1 + 1);
        snprintf(body, sizeof body, "%s", q2 + 1);
        if (strcmp(status, "reply") == 0) {
            col = C_ACCENT;
        } else {
            u32 tag = strcmp(status, "fixed") == 0 ? C_GOOD : strcmp(status, "kept") == 0 ? C_MUTED : C_WARN;
            const char *word = strcmp(status, "fixed") == 0 ? "Fixed" : strcmp(status, "kept") == 0 ? "Kept" : "Open";
            int tw = text_bold(x, y, tag, word);

            text(x + tw + 10, y, C_MUTED, view);
            y += LINE_H + 2;
            col = C_TEXT;
        }
        y += wrapped(x, y, inner, col, body, 0, A.H - LINE_H, NULL) + 12;
    }
}

static void draw_zoom_ui(void)
{
    int hit, w, x, y;
    char label[120];

    if (!A.zoom || A.pick < 0) {
        return;
    }
    device_label(&k_devices[A.pick], label, sizeof label);
    x = LEFT_W + 16;
    y = TOP_H + 8;
    text(x, y + 5, C_PICK, label);
    x += text_w(label) + 16;
    w = button(x, y, A.live_proc > 0 ? "Live (running)" : "Live", A.live_proc > 0, &hit);
    if (hit) {
        if (A.live_proc > 0) {
            live_stop();
        } else {
            live_start(A.pick);
        }
    }
    x += w + 6;
    w = button(x, y, "Back (Esc)", 0, &hit);
    if (hit) {
        A.zoom = 0;
        live_stop();
    }
}

/* The zoomed view's place. */
static SDL_FRect zoom_box(void)
{
    SDL_FRect b;
    const Device *d = &k_devices[A.pick];
    float aw = (float)(A.W - LEFT_W - RIGHT_W - 32), ah = (float)(A.H - TOP_H - LINE_H * 2 - 40);
    float s = SDL_min(aw / d->w, ah / d->h);

    b.w = d->w * s;
    b.h = d->h * s;
    b.x = LEFT_W + 16 + (aw - b.w) / 2;
    b.y = TOP_H + LINE_H + 30 + (ah - b.h) / 2;
    return b;
}

/* The live view: its newest picture, the pointer over it. */
static void live_frame(SDL_FRect b)
{
    EidosEmbedShm *s = A.live;

    if (s == NULL) {
        return;
    }
    s->beat++;
    if (s->w > 0 && s->h > 0 && s->frame != A.live_frame) {
        int w = (int)s->w, h = (int)s->h;

        if (A.live_tex == NULL || A.live_w != w || A.live_h != h) {
            if (A.live_tex != NULL) {
                SDL_DestroyTexture(A.live_tex);
            }
            A.live_tex = SDL_CreateTexture(A.r, SDL_PIXELFORMAT_RGB24, SDL_TEXTUREACCESS_STREAMING, w, h);
            SDL_SetTextureScaleMode(A.live_tex, SDL_SCALEMODE_LINEAR);
            A.live_w = w;
            A.live_h = h;
        }
        if (A.live_tex != NULL) {
            SDL_UpdateTexture(A.live_tex, NULL, s->pixels, w * 3);
        }
        A.live_frame = s->frame;
    }
    if (hover(b.x, b.y, b.w, b.h)) {
        s->mx = (int32_t)((A.mx - b.x) * k_devices[A.pick].w / b.w);
        s->my = (int32_t)((A.my - b.y) * k_devices[A.pick].h / b.h);
        s->buttons = (SDL_GetMouseState(NULL, NULL) & SDL_BUTTON_LMASK) ? 1u : 0u;
    } else {
        s->mx = s->my = -1;
        s->buttons = 0;
    }
}

/* ------------------------------------------------------------------ */
/* Main                                                                */
/* ------------------------------------------------------------------ */

static const char *arg(int argc, char **argv, const char *name)
{
    int i;

    for (i = 1; i + 1 < argc; i++) {
        if (strcmp(argv[i], name) == 0) {
            return argv[i + 1];
        }
    }
    return NULL;
}

int main(int argc, char **argv)
{
    const char *s, *shot = NULL;
    int i, running = 1;
    uint32_t shot_at = 0;

    memset(&A, 0, sizeof A);
    A.pick = -1;
    A.jobs_max = 6;
    if ((s = arg(argc, argv, "--screens")) == NULL || load_states(s) != 0) {
        fprintf(stderr, "eidos_review: --screens FILE (the screens file) is needed\n");
        return 2;
    }
    if ((s = arg(argc, argv, "--settings")) == NULL) {
        fprintf(stderr, "eidos_review: --settings FILE (a settings.ini to start from) is needed\n");
        return 2;
    }
    snprintf(A.settings, sizeof A.settings, "%s", s);
    if ((s = arg(argc, argv, "--work")) == NULL) {
        fprintf(stderr, "eidos_review: --work DIR is needed\n");
        return 2;
    }
    snprintf(A.work, sizeof A.work, "%s", s);
    if ((s = arg(argc, argv, "--jobs")) != NULL) {
        A.jobs_max = atoi(s) < 1 ? 1 : atoi(s) > MAX_JOBS ? MAX_JOBS : atoi(s);
    }
    if ((s = arg(argc, argv, "--exe")) != NULL) {
        snprintf(A.exe, sizeof A.exe, "%s", s);
    } else {
        const char *base = SDL_GetBasePath();

        snprintf(A.exe, sizeof A.exe, "%seidos_boot.exe", base != NULL ? base : "");
    }
#ifdef _WIN32
    {
        /* the game copied beside the work, so a build never waits on us */
        char to[800], from[800], *slash;

        snprintf(to, sizeof to, "%s/bin", A.work);
        make_dirs(to);
        snprintf(to, sizeof to, "%s/bin/eidos_boot.exe", A.work);
        copy_file(A.exe, to);
        snprintf(from, sizeof from, "%s", A.exe);
        slash = strrchr(from, '/');
        if (strrchr(from, '\\') > slash) {
            slash = strrchr(from, '\\');
        }
        if (slash != NULL) {
            snprintf(slash + 1, sizeof from - (size_t)(slash + 1 - from), "SDL3.dll");
            snprintf(to, sizeof to, "%s/bin/SDL3.dll", A.work);
            copy_file(from, to);
        }
    }
#endif
    shot = arg(argc, argv, "--shot");
    if ((s = arg(argc, argv, "--after")) != NULL) {
        shot_at = (uint32_t)atoi(s) * 1000u;
    }
    if ((s = arg(argc, argv, "--style")) != NULL && style_of(s) >= 0) {
        A.style = style_of(s);
    }
    if ((s = arg(argc, argv, "--type")) != NULL) {
        for (i = 0; i < g_ntypes; i++) {
            if (strcmp(g_types[i], s) == 0) {
                A.type = i;
            }
        }
    }

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        fprintf(stderr, "eidos_review: %s\n", SDL_GetError());
        return 1;
    }
    A.win = SDL_CreateWindow("eidos review", 1800, 1000,
                             shot != NULL ? SDL_WINDOW_HIDDEN : SDL_WINDOW_RESIZABLE | SDL_WINDOW_MAXIMIZED);
    A.r = A.win != NULL ? SDL_CreateRenderer(A.win, NULL) : NULL;
    if (A.r == NULL) {
        fprintf(stderr, "eidos_review: %s\n", SDL_GetError());
        return 1;
    }
    SDL_SetRenderVSync(A.r, shot != NULL ? 0 : 1);
    {
        /* the first state of the type, or the one named */
        int list[MAX_STATES], n = states_shown(list, MAX_STATES);

        A.state = n > 0 ? list[0] : 0;
        if ((s = arg(argc, argv, "--state")) != NULL) {
            for (i = 0; i < n; i++) {
                if (strcmp(g_states[list[i]].name, s) == 0) {
                    A.state = list[i];
                }
            }
        }
        choose_state(A.state);
    }
    if (shot != NULL) {
        shot_at += SDL_GetTicks();
    }

    while (running) {
        SDL_Event e;

        A.pressed = A.dbl = A.wheel = 0;
        while (SDL_PollEvent(&e)) {
            switch (e.type) {
            case SDL_EVENT_QUIT:
                running = 0;
                break;
            case SDL_EVENT_KEY_DOWN:
                if (A.typing) {
                    if (e.key.scancode == SDL_SCANCODE_ESCAPE) {
                        A.typing = 0;
                        SDL_StopTextInput(A.win);
                    } else if (e.key.scancode == SDL_SCANCODE_RETURN || e.key.scancode == SDL_SCANCODE_KP_ENTER) {
                        save_comment((e.key.mod & SDL_KMOD_SHIFT) != 0 || A.pick < 0);
                        A.typing = 0;
                        SDL_StopTextInput(A.win);
                    } else if (e.key.scancode == SDL_SCANCODE_BACKSPACE) {
                        size_t n = strlen(A.text);

                        while (n > 0 && ((unsigned char)A.text[n - 1] & 0xC0) == 0x80) {
                            n--;
                        }
                        if (n > 0) {
                            A.text[n - 1] = '\0';
                        }
                    }
                } else if (e.key.scancode == SDL_SCANCODE_ESCAPE) {
                    A.zoom = 0;
                    live_stop();
                } else if (e.key.scancode == SDL_SCANCODE_DOWN || e.key.scancode == SDL_SCANCODE_UP) {
                    int list[MAX_STATES], n = states_shown(list, MAX_STATES), k;

                    for (k = 0; k < n; k++) {
                        if (list[k] == A.state) {
                            break;
                        }
                    }
                    k += e.key.scancode == SDL_SCANCODE_DOWN ? 1 : -1;
                    if (k >= 0 && k < n) {
                        choose_state(list[k]);
                    }
                }
                break;
            case SDL_EVENT_TEXT_INPUT:
                if (A.typing && strlen(A.text) + strlen(e.text.text) + 1 < sizeof A.text) {
                    strcat(A.text, e.text.text);
                }
                break;
            case SDL_EVENT_MOUSE_MOTION:
                A.mx = e.motion.x;
                A.my = e.motion.y;
                break;
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
                A.mx = e.button.x;
                A.my = e.button.y;
                if (e.button.button == SDL_BUTTON_LEFT) {
                    A.pressed = 1;
                    A.dbl = e.button.clicks >= 2;
                }
                break;
            case SDL_EVENT_MOUSE_WHEEL:
                A.wheel += e.wheel.y > 0 ? 1 : e.wheel.y < 0 ? -1 : 0;
                break;
            default:
                break;
            }
        }
        SDL_GetWindowSize(A.win, &A.W, &A.H);
        g_text_px = A.H >= 1300 ? 19 : 15;
        run_jobs();
        if (A.pressed && A.typing && !hover((float)(A.W - RIGHT_W), (float)TOP_H, (float)RIGHT_W, (float)A.H)) {
            A.typing = 0;
            SDL_StopTextInput(A.win);
        }

        /* the views first, the interface over and around them */
        SDL_SetRenderDrawColor(A.r, 0x15, 0x17, 0x1C, 255);
        SDL_RenderClear(A.r);
        if (A.zoom && A.pick >= 0) {
            SDL_FRect b = zoom_box();

            for (i = 0; i < DEVICE_COUNT; i++) {
                A.tiles[i].box.w = 0;
            }
            A.tiles[A.pick].box = b;
        } else {
            layout_tiles(LEFT_W + 16, TOP_H + 16, A.W - LEFT_W - RIGHT_W - 32, A.H - TOP_H - 32);
        }
        if (A.zoom && A.pick >= 0 && A.live != NULL) {
            live_frame(A.tiles[A.pick].box);
        }
        for (i = 0; i < DEVICE_COUNT; i++) {
            Tile *t = &A.tiles[i];
            SDL_Texture *tex = A.zoom && i == A.pick && A.live_tex != NULL ? A.live_tex : t->tex;

            if (t->box.w > 0 && tex != NULL) {
                SDL_RenderTexture(A.r, tex, NULL, &t->box);
            }
        }
        if (A.zoom && A.pick >= 0) {
            SDL_FRect b = A.tiles[A.pick].box;

            if (A.live_proc == 0 && A.tiles[A.pick].tex == NULL) {
                text((int)b.x + 12, (int)b.y + 12, C_MUTED, "Computing...");
            }
            outline((int)b.x - 1, (int)b.y - 1, (int)b.w + 2, (int)b.h + 2, C_LINE);
            draw_zoom_ui();
        } else {
            draw_tiles_ui();
        }
        draw_left();
        draw_right();
        draw_top();
        if (A.note[0] != '\0' && SDL_GetTicks() - A.note_at < 6000) {
            fill(LEFT_W, A.H - LINE_H - 16, A.W - LEFT_W - RIGHT_W, LINE_H + 16, C_PANEL, 235);
            text(LEFT_W + 16, A.H - LINE_H - 8, C_WARN, A.note);
        }
        if (shot != NULL && SDL_GetTicks() >= shot_at) {
            SDL_Surface *sf = SDL_RenderReadPixels(A.r, NULL);

            if (sf == NULL || !SDL_SaveBMP(sf, shot)) {
                fprintf(stderr, "eidos_review: --shot: %s\n", SDL_GetError());
            }
            SDL_DestroySurface(sf);
            running = 0;
        }
        SDL_RenderPresent(A.r);
        if (shot != NULL) {
            SDL_Delay(30);
        }
    }
    live_stop();
#ifdef _WIN32
    for (i = 0; i < A.jobs_max; i++) {
        if (A.jobs[i].proc > 0) {
            TerminateProcess((HANDLE)A.jobs[i].proc, 0);
            CloseHandle((HANDLE)A.jobs[i].proc);
        }
    }
#endif
    SDL_DestroyRenderer(A.r);
    SDL_DestroyWindow(A.win);
    SDL_Quit();
    return 0;
}
