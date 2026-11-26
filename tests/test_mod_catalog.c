/*
 * tests/test_mod_catalog.c -- the mods index's rules (sdk/mod_catalog.c),
 * SHA-256 (sdk/sha256.c) and the folder removal (sdk/plat_dir.c), with no
 * network.
 *
 * What is checked:
 *
 *   - SHA-256 against the standard's own examples, in one piece and in
 *     pieces of every size;
 *   - the index: its entries, their ids and the three forms of a release
 *     address; an entry of a wrong shape is left out; another format is
 *     refused with a line that says to update;
 *   - a mod.json: the fields the game keeps, and each rule the launcher's
 *     check has (another mod's id, repo and id that disagree, a version
 *     that is not x.y.z, no description, a game that is neither, a package
 *     that is not mod.bw, a hash that is not one);
 *   - which mods fit a game: the other game's, a newer interface, code
 *     where none runs;
 *   - a folder's name from a mod's name;
 *   - a folder removed with what it holds, and the record a mod's folder
 *     carries read back.
 */

#include <stdio.h>
#include <string.h>

#include "mod_catalog.h"
#include "plat_dir.h"
#include "plat_save.h"
#include "sha256.h"
#include "types.h"
#include "vfs.h"

#include "port_test.h"

#define T_ROOT "mod_catalog_test_tmp"

static void hash_of(const char *text, size_t size, size_t piece, char hex[65])
{
    BwSha256 s;
    u8 out[32];
    size_t at;

    bw_sha256_start(&s);
    for (at = 0; at < size; at += piece) {
        bw_sha256_add(&s, text + at, size - at < piece ? size - at : piece);
    }
    bw_sha256_end(&s, out);
    bw_sha256_hex(out, hex);
}

