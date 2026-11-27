/*
 * patches: src/game/ov096/tr_ai.c TR_AI_GetAbility TR_AI_CmdLoadCurrentAbility
 *
 * The trainer AI's scripts compare a Pokemon's ability with the game's
 * ability ids ("does the foe have Levitate", "Wonder Guard").  A mod's
 * ability that works like a built-in one (sdk/battle_handlers.h,
 * eidos/tables/ability_handlers.txt) is that ability to the scripts: the
 * ability they load is its base (the first base of a line of several).
 * The AI's guess of a foe's ability it has not seen (one of its species'
 * abilities) and the three it always knows (Shadow Tag, Magnet Pull, Arena
 * Trap) follow the base too.  With no table lines both functions are the
 * game's.
 *
 * symbols:
 *   VM_GetU32                       0x0201134C  thumb  arm9
 *   TR_AI_GetPosition               0x0218A160  thumb  ov096
 *   TR_AI_GetPoke                   0x0218A2FC  thumb  ov096
 *   GF_STANDARD_Rand                0x0203F0A8  thumb  arm9
 *   POKE_TOOL_GetPersonalParam      0x02019C90  thumb  arm9
 *   BTLV_EFFECT_GetPokeEffectValue  0x021F8AFC  thumb  ov094
 *   BTL_POKEPARAM_GetValue          0x021D59B4  thumb  ov093
 *   BTL_POKEPARAM_HasSick           0x021D62C4  thumb  ov093
 *   BTL_POKEPARAM_GetSpecies        0x021D5444  thumb  ov093
 */

typedef unsigned char u8;
typedef signed char s8;
typedef unsigned short u16;
typedef signed short s16;
typedef unsigned int u32;
typedef unsigned long long u64;

struct ScriptVm;
struct BtlPokeParam;

/* tr_ai_types.h: TrAiWork up to the fields these functions use */
typedef struct TrAiWork {
    u8 phase;
    u8 moveIndex;
    u16 moveId;
    int moveScores[4];
    int baseScores[4];
    s16 targetScores[6];
    s8 targetMove[6];
    u8 pad36[2];
    int workValue;
    u32 pendingMask;
    u32 fullMask;
    u8 flags;
    u8 scriptId;
    u8 chosenMove;
    u8 chosenTarget;
    u8 moveRoll[4];
    u16 knownMoves[2][4];
    u8 pad5C[0x20];
    u8 abilityCache[6];
    u8 pad82[0x12];
    u8 selfPos;
    u8 targetPos;
    u8 pad96[2];
    u32 rule;
    u32 competitor;
    u16 heapId;
    u8 padA2[2];
    void *scriptArchive;
    void *itemArchive;
    u8 *script;
    void *battleMain;
    void *serverFlow;
    void *sideTable;
    struct BtlPokeParam *selfPoke;
    struct BtlPokeParam *targetPoke;
    int cmdReturn;
} TrAiWork;

_Static_assert(__builtin_offsetof(TrAiWork, workValue) == 0x38, "TrAiWork.workValue");
_Static_assert(__builtin_offsetof(TrAiWork, abilityCache) == 0x7C, "TrAiWork.abilityCache");
_Static_assert(sizeof(void *) != 4 || __builtin_offsetof(TrAiWork, cmdReturn) == 0xC4, "TrAiWork.cmdReturn");

#define BPP_VALUE_ABILITY          0x10
#define BPP_VALUE_FORM             0x13
#define BPP_VALUE_CURRENT_ABILITY  0x11  /* the ability, 0 while Gastro Acid holds it */
#define BTL_SICK_GASTRO_ACID       0x10
#define PERSONAL_ABILITY_1         0x1a
#define PERSONAL_ABILITY_2         0x1b
#define PERSONAL_ABILITY_HIDDEN    0x1c

#define ABILITY_SHADOW_TAG   0x17
#define ABILITY_MAGNET_PULL  0x2a
#define ABILITY_ARENA_TRAP   0x47

extern u32 VM_GetU32(struct ScriptVm *vm);
extern u8 TR_AI_GetPosition(TrAiWork *ai, int kind);
extern struct BtlPokeParam *TR_AI_GetPoke(TrAiWork *ai, u8 pos);
extern u32 GF_STANDARD_Rand(void);
extern u32 POKE_TOOL_GetPersonalParam(int species, int form, int param);
extern int BTLV_EFFECT_GetPokeEffectValue(int pos);
extern int BTL_POKEPARAM_GetValue(struct BtlPokeParam *pp, int which);
extern int BTL_POKEPARAM_HasSick(struct BtlPokeParam *pp, int sick);
extern u16 BTL_POKEPARAM_GetSpecies(struct BtlPokeParam *pp);

/* sdk/battle_handlers.c */
extern int bw_bh_base(int type, int id);

#define BW_BH_ABILITY 4

/* The ability a script sees: the built-in one a mod's ability works like. */
static int script_ability(int ability)
{
    int base = bw_bh_base(BW_BH_ABILITY, ability);

    return base > 0 && base <= 0xFFFF ? base : ability;
}

/* tr_ai.c draw_below: a random number below n (the random word when n is 0) */
static u32 draw_below(u32 n)
{
    u32 r = GF_STANDARD_Rand();
    u64 random = r;
    if (n != 0)
        r = (random * n) >> 32;
    return r;
}

int TR_AI_CmdLoadCurrentAbility(struct ScriptVm *vm, TrAiWork *ai)
{
    int selector;
    u8 pos;

    selector = VM_GetU32(vm);
    pos = TR_AI_GetPosition(ai, selector);
    ai->workValue = script_ability(BTL_POKEPARAM_GetValue(TR_AI_GetPoke(ai, pos), BPP_VALUE_CURRENT_ABILITY));
    return ai->cmdReturn;
}

int TR_AI_GetAbility(TrAiWork *ai, int kind, u8 pos)
{
    struct BtlPokeParam *poke;
    int value, form;
    u32 ability0, ability1, ability2, abilityCount;
    u32 abilities[3];

    poke = TR_AI_GetPoke(ai, pos);
    if (BTL_POKEPARAM_HasSick(poke, BTL_SICK_GASTRO_ACID) != 0)
        return 0;
    if (kind == 0 || kind == 2) {
        ai->abilityCache[pos] = BTLV_EFFECT_GetPokeEffectValue(pos);
        if (ai->abilityCache[pos] != 0)
            return script_ability(ai->abilityCache[pos]);
        value = script_ability(BTL_POKEPARAM_GetValue(poke, BPP_VALUE_ABILITY));
        if (value == ABILITY_SHADOW_TAG || value == ABILITY_MAGNET_PULL || value == ABILITY_ARENA_TRAP)
            return value;
        value = BTL_POKEPARAM_GetSpecies(poke);
        form = BTL_POKEPARAM_GetValue(poke, BPP_VALUE_FORM);
        abilityCount = 0;
        ability0 = POKE_TOOL_GetPersonalParam(value, form, PERSONAL_ABILITY_1);
        ability1 = POKE_TOOL_GetPersonalParam(value, form, PERSONAL_ABILITY_2);
        ability2 = POKE_TOOL_GetPersonalParam(value, form, PERSONAL_ABILITY_HIDDEN);
        if (ability0 != 0) abilities[abilityCount++] = ability0;
        if (ability1 != 0) abilities[abilityCount++] = ability1;
        if (ability2 != 0) abilities[abilityCount++] = ability2;
        return script_ability((int)abilities[draw_below(abilityCount)]);
    }
    return script_ability(BTL_POKEPARAM_GetValue(poke, BPP_VALUE_ABILITY));
}
