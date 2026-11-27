/*
 * patches: src/game/ov093/btl_client.c BTL_CLIENT_IsMoveSelectionBlocked BTL_CLIENT_ShouldSwitchNoEffectiveMove BTL_CLIENT_ShouldSwitchWhileLocked BTL_CLIENT_FindSwitchByTypeMatchup BTL_CLIENT_FindMemberResistingType BTL_CLIENT_HasMoveWithAffinityVsTarget BTL_CLIENT_SortMonsByMovePower
 *
 * A move the player or the AI cannot choose (the move menu greys it, the
 * AI's usable mask leaves it out, and choosing it shows why).  To the
 * game's own reasons (the Choice item's lock, Encore, Taunt, Torment,
 * Disable, Heal Block, Imprison, Gravity) eidos's move bases add two, as
 * the later games do:
 *
 *  - needs_berry and belch (Belch): not before the Pokemon has eaten a
 *    berry since it came in (the item it used up);
 *  - throat_chop: a sound move, while Throat Chop's hold on the Pokemon
 *    lasts (the field factor stubs/ov/patch_ov093_hand_waza.c adds).
 *
 * The line that says why is eidos's own (sdk/battle_handlers.c); in a
 * language that has none it is the game's "But it failed!".
 * With no table lines the function is the game's own.
 *
 * The AI's switching tests (ShouldSwitchNoEffectiveMove,
 * ShouldSwitchWhileLocked, FindSwitchByTypeMatchup,
 * FindMemberResistingType, HasMoveWithAffinityVsTarget,
 * SortMonsByMovePower) see a Pokemon's third type too (add_type,
 * Trick-or-Treat, Forest's Curse: the server's copy holds it, sdk/
 * battle_moves.h); with none they are the game's own.
 *
 * symbols:
 *   BTL_CLIENT_IsHeldItemEffective     0x021CF89C  thumb  ov093
 *   BTL_POKEPARAM_HasSick              0x021D62C4  thumb  ov093
 *   BTL_POKEPARAM_GetSickContRaw       0x021D6314  thumb  ov093
 *   BTL_POKEPARAM_GetSickParam         0x021D62D8  thumb  ov093
 *   BTL_POKEPARAM_HasMove              0x021D5750  thumb  ov093
 *   BTL_POKEPARAM_GetItem              0x021D5B40  thumb  ov093
 *   BTL_POKEPARAM_GetPokeId            0x021D5440  thumb  ov093
 *   BTL_POKEPARAM_GetLastSelectedMoveId 0x021D67B8 thumb  ov093
 *   BTL_POKEPARAM_GetConsumedItem      0x021D6740  thumb  ov093
 *   BTL_SICKCONT_GetValue              0x021E8B80  thumb  ov093
 *   BTL_FIELD_IsSlotInUse              0x021EFCE4  thumb  ov093
 *   BTL_FIELD_IsMoveSealedBySlot       0x021EFC1C  thumb  ov093
 *   BTLV_SCU_InitMessage               0x021EAF18  thumb  ov093
 *   BTLV_SCU_AppendMessageArg          0x021EAF50  thumb  ov093
 *   BTL_EVENT_FindFactor               0x021D732C  thumb  ov093
 *   WAZA_TOOL_IsDamaging               0x0201C314  thumb
 *   WAZA_TOOL_GetFlag                  0x0201BEB8  thumb
 *   ITEM_IsBerry                       0x020213C0  thumb
 *   BTL_POKEPARAM_GetTypePair          0x021D57FC  thumb  ov093
 *   BTL_POKEPARAM_GetMoveCount         0x021D5448  thumb  ov093
 *   BTL_POKEPARAM_GetMoveId            0x021D54C0  thumb  ov093
 *   BTL_POKEPARAM_GetMovePPChecked     0x021D5584  thumb  ov093
 *   BTL_POKEPARAM_GetHitRecord         0x021D6940  thumb  ov093
 *   BTL_POKEPARAM_IsAbleToBattle       0x021D5B7C  thumb  ov093
 *   BTL_POKEPARAM_GetValue             0x021D59B4  thumb  ov093
 *   BTL_POKEPARAM_IsFainted            0x021D5B68  thumb  ov093
 *   BTL_CALC_GetTypeAffinity           0x021D794C  thumb  ov093
 *   BTL_CALC_GetTypeAffinityVsPair     0x021D799C  thumb  ov093
 *   BTL_CALC_GetScanStartIndex         0x021D7F2C  thumb  ov093
 *   BTL_MAIN_GetRule                   0x021B8588  thumb  ov093
 *   BTL_SLOT_GetCount                  0x021B9B94  thumb  ov093
 *   BTL_SLOT_GetPokeParam              0x021B9C00  thumb  ov093
 *   BTL_CLIENT_AnyMemberHasMoveVsTarget 0x021D055C thumb  ov093
 *   BTL_CLIENT_HasSlotInOrder          0x021CFBBC  thumb  ov093
 *   WAZA_TOOL_GetType                  0x0201C1C8  thumb
 *   WAZA_TOOL_GetPower                 0x0201C1D8  thumb
 */

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;

