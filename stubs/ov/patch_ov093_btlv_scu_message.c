/*
 * patches: src/game/ov093/btlv_scu.c BTLV_SCU_PrintMessageInstant BTLV_SCU_StartMessage BTLV_SCU_UpdateMessage
 *
 * The port's message service in a battle (sdk/mod_text.h, "A text's own
 * turn"; sdk/mod_message.h; docs/mod-services.md, "Messages").
 *
 * A mod's text in a battle
 * ------------------------
 *
 * Every message of a battle goes through the scene unit's message box: the
 * box starts a message (BTLV_SCU_StartMessage, or BTLV_SCU_PrintMessageInstant
 * for one printed at once) and whoever waits for it steps it until it is
 * done (BTLV_SCU_UpdateMessage): the battle's own commands through the
 * core, the entry scenes in this unit.
 *
 * A mod's text gets a turn of its own there.  When the battle starts a
 * message and a mod's text waits, the battle's message is held back and
 * the mod's text is the box's message: it prints at the battle's text
 * speed, in the battle's font, and it waits as long as the battle's own
 * messages wait (A, B or the touch screen, or the battle's 80 frames).
 * When it is done the next waiting text follows, and then the battle's own
 * message starts as the battle asked for it.  Whoever waits sees one
 * message: "done" comes only after the battle's own message is done, and
 * the end of a mod's text is not reported as the end of a message
 * (BTLV_SCU_ConsumeMessageEnded).
 *
 * A mod's question (sdk/mod_message.h, "The question with a ticket") is
 * such a text too, printed as the game prints its own questions (no key
 * wait at its end), and then the battle's yes/no choice opens on the touch
 * screen with the battle's own YES and NO.  The answer is recorded and the
 * next text, or the battle's message, follows.  A question waits while the
 * touch screen holds a menu (BTLV_CORE_CanAskModQuestion) or the battle
 * runs by itself (a link or a recorded battle); the texts behind it wait
 * with it, so the order the mod wrote them in holds.
 *
 * The three functions are the game's own, and the port's part is around
 * them: with no mod's text waiting each one does what the game's does.
 *
 * The game's own records
 * ----------------------
 *
 * The port's library has no aletheia include path, so the scene unit is
 * copied here (src/game/ov093/btlv_scu_types.h, BtlvScu) as far as the
 * message procedure's fields and the window fade after them.  A copy holds
 * the same members in the same order, so the host compiler gives it the
 * same offsets the game's own unit gets; the offsets the game's header
 * documents are the DS ones, so they are checked only where the pointers
 * are four bytes wide.
 */

#include "mod_text.h"

#include <stddef.h>

/* ------------------------------------------------------------------ */
/* The game's own types                                                */
/* ------------------------------------------------------------------ */

struct BtlvCore;
struct BtlContext;
struct BtlMonTable;
struct BtlPokeParam;
struct BtlMain;
struct BmpWin;
struct CharBuf;
struct PrintSys;
struct Strbuf;
struct Bmp;
struct GfFont;
struct Tcbl;
struct TextPrinter;

/* The per-character callback of the game's print stream
 * (src/game/arm9/printsys_shared_types.h). */
typedef u8 (*PrintCallback)(u32 arg);

#define BW_PORT_STATIC_ASSERT(name, cond) \
    typedef char bw_port_static_assert_##name[(cond) ? 1 : -1]

/* ------------------------------------------------------------------ */
/* The game's own records, copied                                      */
/* ------------------------------------------------------------------ */

struct BtlvScu;

typedef int (*BtlvScuProc)(int *state, struct BtlvScu *scu);

/* The fade of the message window. */
typedef struct BtlvScuWinSlide {
    struct BmpWin *win;
    int evA;
    int evB;
    int evADelta;
    int evBDelta;
    u8 state;
    u8 stepsLeft;
} BtlvScuWinSlide;

/* One monster's sprite slot. */
typedef struct BtlvScuMonSlot {
    struct BtlPokeParam *mon;
    struct BtlvScu *owner;
    u8 slotIdx;
    u8 viewPos;
    u8 isShown;
    u8 isActive;
} BtlvScuMonSlot;

