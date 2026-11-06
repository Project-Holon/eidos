/*
 * sdk/mod_message.c -- the message service: the queue and the text handling
 * (docs/mod-services.md, "Messages"; docs/mod-implementation.md, section 6,
 * step 8).
 *
 * What this file owns
 * -------------------
 *
 * The queue of texts a mod wants shown, and the encoding of those texts
 * into the game's own code units.  The queue is the service's own: a mod
 * queues a text from a handler, the handler returns, and the port shows
 * the text at the next point where the game would accept a message, so a
 * handler never draws over the game.
 *
 * The rules the service keeps (docs/mod-services.md):
 *
 *   - The text appears in the field's talk window, or in the battle message
 *     box during a battle, in the game's font.
 *   - It is queued: the port shows it at the next point where the game
 *     would accept a message (after the current script command, or at the
 *     next battle message).
 *   - A confirm is only allowed in the field; in a battle it returns 0 at
 *     once.
 *   - The text is UTF-8; a character the game font lacks shows as '?'.
 *   - A message from a refusal handler appears after the action is refused.
 *
 * The encoding
 * ------------
 *
 * The game's text is a run of 16-bit code units (src/game/arm9/msgdata.c):
 * one unit is one character, 0xFFFF ends a string and 0xFFFE is a line
 * break.  The printable ASCII range is the same number as the byte, which
 * is what the port's own screens already do (engine/platform/mod_options_proc.c
 * and stubs/ov/reimpl_ov088_func_021BAD08.c build their string objects out
 * of plain ASCII code units and the 0xFFFF end code).
 *
 * The font's own table says which characters it has: the character map is
 * a table inside the font file (arc 0x17, member 0) and the game's lookup
 * is GF_FONT_GetGlyphIndex (src/game/arm9/gf_font.c).  The port has no
 * reader for that file yet, so the built-in answer is the English game
 * font's: the printable ASCII range and nothing else.  A character outside
 * it shows as '?', as the document asks.  A port that has read the font
 * installs the game's own answer with bw_mod_message_set_font_has().
 *
 * The show step
 * -------------
 *
 * Showing the text is the port's hook into the game's text path, which is
 * a separate piece of work.  This file owns the queue the hook drains and
 * the call the hook makes: bw_mod_message_pump().  The port installs a sink
 * (bw_mod_message_set_sink) whose three calls are the game's own three
 * jobs with a text: show it, ask it, and advance the game to the next
 * message point.
 *
 * A confirm waits for the answer: the question goes into the queue with
 * the other texts, the wait advances the game to the next message point,
 * the port's text path takes the question, asks it, and reports the answer
 * with bw_mod_message_answer().  Without a sink there is nothing to ask
 * with, and the call returns 0, as it does in a battle.
 */

#include "mod_message.h"
#include "mod_context.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* The limits                                                          */
/* ------------------------------------------------------------------ */

/* How many texts wait at once.  A mod that says more than this before the
 * game reaches a message point loses the extra texts. */
#define BW_MOD_MESSAGE_QUEUE_MAX 8u

/* How many times a confirm waits for the answer before it gives up.  The
 * wait advances the game one message point at a time; a minute of frames
 * is a long time to wait for an answer. */
#define BW_MOD_MESSAGE_CONFIRM_TICKS 3600u

/* ------------------------------------------------------------------ */
/* The state                                                           */
/* ------------------------------------------------------------------ */

/* One queued text, in the game's code units. */
typedef struct BwModMessageSlot {
    u16 units[BW_MOD_MESSAGE_MAX_UNITS];
    u32 count;
    u8  kind;
} BwModMessageSlot;

static BwModMessageSlot g_queue[BW_MOD_MESSAGE_QUEUE_MAX];
static u32             g_queue_head;    /* the next text to take */
static u32             g_queue_count;   /* the texts waiting */

/* The port's own text path. */
static const BwModMessageSink *g_sink;

/* The answer to the question that was asked. */
static int g_answer;
static int g_answer_ready;

/* The font's answer, or the built-in one when the port has none. */
static int (*g_font_has)(u32 codepoint);

/* ------------------------------------------------------------------ */
/* The queue                                                           */
/* ------------------------------------------------------------------ */

/* Put a text at the end of the queue.  Returns 0, or -1 when the queue is
 * full. */
