/*
 * patches: src/game/ov093/btl_pokeparam.c BTL_POKEPARAM_TickSleepTurn
 *
 * Early Bird outside its handlers: a sleeping Pokemon with Early Bird
 * counts its sleep turns twice as fast.  A mod's ability that works like
 * Early Bird (sdk/battle_handlers.h bw_bh_ability_is,
 * eidos/tables/ability_handlers.txt) counts as Early Bird.  With no table
 * lines the function is the game's.
 */

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;

struct PokemonParam;

/* btl_pokeparam_types.h */
union SickCont {
    u32 raw;
    struct { u32 kind : 3; u32 paramLow : 6; u32 paramHigh : 6; u32 flags : 17; } byPart;
};

/* btl_pokeparam_types.h: struct BtlPokeParam up to the fields this function uses */
struct BtlPokeParam {
    struct PokemonParam *mon;
    struct PokemonParam *illusionMon;
    u32 exp;
    u16 species;
    u16 maxHp;
    u16 hp;
    u16 heldItem;
    u16 consumedItem;
    u16 baseAbility;
    u8 level;
    u8 partyIndex;
    u8 genderRatio;
    u8 formBits;
    union SickCont sickCont[36];
    u8 sickTurn[36];
};

_Static_assert(sizeof(void *) != 4 || __builtin_offsetof(struct BtlPokeParam, sickCont) == 0x1c, "BtlPokeParam.sickCont");
_Static_assert(sizeof(void *) != 4 || __builtin_offsetof(struct BtlPokeParam, sickTurn) == 0xac, "BtlPokeParam.sickTurn");

#define BPP_VALUE_CURRENT_ABILITY 0x11  /* the ability, 0 while Gastro Acid holds it */
#define BTL_SICK_SLEEP            2
#define ABILITY_EARLY_BIRD        0x30

extern union SickCont BTL_SICKCONT_MakeNone(void);
extern u8 BTL_SICKCONT_GetLow(union SickCont slot);
extern void BTL_POKEPARAM_CureSick(struct BtlPokeParam *bpp, int sickIndex);
extern int BTL_POKEPARAM_GetValue(struct BtlPokeParam *bpp, int which);

/* sdk/battle_handlers.c */
extern int bw_bh_ability_is(int ability, int builtin);

int BTL_POKEPARAM_TickSleepTurn(struct BtlPokeParam *bpp)
{
    union SickCont *sleep = &bpp->sickCont[BTL_SICK_SLEEP];
    u8 turnLimit;
    int earlyBird;
    int step;
    if (sleep->byPart.kind != 0) {
        turnLimit = BTL_SICKCONT_GetLow(*sleep);
        if (turnLimit != 0) {
            earlyBird = bw_bh_ability_is(BTL_POKEPARAM_GetValue(bpp, BPP_VALUE_CURRENT_ABILITY), ABILITY_EARLY_BIRD);
            step = 2;
            if (earlyBird == 0) step = 1;
            bpp->sickTurn[BTL_SICK_SLEEP] += (u8)step;
            if (bpp->sickTurn[BTL_SICK_SLEEP] >= turnLimit) {
                *sleep = BTL_SICKCONT_MakeNone();
                BTL_POKEPARAM_CureSick(bpp, BTL_SICK_SLEEP);
                return 1;
            }
        }
    }
    return 0;
}
