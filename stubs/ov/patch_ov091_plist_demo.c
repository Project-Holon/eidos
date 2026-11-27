/*
 * patches: src/game/ov091/plist_demo.c PLIST_DEMO_UseItemOnMon
 *
 * An HP item used on a party member outside battle (and a revive) heals
 * what a mod's eidos/tables/item_heal.txt gives the item (sdk/item_heal.h),
 * at most the HP the Pokemon is missing; an item with no line heals the
 * record's byte through the game's own PLIST_DEMO_RestoreHp (253, 254 and
 * 255 a quarter, a half and all).  The rest is the game's function.
 *
 * symbols:
 *   POKE_TOOL_GetPartyValue       0x02017E1C  thumb
 *   POKE_TOOL_SetPartyValue       0x02017E40  thumb
 *   POKE_TOOL_GetExpForLevel      0x02018584  thumb
 *   POKE_TOOL_RecalcParam         0x020185C4  thumb
 *   GFL_HEAP_Free                 0x020307B0  arm
 *   FILE_UTIL_LoadItemRecord      0x02020F34  thumb
 *   FILE_UTIL_GetItemRecordParam  0x02021014  thumb
 */

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef __UINTPTR_TYPE__ PokeToolArg;   /* (arm9 poke_tool_shared_types.h: an address-sized value off the DS) */

struct PokemonParam;
struct ItemRecord;

/* plist_battle_types.h, plist_demo_types.h */
enum {
    PLIST_MON_SPECIES = 5,
    PLIST_MON_EXP = 8,
    PLIST_MON_FRIENDSHIP = 9,
    PLIST_MON_EV_HP = 13,
    PLIST_MON_EV_ATK = 14,
    PLIST_MON_EV_DEF = 15,
    PLIST_MON_EV_SPEED = 16,
    PLIST_MON_EV_SPATK = 17,
    PLIST_MON_EV_SPDEF = 18,
    PLIST_MON_FORM = 0x6F,
    PLIST_MON_STATUS = 0x9D,
    PLIST_MON_LEVEL = 0x9E,
    PLIST_MON_HP = 0xA0,
    PLIST_MON_MAX_HP = 0xA1
};

enum {
    PLIST_SPECIES_SHEDINJA = 292
};

enum {
    PLIST_ITEM_USABLE_ON_PARTY = 14,
    PLIST_ITEM_CURES_SLEEP = 18,
    PLIST_ITEM_CURES_POISON = 19,
    PLIST_ITEM_CURES_BURN = 20,
    PLIST_ITEM_CURES_FREEZE = 21,
    PLIST_ITEM_CURES_PARALYSIS = 22,
    PLIST_ITEM_REVIVES = 26,
    PLIST_ITEM_REVIVES_FULL = 27,
    PLIST_ITEM_RAISES_LEVEL = 28,
    PLIST_ITEM_EVOLVES = 29,
    PLIST_ITEM_RAISES_PP_UPS = 37,
    PLIST_ITEM_MAXES_PP_UPS = 38,
    PLIST_ITEM_RESTORES_PP_ONE = 39,
    PLIST_ITEM_RESTORES_PP_ALL = 40,
    PLIST_ITEM_RESTORES_HP = 41,
    PLIST_ITEM_CHANGES_EV_HP = 42,
    PLIST_ITEM_CHANGES_EV_ATK = 43,
    PLIST_ITEM_CHANGES_EV_DEF = 44,
    PLIST_ITEM_CHANGES_EV_SPEED = 45,
    PLIST_ITEM_CHANGES_EV_SPATK = 46,
    PLIST_ITEM_CHANGES_EV_SPDEF = 47,
    PLIST_ITEM_EV_IGNORES_CAP = 48,
    PLIST_ITEM_FRIENDSHIP_LOW = 49,
    PLIST_ITEM_FRIENDSHIP_MID = 50,
    PLIST_ITEM_FRIENDSHIP_HIGH = 51,
    PLIST_ITEM_EV_HP_AMOUNT = 52,
    PLIST_ITEM_EV_ATK_AMOUNT = 53,
    PLIST_ITEM_EV_DEF_AMOUNT = 54,
    PLIST_ITEM_EV_SPEED_AMOUNT = 55,
    PLIST_ITEM_EV_SPATK_AMOUNT = 56,
    PLIST_ITEM_EV_SPDEF_AMOUNT = 57,
    PLIST_ITEM_HP_AMOUNT = 58,
    PLIST_ITEM_PP_AMOUNT = 59,
    PLIST_ITEM_FRIENDSHIP_LOW_AMOUNT = 60,
    PLIST_ITEM_FRIENDSHIP_MID_AMOUNT = 61,
    PLIST_ITEM_FRIENDSHIP_HIGH_AMOUNT = 62
};

