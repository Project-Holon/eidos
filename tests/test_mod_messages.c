/*
 * tests/test_mod_messages.c -- the message service: the text on screen
 * (docs/mod-services.md, "Messages"; sdk/mod_message.h).
 *
 * A mod puts text on the player's screen through the service's two calls
 * (sdk/mod_state.h, the services table): `message` shows one window and
 * `confirm` asks a yes/no question.  The port queues both: the text appears
 * at the next point where the game would accept a message, so a handler
 * never draws over the game.
 *
 * What is checked here:
 *
 *   - the queue's order: the texts come out in the order they were queued,
 *     the queue says how many are waiting, and an empty queue gives
 *     nothing;
 *   - the show step: the port's pump takes the next text and shows it
 *     through the sink, which is the port's hook into the game's text path;
 *   - a confirm's answer: the question is asked and the player's answer
 *     comes back, 1 for yes and 0 for no;
 *   - the encoding: the text is a run of the game's 16-bit code units, and
 *     a character the game font lacks is a '?';
 *   - a confirm in a battle returns 0 at once: the battle's message box
 *     cannot ask a question, so the answer is "no" and the question is
 *     never asked.
 *
 * Nothing here needs game data: the port's own text path is driven through
 * the sink, not the game's windows.
 */

#include <stdio.h>
#include <string.h>

#include "mod_context.h"
#include "mod_message.h"
#include "port_test.h"

/* ------------------------------------------------------------------ */
/* The sink: the port's hook into the game's text path                */
/* ------------------------------------------------------------------ */

/* What the sink was given to show. */
static u16 g_shown[BW_MOD_MESSAGE_MAX_UNITS];
static u32 g_shown_count;
static int g_shown_times;

/* What the sink is asked, and the answer the player gives. */
static int g_asked;
static int g_answer;

/* How many times the port advanced the game. */
static int g_ticks;

/* The answers to the questions with a ticket, as the asker hears them. */
static struct {
    u32 ticket;
    u32 tag;
    int yes;
} g_heard[8];
static int g_heard_count;

static void heard(void *user, u32 ticket, u32 tag, int yes)
{
    (void)user;
    if (g_heard_count < 8) {
        g_heard[g_heard_count].ticket = ticket;
        g_heard[g_heard_count].tag = tag;
        g_heard[g_heard_count].yes = yes;
        g_heard_count++;
    }
}

/* Shows a text: the port's text path gives the sink the game's code
 * units, without the end code. */
static int sink_show(const u16 *units, u32 count)
{
    g_shown_count = count;
    g_shown_times++;
    if (units != NULL && count <= BW_MOD_MESSAGE_MAX_UNITS) {
        memcpy(g_shown, units, (size_t)count * sizeof(u16));
    }
    return 0;
}

/* Asks the question and gives the player's answer. */
static int sink_ask(const u16 *units, u32 count)
{
    (void)units;
    (void)count;
    g_asked++;
    return g_answer;
}

/* Advances the game to the next point where the game would accept a
 * message: that is where the port's text path shows the next text, and
 * that is what a confirm waits for. */
static void sink_tick(void)
{
    g_ticks++;
    (void)bw_mod_message_pump();
}

static const BwModMessageSink g_sink = {
    sink_show,
    sink_ask,
    sink_tick
};

/* ------------------------------------------------------------------ */
/* The helpers                                                        */
/* ------------------------------------------------------------------ */

/* A font that has every character: what a port that has read the font
 * file installs (sdk/mod_message.h, "The encoding"). */
static int has_everything(u32 codepoint)
{
    (void)codepoint;
    return 1;
}

/* Is the message's text the given ASCII string? */
static int units_are(const BwModMessage *msg, const char *text)
{
    size_t n = strlen(text);
    u32 i;

    if (msg->count != (u32)n) {
        return 0;
    }
    for (i = 0; i < msg->count; i++) {
        if (msg->units[i] != (u16)(unsigned char)text[i]) {
            return 0;
        }
    }
    return 1;
}

/* Is the text the sink was given the given ASCII string? */
static int shown_is(const char *text)
{
    size_t n = strlen(text);
    u32 i;

    if (g_shown_count != (u32)n) {
        return 0;
    }
    for (i = 0; i < g_shown_count; i++) {
        if (g_shown[i] != (u16)(unsigned char)text[i]) {
            return 0;
        }
    }
    return 1;
}

/* ------------------------------------------------------------------ */
/* The test                                                            */
/* ------------------------------------------------------------------ */

