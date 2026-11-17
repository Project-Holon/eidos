/*
 * engine/world/bweather.h -- the field's weather, replayed.
 *
 * The game draws a zone's weather (falling snow, a sandstorm's grains) only
 * while the field runs: up to 32 small sprites on the main screen, spawned
 * and moved by a few lines of game code a weather kind (the field's weather
 * task, ov021/weather_task.c, and the kinds of overlays 24 to 28).  A battle
 * on the real map shows the place the field showed, so it shows that weather
 * too: this module plays the kind the way the game does once the weather is
 * on (its "begin at once" stage, which fills the screen, then its run
 * stage), with the game's own tables (read from its overlay images) and its
 * own sprite pictures (a/0/5/6), one step a field frame (30 a second).
 *
 * Replayed so far: snow (weather 1) and the sandstorm (3 and 12).  The other
 * kinds (rain, blizzard, downpour, thunderstorm, storm, diamond dust, the
 * fogs) show nothing yet.  The sprites only: the kinds' fog and light
 * changes and the sandstorm's scrolling sheet are not replayed yet.
 *
 * Positions are the game's own: pixels of the DS main screen (256 x 192),
 * wrapped as the game wraps them (x -64..320, y -64..256), so a view wider
 * than the DS's shows more of the same field of sprites.
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

/* The weather being played (its id), -1 none. */
int bweather_playing(void);

#ifdef __cplusplus
}
#endif

#endif
