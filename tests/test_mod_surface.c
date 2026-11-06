/*
 * tests/test_mod_surface.c -- the frozen ABI surface, hashed.
 *
 * The surface a mod compiles against is frozen within an interface
 * version (docs/mod-abi.md, section 3).  The rule is that a change to it
 * fails a test until the version is raised and the hash is updated in the
 * same change, so this test recomputes the hash out of the headers
 * themselves and compares it with the number it holds here, and checks
 * that the number in sdk/mod_api.h (BW_MOD_API_HASH) is that same
 * number.  A change outside the headers does not move the hash; a change
 * inside it does.
 *
 * What the hash covers, in the order it folds them in:
 *
 *   - the event header (BwEvent) and the Pokemon reference (BwMonRef):
 *     for each field its name, its offsetof and its size;
 *   - the handler type (BwEventHandler), by its size;
 *   - every event id: its name and its value;
 *   - every small enum: its name and its value;
 *   - every payload struct: its name, the event header it starts with and
 *     each of its own fields;
 *   - the three tables a mod is given (BwModHost, BwModState,
 *     BwModServices): for each member its name, its offsetof and its size;
 *   - the port's own state file header (BwModStateHeader), field by field.
 *
 * The fold is FNV-1a: the seed 2166136261u, the multiplier 16777619u.  A
 * name goes in byte by byte; a number goes in as eight little-endian
 * bytes.
 *
 * The number in sdk/mod_api.h is the 32-bit one.  The three tables hold
 * host pointers -- a function pointer per call, and the host table's two
 * pointers into them -- so their layout follows the pointer width and the
 * hash follows it: the same surface hashes to a different number on a
 * 64-bit build.  The other numbers are here as well, so a change to the
 * surface fails on every width the port is built at.  (The payload
 * structs, the ids and the enums, which are the rest of the surface, hold
 * no pointer at all and are the same on every width.)
 *
 * A change to the surface has to raise BW_MOD_API_VERSION and update
 * both numbers -- the one in sdk/mod_api.h and the one this test holds --
 * in the same change.  The version itself is not checked here: it is the
 * pair that moves, and the hash is the half of it that a test can read.
 *
 * What this test does not cover: the behaviour behind the surface.  The
 * dispatch is tests/test_mod_event.c, the state calls are
 * tests/test_mod_state.c, and the services are theirs to write (step 8 of
 * the build order, docs/mod-implementation.md, section 6).
 */

#include <stddef.h>
#include <stdio.h>

#include "mod_api.h"
#include "mod_state.h"
#include "port_test.h"

/*
 * The hash of the frozen surface on each layout the port is built at.
 *
 *   pointers  hash
 *   4         0xF31D7215u  the number in sdk/mod_api.h
 *   8         0xC25CE7CFu
 *
 * The 32-bit one is the number the header carries.  The other is the same
 * surface on the other layout: the three tables hold host pointers, so
 * their layout follows the pointer width, and the fold puts every number
 * in as eight bytes, so nothing else follows the platform.
 */
static unsigned int expected_hash(void)
{
    return (sizeof(void *) == 4) ? 0xF31D7215u : 0xC25CE7CFu;
}

/* The number in sdk/mod_api.h: the frozen one, the 32-bit hash. */
#define BW_SURFACE_HASH_FROZEN 0xF31D7215u

/* The running hash. */
static unsigned int h;

/* A name, byte by byte. */
static void tag(const char *s)
{
    while (*s) {
        h = (h ^ (unsigned int)(unsigned char)*s) * 16777619u;
        s++;
    }
}

/*
 * A number, as eight little-endian bytes.
 *
 * The width is fixed: the fold puts every number in as eight bytes, so the
 * hash does not follow the platform's `unsigned long`.  Only the pointer
 * width is left, and only because the three tables a mod is handed hold
 * host pointers, so their layout follows it.
 */
static void num(unsigned long v)
{
    int i;

    for (i = 0; i < 8; i++) {
        unsigned int b = (i < (int)sizeof(unsigned long))
                       ? (unsigned int)((v >> (i * 8)) & 0xFFu)
                       : 0u;
        h = (h ^ b) * 16777619u;
    }
}

static void str(const char *s)
{
    tag(s);
}

