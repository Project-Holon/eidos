/*
 * function: STARTMENU_StateSelectRow
 * address:  0x021BA338
 * packet:   pkt_ov088_021BA338
 * region:   ov088
 * compiler: dsi/1.1p1
 * flags:    -O4,p
 * lang:     c99
 * isa:      thumb
 * patches: src/game/ov088/startmenu.c STARTMENU_StateSelectRow
 *
 * The src/game form of stubs/ov/reimpl_ov088_func_021BA338.c (the same code, the
 * tree's names): the title menu's Settings row (docs/hooks.md,
 * "title_settings").  Written by tools/ov088_patches.py from
 * that file; a change goes into the reimpl, then this is
 * written again.
 * port hook "title_settings" (docs/hooks.md, docs/settings-design.md):
 * the menu cursor state. A on the SETTINGS row (kind 7 while the game's own row
 * is off) opens the settings screen: selection 9, which the exit function maps to
 * the menu proc started again in settings mode. In settings mode this state is
 * the settings screen's input: eidos_sui_input decides, a change rebuilds the
 * rows (same set-up calls as state 0, minus the sprites) and waits in state 4
 * for the text, B leaves like the game's own B.
 *
 * With the hook off this is the matched function, unchanged.
 *
 * original status:   EXACT
 *
 * symbols:
 *   BLINK_PALANM_Update  0x0202EA40  thumb
 *   KEY_GetTrg  0x020362DC  arm
 *   GFL_USE_PlaySeDefaultVolume  0x020061E4  thumb
 *   STARTMENU_BeginSelectAnim  0x021BC0AC  thumb  ov088
 *   MISC_SetSlotState  0x0200C2F0  thumb
 *   STARTMENU_BeginFadeIn  0x021BC080  thumb  ov088
 *   KEY_GetRepeat  0x0203633C  arm
 *   STARTMENU_MoveCursor  0x021BB890  thumb  ov088
 *   STARTMENU_DeleteFrameManager  0x021BB474  thumb  ov088
 *   STARTMENU_DeleteRowWindows  0x021BB164  thumb  ov088
 *   STARTMENU_FreeRowBuffers  0x021BB5C4  thumb  ov088
 *   STARTMENU_BuildRowList  0x021BB4B4  thumb  ov088
 *   STARTMENU_LoadRowBuffers  0x021BB544  thumb  ov088
 *   STARTMENU_CreateRowWindows  0x021BAD08  thumb  ov088
 *   STARTMENU_CreateFrameManager  0x021BB380  thumb  ov088
 *   STARTMENU_LayoutRows  0x021BB608  thumb  ov088
  */
/* sdk/mod_title.h, spelled out: the overlay link header declares what the
 * file itself does not (docs/hooks.md). */
extern int bw_mod_title_settings_on(void);
extern int bw_mod_title_in_settings(void);
extern int bw_mod_title_set_in_settings(int on);
extern int bw_mod_title_set_rebuild(void);
extern int bw_mod_title_take_autostart(void);
extern void bw_mod_title_open_multiplayer(void);
extern int bw_mod_title_join_pending(void);
extern int bw_slots_flip(int d);
extern int bw_slots_new_game(void);
extern int bw_mp_take_started(void);
extern int bw_mp_take_started_peek(void);
extern void eidos_sui_enter(void);
extern int eidos_sui_input(unsigned pressed, unsigned held);
extern void STARTMENU_DeleteFrameManager(char *p);
extern void STARTMENU_DeleteRowWindows(char *p);
extern void STARTMENU_FreeRowBuffers(char *p);
extern void STARTMENU_BuildRowList(char *p);
extern void STARTMENU_LoadRowBuffers(char *p);
extern void STARTMENU_CreateRowWindows(char *p);
extern void STARTMENU_CreateFrameManager(char *p);
extern void STARTMENU_LayoutRows(char *p);
#define BW_TITLE_ROW_SETTINGS 7
#define BW_TITLE_SEL_SETTINGS 9
typedef unsigned char u8;
typedef unsigned int u32;

