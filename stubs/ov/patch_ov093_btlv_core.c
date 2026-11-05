/*
 * patches: src/game/ov093/btlv_core.c BTLV_CORE_StartPrintMessage BTLV_CORE_StartYesNoMenu
 *
 * symbols:
 *   BTLV_SCD_OpenChoiceScreen  0x0689B5A4  thumb  ov095
 *
 * The port's message service at the battle's message box (sdk/mod_text.h;
 * sdk/mod_message.h; docs/mod-services.md, "Messages").
 *
 * What this file owns
 * -------------------
 *
 * The port shows a mod's text in the game's own windows, in the game's own
 * font, at the points where the game would accept a message (sdk/mod_text.h,
 * "The show step").  The battle's message box is overlay 93's
 * (src/game/ov093/btlv_scu.c), and the ARM9 cannot call into an overlay,
 * so the side that has the box open is the side that hands it over: this
 * file.
 *
 * BTLV_CORE_StartPrintMessage is the point where the battle starts a
 * message: the command that shows it builds the text into the battle's
 * own buffer and hands it here, and here the battle's message box is
 * opened for it (BTLV_SCU_StartMessage).  The port's part comes after
 * that: the box is handed to the port (bw_mod_text_set_window) and the
 * port's queue is drained (bw_mod_text_pump), so the next text a mod
 * queued is shown in the battle's message box, in the battle's own font.
 *
 * A battle has no yes/no window of its own, so nothing is registered for
 * the draw side here: a confirm is a field's business (the port answers
 * it with a "no" at once in a battle, sdk/mod_message.c).
 *
 * The game's own records
 * ----------------------
 *
 * The port's library has no aletheia include path, so the few records this
 * file reads are copied here, the way sdk/mod_event_field.c does: the
 * battle's core (its heap and its scene unit) and the scene unit itself
 * (its message box, its print queue and its font).  A copy holds the same
 * members in the same order, so the host compiler gives it the same
 * offsets the game's own unit gets; the offsets the game's own header
 * documents are the DS ones, so they are checked below only where the
 * pointers are four bytes wide.
 */

#include "mod_text.h"

#include <stddef.h>

/* ------------------------------------------------------------------ */
/* The game's own types                                                */
/* ------------------------------------------------------------------ */

/*
 * The game's own records, named only as the opaque types its accessors
 * take, the way sdk/mod_event_field.c does.
 */
typedef struct Strbuf Strbuf;
typedef struct BmpWin BmpWin;
typedef struct Bmp Bmp;
typedef struct CharBuf CharBuf;
typedef struct PrintSys PrintSys;
typedef struct TextPrinter TextPrinter;
typedef struct GfFont GfFont;
typedef struct BtlContext BtlContext;
typedef struct BtlClient BtlClient;
typedef struct BtlMonTable BtlMonTable;
typedef struct BtlPokeParam BtlPokeParam;
typedef struct BtlPickList BtlPickList;
typedef struct BtlvAuxView BtlvAuxView;
typedef struct MyItemSave MyItemSave;
typedef struct BtlvScu BtlvScu;

/* The battle's core (src/game/ov093/btlv_core_types.h names it the same
 * way). */
struct BtlvCore;

/* The per-character callback of the game's print stream. */
typedef u8 (*PrintCallback)(u32 arg);

/* The port's own check, in the shape sdk/mod_event_field.c uses: a
 * condition that must hold at every pointer width the port is built for. */
#define BW_PORT_STATIC_ASSERT(name, cond) \
    typedef char bw_port_static_assert_##name[(cond) ? 1 : -1]

/* ------------------------------------------------------------------ */
/* The game's own records, copied                                      */
/* ------------------------------------------------------------------ */

/*
 * The step function of a battle procedure (src/game/ov093/
 * btlv_core_types.h).
 */
typedef int (*BtlvCoreProc)(int *state, struct BtlvCore *core);
typedef int (*BtlvCoreStepFn)(struct BtlvCore *core, int *state, void *work);

/*
 * The request slot of the core (the same header): a pre procedure that
 * runs first and clears itself, then a main procedure, both stepped with
 * the same state counter.
 */
