/*
 * tests/test_mod_v7_host.c -- the services mod API version 7 added for
 * online mods (docs/mod-services.md, "Version 7: the web, the clock, the
 * game's hash and the pop-up"), with no network.
 *
 * What is checked:
 *
 *   - MD5 against RFC 1321's own test values;
 *   - the RetroAchievements hash of a made-up extraction folder, against
 *     the value Python's MD5 gives for the same bytes (the header's first
 *     0x160 bytes, the ARM9 and ARM7 code at the header's sizes, 0xA00
 *     bytes of icon padded with zeros); a folder missing a file has none;
 *   - the clock: the date is after 2020, the count only goes up;
 *   - web_request refuses what it must (not https, a space or a line break
 *     in the address or the type, a body too large);
 *   - a GET and a POST reach the server as such, their answers come as the
 *     event `web` with the ticket, the status and the size, and the mod
 *     that asked reads the bytes while any other mod reads nothing;
 *   - a request with no answer fires with BW_WEB_FAILED and the reason;
 *   - at most four requests wait at once;
 *   - pop-ups wait in order, eight at most, and a long text is cut on a
 *     whole UTF-8 character.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "md5.h"
#include "mod_api.h"
#include "mod_event.h"
#include "mod_popup.h"
#include "mod_v7.h"
#include "mod_web.h"
#include "net_https.h"
#include "plat_thread.h"
#include "types.h"
#include "port_test.h"

#ifdef _WIN32
#include <direct.h>
#define T_MKDIR(p) _mkdir(p)
#define T_RMDIR(p) _rmdir(p)
#else
#include <sys/stat.h>
#include <unistd.h>
#define T_MKDIR(p) mkdir(p, 0755)
#define T_RMDIR(p) rmdir(p)
#endif

void bw_mod_savedata_set_namespace(const char *ns);

/* ------------------------------------------------------------------ */
/* MD5 and the hash                                                    */
/* ------------------------------------------------------------------ */

static const char *md5_of(const void *data, size_t size)
{
    static char hex[33];
    BwMd5 m;
    u8 d[16];

    bw_md5_init(&m);
    bw_md5_append(&m, data, size);
    bw_md5_final(&m, d);
    bw_md5_hex(d, hex);
    return hex;
}

static int test_md5(void)
{
    static const char *const v[][2] = {
        { "", "d41d8cd98f00b204e9800998ecf8427e" },
        { "a", "0cc175b9c0f1b6a831c399e269772661" },
        { "abc", "900150983cd24fb0d6963f7d28e17f72" },
        { "message digest", "f96b697d7cb7938d525a2f31aaf161d0" },
        { "abcdefghijklmnopqrstuvwxyz", "c3fcd3d76192e4007dfb496cca67e13b" },
        { "12345678901234567890123456789012345678901234567890123456789012345678901234567890",
          "57edf4a22be3c955ac49da2e2107b67a" },
    };
    size_t i;
    BwMd5 m;
    u8 d[16];
    char hex[33];

    for (i = 0; i < sizeof v / sizeof v[0]; i++) {
        BW_CHECK_EQ(v[i][0][0] != '\0' ? v[i][0] : "md5 of nothing", strcmp(md5_of(v[i][0], strlen(v[i][0])), v[i][1]), 0);
    }
    /* the same bytes appended in odd pieces */
    bw_md5_init(&m);
    bw_md5_append(&m, "1234567890123456789012345678901234567890", 40);
    bw_md5_append(&m, "1234567", 7);
    bw_md5_append(&m, "890123456789012345678901234567890", 33);
    bw_md5_final(&m, d);
    bw_md5_hex(d, hex);
    BW_CHECK_EQ("md5 in pieces", strcmp(hex, "57edf4a22be3c955ac49da2e2107b67a"), 0);
    return 0;
}

static int put(const char *dir, const char *name, const u8 *data, size_t size)
{
    char path[256];
    FILE *fp;

    snprintf(path, sizeof path, "%s/%s", dir, name);
    fp = fopen(path, "wb");
    if (fp == NULL) {
        return -1;
    }
    if (fwrite(data, 1, size, fp) != size) {
        fclose(fp);
        return -1;
    }
    return fclose(fp);
}

#define T_EXTRACT "mod_v7_host_extract"

