/*
 * patches: src/game/ov021/talkmsgwin.c TALKMSGWIN_DrawEntry TALKMSGWIN_UpdateShape TALKMSGWIN_AddFrameWindow TALKMSGWIN_DeleteFrameWindow
 *
 * The field's talk balloons under the one-screen text boxes
 * (engine/ui/textbox.c, sdk/ui_text.c).
 *
 * TALKMSGWIN_DrawEntry draws a balloon's 3D polygon (its body while it
 * opens, its tail once shown).  While the interface draws the text boxes
 * itself (bw_ui_text_own), the polygon is left out; the window's text is
 * still printed by the game, which the interface copies.
 *
 * TALKMSGWIN_UpdateShape works out, every frame, the screen point a
 * balloon's tail aims at (the speaker, or the window it answers).  It
 * does what the game's does and also hands that point to the port
 * (bw_ui_talk_target), so the interface's balloon aims at the same place.
 *
 * TALKMSGWIN_AddFrameWindow and TALKMSGWIN_DeleteFrameWindow make and
 * remove a plain framed window of the talk system (the field's plain
 * windows: the Union Room's talk and its battle rule card).  Each does what
 * the game's does and also tells the port the window is there or gone
 * (bw_ui_field_win_add), so the interface draws it as its own text box.
 *
 * symbols:
 *   VEC_Subtract  0x0207CA00  arm
 *   BMP_WIN_Delete  0x020452E8  arm
 *
 * The game's own records
 * ----------------------
 *
 * TalkMsgWin and TalkMsgWinSys (src/game/ov021/talkmsgwin_types.h), only
 * the members these functions read, at their offsets: state 0x00,
 * targetPos 0x10, anchorIndex 0x1A, shape.tailSide 0x58 of a window
 * (0x64 bytes); windows 0x018 and savedTarget 0xCB8 of the system.  The
 * offsets are the DS ones; pointers are four bytes in every build that
 * runs the overlays.
 */

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;

typedef struct VecFx32 {
    int x, y, z;
} VecFx32;

typedef struct TalkMsgWin {
    u16 state;                      /* 0x00 */
    u8 pad02[0x10 - 0x02];
    const VecFx32 *targetPos;       /* 0x10 */
    u8 pad14[0x1A - 0x14];
    u16 anchorIndex;                /* 0x1A */
    u8 pad1C[0x58 - 0x1C];
    int tailSide;                   /* 0x58: shape.tailSide */
    u8 pad5C[0x64 - 0x5C];
} TalkMsgWin;

typedef struct TalkMsgWinSys {
    u8 param[0x18];                 /* 0x000 */
    TalkMsgWin windows[32];         /* 0x018 */
    u8 padC98[0xCB8 - 0xC98];
    VecFx32 savedTarget;            /* 0xCB8 */
} TalkMsgWinSys;

typedef char TalkMsgWin_SizeCheck[sizeof(void *) != 4 || sizeof(TalkMsgWin) == 0x64 ? 1 : -1];
typedef char TalkMsgWinSys_SizeCheck[sizeof(void *) != 4 || __builtin_offsetof(TalkMsgWinSys, savedTarget) == 0xCB8 ? 1 : -1];

enum {
    TALKMSGWIN_STATE_OPENING = 3,
    TALKMSGWIN_STATE_SHOWN = 4
};

extern int  bw_ui_text_own(void);
extern void bw_ui_field_win_add(const void *bmpWin);
extern void bw_ui_field_win_remove(const void *bmpWin);
extern void bw_ui_talk_target(const void *win, int x, int y);

void VEC_Subtract(const VecFx32 *a, const VecFx32 *b, VecFx32 *out);
void TALKMSGWIN_DrawBalloonShape(TalkMsgWinSys *sys, TalkMsgWin *win, int tailOnly);
int  TALKMSGWIN_ProjectToWorld(const VecFx32 *relPos, int *screenX, int *screenY, VecFx32 *outWorld);
void TALKMSGWIN_GetAnchorWindowPoint(TalkMsgWin *win, TalkMsgWin *anchor, int *screenX, int *screenY, VecFx32 *outWorld);
int  TALKMSGWIN_CalcTail(TalkMsgWin *win, int targetX, int targetY, VecFx32 *outBaseA, VecFx32 *outBaseB);
void TALKMSGWIN_GetWindowCornerPoints(TalkMsgWin *win, VecFx32 *outTopLeft, VecFx32 *outTopRight, VecFx32 *outBottomLeft,
                                      VecFx32 *outBottomRight);
