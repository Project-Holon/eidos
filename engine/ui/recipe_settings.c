/*
 * engine/ui/recipe_settings.c -- the one-screen recipe for the port's own
 * settings screen (engine/platform/settings_ui.h): the title menu's
 * SETTINGS, and the screen the title menu's MULTIPLAYER row opens
 * (engine/platform/mod_options_proc.c).
 *
 * The model is the port's, so everything is read through the settings
 * screen's own calls (eidos_sui_*): the header, the rows, the choices, the
 * cursor, the help text.  A click becomes the keys the screen already
 * reads (the cursor moved to the row, then A, or LEFT / RIGHT to a
 * choice), pressed one at a time a few frames apart, so the screen does
 * everything it does for a player with a pad: its sounds, its questions,
 * its keyboard.
 */

#include <stdio.h>
#include <string.h>

#include "ui.h"
#include "kit.h"
#include "padglyph.h"
#include "input.h"
#include "settings_ui.h"

extern int bw_mod_title_in_settings(void);
extern int eidos_opt_proc_active(void);

/* ------------------------------------------------------------------ */
/* Keys, one at a time                                                 */
/* ------------------------------------------------------------------ */

#define QMAX 24

static u32 g_q[QMAX];
static int g_qn;
static u32 g_next;

static void q_clear(void)
{
    g_qn = 0;
}

static void q_push(u32 key, int times)
{
    while (times-- > 0 && g_qn < QMAX) {
        g_q[g_qn++] = key;
    }
}

static void q_step(u32 frame)
{
    if (g_qn == 0 || frame < g_next) {
        return;
    }
    eidos_ui_press(g_q[0], 2);
    memmove(g_q, g_q + 1, (size_t)(g_qn - 1) * sizeof(g_q[0]));
    g_qn--;
    g_next = frame + 5;
}

/* ------------------------------------------------------------------ */
/* Text that fits                                                      */
/* ------------------------------------------------------------------ */

/* `s` as drawn in at most `maxw` pixels: itself when it fits, else
 * shortened with "...": a path keeps its end (the folder's own name), any
 * other text its start.  Cuts fall between UTF-8 characters. */
static const char *fit(char *buf, size_t n, const char *s, int maxw, int font)
{
    size_t len = strlen(s), k;
    int path = strchr(s, '/') != NULL || strchr(s, '\\') != NULL;

    if (ui_text_w(s, font) <= maxw || n < 8) {
        return s;
    }
    if (path) {
        for (k = 1; k < len; k++) {
            if (((unsigned char)s[k] & 0xC0u) == 0x80u) {
                continue;                   /* inside a character          */
            }
            if (len - k + 4 > n) {
                continue;
            }
            memcpy(buf, "...", 3);
            memcpy(buf + 3, s + k, len - k + 1);
            if (ui_text_w(buf, font) <= maxw) {
                return buf;
            }
        }
    } else {
        for (k = len < n - 4 ? len : n - 4; k > 0; k--) {
            if (((unsigned char)s[k] & 0xC0u) == 0x80u) {
                continue;
            }
            memcpy(buf, s, k);
            memcpy(buf + k, "...", 4);
            if (ui_text_w(buf, font) <= maxw) {
                return buf;
            }
        }
    }
    snprintf(buf, n, "...");
    return buf;
}

/* ------------------------------------------------------------------ */
/* The recipe                                                          */
/* ------------------------------------------------------------------ */

static int settings_match(void)
{
    return bw_mod_title_in_settings() || eidos_opt_proc_active();
}

static void settings_reset(void)
{
    q_clear();
}

