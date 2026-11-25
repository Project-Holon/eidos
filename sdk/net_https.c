/*
 * sdk/net_https.c -- a file from an https address (sdk/net_https.h).
 *
 * HTTP/1.1, one request at a time, then the next address when the answer
 * is a redirect.  A connection whose answer was read whole is handed back
 * to the stream to keep for the next request.  The stream under it
 * (BwNetStream) does the TLS; this file only speaks HTTP, so the tests
 * drive it with canned answers and no network.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "net_https.h"

/* The bytes held between the stream and the parser. */
#define BUF_SIZE (32u * 1024u)

/* How long one read waits before the stop flag is looked at again. */
#define SLICE_MS 250

/* What a read answers besides a count (the stream's own, and two more). */
enum { RD_CLOSED = 0, RD_BROKEN = -1, RD_IDLE = -2, RD_STOP = -3 };

typedef struct Conn {
    const BwNetStream *st;
    BwHttpsGet *g;
    u8  *buf;
    u32  n;                             /* bytes held                     */
    u32  pos;                           /* bytes of them already used      */
    unsigned long long got;             /* body bytes given to the sink    */
    unsigned long long total;           /* the stated length, 0: none      */
    int  post;                          /* this request sends g->body      */
} Conn;

static void say(BwHttpsGet *g, const char *text)
{
    if (g->err[0] == '\0') {
        snprintf(g->err, sizeof(g->err), "%s", text);
    }
}

static int stopped(const BwHttpsGet *g)
{
    return g->cancel != NULL && *g->cancel != 0;
}

static void say_read(BwHttpsGet *g, int rc)
{
    say(g, rc == RD_STOP ? "stopped" :
           rc == RD_IDLE ? "the server stopped answering" :
           rc == RD_CLOSED ? "the server closed the connection early" :
                             "the connection broke");
}

/* More bytes after the ones held: the count, or RD_*. */
static int more(Conn *c)
{
    int waited = 0;

    if (c->pos > 0) {
        memmove(c->buf, c->buf + c->pos, c->n - c->pos);
        c->n -= c->pos;
        c->pos = 0;
    }
    if (c->n >= BUF_SIZE) {
        return RD_BROKEN;               /* (a line longer than the buffer) */
    }
    for (;;) {
        int rc;

        if (stopped(c->g)) {
            return RD_STOP;
        }
        rc = c->st->recv(c->st->user, c->buf + c->n, BUF_SIZE - c->n, SLICE_MS);
        if (rc > 0) {
            c->n += (u32)rc;
            return rc;
        }
        if (rc != RD_IDLE) {
            return rc < 0 ? RD_BROKEN : RD_CLOSED;
        }
        waited += SLICE_MS;
        if (waited >= BW_HTTPS_IDLE_MS) {
            return RD_IDLE;
        }
    }
}

/* Body bytes to the sink.  0, or -1 (the reason said). */
static int emit(Conn *c, const u8 *data, u32 size)
{
    BwHttpsGet *g = c->g;

    if (size == 0) {
        return 0;
    }
    if (c->got + size > g->max) {
        snprintf(g->err, sizeof(g->err), "the file is larger than %llu bytes", g->max);
        return -1;
    }
    if (g->sink != NULL && g->sink(g->user, data, size) != 0) {
        say(g, "the file could not be kept");
        return -1;
    }
    c->got += size;
    if (g->progress != NULL) {
        g->progress(g->user, c->got, c->total);
    }
    return 0;
}

/* The held bytes, up to `limit` of them, to the sink. */
static int emit_held(Conn *c, unsigned long long limit, u32 *used)
{
    u32 n = c->n - c->pos;

    if ((unsigned long long)n > limit) {
        n = (u32)limit;
    }
    *used = n;
    if (emit(c, c->buf + c->pos, n) != 0) {
        return -1;
    }
    c->pos += n;
    return 0;
}