void TALKMSGWIN_BuildShape(TalkMsgWinSys *sys, TalkMsgWin *win, const VecFx32 *tip, VecFx32 *tailBaseA, VecFx32 *tailBaseB,
                           VecFx32 *topLeft, VecFx32 *topRight, VecFx32 *bottomLeft, VecFx32 *bottomRight);

void TALKMSGWIN_DrawEntry(TalkMsgWinSys *sys, TalkMsgWin *win)
{
    if (bw_ui_text_own()) {
        return;                     /* the interface draws the balloon */
    }
    switch (win->state) {
    case TALKMSGWIN_STATE_OPENING:
        TALKMSGWIN_DrawBalloonShape(sys, win, 0);
        break;
    case TALKMSGWIN_STATE_SHOWN:
        TALKMSGWIN_DrawBalloonShape(sys, win, 1);
        break;
    default:
        break;
    }
}

u32 TALKMSGWIN_UpdateShape(TalkMsgWinSys *sys, TalkMsgWin *win)
{
    VecFx32 tipPos;
    VecFx32 tailBaseA;
    VecFx32 tailBaseB;
    VecFx32 topLeft;
    VecFx32 topRight;
    VecFx32 bottomLeft;
    VecFx32 bottomRight;
    VecFx32 relPos;
    int screenX;
    int screenY;
    int tailSide;

    if (win->targetPos != 0) {
        VEC_Subtract(win->targetPos, &sys->savedTarget, &relPos);
        TALKMSGWIN_ProjectToWorld(&relPos, &screenX, &screenY, &tipPos);
    } else {
        TALKMSGWIN_GetAnchorWindowPoint(win, &sys->windows[win->anchorIndex], &screenX, &screenY, &tipPos);
    }
    bw_ui_talk_target(win, screenX, screenY);
    tailSide = TALKMSGWIN_CalcTail(win, screenX, screenY, &tailBaseA, &tailBaseB);
    TALKMSGWIN_GetWindowCornerPoints(win, &topLeft, &topRight, &bottomLeft, &bottomRight);
    TALKMSGWIN_BuildShape(sys, win, &tipPos, &tailBaseA, &tailBaseB, &topLeft, &topRight, &bottomLeft, &bottomRight);
    if (win->tailSide != tailSide) {
        win->tailSide = tailSide;
        return 1;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Plain framed windows                                                */
/* ------------------------------------------------------------------ */

struct BmpWin;

struct BmpWin *TALKMSGWIN_AddBmpWin(TalkMsgWinSys *sys, u8 x, u8 y, u8 width, u8 height);
void TALKMSGWIN_DrawFrameByType(const void *style, struct BmpWin *bmpWin, u8 frameType, u32 tailSide);
void TALKMSGWIN_ClearFrame(const void *style, struct BmpWin *bmpWin);
int  BMP_WIN_Delete(struct BmpWin *bmpWin);

struct BmpWin *TALKMSGWIN_AddFrameWindow(TalkMsgWinSys *sys, u8 x, u8 y, u8 width, u8 height, u32 frameType)
{
    struct BmpWin *bmpWin;

    bmpWin = TALKMSGWIN_AddBmpWin(sys, x, y, width, height);
    TALKMSGWIN_DrawFrameByType(sys->param, bmpWin, (u8)frameType, 0);
    bw_ui_field_win_add(bmpWin);
    return bmpWin;
}

void TALKMSGWIN_DeleteFrameWindow(TalkMsgWinSys *sys, struct BmpWin *bmpWin)
{
    bw_ui_field_win_remove(bmpWin);
    TALKMSGWIN_ClearFrame(sys->param, bmpWin);
    BMP_WIN_Delete(bmpWin);
}
