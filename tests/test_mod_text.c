/*
 * tests/test_mod_text.c -- the port's own text path: the side of the
 * message service that puts the text on the screen (sdk/mod_text.h;
 * docs/mod-services.md, "Messages").
 *
 * The message service (sdk/mod_message.c) owns the queue and the encoding;
 * the port's text path (sdk/mod_text.c) is the sink the service shows a
 * text through: it builds the game's own string object out of the code
 * units and prints it in the game's own window, in the game's own font.
 * What this test checks is the contract between the two:
 *
 *   - the order: the texts reach the sink in the order they were queued,
 *     one per message point, and an empty queue shows nothing;
 *   - the code units: the sink is handed the units the service encoded,
 *     and the game's string object carries them with the end code the port
 *     adds when it builds the object;
 *   - the answer: a question is asked through the yes/no window the
 *     overlay hands over, and the answer comes back to the call that waits
 *     for it;
 *   - the font: a character the game font lacks shows as '?', and the
 *     port's text path answers the service with the font's own table.
 *
 * Nothing here needs game data: the game's own string object, print queue
 * and font are weak in sdk/mod_text.c, so the test supplies its own and
 * watches what the port's text path hands them.
 */

#include <stdio.h>
#include <string.h>

#include "mod_message.h"
#include "mod_text.h"
#include "port_test.h"

/* ------------------------------------------------------------------ */
/* The game's own text functions: the test's doubles                   */
/* ------------------------------------------------------------------ */
/*
 * The port's text path calls the game's own string object, print queue,
 * window and font (sdk/mod_text.c).  Those calls are weak there, so the
 * test brings its own and records what the port's text path hands them.
 */
struct Strbuf { u32 word; };
struct PrintQueue { u32 word; };
struct Bmp { u32 word; };
struct GfFont { u32 word; };
struct BmpWin { u32 word; };

static struct Strbuf    g_strbuf;
static struct PrintQueue g_print_que;
static struct Bmp       g_bmp;
static struct BmpWin    g_bmp_win;
static struct GfFont    g_font;

/* The units the port's text path handed the game's string object. */
static u16 g_units[BW_MOD_MESSAGE_MAX_UNITS + 1u];
static u32 g_unit_count;

/* The texts the port's text path printed, in order. */
#define T_PRINTED_MAX 8
static char g_printed[T_PRINTED_MAX][64];
static int  g_printed_count;
static int  g_printed_times;

/* How many times the game was asked for a string object, and how many
 * times the object was deleted again. */
static int g_created;
static int g_deleted;

/* How many times the game was asked for a font. */
static int g_font_made;
static int g_font_deleted;

struct Strbuf *STRBUF_Create(u32 capacity, u32 heapId)
{
    (void)capacity;
    (void)heapId;
    g_created++;
    return &g_strbuf;
}

void STRBUF_SetStringN(struct Strbuf *strbuf, const u16 *srcChars,
                       u32 charCount)
{
    (void)strbuf;
    if (srcChars == NULL || charCount > BW_MOD_MESSAGE_MAX_UNITS + 1u) {
        return;
    }
    memcpy(g_units, srcChars, (size_t)charCount * sizeof(u16));
    g_unit_count = charCount;
}

void STRBUF_Delete(struct Strbuf *strbuf)
{
    (void)strbuf;
    g_deleted++;
}

void PRINTSYS_PrintQueColor(struct PrintQueue *queue, struct Bmp *bmp,
                            u32 x, s16 y, struct Strbuf *str,
                            struct GfFont *font, u16 color)
{
    u32 i;

    (void)queue;
    (void)bmp;
    (void)str;
    (void)font;
    (void)color;
    (void)x;
    (void)y;
    g_printed_times++;
    if (g_printed_count < T_PRINTED_MAX) {
        for (i = 0; i < g_unit_count && i < 63u; i++) {
            u16 c = g_units[i];

            if (c == (u16)BW_MOD_MESSAGE_EOS) {
                break;      /* the end code is not a character */
            }
            g_printed[g_printed_count][i] =
                (c >= 0x20u && c <= 0x7Eu) ? (char)c : '?';
        }
        g_printed[g_printed_count][i] = '\0';
        g_printed_count++;
    }
}

struct Bmp *BMP_WIN_GetBmp(struct BmpWin *bmpWin)
{
    (void)bmpWin;
    return &g_bmp;
}

struct GfFont *GF_FONT_Create(int archiveId, int fileId, int format,
                              int isFixedWidth, u32 heapId)
{
    (void)archiveId;
    (void)fileId;
    (void)format;
    (void)isFixedWidth;
    (void)heapId;
    g_font_made++;
    return &g_font;
}

