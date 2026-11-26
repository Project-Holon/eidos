/*
 * sdk/palanm_fade.c -- PALANM_RequestFadeOverwrite (arm9 palanm.c) with the
 * added types' palette row.
 *
 * A screen that shows type icons dims or lights its three type palette rows
 * with one fade request over them (the battle's move buttons: channel 3,
 * rows 8..10, when a Pokemon can only Struggle or has fainted, and back).
 * The types mods add draw from a row of their own on that screen
 * (sdk/type_icons.c, bw_type_screen_push).  A request that covers all three
 * type rows of a pushed screen's animator covers that row too, so an added
 * type's icon dims with the others.  Every other request is the game's own;
 * the body is the game's function.
 */

#include "types.h"
#include "type_icons.h"

#if defined(__GNUC__) || defined(__clang__)
#define GAME_FN __attribute__((weak))
#else
#define GAME_FN
#endif

/* The game's records (arm9 palanm.c). */
typedef struct PaletteFadeParam {
    u16 paletteBits;
    u16 wait : 6;
    u16 current : 5;
    u16 end : 5;
    u16 color : 15;
    u16 direction : 1;
    u16 speed : 5;
    u16 waitCount : 6;
    u16 pad : 5;
} PaletteFadeParam;

typedef struct PaletteFadeWork {
    u16 *defaultWork;
    u16 *transWork;
    u32 size;
    PaletteFadeParam param;
} PaletteFadeWork;

typedef struct PaletteFade {
    PaletteFadeWork work[14];
    u16 requestFlag : 2;
    u16 requestBits : 14;
    u16 transBits : 14;
    u16 taskActive : 1;
    u16 autoTrans : 1;
    u8 forceStop;
} PaletteFade;

typedef char palette_fade_work_is_0x14[sizeof(PaletteFadeWork) == 0x14 || sizeof(void *) != 4 ? 1 : -1];

struct TcbTask;
typedef void (*TcbFunc)(struct TcbTask *task, void *work);

extern u8 PALANM_CheckBit(u32 val, int bit) GAME_FN;
extern void PALANM_MaskColorBits(s32 idx, PaletteFadeWork *c, u16 *pv) GAME_FN;
extern void PALANM_SetFadeParam(PaletteFadeParam *s, u16 f0, s32 a, u8 b, u8 c, u16 d) GAME_FN;
extern void PALANM_SetTransBit(PaletteFade *p, int channel) GAME_FN;
extern void PALANM_ApplyFade(PaletteFade *p, u32 idx, u32 arg) GAME_FN;
extern void PALANM_FadeTask(struct TcbTask *a, void *arg) GAME_FN;
extern struct TcbTask *TCB_AddTask(void *sys, TcbFunc func, void *work, u32 priority) GAME_FN;

int PALANM_RequestFadeOverwrite(PaletteFade *p, u16 mask, u16 col, s16 step, u8 b, u8 c, u16 d, void *ctx)
{
    u16 keep;
    int ret = 0;
    u8 i;

    for (i = 0; i < 14; i++) {
        if ((mask >> i) & 1u) {
            col |= bw_type_screen_fade_extra(p, i, col);
        }
    }
    keep = col;
    for (i = 0; i < 14; i++) {
        if (PALANM_CheckBit(mask, i) == 1) {
            PALANM_MaskColorBits(i, &p->work[i], &col);
            PALANM_SetFadeParam(&p->work[i].param, col, step, b, c, d);
            PALANM_SetTransBit(p, i);
            if (i >= 4) {
                PALANM_ApplyFade(p, i, 0x100);
            } else {
                PALANM_ApplyFade(p, i, 0x10);
            }
            col = keep;
            ret = 1;
        }
    }
    if (ret == 1) {
        p->requestBits = mask;
        if (p->taskActive == 0) {
            p->taskActive = 1;
            p->requestFlag = 1;
            p->forceStop = 0;
            TCB_AddTask(ctx, PALANM_FadeTask, p, (u32)-2);
        }
    }
    return ret;
}
