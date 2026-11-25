/*
 * engine/ui/popup.c -- a mod's pop-up (mod API version 7, the services
 * table's popup; sdk/mod_popup.c holds the queue).
 *
 * The first pop-up waiting shows as one box of the style (ui_popup), its
 * title in the accent over its text, at the top right: under the field's
 * bar and the speed badge, in a battle under the foes' plates when they
 * stand there (Harmonia).  It slides in from the right edge, stays four
 * seconds and slides out the same way; then the next one comes.  It is
 * drawn over every one-screen scene but the title screens, after the
 * scene's own drawing.
 *
 * Test switch: EIDOS_DEBUG_POPUP="title|text|icon;..." queues pop-ups at
 * the first frame (docs/test-switches.md).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ui.h"
#include "kit.h"
#include "mod_popup.h"
#include "popup.h"

#define POPUP_IN    12u     /* frames to slide in, and out */
#define POPUP_SHOW  240u    /* frames in place (four seconds) */

static u32 g_serial = 0xFFFFFFFFu;
static u32 g_start;

static void debug_queue(void)
{
    static int done;
    const char *s = getenv("EIDOS_DEBUG_POPUP");
    char item[256];

    if (done || s == NULL) {
        return;
    }
    done = 1;
    while (*s != '\0') {
        const char *end = strchr(s, ';');
        size_t n = end != NULL ? (size_t)(end - s) : strlen(s);
        char *bar1, *bar2;

        if (n >= sizeof item) {
            n = sizeof item - 1;
        }
        memcpy(item, s, n);
        item[n] = '\0';
        bar1 = strchr(item, '|');
        bar2 = bar1 != NULL ? strchr(bar1 + 1, '|') : NULL;
        if (bar1 != NULL) {
            *bar1 = '\0';
        }
        if (bar2 != NULL) {
            *bar2 = '\0';
        }
        (void)bw_mod_popup(item, bar1 != NULL ? bar1 + 1 : "", bar2 != NULL ? (u32)atoi(bar2 + 1) : 0u);
        s += n;
        if (*s == ';') {
            s++;
        }
    }
}

void eidos_ui_popup_draw(EidosUiCtx *c, u32 frame, int below, int centred)
{
    const char *title = NULL, *text = NULL;
    u32 icon = 0, serial = 0, age;
    int w, h, x, y, slide = 0;

    debug_queue();
    if (!bw_mod_popup_peek(&title, &text, &icon, &serial)) {
        return;
    }
    if (serial != g_serial) {
        g_serial = serial;
        g_start = frame;
    }
    age = frame - g_start;
    if (age >= POPUP_SHOW + 2u * POPUP_IN) {
        bw_mod_popup_done();
        return;
    }
    (void)icon;
    w = ui_popup_w(title, text);
    if (w > c->canvas->w - 6) {
        w = c->canvas->w - 6;
    }
    h = ui_popup_h(title, text);
    (void)h;
    (void)centred;
    /* the top right, flush on the screen's right edge; it slides in from
     * that edge and out the same way */
    if (age < POPUP_IN) {
        slide = (int)((POPUP_IN - age) * (u32)w / POPUP_IN);
    } else if (age >= POPUP_IN + POPUP_SHOW) {
        slide = (int)((age - POPUP_IN - POPUP_SHOW) * (u32)w / POPUP_IN);
    }
    x = c->canvas->w - w + slide;
    y = below;
    ui_popup(c, x, y, w, title, text);
}
