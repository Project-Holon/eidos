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
 *   - The text appears in a turn of its own, in the game's font: in the
 *     field's plain window when the field is quiet, or as the battle box's
 *     next message during a battle (sdk/mod_text.h, "A text's own turn").
 *   - It is queued as written and encoded when its turn comes, with the
 *     font of the window that shows it.
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
#include "mod_event.h"

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

/*
 * One queued text, as the mod wrote it (UTF-8, cut at the cap's number of
 * characters).  It becomes the game's code units when it is taken: the
 * answer to "does the font have this character" is the font of the window
 * that shows it, and that window is only known then.
 */
#define BW_MOD_MESSAGE_TEXT_BYTES (BW_MOD_MESSAGE_MAX_UNITS * 4u + 1u)

typedef struct BwModMessageSlot {
    char text[BW_MOD_MESSAGE_TEXT_BYTES];
    u8   kind;
    u32  ticket;        /* a question with a ticket, 0 for none */
} BwModMessageSlot;

/*
 * A question with a ticket, from the ask to the answer's delivery: the
 * callback that asked, the asker's tag and the mod whose namespace was in
 * force.  `state` says how far it is.
 */
#define BW_MOD_MESSAGE_ASK_MAX 16u

enum {
    ASK_FREE = 0,
    ASK_WAITING,        /* in the queue, or on the screen */
    ASK_ANSWERED        /* answered: the next frame tick delivers it */
};

typedef struct BwModAsk {
    u8            state;
    u8            yes;
    u32           ticket;
    u32           tag;
    u32           order;    /* when it was answered: delivery in that order */
    BwModAnswerFn fn;
    void         *user;
    char          ns[64];
} BwModAsk;

static BwModAsk g_asks[BW_MOD_MESSAGE_ASK_MAX];
static u32      g_ticket;           /* the last ticket given */
static u32      g_answer_order;     /* the answers' running count */
static const char *g_answer_owner = "";

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

static u32 utf8_next(const char **s);

/* Put a text at the end of the queue.  Returns 0, or -1 when the text is
 * empty or the queue is full. */
static int queue_push_ticket(u32 kind, const char *text, u32 ticket);

static int queue_push(u32 kind, const char *text)
{
    return queue_push_ticket(kind, text, 0u);
}

static int queue_push_ticket(u32 kind, const char *text, u32 ticket)
{
    BwModMessageSlot *slot;
    const char *end = text;
    u32 n = 0u;
    size_t bytes;

    if (text == NULL || *text == '\0') {
        return -1;
    }
    if (g_queue_count >= BW_MOD_MESSAGE_QUEUE_MAX) {
        return -1;
    }
    /* The cap counts characters: the cut falls between two of them. */
    while (*end != '\0' && n < BW_MOD_MESSAGE_MAX_UNITS) {
        (void)utf8_next(&end);
        n++;
    }
    bytes = (size_t)(end - text);
    slot = &g_queue[(g_queue_head + g_queue_count) % BW_MOD_MESSAGE_QUEUE_MAX];
    memcpy(slot->text, text, bytes);
    slot->text[bytes] = '\0';
    slot->kind = (u8)kind;
    slot->ticket = ticket;
    g_queue_count++;
    return 0;
}

static u32 encode_with(const char *utf8, u16 *out, u32 max_units, int (*has)(u32 codepoint));

int bw_mod_message_queue_take(BwModMessage *out)
{
    return bw_mod_message_queue_take_with(out, NULL);
}

int bw_mod_message_queue_take_with(BwModMessage *out, int (*has)(u32 codepoint))
{
    const BwModMessageSlot *slot;

    if (out == NULL || g_queue_count == 0u) {
        return -1;
    }
    slot = &g_queue[g_queue_head];
    out->count = encode_with(slot->text, out->units, BW_MOD_MESSAGE_MAX_UNITS, has);
    out->kind = slot->kind;
    out->ticket = slot->ticket;
    g_queue_head = (g_queue_head + 1u) % BW_MOD_MESSAGE_QUEUE_MAX;
    g_queue_count--;
    return 0;
}

u32 bw_mod_message_queue_count(void)
{
    return g_queue_count;
}

int bw_mod_message_queue_peek_kind(void)
{
    if (g_queue_count == 0u) {
        return -1;
    }
    return (int)g_queue[g_queue_head].kind;
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

/* The one code unit a character is written as, `has` answering whether
 * the font has it (NULL: the service's own answer). */
static u16 encode_char(u32 codepoint, int (*has)(u32 codepoint))
{
    if (codepoint == (u32)'\n') {
        return (u16)BW_MOD_MESSAGE_NEWLINE;
    }
    if (codepoint == (u32)'\t') {
        return (u16)' ';
    }
    if (codepoint <= 0xFFFFu &&
        (has != NULL ? has(codepoint) : bw_mod_message_font_has(codepoint)) != 0) {
        return (u16)codepoint;
    }
    return (u16)BW_MOD_MESSAGE_UNKNOWN;
}

static u32 encode_with(const char *utf8, u16 *out, u32 max_units, int (*has)(u32 codepoint))
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
        out[n] = encode_char(codepoint, has);
        n++;
    }
    return n;
}

u32 bw_mod_message_encode(const char *utf8, u16 *out, u32 max_units)
{
    return encode_with(utf8, out, max_units, NULL);
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
        if (msg.ticket != 0u) {
            bw_mod_message_answered(msg.ticket, answer);
        } else {
            bw_mod_message_answer(answer);
        }
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
    return queue_push((u32)BW_MOD_MESSAGE_TEXT, text);
}

