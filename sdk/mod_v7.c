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

#include <string.h>

#include "port.h"
#include "mod_v7.h"

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
