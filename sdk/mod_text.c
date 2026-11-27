/*
 * sdk/mod_text.c -- the port's own text path: the side of the message
 * service that puts the text on the screen (docs/mod-services.md,
 * "Messages"; sdk/mod_message.h, "The port's own text path").
 *
 * What this file owns
 * -------------------
 *
 * The message service (sdk/mod_message.c) owns the queue and the
 * encoding.  It does not know how a text is shown: it asks through its
 * sink, and the sink's three calls are the game's own three jobs with a
 * text.  This file is the port's side of that sink.
 *
 *   show  the text goes into the game's own window, in the game's font:
 *         the string object is built out of the code units and the end
 *         code, and it is printed with the game's own print queue.
 *   ask   the question goes into the game's own yes/no window and the
 *         player's answer comes back.
 *   tick  the game advances to the next point where it would accept a
 *         message.
 *
 * A text's own turn
 * -----------------
 *
 * The game shows a mod's text in a turn of its own (sdk/mod_text.h):
 * in the field an event of the port's own opens the field's plain window
 * (stubs/ov/patch_ov021_scrcmd_msgwin.c; this file starts it on a quiet
 * field, bw_mod_text_frame), in a battle the text is the battle box's
 * next message (stubs/ov/patch_ov093_btlv_scu_message.c).  Both take the
 * text with bw_mod_text_next(), which encodes it with the window's font
 * and lays it out for the window.
 *
 * The show step
 * -------------
 *
 * The older seam stays for a port's own screen that has a window open:
 * the screen hands the port the window's three parts with
 * bw_mod_text_set_window() and calls bw_mod_text_pump(), and the pump
 * prints the next queued text into that window.  The game's own windows
 * do not use it: a text printed into a window the game has open for its
 * own text lands on top of the game's.
 *
 * The font's own table
 * --------------------
 *
 * A character the game font lacks shows as '?' (docs/mod-services.md).
 * The port's own text path answers the service's question with the
 * game's own answer: the font's glyph table (GF_FONT_GetGlyphIndex)
 * gives a character's glyph, and the glyph of '?' is the one a missing
 * character is shown as.  Without the game there is no font, and the
 * answer is the built-in one the service keeps (the printable ASCII
 * range).
 *
 * The port's own functions are weak
 * ---------------------------------
 *
 * Every game function below is weak, the way sdk/mod_state.c and
 * sdk/mod_event_field.c declare theirs: the library is linked by the
 * tests, which have no game code in them, and a weak declaration lets
 * the link succeed without it.  Each call is made only after the name
 * has been checked, so a link without the game reports that there is no
 * text path instead of calling into nothing.
 */

#include "mod_text.h"
#include "mod_message.h"
#include "mod_context.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* The game's own text functions                                       */
/* ------------------------------------------------------------------ */

/*
 * The game's own functions, each with the address the corpus gives it.
 * They are weak so that a link without the game still succeeds; see the
 * file header.
 */
#if defined(__GNUC__) || defined(__clang__)
#define BW_PORT_WEAK __attribute__((weak))
#else
#define BW_PORT_WEAK
#endif

/* The game's own objects, as the game names them. */
struct Strbuf;
struct PrintQueue;
struct Bmp;
struct GfFont;
struct BmpWin;

extern struct Strbuf *STRBUF_Create(u32 capacity, u32 heapId)
    BW_PORT_WEAK;                                     /* 0x020457B0 */
extern void STRBUF_SetStringN(struct Strbuf *strbuf, const u16 *srcChars,
                               u32 charCount)
    BW_PORT_WEAK;                                     /* 0x02045994 */
extern void STRBUF_Delete(struct Strbuf *strbuf)
    BW_PORT_WEAK;                                     /* 0x02045808 */
extern void PRINTSYS_PrintQueColor(struct PrintQueue *queue, struct Bmp *bmp,
                                   u32 x, s16 y, struct Strbuf *str,
                                   struct GfFont *font, u16 color)
    BW_PORT_WEAK;                                     /* 0x0201C724 */
extern struct Bmp *BMP_WIN_GetBmp(struct BmpWin *bmpWin)
    BW_PORT_WEAK;                                     /* 0x02045770 */