struct BtlPokeParam;
struct BtlvMessage;
struct BtlSide;
struct BtlFieldState;
struct BtlEventFactor;

/* btl_client_types.h: the fields of BtlClient this function uses */
#define CLIENT_SIDE(client)     (*(struct BtlSide **)((u8 *)(client) + 0x004))
#define CLIENT_FIELD(client)    (*(struct BtlFieldState **)((u8 *)(client) + 0x034))

/* btl_pokeparam_types.h: a status's duration word */
union SickCont {
    u32 raw;
};

#define BTL_MOVE_STRUGGLE       0xA5
#define BTL_SICK_TAUNT          0x0B
#define BTL_SICK_TORMENT        0x0C
#define BTL_SICK_DISABLE        0x0D
#define BTL_SICK_HEAL_BLOCK     0x0F
#define BTL_SICK_ENCORE         0x17
#define BTL_SICK_CHOICE_LOCK    0x1B
#define BTL_FIELD_SLOT_GRAVITY  2
#define BTL_FIELD_SLOT_IMPRISON 3
#define WAZA_FLAG_SOUND         8
#define WAZA_FLAG_GRAVITY       9
#define WAZA_FLAG_HEAL          0x0C
#define BTL_STRTYPE_STD         1
#define BTL_STRTYPE_SET         2
#define BTL_STR_FAILED          0x47    /* bank 15: "But it failed!" */
#define BTL_EVENT_TYPE_FIELD    3

/* eidos's move bases and their lines (sdk/battle_handlers.h) */
#define NAMED_NEEDS_BERRY       0x10018
#define NAMED_BELCH             0x10019
#define LINE_BANK_SET           14
#define LINE_BELCH              2045
#define LINE_THROAT_CHOP        2036
#define OWNER_THROAT_CHOP       0x68    /* + the Pokemon: patch_ov093_hand_waza.c */

extern int BTL_CLIENT_IsHeldItemEffective(void *client, struct BtlPokeParam *mon);
extern int BTL_POKEPARAM_HasSick(struct BtlPokeParam *pp, int sick);
extern union SickCont BTL_POKEPARAM_GetSickContRaw(struct BtlPokeParam *pp, int sick);
extern u32 BTL_POKEPARAM_GetSickParam(struct BtlPokeParam *pp, int sick);
extern int BTL_POKEPARAM_HasMove(struct BtlPokeParam *pp, int moveId);
extern u16 BTL_POKEPARAM_GetItem(struct BtlPokeParam *pp);
extern u8 BTL_POKEPARAM_GetPokeId(struct BtlPokeParam *pp);
extern u16 BTL_POKEPARAM_GetLastSelectedMoveId(struct BtlPokeParam *pp);
extern u16 BTL_POKEPARAM_GetConsumedItem(struct BtlPokeParam *pp);
extern u32 BTL_SICKCONT_GetValue(union SickCont cont);
extern int BTL_FIELD_IsSlotInUse(struct BtlFieldState *field, int slot);
extern int BTL_FIELD_IsMoveSealedBySlot(struct BtlFieldState *field, struct BtlSide *side, struct BtlPokeParam *mon,
                                        u32 move);
extern void BTLV_SCU_InitMessage(struct BtlvMessage *message, int kind, int line);
extern void BTLV_SCU_AppendMessageArg(struct BtlvMessage *message, u32 arg);
extern struct BtlEventFactor *BTL_EVENT_FindFactor(int factorType, int ownerId);
extern u32 WAZA_TOOL_IsDamaging(u16 move);
extern u32 WAZA_TOOL_GetFlag(u16 move, int flag);
extern int ITEM_IsBerry(u32 itemId);

