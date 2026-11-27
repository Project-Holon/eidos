/*
 * patches: src/game/ov021/scrcmd_msgwin.c SCRCMD_MSGWIN_CmdAddMenuItem
 *
 * symbols:
 *   FLDMMDL_SysPauseMove  0x0216D9A0  thumb  ov010
 *   FLDMMDL_SysResumeMove  0x0216D9E4  thumb  ov010
 *   FLDMMDL_SysSearchPlayer  0x0216DEE0  thumb  ov010
 *   FLDMMDL_CheckEndAcmd  0x0216D454  thumb  ov010
 *   FLDMMDL_CheckMoveBitMove  0x0216DA5C  thumb  ov010
 *   FLDMMDL_OnMoveBitMovePause  0x0216DAF8  thumb  ov010
 *   FLDMMDL_OffMoveBitMovePause  0x0216DB04  thumb  ov010
 *
 * The port's message service in the field (sdk/mod_text.h, "A text's own
 * turn"; sdk/mod_message.h; docs/mod-services.md, "Messages"), and the PC's
 * storage menu of the one-screen layout.
 *
 * A mod's text in the field
 * -------------------------
 *
 * A mod's text gets a turn of its own: it never goes into a window the
 * game has open for its own text.  When the field is quiet and the player
 * stands still, the port starts the event below (sdk/mod_text.c,
 * bw_mod_text_frame).  The event is the shape of the game's own field
 * events that show a message (src/game/ov018/intrude_field.c,
 * INTRUDE_FIELD_EventExitConfirm) and of the script commands that show one
 * (src/game/ov021/scrcmd_msgwin.c, SCRCMD_MSGWIN_OpenPlainWinMsg and
 * SCRCMD_MSGWIN_WaitKeyWithCursor):
 *
 *   1. every model stops; the player finishes the step it is in, as the
 *      script's own lock lets it (src/game/ov021/scrcmd_fldmmdl.c,
 *      SCRCMD_FLDMMDL_PauseMmdlsReleaseActive);
 *   2. the field's plain window opens at the bottom of the screen, made
 *      the way the script's plain window is made, and the text prints in
 *      it at the game's speed, in the field's font;
 *   3. A or B, with the game's cursor and sound, closes it; the next
 *      waiting text follows in the same window.  A question stays on the
 *      screen and the field's yes/no menu opens beside it, as the
 *      script's own question does (SCRCMD_MSGWIN_YesNoEventMain); the
 *      answer is recorded and the asker hears of it on the next frame;
 *   4. the window closes and the models move again.
 *
 * The window is the game's own, so every interface style draws it as it
 * draws the game's plain window (engine/ui/textbox.c).
 *
 * The game's own records
 * ----------------------
 *
 * The port's library has no aletheia include path, so the one record this
 * file reads is copied here, the way sdk/mod_event_field.c does: the
 * field's message BG, for its heap.  A copy holds the same members in the
 * same order, so the host compiler gives it the same offsets the game's
 * own unit gets; the offsets the game's own header documents are the DS
 * ones, so they are checked below only where the pointers are four bytes
 * wide.
 */

#include "mod_text.h"

#include <stddef.h>

/* ------------------------------------------------------------------ */
/* The game's own types                                                */
/* ------------------------------------------------------------------ */

/*
 * The game's own records, named only as the opaque types its accessors
 * take, the way sdk/mod_event_field.c does.  The names are the game's own
 * (src/game/ov021/scrcmd_msgwin.c spells them the same way).
 */
typedef struct ScriptVm ScriptVm;
typedef struct ScrcmdWork ScrcmdWork;
typedef struct ScriptWork ScriptWork;
typedef struct GameSystem GameSystem;
typedef struct GameEvent GameEvent;
typedef struct FieldMap FieldMap;
typedef struct Strbuf Strbuf;
typedef struct MsgBank MsgBank;
typedef struct WordSet WordSet;
typedef struct GfFont GfFont;
typedef struct PrintQue PrintQue;
typedef struct FieldPlainWin FieldPlainWin;
typedef struct MmdlSys MmdlSys;
typedef struct Mmdl Mmdl;

/* The function a game event runs each frame (src/game/arm9/game_event.h). */
typedef int (*GameEventFunc)(GameEvent *event, int *seq, void *work);

