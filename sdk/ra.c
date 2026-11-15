/*
 * sdk/ra.c -- RetroAchievements through rcheevos' rc_client
 * (sdk/ra.h, docs/retroachievements.md).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32) || defined(__wasm__)
#include <stdio.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#include "plat_thread.h"
#include "ra.h"
#include "ra_hash.h"
#include "ra_http.h"
#include "dsmem.h"
#include "port.h"
#include "rc_client.h"

#define TOAST_MAX     8
#define HTTP_TIMEOUT  20000
#define RA_RAM_BASE   0x02000000u       /* RetroAchievements' DS address 0 */
#define RA_RAM_SIZE   0x00400000u
#define RA_STATE_BYTE 0x34u             /* "State of the Game", see read_memory */

typedef struct Job {
    struct Job                *next;
    char                      *url;
    char                      *post;
    char                      *ctype;
    rc_client_server_callback_t cb;
    void                      *cb_data;
    BwRaHttpResult             res;
    int                        net_rc;
} Job;

static struct {
    int         up;
    BwRaConfig  cfg;
    char        home[512];
    char        data_root[512];
    char        version[48];
    char        agent[160];
    rc_client_t *client;

    BwMutex    *lock;
    BwCond     *wake;
    BwThread   *thread;
    int         quit;
    Job        *todo, *todo_tail;          /* for the worker             */
    Job        *done, *done_tail;          /* for the frame thread       */

    BwRaStatus  st;
    char        saved_user[64];
    char        saved_token[96];
    char        game_hash[33];
    int         hash_ok;
    BwRaToast   toasts[TOAST_MAX];
    int         ntoast;
} g;

/* ------------------------------------------------------------------ */
/* The token file                                                      */
/* ------------------------------------------------------------------ */

static void ini_path(char *out, size_t n)
{
    snprintf(out, n, "%s/retroachievements.ini", g.home);
}

static void trim(char *s)
{
    size_t n = strlen(s);

    while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r' || s[n - 1] == ' ')) {
        s[--n] = '\0';
    }
}

static void ini_load(void)
{
    char path[600], line[256];
    FILE *f;

    g.saved_user[0] = g.saved_token[0] = '\0';
    ini_path(path, sizeof(path));
    f = fopen(path, "r");
    if (f == NULL) {
        return;
    }
    while (fgets(line, sizeof(line), f) != NULL) {
        trim(line);
        if (strncmp(line, "user = ", 7) == 0) {
            snprintf(g.saved_user, sizeof(g.saved_user), "%s", line + 7);
        } else if (strncmp(line, "token = ", 8) == 0) {
            snprintf(g.saved_token, sizeof(g.saved_token), "%s", line + 8);
        }
    }
    fclose(f);
}

/* Written owner-only: the token signs in as the player. */
static void ini_save(void)
{
    char path[600], body[300];
    int n;
    FILE *f;

    ini_path(path, sizeof(path));
    n = snprintf(body, sizeof(body), "[retroachievements]\nuser = %s\ntoken = %s\n",
                 g.saved_user, g.saved_token);
    if (n <= 0 || (size_t)n >= sizeof(body)) {
        return;
    }
#if defined(_WIN32) || defined(__wasm__)
    f = fopen(path, "w");
#else
    {
        int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);

        f = (fd >= 0) ? fdopen(fd, "w") : NULL;
        if (fd >= 0) {
            (void)fchmod(fd, 0600);     /* an older, wider file */
        }
    }
#endif
    if (f == NULL) {
        return;
    }
    fputs(body, f);
    fclose(f);
}

/* ------------------------------------------------------------------ */
/* Status and toasts                                                   */
/* ------------------------------------------------------------------ */

static void set_status(BwRaState s, const char *msg)
{
    bw_mutex_lock(g.lock);
    g.st.state = s;
    if (msg != NULL) {
        snprintf(g.st.message, sizeof(g.st.message), "%s", msg);
    }
    bw_mutex_unlock(g.lock);
}

static void toast(const char *title, const char *text, unsigned points)
{
    BwRaToast *t;

    bw_mutex_lock(g.lock);
    if (g.ntoast == TOAST_MAX) {            /* drop the oldest */
        memmove(g.toasts, g.toasts + 1, sizeof(g.toasts[0]) * (TOAST_MAX - 1));
        g.ntoast--;
    }
    t = &g.toasts[g.ntoast++];
    snprintf(t->title, sizeof(t->title), "%s", title != NULL ? title : "");
    snprintf(t->text, sizeof(t->text), "%s", text != NULL ? text : "");
    t->points = points;
    bw_mutex_unlock(g.lock);
}

