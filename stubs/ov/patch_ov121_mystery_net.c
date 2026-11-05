/*
 * patches: src/game/ov121/mystery_net.c MYSTERY_NET_SetAction MYSTERY_NET_GetAction MYSTERY_NET_GetReceivedData
 *
 * Mystery Gift's gifts from metaxy (port hook "mystery_gift", docs/hooks.md;
 * metaxy docs/SPEC.md section 4b).
 *
 * The game asks its network module for a gift through actions: 1 opens the
 * wireless session and 3 closes it, 4 starts the Nintendo WFC download (5
 * cancels it), 7 starts listening for a distribution's beacon (9 stops),
 * 10 and 12 the same for infrared.  It polls MYSTERY_NET_GetAction until
 * the module is idle again (0), then takes the result with
 * MYSTERY_NET_GetReceivedData: 1 and a 720-byte gift, or 2 for nothing
 * found (ov121/mystery.c, MYSTERY_TaskMenu and MYSTERY_TaskReceive).
 *
 * Nintendo's servers and the distribution machines are gone, so with the
 * hook on the module answers from the relay instead: the session is up at
 * once, and a WFC download or a beacon search asks the relay for the
 * events up today for this cartridge's language and game
 * (bw_mod_net_gift_*, sdk/mod_net.c), the ones given out that way kept.
 * With more than one, the module waits until the player picked one
 * (engine/ui/recipe_mystery.c lists them).  The chosen Wonder Card becomes
 * the game's gift: the card (0xCC bytes), the version field 0 (every
 * version), the card's text from the relay, and the CRC MYSTERY_DATA_CheckPacketCrc
 * checks, made with the game's own GF_STANDARD_CalcCrc16.  Infrared keeps
 * the game's own handlers.
 *
 * With the hook off these are the matched functions, unchanged.
 */

#include <stddef.h>

/* sdk/mod_hooks.h and sdk/mod_net.h, spelled out (docs/hooks.md). */
extern int bw_hook_enabled(const char *name);
extern int bw_mod_net_gift_start(const char *method, const char *language, const char *game,
                                 const char *date);
extern int bw_mod_net_gift_poll(void);
extern void bw_mod_net_gift_stop(void);
extern int bw_mod_net_gift_count(void);
extern const unsigned char *bw_mod_net_gift_card(int i);
extern int bw_mod_net_gift_text16(int i, unsigned short *out, int max);
extern int bw_mod_net_gift_chosen(void);

/* The game's own functions this file calls. */
typedef void (*MgHandler)(void *, int *, char *);
extern void MYSTERY_NET_SetHandler(void *p, MgHandler f);
extern int MYSTERY_NET_IsHandler(void *p, MgHandler value);
extern void MYSTERY_NET_NoopHandler(void *a, int *state, char *p);
extern void MYSTERY_NET_StartSession(void *a, int *state, char *p);
extern void MYSTERY_NET_StopSession(void *a, int *state, char *p);
extern void MYSTERY_NET_PollWihStatus(void *a, int *b, char *c);
extern void MYSTERY_NET_StartBeacon(void *a, int *state, char *p);
extern void MYSTERY_NET_DeleteBeacon(void *a, int *state, char *p);
extern void MYSTERY_NET_RunBeacon(void *a, int *b, char *p);
extern void MYSTERY_NET_StartIrc(void *a, int *state, char *p);
extern void MYSTERY_NET_DeleteIrc(void *a, int *state, char *p);
extern void MYSTERY_NET_RunIrc(void *a, int *state, char *p);
extern void MYSTERY_NET_RunNdDownload(void *task, int *state, char *p);
extern void MYSTERY_NET_EndDownload(void *a, int *state, char *p);
extern void MI_CpuCopy8(const void *src, void *dst, unsigned int size);
extern unsigned short GF_STANDARD_CalcCrc16(const unsigned char *src, unsigned int len);
extern void GF_STANDARD_GetCachedDate(void *date);
extern const unsigned char GAME_LANGUAGE_Code;
extern const unsigned char GAME_VERSION_Code;

/* MysteryNetWork (ov121/mystery_net_types.h), the members in the same
 * order so the host compiler gives them the game's offsets. */
typedef struct MgNdRecord {
    unsigned char data[0x93];
    signed char hexFlags[0x1D];
} MgNdRecord;

typedef struct MgNd {
    MgNdRecord records[10];
    unsigned int recordFlags[10];
    int state;
    int done;
    int error;
    int validCount;
    unsigned int progressRatio;
    unsigned int progressDone;
    unsigned int progressTotal;
    int selectedIndex;
    void *destBuffer;
    unsigned int timer;
    int busy;
} MgNd;