typedef struct BtlvCoreTask {
    BtlvCoreProc     preProc;     /* 0x00 */
    BtlvCoreProc     mainProc;    /* 0x04 */
    struct BtlvCore *procCore;    /* 0x08 */
    int              procState;   /* 0x0C */
    u8               work[0x80];  /* 0x10 */
} BtlvCoreTask;

/*
 * The bag screen's work (the same header), embedded in the core.
 */
typedef struct BtlvBagWork {
    void        *myItem;           /* 0x00, the player's bag */
    void        *fontMain;         /* 0x04 */
    int          isComm;           /* 0x08 */
    u16          heapId;           /* 0x0C */
    u8           reserved0E[2];
    void        *regulationRules;  /* 0x10 */
    u8           pointCounter;     /* 0x14 */
    u8           lastItem;         /* 0x15 */
    u16          selectedItem;     /* 0x16 */
    u8           selectedItemCost; /* 0x18 */
    u8           done;             /* 0x19 */
    u8           reserved1A[2];
    MyItemSave  *myItemSave;       /* 0x1C */
    int          skipPartyPrompt;  /* 0x20 */
    int          cursorPos;        /* 0x24 */
    u8           buttonMode;       /* 0x28 */
    u8           isWildBattle;     /* 0x29 */
    u8           selectedItemPage; /* 0x2A */
    u8           reserved2B;
    int          auxViewHandle;    /* 0x2C */
    s16          pocketCursorPos[4]; /* 0x30 */
    s16          pocketScrollPos[4]; /* 0x38 */
    int          allowBag;         /* 0x40 */
} BtlvBagWork;

/*
 * The party and move list screen's work (the same header), embedded in
 * the core.
 */
typedef struct BtlvPlistWork {
    void        *gameData;       /* 0x00 */
    void        *party;          /* 0x04 */
    void        *partnerParty;   /* 0x08 */
    void        *fontMain;       /* 0x0C */
    u16          heapId;         /* 0x10 */
    u8           reserved12[2];
    u32          rule;           /* 0x14 */
    int          isCommMulti;    /* 0x18 */
    u8           isSecondTeam;   /* 0x1C */
    u8           chosenSlots[2]; /* 0x1D */
    u8           mode;           /* 0x1F */
    u8           modeSlot;       /* 0x20 */
    u8           slot;           /* 0x21 */
    u8           positionsPerClient; /* 0x22 */
    u8           reserved23;
    u16          itemId;         /* 0x24 */
    u16          modeParam;      /* 0x26 */
    void        *effectSys;      /* 0x28 */
    void        *effectSysAux;   /* 0x2C */
    int          choiceLocked;   /* 0x30 */
    int          exitFlag;       /* 0x34 */
    u8           sickFlags[6];   /* 0x38 */
    u8           reserved3E[2];
    int          allowBag;       /* 0x40 */
    int          auxViewHandle;  /* 0x44 */
    u8           pickedSlots[3]; /* 0x48 */
    u8           result;         /* 0x4B */
    u8           done;           /* 0x4C */
    u8           reserved4D[3];
} BtlvPlistWork;

/*
 * The counter display's request block (the same header).
 */
typedef struct BtlvCounterCmd {
    u32 current;       /* 0x00 */
    u32 target;        /* 0x04 */
    u32 commandWord;   /* 0x08 */
    u32 mode;          /* 0x0C */
} BtlvCounterCmd;

/*
 * The battle's core (the same header).  The copy holds the whole record:
 * the scene unit and the heap are at its end, and the records embedded in
 * it decide where they are.
 */
struct BtlvCore {
    BtlContext       *context;      /* 0x000 */
    BtlClient        *client;       /* 0x004 */
    BtlMonTable      *monTable;     /* 0x008 */
    u8                clientIndex;  /* 0x00C */
    u8                reserved0D[3];
    int               state;        /* 0x010 */
    BtlvCoreStepFn    stepFn;       /* 0x014, the current menu step */
    int               stepState;    /* 0x018 */
    BtlvCoreTask      task;         /* 0x01C */
    Strbuf           *msgBuffer;    /* 0x0AC */
    void             *fontMain;     /* 0x0B0 */
    void             *fontSub;      /* 0x0B4 */
    u8               *actionCursor; /* 0x0B8 */
    BtlPokeParam     *pokeParam;    /* 0x0BC */
    u32               pokeId;       /* 0x0C0 */
    int               actionResult; /* 0x0C4 */
    BtlvBagWork       bag;          /* 0x0C8 */
    BtlvPlistWork     plist;        /* 0x10C */
    BtlPickList      *choiceFlags;  /* 0x15C */
    BtlvCounterCmd    counterCmd;   /* 0x160 */
    u32              *entry;        /* 0x170 */
    u8                subStep;      /* 0x174 */
    u8                canBack;      /* 0x175 */
    u8                reserved176[2];
    void             *taskList;     /* 0x178 */
    BtlvScu          *sceneUnit;    /* 0x17C */
    BtlvAuxView      *auxView;      /* 0x180 */
    u16               heapId;       /* 0x184 */
    u8                reserved186[2];
};