int bw_ra_toast_pop(BwRaToast *out)
{
    int got = 0;

    if (!g.up) {
        return 0;
    }
    bw_mutex_lock(g.lock);
    if (g.ntoast > 0) {
        *out = g.toasts[0];
        memmove(g.toasts, g.toasts + 1, sizeof(g.toasts[0]) * (size_t)(g.ntoast - 1));
        g.ntoast--;
        got = 1;
    }
    bw_mutex_unlock(g.lock);
    return got;
}

/* ------------------------------------------------------------------ */
/* The network: a worker thread and two queues                         */
/* ------------------------------------------------------------------ */

static void job_free(Job *j)
{
    free(j->url);
    free(j->post);
    free(j->ctype);
    free(j->res.body);
    free(j);
}

static char *str_dup(const char *s)
{
    size_t n;
    char *p;

    if (s == NULL) {
        return NULL;
    }
    n = strlen(s) + 1;
    p = (char *)malloc(n);
    if (p != NULL) {
        memcpy(p, s, n);
    }
    return p;
}

static void worker(void *arg)
{
    (void)arg;
    for (;;) {
        Job *j;

        bw_mutex_lock(g.lock);
        while (g.todo == NULL && !g.quit) {
            bw_cond_wait(g.wake, g.lock);
        }
        if (g.quit) {
            bw_mutex_unlock(g.lock);
            return;
        }
        j = g.todo;
        g.todo = j->next;
        if (g.todo == NULL) {
            g.todo_tail = NULL;
        }
        bw_mutex_unlock(g.lock);

        j->next = NULL;
        j->net_rc = bw_ra_http_request(j->url, g.agent, j->ctype, j->post,
                                       j->post != NULL ? strlen(j->post) : 0,
                                       HTTP_TIMEOUT, &j->res);

        bw_mutex_lock(g.lock);
        if (g.done_tail != NULL) {
            g.done_tail->next = j;
        } else {
            g.done = j;
        }
        g.done_tail = j;
        bw_mutex_unlock(g.lock);
    }
}

/* rc_client asks for a request; the worker makes it. */
static void server_call(const rc_api_request_t *req,
                        rc_client_server_callback_t cb, void *cb_data,
                        rc_client_t *client)
{
    Job *j = (Job *)calloc(1, sizeof(*j));

    (void)client;
    if (j == NULL) {
        rc_api_server_response_t r;

        memset(&r, 0, sizeof(r));
        r.http_status_code = RC_API_SERVER_RESPONSE_RETRYABLE_CLIENT_ERROR;
        r.body = "out of memory";
        r.body_length = 13;
        cb(&r, cb_data);
        return;
    }
    j->url = str_dup(req->url);
    j->post = str_dup(req->post_data);
    j->ctype = str_dup(req->content_type);
    j->cb = cb;
    j->cb_data = cb_data;
    bw_mutex_lock(g.lock);
    if (g.todo_tail != NULL) {
        g.todo_tail->next = j;
    } else {
        g.todo = j;
    }
    g.todo_tail = j;
    bw_cond_broadcast(g.wake);
    bw_mutex_unlock(g.lock);
}

/* The answers the worker has, handed to rcheevos on this thread. */
static void drain(void)
{
    for (;;) {
        Job *j;
        rc_api_server_response_t r;

        bw_mutex_lock(g.lock);
        j = g.done;
        if (j != NULL) {
            g.done = j->next;
            if (g.done == NULL) {
                g.done_tail = NULL;
            }
        }
        bw_mutex_unlock(g.lock);
        if (j == NULL) {
            return;
        }
        memset(&r, 0, sizeof(r));
        if (j->net_rc == 0) {
            r.body = j->res.body;
            r.body_length = j->res.body_len;
            r.http_status_code = j->res.status;
        } else {
            r.body = j->res.error;
            r.body_length = strlen(j->res.error);
            r.http_status_code = RC_API_SERVER_RESPONSE_RETRYABLE_CLIENT_ERROR;
        }
        j->cb(&r, j->cb_data);
        job_free(j);
    }
}

/* ------------------------------------------------------------------ */
/* rc_client's callbacks                                               */
/* ------------------------------------------------------------------ */

