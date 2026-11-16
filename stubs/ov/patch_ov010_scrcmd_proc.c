/*
 * patches: src/game/ov010/scrcmd_proc.c SCRCMD_PROC_CheckMonKnowsMove
 *
 * The scripts' check for a move (Cut's tree, Strength's boulder, the people
 * who ask whether a Pokemon knows a move): 1 when the Pokemon knows it.
 * Its callers are the script commands CheckPartyMonKnowsMove and
 * FindPartyMonWithMove, in its own unit, and the move tutor's check in
 * ov021; calls inside a unit never pass a slot, so the port takes the
 * function out of the unit and asks the mods here, for every caller
 * (move.known, site script, sdk/mod_event_v6.c).  With no mod and no test
 * switch the answer is the game's own.  An egg is never asked: it knows
 * nothing, as in the game.
 *
 * symbols:
 *   POKE_TOOL_GetPartyValue  0x02017E1C  thumb
 */

typedef unsigned int u32;

extern u32 POKE_TOOL_GetPartyValue(void *pp, u32 paramId, u32 arg);
/* sdk/mod_state.c: the party slot of a party Pokemon, or -1 */
extern int bw_mod_state_party_slot(const void *mon);
/* sdk/mod_event_v6.c: the mods' answer (move.known) */
extern int bw_mod_move_known_ask(int slot, int species, int move, int site, int known);

int SCRCMD_PROC_CheckMonKnowsMove(void *mon, int moveId)
{
    int known;

    if (POKE_TOOL_GetPartyValue(mon, 0x4C, 0) != 0) {
        return 0;
    }
    known = moveId == (int)POKE_TOOL_GetPartyValue(mon, 0x36, 0) || moveId == (int)POKE_TOOL_GetPartyValue(mon, 0x37, 0)
            || moveId == (int)POKE_TOOL_GetPartyValue(mon, 0x38, 0) || moveId == (int)POKE_TOOL_GetPartyValue(mon, 0x39, 0);
    return bw_mod_move_known_ask(bw_mod_state_party_slot(mon), (int)POKE_TOOL_GetPartyValue(mon, 0x05, 0), moveId, 1,
                                 known) ? 1 : 0;
}