extern struct GfFont *GF_FONT_Create(int archiveId, int fileId, int format,
                                     int isFixedWidth, u32 heapId)
    BW_PORT_WEAK;                                     /* 0x0201D7EC */
extern u32 GF_FONT_Delete(struct GfFont *font)
    BW_PORT_WEAK;                                     /* 0x0201D83C */
extern u32 GF_FONT_GetGlyphIndex(struct GfFont *font, u16 ch)
    BW_PORT_WEAK;                                     /* 0x0201DB20 */
extern u32 GF_FONT_GetCharWidth(struct GfFont *font, u16 ch)
    BW_PORT_WEAK;                                     /* 0x0201DC6C */

/* The game's system and the field's event (sdk/mod_state.c; the event is
 * stubs/ov/patch_ov021_scrcmd_msgwin.c's, named as the overlay link names
 * an overlay's own functions). */
struct GameSystem;
struct GameEvent;
extern void *bw_mod_state_quiet_field(void) BW_PORT_WEAK;
extern void GAMESYSTEM_SetEvent(struct GameSystem *gameSystem,
                                struct GameEvent *event)
    BW_PORT_WEAK;                                     /* 0x02012108 */
extern struct GameEvent *ov021_EVENT_MOD_MESSAGE_Create(struct GameSystem *gameSystem)
    BW_PORT_WEAK;
extern int ov021_EVENT_MOD_MESSAGE_IsPlayerStill(struct GameSystem *gameSystem)
    BW_PORT_WEAK;

/* ------------------------------------------------------------------ */
/* The port's own constants                                            */
/* ------------------------------------------------------------------ */

/*
 * The font the port's own text path uses when the window has none of
 * its own: arc 0x17 (a/0/2/3), member 0.  It is the font the field's
 * message window uses (src/game/ov021/field_msgbg.c) and the one the
 * port's own screens use (engine/platform/mod_options_proc.c).
 */
#define BW_MOD_TEXT_FONT_ARC     0x17
#define BW_MOD_TEXT_FONT_MEMBER  0

/*
 * The game's own page tag (src/game/arm9/printsys.c, PRINTSYS_Apply): the
 * control block 0xF000, its kind and its argument count.  Kind 0xBE00
 * waits for the player, then scrolls the text up a line; the line break
 * after it is part of the tag.  The game's own text puts it in front of
 * every line after the second (the battle's line "lost all its PP" of bank
 * 14, the field's lines of bank 4), since its windows hold two lines.
 */
#define BW_MOD_TEXT_TAG          0xF000u
#define BW_MOD_TEXT_TAG_SCROLL   0xBE00u

/*
 * The gap the game's print adds after each character: one pixel
 * (PRINTSYS_SetupJob sets the job's spacing field to 1).
 */
#define BW_MOD_TEXT_SPACING      1u

/*
 * The frames the field must stay quiet, the player standing, before a
 * mod's text gets its own turn: one more than a frame's gap between two
 * steps, so a player who keeps walking is never stopped in the middle of
 * a step and the game's own checks of the square the player stands on
 * have run.
 */
#define BW_MOD_TEXT_QUIET_FRAMES 4u

/*
 * The colour the game's own text is drawn in: the letter colour 1, the
 * shadow colour 2 and no background, which is the default the font sets
 * for itself (src/game/arm9/gf_font.c, GF_FONT_InitTextColor).  The
 * colour word is five bits per colour, the letter first.
 */
#define BW_MOD_TEXT_COLOUR       0x0440u

/* ------------------------------------------------------------------ */
/* The state                                                           */
/* ------------------------------------------------------------------ */

/* The window the text goes into. */
static BwModTextWindow g_window;

/* The draw side: the yes/no window and the game's advance. */
static BwModTextDraw g_draw;

/* The port's own font, and the heap it was made in. */
static struct GfFont *g_font;
static u32           g_font_heap;

/* The font of the window that takes the next text: its glyph table is the
 * answer while the text is encoded (bw_mod_text_next). */
static struct GfFont *g_take_font;

