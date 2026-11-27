/*
 * sdk/mod_message.h -- the message service: the text a mod puts on screen
 * (docs/mod-services.md, "Messages"; docs/mod-implementation.md, section 6,
 * step 8).
 *
 * What this is
 * ------------
 *
 * The first of the port's services (docs/mod-services.md).  A mod calls
 * `message` to say something to the player and `confirm` to ask a question;
 * the port shows the text in the game's own windows, in the game's font, at
 * a point where the game would accept a message, so a handler never draws
 * over the game.
 *
 * The rules the service keeps (docs/mod-services.md):
 *
 *   - The text appears in a turn of its own: the field's plain window when
 *     the field is quiet, or the battle box's next message in a battle
 *     (sdk/mod_text.h, "A text's own turn").
 *   - It is queued as written and encoded when its turn comes.
 *   - A question is asked with a ticket and answered later
 *     (bw_mod_message_ask, "The question with a ticket"), in the field and
 *     in a battle.  The old `confirm` call waits for its answer, which a
 *     handler inside the game's frame cannot do: in the game it answers
 *     no at once.
 *   - The text is UTF-8; a character the game font lacks shows as '?'.
 *   - A message from a refusal handler appears after the action is refused:
 *     the handler queues the text while the action is refused, and the
 *     queue is drained at the next message point, which is after it.
 *
 * The text's encoding
 * -------------------
 *
 * The game's text is a run of 16-bit code units, not a run of bytes.  The
 * message bank's strings are code units (src/game/arm9/msgdata.c), and the
 * game prints them with its own font: one unit is one character, 0xFFFF
 * ends a string and 0xFFFE is a line break.  The printable ASCII range is
 * the same number as the byte; a character the font lacks shows as '?'.
 *
 * bw_mod_message_encode() turns a UTF-8 string into those units.  It does
 * not write the end code: the port's text path appends it when it builds
 * the game's string object.
 *
 * The show step
 * -------------
 *
 * The queue is the service's own.  The *showing* is the port's hook into
 * the game's text path, which is a separate piece of work: the port
 * installs a sink (bw_mod_message_set_sink) and calls
 * bw_mod_message_pump() where the game would accept a message.  The sink
 * does the three things the game does with a text: show it, ask it, and
 * advance the game to the next message point.
 *
 * This header is the port's own; a mod never sees it.  What a mod sees is
 * the services table (sdk/mod_state.h), whose two message calls the loader
 * binds to bw_mod_message_show() and bw_mod_message_confirm().
 */

#ifndef BW_PORT_MOD_MESSAGE_H
#define BW_PORT_MOD_MESSAGE_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* The game's text                                                     */
/* ------------------------------------------------------------------ */

/* The game's own codes: the end of a string and the line break. */
#define BW_MOD_MESSAGE_EOS        0xFFFFu
#define BW_MOD_MESSAGE_NEWLINE    0xFFFEu

/* The character a code unit the font lacks shows as. */
#define BW_MOD_MESSAGE_UNKNOWN    0x003Fu   /* '?' */

/*
 * How many characters one text may hold: about four lines of the field's
 * plain window.  The port lays a text out for its window (sdk/mod_text.h),
 * so a long text scrolls instead of running over the edge.  A text longer
 * than this is cut at the cap.
 */
#define BW_MOD_MESSAGE_MAX_UNITS  128u

/* What a queued text is. */
enum {
    BW_MOD_MESSAGE_TEXT    = 0,   /* a message: A closes it */
    BW_MOD_MESSAGE_CONFIRM = 1    /* a question: yes or no */
};

/*
 * One queued text, in the game's code units.  The answer of a question does
 * not travel in the entry: the text path asks the question and reports the
 * answer with bw_mod_message_answer().
 */
