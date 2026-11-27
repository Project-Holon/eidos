/*
 * sdk/snd_se.c -- the player a sound effect a mod adds plays on, and the
 * sound heap that holds it.
 *
 * GFL_USE_GetSeSlot (arm9 gfl_use.c) picks a sound effect's slot from its
 * sequence record (the player number, byte 5, minus one) for the game's
 * sound effects, sequences 0x546 to 0x81A, and puts every other sequence on
 * slot 1.  A mod's sequences are appended after the game's last
 * (sdk/vfs_sdat.h: from 0x81C on), so a sound effect a mod adds played on
 * slot 1 whatever player its declaration names.  Here a mod's sequence
 * takes its own record's player too; the game's are as they were.
 *
 * GFL_USE_InitSound (arm9 gfl_use.c) builds the sound heap over the game's
 * static 0x95AD0-byte buffer.  The sound archive merge (sdk/vfs_sdat.c)
 * adds to what goes in that heap: grown INFO and FAT blocks, player heaps
 * raised for a mod's larger sound effects, a mod's longer music.  With
 * additions the heap is built over a buffer of the game's size plus those
 * bytes, so the room the game leaves for its music stays what it was.  With
 * none it is the game's own buffer, as on the DS.
 */

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

#include "types.h"
#include "vfs.h"
#include "vfs_sdat.h"

#if defined(__GNUC__) || defined(__clang__)
#define GAME_FN __attribute__((weak))
#else
#define GAME_FN
#endif

extern u8 *NNS_SndArcGetSeqParam(int entryNo) GAME_FN;

#define GAME_SE_FIRST   0x546u
#define GAME_SE_END     0x81Bu      /* the game's sound effects end here */
#define FIRST_MOD_SEQ   0x81Cu      /* the game's sequence count: a mod's from here */

int GFL_USE_GetSeSlot(u32 seqNo)
{
    u8 *param;

    if (seqNo < GAME_SE_FIRST || (seqNo >= GAME_SE_END && seqNo < FIRST_MOD_SEQ)) {
        return 1;
    }
    param = NNS_SndArcGetSeqParam((int)seqNo);
    if (param == NULL || param[5] == 0) {
        return 1;                   /* (no such sequence, or no player: as the game's other ids) */
    }
    return param[5] - 1;
}

/* ------------------------------------------------------------------ */
/* The sound heap                                                      */
/* ------------------------------------------------------------------ */

extern void  NNS_SndInit(void) GAME_FN;
extern void *NNS_SndHeapCreate(void *start, u32 size) GAME_FN;
extern int   NNS_SndArcInit(void *arc, const char *path, void *heap, int loadSymbols) GAME_FN;
extern void  GFL_USE_ResetSound(int clearHeap) GAME_FN;
extern int   OS_IsRunOnTwl(void) GAME_FN;
extern void  SNDEXi_Init(void) GAME_FN;

/* The game's globals (gfl_use.c), through their references (sdk/armdata.h). */
extern void *bw_dsref__SndState GAME_FN;
extern void *bw_dsref__SndArc GAME_FN;
extern void *bw_dsref__SndArcPath GAME_FN;
extern void *bw_dsref__SndHeapBuf GAME_FN;

#define SND_HEAP_SIZE   0x95AD0u    /* GFL_USE_InitSound's */
#define SOUND_ARCHIVE   "wb_sound_data.sdat"

/* The head of gfl_use.c's struct SndState, up to the heap pointer. */
typedef struct SndStateHead {
    u16   activeMask;
    u16   pad02;
    int (*startFilter)(u32 seqNo);
    u32   countdown;
    u32   loaderSeqNo;
    void *heap;
} SndStateHead;

static u8 *g_heap_buf;      /* the larger buffer, kept for a later init */
static u32 g_heap_buf_size;

/* The buffer and size of the sound heap: the game's, or a larger one. */
static void *sound_heap_buffer(u32 *size)
{
    const u8 *img;
    u32 img_size, handle, extra;

    *size = SND_HEAP_SIZE;
    /* the merge runs at the archive's first open: have it now */
    if (bw_vfs_acquire(SOUND_ARCHIVE, &img, &img_size, &handle) == 1) {
        bw_vfs_release(handle);
    }
    extra = bw_sdat_sound_heap_extra();
    if (extra == 0) {
        return bw_dsref__SndHeapBuf;
    }
    if (g_heap_buf == NULL || g_heap_buf_size < SND_HEAP_SIZE + extra) {
        u8 *p = (u8 *)realloc(g_heap_buf, SND_HEAP_SIZE + extra);

        if (p == NULL) {
            fprintf(stderr, "eidos: no memory for a %u-byte sound heap: the game's is used\n",
                    (unsigned)(SND_HEAP_SIZE + extra));
            return bw_dsref__SndHeapBuf;
        }
        g_heap_buf = p;
        g_heap_buf_size = SND_HEAP_SIZE + extra;
        fprintf(stderr, "eidos: sound heap %u bytes, %u of them for the mods' sounds\n",
                (unsigned)g_heap_buf_size, (unsigned)extra);
    }
    *size = g_heap_buf_size;
    return g_heap_buf;
}

void GFL_USE_InitSound(void)
{
    SndStateHead *state = (SndStateHead *)bw_dsref__SndState;
    void *heap, *buf;
    u32 size;

    buf = sound_heap_buffer(&size);
    NNS_SndInit();
    heap = NNS_SndHeapCreate(buf, size);
    state->heap = heap;
    NNS_SndArcInit(bw_dsref__SndArc, (const char *)bw_dsref__SndArcPath, heap, 0);
    GFL_USE_ResetSound(1);
    if (OS_IsRunOnTwl() == 1) {
        SNDEXi_Init();
    }
}
