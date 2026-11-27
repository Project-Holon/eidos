/*
 * patches: src/game/ov092/event_battle_return.c EVENT_BATTLE_RETURN_ApplyPickupAndHoneyGather EVENT_BATTLE_RETURN_ApplyNaturalCure
 *
 * The party's abilities after a battle: Pickup and Honey Gather may find
 * an item for a member holding nothing, Natural Cure clears its status.
 * A mod's ability that works like one of them (sdk/battle_handlers.h
 * bw_bh_ability_is, eidos/tables/ability_handlers.txt) counts as that
 * ability.  With no table lines both functions are the game's.
 *
 * symbols:
 *   sPickupRollThresholds  0x021B9D44  data  a1
 *   sPickupCommonItems     0x021B9D6E  data  ov092  a2
 *   sPickupRareItems       0x021B9D58  data  ov092  a2
 *   sHoneyGatherChance     0x021B9D4D  data  ov092  a1
 */

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef __UINTPTR_TYPE__ uptr;

struct PokeParty;
struct Party;
struct PokemonParam;

/* event_battle_return.c */
#define MON_PARAM_HELD_ITEM       6
#define MON_PARAM_ABILITY         0xa
#define MON_PARAM_LEVEL           0x9e
#define MON_PARAM_SPECIES_OR_EGG  0xab
#define SPECIES_EGG               0x28a
#define ABILITY_NATURAL_CURE      0x1e
#define ABILITY_PICKUP            0x35
#define ABILITY_HONEY_GATHER      0x76
#define ITEM_HONEY                0x5e

extern const u8 sPickupRollThresholds[];
extern const u16 sPickupCommonItems[];
extern const u16 sPickupRareItems[];
extern const u8 sHoneyGatherChance[];

extern u32 POKEPARTY_GetCount(struct Party *party);
extern struct PokemonParam *POKEPARTY_GetMember(struct Party *party, int index);
extern u32 POKE_TOOL_GetPartyValue(struct PokemonParam *mon, u32 id, u32 arg);
extern void POKE_TOOL_SetPartyValue(struct PokemonParam *mon, u32 id, uptr value);
extern void POKE_TOOL_SetStatusCondition(struct PokemonParam *mon, u32 status);
extern u32 GFL_USE_GetRandom(u32 range);

/* sdk/battle_handlers.c */
extern int bw_bh_ability_is(int ability, int builtin);

/* Pickup and Honey Gather: members holding nothing may find an item after the battle. */
void EVENT_BATTLE_RETURN_ApplyPickupAndHoneyGather(struct PokeParty *party)
{
    int i;
    struct PokemonParam *mon;
    u16 species, item;
    int ability;
    int roll;
    int j;
    u8 levelTier;
    int levelCeil;
    int chanceIndex;
    int level;
    for (i = 0; i < (int)POKEPARTY_GetCount((struct Party *)party); i++) {
        mon = POKEPARTY_GetMember((struct Party *)party, i);
        species = POKE_TOOL_GetPartyValue(mon, MON_PARAM_SPECIES_OR_EGG, 0);
        item = POKE_TOOL_GetPartyValue(mon, MON_PARAM_HELD_ITEM, 0);
        ability = (u8)POKE_TOOL_GetPartyValue(mon, MON_PARAM_ABILITY, 0);
        if (bw_bh_ability_is(ability, ABILITY_PICKUP) && species != 0 && species != SPECIES_EGG && item == 0 && GFL_USE_GetRandom(10) == 0) {
            roll = GFL_USE_GetRandom(100);
            levelTier = (POKE_TOOL_GetPartyValue(mon, MON_PARAM_LEVEL, 0) - 1) / 10u;
            if (levelTier >= 10) levelTier = 9;
            for (j = 0; j < 9; j++) {
                if (sPickupRollThresholds[j] > roll) {
                    POKE_TOOL_SetPartyValue(mon, MON_PARAM_HELD_ITEM, sPickupCommonItems[levelTier + j]);
                    break;
                } else if (roll >= 98 && roll <= 99) {
                    POKE_TOOL_SetPartyValue(mon, MON_PARAM_HELD_ITEM, sPickupRareItems[levelTier + (99 - roll)]);
                    break;
                }
            }
            /* (an ability of both bases: Honey Gather keeps what Pickup found) */
            item = POKE_TOOL_GetPartyValue(mon, MON_PARAM_HELD_ITEM, 0);
        }
        if (bw_bh_ability_is(ability, ABILITY_HONEY_GATHER) && species != 0 && species != SPECIES_EGG && item == 0) {
            chanceIndex = 0;
            levelCeil = 10;
            level = (u8)POKE_TOOL_GetPartyValue(mon, MON_PARAM_LEVEL, 0);
            while (levelCeil < level) {
                levelCeil += 10;
                chanceIndex++;
            }
            if (GFL_USE_GetRandom(100) < sHoneyGatherChance[chanceIndex]) POKE_TOOL_SetPartyValue(mon, MON_PARAM_HELD_ITEM, ITEM_HONEY);
        }
    }
}

/* Natural Cure: clears the status of every member that has the ability. */
void EVENT_BATTLE_RETURN_ApplyNaturalCure(struct PokeParty *party)
{
    int i;
    struct PokemonParam *mon;
    u16 species;
    u8 ability;

    for (i = 0; i < (int)POKEPARTY_GetCount((struct Party *)party); i++) {
        mon = POKEPARTY_GetMember((struct Party *)party, i);
        species = POKE_TOOL_GetPartyValue(mon, MON_PARAM_SPECIES_OR_EGG, 0);
        ability = POKE_TOOL_GetPartyValue(mon, MON_PARAM_ABILITY, 0);
        if (bw_bh_ability_is(ability, ABILITY_NATURAL_CURE) && species != 0 && species != SPECIES_EGG) {
            POKE_TOOL_SetStatusCondition(mon, 0);
        }
    }
}
