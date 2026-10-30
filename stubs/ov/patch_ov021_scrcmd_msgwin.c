/*
 * patches: src/game/ov021/scrcmd_msgwin.c SCRCMD_MSGWIN_CmdPlainWinMsg SCRCMD_MSGWIN_CmdInfoSysWinMsg SCRCMD_MSGWIN_CmdYesNoWin SCRCMD_MSGWIN_OpenTalkWin SCRCMD_MSGWIN_CmdAddMenuItem
 *
 * symbols:
 *   SCRIPT_SYS_GetFromWork  0x021589DC  thumb  ov010
 *   SCRIPT_SYS_Main  0x0215882C  thumb  ov010
 *
 * The port's message service at the field's message window (sdk/mod_text.h;
 * sdk/mod_message.h; docs/mod-services.md, "Messages").
 *
 * What this file owns
 * -------------------
 *
 * The port shows a mod's text in the game's own windows, in the game's own
 * font, at the points where the game would accept a message (sdk/mod_text.h,
 * "The show step").  The field's message windows are overlay 21's
 * (src/game/ov021/field_msgbg.c), and the ARM9 cannot call into an overlay,
 * so the side that has the window open is the side that hands it over:
 * this file.
 *
 * Four commands of the unit stand in for the game's own here:
 *
 *   SCRCMD_MSGWIN_CmdPlainWinMsg     the plain window: the message box at
 *                                    the bottom of the field
 *   SCRCMD_MSGWIN_CmdInfoSysWinMsg   the information window: the box at
 *                                    the top of the field
 *   SCRCMD_MSGWIN_OpenTalkWin        the talk balloon
 *   SCRCMD_MSGWIN_CmdYesNoWin        the game's own yes/no window
 *
 * Each of them does what the game's did, and then hands the window the game
 * has just opened to the port (bw_mod_text_set_window) and drains the
 * port's queue (bw_mod_text_pump): the next text a mod queued is shown in
 * that window, in the game's own font, at the game's own position.
 *
 * The yes/no window and the game's advance
 * ----------------------------------------
 *
 * A question (a confirm) is asked in the field's own yes/no window: this
 * file creates it (FIELD_MSGBG_CreateYesNoMenu), waits for the player's
 * answer (FIELD_MSGBG_YesNoMenuMain) and hands the answer back to the
 * port.  The advance is the game's own step: the script system runs one
 * step, which is how the game reaches the next point where it would accept
 * a message.
 *
 * The game's own records
 * ----------------------
 *
 * The port's library has no aletheia include path, so the few records this
 * file reads are copied here, the way sdk/mod_event_field.c does: the
 * message BG (its heap, its font and its print queue), the three windows
 * (their bitmap window and their message BG) and the cursor that sits in
 * front of them.  A copy holds the same members in the same order, so the
 * host compiler gives it the same offsets the game's own unit gets; the
 * offsets the game's own header documents are the DS ones, so they are
 * checked below only where the pointers are four bytes wide.
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
typedef struct ScriptSys ScriptSys;
typedef struct GameSystem GameSystem;
typedef struct GameEvent GameEvent;
typedef struct FieldMap FieldMap;
typedef struct Strbuf Strbuf;
typedef struct MsgBank MsgBank;
typedef struct WordSet WordSet;
typedef struct PrintQue PrintQue;
typedef struct GfFont GfFont;
typedef struct BmpWin BmpWin;
typedef struct Bmp Bmp;
typedef struct FieldMsgPrint FieldMsgPrint;
typedef struct FieldMsgStream FieldMsgStream;
typedef struct FieldMenuFunc FieldMenuFunc;
typedef struct TalkMsgWinSys TalkMsgWinSys;
typedef struct VecFx32 VecFx32;

/* The wait a script command sets: the function the script system calls
 * each frame until it answers.  The unit spells its own type for it
 * (src/game/ov021/scrcmd_msgwin.c); the ARM9's own is the same pointer. */
typedef int (*ScrcmdWaitFn)(ScriptVm *vm, ScrcmdWork *cmdWork);

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
 * heap the field's objects come from, the font the field prints with and
 * the print queue the field runs.  The copy stops there: the record is
 * 0x17C bytes long and the rest of it is the field's own business.
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
BW_PORT_STATIC_ASSERT(msgbg_print_que, sizeof(void *) != 4 ||
                      offsetof(FieldMsgBg, printQue) == 0x00Cu);

