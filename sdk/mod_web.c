/*
 * sdk/mod_web.c -- a mod's web requests, mod API version 7
 * (docs/mod-services.md, "Version 7: the web, the clock, the game's hash and
 * the pop-up").
 *
 * - web_request checks the address (https only), the body and the type,
 *   keeps the request with the mod that made it, and starts a thread that
 *   makes it with the HTTPS client the game's mod manager uses
 *   (sdk/net_https.c, the TLS stream and its certificate roots).  The
 *   answer's body goes into memory, up to BW_WEB_ANSWER_MAX bytes.
 * - On the game's frame (bw_mod_web_frame) each finished request fires the
 *   event `web` with its ticket, status and size.  Inside the handler the
 *   mod that made it reads the bytes with web_read; for any other mod
 *   web_read answers -1, so an answer (a sign-in token) stays with the mod
 *   that asked.  Then the bytes are freed.
 * - At most BW_WEB_WAITING requests wait at once, all mods together.
 *
 * A request of a mod that stops is still made; its answer fires for the
 * mods that are running, and nobody can read it.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "types.h"
#include "mod_api.h"
#include "mod_event.h"
#include "mod_web.h"
#include "net_https.h"
#include "plat_thread.h"

/* the namespace of the mod whose call or handler runs (sdk/mod_savedata.c) */
extern const char *bw_mod_savedata_namespace(void) __attribute__((weak));

typedef struct WebJob {
    struct WebJob *next;
    u32   ticket;
    char  ns[64];               /* the mod that made it */
    char *url;
    u8   *body;
    u32   body_size;
    char  type[128];
    BwThread *thread;
    volatile int done;          /* the thread has finished */
    int   status;               /* the HTTP status, or BW_WEB_FAILED */
    u8   *answer;
    u32   answer_size;
    u32   answer_cap;
    char  err[200];
} WebJob;

static BwMutex *g_lock;
static WebJob  *g_jobs;          /* waiting and finished, in order made */
static u32      g_waiting;
static u32      g_ticket;
static WebJob  *g_current;       /* the answer being handled */

static void lock(void)
{
    if (g_lock == NULL) {
        g_lock = bw_mutex_create();
    }
    if (g_lock != NULL) {
        bw_mutex_lock(g_lock);
    }
}

static void unlock(void)
{
    if (g_lock != NULL) {
        bw_mutex_unlock(g_lock);
    }
}

static const char *calling_ns(void)
{
    const char *ns = bw_mod_savedata_namespace != NULL ? bw_mod_savedata_namespace() : NULL;

    return ns != NULL ? ns : "";
}

static int sink(void *user, const void *data, u32 size)
{
    WebJob *j = (WebJob *)user;

    if (j->answer_size + size > BW_WEB_ANSWER_MAX) {
        return -1;
    }
    if (j->answer_size + size > j->answer_cap) {
        u32 cap = j->answer_cap != 0 ? j->answer_cap : 4096u;
        u8 *grown;

        while (cap < j->answer_size + size) {
            cap *= 2u;
        }
        if (cap > BW_WEB_ANSWER_MAX) {
            cap = BW_WEB_ANSWER_MAX;
        }
        grown = (u8 *)realloc(j->answer, cap);
        if (grown == NULL) {
            return -1;
        }
        j->answer = grown;
        j->answer_cap = cap;
    }
    memcpy(j->answer + j->answer_size, data, size);
    j->answer_size += size;
    return 0;
}

static void work(void *arg)
{
    WebJob *j = (WebJob *)arg;
    BwHttpsGet g;

    memset(&g, 0, sizeof g);
    g.stream = bw_mod_web_stream();
    g.max = BW_WEB_ANSWER_MAX;
    g.sink = sink;
    g.user = j;
    g.any_status = 1;
    if (j->body != NULL) {
        g.body = j->body;
        g.body_size = j->body_size;
        g.content_type = j->type;
    }
    if (bw_https_get(j->url, &g) == 0) {
        j->status = g.status;
    } else {
        /* no answer: the reason is what web_read gives */
        j->status = BW_WEB_FAILED;
        j->answer_size = 0;
        snprintf(j->err, sizeof j->err, "%s", g.err[0] != '\0' ? g.err : "the request failed");
    }
    lock();
    j->done = 1;
    unlock();
}

static const BwNetStream *g_stream;

