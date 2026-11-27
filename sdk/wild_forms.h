/*
 * sdk/wild_forms.h -- the form of a wild encounter slot, from a mod's
 * eidos/tables/wild_forms.txt (sdk/wild_forms.c).
 *
 * A slot of an encounter table (a/1/2/6, aletheia ov021
 * field_encount_types.h EncountSlot) keeps the form in 5 bits, and 31 there
 * means a random form: a table can name forms 0 to 30 only.  A line of
 * wild_forms.txt gives a slot any form, and
 * stubs/ov/patch_ov021_field_encount_form.c rolls the wild Pokemon in it.
 */
#ifndef BW_PORT_WILD_FORMS_H
#define BW_PORT_WILD_FORMS_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* No line for the slot. */
#define BW_WILD_FORM_NONE (-1)

/* The encounter types of the table (EncountData.rate's index), in the
 * order of the slot groups: grass, doubleGrass, shakingGrass, surf,
 * surfRipple, fishing, fishingRipple. */
#define BW_WILD_TYPES 7u

/* Reads every mod's wild_forms.txt (a later mod's line wins). */
void bw_wild_forms_load(void);
void bw_wild_forms_clear(void);

/* One line: 1 for a blank or comment line, 0 for a line taken, -1 for a
 * line left out (`why` says why). */
int bw_wild_forms_parse_line(const char *line, char *why, int why_size);

/* The form of slot `slot` of encounter type `type` in encounter table
 * `table` (the a/1/2/6 member) in season `season` (0 spring to 3 winter):
 * a line for that season first, then a line for every season;
 * BW_WILD_FORM_NONE when there is none. */
int bw_wild_form(int table, int season, int type, int slot);

/* stubs/ov/patch_ov021_field_encount_form.c: the slots FIELD_ENCOUNT_LoadSlots
 * copied (`count` slots of 4 bytes at `slots`) and the whole form of each:
 * the lines of `table` (-1 none) in `season` for encounter type `type`, or
 * for a swarm (`swarm_form` 0 to 255, else -1) that form for the one slot.
 * bw_wild_slot_form gives the form noted for a slot of them, or
 * BW_WILD_FORM_NONE. */
void bw_wild_slots_note(const void *slots, int count, int table, int season, int type, int swarm_form);
int bw_wild_slot_form(const void *slot);

#ifdef __cplusplus
}
#endif

#endif /* BW_PORT_WILD_FORMS_H */