/* The field's quiet frames in a row, and the battle mark. */
static u32 g_quiet;
static u32 g_battle_mark;

/* The question on the screen: its units and its ticket (0: none). */
static u16 g_question[BW_MOD_MESSAGE_MAX_UNITS];
static u32 g_question_count;
static u32 g_question_ticket;

/* ------------------------------------------------------------------ */
/* The draw side                                                        */
/* ------------------------------------------------------------------ */

static void sink_follow_draw(void);

void bw_mod_text_set_draw(const BwModTextDraw *draw)
{
    if (draw == NULL) {
        memset(&g_draw, 0, sizeof(g_draw));
    } else {
        g_draw = *draw;
    }
    sink_follow_draw();
}

/* ------------------------------------------------------------------ */
/* The window                                                           */
/* ------------------------------------------------------------------ */

void bw_mod_text_set_window(const BwModTextWindow *win)
{
    if (win == NULL) {
        memset(&g_window, 0, sizeof(g_window));
        return;
    }
    g_window = *win;
}

const BwModTextWindow *bw_mod_text_window(void)
{
    return &g_window;
}

/* ------------------------------------------------------------------ */
/* The font                                                             */
/* ------------------------------------------------------------------ */

void *bw_mod_text_font(u32 heap)
{
    if (GF_FONT_Create == NULL) {
        return NULL;
    }
    if (g_font != NULL && g_font_heap == heap) {
        return g_font;
    }
    if (g_font != NULL) {
        /* The font was made in another heap: the old one goes with it. */
        if (GF_FONT_Delete != NULL) {
            (void)GF_FONT_Delete(g_font);
        }
        g_font = NULL;
    }
    g_font = GF_FONT_Create(BW_MOD_TEXT_FONT_ARC, BW_MOD_TEXT_FONT_MEMBER, 0, 0,
                            heap);
    g_font_heap = heap;
    return g_font;
}

int bw_mod_text_font_has(u32 codepoint)
{
    struct GfFont *font = g_take_font != NULL ? g_take_font : g_font;
    u32 index;
    u32 missing;

    if (font == NULL || GF_FONT_GetGlyphIndex == NULL) {
        /*
         * No game, so no font and no glyph table: the answer is the
         * built-in one the message service keeps (the printable ASCII
         * range), so a text reads the same with and without the game.
         */
        return (codepoint >= 0x20u && codepoint <= 0x7Eu) ? 1 : 0;
    }
    if (codepoint > 0xFFFFu) {
        return 0;
    }
    index = GF_FONT_GetGlyphIndex(font, (u16)codepoint);
    missing = GF_FONT_GetGlyphIndex(font, (u16)BW_MOD_MESSAGE_UNKNOWN);
    return index != missing ? 1 : 0;
}

/* ------------------------------------------------------------------ */
/* The show                                                             */
/* ------------------------------------------------------------------ */

int bw_mod_text_print(const BwModTextWindow *win, const u16 *units, u32 count)
{
    u16 buf[BW_MOD_MESSAGE_MAX_UNITS + 1];
    struct Strbuf *str;
    struct GfFont *font;
    u32 i;

    if (win == NULL || units == NULL || count == 0u) {
        return -1;
    }
    if (count > BW_MOD_MESSAGE_MAX_UNITS) {
        count = BW_MOD_MESSAGE_MAX_UNITS;
    }
    if (win->print_que == NULL || win->bmp_win == NULL) {
        return -1;
    }
    if (STRBUF_Create == NULL || STRBUF_SetStringN == NULL ||
        STRBUF_Delete == NULL || PRINTSYS_PrintQueColor == NULL ||
        BMP_WIN_GetBmp == NULL) {
        /* No game: there is no print system to print into. */
        return -1;
    }
    /*
     * The game's string object: the code units, then the end code.  The
     * run the service hands over does not carry the end code; the port
     * adds it here, when the object is built (the shape of
     * engine/platform/mod_options_proc.c's ascii_obj).
     */
    for (i = 0; i < count; i++) {
        buf[i] = units[i];
    }
    buf[count] = (u16)BW_MOD_MESSAGE_EOS;
    str = STRBUF_Create(count + 1u, win->heap);
    if (str == NULL) {
        return -1;
    }
    STRBUF_SetStringN(str, buf, count + 1u);
    font = win->font != NULL ? (struct GfFont *)win->font
                             : (struct GfFont *)bw_mod_text_font(win->heap);
    if (font != NULL) {
        PRINTSYS_PrintQueColor((struct PrintQueue *)win->print_que,
                               BMP_WIN_GetBmp((struct BmpWin *)win->bmp_win),
                               (u32)win->x, (s16)win->y, str, font,
                               (u16)BW_MOD_TEXT_COLOUR);
    }
    STRBUF_Delete(str);
    return 0;
}

