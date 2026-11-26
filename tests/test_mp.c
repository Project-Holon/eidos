/*
 * tests/test_mp.c -- multiplayer's lobby, start and pause (sdk/mp.c).
 *
 * The layer is driven the way the network drives it (bw_mp_on_net, the
 * port handler) with a network of the test's own that records what the
 * layer sends, so one process plays the host's game, then a joiner's.
 */

#include <stdio.h>
#include <string.h>
#ifdef _WIN32
#include <direct.h>
#else
#include <unistd.h>
#endif

#include "mod_event.h"
#include "mod_net.h"
#include "mp.h"
#include "save.h"

#include "port_test.h"

/* What the layer sent. */
static char g_sent[16][512];
static u32  g_sent_peer[16];
static int  g_sent_count;
static u32  g_removed;
static int  g_joins;
static int  g_join_fails;

static void sent_reset(void)
{
    g_sent_count = 0;
    g_removed = 0;
}

static const char *g_next_code = "ABC234";
static const char *g_error = "";
static const char *g_peer3 = "Bianca";

static int t_host(char *code, u32 size)
{
    snprintf(code, size, "%s", g_next_code);
    return 0;
}

static const char *t_last_error(void)
{
    return g_error;
}

static int t_join(const char *code)
{
    (void)code;
    g_joins++;
    return g_join_fails ? -1 : 0;
}

static void t_leave(void)
{
}

static int t_send(u32 peer, const void *buf, u32 size)
{
    if (g_sent_count < 16) {
        u32 n = size < 511u ? size : 511u;

        memcpy(g_sent[g_sent_count], buf, n);
        g_sent[g_sent_count][n] = '\0';
        g_sent_peer[g_sent_count] = peer;
        g_sent_count++;
    }
    return 0;
}

static int t_remove(u32 peer)
{
    g_removed = peer;
    return 0;
}

static u32 t_self(void)
{
    return 1;
}

static int t_peer_name(u32 peer, char *out, u32 size)
{
    snprintf(out, size, "%s", peer == 2 ? "Cheren" : peer == 3 ? g_peer3 : "N");
    return 0;
}

static const char *t_own_name(void)
{
    return "Hilda";
}

static const BwMpNet k_test_net = {
    t_host, t_join, t_leave, t_send, t_remove, t_self, t_peer_name, t_own_name,
    t_last_error
};

/* Whether the layer sent `word` (a whole message or a message's start). */
static int sent_has(const char *word)
{
    int i;

    for (i = 0; i < g_sent_count; i++) {
        if (strncmp(g_sent[i], word, strlen(word)) == 0) {
            return 1;
        }
    }
    return 0;
}

static void say(u32 peer, const char *text)
{
    bw_mp_on_net(peer, (int)BW_SESSION_MESSAGE, text, (u32)strlen(text));
}

/* A player's hello as this build says it: the game, the mods, who it is
 * (a made-up fingerprint from its number unless given), whether its game
 * runs, and this build's eidos version. */
static void hello_as(u32 peer, const char *game_mods, const char *fp, int in_game,
                     const char *version)
{
    char msg[256], made[33];

    if (fp == NULL) {
        snprintf(made, sizeof made, "%032x", (unsigned)peer);
        fp = made;
    }
    snprintf(msg, sizeof msg, "hello 1 %s %s %d %s", game_mods, fp, in_game,
             version != NULL ? version : bw_mp_version());
    say(peer, msg);
}

static void hello(u32 peer, const char *game_mods)
{
    hello_as(peer, game_mods, NULL, 0, NULL);
}

