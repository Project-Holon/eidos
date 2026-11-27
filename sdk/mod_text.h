/*
 * sdk/mod_text.h -- the port's own text path: the side of the message
 * service that puts the text on the screen (docs/mod-services.md,
 * "Messages"; sdk/mod_message.h, "The port's own text path").
 *
 * What this is
 * ------------
 *
 * The message service (sdk/mod_message.c) owns the queue: a mod queues a
 * text, and the port shows it at the next point where the game would
 * accept a message.  The service does not know how a text is shown; it
 * asks through its sink (sdk/mod_message.h).  This file is the port's
 * side of that sink: the text path that takes a run of the game's code
 * units and shows it in the game's own window, in the game's font.
 *
 * The shape is the one the port already uses twice, in
 * engine/platform/mod_options_proc.c and in
 * stubs/ov/reimpl_ov088_func_021BAD08.c: the game's string object is
 * built out of the code units and the end code (0xFFFF), and it is
 * printed with the game's own print queue and the game's own font.
 *
 *   STRBUF_Create(capacity, heap)        the game's string object
 *   STRBUF_SetStringN(str, units, n)     the code units and the end code
 *   PRINTSYS_PrintQueColor(que, bmp, x, y, str, font, colour)
 *
 * The font is the game's own: GF_FONT_Create(0x17, 0, 0, 0, heap), the
 * font at arc 0x17 (a/0/2/3), member 0, the one the field's message
 * window uses (src/game/ov021/field_msgbg.c).  The font's own glyph
 * table answers what the message service asks about a character the font
 * lacks (sdk/mod_message.h, "The encoding"): GF_FONT_GetGlyphIndex
 * gives a character's glyph, and the glyph of '?' is the one a character
 * the font lacks is shown as.
 *
 * Who holds the window
 * --------------------
 *
 * The game's own windows take a mod's text in a turn of its own ("A
 * text's own turn" below).  What follows is the older seam for a port
 * screen that has a window open (bw_mod_text_pump); no game hook uses it.
 *
 * The field's message window and the field's yes/no window are overlay
 * 21's (src/game/ov021/field_msgbg.c), and the battle's message box is
 * overlay 93's (src/game/ov093/btlv_core.c).  The ARM9 cannot call into
 * an overlay, so the side that has the window open is the side that
 * hands it over: the hook, a port source that stands in for a matched
 * overlay file (stubs/ov/reimpl_ovNNN_*.c, docs/hooks.md) or the
 * platform layer.  It opens the game's own window, hands the port its
 * three parts, and lets the port print into it.
 *
 * The three jobs the port's text path has with a text are the game's
 * own three:
 *
 *   show  shows the text in the game's own window: the field's talk
 *         window, or the battle's message box in a battle.  The units
 *         are the game's code units without the end code; the end code
 *         is added here, when the string object is built.
 *   ask   asks the question in the game's own yes/no window and returns
 *         the player's answer: 1 for yes, 0 for no.  It is the seam of the
 *         old waiting `confirm`; the game registers none, since a handler
 *         cannot wait.  A question with a ticket is asked in its own turn
 *         instead ("A text's own turn" below).
 *   tick  advances the game to the next point where the game would
 *         accept a message.  A confirm waits for that point; without it
 *         a confirm cannot be asked and answers no.
 *
 * This header is the port's own; a mod never sees it.  What a mod sees
 * is the services table (sdk/mod_state.h), whose two message calls the
 * loader binds to bw_mod_message_show() and bw_mod_message_confirm().
 */

#ifndef BW_PORT_MOD_TEXT_H
#define BW_PORT_MOD_TEXT_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* The window                                                           */
/* ------------------------------------------------------------------ */

/*
 * The game's own message window, as the port's text path sees it.
 *
 * The window belongs to the overlay that opened it, so the parts are
 * named here and the types are the port's own (the overlay that hands
 * them over knows what they are):
 *
 *   print_que  the game's print queue: the one the game itself runs,
 *              so the text appears with the game's own speed and the
 *              game's own "the text is still being written" wait.
 *   bmp_win    the game's bitmap window: the window to draw in.
 *   font       the game's font (NULL: the port makes its own, the one
 *              at arc 0x17, member 0).
 *   heap       the heap the game's objects come from.
 *   x, y       where in the window the text starts, in pixels.
 */
typedef struct BwModTextWindow {
    void *print_que;
    void *bmp_win;
    void *font;
    u32   heap;
    u16   x;
    u16   y;
} BwModTextWindow;

/* ------------------------------------------------------------------ */
/* The draw side                                                        */
/* ------------------------------------------------------------------ */

/*
 * The two jobs the port cannot do for itself: the yes/no window is the
 * field's (overlay 21's), and advancing the game is the platform
 * layer's (the game's frame).  Whoever holds them registers them here;
 * with neither of them a text can still be shown, but a confirm cannot
 * be asked and answers no.
 */
typedef struct BwModTextDraw {
    /* The field's yes/no window: it shows the question and waits for the
     * player's answer.  1 for yes, 0 for no. */
    int  (*ask)(const u16 *units, u32 count);
    /* One step of the game: the wait for a confirm's answer needs the
     * game to reach the next point where it would accept a message. */
    void (*tick)(void);
} BwModTextDraw;

/* The draw side.  NULL takes it away. */
void bw_mod_text_set_draw(const BwModTextDraw *draw);

