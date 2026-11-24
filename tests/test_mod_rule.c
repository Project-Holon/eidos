/*
 * tests/test_mod_rule.c -- the rule interpreter: a rule file, the event it
 * binds to and the actions it runs (docs/mod-rules-language.md).
 *
 * A rule is a guarded action at one event, written in the rule language and
 * compiled by the port at load (docs/mod-rules-language.md, sections 2 and
 * 4).  The port's side of that is sdk/mod_rule.c; the contract it keeps is
 * the one of the document:
 *
 *   - a good rule file compiles, and the rule binds to the event it names;
 *   - the rule runs as a handler like any other: its guard decides whether
 *     its actions run, its actions change the payload the game sees, and a
 *     `refuse` action refuses the event;
 *   - a file with a syntax error is refused at load;
 *   - a rule whose event is unknown is refused at load, and so is a rule
 *     whose file names another event than the manifest binds it to;
 *   - the step budget: a firing may execute at most 256 steps, and a rule
 *     that reaches it stops for that firing without refusing.
 *
 * Nothing here needs game data: the rules are text the test writes into a
 * directory of its own, and the payload they act on is the test's own.
 */

#include <stdio.h>
#include <string.h>

#include "mod_api.h"
#include "mod_rule.h"
#include "mod_table.h"
#include "mod_v7.h"
#include "port_test.h"

#ifdef _WIN32
#include <direct.h>
#define BW_MKDIR(p) _mkdir(p)
#define BW_RMDIR(p) _rmdir(p)
#else
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#define BW_MKDIR(p) mkdir((p), 0777)
#define BW_RMDIR(p) rmdir((p))
#endif

/* ------------------------------------------------------------------ */
/* The rule files the test writes                                      */
/* ------------------------------------------------------------------ */

/* The directory the rule files live in, and the mod's own folder in it. */
#define T_DIR      "mod_rule_tmp"
#define T_MOD_DIR  T_DIR "/the-mod"

/* The namespace the test's rules are bound under, and the mod's name for
 * the reports. */
#define T_NS    "the-mod"
#define T_LABEL "the mod"

/* How many steps a firing may execute (docs/mod-rules-language.md,
 * section 4): 256. */

/* How many terms the rule that reaches the budget sums.  A term and a '+'
 * are a node each and a node is a step, so a sum of this many terms costs
 * about twice as many steps: over the budget of 256, and under the port's
 * own limit on the nodes one rule may hold. */
#define T_BIG_SUM 200

/* Writes a rule file.  Returns 0, or -1 when the file cannot be written. */
static int write_rule(const char *dir, const char *name, const char *text)
{
    char path[256];
    FILE *fp;
    int n;

    n = snprintf(path, sizeof path, "%s/%s", dir, name);
    if (n < 0 || (size_t)n >= sizeof path) {
        return -1;
    }
    fp = fopen(path, "wb");
    if (fp == NULL) {
        return -1;
    }
    fputs(text, fp);
    fclose(fp);
    return 0;
}

/* Writes the rule whose guard needs more steps than the budget allows: a
 * sum of T_BIG_SUM ones, which is true when it is evaluated whole. */
static int write_budget_rule(const char *dir, const char *name)
{
    char text[8192];
    size_t n = 0;
    int i;

    n += (size_t)snprintf(text + n, sizeof text - n,
                           "language 1\n"
                           "rule too_long on encounter.wild\n"
                           "    when 1");
    for (i = 1; i < T_BIG_SUM; i++) {
        n += (size_t)snprintf(text + n, sizeof text - n, "+1");
    }
    n += (size_t)snprintf(text + n, sizeof text - n,
                           " == %d\n"
                           "    do set event.level 0\n",
                           T_BIG_SUM);
    if (n >= sizeof text) {
        return -1;
    }
    return write_rule(dir, name, text);
}

/* Writes a block of bytes.  Returns 0, or -1. */
static int write_bytes(const char *path, const u8 *data, u32 size)
{
    FILE *fp = fopen(path, "wb");

    if (fp == NULL) {
        return -1;
    }
    if (fwrite(data, 1u, size, fp) != size) {
        fclose(fp);
        return -1;
    }
    return fclose(fp) == 0 ? 0 : -1;
}

