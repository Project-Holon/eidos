/*
 * tests/test_mod_manifest.c -- the run-time manifest: the keys, the
 * namespace, the declared check and the override grammar.
 *
 * The layer is sdk/mod_manifest.c; the contract it keeps is the one of
 * docs/mod-state.md, section 3, and of docs/mod-interface.md, section 3:
 *
 *   - the parse: `#` starts a comment, each line is `key = value`, a line
 *     with no `=` is skipped and an unknown key is ignored on purpose, so a
 *     later version may add keys;
 *   - the keys: name, order, api, port, code, state, override, rule and
 *     settings, each carried as the record the loader reads;
 *   - the namespace: the `state` key is validated as one path component, and
 *     a namespace that is not one is reported and the default (derived from
 *     the name) stands;
 *   - the declared check: an override whose (region, address) pair is not in
 *     the declared table is reported and skipped, and the mod still loads;
 *   - the override grammar: `<region>:<address> <mode> <symbol>
 *     [sig="<prototype>"] [switch=<name>]`; a bare address, a mode that is
 *     neither replace nor wrap and a symbol that is not an identifier are
 *     all refused;
 *   - the load order: the mods load in ascending `order`, and the apply
 *     takes the manifests in that order.
 *
 * Nothing here needs game data: the manifests are text, the tree the load
 * order test writes is a directory in the test's own working directory, and
 * the declared table is a small one the test registers itself.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mod_api.h"
#include "mod_manifest.h"
#include "mod_slot.h"
#include "vfs.h"

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

/*
 * The manifest layer's own calls (sdk/mod_manifest.c).  The record's types
 * are in sdk/mod_manifest.h; the calls themselves are in the .c until the
 * header carries them too, so the test declares the ones it uses and reads
 * the record through the same accessors the loader reads it through, so the
 * record's shape stays the layer's own.
 */
BwModManifest *bw_mod_manifest_new(const char *dir_name);
void           bw_mod_manifest_delete(BwModManifest *m);
int            bw_mod_manifest_parse(BwModManifest *m, const char *text,
                                     const char *dir_name);
int            bw_mod_manifest_parse_dir(BwModManifest *m, const char *root,
                                         const char *dir_name);
int            bw_mod_manifest_apply(BwModManifest *m);
int            bw_mod_manifest_namespace_valid(const char *ns);
char          *bw_mod_manifest_default_namespace(const char *name);
void           bw_mod_manifest_set_port_version(const char *version);
u32            bw_mod_manifest_override_count(const BwModManifest *m);
u32            bw_mod_manifest_rule_count(const BwModManifest *m);
const char    *bw_mod_manifest_name(const BwModManifest *m);
const char    *bw_mod_manifest_code(const BwModManifest *m);
const char    *bw_mod_manifest_state(const BwModManifest *m);
const char    *bw_mod_manifest_settings(const BwModManifest *m);
const char    *bw_mod_manifest_ui_text(const BwModManifest *m);
u32            bw_mod_manifest_applied_count(void);
const BwModManifest *bw_mod_manifest_applied_at(u32 i);
void           bw_mod_manifest_applied_clear(void);

/*
 * The declared set at run time (docs/mod-state.md, section 4) is
 * sdk/mod_slot.h's: the build emits the table beside the slot publications
 * and sdk/mod_slot.c keeps it; the test registers a small one of its own.
 */

/* ------------------------------------------------------------------ */
/* The tree the load order test writes                                 */
/* ------------------------------------------------------------------ */

#define T_ROOT "mod_manifest_tmp"
#define T_MOD_A T_ROOT "/a"
#define T_MOD_B T_ROOT "/b"
#define T_MOD_C T_ROOT "/c"

/* One mod's tree: the folder and its manifest. */
static int make_mod(const char *dir, const char *manifest)
{
    char path[256];
    FILE *fp;
    int n;

    n = snprintf(path, sizeof path, "%s/eidos_mod.txt", dir);
    if (n < 0 || (size_t)n >= sizeof path) {
        return -1;
    }
    /* What an earlier run left goes first: the folder is only empty once
     * its manifest is out of the way. */
    remove(path);
    BW_RMDIR(dir);
    if (BW_MKDIR(dir) != 0) {
        return -1;
    }
    fp = fopen(path, "wb");
    if (fp == NULL) {
        return -1;
    }
    fputs(manifest, fp);
    fclose(fp);
    return 0;
}

/* Take the tree the test built away. */
static void clean_tree(void)
{
    static const char *const dirs[] = { T_MOD_A, T_MOD_B, T_MOD_C };
    char path[256];
    size_t i;

    for (i = 0; i < sizeof dirs / sizeof dirs[0]; i++) {
        snprintf(path, sizeof path, "%s/eidos_mod.txt", dirs[i]);
        remove(path);
        BW_RMDIR(dirs[i]);
    }
    BW_RMDIR(T_ROOT);
}

