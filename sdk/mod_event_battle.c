/*
 * sdk/mod_event_battle.c -- the battle area's event rows: the start of the
 * battle, the three choices of the action menu, the item, the switch, the
 * escape, the move, the damage, the faint, the catch, the end of the battle
 * and the whiteout (docs/mod-events.md, section 1, "Battle";
 * docs/mod-implementation.md, section 1, "The binding table"; the payloads
 * are docs/mod-payloads.md, section 2).
 *
 * One row per stable-event binding of this area.  A row names the game
 * function the event fires from (the region and the address of
 * mod/declared_set.txt), the event, the point, the scope, the builder that
 * fills the payload from the call, the applier that writes the modifiable
 * fields back and the refusal function that produces the refused outcome.
 * The dispatch core reads the rows from the table this file publishes
 * (bw_event_bindings_battle) and registers them with bw_mod_event_bind_area.
 *
 * The rules the port keeps (docs/mod-payloads.md, section 0): the port owns
 * the payload; a field is filled from the call's arguments, from an accessor
 * call at the slot or from a context; a field the call cannot supply stays
 * 0 and says so in a comment; a field that points at memory the game frees
 * later is copied into the payload, never stored as a pointer.
 *
 * The rules the appliers of this area keep: a builder fills a field with the
 * game's own value, so a handler that writes nothing leaves the game's own
 * value in the payload and the applier writes it back unchanged; a field
 * whose zero is not a value the game uses (the damage, the level
 * adjustment) reads zero for "not changed"; and the format has no such
 * zero, because a single battle is a format a handler may ask for, so the
 * port names the format it does not know with a marker of its own.
 *
 * The game's own accessors are declared under the names src/game gives
 * them, the way sdk/mod_state.c does: the port's older address spellings
 * are redirected onto the same names by the build's gamespell header
 * (tools/gamespell.py), so one spelling links against either tree.
 */

/*
 * The overlay functions this file calls by name, for the overlay
 * link (tools/ovlink.py): each one gets its plain name, a
 * trampoline that calls the overlay resident over the address.
 *
 * symbols:
 *   BTLV_SCU_AppendMessageArg            0x021EAF50  thumb  ov093
 *   BTLV_SCU_InitMessage                 0x021EAF18  thumb  ov093
 *   BTL_CALC_GetItemParam                0x021D805C  thumb  ov093
 *   BTL_MAIN_DecodeMessageCode           0x021B8778  thumb  ov093
 *   BTL_MAIN_GetCompetitor               0x021B86B8  thumb  ov093
 *   BTL_MAIN_GetRule                     0x021B8588  thumb  ov093
 *   BTL_MAIN_GetTrainerSlotOfPoke        0x021B8D8C  thumb  ov093
 *   BTL_MAIN_IsMultiMode                 0x021B86C8  thumb  ov093
 *   BTL_MAIN_PokeIdToClientId            0x021B8D08  thumb  ov093
 *   BTL_POKEPARAM_GetMon                 0x021D5844  thumb  ov093
 *   BTL_POKEPARAM_GetPokeId              0x021D5440  thumb  ov093
 *   BTL_POKEPARAM_GetSpecies             0x021D5444  thumb  ov093
 *   BTL_POKEPARAM_GetValue               0x021D59B4  thumb  ov093
 *   BTL_POKEPARAM_IsFainted              0x021D5B68  thumb  ov093
 *   BTL_SERVER_CMD_Push                  0x021CD4B0  thumb  ov093
 *   BTL_SERVER_FLOW_GetBallCatchRate     0x021CBD48  thumb  ov093
 *   BTL_SERVER_FLOW_GetEffectiveWeather  0x021C2AB0  thumb  ov093
 *   BTL_SERVER_FLOW_GetTurnCount         0x021C8138  thumb  ov093
 *   BTL_SIDE_GetPokeAtPos                0x021B98AC  thumb  ov093
 *   BTL_SIDE_GetPokeAtPosThunk           0x021B98A4  thumb  ov093
 *   BTL_SIDE_GetPokeByIdThunk            0x021B9934  thumb  ov093
 *   BTL_SIDE_GetSlotOfClient             0x021B9AEC  thumb  ov093
 *   BTL_SLOT_GetPokeParam                0x021B9C00  thumb  ov093
 */
#include "mod_event.h"
#include "mod_context.h"
#include "mod_export.h"
#include "mod_state.h"
#include "mod_slot.h"
#include "mod_manifest.h"

#include <stddef.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* The game's own types                                                */
/* ------------------------------------------------------------------ */

/*
 * The game's structures, named only as the opaque types its accessors
 * take.  The port never looks inside them: every value comes back out
 * through another accessor call.  The tags are the game's own
 * (src/game/ov093 and its siblings), so each prototype below can be
 * matched against the corpus by name.
 */
struct GameData;
struct Party;
struct BtlMain;
struct BtlSide;
struct BtlSideSlot;
struct BtlPokeParam;
struct BtlClient;
struct BtlvCore;
struct BtlvMessage;
struct BtlPickList;
struct BtlServerFlow;
struct BtlServerCmdQueue;

/* ------------------------------------------------------------------ */
/* The game's own accessors                                            */
/* ------------------------------------------------------------------ */

/*
 * The game's own functions, each with the address the corpus gives it.
 * They are weak so that a link without the game still succeeds; the port's
 * own seam, sdk/mod_state.c, does the same.
 */
#if defined(__GNUC__) || defined(__clang__)
#define BW_PORT_WEAK __attribute__((weak))
#else
#define BW_PORT_WEAK
#endif

/* The battle record: the Pokemon of a battle. */
extern u8  BTL_POKEPARAM_GetPokeId(struct BtlPokeParam *pp)
    BW_PORT_WEAK;
extern u16 BTL_POKEPARAM_GetSpecies(struct BtlPokeParam *pp)
    BW_PORT_WEAK;                                        /* 0x021D5444 */
extern int BTL_POKEPARAM_GetValue(struct BtlPokeParam *pp, int valueId)
    BW_PORT_WEAK;                                        /* 0x021D59B4 */
extern int BTL_POKEPARAM_IsFainted(struct BtlPokeParam *pp)
    BW_PORT_WEAK;                                        /* 0x021D5B68 */
extern struct PokemonParam *BTL_POKEPARAM_GetMon(struct BtlPokeParam *pp)
    BW_PORT_WEAK;

/* The battle's sides and their slots. */
extern u8 *BTL_SIDE_GetPokeAtPos(struct BtlMain *const *side, u8 position)
    BW_PORT_WEAK;                                        /* 0x021B98AC */
extern struct BtlPokeParam *BTL_SIDE_GetPokeAtPosThunk(struct BtlSide *side,
                                                       u8 position)
    BW_PORT_WEAK;
extern struct BtlSideSlot *BTL_SIDE_GetSlotOfClient(struct BtlSide *side,
                                                     int clientId)
    BW_PORT_WEAK;
extern struct BtlPokeParam *BTL_SLOT_GetPokeParam(struct BtlSideSlot *slot,
                                                  u32 index)
    BW_PORT_WEAK;                                        /* 0x021B9C00 */

/* The battle's main module. */
extern u32 BTL_MAIN_GetCompetitor(struct BtlMain *main)
    BW_PORT_WEAK;                                        /* 0x021B86B8 */
extern u32 BTL_MAIN_GetRule(struct BtlMain *main)
    BW_PORT_WEAK;
extern int BTL_MAIN_IsMultiMode(struct BtlMain *main)
    BW_PORT_WEAK;
extern u8  BTL_MAIN_PokeIdToClientId(u32 pokeId)
    BW_PORT_WEAK;
extern u8  BTL_MAIN_GetTrainerSlotOfPoke(struct BtlMain *main,
                                          struct BtlSide *side, u32 pokeId)
    BW_PORT_WEAK;
extern u8  BTL_MAIN_DecodeMessageCode(struct BtlMain *main, int code, u8 *out)
    BW_PORT_WEAK;                                        /* 0x021B8778 */

/* The battle server's flow. */
extern u16 BTL_SERVER_FLOW_GetTurnCount(void *flow)
    BW_PORT_WEAK;
extern int BTL_SERVER_FLOW_GetEffectiveWeather(struct BtlServerFlow *flow)
    BW_PORT_WEAK;

/*
 * The catch rate the roll uses.  The game computes it when it rolls the
 * catch: BTL_SERVER_FLOW_CalcCaptureShakes asks this function for the
 * ball's factor and multiplies the target's own chance with it.
 */
extern int BTL_SERVER_FLOW_GetBallCatchRate(struct BtlServerFlow *flow,
                                            void *unused,
                                            struct BtlPokeParam *target,
                                            int ballKind)
    BW_PORT_WEAK;                                        /* 0x021CBD48 */

/* The battle command queue: the refusal of a catch pushes a command. */
extern void BTL_SERVER_CMD_Push(void *queue, u32 id, ...)
    BW_PORT_WEAK;

/* The items and the moves. */
extern u32 BTL_CALC_GetItemParam(u32 itemId, u32 paramId)
    BW_PORT_WEAK;
extern u32 POKE_TOOL_IsShiny(struct PokemonParam *pp)
    BW_PORT_WEAK;                                        /* 0x02017EAC */

/* The party, and a Pokemon's own fields. */
extern u32 POKEPARTY_GetCount(const struct Party *party)
    BW_PORT_WEAK;                                        /* 0x0201AA18 */
extern struct PokemonParam *POKEPARTY_GetMember(struct Party *party, u32 pos)
    BW_PORT_WEAK;                                        /* 0x0201AB28 */
extern u32 POKE_TOOL_GetPartyValue(struct PokemonParam *mon, u32 paramId,
                                   u32 arg)
    BW_PORT_WEAK;                                        /* 0x02017E1C */
extern void POKE_TOOL_SetLevelAndRecalc(struct PokemonParam *mon, u32 level)
    BW_PORT_WEAK;                                        /* 0x02019CA0 */
extern void POKE_TOOL_SetPartyValue(struct PokemonParam *mon, u32 paramId,
                                    u32 value) BW_PORT_WEAK;
extern void POKE_TOOL_SetMoveSlot(struct PokemonParam *mon, u16 move, u32 slot)
    BW_PORT_WEAK;
extern u32 POKEPARTY_RemoveMember(struct Party *party, s32 pos) BW_PORT_WEAK;

/* The battle view's messages. */
extern void BTLV_SCU_InitMessage(struct BtlvMessage *msg, int kind, int id)
    BW_PORT_WEAK;                                        /* 0x021EAF18 */
extern void BTLV_SCU_AppendMessageArg(struct BtlvMessage *msg, u32 val)
    BW_PORT_WEAK;

/* ------------------------------------------------------------------ */
/* The port's own views of the game's work blocks                     */
/* ------------------------------------------------------------------ */

/*
 * The port's own check, in the shape sdk/os_thread.c uses: a condition that
 * must hold at every pointer width the port is built for.  The name is the
 * port's own, so a header that later grows one does not clash with it.
 */
#define BW_PORT_STATIC_ASSERT(name, cond) \
    typedef char bw_port_static_assert_##name[(cond) ? 1 : -1]

/*
 * The battle server's flow: the port needs the battle's main module, the
 * poke container and the turn count.  The members are copied from the
 * game's own header (aletheia src/game/ov093/btl_server_flow_types.h); the
 * game hands the flow to the bound function and to nothing else, and it
 * publishes no accessor for the main module, so the port's only way to
 * reach them is a view of the block.
 *
 * The copy is what makes the read correct at either pointer width: the
 * game's ov093 code is compiled by the same host compiler as this file, so
 * the same member types in the same order give the same offsets on both.
 * The offsets the game's own header documents are the DS ones, so they are
 * checked below only where the pointers are four bytes wide.
 */
typedef struct BwPortFlowHead {
    void *server;      /* 0x000: the server that owns the flow */
    void *mainModule;  /* 0x004: the battle's main module */
    void *pokeCon;     /* 0x008: the container of the battle's Pokemon */
    void *cmdQueue;    /* 0x00C: the queue the flow writes its commands to */
    u32   turnCount;   /* 0x010 */
    u32   flowResult;  /* 0x014 */
    int   bagMode;     /* 0x018 */
} BwPortFlowHead;

BW_PORT_STATIC_ASSERT(flow_main_module, sizeof(void *) != 4 ||
                      offsetof(BwPortFlowHead, mainModule) == 0x004u);
BW_PORT_STATIC_ASSERT(flow_poke_con, sizeof(void *) != 4 ||
                      offsetof(BwPortFlowHead, pokeCon) == 0x008u);
BW_PORT_STATIC_ASSERT(flow_cmd_queue, sizeof(void *) != 4 ||
                      offsetof(BwPortFlowHead, cmdQueue) == 0x00Cu);
BW_PORT_STATIC_ASSERT(flow_turn_count, sizeof(void *) != 4 ||
                      offsetof(BwPortFlowHead, turnCount) == 0x010u);
BW_PORT_STATIC_ASSERT(flow_size, sizeof(BwPortFlowHead) ==
                      (sizeof(void *) == 4 ? 0x01Cu : 0x030u));

/*
 * The battle client: the battle's main module, the client's own side and
 * the Pokemon the client acts for (aletheia src/game/ov093/
 * btl_client_types.h).
 *
 * The offsets the game's own header documents are the DS ones, so they are
 * checked below only where the pointers are four bytes wide.
 */
typedef struct BwPortClientHead {
    void *main;       /* 0x000: the battle's main module */
    void *side;       /* 0x004: the client's own side */
    void *activeMon;  /* 0x008: the Pokemon the client acts for */
} BwPortClientHead;