/* One side's name plate. */
typedef struct BtlvScuNamePlate {
    struct BtlvScu *scu;
    struct Bmp *bmp;
    u16 speciesId;
    u8 sideIdx;
    u8 bgPlane;
    u8 pokeId;
    u8 phase;
    u8 reserved0E[2];
    u8 *templatePixels;
    int slideX;
    int slideDx;
    u32 tile;
    u8 timer;
    u8 counter;
    u8 isShown : 1;
    u8 closeRequested : 1;
    u8 reserved23;
} BtlvScuNamePlate;

/* The move effect request. */
typedef struct BtlvScuWazaRequest {
    u16 wazaId;
    int attackerPos;
    int defenderPos;
    u8 turnCount;
    u8 continueCount;
    int effectSeq;
} BtlvScuWazaRequest;

/* The entry scene's state: every member, for the union's size. */
typedef union BtlvScuIntroState {
    struct {
        struct BtlPokeParam *mon;
        u8 monIdx;
        u8 side;
        u8 monId;
        u8 trainerIdx;
        u8 opponentClient;
    } single;
    struct {
        struct BtlPokeParam *mon[2];
        u8 monId[2];
        u8 monIdx[2];
        u8 side[2];
        u8 reserved14;
        u8 localSide;
    } dbl;
    struct {
        u8 side[2];
    } trainers;
    struct {
        struct BtlPokeParam *mon[2];
        u8 monIdx[2];
        u8 side[2];
        u8 monId[2];
        u8 aliveCount;
        u8 clientId;
    } pair;
    struct {
        struct BtlPokeParam *mon[3];
        u8 monIdx[3];
        u8 side[3];
        u8 monId[3];
        u8 aliveCount;
        u8 clientId;
    } triple;
    BtlvScuWazaRequest waza;
} BtlvScuIntroState;

/* The scene unit, as far as the window fade. */
typedef struct BtlvScu {
    struct BmpWin *msgWin;          /* 0x000 */
    struct CharBuf *msgWinChars;    /* 0x004 */
    struct BmpWin *nameWin;         /* 0x008 */
    struct CharBuf *nameWinChars;   /* 0x00C */
    u32 nameTile;                   /* 0x010 */
    u32 msgTile;                    /* 0x014 */
    struct PrintSys *printer;       /* 0x018 */
    struct BmpWin *msgWinCopy;      /* 0x01C */
    u8 lineIndex;                   /* 0x020 */
    u8 reserved21[3];
    void *fontMain;                 /* 0x024 */
    void *fontSub;                  /* 0x028 */
    void *taskSys;                  /* 0x02C */
    struct TextPrinter *msgStream;  /* 0x030, the running message stream */
    struct Strbuf *textBuf;         /* 0x034 */
    struct Strbuf *wordBuf0;        /* 0x038 */
    struct Strbuf *wordBuf1;        /* 0x03C */
    u8 monTaskCount;                /* 0x040 */
    u8 sendOutTaskCount;            /* 0x041 */
    u8 withdrawTaskCount;           /* 0x042 */
    u8 reserved43[5];
    BtlvScuMonSlot monSlots[6];     /* 0x048 */
    BtlvScuNamePlate namePlates[2]; /* 0x090 */
    void *nameGfxFile;              /* 0x0D8 */
    void *nameGfx;                  /* 0x0DC */
    BtlvScuProc preProc;            /* 0x0E0 */
    BtlvScuProc mainProc;           /* 0x0E4 */
    struct BtlvScu *procScu;        /* 0x0E8 */
    int procState;                  /* 0x0EC */
    BtlvScuIntroState intro;        /* 0x0F0 */
    u8 reserved108[0x28];
    struct BtlvCore *core;          /* 0x130 */
    struct BtlContext *context;     /* 0x134 */
    struct BtlMonTable *monTable;   /* 0x138 */
    u16 heapId;                     /* 0x13C */
    u8 msgKeepText;                 /* 0x13E, the text is already printed */
    u8 msgStep;                     /* 0x13F, the message procedure's step */
    u8 clientIndex;                 /* 0x140 */
    u8 msgEnded;                    /* 0x141, the stream finished, read once */
    u8 reserved142[2];
    int msgStreamState;             /* 0x144, 1 paused, 2 finished */
    u16 msgWait;                    /* 0x148, frames left to wait */
    u16 msgWaitReload;              /* 0x14A */
    u16 msgWaitEnd;                 /* 0x14C */
    u8 busyFlag;                    /* 0x14E */
    u8 phase;                       /* 0x14F */
    u8 quickMode;                   /* 0x150 */
    u8 reserved151[3];
    BtlvScuWinSlide slide;          /* 0x154 */
} BtlvScu;

