/*
 * engine/ui/recipe_multiplayer.c -- the one-screen recipe for the
 * Multiplayer screen the title menu's MULTIPLAYER row opens.
 *
 * The screen's model is the port's settings screen (its Multiplayer tab,
 * engine/platform/settings_ui.c) and the multiplayer layer (sdk/mp.h): this
 * recipe only draws them its own way and asks the model for an action by
 * name (eidos_sui_mp_act), which the model does as a touch on that row, so
 * hosting, joining, the keyboards, the questions and the sounds are the
 * ones the DS screens have.
 *
 * Two views:
 * - the hub (before a game): the player's own card with the name the others
 *   see, the ways to play as cards, a Join box and the host's kept games
 *   to go on with;
 * - the lobby (in a session): the players standing in a row, what the
 *   start waits for, the session's rules, and the code above Start.
 *
 * Keys and the pad move a focus of the recipe's own over everything that
 * can be chosen (the arrows and A are taken from the game); B is the
 * model's (back, or leave the screen).  The pointer and a finger choose
 * by touching.
 */
#include <stdio.h>
#include <string.h>

#include "ui.h"
#include "kit.h"
#include "input.h"
#include "settings_ui.h"
#include "mp.h"
#include "theme.h"
#include "avatar.h"

/* engine/ui/recipe_input.c */
extern int eidos_ui_name_entry_up(void);
/* sdk/mod_title.h; engine/ui/recipe_settings.c: the screen is open */
extern int bw_mod_title_in_settings(void);
extern int eidos_ui_options_proc_up;
/* engine/platform/settings.h */
extern const char *eidos_settings_net_name(void);
extern const char *eidos_settings_session_code(void);
extern const char *eidos_settings_session_join_code(void);

#define SPRITE_HERO     0
#define SPRITE_HEROINE  33

#define MP_GAMES 6                      /* kept games the hub lists           */

/* ------------------------------------------------------------------ */
/* The focus: every target the frame drew, chosen by keys or touch     */
/* ------------------------------------------------------------------ */

typedef struct Target {
    EidosUiRect r;
    int act;                            /* EIDOS_SUI_MP_ACT_*, -1 none        */
    int arg;
    int on;                             /* can be chosen now                  */
} Target;

static Target g_t[64];
static int g_nt;
static int g_focus;                     /* index into g_t                     */
static int g_last_view = -9;
/* The target the focus goes to as it is drawn (a list's next row, which
 * the list scrolls to first): its act, and its arg (-1: any). */
static int g_want_act = -9, g_want_arg = -1;
/* The act of the target focused last frame (the lists scroll to it). */
static int g_focus_act = -9, g_focus_arg;
/* The hub card focused last: left from Your games comes back to it. */
static int g_card_act = -9, g_card_arg;
/* Your games: the first row shown, how many fit, and how many there are. */
static int g_games_top, g_games_vis = 1, g_games_n;
/* The Mode and World views' lists: how far they are scrolled (pixels). */
static int g_list_px;

/* A target that is a row of the model's list (the Mode view's options and
 * Start): act MP_ROW + the row, arg the row's kind (EIDOS_SUI_MP_ROW_*). */
#define MP_ROW 1000

/* The trainer panel (the name card opens it): the player's name and look.
 * Its targets' acts: TR_OPEN the card, TR_NAME the name's keyboard,
 * TR_BODY + part 0 (body), 1..4 (hair, outfit, trousers, skin). */
#define TR_OPEN 900
#define TR_NAME 901
#define TR_BODY 902
#define TR_DONE 907
static int g_trainer;                   /* the panel is open */

/* A row of the look one step on (dir -1 or 1), wrapping round. */
static void trainer_change(int row, int dir)
{
    AvatarLook l;
    int n, v;

    avatar_look(&l);
    if (row == 0) {
        l.body = (u8)(l.body ? 0 : 1);
    } else {
        u8 *p = row == 1 ? &l.hair : row == 2 ? &l.accent : row == 3 ? &l.pants : &l.skin;

        n = avatar_choice_count(row - 1);
        v = ((int)*p + dir + n) % n;
        *p = (u8)v;
    }
    avatar_set_look(&l);
    eidos_ui_sound(0x548);
}

static void act_row(int row, int kind, int dir)
{
    /* a value steps (A steps it forward), a text opens the keyboard, an
     * action is done */
    eidos_sui_mp_row_act(row, kind == EIDOS_SUI_MP_ROW_VALUE
                                  ? (dir < 0 ? EIDOS_SUI_ZONE_DEC : EIDOS_SUI_ZONE_INC)
                                  : EIDOS_SUI_ZONE_ROW);
}

static void do_act(int act, int arg)
{
    if (act >= MP_ROW) {
        act_row(act - MP_ROW, arg, 1);
    } else if (act == TR_OPEN) {
        g_trainer = 1;
        eidos_ui_sound(0x54C);
    } else if (act == TR_DONE) {
        g_trainer = 0;
        eidos_ui_sound(0x551);
    } else if (act == TR_NAME) {
        (void)eidos_sui_mp_act(EIDOS_SUI_MP_ACT_NAME, 0);
    } else if (act >= TR_BODY && act <= TR_BODY + 4) {
        trainer_change(act - TR_BODY, 1);
    } else if (act >= 0) {
        (void)eidos_sui_mp_act(act, arg);
    }
}

static int target(EidosUiCtx *c, int x, int y, int w, int h, int act, int arg, int on, int *hover)
{
    EidosUiRect r;
    int st, i = g_nt;

    r.x = x; r.y = y; r.w = w; r.h = h;
    st = eidos_ui_hit(r);
    (void)c;
    if (g_nt < (int)(sizeof g_t / sizeof g_t[0])) {
        g_t[g_nt].r = r;
        g_t[g_nt].act = act;
        g_t[g_nt].arg = arg;
        g_t[g_nt].on = on;
        g_nt++;
    }
    if (on && act == g_want_act && (g_want_arg < 0 || arg == g_want_arg)) {
        g_focus = i;                    /* the list's next row, now drawn */
        g_want_act = -9;
    }
    if ((st & EIDOS_UI_HOVER) && on) {
        g_focus = i;                    /* the pointer moves the focus too   */
    }
    if (hover != NULL) {
        *hover = (st & EIDOS_UI_HOVER) != 0;
    }
    if ((st & EIDOS_UI_CLICK) && on && act >= 0) {
        do_act(act, arg);
    }
    return g_focus == i && on;
}

/* Is this target the focused one, drawn with the keys' highlight? */
static int lit(int focused)
{
    return focused && eidos_ui_keys_lit();
}

/* The next target from the focus in a direction (dx, dy): the nearest one
 * whose middle lies that way, the distance across the direction counting
 * twice. */
static void move_focus(int dx, int dy)
{
    const EidosUiRect *f;
    int i, best = -1, best_d = 0x7FFFFFFF, fx, fy;

    if (g_focus < 0 || g_focus >= g_nt) {
        g_focus = 0;
        return;
    }
    f = &g_t[g_focus].r;
    fx = f->x + f->w / 2;
    fy = f->y + f->h / 2;
    for (i = 0; i < g_nt; i++) {
        int tx = g_t[i].r.x + g_t[i].r.w / 2, ty = g_t[i].r.y + g_t[i].r.h / 2;
        int along = (tx - fx) * dx + (ty - fy) * dy;
        int across = dx != 0 ? ty - fy : tx - fx, d;

        if (i == g_focus || !g_t[i].on || along <= 0) {
            continue;
        }
        if (across < 0) {
            across = -across;
        }
        d = along + 2 * across;
        if (d < best_d) {
            best_d = d;
            best = i;
        }
    }
    if (best >= 0) {
        g_focus = best;
    }
}

