/*
 * tests/test_mod_state.c -- the state API a mod binds to.
 *
 * The state calls are the wrappers the port puts over the game's own
 * accessors (docs/mod-events.md, section 4; sdk/mod_state.h), and the
 * table they are published through is frozen with the rest of the
 * surface (docs/mod-abi.md, section 1).
 *
 * What is checked here:
 *
 *   - the port's own state file header (BwModStateHeader): its size, the
 *     offset and the width of each field, and the magic's spelling;
 *   - the field ids a mod asks a Pokemon's fields by: they are the ids
 *     the game's own accessors take, so they are the game's numbers and
 *     not the port's;
 *   - the Pokemon reference (BwMonRef): the width of each part, the
 *     places a Pokemon can be, and the empty reference;
 *   - the three tables a mod is given (BwModHost, BwModState,
 *     BwModServices): how many members they have, that the members are
 *     laid out as plain pointers one after another, and where the last
 *     one sits;
 *   - the services table's state_in_step: the call that answers for the
 *     calling mod whether its own saved data was written with the game
 *     save in force, and the port's own piece behind it;
 *   - the return conventions the header promises: a call that answers
 *     with a value answers with a negative number when there is no such
 *     thing, so the types have to be able to hold one;
 *   - the state calls themselves, with no game bound: every one of them
 *     has to report a failure rather than crash.  The state's game
 *     accessors are weak, so a build without the game has none of them
 *     to call, and the calls must say so instead of reaching into
 *     nothing.
 *
 * What this test does not cover:
 *
 *   - reading and writing a party member, which is the test state of
 *     step 5 of the build order (docs/mod-implementation.md, section 6):
 *     the game's Pokemon record layer (POKE_TOOL_GetPartyValue and
 *     POKE_TOOL_SetPartyValue) is not in the build, so there is no
 *     party to read;
 *   - the calls that need the game's objects bound (the battle, the
 *     field player): the port binds them as the game runs, and there is
 *     no game here to bind them to;
 *   - the services' behaviour, which is step 8 of the build order
 *     (docs/mod-services.md).
 */

#include <stddef.h>
#include <stdio.h>

#include "mod_api.h"
#include "mod_state.h"
#include "port_test.h"

/*
 * The port's own entry points into the state layer are declared at the
 * end of sdk/mod_state.h: the table the host table points at, the reset
 * that forgets every binding, and the queue of the moves that wait for a
 * quiet moment.  They are the port's own calls, not part of the mod ABI:
 * a mod reaches the state through the BwModState table.
 */

/*
 * The port's own piece behind the services table's state_in_step: the
 * saved-data layer's answer for the namespace in force
 * (sdk/mod_savedata.c).  It is the port's own call, not part of the mod
 * ABI; the loader binds it into the services table.
 */
extern int bw_mod_savedata_in_step(void);

/*
 * How many of the table's calls are not filled.  The layer fills every
 * one of them: a call that cannot answer says so rather than being left
 * NULL (sdk/mod_state.c, "The table").
 */
static int unfilled_calls(const BwModState *t)
{
    int n = 0;

    if (t->party_count == NULL) { n++; }
    if (t->party_ref == NULL) { n++; }
    if (t->party_get == NULL) { n++; }
    if (t->party_set == NULL) { n++; }
    if (t->box_count == NULL) { n++; }
    if (t->box_ref == NULL) { n++; }
    if (t->box_get == NULL) { n++; }
    if (t->box_set == NULL) { n++; }
    if (t->battle_ref == NULL) { n++; }
    if (t->battle_get == NULL) { n++; }
    if (t->battle_set == NULL) { n++; }
    if (t->species_value == NULL) { n++; }
    if (t->species_shiny == NULL) { n++; }
    if (t->dex_caught == NULL) { n++; }
    if (t->badge_count == NULL) { n++; }
    if (t->badge_has == NULL) { n++; }
    if (t->money == NULL) { n++; }
    if (t->money_set == NULL) { n++; }
    if (t->flag == NULL) { n++; }
    if (t->flag_set == NULL) { n++; }
    if (t->zone == NULL) { n++; }
    if (t->player_pos == NULL) { n++; }
    if (t->battle_style == NULL) { n++; }
    if (t->mon_to_box == NULL) { n++; }
    if (t->mon_take_item == NULL) { n++; }
    if (t->mon_release == NULL) { n++; }
    if (t->item_count == NULL) { n++; }
    if (t->item_add == NULL) { n++; }
    if (t->area_of == NULL) { n++; }
    if (t->battle_wild == NULL) { n++; }
    if (t->play_seconds == NULL) { n++; }
    if (t->item_value == NULL) { n++; }
    if (t->move_value == NULL) { n++; }
    if (t->mon_to_box_n == NULL) { n++; }
    if (t->mon_from_box == NULL) { n++; }
    if (t->mon_recalc == NULL) { n++; }
    if (t->species_base == NULL) { n++; }
    if (t->battle_trainer == NULL) { n++; }
    return n;
}