/* sdk/battle_handlers.c */
extern int bw_bh_move_named(int move, int named, int key, int index, int *value);
extern int bw_bh_line_ok(u32 bank, u32 line);

/* The reason found: its line (a set of bank 14) and whether the line
 * names the move.  A language with no such line shows the game's "But it
 * failed!" (bank 15 line 0x47): the move is blocked all the same. */
static int blocked(struct BtlvMessage *outMessage, struct BtlPokeParam *mon, u32 move, int line, int withMove)
{
    if (outMessage) {
        if (!bw_bh_line_ok(LINE_BANK_SET, (u32)line)) {
            BTLV_SCU_InitMessage(outMessage, BTL_STRTYPE_STD, BTL_STR_FAILED);
            return 1;
        }
        BTLV_SCU_InitMessage(outMessage, BTL_STRTYPE_SET, line);
        BTLV_SCU_AppendMessageArg(outMessage, BTL_POKEPARAM_GetPokeId(mon));
        if (withMove) {
            BTLV_SCU_AppendMessageArg(outMessage, move);
        }
    }
    return 1;
}

int BTL_CLIENT_IsMoveSelectionBlocked(void *client, struct BtlPokeParam *mon, u32 move, struct BtlvMessage *outMessage)
{
    u32 lockedMoveId;
    u32 forcedMoveId;
    union SickCont sickCont;

    if (move == BTL_MOVE_STRUGGLE) return 0;
    if (BTL_CLIENT_IsHeldItemEffective(client, mon) && BTL_POKEPARAM_HasSick(mon, BTL_SICK_CHOICE_LOCK)) {
        sickCont = BTL_POKEPARAM_GetSickContRaw(mon, BTL_SICK_CHOICE_LOCK);
        lockedMoveId = BTL_SICKCONT_GetValue(sickCont);
        if (BTL_POKEPARAM_HasMove(mon, (int)lockedMoveId) && lockedMoveId != move) {
            if (outMessage) {
                BTLV_SCU_InitMessage(outMessage, BTL_STRTYPE_STD, 0x63);
                BTLV_SCU_AppendMessageArg(outMessage, BTL_POKEPARAM_GetItem(mon));
                BTLV_SCU_AppendMessageArg(outMessage, lockedMoveId);
            }
            return 1;
        }
    }
    if (BTL_POKEPARAM_HasSick(mon, BTL_SICK_ENCORE)) {
        forcedMoveId = BTL_SICKCONT_GetValue(BTL_POKEPARAM_GetSickContRaw(mon, BTL_SICK_ENCORE));
        if (move != forcedMoveId) {
            if (outMessage) {
                BTLV_SCU_InitMessage(outMessage, BTL_STRTYPE_STD, 0x64);
                BTLV_SCU_AppendMessageArg(outMessage, BTL_POKEPARAM_GetPokeId(mon));
                BTLV_SCU_AppendMessageArg(outMessage, forcedMoveId);
            }
            return 1;
        }
    }
    if (BTL_POKEPARAM_HasSick(mon, BTL_SICK_TAUNT) && WAZA_TOOL_IsDamaging((u16)move) == 0) {
        if (outMessage) {
            BTLV_SCU_InitMessage(outMessage, BTL_STRTYPE_SET, 0x23b);
            BTLV_SCU_AppendMessageArg(outMessage, BTL_POKEPARAM_GetPokeId(mon));
            BTLV_SCU_AppendMessageArg(outMessage, move);
        }
        return 1;
    }
    if (BTL_POKEPARAM_HasSick(mon, BTL_SICK_TORMENT) && move == BTL_POKEPARAM_GetLastSelectedMoveId(mon)) {
        if (outMessage) {
            BTLV_SCU_InitMessage(outMessage, BTL_STRTYPE_SET, 0x244);
            BTLV_SCU_AppendMessageArg(outMessage, BTL_POKEPARAM_GetPokeId(mon));
            BTLV_SCU_AppendMessageArg(outMessage, move);
        }
        return 1;
    }
    if (BTL_POKEPARAM_HasSick(mon, BTL_SICK_DISABLE) && move == BTL_POKEPARAM_GetSickParam(mon, BTL_SICK_DISABLE) &&
        move != BTL_MOVE_STRUGGLE) {
        if (outMessage) {
            BTLV_SCU_InitMessage(outMessage, BTL_STRTYPE_SET, 0x253);
            BTLV_SCU_AppendMessageArg(outMessage, BTL_POKEPARAM_GetPokeId(mon));
            BTLV_SCU_AppendMessageArg(outMessage, move);
        }
        return 1;
    }
    if (BTL_POKEPARAM_HasSick(mon, BTL_SICK_HEAL_BLOCK) && WAZA_TOOL_GetFlag((u16)move, WAZA_FLAG_HEAL)) {
        if (outMessage) {
            BTLV_SCU_InitMessage(outMessage, BTL_STRTYPE_SET, 0x37a);
            BTLV_SCU_AppendMessageArg(outMessage, BTL_POKEPARAM_GetPokeId(mon));
            BTLV_SCU_AppendMessageArg(outMessage, move);
        }
        return 1;
    }
    if (BTL_FIELD_IsSlotInUse(CLIENT_FIELD(client), BTL_FIELD_SLOT_IMPRISON) &&
        BTL_FIELD_IsMoveSealedBySlot(CLIENT_FIELD(client), CLIENT_SIDE(client), mon, move)) {
        if (outMessage) {
            BTLV_SCU_InitMessage(outMessage, BTL_STRTYPE_SET, 0x24d);
            BTLV_SCU_AppendMessageArg(outMessage, BTL_POKEPARAM_GetPokeId(mon));
            BTLV_SCU_AppendMessageArg(outMessage, move);
        }
        return 1;
    }
    if (BTL_FIELD_IsSlotInUse(CLIENT_FIELD(client), BTL_FIELD_SLOT_GRAVITY) && WAZA_TOOL_GetFlag((u16)move, WAZA_FLAG_GRAVITY)) {
        if (outMessage) {
            BTLV_SCU_InitMessage(outMessage, BTL_STRTYPE_SET, 0x43e);
            BTLV_SCU_AppendMessageArg(outMessage, BTL_POKEPARAM_GetPokeId(mon));
            BTLV_SCU_AppendMessageArg(outMessage, move);
        }
        return 1;
    }
    /* eidos's: Belch before a berry was eaten, a sound move under Throat Chop */
    if ((bw_bh_move_named((int)move, NAMED_BELCH, -1, 0, 0) || bw_bh_move_named((int)move, NAMED_NEEDS_BERRY, -1, 0, 0)) &&
        ITEM_IsBerry(BTL_POKEPARAM_GetConsumedItem(mon)) == 0) {
        return blocked(outMessage, mon, move, LINE_BELCH, 0);
    }
    if (WAZA_TOOL_GetFlag((u16)move, WAZA_FLAG_SOUND) &&
        BTL_EVENT_FindFactor(BTL_EVENT_TYPE_FIELD, OWNER_THROAT_CHOP + BTL_POKEPARAM_GetPokeId(mon)) != 0) {
        return blocked(outMessage, mon, move, LINE_THROAT_CHOP, 1);
    }
    return 0;
}

