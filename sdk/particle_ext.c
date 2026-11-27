/*
 * sdk/particle_ext.c -- the particle manager past the game's fixed arrays
 * (arm9 particle.c): more than 16 particle systems at once, more than 64
 * texture and 64 palette handles a system, and a system with pools of its
 * own size (sdk/particle_ext.h).
 *
 * The game's particle manager holds 16 systems (ParticleMan.systems[16]),
 * each with its own work allocator bound to its slot
 * (PARTICLE_AllocWorkSlot0 .. 15).  A system keeps the texture and palette
 * handles it loads in two arrays of 64 (texHandles, palHandles), filled by
 * PARTICLE_AllocTexVramTracked and PARTICLE_AllocPlttVramTracked with no
 * check of the end: a file with a 65th texture writes over the palette
 * handles.  Every system takes 24 emitters and 200 particles
 * (SPL_EMITTER_AllocNodeFromHeap(.., 0x18, 0xC8, ..)).
 *
 * This file defines the game's own names for the functions that touch those
 * arrays, so they are cut from the game's unit (docs/game-tree.md, "Cuts")
 * and these take their place.  Each does what the game's does while the
 * game's arrays have room, so the game's own effects run as before:
 *
 *   - a system past the 16 goes into a port table of 48 more, with a work
 *     allocator of its own (the game's 16 are tried first);
 *   - a handle past the 64 goes into a port list of the system's, freed with
 *     the system;
 *   - bw_particle_create makes a system with the pools its caller asks for
 *     (the battle effect VM, for a mod's file that declares them).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "move_anims.h"
#include "particle_ext.h"
#include "types.h"

#if defined(__GNUC__) || defined(__clang__)
#define GAME_FN __attribute__((weak))
#else
#define GAME_FN
#endif

typedef __UINTPTR_TYPE__ PtcUptr;
typedef u32 (*PtcLoadFn)(u32 size, u32 kind);
typedef void *(*PtcAllocFn)(u32 size);

typedef struct PtcVec {
    s32 x, y, z;
} PtcVec;

/* struct ParticleSys (arm9 particle.c), the same members in the same order. */
typedef struct ParticleSys {
    void *engine;
    void *resourceBlock;
    void *activeEffect;
    PtcUptr workBase;
    PtcUptr workCursor;
    PtcUptr workEnd;
    PtcLoadFn texLoadFn;
    PtcLoadFn palLoadFn;
    void *camera;
    u32 cameraShiftX;
    u32 cameraShiftY;
    u32 cameraShiftZ;
    u16 cameraAngle;
    u16 flags;                  /* bit 0: resourceBlock is not owned */
    PtcVec cameraPos;
    PtcVec cameraTarget;
    PtcVec cameraUp;
    u32 texHandles[0x40];
    u32 palHandles[0x40];
    u8 loadMode;                /* PARTICLE_LOAD_* */
    u8 pad259;
    u8 slotIndex;
    u8 projType;
    u16 texCount;
    u16 palCount;
} ParticleSys;

/* struct ParticleMan (arm9 particle.c). */
typedef struct ParticleMan {
    ParticleSys *systems[16];
    PtcUptr callbackArg;
    ParticleSys *current;
} ParticleMan;

#if !defined(__LP64__) && !defined(_WIN64)
_Static_assert(sizeof(ParticleSys) == 0x260, "struct ParticleSys");
_Static_assert(__builtin_offsetof(ParticleSys, texCount) == 0x25C, "ParticleSys.texCount");
_Static_assert(sizeof(ParticleMan) == 0x48, "struct ParticleMan");
#endif

#define GAME_SYSTEMS    16
#define GAME_HANDLES    0x40
#define GAME_EMITTERS   0x18        /* PARTICLE_CreateSystem's */
#define GAME_PARTICLES  0xC8

#define LOAD_TEX_BULK   1
#define LOAD_TEX_ENTRY  2
#define LOAD_PAL_BULK   4
#define LOAD_PAL_ENTRY  8