static void keys_step(void)
{
    u32 k = eidos_ui_keys_pressed();

    eidos_ui_take_keys(BW_KEY_UP | BW_KEY_DOWN | BW_KEY_LEFT | BW_KEY_RIGHT | BW_KEY_A);
    if (g_focus >= g_nt || (g_nt > 0 && !g_t[g_focus].on)) {
        int i;

        for (i = 0; i < g_nt && !g_t[i].on; i++) {
        }
        g_focus = i < g_nt ? i : 0;
    }
    {
        int was = g_focus, listed = 0;
        const Target *t = g_focus < g_nt ? &g_t[g_focus] : NULL;

        if (t != NULL && (t->act == EIDOS_SUI_MP_ACT_COOP_NEW || t->act == EIDOS_SUI_MP_ACT_MODE ||
                          t->act == TR_OPEN)) {
            g_card_act = t->act;        /* left from Your games comes back here */
            g_card_arg = t->arg;
        }
        if (t != NULL && t->act == EIDOS_SUI_MP_ACT_CONTINUE && (k & (BW_KEY_UP | BW_KEY_DOWN | BW_KEY_LEFT))) {
            /* Your games is a list of its own: up and down go through it
             * (it scrolls), left goes back to the cards */
            int d = (k & BW_KEY_UP) ? -1 : 1;

            listed = 1;
            if (k & BW_KEY_LEFT) {
                g_want_act = g_card_act != -9 ? g_card_act : TR_OPEN;
                g_want_arg = g_card_act != -9 ? g_card_arg : -1;
                eidos_ui_sound(0x548);
            } else if (t->arg + d >= 0 && t->arg + d < g_games_n) {
                g_want_act = EIDOS_SUI_MP_ACT_CONTINUE;
                g_want_arg = t->arg + d;
                eidos_ui_sound(0x548);
            } else if (d < 0) {
                move_focus(0, -1);      /* up past the first: the Join box */
            }
        } else if (t != NULL && t->act >= MP_ROW && (k & (BW_KEY_UP | BW_KEY_DOWN))) {
            /* the Mode and World views' rows: the next one that can be
             * chosen, by its place in the list (it scrolls to it) */
            int d = (k & BW_KEY_UP) ? -1 : 1, j, n = eidos_sui_mp_rows();
            char l[8], v[8];

            for (j = t->act - MP_ROW + d; j >= 0 && j < n; j += d) {
                int kind = eidos_sui_mp_row(j, l, (int)sizeof l, v, (int)sizeof v);

                if (kind >= 0 && kind != EIDOS_SUI_MP_ROW_INFO) {
                    break;
                }
            }
            listed = 1;
            if (j >= 0 && j < n) {
                g_want_act = MP_ROW + j;
                g_want_arg = -1;
                eidos_ui_sound(0x548);
            } else {
                move_focus(0, d);
            }
        }
        if (!listed && (k & BW_KEY_UP)) move_focus(0, -1);
        if (!listed && (k & BW_KEY_DOWN)) move_focus(0, 1);
        if (listed) {
            /* (the sound is the list's own) */
        } else if (t != NULL && t->act >= MP_ROW && t->arg == EIDOS_SUI_MP_ROW_VALUE) {
            /* left and right change the value the focus is on (its own sound) */
            if (k & (BW_KEY_LEFT | BW_KEY_RIGHT)) {
                act_row(t->act - MP_ROW, t->arg, (k & BW_KEY_LEFT) ? -1 : 1);
            }
        } else if (t != NULL && t->act >= TR_BODY && t->act <= TR_BODY + 4) {
            /* the look's rows: left and right step through the choices */
            if (k & (BW_KEY_LEFT | BW_KEY_RIGHT)) {
                trainer_change(t->act - TR_BODY, (k & BW_KEY_LEFT) ? -1 : 1);
            }
        } else {
            if (k & BW_KEY_LEFT) move_focus(-1, 0);
            if (k & BW_KEY_RIGHT) move_focus(1, 0);
        }
        if (g_focus != was) {
            eidos_ui_sound(0x548);      /* SE_FIELD_MENU_MOVE, the menus' cursor */
        }
    }
    if ((k & BW_KEY_A) && g_focus < g_nt && g_t[g_focus].on && g_t[g_focus].act >= 0) {
        do_act(g_t[g_focus].act, g_t[g_focus].arg);
    }
}

/* ------------------------------------------------------------------ */
/* Pieces                                                              */
/* ------------------------------------------------------------------ */

/* A card: the style's panel, lit or under the pointer */
static void card(EidosUiCtx *c, int x, int y, int w, int h, int on, int lit_, int hover)
{
    u32 fill = !on ? 0x0C0E12u : lit_ ? 0x0A283Cu : hover ? 0x1C1C1Cu : UI_BODY;
    u32 edge = lit_ ? UI_CYAN : hover && on ? UI_EDGE2 : UI_EDGE;

    if (ui_theme_active() != NULL && ui_theme_active()->mp_part != NULL) {
        ui_theme_active()->mp_part(c, 0, x, y, w, h, lit_, hover, on);   /* the style's own card */
        return;
    }

    /* a way to play the port cannot run yet: the card half seen through */
    ui_panel(c, x, y, w, h, UI_CUT_TL | UI_CUT_BR, 6, fill, edge, on ? 245 : 120);
    if (lit_) {
        /* the keys' focus in the style's own mark, as on its other screens */
        ui_cursor_corners(c, x - 2, y - 2, w + 4, h + 4, UI_CYAN);
    }
}

/* Where a card's text starts: past a style's cap on the card's left end
 * (Plasma's chips), else Kosmos's margin */
static int card_pad(void)
{
    return ui_theme_active() != NULL && ui_theme_active()->mp_pad > 0 ? ui_theme_active()->mp_pad : 8;
}

/* A check before the start: a small filled box, green when it holds,
 * amber when the start waits on it (the font has no tick) */
static void check_box(EidosUiCtx *c, int x, int y, int ok)
{
    ui_fill(c, x, y, 7, 7, UI_EDGE, 255);
    ui_fill(c, x + 1, y + 1, 5, 5, ok ? 0xA6F24Au : 0xF2B33Du, 255);
}

static void label(EidosUiCtx *c, int x, int y, const char *s)
{
    ui_text(c, x, y, s, UI_CYAN, -1, UI_FONT_SMALL, 1);
}

/* `s` cut to `maxw` pixels with "..." */
static const char *fit(char *buf, size_t n, const char *s, int maxw, int font)
{
    size_t k;

    if (ui_text_w(s, font) <= maxw || n < 5) {
        return s;
    }
    for (k = strlen(s) < n - 4 ? strlen(s) : n - 4; k > 0; k--) {
        if (((unsigned char)s[k] & 0xC0u) == 0x80u) {
            continue;
        }
        memcpy(buf, s, k);
        memcpy(buf + k, "...", 4);
        if (ui_text_w(buf, font) <= maxw) {
            return buf;
        }
    }
    return "...";
}

/* A line of text that wraps at word boundaries in `w`, at most `lines`
 * lines; returns the height used. */
static int para(EidosUiCtx *c, int x, int y, int w, const char *s, u32 col, int font, int lines)
{
    char line[160];
    int n = 0, lh = font == UI_FONT_SMALL ? 10 : 12;

    while (*s != '\0' && n < lines) {
        size_t len = 0, cut = 0;

        while (s[len] != '\0') {
            size_t next = len;

            while (s[next] == ' ') next++;
            while (s[next] != '\0' && s[next] != ' ') next++;
            if (next >= sizeof line) break;
            memcpy(line, s, next);
            line[next] = '\0';
            if (ui_text_w(line, font) > w && cut > 0) break;
            cut = next;
            len = next;
            if (s[len] == '\0') break;
        }
        if (cut == 0) cut = strlen(s) < sizeof line - 1 ? strlen(s) : sizeof line - 1;
        memcpy(line, s, cut);
        line[cut] = '\0';
        ui_text(c, x, y + n * lh, line, col, -1, font, 1);
        s += cut;
        while (*s == ' ') s++;
        n++;
    }
    return n * lh;
}

/* ------------------------------------------------------------------ */
/* The hub                                                             */
/* ------------------------------------------------------------------ */

/* Your games' colour for a kind (the badge and the kind's word) */
static u32 game_colour(int kind)
{
    return kind == EIDOS_SUI_GAME_WORLD ? 0xF2B33Du : kind == EIDOS_SUI_GAME_SOLO ? 0xA6F24Au
         : kind == EIDOS_SUI_GAME_ROOM ? 0xC58BFFu : UI_CYAN;
}