/* --- The AI's switching: the third type ----------------------------- */

/* btl_calc_types.h BtlRngState (packed to 4 off the DS, as the game's
 * header packs it) */
#if !defined(__MWERKS__)
#pragma pack(push, 4)
#endif
typedef struct {
    unsigned long long seed;
    unsigned long long mul;
    unsigned long long add;
} ClientRng;
#if !defined(__MWERKS__)
#pragma pack(pop)
#endif

/* btl_client_types.h struct BtlClient, up to the fields these functions
 * use (main, rng, sideData, slotCount) */
typedef struct {
    void *main;
    void *side;
    void *activeMon;
    void *actionCursor;
    void *recBuffer;
    void *recReader;
    u16 counter[7];             /* BtlClientCounter */
    u8 pad026[2];
    void *tickFn;
    u32 escapedCount;           /* struct BtlClientSet */
    u8 escapedIds[4];
    void *battleField;
    ClientRng rngSave;
    void *netChannel;
    void *view;
    u32 messageA[9];            /* struct BtlvMessage */
    u32 messageB[9];
    void *requestParam;
    struct {
        void *mon;
        u32 aux;
    } recvEntry[3];
    u32 recordKey;
    u32 actionKey;
    u8 windowMode;
    u8 pad0C5[3];
    void *stepFn;
    int stepState;
    void *subFn;
    u32 subState;
    u8 *sendData;
    u32 sendSize;
    int sendFlag;
    u16 timeLimit;
    u16 timeElapsed;
    u16 trainerItems[4];
    void *recorder;
    ClientRng rng;
    signed char slotOrder[6];
    u8 chosenSlots[4];
    u8 pad116[2];
    void *sideData;
    u8 slotCount;
} AiClient;

