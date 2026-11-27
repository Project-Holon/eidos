/*
 * patches: src/game/ov091/plist_menu.c PLIST_MENU_ApplyArceusPlateForm PLIST_MENU_ApplyGenesectDriveForm
 *
 * symbols:
 *   POKE_TOOL_GetPartyValue 0x02017E1C thumb
 *   POKE_TOOL_GetPlateTypeForwarder 0x02019CF8 thumb
 *   POKE_TOOL_ChangeForm 0x02017988 thumb
 *   POKE_TOOL_GetBoxParamIndex 0x02019D00 thumb
 *   ZUKAN_SAVE_RegisterSeen 0x0200CFA0 thumb
 *
 * A plate or a drive given to Arceus or Genesect in the party marks the form
 * it takes seen in the Pokedex, as the PC Box does for the same change
 * (src/game/ov170/box2_main.c, BOX2_MAIN_ChangeFormAndRegister) and as the
 * party does for Giratina's and Shaymin's (plist_demo.c).  The game keeps
 * no Arceus or Genesect form in its Pokedex, so it had no need to; a form a
 * mod adds (a plate or a drive of an added type, sdk/type_items.c) is kept
 * (sdk/dex_save.c).  Otherwise the two functions are the game's own.
 *
 * The game's own record
 * ---------------------
 *
 * PokelistWork.arg at 0x280, PokelistArg.zukanSave at 0x18
 * (src/game/ov091/pokelist_types.h).  The offsets are the DS ones: pointers
 * are four bytes in every build that runs the overlays.
 */

typedef unsigned short u16;
typedef unsigned int u32;
typedef __UINTPTR_TYPE__ PokeToolArg;   /* (arm9 poke_tool_shared_types.h: an address-sized value off the DS) */

struct PokemonParam;
struct PokelistWork;
struct ZukanSave;

u32 POKE_TOOL_GetPartyValue(struct PokemonParam *pp, u32 paramId, PokeToolArg arg);
unsigned int POKE_TOOL_GetPlateTypeForwarder(unsigned int item);
u32 POKE_TOOL_ChangeForm(struct PokemonParam *pp, u32 form);
u32 POKE_TOOL_GetBoxParamIndex(u32 paramId);
void ZUKAN_SAVE_RegisterSeen(struct ZukanSave *pZukan, void *pMon);

#define PLIST_MON_SPECIES 5
#define PLIST_MON_FORM    0x6F

/* the party list's Pokedex block (PokelistWork.arg->zukanSave) */
static struct ZukanSave *plist_zukan(void *work)
{
    char *arg = work != 0 ? *(char **)((char *)work + 0x280) : 0;

    return arg != 0 ? *(struct ZukanSave **)(arg + 0x18) : 0;
}

void PLIST_MENU_ApplyArceusPlateForm(void *unused, void *mon, int itemId)
{
    u32 form = 0;
    int plateForm;
    if (POKE_TOOL_GetPartyValue(mon, PLIST_MON_SPECIES, 0) == 0x1ED) {
        form = POKE_TOOL_GetPartyValue(mon, PLIST_MON_FORM, form);
        plateForm = POKE_TOOL_GetPlateTypeForwarder(itemId);
        if (form != plateForm) {
            POKE_TOOL_ChangeForm(mon, plateForm);
            if (plist_zukan(unused) != 0) {
                ZUKAN_SAVE_RegisterSeen(plist_zukan(unused), mon);
            }
        }
    }
}

void PLIST_MENU_ApplyGenesectDriveForm(struct PokelistWork *unused, void *mon, int itemId)
{
    int form = 0;
    int driveForm;
    if (POKE_TOOL_GetPartyValue(mon, PLIST_MON_SPECIES, 0) == 0x289) {
        form = POKE_TOOL_GetPartyValue(mon, PLIST_MON_FORM, form);
        driveForm = POKE_TOOL_GetBoxParamIndex(itemId);
        if (form != driveForm) {
            POKE_TOOL_ChangeForm(mon, (u16)driveForm);
            if (plist_zukan(unused) != 0) {
                ZUKAN_SAVE_RegisterSeen(plist_zukan(unused), mon);
            }
        }
    }
}