/* ------------------------------------------------------------------ */
/* The sink                                                             */
/* ------------------------------------------------------------------ */

/*
 * Shows the text in the game's own window.  The window is the one the
 * hook set: the game has it open at the point where the port pumps the
 * queue.
 */
static int text_show(const u16 *units, u32 count)
{
    if (g_window.print_que == NULL || g_window.bmp_win == NULL) {
        /* No window: the text cannot be shown, and saying so is all the
         * service can do with it. */
        return -1;
    }
    return bw_mod_text_print(&g_window, units, count);
}

/*
 * Asks the question in the game's own yes/no window and brings the
 * player's answer back.  Without the field's yes/no window there is
 * nothing to ask with, and the answer is no.
 */
static int text_ask(const u16 *units, u32 count)
{
    if (g_draw.ask == NULL) {
        return 0;
    }
    return g_draw.ask(units, count) != 0 ? 1 : 0;
}

/* Advances the game to the next point where it would accept a message. */
static void text_tick(void)
{
    if (g_draw.tick != NULL) {
        g_draw.tick();
    }
}

/*
 * The sink.  Its ask and its tick are there only while a draw side is
 * registered: without one a question cannot be asked, and the service
 * answers no at once and leaves the queued texts where they are.
 */
static BwModMessageSink g_sink = {
    text_show,
    NULL,
    NULL
};

static void sink_follow_draw(void)
{
    g_sink.ask = g_draw.ask != NULL ? text_ask : NULL;
    g_sink.tick = g_draw.tick != NULL ? text_tick : NULL;
}

/* ------------------------------------------------------------------ */
/* The show step                                                       */
/* ------------------------------------------------------------------ */

void bw_mod_text_install(void)
{
    bw_mod_message_set_sink(&g_sink);
    bw_mod_message_set_font_has(bw_mod_text_font_has);
}

int bw_mod_text_pump(void)
{
    if (g_window.print_que == NULL || g_window.bmp_win == NULL) {
        /*
         * No window: the game cannot accept a message here, so the queued
         * texts wait for the next point where it can.  Taking them out of
         * the queue now would lose them.
         */
        return 0;
    }
    return bw_mod_message_pump();
}

/* ------------------------------------------------------------------ */
/* A text's own turn                                                   */
/* ------------------------------------------------------------------ */

/* One character's width in the window, the gap after it included. */
static u32 char_width(struct GfFont *font, u16 ch)
{
    return GF_FONT_GetCharWidth(font, ch) + BW_MOD_TEXT_SPACING;
}

/*
 * The line break in front of line `line` (the second line is 1): the
 * plain break, or from the third line on the game's page tag, which waits
 * for the player and scrolls.  Returns the units written, 0 when they do
 * not fit.
 */
static u32 put_break(u16 *out, u32 at, u32 max_units, u32 line)
{
    if (line < 2u) {
        if (at + 1u > max_units) {
            return 0u;
        }
        out[at] = (u16)BW_MOD_MESSAGE_NEWLINE;
        return 1u;
    }
    if (at + 4u > max_units) {
        return 0u;
    }
    out[at] = (u16)BW_MOD_TEXT_TAG;
    out[at + 1u] = (u16)BW_MOD_TEXT_TAG_SCROLL;
    out[at + 2u] = 0u;
    out[at + 3u] = (u16)BW_MOD_MESSAGE_NEWLINE;
    return 4u;
}