/*
 * The cursor that blinks at the end of a message (the same header): it
 * sits in front of the three windows below, so their own offsets depend
 * on it.
 */
typedef struct FieldMsgCursor {
    u8   isDrawn;           /* 0x00 */
    u8   animFrame;         /* 0x01, index into the y offset table */
    u8   animWait;          /* 0x02, frames on the current animFrame */
    Bmp *bmp;               /* 0x04, the 8x8 cursor image */
} FieldMsgCursor;

/*
 * The plain window: the message box at the bottom of the field (the same
 * header).  The copy stops at the bitmap window the text goes into.
 */
typedef struct FieldPlainWin {
    FieldMsgCursor    cursor;    /* 0x00 */
    Strbuf           *strBuf;    /* 0x08, the text of the window */
    FieldMsgBg       *msgBg;     /* 0x0C, the message BG it belongs to */
    TalkMsgWinSys    *talkSys;   /* 0x10, the talk system window */
    MsgBank          *msgData;   /* 0x14, the message bank it shows */
    BmpWin           *bmpWin;    /* 0x18, the window the text goes into */
} FieldPlainWin;

BW_PORT_STATIC_ASSERT(plain_msg_bg, sizeof(void *) != 4 ||
                      offsetof(FieldPlainWin, msgBg) == 0x0Cu);
BW_PORT_STATIC_ASSERT(plain_bmp_win, sizeof(void *) != 4 ||
                      offsetof(FieldPlainWin, bmpWin) == 0x18u);

/*
 * The information window: the box at the top of the field (the same
 * header).  The copy stops at the message BG it belongs to.
 */
typedef struct FieldSysWin {
    BmpWin           *bmpWin;    /* 0x00, the window the text goes into */
    FieldMsgStream   *stream;    /* 0x04, the running print stream */
    FieldMsgPrint    *msgPrint;  /* 0x08, the print slot */
    MsgBank          *msgData;   /* 0x0C, the message bank it shows */
    Strbuf           *strBuf;    /* 0x10, the text of the window */
    FieldMsgBg       *msgBg;     /* 0x14, the message BG it belongs to */
} FieldSysWin;

BW_PORT_STATIC_ASSERT(sys_bmp_win, sizeof(void *) != 4 ||
                      offsetof(FieldSysWin, bmpWin) == 0x00u);
BW_PORT_STATIC_ASSERT(sys_msg_bg, sizeof(void *) != 4 ||
                      offsetof(FieldSysWin, msgBg) == 0x14u);

/*
 * The talk balloon (the same header).  The copy stops at the message BG
 * it belongs to; the balloon's own text window is reached through the
 * talk system (TALKMSGWIN_GetBmpWin).
 */
typedef struct FieldTalkWin {
    u8             reserved;    /* 0x00 */
    u8             index;       /* 0x01, the balloon slot (0..2) */
    s16            shakeOffset; /* 0x02, the BG y offset of the shake */
    Strbuf        *strBuf;      /* 0x04, the text of the balloon */
    TalkMsgWinSys *talkSys;     /* 0x08, the talk system window */
    FieldMsgCursor cursor;      /* 0x0C */
    FieldMsgBg    *msgBg;       /* 0x14, the message BG it belongs to */
} FieldTalkWin;

BW_PORT_STATIC_ASSERT(talk_index, sizeof(void *) != 4 ||
                      offsetof(FieldTalkWin, index) == 0x01u);
BW_PORT_STATIC_ASSERT(talk_talk_sys, sizeof(void *) != 4 ||
                      offsetof(FieldTalkWin, talkSys) == 0x08u);
BW_PORT_STATIC_ASSERT(talk_msg_bg, sizeof(void *) != 4 ||
                      offsetof(FieldTalkWin, msgBg) == 0x14u);

/* ------------------------------------------------------------------ */
/* The game's own functions                                             */
/* ------------------------------------------------------------------ */

/*
 * The game's own functions, each with the name the game's own headers
 * give it (src/game/ov021/field_msgbg.h, src/game/ov021/talkmsgwin.h,
 * src/game/ov021/fieldmap.h, src/game/arm9/gamesystem.h, the script
 * system's own headers and this unit's own).
 */

/* The message BG of the field. */
extern PrintQue *FIELD_MSGBG_GetPrintQue(FieldMsgBg *msgBg);
extern GfFont   *FIELD_MSGBG_GetFont(FieldMsgBg *msgBg);