int main(void)
{
    const BwModState *t;
    BwMonRef ref;
    u32 value;
    s16 x, y, z;
    u32 magic_bytes;

    printf("the state file header\n");
    /*
     * The header is the port's own: 7 fields, 24 bytes.  (docs/mod-state.md,
     * section 1 describes a 32-byte header with a reserved field at +24;
     * the frozen header in sdk/mod_state.h has no such field, and the
     * frozen header is what a mod compiles against.)
     */
    BW_CHECK_EQ("the header is 24 bytes", (long)sizeof(BwModStateHeader), 24);
    BW_CHECK_EQ("the magic is first", (long)offsetof(BwModStateHeader, magic), 0);
    BW_CHECK_EQ("the version follows it", (long)offsetof(BwModStateHeader, version), 4);
    BW_CHECK_EQ("the flags follow the version", (long)offsetof(BwModStateHeader, flags), 6);
    BW_CHECK_EQ("the payload size follows the flags",
                (long)offsetof(BwModStateHeader, payload_size), 8);
    BW_CHECK_EQ("the payload's checksum follows the size",
                (long)offsetof(BwModStateHeader, payload_crc), 12);
    BW_CHECK_EQ("the api in force follows the checksum",
                (long)offsetof(BwModStateHeader, api_written), 16);
    BW_CHECK_EQ("the save's checksum is last",
                (long)offsetof(BwModStateHeader, save_check), 20);
    BW_CHECK_EQ("the magic is four bytes",
                (long)sizeof(((BwModStateHeader *)0)->magic), 4);
    BW_CHECK_EQ("the version is two bytes",
                (long)sizeof(((BwModStateHeader *)0)->version), 2);
    BW_CHECK_EQ("the flags are two bytes",
                (long)sizeof(((BwModStateHeader *)0)->flags), 2);
    BW_CHECK_EQ("the payload size is four bytes",
                (long)sizeof(((BwModStateHeader *)0)->payload_size), 4);
    BW_CHECK_EQ("the payload's checksum is four bytes",
                (long)sizeof(((BwModStateHeader *)0)->payload_crc), 4);
    BW_CHECK_EQ("the api in force is four bytes",
                (long)sizeof(((BwModStateHeader *)0)->api_written), 4);
    BW_CHECK_EQ("the save's checksum is four bytes",
                (long)sizeof(((BwModStateHeader *)0)->save_check), 4);

    magic_bytes = (u32)'B' | ((u32)'W' << 8) | ((u32)'M' << 16) | ((u32)'S' << 24);
    BW_CHECK_EQ("the magic spells BWMS", (long)BW_MOD_STATE_MAGIC,
                (long)magic_bytes);

    printf("the field ids\n");
    BW_CHECK_EQ("species", (long)BW_MON_FIELD_SPECIES, 0x05);
    BW_CHECK_EQ("the held item", (long)BW_MON_FIELD_ITEM, 0x06);
    BW_CHECK_EQ("experience", (long)BW_MON_FIELD_EXP, 0x08);
    BW_CHECK_EQ("level", (long)BW_MON_FIELD_LEVEL, 0x9E);
    BW_CHECK_EQ("hp", (long)BW_MON_FIELD_HP, 0xA0);
    BW_CHECK_EQ("the maximum hp", (long)BW_MON_FIELD_MAX_HP, 0xA1);
    BW_CHECK_EQ("attack", (long)BW_MON_FIELD_ATK, 0xA2);
    BW_CHECK_EQ("defence", (long)BW_MON_FIELD_DEF, 0xA3);
    BW_CHECK_EQ("speed", (long)BW_MON_FIELD_SPEED, 0xA4);
    BW_CHECK_EQ("the special attack", (long)BW_MON_FIELD_SPATK, 0xA5);
    BW_CHECK_EQ("the special defence", (long)BW_MON_FIELD_SPDEF, 0xA6);
    BW_CHECK_EQ("the first type", (long)BW_MON_FIELD_TYPE1, 0xAE);
    BW_CHECK_EQ("the second type", (long)BW_MON_FIELD_TYPE2, 0xAF);

    printf("the Pokemon reference\n");
    BW_CHECK_EQ("the handle is four bytes",
                (long)sizeof(((BwMonRef *)0)->handle), 4);
    BW_CHECK_EQ("the place is one byte",
                (long)sizeof(((BwMonRef *)0)->place), 1);
    BW_CHECK_EQ("the side is one byte",
                (long)sizeof(((BwMonRef *)0)->side), 1);
    BW_CHECK_EQ("the box is one byte",
                (long)sizeof(((BwMonRef *)0)->box), 1);
    BW_CHECK_EQ("the slot is one byte",
                (long)sizeof(((BwMonRef *)0)->slot), 1);
    BW_CHECK_EQ("the reference is eight bytes", (long)sizeof(BwMonRef), 8);
    BW_CHECK_EQ("the party is the first place", (long)BW_MON_PARTY, 0);
    BW_CHECK_EQ("a box is the second place", (long)BW_MON_BOX, 1);
    BW_CHECK_EQ("a battle is the third place", (long)BW_MON_BATTLE, 2);
    BW_CHECK_EQ("the wild is the fourth place", (long)BW_MON_WILD, 3);
    {
        BwMonRef none = BW_MON_REF_NONE;

        BW_CHECK_EQ("the empty reference has no handle", (long)none.handle, 0);
        BW_CHECK_EQ("the empty reference is in the party", (long)none.place,
                    (long)BW_MON_PARTY);
        BW_CHECK_EQ("the empty reference has no side", (long)none.side, 0);
        BW_CHECK_EQ("the empty reference has no box", (long)none.box, 0);
        BW_CHECK_EQ("the empty reference has no slot", (long)none.slot, 0);
    }

    printf("the host table\n");
    BW_CHECK_EQ("the version is first", (long)offsetof(BwModHost, api), 0);
    BW_CHECK_EQ("the version is four bytes",
                (long)sizeof(((BwModHost *)0)->api), 4);
    /* The registration call is the first pointer, so it sits where the
     * table's alignment puts it: after the version and the padding the
     * pointer's alignment asks for. */
    BW_CHECK_EQ("the registration call follows the version",
                (long)offsetof(BwModHost, on), (long)sizeof(void *));
    /* The script call follows it (version 2 of the surface added it). */
    BW_CHECK_EQ("the script call follows the registration call",
                (long)offsetof(BwModHost, script)
                - (long)offsetof(BwModHost, on), (long)sizeof(void *));
    BW_CHECK_EQ("the state table follows the script call",
                (long)offsetof(BwModHost, state)
                - (long)offsetof(BwModHost, script), (long)sizeof(void *));
    BW_CHECK_EQ("the services table follows the state table",
                (long)offsetof(BwModHost, services)
                - (long)offsetof(BwModHost, state), (long)sizeof(void *));
    BW_CHECK_EQ("and nothing follows the services table",
                (long)sizeof(BwModHost) - (long)offsetof(BwModHost, services),
                (long)sizeof(void *));

    printf("the state table\n");
    BW_CHECK_EQ("the party count is first", (long)offsetof(BwModState, party_count), 0);
    BW_CHECK_EQ("the party reference follows it",
                (long)offsetof(BwModState, party_ref)
                - (long)offsetof(BwModState, party_count), (long)sizeof(void *));
    BW_CHECK_EQ("the box count follows the party's four calls",
                (long)offsetof(BwModState, box_count)
                - (long)offsetof(BwModState, party_count), 4 * (long)sizeof(void *));
    BW_CHECK_EQ("the species data follows the battle's three",
                (long)offsetof(BwModState, species_value)
                - (long)offsetof(BwModState, party_count), 11 * (long)sizeof(void *));
    BW_CHECK_EQ("the dex follows the species data's two",
                (long)offsetof(BwModState, dex_caught)
                - (long)offsetof(BwModState, party_count), 13 * (long)sizeof(void *));
    BW_CHECK_EQ("the badges follow the dex",
                (long)offsetof(BwModState, badge_count)
                - (long)offsetof(BwModState, party_count), 14 * (long)sizeof(void *));
    BW_CHECK_EQ("the map follows the badges and the money",
                (long)offsetof(BwModState, zone)
                - (long)offsetof(BwModState, party_count), 20 * (long)sizeof(void *));
    BW_CHECK_EQ("the item take follows the map's three",
                (long)offsetof(BwModState, mon_take_item)
                - (long)offsetof(BwModState, party_count), 24 * (long)sizeof(void *));
    BW_CHECK_EQ("version 4's release follows it",
                (long)offsetof(BwModState, mon_release)
                - (long)offsetof(BwModState, party_count), 25 * (long)sizeof(void *));
    BW_CHECK_EQ("the bag's two follow the release",
                (long)offsetof(BwModState, item_add)
                - (long)offsetof(BwModState, party_count), 27 * (long)sizeof(void *));
    BW_CHECK_EQ("the area, the battle kind, the play time and the data follow",
                (long)offsetof(BwModState, move_value)
                - (long)offsetof(BwModState, party_count), 32 * (long)sizeof(void *));
    BW_CHECK_EQ("the box moves and the recalculation follow",
                (long)offsetof(BwModState, mon_recalc)
                - (long)offsetof(BwModState, party_count), 35 * (long)sizeof(void *));
    BW_CHECK_EQ("the species base follows",
                (long)offsetof(BwModState, species_base)
                - (long)offsetof(BwModState, party_count), 36 * (long)sizeof(void *));
    BW_CHECK_EQ("the battle's trainers follow",
                (long)offsetof(BwModState, battle_trainer)
                - (long)offsetof(BwModState, party_count), 37 * (long)sizeof(void *));
    BW_CHECK_EQ("version 5's type chart, weather and learnset follow",
                (long)offsetof(BwModState, species_learns)
                - (long)offsetof(BwModState, party_count), 40 * (long)sizeof(void *));
    BW_CHECK_EQ("the whiteout follows",
                (long)offsetof(BwModState, whiteout)
                - (long)offsetof(BwModState, party_count), 41 * (long)sizeof(void *));
    BW_CHECK_EQ("and nothing follows it",
                (long)sizeof(BwModState) - (long)offsetof(BwModState, whiteout),
                (long)sizeof(void *));

    printf("the services table\n");
    BW_CHECK_EQ("the message is first", (long)offsetof(BwModServices, message), 0);
    BW_CHECK_EQ("the session host follows the message and the confirm",
                (long)offsetof(BwModServices, session_host)
                - (long)offsetof(BwModServices, message), 2 * (long)sizeof(void *));
    BW_CHECK_EQ("the session handler follows the session's four calls",
                (long)offsetof(BwModServices, on_session)
                - (long)offsetof(BwModServices, message), 6 * (long)sizeof(void *));
    BW_CHECK_EQ("the timer follows the panel's two",
                (long)offsetof(BwModServices, timer_start)
                - (long)offsetof(BwModServices, message), 14 * (long)sizeof(void *));
    BW_CHECK_EQ("the random source follows the restart",
                (long)offsetof(BwModServices, rand_seed)
                - (long)offsetof(BwModServices, message), 19 * (long)sizeof(void *));
    BW_CHECK_EQ("the mod's own data follows the settings",
                (long)offsetof(BwModServices, state_load)
                - (long)offsetof(BwModServices, message), 28 * (long)sizeof(void *));
    BW_CHECK_EQ("the data's last call follows the data's four",
                (long)offsetof(BwModServices, state_dir)
                - (long)offsetof(BwModServices, message), 32 * (long)sizeof(void *));
    BW_CHECK_EQ("the in-step call follows the data's last one",
                (long)offsetof(BwModServices, state_in_step)
                - (long)offsetof(BwModServices, state_dir),
                (long)sizeof(void *));
    BW_CHECK_EQ("version 4's table read follows it",
                (long)offsetof(BwModServices, table_get)
                - (long)offsetof(BwModServices, state_in_step),
                (long)sizeof(void *));
    BW_CHECK_EQ("version 5's multiplayer calls follow it",
                (long)offsetof(BwModServices, event_player)
                - (long)offsetof(BwModServices, table_get),
                (long)sizeof(void *));
    BW_CHECK_EQ("the shared table's four follow",
                (long)offsetof(BwModServices, shared_cas)
                - (long)offsetof(BwModServices, event_player),
                4 * (long)sizeof(void *));
    BW_CHECK_EQ("and nothing follows them",
                (long)sizeof(BwModServices) - (long)offsetof(BwModServices, shared_cas),
                (long)sizeof(void *));

    printf("the in-step call\n");
    /* The services table's state_in_step answers for the mod that calls
     * it: the port's own layer knows the namespace in force (the loader
     * sets it around every call it makes into a mod) and the checksum of
     * the game save in force, and it compares the mod's own file with it.
     * The save.load payload's byte cannot answer it, because the payload
     * is built once for every handler of the event and the answer is per
     * mod.
     *
     * With no mod calling and no save in force the question cannot be
     * answered, and the layer says so with a negative number. */
    BW_CHECK_EQ("the call is a function pointer",
                (long)sizeof(((BwModServices *)0)->state_in_step),
                (long)sizeof(void *));
    BW_CHECK_EQ("the port's own layer answers",
                (long)(bw_mod_savedata_in_step() < 0), 1);

    printf("the handler types\n");
    BW_CHECK_EQ("the session handler is a function pointer",
                (long)sizeof(BwSessionHandler), (long)sizeof(void *));
    BW_CHECK_EQ("the command handler is a function pointer",
                (long)sizeof(BwCommandHandler), (long)sizeof(void *));
    BW_CHECK_EQ("the frame handler is a function pointer",
                (long)sizeof(BwFrameHandler), (long)sizeof(void *));
    BW_CHECK_EQ("the save handler is a function pointer",
                (long)sizeof(BwSaveHandler), (long)sizeof(void *));

    printf("the session kinds and the restart flags\n");
    BW_CHECK_EQ("a message from a peer", (long)BW_SESSION_MESSAGE, 0);
    BW_CHECK_EQ("a peer joined", (long)BW_SESSION_JOIN, 1);
    BW_CHECK_EQ("a peer left", (long)BW_SESSION_LEAVE, 2);
    BW_CHECK_EQ("a new game", (long)BW_RESTART_NEW_GAME, 0x1);
    BW_CHECK_EQ("the save is deleted", (long)BW_RESTART_DELETE_SAVE, 0x2);

    printf("the return conventions\n");
    /* A call that answers with a value answers with a negative number
     * when there is no such thing, so its type has to hold one. */
    BW_CHECK_EQ("an int holds a negative answer", (long)sizeof(int), 4);
    BW_CHECK_EQ("an int is signed", (long)(-1 < 0), 1);
    /* The mod's own state is loaded into a long, whose width follows the
     * platform: four bytes on Windows, eight on a 64-bit Unix.  The
     * surface hash does not see it (it folds the width of the member,
     * which is a pointer), so a mod built on one and run on the other
     * would read the answer wrong. */
    BW_CHECK_EQ("a long holds a negative answer", (long)(sizeof(long) >= 4), 1);
    printf("  note: long is %ld bytes here, so the state load's answer has "
           "that width\n", (long)sizeof(long));

    printf("the state calls with no game bound\n");
    /* Nothing is bound: there is no game in this build, so every call
     * has to report that it cannot answer rather than reach into the
     * game's accessors, which are not there. */
    bw_mod_state_reset();
    t = bw_mod_state_table();
    BW_REQUIRE("the port publishes a state table", t != NULL);
    BW_CHECK_EQ("every call of the table is filled", unfilled_calls(t), 0);

    ref = (BwMonRef)BW_MON_REF_NONE;
    value = 0;
    x = y = z = 0;

    /* The calls that answer with a value answer with nothing. */
    BW_CHECK_EQ("the party is empty", (long)t->party_count(), 0);
    BW_CHECK_EQ("there are no boxes", (long)t->box_count(), 0);
    BW_CHECK_EQ("there are no badges", (long)t->badge_count(), 0);
    BW_CHECK_EQ("there is no money", (long)t->money(), 0);
    BW_CHECK_EQ("there is no zone", (long)t->zone(), 0);
    BW_CHECK_EQ("there is no battle style", (long)t->battle_style(), 0);

    /* The calls that answer with success or failure fail. */
    BW_CHECK_EQ("a party reference fails", (long)(t->party_ref(0, &ref) != 0), 1);
    BW_CHECK_EQ("reading a party member fails",
                (long)(t->party_get(ref, BW_MON_FIELD_SPECIES, &value) != 0), 1);
    BW_CHECK_EQ("writing a party member fails",
                (long)(t->party_set(ref, BW_MON_FIELD_SPECIES, 1) != 0), 1);
    BW_CHECK_EQ("a box reference fails", (long)(t->box_ref(0, 0, &ref) != 0), 1);
    BW_CHECK_EQ("reading a box member fails",
                (long)(t->box_get(ref, BW_MON_FIELD_SPECIES, &value) != 0), 1);
    BW_CHECK_EQ("writing a box member fails",
                (long)(t->box_set(ref, BW_MON_FIELD_SPECIES, 1) != 0), 1);
    BW_CHECK_EQ("a battle reference fails",
                (long)(t->battle_ref(0, 0, &ref) != 0), 1);
    BW_CHECK_EQ("reading a battle member fails",
                (long)(t->battle_get(ref, BW_MON_FIELD_HP, &value) != 0), 1);
    BW_CHECK_EQ("writing a battle member fails",
                (long)(t->battle_set(ref, BW_MON_FIELD_HP, 1) != 0), 1);
    BW_CHECK_EQ("a species value fails",
                (long)(t->species_value(1, 0, &value) != 0), 1);
    BW_CHECK_EQ("setting the money fails", (long)(t->money_set(100) != 0), 1);
    BW_CHECK_EQ("a flag fails", (long)(t->flag(1, &value) != 0), 1);
    BW_CHECK_EQ("setting a flag fails", (long)(t->flag_set(1, 1) != 0), 1);
    BW_CHECK_EQ("the player's position fails",
                (long)(t->player_pos(&x, &y, &z) != 0), 1);

    /* The calls that answer with a value answer with a negative number
     * when there is no such thing. */
    BW_CHECK_EQ("the shiny test has no answer",
                (long)(t->species_shiny(0, 1) < 0), 1);
    BW_CHECK_EQ("the dex has no answer", (long)(t->dex_caught(1) < 0), 1);
    BW_CHECK_EQ("a badge has no answer", (long)(t->badge_has(0) < 0), 1);

    /* The moves between places: a Pokemon that is not in the party is
     * refused outright, and one that is cannot be queued without a
     * party to take it from. */
    ref.place = (u8)BW_MON_BOX;
    BW_CHECK_EQ("a Pokemon that is not in the party is refused",
                (long)(t->mon_to_box(ref) != 0), 1);
    BW_CHECK_EQ("and so is its held item",
                (long)(t->mon_take_item(ref) != 0), 1);
    ref.place = (u8)BW_MON_PARTY;
    BW_CHECK_EQ("a party Pokemon cannot be moved without the party",
                (long)(t->mon_to_box(ref) != 0), 1);
    BW_CHECK_EQ("and its held item cannot be taken back",
                (long)(t->mon_take_item(ref) != 0), 1);
    BW_CHECK_EQ("a party Pokemon cannot be released without the party",
                (long)(t->mon_release(ref) != 0), 1);
    BW_CHECK_EQ("nothing is queued", (long)bw_mod_state_pending(), 0);

    /* The bag: no game data, no bag. */
    BW_CHECK_EQ("the bag count has no answer", (long)(t->item_count(1) < 0), 1);
    BW_CHECK_EQ("an add without a bag fails", (long)(t->item_add(1, 1) != 0), 1);
    BW_CHECK_EQ("an item past the limit is refused",
                (long)(t->item_add(0xFFFFu, 1) != 0), 1);
    BW_CHECK_EQ("no battle is running", (long)t->battle_wild(), -1);
    BW_CHECK_EQ("the area has no answer", (long)(t->area_of(1) < 0), 1);
    BW_CHECK_EQ("the play time reads 0", (long)t->play_seconds(), 0);

    /* A call with no place to answer into fails too. */
    BW_CHECK_EQ("a party reference with no room fails",
                (long)(t->party_ref(0, NULL) != 0), 1);

    BW_REPORT("test_mod_state");
}
