/*
 * patches: src/game/ov093/btl_client.c BTL_CLIENT_IsHeldItemEffective BTL_CLIENT_GetEscapeBlockerForSide BTL_CLIENT_FindTrapper BTL_CLIENT_CanShadowTagTrap BTL_CLIENT_CanArenaTrapTrap BTL_CLIENT_ShouldSwitchVsWonderGuard BTL_CLIENT_FindNaturalCureSwitch BTL_CLIENT_FindMemberWithAbility
 *
 * The client's own tests of an ability by its id: whether a held item
 * works (Klutz), whether a Pokemon can flee or switch (Run Away, Shadow
 * Tag, Arena Trap, Magnet Pull, Levitate against Arena Trap: the menus
 * refuse Run and the switch with the trapper's name), and the trainer AI's
 * switch choices (a foe with Wonder Guard, its own Natural Cure, a member
 * that absorbs the type it was hit with).  A mod's ability that works like
 * the tested one (sdk/battle_handlers.h bw_bh_ability_is,
 * eidos/tables/ability_handlers.txt) counts as that ability; the trapper's
 * name shown is its own.  With no table lines each function is the game's.
 */

typedef unsigned char u8;
typedef signed char s8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef unsigned long long u64;

struct BtlMain;
struct BtlSide;
struct BtlSideSlot;
struct BtlPokeParam;
struct BtlvCore;
struct BtlServer;
struct BtlHitRecord;
typedef struct BtlClient BtlClient;

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

/* btlv_core_types.h */
struct BtlvMessage {
    u16 msgId;
    u8 param;
    u8 kind : 4;
    u8 argCount : 4;
    u32 args[8];
};

/* btl_client_types.h */
typedef struct {
    u8 state;
    u8 mode;
    u8 flags;
    u8 pad3;
    u16 confirmDelay;
    u16 current;
    u16 target;
    u16 maximum;
    u16 tick;
} BtlClientCounter;

typedef struct BtlClientRecvMon {
    struct BtlPokeParam *mon;
    u32 aux;
} BtlClientRecvMon;

/* btl_client_types.h: struct BtlClient up to the fields these functions use */
struct BtlClient {
    struct BtlMain *main;
    struct BtlSide *side;
    struct BtlPokeParam *activeMon;
    void *actionCursor;
    void *recBuffer;
    void *recReader;
    BtlClientCounter counter;
    u8 pad026[2];
    int (*tickFn)(BtlClient *);
    struct BtlClientSet defeatedClients;
    void *battleField;
    BtlRngState rngSave;
    void *netChannel;
    struct BtlvCore *view;
    struct BtlvMessage messageA;
    struct BtlvMessage messageB;
    struct BtlServer *requestParam;
    BtlClientRecvMon recvEntry[3];
    u32 recordKey;
    u32 actionKey;
    u8 windowMode;
    u8 pad0C5[0x3];
    int (*stepFn)(BtlClient *, int *);
    int stepState;
    u32 (*subFn)(BtlClient *, u32 *);
    u32 subState;
    u8 *sendData;
    u32 sendSize;
    int sendFlag;
    u16 timeLimit;
    u16 timeElapsed;
    u16 trainerItems[4];
    void *recorder;
    BtlRngState rng;
    s8 slotOrder[6];
    u8 chosenSlots[4];
    u8 pad116[0x2];
    struct BtlSideSlot *sideData;
    u8 slotCount;
};

_Static_assert(sizeof(void *) != 4 || __builtin_offsetof(struct BtlClient, battleField) == 0x034, "BtlClient.battleField");
_Static_assert(sizeof(void *) != 4 || __builtin_offsetof(struct BtlClient, rng) == 0x0F4, "BtlClient.rng");
_Static_assert(sizeof(void *) != 4 || __builtin_offsetof(struct BtlClient, sideData) == 0x118, "BtlClient.sideData");
_Static_assert(sizeof(void *) != 4 || __builtin_offsetof(struct BtlClient, slotCount) == 0x11C, "BtlClient.slotCount");

/* btl_pokeparam value ids */
#define BPP_VALUE_CURRENT_ABILITY 0x11  /* the ability, 0 while Gastro Acid holds it */

#define ABILITY_SHADOW_TAG    0x17
#define ABILITY_WONDER_GUARD  0x19
#define ABILITY_LEVITATE      0x1a
#define ABILITY_NATURAL_CURE  0x1e
#define ABILITY_MAGNET_PULL   0x2a
#define ABILITY_RUN_AWAY      0x32
#define ABILITY_ARENA_TRAP    0x47
#define ABILITY_KLUTZ         103

