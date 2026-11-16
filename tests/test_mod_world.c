/*
 * tests/test_mod_world.c -- the world session client of mod API version 6
 * (sdk/mod_world.c; docs/mod-services.md, "World sessions").
 *
 * The synthetic half hands the reader's hook frames as metaxy sends them
 * (bw_mod_world_feed) and checks the events a mod gets: a player's data
 * put together from F pieces, the items, the positions, an error, and the
 * bytes world_read gives inside the handler.
 *
 * The live half runs only with EIDOS_WORLD_RELAY=host:port, against a
 * metaxy that serves world sessions (metaxy's branch world-sessions): it
 * makes a world of two players, starts it, joins as the first, checks a
 * location whose item is the second player's, leaves, joins as the second
 * and finds the item in its push, then joins the first again with its key
 * (the empty V after the first join confirmed the claim).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "port.h"
#include "mod_api.h"
#include "mod_event.h"
#include "mod_net.h"
#include "mod_v6.h"
#include "plat_thread.h"
#include "port_test.h"

#ifdef _WIN32
#include <windows.h>
static void t_sleep_ms(unsigned ms) { Sleep(ms); }
#else
#include <unistd.h>
static void t_sleep_ms(unsigned ms) { usleep(ms * 1000u); }
#endif

typedef struct Seen {
    u32  kind;
    u32  player;
    u32  ticket;
    u32  size;
    char text[256];
    char *all;        /* the whole bytes of a JOINED */
} Seen;

static Seen g_seen[64];
static u32  g_seen_count;

static int world_handler(void *state, BwEvent *event)
{
    BwWorld *w = (BwWorld *)event;
    Seen *s;
    int n;

    (void)state;
    if (g_seen_count == 64) {
        return BW_EVENT_CONTINUE;
    }
    s = &g_seen[g_seen_count++];
    memset(s, 0, sizeof(*s));
    s->kind = w->kind;
    s->player = w->player;
    s->ticket = w->ticket;
    s->size = w->size;
    n = bw_mod_world_read(s->text, sizeof(s->text) - 1);
    if (n >= 0 && (u32)n < sizeof(s->text)) {
        s->text[n] = '\0';
    }
    if (w->kind == BW_WORLD_JOINED && w->size > 0) {
        s->all = (char *)malloc(w->size + 1);
        if (s->all != NULL) {
            (void)bw_mod_world_read(s->all, w->size);
            s->all[w->size] = '\0';
        }
    }
    return BW_EVENT_CONTINUE;
}

static void clear_seen(void)
{
    u32 i;

    for (i = 0; i < g_seen_count; i++) {
        free(g_seen[i].all);
    }
    memset(g_seen, 0, sizeof(g_seen));
    g_seen_count = 0;
}

static const Seen *find(u32 kind)
{
    u32 i;

    for (i = 0; i < g_seen_count; i++) {
        if (g_seen[i].kind == kind) {
            return &g_seen[i];
        }
    }
    return NULL;
}

static void feed(char type, const char *text)
{
    bw_mod_world_feed(type, (const u8 *)text, (u32)strlen(text));
}

