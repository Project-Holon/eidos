/*
 * sdk/mod_v7.c -- the state calls of mod API version 7: the Pokedex's
 * National mode and a Wonder Card from a mod (docs/mod-services.md).
 *
 * The National mode
 * -----------------
 *
 * One flag of the Pokedex save.  The game sets it with
 * ZUKAN_SAVE_EnableNationalDex (its script command
 * SCRCMD_WORK_CmdEnableNationalDex does nothing else) and has no call that
 * clears it, so the port has none either.
 *
 * A Wonder Card
 * -------------
 *
 * The game's Mystery Gift screen receives a card this way (ov121
 * mystery.c): when the card says "once" and the save's flag of the card's
 * id is set, it refuses; else it dates the card, MYSTERY_DATA_AddCard puts
 * it in the first free album slot and MYSTERY_DATA_SetEventFlag sets the
 * id's flag.  MYSTERY_DATA_AddCard itself does not look for a card that is
 * there already.
 *
 * A mod adds its card each time a game loads, so the port's call is "once
 * per save" for every card: it adds nothing when the id's flag is set (a
 * card the player deleted from the album after its gift does not come
 * back) or when the album holds a card of that id.  Then it does what the
 * screen does, through the game's own calls.
 *
 * symbols:
 *   ZUKAN_SAVE_GetFromGameData        0x0200CA1C  thumb  arm9
 *   ZUKAN_SAVE_EnableNationalDex      0x0200CA28  thumb  arm9
 *   ZUKAN_SAVE_IsNationalDexEnabled   0x0200CA38  thumb  arm9
 *   MYSTERY_DATA_GetCard              0x0200A000  thumb  arm9
 *   MYSTERY_DATA_AddCard              0x0200A034  thumb  arm9
 *   MYSTERY_DATA_IsEventFlag          0x0200A1A8  thumb  arm9
 *   MYSTERY_DATA_SetEventFlag         0x0200A1E4  thumb  arm9
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "port.h"
#include "md5.h"
#include "mod_menu.h"
#include "mod_v7.h"
#include "plat_thread.h"

extern void *bw_mod_state_game_data(void);

struct GameData;
struct SaveControl;
struct ZukanSave;
struct MysteryData;

extern void *ZUKAN_SAVE_GetFromGameData(void *gameData) __attribute__((weak));
extern struct ZukanSave *ZUKAN_SAVE_EnableNationalDex(struct ZukanSave *zukan)
    __attribute__((weak));
extern u32 ZUKAN_SAVE_IsNationalDexEnabled(struct ZukanSave *zukan) __attribute__((weak));

extern struct SaveControl *GAME_DATA_GetSaveControl(struct GameData *gd)
    __attribute__((weak));
extern void MYSTERY_DATA_Load(struct MysteryData *data, struct SaveControl *save, int mode,
                              int heapId) __attribute__((weak));
extern void MYSTERY_DATA_Unload(struct MysteryData *data) __attribute__((weak));
extern u32 MYSTERY_DATA_GetCardMax(struct MysteryData *data) __attribute__((weak));
extern int MYSTERY_DATA_GetCard(struct MysteryData *data, u32 slot, void *out)
    __attribute__((weak));
extern int MYSTERY_DATA_AddCard(struct MysteryData *data, const void *card)
    __attribute__((weak));
extern int MYSTERY_DATA_IsEventFlag(struct MysteryData *data, u32 flag) __attribute__((weak));
extern void MYSTERY_DATA_SetEventFlag(struct MysteryData *data, u32 flag)
    __attribute__((weak));

/* The SDK's date, as GF_STANDARD_GetCachedDate fills it. */
typedef struct BwV7Date {
    u32 year;       /* 0 to 99, from 2000 */
    u32 month;
    u32 day;
    u32 week;
} BwV7Date;
extern void GF_STANDARD_GetCachedDate(BwV7Date *date) __attribute__((weak));

/* ------------------------------------------------------------------ */
/* The National mode                                                   */
/* ------------------------------------------------------------------ */

static struct ZukanSave *zukan(void)
{
    void *gd = bw_mod_state_game_data();

    if (gd == NULL || ZUKAN_SAVE_GetFromGameData == NULL) {
        return NULL;
    }
    return (struct ZukanSave *)ZUKAN_SAVE_GetFromGameData(gd);
}