static void hub(EidosUiCtx *c, int x, int y, int w, int h, u32 frame)
{
    /* The cards start things: Co-op (the port's own) and the ways to play
     * the mods on offer (their `mode` lines; a mode's types are inside its
     * card).  Joining is the Join box's alone, and what was started goes on
     * from Your games. */
    struct { const char *title, *line, *foot; int act, arg, on; } cards[4];
    int nmodes = eidos_sui_mp_modes(), ncards = 0;
    const char *name = eidos_settings_net_name();
    int named = name != NULL && name[0] != '\0' && strcmp(name, "Player") != 0;
    int lw = w * 3 / 5, rw = w - lw - 8, rx = x + lw + 8, i, hov, f;
    int cardh = 46, gy = y + cardh + 8;
    char buf[96], tmp[96];

    /* the player's own card: the name the others see, the look, the version */
    f = target(c, x, y, lw, cardh, TR_OPEN, 0, 1, &hov);
    card(c, x, y, lw, cardh, 1, lit(f), hov);
    {
        AvatarLook look;
        const UiImage *fig;

        avatar_look(&look);
        fig = ui_overworld_look(&look, 0, (int)((frame / 12) & 3));
        if (fig != NULL) {
            ui_image(c, x + 8, y + (cardh - fig->h) / 2, fig, 1);
        }
    }
    label(c, x + 46, y + 7, "YOUR TRAINER");
    ui_text(c, x + 46, y + 19, named ? name : "Choose a name", named ? UI_WHITE : UI_CYAN,
            UI_EDGE, UI_FONT_LARGE, 1);
    snprintf(buf, sizeof buf, "eidos %s", bw_mp_version());
    ui_text(c, x + lw - 8 - ui_text_w(buf, UI_FONT_SMALL), y + 6, buf, UI_GREY, -1, UI_FONT_SMALL, 1);
    ui_text(c, x + lw - 8 - ui_text_w("Name & avatar", UI_FONT_SMALL), y + cardh - 14, "Name and look",
            lit(f) ? UI_WHITE : UI_GREY, -1, UI_FONT_SMALL, 1);

    /* the cards, one under another */
    cards[0].title = "Co-op Adventure";
    cards[0].line = "Host a game on your save.";
    cards[0].foot = "Up to 16 players";
    cards[0].act = EIDOS_SUI_MP_ACT_COOP_NEW; cards[0].arg = 0; cards[0].on = 1;
    ncards = 1;
    for (i = 0; i < nmodes && ncards < 4; i++) {
        const char *id, *title, *line;
        int off = eidos_sui_mp_mode_info(i, &id, &title, &line);

        cards[ncards].title = title;
        cards[ncards].line = line;
        cards[ncards].foot = off ? "Not in this version" : "Your own save";
        cards[ncards].act = EIDOS_SUI_MP_ACT_MODE;
        cards[ncards].arg = i;
        cards[ncards].on = !off;
        ncards++;
    }
    {
        int ch = (h - cardh - 8 - (ncards - 1) * 6) / ncards, pad = card_pad();

        if (ch > 84) ch = 84;
        for (i = 0; i < ncards; i++) {
            int cy = gy + i * (ch + 6), lines = (ch - 24 - 16) / 10;

            f = target(c, x, cy, lw, ch, cards[i].act, cards[i].arg, cards[i].on, &hov);
            card(c, x, cy, lw, ch, cards[i].on, lit(f), hov);
            ui_text(c, x + pad, cy + 7, cards[i].title, cards[i].on ? UI_WHITE : UI_GREY, UI_EDGE,
                    UI_FONT_LARGE, 1);
            ui_text(c, x + lw - 8 - ui_text_w(cards[i].foot, UI_FONT_SMALL), cy + 9, cards[i].foot,
                    cards[i].on ? UI_CYAN : 0x707070u, -1, UI_FONT_SMALL, 1);
            para(c, x + pad, cy + 24, lw - pad - 8, cards[i].line, cards[i].on ? UI_LIGHT : 0x707070u,
                 UI_FONT_SMALL, lines < 1 ? 1 : lines);
        }
    }

    /* Join: every code goes here, a friend's co-op game or a world */
    ui_panel(c, rx, y, rw, 56, UI_CUT_TL | UI_CUT_BR, 6, UI_BODY2, UI_EDGE, 240);
    label(c, rx + 8, y + 6, "JOIN");
    {
        /* the code's field, and beside it Paste: the clipboard's code, joined with at once */
        int pw = ui_text_w("Paste", UI_FONT_MEDIUM) + 16, fw = rw - 16 - pw - 4, phov = 0;
        int pf = target(c, rx + 8 + fw + 4, y + 18, pw, 18, EIDOS_SUI_MP_ACT_PASTE, 0, 1, &phov);

        ui_panel(c, rx + 8 + fw + 4, y + 18, pw, 18, UI_CUT_TL | UI_CUT_BR, 4,
                 lit(pf) ? 0x0A283Cu : 0x05080Du, lit(pf) || phov ? UI_CYAN : UI_EDGE, 255);
        ui_text(c, rx + 8 + fw + 4 + 8, y + 18 + ui_text_y(18, UI_FONT_MEDIUM), "Paste",
                lit(pf) || phov ? UI_WHITE : UI_GREY, -1, UI_FONT_MEDIUM, 1);
        f = target(c, rx + 8, y + 18, fw, 18, EIDOS_SUI_MP_ACT_JOIN, 0, 1, &hov);
        ui_panel(c, rx + 8, y + 18, fw, 18, UI_CUT_TL | UI_CUT_BR, 4,
                 lit(f) ? 0x0A283Cu : 0x05080Du, lit(f) || hov ? UI_CYAN : UI_EDGE, 255);
    }
    ui_text(c, rx + 14, y + 18 + ui_text_y(18, UI_FONT_MEDIUM), "Enter code",
            lit(f) || hov ? UI_WHITE : UI_GREY, -1, UI_FONT_MEDIUM, 1);
    para(c, rx + 8, y + 40, rw - 16, "Co-op code, world code, or room port.", UI_GREY, UI_FONT_SMALL, 1);

    /* Your games: a list in its own box (up and down scroll it, left goes
     * back to the cards) */
    {
        int py = y + 64, ph = y + h - py, rowh = 30, gap = 4, ly = py + 18, lh = ph - 22, n, top;
        EidosUiRect box;

        n = eidos_sui_mp_games();
        g_games_n = n;
        ui_panel(c, rx, py, rw, ph, UI_CUT_TL | UI_CUT_BR, 6, UI_BODY2, UI_EDGE, 240);
        label(c, rx + 8, py + 6, "YOUR GAMES");
        if (n <= 0) {
            para(c, rx + 8, ly + 2, rw - 16, "No games yet.", UI_GREY,
                 UI_FONT_SMALL, 3);
        }
        g_games_vis = lh / (rowh + gap);
        if (g_games_vis < 1) g_games_vis = 1;
        /* the list follows the focus, and the wheel over it */
        {
            int sel = g_want_act == EIDOS_SUI_MP_ACT_CONTINUE ? g_want_arg
                    : g_focus_act == EIDOS_SUI_MP_ACT_CONTINUE ? g_focus_arg : -1;

            box.x = rx; box.y = ly; box.w = rw; box.h = lh;
            g_games_top -= eidos_ui_wheel(box);
            if (sel >= 0 && sel < g_games_top) g_games_top = sel;
            if (sel >= 0 && sel >= g_games_top + g_games_vis) g_games_top = sel - g_games_vis + 1;
            if (g_games_top > n - g_games_vis) g_games_top = n - g_games_vis;
            if (g_games_top < 0) g_games_top = 0;
        }
        top = g_games_top;
        for (i = top; i < n && i < top + g_games_vis; i++) {
            EidosSuiGame gm;
            int ry = ly + (i - top) * (rowh + gap), pad = card_pad();
            const char *kind, *badge;
            u32 kc;

            if (eidos_sui_mp_game(i, &gm) != 0) {
                break;
            }
            kind = gm.kind == EIDOS_SUI_GAME_WORLD ? "Multiworld" : gm.kind == EIDOS_SUI_GAME_SOLO ? "Solo"
                 : gm.kind == EIDOS_SUI_GAME_ROOM ? "Archipelago"
                 : "Co-op";
            badge = gm.host ? "HOST" : "JOINED";
            kc = gm.ok ? game_colour(gm.kind) : 0x707070u;
            f = target(c, rx + 8, ry, rw - 20, rowh, EIDOS_SUI_MP_ACT_CONTINUE, i, gm.ok, &hov);
            card(c, rx + 8, ry, rw - 20, rowh, gm.ok, lit(f), hov);
            /* the kind's mark down the left edge, where the style's card
             * has no cap of its own there (Plasma's chip sits in that
             * place; the tag's colour says the kind there) */
            if (ui_theme_active() == NULL || ui_theme_active()->mp_part == NULL) {
                ui_fill(c, rx + 9, ry + 3, 2, rowh - 6, UI_RAW(kc), 255);
            }
            ui_text(c, rx + 8 + pad, ry + 3,
                    fit(tmp, sizeof tmp, gm.title, rw - 36 - pad - ui_text_w(badge, UI_FONT_SMALL), UI_FONT_MEDIUM),
                    gm.ok ? UI_WHITE : UI_GREY, UI_EDGE, UI_FONT_MEDIUM, 1);
            {
                /* the badge: a filled tag in the kind's colour, readable on
                 * every style's rows, lit or not */
                int bw = ui_text_w(badge, UI_FONT_SMALL) + 6, bx = rx + rw - 18 - bw;

                ui_fill(c, bx, ry + 3, bw, 10, UI_RAW(kc), 255);
                ui_text(c, bx + 3, ry + 4, badge, UI_RAW(0x101010u), -1, UI_FONT_SMALL, 1);
            }
            if (!gm.ok) {
                snprintf(buf, sizeof buf, "Needs %s", gm.owner[0] != '\0' ? gm.owner : "its mod");
            } else if (gm.kind == EIDOS_SUI_GAME_COOP) {
                snprintf(buf, sizeof buf, "%s: new code each time", kind);
            } else if (gm.code[0] != '\0') {
                snprintf(buf, sizeof buf, gm.kind == EIDOS_SUI_GAME_ROOM ? "%s: room port %s" : "%s: world %s", kind,
                         gm.kind == EIDOS_SUI_GAME_ROOM && strncmp(gm.code, "AP", 2) == 0 ? gm.code + 2 : gm.code);
            } else {
                snprintf(buf, sizeof buf, "%s", gm.kind == EIDOS_SUI_GAME_SOLO ? "Solo: on this device" : kind);
            }
            ui_text(c, rx + 8 + pad, ry + 17, fit(tmp, sizeof tmp, buf, rw - 32 - pad, UI_FONT_SMALL),
                    gm.ok ? UI_GREY : 0x707070u, -1, UI_FONT_SMALL, 1);
        }
        /* where the list is, when it holds more than it shows */
        if (n > g_games_vis) {
            /* the style's bar: a finger drags it, or the list itself */
            ui_scrollbar(c, rx + rw - 9, ly, lh, top, g_games_vis, n, UI_CYAN);
        }
    }
}

