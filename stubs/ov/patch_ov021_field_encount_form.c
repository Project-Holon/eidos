/*
 * patches: src/game/ov021/field_encount.c FIELD_ENCOUNT_LoadSlots FIELD_ENCOUNT_RollSlotMon FIELD_ENCOUNT_PickSlotByType
 *
 * symbols:
 *   MI_CpuCopy8                 0x02082D44  arm
 *   MI_CpuFill8                 0x02082BCC  arm
 *   GFL_USE_GetRandomBelow      0x02005728  thumb
 *   POKE_TOOL_GetPersonalParam  0x02019C90  thumb
 *   GAME_DATA_GetSeason         0x02012984  thumb
 *   ZONEDATA_GetEncountId       0x02013EC0  thumb
 *
 * A wild Pokemon's form past the slot's room.  A slot of an encounter table
 * keeps the form in 5 bits and 31 there means a random form, so a table
 * named forms 0 to 30 only, and the swarm's form (a byte of its entry) lost
 * its high bits in the slot.  LoadSlots notes, beside the slots it copies,
 * the form a mod's eidos/tables/wild_forms.txt gives each slot of the zone's
 * table (sdk/wild_forms.h) and the swarm entry's whole form.  RollSlotMon
 * rolls the Pokemon in that form, and PickSlotByType (Magnet Pull, Static)
 * reads that form's types.  The rest is the game's: a form the species does
 * not have gives form 0, and a form of 32 or more is kept outside the box
 * record (sdk/mon_form.c).  With no table line the three are the game's own.
 */

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef short s16;

struct GameData;
struct EncountSave;
struct MyStatus;

/* field_encount_types.h: one wild slot (4 bytes); form 0x1F picks a random form */
typedef struct EncountSlot {
    u16 species : 11;
    u16 form : 5;
    u8 minLevel;
    u8 maxLevel;
} EncountSlot;

/* field_encount_types.h: the wild encounter table of one zone (0xE8 bytes) */
typedef struct EncountData {
    u8 rate[7];
    u8 effectKind : 7;
    u8 enabled : 1;
    EncountSlot ground[3][12];
    EncountSlot water[2][5];
    EncountSlot fishing[2][5];
} EncountData;

/* field_encount.c: one generated wild Pokemon (0x14 bytes) */
typedef struct EncountMon {
    u16 species;
    u16 heldItem;
    u8 level;
    u8 form;
    u8 shinyMode;
    u8 hiddenAbility;
    u8 reserved08[8];
    u8 forcedGender;
    u8 reserved11[3];
} EncountMon;

/* field_encount.c: the swarm entry of one zone (8 bytes) */
typedef struct SwarmEntry {
    u16 zoneId;
    u16 species;
    u8 form;
    u8 minLevel;
    u8 maxLevel;
    u8 reserved07;
} SwarmEntry;

/* field_encount.c: the encounter parameters */
typedef struct FieldEncountParam {
    struct GameData *gameData;
    struct EncountSave *encountSave;
    int encountType;
    int triggerMode;
    u32 slotTableKind;
    u8 slotCount;
    u8 monCount;
    u16 leadSpecies;
    u16 leadItem;
    u8 leadIsEgg;
    u8 leadAbility;
    u8 leadGender;
    u8 leadNature;
    u8 leadLevel;
    u8 reserved1F;
    u32 ownerId;
    struct MyStatus *myStatus;
    u32 flags;
} FieldEncountParam;

_Static_assert(sizeof(EncountSlot) == 4, "EncountSlot");
_Static_assert(sizeof(EncountData) == 0xE8, "EncountData");
_Static_assert(sizeof(EncountMon) == 0x14, "EncountMon");
_Static_assert(sizeof(void *) != 4 || sizeof(FieldEncountParam) == 0x2C, "FieldEncountParam");

extern void MI_CpuCopy8(const void *srcp, void *dstp, u32 size);
extern void MI_CpuFill8(void *dstp, u8 data, u32 size);
extern u32 GFL_USE_GetRandomBelow(u32 max);
extern u32 POKE_TOOL_GetPersonalParam(u16 species, u16 form, u32 field);
extern u32 GAME_DATA_GetSeason(struct GameData *gameData);
extern u32 ZONEDATA_GetEncountId(u32 id);
extern u32 FIELD_ENCOUNT_RandPercent(void);
extern int FIELD_ENCOUNT_TrySwarmSlot(EncountData *encountData, FieldEncountParam *param, u16 zoneId, EncountSlot *slot);
extern const SwarmEntry *FIELD_ENCOUNT_GetSwarmEntry(struct GameData *gameData);

