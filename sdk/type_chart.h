/*
 * sdk/type_chart.h -- the type chart with the types mods add (type_chart.c).
 */

#ifndef BW_PORT_TYPE_CHART_H
#define BW_PORT_TYPE_CHART_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The number of types: limit.types + 1 (17 in the game). */
u32 bw_type_count(void);

/* ov093 just loaded; game is its own chart (17 x 17).  Builds the chart with
 * the mods' types and rows and points the game's name at it: 1 when it did,
 * 0 when the game reads its own. */
int bw_type_chart_loaded(const u8 *game);

/* The chart built from the game's cells and the mods' rows, count x count,
 * or NULL when the game's own is in force. */
const u8 *bw_type_chart_built(void);

/* The mod list changed (sdk/bw_limits.c): build again at the next load. */
void bw_type_chart_forget(void);

#ifdef __cplusplus
}
#endif

#endif