static void test_host(void)
{
    char code[16];

    printf("the host's lobby\n");
    bw_mp_reset();
    bw_mp_set_net(&k_test_net);
    bw_mp_set_local('B', "soul=2");
    BW_CHECK_EQ("the host opens a session", bw_mp_host(code, sizeof code), 0);
    BW_CHECK_EQ("with the relay's code", strcmp(code, "ABC234"), 0);
    BW_CHECK_EQ("in the lobby", (long)bw_mp_state(), (long)BW_MP_LOBBY);
    bw_mp_frame_at(1000);
    BW_CHECK_EQ("nobody to start with", bw_mp_start(), -1);
    BW_CHECK_EQ("and it says so",
                strcmp(bw_mp_start_blocker(), "nobody has joined"), 0);
    BW_CHECK_EQ("and its kind", bw_mp_start_blocker_kind(), BW_MP_BLOCK_NOBODY);

    sent_reset();
    bw_mp_on_net(2, (int)BW_SESSION_JOIN, NULL, 0);
    BW_CHECK_EQ("a join sends the roster", sent_has("roster lobby"), 1);
    BW_CHECK_EQ("the lobby has two players", (long)bw_mp_player_count(), 2);
    BW_CHECK_EQ("no start before the player's game answers", bw_mp_start(), -1);
    BW_CHECK_EQ("the reason: no answer yet", bw_mp_start_blocker_kind(), BW_MP_BLOCK_NO_ANSWER);
    hello(2, "W soul=1");
    BW_CHECK_EQ("another multiplayer version blocks the start",
                bw_mp_start(), -1);
    BW_CHECK_EQ("and the lobby names it",
                strstr(bw_mp_start_blocker(), "soul") != NULL, 1);
    BW_CHECK_EQ("the reason: the mods", bw_mp_start_blocker_kind(), BW_MP_BLOCK_MODS);
    hello_as(2, "W soul=2", NULL, 0, "9.9.9");
    BW_CHECK_EQ("another eidos version blocks the start", bw_mp_start(), -1);
    BW_CHECK_EQ("and the lobby names it",
                strstr(bw_mp_start_blocker(), "eidos 9.9.9") != NULL, 1);
    BW_CHECK_EQ("the reason: the version", bw_mp_start_blocker_kind(), BW_MP_BLOCK_VERSION);
    hello(2, "W soul=2");
    /* a ready lobby: the reason is an empty text (never NULL), which is what a screen
     * lights its Start button by */
    BW_CHECK_EQ("ready: a reason is still a text", bw_mp_start_blocker() != NULL, 1);
    BW_CHECK_EQ("ready: the reason is empty", (long)strlen(bw_mp_start_blocker()), 0);
    BW_CHECK_EQ("ready: no kind", bw_mp_start_blocker_kind(), BW_MP_BLOCK_NONE);
    sent_reset();
    BW_CHECK_EQ("the host starts", bw_mp_start(), 0);
    BW_CHECK_EQ("the game runs", (long)bw_mp_state(), (long)BW_MP_PLAYING);
    BW_CHECK_EQ("every game is told to start", sent_has("start"), 1);
    {
        BwMpPlayer p;

        BW_CHECK_EQ("the joiner is listed", bw_mp_player_at(1, &p), 0);
        BW_CHECK_EQ("by its name", strcmp(p.name, "Cheren"), 0);
        BW_CHECK_EQ("on the roster", p.on_roster, 1);
        BW_CHECK_EQ("with its game", p.game, 'W');
    }

    printf("a new player after the start\n");
    sent_reset();
    bw_mp_on_net(3, (int)BW_SESSION_JOIN, NULL, 0);
    BW_CHECK_EQ("is not removed", (long)g_removed, 0);
    hello_as(3, "B soul=2", "0123456789abcdef0123456789abcdef", 0, NULL);
    BW_CHECK_EQ("gets a new game", sent_has("save 0 0 0"), 1);
    BW_CHECK_EQ("and the start", sent_has("start"), 1);
    {
        BwMpPlayer p;

        BW_CHECK_EQ("is on the roster", bw_mp_player_at(2, &p) == 0 && p.on_roster, 1);
    }
    bw_mp_on_net(4, (int)BW_SESSION_JOIN, NULL, 0);
    hello_as(4, "B -", "fedcba9876543210fedcba9876543210", 0, NULL);
    BW_CHECK_EQ("one whose multiplayer mods differ is removed", (long)g_removed, 4);
    bw_mp_on_net(4, (int)BW_SESSION_LEAVE, NULL, 0);
    bw_mp_on_net(3, (int)BW_SESSION_LEAVE, NULL, 0);
    bw_mp_on_net(3, (int)BW_SESSION_JOIN, NULL, 0);
    BW_CHECK_EQ("the new player's drop and return change nothing",
                (long)bw_mp_state(), (long)BW_MP_PLAYING);

    printf("who may be drawn\n");
    BW_CHECK_EQ("a connected player", bw_mp_peer_visible(2), 1);
    BW_CHECK_EQ("not this game's own player", bw_mp_peer_visible(1), 0);
    BW_CHECK_EQ("not one who never joined", bw_mp_peer_visible(9), 0);
    BW_CHECK_EQ("not peer 0", bw_mp_peer_visible(0), 0);
    bw_mp_on_net(2, (int)BW_SESSION_LEAVE, NULL, 0);
    BW_CHECK_EQ("not a player who left (the roster still lists it)",
                bw_mp_peer_visible(2), 0);
    bw_mp_on_net(2, (int)BW_SESSION_JOIN, NULL, 0);
    BW_CHECK_EQ("again when it is back", bw_mp_peer_visible(2), 1);

    printf("a drop and a return within the grace\n");
    bw_mp_on_net(2, (int)BW_SESSION_LEAVE, NULL, 0);
    BW_CHECK_EQ("the others play on", (long)bw_mp_state(), (long)BW_MP_GRACE);
    BW_CHECK_EQ("not held", bw_mp_paused(), 0);
    bw_mp_frame_at(1000 + BW_MP_GRACE_MS - 1);
    BW_CHECK_EQ("still within the grace", (long)bw_mp_state(), (long)BW_MP_GRACE);
    bw_mp_on_net(2, (int)BW_SESSION_JOIN, NULL, 0);
    BW_CHECK_EQ("back in time: play goes on", (long)bw_mp_state(),
                (long)BW_MP_PLAYING);

    printf("a drop past the grace\n");
    bw_mp_frame_at(20000);
    bw_mp_on_net(2, (int)BW_SESSION_LEAVE, NULL, 0);
    sent_reset();
    bw_mp_frame_at(20000 + BW_MP_GRACE_MS);
    BW_CHECK_EQ("every game pauses", (long)bw_mp_state(), (long)BW_MP_PAUSED);
    BW_CHECK_EQ("the host is held", bw_mp_paused(), 1);
    BW_CHECK_EQ("the others are told", sent_has("pause"), 1);
    BW_CHECK_EQ("the wait names the missing player",
                strstr(bw_mp_wait_text(), "Cheren") != NULL, 1);
    BW_CHECK_EQ("no start while a roster player is missing", bw_mp_start(), -1);
    bw_mp_on_net(2, (int)BW_SESSION_JOIN, NULL, 0);
    hello(2, "W soul=2");
    BW_CHECK_EQ("still held until the host starts", bw_mp_paused(), 1);
    BW_CHECK_EQ("the screen says the host may start",
                strstr(bw_mp_wait_text(), "START") != NULL, 1);
    BW_CHECK_EQ("the host starts again", bw_mp_start(), 0);
    BW_CHECK_EQ("the game runs again", (long)bw_mp_state(), (long)BW_MP_PLAYING);

    printf("the host loses the relay\n");
    bw_mp_on_net(0, BW_MOD_NET_PORT_ENDED, "lost", 4);
    BW_CHECK_EQ("the host joins again", (long)bw_mp_state(),
                (long)BW_MP_RECONNECTING);
    BW_CHECK_EQ("held meanwhile", bw_mp_paused(), 1);
    g_joins = 0;
    sent_reset();
    bw_mp_frame_at(35000 + BW_MP_REJOIN_EVERY_MS);
    BW_CHECK_EQ("it tried", g_joins, 1);
    BW_CHECK_EQ("back in: every game waits for the host's start",
                (long)bw_mp_state(), (long)BW_MP_PAUSED);
    BW_CHECK_EQ("and the others are told", sent_has("pause"), 1);
    bw_mp_reset();
}