/* ------------------------------------------------------------------ */
/* The port's own check                                                */
/* ------------------------------------------------------------------ */

/*
 * The port's own check, in the shape sdk/mod_event_field.c uses: a
 * condition that must hold at every pointer width the port is built for.
 */
#define BW_PORT_STATIC_ASSERT(name, cond) \
    typedef char bw_port_static_assert_##name[(cond) ? 1 : -1]

/* ------------------------------------------------------------------ */
/* The game's own records, copied                                      */
/* ------------------------------------------------------------------ */

/*
 * The message BG of the field (src/game/ov021/field_msgbg_types.h): the
 * heap the field's objects come from.  The copy stops at the font: the
 * record is 0x17C bytes long and the rest of it is the field's own
 * business.
 */
typedef struct FieldMsgBg {
    u16      heapId;        /* 0x000, the field's heap */
    u16      bgFrame;       /* 0x002, message frame */
    u16      bgFrameSys;    /* 0x004, system window frame */
    u16      windowKind;    /* 0x006, palette of the last window */
    GfFont  *font;          /* 0x008, the font the field prints with */
    PrintQue *printQue;     /* 0x00C, the queue the field runs */
} FieldMsgBg;

BW_PORT_STATIC_ASSERT(msgbg_heap, sizeof(void *) != 4 ||
                      offsetof(FieldMsgBg, heapId) == 0x000u);
BW_PORT_STATIC_ASSERT(msgbg_font, sizeof(void *) != 4 ||
                      offsetof(FieldMsgBg, font) == 0x008u);

/* ------------------------------------------------------------------ */
/* The game's own functions                                             */
/* ------------------------------------------------------------------ */

/*
 * The game's own functions, each with the name the game's own headers
 * give it (src/game/ov021/field_msgbg.h, src/game/ov021/fieldmap.h,
 * src/game/ov010/fldmmdl.h, src/game/arm9/game_event.h,
 * src/game/arm9/gamesystem.h, src/game/arm9/strbuf.h and the key and
 * sound headers of the ARM9).
 */

/* The field's message BG and its plain window. */
extern GfFont *FIELD_MSGBG_GetFont(FieldMsgBg *msgBg);
extern FieldPlainWin *FIELD_MSGBG_CreatePlainWin(FieldMsgBg *msgBg, MsgBank *msgData,
                                                 u32 winType, u16 x, u16 y,
                                                 u16 width, u16 height);
extern int  FIELD_MSGBG_DeletePlainWin(FieldPlainWin *plainWin);
extern void FIELD_MSGBG_ClearPlainWin(FieldPlainWin *plainWin);
extern void FIELD_MSGBG_PlainWinPrintStream(FieldPlainWin *plainWin, u32 x, u16 y,
                                            Strbuf *str);
extern u32  FIELD_MSGBG_PlainWinMain(FieldPlainWin *plainWin);
extern void FIELD_MSGBG_PlainWinDrawCursor(FieldPlainWin *plainWin);

/* The field's yes/no menu (src/game/ov021/field_msgbg.c). */
typedef struct FieldMenuFunc FieldMenuFunc;
extern FieldMenuFunc *FIELD_MSGBG_CreateYesNoMenu(FieldMsgBg *msgBg, u32 cursorPos);
extern int  FIELD_MSGBG_YesNoMenuMain(FieldMenuFunc *menu);
extern int  FIELD_MSGBG_DeleteMenuFunc(FieldMenuFunc *menu);

/* The field map. */
extern FieldMsgBg *FIELDMAP_GetMsgBg(FieldMap *fieldMap);
extern MmdlSys *FIELDMAP_GetMmdlSys(FieldMap *fieldMap);
extern void *GAMESYSTEM_GetFieldMap(const GameSystem *gameSystem);

/* The field's models. */
extern void  FLDMMDL_SysPauseMove(MmdlSys *sys);
extern void  FLDMMDL_SysResumeMove(MmdlSys *sys);
extern Mmdl *FLDMMDL_SysSearchPlayer(MmdlSys *sys);
extern int   FLDMMDL_CheckEndAcmd(Mmdl *mmdl);
extern int   FLDMMDL_CheckMoveBitMove(Mmdl *mmdl);
extern void  FLDMMDL_OnMoveBitMovePause(Mmdl *mmdl);
extern void  FLDMMDL_OffMoveBitMovePause(Mmdl *mmdl);