typedef struct BwModMessage {
    u32 count;                            /* the code units */
    u16 units[BW_MOD_MESSAGE_MAX_UNITS];  /* the text itself */
    u8  kind;                             /* BW_MOD_MESSAGE_TEXT or _CONFIRM */
    u8  pad[3];
    u32 ticket;                           /* a question's ticket (bw_mod_message_ask), 0 for none */
} BwModMessage;

/* ------------------------------------------------------------------ */
/* The port's own text path                                            */
/* ------------------------------------------------------------------ */

/*
 * The sink is the port's hook into the game's text path.  It is installed
 * by the port, not by a mod, and it is the only thing that touches the
 * game's windows.
 *
 *   show  shows the text in the game's own window: the field's talk
 *         window, or the battle message box in a battle.  The units are
 *         the game's code units without the end code.
 *   ask   asks the question and returns the player's answer: 1 for yes,
 *         0 for no.
 *   tick  advances the game to the next point where the game would accept
 *         a message.  The port's text path calls bw_mod_message_pump()
 *         there, which shows the next queued text.  It is what a confirm
 *         waits for; without it a confirm cannot be asked and returns 0.
 */
typedef struct BwModMessageSink {
    int  (*show)(const u16 *units, u32 count);
    int  (*ask)(const u16 *units, u32 count);
    void (*tick)(void);
} BwModMessageSink;

/* The port's own text path.  NULL takes the sink away. */
void bw_mod_message_set_sink(const BwModMessageSink *sink);

/* ------------------------------------------------------------------ */
/* The two calls the services table carries                            */
/* ------------------------------------------------------------------ */

/*
 * The `message` call of the services table (sdk/mod_state.h): queue a text
 * for the player.  The port shows it at the next point where the game
 * would accept a message.
 */
void bw_mod_message_show(const char *text);

/*
 * The `confirm` call of the services table: ask a question and return the
 * player's answer (1 for yes, 0 for no).
 *
 * A question is only allowed in the field: in a battle it returns 0 at
 * once, and nothing is queued.  In the field the question goes into the
 * queue with the other texts and is asked at the next point where the game
 * would accept a message; the call waits for the answer.
 */
int  bw_mod_message_confirm(const char *text);

/* ------------------------------------------------------------------ */
/* The question with a ticket                                          */
/* ------------------------------------------------------------------ */

/*
 * A handler runs inside the game's frame, so it cannot wait for the
 * player.  A question with a ticket does not wait: it goes into the queue
 * with the texts and keeps its place among them, it is asked in a turn of
 * its own with the game's own yes/no menu (the field's, or the battle's
 * choice screen), and the answer comes back later, on the frame, to the
 * callback that asked.  The text path only records the answer
 * (bw_mod_message_answered); the callbacks run from the frame tick
 * (bw_mod_message_deliver), once the window is closed.
 *
 * Every ticket gets one answer, except at a reset: the reset drops the
 * questions that wait and the answers not yet given, with the mods that
 * asked them.
 */

/* The answer to a question: 1 for yes, 0 for no. */
typedef void (*BwModAnswerFn)(void *user, u32 ticket, u32 tag, int yes);

/*
 * Ask a question.  `tag` is the asker's own number for it, handed back with
 * the answer.  0 with *ticket set (when `ticket` is not NULL), or -1 when the
 * text is empty or the queue is full (nothing is asked then).
 */
int  bw_mod_message_ask(const char *text, u32 tag, BwModAnswerFn fn, void *user,
                        u32 *ticket);

/*
 * The services table's `confirm_ask` (version 7): a mod's question.  The
 * answer is the event confirm.done (sdk/mod_api.h, BwConfirmDone).
 */
int  bw_mod_message_confirm_ask(const char *text, u32 tag, u32 *ticket);

/* The same two for an asker named by its namespace (a rule's mod), not the
 * namespace in force. */
int  bw_mod_message_ask_as(const char *ns, const char *text, u32 tag, BwModAnswerFn fn,
                           void *user, u32 *ticket);
int  bw_mod_message_confirm_ask_as(const char *ns, const char *text, u32 tag, u32 *ticket);