/* RetroAchievements' address 0 is the DS's 0x02000000, 4 MiB of it. */
static uint32_t read_memory(uint32_t address, uint8_t *buffer, uint32_t num_bytes,
                            rc_client_t *client)
{
    uint32_t n;

    (void)client;
    if (g.cfg.read == NULL || address >= RA_RAM_SIZE ||
        num_bytes > RA_RAM_SIZE - address) {
        return 0;
    }
    n = g.cfg.read(RA_RAM_BASE + address, buffer, num_bytes);
    /* "State of the Game" (RetroAchievements' own code note for 0x34): a
     * byte of a real DS's main RAM that is 1 while the game plays, and the
     * guard of 155 of the 209 achievements of the Black set.  The port has
     * no such byte -- its RAM is 0 there, in game or out -- so it is
     * supplied here: 1 while this function is called, which is while the
     * game's frames run (docs/retroachievements.md, "Memory"). */
    if (n == num_bytes && address <= RA_STATE_BYTE && RA_STATE_BYTE < address + num_bytes) {
        buffer[RA_STATE_BYTE - address] = 1;
    }
    return n;
}

static int g_unlocks;                    /* unlocks the frame has not chimed for */
static void snd_find(void);

static void on_event(const rc_client_event_t *ev, rc_client_t *client)
{
    (void)client;
    switch (ev->type) {
    case RC_CLIENT_EVENT_ACHIEVEMENT_TRIGGERED:
        if (ev->achievement != NULL) {
            toast(ev->achievement->title, ev->achievement->description,
                  ev->achievement->points);
            g_unlocks++;
        }
        break;
    case RC_CLIENT_EVENT_GAME_COMPLETED:
        toast("Set complete", "Every achievement of this set is earned", 0);
        break;
    case RC_CLIENT_EVENT_SERVER_ERROR:
        if (ev->server_error != NULL) {
            toast("RetroAchievements", ev->server_error->error_message, 0);
        }
        break;
    case RC_CLIENT_EVENT_DISCONNECTED:
        toast("RetroAchievements", "Offline: unlocks are kept and sent later", 0);
        break;
    case RC_CLIENT_EVENT_RECONNECTED:
        toast("RetroAchievements", "Back online", 0);
        break;
    default:
        break;
    }
}

static void count_set(void)
{
    rc_client_user_game_summary_t s;

    memset(&s, 0, sizeof(s));
    rc_client_get_user_game_summary(g.client, &s);
    bw_mutex_lock(g.lock);
    g.st.achievements = s.num_core_achievements;
    g.st.unlocked = s.num_unlocked_achievements;
    bw_mutex_unlock(g.lock);
}

static void on_game_loaded(int result, const char *error_message,
                           rc_client_t *client, void *userdata)
{
    const rc_client_game_t *game;

    (void)userdata;
    if (result != RC_OK) {
        char m[160];

        snprintf(m, sizeof(m), "No achievements: %s",
                 error_message != NULL ? error_message : "unknown error");
        bw_mutex_lock(g.lock);
        g.st.game_loaded = 0;
        snprintf(g.st.message, sizeof(g.st.message), "%s", m);
        bw_mutex_unlock(g.lock);
        return;
    }
    game = rc_client_get_game_info(client);
    bw_mutex_lock(g.lock);
    g.st.game_loaded = 1;
    snprintf(g.st.game_title, sizeof(g.st.game_title), "%s",
             game != NULL && game->title != NULL ? game->title : "");
    bw_mutex_unlock(g.lock);
    count_set();
    toast(g.st.game_title, "RetroAchievements is on", 0);
}

static void load_game(void)
{
    if (!g.hash_ok || rc_client_get_user_info(g.client) == NULL) {
        return;
    }
    rc_client_begin_load_game(g.client, g.game_hash, on_game_loaded, NULL);
}

static void on_login(int result, const char *error_message,
                     rc_client_t *client, void *userdata)
{
    const rc_client_user_t *u;

    (void)userdata;
    if (result != RC_OK) {
        char m[160];

        snprintf(m, sizeof(m), "Sign-in failed: %s",
                 error_message != NULL ? error_message : "unknown error");
        set_status(BW_RA_ERROR, m);
        return;
    }
    u = rc_client_get_user_info(client);
    if (u == NULL) {
        set_status(BW_RA_ERROR, "Sign-in failed");
        return;
    }
    bw_mutex_lock(g.lock);
    snprintf(g.st.user, sizeof(g.st.user), "%s",
             u->display_name != NULL ? u->display_name : u->username);
    bw_mutex_unlock(g.lock);
    /* Keep the token, never the password. */
    if (u->username != NULL && u->token != NULL) {
        snprintf(g.saved_user, sizeof(g.saved_user), "%s", u->username);
        snprintf(g.saved_token, sizeof(g.saved_token), "%s", u->token);
        ini_save();
    }
    set_status(BW_RA_SIGNED_IN, "Signed in");
    toast("Signed in to RetroAchievements", g.st.user, 0);
    load_game();
}