s32 bw_mod_v7_national_dex(void)
{
    struct ZukanSave *z = zukan();

    if (z == NULL || ZUKAN_SAVE_IsNationalDexEnabled == NULL) {
        return -1;
    }
    return ZUKAN_SAVE_IsNationalDexEnabled(z) != 0 ? 1 : 0;
}

int bw_mod_v7_national_dex_set(void)
{
    struct ZukanSave *z = zukan();

    if (z == NULL || ZUKAN_SAVE_EnableNationalDex == NULL) {
        return -1;
    }
    ZUKAN_SAVE_EnableNationalDex(z);
    return 0;
}

/* ------------------------------------------------------------------ */
/* A Wonder Card                                                       */
/* ------------------------------------------------------------------ */

static u32 card_id(const u8 *card)
{
    return (u32)card[BW_MYSTERY_CARD_ID] | ((u32)card[BW_MYSTERY_CARD_ID + 1u] << 8);
}

int bw_mod_v7_mystery_card_ok(const void *pgf, u32 size)
{
    const u8 *card = (const u8 *)pgf;

    /* (the gift types the game takes: MYSTERY_DATA_IsValidGiftType) */
    return card != NULL && size == BW_MYSTERY_CARD_SIZE &&
           card[BW_MYSTERY_CARD_TYPE] >= 1u && card[BW_MYSTERY_CARD_TYPE] <= 3u &&
           card_id(card) < BW_MYSTERY_CARD_ID_MAX;
}

s32 bw_mod_v7_mystery_card_add(const void *pgf, u32 size)
{
    /* The game's handle (struct MysteryData: a buffer pointer, a save type
     * and the extra save's work) is filled by MYSTERY_DATA_Load.  The port
     * gives it room here, as MYSTERY_DATA_Init does on its own stack, so
     * no game heap is needed where a mod calls from. */
    union {
        void *words[4];
        u32   align;
    } room;
    struct MysteryData *data = (struct MysteryData *)(void *)&room;
    void *gd = bw_mod_state_game_data();
    struct SaveControl *save;
    u8 card[BW_MYSTERY_CARD_SIZE];
    u8 held[BW_MYSTERY_CARD_SIZE];
    u32 id, max, i;
    s32 slot = -1;
    s32 result;

    if (!bw_mod_v7_mystery_card_ok(pgf, size) || gd == NULL ||
        GAME_DATA_GetSaveControl == NULL || MYSTERY_DATA_Load == NULL ||
        MYSTERY_DATA_Unload == NULL || MYSTERY_DATA_GetCardMax == NULL ||
        MYSTERY_DATA_GetCard == NULL || MYSTERY_DATA_AddCard == NULL ||
        MYSTERY_DATA_IsEventFlag == NULL || MYSTERY_DATA_SetEventFlag == NULL) {
        return BW_MYSTERY_ADD_INVALID;
    }
    save = GAME_DATA_GetSaveControl((struct GameData *)gd);
    if (save == NULL) {
        return BW_MYSTERY_ADD_INVALID;
    }
    memcpy(card, pgf, sizeof card);
    id = card_id(card);

    memset(&room, 0, sizeof room);
    MYSTERY_DATA_Load(data, save, 0, 0);    /* mode 0: the main save's block */

    if (MYSTERY_DATA_IsEventFlag(data, id) != 0) {
        result = BW_MYSTERY_ADD_KNOWN;
    } else {
        result = 0;
        max = MYSTERY_DATA_GetCardMax(data);
        for (i = 0; i < max; i++) {
            if (MYSTERY_DATA_GetCard(data, i, held) == 0) {
                if (slot < 0) {
                    slot = (s32)i;      /* the first free slot: where AddCard puts it */
                }
            } else if (card_id(held) == id) {
                result = BW_MYSTERY_ADD_KNOWN;
            }
        }
        if (result == 0 && slot < 0) {
            result = BW_MYSTERY_ADD_FULL;
        }
    }
    if (result == 0) {
        /* A card file can come out of a save where its gift was handed
         * over: this one is new. */
        card[BW_MYSTERY_CARD_FLAGS] &= (u8)~2u;
        if (GF_STANDARD_GetCachedDate != NULL) {
            BwV7Date date;

            /* (the screen's own packing: ov121 mystery.c) */
            memset(&date, 0, sizeof date);
            GF_STANDARD_GetCachedDate(&date);
            card[BW_MYSTERY_CARD_DATE] = (u8)date.day;
            card[BW_MYSTERY_CARD_DATE + 1u] = (u8)date.month;
            card[BW_MYSTERY_CARD_DATE + 2u] = (u8)((date.year + 2000u) & 0xFFu);
            card[BW_MYSTERY_CARD_DATE + 3u] = (u8)((date.year + 2000u) >> 8);
        }
        if (MYSTERY_DATA_AddCard(data, card) != 0) {
            MYSTERY_DATA_SetEventFlag(data, id);
            result = slot;
        } else {
            result = BW_MYSTERY_ADD_FULL;
        }
    }
    MYSTERY_DATA_Unload(data);
    return result;
}