extern void BLINK_PALANM_Update(void *p);
extern u32 KEY_GetTrg(void);
extern void GFL_USE_PlaySeDefaultVolume(u32);
extern int STARTMENU_BeginSelectAnim(char *p, int v);
extern void MISC_SetSlotState(int, int, int);
extern int STARTMENU_BeginFadeIn(char *p, int v);
extern u32 KEY_GetRepeat(void);
extern int STARTMENU_MoveCursor(char *p, int d);
extern u8 MISC_GetSlotState(void *, unsigned);
extern int SAVE_CONTROL_HasFlashSave(void *);

/* The settings screen's cursor state. */
static int settings_state(char *p)
{
    int r;
    BLINK_PALANM_Update(*(void **)(p + 0x11c));
    r = eidos_sui_input(KEY_GetTrg(), KEY_GetRepeat());
    if (r & 0x10)
        GFL_USE_PlaySeDefaultVolume(0x548);
    if (r & 0x20)
        GFL_USE_PlaySeDefaultVolume(0x54c);
    if (r & 0x40)
        GFL_USE_PlaySeDefaultVolume(0x551);
    if (r & 2) {
        *(u8 *)(p + 0x129) = 8;
        return STARTMENU_BeginFadeIn(p, 1);
    }
    if (r & 1) {
        STARTMENU_DeleteFrameManager(p);
        STARTMENU_DeleteRowWindows(p);
        STARTMENU_FreeRowBuffers(p);
        STARTMENU_BuildRowList(p);
        STARTMENU_LoadRowBuffers(p);
        STARTMENU_CreateRowWindows(p);
        STARTMENU_CreateFrameManager(p);
        STARTMENU_LayoutRows(p);
        bw_mod_title_set_rebuild();
        return 4;
    }
    return 5;
}

/* The menu's rows again (the Continue card's text changed). */
static int rebuild(char *p)
{
    STARTMENU_DeleteFrameManager(p);
    STARTMENU_DeleteRowWindows(p);
    STARTMENU_FreeRowBuffers(p);
    STARTMENU_BuildRowList(p);
    STARTMENU_LoadRowBuffers(p);
    STARTMENU_CreateRowWindows(p);
    STARTMENU_CreateFrameManager(p);
    STARTMENU_LayoutRows(p);
    bw_mod_title_set_rebuild();
    return 4;
}

/* Multiplayer's start: the menu goes into the game as A on Continue (or on
 * New Game when there is no save) would. */
static int autostart(char *p)
{
    /* The save the game holds now decides, not the rows: they were built
     * before the multiplayer save was loaded. */
    GFL_USE_PlaySeDefaultVolume(0x54c);
    if (SAVE_CONTROL_HasFlashSave(*(void **)p) == 1) {
        /* Continue */
        *(u8 *)(p + 0x129) = 0;
        return STARTMENU_BeginSelectAnim(p, 7);
    }
    /* New Game, without the "there is a save" warning: the prompt's own
     * path for a menu without Continue */
    *(u8 *)(p + 0x129) = 1;
    return STARTMENU_BeginFadeIn(p, 1);
}

