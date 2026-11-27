/*
 * sdk/poke_value.c -- the Pokemon record's getters, with a mod's Multitype
 * (docs/battle-handlers.md, "Checks of an ability by its id").
 *
 * The game's accessor of the box area, arm9 poke_tool.c
 * POKE_TOOL_GetBoxValueRaw, gives Arceus's types from its held plate when
 * its ability is Multitype (id 121): the party, box and summary screens and
 * the battle (BTL_POKEPARAM_Create) read its types so.  A mod's ability
 * that works like Multitype (eidos/tables/ability_handlers.txt, `165 = 121`)
 * is another id, so the accessor would give the species' own types.
 *
 * The accessor has only two callers, POKE_TOOL_GetBoxValue and
 * POKE_TOOL_GetPartyValueRaw.  These are the game's two (poke_tool.c), and
 * each asks multitype_type first; the accessor itself stays the game's.
 * With no table lines they do what the game's do.
 */

#include "types.h"

/* arm9/poke_tool_types.h */
struct Mail;
struct BoxPokemon;

struct PokemonParam {
    u8 box[0x88];                   /* struct BoxPokemon */
    u32 statusCondition;
    u8 level;
    u8 capsuleIndex;
    u16 hp;
    u16 maxHp;
    u16 attack;
    u16 defense;
    u16 speed;
    u16 spAttack;
    u16 spDefense;
    u8 mail[0x38];                  /* struct Mail */
    u8 pad[8];
};

_Static_assert(sizeof(struct PokemonParam) == 0xDC, "struct PokemonParam");

/* arm9/poke_tool_param.h */
#define POKE_TOOL_PARAM_SPECIES           0x05
#define POKE_TOOL_PARAM_HELD_ITEM         0x06
#define POKE_TOOL_PARAM_ABILITY           0x0A
#define POKE_TOOL_PARAM_STATUS_CONDITION  0x9D
#define POKE_TOOL_PARAM_LEVEL             0x9E
#define POKE_TOOL_PARAM_CAPSULE_INDEX     0x9F
#define POKE_TOOL_PARAM_HP                0xA0
#define POKE_TOOL_PARAM_MAX_HP            0xA1
#define POKE_TOOL_PARAM_ATTACK            0xA2
#define POKE_TOOL_PARAM_DEFENSE           0xA3
#define POKE_TOOL_PARAM_SPEED             0xA4
#define POKE_TOOL_PARAM_SP_ATTACK         0xA5
#define POKE_TOOL_PARAM_SP_DEFENSE        0xA6
#define POKE_TOOL_PARAM_MAIL              0xA7
#define POKE_TOOL_PARAM_NO_DATA           0xA8
#define POKE_TOOL_PARAM_TYPE1             0xAE
#define POKE_TOOL_PARAM_TYPE2             0xAF

#define SPECIES_ARCEUS     493
#define ABILITY_MULTITYPE  121

/* (arm9/poke_tool.h: PokeToolArg is a pointer-sized word) */
typedef __UINTPTR_TYPE__ PokeToolArg;

u32 POKE_TOOL_GetBoxValueRaw(struct BoxPokemon *box, u32 paramId, PokeToolArg arg);
u32 POKE_TOOL_DecodeBoxBlock(struct BoxPokemon *box);
void POKE_TOOL_EncodeBoxBlock(struct BoxPokemon *box);
u32 POKE_TOOL_GetPlateType(u32 item);
void MAIL_Copy(const struct Mail *src, struct Mail *dest);

/* sdk/battle_handlers.c */
int bw_bh_ability_is(int ability, int builtin);

/* sdk/mon_form.c: a form past the box record's room (weak: a test may
 * link this file alone) */
extern int bw_mon_form_get(struct BoxPokemon *box, u32 paramId, u32 *value) __attribute__((weak));

static int mon_form(struct BoxPokemon *box, u32 paramId, u32 *value)
{
    return bw_mon_form_get != NULL && bw_mon_form_get(box, paramId, value);
}

/* Arceus's type from its plate when a mod's ability works like Multitype
 * (the game's own Multitype is the accessor's): 1 with *type set. */
static int multitype_type(struct BoxPokemon *box, u32 paramId, u32 *type)
{
    u32 ability;

    if (paramId != POKE_TOOL_PARAM_TYPE1 && paramId != POKE_TOOL_PARAM_TYPE2) {
        return 0;
    }
    if (POKE_TOOL_GetBoxValueRaw(box, POKE_TOOL_PARAM_SPECIES, 0) != SPECIES_ARCEUS) {
        return 0;
    }
    ability = POKE_TOOL_GetBoxValueRaw(box, POKE_TOOL_PARAM_ABILITY, 0);
    if (ability == ABILITY_MULTITYPE || !bw_bh_ability_is((int)ability, ABILITY_MULTITYPE)) {
        return 0;
    }
    *type = POKE_TOOL_GetPlateType(POKE_TOOL_GetBoxValueRaw(box, POKE_TOOL_PARAM_HELD_ITEM, 0));
    return 1;
}

u32 POKE_TOOL_GetBoxValue(struct BoxPokemon *box, u32 paramId, PokeToolArg arg)
{
    u32 value;

    POKE_TOOL_DecodeBoxBlock(box);
    if (!multitype_type(box, paramId, &value) && !mon_form(box, paramId, &value)) {
        value = POKE_TOOL_GetBoxValueRaw(box, paramId, arg);
    }
    POKE_TOOL_EncodeBoxBlock(box);
    return value;
}

u32 POKE_TOOL_GetPartyValueRaw(struct PokemonParam *pp, u32 paramId, PokeToolArg arg)
{
    u32 type;

    switch (paramId) {
    case POKE_TOOL_PARAM_STATUS_CONDITION: return pp->statusCondition;
    case POKE_TOOL_PARAM_LEVEL: return pp->level;
    case POKE_TOOL_PARAM_CAPSULE_INDEX: return pp->capsuleIndex;
    case POKE_TOOL_PARAM_HP: return pp->hp;
    case POKE_TOOL_PARAM_MAX_HP: return pp->maxHp;
    case POKE_TOOL_PARAM_ATTACK: return pp->attack;
    case POKE_TOOL_PARAM_DEFENSE: return pp->defense;
    case POKE_TOOL_PARAM_SPEED: return pp->speed;
    case POKE_TOOL_PARAM_SP_ATTACK: return pp->spAttack;
    case POKE_TOOL_PARAM_SP_DEFENSE: return pp->spDefense;
    case POKE_TOOL_PARAM_MAIL:
        MAIL_Copy((const struct Mail *)pp->mail, (struct Mail *)arg);
        return 1;
    case POKE_TOOL_PARAM_NO_DATA:
        return 1;
    }
    if (multitype_type((struct BoxPokemon *)pp->box, paramId, &type)
        || mon_form((struct BoxPokemon *)pp->box, paramId, &type)) {
        return type;
    }
    return POKE_TOOL_GetBoxValueRaw((struct BoxPokemon *)pp->box, paramId, arg);
}
