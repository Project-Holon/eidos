/*
 * engine/ui/assets.h -- the kit's game data: internal interface between
 * kit.c (drawing) and assets.c / assets_icons.c (reading the player's data).
 *
 * Nothing here is public API: engine/ui/kit.h is.  The split exists so the
 * drawing code never touches a file, and so a test can give the kit a font
 * built in memory (ui_font_load_nftr) and check text without any game data.
 *
 * Where each thing comes from (the extracted data root, through the port's
 * file layer, so a mod that replaces a member shows in the kit too):
 *
 *   font            a/0/2/3, members 0, 1, 2 (NFTR, 2 bits per pixel)
 *   item icons      a/0/2/5 (members named by a table in the ARM9 image,
 *                   item_gfx_member at 0x0209EB6C: u16 character member and
 *                   u16 palette member per item)
 *   Pokemon icons   a/0/0/7 (member 0 palettes, members 7.. characters,
 *                   two members per species) and a/0/1/6 (personal data,
 *                   for the form block), palette bank table in the ARM9
 *                   image (0x0209E474, one byte per icon, a nibble each
 *                   for male and female)
 *   menu icons      a/0/7/1 (the field menu: member 1 the OBJ palette,
 *                   members 5 to 12 one icon each, two 32 x 32 frames) and
 *                   a/0/8/3 (the applications' common pieces: member 20
 *                   the touch bar's icons with palette 19, members 56 to 58
 *                   the move kinds' badges with palette 33)
 *   trainer card    a/0/4/0 (member 0x1F the figures' 8 bpp characters,
 *                   0x2B / 0x2C the hero's and heroine's screens, member 6
 *                   the palette: colours 64 to 127 are the figures')
 */

#ifndef EIDOS_UI_ASSETS_H
#define EIDOS_UI_ASSETS_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* One glyph of an NFTR font: metrics and a pointer to its packed bitmap
 * (cell_w * cell_h values of 2 bits, MSB first, no row padding). */
typedef struct UiGlyph {
    int bearing;            /* x offset before the first pixel            */
    int adv;                /* advance                                    */
    int cell_w, cell_h;     /* the bitmap's size                          */
    const u8 *bitmap;
} UiGlyph;

/* Find a glyph for a Unicode code point ('?' when the font lacks it).
 * Returns 0 when the font is not available (no data); the caller then
 * draws nothing and advances by ui_font_fallback_adv(). */
int ui_font_glyph(int font, u32 code, UiGlyph *out);
int ui_font_fallback_adv(int font);

/* Give the kit a font from memory instead of the game's data (tests, and
 * a mod that replaces the font).  The bytes are copied.  Returns 0, or -1
 * when they are not an NFTR the kit can read. */
int ui_font_load_nftr(int font, const u8 *data, u32 size);
/* Forget every loaded font and image; the next use loads again. */
void ui_assets_reset(void);
void ui_icons_reset(void);      /* assets_icons.c: forget the cached images */
void ui_sprites_reset(void);    /* assets_sprites.c: the same for the figures */

/* Read one member of a NARC under the data root into a new block (free()
 * it).  The archives stay open between calls.  NULL when unavailable.  A
 * slot is an index into the kit's small table of open archives. */
enum { UI_ARC_FONT, UI_ARC_ITEM, UI_ARC_MON, UI_ARC_PERSONAL, UI_ARC_WALK1, UI_ARC_WALK2,
       UI_ARC_CARD, UI_ARC_FMENU, UI_ARC_APP, UI_ARC_COUNT };
u8 *ui_arc_member(int slot, u32 member, u32 *size);

/* Decode a 4 bpp OBJ-order image: `tiles_w` x `tiles_h` tiles in row-major
 * order, colours from a 16 entry BGR555 palette, index 0 transparent.
 * Returns a new UiImage-compatible RGBA block of (tiles_w*8)x(tiles_h*8)
 * cropped to crop_w x crop_h from the top left. */
u8 *ui_decode_4bpp(const u8 *tiles, int tiles_w, int tiles_h,
                   const u16 *pal16, int crop_w, int crop_h);

#ifdef __cplusplus
}
#endif

#endif /* EIDOS_UI_ASSETS_H */
