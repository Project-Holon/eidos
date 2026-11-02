/*
 * patches: src/game/ov021/scrcmd_trainer_audio_item.c SCRCMD_AddItem
 *
 * The item a script puts in the Bag, known to the one-screen interface
 * (engine/ui/recipe_field.c, sdk/ui_text.c).
 *
 * SCRCMD_AddItem is the script command that adds an item to the Bag: the
 * shared item scripts run it before the game's "obtained" message and its
 * fanfare (a picked-up ball, a gift).  It does what the game's does and
 * also tells the port the item and the count (bw_ui_item_got), so the
 * interface can show the item's card over that message.
 *
 * symbols:
 *   SCRCMD_WORK_GetHeapId  0x0215A5CC  thumb  ov010
 *   SCRCMD_WORK_GetGameData  0x0215A5E4  thumb  ov010
 *   SCRIPT_WORK_ReadWorkValue  0x02159B10  thumb  ov010
 *   SCRIPT_WORK_ReadWorkPointer  0x02159AE8  thumb  ov010
 *   GAME_DATA_GetMyItem  0x0201292C  thumb
 *   MYITEM_SAVEDATA_AddItem  0x02007E50  thumb
 */

typedef unsigned short u16;
typedef unsigned int u32;

typedef struct ScriptVm ScriptVm;
typedef struct ScrcmdWork ScrcmdWork;
typedef struct MyItem MyItem;

extern void bw_ui_item_got(u32 item, u32 count, int added);

u16 SCRCMD_WORK_GetHeapId(ScrcmdWork *cmdWork);
void *SCRCMD_WORK_GetGameData(ScrcmdWork *cmdWork);
u16 SCRIPT_WORK_ReadWorkValue(ScriptVm *vm, ScrcmdWork *cmdWork);
u16 *SCRIPT_WORK_ReadWorkPointer(ScriptVm *vm, ScrcmdWork *cmdWork);
MyItem *GAME_DATA_GetMyItem(void *gameData);
u32 MYITEM_SAVEDATA_AddItem(MyItem *myItem, u16 itemId, u16 quantity, u16 heapId);

int SCRCMD_AddItem(ScriptVm *vm, ScrcmdWork *cmdWork)
{
    u32 heapId;
    MyItem *myItem;
    u32 itemNo, count;
    u16 *result;

    heapId = SCRCMD_WORK_GetHeapId(cmdWork);
    myItem = GAME_DATA_GetMyItem(SCRCMD_WORK_GetGameData(cmdWork));
    itemNo = SCRIPT_WORK_ReadWorkValue(vm, cmdWork);
    count = SCRIPT_WORK_ReadWorkValue(vm, cmdWork);
    result = SCRIPT_WORK_ReadWorkPointer(vm, cmdWork);
    *result = MYITEM_SAVEDATA_AddItem(myItem, itemNo, count, heapId);
    bw_ui_item_got(itemNo, count, *result != 0);
    return 0;
}
