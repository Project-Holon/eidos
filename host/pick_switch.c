/*
 * host/pick_switch.c -- the ROM "dialog" on the Switch (install/picker.h's
 * bw_pick_rom): the system has no file list an app can open, so the ROM is
 * a file the player copies to the SD card, next to the program:
 *
 *   sdmc:/switch/eidos/<anything>.nds
 *
 * The first .nds file there is the answer (docs/consoles.md).
 */

#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "host.h"
#include "picker.h"

#define HOST_SWITCH_DIR "sdmc:/switch/eidos/"

int host_pick_rom(const char *title, char *out, size_t n)
{
    DIR *d = opendir(HOST_SWITCH_DIR);
    struct dirent *e;

    (void)title;
    if (n > 0) {
        out[0] = '\0';
    }
    if (d == NULL) {
        snprintf(out, n, "Copy the game's ROM to %s on the SD card.", HOST_SWITCH_DIR);
        return BW_PICK_UNAVAILABLE;
    }
    while ((e = readdir(d)) != NULL) {
        size_t len = strlen(e->d_name);

        if (len > 4 && strcasecmp(e->d_name + len - 4, ".nds") == 0) {
            snprintf(out, n, "%s%s", HOST_SWITCH_DIR, e->d_name);
            closedir(d);
            return BW_PICK_OK;
        }
    }
    closedir(d);
    snprintf(out, n, "No .nds file in %s on the SD card.", HOST_SWITCH_DIR);
    return BW_PICK_UNAVAILABLE;
}
