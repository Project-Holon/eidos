/*
 * patches: src/game/ov093/btl_main.c BTL_SIDE_SetupClientParty BTL_SLOT_UpdateIllusion
 *
 * Illusion outside its handlers: when the battle sets up a party and
 * whenever a side's order changes, a Pokemon with Illusion takes the look
 * of the last Pokemon of its party that can fight (N's Zoroark the look
 * of the player's starter's line).  A mod's ability that works like
 * Illusion (sdk/battle_handlers.h bw_bh_ability_is,
 * eidos/tables/ability_handlers.txt) counts as Illusion.  With no table
 * lines both functions are the game's.
 *
 * symbols:
 *   gClientFirstPokeIds  0x021EFCF0  data  ov093
 */

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef unsigned long long u64;

struct BattleSetupParam;
struct BtlvCore;
struct BtlServer;
struct BtlClient;
struct MyStatus;
struct Party;
struct PerapVoice;
struct BtlPokeParam;

/* arm9/pms_types.h */
typedef struct PmsData {
    u16 sentenceType;
    u16 sentenceId;
    u16 word[2];
} PmsData;

/* btl_calc_types.h: 24 bytes, 4-aligned as on the DS */
#pragma pack(push, 4)
typedef struct {
    u64 seed;
    u64 mul;
    u64 add;
} BtlRngState;
#pragma pack(pop)

/* btl_calc_types.h */
struct BtlClientSet {
    u32 count;
    u8 ids[4];
};

/* btl_rec_types.h */
typedef struct {
    u8 *bytes;
    u32 size : 31;
    u32 failed : 1;
    u32 readPos[4];
    u32 lastRecord[4][16];
} BtlRecReader;

/* btl_main_types.h */
struct BtlTrainerSlot {
    void *handle;
    void *name;
    u16 trainerType;
    u16 trainerId;
    int aiFlags;
    u16 itemIds[4];
    PmsData winWord;
    PmsData loseWord;
};

struct BtlSideSlot {
    struct BtlPokeParam *items[6];
    u8 count;
    u8 pad19[3];
};

struct BtlMain;

struct BtlSide {
    struct BtlMain *main;
    struct BtlSideSlot slots[4];
    struct Party *parties[4];
    struct BtlPokeParam *pokeParams[24];
    u32 sideIndex;
};

typedef int (*BtlStepFn)(u32 *step, void *arg);

struct BtlTask {
    BtlStepFn current;
    BtlStepFn queued;
    void *arg;
    u32 step;
};

struct BtlRuleExchange {
    u32 regulation[6];
    u16 resultCount;
    u16 limitTimeGame;
    u16 limitTimeCommand;
    u8 textSpeed;
    u8 sceneOff : 4;
    u8 reserved : 4;
};

/* btl_main_types.h: struct BtlMain up to the fields these functions use */
struct BtlMain {
    struct BattleSetupParam *setup;
    struct BtlvCore *viewCore;
    struct BtlServer *server;
    struct BtlServer *subServer;
    struct BtlClient *clients[4];
    struct BtlTrainerSlot trainers[4];
    struct MyStatus *playerStatus;
    u8 clientReady[4];
    struct BtlSide sides[2];
    struct Party *battleParties[4];
    struct Party *savedParties[4];
    struct Party *tempParty;
    void *field;
    void *tempBuffer;
    void *netBuffer;
    BtlRecReader recordReader;
    struct PerapVoice *voices[4];
    BtlRngState rng;
    struct BtlRuleExchange ruleExchange;
    u8 slotClientIds[6];
    u8 pad42E[2];
    int prizeMoney;
    int bonusMoney;
    int penaltyMoney;
    u32 textSpeed;
    u16 limitTimeGame;
    u16 limitTimeCommand;
    u32 outcome;
    struct BtlClientSet defeatedClients;
    struct BtlTask task;
    u32 subStep;
    int (*tick)(struct BtlMain *main);
    u16 heapId;
};

_Static_assert(sizeof(void *) != 4 || sizeof(struct BtlSide) == 0xE8, "BtlSide");
_Static_assert(sizeof(void *) != 4 || __builtin_offsetof(struct BtlMain, sides) == 0x0C8, "BtlMain.sides");
_Static_assert(sizeof(void *) != 4 || __builtin_offsetof(struct BtlMain, tempBuffer) == 0x2C0, "BtlMain.tempBuffer");
_Static_assert(sizeof(void *) != 4 || __builtin_offsetof(struct BtlMain, heapId) == 0x468, "BtlMain.heapId");

#define BPP_VALUE_CURRENT_ABILITY 0x11  /* the ability, 0 while Gastro Acid holds it */
#define ABILITY_ILLUSION          0x95
#define SPECIES_ZOROARK           0x23B

extern const u8 gClientFirstPokeIds[4];