BW_PORT_STATIC_ASSERT(core_task, sizeof(void *) != 4 ||
                      offsetof(struct BtlvCore, task) == 0x01Cu);
BW_PORT_STATIC_ASSERT(core_bag, sizeof(void *) != 4 ||
                      offsetof(struct BtlvCore, bag) == 0x0C8u);
BW_PORT_STATIC_ASSERT(core_plist, sizeof(void *) != 4 ||
                      offsetof(struct BtlvCore, plist) == 0x10Cu);
BW_PORT_STATIC_ASSERT(core_scene_unit, sizeof(void *) != 4 ||
                      offsetof(struct BtlvCore, sceneUnit) == 0x17Cu);
BW_PORT_STATIC_ASSERT(core_heap, sizeof(void *) != 4 ||
                      offsetof(struct BtlvCore, heapId) == 0x184u);

/*
 * The battle's scene unit (src/game/ov093/btlv_scu_types.h): the message
 * box, the print queue and the font the battle prints its messages with.
 * The copy stops at the font: the rest of the record is the battle's own
 * business.
 */
struct BtlvScu {
    BmpWin *msgWin;        /* 0x000, the battle's message box */
    CharBuf *msgWinChars;  /* 0x004, its bitmap */
    BmpWin *nameWin;       /* 0x008 */
    CharBuf *nameWinChars; /* 0x00C */
    u32      nameTile;     /* 0x010 */
    u32      msgTile;      /* 0x014 */
    PrintSys *printer;     /* 0x018, the print queue */
    BmpWin  *msgWinCopy;   /* 0x01C */
    u8       lineIndex;    /* 0x020 */
    u8       reserved21[3];
    void    *fontMain;     /* 0x024, the font the battle prints with */
};

BW_PORT_STATIC_ASSERT(scu_msg_win, sizeof(void *) != 4 ||
                      offsetof(BtlvScu, msgWin) == 0x000u);
BW_PORT_STATIC_ASSERT(scu_printer, sizeof(void *) != 4 ||
                      offsetof(BtlvScu, printer) == 0x018u);
BW_PORT_STATIC_ASSERT(scu_font, sizeof(void *) != 4 ||
                      offsetof(BtlvScu, fontMain) == 0x024u);

/* ------------------------------------------------------------------ */
/* The game's own functions                                             */
/* ------------------------------------------------------------------ */

/*
 * The game's own functions, each with the name the game's own headers
 * give it (src/game/ov093/btl_client.h, src/game/ov093/btlv_scu.h and
 * src/game/arm9/bmp_win.h).
 */
extern u32  BTL_CLIENT_IsMainStepActive(BtlClient *client);
extern void BTLV_SCU_StartMessage(BtlvScu *scu, Strbuf *str, u16 wait,
                                   PrintCallback callback);
extern void BMP_WIN_TransferChars(BmpWin *win);

/* The command this file stands in for. */
extern void BTLV_CORE_StartPrintMessage(struct BtlvCore *core, Strbuf *buf,
                                        u16 speed, PrintCallback callback);

/* ------------------------------------------------------------------ */
/* The message box                                                     */
/* ------------------------------------------------------------------ */

/*
 * The battle's message box: the port's text goes into the same window
 * the battle prints its own messages into.
 *
 * The three parts are the game's own: the print queue the battle made
 * (src/game/ov093/btlv_scu.c, BTLV_SCU_Create), the bitmap window to
 * draw in, and the font the battle prints with.  The heap is the
 * battle's, and the position is the one the battle's own text uses.
 *
 * Returns 0 when the battle has no message box open.
 */
