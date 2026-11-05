/*
 * patches: src/game/ov121/mystery.c MYSTERY_TaskNetExit
 * keeps declarations
 *
 * symbols:
 *   d_021DFB80 0x021DFB80 data ov121 a1
 *   gNetSaveMainProc 0x021D6338 data ov115 a4
 *   ov115_id 0x00000073 data a1
 *
 * Mystery Gift's GET VIA NINTENDO WFC (port hook "mystery_gift",
 * docs/hooks.md).  The game leaves Mystery Gift here for the Wi-Fi
 * connection and save screen (overlay 115, gNetSaveMainProc), which logs
 * in to Nintendo WFC before the download; then it comes back and starts
 * the download in MYSTERY_TaskReceive.  Nintendo WFC is gone, so with the
 * hook on the task goes straight to MYSTERY_TaskReceive, whose download
 * the network module answers from the relay
 * (stubs/ov/patch_ov121_mystery_net.c).
 *
 * With the hook off this is the matched function, unchanged: the members
 * of MysteryWork it uses are read at their offsets (ov121/mystery.c).
 */

#include <stddef.h>

extern int bw_hook_enabled(const char *name);

typedef void (*MgTask)(void *, int *, void *);
extern void MYSTERY_TaskMenu(void *task, int *state, void *pArg);
extern void MYSTERY_TaskReceive(void *task, int *state, void *pArg);
extern void MYSTERY_UTIL_SetTaskHandler(void *task, MgTask f);
extern int  MYSTERY_NET_UpdateWifiError(void *p);
extern void MYSTERY_NET_Exit(char *p);
extern void FADE_Start(int a, int b, int c, int d);
extern int  FADE_IsActive(void);
extern void MYSTERY_SetScrollParam(void *scroll, unsigned v);
extern void MYSTERY_UTIL_DeleteMenuWindow(void *w);
extern void MYSTERY_DeleteClact(void *clact);
extern void MYSTERY_ExitBgHeap(void *heap);
extern void MYSTERY_GRAPHIC_Delete(void *g);
extern void *MYSTERY_GRAPHIC_Create(int a, int heap);
extern void *MYSTERY_GRAPHIC_GetClactUnit(void *g);
extern void MYSTERY_InitBgHeap(void *heap, unsigned short h);
extern void MYSTERY_CreateClact(void *clact, void *owner, unsigned short heap);
extern void *MYSTERY_UTIL_CreateMenuWindow(int a, int b, void *printQueue, void *font, int heap);
extern void MYSTERY_UTIL_DrawMenuWindow(void *w, int a, int b);
extern void G2x_SetBlendAlpha_(void *reg, int a, int b, int c, int d);
extern void GFL_NET_RefreshWmIconAtTopRight(int a, int heap);
extern void GFL_USE_CancelChangeAndPlayBgm(int bgm, int a);
extern int  MYSTERY_DATA_IsMainSave(void *data);
extern void *GFL_HEAP_Alloc(unsigned short heap, unsigned int size, int a, const char *file, unsigned int line);
extern void GFL_HEAP_Free(void *p);
extern void MI_CpuFill8(void *dst, int v, unsigned int size);
extern void PROCSYS_PushGlobal(unsigned int ovl, const void *data, void *arg);
extern const char d_021DFB80[];
extern const char gNetSaveMainProc[];
extern char ov115_id[];

/* MysteryWork (ov121/mystery.c), DS byte offsets. */
#define W_SCROLL     0x138u
#define W_TEXTBOX    0x284u
#define W_CLACT      0x294u
#define W_HEAP       0x390u
#define W_GRAPHIC    0x394u
#define W_FONT       0x398u
#define W_PRINTQUEUE 0x39Cu
#define W_NET        0x3B0u
#define W_GAMEDATA   0x3B8u
#define W_DATA       0x3BCu
#define W_SAVEPARAM  0x3C0u

/* MysteryNetSaveParam (0x24 bytes). */
typedef struct MgSaveParam {
    void *gameData;                 /* 0x00 */
    unsigned int reserved04;
    unsigned int flag;              /* 0x08 */
    unsigned int mode;              /* 0x0C */
    unsigned int reserved10;
    unsigned int saveType;          /* 0x14 */
    unsigned int reserved18;
    unsigned int ready;             /* 0x1C */
    unsigned char pad[8];
} MgSaveParam;