/* The game's events. */
extern GameEvent *GAME_EVENT_Create(GameSystem *gameSystem, GameEvent *parent,
                                    GameEventFunc func, u32 workSize);
extern void *GAME_EVENT_GetWork(GameEvent *event);
extern GameSystem *GAME_EVENT_GetGameSystem(GameEvent *event);

/* The string object, the keys and the sound. */
extern Strbuf *STRBUF_Create(u32 capacity, u32 heapId);
extern void    STRBUF_SetStringN(Strbuf *strbuf, const u16 *srcChars, u32 charCount);
extern void    STRBUF_Delete(Strbuf *strbuf);
extern u32     KEY_GetTrg(void);
extern void    GFL_USE_PlaySeDefaultVolume(u32 seNo);

/* ------------------------------------------------------------------ */
/* The port's own constants                                            */
/* ------------------------------------------------------------------ */

/*
 * The plain window the script's message command opens
 * (SCRCMD_MSGWIN_OpenPlainWinMsg): the plain frame (window type 0) at the
 * bottom of the screen, one tile in, row 0x13, 0x1E tiles wide and 4 high.
 * The text starts at (8, 0) in it.
 */
#define MOD_MESSAGE_WIN_TYPE   0u
#define MOD_MESSAGE_WIN_X      1u
#define MOD_MESSAGE_WIN_Y      0x13u
#define MOD_MESSAGE_WIN_W      0x1Eu
#define MOD_MESSAGE_WIN_H      4u
#define MOD_MESSAGE_TEXT_X     8u
#define MOD_MESSAGE_TEXT_Y     0u

/* The width the text has: the window's pixels after the text's start. */
#define MOD_MESSAGE_TEXT_WIDTH (MOD_MESSAGE_WIN_W * 8u - MOD_MESSAGE_TEXT_X)

/* The keys that close a message and its sound
 * (SCRCMD_MSGWIN_WaitKeyWithCursor): A or B, SE 0x547. */
#define MOD_MESSAGE_KEYS       3u
#define MOD_MESSAGE_SE_NEXT    0x547u

/* The end code of the game's text (src/game/arm9/msgdata.c). */
#define MOD_MESSAGE_EOS        0xFFFFu

/* The script's yes/no (SCRCMD_MSGWIN_YesNoEventMain): 8 frames after the
 * text, the menu with its cursor on YES; the menu's answers. */
#define MOD_MESSAGE_ASK_WAIT   8u
#define MOD_MESSAGE_ASK_YES    0
#define MOD_MESSAGE_ASK_OPEN   2

/* The event's steps. */
enum {
    MOD_MESSAGE_SEQ_LOCK,       /* every model stops, the player's step ends */
    MOD_MESSAGE_SEQ_PLAYER,     /* the wait for the player's step */
    MOD_MESSAGE_SEQ_OPEN,       /* the next text into the window */
    MOD_MESSAGE_SEQ_PRINT,      /* the text prints */
    MOD_MESSAGE_SEQ_KEY,        /* A or B */
    MOD_MESSAGE_SEQ_ASK_WAIT,   /* a question: the frames before its menu */
    MOD_MESSAGE_SEQ_ASK,        /* a question: the yes/no menu */
    MOD_MESSAGE_SEQ_CLOSE       /* the window closes, the models move */
};

/* The event's work. */
typedef struct ModMessageWork {
    FieldPlainWin *plainWin;    /* the window, once it is open */
    Strbuf        *str;         /* the text the window prints */
    FieldMenuFunc *yesNo;       /* a question's menu, while it is open */
    u32            ticket;      /* the question on the screen, 0 for a text */
    u8             playerMoves; /* the player is let finish its step */
    u8             wait;        /* the frames before the question's menu */
    u8             reserved[2];
    u16            units[BW_MOD_TEXT_LAID_OUT_MAX + 1u];
} ModMessageWork;

/* ------------------------------------------------------------------ */
/* The event                                                            */
/* ------------------------------------------------------------------ */