/* `size` bytes of body, from the held bytes and then the stream. */
static int body_exact(Conn *c, unsigned long long size)
{
    while (size > 0) {
        u32 used;
        int rc;

        if (emit_held(c, size, &used) != 0) {
            return -1;
        }
        size -= used;
        if (size == 0) {
            break;
        }
        rc = more(c);
        if (rc <= 0) {
            say_read(c->g, rc);
            return -1;
        }
    }
    return 0;
}

/* The body until the server closes the stream. */
static int body_to_close(Conn *c)
{
    for (;;) {
        u32 used;
        int rc;

        if (emit_held(c, ~0ull, &used) != 0) {
            return -1;
        }
        rc = more(c);
        if (rc == RD_CLOSED) {
            return 0;
        }
        if (rc < 0) {
            say_read(c->g, rc);
            return -1;
        }
    }
}

/*
 * One line of the held bytes (up to its CR LF) as text in `out`, reading
 * more as needed.  0, or -1.
 */
static int take_line(Conn *c, char *out, size_t size)
{
    for (;;) {
        u32 i;
        int rc;

        for (i = c->pos; i + 1 < c->n; i++) {
            if (c->buf[i] == '\r' && c->buf[i + 1] == '\n') {
                u32 len = i - c->pos;

                if (len + 1 > size) {
                    say(c->g, "the server's answer does not read");
                    return -1;
                }
                memcpy(out, c->buf + c->pos, len);
                out[len] = '\0';
                c->pos = i + 2;
                return 0;
            }
        }
        if (c->n - c->pos >= size) {
            say(c->g, "the server's answer does not read");
            return -1;
        }
        rc = more(c);
        if (rc <= 0) {
            say_read(c->g, rc);
            return -1;
        }
    }
}

/* A body sent in chunks (RFC 9112, section 7.1). */
static int body_chunked(Conn *c)
{
    char line[80];

    for (;;) {
        unsigned long long size = 0;
        const char *p;
        int digits = 0;

        if (take_line(c, line, sizeof(line)) != 0) {
            return -1;
        }
        for (p = line; *p != '\0' && *p != ';' && *p != ' '; p++) {
            int d = (*p >= '0' && *p <= '9') ? *p - '0' :
                    (*p >= 'a' && *p <= 'f') ? *p - 'a' + 10 :
                    (*p >= 'A' && *p <= 'F') ? *p - 'A' + 10 : -1;

            if (d < 0 || ++digits > 15) {
                say(c->g, "the server's answer does not read");
                return -1;
            }
            size = size * 16ull + (unsigned long long)d;
        }
        if (digits == 0) {
            say(c->g, "the server's answer does not read");
            return -1;
        }
        if (size == 0) {
            /* the trailer lines, up to the empty one */
            do {
                if (take_line(c, line, sizeof(line)) != 0) {
                    return -1;
                }
            } while (line[0] != '\0');
            return 0;
        }
        if (body_exact(c, size) != 0) {
            return -1;
        }
        if (take_line(c, line, sizeof(line)) != 0) {
            return -1;
        }
        if (line[0] != '\0') {
            say(c->g, "the server's answer does not read");
            return -1;
        }
    }
}

static char lower(char ch)
{
    return (ch >= 'A' && ch <= 'Z') ? (char)(ch - 'A' + 'a') : ch;
}

/*
 * The value of the header `name` (lower case) in the header block `head`
 * (its lines end with CR LF; the block is NUL-terminated): 1 and the value
 * without the spaces around it, or 0.
 */
static int header(const char *head, const char *name, char *out, size_t size)
{
    size_t nl = strlen(name);
    const char *p = strstr(head, "\r\n");

    while (p != NULL && p[2] != '\0') {
        const char *line = p + 2;
        const char *end = strstr(line, "\r\n");
        size_t i;

        if (end == NULL) {
            end = line + strlen(line);
        }
        for (i = 0; i < nl && line + i < end && lower(line[i]) == name[i]; i++) {
        }
        if (i == nl && line + i < end && line[i] == ':') {
            const char *v = line + i + 1;
            size_t len;

            while (v < end && (*v == ' ' || *v == '\t')) {
                v++;
            }
            while (end > v && (end[-1] == ' ' || end[-1] == '\t')) {
                end--;
            }
            len = (size_t)(end - v);
            if (len + 1 > size) {
                return 0;
            }
            memcpy(out, v, len);
            out[len] = '\0';
            return 1;
        }
        p = strstr(line, "\r\n");
    }
    return 0;
}

