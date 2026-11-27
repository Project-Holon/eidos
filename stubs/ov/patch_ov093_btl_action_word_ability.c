/*
 * patches: src/game/ov093/btl_action_word.c BTL_ACTION_SetItem
 *
 * A test switch for the Wonder Launcher's items, which only a link battle
 * offers (EIDOS_DEBUG_LAUNCHER=ITEM, docs/test-switches.md): every Bag
 * action uses ITEM in place of the item picked (a Potion picked for the
 * Pokemon in battle becomes an Ability Urge used on it).  With the switch
 * on, the server uses items with the launcher's rules too
 * (patch_ov093_btl_server_flow_ability.c BTL_SERVER_FLOW_RunBagModeSetup).
 * Off, the function is the game's.
 */

typedef unsigned short u16;
typedef unsigned int u32;

/* btl_calc_types.h: an action word (an item action: kind 2 in bits 0-3,
 * the target in bits 4-6, the item in bits 7-22, its parameter in 23-30) */
union BtlAction {
    u32 raw;
};

#define BTL_ACTION_ITEM 2

/* sdk/battle_handlers.c */
extern int bw_bh_test_launcher_item(void);

/* Makes an item action: the item, the target index and one parameter byte. */
void BTL_ACTION_SetItem(union BtlAction *action, u32 itemId, u32 targetIndex, u32 itemParam)
{
    u32 bits = BTL_ACTION_ITEM;

    if (bw_bh_test_launcher_item() != 0) {
        itemId = (u32)bw_bh_test_launcher_item();
    }
    bits |= (u32)(u16)itemId << 7;
    bits |= (targetIndex & 7) << 4;
    bits |= (itemParam & 255) << 23;
    action->raw = bits;
}
