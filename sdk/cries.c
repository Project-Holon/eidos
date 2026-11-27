/*
 * sdk/cries.c -- the cry a species plays.
 *
 * The game plays a cry by its index, the number of a wave archive of the
 * sound archive (arm9 pm_voice.c, PM_VOICE_LoadWave): PM_VOICE_GetCryIndex
 * gives a species its own number (Shaymin's Sky Forme 650).  A species a
 * mod adds has no wave archive at its number, so a mod names the cry in
 * eidos/tables/cries.txt:
 *
 *     <species>[.<form>] = <wave archive>
 *
 * The wave archive is one of the game's cries (a species' number) or one a
 * mod adds to the sound archive.  A form with no line of its own takes its
 * species' line.  A later mod's line wins.  A species with no line keeps
 * the game's number, and an added one with none plays no cry (an empty
 * slot, whatever its number).
 *
 * Starting a cry whose wave did not load stops a voice that holds no
 * channel: the game's PM_VOICE_Stop then stops channel "0", a read at
 * address 8 that does nothing on the DS and faults on the PC.  The stop
 * here leaves a channel that was never taken alone (a guarded host fix);
 * the rest is the game's own.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "types.h"
#include "vfs.h"

#if defined(__GNUC__) || defined(__clang__)
#define GAME_FN __attribute__((weak))
#else
#define GAME_FN
#endif

/* ------------------------------------------------------------------ */
/* cries.txt                                                           */
/* ------------------------------------------------------------------ */

typedef struct Cry {
    u16 species, form;
    u16 wave;
} Cry;

static Cry *g_cries;
static u32 g_count, g_cap;
static int g_loaded;

static void cry_line(const char *path, u32 line, const char *text)
{
    unsigned species, form = 0, wave;
    int n = 0;
    u32 i;

    if ((sscanf(text, "%u.%u = %u %n", &species, &form, &wave, &n) != 3 || text[n] != '\0')
        && (form = 0, n = 0, sscanf(text, "%u = %u %n", &species, &wave, &n) != 2 || text[n] != '\0')) {
        fprintf(stderr, "eidos: %s:%u: expected `<species>[.<form>] = <wave archive>`\n", path, (unsigned)line);
        return;
    }
    if (species == 0 || species > 0xFFFFu || form > 0xFFFFu || wave > 0xFFFFu) {
        fprintf(stderr, "eidos: %s:%u: a number past 65535\n", path, (unsigned)line);
        return;
    }
    for (i = 0; i < g_count; i++) {
        if (g_cries[i].species == species && g_cries[i].form == form) {
            g_cries[i].wave = (u16)wave;    /* (a later mod's line wins) */
            return;
        }
    }
    if (g_count == g_cap) {
        u32 cap = g_cap != 0 ? g_cap * 2u : 32u;
        Cry *grown = (Cry *)realloc(g_cries, cap * sizeof *grown);

        if (grown == NULL) {
            return;
        }
        g_cries = grown;
        g_cap = cap;
    }
    g_cries[g_count].species = (u16)species;
    g_cries[g_count].form = (u16)form;
    g_cries[g_count].wave = (u16)wave;
    g_count++;
}

static void load(void)
{
    u32 m;

    g_loaded = 1;
    g_count = 0;
    for (m = 0; m < bw_mod_count(); m++) {
        const char *root = bw_mod_root_at(m);
        char path[1200], line[256];
        FILE *f;
        u32 n = 0;

        if (root == NULL || strlen(root) + 32 > sizeof path) {
            continue;
        }
        sprintf(path, "%s/eidos/tables/cries.txt", root);
        if ((f = fopen(path, "rb")) == NULL) {
            continue;
        }
        while (fgets(line, sizeof line, f) != NULL) {
            char *s = line, *e, *h;

            n++;
            if ((h = strchr(s, '#')) != NULL) {
                *h = '\0';
            }
            while (*s == ' ' || *s == '\t') {
                s++;
            }
            e = s + strlen(s);
            while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' || e[-1] == '\n')) {
                *--e = '\0';
            }
            if (*s != '\0') {
                cry_line(path, n, s);
            }
        }
        fclose(f);
    }
}