int main(void)
{
    BwModMessage msg;
    u16 units[BW_MOD_MESSAGE_MAX_UNITS];
    u32 n;

    printf("the queue's order\n");
    bw_mod_message_reset();
    BW_CHECK_EQ("the queue is empty", (long)bw_mod_message_queue_count(), 0);

    BW_REQUIRE("the first text is queued", bw_mod_message_queue_text("first") == 0);
    BW_REQUIRE("the second text is queued", bw_mod_message_queue_text("second") == 0);
    BW_CHECK_EQ("two texts are waiting", (long)bw_mod_message_queue_count(), 2);

    BW_REQUIRE("the next text is taken", bw_mod_message_queue_take(&msg) == 0);
    BW_CHECK_EQ("it is the first one queued", units_are(&msg, "first"), 1);
    BW_CHECK_EQ("it is a message, not a question", (long)msg.kind,
                (long)BW_MOD_MESSAGE_TEXT);
    BW_CHECK_EQ("one text is left", (long)bw_mod_message_queue_count(), 1);

    BW_REQUIRE("the next text is taken", bw_mod_message_queue_take(&msg) == 0);
    BW_CHECK_EQ("it is the second one queued", units_are(&msg, "second"), 1);
    BW_CHECK_EQ("the queue is empty", (long)bw_mod_message_queue_count(), 0);

    BW_CHECK_EQ("an empty queue gives nothing", bw_mod_message_queue_take(&msg), -1);
    BW_CHECK_EQ("and it is still empty", (long)bw_mod_message_queue_count(), 0);

    printf("the services table's message call\n");
    /* `message` queues the text; the port shows it later. */
    bw_mod_message_reset();
    bw_mod_message_show("hello");
    BW_CHECK_EQ("the text is queued", (long)bw_mod_message_queue_count(), 1);
    BW_REQUIRE("it is taken", bw_mod_message_queue_take(&msg) == 0);
    BW_CHECK_EQ("it is the text that was written", units_are(&msg, "hello"), 1);

    printf("the show step\n");
    /* The port's pump shows the next text through the sink: that is the
     * port's hook into the game's text path. */
    bw_mod_message_reset();
    bw_mod_message_set_sink(&g_sink);
    g_shown_times = 0;
    bw_mod_message_show("shown");
    BW_CHECK_EQ("the pump shows the text", bw_mod_message_pump(), 1);
    BW_CHECK_EQ("the sink was given it", shown_is("shown"), 1);
    BW_CHECK_EQ("the sink was called once", g_shown_times, 1);
    BW_CHECK_EQ("the queue is empty", (long)bw_mod_message_queue_count(), 0);
    BW_CHECK_EQ("an empty queue shows nothing", bw_mod_message_pump(), 0);

    printf("a confirm's answer\n");
    bw_mod_message_reset();
    g_asked = 0;
    g_ticks = 0;
    g_answer = 1;
    BW_CHECK_EQ("the player says yes", bw_mod_message_confirm("Ready?"), 1);
    BW_CHECK_EQ("the question was asked", g_asked, 1);
    BW_CHECK_EQ("the game was advanced to the message point", g_ticks > 0, 1);

    bw_mod_message_reset();
    g_asked = 0;
    g_answer = 0;
    BW_CHECK_EQ("the player says no", bw_mod_message_confirm("Ready?"), 0);
    BW_CHECK_EQ("the question was asked", g_asked, 1);

    printf("a confirm with no text path\n");
    /* With no sink there is nothing to ask with, so the call answers at
     * once, the way it does in a battle. */
    bw_mod_message_reset();
    bw_mod_message_set_sink(NULL);
    g_asked = 0;
    g_answer = 1;
    BW_CHECK_EQ("the answer is no", bw_mod_message_confirm("Ready?"), 0);
    BW_CHECK_EQ("nothing was asked", g_asked, 0);

    printf("the encoding\n");
    /* The game's text is a run of 16-bit code units: one unit is one
     * character, and a character the font lacks shows as '?'. */
    bw_mod_message_reset();
    n = bw_mod_message_encode("a\xC3\xA9" "b", units, BW_MOD_MESSAGE_MAX_UNITS);
    BW_CHECK_EQ("the text is three characters", (long)n, 3);
    BW_CHECK_EQ("the letter before it is kept", (long)units[0], (long)'a');
    BW_CHECK_EQ("the character the font lacks is a question mark",
                (long)units[1], (long)BW_MOD_MESSAGE_UNKNOWN);
    BW_CHECK_EQ("the letter after it is kept", (long)units[2], (long)'b');
    BW_CHECK_EQ("the text needs three code units",
                (long)bw_mod_message_units("a\xC3\xA9" "b"), 3);

    BW_CHECK_EQ("the font has the letter", bw_mod_message_font_has('a'), 1);
    BW_CHECK_EQ("the font does not have the accented letter",
                bw_mod_message_font_has(0x00E9u), 0);

    /* The queued text is the encoded one: the game shows what the queue
     * holds. */
    BW_REQUIRE("the text is queued", bw_mod_message_queue_text("a\xC3\xA9" "b") == 0);
    BW_REQUIRE("it is taken", bw_mod_message_queue_take(&msg) == 0);
    BW_CHECK_EQ("it is three code units", (long)msg.count, 3);
    BW_CHECK_EQ("and the question mark is in it", (long)msg.units[1],
                (long)BW_MOD_MESSAGE_UNKNOWN);

    printf("the line break\n");
    n = bw_mod_message_encode("a\nb", units, BW_MOD_MESSAGE_MAX_UNITS);
    BW_CHECK_EQ("the text is three characters", (long)n, 3);
    BW_CHECK_EQ("the line break is the game's own code", (long)units[1],
                (long)BW_MOD_MESSAGE_NEWLINE);

    printf("the font's own answer\n");
    /* A port that has read the font file installs the game's answer; the
     * built-in one is the printable ASCII range. */
    {
        bw_mod_message_set_font_has(has_everything);
        n = bw_mod_message_encode("a\xC3\xA9" "b", units, BW_MOD_MESSAGE_MAX_UNITS);
        BW_CHECK_EQ("the text is three characters", (long)n, 3);
        BW_CHECK_EQ("the accented letter is kept now", (long)units[1], 0x00E9);

        bw_mod_message_set_font_has(NULL);
        n = bw_mod_message_encode("a\xC3\xA9" "b", units, BW_MOD_MESSAGE_MAX_UNITS);
        BW_CHECK_EQ("the built-in answer is back", (long)units[1],
                    (long)BW_MOD_MESSAGE_UNKNOWN);
    }

    printf("a confirm in a battle\n");
    /* A battle's message box cannot ask a question: the confirm answers
     * "no" at once, and the question is never asked. */
    bw_mod_message_reset();
    bw_mod_message_set_sink(&g_sink);
    g_asked = 0;
    g_answer = 1;
    bw_mod_battle_context.active = 1u;
    BW_CHECK_EQ("the answer is no at once", bw_mod_message_confirm("Ready?"), 0);
    BW_CHECK_EQ("the question was never asked", g_asked, 0);
    BW_CHECK_EQ("and nothing was queued", (long)bw_mod_message_queue_count(), 0);

    bw_mod_battle_context.active = 0u;
    BW_CHECK_EQ("in the field it asks again", bw_mod_message_confirm("Ready?"), 1);
    BW_CHECK_EQ("the question was asked", g_asked, 1);

    printf("the reset\n");
    bw_mod_message_reset();
    BW_CHECK_EQ("the queue is empty", (long)bw_mod_message_queue_count(), 0);
    BW_CHECK_EQ("and no answer is waiting", bw_mod_message_answer_take(), -1);

    printf("a question with a ticket\n");
    /* The question keeps its place among the texts, the text path only
     * records the answer, and the asker hears of it at the next delivery,
     * in the order the answers came. */
    bw_mod_message_reset();
    bw_mod_message_set_sink(NULL);
    {
        u32 t1 = 0, t2 = 0;
        BwModMessage m;

        g_heard_count = 0;
        BW_CHECK_EQ("a text first", bw_mod_message_queue_text("Hello"), 0);
        BW_CHECK_EQ("the question is queued",
                    bw_mod_message_ask("Take it?", 7u, heard, NULL, &t1), 0);
        BW_CHECK_EQ("a second question",
                    bw_mod_message_ask("Sure?", 8u, heard, NULL, &t2), 0);
        BW_CHECK_EQ("the tickets differ", t1 != 0u && t2 != 0u && t1 != t2, 1);
        BW_CHECK_EQ("the text is first", bw_mod_message_queue_peek_kind(),
                    (long)BW_MOD_MESSAGE_TEXT);
        (void)bw_mod_message_queue_take(&m);
        BW_CHECK_EQ("then the question", bw_mod_message_queue_peek_kind(),
                    (long)BW_MOD_MESSAGE_CONFIRM);
        (void)bw_mod_message_queue_take(&m);
        BW_CHECK_EQ("it carries its ticket", (long)m.ticket, (long)t1);
        (void)bw_mod_message_queue_take(&m);
        bw_mod_message_answered(t2, 0);
        bw_mod_message_answered(t1, 1);
        BW_CHECK_EQ("nothing is heard before the delivery", g_heard_count, 0);
        bw_mod_message_deliver();
        BW_CHECK_EQ("both are heard", g_heard_count, 2);
        BW_CHECK_EQ("in the order they were answered", (long)g_heard[0].ticket, (long)t2);
        BW_CHECK_EQ("the first said no", g_heard[0].yes, 0);
        BW_CHECK_EQ("with its tag", (long)g_heard[0].tag, 8);
        BW_CHECK_EQ("the second said yes", g_heard[1].yes, 1);
        bw_mod_message_deliver();
        BW_CHECK_EQ("an answer is heard once", g_heard_count, 2);
        bw_mod_message_answered(t1, 1);
        bw_mod_message_deliver();
        BW_CHECK_EQ("an old ticket is not answered again", g_heard_count, 2);

        BW_CHECK_EQ("an empty question is refused",
                    bw_mod_message_ask("", 0u, heard, NULL, &t1), -1);
        (void)bw_mod_message_ask("Lost?", 0u, heard, NULL, &t1);
        bw_mod_message_reset();
        bw_mod_message_answered(t1, 1);
        bw_mod_message_deliver();
        BW_CHECK_EQ("the reset drops a waiting question", g_heard_count, 2);
    }

    bw_mod_message_set_sink(NULL);
    BW_REPORT("test_mod_messages");
}