extern u32 POKE_TOOL_GetPartyValue(struct PokemonParam *pp, u32 paramId, PokeToolArg arg);
extern void POKE_TOOL_SetPartyValue(struct PokemonParam *pp, u32 paramId, PokeToolArg value);
extern u32 POKE_TOOL_GetExpForLevel(u16 species, u16 form, u16 level);
extern void POKE_TOOL_RecalcParam(struct PokemonParam *pp);
extern u32 POKE_TOOL_RestorePP(struct PokemonParam *pp, u32 slot, u32 amount);
extern u32 GFL_HEAP_Free(void *block);
extern void *FILE_UTIL_LoadItemRecord(u32 item, u32 which, u32 heap);
extern u32 FILE_UTIL_GetItemRecordParam(struct ItemRecord *record, u32 prop);

int PLIST_DEMO_RaiseMovePpUps(void *mon, int moveSlot, int ups);
void PLIST_DEMO_RestoreHp(void *mon, u32 hp, u32 maxHp, u32 amount);
int PLIST_DEMO_AddEv(int ev, int otherEvTotal, int delta, int ignoreCap);
int PLIST_DEMO_ApplyFriendshipItem(struct PokemonParam *mon, int friendship, int delta, int item, u16 zoneId, int heapId);

/* sdk/item_heal.c */
extern int bw_item_heal_hp(u32 item, u32 max_hp, u32 missing, u32 *amount);

/* The HP an item restores: a mod's item_heal.txt amount, else the game's
 * PLIST_DEMO_RestoreHp with the record's byte. */
static void restore_hp(void *mon, u16 item, u32 hp, u32 maxHp, void *itemRec)
{
    u32 heal;

    if (bw_item_heal_hp(item, maxHp, maxHp - hp, &heal)) {
        POKE_TOOL_SetPartyValue((struct PokemonParam *)mon, PLIST_MON_HP, (PokeToolArg)(hp + heal));
        return;
    }
    PLIST_DEMO_RestoreHp(mon, hp, maxHp, FILE_UTIL_GetItemRecordParam((struct ItemRecord *)itemRec, PLIST_ITEM_HP_AMOUNT));
}