/* One field: its name, where it sits and how wide it is. */
static void field(const char *name, size_t off, size_t size)
{
    str(name);
    num((unsigned long)off);
    num((unsigned long)size);
}

int main(void)
{
    unsigned int expected;
    const char *width;

    h = 2166136261u;

    /* The event header and the Pokemon reference. */
    str("BwEvent");
    field("size", offsetof(BwEvent, size), sizeof(((BwEvent *)0)->size));
    field("kind", offsetof(BwEvent, kind), sizeof(((BwEvent *)0)->kind));
    str("BwMonRef");
    field("handle", offsetof(BwMonRef, handle), sizeof(((BwMonRef *)0)->handle));
    field("place",  offsetof(BwMonRef, place),  sizeof(((BwMonRef *)0)->place));
    field("side",   offsetof(BwMonRef, side),   sizeof(((BwMonRef *)0)->side));
    field("box",    offsetof(BwMonRef, box),    sizeof(((BwMonRef *)0)->box));
    field("slot",   offsetof(BwMonRef, slot),   sizeof(((BwMonRef *)0)->slot));

    /* The handler type. */
    str("BwEventHandler");
    num(sizeof(BwEventHandler));

    /* The event ids. */
    str("events");
#define EV(n) do { str(#n); num((unsigned long)(n)); } while (0)
    EV(BW_EVENT_GAME_NEW); EV(BW_EVENT_SAVE_BEGIN); EV(BW_EVENT_SAVE_DONE);
    EV(BW_EVENT_SAVE_LOAD); EV(BW_EVENT_GAME_CLEARED);
    EV(BW_EVENT_ROUTE_CHANGE); EV(BW_EVENT_MAP_WARP); EV(BW_EVENT_PLAYER_STEP);
    EV(BW_EVENT_TRAINER_SPOTTED); EV(BW_EVENT_NPC_TALK); EV(BW_EVENT_MENU_OPEN);
    EV(BW_EVENT_MENU_BUILD); EV(BW_EVENT_FIELD_MOVE); EV(BW_EVENT_PC_OPEN);
    EV(BW_EVENT_ENCOUNTER_WILD); EV(BW_EVENT_ENCOUNTER_STATIC);
    EV(BW_EVENT_ENCOUNTER_TRAINER);
    EV(BW_EVENT_MON_CREATE); EV(BW_EVENT_MON_RECEIVE); EV(BW_EVENT_MON_NICKNAME);
    EV(BW_EVENT_MON_EVOLVE); EV(BW_EVENT_MON_EXP); EV(BW_EVENT_MON_LEVELUP);
    EV(BW_EVENT_MOVE_LEARN); EV(BW_EVENT_MON_RELEASE); EV(BW_EVENT_BOX_DEPOSIT);
    EV(BW_EVENT_BOX_WITHDRAW); EV(BW_EVENT_PARTY_SWAP); EV(BW_EVENT_MON_HELDITEM);
    EV(BW_EVENT_EGG_HATCH); EV(BW_EVENT_TRADE_INGAME);
    EV(BW_EVENT_BATTLE_START); EV(BW_EVENT_BATTLE_ACTION); EV(BW_EVENT_BATTLE_ITEM);
    EV(BW_EVENT_BATTLE_SWITCH); EV(BW_EVENT_BATTLE_ESCAPE); EV(BW_EVENT_BATTLE_MOVE);
    EV(BW_EVENT_BATTLE_DAMAGE); EV(BW_EVENT_BATTLE_FAINT); EV(BW_EVENT_CATCH_ATTEMPT);
    EV(BW_EVENT_CATCH_RESULT); EV(BW_EVENT_BATTLE_END); EV(BW_EVENT_WHITEOUT);
    EV(BW_EVENT_BATTLE_TRAINER_MON); EV(BW_EVENT_BATTLE_EXP);
    EV(BW_EVENT_BATTLE_AI); EV(BW_EVENT_BATTLE_AI_REPLACE); EV(BW_EVENT_BATTLE_ABILITY);
    EV(BW_EVENT_BATTLE_STAT); EV(BW_EVENT_BATTLE_HELD_ITEM); EV(BW_EVENT_BATTLE_STATUS);
    EV(BW_EVENT_BATTLE_TURN);
    EV(BW_EVENT_ITEM_ADD); EV(BW_EVENT_ITEM_USE); EV(BW_EVENT_SHOP_BUY);
    EV(BW_EVENT_MONEY_CHANGE); EV(BW_EVENT_HEAL); EV(BW_EVENT_HEAL_HP); EV(BW_EVENT_MON_REVIVE);
    EV(BW_EVENT_MON_CURE); EV(BW_EVENT_MON_PP); EV(BW_EVENT_MON_FORM);
    EV(BW_EVENT_BADGE_GET); EV(BW_EVENT_FLAG_SET); EV(BW_EVENT_DEX_REGISTER);
#undef EV

    /* The small enums. */
    str("enums");
#define EN(n) do { str(#n); num((unsigned long)(n)); } while (0)
    EN(BW_MON_PARTY); EN(BW_MON_BOX); EN(BW_MON_BATTLE); EN(BW_MON_WILD);
    EN(BW_EVENT_CONTINUE); EN(BW_EVENT_REFUSE);
    EN(BW_ENCOUNTER_GRASS); EN(BW_ENCOUNTER_DARK_GRASS); EN(BW_ENCOUNTER_SURF);
    EN(BW_ENCOUNTER_FISHING); EN(BW_ENCOUNTER_SHAKING); EN(BW_ENCOUNTER_RIPPLING);
    EN(BW_ENCOUNTER_DUST);
    EN(BW_RECEIVE_CATCH); EN(BW_RECEIVE_GIFT); EN(BW_RECEIVE_STATIC);
    EN(BW_RECEIVE_EGG); EN(BW_RECEIVE_TRADE);
    EN(BW_FORMAT_SINGLE); EN(BW_FORMAT_DOUBLE); EN(BW_FORMAT_TRIPLE);
    EN(BW_FORMAT_ROTATION); EN(BW_FORMAT_MULTI);
    EN(BW_ACTION_FIGHT); EN(BW_ACTION_SWITCH); EN(BW_ACTION_RUN);
    EN(BW_AI_ITEM); EN(BW_AI_SWITCH); EN(BW_AI_MOVE);
    EN(BW_OUTCOME_WIN); EN(BW_OUTCOME_LOSS); EN(BW_OUTCOME_FLED);
    EN(BW_OUTCOME_CAUGHT); EN(BW_OUTCOME_DRAW);
    EN(BW_MOVE_SOURCE_LEVEL); EN(BW_MOVE_SOURCE_TM); EN(BW_MOVE_SOURCE_TUTOR);
    EN(BW_MOVE_SOURCE_RELEARNER);
    EN(BW_EVOLVE_LEVEL); EN(BW_EVOLVE_ITEM); EN(BW_EVOLVE_TRADE);
    EN(BW_EVOLVE_FRIENDSHIP); EN(BW_EVOLVE_OTHER);
    EN(BW_HEAL_CENTRE); EN(BW_HEAL_SCRIPT); EN(BW_HEAL_POWER);
    EN(BW_HEAL_HP_FIELD); EN(BW_HEAL_HP_BATTLE); EN(BW_HEAL_HP_PARTY);
    EN(BW_EXP_BATTLE); EN(BW_EXP_DAYCARE);
    EN(BW_MONEY_OTHER); EN(BW_MONEY_SCRIPT); EN(BW_MONEY_PURCHASE);
    EN(BW_MONEY_SALE); EN(BW_MONEY_BATTLE);
    EN(BW_LEVELUP_BATTLE); EN(BW_LEVELUP_ITEM);
    EN(BW_ZONE_WARP); EN(BW_ZONE_WALK);
    EN(BW_CURE_FIELD_ITEM); EN(BW_CURE_BATTLE); EN(BW_CURE_PARTY);
    EN(BW_PP_FIELD_ITEM); EN(BW_PP_BATTLE); EN(BW_FORM_FIELD); EN(BW_FORM_BATTLE);
    EN(BW_ITEM_FROM_OTHER); EN(BW_ITEM_FROM_SCRIPT); EN(BW_ITEM_FROM_GIFT);
    EN(BW_ITEM_FROM_PURCHASE); EN(BW_ITEM_FROM_BATTLE); EN(BW_ITEM_FROM_RETURN);
    EN(BW_ITEM_FROM_REWARD);
    EN(BW_REVIVE_FIELD_ITEM); EN(BW_REVIVE_BATTLE_ITEM); EN(BW_REVIVE_MOVE);
    EN(BW_REVIVE_CENTRE); EN(BW_REVIVE_POWER); EN(BW_REVIVE_SCRIPT);
    EN(BW_SESSION_MESSAGE); EN(BW_SESSION_JOIN); EN(BW_SESSION_LEAVE);
    EN(BW_RESTART_NEW_GAME); EN(BW_RESTART_DELETE_SAVE);
    EN(BW_MON_FIELD_SPECIES); EN(BW_MON_FIELD_ITEM); EN(BW_MON_FIELD_EXP);
    EN(BW_MON_FIELD_LEVEL); EN(BW_MON_FIELD_HP); EN(BW_MON_FIELD_MAX_HP);
    EN(BW_MON_FIELD_ATK); EN(BW_MON_FIELD_DEF); EN(BW_MON_FIELD_SPEED);
    EN(BW_MON_FIELD_SPATK); EN(BW_MON_FIELD_SPDEF); EN(BW_MON_FIELD_TYPE1);
    EN(BW_MON_FIELD_TYPE2);
    EN(BW_MON_FIELD_PERSONALITY); EN(BW_MON_FIELD_OT_ID); EN(BW_MON_FIELD_FRIENDSHIP);
    EN(BW_MON_FIELD_ABILITY); EN(BW_MON_FIELD_MOVE1); EN(BW_MON_FIELD_MOVE2);
    EN(BW_MON_FIELD_MOVE3); EN(BW_MON_FIELD_MOVE4); EN(BW_MON_FIELD_IS_EGG);
    EN(BW_MON_FIELD_GENDER); EN(BW_MON_FIELD_FORM); EN(BW_MON_FIELD_NATURE);
    EN(BW_MON_FIELD_HAS_NICKNAME);
    EN(BW_MON_FIELD_STAGE_ATK); EN(BW_MON_FIELD_STAGE_DEF); EN(BW_MON_FIELD_STAGE_SPATK);
    EN(BW_MON_FIELD_STAGE_SPDEF); EN(BW_MON_FIELD_STAGE_SPEED);
    EN(BW_MON_FIELD_STAGE_ACCURACY); EN(BW_MON_FIELD_STAGE_EVASION);
    EN(BW_MON_FIELD_STATUS); EN(BW_MON_FIELD_BATTLE_ABILITY);
    EN(BW_MON_FIELD_FATEFUL); EN(BW_MON_FIELD_ORIGIN_GAME);
    EN(BW_MON_FIELD_MET_LOCATION); EN(BW_MON_FIELD_BALL); EN(BW_MON_FIELD_MET_LEVEL);
    EN(BW_MON_FIELD_PP1); EN(BW_MON_FIELD_PP2); EN(BW_MON_FIELD_PP3); EN(BW_MON_FIELD_PP4);
    EN(BW_MOVE_FIELD_RANK_COUNT); EN(BW_MOVE_FIELD_RANK_TYPE1); EN(BW_MOVE_FIELD_RANK_TYPE2);
    EN(BW_MOVE_FIELD_RANK_TYPE3); EN(BW_MOVE_FIELD_RANK_VALUE1); EN(BW_MOVE_FIELD_RANK_VALUE2);
    EN(BW_MOVE_FIELD_RANK_VALUE3);
#undef EN

    /* The payload structs: the header plus every field. */
    str("payloads");
#define PL0(s) do { str(#s); field("h", offsetof(s, h), sizeof(((s *)0)->h)); } while (0)
#define PL(s, ...) do { str(#s); field("h", offsetof(s, h), sizeof(((s *)0)->h)); \
                        PAYLOAD_FIELDS(s, __VA_ARGS__); } while (0)
