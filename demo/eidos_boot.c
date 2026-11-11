/*
 * demo/eidos_boot.c -- run the game's own ARM9 boot on the host.
 *
 * docs/boot.md is the plan.  This executable is its driver: it installs the
 * player's ARM9 image (docs/armdata.md), resets the emulated machine, points
 * the file layer at the player's extract/, and calls NitroMain, which never
 * returns.  Time passes where the DS would let it pass: when every game
 * thread is waiting (NitroMain's OS_WaitIrq for the vblank, among others),
 * the idle thread halts, and the OS layer's halt hook (bw_os_set_halt_hook)
 * is this file's frame: one pass of the frame loop in engine/frame/, which
 * advances OS_GetTick, raises the vblank interrupt and, with a window,
 * composes and shows both screens.  The interrupt is taken when the halt
 * returns, the handler NitroMain installed runs, and the waiting thread
 * wakes, as on the console.
 *
 *     eidos_boot [--data-root PATH] [--frames N] [--window] [--profile [N]] [--watchdog S]
 *                [--screenshot N[,N...]] [--screenshot-dir DIR]
 *                [--press BUTTONS@FRAME[:HOLD]] [--input SCRIPT] [--keep-going]
 *
 * --keep-going makes a stub (a function the build does not define yet)
 * report itself once and return instead of stopping the run, so one run
 * lists every stub on its way; what follows a stub is not reliable.
 *
 * --press START@4800 presses buttons at a frame (held 4 frames unless
 * :HOLD says), --input reads a script of such events (sdk/input.h's text
 * format); both are the same timed input a window's keys join, so a run
 * past the title screen repeats exactly with no window.
 *
 * --screenshot saves both screens, as the hardware state has them at the
 * end of each named frame, to DIR/eidos_boot_frame<N>.png (DIR defaults to
 * the current directory), with or without a window.
 *
 * Started with no arguments it opens the window, as a double click does.
 * After N frames (default 600 headless; a window runs until closed) it prints a summary and exits 0.  A panic on
 * the way (a stub reached, an overlay not in this build) ends the run with
 * the panic's message and a failure status, which is how the boot's test
 * reads how far it got (tests/boot_run.cmake).  With --watchdog S, a run
 * that passes no frame for S seconds (a game thread spinning on something
 * the port never delivers) is reported with every thread's call chain
 * (sdk/plat_crash.h) and ends with status 3.
 *
 * The mod layer's own seams are here too: what a run restart means to the
 * port (sdk/mod_restart.c) and what the port does with a save that arrived
 * from a peer (sdk/mod_handoff.c).  Neither can be done where the request
 * is made, so the request is recorded and the frame loop carries it out at
 * its next safe point: the port's own reset, and then the run's start-up
 * again.  With the new game flag the game goes into a new game directly,
 * without stopping at the title, the way the title's own rows start one
 * (ov088's startmenu.c).
 *
 * Two builds, chosen by the CMake option EIDOS_BOOT:
 *
 *   OFF (the default)  the boot is not linked.  This build compiles only
 *                      argument handling and the data root, says so and
 *                      exits 0, which keeps this file compiling at both
 *                      pointer widths in every build.
 *
 *   ON                 the boot is linked: NitroMain and the closure of its
 *                      matched callees (cmake/boot.cmake).  NitroMain is
 *                      aletheia's matched src/matched/arm9/func_02005124.c,
 *                      or stubs/reimpl_02005124.c with an older aletheia;
 *                      both define NitroMain() as the original loop.
 *
 * Sound: the ARM7 stand-in plays the game's SND command lists on
 * engine/audio (sdk/arm7/snd.c).  With no audio device the driver's clock
 * is run from here, one frame's worth of output frames per frame
 * (bw_snd_render with no buffer), so sequences end and the sound alarms
 * fire on time.  With a window and an audio device, the same frame thread
 * renders that frame's output and queues it on the device (the driver is
 * the game thread's, never run from SDL's audio thread).
 *
 * The window belongs to the thread that runs the frames (the game's idle
 * thread, where the halt hook runs), so it is created at the first frame,
 * not in main().
 */

#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include "mod_frame.h"
#include "mod_handoff.h"
#include "mod_hooks.h"
#include "mod_loader.h"
#include "bw_limits.h"
#include "mod_menu.h"
#include "mod_net.h"
#include "save.h"
#include "mod_title.h"
#include "plat_save.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "frame.h"
#include "plat_crash.h"
#include "port.h"
#if EIDOS_BOOT_SDL
#include "settings.h"           /* engine/platform: the SDL3 build's */
#endif

#ifndef EIDOS_DEFAULT_DATA_ROOT
#define EIDOS_DEFAULT_DATA_ROOT ""
#endif

#ifndef EIDOS_BOOT
#define EIDOS_BOOT 0
#endif

#ifndef EIDOS_BOOT_SDL
#define EIDOS_BOOT_SDL 0
#endif

#define BOOT_DEFAULT_FRAMES 600u
#define BOOT_CPU_3D_MAX     2u

/* --screenshot: the frames to save both screens at, and where. */
#define BOOT_MAX_SHOTS 1024
static u64         g_shots[BOOT_MAX_SHOTS];
static int         g_shot_count;
static const char *g_shot_dir = ".";
/* --mod FOLDER: run with that one mod only (the player's mod list is not
 * read); --mod-log FILE: everything the port writes to stderr, the mod
 * loader's reports and the mods' own log lines with it, goes to FILE.
 * Both are for a mod tool's "run in eidos" (kenon). */
static const char *g_only_mod;
static const char *g_mod_log;
/* --g3d-trace N: the 3D command trace of frame N (hw3d.c g3d_hw_trace). */
static u64         g_trace_frame;
static int         g_trace_on;

/*
 * --press NAME@FRAME[:HOLD] and --input FILE: timed input as the text
 * script sdk/input.h reads ("<frame> press <buttons> [<hold>]"), so a run
 * can walk past the title screen with no window and repeat exactly.
 */
static char g_input_script[262144];   /* --input and --press, joined */

static int add_input_text(const char *line)
{
    size_t have = strlen(g_input_script);
    size_t add = strlen(line);

    if (have + add + 2 > sizeof(g_input_script)) {
        return -1;
    }
    memcpy(g_input_script + have, line, add);
    g_input_script[have + add] = '\n';
    g_input_script[have + add + 1] = '\0';
    return 0;
}

/* "START@4800" or "A+B@5000:10": press the buttons at that frame, held for
 * HOLD frames (default 4). */
static int parse_press(const char *arg)
{
    char buttons[64];
    char line[128];
    const char *at = strchr(arg, '@');
    unsigned long frame, hold = 4;
    char *end = NULL;

    if (at == NULL || (size_t)(at - arg) >= sizeof(buttons) || at == arg) {
        return -1;
    }
    memcpy(buttons, arg, (size_t)(at - arg));
    buttons[at - arg] = '\0';
    frame = strtoul(at + 1, &end, 10);
    if (end == at + 1) {
        return -1;
    }
    if (*end == ':') {
        const char *h = end + 1;
        hold = strtoul(h, &end, 10);
        if (end == h) {
            return -1;
        }
    }
    if (*end != '\0') {
        return -1;
    }
    snprintf(line, sizeof(line), "%lu press %s %lu", frame, buttons, hold);
    return add_input_text(line);
}

static int read_input_file(const char *path)
{
    FILE *fp = fopen(path, "rb");
    char line[512];

    if (fp == NULL) {
        return -1;
    }
    while (fgets(line, sizeof(line), fp) != NULL) {
        line[strcspn(line, "\r\n")] = '\0';
        if (add_input_text(line) != 0) {
            fclose(fp);
            return -1;
        }
    }
    fclose(fp);
    return 0;
}

/* "N[,N...]": the frames --screenshot names.  0 on success. */
static int parse_shots(const char *list)
{
    const char *p = list;

    while (*p != '\0') {
        char *end = NULL;
        unsigned long long n = strtoull(p, &end, 10);

        if (end == p || g_shot_count >= BOOT_MAX_SHOTS) {
            return -1;
        }
        g_shots[g_shot_count++] = (u64)n;
        p = end;
        if (*p == ',') {
            p++;
        } else if (*p != '\0') {
            return -1;
        }
    }
    return g_shot_count > 0 ? 0 : -1;
}

#if EIDOS_BOOT

#include "armdata.h"
#include "arm7/arm7.h"
#include "arm7/snd.h"
#include "audio.h"
#include "prof.h"
#include "plat_thread.h"
#include "dsmem.h"
#include "g2d.h"
#include "g2d_gpu.h"
#include "hw3d.h"
#include "input.h"
#include "os.h"
#include "overlay.h"

#if EIDOS_BOOT_SDL
#include "font.h"
#include "platform.h"
#include "settings.h"
#include "setup.h"
#include "ui.h"
#include "ui_lang.h"
#include "langmap.h"
#include "kit.h"
#include "wizard.h"
#endif
#include "mp.h"
#include "link.h"
#include "arm7/wm.h"
#include "save_slots.h"

#ifndef EIDOS_OVERLAYS
#define EIDOS_OVERLAYS 0
#endif
#if EIDOS_OVERLAYS
#include "overlay_image.h"
/* Generated by tools/ovlink.py (cmake/overlays.cmake, EIDOS_OVERLAYS): the
 * overlays compiled into this executable (docs/overlays.md). */
extern const BwOvlImageSet bw_ovl_set_boot;
#endif

/* aletheia's src/matched/arm9/func_02005124.c (or stubs/reimpl_02005124.c). */
void NitroMain(void);

/*
 * Generated by tools/bootlink.py (cmake/boot.cmake): the link-time
 * constants that are DS addresses.  After every bw_port_reset.
 */
void bw_boot_link_init(void);

/* Generated by tools/armdata.py (cmake/armdata.cmake): the ARM9 image's
 * data for everything this build compiles (docs/armdata.md). */
extern const BwDataImage bw_arm9_data;

/*
 * A call to a DS code address (sdk/plat_crash.h, bw_plat_crash_code_resolver):
 * the host function the build compiled for it.  The ARM9 image's function
 * table first (tools/armdata.py's k_funcs: every function the build defines,
 * the veneers and the adjustor thunks, by their DS value), then the overlay
 * resident over the address.
 */
static void *boot_resolve_code(unsigned int ds_addr, const char **name)
{
    const BwDataImage *img = bw_data_installed();
    u32 i;

    if (img != NULL) {
        for (i = 0; i < img->func_count; i++) {
            if (img->funcs[i].addr == ds_addr) {
                *name = img->funcs[i].name;
                return (void *)img->funcs[i].fn;
            }
        }
    }
#if EIDOS_OVERLAYS
    {
        BwDataFn fn = bw_ovl_code_entry(ds_addr);
        if (fn != NULL) {
            *name = "overlay code";
            return (void *)fn;
        }
    }
#endif
    return NULL;
}

#define REG_DISPSTAT 0x04000004u

#define DISPSTAT_VBLANK_FLAG 0x0001u
/* VCOUNT, the line the display is on.  The port has no line timing, so it
 * holds the line the game's code would see: 192, the first line of vblank,
 * from vblank until the next frame starts, and 0 from then.  The game's
 * frame work runs after the vblank wait, while the console is still in
 * vblank, and some of it checks for that: MCSS_TransTextureTask (the
 * intro's professor and Pokemon, battle sprites) uploads its texture only
 * when VCOUNT is between 192 and 200, and with VCOUNT stuck at 0 the sprites
 * never became drawable. */
#define REG_VCOUNT 0x04000006u
#define VCOUNT_VBLANK_LINE 192u
#define DISPSTAT_VBLANK_IRQ  0x0008u

/* One DS frame is 560190 cycles of the 33513982 Hz system clock. */
#define DS_FRAME_CLOCKS 560190u
#define DS_CLOCK_HZ     33513982u

typedef struct BootRun {
    EidosFrameLoop loop;
    EidosFrameHooks hooks;    /* the frame loop's hooks, kept for a restart */
    u64 limit;         /* frames to run before the clean exit            */
    u64 vblanks;       /* vblank interrupts raised                       */
    u32 skipped;       /* presents skipped in a row (badly behind)       */
    u64 ff_shown_ns;   /* fast forward: when a frame was last shown      */
    int ff_skip;       /* fast forward: this frame is neither drawn nor shown */
    u32 ff_since;      /* fast forward: frames since one was shown        */
    u64 audio_rem;     /* the output-frame remainder, in clock units     */
    u8 *top;
    u8 *bottom;
#if EIDOS_BOOT_SDL
    u8 *hi_top;        /* the window's frames, 256n x 192n               */
    u8 *hi_bottom;
    u32 hi_n;          /* the n those buffers are sized for              */
    u8 *behind;        /* the 3D screen with its 3D layer off, 256x192   */
    u8 *ui_px[2];      /* the screens at 1x for the interface layer (ONE) */
    u8 *ui_objs[2];    /* their OBJ layers alone, when a recipe asks      */
    int gpu3d_ok;      /* the last frame could use the GPU rasteriser    */
    u8 *frame1x;       /* the GPU's 3D frame at DS size (read back)      */
    int frame1x_valid; /* the GPU drew this frame's 3D (frame1x or sentinel) */
    u8 *sentinel;      /* a 3D layer of one unlikely colour, alpha 255   */
    u8 *black3d;       /* a 3D layer of opaque black                      */
    u8 *zero;          /* the 3D screen composed with black3d, 256x192    */
    int gpu_real;      /* this frame's 3D layer is the real read-back frame */
    int gpu2d;         /* the GPU composes the 2D screens (boot_gpu2d_on) */
    int gpu2d3d;       /* phase 3: the 2D shader composes the main screen's
                          3D layer from the GPU's frame (docs/gpu-2d.md)   */
    u8 *cap_a;         /* capture source A with the sentinel 3D layer, 256x192 */
    u8 *cap_behind;    /* the same with the 3D layer off                  */
    u8 *cap_b;         /* the captured bank as RGBA, to seed the GPU's copy */
    int cap_ok;        /* the GPU holds the capture of bank cap_bank      */
    int cap_bank;
    u32 cap_hash;      /* the bank's content after the capture the GPU mirrors */
    u32 hi_sn[2];      /* the n each screen was written at (top, bottom) */
    EidosPlatform *plat;
    int want_window;   /* --window: open one at the first frame          */
    int window_tried;
#endif
} BootRun;

static BootRun g_run;

/*
 * A PNG of both screens, top above bottom (256 x 384, RGB), written with
 * stored (uncompressed) deflate blocks: no compression library, and any
 * viewer opens it.  One zlib block per scanline keeps each under 64 KiB.
 */
static u32 png_crc_table[256];

static u32 png_crc(u32 crc, const u8 *p, size_t n)
{
    size_t i;

    if (png_crc_table[1] == 0u) {
        u32 c, k;
        for (i = 0; i < 256u; i++) {
            c = (u32)i;
            for (k = 0; k < 8u; k++) {
                c = (c & 1u) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            }
            png_crc_table[i] = c;
        }
    }
    crc = ~crc;
    for (i = 0; i < n; i++) {
        crc = png_crc_table[(crc ^ p[i]) & 0xFFu] ^ (crc >> 8);
    }
    return ~crc;
}

static void png_be32(u8 *p, u32 v)
{
    p[0] = (u8)(v >> 24);
    p[1] = (u8)(v >> 16);
    p[2] = (u8)(v >> 8);
    p[3] = (u8)v;
}

static int png_chunk(FILE *fp, const char *type, const u8 *data, u32 n)
{
    u8 head[8];
    u8 tail[4];
    u32 crc;

    png_be32(head, n);
    memcpy(head + 4, type, 4);
    crc = png_crc(0, head + 4, 4);
    crc = png_crc(crc, data, n);
    png_be32(tail, crc);
    return fwrite(head, 1, 8, fp) == 8 && (n == 0 || fwrite(data, 1, n, fp) == n)
        && fwrite(tail, 1, 4, fp) == 4;
}

static int write_screens_png(const char *path, const u8 *top, const u8 *bottom)
{
    enum { W = 256, H = 384, ROW = 1 + W * 3 };
    static const u8 sig[8] = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };
    u8 ihdr[13];
    u8 *z;
    size_t zlen = 2 + (size_t)H * (5 + ROW) + 4;
    size_t at = 0;
    u32 a = 1, b = 0;
    int y, x, ok;
    FILE *fp;

    z = (u8 *)malloc(zlen);
    if (z == NULL) {
        return -1;
    }
    z[at++] = 0x78;
    z[at++] = 0x01;
    for (y = 0; y < H; y++) {
        const u8 *src = (y < 192 ? top : bottom) + (size_t)(y % 192) * W * 4;
        u8 *row;
        int i;

        z[at++] = (u8)(y == H - 1 ? 1 : 0);
        z[at++] = (u8)(ROW & 0xFF);
        z[at++] = (u8)(ROW >> 8);
        z[at++] = (u8)(~ROW & 0xFF);
        z[at++] = (u8)((~ROW >> 8) & 0xFF);
        row = z + at;
        row[0] = 0;                 /* filter: none */
        for (x = 0; x < W; x++) {
            row[1 + x * 3 + 0] = src[x * 4 + 0];
            row[1 + x * 3 + 1] = src[x * 4 + 1];
            row[1 + x * 3 + 2] = src[x * 4 + 2];
        }
        for (i = 0; i < ROW; i++) {
            a = (a + row[i]) % 65521u;
            b = (b + a) % 65521u;
        }
        at += ROW;
    }
    png_be32(z + at, (b << 16) | a);
    at += 4;

    png_be32(ihdr, W);
    png_be32(ihdr + 4, H);
    ihdr[8] = 8;                    /* bit depth */
    ihdr[9] = 2;                    /* RGB */
    ihdr[10] = ihdr[11] = ihdr[12] = 0;

    fp = fopen(path, "wb");
    if (fp == NULL) {
        free(z);
        return -1;
    }
    ok = fwrite(sig, 1, 8, fp) == 8 && png_chunk(fp, "IHDR", ihdr, 13)
      && png_chunk(fp, "IDAT", z, (u32)at) && png_chunk(fp, "IEND", NULL, 0);
    ok = (fclose(fp) == 0) && ok;
    free(z);
    return ok ? 0 : -1;
}