static int test_hash(void)
{
    static u8 header[0x200], arm9[120], arm7[60], banner[0x840];
    char hash[33], path[256];
    size_t i;

    for (i = 0; i < sizeof header; i++) header[i] = (u8)i;
    header[0x2C] = 100; header[0x2D] = 0; header[0x2E] = 0; header[0x2F] = 0;
    header[0x3C] = 50;  header[0x3D] = 0; header[0x3E] = 0; header[0x3F] = 0;
    for (i = 0; i < sizeof arm9; i++) arm9[i] = (u8)(i * 7);
    for (i = 0; i < sizeof arm7; i++) arm7[i] = (u8)(i * 13 + 1);
    for (i = 0; i < sizeof banner; i++) banner[i] = (u8)(i * 3);
    (void)T_MKDIR(T_EXTRACT);
    BW_REQUIRE("the made-up extraction is written",
               put(T_EXTRACT, "header.bin", header, sizeof header) == 0
               && put(T_EXTRACT, "arm9.bin", arm9, sizeof arm9) == 0
               && put(T_EXTRACT, "arm7.bin", arm7, sizeof arm7) == 0
               && put(T_EXTRACT, "banner.bin", banner, sizeof banner) == 0);
    BW_CHECK_EQ("the RetroAchievements hash of the folder", bw_mod_v7_ra_hash_dir(T_EXTRACT, hash), 0);
    BW_CHECK_EQ("is Python's", strcmp(hash, "e31cfd5b172767738e2947c9d80939a0"), 0);
    snprintf(path, sizeof path, "%s/arm7.bin", T_EXTRACT);
    remove(path);
    BW_CHECK_EQ("a folder missing a file has none", bw_mod_v7_ra_hash_dir(T_EXTRACT, hash), -1);
    BW_CHECK_EQ("an unknown kind is refused", bw_mod_v7_game_hash(99u, hash, sizeof hash), -1);
    BW_CHECK_EQ("too small a buffer is refused", bw_mod_v7_game_hash(BW_GAME_HASH_RA, hash, 32u), -1);
    remove(T_EXTRACT "/header.bin");
    remove(T_EXTRACT "/arm9.bin");
    remove(T_EXTRACT "/banner.bin");
    (void)T_RMDIR(T_EXTRACT);
    return 0;
}

static int test_clock(void)
{
    u64 a = bw_mod_v7_ticks_ms(), b;

    BW_CHECK_EQ("the date is after 2020", bw_mod_v7_time_utc() > 1577836800LL, 1);
    bw_net_sleep_ms(20);
    b = bw_mod_v7_ticks_ms();
    BW_CHECK_EQ("the count goes up", b >= a + 10u, 1);
    return 0;
}

/* ------------------------------------------------------------------ */
/* The web, over a made-up server                                      */
/* ------------------------------------------------------------------ */

static struct {
    const char *answer;         /* what the server sends, all at once */
    int refuse;                 /* the connection fails */
    volatile int hold;          /* the connection waits until this is 0 */
    char sent[4096];            /* the last request */
    size_t at;
} g_srv;

static int srv_open(void *user, const char *host, u16 port, int fresh, char *err, size_t err_size)
{
    (void)user;
    (void)port;
    (void)fresh;
    while (g_srv.hold) {
        bw_net_sleep_ms(1);
    }
    if (g_srv.refuse) {
        snprintf(err, err_size, "no connection to %s", host);
        return -1;
    }
    g_srv.sent[0] = '\0';
    g_srv.at = 0;
    return 0;
}

static int srv_send(void *user, const void *buf, u32 size)
{
    size_t n = strlen(g_srv.sent);

    (void)user;
    if (n + size + 1 > sizeof g_srv.sent) {
        return -1;
    }
    memcpy(g_srv.sent + n, buf, size);
    g_srv.sent[n + size] = '\0';
    return 0;
}

static int srv_recv(void *user, void *buf, u32 size, int timeout_ms)
{
    size_t left = strlen(g_srv.answer) - g_srv.at;
    size_t n = left < size ? left : size;

    (void)user;
    (void)timeout_ms;
    if (n == 0) {
        return 0;
    }
    memcpy(buf, g_srv.answer + g_srv.at, n);
    g_srv.at += n;
    return (int)n;
}

static void srv_close(void *user, int keep)
{
    (void)user;
    (void)keep;
}

static const BwNetStream g_srv_stream = { NULL, srv_open, srv_send, srv_recv, srv_close, NULL };

/* What the handler saw of each answer. */
static struct {
    u32 count;
    u32 ticket[8];
    s32 status[8];
    u32 size[8];
    char body[8][128];
    int other_read[8];          /* what web_read answered another mod */
} g_seen;