#define F(s, n) field(#n, offsetof(s, n), sizeof(((s *)0)->n))
#define PAYLOAD_FIELDS(s, ...) __VA_ARGS__
    PL0(BwGameNew);
    PL(BwSaveBegin, F(BwSaveBegin, play_seconds), F(BwSaveBegin, zone));
    PL(BwSaveDone, F(BwSaveDone, play_seconds), F(BwSaveDone, zone), F(BwSaveDone, ok));
    PL(BwSaveLoad, F(BwSaveLoad, play_seconds), F(BwSaveLoad, zone), F(BwSaveLoad, state_in_step));
    PL(BwGameCleared, F(BwGameCleared, play_seconds), F(BwGameCleared, team_count), F(BwGameCleared, team));
    PL(BwRouteChange, F(BwRouteChange, zone), F(BwRouteChange, prev_zone), F(BwRouteChange, season),
       F(BwRouteChange, how));
    PL(BwMapWarp, F(BwMapWarp, zone), F(BwMapWarp, dest_zone), F(BwMapWarp, dest_x), F(BwMapWarp, dest_z));
    PL(BwPlayerStep, F(BwPlayerStep, zone), F(BwPlayerStep, x), F(BwPlayerStep, y),
       F(BwPlayerStep, z), F(BwPlayerStep, dir), F(BwPlayerStep, mode));
    PL(BwTrainerSpotted, F(BwTrainerSpotted, trainer_id), F(BwTrainerSpotted, partner_id),
       F(BwTrainerSpotted, distance));
    PL(BwNpcTalk, F(BwNpcTalk, zone), F(BwNpcTalk, object_id), F(BwNpcTalk, script_id));
    PL0(BwMenuOpen);
    PL(BwMenuBuild, F(BwMenuBuild, entries));
    PL(BwFieldMove, F(BwFieldMove, move), F(BwFieldMove, user));
    PL0(BwPcOpen);
    PL(BwEncounterWild, F(BwEncounterWild, zone), F(BwEncounterWild, method), F(BwEncounterWild, slot),
       F(BwEncounterWild, species), F(BwEncounterWild, level), F(BwEncounterWild, shiny),
       F(BwEncounterWild, double_), F(BwEncounterWild, reroll),
       F(BwEncounterWild, level2), F(BwEncounterWild, species2));
    PL(BwEncounterStatic, F(BwEncounterStatic, zone), F(BwEncounterStatic, species),
       F(BwEncounterStatic, level), F(BwEncounterStatic, kind));
    PL(BwEncounterTrainer, F(BwEncounterTrainer, trainer_id), F(BwEncounterTrainer, partner_id),
       F(BwEncounterTrainer, format));
    PL(BwMonCreate, F(BwMonCreate, species), F(BwMonCreate, level), F(BwMonCreate, shiny),
       F(BwMonCreate, nature), F(BwMonCreate, personality));
    PL(BwMonReceive, F(BwMonReceive, mon), F(BwMonReceive, source), F(BwMonReceive, to_box),
       F(BwMonReceive, species), F(BwMonReceive, level), F(BwMonReceive, shiny),
       F(BwMonReceive, zone));
    PL(BwMonNickname, F(BwMonNickname, mon), F(BwMonNickname, force));
    PL(BwMonEvolve, F(BwMonEvolve, mon), F(BwMonEvolve, from), F(BwMonEvolve, to),
       F(BwMonEvolve, method));
    PL(BwMonExp, F(BwMonExp, mon), F(BwMonExp, amount), F(BwMonExp, exp), F(BwMonExp, level),
       F(BwMonExp, source), F(BwMonExp, wild));
    PL(BwMonLevelup, F(BwMonLevelup, mon), F(BwMonLevelup, from), F(BwMonLevelup, to),
       F(BwMonLevelup, source), F(BwMonLevelup, item));
    PL(BwMoveLearn, F(BwMoveLearn, mon), F(BwMoveLearn, move), F(BwMoveLearn, source),
       F(BwMoveLearn, slot), F(BwMoveLearn, replaced));
    PL(BwMonRelease, F(BwMonRelease, mon), F(BwMonRelease, species));
    PL(BwBoxDeposit, F(BwBoxDeposit, mon), F(BwBoxDeposit, box), F(BwBoxDeposit, slot));
    PL(BwBoxWithdraw, F(BwBoxWithdraw, mon), F(BwBoxWithdraw, box), F(BwBoxWithdraw, slot));
    PL(BwPartySwap, F(BwPartySwap, slot_a), F(BwPartySwap, slot_b));
    PL(BwMonHelditem, F(BwMonHelditem, mon), F(BwMonHelditem, item), F(BwMonHelditem, previous));
    PL(BwEggHatch, F(BwEggHatch, mon), F(BwEggHatch, species));
    PL(BwTradeIngame, F(BwTradeIngame, trade_id), F(BwTradeIngame, given),
       F(BwTradeIngame, received_species));
    PL(BwBattleStart, F(BwBattleStart, wild), F(BwBattleStart, format),
       F(BwBattleStart, trainer_id), F(BwBattleStart, level_adjust),
       F(BwBattleStart, opponent_party));
    PL(BwBattleAction, F(BwBattleAction, mon), F(BwBattleAction, side), F(BwBattleAction, action),
       F(BwBattleAction, move), F(BwBattleAction, switch_to));
    PL(BwBattleSwitch, F(BwBattleSwitch, out), F(BwBattleSwitch, in), F(BwBattleSwitch, forced));
    PL(BwBattleEscape, F(BwBattleEscape, mon));
    PL(BwBattleMove, F(BwBattleMove, user), F(BwBattleMove, move), F(BwBattleMove, target),
       F(BwBattleMove, turn));
    PL(BwBattleDamage, F(BwBattleDamage, attacker), F(BwBattleDamage, target),
       F(BwBattleDamage, move), F(BwBattleDamage, amount), F(BwBattleDamage, target_hp),
       F(BwBattleDamage, target_max_hp), F(BwBattleDamage, critical),
       F(BwBattleDamage, effectiveness));
    PL(BwBattleFaint, F(BwBattleFaint, mon), F(BwBattleFaint, side), F(BwBattleFaint, cause),
       F(BwBattleFaint, cause_move), F(BwBattleFaint, cause_damage), F(BwBattleFaint, turn));
    PL(BwBattleItem, F(BwBattleItem, item), F(BwBattleItem, mon), F(BwBattleItem, ball),
       F(BwBattleItem, revive));
    PL(BwCatchAttempt, F(BwCatchAttempt, target), F(BwCatchAttempt, species),
       F(BwCatchAttempt, level), F(BwCatchAttempt, shiny), F(BwCatchAttempt, ball),
       F(BwCatchAttempt, rate), F(BwCatchAttempt, static_));
    PL(BwCatchResult, F(BwCatchResult, target), F(BwCatchResult, species),
       F(BwCatchResult, ball), F(BwCatchResult, caught), F(BwCatchResult, shakes));
    PL(BwBattleEnd, F(BwBattleEnd, outcome), F(BwBattleEnd, wild), F(BwBattleEnd, trainer_id),
       F(BwBattleEnd, turns));
    PL(BwWhiteout, F(BwWhiteout, in_battle), F(BwWhiteout, money_lost));
    PL(BwBattleExp, F(BwBattleExp, members), F(BwBattleExp, wild),
       F(BwBattleExp, scale), F(BwBattleExp, total), F(BwBattleExp, share),
       F(BwBattleExp, share_pad));
    PL(BwBattleAi, F(BwBattleAi, mon), F(BwBattleAi, target), F(BwBattleAi, stage),
       F(BwBattleAi, switch_to), F(BwBattleAi, move), F(BwBattleAi, target_pos),
       F(BwBattleAi, item), F(BwBattleAi, pad), F(BwBattleAi, scores));
    PL(BwBattleAiReplace, F(BwBattleAiReplace, foe), F(BwBattleAiReplace, count),
       F(BwBattleAiReplace, order), F(BwBattleAiReplace, pad));
    PL(BwBattleAbility, F(BwBattleAbility, mon), F(BwBattleAbility, ability),
       F(BwBattleAbility, side), F(BwBattleAbility, pad));
    PL(BwBattleStat, F(BwBattleStat, mon), F(BwBattleStat, stat), F(BwBattleStat, delta),
       F(BwBattleStat, side), F(BwBattleStat, pad));
    PL(BwBattleHeldItem, F(BwBattleHeldItem, mon), F(BwBattleHeldItem, item),
       F(BwBattleHeldItem, side), F(BwBattleHeldItem, pad));
    PL(BwBattleStatus, F(BwBattleStatus, mon), F(BwBattleStatus, source),
       F(BwBattleStatus, status), F(BwBattleStatus, applied), F(BwBattleStatus, side));
    PL(BwBattleTurn, F(BwBattleTurn, turn), F(BwBattleTurn, weather), F(BwBattleTurn, pad));
    PL(BwBattleTrainerMon, F(BwBattleTrainerMon, trainer_id), F(BwBattleTrainerMon, index),
       F(BwBattleTrainerMon, level), F(BwBattleTrainerMon, species),
       F(BwBattleTrainerMon, item), F(BwBattleTrainerMon, move1),
       F(BwBattleTrainerMon, move2), F(BwBattleTrainerMon, move3),
       F(BwBattleTrainerMon, move4), F(BwBattleTrainerMon, ability),
       F(BwBattleTrainerMon, form));
    PL(BwItemAdd, F(BwItemAdd, item), F(BwItemAdd, count), F(BwItemAdd, pocket),
       F(BwItemAdd, source));
    PL(BwItemUse, F(BwItemUse, item), F(BwItemUse, target));
    PL(BwShopBuy, F(BwShopBuy, item), F(BwShopBuy, price));
    PL(BwMoneyChange, F(BwMoneyChange, delta), F(BwMoneyChange, before),
       F(BwMoneyChange, source));
    PL(BwHeal, F(BwHeal, source), F(BwHeal, mask), F(BwHeal, script_id));
    PL(BwHealHp, F(BwHealHp, mon), F(BwHealHp, hp), F(BwHealHp, max_hp),
       F(BwHealHp, amount), F(BwHealHp, source));
    PL(BwMonRevive, F(BwMonRevive, mon), F(BwMonRevive, hp), F(BwMonRevive, max_hp),
       F(BwMonRevive, item), F(BwMonRevive, move), F(BwMonRevive, source));
    PL(BwMonCure, F(BwMonCure, mon), F(BwMonCure, status), F(BwMonCure, source),
       F(BwMonCure, item));
    PL(BwMonPp, F(BwMonPp, mon), F(BwMonPp, slot), F(BwMonPp, source),
       F(BwMonPp, move), F(BwMonPp, amount));
    PL(BwMonForm, F(BwMonForm, mon), F(BwMonForm, species), F(BwMonForm, from),
       F(BwMonForm, to), F(BwMonForm, source));
    PL(BwBadgeGet, F(BwBadgeGet, badge), F(BwBadgeGet, count));
    PL(BwFlagSet, F(BwFlagSet, flag), F(BwFlagSet, value));
    PL(BwDexRegister, F(BwDexRegister, species), F(BwDexRegister, caught));