static void test_sha256(void)
{
    static const char two[] = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    static char million[1000];
    char hex[65];
    BwSha256 s;
    u8 out[32];
    size_t piece;
    int i;

    hash_of("", 0, 1, hex);
    BW_CHECK_EQ("sha256 of nothing",
                strcmp(hex, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"), 0);
    hash_of("abc", 3, 3, hex);
    BW_CHECK_EQ("sha256 of abc",
                strcmp(hex, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"), 0);
    for (piece = 1; piece <= 70; piece++) {
        hash_of(two, 56, piece, hex);
        if (strcmp(hex, "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1") != 0) {
            break;
        }
    }
    BW_CHECK_EQ("sha256 of two blocks, in pieces of every size", (long)piece, 71);
    memset(million, 'a', sizeof(million));
    bw_sha256_start(&s);
    for (i = 0; i < 1000; i++) {
        bw_sha256_add(&s, million, sizeof(million));
    }
    bw_sha256_end(&s, out);
    bw_sha256_hex(out, hex);
    BW_CHECK_EQ("sha256 of a million a",
                strcmp(hex, "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0"), 0);
}

static void test_index(void)
{
    static const char index[] =
        "{ \"format\": 1, \"mods\": [\n"
        "  { \"mod\": \"nuzlocke\", \"version\": \"1.1.0\" },\n"
        "  { \"repo\": \"Project-Holon/arkhipelagos\", \"unofficial\": true },\n"
        "  { \"repo\": \"Some.One/Their_Mod\", \"version\": \"v2.0.10\", \"official\": true },\n"
        "  { \"mod\": \"no-version\" },\n"
        "  { \"mod\": \"Bad_Id\", \"version\": \"1.0.0\" },\n"
        "  { \"repo\": \"not-a-repo\" },\n"
        "  { \"repo\": \"a/b/c\" },\n"
        "  { \"repo\": \"a/..\" },\n"
        "  { \"mod\": \"both\", \"repo\": \"a/b\", \"version\": \"1.0.0\" },\n"
        "  { \"repo\": \"a/b\", \"version\": \"1.0\" },\n"
        "  { },\n"
        "  7\n"
        "] }";
    BwCatalogEntry e[8];
    u32 n = 0, bad = 0;
    char err[200], text[300];

    BW_CHECK_EQ("index reads", bw_mod_catalog_parse_index(index, strlen(index), e, 8, &n, &bad, err, sizeof(err)), 0);
    BW_CHECK_EQ("index entries", (long)n, 3);
    BW_CHECK_EQ("index entries left out", (long)bad, 9);
    if (n == 3) {
        bw_mod_catalog_entry_id(&e[0], text, sizeof(text));
        BW_CHECK_EQ("official id", strcmp(text, "katalogos/nuzlocke"), 0);
        BW_CHECK_EQ("official address", bw_mod_catalog_entry_url(&e[0], "https://github.com", "mod.json", text, sizeof(text)), 0);
        BW_CHECK_EQ("official address text",
                    strcmp(text, "https://github.com/Project-Holon/katalogos/releases/download/nuzlocke-v1.1.0/mod.json"), 0);
        bw_mod_catalog_entry_id(&e[1], text, sizeof(text));
        BW_CHECK_EQ("repository id, lower case", strcmp(text, "project-holon/arkhipelagos"), 0);
        (void)bw_mod_catalog_entry_url(&e[1], "https://github.com", "mod.bw", text, sizeof(text));
        BW_CHECK_EQ("newest release address",
                    strcmp(text, "https://github.com/Project-Holon/arkhipelagos/releases/latest/download/mod.bw"), 0);
        (void)bw_mod_catalog_entry_url(&e[2], "https://localhost:8443", "mod.json", text, sizeof(text));
        BW_CHECK_EQ("pinned release address",
                    strcmp(text, "https://localhost:8443/Some.One/Their_Mod/releases/download/v2.0.10/mod.json"), 0);
        BW_CHECK_EQ("address that does not fit", bw_mod_catalog_entry_url(&e[2], "https://h", "mod.json", text, 20), -1);
        BW_CHECK_EQ("official, not marked", e[0].unofficial, 0);
        BW_CHECK_EQ("unofficial only on an index mod", e[1].unofficial, 0);
        BW_CHECK_EQ("repository not marked official", e[1].marked_official, 0);
        BW_CHECK_EQ("repository marked official", e[2].marked_official, 1);
    }
    {
        static const char marked[] =
            "{ \"format\": 1, \"mods\": [ { \"mod\": \"some-mod\", \"version\": \"3.1.1\", \"unofficial\": true, "
            "\"official\": true } ] }";

        BW_CHECK_EQ("marked index reads", bw_mod_catalog_parse_index(marked, strlen(marked), e, 8, &n, &bad, err,
                                                                     sizeof(err)), 0);
        BW_CHECK_EQ("marked entry", (long)n, 1);
        BW_CHECK_EQ("marked unofficial", n == 1 ? e[0].unofficial : -1, 1);
        BW_CHECK_EQ("official only on a repository", n == 1 ? e[0].marked_official : -1, 0);
    }
    BW_CHECK_EQ("index format 2", bw_mod_catalog_parse_index("{\"format\":2,\"mods\":[]}", 22, e, 8, &n, &bad,
                                                             err, sizeof(err)), -1);
    BW_CHECK_EQ("index format 2: says to update", strstr(err, "Update eidos") != NULL, 1);
    BW_CHECK_EQ("index not JSON", bw_mod_catalog_parse_index("<html>", 6, e, 8, &n, &bad, err, sizeof(err)), -1);
    BW_CHECK_EQ("index without mods", bw_mod_catalog_parse_index("{\"format\":1}", 12, e, 8, &n, &bad,
                                                                 err, sizeof(err)), -1);
    BW_CHECK_EQ("index past the room", bw_mod_catalog_parse_index(index, strlen(index), e, 2, &n, &bad,
                                                                  err, sizeof(err)) == 0 && n == 2, 1);
}

#define SHA "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"

/* A mod.json with one part replaced: `find` by `put` (NULL: as it is). */
static int parse_with(const char *base, const char *find, const char *put, const BwCatalogEntry *e,
                      BwCatalogMod *m, char *err, size_t err_size)
{
    char text[2000];
    const char *at = find != NULL ? strstr(base, find) : NULL;

    if (find != NULL && at == NULL) {
        snprintf(err, err_size, "the test's own text has no %s", find);
        return 99;
    }
    if (at == NULL) {
        snprintf(text, sizeof(text), "%s", base);
    } else {
        snprintf(text, sizeof(text), "%.*s%s%s", (int)(at - base), base, put, at + strlen(find));
    }
    return bw_mod_catalog_parse_mod(text, strlen(text), e, m, err, err_size);
}

static void test_mod_json(void)
{
    static const char official[] =
        "{ \"format\": 1, \"id\": \"katalogos/nuzlocke\", \"name\": \"Nuzlocke\", \"version\": \"1.1.0\",\n"
        "  \"author\": \"Project Holon\", \"description\": \"Fainted Pok\\u00e9mon are released.\",\n"
        "  \"repo\": \"Project-Holon/katalogos\", \"path\": \"mods/nuzlocke\", \"official\": true,\n"
        "  \"multiplayer\": 3, \"icon\": {\"item\": 4}, \"games\": [\"black\", \"white\"],\n"
        "  \"tags\": [\"challenge\"], \"links\": [{\"text\": \"Site\", \"url\": \"https://example.org\"}],\n"
        "  \"api\": 6, \"has_code\": false, \"rules\": 4,\n"
        "  \"package\": { \"file\": \"mod.bw\", \"sha256\": \"" SHA "\", \"size\": 612, \"files\": 2, \"unpacked\": 290 } }";
    static const char other[] =
        "{ \"format\": 1, \"id\": \"some.one/their_mod\", \"name\": \"Their Mod\", \"version\": \"2.0.10\",\n"
        "  \"author\": \"Some.One\", \"description\": \"A mod.\", \"repo\": \"Some.One/Their_Mod\",\n"
        "  \"games\": [\"white\"], \"has_code\": true, \"rules\": 0,\n"
        "  \"package\": { \"file\": \"mod.bw\", \"sha256\": \"" SHA "\", \"size\": 5000000000, \"files\": 900, \"unpacked\": 1 } }";
    BwCatalogEntry eo, er, elatest;
    BwCatalogMod m;
    char err[200];

    memset(&eo, 0, sizeof(eo));
    memset(&er, 0, sizeof(er));
    snprintf(eo.official, sizeof(eo.official), "nuzlocke");
    snprintf(eo.version, sizeof(eo.version), "1.1.0");
    snprintf(er.repo, sizeof(er.repo), "Some.One/Their_Mod");
    snprintf(er.version, sizeof(er.version), "2.0.10");
    elatest = er;
    elatest.version[0] = '\0';

    BW_CHECK_EQ("official mod reads", parse_with(official, NULL, NULL, &eo, &m, err, sizeof(err)), 0);
    BW_CHECK_EQ("official id", strcmp(m.id, "katalogos/nuzlocke"), 0);
    BW_CHECK_EQ("official name", strcmp(m.name, "Nuzlocke"), 0);
    BW_CHECK_EQ("official author", strcmp(m.author, "Project Holon"), 0);
    BW_CHECK_EQ("official description", strcmp(m.about, "Fainted Pok\xC3\xA9mon are released."), 0);
    BW_CHECK_EQ("official version", strcmp(m.version, "1.1.0"), 0);
    BW_CHECK_EQ("official games", m.games, BW_MOD_GAME_BLACK | BW_MOD_GAME_WHITE);
    BW_CHECK_EQ("official api", m.api, 6);
    BW_CHECK_EQ("official has no code", m.has_code, 0);
    BW_CHECK_EQ("official is official", m.official, 1);
    eo.unofficial = 1;                  /* the index marks it */
    BW_CHECK_EQ("unofficial mod reads", parse_with(official, NULL, NULL, &eo, &m, err, sizeof(err)), 0);
    BW_CHECK_EQ("unofficial mod is not official", m.official, 0);
    eo.unofficial = 0;
    (void)parse_with(official, NULL, NULL, &eo, &m, err, sizeof(err));
    BW_CHECK_EQ("official size", (long)m.size, 612);
    BW_CHECK_EQ("official files", (long)m.files, 2);
    BW_CHECK_EQ("official hash", strcmp(m.sha256, SHA), 0);

    BW_CHECK_EQ("another mod's json", parse_with(official, NULL, NULL, &er, &m, err, sizeof(err)), -1);
    BW_CHECK_EQ("format 2", parse_with(official, "\"format\": 1", "\"format\": 2", &eo, &m, err, sizeof(err)), -1);
    BW_CHECK_EQ("official from another repo", parse_with(official, "Project-Holon/katalogos", "Someone/katalogos",
                                                         &eo, &m, err, sizeof(err)), -1);
    BW_CHECK_EQ("official with another path", parse_with(official, "mods/nuzlocke", "mods/other", &eo, &m,
                                                         err, sizeof(err)), -1);
    BW_CHECK_EQ("not the index's version", parse_with(official, "\"version\": \"1.1.0\"", "\"version\": \"1.2.0\"",
                                                      &eo, &m, err, sizeof(err)), -1);
    BW_CHECK_EQ("no description", parse_with(official, "Fainted Pok\\u00e9mon are released.", "  ", &eo, &m,
                                             err, sizeof(err)), -1);
    BW_CHECK_EQ("a game that is neither", parse_with(official, "\"white\"]", "\"black2\"]", &eo, &m,
                                                     err, sizeof(err)), -1);
    BW_CHECK_EQ("no game", parse_with(official, "[\"black\", \"white\"]", "[]", &eo, &m, err, sizeof(err)), -1);
    BW_CHECK_EQ("a package that is not mod.bw", parse_with(official, "\"mod.bw\"", "\"mod.zip\"", &eo, &m,
                                                           err, sizeof(err)), -1);
    BW_CHECK_EQ("a hash that is not one", parse_with(official, SHA, "0123456789abcdef", &eo, &m, err, sizeof(err)), -1);
    BW_CHECK_EQ("a hash with other letters",
                parse_with(official, SHA, "g123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
                           &eo, &m, err, sizeof(err)), -1);
    BW_CHECK_EQ("an upper-case hash is read",
                parse_with(official, SHA, "0123456789ABCDEF0123456789abcdef0123456789abcdef0123456789abcdef",
                           &eo, &m, err, sizeof(err)) == 0 && strcmp(m.sha256, SHA) == 0, 1);
    BW_CHECK_EQ("a size of 0", parse_with(official, "\"size\": 612", "\"size\": 0", &eo, &m, err, sizeof(err)), -1);
    BW_CHECK_EQ("no has_code", parse_with(official, "\"has_code\": false,", "", &eo, &m, err, sizeof(err)), -1);

    BW_CHECK_EQ("past the package limit", parse_with(other, NULL, NULL, &er, &m, err, sizeof(err)), -1);
    BW_CHECK_EQ("other mod reads", parse_with(other, "5000000000", "5000", &er, &m, err, sizeof(err)), 0);
    BW_CHECK_EQ("other id", strcmp(m.id, "some.one/their_mod"), 0);
    BW_CHECK_EQ("other games", m.games, BW_MOD_GAME_WHITE);
    BW_CHECK_EQ("other has code", m.has_code, 1);
    BW_CHECK_EQ("other is not official", m.official, 0);
    BW_CHECK_EQ("other names no interface", m.api, 0);
    er.marked_official = 1;             /* the index marks it */
    BW_CHECK_EQ("marked other reads", parse_with(other, "5000000000", "5000", &er, &m, err, sizeof(err)), 0);
    BW_CHECK_EQ("marked other is official", m.official, 1);
    er.marked_official = 0;
    BW_CHECK_EQ("other, newest release", parse_with(other, "5000000000", "5000", &elatest, &m, err, sizeof(err)), 0);
    BW_CHECK_EQ("other with a path", parse_with(other, "\"games\"", "\"path\": \"mods/x\", \"games\"", &er, &m,
                                                err, sizeof(err)), -1);
    BW_CHECK_EQ("other, id not its repo", parse_with(other, "\"repo\": \"Some.One/Their_Mod\"",
                                                     "\"repo\": \"Some.One/Other\"", &er, &m, err, sizeof(err)), -1);
    BW_CHECK_EQ("version not x.y.z", parse_with(other, "2.0.10", "2.0", &elatest, &m, err, sizeof(err)), -1);

    /* which mods fit a game */
    (void)parse_with(other, "5000000000", "5000", &er, &m, err, sizeof(err));
    BW_CHECK_EQ("a White mod on White", bw_mod_catalog_fits(&m, BW_MOD_GAME_WHITE, 7, 1), 1);
    BW_CHECK_EQ("a White mod on Black", bw_mod_catalog_fits(&m, BW_MOD_GAME_BLACK, 7, 1), 0);
    BW_CHECK_EQ("a White mod, game not known", bw_mod_catalog_fits(&m, 0, 7, 1), 1);
    BW_CHECK_EQ("a code mod where none runs", bw_mod_catalog_fits(&m, BW_MOD_GAME_WHITE, 7, 0), 0);
    (void)parse_with(official, NULL, NULL, &eo, &m, err, sizeof(err));
    BW_CHECK_EQ("interface 6 on 6", bw_mod_catalog_fits(&m, BW_MOD_GAME_BLACK, 6, 0), 1);
    BW_CHECK_EQ("interface 6 on 5", bw_mod_catalog_fits(&m, BW_MOD_GAME_BLACK, 5, 0), 0);
}

static void test_folder_name(void)
{
    char out[80];

    bw_mod_catalog_folder_name("  Nuzlocke  ", out, sizeof(out));
    BW_CHECK_EQ("spaces around", strcmp(out, "Nuzlocke"), 0);
    bw_mod_catalog_folder_name("A/B: C? \"D\" <E>|F*", out, sizeof(out));
    BW_CHECK_EQ("characters Windows refuses", strcmp(out, "A_B_ C_ _D_ _E__F_"), 0);
    bw_mod_catalog_folder_name("..hidden. .", out, sizeof(out));
    BW_CHECK_EQ("dots and spaces at the ends", strcmp(out, "hidden"), 0);
    bw_mod_catalog_folder_name(" . ", out, sizeof(out));
    BW_CHECK_EQ("nothing left", strcmp(out, "mod"), 0);
    bw_mod_catalog_folder_name("", out, sizeof(out));
    BW_CHECK_EQ("empty", strcmp(out, "mod"), 0);
    bw_mod_catalog_folder_name("Pok\xC3\xA9mon\tMod", out, sizeof(out));
    BW_CHECK_EQ("a letter of two bytes, a tab", strcmp(out, "Pok\xC3\xA9mon_Mod"), 0);
    bw_mod_catalog_folder_name("12345678901234567890123456789012345678901234567890123456789\xC3\xA9xyz", out, sizeof(out));
    BW_CHECK_EQ("60 characters, whole ones",
                strcmp(out, "12345678901234567890123456789012345678901234567890123456789\xC3\xA9"), 0);
}

static int write_file(const char *path, const char *text)
{
    FILE *f = fopen(path, "wb");

    if (f == NULL) {
        return -1;
    }
    fputs(text, f);
    return fclose(f);
}

static void test_folders(void)
{
    char id[108], version[24];

    (void)bw_plat_remove_tree(T_ROOT);
    BW_CHECK_EQ("nothing there: nothing to remove", bw_plat_remove_tree(T_ROOT), 0);
    BW_CHECK_EQ("folders made", bw_plat_mkdirs(T_ROOT "/mods/Nuzlocke/a/0/1") == 0 &&
                                bw_plat_mkdirs(T_ROOT "/mods/By Hand") == 0, 1);
    BW_CHECK_EQ("files made",
                write_file(T_ROOT "/mods/Nuzlocke/eidos_mod.txt", "name = Nuzlocke\n") == 0 &&
                write_file(T_ROOT "/mods/Nuzlocke/a/0/1/7", "x") == 0 &&
                write_file(T_ROOT "/mods/Nuzlocke/katalogos.json",
                           "{\n  \"id\": \"katalogos/nuzlocke\",\n  \"version\": \"1.1.0\"\n}") == 0 &&
                write_file(T_ROOT "/mods/By Hand/eidos_mod.txt", "name = By Hand\n") == 0, 1);
    BW_CHECK_EQ("the record reads", bw_mod_catalog_installed_as(T_ROOT "/mods/Nuzlocke", id, sizeof(id),
                                                                version, sizeof(version)), 1);
    BW_CHECK_EQ("the record's id", strcmp(id, "katalogos/nuzlocke"), 0);
    BW_CHECK_EQ("the record's version", strcmp(version, "1.1.0"), 0);
    BW_CHECK_EQ("a mod put there by hand has none", bw_mod_catalog_installed_as(T_ROOT "/mods/By Hand", id, sizeof(id),
                                                                                version, sizeof(version)), 0);
    BW_CHECK_EQ("a folder removed", bw_plat_remove_tree(T_ROOT "/mods/Nuzlocke"), 0);
    BW_CHECK_EQ("it is gone", bw_plat_path_kind(T_ROOT "/mods/Nuzlocke"), BW_PATH_NONE);
    BW_CHECK_EQ("its neighbour stays", bw_plat_path_kind(T_ROOT "/mods/By Hand/eidos_mod.txt"), BW_PATH_FILE);
    BW_CHECK_EQ("a file removed", bw_plat_remove_tree(T_ROOT "/mods/By Hand/eidos_mod.txt"), 0);
    BW_CHECK_EQ("the rest removed", bw_plat_remove_tree(T_ROOT), 0);
    BW_CHECK_EQ("nothing is left", bw_plat_path_kind(T_ROOT), BW_PATH_NONE);
}

int main(void)
{
    test_sha256();
    test_index();
    test_mod_json();
    test_folder_name();
    test_folders();
    BW_REPORT("test_mod_catalog");
}