#if !defined(__LP64__) && !defined(_WIN64)
_Static_assert(__builtin_offsetof(AiClient, battleField) == 0x034, "BtlClient.battleField");
_Static_assert(__builtin_offsetof(AiClient, rng) == 0x0F4, "BtlClient.rng");
_Static_assert(__builtin_offsetof(AiClient, sideData) == 0x118, "BtlClient.sideData");
_Static_assert(__builtin_offsetof(AiClient, slotCount) == 0x11C, "BtlClient.slotCount");
#endif

/* the client's own random numbers: the next value below `range` */
static u32 ai_random(AiClient *client, u32 range)
{
    client->rng.seed = client->rng.mul * client->rng.seed + client->rng.add;
    return (u32)(((client->rng.seed >> 32) * range) >> 32);
}

#define BTL_AFF_NONE        0
#define BTL_AFF_QUARTER     1
#define BTL_AFF_NORMAL      3
#define BTL_AFF_QUADRUPLE   5

extern u16 BTL_POKEPARAM_GetTypePair(struct BtlPokeParam *pp);
extern u8 BTL_POKEPARAM_GetMoveCount(struct BtlPokeParam *pp);
extern u16 BTL_POKEPARAM_GetMoveId(struct BtlPokeParam *pp, u8 index);
extern u32 BTL_POKEPARAM_GetMovePPChecked(struct BtlPokeParam *pp, u8 index);
extern int BTL_POKEPARAM_GetHitRecord(struct BtlPokeParam *pp, int fromLast, int index, void *out);
extern int BTL_POKEPARAM_IsAbleToBattle(struct BtlPokeParam *pp);
extern int BTL_POKEPARAM_GetValue(struct BtlPokeParam *pp, int value);
extern int BTL_POKEPARAM_IsFainted(struct BtlPokeParam *pp);
extern u8 WAZA_TOOL_GetType(u16 move);
extern u16 WAZA_TOOL_GetPower(u16 move);
extern int BTL_CALC_GetTypeAffinity(int atkType, int defType);
extern int BTL_CALC_GetTypeAffinityVsPair(int atkType, int defTypes);
extern u32 BTL_CALC_GetScanStartIndex(int rule, u32 slotCount);
extern int BTL_MAIN_GetRule(void *main);
extern u32 BTL_SLOT_GetCount(void *sideData);
extern struct BtlPokeParam *BTL_SLOT_GetPokeParam(void *sideData, u8 index);
extern int BTL_CLIENT_AnyMemberHasMoveVsTarget(void *client, void *target, int minAffinity);
extern int BTL_CLIENT_HasSlotInOrder(void *client, u8 slot);

/* sdk/battle_moves.c: the third type the server's copy of a Pokemon has */
extern int bw_bm_third_type_any(int pokeId);

/* A type's affinity against a Pokemon: its two types (the game's), then
 * its third type (Trick-or-Treat, Forest's Curse), in the game's classes:
 * quarter, half, normal, double, quadruple are 1..5, one step a doubling,
 * so the third type adds its step; the class stops at quarter and
 * quadruple (the AI tests classes only). */
static int ai_affinity(int type, struct BtlPokeParam *pp)
{
    int affinity = BTL_CALC_GetTypeAffinityVsPair(type, BTL_POKEPARAM_GetTypePair(pp));
    int third = bw_bm_third_type_any(BTL_POKEPARAM_GetPokeId(pp));
    int step;

    if (third < 0 || affinity == BTL_AFF_NONE) {
        return affinity;
    }
    step = BTL_CALC_GetTypeAffinity(type, third);
    if (step == BTL_AFF_NONE) {
        return BTL_AFF_NONE;
    }
    affinity += step - BTL_AFF_NORMAL;
    if (affinity < BTL_AFF_QUARTER) {
        affinity = BTL_AFF_QUARTER;
    }
    if (affinity > BTL_AFF_QUADRUPLE) {
        affinity = BTL_AFF_QUADRUPLE;
    }
    return affinity;
}