#undef PL
#undef PL0
#undef F
#undef PAYLOAD_FIELDS

    /* The script commands. */
    str("BwScriptCommand");
    field("opcode", offsetof(BwScriptCommand, opcode), sizeof(((BwScriptCommand *)0)->opcode));
    field("nargs", offsetof(BwScriptCommand, nargs), sizeof(((BwScriptCommand *)0)->nargs));
    field("args", offsetof(BwScriptCommand, args), sizeof(((BwScriptCommand *)0)->args));
    field("pc", offsetof(BwScriptCommand, pc), sizeof(((BwScriptCommand *)0)->pc));
    field("next_pc", offsetof(BwScriptCommand, next_pc), sizeof(((BwScriptCommand *)0)->next_pc));
    str("BwScriptHandler");
    num(sizeof(BwScriptHandler));
    str("script enums");
#define EN(n) do { str(#n); num((unsigned long)(n)); } while (0)
    EN(BW_SCRIPT_VM_FIELD); EN(BW_SCRIPT_VM_BATTLE);
    EN(BW_SCRIPT_CONTINUE); EN(BW_SCRIPT_REPLACE);
#undef EN

    /* The host table. */
    str("BwModHost");
    field("api", offsetof(BwModHost, api), sizeof(((BwModHost *)0)->api));
    field("on", offsetof(BwModHost, on), sizeof(((BwModHost *)0)->on));
    field("script", offsetof(BwModHost, script), sizeof(((BwModHost *)0)->script));
    field("state", offsetof(BwModHost, state), sizeof(((BwModHost *)0)->state));
    field("services", offsetof(BwModHost, services), sizeof(((BwModHost *)0)->services));

    /* The state table. */
    str("BwModState");
