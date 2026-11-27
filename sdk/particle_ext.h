/*
 * sdk/particle_ext.h -- the particle manager past the game's fixed arrays
 * (sdk/particle_ext.c).
 */
#ifndef BW_PORT_PARTICLE_EXT_H
#define BW_PORT_PARTICLE_EXT_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Particle systems the port holds past the game's 16. */
#define BW_PARTICLE_MORE_SYSTEMS 48

/*
 * PARTICLE_CreateSystem with pools of the caller's size: `emitters` live
 * emitters and `particles` particles (the game's systems take 24 and 200).
 * The work buffer must hold them (sdk/move_anims.h bw_anim_particle_work).
 * NULL when every slot is taken.
 */
void *bw_particle_create(void *workBuf, u32 workSize, u32 withCamera, u32 emitters, u32 particles, u32 limitA,
                         u32 limitB, u32 maxResIndex, u32 heapId);

/*
 * The colour every emitter of a system draws with: the emitter's colour,
 * which multiplies each particle's own (parents and children, as the
 * game's 0x7FFF does: white keeps the particle's colours).  BGR555; 0x7FFF
 * takes the tint away.  It holds for emitters started later too, until the
 * system is deleted.
 */
void bw_particle_set_tint(void *sys, u16 color);

#ifdef __cplusplus
}
#endif

#endif /* BW_PORT_PARTICLE_EXT_H */