/* sdk/wild_forms.c */
extern void bw_wild_slots_note(const void *slots, int count, int table, int season, int type, int swarm_form);
extern int bw_wild_slot_form(const void *slot);

#define WILD_FORM_NONE (-1)             /* (sdk/wild_forms.h BW_WILD_FORM_NONE) */

/* The whole form noted for a slot, or WILD_FORM_NONE (the slot's field
 * stands, 0x1F there a random form). */
static int noted_form(const EncountSlot *slot)
{
    return bw_wild_slot_form(slot);
}

/* The game's, and the whole forms noted beside the slots. */
u8 FIELD_ENCOUNT_LoadSlots(EncountData *encountData, FieldEncountParam *param, u16 zoneId, EncountSlot *slots)
{
    u8 count = 0;
    u32 kind = 0;
    EncountSlot *src;
    int table;

    if (FIELD_ENCOUNT_TrySwarmSlot(encountData, param, zoneId, slots) != 0) {
        /* (the slot's 5 bits cut a form of 32 or more of the entry's byte) */
        u32 form = FIELD_ENCOUNT_GetSwarmEntry(param->gameData)->form;

        bw_wild_slots_note(slots, 1, -1, 0, 0, form >= 32 ? (int)form : WILD_FORM_NONE);
        return param->slotCount;
    }
    if (param->encountType >= 7)
        param->encountType = 0;
    switch (param->encountType) {
    case 0:
        count = 12;
        src = encountData->ground[0];
        break;
    case 1:
        count = 12;
        src = encountData->ground[1];
        break;
    case 2:
        count = 12;
        src = encountData->ground[2];
        break;
    case 3:
        kind = 1;
        count = 5;
        src = encountData->water[0];
        break;
    case 4:
        kind = 1;
        count = 5;
        src = encountData->water[1];
        break;
    case 5:
        kind = 2;
        count = 5;
        src = encountData->fishing[0];
        break;
    case 6:
        kind = 2;
        count = 5;
        src = encountData->fishing[1];
        break;
    }
    param->slotTableKind = kind;
    param->slotCount = count;
    MI_CpuCopy8(src, slots, count * 4);
    /* the zone's table (the a/1/2/6 member) and the season it was read for
     * (FIELD_ENCOUNT_ST_LoadEncountData reads the row of GAME_DATA_GetSeason) */
    table = (int)ZONEDATA_GetEncountId(zoneId);
    bw_wild_slots_note(slots, count, table != 0xFFFF ? table : -1, (int)GAME_DATA_GetSeason(param->gameData),
                       param->encountType, WILD_FORM_NONE);
    return count;
}

/* The game's, with the slot's whole form. */
void FIELD_ENCOUNT_RollSlotMon(EncountSlot *slot, EncountMon *mon)
{
    s16 levelRange = slot->maxLevel - slot->minLevel;
    u8 formCount;
    int form;
    u16 species;

    MI_CpuFill8(mon, 0, 0x14);
    if (levelRange < 0)
        levelRange = 0;
    mon->level = slot->minLevel + FIELD_ENCOUNT_RandPercent() % (u32)(levelRange + 1);
    species = slot->species;
    mon->species = species;
    formCount = POKE_TOOL_GetPersonalParam(species, 0, 0x20);
    form = noted_form(slot);
    if (form == WILD_FORM_NONE && (form = slot->form) == 0x1F)
        mon->form = GFL_USE_GetRandomBelow(formCount);
    else if (form >= formCount)
        mon->form = 0;
    else
        mon->form = form;
}

/* Magnet Pull and Static: the game's, with the slots' whole forms. */
u8 FIELD_ENCOUNT_PickSlotByType(FieldEncountParam *param, EncountSlot *slots, u32 type)
{
    u8 candidates[12];
    int i;
    int count;
    int form;
    u8 type1, type2;

    count = 0;
    for (i = 0; i < param->slotCount; i++) {
        form = noted_form(&slots[i]);
        if (form == WILD_FORM_NONE)
            form = slots[i].form;       /* (0x1F: a random form, no types to match) */
        if (form != 0x1F || noted_form(&slots[i]) != WILD_FORM_NONE) {
            type1 = POKE_TOOL_GetPersonalParam(slots[i].species, form, 6);
            type2 = POKE_TOOL_GetPersonalParam(slots[i].species, form, 7);
            if (type1 == type || type2 == type) {
                candidates[count] = i;
                count++;
            }
        }
    }
    if (count == 0)
        return 0xFF;
    return candidates[FIELD_ENCOUNT_RandPercent() % count];
}