/* ------------------------------------------------------------------ */
/* The test                                                            */
/* ------------------------------------------------------------------ */

int main(void)
{
    BwModManifest *m;
    BwModManifest *m2;
    BwModDeclared declared[2];
    char text[512];
    char *ns;

    m = bw_mod_manifest_new("the-mod");
    BW_REQUIRE("the record is made", m != NULL);
    m2 = NULL;

    printf("the parse of the keys\n");
    BW_REQUIRE("the manifest parses",
               bw_mod_manifest_parse(m,
                   "# a comment\n"
                   "name = Nuzlocke Soul Link\n"
                   "order = 20\n"
                   "api = 1\n"
                   "port = 1.0\n"
                   "code = nuzlocke_soullink.dll\n"
                   "state = nuzlocke-soullink\n"
                   "settings = nuzlocke.ini\n"
                   "ui_text = words.tsv\n"
                   "a line with no equals sign\n"
                   "unknown_key = whatever\n"
                   "override = ov093:0x021CBA40 wrap sl_catch\n",
                   "the-mod") == 0);
    BW_CHECK_EQ("the name is the manifest's",
                strcmp(bw_mod_manifest_name(m), "Nuzlocke Soul Link"), 0);
    BW_CHECK_EQ("the code path is the manifest's",
                strcmp(bw_mod_manifest_code(m), "nuzlocke_soullink.dll"), 0);
    BW_CHECK_EQ("the state namespace is the manifest's",
                strcmp(bw_mod_manifest_state(m), "nuzlocke-soullink"), 0);
    BW_CHECK_EQ("the settings path is the manifest's",
                strcmp(bw_mod_manifest_settings(m), "nuzlocke.ini"), 0);
    BW_CHECK_EQ("the ui_text path is the manifest's",
                strcmp(bw_mod_manifest_ui_text(m), "words.tsv"), 0);
    BW_CHECK_EQ("a line with no equals sign and an unknown key are skipped",
                (long)bw_mod_manifest_override_count(m), 1);

    printf("the override grammar\n");
    bw_mod_manifest_delete(m);
    m = bw_mod_manifest_new("the-mod");
    BW_REQUIRE("the manifest parses",
               bw_mod_manifest_parse(m,
                   "override = ov093:0x021CBA40 wrap sl_catch_rate\n"
                   "rule = battle.faint rules/faint_penalty.rule\n",
                   "the-mod") == 0);
    BW_CHECK_EQ("one override is carried",
                (long)bw_mod_manifest_override_count(m), 1);
    BW_CHECK_EQ("and one rule", (long)bw_mod_manifest_rule_count(m), 1);

    bw_mod_manifest_delete(m);
    m = bw_mod_manifest_new("the-mod");
    BW_REQUIRE("the manifest parses",
               bw_mod_manifest_parse(m,
                   "override = ov093:0x021CBA40 replace sl_catch\n"
                   "override = ov093:0x021CBA40 wrap sl_catch sig=\"int f(int)\" "
                   "switch=SL_CATCH\n",
                   "the-mod") == 0);
    BW_CHECK_EQ("two overrides are carried",
                (long)bw_mod_manifest_override_count(m), 2);

    bw_mod_manifest_delete(m);
    m = bw_mod_manifest_new("the-mod");
    BW_REQUIRE("the manifest parses",
               bw_mod_manifest_parse(m,
                   "override = 0x021CBA40 wrap sl_catch\n",
                   "the-mod") == 0);
    BW_CHECK_EQ("a bare address is refused",
                (long)bw_mod_manifest_override_count(m), 0);

    bw_mod_manifest_delete(m);
    m = bw_mod_manifest_new("the-mod");
    BW_REQUIRE("the manifest parses",
               bw_mod_manifest_parse(m,
                   "override = ov093:0x021CBA40 sideways sl_catch\n",
                   "the-mod") == 0);
    BW_CHECK_EQ("a mode that is neither replace nor wrap is refused",
                (long)bw_mod_manifest_override_count(m), 0);

    bw_mod_manifest_delete(m);
    m = bw_mod_manifest_new("the-mod");
    BW_REQUIRE("the manifest parses",
               bw_mod_manifest_parse(m,
                   "override = ov093:0x021CBA40 wrap 9bad\n",
                   "the-mod") == 0);
    BW_CHECK_EQ("a symbol that is not an identifier is refused",
                (long)bw_mod_manifest_override_count(m), 0);

    bw_mod_manifest_delete(m);
    m = bw_mod_manifest_new("the-mod");
    BW_REQUIRE("the manifest parses",
               bw_mod_manifest_parse(m,
                   "rule = battle.faint\n",
                   "the-mod") == 0);
    BW_CHECK_EQ("a rule without a path is refused",
                (long)bw_mod_manifest_rule_count(m), 0);

    printf("the namespace\n");
    BW_CHECK_EQ("a plain name is a namespace",
                (long)bw_mod_manifest_namespace_valid("nuzlocke-soullink"), 1);
    BW_CHECK_EQ("an empty name is not",
                (long)bw_mod_manifest_namespace_valid(""), 0);
    BW_CHECK_EQ("a name with a separator is not",
                (long)bw_mod_manifest_namespace_valid("bad/name"), 0);
    BW_CHECK_EQ("a name with a backslash is not",
                (long)bw_mod_manifest_namespace_valid("bad\\name"), 0);
    BW_CHECK_EQ("the dot component is not",
                (long)bw_mod_manifest_namespace_valid("."), 0);
    BW_CHECK_EQ("the dot dot component is not",
                (long)bw_mod_manifest_namespace_valid(".."), 0);
    BW_CHECK_EQ("a reserved device name is not",
                (long)bw_mod_manifest_namespace_valid("CON"), 0);
    BW_CHECK_EQ("a name with a space is not",
                (long)bw_mod_manifest_namespace_valid("two words"), 0);

    ns = bw_mod_manifest_default_namespace("Nuzlocke Soul Link");
    BW_REQUIRE("the default namespace exists", ns != NULL);
    BW_CHECK_EQ("it is the name, lower case, with dashes",
                strcmp(ns, "nuzlocke-soullink"), 0);
    free(ns);
    ns = bw_mod_manifest_default_namespace("");
    BW_REQUIRE("the default namespace exists", ns != NULL);
    BW_CHECK_EQ("a name with nothing in it still gets one",
                strcmp(ns, "mod"), 0);
    free(ns);

    printf("a bad namespace is reported and the default stands\n");
    bw_mod_manifest_delete(m);
    m = bw_mod_manifest_new("the-mod");
    BW_REQUIRE("the manifest parses",
               bw_mod_manifest_parse(m,
                   "name = The Mod\n"
                   "state = bad/name\n",
                   "the-mod") == 0);
    BW_REQUIRE("the mod is not refused", bw_mod_manifest_apply(m) == 0);
    BW_CHECK_EQ("the default namespace stands",
                strcmp(bw_mod_manifest_state(m), "the-mod"), 0);

    printf("the declared check\n");
    declared[0].region = BW_MOD_REGION_OVL(93);
    declared[0].addr = 0x021CBA40u;
    declared[1].region = BW_MOD_REGION_ARM9;
    declared[1].addr = 0x02007150u;
    bw_mod_declare_table(declared, 2);

    bw_mod_manifest_delete(m);
    m = bw_mod_manifest_new("the-mod");
    BW_REQUIRE("the manifest parses",
               bw_mod_manifest_parse(m,
                   "override = ov093:0x021CBA40 wrap sl_catch\n"
                   "override = ov093:0x021CBD48 wrap sl_other\n",
                   "the-mod") == 0);
    BW_REQUIRE("the mod is not refused", bw_mod_manifest_apply(m) == 0);
    BW_CHECK_EQ("the declared pair is kept and the other one skipped",
                (long)bw_mod_manifest_override_count(m), 1);

    bw_mod_manifest_delete(m);
    m = bw_mod_manifest_new("the-mod");
    BW_REQUIRE("the manifest parses",
               bw_mod_manifest_parse(m,
                   "override = ov093:0x021CBA41 wrap sl_catch\n",
                   "the-mod") == 0);
    BW_REQUIRE("the mod is not refused", bw_mod_manifest_apply(m) == 0);
    BW_CHECK_EQ("the Thumb-tagged form is the same function",
                (long)bw_mod_manifest_override_count(m), 1);

    bw_mod_manifest_delete(m);
    m = bw_mod_manifest_new("the-mod");
    BW_REQUIRE("the manifest parses",
               bw_mod_manifest_parse(m,
                   "override = ov093:0x021CBD48 wrap sl_other\n",
                   "the-mod") == 0);
    BW_REQUIRE("the mod still loads", bw_mod_manifest_apply(m) == 0);
    BW_CHECK_EQ("and the undeclared pair is skipped",
                (long)bw_mod_manifest_override_count(m), 0);
    BW_CHECK_EQ("and the mod is in the applied list",
                (long)bw_mod_manifest_applied_count() > 0, 1);

    printf("the api of the manifest\n");
    bw_mod_manifest_delete(m);
    m = bw_mod_manifest_new("the-mod");
    snprintf(text, sizeof text, "api = %d\n", (int)BW_MOD_API_VERSION);
    BW_REQUIRE("the manifest parses",
               bw_mod_manifest_parse(m, text, "the-mod") == 0);
    BW_REQUIRE("the mod is not refused", bw_mod_manifest_apply(m) == 0);

    bw_mod_manifest_delete(m);
    m = bw_mod_manifest_new("the-mod");
    snprintf(text, sizeof text, "api = %d\n", (int)BW_MOD_API_VERSION + 1);
    BW_REQUIRE("the manifest parses",
               bw_mod_manifest_parse(m, text, "the-mod") == 0);
    BW_CHECK_EQ("a mod built against another api is refused",
                (long)bw_mod_manifest_apply(m), -1);

    printf("the port version a mod with raw hooks is pinned to\n");
    bw_mod_manifest_set_port_version("1.0");
    bw_mod_manifest_delete(m);
    m = bw_mod_manifest_new("the-mod");
    BW_REQUIRE("the manifest parses",
               bw_mod_manifest_parse(m,
                   "port = 2.0\n"
                   "override = ov093:0x021CBA40 wrap sl_catch\n",
                   "the-mod") == 0);
    BW_CHECK_EQ("a mod pinned to another port version is refused",
                (long)bw_mod_manifest_apply(m), -1);
    bw_mod_manifest_set_port_version(NULL);

    printf("the order the mods load in\n");
    BW_MKDIR(T_ROOT);
    BW_REQUIRE("the first mod's tree is written",
               make_mod(T_MOD_A,
                        "name = A\n"
                        "order = 20\n") == 0);
    BW_REQUIRE("the second mod's tree is written",
               make_mod(T_MOD_B,
                        "name = B\n"
                        "order = 10\n") == 0);
    BW_REQUIRE("the third mod's tree is written",
               make_mod(T_MOD_C,
                        "name = C\n") == 0);
    bw_clear_mods();
    BW_REQUIRE("the first mod is added", bw_add_mod_root(T_MOD_A) == 0);
    BW_REQUIRE("the second mod is added", bw_add_mod_root(T_MOD_B) == 0);
    BW_REQUIRE("the third mod is added", bw_add_mod_root(T_MOD_C) == 0);
    BW_CHECK_EQ("three mods are in the list", (long)bw_mod_count(), 3);
    BW_CHECK_EQ("the mod with no order loads first",
                strcmp(bw_mod_root_at(0), T_MOD_C), 0);
    BW_CHECK_EQ("then the mod with the lowest order",
                strcmp(bw_mod_root_at(1), T_MOD_B), 0);
    BW_CHECK_EQ("and the mod with the highest order last",
                strcmp(bw_mod_root_at(2), T_MOD_A), 0);
    BW_CHECK_EQ("the first mod's order is the default",
                (long)bw_mod_order_at(0), 0);
    BW_CHECK_EQ("the second mod's order is the manifest's",
                (long)bw_mod_order_at(1), 10);
    BW_CHECK_EQ("the last mod's order is the manifest's",
                (long)bw_mod_order_at(2), 20);

    printf("the manifests the apply has taken\n");
    bw_mod_manifest_applied_clear();
    m2 = bw_mod_manifest_new("b");
    BW_REQUIRE("the first manifest is parsed",
               bw_mod_manifest_parse_dir(m2, T_MOD_B, NULL) == 0);
    BW_CHECK_EQ("the name is the manifest's",
                strcmp(bw_mod_manifest_name(m2), "B"), 0);
    BW_REQUIRE("the first manifest is applied", bw_mod_manifest_apply(m2) == 0);
    bw_mod_manifest_delete(m);
    m = bw_mod_manifest_new("a");
    BW_REQUIRE("the second manifest is parsed",
               bw_mod_manifest_parse_dir(m, T_MOD_A, NULL) == 0);
    BW_REQUIRE("the second manifest is applied", bw_mod_manifest_apply(m) == 0);
    BW_CHECK_EQ("both manifests are in the applied list",
                (long)bw_mod_manifest_applied_count(), 2);
    BW_CHECK_EQ("and the first one is the first one applied",
                strcmp(bw_mod_manifest_name(bw_mod_manifest_applied_at(0)), "B"),
                0);
    BW_CHECK_EQ("and the second one is the second",
                strcmp(bw_mod_manifest_name(bw_mod_manifest_applied_at(1)), "A"),
                0);

    bw_clear_mods();
    bw_mod_manifest_delete(m);
    bw_mod_manifest_delete(m2);
    clean_tree();
    BW_REPORT("test_mod_manifest");
}