static void test_joiner(void)
{
    printf("a joiner\n");
    bw_mp_reset();
    bw_mp_set_net(&k_test_net);
    bw_mp_set_local('W', "");
    sent_reset();
    BW_CHECK_EQ("the joiner joins", bw_mp_join("abc234"), 0);
    BW_CHECK_EQ("no hello before the host says who it is", sent_has("hello"), 0);
    say(1, "hostid 0123456789abcdef0123456789abcdef");
    BW_CHECK_EQ("then hello, with who the player is to this host",
                sent_has("hello 1 W - "), 1);
    BW_CHECK_EQ("to the host", (long)g_sent_peer[0], 1);
    say(1, "roster lobby\n1\t1\t0\tB\t1\tHilda\n2\t1\t0\tW\t1\tCheren\n");
    BW_CHECK_EQ("the roster is read", (long)bw_mp_player_count(), 2);
    {
        BwMpPlayer p;

        (void)bw_mp_player_at(0, &p);
        BW_CHECK_EQ("the host's name", strcmp(p.name, "Hilda"), 0);
    }
    BW_CHECK_EQ("in the lobby", (long)bw_mp_state(), (long)BW_MP_LOBBY);
    say(1, "start");
    BW_CHECK_EQ("the host's start runs the game", (long)bw_mp_state(),
                (long)BW_MP_PLAYING);
    say(1, "pause");
    BW_CHECK_EQ("the host's pause holds it", bw_mp_paused(), 1);
    say(1, "start");

    printf("the host goes\n");
    bw_mp_frame_at(5000);
    bw_mp_on_net(1, (int)BW_SESSION_LEAVE, NULL, 0);
    BW_CHECK_EQ("the game is held", (long)bw_mp_state(), (long)BW_MP_HOST_GONE);
    sent_reset();
    bw_mp_on_net(1, (int)BW_SESSION_JOIN, NULL, 0);
    BW_CHECK_EQ("the host is back: hello again", sent_has("hello"), 1);
    say(1, "pause");
    say(1, "start");
    BW_CHECK_EQ("and the host's start goes on", (long)bw_mp_state(),
                (long)BW_MP_PLAYING);
    bw_mp_on_net(1, (int)BW_SESSION_LEAVE, NULL, 0);
    bw_mp_frame_at(5000 + BW_MP_HOST_WAIT_MS);
    BW_CHECK_EQ("a host that does not come back ends it", (long)bw_mp_state(),
                (long)BW_MP_ENDED);

    printf("a removed joiner\n");
    bw_mp_reset();
    bw_mp_set_net(&k_test_net);
    (void)bw_mp_join("ABC234");
    bw_mp_on_net(0, BW_MOD_NET_PORT_ENDED, "removed", 7);
    BW_CHECK_EQ("the session is over", (long)bw_mp_state(), (long)BW_MP_ENDED);
    BW_CHECK_EQ("with the reason", strcmp(bw_mp_end_reason(), "removed"), 0);
    bw_mp_reset();
}

