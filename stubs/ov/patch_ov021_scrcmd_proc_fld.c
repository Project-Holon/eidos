/*
 * patches: src/game/ov021/scrcmd_proc_fld.c SCRCMD_PROC_FLD_BuildItemListByBadges SCRCMD_PROC_FLD_BuildItemListFixed
 *
 * symbols:
 *   d_021D946C  0x021D946C  data  ov021
 *   d_021D95DE  0x021D95DE  data  ov021
 *   d_021DE1A8  0x021DE1A8  data  ov021
 *   d_021D9430  0x021D9430  data  ov021
 *   d_021DE164  0x021DE164  data  ov021
 *
 * A mod's shop lists (sdk/shops.h, eidos/tables/shops.txt).  The shop's two
 * list builders take a mod's list when one is given for the list the shop
 * opens, and do what the game's do otherwise:
 *
 *   SCRCMD_PROC_FLD_BuildItemListByBadges  the marts: the game's list n
 *       (d_021DE1A8[n], d_021D95DE[n] items; the Poke Mart's by badges
 *       through d_021D946C), each item at its own price with the Pass Power
 *       discount.  A mod's `mart.<n>`.
 *   SCRCMD_PROC_FLD_BuildItemListFixed     the BP prize lists (d_021DE164[n],
 *       d_021D9430[n] items, each with its price).  A mod's `bp.<n>`.
 *
 * The game's own records
 * ----------------------
 *
 * The port's library has no aletheia include path, so the shop's work
 * (ov021 scrcmd_proc_fld_types.h, ShopWork) is copied up to the members
 * these two read and write, in the same order: the same offsets on any
 * target where pointers are four bytes (the DS's, i686's and wasm32's),
 * checked below.
 */

#include <stddef.h>

#include "shops.h"

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef int s32;

typedef struct ShopItem {
    u16 itemId;
    u16 pad02;
    s32 price;
} ShopItem;

typedef struct ShopWork {
    u8 state;                       /* 0x000 */
    u8 nextState;                   /* 0x001 */
    u8 pad02[2];
    u16 heapId;                     /* 0x004 */
    u8 pad06[2];
    void *fontInfo;                 /* 0x008 */
    void *moneyData;                /* 0x00C */
    void *bagData;                  /* 0x010 */
    void *recordData;               /* 0x014 */
    void *battlePointData;          /* 0x018 */
    void *shopTable;                /* 0x01C */
    ShopItem items[40];             /* 0x020 */
    u16 itemCount;                  /* 0x160 */
    u8 isTmShop;                    /* 0x162 */
    u8 usesBattlePoints;            /* 0x163 */
    void *windows[7];               /* 0x164 */
    void *itemSlots;                /* 0x180 */
    void *yesNoMenu;                /* 0x184 */
    void *shopMsgBank;              /* 0x188 */
} ShopWork;

#define SHOP_CHECK(name, cond) typedef char shop_check_##name[(cond) ? 1 : -1]
SHOP_CHECK(items, sizeof(void *) != 4 || offsetof(ShopWork, items) == 0x020);
SHOP_CHECK(count, sizeof(void *) != 4 || offsetof(ShopWork, itemCount) == 0x160);
SHOP_CHECK(tm, sizeof(void *) != 4 || offsetof(ShopWork, isTmShop) == 0x162);
SHOP_CHECK(slots, sizeof(void *) != 4 || offsetof(ShopWork, itemSlots) == 0x180);
SHOP_CHECK(msgs, sizeof(void *) != 4 || offsetof(ShopWork, shopMsgBank) == 0x188);

struct MsgBank;

/* One entry of a menu's list (arm9 bmp_menuwork_shared_types.h): the item
 * slots are an array of them. */
struct BmpMenuWork {
    const void *text;
    u32 value;
};

extern u8 d_021D946C[];
extern u8 d_021D95DE[];
extern u16 *d_021DE1A8[];
extern u8 d_021D9430[];
extern ShopItem *d_021DE164[];

/* (the game's own prototypes, arm9 msgdata.h, bmp_menuwork.h, file_util.h,
 * gpower.h: the same shape, as the wasm32 build links by it) */