/* ------------------------------------------------------------------ */
/* The trainer panel: your name and your look                          */
/* ------------------------------------------------------------------ */

static void trainer_view(EidosUiCtx *c, int x, int y, int w, int h, u32 frame)
{
    static const char *const part_label[AVATAR_PARTS] = { "Hair", "Outfit", "Trousers", "Skin" };
    AvatarLook look;
    const char *name = eidos_settings_net_name();
    int named = name != NULL && name[0] != '\0' && strcmp(name, "Player") != 0;
    int pw = w * 2 / 5, rx = x + pw + 8, rw = w - pw - 8, ry = y + 12, f, hov, row, pad = card_pad();
    char buf[96];

    avatar_look(&look);
    /* the preview: the walking figure, turning through the four ways */
    ui_panel(c, x, y, pw, h, UI_CUT_TL | UI_CUT_BR, 6, UI_BODY2, UI_EDGE, 240);
    label(c, x + 8, y + 6, "HOW OTHERS SEE YOU");
    {
        static const int dirs[4] = { 0, 3, 1, 2 };
        /* the chosen look, which a plain game does not show */
        const UiImage *fig = ui_overworld_look(&look, dirs[(frame / 96) & 3], (int)((frame / 10) & 3));
        int scale = (h - 60) / 40 > 4 ? 4 : (h - 60) / 40;

        scale = scale < 1 ? 1 : scale;
        if (fig != NULL) {
            ui_image(c, x + (pw - fig->w * scale) / 2, y + 24 + (h - 64 - fig->h * scale) / 2, fig, scale);
        }
        snprintf(buf, sizeof buf, "%s", named ? name : "No name yet");
        ui_text(c, x + (pw - ui_text_w(buf, UI_FONT_LARGE)) / 2, y + h - 30, buf, named ? UI_WHITE : UI_CYAN,
                UI_EDGE, UI_FONT_LARGE, 1);
    }
    /* (the header names the panel) */
    /* the name: the other players see it (the keyboard asks for it) */
    f = target(c, rx, ry, rw, 22, TR_NAME, 0, 1, &hov);
    card(c, rx, ry, rw, 22, 1, lit(f), hov);
    ui_text(c, rx + pad, ry + ui_text_y(22, UI_FONT_MEDIUM), "Name", UI_WHITE, UI_EDGE, UI_FONT_MEDIUM, 1);
    snprintf(buf, sizeof buf, "%s", named ? name : "Choose a name");
    ui_text(c, rx + rw - 8 - ui_text_w(buf, UI_FONT_MEDIUM), ry + ui_text_y(22, UI_FONT_MEDIUM), buf,
            named ? UI_CYAN : 0xF2B33Du, -1, UI_FONT_MEDIUM, 1);
    ry += 26;
    /* the body */
    f = target(c, rx, ry, rw, 22, TR_BODY, 0, 1, &hov);
    card(c, rx, ry, rw, 22, 1, lit(f), hov);
    ui_text(c, rx + pad, ry + ui_text_y(22, UI_FONT_MEDIUM), "Trainer", UI_WHITE, UI_EDGE, UI_FONT_MEDIUM, 1);
    snprintf(buf, sizeof buf, lit(f) || hov ? "< %s >" : "%s", look.body ? "Hilda" : "Hilbert");
    ui_text(c, rx + rw - 8 - ui_text_w(buf, UI_FONT_MEDIUM), ry + ui_text_y(22, UI_FONT_MEDIUM), buf, UI_CYAN, -1,
            UI_FONT_MEDIUM, 1);
    ry += 26;
    /* the four parts: the choice's name and every choice as a swatch */
    for (row = 0; row < AVATAR_PARTS; row++) {
        int id = row == 0 ? look.hair : row == 1 ? look.accent : row == 2 ? look.pants : look.skin;
        int n = avatar_choice_count(row), rh = 34, sw, i, sx;

        if (ry + rh > y + h) {
            break;
        }
        f = target(c, rx, ry, rw, rh, TR_BODY + 1 + row, 0, 1, &hov);
        card(c, rx, ry, rw, rh, 1, lit(f), hov);
        ui_text(c, rx + pad, ry + 4, part_label[row], UI_WHITE, UI_EDGE, UI_FONT_MEDIUM, 1);
        snprintf(buf, sizeof buf, lit(f) || hov ? "< %s >" : "%s", avatar_choice_name(row, id));
        ui_text(c, rx + rw - 8 - ui_text_w(buf, UI_FONT_MEDIUM), ry + 4, buf, UI_CYAN, -1, UI_FONT_MEDIUM, 1);
        sw = (rw - pad - 8) / n;
        sw = sw > 14 ? 14 : sw;
        sx = rx + pad;
        for (i = 0; i < n; i++) {
            u32 col = avatar_choice_rgb(look.body, row, i);

            if (i == id) {
                ui_fill(c, sx + i * sw - 1, ry + 18, sw + 1, 12, UI_RAW(0xFFFFFFu), 255);
            }
            ui_fill(c, sx + i * sw, ry + 19, sw - 1, 10, UI_RAW(col), 255);   /* the colour itself, past the style */
        }
        ry += rh + 4;
    }
    /* back to the hub (B does it too) */
    if (ry + 22 <= y + h) {
        f = target(c, rx, y + h - 22, rw, 22, TR_DONE, 0, 1, &hov);
        if (ui_theme_active() != NULL && ui_theme_active()->mp_part != NULL) {
            ui_theme_active()->mp_part(c, 2, rx, y + h - 22, rw, 22, lit(f), hov, 1);
        } else {
            ui_panel(c, rx, y + h - 22, rw, 22, UI_CUT_TL | UI_CUT_BR, 4, lit(f) || hov ? UI_CYAN : UI_CYAN2,
                     UI_CYAN, 255);
        }
        ui_text(c, rx + (rw - ui_text_w("Done", UI_FONT_LARGE)) / 2, y + h - 22 + ui_text_y(22, UI_FONT_LARGE),
                "Done", UI_WHITE, -1, UI_FONT_LARGE, 1);
    }
}