int EVENT_MOD_MESSAGE_Main(GameEvent *event, int *seq, void *workPtr)
{
    ModMessageWork *work = (ModMessageWork *)workPtr;
    FieldMap *fieldMap = (FieldMap *)GAMESYSTEM_GetFieldMap(GAME_EVENT_GetGameSystem(event));
    MmdlSys *mmdlSys = FIELDMAP_GetMmdlSys(fieldMap);
    Mmdl *player = FLDMMDL_SysSearchPlayer(mmdlSys);
    FieldMsgBg *msgBg;
    u32 count;

    switch (*seq) {
    case MOD_MESSAGE_SEQ_LOCK:
        FLDMMDL_SysPauseMove(mmdlSys);
        if (player != NULL && FLDMMDL_CheckEndAcmd(player) == 0) {
            FLDMMDL_OffMoveBitMovePause(player);
            work->playerMoves = 1;
        }
        *seq = MOD_MESSAGE_SEQ_PLAYER;
        break;
    case MOD_MESSAGE_SEQ_PLAYER:
        if (work->playerMoves != 0) {
            if (FLDMMDL_CheckEndAcmd(player) == 0) {
                break;
            }
            FLDMMDL_OnMoveBitMovePause(player);
            work->playerMoves = 0;
        }
        *seq = MOD_MESSAGE_SEQ_OPEN;
        break;
    case MOD_MESSAGE_SEQ_OPEN:
        msgBg = FIELDMAP_GetMsgBg(fieldMap);
        count = bw_mod_text_take(FIELD_MSGBG_GetFont(msgBg), MOD_MESSAGE_TEXT_WIDTH,
                                 work->units, BW_MOD_TEXT_LAID_OUT_MAX, &work->ticket);
        if (count == 0u) {
            *seq = MOD_MESSAGE_SEQ_CLOSE;
            break;
        }
        work->units[count] = (u16)MOD_MESSAGE_EOS;
        if (work->str == NULL) {
            work->str = STRBUF_Create(BW_MOD_TEXT_LAID_OUT_MAX + 1u, msgBg->heapId);
        }
        STRBUF_SetStringN(work->str, work->units, count + 1u);
        if (work->plainWin == NULL) {
            work->plainWin = FIELD_MSGBG_CreatePlainWin(msgBg, NULL, MOD_MESSAGE_WIN_TYPE,
                                                        MOD_MESSAGE_WIN_X, MOD_MESSAGE_WIN_Y,
                                                        MOD_MESSAGE_WIN_W, MOD_MESSAGE_WIN_H);
        } else {
            FIELD_MSGBG_ClearPlainWin(work->plainWin);
        }
        FIELD_MSGBG_PlainWinPrintStream(work->plainWin, MOD_MESSAGE_TEXT_X,
                                        MOD_MESSAGE_TEXT_Y, work->str);
        *seq = MOD_MESSAGE_SEQ_PRINT;
        break;
    case MOD_MESSAGE_SEQ_PRINT:
        if (FIELD_MSGBG_PlainWinMain(work->plainWin) == 1) {
            /* a question stays on the screen and its menu opens */
            work->wait = (u8)MOD_MESSAGE_ASK_WAIT;
            *seq = work->ticket != 0u ? MOD_MESSAGE_SEQ_ASK_WAIT : MOD_MESSAGE_SEQ_KEY;
        }
        break;
    case MOD_MESSAGE_SEQ_ASK_WAIT:
        if (work->wait != 0u) {
            work->wait--;
            break;
        }
        msgBg = FIELDMAP_GetMsgBg(fieldMap);
        work->yesNo = FIELD_MSGBG_CreateYesNoMenu(msgBg, 0);
        *seq = MOD_MESSAGE_SEQ_ASK;
        break;
    case MOD_MESSAGE_SEQ_ASK: {
        int answer = FIELD_MSGBG_YesNoMenuMain(work->yesNo);

        if (answer == MOD_MESSAGE_ASK_OPEN) {
            break;
        }
        FIELD_MSGBG_DeleteMenuFunc(work->yesNo);
        work->yesNo = NULL;
        /* B is no, as in the script's own question */
        bw_mod_text_answer(work->ticket, answer == MOD_MESSAGE_ASK_YES);
        work->ticket = 0u;
        *seq = MOD_MESSAGE_SEQ_OPEN;
        break;
    }
    case MOD_MESSAGE_SEQ_KEY:
        if (KEY_GetTrg() & MOD_MESSAGE_KEYS) {
            GFL_USE_PlaySeDefaultVolume(MOD_MESSAGE_SE_NEXT);
            *seq = MOD_MESSAGE_SEQ_OPEN;
            break;
        }
        FIELD_MSGBG_PlainWinDrawCursor(work->plainWin);
        break;
    case MOD_MESSAGE_SEQ_CLOSE:
    default:
        if (work->plainWin != NULL) {
            FIELD_MSGBG_DeletePlainWin(work->plainWin);
        }
        if (work->str != NULL) {
            STRBUF_Delete(work->str);
        }
        FLDMMDL_SysResumeMove(mmdlSys);
        return 1;
    }
    return 0;
}

