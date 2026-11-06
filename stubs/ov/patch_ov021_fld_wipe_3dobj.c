/*
 * patches: src/game/ov021/fld_wipe_3dobj.c SCRCMD_StartFld3dCiByTable
 *
 * The key item a script shows off, known to the one-screen interface
 * (engine/ui/item_show.c).
 *
 * SCRCMD_StartFld3dCiByTable is the script command that plays a key item's
 * cut-in (the flash, the item spinning out of a glow with stars, during the
 * key-item jingle): it finds the item in a 17-row table and starts that
 * row's FLD3D_CI sequence.  It does what the game's does and also tells the
 * port the item (bw_ui_cutin_item), so the interface can play its own
 * showcase of it while the cut-in runs.
 *
 * symbols:
 *   SCRCMD_WORK_GetGameSystem  0x0215A5DC  thumb  ov010
 *   GAMESYSTEM_GetFieldMap  0x0201210C  thumb
 *   FIELDMAP_GetFld3dCi  0x02189A00  thumb  ov021
 *   SCRCMD_WORK_GetScriptWork  0x0215A5F4  thumb  ov010
 *   FLD3D_CI_StartSequence  0x021C1CB0  thumb  ov021
 *   SCRIPT_SYS_CallEvent  0x0215897C  thumb  ov010
 *   SCRIPT_WORK_ReadWorkValue  0x02159B10  thumb  ov010
 *   gFld3dCiKeyTable     0x021DAAE0  data  a2
 *   gFld3dCiIdTable     0x021DAAE2  data  a2
 */

typedef unsigned short u16;
typedef unsigned int u32;

typedef struct ScriptVm ScriptVm;
typedef struct ScrcmdWork ScrcmdWork;

extern void bw_ui_cutin_item(u32 item);

extern unsigned short gFld3dCiKeyTable[];
extern unsigned short gFld3dCiIdTable[];

void *SCRCMD_WORK_GetGameSystem(ScrcmdWork *work);
void *GAMESYSTEM_GetFieldMap(void *gameSystem);
void *FIELDMAP_GetFld3dCi(void *fieldMap);
void *SCRCMD_WORK_GetScriptWork(ScrcmdWork *work);
void *FLD3D_CI_StartSequence(void *gameSystem, void *ci, int sequenceId);
void SCRIPT_SYS_CallEvent(void *scriptSys, void *event);
u16 SCRIPT_WORK_ReadWorkValue(ScriptVm *vm, ScrcmdWork *work);

int SCRCMD_StartFld3dCiByTable(ScriptVm *vm, ScrcmdWork *work)
{
    void *gameSystem, *ci, *scriptSys, *event;
    int value, found, i;
    unsigned short ciId = 0;

    gameSystem = SCRCMD_WORK_GetGameSystem(work);
    ci = FIELDMAP_GetFld3dCi(GAMESYSTEM_GetFieldMap(gameSystem));
    value = (int)SCRIPT_WORK_ReadWorkValue(vm, work);
    found = 0;
    for (i = 0; i < 17; i++) {
        if (value == gFld3dCiKeyTable[i * 2]) {
            found = 1;
            ciId = gFld3dCiIdTable[i * 2];
            break;
        }
    }
    if (found) {
        scriptSys = SCRCMD_WORK_GetScriptWork(work);
        event = FLD3D_CI_StartSequence(gameSystem, ci, (unsigned char)ciId);
        if (event) {
            bw_ui_cutin_item((u32)value);
            SCRIPT_SYS_CallEvent(scriptSys, event);
        }
    }
    return 1;
}
