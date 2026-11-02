/*
 * host/pick_mobile.c -- the ROM dialog on phones (install/picker.h's
 * bw_pick_rom for Android and iOS).
 *
 * Android: SDL's own dialog, which is the Storage Access Framework's
 * ACTION_OPEN_DOCUMENT.  It answers on the activity's UI thread with a
 * content:// URI while the game's thread waits here.  SDL opens that URI
 * as a file descriptor (ContentResolver.openFileDescriptor), which the
 * host keeps and names /proc/self/fd/<n>; reopening that path is refused,
 * so host/wasi.c hands the module a copy of the descriptor instead and the
 * installer reads it with no change (docs/installer.md).  The ROM stays
 * where the player keeps it; nothing is copied.
 *
 * iOS: UIDocumentPickerViewController (host/pick_ios.m).
 */

#include <stdio.h>
#include <string.h>
#include <SDL3/SDL.h>

#include "host.h"
#include "picker.h"

static void put(char *out, size_t n, const char *s)
{
    if (n > 0) {
        snprintf(out, n, "%s", s != NULL ? s : "");
    }
}

#if defined(__ANDROID__)
#include <unistd.h>

static SDL_Semaphore *g_done;
static char g_uri[2048];
static int g_rc;

static void SDLCALL picked(void *user, const char *const *files, int filter)
{
    (void)user;
    (void)filter;
    if (files == NULL) {
        g_rc = BW_PICK_UNAVAILABLE;
        put(g_uri, sizeof g_uri, SDL_GetError());
    } else if (files[0] == NULL) {
        g_rc = BW_PICK_CANCELLED;
    } else {
        g_rc = BW_PICK_OK;
        put(g_uri, sizeof g_uri, files[0]);
    }
    SDL_SignalSemaphore(g_done);
}

int host_pick_rom(const char *title, char *out, size_t n)
{
    static int held = -1;       /* the last chosen ROM's descriptor */
    SDL_IOStream *io;
    FILE *fp;
    int fd;

    (void)title;
    put(out, n, "");
    if (g_done == NULL && (g_done = SDL_CreateSemaphore(0)) == NULL) {
        put(out, n, SDL_GetError());
        return BW_PICK_UNAVAILABLE;
    }
    /* No filter: Android knows no MIME type for .nds, and a filter of
     * types it does not know would hide every file. */
    SDL_ShowOpenFileDialog(picked, NULL, NULL, NULL, 0, NULL, false);
    /* The file list covers the game, so Android pauses it and takes the
     * window's surface away.  SDL keeps and gives back the OpenGL context
     * only on the thread that pumps its events, which is this one, so it
     * pumps them while waiting (the events stay queued for the game). */
    while (!SDL_WaitSemaphoreTimeout(g_done, 10)) {
        SDL_PumpEvents();
    }
    fprintf(stderr, "eidos: ROM dialog: %s\n",
            g_rc == BW_PICK_OK ? g_uri : g_rc == BW_PICK_CANCELLED ? "cancelled" : g_uri);
    if (g_rc != BW_PICK_OK) {
        put(out, n, g_rc == BW_PICK_UNAVAILABLE ? g_uri : "");
        return g_rc;
    }
    io = SDL_IOFromFile(g_uri, "rb");
    fp = io != NULL ? (FILE *)SDL_GetPointerProperty(SDL_GetIOProperties(io),
                                                     SDL_PROP_IOSTREAM_STDIO_FILE_POINTER, NULL)
                    : NULL;
    fd = fp != NULL ? dup(fileno(fp)) : -1;
    if (io != NULL) {
        SDL_CloseIO(io);
    }
    if (fd < 0) {
        fprintf(stderr, "eidos: ROM dialog: cannot open it (%s)\n", SDL_GetError());
        put(out, n, "That file could not be opened.");
        return BW_PICK_UNAVAILABLE;
    }
    if (held >= 0) {
        close(held);
    }
    held = fd;
    snprintf(out, n, "/proc/self/fd/%d", fd);
    return BW_PICK_OK;
}

#else /* iOS */

int host_pick_rom_ios(const char *title, char *out, size_t n);

int host_pick_rom(const char *title, char *out, size_t n)
{
    put(out, n, "");
    return host_pick_rom_ios(title, out, n);
}

#endif