BW_PORT_STATIC_ASSERT(client_main, sizeof(void *) != 4 ||
                      offsetof(BwPortClientHead, main) == 0x000u);
BW_PORT_STATIC_ASSERT(client_side, sizeof(void *) != 4 ||
                      offsetof(BwPortClientHead, side) == 0x004u);
BW_PORT_STATIC_ASSERT(client_active_mon, sizeof(void *) != 4 ||
                      offsetof(BwPortClientHead, activeMon) == 0x008u);
BW_PORT_STATIC_ASSERT(client_size, sizeof(BwPortClientHead) ==
                      (sizeof(void *) == 4 ? 0x00Cu : 0x018u));

/*
 * The battle view core: the battle's main module and the client that owns
 * the view (aletheia src/game/ov093/btlv_core_types.h).  The game hands the
 * core to the bound function and to nothing else.
 *
 * The offsets the game's own header documents are the DS ones, so they are
 * checked below only where the pointers are four bytes wide.
 */
typedef struct BwPortCoreHead {
    void *context;   /* 0x000: the battle's main module */
    void *client;    /* 0x004: the client that owns the view */
    void *monTable;  /* 0x008: the battle's sides */
} BwPortCoreHead;

BW_PORT_STATIC_ASSERT(core_context, sizeof(void *) != 4 ||
                      offsetof(BwPortCoreHead, context) == 0x000u);
BW_PORT_STATIC_ASSERT(core_client, sizeof(void *) != 4 ||
                      offsetof(BwPortCoreHead, client) == 0x004u);
BW_PORT_STATIC_ASSERT(core_mon_table, sizeof(void *) != 4 ||
                      offsetof(BwPortCoreHead, monTable) == 0x008u);
BW_PORT_STATIC_ASSERT(core_size, sizeof(BwPortCoreHead) ==
                      (sizeof(void *) == 4 ? 0x00Cu : 0x018u));

/*
 * The battle's main module: the battle setup and the two sides (aletheia
 * src/game/ov093/btl_main_types.h).  The port needs the sides to name the
 * battle record of a side and a position, and the trainer slots to name
 * the opposing trainers.
 *
 * The offsets the game's own header documents are the DS ones, so they are
 * checked below only where the pointers are four bytes wide.
 */
typedef struct BwPortSideSlot {
    void *items[6];   /* 0x00: the Pokemon of the slot */
    u8    count;      /* 0x18 */
    u8    pad19[3];   /* 0x19 */
} BwPortSideSlot;

typedef struct BwPortSide {
    void            *main;        /* 0x00: the battle's main module */
    BwPortSideSlot   slots[4];    /* 0x04: one slot list per client */
    void            *parties[4];  /* 0x74 */
    void            *pokeParams[24]; /* 0x84 */
    u32              sideIndex;   /* 0xE4 */
} BwPortSide;

typedef struct BwPortTrainerSlot {
    void *handle;      /* 0x00 */
    void *name;        /* 0x04 */
    u16   trainerType; /* 0x08 */
    u16   trainerId;   /* 0x0A */
    int   aiFlags;     /* 0x0C */
    u16   itemIds[4];  /* 0x10 */
    u8    winWord[8];  /* 0x18 */
    u8    loseWord[8]; /* 0x20 */
} BwPortTrainerSlot;

typedef struct BwPortMain {
    void                *setup;      /* 0x000: the battle setup */
    void                *viewCore;   /* 0x004 */
    void                *server;     /* 0x008 */
    void                *subServer;  /* 0x00C */
    void                *clients[4]; /* 0x010 */
    BwPortTrainerSlot    trainers[4];/* 0x020: one per client */
    void                *playerStatus; /* 0x0C0 */
    u8                   clientReady[4]; /* 0x0C4 */
    BwPortSide           sides[2];   /* 0x0C8 */
} BwPortMain;

BW_PORT_STATIC_ASSERT(main_sides, sizeof(void *) != 4 ||
                      offsetof(BwPortMain, sides) == 0x0C8u);
BW_PORT_STATIC_ASSERT(main_trainers, sizeof(void *) != 4 ||
                      offsetof(BwPortMain, trainers) == 0x020u);
BW_PORT_STATIC_ASSERT(main_clients, sizeof(void *) != 4 ||
                      offsetof(BwPortMain, clients) == 0x010u);
BW_PORT_STATIC_ASSERT(main_player_status, sizeof(void *) != 4 ||
                      offsetof(BwPortMain, playerStatus) == 0x0C0u);
BW_PORT_STATIC_ASSERT(main_size, sizeof(BwPortMain) ==
                      (sizeof(void *) == 4 ? 0x298u : 0x4B0u));
BW_PORT_STATIC_ASSERT(side_main, sizeof(void *) != 4 ||
                      offsetof(BwPortSide, main) == 0x000u);
BW_PORT_STATIC_ASSERT(side_slots, sizeof(void *) != 4 ||
                      offsetof(BwPortSide, slots) == 0x004u);
BW_PORT_STATIC_ASSERT(side_parties, sizeof(void *) != 4 ||
                      offsetof(BwPortSide, parties) == 0x074u);
BW_PORT_STATIC_ASSERT(side_poke_params, sizeof(void *) != 4 ||
                      offsetof(BwPortSide, pokeParams) == 0x084u);
BW_PORT_STATIC_ASSERT(side_index, sizeof(void *) != 4 ||
                      offsetof(BwPortSide, sideIndex) == 0x0E4u);
BW_PORT_STATIC_ASSERT(side_size, sizeof(BwPortSide) ==
                      (sizeof(void *) == 4 ? 0x0E8u : 0x1D0u));
BW_PORT_STATIC_ASSERT(side_slot_count, sizeof(void *) != 4 ||
                      offsetof(BwPortSideSlot, count) == 0x018u);
BW_PORT_STATIC_ASSERT(side_slot_size, sizeof(BwPortSideSlot) ==
                      (sizeof(void *) == 4 ? 0x01Cu : 0x038u));
BW_PORT_STATIC_ASSERT(trainer_handle, sizeof(void *) != 4 ||
                      offsetof(BwPortTrainerSlot, handle) == 0x000u);
BW_PORT_STATIC_ASSERT(trainer_name, sizeof(void *) != 4 ||
                      offsetof(BwPortTrainerSlot, name) == 0x004u);
BW_PORT_STATIC_ASSERT(trainer_type, sizeof(void *) != 4 ||
                      offsetof(BwPortTrainerSlot, trainerType) == 0x008u);
BW_PORT_STATIC_ASSERT(trainer_id, sizeof(void *) != 4 ||
                      offsetof(BwPortTrainerSlot, trainerId) == 0x0Au);
BW_PORT_STATIC_ASSERT(trainer_ai_flags, sizeof(void *) != 4 ||
                      offsetof(BwPortTrainerSlot, aiFlags) == 0x00Cu);
BW_PORT_STATIC_ASSERT(trainer_items, sizeof(void *) != 4 ||
                      offsetof(BwPortTrainerSlot, itemIds) == 0x010u);
BW_PORT_STATIC_ASSERT(trainer_size, sizeof(BwPortTrainerSlot) ==
                      (sizeof(void *) == 4 ? 0x028u : 0x030u));

/*
 * The battle setup, the object the game builds the whole battle from
 * (aletheia src/game/arm9/btl_setup_types.h).  The port needs the wild
 * flag, the battle rule, the trainers, the parties and the result the
 * battle ended with.
 *
 * The copy is member for member, so the offsets hold at either pointer
 * width; the ones the game's own header documents are the DS ones, so
 * they are checked below only where the pointers are four bytes wide.
 */
typedef struct BwPortFieldSituation {
    int  bgType;   /* 0x00 */
    int  bgAttr;   /* 0x04 */
    u8   weather;  /* 0x08 */
    u8   season;   /* 0x09 */
    u16  zoneId;   /* 0x0A */
    u8   hour;     /* 0x0C */
    u8   minute;   /* 0x0D */
} BwPortFieldSituation;

typedef struct BwPortTrainerData {
    u32  trainerId;   /* 0x00 */
    u32  trainerType; /* 0x04 */
    u32  aiFlags;     /* 0x08 */
    u16  itemIds[4];  /* 0x0C */
    void *name;       /* 0x14 */
    u8   winWord[8];  /* 0x18 */
    u8   loseWord[8]; /* 0x20 */
} BwPortTrainerData;

typedef struct BwPortSetup {
    u32                   competitor;   /* 0x000: 0 a wild battle */
    u32                   rule;         /* 0x004: the battle format */
    BwPortFieldSituation  field;        /* 0x008 */
    u16                   bgmBattle;    /* 0x018 */
    u16                   bgmWin;       /* 0x01A */
    u32                   netHandle;    /* 0x01C */
    u8                    commPos;      /* 0x020 */
    u8                    netId;        /* 0x021 */
    u8                    multiMode;    /* 0x022 */
    u8                    ownsSlots;    /* 0x023 */
    void                 *party[4];     /* 0x024 */
    void                 *playerStatus[4]; /* 0x034 */
    u8                    voiceFlags[4];   /* 0x044 */
    void                 *trainerData[4];  /* 0x048 */
    void                 *gameData;     /* 0x058 */
    void                 *config;       /* 0x05C */
    void                 *myItem;       /* 0x060 */
    u32                   mapMatrix;    /* 0x064 */
    void                 *zukanWork;    /* 0x068 */
    void                 *trainerRecord;/* 0x06C */
    void                 *pmsWork;      /* 0x070 */
    u16                   limitTimeGame;    /* 0x074 */
    u16                   limitTimeCommand; /* 0x076 */
    u8                    regulationRules[7]; /* 0x078 */
    u8                    recordEnabled;     /* 0x07F */
    u8                    badgeCount;        /* 0x080 */
    u8                    pad81;             /* 0x081 */
    u16                   statusFlags;   /* 0x082 */
    u16                   levelCap;      /* 0x084 */
    u16                   resultCount;   /* 0x086 */
    int                   moneyDelta;    /* 0x088 */
    u32                   result;        /* 0x08C: the battle's result */
    u8                    resultValue[4];/* 0x090 */
    void                 *recordBuffer;  /* 0x094 */
    u32                   recordSize;    /* 0x098 */
    u8                    rng[24];       /* 0x09C */
    u16                   hpSum;         /* 0x0B4 */
    u8                    turnCount;     /* 0x0B6 */
} BwPortSetup;

BW_PORT_STATIC_ASSERT(setup_competitor, sizeof(void *) != 4 ||
                      offsetof(BwPortSetup, competitor) == 0x000u);
BW_PORT_STATIC_ASSERT(setup_rule, sizeof(void *) != 4 ||
                      offsetof(BwPortSetup, rule) == 0x004u);
BW_PORT_STATIC_ASSERT(setup_field, sizeof(void *) != 4 ||
                      offsetof(BwPortSetup, field) == 0x008u);
BW_PORT_STATIC_ASSERT(setup_multi_mode, sizeof(void *) != 4 ||
                      offsetof(BwPortSetup, multiMode) == 0x022u);
BW_PORT_STATIC_ASSERT(setup_party, sizeof(void *) != 4 ||
                      offsetof(BwPortSetup, party) == 0x024u);
BW_PORT_STATIC_ASSERT(setup_player_status, sizeof(void *) != 4 ||
                      offsetof(BwPortSetup, playerStatus) == 0x034u);
BW_PORT_STATIC_ASSERT(setup_trainer_data, sizeof(void *) != 4 ||
                      offsetof(BwPortSetup, trainerData) == 0x048u);
BW_PORT_STATIC_ASSERT(setup_game_data, sizeof(void *) != 4 ||
                      offsetof(BwPortSetup, gameData) == 0x058u);
BW_PORT_STATIC_ASSERT(setup_pms_work, sizeof(void *) != 4 ||
                      offsetof(BwPortSetup, pmsWork) == 0x070u);
BW_PORT_STATIC_ASSERT(setup_result, sizeof(void *) != 4 ||
                      offsetof(BwPortSetup, result) == 0x08Cu);
BW_PORT_STATIC_ASSERT(setup_turn_count, sizeof(void *) != 4 ||
                      offsetof(BwPortSetup, turnCount) == 0x0B6u);
BW_PORT_STATIC_ASSERT(setup_size, sizeof(BwPortSetup) ==
                      (sizeof(void *) == 4 ? 0x0B8u : 0x110u));
/*
 * The field situation and the trainer data hold no pointer of their own
 * (the trainer data's name is one), so the situation's layout is the
 * game's at every pointer width.
 */
BW_PORT_STATIC_ASSERT(setup_field_size, sizeof(BwPortFieldSituation) == 0x10u);
BW_PORT_STATIC_ASSERT(setup_field_zone_id,
                      offsetof(BwPortFieldSituation, zoneId) == 0x0Au);
BW_PORT_STATIC_ASSERT(setup_trainer_id, sizeof(void *) != 4 ||
                      offsetof(BwPortTrainerData, trainerId) == 0x000u);
BW_PORT_STATIC_ASSERT(setup_trainer_name, sizeof(void *) != 4 ||
                      offsetof(BwPortTrainerData, name) == 0x014u);
BW_PORT_STATIC_ASSERT(setup_trainer_size, sizeof(BwPortTrainerData) ==
                      (sizeof(void *) == 4 ? 0x028u : 0x030u));

/*
 * The move record the server hands to the bound functions: the move's id
 * is its first word (aletheia src/game/ov093/btl_server_flow.c, the
 * Out64B4 view).  The record holds no pointer, so its layout is the same
 * at either pointer width.
 */