static int queue_push(u32 kind, const u16 *units, u32 count)
{
    BwModMessageSlot *slot;

    if (units == NULL || count == 0u) {
        return -1;
    }
    if (g_queue_count >= BW_MOD_MESSAGE_QUEUE_MAX) {
        return -1;
    }
    if (count > BW_MOD_MESSAGE_MAX_UNITS) {
        count = BW_MOD_MESSAGE_MAX_UNITS;
    }
    slot = &g_queue[(g_queue_head + g_queue_count) % BW_MOD_MESSAGE_QUEUE_MAX];
    memcpy(slot->units, units, (size_t)count * sizeof(u16));
    slot->count = count;
    slot->kind = (u8)kind;
    g_queue_count++;
    return 0;
}

int bw_mod_message_queue_take(BwModMessage *out)
{
    const BwModMessageSlot *slot;

    if (out == NULL || g_queue_count == 0u) {
        return -1;
    }
    slot = &g_queue[g_queue_head];
    out->count = slot->count;
    memcpy(out->units, slot->units, (size_t)slot->count * sizeof(u16));
    out->kind = slot->kind;
    g_queue_head = (g_queue_head + 1u) % BW_MOD_MESSAGE_QUEUE_MAX;
    g_queue_count--;
    return 0;
}

u32 bw_mod_message_queue_count(void)
{
    return g_queue_count;
}

void bw_mod_message_queue_clear(void)
{
    g_queue_head = 0u;
    g_queue_count = 0u;
}

/* ------------------------------------------------------------------ */
/* The encoding                                                        */
/* ------------------------------------------------------------------ */

/*
 * The next code point of a UTF-8 string, advancing *s.  0 at the end, and
 * U+FFFD for a malformed sequence.
 */
static u32 utf8_next(const char **s)
{
    const u8 *p = (const u8 *)*s;
    u32 c = p[0];
    int extra;
    int i;

    if (c == 0u) {
        return 0u;
    }
    if (c < 0x80u) {
        extra = 0;
    } else if ((c & 0xE0u) == 0xC0u) {
        extra = 1;
        c &= 0x1Fu;
    } else if ((c & 0xF0u) == 0xE0u) {
        extra = 2;
        c &= 0x0Fu;
    } else if ((c & 0xF8u) == 0xF0u) {
        extra = 3;
        c &= 0x07u;
    } else {
        *s += 1;                        /* a stray continuation byte */
        return 0xFFFDu;
    }
    for (i = 1; i <= extra; i++) {
        if ((p[i] & 0xC0u) != 0x80u) {
            *s += i;                    /* cut short: one replacement */
            return 0xFFFDu;
        }
        c = (c << 6) | (p[i] & 0x3Fu);
    }
    *s += extra + 1;
    return c;
}

int bw_mod_message_font_has(u32 codepoint)
{
    if (g_font_has != NULL) {
        return g_font_has(codepoint) != 0;
    }
    /*
     * The English game font's own answer: the printable ASCII range.  The
     * rest of the font's table is in the font file, which the port does
     * not read yet; a port that does installs the game's own answer.
     */
    if (codepoint >= 0x20u && codepoint <= 0x7Eu) {
        return 1;
    }
    return 0;
}

void bw_mod_message_set_font_has(int (*has)(u32 codepoint))
{
    g_font_has = has;
}

/* The one code unit a character is written as. */
static u16 encode_char(u32 codepoint)
{
    if (codepoint == (u32)'\n') {
        return (u16)BW_MOD_MESSAGE_NEWLINE;
    }
    if (codepoint == (u32)'\t') {
        return (u16)' ';
    }
    if (codepoint <= 0xFFFFu && bw_mod_message_font_has(codepoint) != 0) {
        return (u16)codepoint;
    }
    return (u16)BW_MOD_MESSAGE_UNKNOWN;
}

u32 bw_mod_message_encode(const char *utf8, u16 *out, u32 max_units)
{
    const char *s = utf8;
    u32 n = 0u;

    if (out == NULL || max_units == 0u || s == NULL) {
        return 0u;
    }
    while (*s != '\0' && n < max_units) {
        u32 codepoint = utf8_next(&s);

        if (codepoint == 0u) {
            break;
        }
        out[n] = encode_char(codepoint);
        n++;
    }
    return n;
}

u32 bw_mod_message_units(const char *utf8)
{
    const char *s = utf8;
    u32 n = 0u;

    if (s == NULL) {
        return 0u;
    }
    while (*s != '\0') {
        u32 codepoint = utf8_next(&s);

        if (codepoint == 0u) {
            break;
        }
        n++;
    }
    return n;
}

/* ------------------------------------------------------------------ */
/* The sink                                                            */
/* ------------------------------------------------------------------ */

void bw_mod_message_set_sink(const BwModMessageSink *sink)
{
    g_sink = sink;
}

/* ------------------------------------------------------------------ */
/* The answer to a question                                            */
/* ------------------------------------------------------------------ */