static void boot_ensure_screens(BootRun *run)
{
    size_t screen = (size_t)256 * 192 * 4;

    if (run->top == NULL) {
        run->top = (u8 *)calloc(1, screen);
        run->bottom = (u8 *)calloc(1, screen);
        if (run->top == NULL || run->bottom == NULL) {
            bw_panic("eidos_boot", 0, "out of memory for the screens");
        }
    }
}

/*
 * The display capture runs with the frame it is enabled in, and the title
 * screen feeds each capture back into the next (a blend of the 3D frame
 * with the bank the main engine shows).  A headless run renders only the
 * frames it saves, so without this step the bank holds whatever the last
 * saved frame left in it.  With a window the frame is rendered every
 * frame anyway.
 */
static void boot_capture_step(BootRun *run)
{
    u32 unsupported[2];

#if EIDOS_BOOT_SDL
    if (run->plat != NULL) {
        return;
    }
#endif
    if (!(bw_io_read32(G2D_HW_DISPCAPCNT) & 0x80000000u)) {
        return;
    }
    boot_ensure_screens(run);
    (void)g2d_hw_render_dsmem(g3d_hw_frame(), run->top, run->bottom, unsupported);
}

#if EIDOS_BOOT_SDL
/* The GPU's 3D frame at DS size, read back now, or NULL when the CPU drew it. */
static const u8 *boot_gpu_frame(BootRun *run)
{
    const G3dSnapshot *snap;

    if (!run->frame1x_valid || run->plat == NULL || run->frame1x == NULL) {
        return NULL;
    }
    snap = g3d_hw_snapshot();
    if (snap == NULL
        || eidos_platform_read3d_1x(run->plat, snap, run->frame1x, 0) < 0) {
        return NULL;
    }
    return run->frame1x;
}
#else
static const u8 *boot_gpu_frame(BootRun *run)
{
    (void)run;
    return NULL;
}
#endif

/*
 * EIDOS_G3D_DUMP=dir: at each screenshot, write what the 3D engine was given
 * for that frame: a text summary per polygon (attributes, texture, vertex
 * colours) and each decoded texture as a PPM (alpha in a second file).  A
 * debugging aid for 3D accuracy work.
 */
/* EIDOS_IO_DUMP=1: the display registers at each screenshot. */
static void boot_dump_io(u64 n)
{
    u32 base, e;

    if (getenv("EIDOS_IO_DUMP") == NULL) {
        return;
    }
    for (e = 0; e < 2u; e++) {
        base = e ? 0x04001000u : 0x04000000u;
        fprintf(stderr, "io %llu %s: DISPCNT %08X BG0CNT %04X BG1CNT %04X BG2CNT %04X BG3CNT %04X"
                " BG0OFS %04X/%04X BG3OFS %04X/%04X BLDCNT %04X MASTER_BRIGHT %04X\n",
                (unsigned long long)n, e ? "sub " : "main", bw_io_read32(base),
                bw_io_read16(base + 8), bw_io_read16(base + 10), bw_io_read16(base + 12),
                bw_io_read16(base + 14), bw_io_read16(base + 0x10), bw_io_read16(base + 0x12),
                bw_io_read16(base + 0x1C), bw_io_read16(base + 0x1E), bw_io_read16(base + 0x50),
                bw_io_read16(base + 0x6C));
    }
    {
        /* EIDOS_PEEK=addr:words prints DS memory beside the registers. */
        const char *pk = getenv("EIDOS_PEEK");
        unsigned addr = 0, words = 0, k;

        if (pk != NULL && sscanf(pk, "%x:%u", &addr, &words) == 2) {
            for (k = 0; k < words; k++) {
                const u32 *w = (const u32 *)bw_ds_ptr(addr + k * 4u);

                fprintf(stderr, "peek %08X: %08X\n", addr + k * 4u, w != NULL ? *w : 0u);
            }
        }
    }
    fprintf(stderr, "io %llu DISPCAPCNT %08X\n", (unsigned long long)n, bw_io_read32(0x04000064u));
    fprintf(stderr, "io %llu POWCNT1 %04X VRAMCNT A-D %02X %02X %02X %02X\n", (unsigned long long)n,
            bw_io_read16(0x04000304u), bw_io_read16(0x04000240u) & 0xFFu,
            (bw_io_read16(0x04000240u) >> 8) & 0xFFu, bw_io_read16(0x04000242u) & 0xFFu,
            (bw_io_read16(0x04000242u) >> 8) & 0xFFu);
}

static void boot_dump_g3d(u64 n)
{
    const char *dir = getenv("EIDOS_G3D_DUMP");
    const G3dSnapshot *s = g3d_hw_snapshot();
    char path[1024];
    FILE *f;
    u32 i;

    if (dir == NULL || s == NULL) {
        return;
    }
    snprintf(path, sizeof(path), "%s/g3d_%llu.txt", dir, (unsigned long long)n);
    f = fopen(path, "w");
    if (f != NULL) {
        fprintf(f, "polys %u verts %u tex %u disp3dcnt %08X wbuf %u alpha_ref %u "
                   "highlight %u clear %08X unsupported %u\n",
                s->n_polys, s->n_verts, s->n_tex, s->disp3dcnt, s->wbuffer,
                s->alpha_ref, s->highlight, s->clear_color, s->unsupported);
        for (i = 0; i < s->n_polys; i++) {
            const G3dPoly *p = &s->polys[i];
            u32 k;

            fprintf(f, "poly %u n=%u attr=%08X (mode %u alpha %u lights %X id %u) "
                       "teximage=%08X (fmt %u) tex=%d  colours:",
                    i, p->count, p->attr, G3D_ATTR_MODE(p->attr),
                    G3D_ATTR_ALPHA(p->attr), G3D_ATTR_LIGHTS(p->attr),
                    G3D_ATTR_ID(p->attr), p->teximage, G3D_TEX_FORMAT(p->teximage),
                    s->poly_tex[i]);
            for (k = 0; k < p->count; k++) {
                const G3dVertex *v = &s->verts[s->idx[p->first + k]];

                fprintf(f, " (%u,%u,%u|%d,%d,%d,%d|%d,%d)", v->r, v->g, v->b, v->x, v->y, v->z, v->w, v->s, v->t);
            }
            fprintf(f, "\n");
        }
        fclose(f);
    }
    {
        /* the 3D layer as the CPU rasteriser drew it, before the 2D engines */
        const u8 *raw = g3d_hw_frame();
        u32 n3 = g3d_hw_frame_scale(), px3 = 256u * n3 * 192u * n3, q;

        snprintf(path, sizeof(path), "%s/g3d_%llu_layer.ppm", dir, (unsigned long long)n);
        f = raw != NULL ? fopen(path, "wb") : NULL;
        if (f != NULL) {
            fprintf(f, "P6 %u %u 255 ", 256u * n3, 192u * n3);
            for (q = 0; q < px3; q++) {
                fwrite(raw + q * 4u, 1, 3, f);
            }
            fclose(f);
        }
    }
    for (i = 0; i < s->n_tex; i++) {
        u32 k, npx = s->tex[i].w * s->tex[i].h;

        snprintf(path, sizeof(path), "%s/g3d_%llu_tex%u.ppm", dir,
                 (unsigned long long)n, i);
        f = fopen(path, "wb");
        if (f == NULL) {
            continue;
        }
        fprintf(f, "P6 %u %u 255 ", s->tex[i].w, s->tex[i].h);
        for (k = 0; k < npx; k++) {
            fwrite(s->tex[i].rgba + k * 4u, 1, 3, f);
        }
        fclose(f);
        snprintf(path, sizeof(path), "%s/g3d_%llu_tex%u_alpha.pgm", dir,
                 (unsigned long long)n, i);
        f = fopen(path, "wb");
        if (f == NULL) {
            continue;
        }
        fprintf(f, "P5 %u %u 255 ", s->tex[i].w, s->tex[i].h);
        for (k = 0; k < npx; k++) {
            fputc(s->tex[i].rgba[k * 4u + 3u], f);
        }
        fclose(f);
    }
}

/* Both screens as the hardware state has them now, saved for frame `n`. */
static void boot_screenshot(BootRun *run, u64 n)
{
    u32 unsupported[2];
    char path[1024];

    boot_ensure_screens(run);
    boot_dump_g3d(n);
    boot_dump_io(n);
    {
        const u8 *gpu3 = boot_gpu_frame(run);

        if (gpu3 != NULL) {
            /* The GPU drew the 3D; its DS-size frame is the layer. */
            (void)g2d_hw_render_dsmem(gpu3, run->top, run->bottom, unsupported);
        } else if (g3d_hw_frame_scale() > 1u) {
            /* A window runs the 3D at n times the DS size: render at n, then
             * box-downsample to the 256x192 the PNG writer takes. */
            u32 n = g3d_hw_frame_scale();
            size_t bytes = (size_t)256 * n * 192 * n * 4;
            u8 *a = (u8 *)calloc(2, bytes);

            if (a == NULL) {
                bw_panic("eidos_boot", 0, "out of memory for the screenshot");
            }
            (void)g2d_hw_render_dsmem_scaled(n, g3d_hw_frame(), a, a + bytes,
                                             unsupported);
            g2d_hw_box_downsample(a, n, run->top);
            g2d_hw_box_downsample(a + bytes, n, run->bottom);
            free(a);
        } else {
            (void)g2d_hw_render_dsmem(g3d_hw_frame(), run->top, run->bottom,
                                      unsupported);
        }
    }
    snprintf(path, sizeof(path), "%s/eidos_boot_frame%llu.png", g_shot_dir,
             (unsigned long long)n);
    if (write_screens_png(path, run->top, run->bottom) == 0) {
        printf("eidos_boot: frame %llu saved to %s\n", (unsigned long long)n, path);
    } else {
        fprintf(stderr, "eidos_boot: cannot write %s\n", path);
    }
#if EIDOS_BOOT_SDL
    /* With a window, also the picture as the window shows it, at its own
     * resolution (the GPU's 3D at the window's scale), as a BMP. */
    if (run->plat != NULL) {
        snprintf(path, sizeof(path), "%s/eidos_boot_frame%llu_window.bmp",
                 g_shot_dir, (unsigned long long)n);
        if (eidos_platform_screenshot(run->plat, path) == 0) {
            printf("eidos_boot: frame %llu window saved to %s\n",
                   (unsigned long long)n, path);
        }
    }
#endif
    fflush(stdout);
}

/* Frames passed, for the watchdog's thread (a 32-bit word: no tearing). */
static volatile u32 g_frames_seen;

static unsigned long long boot_progress(void)
{
    return g_frames_seen;
}

/*
 * The register file is plain memory (sdk/dsmem.h): the hardware's own
 * updates to DISPSTAT are made on its bytes directly, so no write hook runs
 * for them.  IF is never written here: the OS layer owns it.
 */
static u32 io_raw_read(u32 addr, u32 width)
{
    u32 v = 0;
    memcpy(&v, (const void *)bw_io(addr), width);
    return v;
}

static void io_raw_write(u32 addr, u32 width, u32 v)
{
    memcpy((void *)bw_io(addr), &v, width);
}

/* Which overlays are resident: where the run stands, in the game's terms. */
static void boot_print_resident(void)
{
    const BwOverlayTable *t = bw_overlay_table();
    u32 i;

    printf("eidos_boot: resident overlays:");
    for (i = 0; i < t->overlay_count; i++) {
        if (bw_overlay_is_loaded(t->overlays[i].id)) {
            printf(" %lu", (unsigned long)t->overlays[i].id);
        }
    }
    printf("\n");
}

/* A crash, panic or watchdog report's context (sdk/plat_crash.h): how far
 * the run got, in the form tests/boot_run.cmake reads. */
static void boot_report_context(void)
{
    fflush(stdout);
    fprintf(stderr, "eidos_boot: stopped at frame %llu\n",
            (unsigned long long)eidos_frame_count(&g_run.loop));
    boot_print_resident();
    fflush(stdout);
}

static void boot_finish(int status, const char *why)
{
    printf("eidos_boot: %s after %llu frames, %llu vblank interrupts\n", why,
           (unsigned long long)eidos_frame_count(&g_run.loop),
           (unsigned long long)g_run.vblanks);
    boot_print_resident();
#if EIDOS_BOOT
    eidos_bench_finish();
#endif
    fflush(stdout);
#if EIDOS_BOOT_SDL
    if (g_run.plat != NULL) {
        eidos_platform_quit(g_run.plat);
        g_run.plat = NULL;
    }
#endif
    /* The game's threads are host threads blocked in the OS layer; the
     * process ends with them, as the console's power switch would. */
    exit(status);
}

/* The hardware's half of vblank: DISPSTAT's flag, and the request when
 * DISPSTAT asks for it.  The OS layer takes it when the halt returns. */
static void boot_vblank(void *user)
{
    BootRun *run = (BootRun *)user;
    u32 dispstat = io_raw_read(REG_DISPSTAT, 2);

    io_raw_write(REG_DISPSTAT, 2, dispstat | DISPSTAT_VBLANK_FLAG);
    io_raw_write(REG_VCOUNT, 2, VCOUNT_VBLANK_LINE);
    bw_io_sync();
    if (dispstat & DISPSTAT_VBLANK_IRQ) {
        bw_os_raise_irq(OS_IE_V_BLANK);
        run->vblanks++;
    }
}

static u64 g_render_ns;   /* this frame's 3D + 2D time, for the profiler */

#if EIDOS_BOOT_SDL
/*
 * The GPU 3D layer for this frame, when the main engine shows one and the
 * snapshot is something the GPU path can draw.  The screen composed with the
 * 3D layer off goes to the presenter beside the one the CPU composed with its
 * own 1x 3D; the presenter shows the GPU's frame where the two differ.
 */
/* The one-screen world beyond the DS screen, where no 3D is: the main
 * engine's backdrop (BG palette entry 0) under its master brightness, as
 * the DS screen shows it (bg.c's 6-bit stage). */
static void boot_world_backdrop(BootRun *run, u32 bright)
{
    const u8 *pal = (const u8 *)bw_ds_ptr(0x05000000u);
    u16 c = pal != NULL ? bw_rd16(pal) : 0;
    u32 mode = (bright >> 14) & 3u, f = bright & 0x1Fu, rgb = 0, i;

    f = f > 16u ? 16u : f;
    for (i = 0; i < 3u; i++) {
        u32 c5 = ((u32)c >> (5u * i)) & 31u, c6 = c5 != 0u ? c5 * 2u + 1u : 0u;

        if (mode == 1u && f != 0u) {
            c6 += ((63u - c6) * f) >> 4;
        } else if (mode == 2u && f != 0u) {
            c6 -= (c6 * f) >> 4;
        }
        rgb |= (c6 * 255u / 63u) << (16u - 8u * i);
    }
    eidos_platform_set_world_backdrop(run->plat, rgb);
}

static void boot_gpu3d_do(BootRun *run, int behind_done, const char **why);

/* The 3D's path for the next frame (the GPU, or the software rasteriser
 * when the GPU cannot take this one).  A change of path is logged with its
 * reason: the software rasteriser costs about 10 ms a frame more. */
static void boot_gpu3d(BootRun *run, int behind_done)
{
    static int last = -1;
    static const char *last_why;
    const char *why = "";

    boot_gpu3d_do(run, behind_done, &why);
    if (run->plat != NULL && eidos_platform_gpu3d(run->plat) && (run->gpu3d_ok != last || (!run->gpu3d_ok && why != last_why))) {
        const G3dSnapshot *snap = g3d_hw_snapshot();

        fprintf(stderr, "eidos: 3D %s from frame %llu%s%s (snapshot flags 0x%X)\n",
                run->gpu3d_ok ? "on the GPU" : "on the software rasteriser",
                (unsigned long long)eidos_frame_count(&run->loop), why[0] != '\0' ? ": " : "", why,
                snap != NULL ? (unsigned)snap->unsupported : 0xFFFFFFFFu);
        last = run->gpu3d_ok;
        last_why = why;
    }
}