/* The game's functions and data (arm9). */
extern void *GFL_HEAP_Alloc(u16 heapId, u32 size, int clear, const char *file, u16 line) GAME_FN;
extern void GFL_HEAP_Free(void *p) GAME_FN;
extern void MI_CpuFill8(void *dst, u8 value, u32 size) GAME_FN;
extern void PARTICLE_InitCamera(ParticleSys *sys, void *params, u16 angle, const PtcVec *pos, const PtcVec *target,
                                const PtcVec *up, u16 heapId) GAME_FN;
extern void PARTICLE_KillAllEffects(ParticleSys *sys) GAME_FN;
extern void *SPL_EMITTER_AllocNodeFromHeap(PtcAllocFn alloc, u16 emitters, u16 particles, u16 limitA, u16 limitB,
                                           u16 maxResIndex) GAME_FN;
extern void NNS_GfdLoadVramManager(u32 *table) GAME_FN;
extern u32 NNS_GfdFreeTexVram(u32 handle) GAME_FN;
extern void NNSi_GfdSetVramManagerWork(void *work) GAME_FN;
extern int NNS_GfdFreePlttVram(u32 handle) GAME_FN;
extern void G3D_CAMERA_Delete(void *camera) GAME_FN;
extern void *PARTICLE_AllocWorkSlot0(u32 size) GAME_FN;
extern void *PARTICLE_AllocWorkSlot1(u32 size) GAME_FN;
extern void *PARTICLE_AllocWorkSlot2(u32 size) GAME_FN;
extern void *PARTICLE_AllocWorkSlot3(u32 size) GAME_FN;
extern void *PARTICLE_AllocWorkSlot4(u32 size) GAME_FN;
extern void *PARTICLE_AllocWorkSlot5(u32 size) GAME_FN;
extern void *PARTICLE_AllocWorkSlot6(u32 size) GAME_FN;
extern void *PARTICLE_AllocWorkSlot7(u32 size) GAME_FN;
extern void *PARTICLE_AllocWorkSlot8(u32 size) GAME_FN;
extern void *PARTICLE_AllocWorkSlot9(u32 size) GAME_FN;
extern void *PARTICLE_AllocWorkSlot10(u32 size) GAME_FN;
extern void *PARTICLE_AllocWorkSlot11(u32 size) GAME_FN;
extern void *PARTICLE_AllocWorkSlot12(u32 size) GAME_FN;
extern void *PARTICLE_AllocWorkSlot13(u32 size) GAME_FN;
extern void *PARTICLE_AllocWorkSlot14(u32 size) GAME_FN;
extern void *PARTICLE_AllocWorkSlot15(u32 size) GAME_FN;
extern void PARTICLE_DrawSystem(ParticleSys *sys) GAME_FN;
extern int PARTICLE_UpdateSystem(ParticleSys *sys) GAME_FN;

/* The game's globals (sdk/armdata.h): the manager, the file name the heap
 * records, the default camera, the two VRAM allocators. */
extern void *bw_dsref_g_pParticleMan GAME_FN;
extern void *bw_dsref_sParticleFile GAME_FN;
extern void *bw_dsref_sDefaultCameraPos GAME_FN;
extern void *bw_dsref_sDefaultCameraTarget GAME_FN;
extern void *bw_dsref_sDefaultCameraUp GAME_FN;
extern void *bw_dsref_NNS_GfdDefaultFuncAllocTexVram GAME_FN;
extern void *bw_dsref_NNS_GfdDefaultFuncAllocPlttVram GAME_FN;

#define MANAGER()       (*(ParticleMan **)bw_dsref_g_pParticleMan)
#define TEX_ALLOC()     (*(u32 (**)(u32, u32, u32))bw_dsref_NNS_GfdDefaultFuncAllocTexVram)
#define PLTT_ALLOC()    (*(u32 (**)(u32, u32, u32))bw_dsref_NNS_GfdDefaultFuncAllocPlttVram)

/* ------------------------------------------------------------------ */
/* Systems past the 16                                                 */
/* ------------------------------------------------------------------ */

static ParticleSys *s_more[BW_PARTICLE_MORE_SYSTEMS];

/* A system's work allocator: the game's bump allocator (PARTICLE_AllocWorkSlotN)
 * over the system in port slot `i`. */