static int synthetic(void)
{
    static char big[9000];
    char piece[5000];
    u32 i;
    const Seen *s;

    printf("world events from frames\n");
    for (i = 0; i < sizeof(big) - 1; i++) {
        big[i] = (char)('a' + i % 26);
    }
    big[0] = '{';
    big[sizeof(big) - 2] = '}';
    big[sizeof(big) - 1] = '\0';

    /* a player's data in three F pieces, then S naming them */
    snprintf(piece, sizeof(piece), "7 0\n%.*s", 4000, big);
    feed('F', piece);
    snprintf(piece, sizeof(piece), "7 0\n%.*s", 4000, big + 4000);
    feed('F', piece);
    snprintf(piece, sizeof(piece), "7 1\n%s", big + 8000);
    feed('F', piece);
    feed('S', "2 0123456789abcdef0123456789abcdef\n@7");
    feed('Y', "1 1001 1 2 1002 3");
    feed('W', "2 Misty");
    feed('P', "1 Ash");
    feed('U', "1 389 10 0 12 1");
    feed('X', "1");
    feed('R', "wait 3");
    feed('R', "ABCDEF 00112233445566778899aabbccddeeff");
    feed('O', "ABCDEF started");
    feed('E', "no-world no world has that code");
    bw_mod_world_frame();

    s = find(BW_WORLD_JOINED);
    BW_REQUIRE("the join came", s != NULL);
    BW_CHECK_EQ("its player", s->player, 2);
    BW_CHECK_EQ("its size: the head line and the whole data",
                s->size, 35u + (u32)strlen(big));
    BW_CHECK_EQ("the data put together", s->all != NULL && strcmp(s->all + 35, big) == 0, 1);
    BW_CHECK_EQ("the key line", strncmp(s->text, "2 0123456789abcdef0123456789abcdef\n{", 36), 0);
    s = find(BW_WORLD_ITEMS);
    BW_CHECK_EQ("the items", s != NULL && strcmp(s->text, "1 1001 1 2 1002 3") == 0, 1);
    s = find(BW_WORLD_POSITION);
    BW_CHECK_EQ("a position", s != NULL && s->player == 1, 1);
    s = find(BW_WORLD_PLAYER_OUT);
    BW_CHECK_EQ("a player left", s != NULL && s->player == 1, 1);
    s = find(BW_WORLD_WAIT);
    BW_CHECK_EQ("the queue place", s != NULL && strcmp(s->text, "3") == 0, 1);
    s = find(BW_WORLD_GENERATED);
    BW_CHECK_EQ("generated", s != NULL && strncmp(s->text, "ABCDEF ", 7) == 0, 1);
    s = find(BW_WORLD_OWNER_DONE);
    BW_CHECK_EQ("the owner action", s != NULL && strcmp(s->text, "ABCDEF started") == 0, 1);
    s = find(BW_WORLD_ERROR);
    BW_CHECK_EQ("an error", s != NULL && strncmp(s->text, "no-world", 8) == 0, 1);
    BW_CHECK_EQ("world_read outside a handler", bw_mod_world_read(piece, 4), -1);
    BW_CHECK_EQ("checks without a session", bw_mod_world_checks((const u32 *)"\1\0\0\0", 1), -1);
    clear_seen();
    return 0;
}

/* ---- the live half ------------------------------------------------- */

static int wait_for(u32 kind, unsigned ms)
{
    unsigned t;

    for (t = 0; t < ms; t += 20) {
        bw_mod_world_frame();
        if (find(kind) != NULL || find(BW_WORLD_ERROR) != NULL) {
            return find(kind) != NULL ? 0 : -1;
        }
        t_sleep_ms(20);
    }
    return -1;
}

/* The first location of the player data whose receiver is `receiver`:
 * "locations":[[location,item,receiver],...]. */
static u32 location_for(const char *data, u32 receiver, u32 *item)
{
    const char *p = strstr(data, "\"locations\"");
    unsigned loc, it, rec;

    if (p == NULL) {
        return 0;
    }
    p = strchr(p, '[');
    while (p != NULL && (p = strchr(p + 1, '[')) != NULL) {
        if (sscanf(p, "[%u,%u,%u]", &loc, &it, &rec) == 3 && rec == receiver) {
            *item = it;
            return loc;
        }
    }
    return 0;
}