static void boot_gpu3d_do(BootRun *run, int behind_done, const char **why)
{
    const G3dSnapshot *snap;
    u32 dispcnt, bright;
    int main_on_top, screen;
    u32 bmode;

    run->gpu3d_ok = 0;
    if (run->plat == NULL || !eidos_platform_gpu3d(run->plat)) {
        return;
    }
    snap = g3d_hw_snapshot();
    dispcnt = bw_io_read32(0x04000000u);
    if (snap == NULL || snap->unsupported != 0u || (dispcnt & 0x8u) == 0u
        || (dispcnt & 0x100u) == 0u || ((dispcnt >> 16) & 3u) != 1u) {
        (void)eidos_platform_set_3d(run->plat, -1, NULL, NULL, 1, 0, 0.f, 0);
        run->gpu3d_ok = snap != NULL && snap->unsupported == 0u;
        *why = snap == NULL ? "no snapshot" : snap->unsupported != 0u ? "a feature the GPU path lacks" : "";
        return;
    }
    if (run->gpu2d3d) {
        /* phase 3: the 2D shader composes the 3D from the GPU's frame */
        const G2dGpuFrame *mf = g2d_gpu_frame(0);

        if (mf == NULL || !mf->valid) {
            *why = "no 2D frame for the shader";
            return;
        }
        main_on_top = (bw_io_read16(0x04000304u) & 0x8000u) != 0;
        if (!(eidos_ui_enabled() && eidos_platform_world_active(run->plat))) {
            if (eidos_platform_set_3d2d(run->plat, main_on_top ? 0 : 1, snap,
                                        eidos_settings_internal_scale()) == 0) {
                run->gpu3d_ok = 1;
            } else {
                *why = "the GPU did not draw the frame (set_3d2d)";
            }
            return;
        }
        /* the world view: its own composite, with the screens it takes
         * made by the shader (below) */
    } else if (!behind_done || run->behind == NULL) {
        *why = "the compositor did not make the screen with the 3D off";
        return;     /* the compositor did not make the 3D-off screen */
    }
    main_on_top = (bw_io_read16(0x04000304u) & 0x8000u) != 0;
    screen = main_on_top ? 0 : 1;
    bright = bw_io_read16(0x0400006Cu);
    boot_world_backdrop(run, bright);
    bmode = (bright >> 14) & 3u;
    {
        u32 f = bright & 0x1Fu;

        if (f > 16u) {
            f = 16u;
        }
        if (bmode == 0u || f == 0u) {
            /* A fade the game makes with the 2D brightness effect (BLDCNT
             * brighten or darken, BLDY) on the 3D layer, not the master
             * brightness: the GPU's 3D takes it the same way, so the 3D
             * fades with the 2D (in the one-screen world, all of it). */
            u32 bld = bw_io_read16(0x04000050u), fx = (bld >> 6) & 3u;

            if ((fx == 2u || fx == 3u) && (bld & 1u) != 0u) {
                bmode = fx == 2u ? 1u : 2u;
                f = bw_io_read16(0x04000054u) & 0x1Fu;
                if (f > 16u) {
                    f = 16u;
                }
            }
        }
        /* What 2D the game blends over the 3D (the battle's message box):
         * the screen with an opaque black 3D layer, beside the one with the
         * sentinel.  Only when the screen was composed with the sentinel,
         * and only when a 2D pixel can blend over the 3D at all: BG0 (the 3D
         * layer) is one of BLDCNT's second targets.  Otherwise it is a
         * second compose of the main screen every frame for nothing. */
        const u8 *zero = NULL;

        if (run->gpu2d3d) {
            if (eidos_platform_set_3d_from2d(run->plat, screen, snap, eidos_settings_internal_scale(),
                                             bmode == 1u || bmode == 2u ? (int)bmode : 0,
                                             (float)f / 16.f) == 0) {
                run->gpu3d_ok = 1;
            } else {
                *why = "the GPU did not draw the frame (set_3d_from2d)";
            }
            return;
        }
        if (!run->gpu_real && (bw_io_read16(0x04000050u) & 0x0100u) != 0u
            && g2d_hw_main_blended_over_3d()) {
            /* only when the frame's compose blended 2D over the 3D at all:
             * otherwise the second compose finds nothing (the field) */
            if (run->black3d == NULL) {
                u32 k;

                run->black3d = (u8 *)calloc(1, (size_t)256 * 192 * 4);
                for (k = 0; run->black3d != NULL && k < 256u * 192u; k++) {
                    run->black3d[k * 4u + 3] = 255;
                }
            }
            if (run->zero == NULL) {
                run->zero = (u8 *)calloc(1, (size_t)256 * 192 * 4);
            }
            if (g2d_hw_main_zero_done()) {
                zero = run->zero;       /* made by the frame's own compose */
            } else if (run->black3d != NULL && run->zero != NULL
                       && g2d_hw_main_graphics_dsmem(run->black3d, run->zero) == 0) {
                zero = run->zero;
            }
        }
        eidos_platform_set_3d_zero(run->plat, zero);
        if (eidos_platform_set_3d(run->plat, screen, run->behind, snap,
                                  eidos_settings_internal_scale(),
                                  bmode == 1u || bmode == 2u ? (int)bmode : 0,
                                  (float)f / 16.f, run->gpu_real) == 0) {
            run->gpu3d_ok = 1;
        } else {
            *why = "the GPU did not draw the frame (set_3d)";
        }
    }
}

/* The 256 x 192 BGR555 picture at the start of a VRAM bank, hashed. */
static u32 boot_bank_hash(const u8 *bank)
{
    const u32 *w = (const u32 *)(const void *)bank;
    u32 h = 2166136261u, i;

    for (i = 0; i < 256u * 192u / 2u; i++) {
        h = (h ^ w[i]) * 16777619u;
    }
    return h;
}

static u8 *boot_sentinel(BootRun *run)
{
    if (run->sentinel == NULL) {
        u32 k;

        run->sentinel = (u8 *)malloc((size_t)256 * 192 * 4);
        for (k = 0; run->sentinel != NULL && k < 256u * 192u; k++) {
            run->sentinel[k * 4u + 0] = 84;
            run->sentinel[k * 4u + 1] = 168;
            run->sentinel[k * 4u + 2] = 52;
            run->sentinel[k * 4u + 3] = 255;
        }
    }
    return run->sentinel;
}

/*
 * The display capture at the 3D's resolution (the GPU path).  A scene that
 * captures the main screen into a VRAM bank every frame and shows that bank
 * (the title: a feedback blend with the frame before) is DS size by nature.
 * The DS-size capture still runs for the game's memory; when the fields are
 * the plain ones (the whole screen, the graphics as source A, the shown bank
 * as source B) the presenter keeps the same capture at the chosen scale and
 * shows that instead.  boot_capture_plan runs before the frame's capture
 * (it needs the bank as it was), boot_capture_run after it.
 */
typedef struct BootCapture {
    int plan, blend, bank, eva, evb;
    const u8 *bankp;
    const u8 *b1x;
} BootCapture;

static void boot_capture_plan(BootRun *run, BootCapture *c)
{
    u32 cap = bw_io_read32(G2D_HW_DISPCAPCNT);
    u32 dc = bw_io_read32(0x04000000u);
    u32 mode = (cap >> 29) & 3u;
    const G3dSnapshot *snap;

    memset(c, 0, sizeof(*c));
    c->bank = (int)((dc >> 18) & 3u);
    c->bankp = (const u8 *)bw_ds_try_ptr(0x06800000u + (u32)c->bank * 0x20000u);
    if (run->plat == NULL || !eidos_platform_gpu3d(run->plat)
        || eidos_settings_internal_scale() < 2 || c->bankp == NULL) {
        return;
    }
    snap = g3d_hw_snapshot();
    if (snap == NULL || snap->unsupported != 0u
        || (cap & 0x80000000u) == 0u          /* no capture this frame */
        || ((cap >> 20) & 3u) != 3u           /* not the whole screen */
        || ((cap >> 18) & 3u) != 0u           /* a write offset */
        || (cap & 0x01000000u) != 0u          /* source A is the 3D alone */
        || (dc & 0x8u) == 0u || (dc & 0x100u) == 0u
        || ((dc >> 16) & 3u) != 2u            /* the screen does not show a bank */
        || (u32)c->bank != ((cap >> 16) & 3u)) {
        return;
    }
    if (mode == 1u || (mode >= 2u && ((cap & 0x02000000u) != 0u
                                      || ((cap >> 26) & 3u) != 0u))) {
        return;
    }
    if (run->cap_a == NULL) {
        run->cap_a = (u8 *)malloc((size_t)256 * 192 * 4);
        run->cap_behind = (u8 *)malloc((size_t)256 * 192 * 4);
        run->cap_b = (u8 *)malloc((size_t)256 * 192 * 4);
    }
    if (run->cap_a == NULL || run->cap_behind == NULL || run->cap_b == NULL
        || boot_sentinel(run) == NULL) {
        return;
    }
    c->blend = mode != 0u;
    c->eva = (int)((cap & 0x1Fu) > 16u ? 16u : (cap & 0x1Fu));
    c->evb = (int)(((cap >> 8) & 0x1Fu) > 16u ? 16u : ((cap >> 8) & 0x1Fu));
    if (c->blend && !(run->cap_ok && run->cap_bank == c->bank
                      && boot_bank_hash(c->bankp) == run->cap_hash)) {
        /* the GPU has no copy of what the bank holds: give it the bank */
        u32 k;

        for (k = 0; k < 256u * 192u; k++) {
            u32 v = (u32)c->bankp[k * 2u] | ((u32)c->bankp[k * 2u + 1u] << 8);

            run->cap_b[k * 4u + 0] = (u8)((v & 31u) * 255u / 31u);
            run->cap_b[k * 4u + 1] = (u8)(((v >> 5) & 31u) * 255u / 31u);
            run->cap_b[k * 4u + 2] = (u8)(((v >> 10) & 31u) * 255u / 31u);
            run->cap_b[k * 4u + 3] = 255;
        }
        c->b1x = run->cap_b;
    }
    if (g2d_hw_main_graphics_pair_dsmem(run->sentinel, run->cap_a,
                                        run->cap_behind) == 0) {
        c->plan = 1;
    }
}

static void boot_capture_run(BootRun *run, const BootCapture *c)
{
    u32 dc = bw_io_read32(0x04000000u);
    u32 bright = bw_io_read16(0x0400006Cu), bmode = (bright >> 14) & 3u;
    u32 f = bright & 0x1Fu;
    int main_on_top = (bw_io_read16(0x04000304u) & 0x8000u) != 0;
    int show = 0;

    if (run->plat == NULL || !eidos_platform_gpu3d(run->plat)) {
        return;
    }
    if (f > 16u) {
        f = 16u;
    }
    if (c->plan && eidos_ui_enabled() && eidos_platform_world_active(run->plat)) {
        /*
         * The one-screen world: the screen that shows the capture (the
         * title's motion trail) is shown as its live 3D instead, drawn at
         * the window's shape like any other 3D screen, so the scene fills
         * the window; the trail is the price.  cap_a is the screen's
         * graphics with the sentinel 3D layer and cap_behind the same with
         * the 3D off: what eidos_platform_set_3d composes with.
         */
        int scr = main_on_top ? 0 : 1;
        u8 *dst = scr == 0 ? run->hi_top : run->hi_bottom;

        if (run->hi_n == 1u && dst != NULL) {
            memcpy(dst, run->cap_a, (size_t)256 * 192 * 4);
        }
        if (eidos_platform_set_3d(run->plat, scr, run->cap_behind,
                                  g3d_hw_snapshot(),
                                  eidos_settings_internal_scale(),
                                  bmode == 1u || bmode == 2u ? (int)bmode : 0,
                                  (float)f / 16.f, 0) == 0) {
            run->gpu3d_ok = 1;
        }
        run->cap_ok = 0;
        eidos_platform_show_capture(run->plat, -1, 0, 0.f);
        return;
    }
    if (c->plan) {
        if (eidos_platform_capture_hires(run->plat, run->cap_a, run->cap_behind,
                                         g3d_hw_snapshot(),
                                         eidos_settings_internal_scale(),
                                         c->blend, c->eva, c->evb, c->b1x) == 0) {
            run->cap_ok = 1;
            run->cap_bank = c->bank;
            run->cap_hash = boot_bank_hash(c->bankp);
            show = 1;
        } else {
            run->cap_ok = 0;
        }
    } else if (run->cap_ok && c->bankp != NULL && ((dc >> 16) & 3u) == 2u
               && c->bank == run->cap_bank) {
        /* no capture this frame: the copy stands while the bank is as it was */
        if (boot_bank_hash(c->bankp) == run->cap_hash) {
            show = 1;
        } else {
            run->cap_ok = 0;
        }
    }
    if (f > 16u) {
        f = 16u;
    }
    eidos_platform_show_capture(run->plat, show ? (main_on_top ? 0 : 1) : -1,
                                bmode == 1u || bmode == 2u ? (int)bmode : 0,
                                (float)f / 16.f);
}

#if EIDOS_BOOT_SDL
/*
 * The one-screen layout, once per frame before the frame is rendered: tell
 * the interface layer when the layout changes, and in layout ONE ask it
 * what to show (eidos_ui_frame), hand the view and the canvas to the
 * presenter, and give the geometry engine the widening for the next frame
 * so the game's own culling sees the wider view.  The widening applies only
 * while the GPU draws this scene's 3D (gpu3d_ok): the CPU rasteriser draws
 * at the DS shape and must not be fed a widened projection.  Outside layout
 * ONE everything is as before: a fallback view, no canvas, scale 1:1.
 */
static void boot_ui_frame(BootRun *run)
{
    static int was_one = -1;
    int one;
    EidosUiFrame in;
    EidosUiView view;
    const EidosUiCanvas *canvas = NULL;
    s32 kx = 4096, ky = 4096;

    if (run->plat == NULL) {
        return;
    }
    one = eidos_platform_layout(run->plat) == EIDOS_LAYOUT_ONE;
    if (one != was_one) {
        was_one = one;
        eidos_ui_set_enabled(one);
        if (!one) {
            eidos_platform_text_mode(run->plat, 0);
            eidos_platform_set_view(run->plat, NULL);
            eidos_platform_set_ui_canvas(run->plat, NULL);
            g3d_hw_set_view_scale(4096, 4096);
        }
    }
    if (!one) {
        return;
    }
    memset(&in, 0, sizeof(in));
    memset(&view, 0, sizeof(view));
    view.zoom = 1.0f;
    {
        /* the interface covers the whole window and is told what the
         * system covers on each side (a phone's notch, its corners, the
         * home indicator), where it keeps nothing to read or touch */
        int sx, sy, sw, sh;

        eidos_platform_window_size(run->plat, &in.win_w, &in.win_h);
        eidos_platform_safe_area(run->plat, &sx, &sy, &sw, &sh);
        if (sw > 0 && sh > 0) {
            in.safe_l = sx;
            in.safe_t = sy;
            in.safe_r = in.win_w - sx - sw;
            in.safe_b = in.win_h - sy - sh;
        }
        (void)eidos_platform_mouse(run->plat, &in.mouse);
    }
    in.buttons = eidos_platform_buttons(run->plat);
    in.frame = bw_input_frame_number();
    /* The two screens as the game composed them last frame, at 1x (a
     * GPU-drawn 3D area holds the compositor's placeholder colour). */
    if (run->hi_n != 0u && run->hi_top != NULL && run->hi_bottom != NULL) {
        const u8 *src[2];
        int i;

        src[0] = run->hi_top;
        src[1] = run->hi_bottom;
        for (i = 0; i < 2; i++) {
            if (run->hi_sn[i] == 1u) {
                in.screen_rgba[i] = src[i];
                continue;
            }
            if (run->ui_px[i] == NULL) {
                run->ui_px[i] = (u8 *)calloc(1, (size_t)256 * 192 * 4);
            }
            if (run->ui_px[i] != NULL) {
                g2d_hw_box_downsample(src[i], run->hi_sn[i], run->ui_px[i]);
                in.screen_rgba[i] = run->ui_px[i];
            }
        }
    }
    {
        /* the OBJ layers a recipe asked for last frame */
        int want = eidos_ui_objs_wanted(), i;

        for (i = 0; i < 2; i++) {
            if ((want & (1 << i)) == 0) {
                continue;
            }
            if (run->ui_objs[i] == NULL) {
                run->ui_objs[i] = (u8 *)calloc(1, (size_t)256 * 192 * 4);
            }
            if (run->ui_objs[i] != NULL && g2d_hw_objs_dsmem(i, run->ui_objs[i]) == 0) {
                in.objs_rgba[i] = run->ui_objs[i];
            }
        }
    }
    {
        /* typing, for a recipe that asked for it last frame */
        static char typed[128];

        if (eidos_platform_take_text(run->plat, typed, (int)sizeof(typed)) > 0) {
            in.text = typed;
        }
    }
    {
        /* the hints name the keys of the device the player used last */
        const char *names[12];

        eidos_platform_key_names(run->plat, names);
        ui_set_key_names(names);
        /* the pad's icons: the player's choice, or the pad's own kind */
        {
            int icons = eidos_settings_get(EIDOS_OPT_PAD_ICONS);

            ui_set_pad_family(icons > 0 ? icons - 1 : eidos_platform_pad_family(run->plat));
        }
    }
    {
        /* EIDOS_UI_TIME: the interface's cost, the mean over 300 frames */
        static u64 spent, worst;
        static u32 n;
        u64 t0 = eidos_platform_now_ns(run->plat), dt;

        ui_lang_set(bw_lang_language());     /* the labels in the game's language */
        eidos_ui_frame(&in, &view, &canvas);
        eidos_platform_text_mode(run->plat, eidos_ui_text_wanted());
        eidos_platform_set_view(run->plat, &view);
        eidos_platform_set_ui_canvas(run->plat, canvas);
        dt = eidos_platform_now_ns(run->plat) - t0;
        if (eidos_prof_on) {
            eidos_prof_end(EIDOS_PROF_UI, eidos_prof_begin() - dt);
        }
        spent += dt;
        worst = dt > worst ? dt : worst;
        if (++n == 300) {
            if (getenv("EIDOS_UI_TIME") != NULL) {
                fprintf(stderr, "eidos: ui time %s mean %.2f ms worst %.2f ms\n",
                        eidos_ui_scene_name(), (double)spent / n / 1e6, (double)worst / 1e6);
            }
            spent = worst = 0;
            n = 0;
        }
    }
    if (run->gpu3d_ok) {
        eidos_platform_view_scale(run->plat, &kx, &ky);
    }
    g3d_hw_set_view_scale(kx, ky);
}
#endif

/* The 2D screens composed by the GPU (docs/gpu-2d.md): the "GPU drawing"
 * setting, on by default; EIDOS_G2D_GPU=0 or 1 overrides it (tests). */
static int boot_gpu2d_on(void)
{
    const char *env = getenv("EIDOS_G2D_GPU");

    if (env != NULL && env[0] != '\0') {
        return env[0] != '0';
    }
    return eidos_settings_get(EIDOS_OPT_GPU_2D) != 0;
}

/*
 * EIDOS_G2D_GPU_CHECK (docs/gpu-2d.md, phase 1): every frame both engines
 * are also composed by the GPU shader and compared with the CPU's screens;
 * a line every 120 frames gives the frames compared and the pixels that
 * differ, and the first different pixel of a frame is named.
 */