extern struct Party *BTL_MAIN_GetClientParty(struct BtlMain *main, int clientId, u32 sideIndex);
extern u32 POKEPARTY_GetCount(struct Party *party);
extern void *POKEPARTY_GetMember(struct Party *party, int index);
extern struct BtlPokeParam *BTL_POKEPARAM_Create(void *mon, u8 pokeId, u32 heapId);
extern void BTL_SLOT_Add(struct BtlSideSlot *slot, struct BtlPokeParam *poke);
extern int BTL_MAIN_FindLastLivingPartyIndex(struct Party *party);
extern int BTL_POKEPARAM_GetValue(struct BtlPokeParam *pp, int which);
extern void BTL_POKEPARAM_SetFakeMon(struct BtlPokeParam *pp, void *fakeMon);
extern void BTL_POKEPARAM_ClearFakeMon(struct BtlPokeParam *pp);
extern void *BTL_POKEPARAM_GetMon(struct BtlPokeParam *pp);
extern void *BTL_POKEPARAM_GetDisplayMon(struct BtlPokeParam *pp);
extern int BTL_POKEPARAM_IsIllusionActive(struct BtlPokeParam *pp);
extern int BTL_POKEPARAM_IsAbleToBattle(struct BtlPokeParam *pp);
extern u16 BTL_POKEPARAM_GetSpecies(struct BtlPokeParam *pp);
extern u32 BTL_MAIN_GetCompetitor(struct BtlMain *main);
extern int BTL_MAIN_GetRule(struct BtlMain *main);
extern int BTL_MAIN_CountClientTrainers(struct BtlMain *main, int index);
extern u32 POKE_TOOL_GetPartyValue(void *mon, u32 id, u32 arg);
extern void *POKE_TOOL_Create(u16 species, u16 level, u32 id, u32 heapId);
extern void BTL_SLOT_CompactLiving(struct BtlSideSlot *slot);

/* sdk/battle_handlers.c */
extern int bw_bh_ability_is(int ability, int builtin);

static int has_illusion(struct BtlPokeParam *poke)
{
    return bw_bh_ability_is(BTL_POKEPARAM_GetValue(poke, BPP_VALUE_CURRENT_ABILITY), ABILITY_ILLUSION);
}

void BTL_SIDE_SetupClientParty(struct BtlSide *side, struct BtlMain *main, u8 clientId)
{
    u32 slotOffset;
    char *slotBase;
    struct Party *party;
    u32 count;
    struct BtlPokeParam *poke;
    void *mon;
    u8 newSpecies;
    u8 firstPokeId;
    u32 j;
    int i;
    u8 pokeId;
    int lastValid;
    party = BTL_MAIN_GetClientParty(main, clientId, side->sideIndex);
    slotBase = (char *)side->slots;
    slotOffset = clientId * sizeof(struct BtlSideSlot);
    count = POKEPARTY_GetCount(party);
    firstPokeId = gClientFirstPokeIds[clientId];
    side->parties[clientId] = party;
    for (i = 0, pokeId = firstPokeId; i < (int)count; i++, pokeId++) {
        mon = POKEPARTY_GetMember(party, i);
        side->pokeParams[pokeId] = BTL_POKEPARAM_Create(mon, pokeId, 0x12);
        BTL_SLOT_Add((struct BtlSideSlot *)(slotBase + slotOffset), side->pokeParams[pokeId]);
    }
    lastValid = BTL_MAIN_FindLastLivingPartyIndex(party);
    if (lastValid > 0) {
        for (i = 0, pokeId = firstPokeId; i < lastValid; i++, pokeId++) {
            if (has_illusion(side->pokeParams[pokeId]))
                BTL_POKEPARAM_SetFakeMon(side->pokeParams[pokeId], POKEPARTY_GetMember(party, lastValid));
        }
    }
    if (BTL_MAIN_GetCompetitor(main) == 0 && clientId == 1) {
        for (j = 0; j < count; j++, firstPokeId++) {
            poke = side->pokeParams[firstPokeId];
            if (BTL_POKEPARAM_GetSpecies(poke) == SPECIES_ZOROARK && has_illusion(poke)) {
                struct Party *playerParty;
                u8 playerCount, q;
                playerParty = BTL_MAIN_GetClientParty(main, 0, side->sideIndex);
                newSpecies = 0;
                playerCount = POKEPARTY_GetCount(playerParty);
                for (q = 0; q < playerCount; q++) {
                    u16 species = POKE_TOOL_GetPartyValue(POKEPARTY_GetMember(playerParty, q), 5, 0);
                    if (species == 0xf3) { newSpecies = 0xf4; break; }
                    if (species == 0xf4) { newSpecies = 0xf5; break; }
                    if (species == 0xf5) { newSpecies = 0xf3; break; }
                }
                if (newSpecies != 0) {
                    if (main->tempBuffer == 0)
                        main->tempBuffer = POKE_TOOL_Create(newSpecies, BTL_POKEPARAM_GetValue(poke, 0xf), 0, main->heapId);
                    BTL_POKEPARAM_SetFakeMon(poke, main->tempBuffer);
                }
            }
        }
    }
    BTL_SLOT_CompactLiving((struct BtlSideSlot *)(slotBase + slotOffset));
}

void BTL_SLOT_UpdateIllusion(struct BtlMain *main, struct BtlSideSlot *slot, int firstIndex)
{
    int lastAlive;
    struct BtlPokeParam *poke;
    void *displayMon;

    for (lastAlive = slot->count - 1; lastAlive > 0; lastAlive--) {
        if (BTL_POKEPARAM_IsAbleToBattle(slot->items[lastAlive]) != 0)
            break;
    }
    if (BTL_MAIN_GetRule(main) != 3)
        firstIndex = BTL_MAIN_CountClientTrainers(main, firstIndex);
    else
        firstIndex = 3;
    for (; firstIndex < slot->count; firstIndex++) {
        poke = slot->items[firstIndex];
        if (has_illusion(poke)) {
            if (firstIndex < lastAlive) {
                BTL_POKEPARAM_SetFakeMon(poke, BTL_POKEPARAM_GetMon(slot->items[lastAlive]));
            } else {
                displayMon = BTL_POKEPARAM_GetDisplayMon(poke);
                if (displayMon != main->tempBuffer)
                    BTL_POKEPARAM_ClearFakeMon(poke);
            }
        }
    }
    for (firstIndex = 0; firstIndex < slot->count; firstIndex++)
        BTL_POKEPARAM_IsIllusionActive(slot->items[firstIndex]);
}