int STARTMENU_StateSelectRow(char *p)
{
    if (bw_mod_title_settings_on() && bw_mod_title_in_settings())
        return settings_state(p);
    if (bw_mod_title_settings_on() &&
        (bw_mod_title_take_autostart() | bw_mp_take_started())) {
        int r = autostart(p);

        if (r >= 0)
            return r;
    }
    if (bw_mod_title_settings_on() && bw_mod_title_join_pending()) {
        /* a game to join from the start: the Multiplayer screen joins it */
        bw_mod_title_open_multiplayer();
        *(u8 *)(p + 0x129) = BW_TITLE_SEL_SETTINGS;
        return STARTMENU_BeginFadeIn(p, 1);
    }
    BLINK_PALANM_Update(*(void **)(p + 0x11c));
    if (KEY_GetTrg() & 1) {
        GFL_USE_PlaySeDefaultVolume(0x54c);
        switch (*(u8 *)(p + *(u8 *)(p + 0x128) + 0x120)) {
        case 0:
            *(u8 *)(p + 0x129) = *(u8 *)(p + *(u8 *)(p + 0x128) + 0x120);
            return STARTMENU_BeginSelectAnim(p, 7);
        case 1:
            /* New Game takes a free slot (sdk/save_slots.h: there is
             * always one), so the game's "this deletes your save" never
             * comes for a save of the player's */
            if (bw_mod_title_settings_on() && !bw_mp_take_started_peek()) {
                if (bw_slots_new_game()) {
                    /* the new game goes to a free slot: no warning, as
                     * the prompt's own path for a menu without Continue */
                    *(u8 *)(p + 0x129) = 1;
                    return STARTMENU_BeginFadeIn(p, 1);
                }
            }
            return STARTMENU_BeginSelectAnim(p, 8);
        case 2:
            *(u8 *)(p + 0x129) = *(u8 *)(p + *(u8 *)(p + 0x128) + 0x120);
            return STARTMENU_BeginSelectAnim(p, 11);
        case 3:
            MISC_SetSlotState(*(int *)(p + 8), 1, 2);
            *(u8 *)(p + 0x129) = *(u8 *)(p + *(u8 *)(p + 0x128) + 0x120);
            return STARTMENU_BeginSelectAnim(p, 11);
        case 4:
            MISC_SetSlotState(*(int *)(p + 8), 2, 2);
            *(u8 *)(p + 0x129) = *(u8 *)(p + *(u8 *)(p + 0x128) + 0x120);
            return STARTMENU_BeginSelectAnim(p, 11);
        case 7:
            if (bw_mod_title_settings_on() && !MISC_GetSlotState(*(void **)(p + 8), 3)) {
                /* The leave path of kind 1 (STARTMENU_StateConfirmPrompt): state 11's table
                 * entry is the unlabelled stub at 0x021BA984, which the
                 * boot does not list as a code entry yet. */
                *(u8 *)(p + 0x129) = BW_TITLE_SEL_SETTINGS;
                return STARTMENU_BeginFadeIn(p, 1);
            }
            MISC_SetSlotState(*(int *)(p + 8), 3, 2);
            *(u8 *)(p + 0x129) = *(u8 *)(p + *(u8 *)(p + 0x128) + 0x120);
            return STARTMENU_BeginSelectAnim(p, 11);
        case 5:
            return STARTMENU_BeginSelectAnim(p, 9);
        case 6:
            if (bw_mod_title_settings_on()) {
                /* MULTIPLAYER, in the Mic Test's place: the settings
                 * process opens on the Multiplayer screen alone */
                bw_mod_title_open_multiplayer();
                *(u8 *)(p + 0x129) = BW_TITLE_SEL_SETTINGS;
                return STARTMENU_BeginFadeIn(p, 1);
            }
            *(u8 *)(p + 0x129) = *(u8 *)(p + *(u8 *)(p + 0x128) + 0x120);
            return STARTMENU_BeginSelectAnim(p, 11);
        }
    }
    /* Left and right on the Continue card: the next save slot */
    if (bw_mod_title_settings_on() &&
        *(u8 *)(p + *(u8 *)(p + 0x128) + 0x120) == 0 &&
        (KEY_GetTrg() & 0x30)) {
        if (bw_slots_flip((KEY_GetTrg() & 0x10) ? 1 : -1)) {
            GFL_USE_PlaySeDefaultVolume(0x548);
            return rebuild(p);
        }
    }
    if (KEY_GetTrg() & 2) {
        GFL_USE_PlaySeDefaultVolume(0x551);
        *(u8 *)(p + 0x129) = 8;
        return STARTMENU_BeginFadeIn(p, 1);
    }
    if (KEY_GetRepeat() & 0x40) {
        if (STARTMENU_MoveCursor(p, -1) == 1) {
            GFL_USE_PlaySeDefaultVolume(0x548);
            return 6;
        }
    }
    if (KEY_GetRepeat() & 0x80) {
        if (STARTMENU_MoveCursor(p, 1) == 1) {
            GFL_USE_PlaySeDefaultVolume(0x548);
            return 6;
        }
    }
    return 5;
}