int PLIST_DEMO_UseItemOnMon(void *mon, u16 item, u16 moveSlot, u16 arg, int heap)
{
    void *itemRec;
    int changed = 0;
    int applicable;
    int originalStatus, status, evSpAtk, evSpDef, ignoreCap, amount;
    int evHp, hp, evAtk, maxHp, evDef, evSpeed, level, friendship, result, i;
    itemRec = FILE_UTIL_LoadItemRecord(item, 0, (u16)heap);
    if (FILE_UTIL_GetItemRecordParam(itemRec, PLIST_ITEM_USABLE_ON_PARTY) != 1) { GFL_HEAP_Free(itemRec); return changed; }
    applicable = 0;
    originalStatus = POKE_TOOL_GetPartyValue(mon, PLIST_MON_STATUS, 0);
    status = originalStatus;
    if (FILE_UTIL_GetItemRecordParam(itemRec, PLIST_ITEM_CURES_SLEEP)) { status = 0; applicable = 1; }
    if (FILE_UTIL_GetItemRecordParam(itemRec, PLIST_ITEM_CURES_POISON)) { status = 0; applicable = 1; }
    if (FILE_UTIL_GetItemRecordParam(itemRec, PLIST_ITEM_CURES_BURN)) { status = 0; applicable = 1; }
    if (FILE_UTIL_GetItemRecordParam(itemRec, PLIST_ITEM_CURES_FREEZE)) { status = 0; applicable = 1; }
    if (FILE_UTIL_GetItemRecordParam(itemRec, PLIST_ITEM_CURES_PARALYSIS)) { status = 0; applicable = 1; }
    if (originalStatus != status) { POKE_TOOL_SetPartyValue(mon, PLIST_MON_STATUS, status); changed = 1; }
    hp = POKE_TOOL_GetPartyValue(mon, PLIST_MON_HP, 0);
    maxHp = POKE_TOOL_GetPartyValue(mon, PLIST_MON_MAX_HP, 0);
    if ((FILE_UTIL_GetItemRecordParam(itemRec, PLIST_ITEM_REVIVES) || FILE_UTIL_GetItemRecordParam(itemRec, PLIST_ITEM_REVIVES_FULL)) && !FILE_UTIL_GetItemRecordParam(itemRec, PLIST_ITEM_RAISES_LEVEL)) {
        if (hp == 0) {
            restore_hp(mon, item, hp, maxHp, itemRec);
            changed = 1;
        }
        applicable = 1;
    } else if (FILE_UTIL_GetItemRecordParam(itemRec, PLIST_ITEM_RESTORES_HP)) {
        if (hp < maxHp) { restore_hp(mon, item, hp, maxHp, itemRec); changed = 1; }
        applicable = 1;
    }
    level = POKE_TOOL_GetPartyValue(mon, PLIST_MON_LEVEL, 0);
    if (FILE_UTIL_GetItemRecordParam(itemRec, PLIST_ITEM_RAISES_LEVEL)) {
        if (level < 100) {
            i = POKE_TOOL_GetPartyValue(mon, PLIST_MON_SPECIES, 0);
            result = POKE_TOOL_GetPartyValue(mon, PLIST_MON_FORM, 0);
            POKE_TOOL_SetPartyValue(mon, PLIST_MON_EXP, POKE_TOOL_GetExpForLevel(i, result, level + 1));
            POKE_TOOL_RecalcParam(mon);
            if (hp == 0) {
                result = POKE_TOOL_GetPartyValue(mon, PLIST_MON_MAX_HP, 0);
                PLIST_DEMO_RestoreHp(mon, hp, result, result - maxHp);
            }
            changed = 1;
        }
        applicable = 1;
    }
    if (FILE_UTIL_GetItemRecordParam(itemRec, PLIST_ITEM_EVOLVES)) applicable = 1;
    if (FILE_UTIL_GetItemRecordParam(itemRec, PLIST_ITEM_RAISES_PP_UPS)) {
        applicable = 1;
        if (PLIST_DEMO_RaiseMovePpUps(mon, moveSlot, 1) == 1) changed = 1;
        applicable = 1;
    } else if (FILE_UTIL_GetItemRecordParam(itemRec, PLIST_ITEM_MAXES_PP_UPS)) {
        if (PLIST_DEMO_RaiseMovePpUps(mon, moveSlot, 3) == 1) changed = 1;
        applicable = 1;
    }
    if (FILE_UTIL_GetItemRecordParam(itemRec, PLIST_ITEM_RESTORES_PP_ONE)) {
        if (POKE_TOOL_RestorePP(mon, moveSlot, FILE_UTIL_GetItemRecordParam(itemRec, PLIST_ITEM_PP_AMOUNT)) == 1) changed = 1;
        applicable = 1;
    } else if (FILE_UTIL_GetItemRecordParam(itemRec, PLIST_ITEM_RESTORES_PP_ALL)) {
        for (i = 0; i < 4; i++) if (POKE_TOOL_RestorePP(mon, i, FILE_UTIL_GetItemRecordParam(itemRec, PLIST_ITEM_PP_AMOUNT)) == 1) changed = 1;
        applicable = 1;
    }
    evHp = POKE_TOOL_GetPartyValue(mon, PLIST_MON_EV_HP, 0);
    evAtk = POKE_TOOL_GetPartyValue(mon, PLIST_MON_EV_ATK, 0);
    evDef = POKE_TOOL_GetPartyValue(mon, PLIST_MON_EV_DEF, 0);
    evSpeed = POKE_TOOL_GetPartyValue(mon, PLIST_MON_EV_SPEED, 0);
    evSpAtk = POKE_TOOL_GetPartyValue(mon, PLIST_MON_EV_SPATK, 0);
    evSpDef = POKE_TOOL_GetPartyValue(mon, PLIST_MON_EV_SPDEF, 0);
    ignoreCap = FILE_UTIL_GetItemRecordParam(itemRec, PLIST_ITEM_EV_IGNORES_CAP);
    if (POKE_TOOL_GetPartyValue(mon, PLIST_MON_SPECIES, 0) != PLIST_SPECIES_SHEDINJA && FILE_UTIL_GetItemRecordParam(itemRec, PLIST_ITEM_CHANGES_EV_HP)) {
        amount = FILE_UTIL_GetItemRecordParam(itemRec, PLIST_ITEM_EV_HP_AMOUNT);
        result = PLIST_DEMO_AddEv(evHp, evSpDef + (evSpAtk + (evSpeed + (evAtk + evDef))), amount, ignoreCap);
        if (result != -1) { evHp = result; POKE_TOOL_SetPartyValue(mon, PLIST_MON_EV_HP, result); POKE_TOOL_RecalcParam(mon); changed = 1; }
        if (amount > 0) applicable = 1;
    }
    if (FILE_UTIL_GetItemRecordParam(itemRec, PLIST_ITEM_CHANGES_EV_ATK)) {
        amount = FILE_UTIL_GetItemRecordParam(itemRec, PLIST_ITEM_EV_ATK_AMOUNT);
        result = PLIST_DEMO_AddEv(evAtk, evSpDef + (evSpAtk + (evSpeed + (evHp + evDef))), amount, ignoreCap);
        if (result != -1) { evAtk = result; POKE_TOOL_SetPartyValue(mon, PLIST_MON_EV_ATK, result); POKE_TOOL_RecalcParam(mon); changed = 1; }
        if (amount > 0) applicable = 1;
    }
    if (FILE_UTIL_GetItemRecordParam(itemRec, PLIST_ITEM_CHANGES_EV_DEF)) {
        amount = FILE_UTIL_GetItemRecordParam(itemRec, PLIST_ITEM_EV_DEF_AMOUNT);
        result = PLIST_DEMO_AddEv(evDef, evSpDef + (evSpAtk + (evSpeed + (evHp + evAtk))), amount, ignoreCap);
        if (result != -1) { evDef = result; POKE_TOOL_SetPartyValue(mon, PLIST_MON_EV_DEF, result); POKE_TOOL_RecalcParam(mon); changed = 1; }
        if (amount > 0) applicable = 1;
    }
    if (FILE_UTIL_GetItemRecordParam(itemRec, PLIST_ITEM_CHANGES_EV_SPEED)) {
        amount = FILE_UTIL_GetItemRecordParam(itemRec, PLIST_ITEM_EV_SPEED_AMOUNT);
        result = PLIST_DEMO_AddEv(evSpeed, evSpDef + (evSpAtk + (evDef + (evHp + evAtk))), amount, ignoreCap);
        if (result != -1) { evSpeed = result; POKE_TOOL_SetPartyValue(mon, PLIST_MON_EV_SPEED, result); POKE_TOOL_RecalcParam(mon); changed = 1; }
        if (amount > 0) applicable = 1;
    }
    if (FILE_UTIL_GetItemRecordParam(itemRec, PLIST_ITEM_CHANGES_EV_SPATK)) {
        amount = FILE_UTIL_GetItemRecordParam(itemRec, PLIST_ITEM_EV_SPATK_AMOUNT);
        result = PLIST_DEMO_AddEv(evSpAtk, evSpDef + (evSpeed + (evDef + (evHp + evAtk))), amount, ignoreCap);
        if (result != -1) { evSpAtk = result; POKE_TOOL_SetPartyValue(mon, PLIST_MON_EV_SPATK, result); POKE_TOOL_RecalcParam(mon); changed = 1; }
        if (amount > 0) applicable = 1;
    }
    if (FILE_UTIL_GetItemRecordParam(itemRec, PLIST_ITEM_CHANGES_EV_SPDEF)) {
        amount = FILE_UTIL_GetItemRecordParam(itemRec, PLIST_ITEM_EV_SPDEF_AMOUNT);
        result = PLIST_DEMO_AddEv(evSpDef, evSpAtk + (evSpeed + (evDef + (evHp + evAtk))), amount, ignoreCap);
        if (result != -1) { POKE_TOOL_SetPartyValue(mon, PLIST_MON_EV_SPDEF, result); POKE_TOOL_RecalcParam(mon); changed = 1; }
        if (amount > 0) applicable = 1;
    }
    if (changed == 0 && applicable == 1) { GFL_HEAP_Free(itemRec); return 0; }
    friendship = POKE_TOOL_GetPartyValue(mon, PLIST_MON_FRIENDSHIP, 0);
    if (friendship < 100) {
        if (FILE_UTIL_GetItemRecordParam(itemRec, PLIST_ITEM_FRIENDSHIP_LOW)) {
            PLIST_DEMO_ApplyFriendshipItem(mon, friendship, FILE_UTIL_GetItemRecordParam(itemRec, PLIST_ITEM_FRIENDSHIP_LOW_AMOUNT), item, arg, heap);
            GFL_HEAP_Free(itemRec); return changed;
        }
    } else if (friendship >= 100 && friendship < 200) {
        if (FILE_UTIL_GetItemRecordParam(itemRec, PLIST_ITEM_FRIENDSHIP_MID)) {
            PLIST_DEMO_ApplyFriendshipItem(mon, friendship, FILE_UTIL_GetItemRecordParam(itemRec, PLIST_ITEM_FRIENDSHIP_MID_AMOUNT), item, arg, heap);
            GFL_HEAP_Free(itemRec); return changed;
        }
    } else if (friendship >= 200 && friendship <= 255) {
        if (FILE_UTIL_GetItemRecordParam(itemRec, PLIST_ITEM_FRIENDSHIP_HIGH)) {
            PLIST_DEMO_ApplyFriendshipItem(mon, friendship, FILE_UTIL_GetItemRecordParam(itemRec, PLIST_ITEM_FRIENDSHIP_HIGH_AMOUNT), item, arg, heap);
            GFL_HEAP_Free(itemRec); return changed;
        }
    }
    GFL_HEAP_Free(itemRec);
    return changed;
}