static void boot_gpu2d_check(BootRun *run)
{
    static u8 *gpu;
    static unsigned long frames, bad_frames, bad_px, skipped, calls;
    static int logged;
    int slot;

    g2d_gpu_export(1);      /* the next frames' composes fill the frames */
    if (gpu == NULL) {
        gpu = (u8 *)malloc((size_t)256 * 192 * 4);
        if (gpu == NULL) {
            return;
        }
    }
    for (slot = 0; slot < 2; slot++) {
        const G2dGpuFrame *f = g2d_gpu_frame(slot);
        const u8 *cpu;
        unsigned long diff = 0;
        int i, first = -1;

        if (f == NULL || !f->valid || run->hi_sn[f->screen] != 1u
            || eidos_platform_gpu2d(run->plat, f, gpu) != 0) {
            static unsigned long why[4];

            why[f == NULL || !f->valid ? 0 : run->hi_n != 1u ? 1 : run->hi_sn[f->screen] != 1u ? 2 : 3]++;
            if (++skipped % 600u == 0u) {
                fprintf(stderr, "g2d gpu check skipped: invalid %lu scale %lu screen scale %lu gl %lu\n",
                        why[0], why[1], why[2], why[3]);
            }
            continue;
        }
        cpu = f->screen == 0 ? run->hi_top : run->hi_bottom;
        if (cpu == NULL) {
            continue;
        }
        for (i = 0; i < 256 * 192; i++) {
            if (memcmp(cpu + i * 4, gpu + i * 4, 3) != 0) {
                if (first < 0) {
                    first = i;
                }
                diff++;
            }
        }
        frames++;
        if (diff != 0) {
            bad_frames++;
            bad_px += diff;
            if (logged < 20) {
                logged++;
                fprintf(stderr, "g2d gpu: frame %u screen %d: %lu pixels differ, first (%d, %d) cpu %u %u %u gpu %u %u %u\n",
                        (unsigned)bw_input_frame_number(), f->screen, diff, first % 256, first / 256,
                        cpu[first * 4], cpu[first * 4 + 1], cpu[first * 4 + 2],
                        gpu[first * 4], gpu[first * 4 + 1], gpu[first * 4 + 2]);
            }
        }
    }
    if (++calls % 240u == 0u) {
        fprintf(stderr, "g2d gpu check: %lu screens compared, %lu differ (%lu pixels), %lu skipped\n",
                frames, bad_frames, bad_px, skipped);
    }
}

/*
 * The flash watch (#5): a player saw single frames of garbage tiles over
 * the one-screen world every few seconds (0.2.4, OpenGL, a GT 1030) that no
 * run here shows.  Each frame the upper screen's picture as the presenter
 * gets it (run->hi_top, the GPU's own output with the 2D on the GPU) is
 * kept with the 2D state behind it; when a frame differs strongly from the
 * frames on both sides of it while those two agree, the three frames' state
 * goes to <save dir>/eidos_flash.log and the first few events' pictures to
 * eidos_flash_<n>_<a|b|c>.png beside it, so a report carries the cause.
 * About 12k pixel compares a frame.  EIDOS_FLASH_WATCH=0 turns it off.
 */
#define FLASH_PX ((size_t)256 * 192 * 4)

static struct {
    int state;                  /* 0 not looked at, 1 on, 2 off */
    u8 *pic[3];                 /* the last three upper screens, oldest first */
    char info[3][1024];
    u16 pow[3];                 /* POWCNT1: which engine is on top */
    u64 at[3];                  /* the frame each picture is from */
    int have;
    unsigned events;
    u64 last_event;
} g_flash;

static int flash_far(const u8 *a, const u8 *b, int lim)
{
    int i, d = 0;

    for (i = 0; i < 3; i++) {
        int x = (int)a[i] - (int)b[i];

        if (x < 0) {
            x = -x;
        }
        if (x > d) {
            d = x;
        }
    }
    return d > lim;
}

static void flash_info(char *out, size_t size, u64 frame)
{
    int n, slot, i;
    size_t at;

    n = snprintf(out, size, "frame %llu dispcnt %08X/%08X powcnt %04X bgcnt %04X %04X %04X %04X / %04X %04X %04X %04X\n",
                 (unsigned long long)frame, bw_io_read32(0x04000000u), bw_io_read32(0x04001000u),
                 bw_io_read16(0x04000304u), bw_io_read16(0x04000008u), bw_io_read16(0x0400000Au),
                 bw_io_read16(0x0400000Cu), bw_io_read16(0x0400000Eu), bw_io_read16(0x04001008u),
                 bw_io_read16(0x0400100Au), bw_io_read16(0x0400100Cu), bw_io_read16(0x0400100Eu));
    at = n > 0 ? (size_t)n : 0u;
    for (slot = 0; slot < 2 && at < size; slot++) {
        const G2dGpuFrame *f = g2d_gpu_frame(slot);

        if (f == NULL) {
            continue;
        }
        n = snprintf(out + at, size - at, "  slot %d valid %d screen %d gen %u size %u obj %d l3 %d bright %04X skipped %d:",
                     slot, f->valid, f->screen, (unsigned)f->bgv_gen, (unsigned)f->bgv_size, f->obj_on,
                     f->l3_on, (unsigned)f->master_bright, g2d_gpu_cpu_skipped(slot));
        at += n > 0 ? (size_t)n : 0u;
        for (i = 0; i < 4 && at < size; i++) {
            const G2dGpuText *t = &f->text[i];

            if (!t->enabled) {
                continue;
            }
            n = snprintf(out + at, size - at, " bg%d[p%u %ux%u sz%u map %X chr %X %s scr %d,%d]", i,
                         (unsigned)t->priority, (unsigned)t->tiles_wide, (unsigned)t->tiles_high,
                         (unsigned)t->size_code, (unsigned)t->map_ofs, (unsigned)t->char_ofs,
                         t->bpp8 ? "8bpp" : "4bpp", (int)t->hofs, (int)t->vofs);
            at += n > 0 ? (size_t)n : 0u;
        }
        for (i = 0; i < 2 && at < size; i++) {
            const G2dGpuAffine *a = &f->affine[i];

            if (!a->enabled) {
                continue;
            }
            n = snprintf(out + at, size - at, " aff%d[bg%u map %X chr %X]", i, (unsigned)a->bg_index,
                         (unsigned)a->map_ofs, (unsigned)a->char_ofs);
            at += n > 0 ? (size_t)n : 0u;
        }
        if (at < size) {
            n = snprintf(out + at, size - at, "\n");
            at += n > 0 ? (size_t)n : 0u;
        }
    }
}

static void boot_flash_watch(BootRun *run, u64 frame)
{
    u8 *t;
    size_t i;
    int k, hits = 0;

    if (g_flash.state == 0) {
        const char *e = getenv("EIDOS_FLASH_WATCH");

        g_flash.state = e != NULL && e[0] == '0' ? 2 : 1;
    }
    {
        static int told;

        if (told < 2 && getenv("EIDOS_FLASH_TEST") != NULL
            && (told == 0 || frame >= (u64)atol(getenv("EIDOS_FLASH_TEST")))) {
            told++;
            fprintf(stderr, "eidos: flash watch: upper screen at %ux\n", (unsigned)run->hi_sn[0]);
            fprintf(stderr, "eidos: flash watch: state %d, screens at %ux, frame %llu\n", g_flash.state,
                    (unsigned)run->hi_n, (unsigned long long)frame);
        }
    }
    if (g_flash.state != 1 || run->hi_sn[0] != 1u || run->hi_top == NULL) {
        g_flash.have = 0;
        return;
    }
    if (g_flash.pic[0] == NULL) {
        for (k = 0; k < 3; k++) {
            g_flash.pic[k] = (u8 *)malloc(FLASH_PX);
            if (g_flash.pic[k] == NULL) {
                g_flash.state = 2;
                return;
            }
        }
    }
    {
        /* EIDOS_FLASH_TEST=N: frame N's picture gets garbage tiles over its
         * upper half, a stand-in for #5's, to check the watch */
        static long test = -2;

        if (test == -2) {
            const char *e = getenv("EIDOS_FLASH_TEST");

            test = e != NULL ? atol(e) : -1;
        }
        if (test >= 0 && (u64)test <= frame) {
            test = -1;              /* the first composed frame from N (fast forward skips some) */
            for (i = 0; i < FLASH_PX / 2; i += 4) {
                u32 tile = (u32)((i / 4) % 256 / 8 + (i / 4) / 256 / 8 * 32);

                run->hi_top[i] = (u8)(tile * 37u);
                run->hi_top[i + 1] = (u8)(tile * 91u);
                run->hi_top[i + 2] = (u8)(tile * 53u);
            }
        }
    }
    /* the oldest buffer takes this frame */
    t = g_flash.pic[0];
    g_flash.pic[0] = g_flash.pic[1];
    g_flash.pic[1] = g_flash.pic[2];
    g_flash.pic[2] = t;
    memcpy(t, run->hi_top, FLASH_PX);
    memmove(g_flash.info[0], g_flash.info[1], sizeof(g_flash.info[0]) * 2);
    g_flash.pow[0] = g_flash.pow[1];
    g_flash.pow[1] = g_flash.pow[2];
    g_flash.pow[2] = bw_io_read16(0x04000304u);
    g_flash.at[0] = g_flash.at[1];
    g_flash.at[1] = g_flash.at[2];
    g_flash.at[2] = frame;
    flash_info(g_flash.info[2], sizeof(g_flash.info[2]), frame);
    if (g_flash.have < 3) {
        g_flash.have++;
        return;
    }
    /* pic[1] against both neighbours, every 4th pixel */
    for (i = 0; i < FLASH_PX; i += 16) {
        const u8 *a = g_flash.pic[0] + i, *b = g_flash.pic[1] + i, *c = g_flash.pic[2] + i;

        if (flash_far(b, a, 60) && flash_far(b, c, 60) && !flash_far(a, c, 25)) {
            hits++;
        }
    }
    if (getenv("EIDOS_FLASH_TEST") != NULL && frame >= (u64)atol(getenv("EIDOS_FLASH_TEST"))
        && frame < (u64)atol(getenv("EIDOS_FLASH_TEST")) + 4u) {
        fprintf(stderr, "eidos: flash watch: frame %llu hits %d have %d pow %04X %04X %04X\n",
                (unsigned long long)frame, hits, g_flash.have, g_flash.pow[0], g_flash.pow[1], g_flash.pow[2]);
    }
    if (hits < 400 || frame < g_flash.last_event + 4u || g_flash.events >= 50u) {
        return;                     /* under ~3% of the screen, or the same event */
    }
    if (g_flash.at[2] - g_flash.at[0] > 2u) {
        return;                     /* not three frames in a row (fast forward): motion */
    }
    if (((g_flash.pow[0] ^ g_flash.pow[1]) | (g_flash.pow[1] ^ g_flash.pow[2])) & 0x8000u) {
        return;                     /* the game swapped its screens (a scene change) */
    }
    g_flash.last_event = frame;
    g_flash.events++;
    {
        char path[1024];
        FILE *fp;

        if (bw_save_path(path, sizeof path, "eidos_flash.log") == 0 && (fp = fopen(path, "a")) != NULL) {
            fprintf(fp, "flash %u: %d of 12288 sampled pixels, the middle frame of three (with the "
                    "2D on the GPU the pictures arrive a frame late: the middle one's state is the "
                    "first line)\n%s%s%s\n",
                    g_flash.events, hits, g_flash.info[0], g_flash.info[1], g_flash.info[2]);
            fclose(fp);
        }
        fprintf(stderr, "eidos: flash watch: a one-frame change at frame %llu (%d pixels), see eidos_flash.log\n",
                (unsigned long long)(frame - 1u), hits);
        if (g_flash.events <= 6u) {
            static const char tag[3] = { 'a', 'b', 'c' };
            char name[64];

            for (k = 0; k < 3; k++) {
                snprintf(name, sizeof name, "eidos_flash_%u_%c.png", g_flash.events, tag[k]);
                if (bw_save_path(path, sizeof path, name) == 0) {
                    (void)write_screens_png(path, g_flash.pic[k], g_flash.pic[k]);
                }
            }
        }
    }
}

static void boot_render(void *user, u8 *top, u8 *bottom)
{
    u32 unsupported[2];
    u64 t0 = eidos_prof_begin();
    BootRun *run = (BootRun *)user;
    const u8 *rgba3d;
    u32 n;
    int top_fresh;              /* run->hi_top holds a new picture (the flash watch) */

    (void)top;
    (void)bottom;
#if EIDOS_BOOT_SDL
    if (run->ff_skip && eidos_ui_quiet()) {
        /* the style chooser's preview covers the game while it goes to
         * another screen, and this frame is not shown: the interface has
         * nothing to draw for it (it runs with the frames that are shown) */
        return;
    }
    boot_ui_frame(run);
    if (run->ff_skip) {
        return;     /* fast forward: this frame is not shown (boot_frame) */
    }
    if (eidos_ui_quiet() && (bw_io_read32(0x04000064u) & 0x80000000u) == 0u) {
        /* the preview's cut covers the whole window: the game's screens and
         * its 3D are not drawn for it (the frame shown is the cut over the
         * last picture; not while the display capture reads the frame) */
        return;
    }
#endif
    /*
     * The 3D renders at the scale set before its swap, so a change shows one
     * frame late.  With the GPU rasteriser the CPU renders the 3D at 1x only
     * (the 2D layer priorities and the capture need it) and the GPU draws the
     * same polygons at the chosen resolution.  Without it, or for a frame that
     * uses a feature the GPU path lacks, the CPU rasteriser draws the frame at
     * the chosen scale, held at BOOT_CPU_3D_MAX: its cost grows with n
     * squared (about 360 ms a frame at 6x).
     */
    {
        u32 want = (u32)eidos_settings_internal_scale();
        int gpu = run->plat != NULL && eidos_platform_gpu3d(run->plat);
        int skip = gpu && run->gpu3d_ok;

        g3d_hw_snapshot_enable(gpu);
        g3d_hw_skip_raster(skip);
        if (skip) {
            g3d_hw_set_scale(1u);
        } else {
            g3d_hw_set_scale(want > BOOT_CPU_3D_MAX ? BOOT_CPU_3D_MAX : want);
        }
    }
    rgba3d = g3d_hw_frame();
    n = g3d_hw_frame_scale();
    run->frame1x_valid = 0;
    run->gpu_real = 0;
    if (run->plat != NULL && eidos_platform_gpu3d(run->plat)) {
        const G3dSnapshot *snap = g3d_hw_snapshot();

        if (snap != NULL && snap->unsupported == 0u) {
            u32 dispcnt = bw_io_read32(0x04000000u);
            u32 cap = bw_io_read32(G2D_HW_DISPCAPCNT);
            int main3d = (dispcnt & 0x8u) && (dispcnt & 0x100u);
            int graphics = ((dispcnt >> 16) & 3u) == 1u;

            if (run->frame1x == NULL) {
                run->frame1x = (u8 *)calloc(1, (size_t)256 * 192 * 4);
            }
            if (cap & 0x80000000u) {
                /* The display capture reads the 3D layer: it needs the real
                 * frame at DS size.  When that frame only feeds the capture
                 * (the screen shows VRAM) the read does not wait for the GPU
                 * and arrives a frame late. */
                if (run->frame1x != NULL
                    && eidos_platform_read3d_1x(run->plat, snap, run->frame1x,
                                                !(main3d && graphics)) >= 0) {
                    rgba3d = run->frame1x;
                    n = 1u;
                    run->frame1x_valid = 1;
                    run->gpu_real = 1;
                }
            } else {
                /* The GPU draws the 3D layer itself; the 2D compositor only
                 * needs to know where it shows, so its 3D layer is one
                 * opaque unlikely colour. */
                if (boot_sentinel(run) != NULL) {
                    rgba3d = run->sentinel;
                    n = 1u;
                    run->frame1x_valid = 1;
                }
            }
        }
    }
    eidos_prof_end(EIDOS_PROF_G3D, t0);
    if (run->hi_n != n) {
        size_t bytes = (size_t)256 * n * 192 * n * 4;

        free(run->hi_top);
        free(run->hi_bottom);
        run->hi_top = (u8 *)calloc(1, bytes);
        run->hi_bottom = (u8 *)calloc(1, bytes);
        if (run->hi_top == NULL || run->hi_bottom == NULL) {
            bw_panic("eidos_boot", 0, "out of memory for the screens");
        }
        run->hi_n = n;
    }
    run->hi_sn[0] = run->hi_sn[1] = 1u;
    /* phase 3 (docs/gpu-2d.md): with the GPU 2D path on, the main screen's
     * 3D layer is the GPU's own frame in the 2D shader; the CPU's screens
     * with the 3D off and over black are not needed (the one-screen world
     * gets them from the shader too).  Not with the capture (it reads the
     * real frame) nor in the check mode. */
    run->gpu2d = run->plat != NULL && boot_gpu2d_on() && eidos_platform_gpu2d_ready(run->plat);
    /* phase 2 asks the composes for their frames; phase 4 leaves the
     * screens the GPU composes to it (not while the check compares) */
    g2d_gpu_export(run->gpu2d || getenv("EIDOS_G2D_GPU_CHECK") != NULL);
    g2d_gpu_skip_cpu(run->gpu2d && getenv("EIDOS_G2D_GPU_CHECK") == NULL);
    run->gpu2d3d = run->gpu2d && run->frame1x_valid && !run->gpu_real
                   && getenv("EIDOS_G2D_GPU_CHECK") == NULL;
    g2d_gpu_main_3d(run->gpu2d3d);
    if (run->behind == NULL && run->plat != NULL && eidos_platform_gpu3d(run->plat)) {
        run->behind = (u8 *)calloc(1, (size_t)256 * 192 * 4);
    }
    {
        int behind_done = 0;
        BootCapture capt;

        boot_capture_plan(run, &capt);
        /* a frame that can blend 2D over the GPU's 3D (BG0 a second target)
         * also gets its screen over a black 3D layer from the same compose
         * (boot_gpu3d) */
        g2d_hw_main_zero_request(NULL);
        if (run->plat != NULL && eidos_platform_gpu3d(run->plat) && !run->gpu_real && !run->gpu2d3d
            && (bw_io_read16(0x04000050u) & 0x0100u) != 0u) {
            if (run->zero == NULL) {
                run->zero = (u8 *)calloc(1, (size_t)256 * 192 * 4);
            }
            g2d_hw_main_zero_request(run->zero);
        }
        (void)g2d_hw_render_dsmem_adaptive_ex(
            n, rgba3d, run->hi_top, run->hi_bottom, unsupported, run->hi_sn,
            run->frame1x_valid && !run->gpu2d3d ? run->behind : NULL, &behind_done);
        {
            unsigned long long t3 = eidos_prof_begin();

            boot_gpu3d(run, behind_done);
            boot_capture_run(run, &capt);
            eidos_prof_end(EIDOS_PROF_BOOT3D, t3);
        }
    }
    if (run->plat != NULL && getenv("EIDOS_G2D_GPU_CHECK") != NULL) {
        boot_gpu2d_check(run);
    }
    top_fresh = 1;
    if (run->gpu2d) {
        /* docs/gpu-2d.md, phase 2: a screen the shader can compose is shown
         * from its output; the CPU's stays for the readers that need it */
        int slot;

        for (slot = 0; slot < 2; slot++) {
            const G2dGpuFrame *f = g2d_gpu_frame(slot);

            if (f != NULL && f->valid && run->hi_sn[f->screen] == 1u) {
                (void)eidos_platform_gpu2d_show(run->plat, f, f->screen);
                if (g2d_gpu_cpu_skipped(slot)) {
                    /* the readers of the pixels (the one-screen interface,
                     * the held screens) get the GPU's, a frame late; none
                     * when the last frame was not shown (fast forward) */
                    int got = eidos_platform_gpu2d_pixels(run->plat, f->screen,
                                                          f->screen == 0 ? run->hi_top : run->hi_bottom) == 0;

                    if (f->screen == 0) {
                        top_fresh = got;
                    }
                }
            }
        }
    }
    if (top_fresh) {
        boot_flash_watch(run, eidos_frame_count(&run->loop));
    }
    g_render_ns = eidos_prof_on ? bw_clock_ns() - t0 : 0;
    eidos_prof_end(EIDOS_PROF_RENDER, t0);
}

