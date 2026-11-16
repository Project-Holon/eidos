/*
 * patches: src/game/ov021/field_event_check.c FIELD_EVENT_CHECK_FindPartyMemberWithMove
 *
 * The field's A check for a move (Surf at the water's edge, Waterfall at a
 * waterfall): the first party Pokemon that knows the move uses it, or none
 * (6).  The port asks mods about each Pokemon in turn (move.known, site
 * front tile, sdk/mod_event_v6.c): a mod can let a Pokemon use a move it
 * has not learned, or keep one from using a move it has.  With no mod and
 * no test switch the answer is the game's own, Pokemon by Pokemon.  An egg
 * is skipped before anything is asked, as the game does.
 *
 * symbols:
 *   GAME_DATA_GetMyParty  0x02012934  thumb
 *   POKEPARTY_GetCount  0x0201AA18  thumb
 *   POKEPARTY_GetMember  0x0201AB28  thumb
 *   POKE_TOOL_GetPartyValue  0x02017E1C  thumb
 */

typedef unsigned int u32;

extern void *GAME_DATA_GetMyParty(void *gameData);
extern u32   POKEPARTY_GetCount(void *party);
extern void *POKEPARTY_GetMember(void *party, u32 pos);
extern u32   POKE_TOOL_GetPartyValue(void *pp, u32 paramId, u32 arg);
/* sdk/mod_event_v6.c: the mods' answer (move.known) */
extern int bw_mod_move_known_ask(int slot, int species, int move, int site, int known);

int FIELD_EVENT_CHECK_FindPartyMemberWithMove(void *gameData, u32 move)
{
    void *party = GAME_DATA_GetMyParty(gameData);
    int memberCount = (int)POKEPARTY_GetCount(party);
    int index;

    for (index = 0; index < memberCount; index++) {
        void *member = POKEPARTY_GetMember(party, (u32)index);
        int known;

        if (POKE_TOOL_GetPartyValue(member, 0x4C, 0) != 0) {
            continue;                   /* an egg: never asked */
        }
        known = move == POKE_TOOL_GetPartyValue(member, 0x36, 0) || move == POKE_TOOL_GetPartyValue(member, 0x37, 0)
                || move == POKE_TOOL_GetPartyValue(member, 0x38, 0) || move == POKE_TOOL_GetPartyValue(member, 0x39, 0);
        if (bw_mod_move_known_ask(index, (int)POKE_TOOL_GetPartyValue(member, 0x05, 0), (int)move, 0, known)) {
            return index;
        }
    }
    return 6;
}