#define BTL_SICK_LOCKED_MOVE    0x1B    /* the Choice item's lock */

int BTL_CLIENT_ShouldSwitchNoEffectiveMove(AiClient *client, struct BtlPokeParam *mon, struct BtlPokeParam *foe)
{
    u8 moveCount;
    u32 damagingCount;
    u32 i;
    u16 moveId;

    moveCount = BTL_POKEPARAM_GetMoveCount(mon);
    damagingCount = 0;
    for (i = 0; i < moveCount; i++) {
        moveId = BTL_POKEPARAM_GetMoveId(mon, (u8)i);
        if (WAZA_TOOL_IsDamaging(moveId)) {
            if (ai_affinity(WAZA_TOOL_GetType(moveId), foe))
                return 0;
            damagingCount++;
        }
    }
    if (damagingCount >= 2) {
        if (BTL_CLIENT_AnyMemberHasMoveVsTarget(client, foe, 4))
            return ai_random(client, 3) < 2;
        if (BTL_CLIENT_AnyMemberHasMoveVsTarget(client, foe, 3))
            return ai_random(client, 2) < 1;
    }
    return 0;
}

int BTL_CLIENT_ShouldSwitchWhileLocked(AiClient *client, struct BtlPokeParam *mon, void *target)
{
    u32 id;
    int valid;
    u32 kind;

    if (BTL_POKEPARAM_HasSick(mon, BTL_SICK_LOCKED_MOVE)) {
        id = BTL_SICKCONT_GetValue(BTL_POKEPARAM_GetSickContRaw(mon, BTL_SICK_LOCKED_MOVE));
        if (id) {
            valid = (int)WAZA_TOOL_IsDamaging((u16)id);
            kind = WAZA_TOOL_GetType((u16)id);
            if (ai_affinity((int)kind, (struct BtlPokeParam *)target) == 0 && valid) {
                if (BTL_CLIENT_AnyMemberHasMoveVsTarget(client, target, 4))
                    return ai_random(client, 3) < 2;
                if (BTL_CLIENT_AnyMemberHasMoveVsTarget(client, target, 3))
                    return ai_random(client, 2) < 1;
            }
            if (!valid)
                return ai_random(client, 2) < 1;
        }
    }
    return 0;
}

int BTL_CLIENT_HasMoveWithAffinityVsTarget(AiClient *client, struct BtlPokeParam *mon, void *target, int minAffinity);

int BTL_CLIENT_FindSwitchByTypeMatchup(AiClient *client, struct BtlPokeParam *mon, void *target, u8 *out)
{
    u16 hitRecord[4];
    u32 totalBoosts;
    u32 member;
    u32 partyCount;
    struct BtlPokeParam *candidate;
    int hitAffinity;

    if (BTL_CLIENT_HasMoveWithAffinityVsTarget(client, mon, target, 4)) {
        if (ai_random(client, 10) != 0) return 0;
    }
    totalBoosts = 0;
    for (member = 1; member <= 7; member++) totalBoosts += (u32)BTL_POKEPARAM_GetValue(mon, (int)member);
    if (totalBoosts >= 4) return 0;
    if (BTL_POKEPARAM_GetHitRecord(mon, 1, 0, hitRecord)) {
        partyCount = BTL_SLOT_GetCount(client->sideData);
        member = BTL_CALC_GetScanStartIndex(BTL_MAIN_GetRule(client->main), client->slotCount);
        for (; member < partyCount; member++) {
            if (!BTL_CLIENT_HasSlotInOrder(client, (u8)member)) {
                candidate = BTL_SLOT_GetPokeParam(client->sideData, (u8)member);
                if (BTL_CLIENT_HasMoveWithAffinityVsTarget(client, candidate, target, 4)) {
                    hitAffinity = ai_affinity(((u8 *)hitRecord)[4], candidate);   /* (the hit's move type) */
                    if (hitAffinity == 0) {
                        if (ai_random(client, 2) == 0) {
                            *out = (u8)member;
                            return 1;
                        }
                    } else if (hitAffinity < 3) {
                        if (ai_random(client, 3) == 0) {
                            *out = (u8)member;
                            return 1;
                        }
                    }
                }
            }
        }
    }
    return 0;
}