u32 GF_FONT_Delete(struct GfFont *font)
{
    (void)font;
    g_font_deleted++;
    return 0;
}

/*
 * The font's own glyph table: the font has the printable ASCII range and
 * nothing else, and a character outside it has the glyph of '?', which is
 * how the game's own table says that the font lacks it.
 */
u32 GF_FONT_GetGlyphIndex(struct GfFont *font, u16 ch)
{
    (void)font;
    if (ch >= 0x20u && ch <= 0x7Eu) {
        return (u32)ch + 1u;
    }
    return (u32)BW_MOD_MESSAGE_UNKNOWN + 1u;
}

/* The font's widths: every letter 5 pixels, a space 3 (the print adds one
 * pixel after each). */
u32 GF_FONT_GetCharWidth(struct GfFont *font, u16 ch)
{
    (void)font;
    return ch == (u16)' ' ? 3u : 5u;
}

/* ------------------------------------------------------------------ */
/* The draw side: the yes/no window and the game's advance             */
/* ------------------------------------------------------------------ */

/* What the yes/no window was asked, and the answer the player gives. */
static u16 g_asked[BW_MOD_MESSAGE_MAX_UNITS];
static u32 g_asked_count;
static int  g_asked_times;
static int  g_answer;

/* How many times the game was advanced to the next message point. */
static int g_ticks;

/* The field's yes/no window: it shows the question and waits for the
 * player's answer. */
static int text_ask(const u16 *units, u32 count)
{
    g_asked_count = count;
    if (units != NULL && count <= BW_MOD_MESSAGE_MAX_UNITS) {
        memcpy(g_asked, units, (size_t)count * sizeof(u16));
    }
    g_asked_times++;
    return g_answer;
}

/* One step of the game: the wait for a confirm's answer needs the game to
 * reach the next point where it would accept a message. */
static void text_tick(void)
{
    g_ticks++;
    (void)bw_mod_text_pump();
}

/* ------------------------------------------------------------------ */
/* The helpers                                                        */
/* ------------------------------------------------------------------ */

/* Is the question the yes/no window was asked the given ASCII string? */
static int asked_is(const char *text)
{
    size_t n = strlen(text);
    u32 i;

    if (g_asked_count != (u32)n) {
        return 0;
    }
    for (i = 0; i < g_asked_count; i++) {
        if (g_asked[i] != (u16)(unsigned char)text[i]) {
            return 0;
        }
    }
    return 1;
}

/* A font that has every character: what a port that reads another font
 * installs (sdk/mod_message.h, "The encoding"). */
static int has_everything(u32 codepoint)
{
    (void)codepoint;
    return 1;
}

/* Forget what the text path did. */
static void forget(void)
{
    memset(g_units, 0, sizeof(g_units));
    memset(g_printed, 0, sizeof(g_printed));
    memset(g_asked, 0, sizeof(g_asked));
    g_unit_count = 0;
    g_printed_count = 0;
    g_printed_times = 0;
    g_created = 0;
    g_deleted = 0;
    g_asked_count = 0;
    g_asked_times = 0;
    g_ticks = 0;
}

/* ------------------------------------------------------------------ */
/* The test                                                            */
/* ------------------------------------------------------------------ */