static int web_handler(void *state, BwEvent *event)
{
    const BwWeb *w = (const BwWeb *)event;
    u32 k = g_seen.count;
    int n;

    (void)state;
    if (k >= 8u) {
        return BW_EVENT_CONTINUE;
    }
    g_seen.ticket[k] = w->ticket;
    g_seen.status[k] = w->status;
    g_seen.size[k] = w->size;
    /* another mod's handler first: it reads nothing */
    bw_mod_savedata_set_namespace("other");
    g_seen.other_read[k] = bw_mod_web_read(g_seen.body[k], sizeof g_seen.body[k] - 1);
    /* then the mod that asked */
    bw_mod_savedata_set_namespace("asker");
    n = bw_mod_web_read(g_seen.body[k], sizeof g_seen.body[k] - 1);
    g_seen.body[k][n >= 0 && (size_t)n < sizeof g_seen.body[k] ? n : 0] = '\0';
    g_seen.count++;
    return BW_EVENT_CONTINUE;
}

/* The game's frames until `count` answers came (two seconds at most). */
static void pump(u32 count)
{
    int i;

    for (i = 0; i < 2000 && g_seen.count < count; i++) {
        bw_mod_web_frame();
        bw_net_sleep_ms(1);
    }
}

static int test_web(void)
{
    u32 t1 = 0, t2 = 0, t[5];
    int i;
    static char big[BW_WEB_BODY_MAX + 1];

    bw_mod_web_set_stream(&g_srv_stream);
    BW_REQUIRE("bind the handler", bw_mod_event_register(BW_EVENT_WEB, web_handler) == 0);
    bw_mod_savedata_set_namespace("asker");

    printf("refusals\n");
    BW_CHECK_EQ("an http address", (long)bw_mod_web_request("http://h.test/x", NULL, 0, NULL, &t1), -1);
    BW_CHECK_EQ("a space in the address", (long)bw_mod_web_request("https://h.test/a b", NULL, 0, NULL, &t1), -1);
    BW_CHECK_EQ("a line break in the type",
                (long)bw_mod_web_request("https://h.test/x", "a", 1, "text/plain\nX: 1", &t1), -1);
    BW_CHECK_EQ("a body too large",
                (long)bw_mod_web_request("https://h.test/x", big, (u32)sizeof big, "text/plain", &t1), -1);
    BW_CHECK_EQ("no ticket for a refusal", (long)t1, 0);

    printf("a GET\n");
    g_srv.answer = "HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\nhello";
    BW_CHECK_EQ("the request is taken", (long)bw_mod_web_request("https://h.test/get?x=1", NULL, 0, "", &t1), 0);
    BW_CHECK_EQ("with a ticket", t1 != 0, 1);
    pump(1);
    BW_CHECK_EQ("its answer came", (long)g_seen.count, 1);
    BW_CHECK_EQ("the request was a GET", strncmp(g_srv.sent, "GET /get?x=1 HTTP/1.1\r\n", 23), 0);
    BW_CHECK_EQ("the event names the ticket", (long)g_seen.ticket[0], (long)t1);
    BW_CHECK_EQ("the status", (long)g_seen.status[0], 200);
    BW_CHECK_EQ("the size", (long)g_seen.size[0], 5);
    BW_CHECK_EQ("the mod that asked reads the bytes", strcmp(g_seen.body[0], "hello"), 0);
    BW_CHECK_EQ("another mod reads nothing", (long)g_seen.other_read[0], -1);
    BW_CHECK_EQ("outside the handler nobody reads", (long)bw_mod_web_read(NULL, 0), -1);

    printf("a POST\n");
    g_srv.answer = "HTTP/1.1 401 Unauthorized\r\nContent-Length: 14\r\n\r\n{\"Success\":0}\n";
    BW_CHECK_EQ("the request is taken",
                (long)bw_mod_web_request("https://h.test/dorequest.php", "r=login2&u=me", 13, NULL, &t2), 0);
    BW_CHECK_EQ("a new ticket", t2 != t1 && t2 != 0, 1);
    pump(2);
    BW_CHECK_EQ("the request was a POST", strncmp(g_srv.sent, "POST /dorequest.php HTTP/1.1\r\n", 30), 0);
    BW_CHECK_EQ("form data by default",
                strstr(g_srv.sent, "Content-Type: application/x-www-form-urlencoded\r\n") != NULL, 1);
    BW_CHECK_EQ("with its body", strstr(g_srv.sent, "\r\n\r\nr=login2&u=me") != NULL, 1);
    BW_CHECK_EQ("an error status comes with its body", (long)g_seen.status[1], 401);
    BW_CHECK_EQ("the body", strcmp(g_seen.body[1], "{\"Success\":0}\n"), 0);

    printf("no answer\n");
    g_srv.refuse = 1;
    BW_CHECK_EQ("the request is taken", (long)bw_mod_web_request("https://h.test/x", NULL, 0, NULL, &t1), 0);
    pump(3);
    g_srv.refuse = 0;
    BW_CHECK_EQ("it fires failed", (long)g_seen.status[2], (long)BW_WEB_FAILED);
    BW_CHECK_EQ("with the reason", strstr(g_seen.body[2], "no connection") != NULL, 1);

    printf("four wait at most\n");
    g_srv.hold = 1;
    for (i = 0; i < 4; i++) {
        BW_CHECK_EQ("a request is taken", (long)bw_mod_web_request("https://h.test/x", NULL, 0, NULL, &t[i]), 0);
    }
    BW_CHECK_EQ("four wait", (long)bw_mod_web_waiting(), 4);
    BW_CHECK_EQ("a fifth is refused", (long)bw_mod_web_request("https://h.test/x", NULL, 0, NULL, &t[4]), -1);
    g_srv.answer = "HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nok";
    g_srv.hold = 0;
    pump(7);
    BW_CHECK_EQ("all four answered", (long)g_seen.count, 7);
    BW_CHECK_EQ("none waits", (long)bw_mod_web_waiting(), 0);
    bw_mod_web_set_stream(NULL);
    return 0;
}