BW_PORT_STATIC_ASSERT(scu_stream, sizeof(void *) != 4 ||
                      offsetof(BtlvScu, msgStream) == 0x030u);
BW_PORT_STATIC_ASSERT(scu_plates, sizeof(void *) != 4 ||
                      offsetof(BtlvScu, namePlates) == 0x090u);
BW_PORT_STATIC_ASSERT(scu_intro, sizeof(void *) != 4 ||
                      offsetof(BtlvScu, intro) == 0x0F0u);
BW_PORT_STATIC_ASSERT(scu_context, sizeof(void *) != 4 ||
                      offsetof(BtlvScu, context) == 0x134u);
BW_PORT_STATIC_ASSERT(scu_heap, sizeof(void *) != 4 ||
                      offsetof(BtlvScu, heapId) == 0x13Cu);
BW_PORT_STATIC_ASSERT(scu_ended, sizeof(void *) != 4 ||
                      offsetof(BtlvScu, msgEnded) == 0x141u);
BW_PORT_STATIC_ASSERT(scu_state, sizeof(void *) != 4 ||
                      offsetof(BtlvScu, msgStreamState) == 0x144u);
BW_PORT_STATIC_ASSERT(scu_wait_end, sizeof(void *) != 4 ||
                      offsetof(BtlvScu, msgWaitEnd) == 0x14Cu);
BW_PORT_STATIC_ASSERT(scu_slide, sizeof(void *) != 4 ||
                      offsetof(BtlvScu, slide) == 0x154u);

/* ------------------------------------------------------------------ */
/* The game's own functions                                             */
/* ------------------------------------------------------------------ */

/* This unit's (src/game/ov093/btlv_scu.c) and the battle's main module. */
extern void BTLV_SCU_StartMsgWinFadeIn(BtlvScuWinSlide *fade, int keepText);
extern int  BTLV_SCU_UpdateMsgWinFade(BtlvScuWinSlide *fade);
extern int  BTL_MAIN_GetTextSpeedFrames(struct BtlMain *main);
extern u8   BTL_MAIN_GetCommPos(struct BtlMain *main);

/* The ARM9's text, keys, sound and string object. */
extern void BMP_Clear(struct Bmp *bmp, u8 value);
extern void BMP_WIN_TransferChars(struct BmpWin *win);
extern void GF_FONT_SetTextColor(u32 red, u32 green, u32 blue);
extern void GF_FONT_SetDefaultTextColor(void);
extern struct TextPrinter *PRINTSYS_PrintStreamCallBack(struct BmpWin *win, s16 x, s16 y,
                                                        struct Strbuf *str, struct GfFont *font,
                                                        s32 wait, struct Tcbl *tcbl, u32 priority,
                                                        u32 heap, u16 fillColor,
                                                        PrintCallback callback);
