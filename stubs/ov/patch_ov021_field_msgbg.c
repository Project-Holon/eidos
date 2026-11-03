/*
 * patches: src/game/ov021/field_msgbg.c FIELD_MSGBG_CreateWindow FIELD_MSGBG_DeleteWindow FIELD_MSGBG_CreateSysFrameWindow FIELD_MSGBG_DeleteSysFrameWindow FIELD_MSGBG_MenuFuncMain
 *
 * The field's message and system windows, known to the one-screen text
 * boxes (engine/ui/textbox.c, sdk/ui_text.c).
 *
 * Every framed window of the field's message BG (the message, system,
 * information and menu windows; the talk balloons are the talk system's)
 * is made by FIELD_MSGBG_CreateWindow or FIELD_MSGBG_CreateSysFrameWindow
 * and removed by the matching delete.  Each does what the game's does and
 * also tells the port the window is there (bw_ui_field_win_add) or gone
 * (bw_ui_field_win_remove), so the interface can draw every open window
 * as its own text box.
 *
 * FIELD_MSGBG_MenuFuncMain runs an open list every frame (every list the
 * field opens: a script's, the Union Room's talk menu, the Battle Subway's,
 * the yes/no); it also tells the port which list is open
 * (bw_ui_field_menu_seen), so the interface draws it as its own.
 *
 * symbols:
 *   BMP_MENULIST_Main  0x020200CC  thumb
 *   FIELD_MSGBG_IsPrintQueFinished  0x0219049C  thumb  ov021
 *   BMP_WIN_Create  0x020450F0  arm
 *   BMP_WIN_Delete  0x020452E8  arm
 *   BMP_WIN_GetBmp  0x02045770  arm
 *   BMP_WIN_MakeScreen  0x02045374  arm
 *   BMP_WIN_TransferChars  0x02045334  arm
 *   BMP_Clear  0x02043B5C  arm
 *   BG_SYS_PushTile  0x020409B4  arm
 *   BMP_MENUWORK_DrawWindow  0x0201F73C  thumb
 *   BMP_MENUWORK_DrawWindowBorder  0x0201F7A8  thumb
 */

typedef unsigned char u8;
typedef unsigned short u16;

struct BmpWin;
struct Bmp;

#define FIELD_MSGBG_PALETTE_SYSTEM 0xE

extern void bw_ui_field_win_add(const void *bmpWin);
extern void bw_ui_field_win_remove(const void *bmpWin);

struct BmpWin *BMP_WIN_Create(int frame, int x, int y, int width, int height, int palette, int dir);
int  BMP_WIN_Delete(struct BmpWin *bmpWin);
struct Bmp *BMP_WIN_GetBmp(struct BmpWin *bmpWin);
void BMP_WIN_MakeScreen(struct BmpWin *bmpWin);
void BMP_WIN_TransferChars(struct BmpWin *bmpWin);
void BMP_Clear(struct Bmp *bmp, int color);
void BG_SYS_PushTile(u8 frame);
void BMP_MENUWORK_DrawWindow(struct BmpWin *bmpWin, int transfer, int tile, int palette);
void BMP_MENUWORK_DrawWindowBorder(struct BmpWin *bmpWin, int transfer);
void FIELD_MSGBG_DrawSysWindowFrame(struct BmpWin *bmpWin);
void FIELD_MSGBG_ClearSysWindowFrame(struct BmpWin *bmpWin);

struct BmpWin *FIELD_MSGBG_CreateWindow(int frame, int unusedHeapId, int x, int y, u16 width, u16 height, u16 palette)
{
    struct BmpWin *bmpWin;
    int framePalette;

    (void)unusedHeapId;
    bmpWin = BMP_WIN_Create(frame & 0xff, x & 0xff, y & 0xff, width & 0xff, height & 0xff, palette & 0xff, 1);
    BMP_Clear(BMP_WIN_GetBmp(bmpWin), 0xff);
    BMP_WIN_MakeScreen(bmpWin);
    BMP_WIN_TransferChars(bmpWin);
    BG_SYS_PushTile((u8)frame);
    framePalette = 0xd;
    if (palette != FIELD_MSGBG_PALETTE_SYSTEM) {
        framePalette = 0xa;
    }
    BMP_MENUWORK_DrawWindow(bmpWin, 0, 1, framePalette & 0xff);
    bw_ui_field_win_add(bmpWin);
    return bmpWin;
}

int FIELD_MSGBG_DeleteWindow(struct BmpWin *bmpWin)
{
    bw_ui_field_win_remove(bmpWin);
    BMP_MENUWORK_DrawWindowBorder(bmpWin, 0);
    return BMP_WIN_Delete(bmpWin);
}

struct BmpWin *FIELD_MSGBG_CreateSysFrameWindow(u8 x, u8 y, u8 width, u8 height, u16 heapId)
{
    struct BmpWin *bmpWin;

    (void)heapId;
    bmpWin = BMP_WIN_Create(2, x, y, width, height, 0xE, 1);
    BMP_Clear(BMP_WIN_GetBmp(bmpWin), 0xFF);
    BMP_WIN_MakeScreen(bmpWin);
    BMP_WIN_TransferChars(bmpWin);
    FIELD_MSGBG_DrawSysWindowFrame(bmpWin);
    bw_ui_field_win_add(bmpWin);
    return bmpWin;
}

void FIELD_MSGBG_DeleteSysFrameWindow(struct BmpWin *bmpWin)
{
    bw_ui_field_win_remove(bmpWin);
    FIELD_MSGBG_ClearSysWindowFrame(bmpWin);
    BMP_WIN_Delete(bmpWin);
}

/* FieldMenuFunc (src/game/ov021/field_msgbg_types.h): msgBg 0x00, msgPrint
 * 0x04, bmpWin 0x08, items 0x0C, menuList 0x10. */
typedef struct FieldMenuFunc {
    void *msgBg;
    void *msgPrint;
    struct BmpWin *bmpWin;
    void *items;
    void *menuList;
} FieldMenuFunc;

extern void bw_ui_field_menu_seen(const void *menuList, const void *bmpWin);
int FIELD_MSGBG_IsPrintQueFinished(void *msgBg);
int BMP_MENULIST_Main(void *menuList);

int FIELD_MSGBG_MenuFuncMain(FieldMenuFunc *menu)
{
    int result;

    bw_ui_field_menu_seen(menu->menuList, menu->bmpWin);
    if (FIELD_MSGBG_IsPrintQueFinished(menu->msgBg) == 0)
        return -1;
    result = BMP_MENULIST_Main(menu->menuList);
    if (result == -1)
        return -1;
    if (result == -2)
        return -2;
    return result;
}