static void settings_run(EidosUiCtx *c, EidosUiView *view, const EidosUiFrame *in)
{
    static const char *const hints[] = { "A", "Choose", "B", "Back", "L R", "Section" };
    static const u32 hint_keys[] = { BW_KEY_A, BW_KEY_B, BW_KEY_R };
    int cw = c->canvas->w, ch = c->canvas->h;
    int mw, mh, mx, my, i, n, cur, rowh = 20, top;
    int tabs = eidos_sui_tab_count(), tab = eidos_sui_tab(), level = eidos_sui_level();
    char head[48], value[48], label[48];
    int target = -1, target_choice = -1, target_adj = 0;
    int first = 0, shown = 0, total = 0;

    view->mode = EIDOS_VIEW_WORLD;
    view->zoom = 1.0f;
    view->blur = 1;
    view->hide_2d = 1;

    n = eidos_sui_row_count();
    cur = eidos_sui_cursor_row();
    eidos_sui_row_text(0, head, sizeof(head), value, sizeof(value));

    mw = cw * 80 / 100;
    if (mw > 460) mw = 460;
    if (mw < 300) mw = 300;
    mh = ch - 17 - 16;
    if (mh > 270) mh = 270;
    mx = (cw - mw) / 2;
    my = (ch - 17 - mh) / 2;
    ui_panel(c, mx, my, mw, mh, UI_CUT_TL | UI_CUT_BR, 10, 0x060E1Au, 0x103C5Au, 245);
    ui_header(c, mx + 4, my + 4, mw - 8, "SETTINGS", head[0] != '\0' ? head : NULL, NULL, UI_CYAN);
    top = my + 30;

    /* the sections as tabs, inside a section */
    if (level == 1 && tabs > 1) {
        int tx = mx + 8;

        for (i = 0; i < tabs; i++) {
            const char *name = eidos_sui_tab_name(i);
            int tw = ui_text_w(name, UI_FONT_SMALL) + 16;
            EidosUiRect r;
            int st, sel = i == tab;

            if (tx + tw > mx + mw - 8) {
                break;
            }
            r.x = tx; r.y = top; r.w = tw; r.h = 14;
            st = eidos_ui_hit(r);
            ui_panel(c, tx, top, tw, 14, UI_CUT_TL | UI_CUT_BR, 4,
                     sel ? UI_CYAN2 : ((st & EIDOS_UI_HOVER) ? 0x242424u : UI_BODY2),
                     sel ? UI_CYAN : UI_EDGE, 255);
            ui_text(c, tx + 8, top + ui_text_y(14, UI_FONT_SMALL), name,
                    sel ? UI_WHITE : UI_GREY, sel ? 0x003C50 : (s32)UI_EDGE, UI_FONT_SMALL, 1);
            if ((st & EIDOS_UI_CLICK) && !sel) {
                q_clear();
                q_push(i > tab ? BW_KEY_R : BW_KEY_L, i > tab ? i - tab : tab - i);
            }
            tx += tw + 3;
        }
        top += 19;
    }

    /* the rows */
    (void)eidos_sui_scroll(&first, &shown, &total);
    for (i = 1; i < n; i++) {
        int sel = i == cur && eidos_ui_keys_lit(), ry = top + (i - 1) * (rowh + 2), rw = mw - 16 - 8;
        int is_back = eidos_sui_has_back() && i == n - 1;
        int active = 0, nch, st, pctl;
        char keyname[48];
        char names[3][20];
        EidosUiRect r;

        if (ry + rowh > my + mh - 40) {
            break;
        }
        eidos_sui_row_text(i, label, sizeof(label), value, sizeof(value));
        r.x = mx + 8; r.y = ry; r.w = rw; r.h = rowh;
        st = eidos_ui_hit(r);
        if ((st & EIDOS_UI_HOVER) && i != cur && g_qn == 0 && !eidos_sui_capturing()) {
            /* the cursor follows the mouse, so a key goes on from the row
             * the mouse was on */
            (void)eidos_sui_hover(i);
        }
        ui_panel(c, mx + 8, ry, rw, rowh, UI_CUT_TL | UI_CUT_BR, 5,
                 sel ? 0x0A283Cu : (is_back ? 0x141418u : ((st & EIDOS_UI_HOVER) ? 0x1C1C1Cu : UI_BODY)),
                 sel ? UI_CYAN : ((st & EIDOS_UI_HOVER) ? UI_EDGE2 : UI_EDGE), 255);
        ui_text(c, mx + 16, ry + ui_text_y(rowh, UI_FONT_LARGE), label,
                is_back ? UI_GREY : UI_WHITE, sel ? 0x003C50 : (s32)UI_GREY, UI_FONT_LARGE, 1);
        nch = eidos_sui_row_choices(i, &active, names);
        if (nch > 0) {
            int cwid = 52, j, cx = mx + 8 + rw - 8 - cwid * nch;

            for (j = 0; j < nch; j++) {
                EidosUiRect pr;
                int pst, on = j == active;

                pr.x = cx + j * cwid; pr.y = ry + 3; pr.w = cwid - 4; pr.h = 14;
                pst = eidos_ui_hit(pr);
                if (on) {
                    ui_panel(c, pr.x, pr.y, pr.w, pr.h, UI_CUT_TL | UI_CUT_BR, 3, UI_CYAN2, UI_CYAN, 255);
                } else if (pst & EIDOS_UI_HOVER) {
                    ui_panel(c, pr.x, pr.y, pr.w, pr.h, UI_CUT_TL | UI_CUT_BR, 3, 0x242424u, UI_EDGE2, 255);
                }
                ui_text(c, pr.x + (pr.w - ui_text_w(names[j], UI_FONT_SMALL)) / 2,
                        pr.y + ui_text_y(14, UI_FONT_SMALL), names[j], on ? UI_WHITE : UI_EDGE2,
                        on ? 0x003C50 : -1, UI_FONT_SMALL, 1);
                if (pst & EIDOS_UI_CLICK) {
                    target = i;
                    target_choice = j - active;
                }
            }
        } else if ((pctl = eidos_sui_row_pad(i, keyname, sizeof(keyname))) >= 0) {
            /* a binding with a pad control: the key, then the control's icon
             * in the pad's own look */
            int gw = ui_pad_glyph_w(ui_pad_family(), pctl), right = mx + 8 + rw - 8;
            char kv[64];

            snprintf(kv, sizeof(kv), "%s  /", keyname[0] != '\0' ? keyname : "-");
            ui_pad_glyph(c, right - gw, ry + (rowh - 13) / 2, ui_pad_family(), pctl);
            ui_text(c, right - gw - 5 - ui_text_w(kv, UI_FONT_LARGE),
                    ry + ui_text_y(rowh, UI_FONT_LARGE), kv, UI_LIGHT, UI_EDGE, UI_FONT_LARGE, 1);
        } else if (value[0] != '\0') {
            /* from the right: the right arrow, the value, the left arrow;
             * the value at most half the row (a long one is shortened) */
            int adj = eidos_sui_row_adjustable(i);
            int right = mx + 8 + rw - 8, aw = 14, vw, vx;
            char shown[96];
            const char *v = fit(shown, sizeof(shown), value, rw / 2, UI_FONT_LARGE);

            vw = ui_text_w(v, UI_FONT_LARGE);
            vx = right - vw - (adj ? aw + 4 : 4);
            if (adj) {
                EidosUiRect lr, rr;
                int ls, rs;

                rr.x = right - aw; rr.y = ry; rr.w = aw; rr.h = rowh;
                lr.x = vx - 4 - aw; lr.y = ry; lr.w = aw; lr.h = rowh;
                ls = eidos_ui_hit(lr);
                rs = eidos_ui_hit(rr);
                ui_text(c, lr.x + (aw - ui_text_w("\xE2\x86\x90", UI_FONT_LARGE)) / 2,
                        ry + ui_text_y(rowh, UI_FONT_LARGE), "\xE2\x86\x90",
                        (ls & EIDOS_UI_HOVER) ? UI_WHITE : UI_CYAN, -1, UI_FONT_LARGE, 1);
                ui_text(c, rr.x + (aw - ui_text_w("\xE2\x86\x92", UI_FONT_LARGE)) / 2,
                        ry + ui_text_y(rowh, UI_FONT_LARGE), "\xE2\x86\x92",
                        (rs & EIDOS_UI_HOVER) ? UI_WHITE : UI_CYAN, -1, UI_FONT_LARGE, 1);
                if (ls & EIDOS_UI_CLICK) { target = i; target_adj = -1; }
                if (rs & EIDOS_UI_CLICK) { target = i; target_adj = 1; }
            }
            ui_text(c, vx, ry + ui_text_y(rowh, UI_FONT_LARGE), v, UI_LIGHT, UI_EDGE,
                    UI_FONT_LARGE, 1);
        }
        if ((st & EIDOS_UI_CLICK) && target < 0) {
            target = i;
        }
    }
    if (total > shown && shown > 0) {
        ui_scrollbar(c, mx + mw - 10, top, (EIDOS_SUI_VISIBLE + 1) * (rowh + 2) - 2,
                     first, shown, total, UI_CYAN);
    }

    /* the help text for the highlighted row */
    {
        int hy = my + mh - 36, hn = eidos_sui_help_count(), j;

        ui_panel(c, mx + 8, hy, mw - 16, 30, UI_CUT_TL | UI_CUT_BR, 5, UI_BODY2, UI_EDGE, 255);
        for (j = 0; j < hn && j < 2; j++) {
            char line[96];

            char shown[96];

            eidos_sui_help_line(j, line, sizeof(line));
            ui_text(c, mx + 16, hy + 2 + j * 13,
                    fit(shown, sizeof(shown), line, mw - 32, UI_FONT_MEDIUM),
                    UI_LIGHT, UI_EDGE, UI_FONT_MEDIUM, 1);
        }
    }

    ui_hints(c, ch - 17, cw, hints, level == 1 && tabs > 1 ? 3 : 2, hint_keys);

    /* what the mouse asked for, as the keys the screen reads */
    if (target >= 1 && g_qn == 0 && !eidos_sui_capturing()) {
        int d = target - cur;

        q_push(d > 0 ? BW_KEY_DOWN : BW_KEY_UP, d > 0 ? d : -d);
        if (target_choice != -1 && target_choice != 0) {
            q_push(target_choice > 0 ? BW_KEY_RIGHT : BW_KEY_LEFT,
                   target_choice > 0 ? target_choice : -target_choice);
        } else if (target_adj != 0) {
            q_push(target_adj > 0 ? BW_KEY_RIGHT : BW_KEY_LEFT, 1);
        } else if (target_choice == -1 && (d == 0 || !eidos_sui_row_adjustable(target))) {
            /* a section, an action, Back: A, as the player would */
            int nch, act;
            char names[3][20];

            nch = eidos_sui_row_choices(target, &act, names);
            if (nch == 0) {
                q_push(BW_KEY_A, 1);
            }
        }
    }
    {
        EidosUiRect whole, panel;

        whole.x = 0; whole.y = 0; whole.w = cw; whole.h = ch - 17;
        panel.x = mx; panel.y = my; panel.w = mw; panel.h = mh;
        if (eidos_ui_right_click() ||
            ((eidos_ui_hit(whole) & EIDOS_UI_CLICK) && !(eidos_ui_hit(panel) & EIDOS_UI_HOVER))) {
            q_clear();
            q_push(BW_KEY_B, 1);
        }
        if (eidos_ui_wheel(panel) != 0) {
            int w = eidos_ui_wheel(panel);

            q_push(w > 0 ? BW_KEY_UP : BW_KEY_DOWN, w > 0 ? w : -w);
        }
    }
    q_step(in->frame);
}

static const EidosUiRecipe recipe_settings = { "settings", settings_match, settings_run,
                                               settings_reset, 0 };

const EidosUiRecipe *const ui_recipes_settings[] = { &recipe_settings };
const u32 ui_recipes_settings_count = 1;
