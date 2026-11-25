/*
 * sdk/mod_popup.h -- a mod's pop-ups (sdk/mod_popup.c).
 */

#ifndef BW_PORT_MOD_POPUP_H
#define BW_PORT_MOD_POPUP_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BW_POPUP_QUEUE     8u
#define BW_POPUP_TITLE_MAX 64u     /* bytes of UTF-8 with the 0 */
#define BW_POPUP_TEXT_MAX  160u

/* The services table's popup: 0, or -1 (nothing to show, or eight wait). */
int bw_mod_popup(const char *title, const char *text, u32 icon);

/* For the interface: the first pop-up waiting (1) or none (0); `serial`
 * changes each time the first one changes.  _done takes it off. */
int  bw_mod_popup_peek(const char **title, const char **text, u32 *icon, u32 *serial);
void bw_mod_popup_done(void);
/* All of them off (the mods stopped). */
void bw_mod_popup_clear(void);

#ifdef __cplusplus
}
#endif

#endif /* BW_PORT_MOD_POPUP_H */
