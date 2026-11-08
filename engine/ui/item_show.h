/*
 * engine/ui/item_show.h -- the key-item showcase and the item card of the
 * one-screen field (item_show.c; recipe_field.c runs them).
 */
#ifndef EIDOS_UI_ITEM_SHOW_H
#define EIDOS_UI_ITEM_SHOW_H

#include "kit.h"

#define ITEM_CARD_H 34
/* The card's top on a canvas ch tall: above the message box at the bottom. */
#define ITEM_CARD_Y(ch) ((ch) - 4 - 42 - 8 - ITEM_CARD_H)

/* The card over the "obtained" message: the item's icon in a well, its
 * name and its pocket. */
void item_card_draw(EidosUiCtx *c, int x, int y, int w, const UiImage *icon, const char *name,
                    const char *pocket);
int item_card_width(const char *name, const char *pocket);

/* One frame of the showcase: f the mock-up's frame (0 at the flash; held
 * at ITEM_SHOW_HOLD while the game's cut-in lasts, then on to the card,
 * whole from 136, done at ITEM_SHOW_END), live the frames since the flash
 * (the bob and the turning rays go on while f holds), (cx, cy) where the
 * item rests, the card's place. */
#define ITEM_SHOW_HOLD 100.f
#define ITEM_SHOW_END 146.f
void item_show_draw(EidosUiCtx *c, float f, float live, int cx, int cy, int card_x, int card_y, int card_w,
                    const UiImage *icon, const char *name, const char *pocket);

/* Badge i (0 Trio .. 7 Legend) as the badge case shows it, 64 x 64; NULL
 * when the game's graphics cannot be read. */
const UiImage *ui_badge_image(int badge);

#endif
