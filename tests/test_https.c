/*
 * tests/test_https.c -- the https download (sdk/net_https.c) and the JSON
 * reader (sdk/json_read.c), with no network.
 *
 * The download speaks HTTP over a stream it is given; here the stream is a
 * list of canned answers, one per connection, handed out a few bytes at a
 * time so every parser meets its input split at awkward places.
 *
 * What is checked:
 *
 *   - an address is split into host, port and path, and a bad one refused;
 *   - a body with a stated length, a chunked body and a body that ends
 *     when the server closes all reach the sink whole;
 *   - the request names the path and the host and asks for no encoding;
 *   - a redirect is followed (an absolute address, a path on the same
 *     host), a redirect to http is refused, and a loop stops at the limit;
 *   - a connection whose answer was read whole serves the next request to
 *     the same server; one the server closed meanwhile is opened again; an
 *     answer that says "close", or an HTTP/1.0 one, is not kept;
 *   - a body past the limit stops, by its stated length before one byte
 *     and by its real length when none was stated;
 *   - a server that closes early, stops answering or answers 404 or 503
 *     fails with a reason, and so do a sink that refuses and a stop flag;
 *   - the JSON reader takes the mods index and a mod.json, reads escapes,
 *     and refuses broken text.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "json_read.h"
#include "net_https.h"
#include "types.h"

#include "port_test.h"

/* ------------------------------------------------------------------ */
/* The canned stream                                                   */
/* ------------------------------------------------------------------ */

typedef struct Answer {
    const char *bytes;                  /* what the server sends           */
    int         step;                   /* bytes handed out per read       */
    int         end;                    /* after them: 0 closed, -1 broken, -2 silence */
} Answer;

static struct {
    const Answer *list;
    int   count;
    int   opened;                       /* connections made so far         */
    size_t at;                          /* bytes of the current answer out */
    char  host[8][128];                 /* the host of each connection     */
    u16   port[8];
    char  sent[8][4096];                /* the request of each connection  */
    int   refuse_open;
    int   kept;                         /* the last connection was handed back to keep */
    int   reused;                       /* how many opens were given the kept one      */
} g_fake;

static int fake_open(void *user, const char *host, u16 port, int fresh, char *err, size_t err_size)
{
    (void)user;
    if (g_fake.kept && !fresh && g_fake.opened > 0 && strcmp(g_fake.host[g_fake.opened - 1], host) == 0 &&
        g_fake.port[g_fake.opened - 1] == port) {
        g_fake.kept = 0;
        g_fake.reused++;
        return 1;                       /* the same connection goes on */
    }
    g_fake.kept = 0;
    if (g_fake.refuse_open || g_fake.opened >= g_fake.count || g_fake.opened >= 8) {
        snprintf(err, err_size, "no connection to %s", host);
        return -1;
    }
    snprintf(g_fake.host[g_fake.opened], sizeof(g_fake.host[0]), "%s", host);
    g_fake.port[g_fake.opened] = port;
    g_fake.sent[g_fake.opened][0] = '\0';
    g_fake.opened++;
    g_fake.at = 0;
    return 0;
}

static int fake_send(void *user, const void *buf, u32 size)
{
    char *s = g_fake.sent[g_fake.opened - 1];
    size_t n = strlen(s);

    (void)user;
    if (n + size + 1 > sizeof(g_fake.sent[0])) {
        return -1;
    }
    memcpy(s + n, buf, size);
    s[n + size] = '\0';
    return 0;
}

static int fake_recv(void *user, void *buf, u32 size, int timeout_ms)
{
    const Answer *a = &g_fake.list[g_fake.opened - 1];
    size_t left = strlen(a->bytes) - g_fake.at;
    size_t n = (size_t)a->step;

    (void)user;
    (void)timeout_ms;
    if (left == 0) {
        return a->end;
    }
    if (n > left) {
        n = left;
    }
    if (n > size) {
        n = size;
    }
    memcpy(buf, a->bytes + g_fake.at, n);
    g_fake.at += n;
    return (int)n;
}

static void fake_close(void *user, int keep)
{
    (void)user;
    g_fake.kept = keep;
}

static const BwNetStream g_stream = { NULL, fake_open, fake_send, fake_recv, fake_close, NULL };

/* The body as the sink saw it. */
static char   g_body[70000];
static size_t g_body_n;
static int    g_sink_refuses_at = -1;
static unsigned long long g_seen_total;