/* The Host view: the kept games and a new one */
static void host_list(EidosUiCtx *c, int x, int y, int w, int h)
{
    static char games[MP_GAMES][BW_MP_GAME_NAME_MAX];
    int n = bw_mp_game_list(games, MP_GAMES), i, f, hov, rowh = 32, lw = w * 3 / 5;
    char tmp[96];

    label(c, x, y, "YOUR MULTIPLAYER GAMES, NEWEST FIRST");
    f = target(c, x, y + 12, lw, rowh, EIDOS_SUI_MP_ACT_NEW_GAME, 0, 1, &hov);
    card(c, x, y + 12, lw, rowh, 1, lit(f), hov);
    ui_text(c, x + card_pad(), y + 12 + 4, "+ New game", UI_CYAN, UI_EDGE, UI_FONT_LARGE, 1);
    ui_text(c, x + card_pad(), y + 12 + 20, "Everyone starts a fresh save.", UI_GREY, -1,
            UI_FONT_SMALL, 1);
    for (i = 0; i < n && y + 12 + (i + 2) * (rowh + 4) <= y + h; i++) {
        int ry = y + 12 + (i + 1) * (rowh + 4);

        f = target(c, x, ry, lw, rowh, EIDOS_SUI_MP_ACT_GAME, i, 1, &hov);
        card(c, x, ry, lw, rowh, 1, lit(f), hov);
        ui_text(c, x + card_pad(), ry + 4, fit(tmp, sizeof tmp, games[i], lw - 24, UI_FONT_LARGE), UI_WHITE,
                UI_EDGE, UI_FONT_LARGE, 1);
        ui_text(c, x + card_pad(), ry + 20, "Re-host with a new code", UI_GREY, -1, UI_FONT_SMALL, 1);
    }
    {
        int px = x + lw + 8, pw = w - lw - 8;

        ui_panel(c, px, y + 12, pw, h - 12, UI_CUT_TL | UI_CUT_BR, 6, UI_BODY2, UI_EDGE, 240);
        label(c, px + 8, y + 18, "HOW IT WORKS");
        para(c, px + 8, y + 32, pw - 16,
             "Your game keeps everyone's save. The relay gives a new code each time you host. "
             "Multiplayer games use save slots too.", UI_LIGHT, UI_FONT_SMALL, 8);
    }
}

/* ------------------------------------------------------------------ */
/* A mod's way to play: its options and Start                         */
/* ------------------------------------------------------------------ */

/* The big button at a list's foot: the style's own, greyed when it cannot
 * be chosen */
static void foot_button(EidosUiCtx *c, int x, int y, int w, int h, const char *text, int on, int focused,
                        int hover)
{
    if (ui_theme_active() != NULL && ui_theme_active()->mp_part != NULL) {
        ui_theme_active()->mp_part(c, 2, x, y, w, h, focused, hover, on);
    } else {
        ui_panel(c, x, y, w, h, UI_CUT_TL | UI_CUT_BR, 4,
                 on ? (focused || hover ? UI_CYAN : UI_CYAN2) : 0x14181Cu, on ? UI_CYAN : UI_EDGE, 255);
    }
    ui_text(c, x + (w - ui_text_w(text, UI_FONT_LARGE)) / 2, y + ui_text_y(h, UI_FONT_LARGE), text,
            on ? UI_WHITE : UI_GREY, -1, UI_FONT_LARGE, 1);
}

/* The side panel: what the view is (`line`), a rule, then what the row the
 * focus is on does (its label and its help) */
static void side_panel(EidosUiCtx *c, int px, int py, int pw, int ph, const char *title, const char *line,
                       const char *row_label, const char *row_help)
{
    int used;

    ui_panel(c, px, py, pw, ph, UI_CUT_TL | UI_CUT_BR, 6, UI_BODY2, UI_EDGE, 240);
    label(c, px + 8, py + 6, title);
    used = para(c, px + 8, py + 20, pw - 16, line, UI_LIGHT, UI_FONT_SMALL, 6);
    if (row_help != NULL && row_help[0] != '\0') {
        int sy = py + 20 + used + 8;

        ui_fill(c, px + 8, sy, pw - 16, 1, UI_EDGE2, 255);
        if (row_label != NULL && row_label[0] != '\0') {
            ui_text(c, px + 8, sy + 6, row_label, UI_WHITE, UI_EDGE, UI_FONT_MEDIUM, 1);
            sy += 14;
        }
        para(c, px + 8, sy + 6, pw - 16, row_help, UI_GREY, UI_FONT_SMALL, (py + ph - sy - 10) / 10);
    }
}

/* A row's value, right-aligned, with arrows while it can be changed */
static void row_value(EidosUiCtx *c, int rx, int ry, int rw, int rowh, int kind, const char *value, int lit_)
{
    const char *shown = kind == EIDOS_SUI_MP_ROW_TEXT && value[0] == '\0' ? "Not set" : value;
    char arrows[112];

    if (kind == EIDOS_SUI_MP_ROW_VALUE && lit_) {
        snprintf(arrows, sizeof arrows, "< %s >", shown);
        shown = arrows;
    }
    ui_text(c, rx + rw - 8 - ui_text_w(shown, UI_FONT_MEDIUM), ry + ui_text_y(rowh, UI_FONT_MEDIUM), shown,
            kind == EIDOS_SUI_MP_ROW_TEXT && value[0] == '\0' ? UI_GREY : UI_CYAN, -1, UI_FONT_MEDIUM, 1);
}

/*
 * A list of the model's rows (the Mode view's options, the World view's
 * lines) above the foot button (the START row), scrolled so the focused row
 * shows; headings above the rows that have one.  Returns the focused row
 * (-1 none) for the side panel.
 */