/* ------------------------------------------------------------------ */
/* The clock                                                           */
/* ------------------------------------------------------------------ */

s64 bw_mod_v7_time_utc(void)
{
    return (s64)time(NULL);
}

u64 bw_mod_v7_ticks_ms(void)
{
    return (u64)(bw_clock_ns() / 1000000ull);
}

/* ------------------------------------------------------------------ */
/* The game's hash                                                     */
/* ------------------------------------------------------------------ */

/* The whole file `name` in `dir`, malloc'd; NULL when it cannot be read. */
static u8 *read_whole(const char *dir, const char *name, u32 *size)
{
    char path[1200];
    FILE *fp;
    long n;
    u8 *buf = NULL;

    *size = 0;
    if (snprintf(path, sizeof path, "%s/%s", dir, name) >= (int)sizeof path
        || (fp = fopen(path, "rb")) == NULL) {      /* (as the file layer opens the game's files) */
        return NULL;
    }
    if (fseek(fp, 0, SEEK_END) == 0 && (n = ftell(fp)) >= 0 && fseek(fp, 0, SEEK_SET) == 0
        && (buf = (u8 *)malloc((size_t)n + 1u)) != NULL) {
        if (fread(buf, 1, (size_t)n, fp) == (size_t)n) {
            *size = (u32)n;
        } else {
            free(buf);
            buf = NULL;
        }
    }
    fclose(fp);
    return buf;
}

static u32 le32(const u8 *p)
{
    return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}

int bw_mod_v7_ra_hash_dir(const char *extract_dir, char out[33])
{
    u8 *header, *arm9, *arm7, *banner, icon[0xA00];
    u32 nh, n9, n7, nb, s9, s7;
    int rc = -1;

    header = read_whole(extract_dir, "header.bin", &nh);
    arm9 = read_whole(extract_dir, "arm9.bin", &n9);
    arm7 = read_whole(extract_dir, "arm7.bin", &n7);
    banner = read_whole(extract_dir, "banner.bin", &nb);
    if (header != NULL && arm9 != NULL && arm7 != NULL && banner != NULL && nh >= 0x160u) {
        s9 = le32(header + 0x2C);
        s7 = le32(header + 0x3C);
        if (s9 <= n9 && s7 <= n7 && (u64)s9 + s7 <= 16u * 1024u * 1024u) {
            BwMd5 m;
            u8 digest[16];

            /* rcheevos' rc_hash_nintendo_ds: the header's first 0x160
             * bytes, the ARM9 and the ARM7 code at the sizes the header
             * gives, and 0xA00 bytes of the icon and titles, zero-padded */
            memset(icon, 0, sizeof icon);
            memcpy(icon, banner, nb < sizeof icon ? nb : sizeof icon);
            bw_md5_init(&m);
            bw_md5_append(&m, header, 0x160u);
            bw_md5_append(&m, arm9, s9);
            bw_md5_append(&m, arm7, s7);
            bw_md5_append(&m, icon, sizeof icon);
            bw_md5_final(&m, digest);
            bw_md5_hex(digest, out);
            rc = 0;
        }
    }
    free(header);
    free(arm9);
    free(arm7);
    free(banner);
    return rc;
}