void bw_mod_message_answer(int yes)
{
    g_answer = yes != 0 ? 1 : 0;
    g_answer_ready = 1;
}

int bw_mod_message_answer_take(void)
{
    if (g_answer_ready == 0) {
        return -1;
    }
    g_answer_ready = 0;
    return g_answer;
}

/* ------------------------------------------------------------------ */
/* The show step                                                       */
/* ------------------------------------------------------------------ */

int bw_mod_message_pump(void)
{
    BwModMessage msg;

    if (g_sink == NULL) {
        return 0;
    }
    if (bw_mod_message_queue_take(&msg) != 0) {
        return 0;
    }
    if (msg.kind == (u8)BW_MOD_MESSAGE_CONFIRM) {
        int answer = 0;

        if (g_sink->ask != NULL) {
            answer = g_sink->ask(msg.units, msg.count);
        }
        bw_mod_message_answer(answer);
    } else if (g_sink->show != NULL) {
        (void)g_sink->show(msg.units, msg.count);
    }
    return 1;
}

/* ------------------------------------------------------------------ */
/* The two calls the services table carries                            */
/* ------------------------------------------------------------------ */

int bw_mod_message_queue_text(const char *text)
{
    u16 units[BW_MOD_MESSAGE_MAX_UNITS];
    u32 count;

    count = bw_mod_message_encode(text, units, BW_MOD_MESSAGE_MAX_UNITS);
    if (count == 0u) {
        return -1;
    }
    return queue_push((u32)BW_MOD_MESSAGE_TEXT, units, count);
}

int bw_mod_message_queue_confirm(const char *text)
{
    u16 units[BW_MOD_MESSAGE_MAX_UNITS];
    u32 count;

    count = bw_mod_message_encode(text, units, BW_MOD_MESSAGE_MAX_UNITS);
    if (count == 0u) {
        return -1;
    }
    return queue_push((u32)BW_MOD_MESSAGE_CONFIRM, units, count);
}

void bw_mod_message_show(const char *text)
{
    /* EIDOS_MOD_MESSAGE_LOG: each message in the log too, for the tests */
    if (text != NULL && getenv("EIDOS_MOD_MESSAGE_LOG") != NULL) {
        fprintf(stderr, "eidos: mod message: %s\n", text);
    }
    (void)bw_mod_message_queue_text(text);
}

int bw_mod_message_confirm(const char *text)
{
    u16 units[BW_MOD_MESSAGE_MAX_UNITS];
    u32 count;
    u32 i;

    /*
     * A question is only allowed in the field: in a battle it returns 0 at
     * once, and nothing is queued (docs/mod-services.md).
     */
    if (bw_mod_battle_context.active != 0u) {
        return 0;
    }
    if (g_sink == NULL || g_sink->ask == NULL) {
        return 0;
    }
    count = bw_mod_message_encode(text, units, BW_MOD_MESSAGE_MAX_UNITS);
    if (count == 0u) {
        return 0;
    }
    /*
     * An answer that came late (a question whose waiter gave up before it
     * was asked) is not this question's answer.
     */
    (void)bw_mod_message_answer_take();
    if (g_sink->tick == NULL) {
        /*
         * The game cannot be advanced from here, so the question is asked
         * at once: the field's talk window is free, and the port's text
         * path waits for the answer itself.  The texts that are already
         * queued are shown first, so the order the mod wrote them in
         * holds.
         */
        for (i = 0u; i < BW_MOD_MESSAGE_QUEUE_MAX && g_queue_count > 0u; i++) {
            (void)bw_mod_message_pump();
        }
        return g_sink->ask(units, count) != 0 ? 1 : 0;
    }
    /*
     * The question goes into the queue with the other texts and is asked
     * at the next point where the game would accept a message.  The wait
     * advances the game to that point; the port's text path takes the
     * question, asks it, and reports the answer.
     */
    if (queue_push((u32)BW_MOD_MESSAGE_CONFIRM, units, count) != 0) {
        return 0;
    }
    for (i = 0u; i < BW_MOD_MESSAGE_CONFIRM_TICKS; i++) {
        int answer = bw_mod_message_answer_take();

        if (answer >= 0) {
            return answer;
        }
        g_sink->tick();
    }
    /* The question stays in the queue; it is asked at the next message
     * point, and its answer goes to nobody. */
    return 0;
}

/* ------------------------------------------------------------------ */
/* The reset                                                           */
/* ------------------------------------------------------------------ */

void bw_mod_message_reset(void)
{
    bw_mod_message_queue_clear();
    g_answer = 0;
    g_answer_ready = 0;
}