extern struct MsgBank *MSGDATA_CreateFromArchive(u32 resident, u32 arcId, u16 fileId, u32 heapId);
extern u32 MSGDATA_Delete(struct MsgBank *bank);
extern void *BMP_MENUWORK_Create(u32 count, u32 heapId);
extern void BMP_MENUWORK_AddMsgItem(struct BmpMenuWork *items, struct MsgBank *bank, u32 msgId, u32 param, u32 heapId);
extern u32 FILE_UTIL_IsMachineItem(u32 id);
extern u32 FILE_UTIL_GetItemParam(u32 item, u32 sel, u32 heap);
extern u32 GPOWER_ApplyShopPrice(u32 price);
void SCRCMD_PROC_FLD_SetItemSlot(ShopWork *shop, int slotIndex, int isTmShop, int itemId, struct MsgBank *msgBank);
u32 SCRCMD_PROC_FLD_LoadItemRows(ShopItem *items, int count);

void SCRCMD_PROC_FLD_BuildItemListByBadges(ShopWork *shop, int shopKind, int shopVariant, int tierIndex)
{
    struct MsgBank *msgBank = MSGDATA_CreateFromArchive(0, 2, 54, shop->heapId);
    const u16 *items;
    int tableIndex, itemCount, row, itemId;

    if (shopKind == 0xff) {
        tableIndex = d_021D946C[tierIndex];
    } else {
        tableIndex = shopVariant + 6;
    }
    /* (a mod's list for it, else the game's) */
    itemCount = bw_shop_mart(tableIndex, &items);
    if (itemCount < 0) {
        items = d_021DE1A8[tableIndex];
        itemCount = d_021D95DE[tableIndex];
    }
    if (FILE_UTIL_IsMachineItem(items[0])) {
        shop->isTmShop = 1;
    }
    shop->itemSlots = BMP_MENUWORK_Create(itemCount + 1, shop->heapId);
    for (row = 0; row < itemCount; row++) {
        itemId = items[row];
        SCRCMD_PROC_FLD_SetItemSlot(shop, row, shop->isTmShop, itemId, msgBank);
        shop->items[row].itemId = (u16)itemId;
        shop->items[row].price = (s32)GPOWER_ApplyShopPrice(FILE_UTIL_GetItemParam((u32)itemId, 0, shop->heapId));
    }
    shop->itemCount = (u16)row;
    SCRCMD_PROC_FLD_LoadItemRows(shop->items, shop->itemCount);
    BMP_MENUWORK_AddMsgItem((struct BmpMenuWork *)shop->itemSlots + row, (struct MsgBank *)shop->shopMsgBank, 11, -2, shop->heapId);
    MSGDATA_Delete(msgBank);
}

void SCRCMD_PROC_FLD_BuildItemListFixed(ShopWork *shop, int tableIndex)
{
    struct MsgBank *msgBank = MSGDATA_CreateFromArchive(0, 2, 0x36, shop->heapId);
    const u16 *items = 0;
    const s32 *prices = 0;
    int row, itemCount = bw_shop_bp(tableIndex, &items, &prices);
    ShopItem *table = 0;

    if (itemCount < 0) {
        table = d_021DE164[tableIndex];
        itemCount = d_021D9430[tableIndex];
    }
    shop->itemSlots = BMP_MENUWORK_Create(itemCount + 1, shop->heapId);
    for (row = 0; row < itemCount; row++) {
        u16 itemId = table != 0 ? table[row].itemId : items[row];

        SCRCMD_PROC_FLD_SetItemSlot(shop, row, shop->isTmShop, itemId, msgBank);
        shop->items[row].itemId = itemId;
        shop->items[row].price = table != 0 ? table[row].price : prices[row];
    }
    shop->itemCount = (u16)row;
    BMP_MENUWORK_AddMsgItem((struct BmpMenuWork *)shop->itemSlots + row, (struct MsgBank *)shop->shopMsgBank, 0xb, -2, shop->heapId);
    MSGDATA_Delete(msgBank);
}
