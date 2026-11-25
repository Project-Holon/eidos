/*
 * sdk/mod_popup.c -- a mod's pop-ups, mod API version 7 (docs/mod-services.md,
 * "Version 7: the web, the clock, the game's hash and the pop-up").
 *
 * The services table's popup puts one in a queue of eight; the interface
 * (engine/ui/popup.c) shows the first for a few seconds in the style in
 * force, then takes it off and shows the next.  A build without the
 * interface shows nothing and the queue simply fills.
 */

#include <stdio.h>
#include <string.h>

#include "types.h"
#include "mod_api.h"
#include "mod_popup.h"

typedef struct Popup {
    char title[BW_POPUP_TITLE_MAX];
    char text[BW_POPUP_TEXT_MAX];
    u32  icon;
} Popup;

static Popup g_queue[BW_POPUP_QUEUE];
static u32   g_first, g_count, g_serial;

/* `src` cut to fit `out`, never in the middle of a UTF-8 character. */
static void copy_utf8(char *out, size_t size, const char *src)
{
    size_t n = src != NULL ? strlen(src) : 0;

    if (n >= size) {
        n = size - 1;
        while (n > 0 && ((unsigned char)src[n] & 0xC0u) == 0x80u) {
            n--;
        }
    }
    if (n > 0) {
        memcpy(out, src, n);
    }
    out[n] = '\0';
}

int bw_mod_popup(const char *title, const char *text, u32 icon)
{
    Popup *p;

    if ((title == NULL || title[0] == '\0') && (text == NULL || text[0] == '\0')) {
        return -1;
    }
    if (g_count >= BW_POPUP_QUEUE) {
        return -1;
    }
    p = &g_queue[(g_first + g_count) % BW_POPUP_QUEUE];
    copy_utf8(p->title, sizeof p->title, title);
    copy_utf8(p->text, sizeof p->text, text);
    p->icon = icon <= BW_POPUP_WARNING ? icon : BW_POPUP_NONE;
    g_count++;
    return 0;
}

int bw_mod_popup_peek(const char **title, const char **text, u32 *icon, u32 *serial)
{
    const Popup *p;

    if (g_count == 0) {
        return 0;
    }
    p = &g_queue[g_first];
    if (title != NULL) *title = p->title;
    if (text != NULL) *text = p->text;
    if (icon != NULL) *icon = p->icon;
    if (serial != NULL) *serial = g_serial;
    return 1;
}

void bw_mod_popup_done(void)
{
    if (g_count > 0) {
        g_first = (g_first + 1u) % BW_POPUP_QUEUE;
        g_count--;
        g_serial++;
    }
}

void bw_mod_popup_clear(void)
{
    g_first = 0;
    g_count = 0;
    g_serial++;
}
