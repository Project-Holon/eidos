/*
 * patches: src/game/ov021/scrcmd_proc_fld.c SCRCMD_PROC_FLD_BuildItemListByBadges SCRCMD_PROC_FLD_BuildItemListFixed SCRCMD_PROC_FLD_SetItemSlot SCRCMD_PROC_FLD_ShowTmBuyConfirm
 *
 * symbols:
 *   d_021D946C  0x021D946C  data  ov021
 *   d_021D95DE  0x021D95DE  data  ov021
 *   d_021DE1A8  0x021DE1A8  data  ov021
 *   d_021D9430  0x021D9430  data  ov021
 *   d_021DE164  0x021DE164  data  ov021
 *   WORDSET_SetSlotFromMsgCB 0x0201EF34 thumb
 *   WORDSET_SetSlotNumber 0x0201EFA4 thumb
 *   WORDSET_CopyTaggedText 0x0201F250 thumb
 *   BMP_MENUWORK_AddStrItem 0x0201F898 thumb
 *   SCRCMD_PROC_FLD_PrepareMessageWindow 0x021B4878 thumb ov021
 *   SCRCMD_PROC_FLD_ShowMessage 0x021B4894 thumb ov021
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
 * A TM shop's rows and its buy question print the TM's number as the
 * item id - 327 (TM01 is item 328).  A TM a mod adds (TM96 on, an item
 * past 626: sdk/machines.c) prints its own number, in three digits from
 * TM100 (bw_machine_shop_number): SCRCMD_PROC_FLD_SetItemSlot and
 * SCRCMD_PROC_FLD_ShowTmBuyConfirm, the game's otherwise.
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
    u8 pad18c[4];
    void *wordSet;                  /* 0x190 */
    u8 pad194[4];
    void *textBuffer;               /* 0x198 */
    void *cancelText;               /* 0x19C: the TM row's text, "No. [n] [move]" */
} ShopWork;

#define SHOP_CHECK(name, cond) typedef char shop_check_##name[(cond) ? 1 : -1]
SHOP_CHECK(items, sizeof(void *) != 4 || offsetof(ShopWork, items) == 0x020);
SHOP_CHECK(count, sizeof(void *) != 4 || offsetof(ShopWork, itemCount) == 0x160);
SHOP_CHECK(tm, sizeof(void *) != 4 || offsetof(ShopWork, isTmShop) == 0x162);
SHOP_CHECK(slots, sizeof(void *) != 4 || offsetof(ShopWork, itemSlots) == 0x180);
SHOP_CHECK(msgs, sizeof(void *) != 4 || offsetof(ShopWork, shopMsgBank) == 0x188);
SHOP_CHECK(words, sizeof(void *) != 4 || offsetof(ShopWork, wordSet) == 0x190);
SHOP_CHECK(text, sizeof(void *) != 4 || offsetof(ShopWork, cancelText) == 0x19C);

#define SHOP_WIN_MESSAGE 6
#define PM_LANG_HALF_WIDTH_DIGITS 1

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
struct WordSet;
struct Strbuf;
struct U16Vec;
struct BmpWin;
extern void WORDSET_SetSlotFromMsgCB(struct WordSet *words, u32 slot, u32 entryIndex);
extern void WORDSET_SetSlotNumber(struct WordSet *words, u32 slot, u32 number, u32 topDigitIndex, u32 padMode,
                                  u32 isHalfWidth);
extern void WORDSET_CopyTaggedText(struct WordSet *words, struct Strbuf *dst, struct Strbuf *src);
extern void BMP_MENUWORK_AddStrItem(struct BmpMenuWork *items, const struct U16Vec *src, u32 param, u32 heapId);
extern u16 FILE_UTIL_GetMachineMove(u16 machineId);
extern void SCRCMD_PROC_FLD_PrepareMessageWindow(struct BmpWin *window);
extern void SCRCMD_PROC_FLD_ShowMessage(ShopWork *shop, u32 messageId);
/* sdk/machines.c */
extern u32 bw_machine_shop_number(u32 item);
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

/* The digits a TM number takes: 2, 3 from TM100 (a mod's). */
static u32 tm_digits(u32 number)
{
    return number >= 100 ? 3 : 2;
}

void SCRCMD_PROC_FLD_SetItemSlot(ShopWork *shop, int slotIndex, int isTmShop, int itemId, struct MsgBank *msgBank)
{
    if (isTmShop == 1) {
        u32 number = bw_machine_shop_number((u32)itemId);

        WORDSET_SetSlotNumber((struct WordSet *)shop->wordSet, 0, number, tm_digits(number), 2, PM_LANG_HALF_WIDTH_DIGITS);
        WORDSET_SetSlotFromMsgCB((struct WordSet *)shop->wordSet, 1, FILE_UTIL_GetMachineMove((u16)itemId));
        WORDSET_CopyTaggedText((struct WordSet *)shop->wordSet, (struct Strbuf *)shop->textBuffer, (struct Strbuf *)shop->cancelText);
        BMP_MENUWORK_AddStrItem(((struct BmpMenuWork *)shop->itemSlots + slotIndex), (const struct U16Vec *)shop->textBuffer, (u32)slotIndex, shop->heapId);
    } else {
        BMP_MENUWORK_AddMsgItem(((struct BmpMenuWork *)shop->itemSlots + slotIndex), msgBank, (u32)itemId, (u32)slotIndex, shop->heapId);
    }
}

void SCRCMD_PROC_FLD_ShowTmBuyConfirm(ShopWork *shop, u32 messageId, u32 itemId, u32 price)
{
    u32 number = bw_machine_shop_number(itemId);

    SCRCMD_PROC_FLD_PrepareMessageWindow((struct BmpWin *)shop->windows[SHOP_WIN_MESSAGE]);
    WORDSET_SetSlotNumber((struct WordSet *)shop->wordSet, 0, number, tm_digits(number), 2, PM_LANG_HALF_WIDTH_DIGITS);
    WORDSET_SetSlotFromMsgCB((struct WordSet *)shop->wordSet, 1, FILE_UTIL_GetMachineMove((u16)itemId));
    WORDSET_SetSlotNumber((struct WordSet *)shop->wordSet, 2, price, 5, 0, PM_LANG_HALF_WIDTH_DIGITS);
    SCRCMD_PROC_FLD_ShowMessage(shop, messageId);
}
