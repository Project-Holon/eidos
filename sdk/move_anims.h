/*
 * sdk/move_anims.h -- the animation a move plays in battle, from a mod's
 * table, and the preview of one (docs/game-tables.md, move_anims.txt).
 *
 * The battle view plays a move's animation from the move script archive
 * (member = the move's id), for the game's moves only: an id from 0x231 on
 * is read as one of the other effects (ov094 btlv_effvm.c,
 * BTLV_EFFVM_StartScript).  A mod's eidos/tables/move_anims.txt gives a
 * move another move's animation:
 *
 *     <move id> = <the game's move whose animation it plays, 1..559>
 *
 * A move with no line plays its own when it is one of the game's, else the
 * one of the move it works like (move_handlers.txt), else Pound's.
 *
 * The treated move
 * ----------------
 *
 * The effect machine singles out a few moves by id (Transform, Mirror Move,
 * Nature Power, Future Sight, Doom Desire, Baton Pass, Ally Switch,
 * Tailwind: the Substitute doll, the HP gauges, the side spots).  Those
 * cases are about what the move does: Mirror Move and Nature Power call
 * another move, Baton Pass and Ally Switch move the user, Future Sight hits
 * a turn later.  So an animation is treated as:
 *
 *   - a game move: itself, whatever script it plays;
 *   - an added move: the game move its move_handlers.txt line names;
 *   - an added move with no game base: the game move whose script it plays,
 *     or no move (0x230, which the machine treats in no special way) when
 *     it plays a mod's own script.
 */
#ifndef BW_PORT_MOVE_ANIMS_H
#define BW_PORT_MOVE_ANIMS_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The game's moves with an animation of their own: 1 .. BW_ANIM_LAST. */
#define BW_ANIM_LAST 559u

/* The effect ids the battle view asks for: below BW_ANIM_MOVE_END a move's
 * script, member id of a/0/6/6 (the game's BTLV_EFFECT_MOVE_END); with
 * BW_ANIM_REQUEST set, a request of the port (bw_move_anim_request). */
#define BW_ANIM_MOVE_END 0x231u
#define BW_ANIM_NO_MOVE  0x230u     /* the move-range id with no special case */
#define BW_ANIM_REQUEST  0x8000u

/* Read every enabled mod's table again; a line that does not read is said
 * on stderr and left out. */
void bw_move_anims_load(void);

/* The move whose animation script `move` plays (a member of a/0/6/6). */
u32 bw_move_anim(u32 move);

/* The game move whose special cases the animation of `move` gets (above),
 * or BW_ANIM_NO_MOVE. */
u32 bw_move_anim_treated(u32 move);

/*
 * The effect id the battle view asks for to play `move`'s animation: the
 * script's move itself when it is a game move treated as itself (the game's
 * own path), else BW_ANIM_REQUEST | an index the two functions below read
 * back (the request is a u16 and may wait in the effect queue, so it holds
 * no more than that).  The member and the treated id of an id without
 * BW_ANIM_REQUEST are the id itself.
 */
u32 bw_move_anim_request(u32 move);
u32 bw_move_anim_request_move(u32 request);     /* the move it plays for, 0 for another effect */
u32 bw_move_anim_request_script(u32 request);
u32 bw_move_anim_request_treated(u32 request);

/*
 * PlaySoundByName (an effect script command of the port, docs/game-tables.md):
 * the sound sequence named `name` in the sound archive the game plays, a mod's
 * `<mod>:<name>` (sdk/vfs_sdat.h) or a game sequence's own name.  -1 when
 * there is none.
 */
int bw_anim_sound_id(const char *name);

/*
 * A battle particle system's pools: live emitters and particles (the game's
 * 24 and 200, arm9 PARTICLE_CreateSystem), shared by every emitter of the
 * file and its children.  A mod's eidos/tables/particles.txt raises them
 * for a particle file (a member of a/0/0/6):
 *
 *     <member> = emitters <1..1000> particles <1..10000>
 *
 * either key alone keeps the other's default; a value below the game's is
 * the game's.
 */
#define BW_ANIM_GAME_EMITTERS   24u
#define BW_ANIM_GAME_PARTICLES  200u
#define BW_ANIM_MAX_EMITTERS    1000u
#define BW_ANIM_MAX_PARTICLES   10000u

void bw_anim_particle_pools(u32 member, u32 *emitters, u32 *particles);
int  bw_anim_particles_parse_line(const char *line, char *why, int why_size);

/*
 * A particle file's work buffer (CmdLoadParticleResource): the bytes the
 * system takes from it for its pools and the file's loader for its
 * records, at least the game's 0x4800; 0 when the file cannot be loaded
 * (bad header).  `file` is the member's bytes.
 */
u32 bw_anim_particle_work(const u8 *file, u32 size, u32 emitters, u32 particles);

/*
 * The work buffer of a system whose need passes the game's 0x4800 comes
 * from the port, not the battle heap; the effect VM's DeleteEmitter frees
 * it here.  bw_anim_particle_free answers 1 for a buffer of the port's.
 */
void *bw_anim_particle_alloc(u32 size);
int   bw_anim_particle_free(void *buffer);

/*
 * A type's colour for the effect scripts (ParticleColor), BGR555: the
 * types.txt `color` of the type (a game type's own line, or an added
 * type's `color`, else its first `colors`), else the base shade of the
 * type's battle move button: colour 8 of palette row 0 of its a/0/1/1
 * member (the battle's per-type table, ov094 d_0220A5F0, which lists the
 * added types' buttons too; Normal's for a type past them).  Read while the
 * battle's overlay is loaded.
 */
u16 bw_anim_type_color(u32 type);

/*
 * Whether an effect script shows its background now: BG3 holds an a/0/9/5
 * picture (commands 36 EffectBackground and 40 BG3Priority set bit 4,
 * gfxReloadPending, of the effect VM's status word; the script's end
 * clears it).  The VM's Create hands its status word over, Destroy NULL.
 */
void bw_anim_effvm_flags(const u32 *flags);
int  bw_anim_effect_bg_shown(void);

/*
 * The preview (eidos --preview-move ID [--preview-species A B]): a battle
 * that plays the move's animation over and over.  0 when no preview.
 */
void bw_preview_set(u32 move, u32 attacker, u32 defender);
u32  bw_preview_move(void);
u32  bw_preview_attacker(void);
u32  bw_preview_defender(void);

/* For the tests: parse one line (0 read, 1 nothing, -1 refused with
 * `why`), and forget every line. */
int  bw_move_anims_parse_line(const char *line, char *why, int why_size);
void bw_move_anims_clear(void);

#ifdef __cplusplus
}
#endif

#endif /* BW_PORT_MOVE_ANIMS_H */