int main(void)
{
    BwModTextWindow window;
    BwModTextDraw draw;

    /* The window the game has open at a message point, and the yes/no
     * window the overlay hands over. */
    memset(&window, 0, sizeof(window));
    window.print_que = &g_print_que;
    window.bmp_win = &g_bmp_win;
    window.heap = 0;
    window.x = 8;
    window.y = 16;

    memset(&draw, 0, sizeof(draw));
    draw.ask = text_ask;
    draw.tick = text_tick;

    /* The port's own text path is the service's sink. */
    bw_mod_text_install();
    bw_mod_text_set_window(&window);
    bw_mod_text_set_draw(&draw);
    bw_mod_message_reset();
    forget();

    printf("the order the texts reach the sink\n");
    BW_REQUIRE("the first text is queued", bw_mod_message_queue_text("first") == 0);
    BW_REQUIRE("the second text is queued", bw_mod_message_queue_text("second") == 0);

    /* A message point shows one text: the port's text path takes the next
     * one out of the queue. */
    BW_CHECK_EQ("the message point shows a text", bw_mod_text_pump(), 1);
    BW_CHECK_EQ("the message point shows a text", bw_mod_text_pump(), 1);
    BW_CHECK_EQ("two texts were printed", g_printed_count, 2);
    BW_CHECK_EQ("the first text came first", strcmp(g_printed[0], "first"), 0);
    BW_CHECK_EQ("the second text came second", strcmp(g_printed[1], "second"), 0);
    BW_CHECK_EQ("the queue is empty", (long)bw_mod_message_queue_count(), 0);
    BW_CHECK_EQ("and nothing more is shown", bw_mod_text_pump(), 0);
    BW_CHECK_EQ("the sink was not called again", g_printed_times, 2);

    printf("the code units the sink is handed\n");
    bw_mod_message_reset();
    forget();
    BW_REQUIRE("the text is queued", bw_mod_message_queue_text("ab") == 0);
    BW_CHECK_EQ("the message point shows it", bw_mod_text_pump(), 1);
    BW_CHECK_EQ("the string object holds the two units and the end code",
                (long)g_unit_count, 3);
    BW_CHECK_EQ("the first unit is the letter", (long)g_units[0], (long)'a');
    BW_CHECK_EQ("the second unit is the letter", (long)g_units[1], (long)'b');
    BW_CHECK_EQ("and the end code ends it", (long)g_units[2],
                (long)BW_MOD_MESSAGE_EOS);
    BW_CHECK_EQ("the string object was made", g_created, 1);
    BW_CHECK_EQ("and deleted again", g_deleted, 1);

    printf("the font's own answer\n");
    /* The port's text path answers the service with the font's own glyph
     * table, once the font is there. */
    BW_CHECK_EQ("the port's font is made", bw_mod_text_font(0) != NULL, 1);
    BW_CHECK_EQ("the font has the letter", bw_mod_text_font_has('a'), 1);
    BW_CHECK_EQ("the font does not have the accented letter",
                bw_mod_text_font_has(0x00E9u), 0);

    printf("a character the font lacks\n");
    /* The game's text is a run of 16-bit code units, one per character; a
     * character the font lacks shows as '?'. */
    bw_mod_message_reset();
    forget();
    BW_REQUIRE("the text is queued",
               bw_mod_message_queue_text("a\xC3\xA9" "b") == 0);
    BW_CHECK_EQ("the message point shows it", bw_mod_text_pump(), 1);
    BW_CHECK_EQ("the string object holds three units and the end code",
                (long)g_unit_count, 4);
    BW_CHECK_EQ("the letter before it is kept", (long)g_units[0], (long)'a');
    BW_CHECK_EQ("the character the font lacks is a question mark",
                (long)g_units[1], (long)BW_MOD_MESSAGE_UNKNOWN);
    BW_CHECK_EQ("the letter after it is kept", (long)g_units[2], (long)'b');

    printf("another font's answer\n");
    /* A port that has read another font installs its answer, and the text
     * then reads as that font would show it. */
    bw_mod_message_set_font_has(has_everything);
    bw_mod_message_reset();
    forget();
    BW_REQUIRE("the text is queued",
               bw_mod_message_queue_text("a\xC3\xA9" "b") == 0);
    BW_CHECK_EQ("the message point shows it", bw_mod_text_pump(), 1);
    BW_CHECK_EQ("the accented letter is kept now", (long)g_units[1], 0x00E9);

    /* The port's own answer is back. */
    bw_mod_message_set_font_has(bw_mod_text_font_has);

    printf("the answer the text path brings back\n");
    /* The question goes into the yes/no window the overlay handed over,
     * and the answer comes back to the call that waits for it. */
    bw_mod_message_reset();
    forget();
    g_answer = 1;
    BW_CHECK_EQ("the player says yes", bw_mod_message_confirm("Ready?"), 1);
    BW_CHECK_EQ("the question was asked", g_asked_times, 1);
    BW_CHECK_EQ("the question is the text that was queued", asked_is("Ready?"), 1);
    BW_CHECK_EQ("the game was advanced to the message point", g_ticks > 0, 1);

    bw_mod_message_reset();
    forget();
    g_answer = 0;
    BW_CHECK_EQ("the player says no", bw_mod_message_confirm("Ready?"), 0);
    BW_CHECK_EQ("the question was asked", g_asked_times, 1);

    printf("no window to show it in\n");
    /* Without a window the game cannot accept a message here, so the
     * queued texts wait: taking them out of the queue now would lose
     * them. */
    bw_mod_message_reset();
    forget();
    bw_mod_text_set_window(NULL);
    BW_REQUIRE("the text is queued", bw_mod_message_queue_text("later") == 0);
    BW_CHECK_EQ("nothing is shown", bw_mod_text_pump(), 0);
    BW_CHECK_EQ("nothing was printed", g_printed_times, 0);
    BW_CHECK_EQ("the text is still queued", (long)bw_mod_message_queue_count(), 1);

    /* And the window comes back: the queue is shown as it was left. */
    bw_mod_text_set_window(&window);
    BW_CHECK_EQ("the message point shows the text", bw_mod_text_pump(), 1);
    BW_CHECK_EQ("the sink was given it", strcmp(g_printed[0], "later"), 0);

    printf("a text's own turn: the layout\n");
    /* Letters are 6 pixels with the gap, a space 4: in a window 30 pixels
     * wide each word of four letters (24) is a line.  The second line
     * follows a plain break; every line after it follows the game's page
     * tag, which waits and scrolls. */
    {
        static const u16 want[] = {
            'a', 'a', 'a', 'a', 0xFFFEu,
            'b', 'b', 'b', 'b', 0xF000u, 0xBE00u, 0x0000u, 0xFFFEu,
            'c', 'c', 'c', 'c', 0xF000u, 0xBE00u, 0x0000u, 0xFFFEu,
            'd', 'd', 'd', 'd'
        };
        u16 in[19];
        u16 out[BW_MOD_TEXT_LAID_OUT_MAX];
        const char *src = "aaaa bbbb cccc dddd";
        u32 n, i, same = 1;

        for (i = 0; i < 19u; i++) {
            in[i] = (u16)src[i];
        }
        n = bw_mod_text_layout(&g_font, 30u, in, 19u, out, BW_MOD_TEXT_LAID_OUT_MAX);
        BW_CHECK_EQ("four lines and three breaks", (long)n, (long)(sizeof(want) / sizeof(want[0])));
        for (i = 0; i < n && i < sizeof(want) / sizeof(want[0]); i++) {
            same &= out[i] == want[i];
        }
        BW_CHECK_EQ("the breaks are the game's own", (long)same, 1);

        /* Two words that fit share a line, and the space stays. */
        n = bw_mod_text_layout(&g_font, 60u, in, 9u, out, BW_MOD_TEXT_LAID_OUT_MAX);
        BW_CHECK_EQ("two words on one line", (long)n, 9);
        BW_CHECK_EQ("with the space between them", (long)out[4], (long)' ');

        /* The mod's own line break counts as a line. */
        in[4] = (u16)BW_MOD_MESSAGE_NEWLINE;
        n = bw_mod_text_layout(&g_font, 60u, in, 9u, out, BW_MOD_TEXT_LAID_OUT_MAX);
        BW_CHECK_EQ("the mod's break is kept", (long)out[4], 0xFFFE);

        /* With no font to measure, the text is as written. */
        n = bw_mod_text_layout(NULL, 30u, in, 9u, out, BW_MOD_TEXT_LAID_OUT_MAX);
        BW_CHECK_EQ("no font: no layout", (long)n, 9);

        /* The next text: a question in the queue is passed over and answered
         * no, and the text after it is laid out. */
        bw_mod_message_reset();
        BW_REQUIRE("the question is queued", bw_mod_message_queue_confirm("Ready?") == 0);
        BW_REQUIRE("the text is queued", bw_mod_message_queue_text("aaaa bbbb") == 0);
        BW_CHECK_EQ("two wait", (long)bw_mod_text_waiting(), 2);
        n = bw_mod_text_next(&g_font, 30u, out, BW_MOD_TEXT_LAID_OUT_MAX);
        BW_CHECK_EQ("the text comes laid out", (long)n, 9);
        BW_CHECK_EQ("on two lines", (long)out[4], 0xFFFE);
        BW_CHECK_EQ("the question was answered no", bw_mod_message_answer_take(), 0);
        BW_CHECK_EQ("nothing waits", (long)bw_mod_text_waiting(), 0);
        BW_CHECK_EQ("and the next is nothing", (long)bw_mod_text_next(&g_font, 30u, out,
                                                                      BW_MOD_TEXT_LAID_OUT_MAX), 0);

        /* The window's font answers which letters there are: the accented
         * letter is a question mark in this font. */
        BW_REQUIRE("the text is queued", bw_mod_message_queue_text("a\xC3\xA9") == 0);
        n = bw_mod_text_next(&g_font, 300u, out, BW_MOD_TEXT_LAID_OUT_MAX);
        BW_CHECK_EQ("two units", (long)n, 2);
        BW_CHECK_EQ("the font lacks it", (long)out[1], (long)BW_MOD_MESSAGE_UNKNOWN);
    }

    printf("the reset\n");
    bw_mod_text_reset();
    BW_CHECK_EQ("the window is gone", bw_mod_text_window()->print_que == NULL, 1);
    BW_CHECK_EQ("and the text path shows nothing", bw_mod_text_pump(), 0);
    BW_CHECK_EQ("the font went with it", g_font_deleted, 1);

    bw_mod_message_reset();
    bw_mod_message_set_sink(NULL);
    BW_REPORT("test_mod_text");
}