/* The yes/no window of the field. */
extern FieldMenuFunc *FIELD_MSGBG_CreateYesNoMenu(FieldMsgBg *msgBg,
                                                  u32 cursorPos);
extern int FIELD_MSGBG_YesNoMenuMain(FieldMenuFunc *menu);
extern int FIELD_MSGBG_DeleteMenuFunc(FieldMenuFunc *menu);

/* The windows. */
extern void FIELD_MSGBG_InfoSysWinPrintStr(FieldSysWin *sysWin, u32 x, u16 y,
                                           Strbuf *str);
extern FieldTalkWin *FIELD_MSGBG_AddTalkWin(FieldMsgBg *msgBg, int index,
                                            VecFx32 *pos, Strbuf *str,
                                            u32 winType, u32 tailType);
extern BmpWin *TALKMSGWIN_GetBmpWin(TalkMsgWinSys *sys, u32 index);

/* The field map's message BG. */
extern FieldMsgBg *FIELDMAP_GetMsgBg(FieldMap *fieldMap);
extern void *GAMESYSTEM_GetFieldMap(const GameSystem *gameSystem);

/* The script work. */
extern GameSystem *SCRCMD_WORK_GetGameSystem(ScrcmdWork *work);
extern ScriptWork  *SCRCMD_WORK_GetScriptWork(ScrcmdWork *work);
extern void        *SCRCMD_WORK_GetMsgWindow(ScrcmdWork *work);
extern void         SCRCMD_WORK_SetMsgWindow(ScrcmdWork *work, void *window);
extern unsigned short SCRIPT_WORK_ReadWorkValue(ScriptVm *vm,
                                                 ScrcmdWork *cmdWork);
extern u16 *SCRIPT_WORK_ReadWorkPointer(ScriptVm *vm, ScrcmdWork *cmdWork);
extern void SCRIPT_WORK_SetCheckFlag(int flagId);
extern void SCRIPT_SYS_CallEvent(ScriptWork *scriptWork, void *task);

/* The script VM and the script system. */
extern u16  VM_ReadU16(ScriptVm *vm);
extern void VM_SetWait(ScriptVm *vm, ScrcmdWaitFn waitFn);
extern ScriptSys *SCRIPT_SYS_GetFromWork(void *scriptWork);
extern int  SCRIPT_SYS_Main(ScriptSys *sys);

/* The commands of this unit that are not taken the place of here. */
extern int  SCRCMD_MSGWIN_OpenPlainWinMsg(ScriptVm *vm, ScrcmdWork *cmdWork,
                                           int winType);
extern void SCRCMD_MSGWIN_OpenInfoSysWin(ScrcmdWork *cmdWork, u32 windowKind);
extern Strbuf *SCRCMD_MSGWIN_ExpandMessage(ScrcmdWork *cmdWork,
                                            ScriptWork *scriptWork, u32 msgId);
extern GameEvent *SCRCMD_MSGWIN_CreateYesNoEvent(GameSystem *gameSystem,
                                                 ScrcmdWork *cmdWork,
                                                 u16 *result);
extern int  SCRCMD_MSGWIN_WaitInfoSysWin(ScriptVm *vm, ScrcmdWork *cmdWork);

/* The commands this file stands in for. */
extern int  SCRCMD_MSGWIN_CmdPlainWinMsg(ScriptVm *vm, ScrcmdWork *cmdWork);
extern int  SCRCMD_MSGWIN_CmdInfoSysWinMsg(ScriptVm *vm, ScrcmdWork *cmdWork);
extern int  SCRCMD_MSGWIN_CmdYesNoWin(ScriptVm *vm, ScrcmdWork *cmdWork);
extern void SCRCMD_MSGWIN_OpenTalkWin(ScrcmdWork *cmdWork, FieldMsgBg *msgBg,
                                       int talkIndex, VecFx32 *pos, Strbuf *str,
                                       u32 winType, u32 tailType);

/* ------------------------------------------------------------------ */
/* The port's own constants                                            */
/* ------------------------------------------------------------------ */

/*
 * The script work's check flag of the talk window (the unit's own enum,
 * src/game/ov021/scrcmd_msgwin.c): the balloon is open while it is set.
 */
#define MSGWIN_FLAG_TALK_WIN 2

/*
 * How many times the yes/no window is asked before the port gives the
 * question up.  The port's own confirm waits 3600 ticks for an answer
 * (sdk/mod_message.c); the window answers on a step of the game, so it
 * waits the same number of steps.
 */
