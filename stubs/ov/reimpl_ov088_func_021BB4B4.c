/*
 * function: func_021BB4B4
 * address:  0x021BB4B4
 * packet:   pkt_ov088_021BB4B4
 * region:   ov088
 * compiler: dsi/1.1p1
 * flags:    -O4,p
 * lang:     c99
 * isa:      thumb
 * replaces: src/matched/ov/ov088_func_021BB4B4.c
 * port hook "title_settings" (docs/hooks.md, docs/settings-design.md):
 * the row list. The last row, kind 7, is always present: it is the game's
 * conditional row when its test holds, else the SETTINGS row. While the settings
 * screen is up the list holds one kind per settings row (kind 1 is the header,
 * kinds 2.. the option rows, as many as eidos_sui_row_count says).
 *
 * With the hook off this is the matched function, unchanged.
 *
 * original status:   EXACT
 *
 * symbols:
 *   func_020071EC  0x020071EC  thumb
 *   func_0200C308  0x0200C308  thumb
  */
/* sdk/mod_title.h, spelled out: the overlay link header declares what the
 * file itself does not (docs/hooks.md). */
extern int bw_mod_title_settings_on(void);
extern int bw_mod_title_in_settings(void);
extern int bw_mod_title_set_in_settings(int on);
#define BW_TITLE_ROW_SETTINGS 7
#define BW_TITLE_SEL_SETTINGS 9
extern int eidos_sui_row_count(void);
/* sdk/save_slots.h: a new game left before its first save does not hide
 * the save before it */
extern void bw_slots_settle(void);
typedef unsigned char u8;

extern int func_020071EC(void *);
extern u8 func_0200C308(void *, unsigned);

void func_021BB4B4(char *p)
{
    unsigned i = 0;
    int hook = bw_mod_title_settings_on();
    if (hook && bw_mod_title_in_settings()) {
        int n = eidos_sui_row_count();
        if (n > 7)
            n = 7;
        for (i = 0; i < (unsigned)n; i++)
            (p + i)[0x120] = i + 1;
        while (i < 8) {
            (p + i)[0x120] = 8;
            i++;
        }
        return;
    }
    if (hook)
        bw_slots_settle();
    if (func_020071EC(*(void **)p) == 1) {
        p[0x120] = 0;
        i++;
    }
    (p + i)[0x120] = 1;
    i++;
    if (hook) {
        /* MULTIPLAYER (kind 6, the game's Mic Test) comes third */
        (p + i)[0x120] = 6;
        i++;
    }
    (p + i)[0x120] = 2;
    i++;
    if (func_0200C308(*(void **)(p + 8), 1)) {
        (p + i)[0x120] = 3;
        i++;
    }
    if (func_0200C308(*(void **)(p + 8), 2)) {
        (p + i)[0x120] = 4;
        i++;
    }
    /* NINTENDO WFC SETTINGS (kind 5) is never listed: the service closed in
     * 2014, and its screen mounts a memory archive (ov016 DWCi_ARClInit,
     * FS_ChangeDir into "utility:/") that the port's file system does not
     * carry */
    if (!hook) {
        (p + i)[0x120] = 6;
        i++;
    }
    if (func_0200C308(*(void **)(p + 8), 3) || hook) {
        (p + i)[0x120] = 7;
        i++;
    }
    while (i < 8) {
        (p + i)[0x120] = 8;
        i++;
    }
}