static int sink(void *user, const void *data, u32 size)
{
    (void)user;
    if (g_sink_refuses_at >= 0 && g_body_n + size > (size_t)g_sink_refuses_at) {
        return -1;
    }
    if (g_body_n + size >= sizeof(g_body)) {
        return -1;
    }
    memcpy(g_body + g_body_n, data, size);
    g_body_n += size;
    g_body[g_body_n] = '\0';
    return 0;
}

static void progress(void *user, unsigned long long got, unsigned long long total)
{
    (void)user;
    (void)got;
    g_seen_total = total;
}

static BwHttpsGet g_get;
static volatile int g_cancel;

/* Get `url` from the stream as it stands (its kept connection too). */
static int get_next(const char *url, unsigned long long max)
{
    g_body_n = 0;
    g_body[0] = '\0';
    g_seen_total = 0;
    memset(&g_get, 0, sizeof(g_get));
    g_get.stream = &g_stream;
    g_get.max = max;
    g_get.sink = sink;
    g_get.progress = progress;
    g_get.cancel = &g_cancel;
    return bw_https_get(url, &g_get);
}

/* Get `url` against the answers, from a stream with no connection; the
 * call's result. */
static int get(const char *url, const Answer *list, int count, unsigned long long max)
{
    memset(&g_fake, 0, sizeof(g_fake));
    g_fake.list = list;
    g_fake.count = count;
    return get_next(url, max);
}

#define HEAD_OK "HTTP/1.1 200 OK\r\nServer: test\r\n"

/* ------------------------------------------------------------------ */
/* The parts                                                           */
/* ------------------------------------------------------------------ */

static void test_split(void)
{
    char host[64], path[128];
    u16 port = 0;

    BW_CHECK_EQ("split plain", bw_https_split("https://Example.org/a/b?c=1#frag", host, sizeof(host), &port,
                                              path, sizeof(path)), 0);
    BW_CHECK_EQ("split plain host", strcmp(host, "example.org"), 0);
    BW_CHECK_EQ("split plain port", port, 443);
    BW_CHECK_EQ("split plain path", strcmp(path, "/a/b?c=1"), 0);
    BW_CHECK_EQ("split port", bw_https_split("HTTPS://h:8443", host, sizeof(host), &port, path, sizeof(path)), 0);
    BW_CHECK_EQ("split port value", port, 8443);
    BW_CHECK_EQ("split no path", strcmp(path, "/"), 0);
    BW_CHECK_EQ("split query only", bw_https_split("https://h?x=1", host, sizeof(host), &port, path, sizeof(path)), 0);
    BW_CHECK_EQ("split query only path", strcmp(path, "/?x=1"), 0);
    BW_CHECK_EQ("split http", bw_https_split("http://h/a", host, sizeof(host), &port, path, sizeof(path)), -1);
    BW_CHECK_EQ("split user", bw_https_split("https://u@h/a", host, sizeof(host), &port, path, sizeof(path)), -1);
    BW_CHECK_EQ("split no host", bw_https_split("https:///a", host, sizeof(host), &port, path, sizeof(path)), -1);
    BW_CHECK_EQ("split port 0", bw_https_split("https://h:0/a", host, sizeof(host), &port, path, sizeof(path)), -1);
    BW_CHECK_EQ("split port big", bw_https_split("https://h:70000/a", host, sizeof(host), &port, path, sizeof(path)), -1);
    BW_CHECK_EQ("split space", bw_https_split("https://h/a b", host, sizeof(host), &port, path, sizeof(path)), -1);
    BW_CHECK_EQ("split line break", bw_https_split("https://h/a\r\nX: y", host, sizeof(host), &port, path, sizeof(path)), -1);
    BW_CHECK_EQ("split small host", bw_https_split("https://example.org/", host, 4, &port, path, sizeof(path)), -1);
}

