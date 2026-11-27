/*
 * sdk/mon_form.h -- a Pokemon's form past the box record's room
 * (sdk/mon_form.c).
 *
 * The box record keeps the form in 5 bits (arm9 poke_tool_types.h,
 * BoxPokemonBlockB.form): forms 0 to 31.  A mod can give a species more
 * forms (Arceus with 66), so the form of a Pokemon in form 32 or more is
 * kept here, keyed to the Pokemon by its personality and its trainer id,
 * and the box record holds form 0, a form the game always has.  The game's
 * record accessors (POKE_TOOL_GetBoxValue, GetPartyValueRaw, SetBoxValue,
 * SetPartyValueRaw) answer and take the real form, so every reader sees it.
 * The records are saved in the dex extension beside the save
 * (sdk/dex_save.c), in the file of the mod that ships the species' stats
 * record.
 */
#ifndef BW_PORT_MON_FORM_H
#define BW_PORT_MON_FORM_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The forms the box record holds (form : 5). */
#define BW_MON_BOX_FORMS 32u

/* One Pokemon whose form is past the box record's room.  Saved as is
 * (12 bytes). */
typedef struct BwMonForm {
    u32 personality;
    u32 otId;
    u16 species;        /* the species it had when the form was set (kept on evolution) */
    u8 form;            /* the real form, 32 to 255 */
    u8 boxForm;         /* what the box record holds meanwhile (0) */
} BwMonForm;

/* The records in force (all the loaded files' and the ones set since). */
u32 bw_mon_form_count(void);
const BwMonForm *bw_mon_form_at(u32 i);

/* sdk/dex_save.c: a file's records once read (a record already in force
 * is kept), all of them dropped (a new game, another save), and the files
 * to be read again before the next question (the mod list changed). */
void bw_mon_form_merge(const BwMonForm *recs, u32 n);
void bw_mon_form_clear(void);
void bw_mon_form_unready(void);

/* At the game's save: the records of Pokemon the save no longer holds go
 * (`save` is the main save's work area: every Pokemon in the save keeps its
 * personality there in the clear). */
void bw_mon_form_keep_saved(const void *save, u32 size);

/* The battle's base form (BtlPokeParam.baseForm is 5 bits,
 * stubs/ov/patch_ov093_btl_pokeparam_form.c): set when the battle takes the
 * Pokemon in, and read back where the battle reads baseForm (`bits` is what
 * the field holds). */
void bw_bpp_base_form_set(const void *bpp, u32 form);
u32 bw_bpp_base_form(const void *bpp, u32 bits);

#ifdef __cplusplus
}
#endif

#endif /* BW_PORT_MON_FORM_H */