static void *more_alloc(int i, u32 size)
{
    ParticleSys *sys = s_more[i];
    PtcUptr start = sys->workCursor;
    PtcUptr next = start + size;

    if (next & 3) {
        next += 4 - (next & 3);
    }
    sys->workCursor = next;
    return (void *)start;
}

/* One allocator a slot: the system's allocator takes only a size, so each
 * slot has its own function, as the game's 16 do. */
#define MORE_ALLOC_AT(name, n) static void *name(u32 size) { return more_alloc(n, size); }
MORE_ALLOC_AT(more_alloc_0, 0)   MORE_ALLOC_AT(more_alloc_1, 1)   MORE_ALLOC_AT(more_alloc_2, 2)
MORE_ALLOC_AT(more_alloc_3, 3)   MORE_ALLOC_AT(more_alloc_4, 4)   MORE_ALLOC_AT(more_alloc_5, 5)
MORE_ALLOC_AT(more_alloc_6, 6)   MORE_ALLOC_AT(more_alloc_7, 7)   MORE_ALLOC_AT(more_alloc_8, 8)
MORE_ALLOC_AT(more_alloc_9, 9)   MORE_ALLOC_AT(more_alloc_10, 10) MORE_ALLOC_AT(more_alloc_11, 11)
MORE_ALLOC_AT(more_alloc_12, 12) MORE_ALLOC_AT(more_alloc_13, 13) MORE_ALLOC_AT(more_alloc_14, 14)
MORE_ALLOC_AT(more_alloc_15, 15) MORE_ALLOC_AT(more_alloc_16, 16) MORE_ALLOC_AT(more_alloc_17, 17)
MORE_ALLOC_AT(more_alloc_18, 18) MORE_ALLOC_AT(more_alloc_19, 19) MORE_ALLOC_AT(more_alloc_20, 20)
MORE_ALLOC_AT(more_alloc_21, 21) MORE_ALLOC_AT(more_alloc_22, 22) MORE_ALLOC_AT(more_alloc_23, 23)
MORE_ALLOC_AT(more_alloc_24, 24) MORE_ALLOC_AT(more_alloc_25, 25) MORE_ALLOC_AT(more_alloc_26, 26)
MORE_ALLOC_AT(more_alloc_27, 27) MORE_ALLOC_AT(more_alloc_28, 28) MORE_ALLOC_AT(more_alloc_29, 29)
MORE_ALLOC_AT(more_alloc_30, 30) MORE_ALLOC_AT(more_alloc_31, 31) MORE_ALLOC_AT(more_alloc_32, 32)
MORE_ALLOC_AT(more_alloc_33, 33) MORE_ALLOC_AT(more_alloc_34, 34) MORE_ALLOC_AT(more_alloc_35, 35)
MORE_ALLOC_AT(more_alloc_36, 36) MORE_ALLOC_AT(more_alloc_37, 37) MORE_ALLOC_AT(more_alloc_38, 38)
MORE_ALLOC_AT(more_alloc_39, 39) MORE_ALLOC_AT(more_alloc_40, 40) MORE_ALLOC_AT(more_alloc_41, 41)
MORE_ALLOC_AT(more_alloc_42, 42) MORE_ALLOC_AT(more_alloc_43, 43) MORE_ALLOC_AT(more_alloc_44, 44)
MORE_ALLOC_AT(more_alloc_45, 45) MORE_ALLOC_AT(more_alloc_46, 46) MORE_ALLOC_AT(more_alloc_47, 47)

static const PtcAllocFn s_more_alloc[BW_PARTICLE_MORE_SYSTEMS] = {
    more_alloc_0,  more_alloc_1,  more_alloc_2,  more_alloc_3,  more_alloc_4,  more_alloc_5,
    more_alloc_6,  more_alloc_7,  more_alloc_8,  more_alloc_9,  more_alloc_10, more_alloc_11,
    more_alloc_12, more_alloc_13, more_alloc_14, more_alloc_15, more_alloc_16, more_alloc_17,
    more_alloc_18, more_alloc_19, more_alloc_20, more_alloc_21, more_alloc_22, more_alloc_23,
    more_alloc_24, more_alloc_25, more_alloc_26, more_alloc_27, more_alloc_28, more_alloc_29,
    more_alloc_30, more_alloc_31, more_alloc_32, more_alloc_33, more_alloc_34, more_alloc_35,
    more_alloc_36, more_alloc_37, more_alloc_38, more_alloc_39, more_alloc_40, more_alloc_41,
    more_alloc_42, more_alloc_43, more_alloc_44, more_alloc_45, more_alloc_46, more_alloc_47,
};

