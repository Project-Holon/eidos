/*
 * sdk/mp.h -- multiplayer: the lobby, the start and the pause
 * (docs/multiplayer.md; metaxy docs/SPEC.md, Part B).
 *
 * The relay only forwards (sdk/mod_net.c).  Every multiplayer decision is
 * the host's game's: who is in the lobby, the roster, when the game starts,
 * when it pauses.  The games talk with messages under the port's own key
 * (BW_MOD_NET_PORT_KEY), in the text format docs/multiplayer.md gives.
 *
 * The layer runs on the game's frame: the network's port handler feeds it,
 * and bw_mp_frame runs its timers (the 15-second grace after a drop, the
 * 10 minutes a host has to come back, the rejoin attempts).
 */
#ifndef BW_PORT_MP_H
#define BW_PORT_MP_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The layer's states. */
typedef enum BwMpState {
    BW_MP_OFF = 0,        /* no multiplayer session                          */
    BW_MP_LOBBY,          /* in the lobby, before the start                  */
    BW_MP_PLAYING,        /* the game runs                                   */
    BW_MP_GRACE,          /* a player dropped: the game runs 15 s more       */
    BW_MP_PAUSED,         /* every game is held until the host starts again  */
    BW_MP_HOST_GONE,      /* a joiner: the host is gone, the game is held    */
    BW_MP_RECONNECTING,   /* this game lost the relay and joins again        */
    BW_MP_ENDED           /* the session is over (the reason says why)       */
} BwMpState;

/* The longest name and mod list the layer keeps for a player. */
#define BW_MP_NAME_MAX 64u
#define BW_MP_MODS_MAX 512u
#define BW_MP_PLAYERS_MAX 16u

/* How long the others play on after a player drops, and how long a host
 * has to come back. */
#ifndef BW_MP_GRACE_MS
#define BW_MP_GRACE_MS (15u * 1000u)
#endif
#ifndef BW_MP_HOST_WAIT_MS
#define BW_MP_HOST_WAIT_MS (10u * 60u * 1000u)
#endif
#ifndef BW_MP_REJOIN_EVERY_MS
#define BW_MP_REJOIN_EVERY_MS (5u * 1000u)
#endif

/* One player as the lobby shows it. */
typedef struct BwMpPlayer {
    u32  peer;                       /* the relay's peer number (1 the host) */
    char name[BW_MP_NAME_MAX + 1];
    int  connected;
    int  on_roster;                  /* the roster is set at the first start */
    char game;                       /* 'B' or 'W', 0 until the player said  */
    int  mods_ok;                    /* its multiplayer mods match the host's */
    char mods[BW_MP_MODS_MAX + 1];   /* "ns=n;ns=n", or "" for none          */
    char fp[33];                     /* who the player is to this host        */
    int  in_game;                    /* its game runs its multiplayer save    */
    char version[32];                /* its eidos version, "" until it said   */
} BwMpPlayer;

/* This build's eidos version (CMake's EIDOS_VERSION): every player of a
 * game runs the same one (docs/distribution.md). */
const char *bw_mp_version(void);

/* Host a new session (a new code each time); the code goes to `code`. */
int  bw_mp_host(char *code, u32 size);

/* Join the session `code` names. */
int  bw_mp_join(const char *code);

/* Leave the session (the lobby and the game both). */
void bw_mp_leave(void);

/* Take over a session the port opened itself (the settings' worker hosts
 * or joins off the frame thread): `host` says which, `code` is its code. */
void bw_mp_attach(int host, const char *code);

/* The first start of the game (the lobby's Start, or the host's start
 * reaching a joiner): 1 once, then 0.  pending only looks. */
int  bw_mp_take_started(void);
int  bw_mp_started_pending(void);

/* This player's new name: the host's own roster line, or a message to the
 * host, which sends the roster again. */
void bw_mp_rename(const char *name);

/* A number that changes whenever the lobby's rows would. */
u32  bw_mp_stamp(void);

/*
 * The host starts the game: from the lobby, or again after a pause.  0 when
 * the game started, -1 when it cannot yet (bw_mp_start_blocker says why).
 */
int  bw_mp_start(void);

/* Why the host cannot start now ("" when it can). */
const char *bw_mp_start_blocker(void);

BwMpState bw_mp_state(void);
int  bw_mp_is_host(void);

/* 1 while the game must be held: paused, the host gone, reconnecting. */
int  bw_mp_paused(void);

/* The host: how many players' saves have not come in since the host last
 * saved (each joiner's game saves at its next quiet moment).  The window's
 * close waits for them a little (demo/eidos_boot.c). */
int  bw_mp_saves_pending(void);

/* The host's game saved: every player's game is asked to save (the frame
 * calls it when the host's save file changes). */
void bw_mp_host_saved(void);

