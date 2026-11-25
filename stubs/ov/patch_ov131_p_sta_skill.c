/*
 * patches: src/game/ov131/p_sta_skill.c P_STA_SKILL_ShowCantForgetMsg
 * keeps declarations
 *
 * The summary's refusal to forget a move, known to the one-screen interface
 * (engine/ui/recipe_party.c, forget_view; sdk/ui_text.c).
 *
 * P_STA_SKILL_ShowCantForgetMsg is what the forget-a-move page runs when
 * the move chosen is one the game does not let go (an HM move): it prints
 * "HM moves can't be forgotten now." in the place of the move's description
 * and plays the refusal sound.  The game's own sign of it,
 * PStaSkillWork.msgPrintPending, lasts until the print queue is done, which
 * is one frame or two, so an interface that draws one frame in several (the
 * fast speeds) never sees it.  The function does what the game's does and
 * also tells the port (bw_ui_forget_refused), which counts the refusals.
 *
 * symbols:
 *   BMP_WIN_GetBmp  0x02045770  arm
 *   BMP_Clear  0x02043B5C  arm
 *   P_STATUS_PrintMsgToWin  0x021D618C  thumb  ov131
 *   GFL_USE_PlaySeDefaultVolume  0x020061E4  thumb
 */

typedef unsigned char u8;
typedef unsigned int u32;

typedef struct PStatusWork PStatusWork;
struct BmpWin;
struct Bmp;

#define STA_SKILL_WIN_MAX     13
#define STA_SKILL_WIN_MESSAGE 12

/* The start of the game's PStaSkillWork (src/game/ov131/p_sta_skill.c), as
 * far as this function reads it. */
typedef struct PStaSkillWork {
    u32 isOpen;                             /* 0x00 */
    u32 statsPrintPending;                  /* 0x04 */
    u32 movePrintPending;                   /* 0x08 */
    int msgPrintPending;                    /* 0x0C: 1 = message window waits for the print queue */
    u8 pad_10[0x5C - 0x10];
    struct BmpWin *win[STA_SKILL_WIN_MAX];  /* 0x5C: STA_SKILL_WIN_* */
} PStaSkillWork;

extern void bw_ui_forget_refused(int result);

struct Bmp *BMP_WIN_GetBmp(struct BmpWin *win);
void BMP_Clear(struct Bmp *bmp, u8 value);
void P_STATUS_PrintMsgToWin(PStatusWork *wk, struct BmpWin *bmpWinData, int msgId, int x, unsigned short y,
                            unsigned short color);
void GFL_USE_PlaySeDefaultVolume(u32 seqNo);

void P_STA_SKILL_ShowCantForgetMsg(PStatusWork *wk, PStaSkillWork *work, int result)
{
    work->msgPrintPending = 1;
    BMP_Clear(BMP_WIN_GetBmp(work->win[STA_SKILL_WIN_MESSAGE]), 0);
    if (result == 1) {
        P_STATUS_PrintMsgToWin(wk, work->win[STA_SKILL_WIN_MESSAGE], 156, 0, 0, 15424);
    }
    GFL_USE_PlaySeDefaultVolume(1367);
    bw_ui_forget_refused(result);
}