/* The game's per-slot allocators (the table sSystemEngineIds holds). */
static PtcAllocFn game_alloc(int i)
{
    static const PtcAllocFn fns[GAME_SYSTEMS] = {
        PARTICLE_AllocWorkSlot0,  PARTICLE_AllocWorkSlot1,  PARTICLE_AllocWorkSlot2,  PARTICLE_AllocWorkSlot3,
        PARTICLE_AllocWorkSlot4,  PARTICLE_AllocWorkSlot5,  PARTICLE_AllocWorkSlot6,  PARTICLE_AllocWorkSlot7,
        PARTICLE_AllocWorkSlot8,  PARTICLE_AllocWorkSlot9,  PARTICLE_AllocWorkSlot10, PARTICLE_AllocWorkSlot11,
        PARTICLE_AllocWorkSlot12, PARTICLE_AllocWorkSlot13, PARTICLE_AllocWorkSlot14, PARTICLE_AllocWorkSlot15,
    };
    return fns[i];
}

/* ------------------------------------------------------------------ */
/* Handles past the 64                                                 */
/* ------------------------------------------------------------------ */

typedef struct MoreHandles {
    ParticleSys *sys;
    u32 *h[2];                  /* 0 textures, 1 palettes: handle 64 on */
    u32 n[2], cap[2];
} MoreHandles;

static MoreHandles *s_handles;
static u32 s_handles_n, s_handles_cap;

static MoreHandles *more_handles(ParticleSys *sys, int add)
{
    u32 i;

    for (i = 0; i < s_handles_n; i++) {
        if (s_handles[i].sys == sys) {
            return &s_handles[i];
        }
    }
    if (!add) {
        return NULL;
    }
    if (s_handles_n == s_handles_cap) {
        u32 cap = s_handles_cap != 0 ? s_handles_cap * 2 : 8;
        MoreHandles *grown = (MoreHandles *)realloc(s_handles, cap * sizeof *grown);

        if (grown == NULL) {
            return NULL;
        }
        s_handles = grown;
        s_handles_cap = cap;
    }
    memset(&s_handles[s_handles_n], 0, sizeof s_handles[0]);
    s_handles[s_handles_n].sys = sys;
    return &s_handles[s_handles_n++];
}

static void keep_handle(ParticleSys *sys, int kind, u32 handle)
{
    MoreHandles *m = more_handles(sys, 1);

    if (m == NULL) {
        return;
    }
    if (m->n[kind] == m->cap[kind]) {
        u32 cap = m->cap[kind] != 0 ? m->cap[kind] * 2 : 16;
        u32 *grown = (u32 *)realloc(m->h[kind], cap * sizeof *grown);

        if (grown == NULL) {
            fprintf(stderr, "eidos: particle handles: out of memory\n");
            return;
        }
        m->h[kind] = grown;
        m->cap[kind] = cap;
    }
    m->h[kind][m->n[kind]++] = handle;
}

/* Free the handles past the 64 (entry mode) and forget the system's list. */
static void free_more_handles(ParticleSys *sys)
{
    MoreHandles *m = more_handles(sys, 0);
    u32 i;

    if (m == NULL) {
        return;
    }
    for (i = 0; i < m->n[0]; i++) {
        if (m->h[0][i] != 0 && (sys->loadMode & LOAD_TEX_ENTRY)) {
            NNS_GfdFreeTexVram(m->h[0][i]);
        }
    }
    for (i = 0; i < m->n[1]; i++) {
        if (m->h[1][i] != 0 && (sys->loadMode & LOAD_PAL_ENTRY)) {
            NNS_GfdFreePlttVram(m->h[1][i]);
        }
    }
    free(m->h[0]);
    free(m->h[1]);
    *m = s_handles[--s_handles_n];
}

