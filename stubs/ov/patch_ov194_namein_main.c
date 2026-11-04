/*
 * patches: src/game/ov194/namein_graphic_main.c NAMEIN_GRAPHIC_Main
 *
 * The name entry's main step (overlay 194, the proc of gNameInProcData).
 * The game opens the name entry from many apps, several of them on a proc
 * stack of their own (the end of a battle, the PC's boxes, an Egg
 * hatching, the Dream World's return, the Wi-Fi friend list), so the
 * one-screen name entry (engine/ui/recipe_input.c) cannot find the screen
 * by walking proc stacks.  This step does what the game's does and also
 * tells the port the screen's work each frame (bw_ui_namein_seen).
 *
 * symbols:
 *   PRINTSYS_QUE_Main  0x0201C4E4  thumb
 *
 * The game's own record
 * ---------------------
 *
 * NameinGraphicWork (src/game/ov194/namein_graphic_shared_types.h), only
 * the members this step reads: display 0x00, icon 0x264, callback 0x2A0,
 * printSys 0x2B4.
 */

typedef unsigned char u8;
typedef unsigned int u32;

struct PrintQueue;
struct NameinGraphicCallback;

extern void bw_ui_namein_seen(const void *work);

void NAMEIN_GRAPHIC_RunCallback(struct NameinGraphicCallback *callback);
void NAMEIN_GRAPHIC_DrawDisplay(u8 *display);
u32 PRINTSYS_QUE_Main(struct PrintQueue *queue);
void NAMEIN_GRAPHIC_FlushIconOam(u8 *icon);
unsigned NAMEIN_GRAPHIC_GetCallbackState(struct NameinGraphicCallback *callback);

#define NW_DISPLAY   0x000
#define NW_ICON      0x264
#define NW_CALLBACK  0x2A0
#define NW_PRINTSYS  0x2B4

int NAMEIN_GRAPHIC_Main(int a, int b, int c, u8 *p)
{
    (void)a;
    (void)b;
    (void)c;
    bw_ui_namein_seen(p);
    NAMEIN_GRAPHIC_RunCallback((struct NameinGraphicCallback *)(p + NW_CALLBACK));
    NAMEIN_GRAPHIC_DrawDisplay(*(u8 **)(p + NW_DISPLAY));
    PRINTSYS_QUE_Main(*(struct PrintQueue **)(p + NW_PRINTSYS));
    NAMEIN_GRAPHIC_FlushIconOam(p + NW_ICON);
    if (NAMEIN_GRAPHIC_GetCallbackState((struct NameinGraphicCallback *)(p + NW_CALLBACK))) {
        return 1;
    }
    return 0;
}