u32 bw_mod_text_layout(void *fontp, u32 width, const u16 *units, u32 count,
                       u16 *out, u32 max_units)
{
    struct GfFont *font = (struct GfFont *)fontp;
    u32 i = 0u, o = 0u, line = 0u, line_w = 0u, k;

    if (out == NULL || units == NULL || max_units == 0u) {
        return 0u;
    }
    if (font == NULL || GF_FONT_GetCharWidth == NULL || width == 0u) {
        /* No font to measure with: the text as the mod wrote it. */
        for (i = 0u; i < count && o < max_units; i++) {
            out[o++] = units[i];
        }
        return o;
    }
    while (i < count) {
        u32 j = i, word_w = 0u, n;

        if (units[i] == (u16)BW_MOD_MESSAGE_NEWLINE) {
            /* The mod's own line break. */
            if ((n = put_break(out, o, max_units, line + 1u)) == 0u) {
                break;
            }
            o += n;
            line++;
            line_w = 0u;
            i++;
            continue;
        }
        if (units[i] == (u16)' ') {
            u32 space_w = char_width(font, (u16)' ');

            /* A space at the start of a line, or one that would end
             * past the edge, goes with the break. */
            if (line_w != 0u && line_w + space_w <= width && o < max_units) {
                out[o++] = (u16)' ';
                line_w += space_w;
            }
            i++;
            continue;
        }
        while (j < count && units[j] != (u16)' ' &&
               units[j] != (u16)BW_MOD_MESSAGE_NEWLINE) {
            word_w += char_width(font, units[j]);
            j++;
        }
        if (line_w != 0u && line_w + word_w > width) {
            /* The word starts the next line: the space before it goes. */
            if (o > 0u && out[o - 1u] == (u16)' ') {
                o--;
            }
            if ((n = put_break(out, o, max_units, line + 1u)) == 0u) {
                break;
            }
            o += n;
            line++;
            line_w = 0u;
        }
        for (k = i; k < j; k++) {
            u32 w = char_width(font, units[k]);

            if (line_w != 0u && line_w + w > width) {
                /* A word wider than the window is cut where it reaches
                 * the edge. */
                if ((n = put_break(out, o, max_units, line + 1u)) == 0u) {
                    return o;
                }
                o += n;
                line++;
                line_w = 0u;
            }
            if (o >= max_units) {
                return o;
            }
            out[o++] = units[k];
            line_w += w;
        }
        i = j;
    }
    return o;
}

u32 bw_mod_text_next(void *font, u32 width, u16 *out, u32 max_units)
{
    u32 ticket;
    u32 n;

    for (;;) {
        n = bw_mod_text_take(font, width, out, max_units, &ticket);
        if (n == 0u || ticket == 0u) {
            return n;
        }
        /* A question has no yes/no menu here: no. */
        bw_mod_text_answer(ticket, 0);
    }
}

u32 bw_mod_text_take(void *font, u32 width, u16 *out, u32 max_units, u32 *ticket)
{
    BwModMessage msg;
    int taken;

    if (ticket != NULL) {
        *ticket = 0u;
    }
    for (;;) {
        /* The window's font answers which characters it has, whoever
         * installed the service's own answer (a mod with rules only
         * installs none). */
        g_take_font = (struct GfFont *)font;
        taken = bw_mod_message_queue_take_with(&msg, bw_mod_text_font_has);
        g_take_font = NULL;
        if (taken != 0) {
            return 0u;
        }
        if (msg.kind == (u8)BW_MOD_MESSAGE_CONFIRM && msg.ticket == 0u) {
            /* The old waiting question: nothing waits for it here. */
            bw_mod_message_answer(0);
            continue;
        }
        if (msg.kind == (u8)BW_MOD_MESSAGE_CONFIRM) {
            u32 i;

            /* The question on the screen, for an interface that draws the
             * yes/no menu itself (bw_mod_text_question). */
            for (i = 0u; i < msg.count && i < BW_MOD_MESSAGE_MAX_UNITS; i++) {
                g_question[i] = msg.units[i];
            }
            g_question_count = i;
            g_question_ticket = msg.ticket;
            if (ticket != NULL) {
                *ticket = msg.ticket;
            }
        }
        return bw_mod_text_layout(font, width, msg.units, msg.count, out, max_units);
    }
}