int bw_https_split(const char *url, char *host, size_t host_size, u16 *port,
                   char *path, size_t path_size)
{
    static const char scheme[] = "https://";
    const char *p = url;
    size_t i, n = 0;
    unsigned long pt = 443;

    if (url == NULL || strlen(url) > BW_HTTPS_URL_MAX) {
        return -1;
    }
    for (i = 0; scheme[i] != '\0'; i++) {
        if (lower(p[i]) != scheme[i]) {
            return -1;
        }
    }
    p += i;
    while ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
           (*p >= '0' && *p <= '9') || *p == '-' || *p == '.') {
        if (n + 1 >= host_size) {
            return -1;
        }
        host[n++] = lower(*p++);
    }
    if (n == 0 || host[0] == '.' || host[0] == '-') {
        return -1;
    }
    host[n] = '\0';
    if (*p == ':') {
        int digits = 0;

        pt = 0;
        for (p++; *p >= '0' && *p <= '9'; p++) {
            pt = pt * 10ul + (unsigned long)(*p - '0');
            if (++digits > 5) {
                return -1;
            }
        }
        if (digits == 0 || pt == 0 || pt > 65535ul) {
            return -1;
        }
    }
    *port = (u16)pt;
    n = 0;
    if (*p != '/') {
        if (*p != '\0' && *p != '?' && *p != '#') {
            return -1;                  /* "user@", a bad host character */
        }
        if (path_size < 2) {
            return -1;
        }
        path[n++] = '/';
    }
    for (; *p != '\0' && *p != '#'; p++) {
        /* (a space or a control character would end the request line) */
        if ((unsigned char)*p <= 0x20 || (unsigned char)*p == 0x7F || n + 1 >= path_size) {
            return -1;
        }
        path[n++] = *p;
    }
    path[n] = '\0';
    return 0;
}

/* The address a redirect names, from the address that answered with it. */
static int redirect_to(const char *from_host, u16 from_port, const char *location,
                       char *out, size_t size)
{
    int n;

    if (location[0] == '/' && location[1] != '/') {
        n = from_port == 443 ? snprintf(out, size, "https://%s%s", from_host, location)
                             : snprintf(out, size, "https://%s:%u%s", from_host,
                                        (unsigned)from_port, location);
    } else {
        n = snprintf(out, size, "%s", location);
    }
    return (n > 0 && (size_t)n < size) ? 0 : -1;
}

/* Why a header block could not be taken (besides the RD_* of a read). */
#define HEAD_BAD (-9)

/*
 * The header block of the answer, up to its empty line, into the held
 * bytes: 1 with `*end` at the block's last CR LF, or RD_*, or HEAD_BAD.
 */
static int read_head(Conn *c, u32 *end)
{
    for (;;) {
        u32 i;
        int rc, found = 0;

        for (i = 0; i + 3 < c->n; i++) {
            if (memcmp(c->buf + i, "\r\n\r\n", 4) == 0) {
                *end = i;
                found = 1;
                break;
            }
        }
        if (found && *end <= BW_HTTPS_HEAD_MAX) {
            return 1;
        }
        if (found || c->n >= BW_HTTPS_HEAD_MAX) {
            return HEAD_BAD;
        }
        rc = more(c);
        if (rc <= 0) {
            return rc;
        }
    }
}

/* Does the header's value list name `word` (lower case)? */
static int names_word(const char *value, const char *word)
{
    size_t n = strlen(word);
    const char *p;

    for (p = value; *p != '\0'; p++) {
        size_t i;

        for (i = 0; i < n && lower(p[i]) == word[i]; i++) {
        }
        if (i == n) {
            return 1;
        }
    }
    return 0;
}

/*
 * One request.  0: the body reached the sink.  1: a redirect, the next
 * address in `next`.  -1: failed.  `*keep`: the connection can serve
 * another request (the answer was read whole, and the server keeps it
 * open).
 */