/* ------------------------------------------------------------------ */
/* The latest unlocks                                                  */
/* ------------------------------------------------------------------ */

static int newest_first(const void *x, const void *y)
{
    long long a = ((const BwRaRecent *)x)->when, b = ((const BwRaRecent *)y)->when;

    return (a < b) - (a > b);
}

/* The set's achievements the player has earned, newest first. */
int bw_ra_recent(BwRaRecent *out, int max)
{
    rc_client_achievement_list_t *list;
    BwRaRecent *all;
    size_t total = 0, cap = 0;
    uint32_t b, i;
    int n;

    if (!g.up || !g.st.game_loaded || max <= 0) {
        return 0;
    }
    list = rc_client_create_achievement_list(g.client,
               RC_CLIENT_ACHIEVEMENT_CATEGORY_CORE_AND_UNOFFICIAL,
               RC_CLIENT_ACHIEVEMENT_LIST_GROUPING_LOCK_STATE);
    if (list == NULL) {
        return 0;
    }
    for (b = 0; b < list->num_buckets; b++) {
        cap += list->buckets[b].num_achievements;
    }
    all = (BwRaRecent *)calloc(cap != 0 ? cap : 1, sizeof(*all));
    if (all == NULL) {
        rc_client_destroy_achievement_list(list);
        return 0;
    }
    for (b = 0; b < list->num_buckets; b++) {
        for (i = 0; i < list->buckets[b].num_achievements; i++) {
            const rc_client_achievement_t *a = list->buckets[b].achievements[i];
            BwRaRecent *r;

            if (a == NULL || a->state != RC_CLIENT_ACHIEVEMENT_STATE_UNLOCKED || total >= cap) {
                continue;
            }
            r = &all[total++];
            snprintf(r->title, sizeof(r->title), "%s", a->title != NULL ? a->title : "");
            snprintf(r->text, sizeof(r->text), "%s", a->description != NULL ? a->description : "");
            r->points = a->points;
            r->when = (long long)a->unlock_time;
        }
    }
    rc_client_destroy_achievement_list(list);
    qsort(all, total, sizeof(*all), newest_first);
    n = total < (size_t)max ? (int)total : max;
    memcpy(out, all, (size_t)n * sizeof(*out));
    free(all);
    return n;
}

/* ------------------------------------------------------------------ */
/* The public calls                                                    */
/* ------------------------------------------------------------------ */

int bw_ra_init(const BwRaConfig *cfg)
{
    char clause[64], err[160];

    if (g.up) {
        return 0;
    }
    memset(&g, 0, sizeof(g));
    g.cfg = *cfg;
    snprintf(g.home, sizeof(g.home), "%s", cfg->home != NULL ? cfg->home : ".");
    snprintf(g.data_root, sizeof(g.data_root), "%s", cfg->data_root != NULL ? cfg->data_root : ".");
    snprintf(g.version, sizeof(g.version), "%s", cfg->version != NULL ? cfg->version : "dev");

    g.client = rc_client_create(read_memory, server_call);
    if (g.client == NULL) {
        return -1;
    }
    clause[0] = '\0';
    rc_client_get_user_agent_clause(g.client, clause, sizeof(clause));
    snprintf(g.agent, sizeof(g.agent), "eidos/%s %s", g.version, clause);

    g.lock = bw_mutex_create();
    g.wake = bw_cond_create();
    rc_client_set_event_handler(g.client, on_event);
    rc_client_set_hardcore_enabled(g.client, 0);        /* softcore only */

    err[0] = '\0';
    g.hash_ok = bw_ra_hash_from_root(g.data_root, g.game_hash, err, sizeof(err)) == 0;
    g.thread = bw_thread_start(worker, NULL);
    g.up = 1;

    ini_load();
    snd_find();
    if (g.saved_user[0] != '\0' && g.saved_token[0] != '\0') {
        set_status(BW_RA_SIGNING_IN, "Signing in...");
        rc_client_begin_login_with_token(g.client, g.saved_user, g.saved_token,
                                         on_login, NULL);
    } else {
        set_status(BW_RA_SIGNED_OUT, g.hash_ok ? "Not signed in" : err);
    }
    return 0;
}