static void test_bodies(void)
{
    static const Answer length[] = {
        { HEAD_OK "Content-Length: 11\r\n\r\nhello world", 3, 0 } };
    static const Answer length_extra[] = {
        { HEAD_OK "content-length:5\r\n\r\nhelloEXTRA", 100, 0 } };
    static const Answer chunked[] = {
        { HEAD_OK "Transfer-Encoding: Chunked\r\n\r\n"
          "5\r\nhello\r\n1;ext=1\r\n \r\nA\r\nworld12345\r\n0\r\nTrailer: x\r\n\r\n", 1, 0 } };
    static const Answer to_close[] = {
        { "HTTP/1.0 200 OK\r\n\r\nall of it", 4, 0 } };
    static const Answer empty[] = {
        { HEAD_OK "Content-Length: 0\r\n\r\n", 7, 0 } };

    BW_CHECK_EQ("length", get("https://files.test/a/b.json", length, 1, 100), 0);
    BW_CHECK_EQ("length body", strcmp(g_body, "hello world"), 0);
    BW_CHECK_EQ("length total", (long)g_seen_total, 11);
    BW_CHECK_EQ("length status", g_get.status, 200);
    BW_CHECK_EQ("request line", strncmp(g_fake.sent[0], "GET /a/b.json HTTP/1.1\r\n", 24), 0);
    BW_CHECK_EQ("request host", strstr(g_fake.sent[0], "\r\nHost: files.test\r\n") != NULL, 1);
    BW_CHECK_EQ("request identity", strstr(g_fake.sent[0], "\r\nAccept-Encoding: identity\r\n") != NULL, 1);
    BW_CHECK_EQ("request ends", strstr(g_fake.sent[0], "\r\nAccept-Encoding: identity\r\n\r\n") != NULL, 1);
    BW_CHECK_EQ("connect host", strcmp(g_fake.host[0], "files.test"), 0);
    BW_CHECK_EQ("connect port", g_fake.port[0], 443);

    BW_CHECK_EQ("length, bytes after it", get("https://h/x", length_extra, 1, 100), 0);
    BW_CHECK_EQ("length, bytes after it: body", strcmp(g_body, "hello"), 0);

    BW_CHECK_EQ("chunked", get("https://h/x", chunked, 1, 100), 0);
    BW_CHECK_EQ("chunked body", strcmp(g_body, "hello world12345"), 0);

    BW_CHECK_EQ("to close", get("https://h/x", to_close, 1, 100), 0);
    BW_CHECK_EQ("to close body", strcmp(g_body, "all of it"), 0);

    BW_CHECK_EQ("empty", get("https://h/x", empty, 1, 100), 0);
    BW_CHECK_EQ("empty body", (long)g_body_n, 0);
}

static void test_redirects(void)
{
    static const Answer hops[] = {
        { "HTTP/1.1 302 Found\r\nLocation: /releases/download/v1.0.0/mod.json\r\nContent-Length: 0\r\n\r\n", 5, 0 },
        { "HTTP/1.1 302 Found\r\nlocation:   https://assets.test:444/signed?se=2026&sig=ab%2Fcd  \r\n\r\n", 9, 0 },
        { HEAD_OK "Content-Length: 2\r\n\r\n{}", 2, 0 } };
    static const Answer to_http[] = {
        { "HTTP/1.1 301 Moved\r\nLocation: http://plain.test/x\r\n\r\n", 50, 0 } };
    static const Answer no_location[] = {
        { "HTTP/1.1 302 Found\r\nContent-Length: 0\r\n\r\n", 50, 0 } };
    static Answer loop[8];
    int i;

    BW_CHECK_EQ("redirects", get("https://github.test/o/r/releases/latest/download/mod.json", hops, 3, 100), 0);
    BW_CHECK_EQ("redirects body", strcmp(g_body, "{}"), 0);
    BW_CHECK_EQ("redirects connections", g_fake.opened, 3);
    BW_CHECK_EQ("redirect 1 host", strcmp(g_fake.host[1], "github.test"), 0);
    BW_CHECK_EQ("redirect 1 path", strncmp(g_fake.sent[1], "GET /releases/download/v1.0.0/mod.json HTTP/1.1\r\n", 49), 0);
    BW_CHECK_EQ("redirect 2 host", strcmp(g_fake.host[2], "assets.test"), 0);
    BW_CHECK_EQ("redirect 2 port", g_fake.port[2], 444);
    BW_CHECK_EQ("redirect 2 path", strncmp(g_fake.sent[2], "GET /signed?se=2026&sig=ab%2Fcd HTTP/1.1\r\n", 42), 0);

    BW_CHECK_EQ("redirect to http", get("https://h/x", to_http, 1, 100), -1);
    BW_CHECK_EQ("redirect to http: one connection", g_fake.opened, 1);
    BW_CHECK_EQ("redirect to http: said", g_get.err[0] != '\0', 1);

    BW_CHECK_EQ("redirect without address", get("https://h/x", no_location, 1, 100), -1);

    for (i = 0; i < 8; i++) {
        loop[i].bytes = "HTTP/1.1 302 Found\r\nLocation: /again\r\n\r\n";
        loop[i].step = 50;
        loop[i].end = 0;
    }
    BW_CHECK_EQ("redirect loop", get("https://h/x", loop, 8, 100), -1);
    BW_CHECK_EQ("redirect loop: connections", g_fake.opened, BW_HTTPS_REDIRECTS_MAX + 1);
    BW_CHECK_EQ("redirect loop: said", strcmp(g_get.err, "too many redirects"), 0);
}