static int one_get(Conn *c, const char *url, char *next, size_t next_size, int *keep)
{
    BwHttpsGet *g = c->g;
    char host[256];
    char *path, *req, *head;
    char value[64];
    u16 port = 443;
    u32 end = 0;
    int rc, n, attempt, lasting;

    *keep = 0;
    path = (char *)malloc(BW_HTTPS_URL_MAX + 1);
    req = (char *)malloc(BW_HTTPS_URL_MAX + 768);
    if (path == NULL || req == NULL) {
        free(path);
        free(req);
        say(g, "out of memory");
        return -1;
    }
    if (bw_https_split(url, host, sizeof(host), &port, path, BW_HTTPS_URL_MAX + 1) != 0) {
        free(path);
        free(req);
        say(g, "the address is not an https address");
        return -1;
    }
    if (c->post) {
        n = snprintf(req, BW_HTTPS_URL_MAX + 768,
                     "POST %s HTTP/1.1\r\n"
                     "Host: %s\r\n"
                     "User-Agent: eidos\r\n"
                     "Accept: */*\r\n"
                     "Accept-Encoding: identity\r\n"
                     "Content-Type: %.200s\r\n"
                     "Content-Length: %u\r\n"
                     "\r\n", path, host,
                     g->content_type != NULL ? g->content_type : "application/octet-stream",
                     (unsigned)g->body_size);
    } else {
        n = snprintf(req, BW_HTTPS_URL_MAX + 768,
                     "GET %s HTTP/1.1\r\n"
                     "Host: %s\r\n"
                     "User-Agent: eidos\r\n"
                     "Accept: */*\r\n"
                     "Accept-Encoding: identity\r\n"
                     "\r\n", path, host);
    }
    free(path);
    c->got = c->total = 0;
    for (attempt = 0; ; attempt++) {
        int kept;

        c->n = c->pos = 0;
        if (stopped(g)) {
            free(req);
            say(g, "stopped");
            return -1;
        }
        kept = c->st->open(c->st->user, host, port, attempt > 0, g->err, sizeof(g->err));
        if (kept < 0) {
            free(req);
            if (g->err[0] == '\0') {
                snprintf(g->err, sizeof(g->err), "no connection to %.150s", host);
            }
            return -1;
        }
        rc = c->st->send(c->st->user, req, (u32)n) == 0
             && (!c->post || g->body_size == 0 || c->st->send(c->st->user, g->body, g->body_size) == 0)
             ? read_head(c, &end) : RD_BROKEN;
        if (rc == 1) {
            break;
        }
        if (kept == 1 && attempt == 0 && c->n == 0 && rc != RD_STOP) {
            /* the kept connection was closed by the server meanwhile */
            c->st->close(c->st->user, 0);
            g->err[0] = '\0';
            continue;
        }
        free(req);
        if (rc == HEAD_BAD) {
            say(g, "the server's answer does not read");
        } else {
            say_read(g, rc);
        }
        return -1;
    }
    free(req);
    head = (char *)malloc(end + 3);
    if (head == NULL) {
        say(g, "out of memory");
        return -1;
    }
    memcpy(head, c->buf, end + 2);      /* with the last line's CR LF */
    head[end + 2] = '\0';
    c->pos = end + 4;
    if (strlen(head) != end + 2 || strncmp(head, "HTTP/1.", 7) != 0 || head[8] != ' ' ||
        head[9] < '1' || head[9] > '9' || head[10] < '0' || head[10] > '9' ||
        head[11] < '0' || head[11] > '9') {
        free(head);
        say(g, "the server's answer does not read");
        return -1;
    }
    g->status = (head[9] - '0') * 100 + (head[10] - '0') * 10 + (head[11] - '0');
    /* HTTP/1.1 keeps a connection open unless the answer says "close" */
    lasting = head[7] == '1' && !(header(head, "connection", value, sizeof(value)) && names_word(value, "close"));

    if (g->status == 301 || g->status == 302 || g->status == 303 || g->status == 307 ||
        g->status == 308) {
        char *loc = (char *)malloc(BW_HTTPS_URL_MAX + 1);

        rc = -1;
        if (loc != NULL && header(head, "location", loc, BW_HTTPS_URL_MAX + 1) &&
            redirect_to(host, port, loc, next, next_size) == 0) {
            rc = 1;
            if (g->status != 307 && g->status != 308) {
                c->post = 0;            /* (the next request is a GET) */
            }
            /* (its body is not read: only an empty one leaves the connection clean) */
            *keep = lasting && header(head, "content-length", value, sizeof(value)) &&
                    strcmp(value, "0") == 0 && c->pos == c->n;
        } else {
            say(g, "the server's redirect does not read");
        }
        free(loc);
        free(head);
        return rc;
    }
    if (g->status != 200 && !g->any_status) {
        snprintf(g->err, sizeof(g->err), "the server answered %d", g->status);
        free(head);
        return -1;
    }

    if (header(head, "transfer-encoding", value, sizeof(value))) {
        size_t i;

        for (i = 0; value[i] != '\0'; i++) {
            value[i] = lower(value[i]);
        }
        free(head);
        if (strcmp(value, "chunked") != 0) {
            say(g, "the server's answer does not read");
            return -1;
        }
        rc = body_chunked(c);
        *keep = rc == 0 && lasting && c->pos == c->n;
        return rc;
    }
    if (header(head, "content-length", value, sizeof(value))) {
        unsigned long long len = 0;
        size_t i;

        free(head);
        for (i = 0; value[i] != '\0'; i++) {
            if (value[i] < '0' || value[i] > '9' || i > 18) {
                say(g, "the server's answer does not read");
                return -1;
            }
            len = len * 10ull + (unsigned long long)(value[i] - '0');
        }
        if (i == 0) {
            say(g, "the server's answer does not read");
            return -1;
        }
        if (len > g->max) {
            snprintf(g->err, sizeof(g->err), "the file is larger than %llu bytes", g->max);
            return -1;
        }
        c->total = len;
        if (g->progress != NULL) {
            g->progress(g->user, 0, len);
        }
        rc = body_exact(c, len);
        *keep = rc == 0 && lasting && c->pos == c->n;
        return rc;
    }
    free(head);
    return body_to_close(c);
}

