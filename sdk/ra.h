/*
 * sdk/ra.h -- RetroAchievements (docs/retroachievements.md).
 *
 * The port signs the player in to retroachievements.org, identifies the
 * game by the hash of the extracted ROM parts (sdk/ra_hash.h), loads the
 * game's achievement set and, each game frame, lets rcheevos read the
 * game's RAM at the DS's own addresses and say what the player has
 * earned.  Softcore only: hardcore needs RetroAchievements' approval of
 * this client.
 *
 * Threads.  Everything here is called on the game's frame thread (the
 * settings screen draws there too).  The network runs on the module's own
 * worker thread (sdk/ra_http.c); its answers wait in a queue until
 * bw_ra_frame or bw_ra_idle hands them to rcheevos, so the frame never
 * blocks on a server.
 *
 * The player's password is passed to rcheevos once, to sign in, and never
 * kept.  What is kept is the token the server answers with, in
 * <home>/retroachievements.ini, readable by the owner only.
 */

#ifndef BW_PORT_RA_H
#define BW_PORT_RA_H

#ifdef __cplusplus
extern "C" {
#endif

/* Reads `n` bytes of the game's memory at DS address `addr` into `dst`.
 * Returns the bytes read (0 when the address is not backed). */
typedef unsigned (*BwRaReadFn)(unsigned addr, void *dst, unsigned n);

typedef struct BwRaConfig {
    const char *home;         /* folder of retroachievements.ini            */
    const char *data_root;    /* the extracted ROM (header.bin, arm9.bin..) */
    const char *version;      /* the port's version, for the User-Agent     */
    BwRaReadFn  read;         /* the game's memory                          */
} BwRaConfig;

typedef enum {
    BW_RA_OFF = 0,            /* not started (no library, or disabled)      */
    BW_RA_SIGNED_OUT,
    BW_RA_SIGNING_IN,
    BW_RA_SIGNED_IN,
    BW_RA_ERROR               /* the last sign-in failed: see `message`     */
} BwRaState;

typedef struct BwRaStatus {
    BwRaState state;
    char      user[64];       /* display name when signed in                */
    char      message[160];   /* the last sentence worth showing            */
    int       game_loaded;    /* an achievement set is running              */
    char      game_title[96];
    unsigned  achievements;   /* in the set                                 */
    unsigned  unlocked;       /* earned so far                              */
} BwRaStatus;

typedef struct BwRaToast {
    char     title[96];
    char     text[160];
    unsigned points;          /* 0 for a plain message                      */
} BwRaToast;

/* Starts the module; signs in with a saved token and loads the game.
 * Returns 0, or -1 when the module is not in this build. */
int  bw_ra_init(const BwRaConfig *cfg);
void bw_ra_shutdown(void);

/* The port's own start: the data root as home and ROM parts, the game's
 * memory through bw_ds_try_ptr, the build's version.  Runs once, on the
 * first call; later calls do nothing.  bw_ra_tick is that and bw_ra_frame
 * in one, what the port's frame hook calls. */
int  bw_ra_start_default(void);
void bw_ra_tick(void);

/* Once per game frame, after the game's own frame work. */
void bw_ra_frame(void);
/* While the game is not running frames (a menu): the queue and rcheevos's
 * own timers still move. */
void bw_ra_idle(void);
/* The run restarted or a save was loaded: the set starts again. */
void bw_ra_reset(void);

/* Signs in; the answer arrives through bw_ra_status. */
int  bw_ra_login(const char *user, const char *password);
void bw_ra_logout(void);
void bw_ra_status(BwRaStatus *out);

/* The achievement sound: achievement.wav, from the profile folder or the
 * folder above the game data, converted once to the game's own 32768 Hz
 * stereo.  The game's audio path calls bw_ra_sound_mix on each block it
 * renders: the sound is added to it while one is playing. */
void bw_ra_sound_mix(short *buf, unsigned frames);

/* An achievement the player has earned. */
typedef struct BwRaRecent {
    char     title[96];
    char     text[160];
    unsigned points;
    long long when;           /* unix time of the unlock, 0 if unknown      */
} BwRaRecent;

/* The player's latest unlocks of the loaded set, newest first (earlier
 * sessions included): fills up to `max` entries and returns how many. */
int  bw_ra_recent(BwRaRecent *out, int max);

/* The banner on show, for the renderers (engine/ui/ui.c for the one-screen
 * layout, engine/platform/ra_banner_draw.c for the others).  `progress` is
 * how far down it is, 0 (hidden) to 256 (all the way); `generation`
 * changes when a new banner begins.  Returns 1 while there is one. */
typedef struct BwRaBannerView {
    int      active;
    unsigned generation;
    int      progress;
    unsigned points;
    char     title[96];
    char     text[160];
} BwRaBannerView;
int  bw_ra_banner(BwRaBannerView *v);

/* The oldest toast waiting; 1 when `out` was filled, else 0. */
int  bw_ra_toast_pop(BwRaToast *out);

#ifdef __cplusplus
}
#endif

#endif /* BW_PORT_RA_H */