#define AT(p, off)  ((void *)((char *)(p) + (off)))
#define PTR(p, off) (*(void **)((char *)(p) + (off)))

void MYSTERY_TaskNetExit(void *task, int *state, void *pArg)
{
    char *p = (char *)pArg;
    int result;
    MgSaveParam *sp;

    if (bw_hook_enabled("mystery_gift")) {
        /* no Nintendo WFC login: the download, answered from the relay */
        MYSTERY_UTIL_SetTaskHandler(task, MYSTERY_TaskReceive);
        return;
    }
    result = MYSTERY_NET_UpdateWifiError(PTR(p, W_NET));
    switch (result) {
    case 1: case 2:
        MYSTERY_NET_Exit((char *)PTR(p, W_NET));
        MYSTERY_UTIL_SetTaskHandler(task, MYSTERY_TaskMenu);
        break;
    }
    switch (*state) {
    case 0:
        FADE_Start(3, 0, 16, 0);
        *state = 1; return;
    case 1:
        if (!FADE_IsActive()) {
            MYSTERY_SetScrollParam(AT(p, W_SCROLL), 0);
            *state = 2; return;
        }
        break;
    case 2:
        if (PTR(p, W_TEXTBOX)) {
            MYSTERY_UTIL_DeleteMenuWindow(PTR(p, W_TEXTBOX));
            PTR(p, W_TEXTBOX) = 0;
        }
        MYSTERY_DeleteClact(AT(p, W_CLACT));
        MYSTERY_ExitBgHeap(AT(p, W_HEAP));
        MYSTERY_GRAPHIC_Delete(PTR(p, W_GRAPHIC));
        PTR(p, W_GRAPHIC) = 0;
        *state = 3; return;
    case 3:
        sp = (MgSaveParam *)GFL_HEAP_Alloc(29, sizeof(MgSaveParam), 0, d_021DFB80, 2664);
        PTR(p, W_SAVEPARAM) = sp;
        MI_CpuFill8(sp, 0, sizeof(MgSaveParam));
        sp->gameData = PTR(p, W_GAMEDATA);
        sp->reserved04 = 0;
        sp->flag = 1;
        sp->reserved10 = 0;
        sp->reserved18 = 0;
        sp->mode = 9;
        if (!MYSTERY_DATA_IsMainSave(PTR(p, W_DATA))) sp->saveType = 2;
        else sp->saveType = 0;
        PROCSYS_PushGlobal((unsigned int)(size_t)ov115_id, gNetSaveMainProc, sp);
        *state = 5; return;
    case 5:
        PTR(p, W_GRAPHIC) = MYSTERY_GRAPHIC_Create(1, 29);
        MYSTERY_InitBgHeap(AT(p, W_HEAP), 29);
        MYSTERY_CreateClact(AT(p, W_CLACT), MYSTERY_GRAPHIC_GetClactUnit(PTR(p, W_GRAPHIC)), 29);
        PTR(p, W_TEXTBOX) = MYSTERY_UTIL_CreateMenuWindow(1, 15, PTR(p, W_PRINTQUEUE), PTR(p, W_FONT), 29);
        MYSTERY_UTIL_DrawMenuWindow(PTR(p, W_TEXTBOX), 10, 14);
        G2x_SetBlendAlpha_((void *)(size_t)0x04000050u, 4, 24, 15, 3);
        GFL_NET_RefreshWmIconAtTopRight(1, 29);
        GFL_USE_CancelChangeAndPlayBgm(1154, 65535);
        MYSTERY_SetScrollParam(AT(p, W_SCROLL), 1);
        *state = 4; return;
    case 4:
        *state = 6; return;
    case 6:
        FADE_Start(3, 16, 0, 0);
        *state = 7; return;
    case 7:
        if (!FADE_IsActive()) { *state = 8; return; }
        break;
    case 8:
        sp = (MgSaveParam *)PTR(p, W_SAVEPARAM);
        if (!sp->ready) MYSTERY_UTIL_SetTaskHandler(task, MYSTERY_TaskReceive);
        else MYSTERY_UTIL_SetTaskHandler(task, MYSTERY_TaskMenu);
        GFL_HEAP_Free(sp);
        break;
    }
}