static void test_kept(void)
{
    /* two answers on one connection, handed out a byte at a time so the
     * first read stops where its answer ends */
    static const Answer two[] = {
        { HEAD_OK "Content-Length: 5\r\n\r\nfirst"
          HEAD_OK "Transfer-Encoding: chunked\r\n\r\n6\r\nsecond\r\n0\r\n\r\n"
          HEAD_OK "Content-Length: 5\r\n\r\nthird", 1, 0 },
        { HEAD_OK "Content-Length: 5\r\n\r\nfresh", 1, 0 } };
    static const Answer says_close[] = {
        { HEAD_OK "Connection: Close\r\nContent-Length: 3\r\n\r\none", 1, 0 },
        { HEAD_OK "Content-Length: 3\r\n\r\ntwo", 1, 0 } };
    static const Answer old_http[] = {
        { "HTTP/1.0 200 OK\r\nContent-Length: 3\r\n\r\none", 1, 0 },
        { HEAD_OK "Content-Length: 3\r\n\r\ntwo", 1, 0 } };
    static const Answer more_than_asked[] = {
        { HEAD_OK "Content-Length: 3\r\n\r\noneEXTRA", 100, 0 },
        { HEAD_OK "Content-Length: 3\r\n\r\ntwo", 1, 0 } };

    BW_CHECK_EQ("kept: first", get("https://h/1", two, 2, 100), 0);
    BW_CHECK_EQ("kept: first body", strcmp(g_body, "first"), 0);
    BW_CHECK_EQ("kept: second", get_next("https://h/2", 100), 0);
    BW_CHECK_EQ("kept: second body", strcmp(g_body, "second"), 0);
    BW_CHECK_EQ("kept: third", get_next("https://h/3", 100), 0);
    BW_CHECK_EQ("kept: third body", strcmp(g_body, "third"), 0);
    BW_CHECK_EQ("kept: one connection", g_fake.opened, 1);
    BW_CHECK_EQ("kept: given twice", g_fake.reused, 2);
    BW_CHECK_EQ("kept: the three requests", strstr(g_fake.sent[0], "GET /1 ") != NULL &&
                                             strstr(g_fake.sent[0], "GET /2 ") != NULL &&
                                             strstr(g_fake.sent[0], "GET /3 ") != NULL, 1);
    /* the server closed the kept connection meanwhile: opened again */
    BW_CHECK_EQ("kept, then closed by the server", get_next("https://h/4", 100), 0);
    BW_CHECK_EQ("kept, then closed: body", strcmp(g_body, "fresh"), 0);
    BW_CHECK_EQ("kept, then closed: a second connection", g_fake.opened, 2);
    /* another server: not the kept connection */
    BW_CHECK_EQ("another server", get("https://h/1", two, 2, 100) == 0 && get_next("https://other/2", 100) == 0, 1);
    BW_CHECK_EQ("another server: a second connection", g_fake.opened, 2);
    BW_CHECK_EQ("another server: body", strcmp(g_body, "fresh"), 0);

    BW_CHECK_EQ("says close", get("https://h/1", says_close, 2, 100) == 0 && get_next("https://h/2", 100) == 0, 1);
    BW_CHECK_EQ("says close: not kept", g_fake.reused, 0);
    BW_CHECK_EQ("says close: body", strcmp(g_body, "two"), 0);
    BW_CHECK_EQ("HTTP/1.0", get("https://h/1", old_http, 2, 100) == 0 && get_next("https://h/2", 100) == 0, 1);
    BW_CHECK_EQ("HTTP/1.0: not kept", g_fake.reused, 0);
    BW_CHECK_EQ("bytes after the answer", get("https://h/1", more_than_asked, 2, 100) == 0 &&
                                            get_next("https://h/2", 100) == 0, 1);
    BW_CHECK_EQ("bytes after the answer: not kept", g_fake.reused, 0);
    BW_CHECK_EQ("bytes after the answer: body", strcmp(g_body, "two"), 0);
}