#define MSGWIN_ASK_STEPS 3600u

/* ------------------------------------------------------------------ */
/* The state                                                           */
/* ------------------------------------------------------------------ */

/* The message BG of the field: the yes/no window is made in it. */
static FieldMsgBg *g_msg_bg;

/* The script work the commands run in: the advance runs its script
 * system. */
static ScriptWork *g_script_work;

/* ------------------------------------------------------------------ */
/* The window                                                          */
/* ------------------------------------------------------------------ */

/*
 * Hands the window the game has just opened to the port and drains the
 * port's queue: the next text a mod queued is shown in that window.
 *
 * The three parts are the game's own: the print queue the field runs, so
 * the text appears with the game's own speed, the bitmap window to draw
 * in, and the font the field prints with.  The heap is the field's, and
 * the position is the one the game's own text uses.
 */
static void msgwin_show(void *bmpWin, FieldMsgBg *msgBg, u16 x, u16 y)
{
    BwModTextWindow window;

    if (bmpWin == NULL || msgBg == NULL) {
        return;
    }
    window.print_que = FIELD_MSGBG_GetPrintQue(msgBg);
    window.bmp_win = bmpWin;
    window.font = FIELD_MSGBG_GetFont(msgBg);
    window.heap = (u32)msgBg->heapId;
    window.x = x;
    window.y = y;
    g_msg_bg = msgBg;
    bw_mod_text_set_window(&window);
    (void)bw_mod_text_pump();
}

/* ------------------------------------------------------------------ */
/* The draw side                                                       */
/* ------------------------------------------------------------------ */

/*
 * The game's advance: one step of the game's own script system, which is
 * what runs the field's scripts (src/game/ov010/script_sys.c,
 * SCRIPT_SYS_Main).
 *
 * A script that waits (a message window, a menu) runs its wait function
 * in the step, so the window the game has open moves on; a script that
 * runs runs its next command in the step, and that command is the point
 * where the game would accept a message again: the pump there (in the
 * commands above) shows the port's text and asks a port's question.
 *
 * The frame itself (the input poll, the screen) belongs to the platform
 * layer (sdk/mod_text.h, "The draw side"), which an overlay cannot
 * reach; this is the part of it the field's scripts own.
 */
static void msgwin_tick(void)
{
    ScriptSys *sys;

    if (g_script_work == NULL) {
        return;
    }
    sys = SCRIPT_SYS_GetFromWork(g_script_work);
    if (sys == NULL) {
        return;
    }
    (void)SCRIPT_SYS_Main(sys);
}

/*
 * The field's yes/no window: it shows the question and waits for the
 * player's answer.  The question goes into the message window the game
 * has open, and the window itself is the field's own yes/no menu.
 *
 * The menu's answer comes from the game's own menu code, which the
 * advance feeds: the answer is 1 for yes and 0 for no.  A question
 * nobody answers in time is a "no".
 */
static int msgwin_ask(const u16 *units, u32 count)
{
    const BwModTextWindow *window;
    FieldMenuFunc *menu;
    u32 i;
    int result;

    if (g_msg_bg == NULL) {
        return 0;
    }
    window = bw_mod_text_window();
    if (window != NULL && window->bmp_win != NULL) {
        /* The question goes into the message window the game has open. */
        (void)bw_mod_text_print(window, units, count);
    }
    menu = FIELD_MSGBG_CreateYesNoMenu(g_msg_bg, 0);
    if (menu == NULL) {
        return 0;
    }
    for (i = 0; i < MSGWIN_ASK_STEPS; i++) {
        result = FIELD_MSGBG_YesNoMenuMain(menu);
        if (result != 2) {
            /* 0 is the first item of the menu (yes), 1 the second (no). */
            (void)FIELD_MSGBG_DeleteMenuFunc(menu);
            return (result == 0) ? 1 : 0;
        }
        msgwin_tick();
    }
    (void)FIELD_MSGBG_DeleteMenuFunc(menu);
    return 0;
}

/*
 * Registers the draw side with the port: the field's yes/no window and
 * the game's advance.  The port's own reset takes the draw side away
 * (the overlay that owns it is unloaded), so every command registers it
 * again.
 */
static void msgwin_setup(ScrcmdWork *cmdWork)
{
    BwModTextDraw draw;

    g_script_work = SCRCMD_WORK_GetScriptWork(cmdWork);
    draw.ask = msgwin_ask;
    draw.tick = msgwin_tick;
    bw_mod_text_set_draw(&draw);
}