#define S(n) field(#n, offsetof(BwModState, n), sizeof(((BwModState *)0)->n))
    S(party_count); S(party_ref); S(party_get); S(party_set);
    S(box_count); S(box_ref); S(box_get); S(box_set);
    S(battle_ref); S(battle_get); S(battle_set);
    S(species_value); S(species_shiny);
    S(dex_caught);
    S(badge_count); S(badge_has); S(money); S(money_set); S(flag); S(flag_set);
    S(zone); S(player_pos); S(battle_style);
    S(mon_to_box); S(mon_take_item);
    S(mon_release); S(item_count); S(item_add);
    S(area_of); S(battle_wild); S(play_seconds); S(item_value); S(move_value);
    S(mon_to_box_n); S(mon_from_box); S(mon_recalc); S(species_base); S(battle_trainer);
    S(type_effect); S(battle_weather); S(species_learns); S(whiteout);
#undef S

    /* The services table. */
    str("BwModServices");
#define V(n) field(#n, offsetof(BwModServices, n), sizeof(((BwModServices *)0)->n))
    V(message); V(confirm);
    V(session_host); V(session_join); V(session_leave); V(session_send); V(on_session);
    V(avatar_create); V(avatar_move); V(avatar_delete);
    V(mon_export); V(mon_import);
    V(panel_set); V(panel_clear);
    V(timer_start); V(timer_stop); V(timer_ms); V(split);
    V(run_restart);
    V(rand_seed); V(rand_next);
    V(on_command); V(publish);
    V(input_press);
    V(on_frame);
    V(save_send); V(on_save_received);
    V(setting_get);
    V(state_load); V(state_store); V(state_clear); V(state_info); V(state_dir);
    V(state_in_step);
    V(table_get);
    V(event_player); V(shared_get); V(shared_set); V(shared_add); V(shared_cas);