void bw_ra_shutdown(void)
{
    Job *j;

    if (!g.up) {
        return;
    }
    bw_mutex_lock(g.lock);
    g.quit = 1;
    bw_cond_broadcast(g.wake);
    bw_mutex_unlock(g.lock);
    bw_thread_join(g.thread);
    /* The answers still queued are dropped: rcheevos is going away. */
    for (j = g.todo; j != NULL;) {
        Job *n = j->next;

        job_free(j);
        j = n;
    }
    for (j = g.done; j != NULL;) {
        Job *n = j->next;

        job_free(j);
        j = n;
    }
    rc_client_destroy(g.client);
    bw_cond_destroy(g.wake);
    bw_mutex_destroy(g.lock);
    memset(&g, 0, sizeof(g));
}

void bw_ra_idle(void)
{
    if (!g.up) {
        return;
    }
    drain();
    rc_client_idle(g.client);
}

void bw_ra_frame(void)
{
    if (!g.up) {
        return;
    }
    drain();
    if (g.st.game_loaded) {
        rc_client_do_frame(g.client);
    } else {
        rc_client_idle(g.client);
    }
}

void bw_ra_reset(void)
{
    if (g.up && g.st.game_loaded) {
        rc_client_reset(g.client);
    }
}

int bw_ra_login(const char *user, const char *password)
{
    if (!g.up || user == NULL || password == NULL || user[0] == '\0' ||
        password[0] == '\0') {
        return -1;
    }
    set_status(BW_RA_SIGNING_IN, "Signing in...");
    rc_client_begin_login_with_password(g.client, user, password, on_login, NULL);
    return 0;
}

void bw_ra_logout(void)
{
    if (!g.up) {
        return;
    }
    rc_client_logout(g.client);
    g.saved_user[0] = g.saved_token[0] = '\0';
    ini_save();
    bw_mutex_lock(g.lock);
    g.st.user[0] = '\0';
    g.st.game_loaded = 0;
    g.st.game_title[0] = '\0';
    g.st.achievements = g.st.unlocked = 0;
    bw_mutex_unlock(g.lock);
    set_status(BW_RA_SIGNED_OUT, "Signed out");
}

void bw_ra_status(BwRaStatus *out)
{
    memset(out, 0, sizeof(*out));
    if (!g.up) {
        return;
    }
    bw_mutex_lock(g.lock);
    *out = g.st;
    bw_mutex_unlock(g.lock);
}

/* ------------------------------------------------------------------ */
/* The port's own start                                                */
/* ------------------------------------------------------------------ */

#ifndef EIDOS_VERSION
#define EIDOS_VERSION "0.0.0+unknown"
#endif

static unsigned ds_read(unsigned addr, void *dst, unsigned n)
{
    const unsigned char *p;

    if (n == 0) {
        return 0;
    }
    /* Both ends must be backed: a read never runs off a region. */
    p = (const unsigned char *)bw_ds_try_ptr(addr);
    if (p == NULL || bw_ds_try_ptr(addr + n - 1) != (const void *)(p + n - 1)) {
        return 0;
    }
    memcpy(dst, p, n);
    return n;
}

int bw_ra_start_default(void)
{
    static int tried;
    BwRaConfig cfg;
    const char *root, *env;
    char home[1024];

    if (g.up) {
        return 0;
    }
    if (tried) {
        return -1;
    }
    tried = 1;
    root = bw_data_root();
    if (root == NULL || root[0] == '\0') {
        return -1;
    }
    /* The sign-in file sits with the player's settings: the folder of
     * settings.ini (the launcher's profile folder), else the save folder,
     * else the data root. */
    home[0] = '\0';
    if ((env = getenv("EIDOS_SETTINGS")) != NULL && env[0] != '\0') {
        char *slash;

        snprintf(home, sizeof(home), "%s", env);
        slash = strrchr(home, '/');
        if (slash == NULL) {
            slash = strrchr(home, '\\');
        }
        if (slash != NULL) {
            *slash = '\0';
        } else {
            home[0] = '\0';
        }
    }
    if (home[0] == '\0' && (env = getenv("BW_SAVE_DIR")) != NULL && env[0] != '\0') {
        snprintf(home, sizeof(home), "%s", env);
    }
    if (home[0] == '\0') {
        snprintf(home, sizeof(home), "%s", root);
    }
    memset(&cfg, 0, sizeof(cfg));
    cfg.home = home;
    cfg.data_root = root;
    cfg.version = EIDOS_VERSION;
    cfg.read = ds_read;
    return bw_ra_init(&cfg);
}