/*
 * The event for the waiting texts (sdk/mod_text.c starts it on a quiet
 * field with GAMESYSTEM_SetEvent).
 */
GameEvent *EVENT_MOD_MESSAGE_Create(GameSystem *gameSystem)
{
    GameEvent *event;
    ModMessageWork *work;

    event = GAME_EVENT_Create(gameSystem, NULL, EVENT_MOD_MESSAGE_Main,
                              sizeof(ModMessageWork));
    if (event == NULL) {
        return NULL;
    }
    work = (ModMessageWork *)GAME_EVENT_GetWork(event);
    work->plainWin = NULL;
    work->str = NULL;
    work->yesNo = NULL;
    work->ticket = 0u;
    work->playerMoves = 0;
    work->wait = 0;
    return event;
}

/*
 * The player stands: no step under way.  The port waits for this before a
 * text's turn, so a player who keeps walking is not stopped in the middle
 * of a step.
 */
int EVENT_MOD_MESSAGE_IsPlayerStill(GameSystem *gameSystem)
{
    FieldMap *fieldMap = (FieldMap *)GAMESYSTEM_GetFieldMap(gameSystem);
    Mmdl *player;

    if (fieldMap == NULL) {
        return 0;
    }
    player = FLDMMDL_SysSearchPlayer(FIELDMAP_GetMmdlSys(fieldMap));
    if (player == NULL) {
        return 0;
    }
    return FLDMMDL_CheckEndAcmd(player) != 0 && FLDMMDL_CheckMoveBitMove(player) == 0;
}

/* ------------------------------------------------------------------ */
/* The script commands the PC's menu uses                              */
/* ------------------------------------------------------------------ */

extern GameSystem *SCRCMD_WORK_GetGameSystem(ScrcmdWork *work);
extern ScriptWork  *SCRCMD_WORK_GetScriptWork(ScrcmdWork *work);
extern unsigned short SCRIPT_WORK_ReadWorkValue(ScriptVm *vm, ScrcmdWork *cmdWork);
extern u16  VM_ReadU16(ScriptVm *vm);

/* ------------------------------------------------------------------ */
/* The PC's storage menu                                               */
/* ------------------------------------------------------------------ */

/*
 * The storage system's menu (the PC's script, message file 314 of the
 * script texts: 14 DEPOSIT POKEMON, 15 WITHDRAW POKEMON, 16 MOVE POKEMON)
 * offers three ways into the boxes.  The one-screen layout's box screen
 * does all of it in MOVE POKEMON (the party and the box side by side, drag
 * either way), so there the other two are not listed: the menu keeps MOVE
 * POKEMON, BATTLE BOX, MOVE ITEMS and NO THANKS.  The two-screen layout is
 * the game as it is: the menu is untouched there (eidos_ui_active).  The script picks by each item's value,
 * not its place, so the rest of the menu works as before.  A menu with a
 * message file of its own (ScrcmdMenu.ownsMsgData) is never touched.
 */
#define PC_MENU_MSG_FILE    314u
#define PC_MENU_DEPOSIT     14u
#define PC_MENU_WITHDRAW    15u

extern void SCRCMD_WORK_AddMenuItem(ScrcmdWork *work, int msgId, int helpMsgId, int param,
                                    Strbuf *str, Strbuf *tmp);