typedef struct MgNet {
    MgHandler handler;
    int paused;
    int state;
    void *handlerArg;
    void *wihHandle;
    unsigned char buffer[0x20];
    unsigned char unknown34[0x20];
    unsigned int result;
    unsigned short heapId;
    unsigned char unknown5A[2];
    int flag5C;
    MgNd nd;
    void *param;
    void *beacon;
    void *irc;
    unsigned char unknown7A0[4];
    unsigned char recvBuffer[0x1000];
    int recvState;
} MgNet;

/* The game's RTCDate: years since 2000, month, day, weekday. */
typedef struct MgDate { int y, m, d, week; } MgDate;

#define MG_PACKET      0x2D0u   /* the gift: MysteryPacket                 */
#define MG_CARD        0xCCu    /* the Wonder Card at its head             */
#define MG_VERSIONS    0xCCu    /* u32: the versions it is for, 0 all      */
#define MG_TEXT        0xD0u    /* u16[253]: its message                   */
#define MG_CRC         0x2CEu   /* u16: GF_STANDARD_CalcCrc16 of the rest  */

/* The hook's own state: 0 off (the game's handlers), 2 the session is up,
 * 4 a WFC download, 8 a beacon search (the actions GetAction reports
 * while they run). */
static int g_fake;

/* The overlay's link header declares the C library's copy functions its own
 * way: the few bytes here are copied by hand. */
static void mg_copy(unsigned char *dst, const unsigned char *src, unsigned int n)
{
    while (n-- > 0) {
        *dst++ = *src++;
    }
}

static void mg_zero(unsigned char *dst, unsigned int n)
{
    while (n-- > 0) {
        *dst++ = 0;
    }
}

static int mg_hook(void)
{
    return bw_hook_enabled("mystery_gift");
}

static const char *mg_language(void)
{
    switch (GAME_LANGUAGE_Code) {
    case 1: return "ja";
    case 3: return "fr";
    case 4: return "it";
    case 5: return "de";
    case 7: return "es";
    case 8: return "ko";
    default: return "en";
    }
}

/* Ask the relay; the module then reports the action until it has the
 * answer (and the player's pick). */
static void mg_start(const char *method, int action)
{
    MgDate d;
    char date[16];
    unsigned int y;

    mg_zero((unsigned char *)&d, sizeof d);
    GF_STANDARD_GetCachedDate(&d);
    y = 2000u + (unsigned int)d.y;
    /* YYYY-MM-DD by hand: the overlay links no printf */
    date[0] = (char)('0' + y / 1000u % 10u);
    date[1] = (char)('0' + y / 100u % 10u);
    date[2] = (char)('0' + y / 10u % 10u);
    date[3] = (char)('0' + y % 10u);
    date[4] = '-';
    date[5] = (char)('0' + (unsigned int)d.m / 10u % 10u);
    date[6] = (char)('0' + (unsigned int)d.m % 10u);
    date[7] = '-';
    date[8] = (char)('0' + (unsigned int)d.d / 10u % 10u);
    date[9] = (char)('0' + (unsigned int)d.d % 10u);
    date[10] = '\0';
    (void)bw_mod_net_gift_start(method, mg_language(), GAME_VERSION_Code == 20 ? "white" : "black", date);
    g_fake = action;
}

/* The gift the player gets: the one picked, or the only one. */
static int mg_pick(void)
{
    int n = bw_mod_net_gift_count();

    if (n <= 0) {
        return -1;
    }
    if (n == 1) {
        return 0;
    }
    return bw_mod_net_gift_chosen();
}

void MYSTERY_NET_SetAction(char *p, unsigned action)
{
    if (mg_hook()) {
        switch (action) {
        case 1: g_fake = 2; return;                 /* the session is up   */
        case 3: g_fake = 0; return;
        case 4: mg_start("wfc", 4); return;
        case 7: mg_start("wireless", 8); return;
        case 5: case 9: case 0:
            if (g_fake != 0) {
                bw_mod_net_gift_stop();
                g_fake = 0;
                return;
            }
            break;
        default:
            break;                                  /* infrared: the game's */
        }
    }
    switch (action) {
    case 1:
        if (MYSTERY_NET_IsHandler((void *)p, MYSTERY_NET_NoopHandler)) MYSTERY_NET_SetHandler((void *)p, MYSTERY_NET_StartSession);
        break;
    case 3:
        if (MYSTERY_NET_IsHandler((void *)p, MYSTERY_NET_PollWihStatus)) MYSTERY_NET_SetHandler((void *)p, MYSTERY_NET_StopSession);
        break;
    case 4:
        if (MYSTERY_NET_IsHandler((void *)p, MYSTERY_NET_NoopHandler)) MYSTERY_NET_SetHandler((void *)p, MYSTERY_NET_RunNdDownload);
        break;
    case 5:
        if (MYSTERY_NET_IsHandler((void *)p, MYSTERY_NET_RunNdDownload)) ((MgNet *)p)->nd.done = 1;
        break;
    case 6: MYSTERY_NET_SetHandler((void *)p, MYSTERY_NET_EndDownload); break;
    case 7: MYSTERY_NET_SetHandler((void *)p, MYSTERY_NET_StartBeacon); break;
    case 9: MYSTERY_NET_SetHandler((void *)p, MYSTERY_NET_DeleteBeacon); break;
    case 10: MYSTERY_NET_SetHandler((void *)p, MYSTERY_NET_StartIrc); break;
    case 12: MYSTERY_NET_SetHandler((void *)p, MYSTERY_NET_DeleteIrc); break;
    case 0: MYSTERY_NET_SetHandler((void *)p, MYSTERY_NET_NoopHandler); break;
    }
}