extern void PRINTSYS_PrintStreamStop(struct TextPrinter *printer);
extern void PRINTSYS_PrintStreamRun(struct TextPrinter *printer);
extern u32  PRINTSYS_PrintStreamGetState(const struct TextPrinter *printer);
extern u32  PRINTSYS_PrintStreamIsOverrideSpeed(const struct TextPrinter *printer);
extern void PRINTSYS_PrintStreamSetSpeed(struct TextPrinter *printer, s32 wait);
extern void PRINTSYS_PrintStreamResetSpeed(struct TextPrinter *printer);
extern void PRINTSYS_PrintStreamReleasePause(struct TextPrinter *printer);
extern void PRINTSYS_PrintStreamDelete(struct TextPrinter *printer);
extern void PRINTSYS_Print(struct Bmp *bmp, s16 x, s16 y, struct Strbuf *str, struct GfFont *font);
extern u32  KEY_GetCont(void);
extern u32  KEY_GetTrg(void);
extern int  TOUCHPANEL_GetCont(void);
extern int  TOUCHPANEL_GetTrg(void);
extern signed char MSG_SPEED_GetWaitForSpeed(u32 speed);
extern void GFL_USE_PlaySeDefaultVolume(u32 seqNo);
extern int  GFL_USE_IsSeqPlaying(u32 seqNo);
extern struct Strbuf *STRBUF_Create(u32 capacity, u32 heapId);
extern void STRBUF_SetStringN(struct Strbuf *strbuf, const u16 *srcChars, u32 charCount);
extern void STRBUF_Delete(struct Strbuf *strbuf);

/* A message of the battle's own banks (src/game/ov093/btlv_core_types.h)
 * and the yes/no choice (btlv_core.c; the port's check is
 * stubs/ov/patch_ov093_btlv_core.c's). */
typedef struct BtlvMessage {
    u16 msgId;
    u8 param;
    u8 kind : 4;
    u8 argCount : 4;
    u32 args[8];
} BtlvMessage;

extern void BTLV_SCU_InitMessage(BtlvMessage *msg, int kind, int id);
extern void BTLV_CORE_StartYesNoMenu(struct BtlvCore *core, BtlvMessage *yesMsg,
                                     BtlvMessage *noMsg, u32 mode);
extern int  BTLV_CORE_PollYesNoMenu(struct BtlvCore *core, int *outResult);
extern int  BTLV_CORE_CanAskModQuestion(struct BtlvCore *core);

/* The functions this file stands in for. */
void BTLV_SCU_PrintMessageInstant(BtlvScu *scu, struct Strbuf *str);
void BTLV_SCU_StartMessage(BtlvScu *scu, struct Strbuf *str, u16 wait, PrintCallback callback);
int  BTLV_SCU_UpdateMessage(BtlvScu *scu);

/* ------------------------------------------------------------------ */
/* The game's own constants                                            */
/* ------------------------------------------------------------------ */

/* The sound of the next line (btlv_scu.c's own). */
#define BTLV_SCU_SE_MSG_ADVANCE 0x547

/* The message box's text colours: letters 1, shadow 9, ground 0xC. */
#define BTLV_SCU_MSG_GROUND     0xC

/* The wait the battle's own messages take (BTLV_CORE_PrintMessageBuffer
 * and the entry scenes): 0x50 frames after the text, or a key. */
#define MOD_MESSAGE_WAIT        0x50

/* The box is 0x1E tiles wide and the text starts at its left edge
 * (BTLV_SCU_Create, BTLV_SCU_StartMessage). */
#define MOD_MESSAGE_TEXT_WIDTH  (0x1Eu * 8u)

/* The end code of the game's text. */
#define MOD_MESSAGE_EOS         0xFFFFu

/* A question as the game asks its own (BTL_CLIENT_StepSelectRun): the text
 * with the wait 0xFF, so its end waits for no key; then the choice with the
 * battle's UI lines YES (8) and NO (9) of bank 16 (BTLV_MSG_KIND_UI), mode
 * 1; the first answer is YES. */
#define MOD_QUESTION_WAIT       0xFF
#define MOD_QUESTION_MSG_KIND   3
#define MOD_QUESTION_MSG_YES    8
#define MOD_QUESTION_MSG_NO     9
#define MOD_QUESTION_MODE       1u
#define MOD_QUESTION_ANSWER_YES 0

/* ------------------------------------------------------------------ */
/* The game's own bodies                                               */
/* ------------------------------------------------------------------ */