#endif

/* The output frames one DS frame is worth, the remainder carried. */
static u32 boot_audio_frames(BootRun *run)
{
    u64 num = (u64)BW_AUDIO_RATE * DS_FRAME_CLOCKS + run->audio_rem;
    u32 n = (u32)(num / DS_CLOCK_HZ);

    run->audio_rem = num % DS_CLOCK_HZ;
    return n;
}

/*
 * The sound of a frame with no audio device (EIDOS_NO_AUDIO, a hidden
 * test run): rendered and dropped, or with EIDOS_AUDIO_WAV=FILE written to
 * a 16-bit stereo WAV at BW_AUDIO_RATE.  Each frame writes exactly its own
 * share, so the file stays in step with the frames at any speed, fast
 * forward included: a recording of a scripted run lines up with its
 * --screenshot frames (frame N starts at N * 560190 / 33513982 s).
 */
static void boot_audio_silent(u32 n)
{
    static int state;               /* 0 not looked at, 1 off, 2 writing */
    static FILE *wav;
    static u32 bytes;
    static s16 buf[2u * 2048u];
    unsigned char h[44];

    if (state == 0) {
        const char *path = getenv("EIDOS_AUDIO_WAV");

        state = 1;
        if (path != NULL && *path != '\0' && (wav = fopen(path, "wb")) != NULL) {
            state = 2;
        }
    }
    if (state != 2 || n > 2048u) {
        bw_snd_render(NULL, n);
        return;
    }
    bw_snd_render(buf, n);
    fseek(wav, 44 + (long)bytes, SEEK_SET);
    fwrite(buf, 4, n, wav);
    bytes += n * 4u;
    /* the header again with the sizes so far: the file is whole whenever
     * the run stops */
    memcpy(h, "RIFF\0\0\0\0WAVEfmt \x10\0\0\0\x01\0\x02\0\0\0\0\0\0\0\0\0\x04\0\x10\0data\0\0\0\0", 44);
    h[4] = (unsigned char)(36u + bytes); h[5] = (unsigned char)((36u + bytes) >> 8);
    h[6] = (unsigned char)((36u + bytes) >> 16); h[7] = (unsigned char)((36u + bytes) >> 24);
    h[24] = (unsigned char)BW_AUDIO_RATE; h[25] = (unsigned char)(BW_AUDIO_RATE >> 8);
    h[26] = (unsigned char)(BW_AUDIO_RATE >> 16); h[27] = 0;
    h[28] = (unsigned char)(BW_AUDIO_RATE * 4); h[29] = (unsigned char)((BW_AUDIO_RATE * 4) >> 8);
    h[30] = (unsigned char)((BW_AUDIO_RATE * 4) >> 16); h[31] = 0;
    h[40] = (unsigned char)bytes; h[41] = (unsigned char)(bytes >> 8);
    h[42] = (unsigned char)(bytes >> 16); h[43] = (unsigned char)(bytes >> 24);
    fseek(wav, 0, SEEK_SET);
    fwrite(h, 1, 44, wav);
    fflush(wav);
}

#if EIDOS_BOOT_SDL
/*
 * The window, opened by the thread that runs the frames.  The halt hook runs
 * on the game's idle thread, not on main(): a window belongs to the thread
 * that created it (a Win32 window's messages reach only that thread's
 * queue, and SDL's renderer is not thread-safe), so it is created here, at
 * the first frame, and pumped, drawn and closed by the same thread for the
 * rest of the run.  Created on main(), it was never pumped while main() sat
 * in the game's OS_WaitIrq, and Windows showed it frozen after a few
 * seconds.
 */
static void boot_window_open(BootRun *run)
{
    EidosPlatformConfig cfg;

    run->window_tried = 1;
    memset(&cfg, 0, sizeof cfg);
    cfg.title = "eidos boot";
    eidos_settings_fill_config(&cfg);
    cfg.audio = 1;
    cfg.audio_push = 1;
    if (eidos_platform_init(&run->plat, &cfg) != 0) {
        fprintf(stderr, "eidos_boot: no window; running headless\n");
        run->plat = NULL;
    } else {
        eidos_settings_attach(run->plat);
    }
}

/*
 * Sound, clocked by the device.  The game's sound driver (sdk/arm7/snd.c)
 * is the ARM7's, driven by the game thread through the PXI: rendering it
 * from SDL's audio thread would race the game.  So it renders here, on the
 * frame thread, in fixed chunks, and the device's queue is kept filled to a
 * fixed latency (AUDIO_LATENCY_FRAMES, about 70 ms).  A frame that runs late
 * finds the queue shorter and renders more chunks to catch up, so the device
 * never runs dry for a late frame; a frame that is early finds it full and
 * renders none.  The driver's clock follows what the device plays, so the
 * sound neither drifts nor stutters in bursts when the game runs slow.
 */
#define AUDIO_CHUNK_FRAMES   256u
#define AUDIO_LATENCY_FRAMES 2304u     /* 70 ms at 32768 Hz */
#define AUDIO_MAX_PER_CALL   8192u     /* a stall's catch-up bound */

static void boot_audio_push(BootRun *run)
{
    static s16 buf[2u * AUDIO_CHUNK_FRAMES];
    u32 queued = eidos_platform_audio_queued(run->plat);
    u32 done = 0;

    while (queued < AUDIO_LATENCY_FRAMES && done < AUDIO_MAX_PER_CALL) {
        bw_snd_render(buf, AUDIO_CHUNK_FRAMES);
        eidos_platform_queue_audio(run->plat, buf, AUDIO_CHUNK_FRAMES);
        queued += AUDIO_CHUNK_FRAMES;
        done += AUDIO_CHUNK_FRAMES;
    }
}

/*
 * Fast forward's sound: every frame's share is rendered, so the music and
 * the effects advance with the game, and queued while the device holds less
 * than the latency.  The device plays without a gap; what it cannot take in
 * real time is dropped.
 */
static void boot_audio_fast(BootRun *run)
{
    static s16 buf[2u * 1024u];
    u32 n = boot_audio_frames(run);

    if (n > 1024u) {
        n = 1024u;
    }
    bw_snd_render(buf, n);
    if (eidos_ui_quiet()) {
        return;         /* the interface covers a jump in the game: not heard */
    }
    if (eidos_platform_audio_queued(run->plat) < AUDIO_LATENCY_FRAMES) {
        eidos_platform_queue_audio(run->plat, buf, n);
    }
}
#endif

/* ------------------------------------------------------------------ */
/* The run restart and the save handoff's reload                        */
/* ------------------------------------------------------------------ */

/*
 * The mod layer's run restart (sdk/mod_restart.c) and the save handoff's
 * reload (sdk/mod_handoff.c) both ask the port to start the game again:
 * the restart resets the game the way the port's own reset does and, with
 * BW_RESTART_NEW_GAME, starts a new game directly, without stopping at the
 * title; the reload installs a save that arrived from a peer and goes on
 * with it (docs/mod-services.md, "Run restart" and "Save handoff").
 *
 * Neither can be done where the request is made: the restart's handler runs
 * on the game's thread and the reload on the frame, and the reset clears the
 * main RAM the game is running in.  So the request is recorded here and the
 * frame loop carries it out at its next safe point (boot_frame).
 *
 * What the run does once it has started again.
 */
typedef enum BootRunNext {
    BOOT_RUN_TITLE = 0,    /* the title screen, as the port's reset leaves it */
    BOOT_RUN_NEW_GAME,     /* a new game, without the title                   */
    BOOT_RUN_CONTINUE,     /* the save that is in force                       */
    BOOT_RUN_MENU          /* the title menu, without the logos and the title */
} BootRunNext;

/*
 * The restart's calls.  sdk/mod_restart.c has no header of its own (the
 * saved-data layer does the same), so the prototypes are here.
 */
void bw_mod_restart_set_handler(void (*fn)(u32 flags));
int  bw_mod_restart_take(u32 *flags);

/*
 * The port's own confirmation for a save that arrives from a peer.  It lives
 * with the port's options screen (engine/platform/mod_options_proc.c), which
 * has no header of its own, so the prototype is here.
 */
void eidos_mod_handoff_install_confirm(void);

/*
 * The mod layer's timer (sdk/mod_timer.c has no header of its own either).
 */
void bw_mod_timer_start(void);
void bw_mod_timer_stop(void);

/* The request that waits for the frame loop, and what it asks for. */
static BootRunNext g_run_next = BOOT_RUN_TITLE;
static int         g_restart_pending;

/* The restart's machinery (below): the game starts again, and the game
 * goes on with a new game or with the save that is in force. */
static void boot_start_game(BootRunNext next);
static void boot_run_restart(void);

/* ------------------------------------------------------------------ */
/* Multiplayer: the held game                                          */
/* ------------------------------------------------------------------ */

#if EIDOS_BOOT_SDL
/* One line of the port's bitmap font into an RGBA screen of 256n x 192n,
 * centred, at row `y` of the DS screen, in white (ASCII only: the game's
 * font is not open yet). */
static void hold_text_bitmap(u8 *rgba, u32 n, int y, const char *text)
{
    const char *s = text;
    int len = 0;
    int x0;
    u32 ch;

    while (eidos_utf8_next(&s) != 0u) {
        len++;
    }
    x0 = (256 - len * EIDOS_FONT_ADVANCE) / 2;
    if (x0 < 2) {
        x0 = 2;
    }
    s = text;
    while ((ch = eidos_utf8_next(&s)) != 0u) {
        const u8 *g = eidos_font_glyph(ch);
        int r, c;

        for (r = 0; r < EIDOS_FONT_ROWS && g != NULL; r++) {
            for (c = 0; c < EIDOS_FONT_COLS; c++) {
                u32 px, py;

                if (!(g[r] & (0x10u >> c))) {
                    continue;
                }
                for (py = 0; py < n; py++) {
                    for (px = 0; px < n; px++) {
                        int X = (x0 + c) * (int)n + (int)px;
                        int Y = (y + r) * (int)n + (int)py;
                        u8 *d;

                        if (X < 0 || X >= 256 * (int)n || Y < 0 ||
                            Y >= 192 * (int)n) {
                            continue;
                        }
                        d = rgba + ((size_t)Y * 256u * n + (size_t)X) * 4u;
                        d[0] = d[1] = d[2] = 255;
                    }
                }
            }
        }
        x0 += EIDOS_FONT_ADVANCE;
        if (x0 > 256 - EIDOS_FONT_ADVANCE) {
            break;
        }
    }
}

/* One line in the game's font (the interface's, so a translation's own
 * letters show), centred at row `y` of the DS screen, in white. */
static void hold_text(u8 *rgba, u32 n, int y, const char *text)
{
    static EidosUiCanvas canvas;
    EidosUiCtx c;
    int w = ui_text_w(text, UI_FONT_SMALL);
    int x, yy;

    if (w <= 0) {
        hold_text_bitmap(rgba, n, y, text);
        return;
    }
    ui_begin(&c, &canvas, 256, 16, 1);
    if (canvas.rgba == NULL || canvas.w != 256) {
        hold_text_bitmap(rgba, n, y, text);
        return;
    }
    x = w < 252 ? (256 - w) / 2 : 2;
    (void)ui_text(&c, x, 2, text, 0xFFFFFFu, 0x404040, UI_FONT_SMALL, 1);
    for (yy = 0; yy < 16; yy++) {
        for (x = 0; x < 256; x++) {
            const u8 *s = canvas.rgba + ((size_t)yy * 256u + (size_t)x) * 4u;
            u32 px, py;

            if (s[3] == 0) {
                continue;
            }
            for (py = 0; py < n; py++) {
                int Y = (y - 2 + yy) * (int)n + (int)py;
                u8 *d;

                if (Y < 0 || Y >= 192 * (int)n) {
                    continue;
                }
                d = rgba + ((size_t)Y * 256u * n + (size_t)x * n) * 4u;
                for (px = 0; px < n; px++, d += 4) {
                    d[0] = s[0];
                    d[1] = s[1];
                    d[2] = s[2];
                }
            }
        }
    }
}

/* The held screen: the last frame, darkened, with the wait's text. */
static void hold_draw(u8 *dst, const u8 *src, u32 n, const char *line1,
                      const char *line2)
{
    size_t i;
    size_t bytes = (size_t)256 * n * 192 * n * 4;

    for (i = 0; i < bytes; i += 4) {
        dst[i] = (u8)(src[i] / 3);
        dst[i + 1] = (u8)(src[i + 1] / 3);
        dst[i + 2] = (u8)(src[i + 2] / 3);
        dst[i + 3] = src[i + 3];
    }
    if (line1 != NULL) {
        hold_text(dst, n, 84, line1);
    }
    if (line2 != NULL && line2[0] != '\0') {
        hold_text(dst, n, 100, line2);
    }
}
#endif

/*
 * Multiplayer holds the game (sdk/mp.h): a player dropped past the grace,
 * the host is gone, or this game lost the relay.  The game's own thread is
 * held here, in its halt hook, so nothing of the game runs; the network,
 * the multiplayer timers and the window go on.  The host's START starts
 * the game again when everyone is back.  A session that ends while held
 * (the host never came back) sends a joiner back to the title.
 */
static void boot_mp_hold(BootRun *run)
{
#if EIDOS_BOOT_SDL
    u8 *top = NULL;
    u8 *bottom = NULL;
    u8 *work = NULL;
    u32 n = run->hi_n;
    size_t bytes = (size_t)256 * n * 192 * n * 4;
#endif

    printf("eidos_boot: the game is held: %s\n", bw_mp_wait_text());
    fflush(stdout);
#if EIDOS_BOOT_SDL
    if (n != 0u && run->hi_top != NULL && run->hi_bottom != NULL) {
        top = (u8 *)malloc(bytes);
        bottom = (u8 *)malloc(bytes);
        work = (u8 *)malloc(bytes);
        if (top != NULL && bottom != NULL && work != NULL) {
            memcpy(top, run->hi_top, bytes);
            memcpy(bottom, run->hi_bottom, bytes);
        }
    }
#endif
    while (bw_mp_paused()) {
        bw_mod_net_frame();
        bw_mp_frame();
#if EIDOS_BOOT_SDL
        if (run->plat != NULL) {
            if (!eidos_platform_pump(run->plat)) {
                boot_finish(0, "window closed");
            }
            if (bw_mp_is_host() &&
                (eidos_platform_buttons_pressed(run->plat) & EIDOS_PAD_START)) {
                (void)bw_mp_start();
            }
            if (top != NULL && bottom != NULL && work != NULL) {
                /* the code again, in case the players forgot it */
                char code_line[64];

                snprintf(code_line, sizeof code_line, ui_trf("Code: %s"), bw_mp_code());
                /* a host whose relay restarted shows its new code */
                hold_draw(work, top, n, bw_mp_wait_text(),
                          !bw_mp_is_host() ? ui_tr("The host starts the game.") :
                          bw_mp_relay_restarted()
                          ? ui_tr(bw_mp_end_reason()) : code_line);
                memcpy(run->hi_top, work, bytes);
                /* the bottom screen only dims: the text is on the top one,
                 * and the title's Multiplayer screen, when it is the one
                 * held, already shows the code there (the two overlapped) */
                hold_draw(work, bottom, n, NULL, NULL);
                memcpy(run->hi_bottom, work, bytes);
                eidos_platform_present_nn(run->plat, run->hi_top,
                                          (int)run->hi_sn[0], run->hi_bottom,
                                          (int)run->hi_sn[1]);
            }
            eidos_platform_wait_frame(run->plat);
            continue;
        }
#endif
        bw_mp_hold_wait();
    }
#if EIDOS_BOOT_SDL
    if (top != NULL && bottom != NULL) {
        memcpy(run->hi_top, top, bytes);
        memcpy(run->hi_bottom, bottom, bytes);
    }
    free(top);
    free(bottom);
    free(work);
#endif
    if (bw_mp_state() == BW_MP_ENDED && !bw_mp_is_host()) {
        printf("eidos_boot: the session ended (%s); back to the title\n",
               bw_mp_end_reason());
        fflush(stdout);
        bw_mp_leave();
        g_run_next = BOOT_RUN_TITLE;
        g_restart_pending = 1;
    } else {
        printf("eidos_boot: the game goes on\n");
        fflush(stdout);
    }
}