/* The shared table (SPEC section 17): numbers every player's mods see,
 * the host's copy the real one.  `ns` is the mod's namespace.  get: 0 set,
 * 1 not set (*out 0), -1 outside a multiplayer game; set: 0 made or sent,
 * -1 outside one.  The _calling forms are the services table's: the
 * namespace is the calling mod's. */
int  bw_mp_shared_get(const char *ns, const char *key, s32 *out);
int  bw_mp_shared_set(const char *ns, const char *key, s32 value);
int  bw_mp_shared_get_calling(const char *key, s32 *out);
int  bw_mp_shared_set_calling(const char *key, s32 value);

/* The host's own changes (no change is lost when two players change a key
 * at the same moment): add adds delta; cas sets value only when the value
 * is expected then.  0 made or sent, -1 outside a multiplayer game. */
int  bw_mp_shared_add(const char *ns, const char *key, s32 delta);
int  bw_mp_shared_cas(const char *ns, const char *key, s32 expected, s32 value);
int  bw_mp_shared_add_calling(const char *key, s32 delta);
int  bw_mp_shared_cas_calling(const char *key, s32 expected, s32 value);

/* The line the "waiting for players" screen shows. */
const char *bw_mp_wait_text(void);

/* The code the session has now: a host whose relay restarted opens a new
 * one and shows it (the players join it again). */
const char *bw_mp_code(void);

/* Why the session ended (BW_MP_ENDED), or why the code changed. */
const char *bw_mp_end_reason(void);

/* Whether the reason is the relay's (it restarted, or cannot be reached). */
int bw_mp_relay_restarted(void);

/* The players, in peer order (the host first). */
u32  bw_mp_player_count(void);
int  bw_mp_player_at(u32 index, BwMpPlayer *out);

/* The timers: once a frame, also while the game is held. */
void bw_mp_frame(void);

/* The same with the time given (tests). */
void bw_mp_frame_at(u64 now_ms);

/* A frame's wait for a held game without a window (16 ms). */
void bw_mp_hold_wait(void);

/* This game's own part: its game ('B' or 'W') and its multiplayer mods
 * ("ns=n;ns=n").  The port sets both before a session; the mods default to
 * the loaded mods that state a `multiplayer` number. */
void bw_mp_set_local(char game, const char *mods);

/* The network's port handler (sdk/mod_net.h); bw_mp_host and bw_mp_join
 * install it.  Exposed for the tests. */
void bw_mp_on_net(u32 peer, int kind, const void *buf, u32 size);

/* Forget everything (the port's reset). */
void bw_mp_reset(void);

/*
 * How a joiner's game is started on the save the host sent: the port boots
 * the game again, on the save in force (new_game 0) or on a new game.  The
 * port sets it (demo/eidos_boot.c).
 */
void bw_mp_set_boot(void (*fn)(int new_game));

/* 1 on a joiner's game in a multiplayer game: its field menu has no Save
 * (only the host saves). */
int  bw_mp_hide_save(void);

/* The port's own check: the next quiet moment saves (as when the host
 * saved), on the host's game too.  A quiet moment is a frame where the
 * field runs and no field event (a menu, a script, a warp) does. */
void bw_mp_request_save_for_test(void);

/* 1 when the player `peer` may be drawn as an avatar: connected now, and
 * not this game's own player.  Exposed for the tests. */
int  bw_mp_peer_visible(u32 peer);

/* The save delta a joiner uploads (tests): see sdk/mp.c. */
int  bw_mp_delta_test(const u8 *base, const u8 *now, u32 size, u32 *dsize, int *bad_ok);

/* The folder of the multiplayer files: <save folder>/multiplayer. */
int  bw_mp_dir(char *out, u32 size);

/* The host's games: each one a folder of <save>/multiplayer/games with
 * its own saves.  The list is newest first; select chooses the game the
 * next host plays, create makes a new one from a name (made a folder name,
 * numbered when taken) and chooses it.  0 or a count on success. */
#define BW_MP_GAME_NAME_MAX 24
#define BW_MP_GAMES_MAX     32
int  bw_mp_game_list(char names[][BW_MP_GAME_NAME_MAX], int max);
int  bw_mp_game_select(const char *name);
int  bw_mp_game_create(const char *name, char *out, u32 size);
const char *bw_mp_game(void);

/* The network calls the layer makes (sdk/mod_net.h by default); the tests
 * put their own in.  NULL puts the default back. */
typedef struct BwMpNet {
    int  (*host)(char *code_out, u32 size);
    int  (*join)(const char *code);
    void (*leave)(void);
    int  (*send)(u32 peer, const void *buf, u32 size);
    int  (*remove)(u32 peer);
    u32  (*self_peer)(void);
    int  (*peer_name)(u32 peer, char *out, u32 size);
    const char *(*own_name)(void);
    const char *(*last_error)(void);
} BwMpNet;

void bw_mp_set_net(const BwMpNet *net);

#ifdef __cplusplus
}
#endif

#endif /* BW_PORT_MP_H */
