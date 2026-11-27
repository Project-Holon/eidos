/*
 * sdk/type_items.c -- the plates and drives of the types mods add
 * (docs/game-tables.md, types.txt `plate` and `drive`).
 *
 * The game maps a held item to Arceus's type and form (a plate, arm9
 * poke_tool.c POKE_TOOL_GetPlateType: Arceus with Multitype takes the
 * plate's type, and the party and box screens set its form to that type's
 * number) and to Genesect's form (a drive, POKE_TOOL_GetBoxParamIndex,
 * forms 1 to 4).  These are the game's two functions with the added types'
 * plates and drives after the game's own: a plate gives its type (and so
 * Arceus form 17 and up, which the mod adds as Arceus's form), a drive the
 * Genesect form its line names.  Judgment and Techno Blast take the same
 * items in battle (stubs/ov/patch_ov093_hand_waza.c).
 */

#include "type_icons.h"
#include "types.h"

u32 POKE_TOOL_GetPlateType(u32 item)
{
    switch (item) {
    case 0x12A: return 9;
    case 0x12B: return 10;
    case 0x12C: return 12;
    case 0x12D: return 11;
    case 0x12E: return 14;
    case 0x12F: return 1;
    case 0x130: return 3;
    case 0x131: return 4;
    case 0x132: return 2;
    case 0x133: return 13;
    case 0x134: return 6;
    case 0x135: return 5;
    case 0x136: return 7;
    case 0x137: return 15;
    case 0x138: return 16;
    case 0x139: return 8;
    }
    return bw_type_of_plate(item);      /* an added type's plate, else 0 (Normal) */
}

u32 POKE_TOOL_GetBoxParamIndex(u32 paramId)
{
    u32 type, form;

    switch (paramId) {
    case 0x74:
        return 1;
    case 0x75:
        return 2;
    case 0x76:
        return 3;
    case 0x77:
        return 4;
    }
    if (bw_type_of_drive(paramId, &type, &form)) {
        return form;                    /* an added type's drive */
    }
    return 0;
}