/* A texture or palette the VRAM manager had no room for (its handle is 0, and
 * the particles sample whatever lies at address 0): said once a system. */
static void say_vram_full(ParticleSys *sys, int kind, u32 index)
{
    static ParticleSys *said[2];

    if (said[kind] != sys) {
        said[kind] = sys;
        fprintf(stderr, "eidos: a particle file's %s %u found no room in the %s memory; it and the later ones show "
                "garbage (the battle has 128 KB of textures and 16 KB of palettes for everything on screen)\n",
                kind == 0 ? "texture" : "palette", (unsigned)index, kind == 0 ? "texture" : "palette");
    }
}

/* The game's: a texture's VRAM, its handle kept for the system's end. */
u32 PARTICLE_AllocTexVramTracked(u32 szByte, u32 is4x4comp)
{
    u32 v = TEX_ALLOC()(szByte, is4x4comp, 0);
    ParticleSys *base = MANAGER()->current;
    u16 n = base->texCount;

    if (v == 0) {
        say_vram_full(base, 0, n);
    }
    base->texCount = (u16)(n + 1);
    if (n < GAME_HANDLES) {
        base->texHandles[n] = v;
    } else {
        keep_handle(base, 0, v);
    }
    return (u16)v * 8;
}

/* The game's: a palette's VRAM, likewise. */
u32 PARTICLE_AllocPlttVramTracked(u32 a, u32 b)
{
    u32 v = PLTT_ALLOC()(a, b, 0);
    ParticleSys *base = MANAGER()->current;
    u16 n = base->palCount;

    if (v == 0) {
        say_vram_full(base, 1, n);
    }
    base->palCount = (u16)(n + 1);
    if (n < GAME_HANDLES) {
        base->palHandles[n] = v;
    } else {
        keep_handle(base, 1, v);
    }
    return (u16)v * 8;
}

/* ------------------------------------------------------------------ */
/* Tints                                                               */
/* ------------------------------------------------------------------ */

/* The heads of struct SPLManager and struct SPLEmitter (arm9 spl_emitter.c),
 * the same members in the same order, up to the emitter's colour. */
typedef struct PtcEmitter {
    struct PtcEmitter *next;
    struct PtcEmitter *prev;
    void *particlesFirst;
    int particlesCount;
    void *particlesLast;
    void *childFirst;
    int childCount;
    void *childLast;
    void *resource;
    u32 state;
    PtcVec position;
    PtcVec velocity;
    PtcVec particleInitVelocity;
    u16 age;
    s16 emissionCountFractional;
    s16 axis[3];
    u16 initAngle;
    s32 emissionCount;
    s32 radius;
    s32 length;
    s32 initVelPositionAmplifier;
    s32 initVelAxisAmplifier;
    s32 baseScale;
    u16 particleLifeTime;
    u16 color;                  /* 0x7FFF at the start: multiplies the particles' */
} PtcEmitter;

typedef struct PtcManager {
    PtcAllocFn alloc;
    PtcEmitter *activeFirst;
    int activeCount;
    PtcEmitter *activeLast;
} PtcManager;

#if !defined(__LP64__) && !defined(_WIN64)
_Static_assert(__builtin_offsetof(PtcEmitter, color) == 0x72, "SPLEmitter.color");
#endif

typedef struct Tint {
    ParticleSys *sys;
    u16 color;
} Tint;

static Tint s_tints[GAME_SYSTEMS + BW_PARTICLE_MORE_SYSTEMS];

static void apply_tint(const Tint *t)
{
    PtcEmitter *e;

    for (e = ((PtcManager *)t->sys->engine)->activeFirst; e != NULL; e = e->next) {
        e->color = t->color;
    }
}

void bw_particle_set_tint(void *sys, u16 color)
{
    u32 i, free_at = (u32)-1;

    if (sys == NULL) {
        return;
    }
    for (i = 0; i < sizeof s_tints / sizeof s_tints[0]; i++) {
        if (s_tints[i].sys == sys) {
            break;
        }
        if (s_tints[i].sys == NULL && free_at == (u32)-1) {
            free_at = i;
        }
    }
    if (i == sizeof s_tints / sizeof s_tints[0]) {
        if (free_at == (u32)-1) {
            return;
        }
        i = free_at;
    }
    s_tints[i].sys = (ParticleSys *)sys;
    s_tints[i].color = color;
    apply_tint(&s_tints[i]);
    if (color == 0x7FFF) {
        s_tints[i].sys = NULL;      /* untinted again: the game's own colour */
    }
}