/* Takes the rule files away. */
static void clean_rules(void)
{
    static const char *const names[] = {
        "good.rule", "refuse.rule", "broken.rule", "bad_event.rule",
        "bad_version.rule", "budget.rule"
    };
    char path[256];
    size_t i;

    for (i = 0; i < sizeof names / sizeof names[0]; i++) {
        snprintf(path, sizeof path, "%s/%s", T_DIR, names[i]);
        remove(path);
        snprintf(path, sizeof path, "%s/%s", T_MOD_DIR, names[i]);
        remove(path);
    }
    BW_RMDIR(T_MOD_DIR);
    BW_RMDIR(T_DIR);
}

/* ------------------------------------------------------------------ */
/* The payload the rules act on                                        */
/* ------------------------------------------------------------------ */

/*
 * The event the test's rules bind to: encounter.wild, whose `level` and
 * `shiny` are fields a rule may write, and which has a refused outcome
 * (docs/mod-events.md, section 1).
 */
static BwEncounterWild make_payload(u8 level)
{
    BwEncounterWild p;

    memset(&p, 0, sizeof(p));
    p.h.size = (u32)sizeof(p);
    p.h.kind = BW_EVENT_ENCOUNTER_WILD;
    p.level = level;
    return p;
}

/* ------------------------------------------------------------------ */
/* The test                                                            */
/* ------------------------------------------------------------------ */