/*
 * The host closes the window while its players' games are still saving
 * (they save at their next quiet moment after the host did): the window
 * stays, darkened, until every save is in, for up to 30 seconds.  The
 * network and the multiplayer timers go on; the game does not.
 */
#define BOOT_SAVE_WAIT_FRAMES (30 * 60)

static void boot_mp_wait_saves(BootRun *run)
{
#if EIDOS_BOOT_SDL
    u32 n = run->hi_n;
    size_t bytes = (size_t)256 * n * 192 * n * 4;
    u8 *work = NULL;
    int f;

    if (bw_mp_saves_pending() == 0) {
        return;
    }
    printf("eidos_boot: the window waits for %d player save(s)\n",
           bw_mp_saves_pending());
    fflush(stdout);
    if (n != 0u && run->hi_top != NULL && run->hi_bottom != NULL) {
        work = (u8 *)malloc(bytes);
    }
    for (f = 0; f < BOOT_SAVE_WAIT_FRAMES && bw_mp_saves_pending() > 0; f++) {
        char line[64];

        bw_mod_net_frame();
        bw_mp_frame();
        if (run->plat == NULL) {
            bw_mp_hold_wait();
            continue;
        }
        (void)eidos_platform_pump(run->plat);   /* a second close: still waits */
        snprintf(line, sizeof line, "%d of the players' saves to come",
                 bw_mp_saves_pending());
        if (work != NULL) {
            u8 *top = (u8 *)malloc(bytes);
            u8 *bottom = (u8 *)malloc(bytes);

            if (top != NULL && bottom != NULL) {
                memcpy(top, run->hi_top, bytes);
                memcpy(bottom, run->hi_bottom, bytes);
                hold_draw(work, top, n, "Saving everyone's games", line);
                memcpy(run->hi_top, work, bytes);
                hold_draw(work, bottom, n, NULL, NULL);
                memcpy(run->hi_bottom, work, bytes);
                eidos_platform_present_nn(run->plat, run->hi_top,
                                          (int)run->hi_sn[0], run->hi_bottom,
                                          (int)run->hi_sn[1]);
                memcpy(run->hi_top, top, bytes);
                memcpy(run->hi_bottom, bottom, bytes);
            }
            free(top);
            free(bottom);
        }
        eidos_platform_wait_frame(run->plat);
    }
    free(work);
    printf("eidos_boot: %s\n", bw_mp_saves_pending() == 0
           ? "every player's save is in"
           : "closing without every player's save");
    fflush(stdout);
#else
    (void)run;
#endif
}

/* The game's save control (weak: declared again below with the boot's
 * other game calls). */
#if defined(__GNUC__) || defined(__clang__)
extern void *SAVE_CONTROL_Get(void) __attribute__((weak));
#else
extern void *SAVE_CONTROL_Get(void);
#endif

/*
 * The interface's own restarts (the style chooser's preview, engine/ui/
 * ui.c): 1 the game starts again on the save in force, with no title and
 * no menu; 2 it starts again at the title menu, with no logos and no title
 * screen.  Carried out at the frame loop's next safe point.
 */
static int g_run_quick;     /* the start takes the logo screen's place (boot_proc_pushing_logo) */
static int g_boot_push;     /* boot_start_game queues a push, not a replace */

void eidos_boot_restart_to(int what)
{
    g_run_next = what == 1 ? BOOT_RUN_CONTINUE : what == 2 ? BOOT_RUN_MENU : BOOT_RUN_TITLE;
    g_run_quick = g_run_next != BOOT_RUN_TITLE;
    g_restart_pending = 1;
}

/*
 * The game's process manager (arm9's procsys.c).  The launch driver at the
 * bottom pushes the boot's processes one after the other, and a push shares
 * its queue with a replace: a replace asked for while the driver is on top
 * is lost to its next push (the flag stays, the table does not, and the
 * child's end starts a process with no table).  1 while one of the driver's
 * children is on top and nothing is queued: a replace then takes that
 * child's place, the way the title's rows replace the title menu.
 */
typedef struct BootProc {
    const void *data;
    u32 overlayId, seq, subSeq;
    void *procWork, *work;
    struct BootProc *prev, *next;
} BootProc;

typedef struct BootProcSys {
    u16 heapId, padding02;
    u32 unused04;
    BootProc *current;
    u32 pushPending, replacePending, nextOverlayId;
    const void *nextData;
    void *nextProcWork;
} BootProcSys;

#if defined(__GNUC__) || defined(__clang__)
extern BootProcSys *g_ProcSys __attribute__((weak));
extern void *bw_dsref_g_ProcSys __attribute__((weak));
#else
extern BootProcSys *g_ProcSys;
extern void *bw_dsref_g_ProcSys;
#endif

static int boot_proc_replaceable(void)
{
    BootProcSys *sys = NULL;

    if (&g_ProcSys != NULL) {
        sys = g_ProcSys;
    } else if (&bw_dsref_g_ProcSys != NULL && bw_dsref_g_ProcSys != NULL) {
        memcpy(&sys, bw_dsref_g_ProcSys, sizeof(sys));
    } else {
        return 1;                   /* (not in reach in this build: as it was) */
    }
    return sys != NULL && sys->current != NULL && sys->current->prev != NULL
           && sys->pushPending == 0u && sys->replacePending == 0u;
}

/*
 * 1 while the launch driver is on top with its first push queued: the logo
 * screen (ov088's STARTMENU_LogoScreen, the ProcData at 0x021BD8E8), which
 * has not begun.  A push queued now takes its place in the queue, so the
 * process it names starts where the logo screen would have; when that
 * process ends the driver goes on with its next one, as after the logo
 * screen.
 */
#define BOOT_LOGO_PROC_ADDR 0x021BD8E8u

static int boot_proc_pushing_logo(void)
{
    BootProcSys *sys = NULL;

    if (&g_ProcSys != NULL) {
        sys = g_ProcSys;
    } else if (&bw_dsref_g_ProcSys != NULL && bw_dsref_g_ProcSys != NULL) {
        memcpy(&sys, bw_dsref_g_ProcSys, sizeof(sys));
    }
    return sys != NULL && sys->current != NULL && sys->current->prev == NULL
           && sys->pushPending != 0u && sys->replacePending == 0u && sys->nextOverlayId == 88u
           && sys->nextData != NULL && sys->nextData == bw_ds_try_ptr(BOOT_LOGO_PROC_ADDR);
}

/* Multiplayer boots a joiner's game on the save the host sent. */
static void boot_mp_boot(int new_game)
{
    printf("eidos_boot: multiplayer: the game starts on %s\n",
           new_game ? "a new game" : "the host's save");
    fflush(stdout);
    g_run_next = new_game ? BOOT_RUN_NEW_GAME : BOOT_RUN_CONTINUE;
    g_restart_pending = 1;
}

/* The halt hook: one frame of time passing. */
static unsigned long long g_game_since;     /* the end of the last frame function (the profiler's clock) */

static void boot_frame(void *user)
{
    BootRun *run = (BootRun *)user;
    int audio_device = 0;
    int speed = 1;   /* fast forward factor (Tab), eidos_platform_fast_forward */
    u64 t_step, t_ph;

    /*
     * The restart and the handoff's reload: both want the game to start
     * again, and neither can be done where the request was made (the
     * mod's handler is on the game's thread, and the reset clears the main
     * RAM the game is running in).  The request waits here and is carried
     * out now, at the frame loop's safe point.
     */
    {
        u32 flags = 0;

        if (g_restart_pending) {
            g_restart_pending = 0;
            boot_run_restart();
        } else if (bw_mod_restart_take(&flags) != 0) {
            /* A request that came in with no handler in place waits for the
             * port to take it, so that it is not lost. */
            g_run_next = ((flags & BW_RESTART_NEW_GAME) != 0u)
                         ? BOOT_RUN_NEW_GAME : BOOT_RUN_TITLE;
            boot_run_restart();
        }
    }

    /* A scripted multiplayer run for the port's own checks:
     * EIDOS_MP_TEST=host:<file> hosts at frame 300, writes the code to the
     * file and starts as soon as the lobby is ready; EIDOS_MP_TEST=join:<file>
     * joins with the code in the file. */
    {
        static int mp_test_done, mp_test_started;
        const char *t = getenv("EIDOS_MP_TEST");
        u64 n = eidos_frame_count(&run->loop);

        const char *at = getenv("EIDOS_MP_TEST_AT");

        if (t != NULL && !mp_test_done &&
            n >= (at != NULL ? (u64)strtoul(at, NULL, 10) : 300u)) {
            char code[40];
            FILE *f;

            if (strncmp(t, "host:", 5) == 0) {
                if (bw_mp_host(code, sizeof code) == 0 &&
                    (f = fopen(t + 5, "wb")) != NULL) {
                    fputs(code, f);
                    fclose(f);
                }
                mp_test_done = 1;
            } else if (strncmp(t, "join:", 5) == 0 &&
                       (f = fopen(t + 5, "rb")) != NULL) {
                if (fgets(code, sizeof code, f) != NULL) {
                    (void)bw_mp_join(code);
                }
                fclose(f);
                mp_test_done = 1;
            }
        }
        if (t != NULL && mp_test_done && !mp_test_started && bw_mp_is_host() &&
            bw_mp_state() == BW_MP_LOBBY && bw_mp_start_blocker()[0] == '\0') {
            mp_test_started = 1;
            printf("eidos_boot: multiplayer test: the host starts\n");
            fflush(stdout);
            (void)bw_mp_start();
        }
    }

    /* A scripted Link run (sdk/link.h): EIDOS_LINK_TEST=host:<file> hosts a
     * Link session at frame EIDOS_LINK_TEST_AT (default 300) and writes the
     * code to the file; EIDOS_LINK_TEST=join:<file> waits for the file and
     * joins with its code.  EIDOS_LINK_LOG=1 prints the radio's state each
     * second. */
    {
        static int link_test_done;
        static u64 link_log_at;
        const char *t = getenv("EIDOS_LINK_TEST");
        const char *at = getenv("EIDOS_LINK_TEST_AT");
        u64 n = eidos_frame_count(&run->loop);

        if (t != NULL && !link_test_done &&
            n >= (at != NULL ? (u64)strtoul(at, NULL, 10) : 300u)) {
            char code[40];
            FILE *f;

            if (strncmp(t, "host:", 5) == 0) {
                int rc = bw_link_host(code, sizeof code);

                printf("eidos_boot: link test: host rc=%d code=%s\n", rc, rc == 0 ? code : "-");
                if (rc == 0 && (f = fopen(t + 5, "wb")) != NULL) {
                    fputs(code, f);
                    fclose(f);
                }
                link_test_done = 1;
            } else if (strncmp(t, "join:", 5) == 0 &&
                       (f = fopen(t + 5, "rb")) != NULL) {
                if (fgets(code, sizeof code, f) != NULL) {
                    int rc = bw_link_join(code);

                    printf("eidos_boot: link test: join rc=%d\n", rc);
                    link_test_done = 1;
                }
                fclose(f);
            }
            fflush(stdout);
        }
        if (getenv("EIDOS_LINK_LOG") != NULL && bw_link_state() == BW_LINK_OPEN &&
            n >= link_log_at) {
            BwWmInfo wi;

            bw_arm7_wm_info(&wi);
            printf("eidos_boot: link f%llu state=%u aid=%u children=%04x parents=%u sent=%u heard=%u\n",
                   (unsigned long long)n, wi.state, wi.aid, wi.child_bitmap,
                   wi.parents_heard, wi.sent, wi.heard);
            fflush(stdout);
            link_log_at = n + 60u;
        }
    }

    {
        /* EIDOS_SAVE_TEST_AT=<frame>: the next step after that frame saves
         * the way a joiner's game saves when the host saved (sdk/mp.h). */
        const char *st = getenv("EIDOS_SAVE_TEST_AT");
        static int save_test_done;

        if (st != NULL && !save_test_done &&
            eidos_frame_count(&run->loop) >= (u64)strtoul(st, NULL, 10)) {
            save_test_done = 1;
            bw_mp_request_save_for_test();
        }
    }

    /* Multiplayer may hold the game here (sdk/mp.h). */
    if (bw_mp_paused()) {
        boot_mp_hold(run);
    }

    /*
     * The first frame of a run that was started again: the game goes into a
     * new game, or loads the save that is in force, the way the title's own
     * rows do it (ov088's startmenu.c).
     */
    if (g_run_next != BOOT_RUN_TITLE && eidos_frame_count(&run->loop) > 0 &&
        SAVE_CONTROL_Get != NULL && SAVE_CONTROL_Get() != NULL) {
        /* the game's own start-up has made its save control: the game
         * systems are up, and the title's own continue would run now */
        if (g_run_quick && boot_proc_pushing_logo()) {
            g_boot_push = 1;            /* in the logo screen's place */
            boot_start_game(g_run_next);
            g_boot_push = 0;
            g_run_next = BOOT_RUN_TITLE;
            g_run_quick = 0;
        } else if (boot_proc_replaceable()) {
            boot_start_game(g_run_next);
            g_run_next = BOOT_RUN_TITLE;
            g_run_quick = 0;
        }
    }

#if EIDOS_BOOT_SDL
    if (run->want_window && !run->window_tried) {
        boot_window_open(run);
    }
#endif

    if (eidos_frame_count(&run->loop) >= run->limit) {
        boot_finish(0, "ran");
    }
    if (eidos_ui_script_quit()) {
        boot_finish(eidos_ui_script_quit() == 2 ? 3 : 0, "the test script ended");
    }

    if (eidos_prof_on && g_game_since != 0) {
        unsigned long long now = eidos_prof_begin();

        eidos_prof_end(EIDOS_PROF_GAME, g_game_since);
        eidos_hitch_frame((double)(now - g_game_since) / 1e6, eidos_frame_count(&run->loop));
        {
            /* the VRAM bank map's share of a slow frame (sdk/dsmem.c) */
            extern unsigned bw_vram_stat_remaps, bw_vram_stat_moves;
            extern unsigned long long bw_vram_stat_bytes, bw_vram_stat_ns;

            if (now - g_game_since >= 50000000ull && getenv("EIDOS_HITCH_MS") != NULL) {
                fprintf(stderr, "eidos: hitch vram: %u remaps, %u bank moves, %.1f MB copied, %.1f ms\n",
                        bw_vram_stat_remaps, bw_vram_stat_moves, (double)bw_vram_stat_bytes / 1048576.0,
                        (double)bw_vram_stat_ns / 1e6);
            }
            bw_vram_stat_remaps = bw_vram_stat_moves = 0;
            bw_vram_stat_bytes = bw_vram_stat_ns = 0;
        }
    } else {
        eidos_hitch_frame(0.0, 0);      /* EIDOS_HITCH_MS turns the profiler's clock on */
    }

    /* Line 0 of the next frame: the vblank flag drops. */
    io_raw_write(REG_DISPSTAT, 2,
                 io_raw_read(REG_DISPSTAT, 2) & ~DISPSTAT_VBLANK_FLAG);
    io_raw_write(REG_VCOUNT, 2, 0);

    /* The input of this frame, before its vblank (sdk/input.h): the
     * window's keys and the --press / --input script, KEYINPUT and the
     * ARM7's XY halfword written, the touch panel's automatic samples. */
    t_step = eidos_prof_begin();
    g_render_ns = 0;
    bw_input_frame();
    if (g_trace_on) {
        g3d_hw_trace(eidos_frame_count(&run->loop) + 1u == g_trace_frame);
    }
#if EIDOS_BOOT_SDL
    /* Fast forward (Tab): one frame in `speed` is composed and shown, the
     * rest neither, so the game's own work is most of what runs; composing a
     * frame costs more than running one, more so at higher resolutions.  The
     * 3D engine still works out every frame: a swap shows what was sent the
     * frame before, so a frame it skipped would show stale or empty.  A frame is
     * shown sooner when a display period (1/60 s) has passed, so the window
     * keeps its frame rate at any speed, unlimited (left Shift+Tab) included.  Not
     * while the display capture is on: it reads the drawn frame back into
     * VRAM for the game. */
    {
        int ff;

        /* No fast forward in a multiplayer session (host or joiner): the
         * other players run at the game's own speed. */
        eidos_platform_fast_forward_lock(bw_mp_state() != BW_MP_OFF &&
                                         bw_mp_state() != BW_MP_ENDED);
        ff = run->plat != NULL ? eidos_platform_fast_forward(run->plat) : 1;

        run->ff_skip = ff > 1 && run->ff_since + 1u < (u32)ff
            && eidos_platform_now_ns(run->plat) - run->ff_shown_ns < 16666667ull
            && (bw_io_read32(0x04000064u) & 0x80000000u) == 0u
            && !eidos_ui_script_draw();     /* a test's picture is about to be taken */
        run->ff_since = run->ff_skip ? run->ff_since + 1u : 0u;
    }
#endif
    eidos_frame_run_one(&run->loop);
    eidos_prof_end(EIDOS_PROF_STEP, t_step + g_render_ns);
    /* The mod layer's frame tick: the mods' own frame handlers and the
     * network's, once a frame, after the game's own frame work and in the
     * game's thread (the halt hook runs on the game's idle thread). */
    bw_mod_frame_tick();
    g_frames_seen++;
    boot_capture_step(run);
    {
        u64 n = eidos_frame_count(&run->loop);
        int i;
        for (i = 0; i < g_shot_count; i++) {
            if (g_shots[i] == n) {
                boot_screenshot(run, n);
            }
        }
        {
            /* EIDOS_SHOT_EVERY=N: a screenshot every N frames from frame
             * EIDOS_SHOT_FROM on (a recording of a whole scene, which
             * --screenshot's list is too short for) */
            static int every = -1;
            static u64 from;

            if (every < 0) {
                const char *e = getenv("EIDOS_SHOT_EVERY"), *f = getenv("EIDOS_SHOT_FROM");

                every = e != NULL ? atoi(e) : 0;
                from = f != NULL ? strtoull(f, NULL, 10) : 0;
            }
            if (every > 0 && n >= from && (n - from) % (u64)every == 0) {
                boot_screenshot(run, n);
            }
        }
    }

#if EIDOS_BOOT_SDL
    if (run->plat != NULL) {
        if (!eidos_platform_pump(run->plat)) {
            boot_mp_wait_saves(run);
            boot_finish(0, "window closed");
        }
        speed = eidos_platform_fast_forward(run->plat);
        t_ph = eidos_prof_begin();
        /* Badly behind (two frames or more): skip showing this frame, at
         * most three in a row; the game's steps are never skipped.  Fast
         * forward (Tab) shows a frame whenever a display period has passed
         * since the last one, so the window keeps its frame rate whatever
         * speed the host reaches. */
        if (run->ff_skip) {
            run->skipped = 0;
        } else if (eidos_platform_lag_frames(run->plat) >= 2u && run->skipped < 3u) {
            run->skipped++;
        } else {
            run->skipped = 0;
            run->ff_shown_ns = eidos_platform_now_ns(run->plat);
            if (run->hi_n != 0u) {
                eidos_platform_present_nn(run->plat, run->hi_top,
                                          (int)run->hi_sn[0], run->hi_bottom,
                                          (int)run->hi_sn[1]);
            }
        }
        eidos_prof_end(EIDOS_PROF_PRESENT, t_ph);
        {
            /* EIDOS_UI_SCRIPT's "shot NAME": the window as just shown */
            char name[80], path[1024];

            if (eidos_ui_script_shot(name, (int)sizeof name)) {
                snprintf(path, sizeof(path), "%s/%s.bmp", g_shot_dir, name);
                if (eidos_platform_screenshot(run->plat, path) == 0) {
                    printf("eidos_boot: script shot %s (frame %llu)\n", path,
                           (unsigned long long)eidos_frame_count(&run->loop));
                } else {
                    fprintf(stderr, "eidos_boot: cannot write %s\n", path);
                }
                fflush(stdout);
            }
        }
        {
            /* the style chooser's card: the window as just shown, when the
             * interface asks for it (ui.h, eidos_ui_card_shot) */
            char path[1024];

            if (eidos_ui_card_shot(path, (int)sizeof path)) {
                (void)eidos_platform_screenshot(run->plat, path);
            }
        }
        audio_device = eidos_platform_audio_open(run->plat);
        t_ph = eidos_prof_begin();
        if (audio_device && speed > 1) {
            boot_audio_fast(run);
        } else if (audio_device) {
            boot_audio_push(run);
        }
        eidos_prof_end(EIDOS_PROF_AUDIO, t_ph);
        t_ph = eidos_prof_begin();
        eidos_platform_wait_frame(run->plat);
        eidos_prof_end(EIDOS_PROF_WAIT, t_ph);
    }
#endif
    if (!audio_device) {
        boot_audio_silent(boot_audio_frames(run));
    }
    eidos_prof_frame(eidos_frame_count(&run->loop));
    g_game_since = eidos_prof_begin();  /* the game's own threads run from here */
}