static void test_limits(void)
{
    static const Answer stated[] = {
        { HEAD_OK "Content-Length: 12\r\n\r\nhello world!", 100, 0 } };
    static const Answer unstated[] = {
        { HEAD_OK "\r\nhello world!", 4, 0 } };
    static const Answer chunked[] = {
        { HEAD_OK "Transfer-Encoding: chunked\r\n\r\nC\r\nhello world!\r\n0\r\n\r\n", 6, 0 } };

    BW_CHECK_EQ("past the limit, stated", get("https://h/x", stated, 1, 11), -1);
    BW_CHECK_EQ("past the limit, stated: nothing kept", (long)g_body_n, 0);
    BW_CHECK_EQ("at the limit, stated", get("https://h/x", stated, 1, 12), 0);
    BW_CHECK_EQ("past the limit, unstated", get("https://h/x", unstated, 1, 11), -1);
    BW_CHECK_EQ("past the limit, unstated: within it", g_body_n <= 11, 1);
    BW_CHECK_EQ("past the limit, chunked", get("https://h/x", chunked, 1, 11), -1);
    BW_CHECK_EQ("at the limit, chunked", get("https://h/x", chunked, 1, 12), 0);
}

static void test_failures(void)
{
    static const Answer early[] = {
        { HEAD_OK "Content-Length: 20\r\n\r\nonly this", 3, 0 } };
    static const Answer broken[] = {
        { HEAD_OK "\r\nsome of it", 3, -1 } };
    static const Answer silent[] = {
        { HEAD_OK "Content-Length: 20\r\n\r\nonly this", 3, -2 } };
    static const Answer silent_head[] = {
        { "HTTP/1.1 200 OK\r\nServ", 3, -2 } };
    static const Answer missing[] = {
        { "HTTP/1.1 404 Not Found\r\nContent-Length: 9\r\n\r\nNot Found", 3, 0 } };
    static const Answer down[] = {
        { "HTTP/1.1 503 Service Unavailable\r\n\r\n", 3, 0 } };
    static const Answer not_http[] = {
        { "SSH-2.0-OpenSSH\r\n\r\n", 3, 0 } };
    static const Answer bad_chunk[] = {
        { HEAD_OK "Transfer-Encoding: chunked\r\n\r\nZZ\r\nhello\r\n0\r\n\r\n", 3, 0 } };
    static const Answer chunk_cut[] = {
        { HEAD_OK "Transfer-Encoding: chunked\r\n\r\n5\r\nhel", 3, 0 } };
    static const Answer ok[] = {
        { HEAD_OK "Content-Length: 11\r\n\r\nhello world", 3, 0 } };
    static char big_head[20000];

    BW_CHECK_EQ("closed early", get("https://h/x", early, 1, 100), -1);
    BW_CHECK_EQ("closed early: said", strcmp(g_get.err, "the server closed the connection early"), 0);
    BW_CHECK_EQ("broken", get("https://h/x", broken, 1, 100), -1);
    BW_CHECK_EQ("silent", get("https://h/x", silent, 1, 100), -1);
    BW_CHECK_EQ("silent: said", strcmp(g_get.err, "the server stopped answering"), 0);
    BW_CHECK_EQ("silent in the header", get("https://h/x", silent_head, 1, 100), -1);
    BW_CHECK_EQ("404", get("https://h/x", missing, 1, 100), -1);
    BW_CHECK_EQ("404: status", g_get.status, 404);
    BW_CHECK_EQ("404: said", strcmp(g_get.err, "the server answered 404"), 0);
    BW_CHECK_EQ("404: no body kept", (long)g_body_n, 0);
    BW_CHECK_EQ("503", get("https://h/x", down, 1, 100), -1);
    BW_CHECK_EQ("503: status", g_get.status, 503);
    BW_CHECK_EQ("not http", get("https://h/x", not_http, 1, 100), -1);
    BW_CHECK_EQ("bad chunk size", get("https://h/x", bad_chunk, 1, 100), -1);
    BW_CHECK_EQ("chunk cut short", get("https://h/x", chunk_cut, 1, 100), -1);
    BW_CHECK_EQ("no server", get("https://h/x", ok, 0, 100), -1);
    BW_CHECK_EQ("no server: said", strcmp(g_get.err, "no connection to h"), 0);
    BW_CHECK_EQ("not https", get("http://h/x", ok, 1, 100), -1);
    BW_CHECK_EQ("not https: no connection", g_fake.opened, 0);

    g_sink_refuses_at = 4;
    BW_CHECK_EQ("the sink refuses", get("https://h/x", ok, 1, 100), -1);
    g_sink_refuses_at = -1;

    g_cancel = 1;
    BW_CHECK_EQ("stopped", get("https://h/x", ok, 1, 100), -1);
    BW_CHECK_EQ("stopped: said", strcmp(g_get.err, "stopped"), 0);
    g_cancel = 0;

    /* a header block past the limit */
    {
        Answer a[1];
        size_t n = (size_t)snprintf(big_head, sizeof(big_head), "HTTP/1.1 200 OK\r\nX-Pad: ");

        memset(big_head + n, 'a', BW_HTTPS_HEAD_MAX + 100);
        strcpy(big_head + n + BW_HTTPS_HEAD_MAX + 100, "\r\n\r\nbody");
        a[0].bytes = big_head;
        a[0].step = 1000;
        a[0].end = 0;
        BW_CHECK_EQ("header past the limit", get("https://h/x", a, 1, 100), -1);
    }
}

