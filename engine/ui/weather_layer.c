/*
 * engine/ui/weather_layer.c -- the field's weather over a battle view.
 *
 * Draws the replayed weather (engine/world/bweather.c: the game's own
 * sheets and sprites at the game's own places on the DS main screen) over a
 * view of the battle: under the interface, over the map and the Pokemon, as
 * the field draws them over its map (the sheets first: the field draws them
 * with its map, under its sprites).  The DS screen is
 * 192 high: its pixel is the view's height / 192, the screen's left edge at
 * the view's left edge.  The game keeps its sprites in x -64..320 and spawns
 * snow up to x 382, so a 16:9 view (341 of those pixels wide) is covered; a
 * view wider than 382 of them takes its width / 382 as the pixel instead.
 * Each sprite pixel is one filled square of that size.
 */

#include <stddef.h>

#include "kit.h"
#include "bweather.h"

void ui_weather_layer(EidosUiCtx *c, int x, int y, int w, int h, unsigned frame60)
{
    BweatherSprite sp[32];
    BweatherSheet sh[2];
    EidosUiRect saved;
    unsigned n, nsh, i;
    float s;

    bweather_tick(frame60);
    n = bweather_sprites(sp, 32);
    nsh = bweather_sheets(sh, 2);
    if ((n == 0u && nsh == 0u) || w <= 0 || h <= 0) {
        return;
    }
    s = (float)h / 192.0f;
    if ((float)w > 382.0f * s) {
        s = (float)w / 382.0f;
    }
    ui_push(c, 0, 0, &saved);
    ui_clip(c, x, y, w, h);
    for (i = 0; i < nsh; i++) {
        /* a sheet: each screen pixel its texel, the picture repeated; beyond the DS screen's 256 x 192 the same
         * rule goes on (a wider view shows more of the repeat) */
        const int cols = (int)((float)w / s) + 1, rows = (int)((float)h / s) + 1;
        int px, py;

        for (py = 0; py < rows; py++) {
            const int y0 = y + (int)((float)py * s + 0.5f), y1 = y + (int)((float)(py + 1) * s + 0.5f);
            int ty = (int)(((long long)(sh[i].off_y + py * sh[i].down / 192) * sh[i].scale_y) >> 12) % sh[i].h;

            ty = ty < 0 ? ty + sh[i].h : ty;
            for (px = 0; px < cols; px++) {
                const unsigned char *q;
                int tx = (int)(((long long)(sh[i].off_x + px * sh[i].across / 256) * sh[i].scale_x) >> 12) % sh[i].w;

                tx = tx < 0 ? tx + sh[i].w : tx;
                q = sh[i].rgba + ((size_t)ty * (size_t)sh[i].w + (size_t)tx) * 4u;
                if (q[3] != 0) {
                    const int x0 = x + (int)((float)px * s + 0.5f), x1 = x + (int)((float)(px + 1) * s + 0.5f);

                    if (x1 > x0 && y1 > y0) {
                        ui_fill(c, x0, y0, x1 - x0, y1 - y0, ((u32)q[0] << 16) | ((u32)q[1] << 8) | q[2], 255);
                    }
                }
            }
        }
    }
    for (i = 0; i < n; i++) {
        int iw = 0, ih = 0, px, py;
        const unsigned char *img = bweather_image(sp[i].image, &iw, &ih);

        if (img == NULL) {
            continue;
        }
        for (py = 0; py < ih; py++) {
            const int y0 = y + (int)((float)(sp[i].y + py) * s + 0.5f), y1 = y + (int)((float)(sp[i].y + py + 1) * s + 0.5f);

            for (px = 0; px < iw; px++) {
                const unsigned char *q = img + ((size_t)py * (size_t)iw + (size_t)px) * 4u;
                const int x0 = x + (int)((float)(sp[i].x + px) * s + 0.5f), x1 = x + (int)((float)(sp[i].x + px + 1) * s + 0.5f);

                if (q[3] == 0 || x1 <= x0 || y1 <= y0) {
                    continue;
                }
                ui_fill(c, x0, y0, x1 - x0, y1 - y0, ((u32)q[0] << 16) | ((u32)q[1] << 8) | q[2], 255);
            }
        }
    }
    ui_pop(c, 0, 0, &saved);
}