typedef struct BwPortMoveRecord {
    u16 moveId;   /* 0x00: the move */
    u16 h2;       /* 0x02 */
    u16 h4;       /* 0x04 */
    u8  b6;       /* 0x06: the move's type */
    u8  pad7;     /* 0x07 */
    u32 w8;       /* 0x08 */
    u32 wC;       /* 0x0C */
    u32 w10;      /* 0x10 */
} BwPortMoveRecord;

BW_PORT_STATIC_ASSERT(move_id, offsetof(BwPortMoveRecord, moveId) == 0x00u);
BW_PORT_STATIC_ASSERT(move_type, offsetof(BwPortMoveRecord, b6) == 0x06u);
BW_PORT_STATIC_ASSERT(move_size, sizeof(BwPortMoveRecord) == 0x14u);

/*
 * The context the end of the battle carries: the game data and the battle
 * setup (aletheia src/game/ov010/event_battle.c, the BattleEndCtx view).
 *
 * The offsets the game's own header documents are the DS ones, so they are
 * checked below only where the pointers are four bytes wide.
 */
typedef struct BwPortBattleEndCtx {
    u32  unused00;    /* 0x00 */
    void *btlSetupObj;/* 0x04: the game data */
    void *btlSetup;   /* 0x08: the battle setup */
} BwPortBattleEndCtx;

BW_PORT_STATIC_ASSERT(battle_end_game_data, sizeof(void *) != 4 ||
                      offsetof(BwPortBattleEndCtx, btlSetupObj) == 0x004u);
BW_PORT_STATIC_ASSERT(battle_end_setup, sizeof(void *) != 4 ||
                      offsetof(BwPortBattleEndCtx, btlSetup) == 0x008u);
BW_PORT_STATIC_ASSERT(battle_end_size, sizeof(BwPortBattleEndCtx) ==
                      (sizeof(void *) == 4 ? 0x00Cu : 0x018u));

/* ------------------------------------------------------------------ */
/* The game's own constants                                            */
/* ------------------------------------------------------------------ */

/*
 * The battle's result codes, the word the game writes into the setup when
 * the battle ends (the game's own BtlSetup view names it the outcome at
 * +0x8C).  The port's own codes are the ones of sdk/mod_api.h, so the
 * game codes are translated (see battle_outcome_of).
 */
#define BW_PORT_RESULT_LOSS   0u
#define BW_PORT_RESULT_WIN    1u
#define BW_PORT_RESULT_DRAW   2u
#define BW_PORT_RESULT_CAUGHT 3u
#define BW_PORT_RESULT_FLED   4u

/* The poke id the game uses for "no Pokemon". */
#define BW_PORT_POKE_ID_NONE  0x1Fu

/*
 * The format the port does not know yet.  The battle's setup does not exist
 * when the event fires at BTL_SETUP_Create, so the format is not known
 * there, and a single battle (0) is a format a handler may ask for: zero
 * cannot mean "not changed" for the format, so the port names the one it
 * does not know with a value no format has.
 */
#define BW_PORT_FORMAT_UNKNOWN 0xFFu

/* A Pokemon's own field the port reads and writes (the party's record). */
#define BW_PORT_FIELD_LEVEL  0x9Eu

/* The levels a Pokemon may have. */
#define BW_PORT_LEVEL_MIN 1u
#define BW_PORT_LEVEL_MAX 100u

/*
 * The game's own catch rate of one.  The rate is a 12.12 fixed-point
 * factor (the game multiplies the target's chance with it), so 0x1000 is
 * one and the payload's field carries it as a percentage of it.
 */
#define BW_PORT_CATCH_RATE_ONE 0x1000

/* The declared row of the game's own catch rate (ov093). */
#define BW_PORT_CATCH_RATE_ADDR 0x021CBD48u

/* The item record's own fields the port reads (the game's party screen
 * names them PLIST_ITEM_REVIVES and PLIST_ITEM_REVIVES_FULL). */
#define BW_PORT_ITEM_PARAM_BALL      0x0Fu
#define BW_PORT_ITEM_PARAM_BALL_BALL 4u
#define BW_PORT_ITEM_PARAM_REVIVES      26u
#define BW_PORT_ITEM_PARAM_REVIVES_FULL 27u

/*
 * The battle message the port's refusal of a move shows: the game's own
 * "the Pokemon cannot use this move" message (the one the blocked-move
 * check of BTL_CLIENT_IsMoveSelectionBlocked writes), with the Pokemon and
 * the move as its two arguments.
 */
#define BW_PORT_MESSAGE_CANT_USE_MOVE 0x23Bu

/* ------------------------------------------------------------------ */
/* The port's own state                                                 */
/* ------------------------------------------------------------------ */

/*
 * The battle's own request: what the handlers asked for when the battle
 * started.  The event fires at BTL_SETUP_Create, where the setup the game
 * will build does not exist yet, so the port cannot write the game's own
 * work there: it keeps the request and applies it at the rows where the
 * setup is built (the trainer battle, the scripted battle and the wild
 * battle).
 *
 * The format is the one the handlers asked for, or BW_PORT_FORMAT_UNKNOWN
 * when they asked for nothing: the game's own format is not known before
 * the setup is built, and a single battle (0) is a format a handler may
 * ask for, so zero cannot mean "not changed" here.
 *
 * The level adjustment is the one the handlers asked for, and zero is no
 * adjustment, so a handler that wrote nothing leaves the game's own levels
 * alone.
 */
static struct {
    u8  format;       /* BW_FORMAT_*, or BW_PORT_FORMAT_UNKNOWN */
    s8  level_adjust; /* the level the handlers asked to add */
} g_battle_request = { BW_PORT_FORMAT_UNKNOWN, 0 };

/*
 * The catch rate the roll uses.  The game's own rate is read when the
 * event fires and kept here, so the applier can tell a handler's write
 * from a handler that wrote nothing: the payload carries the game's own
 * rate until a handler changes it.
 */
static int g_catch_rate_own;     /* the game's own rate, in percent */
static int g_catch_rate_pending; /* the rate the handlers asked for, or -1 */
static int g_catch_rate_hook;    /* the port's own hook is installed */

/* ------------------------------------------------------------------ */
/* The helpers                                                         */
/* ------------------------------------------------------------------ */

/* The battle's main module of a call that carries the server's flow. */
static struct BtlMain *main_of_flow(const BwEventCall *call)
{
    const BwPortFlowHead *flow = (const BwPortFlowHead *)call->a[0];

    if (flow == NULL) {
        return NULL;
    }
    return (struct BtlMain *)flow->mainModule;
}

/* The battle's main module of a call that carries the client. */
static struct BtlMain *main_of_client(const BwEventCall *call)
{
    const BwPortClientHead *client = (const BwPortClientHead *)call->a[0];

    if (client == NULL) {
        return NULL;
    }
    return (struct BtlMain *)client->main;
}

/* The battle's main module of a call that carries the view core. */
static struct BtlMain *main_of_core(const BwEventCall *call)
{
    const BwPortCoreHead *core = (const BwPortCoreHead *)call->a[0];

    if (core == NULL) {
        return NULL;
    }
    return (struct BtlMain *)core->context;
}

/*
 * Bind the battle's main module to the state layer, so the state calls
 * that work on a Pokemon in battle (the battle's side and its slots) can
 * find it (sdk/mod_state.h: "the port binds them when it learns them").
 * The main module is the battle task's work, which the battle server and
 * the battle client both hold.
 *
 * The game data is bound with it: the battle setup holds the game data
 * the battle runs on, and the state calls that read the party, the money
 * or the species data need it.
 */
static void bind_battle(struct BtlMain *main)
{
    const BwPortMain *view;

    if (main == NULL) {
        return;
    }
    bw_mod_state_set_battle(main);
    view = (const BwPortMain *)main;
    if (view->setup != NULL) {
        const BwPortSetup *setup = (const BwPortSetup *)view->setup;

        if (setup->gameData != NULL) {
            bw_mod_state_set_game_data(setup->gameData);
            /* The export service writes to the party, so it needs the same
             * object (sdk/mod_export.h). */
            bw_mod_export_set_game_data(setup->gameData);
        }
    }
}

/*
 * The battle format of a battle, from the game's own rule (the setup's
 * rule: 0 single, 1 double, 2 triple, 3 rotation) and the setup's multi
 * flag (a multi battle is a single screen with two trainers).
 */
static u8 format_of(struct BtlMain *main)
{
    u32 rule;

    if (main == NULL || BTL_MAIN_GetRule == NULL) {
        return (u8)BW_FORMAT_SINGLE;
    }
    rule = BTL_MAIN_GetRule(main);
    if (BTL_MAIN_IsMultiMode != NULL && BTL_MAIN_IsMultiMode(main) != 0) {
        return (u8)BW_FORMAT_MULTI;
    }
    switch (rule) {
    case 1u:
        return (u8)BW_FORMAT_DOUBLE;
    case 2u:
        return (u8)BW_FORMAT_TRIPLE;
    case 3u:
        return (u8)BW_FORMAT_ROTATION;
    case 0u:
    default:
        return (u8)BW_FORMAT_SINGLE;
    }
}

/*
 * The battle format of a setup, from the setup's own rule and its multi
 * flag: the same reading as format_of, but from the setup the port holds
 * before the battle's main module exists.
 */
static u8 format_of_setup(const BwPortSetup *setup)
{
    if (setup == NULL) {
        return (u8)BW_FORMAT_SINGLE;
    }
    if (setup->multiMode != 0u) {
        return (u8)BW_FORMAT_MULTI;
    }
    switch (setup->rule) {
    case 1u:
        return (u8)BW_FORMAT_DOUBLE;
    case 2u:
        return (u8)BW_FORMAT_TRIPLE;
    case 3u:
        return (u8)BW_FORMAT_ROTATION;
    case 0u:
    default:
        return (u8)BW_FORMAT_SINGLE;
    }
}

/*
 * Fill the battle context's wild flag and format from the battle's main
 * module.  The setup is filled by the time the battle runs, so this is
 * where the port learns whether the battle is a wild one and which format
 * it has (docs/mod-payloads.md, section 1, "The battle context").
 */
static void feed_battle_context(struct BtlMain *main)
{
    if (main == NULL || BTL_MAIN_GetCompetitor == NULL) {
        return;
    }
    bw_mod_battle_context.wild =
        (u8)(BTL_MAIN_GetCompetitor(main) == 0u ? 1u : 0u);
    bw_mod_battle_context.format = format_of(main);
}

/*
 * The battle's turn record, read at the events that hold the flow
 * (docs/mod-payloads.md, section 1: "the battle's turn record, read at
 * each battle event").
 */
static u16 turn_of(struct BtlServerFlow *flow)
{
    if (flow == NULL || BTL_SERVER_FLOW_GetTurnCount == NULL) {
        return 0u;
    }
    return BTL_SERVER_FLOW_GetTurnCount((void *)flow);
}

/*
 * The weather the battle runs in, read at the events that hold the flow.
 */
static u8 weather_of(struct BtlServerFlow *flow)
{
    if (flow == NULL || BTL_SERVER_FLOW_GetEffectiveWeather == NULL) {
        return 0u;
    }
    return (u8)BTL_SERVER_FLOW_GetEffectiveWeather(flow);
}

/*
 * The battle's turn record and its weather, read at every event that holds
 * the flow (docs/mod-payloads.md, section 1: "the battle's turn record,
 * read at each battle event" and "the battle's field situation, read at
 * each battle event").
 */
static void feed_turn_weather(struct BtlServerFlow *flow)
{
    bw_mod_battle_context.turn = turn_of(flow);
    bw_mod_battle_context.weather = weather_of(flow);
}

/*
 * A reference to a Pokemon of the battle.  The side comes from the
 * Pokemon's own client (the game's own accessor, BTL_MAIN_PokeIdToClientId)
 * and the slot from the game's own trainer slot (the position the Pokemon
 * stands at in the battle).
 *
 * A reference to no Pokemon is the empty one the port uses everywhere
 * (BW_MON_REF_NONE: a zeroed reference).
 *
 * The handle is the game's own record, as far as the ABI's room for it
 * allows (a u32).  On a host whose pointers are eight bytes the address
 * does not fit in it; the state layer (sdk/mod_state.c) owns what the
 * handle means, and the place and the slot name the Pokemon either way.
 */
static void fill_battle_ref(BwMonRef *ref, struct BtlPokeParam *record,
                             struct BtlMain *main, void *sideBlock)
{
    u32 pokeId;
    u8 clientId;

    memset(ref, 0, sizeof(*ref));
    if (record == NULL) {
        return;
    }
    ref->place = (u8)BW_MON_BATTLE;
    ref->handle = (u32)(uintptr_t)record;
    if (BTL_POKEPARAM_GetPokeId == NULL) {
        return;
    }
    pokeId = BTL_POKEPARAM_GetPokeId(record);
    if (BTL_MAIN_PokeIdToClientId != NULL) {
        clientId = BTL_MAIN_PokeIdToClientId(pokeId);
        ref->side = (u8)(clientId & 1u);
    }
    if (main != NULL && sideBlock != NULL &&
        BTL_MAIN_GetTrainerSlotOfPoke != NULL) {
        ref->slot = BTL_MAIN_GetTrainerSlotOfPoke(main,
                                                  (struct BtlSide *)sideBlock,
                                                  pokeId);
    }
}

/*
 * The trainer ids out of the battle setup, for the end of the battle
 * (the battle's main module is gone by then, but the setup is not).
 */