static int btlv_message_box(struct BtlvCore *core, BwModTextWindow *window)
{
    BtlvScu *scu;

    scu = core->sceneUnit;
    if (scu == NULL || scu->msgWin == NULL) {
        return 0;
    }
    window->print_que = scu->printer;
    window->bmp_win = scu->msgWin;
    window->font = scu->fontMain;
    window->heap = (u32)core->heapId;
    window->x = 0;
    window->y = 0;
    return 1;
}

/* ------------------------------------------------------------------ */
/* The command                                                         */
/* ------------------------------------------------------------------ */

/*
 * The battle starts a message: the box is opened for the battle's own
 * text, and then the port's queue is drained into the same box.
 */
void BTLV_CORE_StartPrintMessage(struct BtlvCore *core, Strbuf *buf,
                                 u16 speed, PrintCallback callback)
{
    BwModTextWindow window;

    if (BTL_CLIENT_IsMainStepActive(core->client) == 0) {
        BTLV_SCU_StartMessage(core->sceneUnit, buf, speed, callback);
    }
    if (!btlv_message_box(core, &window)) {
        return;
    }
    bw_mod_text_set_window(&window);
    if (bw_mod_text_pump() != 0) {
        /*
         * The port's text is in the box's own bitmap: the battle's box
         * draws its characters itself, so they are put on the screen
         * here.
         */
        BMP_WIN_TransferChars((BmpWin *)window.bmp_win);
    }
}

/* ------------------------------------------------------------------ */
/* The yes / no answers                                                */
/* ------------------------------------------------------------------ */

/*
 * The choice's task work (src/game/ov093/btlv_core_types.h): the two
 * answers and the mode.
 */
typedef struct BtlvYesNoWork {
    void *yesText;
    void *noText;
    u32   mode;
} BtlvYesNoWork;

struct BtlvMessage;

extern void   *BTLV_CORE_GetTaskWork(struct BtlvCore *core, int size);
extern Strbuf *STRBUF_Create(u32 capacity, u32 heapId);
extern void    BTLV_CORE_BuildMessage(struct BtlvMessage *msg, Strbuf *buf);

extern void    BTLV_SCD_OpenChoiceScreen(BtlvAuxView *view, BtlvYesNoWork *value);
extern u32     GFL_HEAP_Free(void *block);

/* The port's own (sdk/battle_yesno.c). */
extern int   bw_battle_yesno_on(void);
extern void *bw_battle_yesno_keep(int which, const void *record);

extern void BTLV_CORE_StartYesNoMenu(struct BtlvCore *core, struct BtlvMessage *yesMsg,
                                     struct BtlvMessage *noMsg, u32 mode);

/*
 * The game frees the two answers as soon as the choice screen has drawn
 * them, and the one-screen interface reads them while the choice is open
 * (engine/ui/recipe_battle.c, from this task work).  With the hook on, the
 * task work points at the port's own copies instead of the freed strings;
 * the game itself reads neither after the screen opened.
 */
void BTLV_CORE_StartYesNoMenu(struct BtlvCore *core, struct BtlvMessage *yesMsg,
                              struct BtlvMessage *noMsg, u32 mode)
{
    BtlvYesNoWork *work = BTLV_CORE_GetTaskWork(core, 12);

    work->yesText = STRBUF_Create(0x20, (u16)((core->heapId & 0x7fff) | 0x8000));
    work->noText = STRBUF_Create(0x20, (u16)((core->heapId & 0x7fff) | 0x8000));
    work->mode = mode;
    BTLV_CORE_BuildMessage(yesMsg, work->yesText);
    BTLV_CORE_BuildMessage(noMsg, work->noText);
    BTLV_SCD_OpenChoiceScreen(core->auxView, work);
    if (bw_battle_yesno_on()) {
        void *yes = bw_battle_yesno_keep(0, work->yesText);
        void *no = bw_battle_yesno_keep(1, work->noText);

        GFL_HEAP_Free(work->yesText);
        GFL_HEAP_Free(work->noText);
        work->yesText = yes;
        work->noText = no;
        return;
    }
    GFL_HEAP_Free(work->yesText);
    GFL_HEAP_Free(work->noText);
}