static void test_relay_restart(void)
{
    char code[16];

    printf("the relay restarts under the host\n");
    bw_mp_reset();
    bw_mp_set_net(&k_test_net);
    bw_mp_set_local('B', "");
    g_next_code = "ABC234";
    (void)bw_mp_host(code, sizeof code);
    bw_mp_frame_at(1000);
    bw_mp_on_net(2, (int)BW_SESSION_JOIN, NULL, 0);
    hello(2, "W -");
    BW_CHECK_EQ("the game starts", bw_mp_start(), 0);
    bw_mp_on_net(0, BW_MOD_NET_PORT_ENDED, "lost", 4);
    g_join_fails = 1;
    g_error = "unknown-code";
    g_next_code = "NEW567";
    bw_mp_frame_at(1000 + BW_MP_REJOIN_EVERY_MS);
    BW_CHECK_EQ("the host opens a new session", strcmp(bw_mp_code(), "NEW567"), 0);
    BW_CHECK_EQ("and says so", strstr(bw_mp_end_reason(), "NEW567") != NULL, 1);
    BW_CHECK_EQ("every game waits for the host's start", (long)bw_mp_state(),
                (long)BW_MP_PAUSED);
    g_peer3 = "Cheren";
    sent_reset();
    bw_mp_on_net(3, (int)BW_SESSION_JOIN, NULL, 0);
    BW_CHECK_EQ("the player who comes back under a new number is not removed",
                (long)g_removed, 0);
    hello(3, "W -");
    BW_CHECK_EQ("it takes its roster place: the host may start",
                bw_mp_start(), 0);
    g_peer3 = "Bianca";
    g_join_fails = 0;
    g_error = "";
    g_next_code = "ABC234";

    printf("the relay restarts under a joiner\n");
    bw_mp_reset();
    bw_mp_set_net(&k_test_net);
    (void)bw_mp_join("ABC234");
    say(1, "start");
    bw_mp_frame_at(1000);
    bw_mp_on_net(0, BW_MOD_NET_PORT_ENDED, "lost", 4);
    g_join_fails = 1;
    g_error = "unknown-code";
    bw_mp_frame_at(1000 + BW_MP_REJOIN_EVERY_MS);
    BW_CHECK_EQ("the joiner's session ends", (long)bw_mp_state(),
                (long)BW_MP_ENDED);
    BW_CHECK_EQ("asking for the new code",
                strstr(bw_mp_end_reason(), "new code") != NULL, 1);
    g_join_fails = 0;
    g_error = "";

    printf("a plain leave\n");
    bw_mp_leave();
    BW_CHECK_EQ("says why", strcmp(bw_mp_end_reason(), "left"), 0);
    bw_mp_reset();
}