static void fill_setup_trainer_ids(u16 *out, const BwPortSetup *setup)
{
    u32 i;

    out[0] = 0u;
    out[1] = 0u;
    if (setup == NULL) {
        return;
    }
    for (i = 0u; i < 2u; i++) {
        const BwPortTrainerData *trainer =
            (const BwPortTrainerData *)setup->trainerData[i + 1u];

        if (trainer != NULL) {
            out[i] = trainer->trainerId;
        }
    }
}

/*
 * The battle's result, translated into the port's own outcome codes
 * (sdk/mod_api.h, BW_OUTCOME_*).  The game writes its result into the
 * setup: 0 a loss, 1 a win, 2 a battle that ended without a winner, 3 a
 * catch and 4 a wild battle the player left without catching it (the
 * port's own survey, progress/PAYLOAD_SURVEY.md, reads the word at +0x8C
 * as "0 loss, 1 win, 2 draw, 3 caught", and the game adds a trainer
 * record for the wild battle of code 4).  The port's codes name the win
 * first, so the two are translated here; a result the port does not know
 * reads 0, which is the port's win.
 */
static u8 battle_outcome_of(u32 result)
{
    switch (result) {
    case BW_PORT_RESULT_LOSS:
        return (u8)BW_OUTCOME_LOSS;
    case BW_PORT_RESULT_WIN:
        return (u8)BW_OUTCOME_WIN;
    case BW_PORT_RESULT_DRAW:
        return (u8)BW_OUTCOME_DRAW;
    case BW_PORT_RESULT_CAUGHT:
        return (u8)BW_OUTCOME_CAUGHT;
    case BW_PORT_RESULT_FLED:
        return (u8)BW_OUTCOME_FLED;
    default:
        return (u8)BW_OUTCOME_WIN;
    }
}

/*
 * The effectiveness of a hit, from the type matchup the game computes
 * (the game's own BTL_CALC_ApplyTypeAffinity: 0 no effect, 1 and 2 not
 * very effective, 3 neutral, 4 and 5 super effective).  The payload names
 * the four cases the game shows.
 */
static u8 effectiveness_of(u32 matchup)
{
    switch (matchup) {
    case 0u:
        return 0u;   /* no effect */
    case 1u:
    case 2u:
        return 1u;   /* not very effective */
    case 4u:
    case 5u:
        return 3u;   /* super effective */
    case 3u:
    default:
        return 2u;   /* neutral */
    }
}

/*
 * The Pokemon a ball is thrown at: the game's own resolution of the
 * throw (BTL_SERVER_FLOW_TryThrowBall decodes the message code of the
 * thrower's trainer slot and takes the first Pokemon of that set that has
 * not fainted).  The port runs the same steps with the game's own
 * accessors, so it names the same Pokemon the game will.
 *
 * The position of that Pokemon in the battle is the one the game pushes
 * into its command stream, so it is handed back as well.
 */
static struct BtlPokeParam *catch_target_of(struct BtlServerFlow *flow,
                                             void *thrower, u8 *outPosition)
{
    const BwPortFlowHead *head = (const BwPortFlowHead *)flow;
    struct BtlMain *main;
    u8 positions[8];
    u8 count;
    u8 i;
    u8 chosen = 6u;
    u32 pokeId;

    if (outPosition != NULL) {
        *outPosition = chosen;
    }
    if (head == NULL || thrower == NULL || BTL_MAIN_DecodeMessageCode == NULL ||
        BTL_SIDE_GetPokeAtPosThunk == NULL ||
        BTL_POKEPARAM_GetPokeId == NULL) {
        return NULL;
    }
    main = (struct BtlMain *)head->mainModule;
    if (main == NULL || BTL_MAIN_GetTrainerSlotOfPoke == NULL) {
        return NULL;
    }
    pokeId = BTL_POKEPARAM_GetPokeId((struct BtlPokeParam *)thrower);
    count = BTL_MAIN_DecodeMessageCode(
        main,
        (int)((chosen << 8) |
              BTL_MAIN_GetTrainerSlotOfPoke(main,
                                            (struct BtlSide *)head->pokeCon,
                                            pokeId)),
        positions);
    for (i = 0u; i < count; i++) {
        struct BtlPokeParam *mon =
            BTL_SIDE_GetPokeAtPosThunk((struct BtlSide *)head->pokeCon,
                                       positions[i]);

        if (mon != NULL && BTL_POKEPARAM_IsFainted != NULL &&
            BTL_POKEPARAM_IsFainted(mon) == 0) {
            chosen = positions[i];
            break;
        }
    }
    if (chosen == 6u) {
        return NULL;
    }
    if (outPosition != NULL) {
        *outPosition = chosen;
    }
    return BTL_SIDE_GetPokeAtPosThunk((struct BtlSide *)head->pokeCon, chosen);
}

/*
 * The catch rate the roll uses, in percent.  The game's own rate is a
 * 12.12 fixed-point factor and 0x1000 is one, so the percentage is the
 * factor's own.
 */
static int catch_rate_percent(int rate)
{
    if (rate <= 0) {
        return 0;
    }
    return (int)(((s64)rate * 100) / (s64)BW_PORT_CATCH_RATE_ONE);
}

/*
 * The port's own hook on the game's catch rate.  The rate is computed
 * inside the roll (BTL_SERVER_FLOW_CalcCaptureShakes asks the game's own
 * function for it), and the only way the port's own value reaches the roll
 * is the declared row of that function: the port's hook is a wrap on it,
 * so it answers with the rate the handlers asked for and with the game's
 * own rate otherwise.
 *
 * The hook asks the next link inward for the game's own rate, so a mod's
 * own hook on the same row still runs and its rate is the one the port
 * scales.
 */
static int port_catch_rate(struct BtlServerFlow *flow, void *unused,
                           struct BtlPokeParam *target, int ballKind)
{
    int (*inner)(struct BtlServerFlow *, void *, struct BtlPokeParam *, int);
    void *next;
    int own;

    next = bw_mod_slot_original(BW_MOD_REGION_OVL(93),
                                BW_PORT_CATCH_RATE_ADDR);
    inner = (int (*)(struct BtlServerFlow *, void *, struct BtlPokeParam *,
                     int))next;
    if (inner != NULL) {
        own = inner(flow, unused, target, ballKind);
    } else if (BTL_SERVER_FLOW_GetBallCatchRate != NULL) {
        own = BTL_SERVER_FLOW_GetBallCatchRate(flow, unused, target, ballKind);
    } else {
        own = BW_PORT_CATCH_RATE_ONE;
    }
    if (g_catch_rate_pending < 0) {
        return own;
    }
    return (int)(((s64)own * g_catch_rate_pending) / 100);
}

/*
 * The port's own hook is installed once, the first time a handler asks for
 * a rate of its own.  It is a wrap on the declared row, so it nests with
 * the hooks the mods install on the same row.
 */
static void install_catch_rate_hook(void)
{
    if (g_catch_rate_hook != 0) {
        return;
    }
    if (bw_mod_slot_install_mode(BW_MOD_REGION_OVL(93),
                                  BW_PORT_CATCH_RATE_ADDR,
                                  (void *)(uintptr_t)port_catch_rate, "eidos",
                                  BW_MOD_OVERRIDE_WRAP) == 0) {
        g_catch_rate_hook = 1;
    }
}

/*
 * The level the handlers asked for is added to every opposing Pokemon's
 * level.  The opposing party is the setup's own second party: the trainer's
 * team, or the wild Pokemon.  The game's own setter for a level is used, so
 * the Pokemon's experience and its stats follow the new level.
 */
static void apply_level_adjust(const BwPortSetup *setup, s8 adjust)
{
    struct Party *party;
    u32 count;
    u32 i;

    if (adjust == 0 || setup == NULL) {
        return;
    }
    party = (struct Party *)setup->party[1];
    if (party == NULL || POKEPARTY_GetCount == NULL ||
        POKEPARTY_GetMember == NULL || POKE_TOOL_GetPartyValue == NULL ||
        POKE_TOOL_SetLevelAndRecalc == NULL) {
        return;
    }
    count = POKEPARTY_GetCount(party);
    if (count > 6u) {
        count = 6u;
    }
    for (i = 0u; i < count; i++) {
        struct PokemonParam *mon = POKEPARTY_GetMember(party, i);
        s32 level;

        if (mon == NULL) {
            continue;
        }
        level = (s32)POKE_TOOL_GetPartyValue(mon, BW_PORT_FIELD_LEVEL, 0u);
        level += (s32)adjust;
        if (level < (s32)BW_PORT_LEVEL_MIN) {
            level = (s32)BW_PORT_LEVEL_MIN;
        }
        if (level > (s32)BW_PORT_LEVEL_MAX) {
            level = (s32)BW_PORT_LEVEL_MAX;
        }
        POKE_TOOL_SetLevelAndRecalc(mon, (u32)level);
    }
}

/* ------------------------------------------------------------------ */
/* The builders                                                        */
/* ------------------------------------------------------------------ */

/*
 * battle.start: the battle begins.  The call carries the heap the game
 * allocates the battle setup on and nothing else, and the game creates the
 * setup in this very function and fills it in the calls that follow, so
 * the wild flag, the format and the trainer ids are not obtainable here.
 * The port feeds them into the battle context at the first battle event
 * that carries the battle's main module, where the setup is filled.
 */
static void build_battle_start(const BwEventCall *call, BwEventPayload *out)
{
    (void)call;
    memset(&out->battle_start, 0, sizeof(out->battle_start));

    /*
     * The battle context is reset here (docs/mod-payloads.md, section 1:
     * "Reset at battle.start"), so no field of the previous battle is
     * left behind.
     */
    memset(&bw_mod_battle_context, 0, sizeof(bw_mod_battle_context));
    bw_mod_battle_context.active = 1u;

    /*
     * The format reads the one the port does not know: the setup does not
     * exist yet, so the game's own format is not known, and zero is a
     * format a handler may ask for.  The request of the last battle goes
     * with the context.
     */
    out->battle_start.format = BW_PORT_FORMAT_UNKNOWN;
    g_battle_request.format = BW_PORT_FORMAT_UNKNOWN;
    g_battle_request.level_adjust = 0;

    /*
     * The fields the port cannot fill: the setup the game will build does
     * not exist yet, so the wild flag, the trainer ids and the handle of
     * the opposing party stay 0, and the format reads the one the port
     * does not know.  The port reads the battle's own values from the
     * battle's main module at the first battle event (see
     * feed_battle_context and fill_trainer_ids).
     */
}

/*
 * battle.start: what the handlers asked for is kept until the setup is
 * built.  The setup does not exist at this row, so the port cannot write
 * the game's own work here: the request is applied at the rows where the
 * setup is built (the trainer battle, the scripted battle and the wild
 * battle).
 *
 * The format is kept only when it is a format and not the marker the port
 * uses for the one it does not know: the game's own format is not known
 * yet, so the port cannot tell a write of "single" from no write at all,
 * and it leaves the game's own format alone rather than guess it.
 */
static void apply_battle_start(const BwEventPayload *in, BwEventCall *call)
{
    (void)call;

    if (in->battle_start.format != BW_PORT_FORMAT_UNKNOWN) {
        g_battle_request.format = in->battle_start.format;
    }
    g_battle_request.level_adjust = in->battle_start.level_adjust;
}

/*
 * battle.start: the setup is built, so the payload names the battle's own
 * values: the wild flag, the format, the opposing trainers and the handle
 * of the opposing party.
 *
 * The format is the one the handlers asked for, or the game's own: the
 * setup's rule and its multi flag say which format the game built.
 */
static void build_battle_setup(const BwPortSetup *setup, BwEventPayload *out)
{
    memset(&out->battle_start, 0, sizeof(out->battle_start));
    out->battle_start.format = BW_PORT_FORMAT_UNKNOWN;

    if (setup == NULL) {
        return;
    }
    out->battle_start.wild = (u8)(setup->competitor == 0u ? 1u : 0u);
    bw_mod_state_set_battle_wild((s32)out->battle_start.wild);
    if (g_battle_request.format != BW_PORT_FORMAT_UNKNOWN) {
        out->battle_start.format = g_battle_request.format;
    } else {
        out->battle_start.format = format_of_setup(setup);
    }
    out->battle_start.level_adjust = g_battle_request.level_adjust;
    fill_setup_trainer_ids(out->battle_start.trainer_id, setup);
    bw_mod_state_set_battle_trainers(out->battle_start.trainer_id[0],
                                     out->battle_start.trainer_id[1]);

    /*
     * The handle of the opposing party is the party's own address, as far
     * as the ABI's room for it allows (a u32).  On a host whose pointers
     * are eight bytes the address does not fit in it; the state layer
     * (sdk/mod_state.c) owns what the handle means.
     */
    out->battle_start.opponent_party = (u32)(uintptr_t)setup->party[1];
}

/*
 * battle.start: the trainer battle's setup (FIELD_ENCOUNT_SetupTrainerBattle
 * carries the setup as its second argument).
 */
static void build_battle_setup_trainer(const BwEventCall *call,
                                        BwEventPayload *out)
{
    build_battle_setup((const BwPortSetup *)call->a[1], out);
}

/*
 * battle.start: the scripted battle's setup (EVENT_BATTLE_CreateScriptedBattle
 * carries the setup as its third argument).
 */
static void build_battle_setup_scripted(const BwEventCall *call,
                                         BwEventPayload *out)
{
    build_battle_setup((const BwPortSetup *)call->a[2], out);
}

/*
 * battle.start: the wild battle's setup (BTL_SETUP_SetupWild carries the
 * setup as its first argument).
 */