/* BTLV_SCU_PrintMessageInstant as the game has it. */
static void scu_print_instant(BtlvScu *scu, struct Strbuf *str)
{
    BMP_Clear((struct Bmp *)scu->msgWinChars, BTLV_SCU_MSG_GROUND);
    GF_FONT_SetTextColor(1, 9, BTLV_SCU_MSG_GROUND);
    PRINTSYS_Print((struct Bmp *)scu->msgWinChars, 0, 0, str, (struct GfFont *)scu->fontMain);
    BMP_WIN_TransferChars(scu->msgWin);
    scu->msgStep = 0;
    scu->msgWait = 0;
    scu->msgWaitReload = 0;
    scu->msgWaitEnd = 0;
    scu->msgKeepText = 1;
    scu->msgEnded = 0;
}

/* BTLV_SCU_StartMessage as the game has it: wait 0xFF is the default
 * 0x50 frames. */
static void scu_start_message(BtlvScu *scu, struct Strbuf *str, u16 wait, PrintCallback callback)
{
    struct TextPrinter *stream;

    BMP_Clear((struct Bmp *)scu->msgWinChars, BTLV_SCU_MSG_GROUND);
    GF_FONT_SetTextColor(1, 9, BTLV_SCU_MSG_GROUND);
    stream = PRINTSYS_PrintStreamCallBack(scu->msgWin, 0, 0, str, (struct GfFont *)scu->fontMain,
                                          BTL_MAIN_GetTextSpeedFrames((struct BtlMain *)scu->context),
                                          (struct Tcbl *)scu->taskSys, 0,
                                          scu->heapId, BTLV_SCU_MSG_GROUND, callback);
    scu->msgStream = stream;
    PRINTSYS_PrintStreamStop(stream);
    scu->msgStep = 0;
    scu->msgKeepText = 0;
    if (wait == 0xff) {
        scu->msgWait = 0x50;
        scu->msgWaitEnd = 0;
    } else {
        scu->msgWait = wait;
        scu->msgWaitEnd = wait;
    }
    scu->msgWaitReload = scu->msgWait;
    scu->msgEnded = 0;
}

