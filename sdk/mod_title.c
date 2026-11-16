/*
 * sdk/mod_title.c -- see mod_title.h.
 */
#include "mod_title.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mod_hooks.h"

static int g_in_settings;
static int g_rebuild;

int bw_mod_title_settings_on(void)
{
    return bw_hook_enabled(BW_HOOK_TITLE_SETTINGS);
}

int bw_mod_title_in_settings(void)
{
    return g_in_settings;
}

int bw_mod_title_set_in_settings(int on)
{
    g_in_settings = on != 0;
    return g_in_settings;
}

int bw_mod_title_set_rebuild(void)
{
    g_rebuild = 1;
    return 1;
}

int bw_mod_title_take_rebuild(void)
{
    int r = g_rebuild;
    g_rebuild = 0;
    return r;
}

static int g_autostart;
static int g_multiplayer;

void bw_mod_title_open_multiplayer(void)
{
    g_multiplayer = 1;
}

int bw_mod_title_take_multiplayer(void)
{
    int r = g_multiplayer;

    g_multiplayer = 0;
    return r;
}

static char g_join[16];

void bw_mod_title_request_join(const char *code)
{
    size_t i, n = 0;

    for (i = 0; code != NULL && code[i] != '\0' && n + 1 < sizeof(g_join); i++) {
        char c = code[i];

        if (c >= 'a' && c <= 'z') {
            c = (char)(c - 'a' + 'A');
        }
        if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) {
            g_join[n++] = c;      /* a code is letters and digits */
        }
    }
    g_join[n] = '\0';
}

int bw_mod_title_join_pending(void)
{
    return g_join[0] != '\0';
}

const char *bw_mod_title_take_join(void)
{
    static char taken[sizeof(g_join)];

    if (g_join[0] == '\0') {
        return NULL;
    }
    memcpy(taken, g_join, sizeof(taken));
    g_join[0] = '\0';
    return taken;
}

void bw_mod_title_request_autostart(void)
{
    g_autostart = 1;
}

int bw_mod_title_autostart_pending(void)
{
    return g_autostart;
}

int bw_mod_title_take_autostart(void)
{
    int r = g_autostart;

    g_autostart = 0;
    if (r && getenv("EIDOS_MP_UI_LOG") != NULL) {
        fprintf(stderr, "eidos: title: autostart taken\n");
    }
    return r;
}

static int g_return_ov;
static const void *g_return_table;

void bw_mod_title_set_return(int overlay, const void *table)
{
    g_return_ov = overlay;
    g_return_table = table;
}

int bw_mod_title_return_ov(void)
{
    return g_return_ov;
}

const void *bw_mod_title_return_table(void)
{
    return g_return_table;
}