static void build_battle_setup_wild(const BwEventCall *call,
                                     BwEventPayload *out)
{
    build_battle_setup((const BwPortSetup *)call->a[0], out);
}

/*
 * battle.start: the format a handler changed is the format the battle runs
 * in, and the level a handler changed is added to every opposing Pokemon's
 * level.
 *
 * The format is written into the setup's own rule, which the battle's main
 * module reads (BTL_MAIN_GetRule reads the setup's rule), so the battle is
 * built with the format the handlers asked for.
 *
 * A multi battle is not applied: it needs a second trainer's team and a
 * second party in the setup, and the setup of a battle that was not a multi
 * one holds neither.
 */
/*
 * battle.trainer_mon: one firing per member of the opposing trainer's
 * party, once the setup holds the party.  The game's own ids for the
 * fields (src/game/arm9/poke_tool_param.h).
 */
#define BW_PORT_FIELD_SPECIES_ID 0x05u
#define BW_PORT_FIELD_ITEM_ID    0x06u
#define BW_PORT_FIELD_ABILITY_ID 0x0Au
#define BW_PORT_FIELD_MOVE1_ID   0x36u
#define BW_PORT_FIELD_FORM_ID    0x6Fu

static void fire_trainer_mons(const BwPortSetup *setup)
{
    struct Party *party;
    u16 ids[2];
    u32 count, i, k;

    if (setup == NULL || setup->competitor == 0u ||
        bw_mod_event_handler_count(BW_EVENT_BATTLE_TRAINER_MON) == 0u) {
        return;     /* a wild battle, or nobody listens */
    }
    party = (struct Party *)setup->party[1];
    if (party == NULL || POKEPARTY_GetCount == NULL ||
        POKEPARTY_GetMember == NULL || POKE_TOOL_GetPartyValue == NULL ||
        POKE_TOOL_SetPartyValue == NULL || POKE_TOOL_SetLevelAndRecalc == NULL ||
        POKE_TOOL_SetMoveSlot == NULL) {
        return;
    }
    fill_setup_trainer_ids(ids, setup);
    count = POKEPARTY_GetCount(party);
    if (count > 6u) {
        count = 6u;
    }
    for (i = 0u; i < count; i++) {
        struct PokemonParam *mon = POKEPARTY_GetMember(party, i);
        BwEventPayload p;
        BwBattleTrainerMon *t = &p.battle_trainer_mon;
        u16 moves[4];
        int restat = 0;

        if (mon == NULL) {
            continue;
        }
        memset(&p, 0, sizeof p);
        t->trainer_id = ids[0];
        t->index = (u8)i;
        t->level = (u8)POKE_TOOL_GetPartyValue(mon, BW_PORT_FIELD_LEVEL, 0u);
        t->species = (u16)POKE_TOOL_GetPartyValue(mon, BW_PORT_FIELD_SPECIES_ID, 0u);
        t->item = (u16)POKE_TOOL_GetPartyValue(mon, BW_PORT_FIELD_ITEM_ID, 0u);
        for (k = 0u; k < 4u; k++) {
            moves[k] = (u16)POKE_TOOL_GetPartyValue(mon, BW_PORT_FIELD_MOVE1_ID + k, 0u);
        }
        t->move1 = moves[0];
        t->move2 = moves[1];
        t->move3 = moves[2];
        t->move4 = moves[3];
        t->ability = (u16)POKE_TOOL_GetPartyValue(mon, BW_PORT_FIELD_ABILITY_ID, 0u);
        t->form = (u8)POKE_TOOL_GetPartyValue(mon, BW_PORT_FIELD_FORM_ID, 0u);
        {
            BwBattleTrainerMon before = *t;

            if (bw_mod_event_fire(BW_EVENT_BATTLE_TRAINER_MON, &p)) {
                /* Refused: the member leaves the party before the battle.
                 * The last one stays: a trainer with no Pokemon cannot
                 * battle. */
                if (count > 1u && POKEPARTY_RemoveMember != NULL) {
                    POKEPARTY_RemoveMember(party, (s32)i);
                    count--;
                    i--;
                    continue;
                }
            }
            if (t->species != 0u && t->species != before.species) {
                POKE_TOOL_SetPartyValue(mon, BW_PORT_FIELD_SPECIES_ID, t->species);
                POKE_TOOL_SetPartyValue(mon, BW_PORT_FIELD_FORM_ID, t->form);
                restat = 1;
            } else if (t->form != before.form) {
                POKE_TOOL_SetPartyValue(mon, BW_PORT_FIELD_FORM_ID, t->form);
                restat = 1;
            }
            if (t->ability != 0u && t->ability != before.ability) {
                POKE_TOOL_SetPartyValue(mon, BW_PORT_FIELD_ABILITY_ID, t->ability);
            }
            if (t->item != before.item) {
                POKE_TOOL_SetPartyValue(mon, BW_PORT_FIELD_ITEM_ID, t->item);
            }
            if (t->move1 != before.move1) {
                POKE_TOOL_SetMoveSlot(mon, t->move1, 0u);
            }
            if (t->move2 != before.move2) {
                POKE_TOOL_SetMoveSlot(mon, t->move2, 1u);
            }
            if (t->move3 != before.move3) {
                POKE_TOOL_SetMoveSlot(mon, t->move3, 2u);
            }
            if (t->move4 != before.move4) {
                POKE_TOOL_SetMoveSlot(mon, t->move4, 3u);
            }
            if (t->level != 0u && t->level != before.level) {
                restat = 1;
            }
            if (restat) {
                /* The game's own setter works out the experience for the
                 * level under the species' growth and the stats from it. */
                POKE_TOOL_SetLevelAndRecalc(mon, t->level != 0u ? t->level
                                                                : before.level);
            }
        }
    }
}

static void apply_battle_setup(const BwEventPayload *in, BwPortSetup *setup)
{
    if (setup == NULL) {
        return;
    }
    switch (in->battle_start.format) {
    case (u8)BW_FORMAT_SINGLE:
        setup->rule = 0u;
        setup->multiMode = 0u;
        break;
    case (u8)BW_FORMAT_DOUBLE:
        setup->rule = 1u;
        setup->multiMode = 0u;
        break;
    case (u8)BW_FORMAT_TRIPLE:
        setup->rule = 2u;
        setup->multiMode = 0u;
        break;
    case (u8)BW_FORMAT_ROTATION:
        setup->rule = 3u;
        setup->multiMode = 0u;
        break;
    default:
        break;   /* the multi battle is not applied */
    }
    apply_level_adjust(setup, in->battle_start.level_adjust);
    fire_trainer_mons(setup);
}

/*
 * battle.start: the trainer battle's setup.
 */
static void apply_battle_setup_trainer(const BwEventPayload *in,
                                        BwEventCall *call)
{
    apply_battle_setup(in, (BwPortSetup *)call->a[1]);
}

/*
 * battle.start: the scripted battle's setup.
 */
static void apply_battle_setup_scripted(const BwEventPayload *in,
                                         BwEventCall *call)
{
    apply_battle_setup(in, (BwPortSetup *)call->a[2]);
}

/*
 * battle.start: the wild battle's setup.
 */
static void apply_battle_setup_wild(const BwEventPayload *in,
                                     BwEventCall *call)
{
    apply_battle_setup(in, (BwPortSetup *)call->a[0]);
}

/*
 * battle.action: the player (or the AI) chooses a move.  The call carries
 * the client, the Pokemon whose move is checked, the move and the
 * message the game fills when the move is blocked.
 */
static void build_battle_action_fight(const BwEventCall *call,
                                      BwEventPayload *out)
{
    const BwPortClientHead *client = (const BwPortClientHead *)call->a[0];
    struct BtlPokeParam *mon = (struct BtlPokeParam *)call->a[1];
    struct BtlMain *main = main_of_client(call);

    memset(&out->battle_action, 0, sizeof(out->battle_action));
    out->battle_action.action = (u8)BW_ACTION_FIGHT;
    out->battle_action.move = (u16)call->a[2];
    fill_battle_ref(&out->battle_action.mon, mon, main,
                    client != NULL ? client->side : NULL);
    out->battle_action.side = out->battle_action.mon.side;
    bind_battle(main);
    feed_battle_context(main);
}

/*
 * battle.action: the player (or the AI) chooses to run.  The call carries
 * the client and the two words the game fills with the Pokemon that traps
 * the escape and the ability that traps it.
 */
static void build_battle_action_run(const BwEventCall *call,
                                     BwEventPayload *out)
{
    const BwPortClientHead *client = (const BwPortClientHead *)call->a[0];
    struct BtlPokeParam *mon = NULL;
    struct BtlMain *main = main_of_client(call);

    memset(&out->battle_action, 0, sizeof(out->battle_action));
    out->battle_action.action = (u8)BW_ACTION_RUN;
    if (client != NULL) {
        mon = (struct BtlPokeParam *)client->activeMon;
    }
    fill_battle_ref(&out->battle_action.mon, mon, main,
                    client != NULL ? client->side : NULL);
    out->battle_action.side = out->battle_action.mon.side;
    bind_battle(main);
    feed_battle_context(main);
}

/*
 * battle.action: the player chooses a Pokemon to switch to.  The event
 * fires at the return of the choice list (the list does not know the
 * Pokemon that is choosing), so the party position the player picked is
 * the function's result.
 */
static void build_battle_action_switch(const BwEventCall *call,
                                       BwEventPayload *out)
{
    memset(&out->battle_action, 0, sizeof(out->battle_action));
    out->battle_action.action = (u8)BW_ACTION_SWITCH;

    /*
     * The party position the player picked is the function's result: the
     * position in the party, or 6 when the player backed out
     * (the game's own BTL_PICK_SLOT_NONE).
     */
    out->battle_action.switch_to = (u8)call->ret;

    /*
     * The field the port cannot fill: the Pokemon that is choosing.  The
     * choice list holds the party positions and nothing else, so the
     * reference stays empty.
     */
}

/*
 * battle.item: the player picks an item from the bag.  The event fires at
 * the return of the view's own getter, so the item is the function's
 * result.
 */
static void build_battle_item(const BwEventCall *call, BwEventPayload *out)
{
    const BwPortCoreHead *core = (const BwPortCoreHead *)call->a[0];
    const BwPortClientHead *client = NULL;
    struct BtlMain *main = main_of_core(call);
    u16 item = (u16)call->ret;

    memset(&out->battle_item, 0, sizeof(out->battle_item));
    out->battle_item.item = item;

    if (item != 0u && BTL_CALC_GetItemParam != NULL) {
        /*
         * The game itself tells a ball from the other items with the
         * item's own party-use field (BTL_CLIENT_StepSelectItem and
         * BTL_SERVER_FLOW_ProcBagItemUse), so the port uses the same test.
         */
        out->battle_item.ball = (u8)(BTL_CALC_GetItemParam(item,
                                     BW_PORT_ITEM_PARAM_BALL) ==
                                     BW_PORT_ITEM_PARAM_BALL_BALL);
        /*
         * The item revives a fainted Pokemon when its record carries the
         * revive fields (the game's party screen names them
         * PLIST_ITEM_REVIVES and PLIST_ITEM_REVIVES_FULL).
         */
        out->battle_item.revive =
            (u8)(BTL_CALC_GetItemParam(item,
                                        BW_PORT_ITEM_PARAM_REVIVES) != 0u ||
                 BTL_CALC_GetItemParam(item,
                                       BW_PORT_ITEM_PARAM_REVIVES_FULL) != 0u);
    }

    if (core != NULL) {
        client = (const BwPortClientHead *)core->client;
    }
    if (client != NULL) {
        fill_battle_ref(&out->battle_item.mon,
                        (struct BtlPokeParam *)client->activeMon, main,
                        client->side);
    }
    bind_battle(main);
    feed_battle_context(main);
}

/*
 * battle.switch: a Pokemon is sent out.  The call carries the server's
 * flow, the client that switches, the position the Pokemon leaves and the
 * position it comes from.
 */
static void build_battle_switch(const BwEventCall *call, BwEventPayload *out)
{
    const BwPortFlowHead *flow = (const BwPortFlowHead *)call->a[0];
    struct BtlMain *main = main_of_flow(call);
    struct BtlSideSlot *slot = NULL;
    struct BtlPokeParam *leaving = NULL;
    struct BtlPokeParam *entering = NULL;
    u8 clientId = (u8)call->a[1];
    u8 memberIndex = (u8)call->a[2];
    u8 swapIndex = (u8)call->a[3];

    memset(&out->battle_switch, 0, sizeof(out->battle_switch));

    if (flow != NULL && BTL_SIDE_GetSlotOfClient != NULL &&
        BTL_SLOT_GetPokeParam != NULL) {
        slot = BTL_SIDE_GetSlotOfClient((struct BtlSide *)flow->pokeCon,
                                        (int)clientId);
        if (slot != NULL) {
            /*
             * The game swaps the two positions after the call, so before
             * the call the position that is left holds the Pokemon that
             * goes out and the other one holds the Pokemon that comes in.
             */
            leaving = BTL_SLOT_GetPokeParam(slot, memberIndex);
            entering = BTL_SLOT_GetPokeParam(slot, swapIndex);
        }
    }

    fill_battle_ref(&out->battle_switch.out, leaving, main,
                    flow != NULL ? flow->pokeCon : NULL);
    fill_battle_ref(&out->battle_switch.in, entering, main,
                    flow != NULL ? flow->pokeCon : NULL);

    /*
     * The switch is forced when it replaces a Pokemon that has fainted.
     * The other case the documents name, a move that forces the switch,
     * cannot be told from the call: the server runs the same function for
     * both.
     */
    if (leaving != NULL && BTL_POKEPARAM_IsFainted != NULL &&
        BTL_POKEPARAM_IsFainted(leaving) != 0) {
        out->battle_switch.forced = 1u;

        /*
         * The Pokemon that leaves has fainted, so the reference to it is
         * empty (the payload names the Pokemon that is out, "or empty
         * after a faint").
         */
        memset(&out->battle_switch.out, 0, sizeof(out->battle_switch.out));
    }

    feed_turn_weather((struct BtlServerFlow *)flow);
    bind_battle(main);
    feed_battle_context(main);
}