int bw_mod_message_queue_confirm(const char *text)
{
    return queue_push((u32)BW_MOD_MESSAGE_CONFIRM, text);
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
    if (queue_push((u32)BW_MOD_MESSAGE_CONFIRM, text) != 0) {
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
/* The question with a ticket                                          */
/* ------------------------------------------------------------------ */

/* The event layer, when it is linked (the message tests link without it:
 * there a mod's answer reaches no handler). */
#pragma weak bw_mod_event_fire
extern const char *bw_mod_savedata_namespace(void);
#pragma weak bw_mod_savedata_namespace

int bw_mod_message_ask(const char *text, u32 tag, BwModAnswerFn fn, void *user,
                       u32 *ticket)
{
    const char *ns = bw_mod_savedata_namespace != NULL ? bw_mod_savedata_namespace() : "";

    return bw_mod_message_ask_as(ns, text, tag, fn, user, ticket);
}

int bw_mod_message_ask_as(const char *ns, const char *text, u32 tag, BwModAnswerFn fn,
                          void *user, u32 *ticket)
{
    BwModAsk *a = NULL;
    u32 i;

    if (text == NULL || *text == '\0') {
        return -1;
    }
    for (i = 0u; i < BW_MOD_MESSAGE_ASK_MAX; i++) {
        if (g_asks[i].state == ASK_FREE) {
            a = &g_asks[i];
            break;
        }
    }
    if (a == NULL) {
        return -1;
    }
    if (++g_ticket == 0u) {
        g_ticket = 1u;
    }
    if (queue_push_ticket((u32)BW_MOD_MESSAGE_CONFIRM, text, g_ticket) != 0) {
        return -1;
    }
    memset(a, 0, sizeof(*a));
    a->state = ASK_WAITING;
    a->ticket = g_ticket;
    a->tag = tag;
    a->fn = fn;
    a->user = user;
    snprintf(a->ns, sizeof(a->ns), "%s", ns != NULL ? ns : "");
    if (getenv("EIDOS_MOD_MESSAGE_LOG") != NULL) {
        fprintf(stderr, "eidos: mod question %u (tag %u): %s\n",
                (unsigned int)a->ticket, (unsigned int)tag, text);
    }
    if (ticket != NULL) {
        *ticket = a->ticket;
    }
    return 0;
}

void bw_mod_message_answered(u32 ticket, int yes)
{
    u32 i;

    for (i = 0u; i < BW_MOD_MESSAGE_ASK_MAX; i++) {
        BwModAsk *a = &g_asks[i];

        if (a->state == ASK_WAITING && a->ticket == ticket) {
            a->state = ASK_ANSWERED;
            a->yes = yes != 0 ? 1u : 0u;
            a->order = ++g_answer_order;
            return;
        }
    }
}

void bw_mod_message_deliver(void)
{
    for (;;) {
        BwModAsk *next = NULL;
        BwModAsk done;
        u32 i;

        for (i = 0u; i < BW_MOD_MESSAGE_ASK_MAX; i++) {
            if (g_asks[i].state == ASK_ANSWERED &&
                (next == NULL || g_asks[i].order < next->order)) {
                next = &g_asks[i];
            }
        }
        if (next == NULL) {
            return;
        }
        /* The record is free before the callback runs: a callback may ask
         * again. */
        done = *next;
        memset(next, 0, sizeof(*next));
        if (getenv("EIDOS_MOD_MESSAGE_LOG") != NULL) {
            fprintf(stderr, "eidos: mod question %u answered %s\n",
                    (unsigned int)done.ticket, done.yes ? "yes" : "no");
        }
        g_answer_owner = done.ns;
        if (done.fn != NULL) {
            done.fn(done.user, done.ticket, done.tag, done.yes);
        }
        g_answer_owner = "";
    }
}

const char *bw_mod_message_answer_owner(void)
{
    return g_answer_owner;
}

/* A mod's answer: the event confirm.done, for every handler. */
static void mod_answer(void *user, u32 ticket, u32 tag, int yes)
{
    BwEventPayload p;

    (void)user;
    if (bw_mod_event_fire == NULL) {
        return;
    }
    memset(&p, 0, sizeof(p));
    p.confirm_done.ticket = ticket;
    p.confirm_done.tag = tag;
    p.confirm_done.answer = yes != 0 ? 1u : 0u;
    (void)bw_mod_event_fire(BW_EVENT_CONFIRM_DONE, &p);
}

int bw_mod_message_confirm_ask(const char *text, u32 tag, u32 *ticket)
{
    return bw_mod_message_ask(text, tag, mod_answer, NULL, ticket);
}

int bw_mod_message_confirm_ask_as(const char *ns, const char *text, u32 tag, u32 *ticket)
{
    return bw_mod_message_ask_as(ns, text, tag, mod_answer, NULL, ticket);
}

/* ------------------------------------------------------------------ */
/* The reset                                                           */
/* ------------------------------------------------------------------ */

void bw_mod_message_reset(void)
{
    bw_mod_message_queue_clear();
    g_answer = 0;
    g_answer_ready = 0;
    /* The questions that wait and the answers not yet given go with the
     * mods that asked them. */
    memset(g_asks, 0, sizeof(g_asks));
    g_answer_owner = "";
}