/*
 * Back all of main RAM now.  sdk/dsmem.c commits its flat main RAM in
 * 64 KiB pieces as addresses are first translated, and a pointer reached by
 * arithmetic may run one piece past its translation.  The game goes much
 * further than that: OS_InitArena hands it the whole arena (DS
 * 0x02xxxxxx-0x023E0000, translated once at each end) and its heaps carve
 * megabytes out of it by pointer arithmetic alone.  Translating one
 * address in every piece commits them all, through dsmem's own interface.
 */
static void commit_main_ram(void)
{
    u32 addr;

    for (addr = 0x02000000u; addr < 0x03000000u; addr += 0x10000u) {
        (void)bw_ds_ptr(addr);
    }
}

/* With a window when asked and this build has one; headless otherwise. */
static void boot_window(EidosFrameHooks *hooks, int window)
{
#if EIDOS_BOOT_SDL
    size_t screen = (size_t)256 * 192 * 4;

    if (!window) {
        return;
    }
    /* The window itself opens at the first frame (boot_window_open). */
    g_run.want_window = 1;
    g_run.top = (u8 *)calloc(1, screen);
    g_run.bottom = (u8 *)calloc(1, screen);
    if (g_run.top == NULL || g_run.bottom == NULL) {
        bw_panic("eidos_boot", 0, "out of memory for the screens");
    }
    hooks->render = boot_render;
#else
    (void)hooks;
    if (window) {
        fprintf(stderr, "eidos_boot: built without SDL; running headless\n");
    }
#endif
}

/* ------------------------------------------------------------------ */
/* The run restart and the save handoff's reload                        */
/* ------------------------------------------------------------------ */

/*
 * The restart the mod layer asks for: the request is recorded here, and the
 * frame loop carries it out.  With BW_RESTART_NEW_GAME the game goes into a
 * new game directly; without it the game boots to the title, the way the
 * port's own reset leaves it.
 */
static void boot_restart(u32 flags)
{
    g_run_next = ((flags & BW_RESTART_NEW_GAME) != 0u) ? BOOT_RUN_NEW_GAME
                                                       : BOOT_RUN_TITLE;
    g_restart_pending = 1;
}

/*
 * The handoff's reload: a save arrived from a peer and the player took it,
 * so the save is installed (sdk/mod_handoff.c) and the game goes on with it.
 * The run starts again and loads the save that is now in force.
 */
static void boot_reload(void)
{
    g_run_next = BOOT_RUN_CONTINUE;
    g_restart_pending = 1;
}

/*
 * The game's own calls, the way the title screen starts a game (ov088's
 * startmenu.c): the save is reset for a new game, the init work says which
 * way the game starts, and the global process is replaced with the game's
 * own main process, which makes the map change that starts the game
 * (ov020's EVENT_MAPCHANGE_CreateByMode; the new game start event it
 * reaches is the port's game.new, mod/declared_set.txt).
 *
 * They are weak, so that a build without the game still links: the port
 * then says that it cannot start a game, and the game stays at the title.
 */
#if defined(__GNUC__) || defined(__clang__)
#define BW_BOOT_WEAK __attribute__((weak))
#else
#define BW_BOOT_WEAK
#endif

extern void *SAVE_CONTROL_Get(void) BW_BOOT_WEAK;
extern void  SAVE_CONTROL_ResetToNewGame(void *saveControl) BW_BOOT_WEAK;
extern void *GAMESYSTEM_SetupInitWork(u32 mode, u16 zoneId, void *position,
                                       int direction) BW_BOOT_WEAK;
extern void  PROCSYS_ReplaceGlobal(u32 overlayId, const void *data,
                                    void *procWork) BW_BOOT_WEAK;
extern void  PROCSYS_PushGlobal(u32 overlayId, const void *data,
                                 void *procWork) BW_BOOT_WEAK;

/* The process the game goes on with: in the top process's place, or (a
 * start in the logo screen's place) as the push that is queued. */
static void boot_proc_set(u32 overlayId, const void *data, void *procWork)
{
    if (g_boot_push && PROCSYS_PushGlobal != NULL) {
        PROCSYS_PushGlobal(overlayId, data, procWork);
    } else {
        PROCSYS_ReplaceGlobal(overlayId, data, procWork);
    }
}
extern void *PLAYER_LOCATION_GetData(void *save) BW_BOOT_WEAK;
extern u16   PLAYER_LOCATION_GetZoneId(const void *location) BW_BOOT_WEAK;
extern void  PLAYER_LOCATION_CopyRecord(void *save, void *dest) BW_BOOT_WEAK;

/* The save's location record as the title's Continue reads it (ov088's
 * startmenu.c, SaveLocation: 0x1C bytes). */
typedef struct BootSaveLocation {
    u16 mapId, pad02;
    u32 position[3];
    u32 spawnX, spawnY;
    s16 facing, spawnFlags;
    u32 spare;
} BootSaveLocation;

/*
 * The game's main process: the one the title replaces the global process
 * with when a game starts (ov088's startmenu.c, the ProcData at the ARM9's
 * 0x0209DF9C).
 */
#define BOOT_MAIN_PROC_ADDR 0x0209DF9Cu

/* The init work's modes (ov020's event_mapchange.c). */
#define BOOT_INIT_FIRST    0u
#define BOOT_INIT_CONTINUE 1u

/*
 * The title menu's process: the one the title screen replaces the global
 * process with at START (ov088's startmenu.c, STARTMENU_TitleDemoExit: the
 * ProcData at overlay 88's 0x021BD66C).
 */
#define BOOT_MENU_OVERLAY   88u
#define BOOT_MENU_PROC_ADDR 0x021BD66Cu
extern const char d_021BD66C[] BW_BOOT_WEAK;

/*
 * The game goes on: the global process is replaced with the game's own main
 * process and the init work says which way the game starts, which is what
 * the title's rows do (ov088's startmenu.c).  The main process makes the
 * map change that starts the game.
 */
static void boot_start_game(BootRunNext next)
{
    u32 pos[4] = { 0u, 0u, 0u, 0u };
    const void *main_proc;
    void *init;

    if (PROCSYS_ReplaceGlobal == NULL || GAMESYSTEM_SetupInitWork == NULL) {
        fprintf(stderr, "eidos_boot: the game's own calls are not in this "
                        "build; the game stays at the title\n");
        return;
    }
    if (next == BOOT_RUN_MENU) {
        /* The title menu at once, the way the title screen hands over to it
         * (it made nothing the menu needs: its own heap is gone by then). */
        const void *menu = d_021BD66C != NULL ? (const void *)d_021BD66C
                                              : bw_ds_try_ptr(BOOT_MENU_PROC_ADDR);

        if (menu == NULL) {
            fprintf(stderr, "eidos_boot: the title menu's process is not in "
                            "this build; the game stays at the title\n");
            return;
        }
        boot_proc_set(BOOT_MENU_OVERLAY, menu, NULL);
        return;
    }
    main_proc = bw_ds_try_ptr(BOOT_MAIN_PROC_ADDR);
    if (main_proc == NULL) {
        fprintf(stderr, "eidos_boot: the game's main process is not in this "
                        "build; the game stays at the title\n");
        return;
    }
    if (next == BOOT_RUN_NEW_GAME) {
        /* A new game: the save is reset to the defaults, the way the title's
         * new game row does it before it asks for the name. */
        if (SAVE_CONTROL_Get != NULL && SAVE_CONTROL_ResetToNewGame != NULL) {
            SAVE_CONTROL_ResetToNewGame(SAVE_CONTROL_Get());
        }
        init = GAMESYSTEM_SetupInitWork(BOOT_INIT_FIRST, 0, pos, 0);
    } else if (next == BOOT_RUN_CONTINUE) {
        /* The save that is in force: the game loads it and resumes where it
         * was, the way the title's continue row does it. */
        u16 zone = 0;
        void *save = (SAVE_CONTROL_Get != NULL) ? SAVE_CONTROL_Get() : NULL;

        /* The style chooser's preview: its save is read again first, as the
         * title's own Continue reads the player's (ov088's
         * STARTMENU_BattleFlagProcEnd); a file that does not load leaves
         * the game at the title. */
        if (bw_slots_try_active() && !bw_slots_try_reload()) {
            fprintf(stderr, "eidos_boot: the preview's save did not load; "
                            "the game stays at the title\n");
            return;
        }

        if (save != NULL && PLAYER_LOCATION_CopyRecord != NULL) {
            /* where the player stands: the record's map, position and
             * facing, as the title's Continue gives them */
            BootSaveLocation loc;

            memset(&loc, 0, sizeof loc);
            PLAYER_LOCATION_CopyRecord(save, &loc);
            printf("eidos_boot: the game goes on in zone %u, facing %d\n",
                   (unsigned)loc.mapId, (int)loc.facing);
            fflush(stdout);
            init = GAMESYSTEM_SetupInitWork(BOOT_INIT_CONTINUE, loc.mapId, loc.position, loc.facing);
        } else {
            if (save != NULL && PLAYER_LOCATION_GetData != NULL &&
                PLAYER_LOCATION_GetZoneId != NULL) {
                zone = PLAYER_LOCATION_GetZoneId(PLAYER_LOCATION_GetData(save));
            }
            init = GAMESYSTEM_SetupInitWork(BOOT_INIT_CONTINUE, zone, pos, 0);
        }
    } else {
        return;
    }
    boot_proc_set((u32)-1, main_proc, init);
    /* The port's own run starts here: the run-status panel shows its time,
     * and the mod layer's timer measures it in the port's own ticks
     * (sdk/mod_timer.c).  No mod is in force, so this is the port's run. */
    bw_mod_timer_start();
}

/*
 * The timed input script (sdk/input.h's text format): the run's own input,
 * read again for every run, because the reset clears the input state.
 */
static int boot_load_input_script(void)
{
    char err[160];

    if (g_input_script[0] == '\0') {
        return 0;
    }
    if (bw_input_script_load(g_input_script, err, sizeof(err)) != 0) {
        fprintf(stderr, "eidos_boot: input script: %s\n", err);
        return -1;
    }
    return 0;
}

/*
 * The game's own thread: the game's main loop.  NitroMain does not return.
 */
static void boot_game_thread(void *arg)
{
    (void)arg;
    NitroMain();
    bw_panic("NitroMain returned", 0x02005124u, "its loop has no exit");
}

/*
 * The run's start-up: what the boot does after the reset.  The first run
 * does it once; a restart does it again, after the port's own reset.
 *
 * `own_thread` says where the game runs: on the calling thread (the first
 * run, where the caller is the process's main thread) or on a fresh host
 * thread (a restart, where the caller is the frame thread).
 */
static int boot_run_start(EidosFrameHooks *hooks, int own_thread)
{
    extern void bw_boot_card_state_reset(void);
    bw_boot_card_state_reset();
    bw_input_reset();
    if (boot_load_input_script() != 0) {
        return -1;
    }
    if (getenv("EIDOS_G3D_DUMP") != NULL) {
        g3d_hw_snapshot_enable(1);
    }
    /* The 3D hardware after the reset that cleared its access hook: the
     * game's geometry FIFO and SWAP_BUFFERS land there, and its frame is
     * the 3D layer the screens are composed with (engine/g3d/hw3d.h). */
    g3d_hw_reset();
    printf("eidos_boot: DS address space %s; %lu code pointers the metadata "
           "does not list relocated by the scan\n",
           bw_ds_identity() ? "identity-mapped" : "on movable host blocks",
           (unsigned long)bw_data_scanned());
    fflush(stdout);
    bw_boot_link_init();
    /* The mod layer's own seams: what a restart means to the port, and what
     * the port does with a save that arrived from a peer. */
    bw_mod_restart_set_handler(boot_restart);
    bw_mp_set_boot(boot_mp_boot);
    bw_mod_handoff_set_reload(boot_reload);
    /* The port's own confirmation for a save that arrives: the player is
     * asked before anything is installed. */
#if EIDOS_BOOT_SDL
    eidos_mod_handoff_install_confirm();
#endif
    /* The network service's own settings: the relay the sessions reach, the
     * port the external control listens on, and the name the player is
     * known by.  Without a transport the sessions answer "no relay", and a
     * control port of 0 is the control off (engine/platform/settings.h). */
    bw_mod_net_set_transport(bw_mod_net_tcp_transport());
#if EIDOS_BOOT_SDL
    bw_mod_net_set_relay(eidos_settings_net_relay_in_use(),
                         (u16)eidos_settings_net_relay_port());
    bw_mod_net_set_name(eidos_settings_net_name());
#endif
    {
        /* The session tokens, one per code, next to the save. */
        char tokens[512];

        if (bw_save_path(tokens, sizeof tokens, "net_tokens.txt") == 0) {
            bw_mod_net_set_token_file(tokens);
        }
    }
    /* The save slot in force is the backup chip's file (sdk/save_slots.h);
     * a multiplayer game holds its own file and keeps it. */
    if (bw_slots_try_boot()) {
        /* the style chooser's preview: its save, held in memory, through
         * this restart too */
    } else if (bw_mp_state() == BW_MP_OFF || bw_mp_state() == BW_MP_ENDED) {
        bw_slots_apply();
    }
#if EIDOS_BOOT_SDL
    bw_mod_net_control_set_port(eidos_settings_net_control_port());
#endif
    /* The player's own mod list: the menu's record restores the roots the
     * player chose, before the loader reads them (sdk/mod_menu.h). */
    if (g_only_mod != NULL) {
        /* One mod, chosen on the command line: the player's own list
         * (the menu's record) is neither read nor changed. */
        bw_set_mod_root(g_only_mod);
    } else {
        bw_mod_menu_start();
    }
    /* The content limits the mods raise, and the game's per-id tables
     * extended to match (sdk/limits.h): the mod list is final and the
     * game's data is bound, and nothing of the game has run yet. */
    (void)bw_limits_apply();
    /* The mods: the loader runs after the save is chosen and before the
     * game runs (docs/mod-implementation.md, section 3).  The declared
     * table the manifest's address check asks is registered by
     * bw_boot_link_init, so the loader comes after it.  With no mod root
     * there is nothing to load. */
    {
        int loaded = bw_mod_loader_load_all();

        if (loaded > 0) {
            printf("eidos_boot: %d mod(s) loaded\n", loaded);
            fflush(stdout);
        }
    }
    commit_main_ram();

    eidos_frame_init(&g_run.loop, hooks, g_run.top, g_run.bottom);
    eidos_frame_set_current(&g_run.loop);
    bw_os_set_halt_hook(boot_frame, &g_run);
    if (own_thread) {
        /* The game runs on a fresh host thread.  The thread that calls the
         * game's OS_InitThread is the launcher, and the frame thread is the
         * OS layer's idle thread's host, so it cannot be the launcher at the
         * same time (sdk/os.h). */
        (void)bw_thread_start(boot_game_thread, NULL);
    } else {
        NitroMain();
        bw_panic("NitroMain returned", 0x02005124u, "its loop has no exit");
    }
    return 0;
}