/* The mod list changed (sdk/bw_limits.c): read again when next asked. */
void bw_cries_forget(void)
{
    g_loaded = 0;
}

static int lookup(u32 species, u32 form, u32 *wave)
{
    u32 i;

    for (i = 0; i < g_count; i++) {
        if (g_cries[i].species == species && g_cries[i].form == form) {
            *wave = g_cries[i].wave;
            return 1;
        }
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* The game's two functions                                            */
/* ------------------------------------------------------------------ */

#define GAME_CRY_LAST 649u      /* the game's last species with a cry of its own number */
#define NO_CRY_SLOT   651u      /* an empty wave archive slot */

void PM_VOICE_GetCryIndex(u32 species, u32 form, u32 *cryIndex)
{
    if (!g_loaded) {
        load();
    }
    if (lookup(species, form, cryIndex) || (form != 0 && lookup(species, 0, cryIndex))) {
        return;
    }
    /* the game's: the species' own number, Shaymin's Sky Forme 650 */
    if (species == 492 && form == 1) {
        *cryIndex = 650;
        return;
    }
    /* an added species with no line plays no cry: wave archive 651, past
     * the game's cries.  The archive's slots 651..999 hold nothing, and the
     * merge appends a mod's wave archives past the game's 1986 (sdk/
     * vfs_sdat.h), so 651 stays empty; the species' own number would reach
     * the game's music and sound effect waves (1000..1985) and mods' waves. */
    if (species > GAME_CRY_LAST) {
        *cryIndex = NO_CRY_SLOT;
        return;
    }
    *cryIndex = species;
}

struct PmVoice;
struct NNSSndWaveOutHandle;

extern void NNS_SndWaveOutStop(struct NNSSndWaveOutHandle *handle) GAME_FN;
extern void NNS_SndWaveOutWaitIdle(struct NNSSndWaveOutHandle *handle) GAME_FN;
extern void NNSi_SndWaveOutUnlockChannel(u32 *channelNoPtr) GAME_FN;
extern void PM_VOICE_ResetParams(struct PmVoice *voice) GAME_FN;

/* struct PmVoice (arm9 pm_voice.c): active 0x00, channelA 0x14, channelB
 * 0x18, dual 0x2C.  The DS offsets: the voice is ARM9 data, laid out by the
 * game on every build. */
#define V_ACTIVE(v)   (*(u32 *)((char *)(v) + 0x00))
#define V_CHANNEL(v, k) (*(void **)((char *)(v) + 0x14 + 4 * (k)))
#define V_DUAL(v)     (*(u32 *)((char *)(v) + 0x2C))

void PM_VOICE_Stop(struct PmVoice *voice)
{
    if (V_CHANNEL(voice, 0) != NULL) {          /* (a voice that took no channel: nothing to stop) */
        NNS_SndWaveOutStop((struct NNSSndWaveOutHandle *)V_CHANNEL(voice, 0));
        NNS_SndWaveOutWaitIdle((struct NNSSndWaveOutHandle *)V_CHANNEL(voice, 0));
        NNSi_SndWaveOutUnlockChannel((u32 *)V_CHANNEL(voice, 0));
    }
    if (V_DUAL(voice) == 1 && V_CHANNEL(voice, 1) != NULL) {
        NNS_SndWaveOutStop((struct NNSSndWaveOutHandle *)V_CHANNEL(voice, 1));
        NNS_SndWaveOutWaitIdle((struct NNSSndWaveOutHandle *)V_CHANNEL(voice, 1));
        NNSi_SndWaveOutUnlockChannel((u32 *)V_CHANNEL(voice, 1));
    }
    V_ACTIVE(voice) = 0;
    PM_VOICE_ResetParams(voice);
}