#undef V

    /* The state file header. */
    str("BwModStateHeader");
    field("magic", offsetof(BwModStateHeader, magic), sizeof(((BwModStateHeader *)0)->magic));
    field("version", offsetof(BwModStateHeader, version), sizeof(((BwModStateHeader *)0)->version));
    field("flags", offsetof(BwModStateHeader, flags), sizeof(((BwModStateHeader *)0)->flags));
    field("payload_size", offsetof(BwModStateHeader, payload_size), sizeof(((BwModStateHeader *)0)->payload_size));
    field("payload_crc", offsetof(BwModStateHeader, payload_crc), sizeof(((BwModStateHeader *)0)->payload_crc));
    field("api_written", offsetof(BwModStateHeader, api_written), sizeof(((BwModStateHeader *)0)->api_written));
    field("save_check", offsetof(BwModStateHeader, save_check), sizeof(((BwModStateHeader *)0)->save_check));

    /* The width the surface was laid out on. */
    expected = expected_hash();
    if (sizeof(void *) == 4) {
        width = "32-bit pointers";
    } else {
        width = "64-bit pointers";
    }

    printf("surface hash: %08x (%s)\n", h, width);
    printf("expected:     %08x\n", expected);
    printf("covers:       the event header, the Pokemon reference, the handler\n");
    printf("              type, the event ids, the small enums, every payload\n");
    printf("              struct, the script command's view, the host, state\n");
    printf("              and services tables and the state file header\n");

    BW_CHECK_EQ("the surface hash matches", (long)h, (long)expected);
    if (h != expected) {
        printf("  the surface above changed; raise BW_MOD_API_VERSION and\n");
        printf("  update BW_MOD_API_HASH in sdk/mod_api.h and the numbers\n");
        printf("  above in the same change\n");
    }

    /* The number in the header is the frozen one: the 32-bit hash of the
     * surface as it stands.  It cannot be moved without this test. */
    BW_CHECK_EQ("the header's hash is the frozen one",
                (long)BW_MOD_API_HASH, (long)BW_SURFACE_HASH_FROZEN);

    BW_REPORT("test_mod_surface");
}