/* BTLV_SCU_UpdateMessage as the game has it: 1 when the message is done. */
static int scu_update_message(BtlvScu *scu)
{
    switch (scu->msgStep) {
    case 0:
        BTLV_SCU_StartMsgWinFadeIn(&scu->slide, scu->msgKeepText);
        scu->msgStep = scu->msgStep + 1;
        /* fall through */
    case 1:
        if (BTLV_SCU_UpdateMsgWinFade(&scu->slide) == 0)
            break;
        if (scu->msgKeepText != 0) {
            scu->msgStep = 6;
            break;
        }
        if (scu->msgStream != 0)
            PRINTSYS_PrintStreamRun(scu->msgStream);
        scu->msgStep = scu->msgStep + 1;
        /* fall through */
    case 2:
        if (scu->msgStream != 0) {
            if (BTL_MAIN_GetCommPos((struct BtlMain *)scu->context) == 0) {
                if ((KEY_GetCont() & 3) != 0 || TOUCHPANEL_GetCont() != 0) {
                    if (PRINTSYS_PrintStreamIsOverrideSpeed(scu->msgStream) == 0)
                        PRINTSYS_PrintStreamSetSpeed(scu->msgStream, MSG_SPEED_GetWaitForSpeed(2));
                } else {
                    if (PRINTSYS_PrintStreamIsOverrideSpeed(scu->msgStream) != 0)
                        PRINTSYS_PrintStreamResetSpeed(scu->msgStream);
                }
            }
            scu->msgStreamState = PRINTSYS_PrintStreamGetState(scu->msgStream);
            if (scu->msgStreamState != 0) {
                if (BTL_MAIN_GetCommPos((struct BtlMain *)scu->context) == 0)
                    scu->msgStep = 3;
                else
                    scu->msgStep = 4;
                if (scu->msgStreamState == 2) {
                    scu->msgWait = scu->msgWaitEnd;
                    scu->msgEnded = 1;
                }
            }
        } else {
            scu->msgStep = 6;
        }
        break;
    case 3:
        if ((KEY_GetTrg() & 3) != 0 || TOUCHPANEL_GetTrg() != 0)
            scu->msgWait = 0;
        if (scu->msgWait != 0) {
            scu->msgWait = scu->msgWait - 1;
            break;
        }
        scu->msgStep = 5;
        break;
    case 4:
        if (scu->msgWait != 0) {
            scu->msgWait--;
            break;
        }
        scu->msgStep = 5;
        break;
    case 5:
        if (scu->msgStreamState == 2) {
            GF_FONT_SetDefaultTextColor();
            PRINTSYS_PrintStreamDelete(scu->msgStream);
            scu->msgStream = 0;
            scu->msgStep = 6;
            break;
        }
        PRINTSYS_PrintStreamReleasePause(scu->msgStream);
        GFL_USE_PlaySeDefaultVolume(BTLV_SCU_SE_MSG_ADVANCE);
        scu->msgStep = 2;
        scu->msgWait = scu->msgWaitReload;
        break;
    case 6:
        if (GFL_USE_IsSeqPlaying(BTLV_SCU_SE_MSG_ADVANCE) == 0) {
            scu->msgStep = 7;
            return 1;
        }
        break;
    case 7:
    default:
        return 1;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* The port's state                                                    */
/* ------------------------------------------------------------------ */

/* What the battle asked for while a mod's text has the box. */
enum {
    HELD_NONE,
    HELD_STREAM,        /* BTLV_SCU_StartMessage */
    HELD_INSTANT        /* BTLV_SCU_PrintMessageInstant */
};

typedef struct ModMessageHeld {
    BtlvScu *scu;
    struct Strbuf *str;
    PrintCallback callback;
    u32 mark;               /* bw_mod_text_battle_mark when it was held */
    u16 wait;
    u8 kind;
} ModMessageHeld;

static ModMessageHeld sHeld;

/* The mod's text the box is showing: the stream reads it in place, so it
 * lives until the stream is deleted. */
static struct Strbuf *sModStr;
static BtlvScu *sModScu;
static u32 sModMark;
static u16 sModUnits[BW_MOD_TEXT_LAID_OUT_MAX + 1u];

/* The question in the box: its ticket, and whether its choice is open. */
static u32 sAskTicket;
static u8  sAskOpen;

/* The mod's text goes once its stream is gone.  One from another battle
 * went with that battle's heap: it is only forgotten. */
static void mod_text_release(BtlvScu *scu)
{
    if (sModStr == NULL) {
        return;
    }
    if (sModScu == scu && sModMark == bw_mod_text_battle_mark()) {
        if (scu->msgStream != NULL) {
            return;
        }
        STRBUF_Delete(sModStr);
    }
    sModStr = NULL;
    sModScu = NULL;
}

/* The next waiting text starts in the box.  Returns 1 when one did. */
static int mod_text_start(BtlvScu *scu)
{
    struct Strbuf *str;
    u32 count;
    u32 ticket;

    if (bw_mod_text_waiting() == 0u || scu->msgWin == NULL) {
        return 0;
    }
    if (bw_mod_text_next_is_question() &&
        (BTL_MAIN_GetCommPos((struct BtlMain *)scu->context) != 0 ||
         !BTLV_CORE_CanAskModQuestion(scu->core))) {
        /* the touch screen holds a menu, or the battle runs by itself:
         * the question and the texts behind it wait */
        return 0;
    }
    mod_text_release(scu);
    if (sModStr != NULL) {
        /* the last one's stream is still running */
        return 0;
    }
    count = bw_mod_text_take(scu->fontMain, MOD_MESSAGE_TEXT_WIDTH, sModUnits,
                             BW_MOD_TEXT_LAID_OUT_MAX, &ticket);
    if (count == 0u) {
        return 0;
    }
    sModUnits[count] = (u16)MOD_MESSAGE_EOS;
    str = STRBUF_Create(BW_MOD_TEXT_LAID_OUT_MAX + 1u, scu->heapId);
    if (str == NULL) {
        return 0;
    }
    STRBUF_SetStringN(str, sModUnits, count + 1u);
    sModStr = str;
    sModScu = scu;
    sModMark = bw_mod_text_battle_mark();
    sAskTicket = ticket;
    sAskOpen = 0;
    scu_start_message(scu, str, ticket != 0u ? MOD_QUESTION_WAIT : MOD_MESSAGE_WAIT, NULL);
    return 1;
}

/*
 * A question's turn after its text: the choice opens, and the answer is
 * recorded once the player has chosen.  Returns 1 while the question still
 * holds the box.
 */
static int mod_question_step(BtlvScu *scu)
{
    BtlvMessage yes, no;
    int choice;

    if (sAskOpen == 0) {
        BTLV_SCU_InitMessage(&yes, MOD_QUESTION_MSG_KIND, MOD_QUESTION_MSG_YES);
        BTLV_SCU_InitMessage(&no, MOD_QUESTION_MSG_KIND, MOD_QUESTION_MSG_NO);
        BTLV_CORE_StartYesNoMenu(scu->core, &yes, &no, MOD_QUESTION_MODE);
        sAskOpen = 1;
        return 1;
    }
    if (BTLV_CORE_PollYesNoMenu(scu->core, &choice) == 0) {
        return 1;
    }
    bw_mod_text_answer(sAskTicket, choice == MOD_QUESTION_ANSWER_YES);
    sAskTicket = 0u;
    sAskOpen = 0;
    return 0;
}

/* Hold back what the battle asked for. */
static void hold(BtlvScu *scu, struct Strbuf *str, u16 wait, PrintCallback callback, u8 kind)
{
    sHeld.scu = scu;
    sHeld.str = str;
    sHeld.wait = wait;
    sHeld.callback = callback;
    sHeld.mark = bw_mod_text_battle_mark();
    sHeld.kind = kind;
}

/* A message held in another battle, or by another scene unit, is not this
 * one's. */
static int held_here(BtlvScu *scu)
{
    if (sHeld.kind == HELD_NONE) {
        return 0;
    }
    if (sHeld.scu != scu || sHeld.mark != bw_mod_text_battle_mark()) {
        sHeld.kind = HELD_NONE;
        return 0;
    }
    return 1;
}

/* ------------------------------------------------------------------ */
/* The functions                                                       */
/* ------------------------------------------------------------------ */

void BTLV_SCU_PrintMessageInstant(BtlvScu *scu, struct Strbuf *str)
{
    if (held_here(scu) || mod_text_start(scu)) {
        /* (a newer message of the battle takes the older one's place) */
        hold(scu, str, 0, NULL, HELD_INSTANT);
        return;
    }
    scu_print_instant(scu, str);
}

void BTLV_SCU_StartMessage(BtlvScu *scu, struct Strbuf *str, u16 wait, PrintCallback callback)
{
    if (held_here(scu) || mod_text_start(scu)) {
        hold(scu, str, wait, callback, HELD_STREAM);
        return;
    }
    scu_start_message(scu, str, wait, callback);
}

int BTLV_SCU_UpdateMessage(BtlvScu *scu)
{
    int done = scu_update_message(scu);
    ModMessageHeld held;

    if (!held_here(scu)) {
        return done;
    }
    /* The mod's text is not the battle's message: its end is not one. */
    scu->msgEnded = 0;
    if (done == 0) {
        return 0;
    }
    if (sAskTicket != 0u && mod_question_step(scu)) {
        return 0;
    }
    if (mod_text_start(scu)) {
        return 0;
    }
    held = sHeld;
    sHeld.kind = HELD_NONE;
    mod_text_release(scu);
    if (held.kind == HELD_INSTANT) {
        scu_print_instant(scu, held.str);
    } else {
        scu_start_message(scu, held.str, held.wait, held.callback);
    }
    return 0;
}