/*
 * battle.escape: a Pokemon tries to flee.  The call carries the server's
 * flow, the Pokemon that tries and the word that says the escape is
 * forced.
 */
static void build_battle_escape(const BwEventCall *call, BwEventPayload *out)
{
    const BwPortFlowHead *flow = (const BwPortFlowHead *)call->a[0];
    struct BtlMain *main = main_of_flow(call);
    struct BtlPokeParam *mon = (struct BtlPokeParam *)call->a[1];

    memset(&out->battle_escape, 0, sizeof(out->battle_escape));
    fill_battle_ref(&out->battle_escape.mon, mon, main,
                    flow != NULL ? flow->pokeCon : NULL);
    feed_turn_weather((struct BtlServerFlow *)flow);
    bind_battle(main);
    feed_battle_context(main);
}

/*
 * battle.move: a Pokemon uses a move.  The call carries the server's flow,
 * the Pokemon that attacks and the move record.
 */
static void build_battle_move(const BwEventCall *call, BwEventPayload *out)
{
    const BwPortFlowHead *flow = (const BwPortFlowHead *)call->a[0];
    struct BtlMain *main = main_of_flow(call);
    struct BtlPokeParam *attacker = (struct BtlPokeParam *)call->a[1];
    const BwPortMoveRecord *move = (const BwPortMoveRecord *)call->a[2];

    memset(&out->battle_move, 0, sizeof(out->battle_move));
    if (move != NULL) {
        out->battle_move.move = move->moveId;
    }
    fill_battle_ref(&out->battle_move.user, attacker, main,
                    flow != NULL ? flow->pokeCon : NULL);

    /*
     * The field the port cannot fill: the target of the move.  The call
     * carries the attacker and the move record, and the move record holds
     * no target: the server picks the target set itself.
     */
    out->battle_move.turn = turn_of((struct BtlServerFlow *)flow);

    /*
     * The context is fed here (docs/mod-payloads.md, section 1): the move
     * that is used, the Pokemon that used it, and the damage of the
     * previous move, which is cleared so that a faint from the weather, a
     * hazard or a status reads 0.
     */
    bw_mod_battle_context.last_move = out->battle_move.move;
    bw_mod_battle_context.last_attacker = out->battle_move.user;
    bw_mod_battle_context.last_damage = 0u;
    bw_mod_battle_context.turn = out->battle_move.turn;
    bw_mod_battle_context.weather = weather_of((struct BtlServerFlow *)flow);

    bind_battle(main);
    feed_battle_context(main);
}

/*
 * battle.damage: a move hits.  The call carries the server's flow, the
 * attacker, the defender, the move record, the type matchup, the range
 * modifier, the critical flag, the fixed roll and the word the damage is
 * written into.
 *
 * The event fires on the return of the game's own function: the damage is
 * computed inside it and written into the word the caller reads after the
 * call, so this is where the amount is known.  The hit has not landed yet
 * (the damage record is applied later, when the turn's damage is dealt),
 * so the target still has the HP it had before the hit.
 */
static void build_battle_damage(const BwEventCall *call, BwEventPayload *out)
{
    const BwPortFlowHead *flow = (const BwPortFlowHead *)call->a[0];
    struct BtlMain *main = main_of_flow(call);
    struct BtlPokeParam *attacker = (struct BtlPokeParam *)call->a[1];
    struct BtlPokeParam *defender = (struct BtlPokeParam *)call->a[2];
    const BwPortMoveRecord *move = (const BwPortMoveRecord *)call->a[3];
    const u16 *damage_word = (const u16 *)call->a[8];
    u32 matchup = (u32)call->a[4];
    u32 critical = (u32)call->a[6];

    memset(&out->battle_damage, 0, sizeof(out->battle_damage));
    if (move != NULL) {
        out->battle_damage.move = move->moveId;
    }
    fill_battle_ref(&out->battle_damage.attacker, attacker, main,
                    flow != NULL ? flow->pokeCon : NULL);
    fill_battle_ref(&out->battle_damage.target, defender, main,
                    flow != NULL ? flow->pokeCon : NULL);

    if (defender != NULL && BTL_POKEPARAM_GetValue != NULL) {
        /*
         * The hit has not landed yet, so the defender still has the HP it
         * had before it.
         */
        out->battle_damage.target_hp =
            (u16)BTL_POKEPARAM_GetValue(defender, 13);
        out->battle_damage.target_max_hp =
            (u16)BTL_POKEPARAM_GetValue(defender, 14);
    }
    out->battle_damage.critical = (u8)(critical != 0u ? 1u : 0u);
    out->battle_damage.effectiveness = effectiveness_of(matchup);

    /*
     * The damage the game computed: it is in the word the call carries,
     * and the caller reads that word after the call.
     */
    if (damage_word != NULL) {
        out->battle_damage.amount = (u32)*damage_word;
    }

    /*
     * The context is fed here (docs/mod-payloads.md, section 1): the
     * attacker and the damage of the hit.
     */
    bw_mod_battle_context.last_attacker = out->battle_damage.attacker;
    bw_mod_battle_context.last_damage = out->battle_damage.amount;
    bw_mod_battle_context.turn = turn_of((struct BtlServerFlow *)flow);
    bw_mod_battle_context.weather = weather_of((struct BtlServerFlow *)flow);

    bind_battle(main);
    feed_battle_context(main);
}

/*
 * battle.damage: the damage a handler changed is the damage the hit does.
 * The applier writes it into the word the game's own function filled, and
 * the caller reads that word after the call, so the hit does the damage
 * the handler asked for.
 *
 * A damage of zero is not written: the game never computes a hit of no
 * damage, so a zero is a handler that wrote nothing, and the game's own
 * damage stands.
 */
static void apply_battle_damage(const BwEventPayload *in, BwEventCall *call)
{
    u16 *damage_word = (u16 *)call->a[8];

    if (damage_word == NULL || in->battle_damage.amount == 0u) {
        return;
    }
    if (in->battle_damage.amount > 0xFFFFu) {
        damage_word[0] = 0xFFFFu;   /* the game's own word holds no more */
        return;
    }
    damage_word[0] = (u16)in->battle_damage.amount;
}

/*
 * battle.faint: a Pokemon faints.  The call carries the server's flow and
 * the Pokemon that fainted; the cause of the faint comes from the battle
 * context, which the damage and the move events feed.
 */
static void build_battle_faint(const BwEventCall *call, BwEventPayload *out)
{
    const BwPortFlowHead *flow = (const BwPortFlowHead *)call->a[0];
    struct BtlMain *main = main_of_flow(call);
    struct BtlPokeParam *mon = (struct BtlPokeParam *)call->a[1];

    memset(&out->battle_faint, 0, sizeof(out->battle_faint));
    fill_battle_ref(&out->battle_faint.mon, mon, main,
                    flow != NULL ? flow->pokeCon : NULL);
    out->battle_faint.side = out->battle_faint.mon.side;

    /*
     * The cause of the faint comes from the context: the last attacker,
     * the move it used and the damage it did.  A faint from the weather, a
     * hazard or a status has no cause, and the context reads 0 then.
     */
    out->battle_faint.cause = bw_mod_battle_context.last_attacker;
    out->battle_faint.cause_move = bw_mod_battle_context.last_move;
    out->battle_faint.cause_damage = bw_mod_battle_context.last_damage;
    out->battle_faint.turn = bw_mod_battle_context.turn;

    feed_turn_weather((struct BtlServerFlow *)flow);
    bind_battle(main);
    feed_battle_context(main);
}

/*
 * catch.attempt: a ball is thrown.  The call carries the server's flow,
 * the Pokemon that throws and the ball.
 *
 * The rate a handler asked for at the last throw does not reach this one:
 * the port forgets it here, before the handlers run, so a rate asked for
 * and then not used (a ball the game blocks, say) is not applied to a
 * later throw.  The applier asks for the rate again if a handler wants
 * one here.
 */
static void build_catch_attempt(const BwEventCall *call, BwEventPayload *out)
{
    const BwPortFlowHead *flow = (const BwPortFlowHead *)call->a[0];
    struct BtlMain *main = main_of_flow(call);
    void *thrower = (void *)call->a[1];
    struct BtlPokeParam *target;

    memset(&out->catch_attempt, 0, sizeof(out->catch_attempt));
    out->catch_attempt.static_ = (u8)bw_mod_state_battle_static();
    out->catch_attempt.ball = (u16)call->a[2];

    /* The rate of the last throw does not reach this one. */
    g_catch_rate_pending = -1;

    target = catch_target_of((struct BtlServerFlow *)flow, thrower, NULL);
    fill_battle_ref(&out->catch_attempt.target, target, main,
                    flow != NULL ? flow->pokeCon : NULL);
    if (target != NULL) {
        if (BTL_POKEPARAM_GetSpecies != NULL) {
            out->catch_attempt.species = BTL_POKEPARAM_GetSpecies(target);
        }
        if (BTL_POKEPARAM_GetValue != NULL) {
            out->catch_attempt.level =
                (u8)BTL_POKEPARAM_GetValue(target, 15);
        }
        if (BTL_POKEPARAM_GetMon != NULL && POKE_TOOL_IsShiny != NULL) {
            struct PokemonParam *mon = BTL_POKEPARAM_GetMon(target);

            if (mon != NULL) {
                out->catch_attempt.shiny =
                    (u8)(POKE_TOOL_IsShiny(mon) != 0u ? 1u : 0u);
            }
        }
    }

    /*
     * The catch rate the roll uses: the game's own function computes it
     * when the roll asks for it (BTL_SERVER_FLOW_CalcCaptureShakes calls
     * BTL_SERVER_FLOW_GetBallCatchRate), and the port asks it the same
     * question, so the payload carries the rate the roll will use.  The
     * rate is a 12.12 fixed-point factor and the payload carries it as a
     * percentage of it.
     */
    g_catch_rate_own = 0;
    if (BTL_SERVER_FLOW_GetBallCatchRate != NULL && target != NULL) {
        g_catch_rate_own = catch_rate_percent(
            BTL_SERVER_FLOW_GetBallCatchRate((struct BtlServerFlow *)flow,
                                             thrower, target,
                                             (int)out->catch_attempt.ball));
    }
    out->catch_attempt.rate = (u8)g_catch_rate_own;

    feed_turn_weather((struct BtlServerFlow *)flow);
    bind_battle(main);
    feed_battle_context(main);
}

/*
 * catch.attempt: the rate a handler changed is the rate the roll uses.  The
 * rate is computed inside the roll, so the port's own hook on the game's
 * own function (the declared row of the catch rate) answers with it.
 *
 * A rate the handlers left alone is not applied: the game's own rate is
 * the one the roll uses, and the port does not scale it a second time.
 */
static void apply_catch_attempt(const BwEventPayload *in, BwEventCall *call)
{
    (void)call;

    if ((int)in->catch_attempt.rate == g_catch_rate_own) {
        return;
    }
    install_catch_rate_hook();
    g_catch_rate_pending = (int)in->catch_attempt.rate;
}

/*
 * catch.result: the ball was thrown and the roll is done.  The event
 * fires at the return of the game's own roll
 * (BTL_SERVER_FLOW_CalcCaptureShakes), because that is the place the game
 * keeps the outcome: the function's own result says whether the Pokemon
 * was caught, and the shakes it shows are the ones it writes into the
 * word the call carries.
 *
 * The target is the Pokemon the roll was made for and the ball is the
 * item the roll used: both are the call's own arguments, so the port does
 * not run the throw's own search a second time to name them.
 */
static void build_catch_result(const BwEventCall *call, BwEventPayload *out)
{
    const BwPortFlowHead *flow = (const BwPortFlowHead *)call->a[0];
    struct BtlMain *main = main_of_flow(call);
    struct BtlPokeParam *target = (struct BtlPokeParam *)call->a[2];
    const u8 *shakes = (const u8 *)call->a[4];

    memset(&out->catch_result, 0, sizeof(out->catch_result));
    out->catch_result.ball = (u16)call->a[3];

    fill_battle_ref(&out->catch_result.target, target, main,
                    flow != NULL ? flow->pokeCon : NULL);
    if (target != NULL && BTL_POKEPARAM_GetSpecies != NULL) {
        out->catch_result.species = BTL_POKEPARAM_GetSpecies(target);
    }

    /*
     * The catch succeeded when the roll's own result says so: the game
     * answers 1 when the Pokemon was caught and 0 when it broke out of
     * the ball.
     */
    out->catch_result.caught = (u8)(call->ret != 0 ? 1u : 0u);

    /*
     * The shakes the game shows are the ones the roll wrote into the word
     * it was given.
     */
    if (shakes != NULL) {
        out->catch_result.shakes = *shakes;
    }

    feed_turn_weather((struct BtlServerFlow *)flow);
    bind_battle(main);
    feed_battle_context(main);
}