extern int BTL_FIELD_IsSlotInUse(void *field, int slot);
extern int BTL_POKEPARAM_HasSick(struct BtlPokeParam *pp, int sick);
extern int BTL_POKEPARAM_GetValue(struct BtlPokeParam *pp, int which);
extern u16 BTL_POKEPARAM_GetItem(struct BtlPokeParam *pp);
extern u8 BTL_POKEPARAM_GetPokeId(struct BtlPokeParam *pp);
extern int BTL_POKEPARAM_IsFainted(struct BtlPokeParam *pp);
extern int BTL_POKEPARAM_HasType(struct BtlPokeParam *pp, int type);
extern int BTL_POKEPARAM_GetHpRatioFixed(struct BtlPokeParam *pp);
extern int BTL_POKEPARAM_GetHitRecord(struct BtlPokeParam *pp, int kind, int index, struct BtlHitRecord *out);
extern struct BtlPokeParam *BTL_SLOT_GetPokeParam(struct BtlSideSlot *slot, u8 index);
extern u32 BTL_SLOT_GetCount(struct BtlSideSlot *slot);
extern u8 BTL_MAIN_CollectLivingPokeIds(struct BtlMain *main, struct BtlSide *side, u16 code, u8 *out);
extern int BTL_MAIN_GetTrainerSlotOfPoke(struct BtlMain *main, struct BtlSide *side, int pokeId);
extern int BTL_MAIN_GetRule(struct BtlMain *main);
extern struct BtlPokeParam *BTL_SIDE_GetPokeByIdThunk(struct BtlSide *side, int pokeId);
extern int BTL_CALC_GetScanStartIndex(int rule, int slotCount);
int BTL_CLIENT_CanMagnetPullTrap(BtlClient *client, struct BtlPokeParam *mon);
int BTL_CLIENT_HasSlotInOrder(BtlClient *client, int v);
int BTL_CLIENT_AnyMemberHasMoveVsTarget(BtlClient *client, void *target, int minAffinity);
int BTL_CLIENT_FindMemberResistingType(BtlClient *client, u8 attackType, int maxAffinity, u8 *outMember);
int BTL_CLIENT_HasMoveWithAffinityVsTarget(BtlClient *client, struct BtlPokeParam *mon, void *target, int minAffinity);
int BTL_CLIENT_CanShadowTagTrap(BtlClient *client, struct BtlPokeParam *mon);
int BTL_CLIENT_CanArenaTrapTrap(BtlClient *client, struct BtlPokeParam *mon);
u32 BTL_CLIENT_FindTrapper(BtlClient *client, struct BtlPokeParam *mon, u8 *outBlockerPokeId, u16 *outBlockerAbility);

/* sdk/battle_handlers.c */
extern int bw_bh_ability_is(int ability, int builtin);

static int has_ability(struct BtlPokeParam *mon, int builtin)
{
    return bw_bh_ability_is(BTL_POKEPARAM_GetValue(mon, BPP_VALUE_CURRENT_ABILITY), builtin);
}

int BTL_CLIENT_IsHeldItemEffective(BtlClient *client, struct BtlPokeParam *mon)
{
    if (BTL_FIELD_IsSlotInUse(client->battleField, 7)) return 0;
    if (BTL_POKEPARAM_HasSick(mon, 19)) return 0;
    if (!has_ability(mon, ABILITY_KLUTZ)) return 1;
    return 0;
}

u32 BTL_CLIENT_GetEscapeBlockerForSide(BtlClient *client, u8 *outBlockerPokeId, u16 *outBlockerAbility)
{
    u32 i;
    struct BtlPokeParam *mon;
    u32 result;

    for (i = 0; i < client->slotCount; i++) {
        mon = BTL_SLOT_GetPokeParam(client->sideData, (u8)i);
        if (BTL_POKEPARAM_IsFainted(mon) == 0) {
            if (BTL_CLIENT_IsHeldItemEffective(client, mon) != 0 && BTL_POKEPARAM_GetItem(mon) == 0xe4)
                return 4;
            if (has_ability(mon, ABILITY_RUN_AWAY))
                return 4;
        }
    }
    for (i = 0; i < client->slotCount; i++) {
        result = BTL_CLIENT_FindTrapper(client, BTL_SLOT_GetPokeParam(client->sideData, (u8)i), outBlockerPokeId, outBlockerAbility);
        if (result != 4)
            return result;
    }
    return 4;
}

/* The foe that traps the Pokemon, and its ability as it shows (its own id). */
u32 BTL_CLIENT_FindTrapper(BtlClient *client, struct BtlPokeParam *mon, u8 *outBlockerPokeId, u16 *outBlockerAbility)
{
    u8 foeSlots[4];
    u8 foeCount;
    u8 i;
    struct BtlPokeParam *foe;
    u16 foeAbility;

    foeCount = BTL_MAIN_CollectLivingPokeIds(client->main, client->side, (u16)(BTL_MAIN_GetTrainerSlotOfPoke(client->main, client->side, BTL_POKEPARAM_GetPokeId(mon)) | 0x100), foeSlots);
    for (i = 0; i < foeCount; i++) {
        foe = BTL_SIDE_GetPokeByIdThunk(client->side, foeSlots[i]);
        foeAbility = (u16)BTL_POKEPARAM_GetValue(foe, BPP_VALUE_CURRENT_ABILITY);
        if (bw_bh_ability_is(foeAbility, ABILITY_SHADOW_TAG) && BTL_CLIENT_CanShadowTagTrap(client, mon)) {
            *outBlockerPokeId = BTL_POKEPARAM_GetPokeId(foe);
            *outBlockerAbility = foeAbility;
            return 0;
        }
        if (bw_bh_ability_is(foeAbility, ABILITY_ARENA_TRAP) && BTL_CLIENT_CanArenaTrapTrap(client, mon)) {
            *outBlockerPokeId = BTL_POKEPARAM_GetPokeId(foe);
            *outBlockerAbility = foeAbility;
            return 0;
        }
        if (bw_bh_ability_is(foeAbility, ABILITY_MAGNET_PULL) && BTL_CLIENT_CanMagnetPullTrap(client, mon)) {
            *outBlockerPokeId = BTL_POKEPARAM_GetPokeId(foe);
            *outBlockerAbility = foeAbility;
            return 0;
        }
    }
    if (BTL_POKEPARAM_HasSick(mon, 0x16) || BTL_POKEPARAM_HasSick(mon, 8) || BTL_POKEPARAM_HasSick(mon, 0x15)) {
        *outBlockerPokeId = BTL_POKEPARAM_GetPokeId(mon);
        *outBlockerAbility = 0;
        return 3;
    }
    return 4;
}

