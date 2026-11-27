/*
 * patches: src/game/ov093/btlv_core.c BTLV_CORE_StartYesNoMenu
 *
 * symbols:
 *   BTLV_SCD_OpenChoiceScreen  0x0689B5A4  thumb  ov095
 *   BTLV_INPUT_GetScreen  0x022033F0  thumb  ov094
 *   BTLV_INPUT_IsInputBlocked  0x022033CC  thumb  ov094
 *
 * The battle's yes/no choice for the one-screen interface (sdk/battle_yesno.c),
 * and whether a mod's question may open it now (BTLV_CORE_CanAskModQuestion).
 * A mod's text in a battle has its own turn in the battle's message box:
 * stubs/ov/patch_ov093_btlv_scu_message.c.
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

/* ------------------------------------------------------------------ */
/* A mod's question                                                    */
/* ------------------------------------------------------------------ */

/*
 * The command view of the touch screen (src/game/ov095/btlv_scd_types.h,
 * BtlvAuxView), copied as far as its input module: the choice screen of a
 * question is that module's.
 */
typedef int (*BtlvScdStepFn)(int *state, BtlvAuxView *view);

typedef struct BtlvScdTask {
    BtlvScdStepFn openFn;           /* 0x00 */
    BtlvScdStepFn waitFn;           /* 0x04 */
    BtlvAuxView  *view;             /* 0x08 */
    int           state;            /* 0x0C */
} BtlvScdTask;

typedef struct BtlvTargetSet {
    u8 count;                       /* 0x00 */
    u8 altCount;                    /* 0x01 */
    u8 altKind;                     /* 0x02 */
    u8 viewPos[6];                  /* 0x03 */
} BtlvTargetSet;

typedef struct BtlvInput BtlvInput;

typedef struct BtlvAuxViewHead {
    u8             pad00[8];
    void          *fontMain;        /* 0x08 */
    Strbuf        *msgBuffer;       /* 0x0C */
    BtlClient     *client;          /* 0x10 */
    void          *printQueue;      /* 0x14 */
    u8             pad18[8];
    BtlvScdTask    taskStack[4];    /* 0x20 */
    int            taskDepth;       /* 0x60 */
    u8             pad64[4];
    BtlvScdStepFn  reselectFn;      /* 0x68 */
    BtlPokeParam  *pokeParam;       /* 0x6C */
    u32           *action;          /* 0x70 */
    int            cursorIndex;     /* 0x74 */
    u8             pad78[4];
    void          *rotateSet;       /* 0x7C */
    BtlvTargetSet  targets;         /* 0x80 */
    u8             targetChosen;    /* 0x89 */
    u8             moveTargetKind;  /* 0x8A */
    u8             canBack;         /* 0x8B */
    s8             slideX;          /* 0x8C */
    s8             slideY;          /* 0x8D */
    s8             slideStep;       /* 0x8E */
    u8             inputFlags;      /* 0x8F */
    u8             viewParam;       /* 0x90 */
    u8             clientSlot;      /* 0x91 */
    u8             status;          /* 0x92 */
    u8             choiceStep;      /* 0x93 */
    int            choiceIndex;     /* 0x94 */
    u32            result;          /* 0x98 */
    struct BtlvCore *core;          /* 0x9C */
    void          *mainModule;      /* 0xA0 */
    void          *pokeCon;         /* 0xA4 */
    void          *ownParty;        /* 0xA8 */
    u16            heapId;          /* 0xAC */
    u8             padAE[2];
    BtlvInput     *input;           /* 0xB0 */
} BtlvAuxViewHead;

BW_PORT_STATIC_ASSERT(aux_targets, sizeof(void *) != 4 ||
                      offsetof(BtlvAuxViewHead, targets) == 0x80u);
BW_PORT_STATIC_ASSERT(aux_core, sizeof(void *) != 4 ||
                      offsetof(BtlvAuxViewHead, core) == 0x9Cu);
BW_PORT_STATIC_ASSERT(aux_input, sizeof(void *) != 4 ||
                      offsetof(BtlvAuxViewHead, input) == 0xB0u);

/* The input module's screens (BTLV_INPUT_SCREEN_*): none is open. */
#define BTLV_INPUT_SCREEN_CLOSE 0

extern int BTLV_INPUT_GetScreen(BtlvInput *input);
extern int BTLV_INPUT_IsInputBlocked(BtlvInput *input);

int BTLV_CORE_CanAskModQuestion(struct BtlvCore *core);

/*
 * A mod's question opens the battle's choice screen after its text
 * (stubs/ov/patch_ov093_btlv_scu_message.c), the way the game's own
 * questions do (BTL_CLIENT_StepSelectRun: the text, then
 * BTLV_CORE_StartYesNoMenu).  The game asks only where its touch screen
 * holds no menu, so the port does the same: the input module is there, no
 * screen of it is open (not the commands, the moves, the targets) and it
 * is not in the middle of a change.  Before the input module is made (the
 * battle's entry) and while a menu is up the question waits, and the
 * battle's own message goes first.
 */
int BTLV_CORE_CanAskModQuestion(struct BtlvCore *core)
{
    const BtlvAuxViewHead *aux;

    if (core == NULL || core->auxView == NULL) {
        return 0;
    }
    aux = (const BtlvAuxViewHead *)core->auxView;
    if (aux->input == NULL) {
        return 0;
    }
    return BTLV_INPUT_GetScreen(aux->input) == BTLV_INPUT_SCREEN_CLOSE &&
           BTLV_INPUT_IsInputBlocked(aux->input) == 0;
}