static int g_booted = -1;

static void t_boot(int new_game)
{
    g_booted = new_game;
}

static int file_size(const char *name)
{
    char dir[512], path[600];
    FILE *f;
    long n;

    if (bw_mp_dir(dir, sizeof dir) != 0) {
        return -1;
    }
    if (snprintf(path, sizeof path, "%s/%s", dir, name) >= (int)sizeof path) {
        return -1;
    }
    f = fopen(path, "rb");
    if (f == NULL) {
        return -1;
    }
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fclose(f);
    return (int)n;
}

/* The saves: the host sends a joiner its save at the start (none: a new
 * game), and keeps what a joiner's game uploads. */
static void test_saves(void)
{
    char code[16];
    char fp[33];
    char msg[256];
    static unsigned char data[9000];
    unsigned crc = 0xFFFFFFFFu;
    unsigned i;
    int k, n;

    printf("the saves\n");
    {
        /* what an earlier run kept is gone first */
        static const char *const old[] = {
            "test_mp_tmp/multiplayer/ffffeeeeddddccccbbbbaaaa99998888.sav",
            "test_mp_tmp/multiplayer/ffffeeeeddddccccbbbbaaaa99998888.sav.1",
            "test_mp_tmp/multiplayer/ffffeeeeddddccccbbbbaaaa99998888.sav.2",
            "test_mp_tmp/multiplayer/ffffeeeeddddccccbbbbaaaa99998888.sav.3"
        };

        for (k = 0; k < 4; k++) {
            remove(old[k]);
        }
    }
    bw_mp_reset();
    bw_mp_set_net(&k_test_net);
    bw_mp_set_local('B', "");
    bw_mp_set_boot(t_boot);
    (void)bw_mp_host(code, sizeof code);
    bw_mp_frame_at(1000);
    sent_reset();
    bw_mp_on_net(2, (int)BW_SESSION_JOIN, NULL, 0);
    BW_CHECK_EQ("the host says who it is", sent_has("hostid "), 1);
    sent_reset();
    say(2, "who");
    BW_CHECK_EQ("and again when asked", sent_has("hostid "), 1);
    snprintf(fp, sizeof fp, "%s", "ffffeeeeddddccccbbbbaaaa99998888");
    hello_as(2, "W -", fp, 0, NULL);
    sent_reset();
    BW_CHECK_EQ("the host starts", bw_mp_start(), 0);
    BW_CHECK_EQ("a player the host has no save for: a new game",
                sent_has("save 0 0 0"), 1);

    /* the host saves: the joiner's save is to come */
    sent_reset();
    bw_mp_host_saved();
    BW_CHECK_EQ("the host asks every game to save", sent_has("savenow"), 1);
    BW_CHECK_EQ("one save to come", bw_mp_saves_pending(), 1);

    /* the joiner's game uploads its save: two pieces, then the host keeps it */
    for (i = 0; i < sizeof data; i++) {
        data[i] = (unsigned char)(i * 7u);
    }
    for (i = 0; i < sizeof data; i++) {
        crc ^= data[i];
        for (k = 0; k < 8; k++) {
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
        }
    }
    crc = ~crc;
    for (i = 0; i < sizeof data; i += 3840u) {
        static unsigned char piece[4096];
        unsigned m = sizeof data - i < 3840u ? (unsigned)sizeof data - i : 3840u;

        n = snprintf((char *)piece, 64, "save %u %u %u\n", (unsigned)sizeof data,
                     i, crc);
        memcpy(piece + n, data + i, m);
        bw_mp_on_net(2, (int)BW_SESSION_MESSAGE, piece, (u32)n + m);
    }
    snprintf(msg, sizeof msg, "%s.sav", fp);
    BW_CHECK_EQ("the host keeps the joiner's save", file_size(msg),
                (int)sizeof data);
    BW_CHECK_EQ("every save is in", bw_mp_saves_pending(), 0);
    bw_mp_reset();

    /* a joiner: an empty save from the host starts a new game */
    bw_mp_set_net(&k_test_net);
    bw_mp_set_boot(t_boot);
    sent_reset();
    (void)bw_mp_join("ABC234");
    BW_CHECK_EQ("a joiner that missed the host's id asks", sent_has("who"), 1);
    say(1, "hostid 0123456789abcdef0123456789abcdef");
    g_booted = -1;
    bw_mp_on_net(1, (int)BW_SESSION_MESSAGE, "save 0 0 0\n", 11);
    BW_CHECK_EQ("the joiner's game starts from the title menu",
                bw_mp_started_pending(), 1);
    BW_CHECK_EQ("without a restart", g_booted, -1);
    bw_mp_leave();
    bw_mp_reset();
}