static void forget_tint(ParticleSys *sys)
{
    u32 i;

    for (i = 0; i < sizeof s_tints / sizeof s_tints[0]; i++) {
        if (s_tints[i].sys == sys) {
            s_tints[i].sys = NULL;
        }
    }
}

/* Before a system's update: the emitters started since get the tint before
 * they draw a particle (PARTICLE_Main draws, then updates). */
static void tint_before_update(ParticleSys *sys)
{
    u32 i;

    for (i = 0; i < sizeof s_tints / sizeof s_tints[0]; i++) {
        if (s_tints[i].sys == sys) {
            apply_tint(&s_tints[i]);
        }
    }
}

/* ------------------------------------------------------------------ */
/* Create and delete                                                   */
/* ------------------------------------------------------------------ */

void *bw_particle_create(void *workBuf, u32 workSize, u32 withCamera, u32 emitters, u32 particles, u32 limitA,
                         u32 limitB, u32 maxResIndex, u32 heapId)
{
    ParticleMan *man = MANAGER();
    ParticleSys *sys;
    PtcAllocFn alloc;
    int i;

    for (i = 0; i < GAME_SYSTEMS; i++) {
        if (man->systems[i] == NULL) {
            break;
        }
    }
    if (i == GAME_SYSTEMS) {
        for (i = 0; i < BW_PARTICLE_MORE_SYSTEMS; i++) {
            if (s_more[i] == NULL) {
                break;
            }
        }
        if (i == BW_PARTICLE_MORE_SYSTEMS) {
            return NULL;
        }
        i += GAME_SYSTEMS;
    }
    sys = (ParticleSys *)GFL_HEAP_Alloc((u16)heapId, sizeof *sys, 0, (const char *)bw_dsref_sParticleFile, 285);
    MI_CpuFill8(sys, 0, sizeof *sys);
    sys->cameraPos = *(const PtcVec *)bw_dsref_sDefaultCameraPos;
    sys->cameraTarget = *(const PtcVec *)bw_dsref_sDefaultCameraTarget;
    sys->cameraUp = *(const PtcVec *)bw_dsref_sDefaultCameraUp;
    sys->flags &= (u16)~1u;
    MI_CpuFill8(workBuf, 0, workSize);
    sys->workBase = (PtcUptr)workBuf;
    sys->workCursor = (PtcUptr)workBuf;
    sys->workEnd = (PtcUptr)workBuf + workSize;
    sys->slotIndex = (u8)i;
    if (i < GAME_SYSTEMS) {
        man->systems[i] = sys;
        alloc = game_alloc(i);
    } else {
        s_more[i - GAME_SYSTEMS] = sys;
        alloc = s_more_alloc[i - GAME_SYSTEMS];
    }
    if (withCamera == 1) {
        PARTICLE_InitCamera(sys, NULL, 0x2000, NULL, NULL, NULL, (u16)heapId);
    }
    sys->engine = SPL_EMITTER_AllocNodeFromHeap(alloc, (u16)emitters, (u16)particles, (u16)limitA, (u16)limitB,
                                                (u16)maxResIndex);
    return sys;
}

/* The game's, with the 48 more slots after its 16. */
void *PARTICLE_CreateSystem(void *workBuf, u32 workSize, u32 withCamera, u32 limitA, u32 limitB, u32 maxResIndex,
                            u32 heapId)
{
    return bw_particle_create(workBuf, workSize, withCamera, GAME_EMITTERS, GAME_PARTICLES, limitA, limitB,
                              maxResIndex, heapId);
}