static int row_list(EidosUiCtx *c, int x, int y, int w, int h, int rowh, int bh, const char *empty)
{
    int n = eidos_sui_mp_rows(), i, f, hov, gap = rowh >= 20 ? 4 : 2, pad = card_pad();
    int sy = y + h - bh - 12, top = y, bottom = sy - 6, cy = 0, focus_row = -1, total = 0;
    int want = g_want_act >= MP_ROW ? g_want_act - MP_ROW : g_focus_act >= MP_ROW ? g_focus_act - MP_ROW : -1;
    int want_y0 = -1, want_y1 = -1;
    char label_[96], value[96], tmp[96];
    EidosUiRect area;

    /* where each row sits, before the scroll; a heading where the group
     * changes (an option's heading is its group's) */
    const char *prev = "";

    for (i = 0; i < n; i++) {
        const char *head = "";
        int kind = eidos_sui_mp_row(i, label_, (int)sizeof label_, value, (int)sizeof value);

        if (kind == EIDOS_SUI_MP_ROW_START || kind < 0) {
            continue;
        }
        (void)eidos_sui_mp_row_extra(i, &head, NULL, NULL);
        if (head[0] != '\0' && strcmp(head, prev) != 0) {
            cy += 16;
        }
        prev = head;
        if (i == want) {
            want_y0 = cy;
            want_y1 = cy + rowh;
        }
        cy += (kind == EIDOS_SUI_MP_ROW_INFO ? 15 : rowh) + gap;
    }
    total = cy;
    area.x = x; area.y = top; area.w = w; area.h = bottom - top;
    g_list_px -= eidos_ui_wheel(area) * (rowh + gap);
    if (want_y0 >= 0 && want_y0 - (want_y0 < 20 ? want_y0 : 16) < g_list_px) {
        g_list_px = want_y0 - (want_y0 < 20 ? want_y0 : 16);    /* its heading shows too */
    }
    if (want_y1 >= 0 && want_y1 > g_list_px + (bottom - top)) {
        g_list_px = want_y1 - (bottom - top);
    }
    if (g_list_px > total - (bottom - top)) g_list_px = total - (bottom - top);
    if (g_list_px < 0) g_list_px = 0;

    cy = top - g_list_px;
    prev = "";
    for (i = 0; i < n; i++) {
        const char *head = "";
        int ok = 1, kind = eidos_sui_mp_row(i, label_, (int)sizeof label_, value, (int)sizeof value);
        int rh = kind == EIDOS_SUI_MP_ROW_INFO ? 15 : rowh;

        if (kind < 0) {
            continue;
        }
        (void)eidos_sui_mp_row_extra(i, &head, NULL, &ok);
        if (kind == EIDOS_SUI_MP_ROW_START) {
            /* the foot button; the model's word under it */
            f = target(c, x, sy, w, bh, MP_ROW + i, kind, ok, &hov);
            foot_button(c, x, sy, w, bh, label_, ok, lit(f), hov);
            if (f) focus_row = i;
            if (value[0] != '\0') {
                ui_text(c, x, sy + bh + 2, fit(tmp, sizeof tmp, value, w, UI_FONT_SMALL), 0xF2B33Du, -1,
                        UI_FONT_SMALL, 1);
            }
            continue;
        }
        if (head[0] != '\0' && strcmp(head, prev) == 0) {
            head = "";                  /* the group goes on */
        } else {
            prev = head;
        }
        if (head[0] != '\0') {
            if (cy >= top && cy + 16 + rh <= bottom) {   /* with its row, or not at all */
                char up[64];
                size_t k;

                for (k = 0; head[k] != '\0' && k < sizeof up - 1; k++) {
                    up[k] = (char)(head[k] >= 'a' && head[k] <= 'z' ? head[k] - 'a' + 'A' : head[k]);
                }
                up[k] = '\0';
                label(c, x + 2, cy + 3, up);
            }
            cy += 16;
        }
        if (cy >= top && cy + rh <= bottom) {
            if (kind == EIDOS_SUI_MP_ROW_INFO) {
                ui_text(c, x + 4, cy + ui_text_y(rh, UI_FONT_MEDIUM),
                        fit(tmp, sizeof tmp, label_, w / 2 - 8, UI_FONT_MEDIUM), UI_GREY, -1, UI_FONT_MEDIUM, 1);
                {
                    const char *v = fit(tmp, sizeof tmp, value, w / 2, UI_FONT_MEDIUM);

                    ui_text(c, x + w - 4 - ui_text_w(v, UI_FONT_MEDIUM), cy + ui_text_y(rh, UI_FONT_MEDIUM), v,
                            UI_WHITE, -1, UI_FONT_MEDIUM, 1);
                }
                ui_fill(c, x + 4, cy + rh, w - 8, 1, UI_EDGE, 160);
            } else {
                f = target(c, x, cy, w - 6, rh, MP_ROW + i, kind, 1, &hov);
                if (f) focus_row = i;
                card(c, x, cy, w - 6, rh, 1, lit(f), hov);
                ui_text(c, x + pad, cy + ui_text_y(rh, UI_FONT_MEDIUM),
                        fit(tmp, sizeof tmp, label_, w / 2 - pad, UI_FONT_MEDIUM), UI_WHITE, UI_EDGE,
                        UI_FONT_MEDIUM, 1);
                row_value(c, x, cy, w - 6, rh, kind, value, lit(f) || hov);
            }
        }
        cy += rh + gap;
    }
    if (total > bottom - top) {
        /* the style's bar, in rows: a finger drags it, or the list itself */
        int step = rowh + gap;

        ui_scrollbar(c, x + w - 4, top, bottom - top, (g_list_px + step - 1) / step, (bottom - top) / step,
                     (total + step - 1) / step, UI_CYAN);
    }
    if (n <= 1 && empty != NULL) {
        para(c, x, y + 2, w, empty, UI_GREY, UI_FONT_SMALL, 2);
    }
    return focus_row;
}

static void mode_view(EidosUiCtx *c, int x, int y, int w, int h)
{
    const char *line = "", *help = "";
    int lw = w * 3 / 5, focus_row;
    char row_label[96] = "", value[8];

    (void)eidos_sui_mp_mode_info(-1, NULL, NULL, &line);
    label(c, x, y, "OPTIONS");
    focus_row = row_list(c, x, y + 12, lw, h - 12, 22, 24, "This mode has no options.");
    if (focus_row >= 0) {
        (void)eidos_sui_mp_row(focus_row, row_label, (int)sizeof row_label, value, (int)sizeof value);
        (void)eidos_sui_mp_row_extra(focus_row, NULL, &help, NULL);
    }
    side_panel(c, x + lw + 8, y + 12, w - lw - 8, h - 12, "HOW IT WORKS", line, row_label, help);
}

/* An Archipelago room a typed port names: the server, the slot, the
 * password, and Join */
static void room_view(EidosUiCtx *c, int x, int y, int w, int h)
{
    const char *help = "";
    int lw = w * 3 / 5, focus_row;
    char row_label[96] = "", value[8];

    label(c, x, y, "ROOM");
    focus_row = row_list(c, x, y + 12, lw, h - 12, 22, 24, NULL);
    if (focus_row >= 0) {
        (void)eidos_sui_mp_row(focus_row, row_label, (int)sizeof row_label, value, (int)sizeof value);
        (void)eidos_sui_mp_row_extra(focus_row, NULL, &help, NULL);
    }
    side_panel(c, x + lw + 8, y + 12, w - lw - 8, h - 12, "ARCHIPELAGO",
               "Cross-game room generated by the host from each player's options file. Items you find "
               "can go to other games, and theirs to yours.", row_label, help);
}

/* The world a typed code names: its game, players and rules, and Join */
static void world_view(EidosUiCtx *c, int x, int y, int w, int h)
{
    const char *help = "";
    int lw = w * 3 / 5, focus_row;
    char row_label[96] = "", value[8];

    label(c, x, y, "WORLD RULES");
    focus_row = row_list(c, x, y + 12, lw, h - 12, 22, 24, NULL);
    if (focus_row >= 0) {
        (void)eidos_sui_mp_row(focus_row, row_label, (int)sizeof row_label, value, (int)sizeof value);
        (void)eidos_sui_mp_row_extra(focus_row, NULL, &help, NULL);
    }
    side_panel(c, x + lw + 8, y + 12, w - lw - 8, h - 12, "JOINING",
               "Everyone plays on their own save. Items you find go to other players. The world's "
               "creator set these rules.", row_label, help);
}

/* ------------------------------------------------------------------ */
/* The lobby                                                           */
/* ------------------------------------------------------------------ */