/* The host's games: a folder each, named from what the player typed, and
 * a taken name gets a number. */
static void test_games(void)
{
    char names[32][BW_MP_GAME_NAME_MAX];
    char made[BW_MP_GAME_NAME_MAX];
    int n, i, a = 0, b = 0;

    printf("the host's games\n");
    for (i = 1; i < 100; i++) {
        /* the games an earlier run made go first */
        char path[96], file[128];

        if (i == 1) {
            snprintf(path, sizeof path, "test_mp_tmp/multiplayer/games/Tour1");
        } else {
            snprintf(path, sizeof path, "test_mp_tmp/multiplayer/games/Tour1 %d", i);
        }
        snprintf(file, sizeof file, "%s/host_id.txt", path);
        remove(file);
        snprintf(file, sizeof file, "%s/shared.txt", path);
        remove(file);
        (void)rmdir(path);
    }
    BW_CHECK_EQ("a new game", bw_mp_game_create("Tour/1", made, sizeof made), 0);
    BW_CHECK_EQ("a folder name", strcmp(made, "Tour1") == 0 ||
                strncmp(made, "Tour1 ", 6) == 0, 1);
    BW_CHECK_EQ("chosen", strcmp(bw_mp_game(), made), 0);
    BW_CHECK_EQ("the same name again", bw_mp_game_create("Tour1", made, sizeof made), 0);
    BW_CHECK_EQ("gets a number", strncmp(made, "Tour1 ", 6), 0);
    n = bw_mp_game_list(names, 32);
    for (i = 0; i < n; i++) {
        a |= strcmp(names[i], "Tour1") == 0;
        b |= strcmp(names[i], made) == 0;
    }
    BW_CHECK_EQ("both are listed", a && b, 1);
    BW_CHECK_EQ("an empty name is refused", bw_mp_game_select(""), -1);
    BW_CHECK_EQ("chosen again", bw_mp_game_select("Tour1"), 0);
}

