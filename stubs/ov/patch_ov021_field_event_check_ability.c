/*
 * patches: src/game/ov021/field_event_check.c FIELD_EVENT_CHECK_GetEggCycleDecrement
 *
 * Eggs hatch twice as fast while a party member that is not an egg has
 * Flame Body or Magma Armor.  A mod's ability that works like one of them
 * (sdk/battle_handlers.h bw_bh_ability_is, eidos/tables/ability_handlers.txt)
 * counts as that ability.  With no table lines the function is the game's.
 */

typedef unsigned int u32;

#define MON_PARAM_ABILITY     0xA
#define MON_PARAM_IS_EGG      0x4C
#define ABILITY_MAGMA_ARMOR   0x28
#define ABILITY_FLAME_BODY    0x31

extern int POKEPARTY_GetCount(void *party);
extern void *POKEPARTY_GetMember(void *party, int index);
extern u32 POKE_TOOL_GetPartyValue(void *mon, u32 id, u32 arg);

/* sdk/battle_handlers.c */
extern int bw_bh_ability_is(int ability, int builtin);

/*
 * Returns 2 when a non-egg party member has the ability Flame Body (0x31) or Magma Armor (0x28), else 1.
 */
int FIELD_EVENT_CHECK_GetEggCycleDecrement(void *party)
{
    int memberCount = POKEPARTY_GetCount(party);
    int index;

    for (index = 0; index < memberCount; index++) {
        void *member = POKEPARTY_GetMember(party, index);
        u32 ability = POKE_TOOL_GetPartyValue(member, MON_PARAM_ABILITY, 0);
        if (POKE_TOOL_GetPartyValue(member, MON_PARAM_IS_EGG, 0) == 0
            && (bw_bh_ability_is((int)ability, ABILITY_FLAME_BODY) || bw_bh_ability_is((int)ability, ABILITY_MAGMA_ARMOR)))
            return 2;
    }
    return 1;
}