/*
 * catch.result: the roll is over, so the rate the handlers asked for is
 * for that roll alone: the port's own hook answers with the game's own
 * rate again (the port's own state, like the save window the save.done
 * row closes).
 */
static void apply_catch_result(const BwEventPayload *in, BwEventCall *call)
{
    (void)in;
    (void)call;

    g_catch_rate_pending = -1;
}

/*
 * battle.end: the battle is over.  The call carries the context the game
 * fills with the game data and the battle setup.
 */
static void build_battle_end(const BwEventCall *call, BwEventPayload *out)
{
    const BwPortBattleEndCtx *ctx = (const BwPortBattleEndCtx *)call->a[0];
    const BwPortSetup *setup = NULL;

    memset(&out->battle_end, 0, sizeof(out->battle_end));

    if (ctx != NULL) {
        setup = (const BwPortSetup *)ctx->btlSetup;

        /*
         * The game data is bound here (sdk/mod_state.h: the port binds
         * the game's live objects where it learns them, and this is where
         * the end of the battle hands it the game data).
         */
        if (ctx->btlSetupObj != NULL) {
            bw_mod_state_set_game_data(ctx->btlSetupObj);
        }
    }

    if (setup != NULL) {
        /*
         * The result the game wrote into the setup, the trainers of the
         * battle and the number of turns it took.
         */
        out->battle_end.outcome = battle_outcome_of(setup->result);
        out->battle_end.wild = (u8)(setup->competitor == 0u ? 1u : 0u);
        fill_setup_trainer_ids(out->battle_end.trainer_id, setup);
        out->battle_end.turns = setup->turnCount;
    }

    /*
     * The battle is over, so the context is no longer active
     * (docs/mod-payloads.md, section 1: "active cleared at battle.end"),
     * and the battle's main module is unbound: the state layer is told
     * that the battle is over with a NULL (sdk/mod_state.h).
     */
    bw_mod_battle_context.active = 0u;
    bw_mod_state_set_battle(NULL);
}

/*
 * whiteout: the player lost a battle and the game shows the messages of
 * the blackout.  The call carries the client and the state of the step.
 */
static void build_whiteout_battle(const BwEventCall *call,
                                   BwEventPayload *out)
{
    struct BtlMain *main = main_of_client(call);

    memset(&out->whiteout, 0, sizeof(out->whiteout));
    out->whiteout.in_battle = 1u;

    /*
     * The field the port cannot fill: the money the game takes.  The game
     * computes the penalty inside this step, at the state that shows the
     * amount, and the accessor that computes it latches its result, so
     * the port does not call it here: the game would find nothing left
     * to show.
     */
    out->whiteout.money_lost = 0u;

    bind_battle(main);
}

/*
 * whiteout: the player blacked out in the field.  The call carries the
 * field's own system and a word the game does not use.
 */
static void build_whiteout_field(const BwEventCall *call, BwEventPayload *out)
{
    (void)call;
    memset(&out->whiteout, 0, sizeof(out->whiteout));
    out->whiteout.in_battle = 0u;

    /*
     * The field the port cannot fill: the money the game takes.  The
     * recover script takes it, and the port does not read the script's
     * work.
     */
    out->whiteout.money_lost = 0u;
}

/* ------------------------------------------------------------------ */
/* The refusals                                                        */
/* ------------------------------------------------------------------ */

/*
 * battle.action: the choice of a move is refused.  The game answers 1
 * when a move is blocked, and the message it wrote is shown while the
 * action menu stays (docs/mod-implementation.md, section 1, "Refusal
 * functions").
 */
static void refuse_battle_action_fight(BwEventCall *call)
{
    void *outMessage = (void *)call->a[3];
    struct BtlPokeParam *mon = (struct BtlPokeParam *)call->a[1];

    call->ret = 1;

    /*
     * The message the game shows for a blocked move.  The port's message
     * service is built (sdk/mod_message.c), but a message it shows would
     * not be the game's own, so the port fills the game's own "the Pokemon
     * cannot use this move" message, with the Pokemon and the move as its
     * arguments.
     */
    if (outMessage != NULL && BTLV_SCU_InitMessage != NULL) {
        u32 pokeId = 0u;

        if (mon != NULL && BTL_POKEPARAM_GetPokeId != NULL) {
            pokeId = BTL_POKEPARAM_GetPokeId(mon);
        }
        BTLV_SCU_InitMessage((struct BtlvMessage *)outMessage, 2,
                             (int)BW_PORT_MESSAGE_CANT_USE_MOVE);
        if (BTLV_SCU_AppendMessageArg != NULL) {
            BTLV_SCU_AppendMessageArg((struct BtlvMessage *)outMessage,
                                      pokeId);
            BTLV_SCU_AppendMessageArg((struct BtlvMessage *)outMessage,
                                      (u32)call->a[2]);
        }
    }
}

/*
 * battle.action: the choice to run is refused.  The game answers 4 when
 * the escape is blocked, and writes the Pokemon that traps the escape and
 * the ability that traps it; an answer of 4 with no ability is the
 * "Can't escape!" path (docs/mod-implementation.md, section 1).
 */
static void refuse_battle_action_run(BwEventCall *call)
{
    u8 *blockerPokeId = (u8 *)call->a[1];
    u16 *blockerAbility = (u16 *)call->a[2];

    call->ret = 4;

    if (blockerPokeId != NULL) {
        *blockerPokeId = (u8)BW_PORT_POKE_ID_NONE;
    }
    if (blockerAbility != NULL) {
        *blockerAbility = 0u;
    }
}

/*
 * battle.action: the choice of a Pokemon to switch to is refused.  The
 * answer 6 is the value the game reads as "the player backed out", so the
 * action menu comes back (docs/mod-implementation.md, section 1).
 */
static void refuse_battle_action_switch(BwEventCall *call)
{
    call->ret = 6;
}

/*
 * battle.item: the item is refused.  The answer 0 is the value the game
 * reads as "the player backed out", so the action menu comes back, the
 * item stays in the bag and the turn is not spent
 * (docs/mod-implementation.md, section 1).
 */
static void refuse_battle_item(BwEventCall *call)
{
    call->ret = 0;
}

/*
 * battle.escape: the escape is refused.  The answer 0 is a failed run:
 * the caller of the escape shows the "Can't escape!" message and spends
 * the turn (docs/mod-events.md, section 1, "Battle").
 */
static void refuse_battle_escape(BwEventCall *call)
{
    call->ret = 0;
}

/*
 * catch.attempt: the ball is refused.  The game blocks the ball the way
 * it blocks it in a trainer battle: the block command (0x47) is pushed
 * for the target and the function answers 0, so the caller does not take
 * the ball from the bag (docs/mod-implementation.md, section 1).
 */
static void refuse_catch_attempt(BwEventCall *call)
{
    const BwPortFlowHead *flow = (const BwPortFlowHead *)call->a[0];
    void *thrower = (void *)call->a[1];
    u16 ball = (u16)call->a[2];
    u8 position = 6u;

    /*
     * The block command names the target by its position in the battle,
     * the way the game pushes it when the ball is blocked in a trainer
     * battle.
     */
    (void)catch_target_of((struct BtlServerFlow *)flow, thrower, &position);

    if (flow != NULL && flow->cmdQueue != NULL && BTL_SERVER_CMD_Push != NULL) {
        BTL_SERVER_CMD_Push(flow->cmdQueue, 0x47u, (u32)position, (u32)ball);
    }
    call->ret = 0;
}

/* ------------------------------------------------------------------ */
/* The rows                                                            */
/* ------------------------------------------------------------------ */

/*
 * The rows of this area.  The scope of a row is BW_SCOPE_ANY, except for
 * the two rows the documents scope: the choice of a Pokemon to switch to
 * only means the event inside BTL_CLIENT_StepSelectSwitchTarget, and the
 * choice of an item only inside BTL_CLIENT_StepSelectItem
 * (docs/mod-events.md, section 1).
 *
 * The rows of this area are the ones mod/declared_set.txt gives the
 * battle events: BTL_SETUP_Create (battle.start), the two blocked-move
 * and escape checks of the client (battle.action), the pick list of the
 * switch (battle.action), the view's item getter (battle.item), the
 * server's send-out (battle.switch), the server's escape (battle.escape),
 * the server's move used and damage (battle.move, battle.damage), the
 * server's faint (battle.faint), the server's throw (catch.attempt) and
 * the server's roll of the catch (catch.result), the end of the battle
 * (battle.end) and the two blackouts (whiteout).
 *
 * battle.start fires twice for a battle: once at BTL_SETUP_Create, where
 * the setup the game will build does not exist yet and the payload is
 * empty, and once at the rows that build the setup, where the payload
 * names the battle's own values and the port applies the format and the
 * level the handlers asked for.  The wild battle's row (BTL_SETUP_SetupWild)
 * is in the table and the declared set gives that row an event, so its slot
 * calls the event layer and the row fires.
 *
 * catch.result fires at the roll (BTL_SERVER_FLOW_CalcCaptureShakes), not
 * at the throw: the roll is where the game knows whether the Pokemon was
 * caught and how many shakes it took, and the throw itself does not roll
 * in a trainer battle.
 */
/*
 * heal.hp in a battle: BTL_SERVER_FLOW_ApplyHpRecover(flow, pokeParam,
 * amount), the one place every HP restore of the battle goes through (a bag
 * item, a held item, a move, an ability).  The game caps the result at max
 * HP itself.
 */
static void build_heal_hp_battle(const BwEventCall *call, BwEventPayload *out)
{
    const BwPortFlowHead *flow = (const BwPortFlowHead *)call->a[0];
    struct BtlPokeParam *mon = (struct BtlPokeParam *)call->a[1];
    s32 amount = (s32)call->a[2];

    memset(&out->heal_hp, 0, sizeof(out->heal_hp));
    fill_battle_ref(&out->heal_hp.mon, mon, main_of_flow(call),
                    flow != NULL ? flow->pokeCon : NULL);
    if (mon != NULL && BTL_POKEPARAM_GetValue != NULL) {
        out->heal_hp.hp = (u16)BTL_POKEPARAM_GetValue(mon, 13);
        out->heal_hp.max_hp = (u16)BTL_POKEPARAM_GetValue(mon, 14);
    }
    out->heal_hp.amount = (u16)(amount < 0 ? 0 : (amount > 0xFFFF ? 0xFFFF : amount));
    out->heal_hp.source = (u8)BW_HEAL_HP_BATTLE;
}

static void apply_heal_hp_battle(const BwEventPayload *in, BwEventCall *call)
{
    call->a[2] = (uintptr_t)in->heal_hp.amount;
}

/* heal.hp: the refused outcome is that the HP stays as it is; the game's
 * own message about the restore may still show. */
static void refuse_heal_hp_battle(BwEventCall *call)
{
    call->ret = 0;
}

/*
 * mon.revive in a battle.  A Revive from the battle bag runs
 * BTL_SERVER_FLOW_ItemEffectRevive (flow, poke, item): the bag takes the
 * item only when an effect answers 1, so a refusal there (answer 0, "it
 * had no effect") keeps the item.  The effect writes a revive command that
 * BTL_SERVER_FLOW_ProcHandExReviveAndSendIn (flow, cmd) carries out later:
 * the Pokemon's id at +4, the HP it gets at +6, added straight to the
 * record past BTL_SERVER_FLOW_ApplyHpRecover.  The port's own row there
 * writes the HP the handlers chose.
 */
typedef struct BwPortReviveCmd {
    u32 header;
    u8  pokeId;     /* +4 */
    u8  pad5;
    u16 amount;     /* +6 */
} BwPortReviveCmd;

extern struct BtlPokeParam *BTL_SIDE_GetPokeByIdThunk(void *side, int pokeId)
    BW_PORT_WEAK;                                        /* 0x021B9934 */

extern u32 FILE_UTIL_GetItemParam(u32 item, u32 sel, u32 heap) BW_PORT_WEAK;

/* The HP the handlers chose for a Pokemon the bag revives, by its id. */
static struct {
    int  set;
    u8   pokeId;
    u16  hp;
} g_battle_revive;

static void build_mon_revive_battle(const BwEventCall *call, BwEventPayload *out)
{
    const BwPortFlowHead *flow = (const BwPortFlowHead *)call->a[0];
    struct BtlPokeParam *mon = (struct BtlPokeParam *)call->a[1];
    u32 item = (u32)call->a[2];
    u32 maxHp = 0, code;

    memset(&out->mon_revive, 0, sizeof(out->mon_revive));
    out->mon_revive.source = (u8)BW_REVIVE_BATTLE_ITEM;
    out->mon_revive.item = (u16)item;
    fill_battle_ref(&out->mon_revive.mon, mon, main_of_flow(call),
                    flow != NULL ? flow->pokeCon : NULL);
    if (mon != NULL && BTL_POKEPARAM_GetValue != NULL) {
        maxHp = (u32)BTL_POKEPARAM_GetValue(mon, 14);
    }
    out->mon_revive.max_hp = (u16)maxHp;
    /* The amount the effect works out: the item's HP code (0xFF all,
     * 0xFE half, 0xFD a quarter, each at least 1), or a number. */
    code = FILE_UTIL_GetItemParam != NULL ? FILE_UTIL_GetItemParam(item, 0x3Au, 3u) : 0xFFu;
    if (code == 0xFFu) {
        out->mon_revive.hp = (u16)maxHp;
    } else if (code == 0xFEu || code == 0xFDu) {
        u32 part = maxHp / (code == 0xFEu ? 2u : 4u);

        out->mon_revive.hp = (u16)(part == 0u ? 1u : part);
    } else {
        out->mon_revive.hp = (u16)code;
    }
}