void bw_mod_text_answer(u32 ticket, int yes)
{
    if (ticket == 0u) {
        return;
    }
    if (ticket == g_question_ticket) {
        g_question_ticket = 0u;
        g_question_count = 0u;
    }
    bw_mod_message_answered(ticket, yes);
}

int bw_mod_text_question(char *out, u32 size)
{
    u32 i, n = 0u;

    if (out == NULL || size == 0u) {
        return 0;
    }
    out[0] = '\0';
    if (g_question_ticket == 0u) {
        return 0;
    }
    for (i = 0u; i < g_question_count; i++) {
        u32 c = g_question[i];

        if (c == BW_MOD_MESSAGE_NEWLINE) {
            c = (u32)' ';
        }
        /* UTF-8 of one code unit: the game's units are the characters. */
        if (c < 0x80u) {
            if (n + 1u >= size) {
                break;
            }
            out[n++] = (char)c;
        } else if (c < 0x800u) {
            if (n + 2u >= size) {
                break;
            }
            out[n++] = (char)(0xC0u | (c >> 6));
            out[n++] = (char)(0x80u | (c & 0x3Fu));
        } else {
            if (n + 3u >= size) {
                break;
            }
            out[n++] = (char)(0xE0u | (c >> 12));
            out[n++] = (char)(0x80u | ((c >> 6) & 0x3Fu));
            out[n++] = (char)(0x80u | (c & 0x3Fu));
        }
    }
    out[n] = '\0';
    return 1;
}

int bw_mod_text_next_is_question(void)
{
    return bw_mod_message_queue_peek_kind() == (int)BW_MOD_MESSAGE_CONFIRM;
}

u32 bw_mod_text_waiting(void)
{
    return bw_mod_message_queue_count();
}

u32 bw_mod_text_battle_mark(void)
{
    return g_battle_mark;
}

void bw_mod_text_frame(void)
{
    struct GameSystem *gs;
    struct GameEvent *ev;

    /* The answers the windows recorded go to the callbacks that asked,
     * here on the frame and never inside the game's window code. */
    bw_mod_message_deliver();
    /* Every frame outside a battle moves the mark on: a battle's own
     * record of a held message is not good in the next battle. */
    if (bw_mod_battle_context.active == 0u) {
        g_battle_mark++;
    }
    if (bw_mod_message_queue_count() == 0u || bw_mod_state_quiet_field == NULL ||
        GAMESYSTEM_SetEvent == NULL || ov021_EVENT_MOD_MESSAGE_Create == NULL ||
        ov021_EVENT_MOD_MESSAGE_IsPlayerStill == NULL) {
        g_quiet = 0u;
        return;
    }
    gs = (struct GameSystem *)bw_mod_state_quiet_field();
    if (gs == NULL || ov021_EVENT_MOD_MESSAGE_IsPlayerStill(gs) == 0) {
        g_quiet = 0u;
        return;
    }
    if (++g_quiet < BW_MOD_TEXT_QUIET_FRAMES) {
        return;
    }
    g_quiet = 0u;
    ev = ov021_EVENT_MOD_MESSAGE_Create(gs);
    if (ev != NULL) {
        GAMESYSTEM_SetEvent(gs, ev);
    }
}

/* ------------------------------------------------------------------ */
/* The reset                                                           */
/* ------------------------------------------------------------------ */

void bw_mod_text_reset(void)
{
    if (g_font != NULL && GF_FONT_Delete != NULL) {
        (void)GF_FONT_Delete(g_font);
    }
    g_font = NULL;
    g_font_heap = 0;
    memset(&g_window, 0, sizeof(g_window));
    /*
     * The draw side goes with the reset: it is the overlay's (the field's
     * yes/no window is overlay 21's), and the overlays are unloaded at a
     * reset.  The side that owns it registers it again when it loads.
     */
    memset(&g_draw, 0, sizeof(g_draw));
    sink_follow_draw();
    g_take_font = NULL;
    g_quiet = 0;
    g_question_count = 0u;
    g_question_ticket = 0u;
}
