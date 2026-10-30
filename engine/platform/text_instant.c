/*
 * engine/platform/text_instant.c -- the Instant text setting (Extras).
 *
 * The game prints a message through one routine (src/game/arm9/printsys.c):
 * PRINTSYS_PrintStream starts a text stream whose task prints `glyphsPerTick`
 * characters every `waitFrames` frames, from the wait the caller passes (the
 * player's text speed, MSG_SPEED_GetWait); PRINTSYS_SplitWait reads a wait
 * of -n as n characters a frame with no wait.  The task stops on its own at
 * the end of a page (the control code that waits for a button) and at the
 * end of the text, whatever the speed.
 *
 * With Instant text on, every stream is started, and every speed change is
 * made, with the wait -255: up to 255 characters a frame, which is a whole
 * page, and the page's own stop is the game's, unchanged.  The three entry
 * points are wrapped at link time (-Wl,--wrap, CMakeLists.txt), so the game's
 * callers in the ARM9 and in the overlays reach these first.
 *
 * Linked into eidos_boot only, beside the wrap options: a link without them
 * has no __real_ names.
 */

#include "settings.h"

#include <stdint.h>

/* As many characters a frame as a page can hold (PRINTSYS_SplitWait). */
#define INSTANT_WAIT (-255)

struct BmpWin;
struct Strbuf;
struct GfFont;
struct Tcbl;
struct TextPrinter;
typedef unsigned char (*PrintCallback)(uint32_t arg);

struct TextPrinter *__real_PRINTSYS_PrintStream(
    struct BmpWin *win, int16_t x, int16_t y, struct Strbuf *str,
    struct GfFont *font, int32_t wait, struct Tcbl *tcbl, uint32_t priority,
    uint16_t heap, uint16_t fillColor);
struct TextPrinter *__real_PRINTSYS_PrintStreamCallBack(
    struct BmpWin *win, int16_t x, int16_t y, struct Strbuf *str,
    struct GfFont *font, int32_t wait, struct Tcbl *tcbl, uint32_t priority,
    uint32_t heap, uint16_t fillColor, PrintCallback callback);
void __real_PRINTSYS_PrintStreamSetSpeed(struct TextPrinter *printer, int32_t wait);

/* The window the game printed a message stream into last, and how many it
 * has printed: the one-screen evolution page reads the game's message from
 * that window (engine/ui/recipe_party.c). */
static struct BmpWin *g_last_window;
static uint32_t g_print_count;

struct BmpWin *eidos_text_last_window(uint32_t *count)
{
    if (count != NULL) {
        *count = g_print_count;
    }
    return g_last_window;
}

static int32_t instant(int32_t wait)
{
    return eidos_settings_get(EIDOS_OPT_EXTRAS_INSTANT_TEXT) ? INSTANT_WAIT : wait;
}

struct TextPrinter *__wrap_PRINTSYS_PrintStream(
    struct BmpWin *win, int16_t x, int16_t y, struct Strbuf *str,
    struct GfFont *font, int32_t wait, struct Tcbl *tcbl, uint32_t priority,
    uint16_t heap, uint16_t fillColor)
{
    g_last_window = win;
    g_print_count++;
    return __real_PRINTSYS_PrintStream(win, x, y, str, font, instant(wait), tcbl,
                                       priority, heap, fillColor);
}

struct TextPrinter *__wrap_PRINTSYS_PrintStreamCallBack(
    struct BmpWin *win, int16_t x, int16_t y, struct Strbuf *str,
    struct GfFont *font, int32_t wait, struct Tcbl *tcbl, uint32_t priority,
    uint32_t heap, uint16_t fillColor, PrintCallback callback)
{
    g_last_window = win;
    g_print_count++;
    return __real_PRINTSYS_PrintStreamCallBack(win, x, y, str, font, instant(wait),
                                               tcbl, priority, heap, fillColor,
                                               callback);
}

void __wrap_PRINTSYS_PrintStreamSetSpeed(struct TextPrinter *printer, int32_t wait)
{
    __real_PRINTSYS_PrintStreamSetSpeed(printer, instant(wait));
}