/* The game's, with the handles past the 64 and the slots past the 16. */
void PARTICLE_DeleteSystem(void *p)
{
    ParticleSys *sys = (ParticleSys *)p;
    ParticleMan *man = MANAGER();
    int i;

    PARTICLE_KillAllEffects(sys);
    if (sys->loadMode & LOAD_TEX_BULK) {
        NNS_GfdLoadVramManager(sys->texHandles);
    } else if (sys->loadMode & LOAD_TEX_ENTRY) {
        for (i = 0; i < GAME_HANDLES; i++) {
            if (sys->texHandles[i] != 0) {
                NNS_GfdFreeTexVram(sys->texHandles[i]);
                sys->texHandles[i] = 0;
            }
        }
    }
    if (sys->loadMode & LOAD_PAL_BULK) {
        NNSi_GfdSetVramManagerWork(sys->palHandles);
    } else if (sys->loadMode & LOAD_PAL_ENTRY) {
        for (i = 0; i < GAME_HANDLES; i++) {
            if (sys->palHandles[i] != 0) {
                NNS_GfdFreePlttVram(sys->palHandles[i]);
                sys->palHandles[i] = 0;
            }
        }
    }
    free_more_handles(sys);
    forget_tint(sys);
    sys->loadMode = 0;
    sys->activeEffect = NULL;
    if (sys->resourceBlock != NULL && (sys->flags & 1) == 0) {
        if (!bw_anim_particle_free(sys->resourceBlock)) {   /* the port's when the heap was full */
            GFL_HEAP_Free(sys->resourceBlock);
        }
        sys->resourceBlock = NULL;
    }
    for (i = 0; i < GAME_SYSTEMS; i++) {
        if (man->systems[i] == sys) {
            man->systems[i] = NULL;
            break;
        }
    }
    for (i = 0; i < BW_PARTICLE_MORE_SYSTEMS; i++) {
        if (s_more[i] == sys) {
            s_more[i] = NULL;
            break;
        }
    }
    if (sys->camera != NULL) {
        G3D_CAMERA_Delete(sys->camera);
    }
    GFL_HEAP_Free(sys);
}

void PARTICLE_ExitManager(void)
{
    ParticleMan *man = MANAGER();
    int i;

    for (i = 0; i < GAME_SYSTEMS; i++) {
        if (man->systems[i] != NULL) {
            PARTICLE_DeleteSystem(man->systems[i]);
        }
    }
    for (i = 0; i < BW_PARTICLE_MORE_SYSTEMS; i++) {
        if (s_more[i] != NULL) {
            PARTICLE_DeleteSystem(s_more[i]);
        }
    }
    GFL_HEAP_Free(man);
    MANAGER() = NULL;
}

/* ------------------------------------------------------------------ */
/* The walkers                                                         */
/* ------------------------------------------------------------------ */

int PARTICLE_CountSystems(void)
{
    ParticleMan *man = MANAGER();
    int n = 0;
    int i;

    for (i = 0; i < GAME_SYSTEMS; i++) {
        n += man->systems[i] != NULL;
    }
    for (i = 0; i < BW_PARTICLE_MORE_SYSTEMS; i++) {
        n += s_more[i] != NULL;
    }
    return n;
}

int PARTICLE_DrawAllSystems(void)
{
    ParticleMan *man = MANAGER();
    int n = 0;
    int i;

    for (i = 0; i < GAME_SYSTEMS; i++) {
        if (man->systems[i] != NULL) {
            PARTICLE_DrawSystem(man->systems[i]);
            n++;
        }
    }
    for (i = 0; i < BW_PARTICLE_MORE_SYSTEMS; i++) {
        if (s_more[i] != NULL) {
            PARTICLE_DrawSystem(s_more[i]);
            n++;
        }
    }
    return n;
}

int PARTICLE_UpdateAllSystems(void)
{
    ParticleMan *man = MANAGER();
    int n = 0;
    int i;

    for (i = 0; i < GAME_SYSTEMS; i++) {
        if (man->systems[i] != NULL) {
            tint_before_update(man->systems[i]);
            PARTICLE_UpdateSystem(man->systems[i]);
            n++;
        }
    }
    for (i = 0; i < BW_PARTICLE_MORE_SYSTEMS; i++) {
        if (s_more[i] != NULL) {
            tint_before_update(s_more[i]);
            PARTICLE_UpdateSystem(s_more[i]);
            n++;
        }
    }
    return n;
}
