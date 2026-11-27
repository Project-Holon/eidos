/*
 * tests/test_mod_strings.c -- a mod's own words per language
 * (sdk/mod_strings.c, docs/mod-strings.md): the strings files and their
 * lookup, the values in a text, the manifest's `name.<lang>` and
 * `description.<lang>`, a rule's `@key`, and the services table's
 * string_get and string_language.
 *
 * There is no cartridge here, so the game is English: strings.en.txt is
 * the game's language's file and strings.txt the plain one.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mod_api.h"
#include "mod_manifest.h"
#include "mod_message.h"
#include "mod_rule.h"
#include "mod_strings.h"
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
#define BW_RMDIR(p) rmdir(p)
#endif

/* The saved-data layer's namespace: the one the services answer for. */
void bw_mod_savedata_set_namespace(const char *ns);

/* The manifest layer's calls (sdk/mod_manifest.c; its header carries only
 * the record). */
void       *bw_mod_manifest_new(const char *dir_name);
void        bw_mod_manifest_delete(void *manifest);
int         bw_mod_manifest_parse(void *manifest, const char *text, const char *dir_name);
const char *bw_mod_manifest_name(const void *manifest);
const char *bw_mod_manifest_title(const void *manifest);
const char *bw_mod_manifest_about(const void *manifest, int what);

#define T_DIR "mod_strings_tmp"

static int write_file(const char *path, const char *text)
{
    FILE *f = fopen(path, "wb");

    if (f == NULL) {
        return -1;
    }
    fputs(text, f);
    return fclose(f) == 0 ? 0 : -1;
}

/* The queued message's text equals `want` (as the game's code units). */
static int message_is(const char *want)
{
    BwModMessage m;
    u16 units[BW_MOD_MESSAGE_MAX_UNITS];
    u32 n;

    if (bw_mod_message_queue_take(&m) != 0) {
        return 0;
    }
    n = bw_mod_message_encode(want, units, BW_MOD_MESSAGE_MAX_UNITS);
    return n == m.count && memcmp(units, m.units, n * sizeof units[0]) == 0;
}

static const char *const g_files[] = {
    T_DIR "/strings.txt", T_DIR "/strings.en.txt", T_DIR "/eidos_mod.txt",
    T_DIR "/hello.rule", T_DIR "/plain.rule", T_DIR "/missing.rule", T_DIR "/many.rule"
};

static void clean(void)
{
    size_t i;

    for (i = 0; i < sizeof g_files / sizeof g_files[0]; i++) {
        remove(g_files[i]);
    }
    (void)BW_RMDIR(T_DIR);
}