/* The text path's answer to the question with this ticket (1 yes, 0 no). */
void bw_mod_message_answered(u32 ticket, int yes);

/* The frame tick: the answers recorded since the last tick go to the
 * callbacks that asked, in the order they came. */
void bw_mod_message_deliver(void);

/* While confirm.done is fired: the namespace of the mod that asked (a rule
 * sees only its own mod's answers); "" otherwise. */
const char *bw_mod_message_answer_owner(void);

/* The kind of the next queued text (BW_MOD_MESSAGE_TEXT or _CONFIRM), or -1
 * when the queue is empty.  A text path that cannot ask a question now
 * leaves it, and the texts behind it, in the queue. */
int  bw_mod_message_queue_peek_kind(void);

/* ------------------------------------------------------------------ */
/* The queue                                                           */
/* ------------------------------------------------------------------ */

/* Queue a text.  Returns 0, or -1 when the queue is full or the text is
 * empty. */
int  bw_mod_message_queue_text(const char *text);

/* Queue a question.  Returns 0, or -1 when the queue is full or the text
 * is empty.  The text path asks it at the next message point. */
int  bw_mod_message_queue_confirm(const char *text);

/* How many texts are waiting. */
u32  bw_mod_message_queue_count(void);

/* Take the next text.  Returns 0, or -1 when the queue is empty. */
int  bw_mod_message_queue_take(BwModMessage *out);

/* The same, the text encoded with `has` as the answer to "does the font
 * have this character" (the font of the window that shows it); NULL is the
 * service's own answer. */
int  bw_mod_message_queue_take_with(BwModMessage *out, int (*has)(u32 codepoint));

/* Drop every queued text: at power-on and at every reset. */
void bw_mod_message_queue_clear(void);

/* ------------------------------------------------------------------ */
/* The show step                                                       */
/* ------------------------------------------------------------------ */

/*
 * The show step: the port calls this where the game would accept a
 * message.  It takes the next queued text and shows it through the sink;
 * a question is asked instead of shown, and its answer is recorded.
 * Returns 1 when a text was shown, 0 when the queue was empty or the port
 * has no text path.
 */
int  bw_mod_message_pump(void);

/* ------------------------------------------------------------------ */
/* The answer to a question                                            */
/* ------------------------------------------------------------------ */

/* The text path reports the answer to a question it asked. */
void bw_mod_message_answer(int yes);

/* The answer, once it has come: 1 for yes, 0 for no, and -1 when no
 * answer has come yet.  Taking it clears it. */
int  bw_mod_message_answer_take(void);

/* ------------------------------------------------------------------ */
/* The encoding                                                        */
/* ------------------------------------------------------------------ */

/*
 * The text in the game's code units.  Writes at most `max_units` units and
 * returns how many it wrote; the end code is not written.
 */
u32  bw_mod_message_encode(const char *utf8, u16 *out, u32 max_units);

/* How many code units the text needs, without the cap of the encode. */
u32  bw_mod_message_units(const char *utf8);

/* Whether the game font has the character: 1 when it has, 0 when it does
 * not (the text shows it as '?'). */
int  bw_mod_message_font_has(u32 codepoint);

/*
 * The port's own answer to the question above.  The font's character map
 * is a table in the font file (arc 0x17) and the game's lookup is
 * GF_FONT_GetGlyphIndex (src/game/arm9/gf_font.c); a port that has read
 * the font installs its answer here.  NULL puts the built-in one back.
 */
void bw_mod_message_set_font_has(int (*has)(u32 codepoint));

/* ------------------------------------------------------------------ */
/* The reset                                                           */
/* ------------------------------------------------------------------ */

/* Drop the queued texts and the answer: at power-on and at every reset
 * (bw_port_reset). */
void bw_mod_message_reset(void);

#ifdef __cplusplus
}
#endif
#endif /* BW_PORT_MOD_MESSAGE_H */
