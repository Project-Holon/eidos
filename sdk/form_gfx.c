/*
 * sdk/form_gfx.c -- the pictures of a form a mod adds.
 *
 * A species a mod adds keeps its pictures where its id puts them, the way
 * the game's own species do: battle and Pokedex sprites at member id * 20
 * of a/0/0/4, party icons at id * 2 + 7 of a/0/0/7, the icon palette row
 * id.  The game's forms instead count from a base after the eggs: their
 * stats record names the first form's picture (formSpriteIndex, 0 to 62 in
 * the game), and form f is picture k = first + f - 1, at the slot 652 + k:
 * sprites (652 + k) * 20 (0x32F0 + k * 20), icons (652 + k) * 2 + 7, the
 * palette row 652 + k.
 *
 * A mod's form has a stats slot of its own from 715 (formStatsIndex + f -
 * 1, sdk/dex_data.c), and its formSpriteIndex is that same slot: a value of
 * 715 or more names the slot itself, so form f's pictures are those of slot
 * formSpriteIndex + f - 1, at the places a species of that id has them.
 * The game's values (below 715) keep the game's own arithmetic.
 *
 * A form whose only change is its colour (formSpriteShared) keeps the
 * species' sprite and takes its palette: the game's from its list of form
 * palettes (0x37AD + k * 2 + shiny), a mod's from its slot's own palettes
 * (slot * 20 + 18 + shiny).
 *
 * The three functions are the game's own (arm9 pokegra.c,
 * poke_tool_ext.c), with that one case added.
 */

#include <stddef.h>

#include "bw_limits.h"
#include "types.h"

#if defined(__GNUC__) || defined(__clang__)
#define GAME_FN __attribute__((weak))
#else
#define GAME_FN
#endif

extern u32 POKE_TOOL_GetPersonalParam(u16 species, u16 form, u32 field) GAME_FN;
extern u32 ARC_TOOL_GetMemberSize(u32 archiveID, u32 memberIdx) GAME_FN;

#define PARAM_FORM_SPRITE  0x1Fu
#define PARAM_FORM_COUNT   0x20u
#define PARAM_FORM_SHARED  0x23u

#define FIRST_SLOT 715u          /* the first id past the game's pictures */

/* A form's slot when it is a mod's (formSpriteIndex from 715), else 0. */
static u32 mod_slot(u32 first, u32 form)
{
    return first >= FIRST_SLOT ? first + form - 1u : 0u;
}

void POKEGRA_CalcArcIndex(u32 arcId, u32 monsNo, s32 formNo, s32 sex, u32 rare, u32 dir, u32 egg, u32 *outIndex,
                          u32 *outDirOffset, u32 *outSex, u32 *outRare, u32 *outFormPalIndex, u32 isCgr)
{
    s32 index, formCount, formStart, palOnlyForms, sexFileOffset;
    u32 dirOffset;

    index = (s32)(monsNo * 0x14u);
    dirOffset = dir == 0 ? 0u : 9u;
    if (isCgr != 0) {
        sexFileOffset = dir == 0 ? 0 : 9;
    } else {
        sexFileOffset = dir == 0 ? 2 : 0xb;
    }
    if (egg != 0) {
        index = ((monsNo == 0x1ea ? 1 : 0) + 0x28a) * 0x14;
    } else if (formNo != 0) {
        formStart = (s32)POKE_TOOL_GetPersonalParam((u16)monsNo, 0, PARAM_FORM_SPRITE);
        palOnlyForms = (s32)POKE_TOOL_GetPersonalParam((u16)monsNo, 0, PARAM_FORM_SHARED);
        formCount = (s32)POKE_TOOL_GetPersonalParam((u16)monsNo, 0, PARAM_FORM_COUNT);
        if (formNo >= formCount) {
            formNo = 0;
        }
        if (palOnlyForms != 0) {
            if (outFormPalIndex != NULL && formNo != 0) {
                u32 slot = mod_slot((u32)formStart, (u32)formNo);

                *outFormPalIndex = slot != 0 ? slot * 0x14u + 0x12u + rare
                                             : rare + (u32)((formStart + formNo - 1) * 2 + 0x37ad);
            }
        } else if (formNo != 0) {
            u32 slot = mod_slot((u32)formStart, (u32)formNo);

            index = slot != 0 ? (s32)(slot * 0x14u) : (formStart + formNo - 1) * 0x14 + 0x32f0;
        }
    }
    switch (sex) {
    case 0:
        break;
    case 1:
        formCount = (s32)ARC_TOOL_GetMemberSize(arcId, (u32)(index + sexFileOffset + 1));
        sex = formCount == 0 ? 0 : 1;
        break;
    case 2:
        sex = 0;
        break;
    default:
        break;
    }
    if (outIndex != NULL) {
        *outIndex = (u32)index;
    }
    if (outDirOffset != NULL) {
        *outDirOffset = dirOffset;
    }
    if (outSex != NULL) {
        *outSex = (u32)sex;
    }
    if (outRare != NULL) {
        *outRare = rare;
    }
}

u32 POKE_TOOL_CalcGraphicIndex(u32 species, u32 form, u32 gender, u32 isEgg)
{
    u32 firstFormIndex, formsShared, formCount, index, hasFemale;

    index = species * 2 + 7;
    if (isEgg != 0) {
        index = (u32)((species == 490 ? 1 : 0) + 650) * 2 + 7;
    } else if (form != 0) {
        firstFormIndex = POKE_TOOL_GetPersonalParam((u16)species, 0, PARAM_FORM_SPRITE);
        formsShared = POKE_TOOL_GetPersonalParam((u16)species, 0, PARAM_FORM_SHARED);
        formCount = POKE_TOOL_GetPersonalParam((u16)species, 0, PARAM_FORM_COUNT);
        if (form >= formCount) {
            form = 0;
        }
        if (formsShared == 0 && form != 0) {
            u32 slot = mod_slot(firstFormIndex, form);

            index = slot != 0 ? slot * 2 + 7 : (firstFormIndex + form - 1) * 2 + 1311;
        }
    }
    switch (gender) {
    case 0:
        break;
    case 1:
        hasFemale = ARC_TOOL_GetMemberSize(7, index + 1);
        gender = hasFemale == 0 ? 0 : 1;
        break;
    case 2:
        gender = 0;
        break;
    }
    return index + gender;
}

u32 POKE_TOOL_CalcIconPalette(u32 species, u32 form, u32 female, u32 isEgg)
{
    u32 firstFormIndex, formsShared, formCount, index, rows = 0;
    const u8 *table = bw_icon_palette_table(&rows);     /* (the game's, or the copy with the mods' rows) */

    index = species;
    if (isEgg != 0) {
        index = (species == 490 ? 1 : 0) + 650;
    } else if (form != 0) {
        firstFormIndex = POKE_TOOL_GetPersonalParam((u16)species, 0, PARAM_FORM_SPRITE);
        formsShared = POKE_TOOL_GetPersonalParam((u16)species, 0, PARAM_FORM_SHARED);
        formCount = POKE_TOOL_GetPersonalParam((u16)species, 0, PARAM_FORM_COUNT);
        if (form >= formCount) {
            form = 0;
        }
        if (formsShared == 0 && form != 0) {
            u32 slot = mod_slot(firstFormIndex, form);

            index = slot != 0 ? slot : firstFormIndex + form + 651;
        }
    }
    if (table == NULL || index >= rows) {
        return 0;
    }
    if (female != 0) {
        return (u8)((table[index] & 0xF0) >> 4);
    }
    return (u8)(table[index] & 0xF);
}
