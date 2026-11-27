/*
 * patches: src/game/ov095/btl_id_sets.c BTL_ID_IsAbilityRolePlayBlocked BTL_ID_IsAbilityTraceBlocked BTL_ID_IsAbilitySkillSwapBlocked BTL_ID_IsMultitypeAbility
 *
 * The abilities Role Play, Entrainment, Trace and Skill Swap cannot copy
 * or swap, and Multitype (which Gastro Acid and an ability change leave
 * alone), take a mod's ability that works like a listed one
 * (sdk/battle_handlers.h bw_bh_ability_is, eidos/tables/ability_handlers.txt)
 * as that ability: an ability of several bases is refused when one of its
 * bases is.  BTL_ID_IsAbilityEntrainmentBlocked is Trace's test, and Mold
 * Breaker's list is asked with the base of the row by its skip check
 * (stubs/ov/patch_ov093_hand_tokusei.c).  With no table lines each
 * function is the game's own test.
 *
 * symbols:
 *   gRolePlayBlockedAbilities   0x0689E2E6  data  a2
 *   gSkillSwapBlockedAbilities  0x0689E2A4  data  a2
 */

typedef unsigned short u16;

#define ABILITY_WONDER_GUARD 25         /* Trace copies it; Role Play does not */
#define ABILITY_MULTITYPE    0x79

extern u16 gRolePlayBlockedAbilities[8];
extern u16 gSkillSwapBlockedAbilities[3];

int BTL_ID_IsInList(unsigned id, const u16 *list, unsigned count);

/* sdk/battle_handlers.c */
extern int bw_bh_ability_is(int ability, int builtin);
extern int bw_bh_parts(int type, int id);
extern int bw_bh_part_base(int type, int id, int part);

#define BW_BH_ABILITY 4

static int role_play_blocked(unsigned base)
{
    if (base == ABILITY_MULTITYPE) return 1;
    return BTL_ID_IsInList(base, gRolePlayBlockedAbilities, 8);
}

int BTL_ID_IsAbilityRolePlayBlocked(unsigned abilityId)
{
    int k, n = bw_bh_parts(BW_BH_ABILITY, (int)abilityId);

    for (k = 0; k < n; k++) {
        if (role_play_blocked((unsigned)bw_bh_part_base(BW_BH_ABILITY, (int)abilityId, k))) return 1;
    }
    return 0;
}

int BTL_ID_IsAbilityTraceBlocked(unsigned abilityId)
{
    int k, n = bw_bh_parts(BW_BH_ABILITY, (int)abilityId);
    unsigned base;

    for (k = 0; k < n; k++) {
        base = (unsigned)bw_bh_part_base(BW_BH_ABILITY, (int)abilityId, k);
        if (base != ABILITY_WONDER_GUARD && role_play_blocked(base)) return 1;
    }
    return 0;
}

int BTL_ID_IsAbilitySkillSwapBlocked(unsigned abilityId)
{
    int k, n = bw_bh_parts(BW_BH_ABILITY, (int)abilityId);
    unsigned base;

    for (k = 0; k < n; k++) {
        base = (unsigned)bw_bh_part_base(BW_BH_ABILITY, (int)abilityId, k);
        if (base == ABILITY_MULTITYPE || BTL_ID_IsInList(base, gSkillSwapBlockedAbilities, 3)) return 1;
    }
    return 0;
}

int BTL_ID_IsMultitypeAbility(int abilityId)
{
    return bw_bh_ability_is(abilityId, ABILITY_MULTITYPE);
}
