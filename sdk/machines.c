/*
 * sdk/machines.c -- the TMs mods add (eidos/tables/machine_items.txt).
 *
 * The game knows a TM or HM by its item id: items 328..425 (TM01..TM92,
 * HM01..HM06) and 618..620 (TM93..TM95), in FILE_UTIL_IsMachineItem.  An item
 * is mapped to its move through a table of 101 rows in item order
 * (FILE_UTIL_GetMachineMove) and to its machine bit in a species record
 * (FILE_UTIL_GetId: TM01..TM95 bits 0..94, HM01..HM06 95..100), which is what
 * says whether a species can learn it.  A TM a mod adds (TM96..TM122) is an
 * item past the game's (machine_items.txt); its bit and its row of the
 * port's move table are its TM number + 5 (sdk/bw_limits.c).  These are the
 * game's three functions with those items added; the game's own machines go
 * the game's way.  FILE_UTIL_GetMachineIndex (the HM number) and
 * FILE_UTIL_IsHmMove are the game's: an added TM is never an HM.
 */

#include "bw_limits.h"
#include "types.h"

u32 FILE_UTIL_IsMachineItem(u32 id)
{
    if ((id >= 0x148 && id <= 0x1A9) || (id >= 0x26A && id <= 0x26C)) {
        return 1;
    }
    return bw_machine_bit_of_item(id) >= 0 ? 1u : 0u;
}

u16 FILE_UTIL_GetMachineMove(u16 machineId)
{
    const u16 *rows = bw_machine_moves();
    int bit;

    if (rows == NULL) {
        return 0;
    }
    if ((machineId >= 0x148 && machineId <= 0x1A9) || (machineId >= 0x26A && machineId <= 0x26C)) {
        if (machineId >= 0x26A) {
            machineId -= 0x208;
        } else {
            machineId -= 0x148;
        }
        return rows[machineId];
    }
    bit = bw_machine_bit_of_item(machineId);
    return bit >= 0 ? rows[bit] : 0;
}

/* The number a shop prints for a TM item: the game's item - 327 for its
 * own (TM01..TM92), the TM number for a mod's (bit - 5). */
u32 bw_machine_shop_number(u32 item)
{
    int bit = item > 626u ? bw_machine_bit_of_item(item) : -1;

    return bit >= 0 ? (u32)bit - 5u : item - 327u;
}

/* The number the bag prints for a TM by its id (FILE_UTIL_GetId): the
 * game's id + 1 for its own, the TM number for a mod's. */
u32 bw_machine_bag_number(u32 id)
{
    return id > 100u ? id - 5u : id + 1u;
}

u32 FILE_UTIL_GetId(u32 machineId)
{
    int bit;

    if ((machineId >= 0x148 && machineId <= 0x1A9) || (machineId >= 0x26A && machineId <= 0x26C)) {
        if (machineId >= 0x26A) {
            return (u8)(machineId - 0x20E);
        }
        if (machineId >= 0x1A4) {
            return (u8)(machineId - 0x145);
        }
        return (u8)(machineId - 0x148);
    }
    bit = bw_machine_bit_of_item(machineId);
    return bit >= 0 ? (u32)bit : 0xFFu;
}