int main(void)
{
    u32 kind;
    u32 other;
    BwEncounterWild payload;

    /* What an earlier run left goes first. */
    clean_rules();
    BW_REQUIRE("the rule directory is made", BW_MKDIR(T_DIR) == 0);
    BW_REQUIRE("the mod's own directory is made", BW_MKDIR(T_MOD_DIR) == 0);

    /* A good rule: a guard on the payload and two actions. */
    BW_REQUIRE("the good rule is written",
               write_rule(T_DIR, "good.rule",
                   "language 1\n"
                   "rule bump_wild on encounter.wild\n"
                   "    when event.level < 5\n"
                   "    do set event.level 5\n"
                   "    do set event.shiny 1\n") == 0);
    /* The same rule in the mod's own folder, for the manifest's binding. */
    BW_REQUIRE("the mod's rule is written",
               write_rule(T_MOD_DIR, "good.rule",
                   "language 1\n"
                   "rule bump_wild on encounter.wild\n"
                   "    when event.level < 5\n"
                   "    do set event.level 5\n"
                   "    do set event.shiny 1\n") == 0);
    /* A rule that refuses the event. */
    BW_REQUIRE("the refusing rule is written",
               write_rule(T_DIR, "refuse.rule",
                   "language 1\n"
                   "rule no_wild on encounter.wild\n"
                   "    do refuse\n") == 0);
    /* A file with a syntax error: the comparison has no right-hand side. */
    BW_REQUIRE("the broken rule is written",
               write_rule(T_DIR, "broken.rule",
                   "language 1\n"
                   "rule broken on encounter.wild\n"
                   "    when event.level >\n"
                   "    do set event.level 0\n") == 0);
    /* A rule whose event is unknown. */
    BW_REQUIRE("the rule with the unknown event is written",
               write_rule(T_DIR, "bad_event.rule",
                   "language 1\n"
                   "rule bad_event on no.such.event\n"
                   "    do refuse\n") == 0);
    /* A rule written for another version of the language. */
    BW_REQUIRE("the rule of another version is written",
               write_rule(T_DIR, "bad_version.rule",
                   "language 2\n"
                   "rule future on encounter.wild\n"
                   "    do refuse\n") == 0);
    /* The rule that needs more steps than the budget allows. */
    BW_REQUIRE("the long rule is written",
               write_budget_rule(T_DIR, "budget.rule") == 0);

    printf("the event a rule binds to\n");
    kind = bw_mod_rule_event_kind("encounter.wild");
    BW_CHECK_EQ("the event has its id", (long)kind,
                (long)BW_EVENT_ENCOUNTER_WILD);
    BW_CHECK_EQ("the event has its name back",
                strcmp(bw_mod_rule_event_name(kind), "encounter.wild"), 0);
    BW_CHECK_EQ("an event the port does not know has no id",
                (long)bw_mod_rule_event_kind("no.such.event"), 0);
    other = bw_mod_rule_event_kind("catch.attempt");
    BW_CHECK_EQ("another event has another id", (long)other,
                (long)BW_EVENT_CATCH_ATTEMPT);

    printf("a good rule\n");
    bw_mod_rule_reset();
    BW_REQUIRE("the rule binds",
               bw_mod_rule_bind(kind, T_DIR "/good.rule", T_NS, T_LABEL) == 0);
    BW_CHECK_EQ("one rule is bound to the event", (long)bw_mod_rule_count(kind),
                1);

    printf("the actions it runs\n");
    payload = make_payload(2);
    BW_CHECK_EQ("the rule does not refuse",
                bw_mod_rule_run(kind, (BwEvent *)&payload), 0);
    BW_CHECK_EQ("the first action changed the payload", (long)payload.level, 5);
    BW_CHECK_EQ("the second action changed it too", (long)payload.shiny, 1);

    printf("the guard\n");
    /* The guard is false, so the actions do not run. */
    payload = make_payload(10);
    BW_CHECK_EQ("the rule does not refuse",
                bw_mod_rule_run(kind, (BwEvent *)&payload), 0);
    BW_CHECK_EQ("the payload is untouched", (long)payload.level, 10);
    BW_CHECK_EQ("and the other field too", (long)payload.shiny, 0);

    printf("a rule that refuses\n");
    bw_mod_rule_reset();
    BW_REQUIRE("the rule binds",
               bw_mod_rule_bind(kind, T_DIR "/refuse.rule", T_NS, T_LABEL) == 0);
    payload = make_payload(2);
    BW_CHECK_EQ("the rule refuses the event",
                bw_mod_rule_run(kind, (BwEvent *)&payload), 1);

    printf("an operator next to a minus\n");
    /* `*-` and `<-` are not pairs of the language: each reads as an
     * operator and a negative number. */
    BW_REQUIRE("the rule is written",
               write_rule(T_DIR, "negops.rule",
                   "language 1\n"
                   "rule negops on encounter.wild\n"
                   "    when event.level*-1 <-1\n"
                   "    do set event.level 7\n") == 0);
    bw_mod_rule_reset();
    BW_REQUIRE("the rule binds",
               bw_mod_rule_bind(kind, T_DIR "/negops.rule", T_NS, T_LABEL) == 0);
    payload = make_payload(2);
    bw_mod_rule_run(kind, (BwEvent *)&payload);
    BW_CHECK_EQ("the guard read -2 < -1", (long)payload.level, 7);

    printf("a table read\n");
    BW_REQUIRE("the table is written",
               write_rule(T_DIR, "caps.txt", "1 = 9\ndefault = 3\n") == 0);
    BW_REQUIRE("the table loads",
               bw_mod_table_load(T_NS, "caps", T_DIR "/caps.txt", T_LABEL) == 0);
    BW_REQUIRE("the rule is written",
               write_rule(T_DIR, "table.rule",
                   "language 1\n"
                   "rule cap on encounter.wild\n"
                   "    do set event.level table.caps[event.level] + table.caps[50]\n") == 0);
    BW_REQUIRE("the rule is written",
               write_rule(T_DIR, "notable.rule",
                   "language 1\n"
                   "rule cap on encounter.wild\n"
                   "    do set event.level table.nope[1]\n") == 0);
    bw_mod_rule_reset();
    BW_REQUIRE("the rule binds",
               bw_mod_rule_bind(kind, T_DIR "/table.rule", T_NS, T_LABEL) == 0);
    payload = make_payload(1);
    bw_mod_rule_run(kind, (BwEvent *)&payload);
    BW_CHECK_EQ("a key and the default", (long)payload.level, 12);
    BW_CHECK_EQ("a rule that reads a table the mod has not is refused",
                bw_mod_rule_bind(kind, T_DIR "/notable.rule", T_NS, T_LABEL) != 0, 1);
    bw_mod_table_reset();
    remove(T_DIR "/negops.rule");
    remove(T_DIR "/table.rule");
    remove(T_DIR "/notable.rule");
    remove(T_DIR "/caps.txt");

    printf("the manifest's binding\n");
    /* The manifest's rule line is `rule = <event> <path>`, and the path is
     * relative to the mod's own folder. */
    bw_mod_rule_reset();
    BW_REQUIRE("the rule binds",
               bw_mod_rule_bind_manifest(T_DIR, "the-mod", "encounter.wild",
                                        "good.rule", T_NS, T_LABEL) == 0);
    BW_CHECK_EQ("one rule is bound to the event", (long)bw_mod_rule_count(kind),
                1);
    payload = make_payload(2);
    BW_CHECK_EQ("the rule does not refuse",
                bw_mod_rule_run(kind, (BwEvent *)&payload), 0);
    BW_CHECK_EQ("the action changed the payload", (long)payload.level, 5);

    printf("a file with a syntax error\n");
    bw_mod_rule_reset();
    BW_CHECK_EQ("the rule is refused",
                bw_mod_rule_bind(kind, T_DIR "/broken.rule", T_NS, T_LABEL), -1);
    BW_CHECK_EQ("no rule is bound", (long)bw_mod_rule_count(kind), 0);

    printf("a rule whose event is unknown\n");
    BW_CHECK_EQ("the rule is refused",
                bw_mod_rule_bind(kind, T_DIR "/bad_event.rule", T_NS, T_LABEL),
                -1);
    BW_CHECK_EQ("and so is a rule for an event the port does not know",
                bw_mod_rule_bind(0u, T_DIR "/good.rule", T_NS, T_LABEL), -1);
    BW_CHECK_EQ("no rule is bound", (long)bw_mod_rule_count(kind), 0);

    printf("a rule whose file names another event\n");
    /* The port refuses the rule when the manifest's event and the file's
     * disagree. */
    BW_CHECK_EQ("the rule is refused",
                bw_mod_rule_bind(other, T_DIR "/good.rule", T_NS, T_LABEL), -1);
    BW_CHECK_EQ("no rule is bound", (long)bw_mod_rule_count(other), 0);

    printf("a rule written for another version\n");
    BW_CHECK_EQ("the rule is refused",
                bw_mod_rule_bind(kind, T_DIR "/bad_version.rule", T_NS, T_LABEL),
                -1);

    printf("the step budget\n");
    /* A firing may execute at most 256 steps.  The rule's guard needs more
     * than that, so the rule stops for that firing: it does not refuse and
     * its action does not run. */
    bw_mod_rule_reset();
    BW_REQUIRE("the rule binds",
               bw_mod_rule_bind(kind, T_DIR "/budget.rule", T_NS, T_LABEL) == 0);
    payload = make_payload(2);
    BW_CHECK_EQ("the rule does not refuse",
                bw_mod_rule_run(kind, (BwEvent *)&payload), 0);
    BW_CHECK_EQ("and its action did not run", (long)payload.level, 2);

    printf("version 7: the gift action\n");
    {
        u8 card[BW_MYSTERY_CARD_SIZE];

        memset(card, 0, sizeof card);
        card[BW_MYSTERY_CARD_TYPE] = 1;
        card[BW_MYSTERY_CARD_ID] = 7;
        BW_MKDIR(T_MOD_DIR "/rules");
        BW_REQUIRE("the card is written beside the rule",
                   write_bytes(T_DIR "/card.pgf", card, sizeof card) == 0);
        BW_REQUIRE("and in the mod's folder",
                   write_bytes(T_MOD_DIR "/modcard.pgf", card, sizeof card) == 0);
        BW_REQUIRE("a file that is not a card is written",
                   write_bytes(T_DIR "/short.pgf", card, 10) == 0);
        BW_REQUIRE("the rules are written",
                   write_rule(T_DIR, "gift.rule",
                       "language 1\n"
                       "rule card on encounter.wild\n"
                       "    do gift \"card.pgf\"\n"
                       "    do set event.level 9\n") == 0 &&
                   write_rule(T_DIR, "gift_missing.rule",
                       "language 1\n"
                       "rule card on encounter.wild\n"
                       "    do gift \"nothing.pgf\"\n") == 0 &&
                   write_rule(T_DIR, "gift_short.rule",
                       "language 1\n"
                       "rule card on encounter.wild\n"
                       "    do gift \"short.pgf\"\n") == 0 &&
                   write_rule(T_DIR, "gift_outside.rule",
                       "language 1\n"
                       "rule card on encounter.wild\n"
                       "    do gift \"../" T_DIR "/card.pgf\"\n") == 0 &&
                   write_rule(T_DIR, "gift_number.rule",
                       "language 1\n"
                       "rule card on encounter.wild\n"
                       "    do gift 5\n") == 0 &&
                   write_rule(T_DIR, "gift_more.rule",
                       "language 1\n"
                       "rule card on encounter.wild\n"
                       "    do gift \"card.pgf\" 1\n") == 0 &&
                   write_rule(T_MOD_DIR "/rules", "gift.rule",
                       "language 1\n"
                       "rule card on encounter.wild\n"
                       "    do gift \"modcard.pgf\"\n") == 0);
        bw_mod_rule_reset();
        BW_REQUIRE("a rule with a card beside it binds",
                   bw_mod_rule_bind(kind, T_DIR "/gift.rule", T_NS, T_LABEL) == 0);
        /* No game is loaded here, so the action fails: the rule stops
         * for this firing and does not refuse. */
        payload = make_payload(2);
        BW_CHECK_EQ("the rule does not refuse",
                    bw_mod_rule_run(kind, (BwEvent *)&payload), 0);
        BW_CHECK_EQ("the action after the failed one did not run", (long)payload.level, 2);
        BW_CHECK_EQ("a card file that is not there: the rule is refused",
                    bw_mod_rule_bind(kind, T_DIR "/gift_missing.rule", T_NS, T_LABEL), -1);
        BW_CHECK_EQ("a file that is not a card: refused",
                    bw_mod_rule_bind(kind, T_DIR "/gift_short.rule", T_NS, T_LABEL), -1);
        BW_CHECK_EQ("a file outside the folder: refused",
                    bw_mod_rule_bind(kind, T_DIR "/gift_outside.rule", T_NS, T_LABEL), -1);
        BW_CHECK_EQ("a number in place of the file's name: refused",
                    bw_mod_rule_bind(kind, T_DIR "/gift_number.rule", T_NS, T_LABEL), -1);
        BW_CHECK_EQ("something after the file's name: refused",
                    bw_mod_rule_bind(kind, T_DIR "/gift_more.rule", T_NS, T_LABEL), -1);
        /* From a manifest the card's name is relative to the mod's
         * folder, not to the rule file's. */
        bw_mod_rule_reset();
        BW_CHECK_EQ("the manifest's rule finds the card in the mod's folder",
                    bw_mod_rule_bind_manifest(T_MOD_DIR, "the-mod", "encounter.wild",
                                              "rules/gift.rule", T_NS, T_LABEL), 0);
        BW_CHECK_EQ("bound alone, the same rule looks beside itself and is refused",
                    bw_mod_rule_bind(kind, T_MOD_DIR "/rules/gift.rule", T_NS, T_LABEL), -1);
        remove(T_DIR "/card.pgf");
        remove(T_DIR "/short.pgf");
        remove(T_DIR "/gift.rule");
        remove(T_DIR "/gift_missing.rule");
        remove(T_DIR "/gift_short.rule");
        remove(T_DIR "/gift_outside.rule");
        remove(T_DIR "/gift_number.rule");
        remove(T_DIR "/gift_more.rule");
        remove(T_MOD_DIR "/rules/gift.rule");
        remove(T_MOD_DIR "/modcard.pgf");
        BW_RMDIR(T_MOD_DIR "/rules");
    }

    printf("version 7: the National Pokedex field\n");
    BW_REQUIRE("the rules are written",
               write_rule(T_DIR, "natdex.rule",
                   "language 1\n"
                   "rule natdex on encounter.wild\n"
                   "    when national_dex == 0\n"
                   "    do set national_dex 1\n") == 0 &&
               write_rule(T_DIR, "natdex_sub.rule",
                   "language 1\n"
                   "rule natdex on encounter.wild\n"
                   "    do set national_dex[1] 1\n") == 0);
    bw_mod_rule_reset();
    BW_REQUIRE("a rule that reads and writes national_dex binds",
               bw_mod_rule_bind(kind, T_DIR "/natdex.rule", T_NS, T_LABEL) == 0);
    payload = make_payload(2);
    BW_CHECK_EQ("with no game the read fails and the rule does not refuse",
                bw_mod_rule_run(kind, (BwEvent *)&payload), 0);
    BW_CHECK_EQ("the field takes no subscript",
                bw_mod_rule_bind(kind, T_DIR "/natdex_sub.rule", T_NS, T_LABEL), -1);
    remove(T_DIR "/natdex.rule");
    remove(T_DIR "/natdex_sub.rule");

    printf("the reset\n");
    bw_mod_rule_reset();
    BW_CHECK_EQ("no rule is bound", (long)bw_mod_rule_count(kind), 0);

    clean_rules();
    BW_REPORT("test_mod_rule");
}