/* (the Western games' test: the Japanese one does not ask whether the
 * member can battle) */
int BTL_CLIENT_FindMemberResistingType(AiClient *client, u8 attackType, int maxAffinity, u8 *outMember)
{
    u32 n;
    u32 i;
    struct BtlPokeParam *m;

    n = BTL_SLOT_GetCount(client->sideData);
    i = BTL_CALC_GetScanStartIndex(BTL_MAIN_GetRule(client->main), client->slotCount);
    for (; i < n; i++) {
        if (BTL_CLIENT_HasSlotInOrder(client, (u8)i) == 0) {
            m = BTL_SLOT_GetPokeParam(client->sideData, (u8)i);
            if (BTL_POKEPARAM_IsAbleToBattle(m) != 0 && ai_affinity(attackType, m) <= maxAffinity) {
                *outMember = (u8)i;
                return 1;
            }
        }
    }
    return 0;
}

int BTL_CLIENT_HasMoveWithAffinityVsTarget(AiClient *client, struct BtlPokeParam *mon, void *target, int minAffinity)
{
    u8 moveCount;
    u32 i;
    u16 moveId;

    if (BTL_POKEPARAM_IsFainted(mon) == 0) {
        moveCount = BTL_POKEPARAM_GetMoveCount(mon);
        for (i = 0; i < moveCount; i++) {
            moveId = BTL_POKEPARAM_GetMoveId(mon, (u8)i);
            if (BTL_POKEPARAM_GetMovePPChecked(mon, (u8)i) && WAZA_TOOL_IsDamaging(moveId)
                && BTL_CLIENT_IsMoveSelectionBlocked(client, mon, moveId, 0) == 0) {
                if (ai_affinity(WAZA_TOOL_GetType(moveId), (struct BtlPokeParam *)target) >= minAffinity)
                    return 1;
            }
        }
    }
    return 0;
}

void BTL_CLIENT_SortMonsByMovePower(AiClient *client, u8 *partyOrder, u8 orderCount, void *targetMon)
{
    u16 bestPower[6];
    u8 orderIndex;
    u8 moveIndex;
    struct BtlPokeParam *partyMon;
    u16 moveId;
    u16 movePower;
    u16 swapScore;
    u8 swapIndex;

    for (orderIndex = 0; orderIndex < orderCount; orderIndex++) {
        bestPower[orderIndex] = 0;
        partyMon = BTL_SLOT_GetPokeParam(client->sideData, partyOrder[orderIndex]);
        if (!BTL_POKEPARAM_IsFainted(partyMon)) {
            u8 n = BTL_POKEPARAM_GetMoveCount(partyMon);

            for (moveIndex = 0; moveIndex < n; moveIndex++) {
                if (BTL_POKEPARAM_GetMovePPChecked(partyMon, moveIndex)) {
                    moveId = BTL_POKEPARAM_GetMoveId(partyMon, moveIndex);
                    if (WAZA_TOOL_IsDamaging(moveId)) {
                        movePower = WAZA_TOOL_GetPower(moveId);
                        if (movePower < 10) movePower = 60;
                        switch (ai_affinity(WAZA_TOOL_GetType(moveId), (struct BtlPokeParam *)targetMon)) {
                        case 5: movePower *= 4; break;
                        case 4: movePower *= 2; break;
                        case 2: movePower /= 2; break;
                        case 1: movePower /= 4; break;
                        case 0: movePower = 0; break;
                        case 3: break;
                        }
                        if (bestPower[orderIndex] < movePower) bestPower[orderIndex] = movePower;
                    }
                }
            }
        }
    }
    for (orderIndex = 0; orderIndex < orderCount; orderIndex++) {
        for (moveIndex = orderIndex + 1; moveIndex < orderCount; moveIndex++) {
            if (bestPower[orderIndex] < bestPower[moveIndex]) {
                swapScore = bestPower[orderIndex];
                bestPower[orderIndex] = bestPower[moveIndex];
                bestPower[moveIndex] = swapScore;
                swapIndex = partyOrder[orderIndex];
                partyOrder[orderIndex] = partyOrder[moveIndex];
                partyOrder[moveIndex] = swapIndex;
            }
        }
    }
}