/*
 * The banner: each toast drops down from the top-middle of the window,
 * stays, and goes back up (docs/retroachievements.md, "The banner").  This
 * file owns the timing, counted in game frames so it waits when the game
 * does; the pictures are drawn by the one-screen interface
 * (engine/ui/ui.c, ra_banner) and by the platform for the two-screen
 * layouts (engine/platform/ra_banner_draw.c), both reading it through
 * bw_ra_banner.
 */
#define BAN_IN        18                 /* frames sliding in                  */
#define BAN_HOLD      300                /* frames held                        */
#define BAN_OUT       18                 /* frames sliding out                 */
#define BAN_TOTAL     (BAN_IN + BAN_HOLD + BAN_OUT)

static BwRaToast g_ban;                  /* the banner shown                    */
static int       g_ban_t = -1;           /* frames into it, -1 when none        */
static unsigned  g_ban_gen;              /* changes when a banner begins        */

/* The game's own sound effect, the way engine/ui/ui.c plays one. */
extern void GFL_USE_PlaySeDefaultVolume(unsigned seqNo) __attribute__((weak));

static void play_se(unsigned seq)
{
    if (GFL_USE_PlaySeDefaultVolume != NULL) {
        GFL_USE_PlaySeDefaultVolume(seq);
    }
}

/* The setting "Achievement sound" (settings.h's EIDOS_OPT_RA_SOUND). */
extern int eidos_settings_get(int id) __attribute__((weak));
#define OPT_RA_SOUND 21

static int sound_on(void)
{
    return eidos_settings_get == NULL || eidos_settings_get(OPT_RA_SOUND) != 0;
}

/*
 * The achievement sound.  achievement.wav (PCM, 8 or 16 bit, mono or
 * stereo, any rate) is looked for in the profile folder, then beside the
 * game's data; it is converted once to 32768 Hz stereo, the rate the game's
 * audio is mixed at, and added to the next blocks of that audio
 * (bw_ra_sound_mix) from its start whenever an achievement unlocks.  With no
 * such file the game's own "decide" sound is played instead.
 */
#define SND_RATE      32768
#define SND_MAX_FILE  (8u * 1024u * 1024u)
#define SND_MAX_SEC   10

static short   *g_snd;                   /* stereo frames, SND_RATE             */
static unsigned g_snd_frames;
static unsigned g_snd_pos = 0xFFFFFFFFu; /* next frame to mix; >= frames: idle  */

static unsigned le16(const unsigned char *p) { return (unsigned)p[0] | ((unsigned)p[1] << 8); }
static unsigned le32(const unsigned char *p) { return le16(p) | (le16(p + 2) << 16); }

/* Reads a WAV file into g_snd.  1 on success. */
static int snd_load(const char *path)
{
    FILE *f = fopen(path, "rb");
    unsigned char *d = NULL;
    long size;
    size_t pos = 12, got;
    unsigned fmt_tag = 0, ch = 0, rate = 0, bits = 0, data_off = 0, data_len = 0;
    unsigned in_frames, out_frames, i;
    short *out;

    if (f == NULL) {
        return 0;
    }
    if (fseek(f, 0, SEEK_END) != 0 || (size = ftell(f)) < 44 || (unsigned long)size > SND_MAX_FILE ||
        fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return 0;
    }
    d = (unsigned char *)malloc((size_t)size);
    got = d != NULL ? fread(d, 1, (size_t)size, f) : 0;
    fclose(f);
    if (d == NULL || got != (size_t)size || memcmp(d, "RIFF", 4) != 0 || memcmp(d + 8, "WAVE", 4) != 0) {
        free(d);
        return 0;
    }
    while (pos + 8 <= got) {
        unsigned len = le32(d + pos + 4);

        if (memcmp(d + pos, "fmt ", 4) == 0 && len >= 16 && pos + 8 + 16 <= got) {
            fmt_tag = le16(d + pos + 8);
            ch = le16(d + pos + 10);
            rate = le32(d + pos + 12);
            bits = le16(d + pos + 22);
        } else if (memcmp(d + pos, "data", 4) == 0) {
            data_off = (unsigned)pos + 8;
            data_len = len;
            if (data_off + data_len > got) {
                data_len = (unsigned)got - data_off;
            }
            break;
        }
        pos += 8 + (size_t)len + (len & 1u);
    }
    if (fmt_tag != 1 || (ch != 1 && ch != 2) || (bits != 8 && bits != 16) || rate < 1000 || rate > 400000 ||
        data_off == 0 || data_len == 0) {
        free(d);
        return 0;
    }
    in_frames = data_len / (ch * (bits / 8));
    if (in_frames < 2) {
        free(d);
        return 0;
    }
    out_frames = (unsigned)((unsigned long long)in_frames * SND_RATE / rate);
    if (out_frames > (unsigned)(SND_MAX_SEC * SND_RATE)) {
        out_frames = (unsigned)(SND_MAX_SEC * SND_RATE);
    }
    out = (short *)malloc((size_t)out_frames * 2u * sizeof(short));
    if (out == NULL || out_frames == 0) {
        free(d);
        free(out);
        return 0;
    }
    for (i = 0; i < out_frames; i++) {
        double src = (double)i * (double)rate / (double)SND_RATE;
        unsigned i0 = (unsigned)src, i1 = i0 + 1 < in_frames ? i0 + 1 : i0;
        double t = src - (double)i0;
        double v[2];
        unsigned c;

        if (i0 >= in_frames) {
            i0 = i1 = in_frames - 1;
        }
        for (c = 0; c < 2; c++) {
            unsigned cc = ch == 1 ? 0 : c;
            const unsigned char *a0 = d + data_off + ((size_t)i0 * ch + cc) * (bits / 8);
            const unsigned char *a1 = d + data_off + ((size_t)i1 * ch + cc) * (bits / 8);
            double s0, s1;

            if (bits == 16) {
                s0 = (double)(short)le16(a0);
                s1 = (double)(short)le16(a1);
            } else {
                s0 = ((double)a0[0] - 128.0) * 256.0;
                s1 = ((double)a1[0] - 128.0) * 256.0;
            }
            v[c] = (s0 + (s1 - s0) * t) * 0.65;       /* under the game's own sound */
        }
        out[i * 2u] = (short)v[0];
        out[i * 2u + 1u] = (short)v[1];
    }
    free(d);
    free(g_snd);
    g_snd = out;
    g_snd_frames = out_frames;
    g_snd_pos = 0xFFFFFFFFu;
    return 1;
}