/* ------------------------------------------------------------------ */
/* The window the hook has open                                         */
/* ------------------------------------------------------------------ */

/*
 * The window the text goes into.  The hook sets it before it pumps the
 * queue: the window is the one the game has open at that point.
 */
void bw_mod_text_set_window(const BwModTextWindow *win);

/* The window that is set, or NULL. */
const BwModTextWindow *bw_mod_text_window(void);

/* ------------------------------------------------------------------ */
/* The port's own text path                                            */
/* ------------------------------------------------------------------ */

/*
 * Show a run of the game's code units in the game's own window, in the
 * game's font: the game's string object is built out of the units and
 * the end code, and it is printed with the game's own print queue.
 *
 * Returns 0 when the text was printed, and -1 when there is no window to
 * print in or the game's print system is not there.
 */
int bw_mod_text_print(const BwModTextWindow *win, const u16 *units, u32 count);

/* The game's font, made on first use: the font at arc 0x17, member 0.
 * NULL when the game is not there. */
void *bw_mod_text_font(u32 heap);

/*
 * The game's own answer to "does the font have this character": the
 * font's own glyph table (GF_FONT_GetGlyphIndex).  A character the font
 * lacks is shown as '?', and the service asks this before it writes a
 * character out (sdk/mod_message.h, "The encoding").
 */
int bw_mod_text_font_has(u32 codepoint);

/* ------------------------------------------------------------------ */
/* The show step                                                       */
/* ------------------------------------------------------------------ */

/*
 * Install the message service's sink: the port's own text path is what
 * the service asks to show a text, to ask a question and to advance the
 * game (sdk/mod_message.h, "The sink").  The port calls this at
 * start-up, beside the loader's own set-up.
 */
void bw_mod_text_install(void);

/*
 * The show step: the port calls this where the game would accept a
 * message.  It takes the next queued text and shows it in the window
 * that is set; a question is asked instead of shown, and its answer is
 * recorded.  Returns 1 when a text was shown, 0 when the queue was
 * empty or there is no window to show it in.
 */
int bw_mod_text_pump(void);

/* ------------------------------------------------------------------ */
/* A text's own turn                                                   */
/* ------------------------------------------------------------------ */

/*
 * A mod's text is shown in a turn of its own, never in a window the game
 * has open for its own text:
 *
 *   field   when the field is quiet and the player stands still, the
 *           port starts an event of its own (bw_mod_text_frame): the
 *           field's plain window opens, the text prints at the game's
 *           speed, A or B closes it, and the next text follows.  A
 *           question opens the field's yes/no menu after its text.
 *   battle  the next battle message waits: the mod's text is the battle
 *           box's own message first, then the game's message
 *           (stubs/ov/patch_ov093_btlv_scu_message.c).  A question opens
 *           the battle's yes/no choice after its text, when the touch
 *           screen is free.
 *
 * The overlay that has the window asks for the next text here.
 */

/*
 * The next queued text for a window: encoded with the window's own font
 * (a character it lacks is '?') and laid out for a window `width` pixels
 * wide with two lines, the game's own line break and page tag between the
 * lines.  `out` gets the units without the end code; `max_units` should
 * leave room for the tags (BW_MOD_TEXT_LAID_OUT_MAX).  A question in the
 * queue is answered no and passed over.  Returns the units, 0 when no
 * text waits.
 */
#define BW_MOD_TEXT_LAID_OUT_MAX 256u
u32 bw_mod_text_next(void *font, u32 width, u16 *out, u32 max_units);

/*
 * The same, for a window that can ask: a question with a ticket is taken
 * like a text, and *ticket is its ticket (0 for a text).  The window shows
 * the question, opens the game's own yes/no menu, and reports the answer
 * with bw_mod_text_answer.
 */
u32 bw_mod_text_take(void *font, u32 width, u16 *out, u32 max_units, u32 *ticket);

/* Whether the next waiting text is a question: a window that cannot ask
 * now leaves it, and the texts behind it, for later. */
int bw_mod_text_next_is_question(void);

/* The player's answer to the question with this ticket: 1 yes, 0 no.  It
 * is recorded; the asker hears of it on the next frame. */
void bw_mod_text_answer(u32 ticket, int yes);

/* The question on the screen, in UTF-8 (cut to `size`), for an interface
 * that draws the yes/no menu itself: 1 while one is asked, else 0. */
int bw_mod_text_question(char *out, u32 size);

/* The layout itself, on a run of code units (for the tests). */
u32 bw_mod_text_layout(void *font, u32 width, const u16 *units, u32 count,
                       u16 *out, u32 max_units);

/* How many texts wait. */
u32 bw_mod_text_waiting(void);

/*
 * The field's turn: called once a frame (sdk/mod_frame.c).  It starts the
 * field's event for the waiting texts once the field has been quiet and
 * the player still for a few frames.
 */
void bw_mod_text_frame(void);

/*
 * A number that stays the same through one battle and moves on outside
 * battles: the battle's hook keeps it with a message it holds back, and a
 * message held in another battle is let go, not shown.
 */
u32 bw_mod_text_battle_mark(void);

/* The reset: the port's own font goes away with the game's, the window
 * with it, and the draw side with the overlay that owns it (at power-on
 * and at every reset, bw_port_reset). */
void bw_mod_text_reset(void);

#ifdef __cplusplus
}
#endif
#endif /* BW_PORT_MOD_TEXT_H */