static void test_json(void)
{
    static const char index[] =
        "{\n  \"format\": 1,\n  \"mods\": [\n"
        "    { \"mod\": \"nuzlocke\", \"version\": \"1.1.0\" },\n"
        "    { \"repo\": \"Project-Holon/arkhipelagos\" }\n  ]\n}\n";
    static const char mod[] =
        "{\"format\":1,\"id\":\"someone/some-mod\",\"name\":\"Caf\\u00e9 \\\"Mod\\\" \\ud83d\\ude00\","
        "\"games\":[\"black\",\"white\"],\"has_code\":false,\"official\":true,"
        "\"package\":{\"file\":\"mod.bw\",\"size\":612,\"sha256\":\"ab\\/cd\"},\"api\":7,\"x\":-1.5e3,\"n\":null}";
    BwJson root, v, w;
    unsigned long long n = 0;
    char text[64];
    int b = 0;

    BW_CHECK_EQ("index parses", bw_json_parse(index, strlen(index), &root), 0);
    BW_CHECK_EQ("index is an object", bw_json_kind(root), BW_JSON_OBJECT);
    BW_CHECK_EQ("index format", bw_json_member(root, "format", &v) && bw_json_u64(v, &n) == 0 && n == 1, 1);
    BW_CHECK_EQ("index mods", bw_json_member(root, "mods", &v), 1);
    BW_CHECK_EQ("index mods count", (long)bw_json_count(v), 2);
    BW_CHECK_EQ("index mod 0", bw_json_item(v, 0, &w) && bw_json_member(w, "mod", &w) &&
                               bw_json_string(w, text, sizeof(text)) == 0 && strcmp(text, "nuzlocke") == 0, 1);
    BW_CHECK_EQ("index mod 1", bw_json_item(v, 1, &w) && bw_json_member(w, "repo", &w) &&
                               bw_json_string(w, text, sizeof(text)) == 0 &&
                               strcmp(text, "Project-Holon/arkhipelagos") == 0, 1);
    BW_CHECK_EQ("index mod 1 has no version", bw_json_item(v, 1, &w) && !bw_json_member(w, "version", &w), 1);
    BW_CHECK_EQ("index mod 2", bw_json_item(v, 2, &w), 0);

    BW_CHECK_EQ("mod parses", bw_json_parse(mod, strlen(mod), &root), 0);
    BW_CHECK_EQ("mod name", bw_json_member(root, "name", &v) && bw_json_string(v, text, sizeof(text)) == 0, 1);
    BW_CHECK_EQ("mod name text", strcmp(text, "Caf\xC3\xA9 \"Mod\" \xF0\x9F\x98\x80"), 0);
    BW_CHECK_EQ("mod name cut", bw_json_string(v, text, 5), 1);
    BW_CHECK_EQ("mod name cut at a character", strcmp(text, "Caf"), 0);
    BW_CHECK_EQ("mod has_code", bw_json_member(root, "has_code", &v) && bw_json_bool(v, &b) == 0 && b == 0, 1);
    BW_CHECK_EQ("mod official", bw_json_member(root, "official", &v) && bw_json_bool(v, &b) == 0 && b == 1, 1);
    BW_CHECK_EQ("mod package size", bw_json_member(root, "package", &v) && bw_json_member(v, "size", &w) &&
                                    bw_json_u64(w, &n) == 0 && n == 612, 1);
    BW_CHECK_EQ("mod package sha", bw_json_member(v, "sha256", &w) && bw_json_string(w, text, sizeof(text)) == 0 &&
                                   strcmp(text, "ab/cd") == 0, 1);
    BW_CHECK_EQ("mod games", bw_json_member(root, "games", &v) && bw_json_count(v) == 2, 1);
    BW_CHECK_EQ("a fraction is no whole number", bw_json_member(root, "x", &v) && bw_json_u64(v, &n) == -1, 1);
    BW_CHECK_EQ("null", bw_json_member(root, "n", &v) && bw_json_kind(v) == BW_JSON_NULL, 1);
    BW_CHECK_EQ("a string is no number", bw_json_member(root, "id", &v) && bw_json_u64(v, &n) == -1, 1);
    BW_CHECK_EQ("a number is no string", bw_json_member(root, "api", &v) && bw_json_string(v, text, sizeof(text)) == -1, 1);
    BW_CHECK_EQ("no such member", bw_json_member(root, "nam", &v), 0);

    BW_CHECK_EQ("empty text", bw_json_parse("", 0, &root), -1);
    BW_CHECK_EQ("open object", bw_json_parse("{\"a\":1", 6, &root), -1);
    BW_CHECK_EQ("trailing comma", bw_json_parse("[1,]", 4, &root), -1);
    BW_CHECK_EQ("two values", bw_json_parse("1 2", 3, &root), -1);
    BW_CHECK_EQ("bad escape", bw_json_parse("\"\\q\"", 4, &root), -1);
    BW_CHECK_EQ("bad unicode escape", bw_json_parse("\"\\u12g4\"", 8, &root), -1);
    BW_CHECK_EQ("leading zero", bw_json_parse("01", 2, &root), -1);
    BW_CHECK_EQ("a line break in a string", bw_json_parse("\"a\nb\"", 5, &root), -1);
    BW_CHECK_EQ("a NUL in the text", bw_json_parse("[1,\0 2]", 7, &root), -1);
    BW_CHECK_EQ("an html page", bw_json_parse("<!DOCTYPE html>", 15, &root), -1);
    {
        char deep[200];
        int i;

        for (i = 0; i < 40; i++) {
            deep[i] = '[';
            deep[79 - i] = ']';
        }
        BW_CHECK_EQ("nested too deep", bw_json_parse(deep, 80, &root), -1);
        BW_CHECK_EQ("nested within the limit", bw_json_parse(deep + 10, 60, &root), 0);
    }
    BW_CHECK_EQ("a number past 64 bits", bw_json_parse("99999999999999999999", 20, &root) == 0 &&
                                         bw_json_u64(root, &n) == -1, 1);
}

