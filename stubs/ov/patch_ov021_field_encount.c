/*
 * patches: src/game/ov021/field_encount.c FIELD_ENCOUNT_ApplyLeadAbility
 *
 * The lead Pokemon's ability outside battle: more or fewer wild Pokemon
 * (Illuminate, Arena Trap, No Guard; Stench, White Smoke, Quick Feet; Sand
 * Veil and Snow Cloak in their weather), more bites (Suction Cups, Sticky
 * Hold), a held item more often (Compoundeyes), the other gender (Cute
 * Charm), Steel or Electric Pokemon (Magnet Pull, Static), higher levels
 * (Pressure, Hustle, Vital Spirit), weak Pokemon kept away (Intimidate,
 * Keen Eye), its own nature (Synchronize).  A mod's ability that works like
 * one of them (sdk/battle_handlers.h, eidos/tables/ability_handlers.txt)
 * has its effect; an ability of several bases has each base's, in line
 * order.  With no table lines the function is the game's.
 */

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;

struct GameData;
struct EncountSave;
struct MyStatus;

/* field_encount.c: the encounter parameters, filled from the lead Pokemon */
typedef struct FieldEncountParam {
    struct GameData *gameData;
    struct EncountSave *encountSave;
    int encountType;
    int triggerMode;
    u32 slotTableKind;
    u8 slotCount;
    u8 monCount;
    u16 leadSpecies;
    u16 leadItem;
    u8 leadIsEgg;
    u8 leadAbility;
    u8 leadGender;
    u8 leadNature;
    u8 leadLevel;
    u8 reserved1F;
    u32 ownerId;
    struct MyStatus *myStatus;
    u32 swarmEnabled : 1;
    u32 isFishing : 1;
    u32 repelActive : 1;
    u32 ignoreRepel : 1;
    u32 unusedFlag : 1;
    u32 isDoubleBattle : 1;
    u32 rateUp : 1;
    u32 rateDown : 1;
    u32 fishingRateUp : 1;
    u32 compoundEyes : 1;
    u32 syncNature : 1;
    u32 cuteCharm : 1;
    u32 magnetPull : 1;
    u32 staticElectric : 1;
    u32 highLevelBias : 1;
    u32 intimidate : 1;
} FieldEncountParam;

_Static_assert(sizeof(void *) != 4 || __builtin_offsetof(FieldEncountParam, leadAbility) == 0x1B, "FieldEncountParam.leadAbility");
_Static_assert(sizeof(void *) != 4 || sizeof(FieldEncountParam) == 0x2C, "FieldEncountParam");

extern u32 FIELD_ENCOUNT_RandPercent(void);

/* sdk/battle_handlers.c */
extern int bw_bh_parts(int type, int id);
extern int bw_bh_part_base(int type, int id, int part);
extern int bw_bh_ability_is(int ability, int builtin);

#define BW_BH_ABILITY 4
#define BW_BH_NAMED_FIRST 0x10000       /* eidos's own bases: no game id */

/* The game's lead effects of one built-in ability. */
static void apply_lead_ability(FieldEncountParam *param, u32 ability, u16 weather)
{
    switch (ability) {
    case 0x23:      /* Illuminate */
    case 0x47:      /* Arena Trap */
    case 0x63:      /* No Guard */
        param->rateUp = 1;
        return;
    case 0x01:      /* Stench */
    case 0x49:      /* White Smoke */
    case 0x5F:      /* Quick Feet */
        param->rateDown = 1;
        return;
    case 0x08:      /* Sand Veil */
        if (weather == 3 || weather == 12)
            param->rateDown = 1;
        return;
    case 0x51:      /* Snow Cloak */
        if (weather == 1 || weather == 4 || weather == 5)
            param->rateDown = 1;
        return;
    case 0x15:      /* Suction Cups */
    case 0x3C:      /* Sticky Hold */
        if (param->isFishing)
            param->fishingRateUp = 1;
        return;
    case 0x0E:      /* Compoundeyes */
        param->compoundEyes = 1;
        return;
    }
    if (ability == 0x38 && FIELD_ENCOUNT_RandPercent() < 0x43) {     /* Cute Charm */
        param->cuteCharm = 1;
        return;
    }
    if (FIELD_ENCOUNT_RandPercent() < 0x32)
        return;
    switch (ability) {
    case 0x2A:      /* Magnet Pull */
        param->magnetPull = 1;
        return;
    case 0x09:      /* Static */
        param->staticElectric = 1;
        return;
    case 0x2E:      /* Pressure */
    case 0x37:      /* Hustle */
    case 0x48:      /* Vital Spirit */
        param->highLevelBias = 1;
        return;
    case 0x16:      /* Intimidate */
    case 0x33:      /* Keen Eye */
        param->intimidate = 1;
        return;
    case 0x1C:      /* Synchronize */
        param->syncNature = 1;
        return;
    }
}

/* Lead Pokemon abilities that change the rate or the wild Pokemon: each
 * built-in ability the lead's works like. */
void FIELD_ENCOUNT_ApplyLeadAbility(FieldEncountParam *param, u16 weather)
{
    int k, n = bw_bh_parts(BW_BH_ABILITY, param->leadAbility);
    int base;

    for (k = 0; k < n; k++) {
        base = bw_bh_part_base(BW_BH_ABILITY, param->leadAbility, k);
        /* (eidos's own bases have no lead effect; for a game base the test
         * is always so, and says it in the log) */
        if (base > 0 && base < BW_BH_NAMED_FIRST && bw_bh_ability_is(param->leadAbility, base)) {
            apply_lead_ability(param, (u32)base, weather);
        }
    }
}