/* ------------------------------------------------------------------ */
/* The pop-ups                                                         */
/* ------------------------------------------------------------------ */

static int test_popup(void)
{
    const char *title = NULL, *text = NULL;
    u32 icon = 99, serial = 0, serial2 = 0;
    char long_text[300];
    int i;

    bw_mod_popup_clear();
    BW_CHECK_EQ("none waits", (long)bw_mod_popup_peek(NULL, NULL, NULL, NULL), 0);
    BW_CHECK_EQ("an empty one is refused", (long)bw_mod_popup("", "", BW_POPUP_INFO), -1);
    BW_CHECK_EQ("one", (long)bw_mod_popup("Achievement", "First badge", BW_POPUP_TROPHY), 0);
    BW_CHECK_EQ("another", (long)bw_mod_popup("Offline", "", BW_POPUP_WARNING), 0);
    BW_CHECK_EQ("the first shows", (long)bw_mod_popup_peek(&title, &text, &icon, &serial), 1);
    BW_CHECK_EQ("its title", strcmp(title, "Achievement"), 0);
    BW_CHECK_EQ("its text", strcmp(text, "First badge"), 0);
    BW_CHECK_EQ("its icon", (long)icon, (long)BW_POPUP_TROPHY);
    bw_mod_popup_done();
    BW_CHECK_EQ("then the second", (long)bw_mod_popup_peek(&title, NULL, &icon, &serial2), 1);
    BW_CHECK_EQ("its title", strcmp(title, "Offline"), 0);
    BW_CHECK_EQ("the serial changed", serial2 != serial, 1);
    bw_mod_popup_done();
    for (i = 0; i < 8; i++) {
        BW_CHECK_EQ("eight fit", (long)bw_mod_popup("t", "x", BW_POPUP_NONE), 0);
    }
    BW_CHECK_EQ("a ninth is refused", (long)bw_mod_popup("t", "x", BW_POPUP_NONE), -1);
    bw_mod_popup_clear();
    /* a text of two-byte characters past the limit is cut between them */
    for (i = 0; i + 1 < (int)sizeof long_text - 1; i += 2) {
        long_text[i] = (char)0xC3;
        long_text[i + 1] = (char)0xA9;
    }
    long_text[i] = '\0';
    BW_CHECK_EQ("a long text", (long)bw_mod_popup("t", long_text, 77u), 0);
    BW_CHECK_EQ("shows", (long)bw_mod_popup_peek(NULL, &text, &icon, NULL), 1);
    BW_CHECK_EQ("cut on a whole character", strlen(text) % 2u == 0 && strlen(text) < BW_POPUP_TEXT_MAX, 1);
    BW_CHECK_EQ("an unknown icon is none", (long)icon, (long)BW_POPUP_NONE);
    bw_mod_popup_clear();
    return 0;
}

int main(void)
{
    if (test_md5() != 0 || test_hash() != 0 || test_clock() != 0 || test_web() != 0 || test_popup() != 0) {
        printf("a part stopped at a failed requirement\n");
    }
    BW_REPORT("test_mod_v7_host");
}