static int when_battle_fainted(const BwEventCall *call)
{
    struct BtlPokeParam *mon = (struct BtlPokeParam *)call->a[1];

    return mon != NULL && BTL_POKEPARAM_GetValue != NULL &&
           BTL_POKEPARAM_GetValue(mon, 13) == 0;
}

static void apply_mon_revive_battle(const BwEventPayload *in, BwEventCall *call)
{
    struct BtlPokeParam *mon = (struct BtlPokeParam *)call->a[1];

    if (mon == NULL || BTL_POKEPARAM_GetPokeId == NULL) {
        return;
    }
    g_battle_revive.set = 1;
    g_battle_revive.pokeId = (u8)BTL_POKEPARAM_GetPokeId(mon);
    g_battle_revive.hp = in->mon_revive.hp;
}

/* mon.revive from the bag, refused: the effect answers 0 and the bag keeps
 * the item. */
static void refuse_mon_revive_battle(BwEventCall *call)
{
    call->ret = 0;
}

/* The revive command: the HP the handlers chose replaces the effect's. */
static void apply_revive_cmd(const BwEventPayload *unused, BwEventCall *call)
{
    BwPortReviveCmd *cmd = (BwPortReviveCmd *)call->a[1];
    u32 maxHp = 0;

    (void)unused;
    if (cmd == NULL || !g_battle_revive.set || g_battle_revive.pokeId != cmd->pokeId) {
        return;
    }
    g_battle_revive.set = 0;
    {
        const BwPortFlowHead *flow = (const BwPortFlowHead *)call->a[0];
        struct BtlPokeParam *mon = NULL;

        if (flow != NULL && flow->pokeCon != NULL && BTL_SIDE_GetPokeByIdThunk != NULL) {
            mon = BTL_SIDE_GetPokeByIdThunk(flow->pokeCon, (int)cmd->pokeId);
        }
        if (mon != NULL && BTL_POKEPARAM_GetValue != NULL) {
            maxHp = (u32)BTL_POKEPARAM_GetValue(mon, 14);
        }
    }
    cmd->amount = g_battle_revive.hp == 0u ? 1u : g_battle_revive.hp;
    if (maxHp != 0u && cmd->amount > maxHp) {
        cmd->amount = (u16)maxHp;
    }
}

/*
 * mon.cure in a battle: BTL_SERVER_FLOW_CureSick (flow, poke, sickId,
 * prev), which every cure of the battle server runs (an item, a move, an
 * ability, a berry).  Refused, the status stays.
 */
static int when_battle_cure(const BwEventCall *call)
{
    return (int)call->a[2] != 0;
}

static void build_mon_cure_battle(const BwEventCall *call, BwEventPayload *out)
{
    const BwPortFlowHead *flow = (const BwPortFlowHead *)call->a[0];

    memset(&out->mon_cure, 0, sizeof(out->mon_cure));
    fill_battle_ref(&out->mon_cure.mon, (struct BtlPokeParam *)call->a[1],
                    main_of_flow(call), flow != NULL ? flow->pokeCon : NULL);
    out->mon_cure.status = (u8)call->a[2];
    out->mon_cure.source = (u8)BW_CURE_BATTLE;
}

static void refuse_mon_cure_battle(BwEventCall *call)
{
    call->ret = 0;
}

/*
 * mon.pp in a battle: BTL_SERVER_FLOW_RestoreMovePp (flow, poke, slot,
 * amount, real).  Refused, no PP comes back.
 */
static void build_mon_pp_battle(const BwEventCall *call, BwEventPayload *out)
{
    const BwPortFlowHead *flow = (const BwPortFlowHead *)call->a[0];

    memset(&out->mon_pp, 0, sizeof(out->mon_pp));
    fill_battle_ref(&out->mon_pp.mon, (struct BtlPokeParam *)call->a[1],
                    main_of_flow(call), flow != NULL ? flow->pokeCon : NULL);
    out->mon_pp.slot = (u8)call->a[2];
    out->mon_pp.source = (u8)BW_PP_BATTLE;
    out->mon_pp.amount = (u16)call->a[3];
}

static void apply_mon_pp_battle(const BwEventPayload *in, BwEventCall *call)
{
    call->a[3] = (uintptr_t)in->mon_pp.amount;
}

static void refuse_mon_pp_battle(BwEventCall *call)
{
    call->ret = 0;
}

/*
 * mon.form in a battle: BTL_SERVER_FLOW_ProcHandExChangeForm (flow, cmd),
 * the form change of an ability, a move or a held item.  The command holds
 * the Pokemon's id at +4 and the form at +5.  Refused, the command answers
 * 0 and the form stays.
 */
typedef struct BwPortFormCmd {
    u32 header;
    u8  pokeId;     /* +4 */
    u8  form;       /* +5 */
} BwPortFormCmd;

static void build_mon_form_battle(const BwEventCall *call, BwEventPayload *out)
{
    const BwPortFlowHead *flow = (const BwPortFlowHead *)call->a[0];
    const BwPortFormCmd *cmd = (const BwPortFormCmd *)call->a[1];
    struct BtlPokeParam *mon = NULL;

    memset(&out->mon_form, 0, sizeof(out->mon_form));
    out->mon_form.source = (u8)BW_FORM_BATTLE;
    if (cmd == NULL) {
        return;
    }
    if (flow != NULL && flow->pokeCon != NULL && BTL_SIDE_GetPokeByIdThunk != NULL) {
        mon = BTL_SIDE_GetPokeByIdThunk(flow->pokeCon, (int)cmd->pokeId);
    }
    fill_battle_ref(&out->mon_form.mon, mon, main_of_flow(call),
                    flow != NULL ? flow->pokeCon : NULL);
    if (mon != NULL && BTL_POKEPARAM_GetValue != NULL) {
        out->mon_form.from = (u8)BTL_POKEPARAM_GetValue(mon, 0x13);
    }
    if (mon != NULL && BTL_POKEPARAM_GetSpecies != NULL) {
        out->mon_form.species = BTL_POKEPARAM_GetSpecies(mon);
    }
    out->mon_form.to = cmd->form;
}

static void apply_mon_form_battle(const BwEventPayload *in, BwEventCall *call)
{
    BwPortFormCmd *cmd = (BwPortFormCmd *)call->a[1];

    if (cmd != NULL) {
        cmd->form = in->mon_form.to;
    }
}

static void refuse_mon_form_battle(BwEventCall *call)
{
    call->ret = 0;
}

static const BwEventBinding g_battle_rows[] = {
    { BW_MOD_REGION_OVL(93), 0x021C5620u, BW_EVENT_MON_CURE, BW_EVENT_BEFORE,
      BW_SCOPE_ANY, 0u, 0u, build_mon_cure_battle, NULL, refuse_mon_cure_battle,
      when_battle_cure },

    { BW_MOD_REGION_OVL(93), 0x021C6080u, BW_EVENT_MON_PP, BW_EVENT_BEFORE,
      BW_SCOPE_ANY, 0u, 0u, build_mon_pp_battle, apply_mon_pp_battle,
      refuse_mon_pp_battle, NULL },

    { BW_MOD_REGION_OVL(93), 0x021CA748u, BW_EVENT_MON_FORM, BW_EVENT_BEFORE,
      BW_SCOPE_ANY, 0u, 0u, build_mon_form_battle, apply_mon_form_battle,
      refuse_mon_form_battle, NULL },

    { BW_MOD_REGION_OVL(93), 0x021CC008u, BW_EVENT_MON_REVIVE, BW_EVENT_BEFORE,
      BW_SCOPE_ANY, 0u, 0u, build_mon_revive_battle, apply_mon_revive_battle,
      refuse_mon_revive_battle, when_battle_fainted },

    { BW_MOD_REGION_OVL(93), 0x021CA270u, BW_EVENT_PORT, BW_EVENT_BEFORE,
      BW_SCOPE_ANY, 0u, 0u, NULL, apply_revive_cmd, NULL, NULL },

    { BW_MOD_REGION_OVL(93), 0x021C33E8u, BW_EVENT_HEAL_HP, BW_EVENT_BEFORE,
      BW_SCOPE_ANY, 0u, 0u, build_heal_hp_battle, apply_heal_hp_battle,
      refuse_heal_hp_battle, NULL },

    { BW_MOD_REGION_ARM9, 0x02013090u, BW_EVENT_BATTLE_START, BW_EVENT_BEFORE,
      BW_SCOPE_ANY, 0u, 0u, build_battle_start, apply_battle_start, NULL, NULL },

    { BW_MOD_REGION_OVL(21), 0x021AA5CCu, BW_EVENT_BATTLE_START, BW_EVENT_AFTER,
      BW_SCOPE_ANY, 0u, 0u, build_battle_setup_trainer,
      apply_battle_setup_trainer, NULL, NULL },

    { BW_MOD_REGION_OVL(10), 0x0216E7C8u, BW_EVENT_BATTLE_START, BW_EVENT_AFTER,
      BW_SCOPE_ANY, 0u, 0u, build_battle_setup_scripted,
      apply_battle_setup_scripted, NULL, NULL },

    { BW_MOD_REGION_ARM9, 0x0201347Cu, BW_EVENT_BATTLE_START, BW_EVENT_AFTER,
      BW_SCOPE_ANY, 0u, 0u, build_battle_setup_wild, apply_battle_setup_wild,
      NULL, NULL },

    { BW_MOD_REGION_OVL(93), 0x021CF5B0u, BW_EVENT_BATTLE_ACTION,
      BW_EVENT_BEFORE, BW_SCOPE_ANY, 0u, 0u, build_battle_action_fight, NULL,
      refuse_battle_action_fight, NULL },

    { BW_MOD_REGION_OVL(93), 0x021CF918u, BW_EVENT_BATTLE_ACTION,
      BW_EVENT_BEFORE, BW_SCOPE_ANY, 0u, 0u, build_battle_action_run, NULL,
      refuse_battle_action_run, NULL },

    { BW_MOD_REGION_OVL(95), 0x0689CB1Cu, BW_EVENT_BATTLE_ACTION,
      BW_EVENT_AFTER, BW_SCOPE_INSIDE, BW_MOD_REGION_OVL(93), 0x021CEDC0u,
      build_battle_action_switch, NULL, refuse_battle_action_switch, NULL },

    { BW_MOD_REGION_OVL(93), 0x021EA01Cu, BW_EVENT_BATTLE_ITEM,
      BW_EVENT_AFTER, BW_SCOPE_INSIDE, BW_MOD_REGION_OVL(93), 0x021CEEB8u,
      build_battle_item, NULL, refuse_battle_item, NULL },

    { BW_MOD_REGION_OVL(93), 0x021BD9D4u, BW_EVENT_BATTLE_SWITCH,
      BW_EVENT_BEFORE, BW_SCOPE_ANY, 0u, 0u, build_battle_switch, NULL, NULL, NULL },

    { BW_MOD_REGION_OVL(93), 0x021BD60Cu, BW_EVENT_BATTLE_ESCAPE,
      BW_EVENT_BEFORE, BW_SCOPE_ANY, 0u, 0u, build_battle_escape, NULL,
      refuse_battle_escape, NULL },

    { BW_MOD_REGION_OVL(93), 0x021C1A10u, BW_EVENT_BATTLE_MOVE,
      BW_EVENT_BEFORE, BW_SCOPE_ANY, 0u, 0u, build_battle_move, NULL, NULL, NULL },

    { BW_MOD_REGION_OVL(93), 0x021C1E74u, BW_EVENT_BATTLE_DAMAGE,
      BW_EVENT_AFTER, BW_SCOPE_ANY, 0u, 0u, build_battle_damage,
      apply_battle_damage, NULL, NULL },

    { BW_MOD_REGION_OVL(93), 0x021C4F64u, BW_EVENT_BATTLE_FAINT,
      BW_EVENT_BEFORE, BW_SCOPE_ANY, 0u, 0u, build_battle_faint, NULL, NULL, NULL },

    { BW_MOD_REGION_OVL(93), 0x021CBA40u, BW_EVENT_CATCH_ATTEMPT,
      BW_EVENT_BEFORE, BW_SCOPE_ANY, 0u, 0u, build_catch_attempt,
      apply_catch_attempt, refuse_catch_attempt, NULL },

    { BW_MOD_REGION_OVL(93), 0x021CBB34u, BW_EVENT_CATCH_RESULT,
      BW_EVENT_AFTER, BW_SCOPE_ANY, 0u, 0u, build_catch_result,
      apply_catch_result, NULL, NULL },

    { BW_MOD_REGION_OVL(10), 0x0216EE7Cu, BW_EVENT_BATTLE_END,
      BW_EVENT_BEFORE, BW_SCOPE_ANY, 0u, 0u, build_battle_end, NULL, NULL, NULL },

    { BW_MOD_REGION_OVL(93), 0x021D1960u, BW_EVENT_WHITEOUT,
      BW_EVENT_BEFORE, BW_SCOPE_ANY, 0u, 0u, build_whiteout_battle, NULL,
      NULL, NULL },

    { BW_MOD_REGION_OVL(10), 0x02158A20u, BW_EVENT_WHITEOUT,
      BW_EVENT_BEFORE, BW_SCOPE_ANY, 0u, 0u, build_whiteout_field, NULL, NULL, NULL }
};

/* The rows of this area, for the dispatch core. */
const BwEventBinding *bw_event_bindings_battle(u32 *count)
{
    if (count != NULL) {
        *count = (u32)(sizeof(g_battle_rows) / sizeof(g_battle_rows[0]));
    }
    return g_battle_rows;
}
