/*
 * sdk/mod_tracker.c -- what a mod shows on the field's Tracker
 * (mod_tracker.h).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mod_tracker.h"

/* sdk/mod_savedata.h: the namespace of the mod that is calling (weak: the
 * tests link without the loader) */
extern const char *bw_mod_savedata_namespace(void) __attribute__((weak));

static char *g_text;
static char  g_owner[32];
static u32   g_stamp;

int bw_mod_tracker_set(const char *text, u32 size)
{
    char *t;

    if (text == NULL || size > BW_MOD_TRACKER_MAX) {
        return -1;
    }
    t = (char *)malloc((size_t)size + 1);
    if (t == NULL) {
        return -1;
    }
    memcpy(t, text, size);
    t[size] = '\0';
    free(g_text);
    g_text = t;
    snprintf(g_owner, sizeof g_owner, "%s",
             bw_mod_savedata_namespace != NULL && bw_mod_savedata_namespace() != NULL
             ? bw_mod_savedata_namespace() : "");
    g_stamp++;
    return 0;
}

void bw_mod_tracker_clear(void)
{
    if (g_text != NULL) {
        free(g_text);
        g_text = NULL;
        g_owner[0] = '\0';
        g_stamp++;
    }
}

const char *bw_mod_tracker_text(void)
{
    return g_text != NULL ? g_text : "";
}

const char *bw_mod_tracker_owner(void)
{
    return g_owner;
}

u32 bw_mod_tracker_stamp(void)
{
    return g_stamp;
}