int bw_mod_v7_game_hash(u32 kind, char *out, u32 size)
{
    static char hash[33];
    static u32 seen;
    static int known;
    char dir[1100];
    size_t n;

    if (kind != BW_GAME_HASH_RA || out == NULL || size < 33u) {
        return -1;
    }
    if (!known || seen != bw_data_generation()) {
        /* the extraction folder is the data root's parent */
        seen = bw_data_generation();
        snprintf(dir, sizeof dir, "%s", bw_data_root());
        n = strlen(dir);
        while (n > 0 && dir[n - 1] != '/' && dir[n - 1] != '\\') {
            n--;
        }
        dir[n > 0 ? n - 1 : 0] = '\0';
        known = n > 1 && bw_mod_v7_ra_hash_dir(dir, hash) == 0;
    }
    if (!known) {
        return -1;
    }
    memcpy(out, hash, 33);
    return 32;
}

/* ------------------------------------------------------------------ */
/* RetroAchievements' hardcore                                         */
/* ------------------------------------------------------------------ */

/*
 * The test switches that change the game (docs/test-switches.md): a
 * battle, a warp, the party, items, moves, the clock, scripted keys, a mod
 * named on the command line, a save written by a test.  The ones that only
 * log or show something (EIDOS_DEBUG_WHERE, _POPUP, _TRACKER, _AVATAR, the
 * logs) are not here: they change nothing of the game.
 */
static const char *const k_game_switches[] = {
    "EIDOS_DEBUG_A_FROM", "EIDOS_DEBUG_A_UNTIL", "EIDOS_DEBUG_AUTO_PLAYER", "EIDOS_DEBUG_BATTLE",
    "EIDOS_DEBUG_BATTLE_AGAIN", "EIDOS_DEBUG_BATTLE_SCENE", "EIDOS_DEBUG_CUTIN", "EIDOS_DEBUG_DEX_NATIONAL", "EIDOS_DEBUG_DEX_OWN",
    "EIDOS_DEBUG_DEX_STATE", "EIDOS_DEBUG_DRESSUP_DEMO", "EIDOS_DEBUG_DRESSUP_STYLUS", "EIDOS_DEBUG_HELD",
    "EIDOS_DEBUG_ITEMS", "EIDOS_DEBUG_LEVEL", "EIDOS_DEBUG_MOVE", "EIDOS_DEBUG_MOVES", "EIDOS_DEBUG_MOVE_KNOWN",
    "EIDOS_DEBUG_OPEN", "EIDOS_DEBUG_PARTY", "EIDOS_DEBUG_PARTY_COUNT", "EIDOS_DEBUG_PROPS", "EIDOS_DEBUG_WARP",
    "EIDOS_DEBUG_WARP_SEASON", "EIDOS_DEBUG_WEATHER", "EIDOS_RTC", "EIDOS_UI_SCRIPT", "EIDOS_MOD_CODE",
    "EIDOS_MOD_MODE", "EIDOS_MP_TEST", "EIDOS_FLASH_TEST", "EIDOS_SAVE_TEST_AT", "EIDOS_GAME_SWITCH_AT",
    "EIDOS_GAME_ADD_ROM",
};

/* the namespace of the mod that is calling (sdk/mod_savedata.c) */
#if defined(__GNUC__) || defined(__clang__)
extern const char *bw_mod_savedata_namespace(void) __attribute__((weak));
#else
extern const char *bw_mod_savedata_namespace(void);
#endif

int bw_mod_v7_hardcore_allowed(char *why, u32 size)
{
    const char *self = bw_mod_savedata_namespace != NULL ? bw_mod_savedata_namespace() : NULL;
    char reason[96];
    u32 i;

    reason[0] = '\0';
    for (i = 0; i < sizeof k_game_switches / sizeof k_game_switches[0] && reason[0] == '\0'; i++) {
        const char *v = getenv(k_game_switches[i]);

        if (v != NULL && v[0] != '\0') {
            snprintf(reason, sizeof reason, "A test switch is set (%s)", k_game_switches[i]);
        }
    }
    for (i = 0; i < bw_mod_menu_count() && reason[0] == '\0'; i++) {
        const char *ns = bw_mod_menu_namespace_at(i);

        if (bw_mod_menu_running_at(i) && (self == NULL || ns == NULL || strcmp(ns, self) != 0)) {
            snprintf(reason, sizeof reason, "Another mod is on: %s", bw_mod_menu_name_at(i));
        }
    }
    if (why != NULL && size > 0) {
        snprintf(why, size, "%s", reason);
    }
    return reason[0] == '\0';
}