int BTL_CLIENT_CanShadowTagTrap(BtlClient *client, struct BtlPokeParam *mon)
{
    (void)client;
    if (!has_ability(mon, ABILITY_SHADOW_TAG)) return 1;
    return 0;
}

int BTL_CLIENT_CanArenaTrapTrap(BtlClient *client, struct BtlPokeParam *mon)
{
    u32 itemEffective;

    itemEffective = BTL_CLIENT_IsHeldItemEffective(client, mon);
    if (BTL_FIELD_IsSlotInUse(client->battleField, 2) != 0)
        return 1;
    if (BTL_POKEPARAM_HasSick(mon, 0x1f) != 0)
        return 1;
    if (BTL_POKEPARAM_HasSick(mon, 0x15) != 0)
        return 1;
    if (itemEffective != 0 && BTL_POKEPARAM_GetItem(mon) == 0x116)
        return 1;
    if (has_ability(mon, ABILITY_LEVITATE))
        return 0;
    if (BTL_POKEPARAM_HasType(mon, 2) != 0)
        return 0;
    if (BTL_POKEPARAM_HasSick(mon, 0x1e) != 0)
        return 0;
    if (BTL_POKEPARAM_HasSick(mon, 0x20) != 0)
        return 0;
    if (itemEffective != 0 && BTL_POKEPARAM_GetItem(mon) == 0x21d)
        return 0;
    return 1;
}

int BTL_CLIENT_ShouldSwitchVsWonderGuard(BtlClient *client, struct BtlPokeParam *mon, struct BtlPokeParam *foe)
{
    if (BTL_MAIN_GetRule(client->main) != 0)
        return 0;
    if (has_ability(foe, ABILITY_WONDER_GUARD)) {
        if (BTL_CLIENT_HasMoveWithAffinityVsTarget(client, mon, foe, 4) == 0 && BTL_CLIENT_AnyMemberHasMoveVsTarget(client, foe, 4) != 0) {
            client->rng.seed = client->rng.mul * client->rng.seed + client->rng.add;
            if ((u32)(((client->rng.seed >> 32) * 3) >> 32) < 2)
                return 1;
        }
    }
    return 0;
}

int BTL_CLIENT_FindNaturalCureSwitch(BtlClient *client, struct BtlPokeParam *mon, u8 *out)
{
    u16 hitRecord[4];
    u8 member;

    if (has_ability(mon, ABILITY_NATURAL_CURE)) {
        if (BTL_POKEPARAM_HasSick(mon, 2) || BTL_POKEPARAM_HasSick(mon, 3)) {
            if (BTL_POKEPARAM_GetHpRatioFixed(mon) >= 0x32000) {
                if (BTL_POKEPARAM_GetHitRecord(mon, 1, 0, (struct BtlHitRecord *)hitRecord)) {
                    if (BTL_CLIENT_FindMemberResistingType(client, ((u8 *)hitRecord)[4], 2, &member)) {
                        *out = member;
                        return 1;
                    }
                } else {
                    client->rng.seed = client->rng.mul * client->rng.seed + client->rng.add;
                    return (u32)(((client->rng.seed >> 32) * 2) >> 32) ? 0 : 1;
                }
            }
        }
    }
    return 0;
}

int BTL_CLIENT_FindMemberWithAbility(BtlClient *client, u16 ability, u8 *outMember)
{
    u32 n;
    u32 i;
    struct BtlPokeParam *member;

    n = BTL_SLOT_GetCount(client->sideData);
    i = BTL_CALC_GetScanStartIndex(BTL_MAIN_GetRule(client->main), client->slotCount);
    for (; i < n; i++) {
        if (BTL_CLIENT_HasSlotInOrder(client, (u8)i) == 0) {
            member = BTL_SLOT_GetPokeParam(client->sideData, (u8)i);
            if (BTL_POKEPARAM_IsFainted(member) == 0 && has_ability(member, ability)) {
                *outMember = i;
                return 1;
            }
        }
    }
    return 0;
}