int bw_https_get(const char *url, BwHttpsGet *g)
{
    Conn c;
    char *cur, *next;
    int hop, rc = -1;

    g->status = 0;
    g->err[0] = '\0';
    memset(&c, 0, sizeof(c));
    c.g = g;
    c.post = g->body != NULL;
    if (c.post && g->content_type != NULL && strpbrk(g->content_type, "\r\n") != NULL) {
        say(g, "the content type is not one header line");
        return -1;
    }
    c.st = g->stream != NULL ? g->stream : bw_net_tls_stream();
    c.buf = (u8 *)malloc(BUF_SIZE);
    cur = (char *)malloc(BW_HTTPS_URL_MAX + 1);
    next = (char *)malloc(BW_HTTPS_URL_MAX + 1);
    if (c.buf == NULL || cur == NULL || next == NULL || url == NULL ||
        strlen(url) > BW_HTTPS_URL_MAX) {
        say(g, url != NULL && strlen(url) > BW_HTTPS_URL_MAX ? "the address is too long"
                                                              : "out of memory");
        goto done;
    }
    strcpy(cur, url);
    for (hop = 0; ; hop++) {
        int keep = 0;

        rc = one_get(&c, cur, next, BW_HTTPS_URL_MAX + 1, &keep);
        c.st->close(c.st->user, keep);
        if (rc != 1) {
            break;
        }
        if (hop >= BW_HTTPS_REDIRECTS_MAX) {
            say(g, "too many redirects");
            rc = -1;
            break;
        }
        strcpy(cur, next);
    }
done:
    free(c.buf);
    free(cur);
    free(next);
    return rc == 0 ? 0 : -1;
}

void bw_https_idle(const BwNetStream *stream)
{
    if (stream == NULL) {
        stream = bw_net_tls_stream();
    }
    if (stream->idle != NULL) {
        stream->idle(stream->user);
    }
}
