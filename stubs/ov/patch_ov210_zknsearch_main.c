/*
 * patches: src/game/ov210/zknsearch_main.c ZKNSEARCH_MAIN_LookupEntryId ZKNSEARCH_MAIN_LookupSavedEntryId
 *
 * The Pokedex search names the types mods add (sdk/type_icons.c; the list
 * itself: patch_ov210_zknsearch_list.c).
 *
 * The search names a type by its line in bank 277, the search screen's own
 * texts, through a table of 17 (d_021C072E): the list's rows, the chosen
 * types under TYPE, the summary of a search.  A type past 16 would read past
 * the table.  Its name is at line 256 + type - 17 of bank 277, which the
 * port copies there from bank 199, the game's type names, when the mods are
 * applied; these two give that line for an added type and the table's for
 * the game's.  A saved search can name a type past the types the mods give
 * now (a mod was taken out): it shows as "-" (line 48, the empty choice).
 * (The build outside Japan.)
 *
 * symbols:
 *   d_021C072E 0x021C072E data ov210
 */

typedef unsigned short u16;

#define GAME_TYPES 17
#define SEARCH_NAME_LINE 256    /* sdk/type_icons.c */
#define SEARCH_NONE_LINE 48     /* "-" */

struct ZknSearchWork;

/* sdk/type_chart.h */
extern unsigned bw_type_count(void);

extern u16 d_021C072E[];

u16 ZKNSEARCH_MAIN_LookupEntryId(struct ZknSearchWork *work, int index)
{
    (void)work;
    if (index >= GAME_TYPES) {
        return (u16)(SEARCH_NAME_LINE + index - GAME_TYPES);
    }
    return d_021C072E[index];
}

u16 ZKNSEARCH_MAIN_LookupSavedEntryId(struct ZknSearchWork *work, int index)
{
    (void)work;
    if (index >= (int)bw_type_count()) {
        return SEARCH_NONE_LINE;
    }
    if (index >= GAME_TYPES) {
        return (u16)(SEARCH_NAME_LINE + index - GAME_TYPES);
    }
    return d_021C072E[index];
}