static int live(const char *relay)
{
    char host[128], code[16], owner[64], key1[64], msg[64];
    const char *colon = strrchr(relay, ':');
    const char *config =
        "{\"schema\": 1, \"seed\": 4242, \"options\": {},"
        " \"players\": [{\"name\": \"Ash\", \"version\": \"black\"},"
        " {\"name\": \"Misty\", \"version\": \"white\"},"
        " {\"name\": \"Brock\", \"version\": \"black\"}]}";
    u32 ticket = 0, item = 0, loc;
    const Seen *s;
    unsigned player;

    printf("live: %s\n", relay);
    BW_REQUIRE("host:port", colon != NULL && (size_t)(colon - relay) < sizeof(host));
    memcpy(host, relay, (size_t)(colon - relay));
    host[colon - relay] = '\0';
    bw_mod_net_set_transport(bw_mod_net_tcp_transport());
    bw_mod_net_set_relay(host, (u16)atoi(colon + 1));

    BW_CHECK_EQ("world_create", bw_mod_world_create(config, &ticket), 0);
    BW_REQUIRE("the world is made", wait_for(BW_WORLD_GENERATED, 60000) == 0);
    s = find(BW_WORLD_GENERATED);
    BW_CHECK_EQ("the answer carries the create's ticket", s->ticket, ticket);
    BW_REQUIRE("code and owner key", sscanf(s->text, "%15s %63s", code, owner) == 2);
    printf("  world %s\n", code);
    clear_seen();

    BW_CHECK_EQ("start", bw_mod_world_owner(code, owner, BW_WORLD_OWNER_START), 0);
    BW_REQUIRE("started", wait_for(BW_WORLD_OWNER_DONE, 10000) == 0);
    clear_seen();

    BW_CHECK_EQ("join as Ash", bw_mod_world_join(code, "Ash", &ticket), 0);
    BW_REQUIRE("the push", wait_for(BW_WORLD_JOINED, 20000) == 0);
    s = find(BW_WORLD_JOINED);
    BW_REQUIRE("player and key", sscanf(s->text, "%u %63s", &player, key1) == 2);
    BW_CHECK_EQ("Ash is player 1", player, 1);
    BW_CHECK_EQ("the join's ticket", s->ticket, ticket);
    loc = s->all != NULL ? location_for(s->all, 2, &item) : 0;
    BW_REQUIRE("a location of Ash's with Misty's item", loc != 0);
    printf("  location %u holds item %u for Misty\n", loc, item);
    (void)wait_for(BW_WORLD_PLAYER_IN, 3000);
    BW_CHECK_EQ("check it", bw_mod_world_checks(&loc, 1), 0);
    t_sleep_ms(500);
    bw_mod_world_frame();
    bw_mod_world_leave();
    clear_seen();

    BW_CHECK_EQ("join as Misty", bw_mod_world_join(code, "Misty", &ticket), 0);
    BW_REQUIRE("Misty's push", wait_for(BW_WORLD_JOINED, 20000) == 0);
    (void)wait_for(BW_WORLD_ITEMS, 5000);
    s = find(BW_WORLD_ITEMS);
    snprintf(msg, sizeof(msg), "1 %u 1", item);
    BW_CHECK_EQ("Misty's first item is Ash's find", s != NULL && strcmp(s->text, msg) == 0, 1);
    if (s != NULL) {
        printf("  Misty's items: %s\n", s->text);
    }
    bw_mod_world_leave();
    clear_seen();

    /* the empty V after Ash's first join confirmed the claim: the key works */
    BW_CHECK_EQ("join Ash again with the key", bw_mod_world_join(code, key1, &ticket), 0);
    BW_REQUIRE("the push again", wait_for(BW_WORLD_JOINED, 20000) == 0);
    s = find(BW_WORLD_JOINED);
    BW_CHECK_EQ("the same player", s != NULL && s->text[0] == '1', 1);
    bw_mod_world_leave();
    clear_seen();

    /* A claim released before the game confirmed it: join Brock by name and
     * leave before the frame (no V goes out), then join with that key: metaxy
     * answers bad-key and the port joins again by name, with a new key. */
    {
        char key2[64], key3[64];

        BW_CHECK_EQ("join Brock", bw_mod_world_join(code, "Brock", &ticket), 0);
        t_sleep_ms(3000);
        bw_mod_world_leave();              /* the push is still queued: no V */
        t_sleep_ms(1000);
        bw_mod_world_frame();
        s = find(BW_WORLD_JOINED);
        BW_REQUIRE("Brock's first push", s != NULL && sscanf(s->text, "%u %63s", &player, key2) == 2);
        clear_seen();
        BW_CHECK_EQ("join Brock with the released key", bw_mod_world_join(code, key2, &ticket), 0);
        BW_REQUIRE("the port joined again by name", wait_for(BW_WORLD_JOINED, 20000) == 0);
        s = find(BW_WORLD_JOINED);
        BW_REQUIRE("a key", sscanf(s->text, "%u %63s", &player, key3) == 2);
        BW_CHECK_EQ("Brock is player 3", player, 3);
        BW_CHECK_EQ("the key is new", strcmp(key2, key3) != 0, 1);
        BW_CHECK_EQ("no bad-key reached the mod", find(BW_WORLD_ERROR) == NULL, 1);
        bw_mod_world_leave();
        clear_seen();
    }

    BW_CHECK_EQ("delete", bw_mod_world_owner(code, owner, BW_WORLD_OWNER_DELETE), 0);
    BW_CHECK_EQ("deleted", wait_for(BW_WORLD_OWNER_DONE, 10000), 0);
    bw_mod_world_leave();
    return 0;
}

int main(void)
{
    const char *relay = getenv("EIDOS_WORLD_RELAY");

    bw_mod_event_reset();
    BW_REQUIRE("bind the handler", bw_mod_event_register(BW_EVENT_WORLD, world_handler) == 0);
    if (synthetic() != 0) {
        BW_REPORT("test_mod_world");
    }
    if (relay != NULL && relay[0] != '\0') {
        (void)live(relay);
    } else {
        printf("live: skipped (EIDOS_WORLD_RELAY not set)\n");
    }
    BW_REPORT("test_mod_world");
}
