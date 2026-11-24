/*
 * patches: src/game/ov207/zukan_toroku_graphic.c ZUKAN_TOROKU_GRAPHIC_Main
 *
 * The Pokedex registration after a catch, under the one-screen interface
 * (engine/ui/recipe_menus.c, the scene "catch"; sdk/ui_catch.c).
 *
 * On the DS the registration is two screens: the Pokedex page of the caught
 * Pokemon, then the ground on which the game asks for a nickname.  Between
 * them the game takes the blend's brightness down one level every 4 frames
 * and up again the same way: 64 frames to black, 64 frames back (steps 3
 * and 4 of state 0).  The one-screen interface shows the first as its own
 * Pokedex screen and makes the change to the second itself (the Pokedex
 * closes: recipe_menus.c, catch_registration), so that way through the dark
 * shows nothing there: after the player's press the screen stood for more
 * than two seconds without a change.
 *
 * While the one-screen card draws (bw_ui_catch_own), the two steps take a
 * frame each: the brightness goes to its end at once, and everything the
 * game does at those ends (the page's display cleared, the question's ground
 * made) is done as the game does it.  The game runs at its own speed; only
 * this change of screen has no fade.
 *
 * The game reads A, B or a touch for the registration in its step 1 alone.
 * Its step 0 before that is the screen's fade in, 23 frames in which the
 * one-screen card already stands: a press made then was lost, and the player
 * had to press again.  While the card draws, A or B in step 0 is kept
 * (bw_ui_catch_early_note) and counts at the first frame of step 1.
 *
 * With the card not drawing (the two screens, a skipped recipe) the function
 * is the game's own, line for line.
 *
 * symbols:
 *   ZUKAN_INFO_Update 0x021BA374 thumb ov206
 *   ZUKAN_NICKNAME_Main 0x0220B72C thumb ov207
 *   FADE_IsActive 0x0204E804 arm
 *   ZUKAN_INFO_Start 0x021BA544 thumb ov206
 *   TOUCHPANEL_GetTrg 0x02035C60 arm
 *   KEY_GetTrg 0x020362DC arm
 *   UI_SetInputMode 0x020355D8 arm
 *   ZUKAN_INFO_NotifyMoveDone 0x021BA514 thumb ov206
 *   ZUKAN_INFO_IsMoving 0x021BA524 thumb ov206
 *   ZUKAN_INFO_IsDone 0x021BA534 thumb ov206
 *   G2x_SetBlendBrightnessExt_ 0x0207D67C arm
 *   ZUKAN_INFO_ClearDisplay 0x021BA4CC thumb ov206
 *   ZUKAN_INFO_NotifyFadeDone 0x021BA51C thumb ov206
 *   ZUKAN_NICKNAME_SetupScreen 0x0220B908 thumb ov207
 *   ZUKAN_NICKNAME_GetResult 0x0220B940 thumb ov207
 *   FADE_Start 0x0204E720 arm
 *   PRINTSYS_QUE_Main 0x0201C4E4 thumb
 *
 * The game's own record
 * ---------------------
 *
 * ZukanTorokuWork (src/game/ov207/zukan_toroku_graphic.c), at its offsets:
 * display 0x04, printQueue 0x0C, state 0x10, step 0x14, fadeLevel 0x18,
 * counter 0x1C, menu 0x20, nickname 0x24.  ZukanTorokuArgs: result 0x1C.
 * The offsets are the DS ones; pointers are four bytes in every build that
 * runs the overlays.
 */

typedef unsigned int u32;
typedef int s32;

struct ZukanTorokuArgs;
struct ZukanTorokuWork;

extern int bw_ui_catch_own(void);
extern void bw_ui_catch_early_note(void);
extern int bw_ui_catch_early_take(void);

void ZUKAN_INFO_Update(char *p);
void ZUKAN_INFO_ClearDisplay(char *p);
void ZUKAN_INFO_NotifyMoveDone(char *p);
void ZUKAN_INFO_NotifyFadeDone(char *p);
int ZUKAN_INFO_IsMoving(char *p);
int ZUKAN_INFO_IsDone(char *p);
void ZUKAN_INFO_Start(char *p);
int ZUKAN_NICKNAME_Main(void *p);
void ZUKAN_NICKNAME_SetupScreen(void *p);
int ZUKAN_NICKNAME_GetResult(void *p);
void FADE_Start(u32 a, s32 b, s32 c, s32 d);
int FADE_IsActive(void);
int TOUCHPANEL_GetTrg(void);
u32 KEY_GetTrg(void);
void UI_SetInputMode(u32 value);
void G2x_SetBlendBrightnessExt_(u32 addr, int plane1, int plane2, int ev1, int ev2, int brightness);
u32 PRINTSYS_QUE_Main(void *queue);

void ZUKAN_TOROKU_GRAPHIC_CreateBg(struct ZukanTorokuWork *work);
void ZUKAN_TOROKU_GRAPHIC_Draw(void *display);
void ZUKAN_TOROKU_GRAPHIC_Update(void *unused);
void ZUKAN_TOROKU_GRAPHIC_UpdateSub(void *unused);

#define W_INT(work, off)  (*(int *)((char *)(work) + (off)))
#define W_PTR(work, off)  (*(void **)((char *)(work) + (off)))