static void lobby(EidosUiCtx *c, int x, int y, int w, int h, u32 frame)
{
    BwMpPlayer p[BW_MP_PLAYERS_MAX];
    u32 n = bw_mp_player_count(), i, here = 0, mods_ok = 0, same_ver = 0;
    int host = bw_mp_is_host(), f, hov, standh = 60, slot, sx;
    const char *ver = bw_mp_version(), *block = host ? bw_mp_start_blocker() : NULL;
    const char *host_name = "the host";
    char buf[128], tmp[96];

    if (n > BW_MP_PLAYERS_MAX) {
        n = BW_MP_PLAYERS_MAX;
    }
    for (i = 0; i < n; i++) {
        if (bw_mp_player_at(i, &p[i]) != 0) {
            n = i;
            break;
        }
        here += p[i].connected != 0;
        mods_ok += p[i].mods_ok != 0;
        same_ver += p[i].version[0] == '\0' || strcmp(p[i].version, ver) == 0;
        if (p[i].peer == 1u) {
            host_name = p[i].name;
        }
    }

    /* the players, standing in a row */
    slot = n > 0 ? w / (int)(n < 8 ? (n > 4 ? n : 4) : 8) : w;
    if (slot > 96) slot = 96;
    sx = x + (w - slot * (int)(n < 8 ? (n > 4 ? n : 4) : 8)) / 2;
    for (i = 0; i < n && i < 8; i++) {
        AvatarLook look;
        const UiImage *fig;
        int px = sx + (int)i * slot, tw;

        /* each player as they chose to look (their hello says it) */
        avatar_look_from_bytes(p[i].look, &look);
        fig = ui_overworld_look(&look, 0, p[i].connected ? (int)((frame / 12 + i) & 3) : 0);
        const char *tag = !p[i].connected ? "Missing" : !p[i].mods_ok ? "Mods differ"
                        : (p[i].version[0] != '\0' && strcmp(p[i].version, ver) != 0) ? "Other version"
                        : "Here";
        u32 tagc = strcmp(tag, "Here") == 0 ? 0xA6F24Au : strcmp(tag, "Missing") == 0 ? UI_GREY : 0xF2B33Du;

        const char *shown;

        if (fig != NULL) {
            ui_image(c, px + (slot - fig->w) / 2, y, fig, 1);
        }
        snprintf(buf, sizeof buf, "%s%s", p[i].name, p[i].peer == 1u ? " (host)" : "");
        shown = fit(tmp, sizeof tmp, buf, slot - 4, UI_FONT_MEDIUM);
        tw = ui_text_w(shown, UI_FONT_MEDIUM);
        ui_text(c, px + (slot - tw) / 2, y + 34, shown, UI_WHITE, UI_EDGE, UI_FONT_MEDIUM, 1);
        tw = ui_text_w(tag, UI_FONT_SMALL);
        ui_text(c, px + (slot - tw) / 2, y + 47, tag, tagc, -1, UI_FONT_SMALL, 1);
    }
    for (; i < 4; i++) {
        int px = sx + (int)i * slot;

        ui_panel(c, px + slot / 2 - 12, y + 2, 24, 28, 0, 0, 0x0C0E12u, UI_EDGE, 200);
        ui_text(c, px + (slot - ui_text_w("Open", UI_FONT_SMALL)) / 2, y + 36, "Open", UI_GREY, -1,
                UI_FONT_SMALL, 1);
    }
    ui_fill(c, x + 20, y + standh, w - 40, 1, UI_CYAN, 255);

    /* below: what the start waits for, the rules, the code above Start */
    {
        int py = y + standh + 8, ph = h - standh - 8, cw3 = (w - 16) / 3;
        int ax = x, bx = x + cw3 + 8, cx = x + 2 * (cw3 + 8), cwl = w - 2 * (cw3 + 8);
        struct { const char *what; int ok; } checks[4];
        int k;

        checks[0].what = "Everyone is here";          checks[0].ok = here == n;
        checks[1].what = "Same eidos version";        checks[1].ok = same_ver == n;
        checks[2].what = "Multiplayer mods match";    checks[2].ok = mods_ok == n;
        checks[3].what = "Same game per save";          checks[3].ok = !host || bw_mp_start_blocker_kind() != BW_MP_BLOCK_SAVE_GAME;

        ui_panel(c, ax, py, cw3, ph, UI_CUT_TL | UI_CUT_BR, 6, UI_BODY2, UI_EDGE, 240);
        label(c, ax + 8, py + 6, "START CHECKLIST");
        for (k = 0; k < 4; k++) {
            check_box(c, ax + 8, py + 22 + k * 13, checks[k].ok);
            ui_text(c, ax + 20, py + 20 + k * 13, checks[k].what, checks[k].ok ? UI_LIGHT : 0xF2B33Du, -1,
                    UI_FONT_MEDIUM, 1);
        }

        ui_panel(c, bx, py, cw3, ph, UI_CUT_TL | UI_CUT_BR, 6, UI_BODY2, UI_EDGE, 240);
        label(c, bx + 8, py + 6, host ? "RULES" : "HOST RULES");
        {
            const char *mods = n > 0 ? p[0].mods : "";
            int ly = py + 20, rows = eidos_sui_mp_rows(), r, shown = 0;
            char rl[64], rv[64];

            for (i = 0; i < n; i++) {
                if (p[i].peer == 1u) {
                    mods = p[i].mods;
                }
            }
            /* the multiplayer mods' rules (their `@lobby` options): the
             * host changes them here, everyone plays by them */
            for (r = 0; r < rows && ly + 16 <= py + ph - 4; r++) {
                if (eidos_sui_mp_row(r, rl, (int)sizeof rl, rv, (int)sizeof rv) != EIDOS_SUI_MP_ROW_VALUE) {
                    continue;
                }
                f = target(c, bx + 4, ly, cw3 - 8, 15, MP_ROW + r, EIDOS_SUI_MP_ROW_VALUE, host, &hov);
                if (lit(f) || (hov && host)) {
                    ui_panel(c, bx + 4, ly, cw3 - 8, 15, UI_CUT_TL | UI_CUT_BR, 3, 0x0A283Cu, UI_CYAN, 255);
                }
                ui_text(c, bx + 8, ly + ui_text_y(15, UI_FONT_MEDIUM), fit(tmp, sizeof tmp, rl, cw3 / 2, UI_FONT_MEDIUM),
                        UI_LIGHT, -1, UI_FONT_MEDIUM, 1);
                if (rv[0] >= 'a' && rv[0] <= 'z') {
                    rv[0] = (char)(rv[0] - 'a' + 'A');     /* "on" as "On", beside "Off" */
                }
                snprintf(buf, sizeof buf, host && (lit(f) || hov) ? "< %s >" : "%s", rv);
                ui_text(c, bx + cw3 - 8 - ui_text_w(buf, UI_FONT_MEDIUM), ly + ui_text_y(15, UI_FONT_MEDIUM), buf,
                        UI_CYAN, -1, UI_FONT_MEDIUM, 1);
                ly += 17;
                shown++;
            }
            if (shown > 0) {
                /* the rules are drawn; the mods' names are not repeated */
            } else if (mods[0] == '\0') {
                para(c, bx + 8, ly, cw3 - 16, "No multiplayer mods. Default rules apply.", UI_GREY,
                     UI_FONT_SMALL, 3);
            } else {
                const char *s = mods;

                while (*s != '\0' && ly < py + ph - 12) {
                    size_t len = strcspn(s, ";");
                    char one[64];

                    snprintf(one, sizeof one, "%.*s", (int)(len < 60 ? len : 60), s);
                    {
                        char *eq = strchr(one, '=');

                        if (eq != NULL) {
                            *eq = '\0';
                        }
                    }
                    ui_text(c, bx + 8, ly, one, UI_LIGHT, -1, UI_FONT_MEDIUM, 1);
                    ly += 13;
                    s += len;
                    if (*s == ';') s++;
                }
            }
        }

        /* the code, big, above Start */
        ui_panel(c, cx, py, cwl, ph, UI_CUT_TL | UI_CUT_BR, 6, UI_BODY2, UI_EDGE, 240);
        label(c, cx + 8, py + 6, "JOIN CODE");
        {
            const char *code = bw_mp_code();
            int cy = py + 18, bh = 20, sy = py + ph - 8 - bh - 22;

            ui_text(c, cx + 8, cy, code != NULL && code[0] != '\0' ? code : "------", UI_CYAN, UI_EDGE,
                    UI_FONT_LARGE, 2);
            f = target(c, cx + 8, cy + 30, 46, 16, EIDOS_SUI_MP_ACT_COPY, 0, 1, &hov);
            ui_panel(c, cx + 8, cy + 30, 46, 16, UI_CUT_TL | UI_CUT_BR, 3, lit(f) || hov ? UI_CYAN2 : UI_BODY,
                     lit(f) || hov ? UI_CYAN : UI_EDGE, 255);
            ui_text(c, cx + 8 + (46 - ui_text_w("Copy", UI_FONT_SMALL)) / 2, cy + 30 + ui_text_y(16, UI_FONT_SMALL),
                    "Copy", UI_WHITE, -1, UI_FONT_SMALL, 1);
            if (host) {
                /* (the model's reason is a text, "" when the lobby is ready: never NULL) */
                int ok = block == NULL || block[0] == '\0';

                f = target(c, cx + 8, sy, cwl - 16, bh, EIDOS_SUI_MP_ACT_START, 0, ok, &hov);
                if (ui_theme_active() != NULL && ui_theme_active()->mp_part != NULL) {
                    ui_theme_active()->mp_part(c, 2, cx + 8, sy, cwl - 16, bh, lit(f), hov, ok);
                } else {
                    ui_panel(c, cx + 8, sy, cwl - 16, bh, UI_CUT_TL | UI_CUT_BR, 4,
                             ok ? (lit(f) || hov ? UI_CYAN : UI_CYAN2) : 0x14181Cu, ok ? UI_CYAN : UI_EDGE, 255);
                }
                ui_text(c, cx + 8 + (cwl - 16 - ui_text_w("Start", UI_FONT_LARGE)) / 2,
                        sy + ui_text_y(bh, UI_FONT_LARGE), "Start", ok ? UI_WHITE : UI_GREY, -1, UI_FONT_LARGE, 1);
                /* the model's reason, as a sentence */
                snprintf(buf, sizeof buf, "%s", ok ? "Everyone gets their save from you." : block);
                if (buf[0] >= 'a' && buf[0] <= 'z') {
                    buf[0] = (char)(buf[0] - 'a' + 'A');
                }
                para(c, cx + 8, sy + bh + 4, cwl - 16, buf, ok ? UI_GREY : 0xF2B33Du, UI_FONT_SMALL, 2);
            } else {
                snprintf(buf, sizeof buf, "Waiting for %s to start.", host_name);
                para(c, cx + 8, sy + 4, cwl - 16, buf, UI_LIGHT, UI_FONT_MEDIUM, 3);
            }
        }
    }
}