/*
 * The run starts again: the port's own reset, and then the run's start-up
 * again.
 *
 * The reset is the port's own (sdk/port_reset.c): it puts the emulated
 * machine back to power-on and joins the layers again.  It cannot run where
 * the request was made: the mod's handler is on the game's thread, and the
 * reset clears the main RAM the game is running in.  It runs here, on the
 * frame thread, at the frame loop's safe point.
 *
 * What the run does next (a new game, the save in force) is applied at the
 * first frame of the new run.
 */
static void boot_run_restart(void)
{
    printf("eidos_boot: the run starts again (%s) at input frame %u\n",
           g_run_next == BOOT_RUN_NEW_GAME ? "a new game" :
           g_run_next == BOOT_RUN_CONTINUE ? "the save in force" :
           g_run_next == BOOT_RUN_MENU ? "the title menu" :
           "the title screen", (unsigned)bw_input_frame_number());
    fflush(stdout);
    /* The image is in place before the reset, which copies it into main
     * RAM, binds the names and relocates the pointers. */
    bw_data_install(&bw_arm9_data, sizeof(void *) > 4 ? BW_DATA_ALLOW_UNRELOCATED : 0);
    /* The port's own restart: the old game's threads stop (the caller's own
     * thread is left alone, and this is the frame thread), and the machine
     * goes back to power-on (sdk/port_reset.c). */
    bw_port_restart();
    /* No overlay is resident any more: the reset cleared their data. */
    bw_overlay_sdk_init();
    /* The run's start-up again: the input, the 3D, the link-time
     * constants, the mods, the frame loop and the game.  The game runs on a
     * fresh host thread: the frame thread is the OS layer's idle thread's
     * host, and the thread that calls the game's OS_InitThread is the
     * launcher. */
    (void)boot_run_start(&g_run.hooks, 1);
}

static int run_boot(u64 frames, int window, unsigned watchdog)
{
    memset(&g_run, 0, sizeof g_run);
    g_run.limit = frames;

    memset(&g_run.hooks, 0, sizeof g_run.hooks);
    g_run.hooks.step = NULL;          /* the game runs on its own threads */
    g_run.hooks.vblank = boot_vblank;
    g_run.hooks.user = &g_run;
    boot_window(&g_run.hooks, window);

    /* The image is installed before the first reset, which copies it into
     * main RAM, binds the names and relocates the pointers. */
    bw_data_install(&bw_arm9_data, sizeof(void *) > 4 ? BW_DATA_ALLOW_UNRELOCATED : 0);
#if EIDOS_OVERLAYS
    /* The overlays load through FS_LoadOverlay like the game's own:
     * copied into their windows, bound and relocated at each load. */
    bw_ovl_install(&bw_ovl_set_boot, sizeof(void *) > 4 ? BW_DATA_ALLOW_UNRELOCATED : 0);
#endif
    bw_port_reset();
    bw_plat_crash_context(boot_report_context);
    if (watchdog != 0) {
        bw_plat_watchdog(watchdog, boot_progress);
    }
    /* The run's start-up: the input, the 3D, the link-time constants, the
     * mods, the frame loop and the game.  A restart does the same again,
     * after the port's own reset (boot_run_restart). */
    if (boot_run_start(&g_run.hooks, 0) != 0) {
        return 2;
    }
    return 3;
}

#endif /* EIDOS_BOOT */

static void usage(void)
{
    fprintf(stderr, "usage: eidos_boot [--data-root PATH] [--frames N] [--window] "
                    "[--watchdog SECONDS] [--screenshot N[,N...]] "
                    "[--screenshot-dir DIR] [--g3d-trace N] [--keep-going] [--press BUTTONS@FRAME[:HOLD]] "
                    "[--input SCRIPT] [--fullscreen|--borderless|--windowed] "
                    "[--resolution N|auto] [--pixel-scale N|auto] "
                    "[--layout vertical|horizontal|one] [--two-screens] [--swap-screens] "
                    "[--gap N] [--volume N] [--mod FOLDER] [--mod-log FILE]\n");
#if EIDOS_BOOT_SDL
    fputs(bw_setup_usage(), stderr);
#endif
}

/*
 * The launcher's arguments (docs/distribution.md), read before anything
 * else reads the settings or the save folder:
 *
 *   --profile-dir DIR   the profile: saves in DIR/saves, mods in DIR/mods,
 *                       settings in DIR/settings.ini
 *   --join CODE         the title menu joins that game at once
 *   eidos://join/CODE   the same, as an invite link hands it over
 *   --console           a console window for the program's messages (a
 *                       release build on Windows opens none)
 *
 * They are the player's own run, so they do not stop the window from
 * opening (a tool's arguments do).  1 when argv[*i] was one of them (*i is
 * moved past its value).
 */
#if defined(__vita__)
/* POSIX's, which the Vita's newlib declares only outside strict C99 */
int setenv(const char *name, const char *value, int overwrite);
#endif

static void boot_setenv(const char *name, const char *value)
{
#ifdef _WIN32
    (void)_putenv_s(name, value);
#else
    (void)setenv(name, value, 1);
#endif
}

static int boot_launcher_arg(int argc, char **argv, int *i, int apply)
{
    const char *a = argv[*i];

    if (strcmp(a, "--profile-dir") == 0 && *i + 1 < argc) {
        const char *dir = argv[++*i];

        if (apply) {
            char path[1024];

            snprintf(path, sizeof path, "%s/saves", dir);
            (void)bw_plat_mkdirs(path);
            boot_setenv("BW_SAVE_DIR", path);
            snprintf(path, sizeof path, "%s/mods", dir);
            (void)bw_plat_mkdirs(path);
            boot_setenv("EIDOS_MODS_DIR", path);
            snprintf(path, sizeof path, "%s/settings.ini", dir);
            boot_setenv("EIDOS_SETTINGS", path);
        }
        return 1;
    }
    if (strcmp(a, "--join") == 0 && *i + 1 < argc) {
        const char *code = argv[++*i];

        if (apply) {
            bw_mod_title_request_join(code);
        }
        return 1;
    }
    if (strncmp(a, "eidos://join/", 13) == 0) {
        if (apply) {
            bw_mod_title_request_join(a + 13);
        }
        return 1;
    }
    if (strcmp(a, "--console") == 0) {
        if (apply) {
            bw_plat_console_open();
        }
        return 1;
    }
    return 0;
}

#if defined(__vita__)
#include <sys/stat.h>

/* VitaSDK's C library takes these from the program: the heap the game's
 * malloc may use (the DS memory, the decoded assets, SDL) and the main
 * thread's stack (the game's boot runs on it). */
/* (an application has about 365 MiB: the rest is the GPU's memory, which
 * vitaGL or SDL's GXM renderer allocates outside the C heap; 256 MiB left
 * too little for it) */
unsigned int _newlib_heap_size_user = 160u * 1024u * 1024u;
unsigned int sceUserMainThreadStackSize = 4u * 1024u * 1024u;

/* A Vita app has no command line: its arguments are made as the phones'
 * are (host/main.c, mobile_args), from its folder ux0:data/eidos/:
 *
 *   extract/     the extracted game data (docs/installer.md)
 *   profile/     settings and saves
 *   args.txt     more arguments, one a line
 *   env.txt      NAME=value lines for the environment (EIDOS_* switches)
 *   stderr.txt   the program's messages
 *
 * and the one-screen layout on the Vita's screen (docs/consoles.md). */
static char **vita_args(int *argc)
{
    static char *argv[64];
    static char line[1024];
    const char *dir = "ux0:data/eidos/";
    char file[256];
    int n = 0;
    FILE *f;

    mkdir("ux0:data/eidos", 0777);
    mkdir("ux0:data/eidos/profile", 0777);
    snprintf(file, sizeof file, "%sstderr.txt", dir);
    (void)freopen(file, "w", stderr);
    setvbuf(stderr, NULL, _IONBF, 0);
    /* the program's other output goes to the same file: a stdout reopened
     * on its own wrote nothing under Vita3K */
    (void)freopen(file, "a", stdout);
    setvbuf(stdout, NULL, _IONBF, 0);   /* each line reaches the card at once */
    argv[n++] = "eidos";
    argv[n++] = "--data-root";
    argv[n++] = "ux0:data/eidos/extract";
    argv[n++] = "--profile-dir";
    argv[n++] = "ux0:data/eidos/profile";
    argv[n++] = "--layout";
    argv[n++] = "one";
    argv[n++] = "--window";
    setenv("EIDOS_TOUCH_DEVICE", "1", 1);
    /* the first-run screen names the ROM's folder (install/picker.c) */
    setenv("EIDOS_CONSOLE", "Vita", 1);
    setenv("EIDOS_ROM_FOLDER", "ux0:data/eidos/", 1);
    /* The presenter: OpenGL through vitaGL when the shader compiler the
     * player extracts from their own firmware is there (vitaGL looks in
     * these two places, docs/consoles.md); without it SDL's renderer
     * (GXM).  env.txt can still ask for either. */
    {
        FILE *cg = fopen("ur0:data/libshacccg.suprx", "rb");

        if (cg == NULL) {
            cg = fopen("ur0:data/external/libshacccg.suprx", "rb");
        }
        if (cg != NULL) {
            fclose(cg);
            setenv("EIDOS_RENDERER", "gl", 1);
        } else {
            setenv("EIDOS_RENDERER", "sdl", 1);
        }
    }
    snprintf(file, sizeof file, "%senv.txt", dir);
    if ((f = fopen(file, "r")) != NULL) {
        while (fgets(line, sizeof line, f) != NULL) {
            char *eq = strchr(line, '=');

            line[strcspn(line, "\r\n")] = 0;
            if (eq != NULL && line[0] != '#') {
                *eq = 0;
                setenv(line, eq + 1, 1);
            }
        }
        fclose(f);
    }
    snprintf(file, sizeof file, "%sargs.txt", dir);
    if ((f = fopen(file, "r")) != NULL) {
        while (n < 63 && fgets(line, sizeof line, f) != NULL) {
            line[strcspn(line, "\r\n")] = 0;
            if (line[0] != '\0' && line[0] != '#') {
                char *a = (char *)malloc(strlen(line) + 1);

                if (a != NULL) {
                    strcpy(a, line);
                    argv[n++] = a;
                }
            }
        }
        fclose(f);
    }
    argv[n] = NULL;
    *argc = n;
    return argv;
}
#endif

int main(int argc, char **argv)
{
    const char *data_root = NULL;
    int tool_args = 0;
    unsigned long long frames = BOOT_DEFAULT_FRAMES;
    int frames_given = 0;
    int window = 0;
    unsigned long watchdog = 0;
    int i;
#if EIDOS_BOOT_SDL
    BwSetupRequest setup;
    static char setup_data[BW_SETUP_PATH_MAX];

    memset(&setup, 0, sizeof setup);
#endif
#if defined(__vita__)
    argv = vita_args(&argc);
#endif

#if EIDOS_BOOT
    {
        /* The DS address space at its own addresses (docs/boot.md,
         * "Identity mapping"), before anything else maps memory. */
        int status = 0;
        if (bw_ds_identity_prepare(&status)) {
            return status;
        }
    }
#endif
    bw_plat_crash_install();
    bw_plat_sampler_start();
#if EIDOS_BOOT
    bw_plat_crash_code_resolver(boot_resolve_code);
#endif
    /* the launcher's arguments first: they say where the settings are */
    for (i = 1; i < argc; i++) {
        (void)boot_launcher_arg(argc, argv, &i, 1);
    }
#if EIDOS_BOOT_SDL
    eidos_settings_load();      /* settings.ini; the flags below override it */
#endif
    for (i = 1; i < argc; i++) {
        if (boot_launcher_arg(argc, argv, &i, 0)) {
            continue;           /* read above */
        }
        tool_args = 1;
#if EIDOS_BOOT_SDL
        int sr = eidos_settings_parse_arg(argc, argv, &i);

        if (sr < 0) {
            usage();
            return 2;
        }
        if (sr > 0) {
            continue;
        }
        /* --data-root, --rom, --move-data, --reset-data (install/setup.h) */
        sr = bw_setup_parse_arg(&setup, argc, argv, &i);
        if (sr < 0) {
            usage();
            return 2;
        }
        if (sr > 0) {
            continue;
        }
#endif
        if (strcmp(argv[i], "--data-root") == 0 && i + 1 < argc) {
            data_root = argv[++i];
        } else if (strcmp(argv[i], "--mod") == 0 && i + 1 < argc) {
            g_only_mod = argv[++i];
        } else if (strcmp(argv[i], "--mod-log") == 0 && i + 1 < argc) {
            g_mod_log = argv[++i];
        } else if (strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
            char *end = NULL;
            frames = strtoull(argv[++i], &end, 10);
            frames_given = 1;
            if (end == argv[i] || *end != '\0') {
                usage();
                return 2;
            }
        } else if (strcmp(argv[i], "--screenshot") == 0 && i + 1 < argc) {
            if (parse_shots(argv[++i]) != 0) {
                usage();
                return 2;
            }
        } else if (strcmp(argv[i], "--no-hook") == 0 && i + 1 < argc) {
            bw_hook_set(argv[++i], 0);
        } else if (strcmp(argv[i], "--keep-going") == 0) {
            /* A diagnostic run: every stub on the way is reported once and
             * returns instead of stopping the run (sdk/port.h). */
            bw_set_keep_going(1);
        } else if (strcmp(argv[i], "--press") == 0 && i + 1 < argc) {
            if (parse_press(argv[++i]) != 0) {
                usage();
                return 2;
            }
        } else if (strcmp(argv[i], "--input") == 0 && i + 1 < argc) {
            if (read_input_file(argv[++i]) != 0) {
                fprintf(stderr, "eidos_boot: cannot read input script %s\n", argv[i]);
                return 2;
            }
        } else if (strcmp(argv[i], "--g3d-trace") == 0 && i + 1 < argc) {
            char *end = NULL;
            g_trace_frame = strtoull(argv[++i], &end, 10);
            if (end == argv[i] || *end != '\0') {
                usage();
                return 2;
            }
            g_trace_on = 1;
        } else if (strcmp(argv[i], "--screenshot-dir") == 0 && i + 1 < argc) {
            g_shot_dir = argv[++i];
        } else if (strcmp(argv[i], "--profile") == 0) {
            unsigned long every = 300;

            if (i + 1 < argc && argv[i + 1][0] >= '0' && argv[i + 1][0] <= '9') {
                every = strtoul(argv[++i], NULL, 10);
            }
#if EIDOS_BOOT
            eidos_prof_enable((unsigned)every);
#else
            (void)every;    /* the profiler is part of the boot build */
#endif
        } else if (strcmp(argv[i], "--bench") == 0 && i + 1 < argc) {
            /* --bench FILE[:SKIP]: the frames after the first SKIP (default
             * 300) to FILE and FILE.csv at the run's end (engine/frame/prof.h) */
            char path[512];
            char *colon;
            unsigned long skip = 300;

            snprintf(path, sizeof path, "%s", argv[++i]);
            colon = strrchr(path, ':');
            if (colon != NULL && colon[1] >= '0' && colon[1] <= '9') {
                skip = strtoul(colon + 1, NULL, 10);
                *colon = '\0';
            }
#if EIDOS_BOOT
            eidos_bench_start(path, (unsigned)skip);
#else
            (void)skip;
#endif
        } else if (strcmp(argv[i], "--window") == 0) {
            window = 1;
        } else if (strcmp(argv[i], "--watchdog") == 0 && i + 1 < argc) {
            char *end = NULL;
            watchdog = strtoul(argv[++i], &end, 10);
            if (end == argv[i] || *end != '\0' || watchdog > 86400ul) {
                usage();
                return 2;
            }
        } else {
            usage();
            return 2;
        }
    }

    if (g_mod_log != NULL) {
        if (freopen(g_mod_log, "w", stderr) == NULL) {
            fprintf(stdout, "eidos_boot: cannot write the mod log %s\n", g_mod_log);
            return 2;
        }
        setvbuf(stderr, NULL, _IONBF, 0);   /* a tool reads it while the game runs */
    }

    /* Started with no arguments (a double click), or only the launcher's:
     * the player wants to play, so the window opens.  Tests and tools always
     * pass arguments of their own. */
    if (!tool_args) {
        window = 1;
    }
    if (window && !frames_given) {
        frames = ~0ull; /* a window runs until it is closed */
    }
#if EIDOS_BOOT_SDL
    if (setup.data_root != NULL) {
        data_root = setup.data_root;
    }
#endif
    if (data_root == NULL) {
        data_root = getenv("BW_DATA_ROOT");
    }
    if (data_root == NULL || data_root[0] == '\0') {
        data_root = EIDOS_DEFAULT_DATA_ROOT;
    }
#if EIDOS_BOOT_SDL
    /* The data the way every front end finds it (engine/platform/wizard.h):
     * the folder given, else the saved setting or the platform default, and
     * the first-run window that asks for the player's ROM when none of them
     * holds playable data.  A release build compiles in no data root, so a
     * player's first start always comes here. */
    {
        int status = 0;

        setup.data_root = data_root[0] != '\0' ? data_root : NULL;
        setup.no_window = !window;
        if (!eidos_firstrun(&setup, setup_data, sizeof setup_data, &status)) {
            return status;
        }
        data_root = setup_data;
    }
#endif
    if (data_root[0] != '\0') {
        bw_set_data_root(data_root);
    }

#if EIDOS_BOOT
    return run_boot((u64)frames, window, (unsigned)watchdog);
#else
    (void)window;
    (void)frames_given;
    (void)watchdog;
    (void)g_shots;
    (void)g_shot_dir;
    (void)g_input_script;
    printf("eidos_boot: built with EIDOS_BOOT=OFF, so the game's boot is not "
           "linked into this binary (%llu frames requested, data root '%s'). "
           "Reconfigure with -DEIDOS_BOOT=ON; docs/boot.md has the plan.\n",
           frames, bw_data_root());
    return 0;
#endif
}