#define W_DISPLAY(work)   W_PTR(work, 0x04)
#define W_QUEUE(work)     W_PTR(work, 0x0C)
#define W_STATE(work)     W_INT(work, 0x10)
#define W_STEP(work)      W_INT(work, 0x14)
#define W_FADE(work)      W_INT(work, 0x18)
#define W_COUNTER(work)   W_INT(work, 0x1C)
#define W_MENU(work)      ((char *)W_PTR(work, 0x20))
#define W_NICKNAME(work)  W_PTR(work, 0x24)
#define A_RESULT(args)    (*(u32 *)((char *)(args) + 0x1C))

#define REG_BLDCNT 0x04000050u

int ZUKAN_TOROKU_GRAPHIC_Main(void *unused, int unused2, struct ZukanTorokuArgs *args, struct ZukanTorokuWork *work)
{
    int done = 0;
    int result, flag, input, early;

    (void)unused;
    (void)unused2;
    ZUKAN_INFO_Update(W_MENU(work));
    result = ZUKAN_NICKNAME_Main(W_NICKNAME(work));
    switch (W_STATE(work)) {
    case 0:
        switch (W_STEP(work)) {
        case 0:
            if (bw_ui_catch_own() && (KEY_GetTrg() & 3) != 0) {
                bw_ui_catch_early_note();   /* (the one-screen card: a press during the fade in counts) */
            }
            if (FADE_IsActive() == 0) {
                ZUKAN_INFO_Start(W_MENU(work));
                W_STEP(work) = 1;
            }
            break;
        case 1:
            flag = 0;
            input = TOUCHPANEL_GetTrg();
            early = bw_ui_catch_early_take() && bw_ui_catch_own();
            if ((KEY_GetTrg() & 3) != 0 || early) {
                UI_SetInputMode(0);
                flag = 1;
            }
            if (input != 0) {
                UI_SetInputMode(1);
                flag = 1;
            }
            if (flag != 0) {
                ZUKAN_INFO_NotifyMoveDone(W_MENU(work));
                W_STEP(work) = 2;
            }
            break;
        case 2:
            if (ZUKAN_INFO_IsMoving(W_MENU(work)) != 0 || ZUKAN_INFO_IsDone(W_MENU(work)) != 0) {
                W_COUNTER(work) = 0;
                W_FADE(work) = 16;
                W_STEP(work) = 3;
            }
            break;
        case 3:
            if (bw_ui_catch_own()) {
                W_COUNTER(work) = 0;
                W_FADE(work) = 0;           /* (the one-screen card: no way through the dark) */
            } else {
                W_COUNTER(work)++;
                if (W_COUNTER(work) == 4) {
                    W_COUNTER(work) = 0;
                    W_FADE(work)--;
                }
            }
            G2x_SetBlendBrightnessExt_(REG_BLDCNT, 15, 15, W_FADE(work), 0, W_FADE(work) - 16);
            if (W_FADE(work) == 0) {
                ZUKAN_INFO_ClearDisplay(W_MENU(work));
                ZUKAN_TOROKU_GRAPHIC_CreateBg(work);
                W_STEP(work) = 4;
            }
            break;
        case 4:
            if (bw_ui_catch_own()) {
                W_COUNTER(work) = 0;
                W_FADE(work) = 16;
            } else {
                W_COUNTER(work)++;
                if (W_COUNTER(work) == 4) {
                    W_COUNTER(work) = 0;
                    W_FADE(work)++;
                }
            }
            G2x_SetBlendBrightnessExt_(REG_BLDCNT, 15, 15, W_FADE(work), 0, W_FADE(work) - 16);
            if (W_FADE(work) == 16) {
                W_STEP(work) = 5;
            }
            break;
        case 5:
            if (ZUKAN_INFO_IsDone(W_MENU(work)) != 0) {
                W_STATE(work) = 1;
                W_STEP(work) = 1;
            }
            break;
        }
        break;
    case 1:
        switch (W_STEP(work)) {
        case 0:
            if (FADE_IsActive() == 0) {
                ZUKAN_INFO_NotifyFadeDone(W_MENU(work));
                W_STEP(work) = 1;
            }
            break;
        case 1:
            ZUKAN_NICKNAME_SetupScreen(W_NICKNAME(work));
            W_STEP(work) = 2;
            break;
        case 2:
            if (result == 1) {
                FADE_Start(3, 0, 16, 2);
                W_STEP(work) = 3;
            }
            break;
        case 3:
            if (FADE_IsActive() == 0) {
                switch (ZUKAN_NICKNAME_GetResult(W_NICKNAME(work))) {
                case 0:
                    A_RESULT(args) = 0;
                    break;
                case 1:
                    A_RESULT(args) = 1;
                    break;
                }
                W_STEP(work) = 4;
                done = 1;
            }
            break;
        }
        break;
    }
    PRINTSYS_QUE_Main(W_QUEUE(work));
    ZUKAN_TOROKU_GRAPHIC_Draw(W_DISPLAY(work));
    ZUKAN_TOROKU_GRAPHIC_Update(W_DISPLAY(work));
    ZUKAN_TOROKU_GRAPHIC_UpdateSub(W_DISPLAY(work));
    return done;
}