/* ------------------------------------------------------------------ */
/* The commands                                                        */
/* ------------------------------------------------------------------ */

/*
 * The plain window: the message box at the bottom of the field.  The
 * game's own text goes into it at (8, 0), and the port's text goes into
 * the same place.
 */
int SCRCMD_MSGWIN_CmdPlainWinMsg(ScriptVm *vm, ScrcmdWork *cmdWork)
{
    FieldPlainWin *plainWin;
    int result;

    result = SCRCMD_MSGWIN_OpenPlainWinMsg(vm, cmdWork, 0);
    msgwin_setup(cmdWork);
    if (result != 1) {
        return result;
    }
    plainWin = (FieldPlainWin *)SCRCMD_WORK_GetMsgWindow(cmdWork);
    if (plainWin == NULL) {
        return result;
    }
    msgwin_show(plainWin->bmpWin, plainWin->msgBg, 8, 0);
    return result;
}

/*
 * The information window: the box at the top of the field.  The game's
 * own text goes into it at (0, 0).
 */
int SCRCMD_MSGWIN_CmdInfoSysWinMsg(ScriptVm *vm, ScrcmdWork *cmdWork)
{
    FieldSysWin *sysWin;
    Strbuf *str;
    ScriptWork *scriptWork;
    u32 msgId;

    scriptWork = SCRCMD_WORK_GetScriptWork(cmdWork);
    msgId = SCRIPT_WORK_ReadWorkValue(vm, cmdWork);
    SCRCMD_MSGWIN_OpenInfoSysWin(cmdWork, (u8)VM_ReadU16(vm));
    str = SCRCMD_MSGWIN_ExpandMessage(cmdWork, scriptWork, msgId);
    sysWin = (FieldSysWin *)SCRCMD_WORK_GetMsgWindow(cmdWork);
    FIELD_MSGBG_InfoSysWinPrintStr(sysWin, 0, 0, str);
    VM_SetWait(vm, SCRCMD_MSGWIN_WaitInfoSysWin);
    msgwin_setup(cmdWork);
    if (sysWin != NULL) {
        msgwin_show(sysWin->bmpWin, sysWin->msgBg, 0, 0);
    }
    return 1;
}

/*
 * The game's own yes/no window: the command opens the event that asks
 * the player.  The port's own question is asked in the same window (see
 * msgwin_ask), so the message BG of the field is remembered here.
 */
int SCRCMD_MSGWIN_CmdYesNoWin(ScriptVm *vm, ScrcmdWork *cmdWork)
{
    u16 *result;
    ScriptWork *scriptWork;
    GameSystem *gameSystem;

    result = SCRIPT_WORK_ReadWorkPointer(vm, cmdWork);
    scriptWork = SCRCMD_WORK_GetScriptWork(cmdWork);
    gameSystem = SCRCMD_WORK_GetGameSystem(cmdWork);
    SCRIPT_SYS_CallEvent(scriptWork,
                          SCRCMD_MSGWIN_CreateYesNoEvent(gameSystem, cmdWork,
                                                         result));
    msgwin_setup(cmdWork);
    g_msg_bg = FIELDMAP_GetMsgBg(
        (FieldMap *)GAMESYSTEM_GetFieldMap(gameSystem));
    return 1;
}

/*
 * The talk balloon.  The balloon's own text goes into the text window
 * the talk system holds for it; the port's text goes into the same one.
 */
void SCRCMD_MSGWIN_OpenTalkWin(ScrcmdWork *cmdWork, FieldMsgBg *msgBg,
                                int talkIndex, VecFx32 *pos, Strbuf *str,
                                u32 winType, u32 tailType)
{
    FieldTalkWin *talkWin;

    SCRCMD_WORK_SetMsgWindow(cmdWork,
                             FIELD_MSGBG_AddTalkWin(msgBg, talkIndex, pos, str,
                                                    winType, tailType));
    SCRIPT_WORK_SetCheckFlag(MSGWIN_FLAG_TALK_WIN);
    msgwin_setup(cmdWork);
    talkWin = (FieldTalkWin *)SCRCMD_WORK_GetMsgWindow(cmdWork);
    if (talkWin == NULL || talkWin->talkSys == NULL) {
        return;
    }
    msgwin_show(TALKMSGWIN_GetBmpWin(talkWin->talkSys, talkWin->index),
                talkWin->msgBg, 0, 0);
}

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
