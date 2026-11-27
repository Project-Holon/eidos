/*
 * patches: src/game/ov093/btl_sickcont.c BTL_SICKCONT_MakeKind2Index BTL_SICKCONT_MakeKind2
 *
 * The duration words a handler makes for an effect that lasts some turns
 * (Reflect's 5, Tailwind's 4, Uproar's 3, Trick Room's 5) take the running
 * move's, ability's or item's `turns` key (sdk/battle_handlers.h
 * bw_bh_turns, eidos/tables/*_handlers.txt) where the handler table lists
 * the place.  With no table line they are the game's own words.
 */

typedef unsigned int u32;

/* btl_pokeparam_types.h: union SickCont, byValue: kind 3 bits, index (the
 * turns) 6 bits, value 16 bits */
union SickCont {
    u32 raw;
};

#define SICKCONT_KIND_TURNS 2

/* sdk/battle_handlers.c */
extern u32 bw_bh_turns(u32 turns);

union SickCont BTL_SICKCONT_MakeKind2Index(u32 index)
{
    union SickCont slot;

    index = bw_bh_turns(index);
    slot.raw = SICKCONT_KIND_TURNS | ((index & 0x3f) << 3);
    return slot;
}

union SickCont BTL_SICKCONT_MakeKind2(u32 index, u32 value)
{
    union SickCont slot;

    index = bw_bh_turns(index);
    slot.raw = SICKCONT_KIND_TURNS | ((index & 0x3f) << 3) | ((value & 0xffff) << 9);
    return slot;
}