/* A multiplayer mod's handler on the host: the players' events. */
static int g_remote_seen;
static u32 g_remote_player;

static int remote_handler(void *state, BwEvent *event)
{
    (void)state;
    (void)event;
    g_remote_seen++;
    g_remote_player = bw_mod_event_player();
    return BW_EVENT_CONTINUE;
}

/* The shared table: the host's copy is the real one; a joiner's change
 * goes to the host, which sends every change to everyone. */
static void test_shared(void)
{
    char code[16];
    s32 v = -1;

    printf("the shared table\n");
    BW_CHECK_EQ("outside a game: no table", bw_mp_shared_get("soul", "pair", &v), -1);
    /* a game of its own, with no table kept from an earlier run */
    (void)bw_mp_game_select("Shared");
    remove("test_mp_tmp/multiplayer/games/Shared/shared.txt");
    bw_mp_reset();
    bw_mp_set_net(&k_test_net);
    bw_mp_set_local('B', "");
    (void)bw_mp_host(code, sizeof code);
    bw_mp_on_net(2, (int)BW_SESSION_JOIN, NULL, 0);
    sent_reset();
    BW_CHECK_EQ("the host sets a key", bw_mp_shared_set("soul", "pair.1", 42), 0);
    BW_CHECK_EQ("and sends it to everyone", sent_has("tset soul/pair.1 42"), 1);
    BW_CHECK_EQ("the host reads it", bw_mp_shared_get("soul", "pair.1", &v), 0);
    BW_CHECK_EQ("its value", (int)v, 42);
    BW_CHECK_EQ("a key not set", bw_mp_shared_get("soul", "nothing", &v), 1);
    BW_CHECK_EQ("another mod's key is its own", bw_mp_shared_get("other", "pair.1", &v), 1);
    sent_reset();
    say(2, "treq soul/pair.2 7");
    BW_CHECK_EQ("a joiner's change is applied", bw_mp_shared_get("soul", "pair.2", &v), 0);
    BW_CHECK_EQ("as asked", (int)v, 7);
    BW_CHECK_EQ("and sent to everyone", sent_has("tset soul/pair.2 7"), 1);
    BW_CHECK_EQ("a key with a space is refused", bw_mp_shared_set("soul", "a b", 1), -1);
    /* two players' additions at the same moment are both kept */
    say(2, "tadd soul/faints 1");
    BW_CHECK_EQ("the host adds", bw_mp_shared_add("soul", "faints", 1), 0);
    (void)bw_mp_shared_get("soul", "faints", &v);
    BW_CHECK_EQ("both additions count", (int)v, 2);
    say(2, "tcas soul/faints 1 9");
    (void)bw_mp_shared_get("soul", "faints", &v);
    BW_CHECK_EQ("a cas that does not match changes nothing", (int)v, 2);
    say(2, "tcas soul/faints 2 9");
    (void)bw_mp_shared_get("soul", "faints", &v);
    BW_CHECK_EQ("a cas that matches sets it", (int)v, 9);
    sent_reset();
    hello_as(2, "B -", "ffffeeeeddddccccbbbbaaaa99998888", 0, NULL);
    BW_CHECK_EQ("a hello gets the whole table", sent_has("tset soul/pair.1 42"), 1);
    {
        /* two events of a joiner in one message reach the mod's handler */
        u8 msg[4 + 2 * sizeof(BwMoneyChange)];
        BwMoneyChange e;

        bw_mod_event_reset();
        bw_mod_event_set_registering_remote(1);
        (void)bw_mod_event_register(BW_EVENT_MONEY_CHANGE, remote_handler);
        bw_mod_event_set_registering_remote(0);
        memset(&e, 0, sizeof e);
        e.h.kind = BW_EVENT_MONEY_CHANGE;
        e.h.size = (u32)sizeof e;
        memcpy(msg, "evs ", 4);
        memcpy(msg + 4, &e, sizeof e);
        memcpy(msg + 4 + sizeof e, &e, sizeof e);
        bw_mp_on_net(2, (int)BW_SESSION_MESSAGE, msg, (u32)sizeof msg);
        BW_CHECK_EQ("a joiner's events reach the mods", g_remote_seen, 2);
        BW_CHECK_EQ("as that player's", (int)g_remote_player, 2);
        bw_mod_event_reset();
    }
    bw_mp_reset();

    /* a joiner asks the host and keeps what the host sends */
    bw_mp_set_net(&k_test_net);
    (void)bw_mp_join("ABC234");
    sent_reset();
    BW_CHECK_EQ("a joiner's change", bw_mp_shared_set("soul", "pair.3", 5), 0);
    BW_CHECK_EQ("goes to the host", sent_has("treq soul/pair.3 5"), 1);
    BW_CHECK_EQ("a joiner's addition", bw_mp_shared_add("soul", "faints", 1), 0);
    BW_CHECK_EQ("goes to the host to make", sent_has("tadd soul/faints 1"), 1);
    BW_CHECK_EQ("and is not made here", bw_mp_shared_get("soul", "pair.3", &v), 1);
    say(1, "tset soul/pair.3 5");
    BW_CHECK_EQ("the host's word makes it", bw_mp_shared_get("soul", "pair.3", &v), 0);
    BW_CHECK_EQ("its value", (int)v, 5);
    bw_mp_leave();
    bw_mp_reset();
}

