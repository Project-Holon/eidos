/*
 * sdk/battle_view.h -- the one-screen layout's battle backdrop, game side.
 *
 * The game's battle scenery (two stage platforms and the field disc and sky
 * dome) is not drawn; the real map where the battle began is drawn instead,
 * with the battle's own camera (engine/platform/plat_gl3d.c draws it from
 * the G3dBackdrop the snapshot carries).  docs/battle-backdrop.md.
 */

#ifndef BW_PORT_BATTLE_VIEW_H
#define BW_PORT_BATTLE_VIEW_H

#ifdef __cplusplus
extern "C" {
#endif

/* Non-zero while a battle runs and its scenery is the real map. */
int bw_battle_view_active(void);

/* Where the Pokemon at battle position `pos` (0..7, even the player's side)
 * was drawn a moment ago: box = left, top, right, bottom as shares of the
 * 3D picture (0..1, y down).  0 when it was not drawn or no battle runs. */
int bw_battle_view_mon_box(int pos, float box[4]);

#ifdef __cplusplus
}
#endif

#endif