extern void *SCRIPT_WORK_GetMsgBuffer(ScriptWork *work);
extern void *SCRIPT_WORK_GetTempBuffer(ScriptWork *work);
extern int eidos_ui_active(void) __attribute__((weak));

/*
 * The script command records up to the fields read here
 * (src/game/ov010/scrcmd_work.c), with the game's own field types, so the
 * host compiler lays them out as it lays out the game's at every pointer
 * width.  The copies stop at the last field read.
 */
typedef struct PcScrcmdMenu {
    u16 x;                          /* 0x00 */
    u16 y;                          /* 0x02 */
    u16 cursorPos;                  /* 0x04 */
    u8 cancelable;                  /* 0x06 */
    u8 alignRight;                  /* 0x07 */
    u16 *result;                    /* 0x08 */
    WordSet *wordSet;               /* 0x0C */
    int ownsMsgData;                /* 0x10 */
} PcScrcmdMenu;

typedef struct PcScrcmdCommon {
    ScriptWork *scriptWork;         /* 0x000 */
    GameSystem *gameSystem;         /* 0x004 */
    void *gameData;                 /* 0x008 */
    void *occupyInfo;               /* 0x00C */
    void *targetObject;             /* 0x010 */
    void *animeTasks[8];            /* 0x014 */
    PcScrcmdMenu menu;              /* 0x034 */
} PcScrcmdCommon;

typedef struct PcScrcmdParam {
    u16 scriptId;                   /* 0x00 */
    u32 callParam;                  /* 0x04 */
    u32 callMode;                   /* 0x08 */
    ScriptWork *scriptWork;         /* 0x0C */
} PcScrcmdParam;

typedef struct PcScrcmdWork {
    u16 heapId;                     /* 0x00 */
    PcScrcmdParam param;            /* 0x04 */
    MsgBank *msgData;               /* 0x14 */
    u16 msgDataId;                  /* 0x18 */
    u8 childScriptIndex;            /* 0x1A */
    void *savedLocalWork;           /* 0x1C */
    PcScrcmdCommon *common;         /* 0x20 */
} PcScrcmdWork;

BW_PORT_STATIC_ASSERT(scrcmd_msg_id, sizeof(void *) != 4 ||
                      offsetof(PcScrcmdWork, msgDataId) == 0x18u);
BW_PORT_STATIC_ASSERT(scrcmd_common, sizeof(void *) != 4 ||
                      offsetof(PcScrcmdWork, common) == 0x20u);
BW_PORT_STATIC_ASSERT(scrcmd_owns_msg, sizeof(void *) != 4 ||
                      offsetof(PcScrcmdCommon, menu.ownsMsgData) == 0x44u);

static int pc_menu_hidden(ScrcmdWork *cmdWork, u32 msgId)
{
    const PcScrcmdWork *w = (const PcScrcmdWork *)cmdWork;
    const PcScrcmdCommon *common = w->common;
    u16 file = w->msgDataId;

    if (msgId != PC_MENU_DEPOSIT && msgId != PC_MENU_WITHDRAW) {
        return 0;
    }
    if (eidos_ui_active == NULL || !eidos_ui_active()) {
        return 0;               /* the two-screen layout: the game's menu  */
    }
    if (common == NULL || common->menu.ownsMsgData != 0) {
        return 0;
    }
    return file == PC_MENU_MSG_FILE;
}

int SCRCMD_MSGWIN_CmdAddMenuItem(ScriptVm *vm, ScrcmdWork *cmdWork)
{
    u32 msgId, helpMsgId, itemValue;
    ScriptWork *scriptWork;

    scriptWork = SCRCMD_WORK_GetScriptWork(cmdWork);
    msgId = VM_ReadU16(vm);
    helpMsgId = VM_ReadU16(vm);
    itemValue = SCRIPT_WORK_ReadWorkValue(vm, cmdWork);
    if (pc_menu_hidden(cmdWork, msgId)) {
        return 0;
    }
    SCRCMD_WORK_AddMenuItem(cmdWork, (int)msgId, (int)helpMsgId, (int)itemValue,
                            (Strbuf *)SCRIPT_WORK_GetMsgBuffer(scriptWork),
                            (Strbuf *)SCRIPT_WORK_GetTempBuffer(scriptWork));
    return 0;
}