static void test_delta(void)
{
    enum { N = 100000 };
    static u8 base[N], now[N];
    u32 dsize = 0;
    int bad_ok = 0, i;

    printf("a save as changes\n");
    for (i = 0; i < N; i++) {
        base[i] = (u8)(i * 7);
    }
    memcpy(now, base, N);
    BW_CHECK_EQ("a save that did not change is a few bytes",
                bw_mp_delta_test(base, now, N, &dsize, &bad_ok), 1);
    BW_CHECK_EQ("of 16", (long)dsize, 16);
    BW_CHECK_EQ("a damaged delta is refused", bad_ok, 1);
    now[10] ^= 1;
    now[11] ^= 1;
    now[50000] ^= 0xFF;
    now[N - 1] ^= 3;
    BW_CHECK_EQ("a few changed blocks come back whole",
                bw_mp_delta_test(base, now, N, &dsize, &bad_ok), 1);
    BW_CHECK_EQ("in far fewer bytes than the file", dsize < 200, 1);
    BW_CHECK_EQ("and a damaged one is refused", bad_ok, 1);
    for (i = 0; i < N; i++) {
        now[i] = (u8)(base[i] + 1);
    }
    BW_CHECK_EQ("a file that changed everywhere is sent whole",
                bw_mp_delta_test(base, now, N, &dsize, &bad_ok), -1);
}

int main(void)
{
    /* every file the layer writes goes to the test's own folder */
    bw_set_save_dir("test_mp_tmp");
    test_host();
    test_joiner();
    test_relay_restart();
    test_saves();
    test_games();
    test_shared();
    test_delta();
    BW_REPORT("test_mp");
}