int main(void)
{
    const char *before;
    char buf[64];
    char small[8];

    clean();
    BW_REQUIRE("the folder", BW_MKDIR(T_DIR) == 0);
    BW_REQUIRE("the plain words",
               write_file(T_DIR "/strings.txt",
                          "\xEF\xBB\xBF# a comment line\n"
                          "a.one = One\r\n"
                          "a.two=Two   \n"
                          "a.both = Plain both\n"
                          "a.empty = Plain empty\n"
                          "a.lines = first\\nsecond \\\\ # not a comment\n"
                          "a.twice = first\n"
                          "a.twice = second\n"
                          "a.fill = {2} then {1}, {{1}, {3}.\n"
                          "a.utf = \xC3\xA9t\xC3\xA9 \xC3\xA0 Paris\n"
                          "bad key = left out\n"
                          "no equals sign here\n") == 0);
    BW_REQUIRE("the English words",
               write_file(T_DIR "/strings.en.txt",
                          "a.both = English both\n"
                          "a.empty =\n") == 0);

    printf("the language and the lookup\n");
    BW_CHECK_EQ("no cartridge: English", strcmp(bw_mod_lang_code(), "en"), 0);
    BW_CHECK_EQ("a plain line", strcmp(bw_mod_strings_get(T_DIR, "a.one"), "One"), 0);
    BW_CHECK_EQ("no blanks around it", strcmp(bw_mod_strings_get(T_DIR, "a.two"), "Two"), 0);
    BW_CHECK_EQ("the language's line first", strcmp(bw_mod_strings_get(T_DIR, "a.both"), "English both"), 0);
    BW_CHECK_EQ("an empty line is as none", strcmp(bw_mod_strings_get(T_DIR, "a.empty"), "Plain empty"), 0);
    BW_CHECK_EQ("the escapes, '#' kept",
                strcmp(bw_mod_strings_get(T_DIR, "a.lines"), "first\nsecond \\ # not a comment"), 0);
    BW_CHECK_EQ("a key given twice: the first", strcmp(bw_mod_strings_get(T_DIR, "a.twice"), "first"), 0);
    BW_CHECK_EQ("a key no file has", bw_mod_strings_get(T_DIR, "a.none") == NULL, 1);
    BW_CHECK_EQ("a bad key is left out", bw_mod_strings_get(T_DIR, "bad key") == NULL, 1);
    BW_CHECK_EQ("a folder with no files", bw_mod_strings_get(T_DIR "/nothing", "a.one") == NULL, 1);
    BW_CHECK_EQ("@key is the line", strcmp(bw_mod_strings_text(T_DIR, "@a.one"), "One"), 0);
    BW_CHECK_EQ("@key no file has: the key", strcmp(bw_mod_strings_text(T_DIR, "@a.none"), "a.none"), 0);
    BW_CHECK_EQ("a plain text is itself", strcmp(bw_mod_strings_text(T_DIR, "Lives"), "Lives"), 0);
    BW_CHECK_EQ("'@' with no key is itself", strcmp(bw_mod_strings_text(T_DIR, "@ x"), "@ x"), 0);
    BW_CHECK_EQ("keys", bw_mod_strings_key_valid("a.b_c-9") && !bw_mod_strings_key_valid("a b") &&
                        !bw_mod_strings_key_valid(""), 1);

    printf("the values in a text\n");
    {
        const char *v[2] = { "A", "B" };

        bw_mod_strings_fill(bw_mod_strings_get(T_DIR, "a.fill"), v, 2, buf, sizeof buf);
        BW_CHECK_EQ("in the text's order, {{ and a place with no value", strcmp(buf, "B then A, {1}, ."), 0);
        bw_mod_strings_fill(bw_mod_strings_get(T_DIR, "a.utf"), NULL, 0, small, 4);
        BW_CHECK_EQ("cut before a character that does not fit", strcmp(small, "\xC3\xA9t"), 0);
        bw_mod_strings_fill(bw_mod_strings_get(T_DIR, "a.utf"), NULL, 0, small, 3);
        BW_CHECK_EQ("never in its middle", strcmp(small, "\xC3\xA9"), 0);
    }

    printf("the services table\n");
    bw_mod_strings_bind("strings-test", T_DIR);
    bw_mod_savedata_set_namespace("strings-test");
    BW_CHECK_EQ("string_get: the length", (long)bw_mod_strings_service_get("a.both", buf, sizeof buf), 12);
    BW_CHECK_EQ("and the line", strcmp(buf, "English both"), 0);
    BW_CHECK_EQ("cut to the size, the whole length answered",
                (long)bw_mod_strings_service_get("a.both", small, sizeof small), 12);
    BW_CHECK_EQ("cut", strcmp(small, "English"), 0);
    BW_CHECK_EQ("a key no file has", (long)bw_mod_strings_service_get("a.none", buf, sizeof buf), -1);
    BW_CHECK_EQ("string_language", (long)bw_mod_strings_service_language(buf, sizeof buf), 2);
    BW_CHECK_EQ("en", strcmp(buf, "en"), 0);
    BW_CHECK_EQ("too small for the code", (long)bw_mod_strings_service_language(buf, 2), -1);
    bw_mod_savedata_set_namespace("another-mod");
    BW_CHECK_EQ("another mod's words are not the caller's",
                (long)bw_mod_strings_service_get("a.both", buf, sizeof buf), -1);
    bw_mod_savedata_set_namespace(NULL);

    printf("the manifest\n");
    {
        void *m = bw_mod_manifest_new("dir");

        BW_REQUIRE("a record", m != NULL);
        BW_REQUIRE("the parse",
                   bw_mod_manifest_parse(m, "name = Words\nname.fr = Mots\nname.en = Words in English\n"
                                            "description = Plain.\ndescription.de = Deutsch.\n", "dir") == 0);
        BW_CHECK_EQ("the name stays", strcmp(bw_mod_manifest_name(m), "Words"), 0);
        BW_CHECK_EQ("the name shown: the game's language's",
                    strcmp(bw_mod_manifest_title(m), "Words in English"), 0);
        BW_CHECK_EQ("another language's description is not this game's",
                    strcmp(bw_mod_manifest_about(m, 0), "Plain."), 0);
        bw_mod_manifest_delete(m);
    }

    printf("a rule's @key\n");
    {
        u32 kind = BW_EVENT_GAME_NEW;
        BwGameNew payload;

        BW_REQUIRE("the rules",
                   write_file(T_DIR "/hello.rule",
                              "language 1\nrule hello on game.new\n"
                              "    do message @a.fill(\"x\", 7 * 6)\n") == 0 &&
                   write_file(T_DIR "/plain.rule",
                              "language 1\nrule plain on game.new\n    do message @a.both\n") == 0 &&
                   write_file(T_DIR "/missing.rule",
                              "language 1\nrule missing on game.new\n    do message @a.none\n") == 0 &&
                   write_file(T_DIR "/many.rule",
                              "language 1\nrule many on game.new\n"
                              "    do message @a.one(1, 2, 3, 4, 5, 6, 7, 8, 9, 10)\n") == 0);
        bw_mod_message_reset();
        BW_CHECK_EQ("@key with values loads",
                    (long)bw_mod_rule_bind_manifest(T_DIR, "dir", "game.new", "hello.rule", "strings-test",
                                                    "test"), 0);
        BW_CHECK_EQ("@key loads",
                    (long)bw_mod_rule_bind_manifest(T_DIR, "dir", "game.new", "plain.rule", "strings-test",
                                                    "test"), 0);
        BW_CHECK_EQ("a key no strings file has is refused",
                    (long)bw_mod_rule_bind_manifest(T_DIR, "dir", "game.new", "missing.rule", "strings-test",
                                                    "test"), -1);
        BW_CHECK_EQ("ten values are refused",
                    (long)bw_mod_rule_bind_manifest(T_DIR, "dir", "game.new", "many.rule", "strings-test",
                                                    "test"), -1);
        memset(&payload, 0, sizeof payload);
        payload.h.size = sizeof payload;
        payload.h.kind = kind;
        bw_mod_rule_run(kind, (BwEvent *)&payload);
        BW_CHECK_EQ("two messages", (long)bw_mod_message_queue_count(), 2);
        BW_CHECK_EQ("the values in their places", message_is("42 then x, {1}, ."), 1);
        BW_CHECK_EQ("the language's line", message_is("English both"), 1);
        bw_mod_rule_reset();
        bw_mod_message_reset();
    }

    printf("reload\n");
    before = bw_mod_strings_get(T_DIR, "a.one");
    BW_REQUIRE("the plain words change", write_file(T_DIR "/strings.txt", "a.one = Uno\n") == 0);
    BW_CHECK_EQ("read once: the old line", strcmp(bw_mod_strings_get(T_DIR, "a.one"), "One"), 0);
    bw_mod_strings_reload();
    BW_CHECK_EQ("after a reload: the new line", strcmp(bw_mod_strings_get(T_DIR, "a.one"), "Uno"), 0);
    BW_CHECK_EQ("a line given out before stays valid", strcmp(before, "One"), 0);

    clean();
    BW_REPORT("test_mod_strings");
}
