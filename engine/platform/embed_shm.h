/*
 * engine/platform/embed_shm.h -- the game shown inside another program (the
 * spot editor's Preview): the shared memory both sides map.
 *
 * The host (the spot editor) makes the mapping, names it in EIDOS_EMBED and
 * starts eidos_boot.  The game keeps its window hidden and, each frame,
 * writes its finished picture here instead of showing it; it reads its
 * keyboard, mouse and window size from here instead of its window's.
 *
 *   host writes   want_w, want_h, keys, mouse, beat, quit
 *   game writes   pixels, w, h, frame (last: a new picture is whole once
 *                 frame has moved), done
 *
 * The picture is RGB, rows top-down, w * h * 3 bytes, at most
 * EIDOS_EMBED_MAX_W x EIDOS_EMBED_MAX_H.  A host that stops beating (beat
 * unchanged for EIDOS_EMBED_STALE_MS) is gone: the game quits.
 */
#ifndef EIDOS_EMBED_SHM_H
#define EIDOS_EMBED_SHM_H

#include <stdint.h>

#define EIDOS_EMBED_MAGIC    0x44424D45u        /* "EMBD" */
#define EIDOS_EMBED_MAX_W    2560
#define EIDOS_EMBED_MAX_H    1440
#define EIDOS_EMBED_STALE_MS 3000u

typedef struct EidosEmbedShm {
    uint32_t magic;
    uint32_t size;                  /* the whole mapping, bytes            */
    /* the host's */
    volatile uint32_t want_w, want_h;   /* the picture size it shows       */
    volatile uint32_t keys[16];     /* SDL scancodes held, one bit each     */
    volatile int32_t mx, my;        /* the mouse in picture pixels, -1 out  */
    volatile uint32_t buttons;      /* SDL_BUTTON_MASK bits held            */
    volatile int32_t wheel;         /* wheel steps, summed since the start  */
    volatile uint32_t beat;         /* moves while the host runs            */
    volatile uint32_t quit;         /* 1: the game ends                     */
    /* the game's */
    volatile uint32_t w, h;         /* the picture in `pixels`              */
    volatile uint32_t frame;        /* moves after each whole picture       */
    volatile uint32_t done;         /* 1: the game has ended                */
    uint32_t pad[32];
    uint8_t pixels[];               /* EIDOS_EMBED_MAX_W * MAX_H * 3        */
} EidosEmbedShm;

#define EIDOS_EMBED_BYTES \
    (sizeof(EidosEmbedShm) + (size_t)EIDOS_EMBED_MAX_W * EIDOS_EMBED_MAX_H * 3u)

#endif