/* Whether any request sent on the connections holds `text`. */
static int sent_has(const char *text)
{
    int i;

    for (i = 0; i < g_fake.opened && i < 8; i++) {
        if (strstr(g_fake.sent[i], text) != NULL) {
            return 1;
        }
    }
    return 0;
}

/* A request with a body: a POST with its type and length, the body after
 * the head; a 303 goes on as a GET, a 307 sends the body again; with
 * any_status an error answer's body reaches the sink. */
static void test_post(void)
{
    static const Answer ok[] = {
        { HEAD_OK "Content-Length: 7\r\n\r\n{\"a\":1}", 5, 0 },
    };
    static const Answer see_other[] = {
        { "HTTP/1.1 303 See Other\r\nLocation: /done\r\nContent-Length: 0\r\n\r\n", 9, 0 },
        { HEAD_OK "Content-Length: 2\r\n\r\nok", 3, 0 },
    };
    static const Answer temporary[] = {
        { "HTTP/1.1 307 Temporary Redirect\r\nLocation: /again\r\nContent-Length: 0\r\n\r\n", 9, 0 },
        { HEAD_OK "Content-Length: 2\r\n\r\nok", 3, 0 },
    };
    static const Answer refused[] = {
        { "HTTP/1.1 401 Unauthorized\r\nContent-Length: 13\r\n\r\n{\"Error\":\"x\"}", 4, 0 },
    };
    static const char body[] = "u=player&t=token";

    memset(&g_fake, 0, sizeof(g_fake));
    g_fake.list = ok;
    g_fake.count = 1;
    g_body_n = 0;
    memset(&g_get, 0, sizeof(g_get));
    g_get.stream = &g_stream;
    g_get.max = 1000;
    g_get.sink = sink;
    g_get.body = body;
    g_get.body_size = (u32)strlen(body);
    g_get.content_type = "application/x-www-form-urlencoded";
    BW_CHECK_EQ("a POST", (long)bw_https_get("https://retroachievements.org/dorequest.php", &g_get), 0);
    BW_CHECK_EQ("is a POST of the path", strncmp(g_fake.sent[0], "POST /dorequest.php HTTP/1.1\r\n", 30), 0);
    BW_CHECK_EQ("with its type", strstr(g_fake.sent[0], "\r\nContent-Type: application/x-www-form-urlencoded\r\n") != NULL, 1);
    BW_CHECK_EQ("and its length", strstr(g_fake.sent[0], "\r\nContent-Length: 16\r\n") != NULL, 1);
    BW_CHECK_EQ("the body after the head", strstr(g_fake.sent[0], "\r\n\r\nu=player&t=token") != NULL, 1);
    BW_CHECK_EQ("the answer reached the sink", strcmp(g_body, "{\"a\":1}"), 0);

    memset(&g_fake, 0, sizeof(g_fake));
    g_fake.list = see_other;
    g_fake.count = 2;
    g_body_n = 0;
    BW_CHECK_EQ("a POST answered 303", (long)bw_https_get("https://h.test/p", &g_get), 0);
    BW_CHECK_EQ("goes on as a GET", sent_has("GET /done HTTP/1.1\r\n"), 1);
    {
        const char *get = NULL;
        int i;

        for (i = 0; i < g_fake.opened && i < 8 && get == NULL; i++) {
            get = strstr(g_fake.sent[i], "GET /done HTTP/1.1\r\n");
        }
        BW_CHECK_EQ("with no body", get != NULL && strstr(get, "Content-") == NULL
                                    && strstr(get, "u=player") == NULL, 1);
    }

    memset(&g_fake, 0, sizeof(g_fake));
    g_fake.list = temporary;
    g_fake.count = 2;
    g_body_n = 0;
    BW_CHECK_EQ("a POST answered 307", (long)bw_https_get("https://h.test/p", &g_get), 0);
    BW_CHECK_EQ("is sent again as a POST", sent_has("POST /again HTTP/1.1\r\n"), 1);
    BW_CHECK_EQ("with its body", sent_has("\r\n\r\nu=player&t=token"), 1);

    memset(&g_fake, 0, sizeof(g_fake));
    g_fake.list = refused;
    g_fake.count = 1;
    g_body_n = 0;
    BW_CHECK_EQ("a 401 fails without any_status", (long)bw_https_get("https://h.test/p", &g_get), -1);
    memset(&g_fake, 0, sizeof(g_fake));
    g_fake.list = refused;
    g_fake.count = 1;
    g_body_n = 0;
    g_body[0] = '\0';
    g_get.any_status = 1;
    BW_CHECK_EQ("with any_status it answers", (long)bw_https_get("https://h.test/p", &g_get), 0);
    BW_CHECK_EQ("with the status", (long)g_get.status, 401);
    BW_CHECK_EQ("and the body", strcmp(g_body, "{\"Error\":\"x\"}"), 0);

    g_get.content_type = "text/plain\r\nX-Evil: 1";
    BW_CHECK_EQ("a type of two lines is refused", (long)bw_https_get("https://h.test/p", &g_get), -1);
}

int main(void)
{
    test_split();
    test_bodies();
    test_redirects();
    test_kept();
    test_limits();
    test_failures();
    test_post();
    test_json();
    BW_REPORT("test_https");
}