/* ------------------------------------------------------------------ */
/* The recipe                                                          */
/* ------------------------------------------------------------------ */

static int mp_match(void)
{
    if (eidos_ui_name_entry_up()) {
        return 0;                       /* the game's keyboard is its own scene */
    }
    /* the screen is up (the title's own state or the port's options
     * process), not only its last view remembered after it closed */
    return eidos_sui_mp_up() && (bw_mod_title_in_settings() || eidos_ui_options_proc_up);
}

static void mp_reset(void)
{
    g_focus = 0;
    g_trainer = 0;
    g_last_view = -9;
}

static void mp_run(EidosUiCtx *c, EidosUiView *view, const EidosUiFrame *in)
{
    static const char *const hints_hub[] = { "A", "Choose", "B", "Back" };
    static const u32 keys_hub[] = { BW_KEY_A, BW_KEY_B };
    static const char *const hints_lobby[] = { "A", "Choose", "B", "Close" };
    int cw = c->canvas->w, ch = c->canvas->h, v = eidos_sui_mp_view();
    int in_game = bw_mp_state() != BW_MP_OFF && bw_mp_state() != BW_MP_ENDED;
    int mw, mh, mx, my, key = in_game ? 100 : g_trainer ? 200 : v;
    char status[96], sub[96];

    view->mode = EIDOS_VIEW_WORLD;
    view->zoom = 1.0f;
    view->blur = 1;
    view->hide_2d = 1;
    if (key != g_last_view) {
        g_focus = 0;                    /* a new view: the focus on its first part */
        g_last_view = key;
        g_list_px = 0;
        g_want_act = -9;
    }
    /* what the focus was on (the lists keep it in sight), before the
     * targets are drawn again */
    g_focus_act = g_focus >= 0 && g_focus < g_nt ? g_t[g_focus].act : -9;
    g_focus_arg = g_focus >= 0 && g_focus < g_nt ? g_t[g_focus].arg : 0;
    g_nt = 0;

    mw = cw - 24;
    if (mw > 600) mw = 600;
    mh = ch - ui_bar_h(c) - 16;
    if (mh > 330) mh = 330;
    mx = (cw - mw) / 2;
    my = (ch - ui_bar_h(c) - mh) / 2;
    if (!ui_app_window(c, "multiplayer", &mx, &my, &mw, &mh)) {
        ui_panel(c, mx, my, mw, mh, UI_CUT_TL | UI_CUT_BR, 10, 0x060E1Au, 0x103C5Au, 245);
    }
    if (in_game) {
        const char *game = bw_mp_game();

        snprintf(sub, sizeof sub, "%s", bw_mp_is_host() ? "Hosting" : "Co-op Adventure");
        ui_header(c, mx + 4, my + 4, mw - 8, game != NULL && game[0] != '\0' ? game : "CO-OP ADVENTURE", sub,
                  NULL, UI_CYAN);
        lobby(c, mx + 10, my + 32, mw - 20, mh - 40, in->frame);
    } else if (v == EIDOS_SUI_MP_HOST) {
        ui_header(c, mx + 4, my + 4, mw - 8, "CO-OP ADVENTURE", "Host a game", NULL, UI_CYAN);
        host_list(c, mx + 10, my + 32, mw - 20, mh - 40);
    } else if (v == EIDOS_SUI_MP_MODE) {
        const char *title = "";
        char up[64];
        size_t k;

        (void)eidos_sui_mp_mode_info(-1, NULL, &title, NULL);
        for (k = 0; title[k] != '\0' && k < sizeof up - 1; k++) {
            up[k] = (char)(title[k] >= 'a' && title[k] <= 'z' ? title[k] - 'a' + 'A' : title[k]);
        }
        up[k] = '\0';
        ui_header(c, mx + 4, my + 4, mw - 8, up, eidos_sui_mp_mode_owner(), NULL, UI_CYAN);
        mode_view(c, mx + 10, my + 32, mw - 20, mh - 40);
    } else if (v == EIDOS_SUI_MP_ROOM) {
        ui_header(c, mx + 4, my + 4, mw - 8, "ARCHIPELAGO ROOM", "Join", NULL, UI_CYAN);
        room_view(c, mx + 10, my + 32, mw - 20, mh - 40);
    } else if (v == EIDOS_SUI_MP_WORLD) {
        const char *code = eidos_settings_session_join_code();

        snprintf(sub, sizeof sub, "World %s", code != NULL ? code : "");
        ui_header(c, mx + 4, my + 4, mw - 8, "JOIN WORLD", sub, NULL, UI_CYAN);
        world_view(c, mx + 10, my + 32, mw - 20, mh - 40);
    } else if (v == EIDOS_SUI_MP_BUSY || v == EIDOS_SUI_MP_JOIN) {
        /* the code being joined (the one in force is a session's, later) */
        const char *code = eidos_settings_session_join_code();

        ui_header(c, mx + 4, my + 4, mw - 8, "PLAY TOGETHER", "Connecting", NULL, UI_CYAN);
        snprintf(sub, sizeof sub, v == EIDOS_SUI_MP_JOIN && code != NULL && code[0] != '\0'
                 ? "Joining %s" : "Contacting relay", code);
        ui_text(c, mx + (mw - ui_text_w(sub, UI_FONT_LARGE)) / 2, my + mh / 2 - 6, sub, UI_WHITE, UI_EDGE,
                UI_FONT_LARGE, 1);
    } else if (g_trainer) {
        /* the trainer panel: B is its own (back to the hub), not the
         * screen's (which would leave it) */
        eidos_ui_take_keys(BW_KEY_B);
        if (eidos_ui_keys_pressed() & BW_KEY_B) {
            do_act(TR_DONE, 0);
        }
        ui_header(c, mx + 4, my + 4, mw - 8, "YOUR TRAINER", "Name & avatar", NULL, UI_CYAN);
        trainer_view(c, mx + 10, my + 32, mw - 20, mh - 40, in->frame);
    } else {
        ui_header(c, mx + 4, my + 4, mw - 8, "PLAY TOGETHER", "Multiplayer", NULL, UI_CYAN);
        hub(c, mx + 10, my + 32, mw - 20, mh - 40, in->frame);
    }
    /* the model's own line: connecting, a code not found, how a session ended */
    if (eidos_sui_mp_status(status, (int)sizeof status)) {
        int sw = ui_text_w(status, UI_FONT_MEDIUM) + 16;

        ui_panel(c, (cw - sw) / 2, my + mh - 22, sw, 16, UI_CUT_TL | UI_CUT_BR, 4, 0x2A1A00u, 0xF2B33Du, 250);
        ui_text(c, (cw - sw) / 2 + 8, my + mh - 22 + ui_text_y(16, UI_FONT_MEDIUM), status, 0xF2B33Du, -1,
                UI_FONT_MEDIUM, 1);
    }
    {
        /* in the trainer panel B is the panel's (Done); its hint only shows the key */
        static const u32 keys_trainer[] = { BW_KEY_A, 0 };

        ui_hints(c, ch - ui_bar_h(c), cw, in_game ? hints_lobby : hints_hub, 2,
                 !in_game && g_trainer ? keys_trainer : keys_hub);
    }
    keys_step();
}

static const EidosUiRecipe recipe_multiplayer = {
    "multiplayer", mp_match, mp_run, mp_reset, 0
};

const EidosUiRecipe *const ui_recipes_multiplayer[] = { &recipe_multiplayer };
const u32 ui_recipes_multiplayer_count = 1;