void bw_mod_web_set_stream(const BwNetStream *stream)
{
    g_stream = stream;
}

const BwNetStream *bw_mod_web_stream(void)
{
    return g_stream;            /* NULL: the game's own TLS stream */
}

int bw_mod_web_request(const char *url, const void *body, u32 body_size, const char *content_type,
                       u32 *ticket)
{
    WebJob *j, **tail;
    size_t n;

    if (ticket != NULL) {
        *ticket = 0;
    }
    /* A POST when there is a body or a type, else a GET.  (A wasm mod's
     * null pointer arrives as the start of its memory, not as NULL: the
     * size and the type decide, not the pointer.) */
    if (body_size == 0 && (content_type == NULL || content_type[0] == '\0')) {
        body = NULL;
    } else if (body == NULL && body_size == 0) {
        body = "";
    }
    if (url == NULL || strncmp(url, "https://", 8) != 0 || (n = strlen(url)) > BW_HTTPS_URL_MAX
        || strpbrk(url, " \r\n") != NULL || (body == NULL && body_size != 0) || body_size > BW_WEB_BODY_MAX
        || (content_type != NULL && (strlen(content_type) >= sizeof(j->type)
                                     || strpbrk(content_type, "\r\n") != NULL))) {
        return -1;
    }
    lock();
    if (g_waiting >= BW_WEB_WAITING) {
        unlock();
        return -1;
    }
    unlock();
    j = (WebJob *)calloc(1, sizeof *j);
    if (j == NULL || (j->url = (char *)malloc(n + 1u)) == NULL
        || (body != NULL && (j->body = (u8 *)malloc(body_size != 0 ? body_size : 1u)) == NULL)) {
        if (j != NULL) {
            free(j->url);
            free(j);
        }
        return -1;
    }
    memcpy(j->url, url, n + 1u);
    if (body != NULL) {
        memcpy(j->body, body, body_size);
        j->body_size = body_size;
        snprintf(j->type, sizeof j->type, "%s",
                 content_type != NULL && content_type[0] != '\0' ? content_type
                                                                 : "application/x-www-form-urlencoded");
    }
    snprintf(j->ns, sizeof j->ns, "%s", calling_ns());
    lock();
    j->ticket = ++g_ticket != 0 ? g_ticket : ++g_ticket;
    for (tail = &g_jobs; *tail != NULL; tail = &(*tail)->next) {
    }
    *tail = j;
    g_waiting++;
    unlock();
    j->thread = bw_thread_start(work, j);
    if (j->thread == NULL) {
        j->status = BW_WEB_FAILED;
        snprintf(j->err, sizeof j->err, "no thread for the request");
        j->done = 1;
    }
    if (ticket != NULL) {
        *ticket = j->ticket;
    }
    return 0;
}

int bw_mod_web_read(void *out, u32 size)
{
    const WebJob *j = g_current;
    const void *bytes;
    u32 total;

    if (j == NULL || strcmp(j->ns, calling_ns()) != 0 || (out == NULL && size != 0)) {
        return -1;
    }
    if (j->status == BW_WEB_FAILED) {
        bytes = j->err;
        total = (u32)strlen(j->err);
    } else {
        bytes = j->answer;
        total = j->answer_size;
    }
    if (size > 0 && total > 0) {
        memcpy(out, bytes, size < total ? size : total);
    }
    return (int)total;
}

void bw_mod_web_frame(void)
{
    for (;;) {
        WebJob *j = NULL, **at;
        BwEventPayload p;

        lock();
        for (at = &g_jobs; *at != NULL; at = &(*at)->next) {
            if ((*at)->done) {
                j = *at;
                *at = j->next;
                g_waiting--;
                break;
            }
        }
        unlock();
        if (j == NULL) {
            break;
        }
        if (j->thread != NULL) {
            bw_thread_join(j->thread);
        }
        memset(&p, 0, sizeof p);
        p.web.ticket = j->ticket;
        p.web.status = j->status;
        p.web.size = j->status == BW_WEB_FAILED ? (u32)strlen(j->err) : j->answer_size;
        g_current = j;
        (void)bw_mod_event_fire(BW_EVENT_WEB, &p);
        g_current = NULL;
        free(j->url);
        free(j->body);
        free(j->answer);
        free(j);
    }
}

u32 bw_mod_web_waiting(void)
{
    u32 n;

    lock();
    n = g_waiting;
    unlock();
    return n;
}