int MYSTERY_NET_GetAction(void *p)
{
    if (mg_hook() && g_fake != 0) {
        if (g_fake == 2) {
            return 2;
        }
        /* a download or a search: busy until the relay answered and, with
         * several gifts, until the player picked one */
        if (bw_mod_net_gift_poll() == 0) {
            return g_fake;
        }
        if (bw_mod_net_gift_count() > 1 && bw_mod_net_gift_chosen() < 0) {
            return g_fake;
        }
        return 0;
    }
    if (MYSTERY_NET_IsHandler(p, MYSTERY_NET_NoopHandler)) return 0;
    if (MYSTERY_NET_IsHandler(p, MYSTERY_NET_StartSession)) return 1;
    if (MYSTERY_NET_IsHandler(p, MYSTERY_NET_PollWihStatus)) return 2;
    if (MYSTERY_NET_IsHandler(p, MYSTERY_NET_StopSession)) return 3;
    if (MYSTERY_NET_IsHandler(p, MYSTERY_NET_RunNdDownload)) return 4;
    if (MYSTERY_NET_IsHandler(p, MYSTERY_NET_EndDownload)) return 6;
    if (MYSTERY_NET_IsHandler(p, MYSTERY_NET_StartBeacon)) return 7;
    if (MYSTERY_NET_IsHandler(p, MYSTERY_NET_RunBeacon)) return 8;
    if (MYSTERY_NET_IsHandler(p, MYSTERY_NET_DeleteBeacon)) return 9;
    if (MYSTERY_NET_IsHandler(p, MYSTERY_NET_StartIrc)) return 10;
    if (MYSTERY_NET_IsHandler(p, MYSTERY_NET_RunIrc)) return 11;
    if (MYSTERY_NET_IsHandler(p, MYSTERY_NET_DeleteIrc)) return 12;
    return 0;
}

int MYSTERY_NET_GetReceivedData(unsigned char *p, void *a, unsigned int b)
{
    if (mg_hook() && (g_fake == 4 || g_fake == 8)) {
        unsigned char *out = (unsigned char *)a;
        const unsigned char *card;
        unsigned short crc;
        unsigned int i;
        int k;

        if (bw_mod_net_gift_poll() == 0) {
            return 0;
        }
        k = mg_pick();
        card = k >= 0 ? bw_mod_net_gift_card(k) : NULL;
        if (card == NULL || b < MG_PACKET) {
            return 2;                               /* nothing found       */
        }
        mg_zero(out, MG_PACKET);
        mg_copy(out, card, MG_CARD);
        for (i = 0; i < 4; i++) {
            out[MG_VERSIONS + i] = 0;               /* every version       */
        }
        {
            /* the card's text (the gift's details window shows it): 253
             * units, the last the end */
            unsigned short units[253];
            int n = bw_mod_net_gift_text16(k, units, 253);

            for (i = 0; i <= (unsigned int)n && i < 253; i++) {
                out[MG_TEXT + 2 * i] = (unsigned char)(units[i] & 0xFF);
                out[MG_TEXT + 2 * i + 1] = (unsigned char)(units[i] >> 8);
            }
        }
        crc = GF_STANDARD_CalcCrc16(out, MG_CRC);
        out[MG_CRC] = (unsigned char)(crc & 0xFF);
        out[MG_CRC + 1] = (unsigned char)(crc >> 8);
        return 1;
    }
    {
        int s = ((MgNet *)p)->recvState;

        if (s == 1) {
            MI_CpuCopy8(((MgNet *)p)->recvBuffer, a, b);
            return 1;
        }
        return s;
    }
}
