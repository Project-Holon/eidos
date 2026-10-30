/*
 * engine/platform/plat_embed.c -- the game inside another program's window
 * (engine/platform/embed_shm.h): the game's side.
 *
 * EIDOS_EMBED names the host's mapping.  With it the window stays hidden,
 * the keyboard and the mouse come from the mapping (eidos_embed_key,
 * eidos_embed_mouse, eidos_embed_edges), the window takes the host's size
 * (eidos_embed_want_size) and each picture goes to the mapping
 * (eidos_embed_pixels, eidos_embed_publish).  Windows only: elsewhere
 * eidos_embed_on() is 0 and nothing changes.
 */

#include "plat_embed.h"

#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

static EidosEmbedShm *g_shm;
static int g_tried;
static uint32_t g_last_beat, g_beat_at;
static uint32_t g_prev_buttons;
static int32_t g_prev_wheel;

static EidosEmbedShm *shm(void)
{
    if (!g_tried) {
        const char *name = getenv("EIDOS_EMBED");
        HANDLE h;

        g_tried = 1;
        if (name == NULL || name[0] == '\0') {
            return NULL;
        }
        h = OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, name);
        if (h != NULL) {
            g_shm = (EidosEmbedShm *)MapViewOfFile(h, FILE_MAP_ALL_ACCESS, 0, 0, 0);
            if (g_shm != NULL && g_shm->magic != EIDOS_EMBED_MAGIC) {
                UnmapViewOfFile(g_shm);
                g_shm = NULL;
            }
        }
        if (g_shm != NULL) {
            g_prev_wheel = g_shm->wheel;
            g_last_beat = g_shm->beat;
            g_beat_at = GetTickCount();
        }
    }
    return g_shm;
}

int eidos_embed_on(void)
{
    return shm() != NULL;
}

int eidos_embed_key(int scancode)
{
    EidosEmbedShm *s = shm();

    if (s == NULL || scancode < 0 || scancode >= 512) {
        return 0;
    }
    return (s->keys[scancode >> 5] >> (scancode & 31)) & 1u;
}

uint32_t eidos_embed_mouse(float *x, float *y)
{
    EidosEmbedShm *s = shm();

    if (s == NULL) {
        return 0;
    }
    if (x != NULL) {
        *x = (float)s->mx;
    }
    if (y != NULL) {
        *y = (float)s->my;
    }
    return s->mx < 0 || s->my < 0 ? 0u : s->buttons;
}

void eidos_embed_edges(int *pressed, int *released, int *rpressed, int *wheel)
{
    EidosEmbedShm *s = shm();
    uint32_t b, was;
    int32_t wh;

    if (s == NULL) {
        return;
    }
    b = s->mx < 0 || s->my < 0 ? 0u : s->buttons;
    was = g_prev_buttons;
    g_prev_buttons = b;
    if ((b & 1u) && !(was & 1u)) {
        *pressed = 1;
    }
    if (!(b & 1u) && (was & 1u)) {
        *released = 1;
    }
    if ((b & 4u) && !(was & 4u)) {
        *rpressed = 1;
    }
    wh = s->wheel;
    *wheel += (int)(wh - g_prev_wheel);
    g_prev_wheel = wh;
}

int eidos_embed_want_size(int *w, int *h)
{
    EidosEmbedShm *s = shm();

    if (s == NULL || s->want_w < 64 || s->want_h < 64) {
        return 0;
    }
    *w = (int)(s->want_w > EIDOS_EMBED_MAX_W ? EIDOS_EMBED_MAX_W : s->want_w);
    *h = (int)(s->want_h > EIDOS_EMBED_MAX_H ? EIDOS_EMBED_MAX_H : s->want_h);
    return 1;
}

unsigned char *eidos_embed_pixels(int w, int h)
{
    EidosEmbedShm *s = shm();

    if (s == NULL || w <= 0 || h <= 0 || w > EIDOS_EMBED_MAX_W || h > EIDOS_EMBED_MAX_H) {
        return NULL;
    }
    return s->pixels;
}

void eidos_embed_publish(int w, int h)
{
    EidosEmbedShm *s = shm();

    if (s == NULL) {
        return;
    }
    s->w = (uint32_t)w;
    s->h = (uint32_t)h;
    MemoryBarrier();
    s->frame++;
}

int eidos_embed_quit(void)
{
    EidosEmbedShm *s = shm();
    uint32_t now;

    if (s == NULL) {
        return 0;
    }
    if (s->quit) {
        return 1;
    }
    now = GetTickCount();
    if (s->beat != g_last_beat) {
        g_last_beat = s->beat;
        g_beat_at = now;
    }
    return now - g_beat_at > EIDOS_EMBED_STALE_MS;
}

void eidos_embed_end(void)
{
    EidosEmbedShm *s = shm();

    if (s != NULL) {
        s->done = 1;
    }
}

#else

int eidos_embed_on(void) { return 0; }
int eidos_embed_key(int scancode) { (void)scancode; return 0; }
uint32_t eidos_embed_mouse(float *x, float *y) { (void)x; (void)y; return 0; }
void eidos_embed_edges(int *pressed, int *released, int *rpressed, int *wheel)
{
    (void)pressed; (void)released; (void)rpressed; (void)wheel;
}
int eidos_embed_want_size(int *w, int *h) { (void)w; (void)h; return 0; }
unsigned char *eidos_embed_pixels(int w, int h) { (void)w; (void)h; return NULL; }
void eidos_embed_publish(int w, int h) { (void)w; (void)h; }
int eidos_embed_quit(void) { return 0; }
void eidos_embed_end(void) { }

#endif