static void snd_find(void)
{
    char path[1100];

    snprintf(path, sizeof(path), "%s/achievement.wav", g.home);
    if (snd_load(path)) {
        return;
    }
    snprintf(path, sizeof(path), "%s/../achievement.wav", g.data_root);
    if (snd_load(path)) {
        return;
    }
    snprintf(path, sizeof(path), "%s/achievement.wav", g.data_root);
    (void)snd_load(path);
}

/* The sound plays: from its start, or, with no file, the game's own. */
static void chime(void)
{
    if (!sound_on()) {
        return;
    }
    if (g_snd != NULL) {
        g_snd_pos = 0;
    } else {
        play_se(0x54Du);                 /* SEQ_SE_DECIDE2 */
    }
}

void bw_ra_sound_mix(short *buf, unsigned frames)
{
    unsigned i;
    unsigned pos = g_snd_pos;

    if (buf == NULL || g_snd == NULL || pos >= g_snd_frames) {
        return;
    }
    for (i = 0; i < frames && pos < g_snd_frames; i++, pos++) {
        int l = buf[i * 2u] + g_snd[pos * 2u];
        int r = buf[i * 2u + 1u] + g_snd[pos * 2u + 1u];

        buf[i * 2u] = (short)(l > 32767 ? 32767 : l < -32768 ? -32768 : l);
        buf[i * 2u + 1u] = (short)(r > 32767 ? 32767 : r < -32768 ? -32768 : r);
    }
    g_snd_pos = pos;
}

/* How far down the banner is, 0 (hidden) to 256 (all the way), eased. */
static int ban_progress(int t)
{
    int left, p;

    if (t < BAN_IN) {
        left = BAN_IN - t;
        p = 256 - left * left * 256 / (BAN_IN * BAN_IN);     /* out-quad */
        return p;
    }
    if (t < BAN_IN + BAN_HOLD) {
        return 256;
    }
    left = t - (BAN_IN + BAN_HOLD);                           /* going up */
    p = 256 - left * left * 256 / (BAN_OUT * BAN_OUT);
    return p < 0 ? 0 : p;
}

int bw_ra_banner(BwRaBannerView *v)
{
    int on = 0;

    memset(v, 0, sizeof(*v));
    if (!g.up) {
        return 0;
    }
    bw_mutex_lock(g.lock);
    if (g_ban_t >= 0) {
        on = 1;
        v->active = 1;
        v->generation = g_ban_gen;
        snprintf(v->title, sizeof(v->title), "%s", g_ban.title);
        snprintf(v->text, sizeof(v->text), "%s", g_ban.text);
        v->points = g_ban.points;
        v->progress = ban_progress(g_ban_t);
    }
    bw_mutex_unlock(g.lock);
    return on;
}

