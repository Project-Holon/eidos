/*
 * engine/world/bweather.h -- the field's weather, replayed.
 *
 * The game draws a zone's weather (falling snow, rain, a sandstorm's grains)
 * only while the field runs.  A weather kind (the field's weather task,
 * ov021/weather_task.c, and the kinds of overlays 24 to 28) has up to three
 * parts:
 *   - sprites: up to 32 on the main screen, spawned and moved by a few lines
 *     of game code;
 *   - sheets: one or two pictures that repeat over the whole screen and
 *     scroll (the blizzard's flurry, the storms' streaks, the desert's sand);
 *   - a light and a fog of its own.
 * A battle on the real map shows the place the field showed, so it shows
 * that weather too.  This module plays the first two parts the way the game
 * does once the weather is on (the kind's "begin at once" stage, which fills
 * the screen, then its run stage), with the game's own tables (read from its
 * overlay images) and its own pictures (a/0/5/6), one step a field frame (30
 * a second).  The third part is the battle map's (bmap_set_weather).
 *
 * Replayed: snow (weather 1), rain (2), the sandstorm (3, and 12 with its
 * sheet), the blizzard (4), the downpour (5), the thunderstorm (6), the
 * storm (7), diamond dust (8).  The fogs (9, 10, 11, 13, 14) have neither
 * sprites nor sheets.  Not replayed: the thunderstorm's lightning (flashes of
 * the field's light) and the storm's fog change in a gust, which change the
 * map's light and fog while it is drawn; the sounds; the fog the hardware
 * lays over a sheet (the blizzard's, a little).
 *
 * Positions are the game's own: pixels of the DS main screen (256 x 192),
 * so a view wider than the DS's shows more of the same field of sprites
 * where the game keeps them beyond the screen's edges.
 */

#ifndef BWEATHER_H
#define BWEATHER_H

#ifdef __cplusplus
extern "C" {
#endif

/* Ask for a weather (the game's weather id: 0 clear, 1 snow, 2 rain, 3 the
 * sandstorm ...; a negative id or a kind not replayed: none).  May be called
 * from any thread: the change is taken at the next bweather_tick. */
void bweather_want(int weather_id);

/* Advance to field frame `frame60` / 2 (frame60 counts 60 a second, as
 * bw_input_frame_number): the steps since the last call, at most 4 (a gap is
 * not caught up).  Call from the thread that draws. */
void bweather_tick(unsigned frame60);

/* One sprite on the DS main screen: its top left corner and its picture. */
typedef struct BweatherSprite {
    int x, y;
    int image;              /* for bweather_image */
} BweatherSprite;

/* The sprites now (max at most); returns their count, 0 with no weather. */
unsigned bweather_sprites(BweatherSprite *out, unsigned max);

/* A sprite's picture: w * h RGBA (alpha 0 where clear), NULL when there is
 * none. */
const unsigned char *bweather_image(int image, int *w, int *h);

/* A sheet: a picture that repeats over the whole screen.  The screen's pixel (px, py), of across x down of them,
 * shows the texel ((off_x + px) * scale_x >> 12, (off_y + py) * scale_y >> 12) of the w x h picture, repeated both
 * ways (the hardware's texture coordinates of the game's quad: one texel a screen pixel before the scale). */
typedef struct BweatherSheet {
    const unsigned char *rgba;  /* w * h RGBA, alpha 0 where clear */
    int w, h;
    int off_x, off_y;
    int scale_x, scale_y;       /* 20.12 */
    int across, down;           /* the texels across and down the DS screen (256, 192) */
} BweatherSheet;

/* The sheets shown now (max at most), the first under the second; returns their count. */
unsigned bweather_sheets(BweatherSheet *out, unsigned max);

/* The weather being played (its id), -1 none. */
int bweather_playing(void);

#ifdef __cplusplus
}
#endif

#endif