static void toast_frame(void)
{
    BwRaToast t;

    if (g_unlocks > 0) {                 /* an unlock: the sound at once */
        g_unlocks = 0;
        chime();
    }

    if (g_ban_t >= 0) {
        bw_mutex_lock(g.lock);
        if (++g_ban_t >= BAN_TOTAL) {
            g_ban_t = -1;
        }
        bw_mutex_unlock(g.lock);
        if (g_ban_t >= 0) {
            return;
        }
    }
    if (!bw_ra_toast_pop(&t)) {
        return;
    }
    bw_mutex_lock(g.lock);
    g_ban = t;
    g_ban_t = 0;
    g_ban_gen++;
    bw_mutex_unlock(g.lock);
}

/* EIDOS_RA_DEBUG_BANNER: a sample banner every ten seconds, to see the
 * banner in every layout and style without earning anything. */
static void debug_banner(void)
{
    static int on = -1;
    static unsigned n;

    if (on < 0) {
        on = getenv("EIDOS_RA_DEBUG_BANNER") != NULL;
    }
    if (on && ++n % 600 == 1) {
        toast("Bedroom Blitz", "Battle against Bianca and Cheren after receiving your first Pokemon", 5);
        g_unlocks++;
    }
}

/*
 * A probe for finding out whether the game's memory sits where
 * RetroAchievements expects it (docs/retroachievements.md, "Memory").
 * Off unless EIDOS_RA_DEBUG is set in the environment.  Every two seconds
 * it appends to <home>/ra-debug.log the values at a few RetroAchievements
 * addresses (EIDOS_RA_PROBE, hex addresses separated by commas, are read
 * as 16-bit values) and where DS main RAM sits in the host's memory; and
 * when the file <home>/ra-dump exists it writes the 4 MiB RetroAchievements
 * sees to <home>/ra-ram-<n>.bin and removes the file.
 */
static int g_dbg = -1;
static unsigned g_dbg_frame, g_dbg_dumps;

static void debug_frame(void)
{
    char path[640];
    FILE *f;
    const char *probe;

    if (g_dbg < 0) {
        g_dbg = getenv("EIDOS_RA_DEBUG") != NULL;
    }
    if (!g_dbg || ++g_dbg_frame % 120 != 0) {
        return;
    }
    snprintf(path, sizeof(path), "%s/ra-debug.log", g.home);
    f = fopen(path, "a");
    if (f != NULL) {
        unsigned long base = (unsigned long)(size_t)bw_ds_try_ptr(RA_RAM_BASE);
        unsigned long far_ = (unsigned long)(size_t)bw_ds_try_ptr(RA_RAM_BASE + 0x24F90Cu);

        fprintf(f, "frame %u: ram@%lx, +0x24F90C at %lx (offset %lx)", g_dbg_frame, base, far_,
                far_ - base);
        for (probe = getenv("EIDOS_RA_PROBE"); probe != NULL && *probe != '\0';) {
            char *end;
            unsigned long a = strtoul(probe, &end, 16);
            unsigned char v[2] = { 0, 0 };

            if (end == probe) {
                break;
            }
            ds_read((unsigned)(RA_RAM_BASE + a), v, 2);
            fprintf(f, " [%lx]=%u", a, (unsigned)v[0] | ((unsigned)v[1] << 8));
            probe = (*end == ',') ? end + 1 : end;
        }
        fprintf(f, " game=%d unlocked=%u/%u\n", g.st.game_loaded, g.st.unlocked, g.st.achievements);
        fclose(f);
    }
    snprintf(path, sizeof(path), "%s/ra-dump", g.home);
    f = fopen(path, "r");
    if (f != NULL) {
        unsigned char *buf = (unsigned char *)malloc(RA_RAM_SIZE);

        fclose(f);
        remove(path);
        if (buf != NULL) {
            unsigned off;

            for (off = 0; off < RA_RAM_SIZE; off += 0x1000) {
                if (ds_read(RA_RAM_BASE + off, buf + off, 0x1000) == 0) {
                    memset(buf + off, 0, 0x1000);
                }
            }
            snprintf(path, sizeof(path), "%s/ra-ram-%u.bin", g.home, ++g_dbg_dumps);
            f = fopen(path, "wb");
            if (f != NULL) {
                fwrite(buf, 1, RA_RAM_SIZE, f);
                fclose(f);
            }
            free(buf);
        }
    }
}

void bw_ra_tick(void)
{
    if (!g.up && bw_ra_start_default() != 0) {
        return;
    }
    bw_ra_frame();
    debug_banner();
    toast_frame();
    debug_frame();
}
