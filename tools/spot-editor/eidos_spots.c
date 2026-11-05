/*
 * tools/spot-editor/eidos_spots.c -- the battle spot editor: where each battle of the
 * one-screen battle backdrop stands (docs/battle-backdrop.md).
 *
 * A program of its own: no game runs.  It reads the player's extracted
 * data, builds the map of any place in the spot table (engine/world/
 * bmap_spots.c: every trainer and every patch of wild encounter tiles),
 * places it the way a battle there would be placed and shows it:
 *
 *   left     the place list, the map seen from above (the walk grid, the
 *            trainer and its sight line, the wild area, the safe area, the
 *            player, the two Pokemon, the camera) and the controls
 *   right    top: the battle as the one-screen layout shows it (the game's
 *            battle camera, a Pokemon sprite on each side)
 *            bottom: the place as the field shows it (the zone's field
 *            camera over the player)
 *
 * Every change is written at once to battle_spots.tsv beside the game's
 * settings file, which the game reads over its built-in table; tools/
 * battle_spots.py merge takes it into the source.
 *
 *     eidos_spots [--data-root PATH] [--place ID] [--shot PATH]
 *
 * --shot draws one frame of --place, writes it as a BMP and exits (tests).
 */

#define SDL_MAIN_HANDLED    /* main() is ours */
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#ifdef _WIN32
#include <direct.h>
#include <process.h>
#endif
#include <ctype.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bcam_sim.h"
#include "bmap.h"
#include "bmap_npc.h"
#include "bmap_place.h"
#include "bmap_spots.h"
#include "g2d.h"
#include "hw3d.h"
#include "kit.h"
#include "plat_gl.h"
#include "port.h"
#include "settings.h"
#include "setup.h"
#include "spots_embed.h"
#include "wizard.h"

/* ------------------------------------------------------------------ */
/* Look                                                                */
/* ------------------------------------------------------------------ */

#define C_BG      0x15171Cu
#define C_PANEL   0x1D2027u
#define C_PANEL2  0x252932u
#define C_LINE    0x323743u
#define C_TEXT    0xE6E8ECu
#define C_MUTED   0x9AA1AEu
#define C_ACCENT  0x4A94FFu
#define C_GOOD    0x63DE31u
#define C_WARN    0xFFBD29u
#define C_BAD     0xFF5231u
#define C_FOE     0xFF4F5Eu
#define C_OWN     0x4A94FFu
#define C_CAM     0xFFE45Cu

#define FONT      UI_FONT_SMALL
static int g_fk = 2;               /* text at twice the game font's size (3 on a big window) */
#define FK        g_fk
static int g_u = 4;                /* layout sizes in quarters: 4 normal, 5 on a big window */
#define U(n)      ((n) * g_u / 4)  /* a layout size, scaled with the window */
#define LINE_H    22

/* the battle: slots (battle units), the scale, the cameras */
#define OWN_X     0.5f
#define OWN_Z     7.0f
#define FOE_X     0.3f
#define FOE_Z     (-10.0f)
/* battle units per sprite pixel at scale 1 (draw_battle_view sets each
 * sprite's scale from the game's own sizing: its spot's depth before the
 * command camera, sdk/battle_view.c) */
#define SPRITE_U  (1.0f / 16.2f)
/* The scale each battle position draws its sprite at (ov094's slot scale
 * tables, gBtlvMcssSlotScale*): positions 0 and 1 a single battle's two
 * Pokemon, 2 to 5 a double's, 8 to 13 the trainers. */
static float slot_scale(int pos)
{
    static const float k_single[2] = { 1.0117f, 1.1091f };
    static const float k_double[4] = { 1.1074f, 1.0972f, 0.9009f, 1.1953f };
    static const float k_trainer[6] = { 1.0117f, 1.1875f, 0.9375f, 1.1875f, 0.8125f, 1.1875f };

    return pos >= 0 && pos <= 1 ? k_single[pos] : pos >= 2 && pos <= 5 ? k_double[pos - 2]
           : pos >= 8 && pos <= 13 ? k_trainer[pos - 8] : 1.0f;
}
static const float k_cam[2][2][3] = {
    { { 6.7f, 6.7f, 17.3f }, { 0.0f, 2.6f, 0.0f } },        /* command menu */
    { { 16.7f, 7.7f, 33.3f }, { 6.0f, 4.6f, 16.0f } },      /* intro, send-out */
};
#define BATTLE_FOV_SIN 0.225f
#define BATTLE_FOV_COS 0.974f
#define SPECIES_FOE 504             /* the sprites shown: Patrat ...       */
#define SPECIES_OWN 500             /* ... and Emboar                      */

static const char *k_seasons[4] = { "Spring", "Summer", "Autumn", "Winter" };
static const int k_hours[4] = { 9, 13, 18, 22 };
static const char *k_hour_names[4] = { "Morning", "Day", "Evening", "Night" };

/* ------------------------------------------------------------------ */
/* State                                                               */
/* ------------------------------------------------------------------ */

typedef struct Sprite {
    int w, h;
    u8 *rgba;
} Sprite;

/* A recorded battle camera (EIDOS_CAMERA_RECORD, sdk/battle_view.c): one
 * frame a line, the game's eye and target and the Pokemon's places. */
typedef struct ClipFrame {
    float eye[3], at[3];
    int np;
    int slot[14];                   /* 0..7 the Pokemon, 8..13 the trainers */
    float pos[14][3];
    /* each one as the game draws it (the scripts' own changes): its size
     * (a Pokemon's over its size at rest, BmapRest), its alpha (0..31), its
     * palette faded fade/16 of the way to fade_color, the animation an
     * effect set and the frames since (anim 0: its own) */
    float size[14];
    int alpha[14], fade[14], fade_color[14], anim[14], anim_t[14];
} ClipFrame;

/* A trainer's battle picture animated as the game's McssWork does: its
 * rig (the cells, their animations, the multi-cells) and its multi-cell
 * animation (the NMAR: sequence 0 standing, 1 the throw), each frame made
 * once as it is first shown.  A frame's anchor is the picture's point that
 * stands on the trainer's place: the standing picture's foot (its bottom
 * middle) moved with the multi-cell's origin. */
#define TR_SEQS 4
#define TR_TIME 160                 /* frames kept a sequence (the throw: 100) */
typedef struct TrFrame {
    int w, h;
    u8 *rgba;                       /* NULL: not made yet                  */
    float ax, ay;                   /* the anchor (picture pixels)         */
    int tried;
} TrFrame;
typedef struct TrAnim {
    int key;                        /* the set loaded (archive * 1000 + set), -1 none */
    G2dRig rig;
    G2dNanr nmar;
    u8 *nmar_buf;                   /* the NMAR's bytes (the bank reads them) */
    int have_rig, have_nmar;
    float ox0, oy0, w0, h0;         /* the standing picture: its origin and size */
    TrFrame frame[TR_SEQS][TR_TIME];
} TrAnim;

typedef struct View3d {
    int x, y, w, h;                 /* where the picture goes (window)     */
    int rw, rh;                     /* its render size                     */
    u8 *rgba;
    u8 *front;                      /* the map in front of the Pokemon (rgba, 0 alpha elsewhere) */
    u8 *fronts[16];                 /* (unused: the pictures are GPU textures, tex and ftex) */
    unsigned tex;                   /* the 3D picture (GL texture, kept from the render) */
    unsigned ftex[17];              /* the map in front of each sprite ([16]: of all, the field view's) */
    int has_front[17];
    int nfronts;
    int dirty;
    float eye[3], at[3], fsin, fcos;
} View3d;

enum { F_ALL, F_TRAINERS, F_STORY, F_LEGEND, F_WILD, F_TODO, F_CHANGED, F_COUNT };
static const char *k_filters[F_COUNT] = { "All", "Trainers", "Story", "Legendary", "Wild", "To check", "Changed" };

typedef struct App {
    SDL_Window *win, *glwin;
    EidosGl *gl;
    EidosUiCanvas canvas;
    int W, H;
    /* the place */
    int row;
    float tx, tz;                   /* the test player (map units)         */
    int tf;                         /* and facing (degrees)                */
    /* the list */
    int filter;
    char search[48];
    int *vis, nvis, scroll;
    /* the scenes */
    BmapScene base, placed, field, obase;
    int have_base, have_placed, have_field, have_obase;
    /* the base with what does not move with the spot done (bmap_place_prepare),
     * for the floor height it was done at: each placing copies it */
    BmapScene prep;
    int have_prep, prep_field, prep_has_ref;
    float prep_ref;
    /* the overworld view: its own map, around where it looks */
    unsigned ob_zone;
    float ob_cx, ob_cz;
    int ob_season, ob_hour;
    float ow_tx, ow_tz;             /* where it looks (map units)          */
    float ow_yaw, ow_pitch, ow_dist, ow_fov, ow_lift;
    int ow_reset;                   /* back to the zone's field camera      */
    int ow_build;                   /* build its map again                 */
    u32 battle_gen, field_gen;
    BmapNpc *npcs;                  /* the overworld zone's NPCs            */
    int nnpc;
    unsigned npc_zone;
    unsigned base_zone;
    float base_cx, base_cz;
    int base_season, base_hour;
    u32 gen;
    int need_place, need_build;
    /* views */
    View3d battle[2], over;         /* the command camera's, the intro's   */
    int cam, season, hour;
    Sprite foe, own;
    Sprite tr_foe, tr_own;          /* the trainers' pictures (trainer_load)  */
    int tr_foe_id;                  /* the trainer tr_foe shows, -2 none yet   */
    int show_tr;                    /* the trainers shown in the battle views */
    int hide_mon;                   /* the Pokemon hidden in them (the trainers alone) */
    int gender;                     /* your trainer: 0 the boy, 1 the girl (spot_editor.ini), -1 not read yet */
    int tr_own_gender;              /* the one tr_own shows, -2 none yet       */
    Sprite tr_ally;                 /* the ally's picture, in a battle beside one */
    int tr_ally_id;                 /* the trainer tr_ally shows, -2 none yet  */
    int species[2];                 /* the sprites shown: foe, yours         */
    /* the battle camera played (engine/world/bcam_sim.c): the scene chosen
     * (0 the wild intro, 1 the trainer intro, 2 idle, 3 and on the moves
     * from 1), who attacks, and the frames worked out for it */
    int scene, foe_attacks, play;
    int play_at;                    /* tests: the frame shown while playing, -1 by the clock */
    Uint32 play_t0;
    BcamFrame *sim;
    int nsim, sim_scene, sim_attacker;
    float *sim_rest;                /* each frame's size at rest of slots 0..13 (BmapRest) */
    TrAnim an_foe, an_own;          /* the trainers' pictures, animated     */
    int doubles;                    /* show a double battle (the trainer's)  */
    int dark_single;                /* dark grass shown as a single (the top
                                       bar's switch, kept for every such area) */
    int idle_ticks;                 /* event waits without an event since the last draw */
    Sprite hero[4];                 /* the player, facing up, down, left, right */
    float hero_h;                   /* its height (map units)                */
    /* the map */
    int map_x, map_y, map_s;
    float zoom;                     /* map pixels a tile                   */
    float view_x, view_z;
    int follow;
    /* the mouse */
    int mx, my, down, rdown, mdown, pressed, released, rpressed, wheel, shift, ctrl;
    /* a number typed in (num_field): the field, its text, Enter pressed */
    int edit, edit_done;
    char edit_buf[24];
    int drag;                       /* 1 move, 2 turn, 3 area, 4 pan, 5 list scroll, 6-11 the views, 20 a slider */
    int drag_id, drag_undo, drag_dirty;   /* the slider dragged, kept for Undo, changed (saved on release) */
    int tab;                        /* the view on show: TAB_COMMAND, TAB_INTRO, TAB_FIELD */
    int last_move;                  /* the move scene played last */
    int insp_scroll, insp_h;        /* the inspector: scrolled by, its height */
    int prog_open, prog_scroll;     /* the Progress sheet: shown, scrolled by rows */
    int prog_btn;                   /* the pointer is on the Progress button     */
    int size_custom;                /* the trainer's Size shows its slider (Custom) */
    /* Preview: the game itself, played inside the middle view (spots_embed.c) */
    EidosEmbedShm *emb;             /* the shared picture and input, NULL when none */
    intptr_t emb_proc;              /* the game's process                    */
    u8 *emb_rgba;                   /* its last picture, for blit_rgba        */
    int emb_w, emb_h;
    uint32_t emb_frame;
    int map_left;                   /* the map under the list, not in the inspector */
    float drag_x, drag_z;
    int drag_mx, drag_my;
    float drag_vx, drag_vz;
    /* undo */
    BmapSpot undo[64];
    int undo_row[64], nundo;
    char status[160];
    Uint32 status_at;
    int quit;
} App;

static App A;

static void field_camera(unsigned zone, float *dist, float *pitch, float *yaw, float *fov, float *lift);
static void preview_in_game(int play);
static void preview_stop(void);
static void draw_embed(int x, int y, int w, int h);
static const char *scene_name(int sc);
static void scene_frames(void);
#define SCENE_MOVES 559             /* Black's moves */
#define SCENE_COUNT (3 + SCENE_MOVES)
#define SCENE_MID   SCENE_COUNT     /* a trainer back mid-battle to speak */
#define SCENE_SWITCH (SCENE_COUNT + 1)  /* the player switches Pokemon */
#define SCENE_BALL  (SCENE_COUNT + 2)   /* the player throws a ball (wild) */
static const ClipFrame *play_frame(void);

/* The interface layer's two hooks the kit calls (engine/ui/ui.c is the
 * game's): the editor does its own hit tests and has no DS keys. */
int eidos_ui_hit(EidosUiRect r)
{
    (void)r;
    return 0;
}

void eidos_ui_press(u32 keys, u32 frames)
{
    (void)keys;
    (void)frames;
}

static float clampf(float v, float lo, float hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

static unsigned deg_to_facing(float deg)
{
    double d = fmod((double)deg, 360.0);

    if (d < 0.0) {
        d += 360.0;
    }
    return (unsigned)(d * 65536.0 / 360.0 + 0.5) & 0xFFFFu;
}

static int norm_deg(float d)
{
    int v = (int)lroundf(d) % 360;

    return v < 0 ? v + 360 : v;
}

/* degrees into -180 .. 180 (a turn either way) */
static float wrap_deg(float d)
{
    d = fmodf(d, 360.0f);
    if (d > 180.0f) {
        d -= 360.0f;
    } else if (d <= -180.0f) {
        d += 360.0f;
    }
    return d;
}

static const char *dir_name(int deg)
{
    static const char *n[8] = { "north", "north-west", "west", "south-west",
                                "south", "south-east", "east", "north-east" };

    return n[((deg % 360 + 360) % 360 + 22) / 45 % 8];
}

static void say(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(A.status, sizeof A.status, fmt, ap);
    va_end(ap);
    A.status_at = SDL_GetTicks();
}

static BmapSpot *cur(void)
{
    return bmap_spots_get(A.row);
}

/* ------------------------------------------------------------------ */
/* The table: list, edits, undo, saving                                */
/* ------------------------------------------------------------------ */

static int changed_row(const BmapSpot *r)
{
    BmapSpot d;

    if (bmap_spots_default(r->id, &d) != 0) {
        return 1;
    }
    return r->mode != d.mode || r->sx != d.sx || r->sz != d.sz || r->angle != d.angle || r->ax0 != d.ax0
           || r->az0 != d.az0 || r->ax1 != d.ax1 || r->az1 != d.az1 || r->foe_level != d.foe_level
           || r->foe_dz != d.foe_dz || memcmp(&r->intro, &d.intro, sizeof r->intro) != 0
           || memcmp(&r->cam, &d.cam, sizeof r->cam) != 0 || memcmp(r->trainer_foe, d.trainer_foe, sizeof d.trainer_foe) != 0
           || memcmp(r->trainer_own, d.trainer_own, sizeof d.trainer_own) != 0
           || memcmp(r->trainer_ally, d.trainer_ally, sizeof d.trainer_ally) != 0
           || r->trainer_size != d.trainer_size || r->trainer_own_size != d.trainer_own_size
           || r->trainer_ally_size != d.trainer_ally_size
           || r->pick_angle != d.pick_angle;
}

static int ci_contains(const char *hay, const char *needle)
{
    size_t n = strlen(needle), i, j;

    if (n == 0) {
        return 1;
    }
    for (i = 0; hay[i] != '\0'; i++) {
        for (j = 0; j < n && hay[i + j] != '\0'
                    && tolower((unsigned char)hay[i + j]) == tolower((unsigned char)needle[j]); j++) {
        }
        if (j == n) {
            return 1;
        }
    }
    return 0;
}

static int story_rank(const BmapSpot *r);

/* The list in story order (the Progress sheet's), the table's order within
 * a place; places the sheet does not name last. */
static int by_story(const void *a, const void *b)
{
    int ia = *(const int *)a, ib = *(const int *)b;
    int ra = story_rank(bmap_spots_get(ia)), rb = story_rank(bmap_spots_get(ib));

    return ra != rb ? (ra < rb ? -1 : 1) : (ia < ib ? -1 : ia > ib);
}

static void refilter(void)
{
    int n = bmap_spots_count(), i;

    free(A.vis);
    A.vis = (int *)malloc((size_t)(n > 0 ? n : 1) * sizeof *A.vis);
    A.nvis = 0;
    for (i = 0; A.vis != NULL && i < n; i++) {
        const BmapSpot *r = bmap_spots_get(i);
        int keep = 1, k;

        if (r->view != 0) {
            continue;                   /* the list holds each spot once: its first view */
        }
        switch (A.filter) {
        case F_TRAINERS: keep = r->kind == 'T' && strstr(r->label, "(story)") == NULL && r->trainer_id < 100000; break;
        case F_LEGEND: keep = r->trainer_id >= 100000; break;
        case F_STORY: keep = strstr(r->label, "(story)") != NULL; break;
        case F_WILD: keep = r->kind == 'A'; break;
        case F_TODO: keep = !r->ok; break;
        case F_CHANGED:
            /* any of its views changed, or a view added */
            keep = bmap_spots_view_count(i) > 1;
            for (k = 0; !keep && k < n; k++) {
                const BmapSpot *o = bmap_spots_get(k);

                keep = o->view >= 0 && strcmp(o->id, r->id) == 0 && changed_row(o);
            }
            break;
        default: break;
        }
        if (keep && A.search[0] != '\0') {
            keep = ci_contains(r->label, A.search) || ci_contains(r->id, A.search);
        }
        if (keep) {
            A.vis[A.nvis++] = i;
        }
    }
    if (A.vis != NULL && A.nvis > 1) {
        qsort(A.vis, (size_t)A.nvis, sizeof *A.vis, by_story);
    }
}

/* The row's first view (the list holds those). */
static int first_view(int row)
{
    int b = bmap_spots_view_at(row, 0);

    return b >= 0 ? b : row;
}

/* The place of row `row` in the view list: 0 for its first view. */
static int view_no(int row)
{
    int v, n = bmap_spots_view_count(row);

    for (v = 0; v < n; v++) {
        if (bmap_spots_view_at(row, v) == row) {
            return v;
        }
    }
    return 0;
}

static int vis_pos(int row)
{
    int i;

    row = first_view(row);
    for (i = 0; i < A.nvis; i++) {
        if (A.vis[i] == row) {
            return i;
        }
    }
    return -1;
}

static void sprite_load(Sprite *sp, int species, int back);
static int trainer_lead(int tid);

static void last_place_keep(const BmapSpot *r);
static int g_opened;                    /* the start's own select_row calls done */

static void select_row(int row)
{
    static int had_row;
    BmapSpot *r;
    int same_place;

    if (row < 0 || row >= bmap_spots_count()) {
        return;
    }
    same_place = had_row && first_view(A.row) == first_view(row);
    had_row = 1;
    A.row = row;
    r = cur();
    if (g_opened) {
        last_place_keep(bmap_spots_get(first_view(row)));
    }
    A.tx = r->px;
    A.tz = r->pz;
    A.tf = r->pf;
    A.follow = 1;
    A.need_place = 1;
    A.ow_reset = 1;
    /* the still views show the battle as it runs: the trainers have left
     * (the Trainers chip shows them, and so does a change to their
     * offsets; the played intro and mid-battle scenes show them as the
     * game does); the Trainers chip stays as set from place to place,
     * another view of the same place keeps the Pokemon button too */
    if (!same_place) {
        A.hide_mon = 0;
    }
    A.size_custom = 0;
    A.tr_foe_id = -2;
    /* the battle shown as it is fought there: two Pokemon a side where it
     * is a double one (the trainer's own, two trainers together, the
     * grass's), else one */
    A.doubles = r->doubles && !(r->kind == 'A' && A.dark_single);
    A.battle[0].dirty = A.battle[1].dirty = 1;
    if (r->kind == 'T' && r->trainer_id >= 0 && r->trainer_id < 100000) {
        int lead = trainer_lead(r->trainer_id);

        if (lead > 0 && lead != A.species[0]) {
            A.species[0] = lead;            /* the trainer's own first Pokemon */
            free(A.foe.rgba);
            sprite_load(&A.foe, A.species[0], 0);
        }
    }
    if (r->trainer_id >= 100000 && A.species[0] != r->trainer_id % 1000) {
        A.species[0] = r->trainer_id % 1000;    /* a legendary: the foe is it */
        free(A.foe.rgba);
        sprite_load(&A.foe, A.species[0], 0);
    }
    {
        int p = vis_pos(row), rows = 10;

        if (p >= 0 && (p < A.scroll || p >= A.scroll + rows)) {
            A.scroll = p - rows / 2 < 0 ? 0 : p - rows / 2;
        }
    }
}

/* Before a change to the row: keep it for Ctrl+Z. */
static void remember(void)
{
    if (A.nundo == 64) {
        memmove(&A.undo[0], &A.undo[1], 63 * sizeof A.undo[0]);
        memmove(&A.undo_row[0], &A.undo_row[1], 63 * sizeof A.undo_row[0]);
        A.nundo--;
    }
    A.undo[A.nundo] = *cur();
    A.undo_row[A.nundo] = A.row;
    A.nundo++;
}

static void save_now(void)
{
    if (bmap_spots_save() == 0) {
        say("Saved");
    } else {
        say("Could not save to %s", bmap_spots_user_path());
    }
    A.need_place = 1;
}

static void undo(void)
{
    if (A.nundo == 0) {
        say("Nothing to undo");
        return;
    }
    A.nundo--;
    *bmap_spots_get(A.undo_row[A.nundo]) = A.undo[A.nundo];
    if (A.row != A.undo_row[A.nundo]) {
        select_row(A.undo_row[A.nundo]);
    }
    save_now();
    say("Undone");
}

/* The placement on show into the row as a fixed spot. */
static void set_mode(int m)
{
    BmapSpot *r = cur();

    if (r->mode == m) {
        return;
    }
    remember();
    if (m == BMAP_SPOT_FIXED) {
        if (A.have_placed) {
            r->sx = A.placed.place_x;
            r->sz = A.placed.place_z;
            r->angle = (float)norm_deg(A.placed.place_a * 180.0f / 3.14159265f);
        } else {
            r->sx = A.tx;
            r->sz = A.tz;
            r->angle = (float)A.tf;
        }
    } else if (m == BMAP_SPOT_ZONE) {
        if (r->ax1 < r->ax0) {
            int tx = (int)floorf(A.tx / 16.0f), tz = (int)floorf(A.tz / 16.0f);

            r->ax0 = tx - 2;
            r->az0 = tz - 2;
            r->ax1 = tx + 2;
            r->az1 = tz + 2;
        }
        r->angle = -1.0f;
    }
    r->mode = m;
    save_now();
}

/* ------------------------------------------------------------------ */
/* Scenes and pictures                                                 */
/* ------------------------------------------------------------------ */

static void scene_drop(BmapScene *s, int *have)
{
    if (*have) {
        bmap_free(s);
        *have = 0;
    }
}

/* The spot and the build centre for the row and the test player. */
static void current_spec(BmapPlaceSpec *sp)
{
    bmap_spots_spec(cur(), A.tx, A.tz, deg_to_facing((float)A.tf), sp);
}

static void ensure_base(const BmapPlaceSpec *sp)
{
    const BmapSpot *r = cur();
    float dx = sp->px - A.base_cx, dz = sp->pz - A.base_cz;

    if (A.have_base && A.base_zone == r->zone && dx * dx + dz * dz < 200.0f * 200.0f
        && A.base_season == A.season && A.base_hour == k_hours[A.hour]) {
        return;
    }
    scene_drop(&A.placed, &A.have_placed);
    scene_drop(&A.prep, &A.have_prep);
    scene_drop(&A.base, &A.have_base);
    if (bmap_build(&A.base, r->zone, sp->px, sp->pz, A.season, k_hours[A.hour], 0) != 0) {
        say("Could not build zone %u", r->zone);
        return;
    }
    A.have_base = 1;
    A.base_zone = r->zone;
    A.base_cx = sp->px;
    A.base_cz = sp->pz;
    A.base_season = A.season;
    A.base_hour = k_hours[A.hour];
}

/* The NPCs of the row's zone (the overworld's, the trainers' heights). */
static void load_npcs(unsigned zone)
{
    if (A.npcs == NULL || A.npc_zone != zone) {
        bmap_npc_free(A.npcs, A.nnpc);
        A.npcs = NULL;
        A.nnpc = 0;
        if (bmap_npc_load(zone, &A.npcs, &A.nnpc) != 0) {
            A.npcs = NULL;
            A.nnpc = 0;
        }
        /* Black City and White Forest as they look full: their residents */
        (void)bmap_npc_add_residents(zone, &A.npcs, &A.nnpc);
        A.npc_zone = zone;
    }
}

/* The height the player stands at for the row (map units): a trainer's
 * own, from its header (the player stands on the trainer's floor; a
 * leader's, whoever stands on its tile).  Where
 * floors lie over one another (Nimbasa's walkways, a gym's stages) the
 * ground is the one nearest it (bmap_set_ref), as the game's own placement
 * takes the player's.  0 when it is not known (a wild area). */
static int row_height(const BmapSpot *r, float *y)
{
    int i;

    if (r != NULL && r->kind != 'T') {
        /* a wild area: just under its grass (the game knows the player's
         * height; a plane deep under the map, Dreamyard's, is no floor) */
        if (A.have_base && r->rx1 >= r->rx0
            && bmap_wild_height(&A.base, (float)r->rx0 * 16.0f, (float)r->rz0 * 16.0f, (float)r->rx1 * 16.0f + 16.0f,
                                (float)r->rz1 * 16.0f + 16.0f, y)) {
            *y -= 2.0f;
            return 1;
        }
        return 0;
    }
    if (r == NULL) {
        return 0;
    }
    load_npcs(r->zone);
    for (i = 0; i < A.nnpc; i++) {
        if (A.npcs[i].is_trainer && !A.npcs[i].resident && A.npcs[i].gx == r->tx && A.npcs[i].gz == r->tz) {
            *y = A.npcs[i].y;
            return 1;
        }
    }
    /* a leader: its battle starts from a script, the header does not call
     * it a trainer; whoever stands on the row's tile */
    for (i = 0; i < A.nnpc; i++) {
        if (!A.npcs[i].resident && A.npcs[i].gx == r->tx && A.npcs[i].gz == r->tz) {
            *y = A.npcs[i].y;
            return 1;
        }
    }
    /* a story scene's fight (SCENE_PLACES, tools/battle_spots.py): the
     * opponent walked there, no header stands on the tile; the floor of the
     * nearest character within 8 tiles (the lowest surface can be a plane
     * under the map, Accumula's) */
    {
        int best = -1, bd = 8 * 8 + 1;

        for (i = 0; i < A.nnpc; i++) {
            int dx = A.npcs[i].gx - r->tx, dz = A.npcs[i].gz - r->tz;

            if (!A.npcs[i].resident && dx * dx + dz * dz < bd) {
                bd = dx * dx + dz * dz;
                best = i;
            }
        }
        if (best >= 0) {
            *y = A.npcs[best].y;
            return 1;
        }
    }
    return 0;
}

/* A point of the map in the placed scene's battle space (bmap_place.c). */
static void map_to_battle(const BmapScene *s, float mx, float my, float mz, float out[3])
{
    float ca = cosf(s->place_a), sa = sinf(s->place_a), k = s->place_k;
    float ux = mx - s->place_x, uz = mz - s->place_z;
    float gy = s->foe_dy != 0.0f || s->place_k == 0.0f ? s->place_g[0] : 0.5f * (s->place_g[0] + s->place_g[1]);

    out[0] = k * (ca * ux - sa * uz);
    out[2] = k * (sa * ux + ca * uz) + OWN_Z;
    out[1] = 0.4f + k * (my - gy);
}

static void place_all(void)
{
    BmapPlaceSpec sp;
    const BmapSpot *r = cur();

    if (r == NULL) {
        return;
    }
    current_spec(&sp);
    ensure_base(&sp);
    if (!A.have_base) {
        return;
    }
    scene_drop(&A.placed, &A.have_placed);
    {
        float y = 0.0f;
        int has = row_height(r, &y);

        if (has) {
            bmap_set_ref(&A.base, y);
        } else {
            bmap_clear_ref(&A.base);
        }
        /* the grass and the cards lie where the floor height says: done
         * again only when it (or the base) changes, not each drag step */
        if (A.have_prep && (A.prep_field != sp.field || A.prep_has_ref != has || (has && A.prep_ref != y))) {
            scene_drop(&A.prep, &A.have_prep);
        }
        if (!A.have_prep && bmap_copy(&A.prep, &A.base) == 0) {
            bmap_place_prepare(&A.prep, sp.field);
            A.have_prep = 1;
            A.prep_field = sp.field;
            A.prep_has_ref = has;
            A.prep_ref = y;
        }
    }
    if (bmap_copy(&A.placed, A.have_prep ? &A.prep : &A.base) == 0) {
        bmap_place_spec(&A.placed, &sp);
        A.have_placed = 1;
        A.battle_gen = ++A.gen;
    }
    if (A.ow_reset) {
        float dist, pitch, yaw, fov, lift;

        field_camera(r->zone, &dist, &pitch, &yaw, &fov, &lift);
        A.ow_tx = A.tx;
        A.ow_tz = A.tz;
        A.ow_yaw = yaw;
        A.ow_pitch = pitch;
        A.ow_dist = dist;
        A.ow_fov = fov;
        A.ow_lift = lift;
        A.ow_reset = 0;
        A.ow_build = 1;
    }
    if (!A.have_obase || A.ob_zone != r->zone || A.ob_season != A.season || A.ob_hour != k_hours[A.hour]) {
        A.ow_build = 1;
    }
    A.battle[0].dirty = A.battle[1].dirty = A.over.dirty = 1;
    A.need_place = 0;
}

/* The overworld's own map, built around where the view looks; placed north
 * up at that point with the field's grass (the renderer works in battle
 * units).  Built again when the view moves off it. */
static void build_field(void)
{
    const BmapSpot *r = cur();
    BmapPlaceSpec fs;

    if (r == NULL) {
        return;
    }
    scene_drop(&A.field, &A.have_field);
    scene_drop(&A.obase, &A.have_obase);
    load_npcs(r->zone);
    if (bmap_build(&A.obase, r->zone, A.ow_tx, A.ow_tz, A.season, k_hours[A.hour], 0) != 0) {
        return;
    }
    A.have_obase = 1;
    {
        /* the same floor as the battle's (the trainer's, the area's grass) */
        float y;

        if (row_height(r, &y)) {
            bmap_set_ref(&A.obase, y);
        }
    }
    if (A.nnpc > 0) {
        int i;

        (void)bmap_npc_add_models(&A.obase, A.npcs, A.nnpc);   /* the NPCs drawn as 3D models */
        for (i = 0; i < A.nnpc; i++) {
            if (A.npcs[i].resident) {
                /* a resident stands on the ground of its tile */
                A.npcs[i].y = bmap_ground_y(&A.obase, A.npcs[i].x, A.npcs[i].z);
            }
        }
    }
    A.ob_zone = r->zone;
    A.ob_cx = A.ow_tx;
    A.ob_cz = A.ow_tz;
    A.ob_season = A.season;
    A.ob_hour = k_hours[A.hour];
    if (bmap_copy(&A.field, &A.obase) == 0) {
        memset(&fs, 0, sizeof fs);
        fs.px = A.ow_tx;
        fs.pz = A.ow_tz;
        fs.facing = 0;
        fs.reach = -1;
        fs.field = 1;
        bmap_place_spec(&A.field, &fs);
        A.have_field = 1;
        A.field_gen = ++A.gen;
    }
    A.ow_build = 0;
    A.over.dirty = 1;
}

/* The zone's field camera (a/0/6/0 member 0, 0x2C bytes a setting; the zone
 * header's camera id, field_camera.c): distance, pitch, field of view and
 * the target's shift above the player. */
static void field_camera(unsigned zone, float *dist, float *pitch, float *yaw, float *fov, float *lift)
{
    static u8 *zh, *cams;
    static long nzh, ncams;
    BwArcFile f;

    *dist = 237.0f;
    *pitch = 53.2f;
    *yaw = 0.0f;
    *fov = 20.0f;
    *lift = 4.0f;
    if (zh == NULL && bw_arc_open(&f, "a/0/1/2") == 0) {
        nzh = bw_arc_member_size(&f, 0);
        zh = nzh > 0 ? (u8 *)malloc((size_t)nzh) : NULL;
        if (zh != NULL && bw_arc_read_member(&f, 0, zh, (u32)nzh) != nzh) {
            free(zh);
            zh = NULL;
        }
        bw_arc_close(&f);
    }
    if (cams == NULL && bw_arc_open(&f, "a/0/6/0") == 0) {
        ncams = bw_arc_member_size(&f, 0);
        cams = ncams > 0 ? (u8 *)malloc((size_t)ncams) : NULL;
        if (cams != NULL && bw_arc_read_member(&f, 0, cams, (u32)ncams) != ncams) {
            free(cams);
            cams = NULL;
        }
        bw_arc_close(&f);
    }
    if (zh != NULL && cams != NULL && (long)(zone * 48u + 0x1Eu) <= nzh) {
        unsigned h = (unsigned)zh[zone * 48u + 0x1C] | ((unsigned)zh[zone * 48u + 0x1D] << 8);
        unsigned id = (h >> 9) & 0x7Fu;
        const u8 *c = cams + id * 0x2Cu;
        int32_t v[11];

        if ((long)(id * 0x2Cu + 0x2Cu) <= ncams) {
            memcpy(v, c, sizeof v);
            *dist = (float)v[0];
            *pitch = (float)(v[1] & 0xFFFF) * 360.0f / 65536.0f;
            *yaw = (float)(v[2] & 0xFFFF) * 360.0f / 65536.0f;
            *fov = (float)(((unsigned)c[0x12] | ((unsigned)c[0x13] << 8))) * 360.0f / 65536.0f;
            *lift = v[7] != 0 ? (float)v[9] / 4096.0f : 4.0f;
        }
    }
}

/* The view's picture, and the map that stands in front of the sprites at
 * `poke` (where, and the height of each one's box): all in one picture, or
 * with `each` one picture a sprite, so that a sprite drawn later (nearer)
 * covers the map in front of a farther one and not the other way round. */
static void render_view(View3d *v, const BmapScene *s, u32 gen, const float (*poke)[4], int npoke, int each)
{
    G3dSnapshot snap;
    int i;

    if (v->rw <= 0 || v->rh <= 0) {
        return;
    }
    memset(&snap, 0, sizeof snap);
    snap.clear_depth = 0x7FFF;          /* the far plane, as the game clears */
    snap.backdrop.valid = 1;
    memcpy(snap.backdrop.eye, v->eye, sizeof v->eye);
    memcpy(snap.backdrop.target, v->at, sizeof v->at);
    snap.backdrop.up[1] = 1.0f;
    snap.backdrop.fovy_sin = v->fsin;
    snap.backdrop.fovy_cos = v->fcos;
    snap.backdrop.aspect = (float)v->rw / (float)v->rh;
    snap.backdrop.znear = 1.0f;
    snap.backdrop.zfar = 512.0f;
    snap.backdrop.scene = s;
    snap.backdrop.scene_gen = gen;
    memcpy(snap.backdrop.sky, s->clear, 3);
    /* each picture stays on the GPU: kept in a texture, drawn at the frame's
     * end (gpu_flush), never read back */
    if (eidos_gl_render3d(A.gl, &snap, v->rw, v->rh) == 0) {
        v->tex = eidos_gl_view_keep3d(A.gl, v->tex, v->rw, v->rh);
    }
    /* the map in front of the Pokemon, alone over a clear background: laid
     * over the sprites, as the game's second pass draws it over its own */
    memset(v->has_front, 0, sizeof v->has_front);
    v->nfronts = 0;
    if (each && poke != NULL) {
        for (i = 0; i < npoke && i < 16; i++) {
            memcpy(snap.backdrop.poke[0], poke[i], 4 * sizeof(float));
            snap.backdrop.n_poke = 1;
            snap.backdrop.front_only = 1;
            if (eidos_gl_render3d(A.gl, &snap, v->rw, v->rh) == 0) {
                v->ftex[i] = eidos_gl_view_keep3d(A.gl, v->ftex[i], v->rw, v->rh);
                v->has_front[i] = 1;
            }
        }
        v->nfronts = i;
    } else if (poke != NULL && npoke > 0) {
        for (i = 0; i < npoke && i < G3D_BACKDROP_SPRITES; i++) {
            memcpy(snap.backdrop.poke[i], poke[i], 4 * sizeof(float));    /* where, and its box's height */
        }
        snap.backdrop.n_poke = (u32)(npoke < G3D_BACKDROP_SPRITES ? npoke : G3D_BACKDROP_SPRITES);
        snap.backdrop.front_only = 1;
        if (eidos_gl_render3d(A.gl, &snap, v->rw, v->rh) == 0) {
            v->ftex[16] = eidos_gl_view_keep3d(A.gl, v->ftex[16], v->rw, v->rh);
            v->has_front[16] = 1;
        }
    }
    v->dirty = 0;
}

/* Project a battle-space point through a view: window pixels, and the
 * pixels a battle unit spans there.  Returns 0 behind the camera. */
static int project(const View3d *v, const float p[3], float *sx, float *sy, float *unit)
{
    float f[3], rt[3], u[3], d[3], len, x, y, z, t;
    int i;

    for (i = 0; i < 3; i++) {
        f[i] = v->at[i] - v->eye[i];
        d[i] = p[i] - v->eye[i];
    }
    len = sqrtf(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]);
    for (i = 0; i < 3; i++) {
        f[i] /= len;
    }
    rt[0] = -f[2];
    rt[1] = 0.0f;
    rt[2] = f[0];
    len = sqrtf(rt[0] * rt[0] + rt[2] * rt[2]);
    rt[0] /= len;
    rt[2] /= len;
    u[0] = rt[1] * f[2] - rt[2] * f[1];
    u[1] = rt[2] * f[0] - rt[0] * f[2];
    u[2] = rt[0] * f[1] - rt[1] * f[0];
    z = d[0] * f[0] + d[1] * f[1] + d[2] * f[2];
    if (z < 0.5f) {
        return 0;
    }
    x = d[0] * rt[0] + d[1] * rt[1] + d[2] * rt[2];
    y = d[0] * u[0] + d[1] * u[1] + d[2] * u[2];
    t = v->fsin / v->fcos;
    *unit = (float)v->h * 0.5f / (z * t);
    *sx = (float)v->x + (float)v->w * 0.5f + x * *unit;
    *sy = (float)v->y + (float)v->h * 0.5f - y * *unit;
    return 1;
}

/* A trainer's first pose, as the game's cell animation shows it: each node of
 * the first multi-cell group names one cell by its animation's first frame,
 * and only that cell's pieces are drawn.  A trainer's picture is one bitmap in
 * its sheet; a cell's pieces are cut from it where they lie in the cell (the
 * sheet position of the piece at the cell's top left, the others by their
 * offsets from it), not by their tile numbers (g2d_compose_pose reads those
 * as tile numbers of a 2D sheet and so draws a second head from other
 * pixels).  Canvas as g2d_compose_pose's: two pixels of margin. */
static int trainer_compose(const G2dRig *rig, int mc, u32 t, G2dPose *out)
{
    enum { MAXU = 64, CANVAS = 320, ORG = 160 };
    const G2dMcGroup *grp = &rig->nmcr.groups[mc >= 0 && mc < (int)rig->nmcr.num_multi_cell ? mc : 0];
    const G2dNcgr *cg = &rig->ncgr;
    struct { const G2dOam *oam; int ax, ay, sx, sy; } un[MAXU];
    int n = 0, minx = 1 << 20, miny = 1 << 20, maxx = -(1 << 20), maxy = -(1 << 20), pr, k, i;
    u8 *cv;

    memset(out, 0, sizeof *out);
    if (!rig->has_nanr || cg->pixels == NULL || cg->tile_num_x == 0 || cg->tile_num_x == G2D_NCGR_DIM_SENTINEL) {
        return -1;
    }
    for (i = 0; i < grp->num_nodes; i++) {
        const G2dMcNode *nd = &grp->nodes[i];
        const G2dCell *cell;
        const G2dOam *top = NULL;
        G2dFrame fr;
        u32 j;

        /* the node's own cell animation at time t (MCSS: they run with
         * the multi-cell animation's clock) */
        if (g2d_nanr_frame_data(&rig->nanr, nd->anim_seq_idx, g2d_nanr_frame_at_time(&rig->nanr, nd->anim_seq_idx, t),
                                &fr) != 0
            || fr.cell_idx >= rig->ncer.num_cells) {
            continue;
        }
        cell = &rig->ncer.cells[fr.cell_idx];
        for (j = 0; j < cell->num_oam; j++) {
            const G2dOam *o = &cell->oams[j];

            if (top == NULL || o->y < top->y || (o->y == top->y && o->x < top->x)) {
                top = o;
            }
        }
        for (j = 0; j < cell->num_oam && n < MAXU; j++) {
            const G2dOam *o = &cell->oams[j];

            un[n].oam = o;
            un[n].ax = nd->x + fr.px + o->x;
            un[n].ay = nd->y + fr.py + o->y;
            un[n].sx = (int)(top->tile_idx % cg->tile_num_x) * 8 + o->x - top->x;
            un[n].sy = (int)(top->tile_idx / cg->tile_num_x) * 8 + o->y - top->y;
            minx = un[n].ax < minx ? un[n].ax : minx;
            miny = un[n].ay < miny ? un[n].ay : miny;
            maxx = un[n].ax + (int)o->w > maxx ? un[n].ax + (int)o->w : maxx;
            maxy = un[n].ay + (int)o->h > maxy ? un[n].ay + (int)o->h : maxy;
            n++;
        }
    }
    if (n == 0 || maxx - minx > CANVAS - 8 || maxy - miny > CANVAS - 8) {
        return -1;
    }
    cv = (u8 *)calloc((size_t)CANVAS * CANVAS, 1);
    if (cv == NULL) {
        return -1;
    }
    for (pr = 3; pr >= 0; pr--) {                   /* the front pieces last, the first emitted on top */
        for (k = n - 1; k >= 0; k--) {
            const G2dOam *o = un[k].oam;
            u32 lx, ly, off;

            if (o->priority != (u16)pr) {
                continue;
            }
            off = o->pal * 16u + 16u <= rig->palette.count ? (u32)o->pal * 16u : 0u;
            for (ly = 0; ly < o->h; ly++) {
                for (lx = 0; lx < o->w; lx++) {
                    u32 fx = o->hflip ? o->w - 1u - lx : lx, fy = o->vflip ? o->h - 1u - ly : ly;
                    int sx = un[k].sx + (int)fx, sy = un[k].sy + (int)fy;
                    u8 v;

                    if (sx < 0 || sy < 0 || sx >= (int)cg->width || sy >= (int)cg->height) {
                        continue;
                    }
                    v = cg->pixels[(u32)sy * cg->width + (u32)sx];
                    if (v != 0) {
                        cv[(un[k].ay - miny + 2 + (int)ly) * CANVAS + (un[k].ax - minx + 2 + (int)lx)] = (u8)(v + off);
                    }
                }
            }
        }
    }
    out->width = (u32)(maxx - minx) + 4u;
    out->height = (u32)(maxy - miny) + 4u;
    out->ox = 2 - minx;
    out->oy = 2 - miny;
    out->indices = (u8 *)malloc((size_t)out->width * out->height);
    if (out->indices == NULL) {
        free(cv);
        return -1;
    }
    for (i = 0; i < (int)out->height; i++) {
        memcpy(out->indices + (size_t)i * out->width, cv + (size_t)i * CANVAS, out->width);
    }
    free(cv);
    return 0;
}

/* The first pose of a loaded rig as a picture. */
static void pose_sprite(Sprite *sp, G2dRig *rig, int trainer)
{
    G2dPose pose;
    u32 i;

    if (rig->nmcr.num_multi_cell > 0
        && (trainer ? trainer_compose(rig, 0, 0, &pose)
                    : g2d_compose_pose(&rig->ncgr, &rig->ncer, rig->has_nanr ? &rig->nanr : NULL, &rig->nmcr.groups[0], 0,
                                       rig->palette.count, &pose)) == 0) {
        sp->w = (int)pose.width;
        sp->h = (int)pose.height;
        sp->rgba = (u8 *)malloc((size_t)sp->w * (size_t)sp->h * 4u);
        if (sp->rgba != NULL) {
            g2d_pose_to_rgba(&pose, &rig->palette, sp->rgba);
            for (i = 0; i < (u32)(sp->w * sp->h); i++) {
                sp->rgba[i * 4 + 3] = pose.indices[i] == 0 ? 0 : 255;
            }
        }
        g2d_pose_free(&pose);
    }
}

static void sprite_load(Sprite *sp, int species, int back)
{
    BwArcFile arc;
    G2dRig rig;
    u32 start = (u32)species * 20u + (back ? 9u : 0u);     /* front set, back set, palettes */

    memset(sp, 0, sizeof *sp);
    if (bw_arc_open(&arc, "a/0/0/4") != 0) {
        return;
    }
    if (g2d_rig_load(&rig, &arc, start, 9) == 0) {
        (void)g2d_rig_load_palette(&rig, &arc, (u32)species * 20u + 18u);
        pose_sprite(sp, &rig, 0);
        g2d_rig_free(&rig);
    }
    bw_arc_close(&arc);
}

/* Byte `at` of a trainer's record in a/0/9/2 (1 its class, 2 its battle:
 * 0 single, 1 double, 2 triple, 3 rotation).  -1 when unknown. */
static int trainer_byte(int tid, int at)
{
    BwArcFile arc;
    u8 *rec;
    long n;
    int cls = -1;

    if (tid < 0 || bw_arc_open(&arc, "a/0/9/2") != 0) {
        return -1;
    }
    n = bw_arc_member_size(&arc, (u32)tid);
    rec = n > at ? (u8 *)malloc((size_t)n) : NULL;
    if (rec != NULL && bw_arc_read_member(&arc, (u32)tid, rec, (u32)n) > at) {
        cls = rec[at];
    }
    free(rec);
    bw_arc_close(&arc);
    return cls;
}

static int trainer_class(int tid)
{
    return trainer_byte(tid, 1);
}

/* A trainer's first Pokemon's species (the party file a/0/9/3: a member is
 * u8 the strength, u8 the gender and ability, u16 the level, u16 the
 * species, u16 the form, then an item and four moves when the trainer's
 * record says so), or 0. */
static int trainer_lead(int tid)
{
    BwArcFile arc;
    u8 *rec;
    long n;
    int species = 0;

    if (tid < 0 || bw_arc_open(&arc, "a/0/9/3") != 0) {
        return 0;
    }
    /* the whole member: a read asks for all of it */
    n = bw_arc_member_size(&arc, (u32)tid);
    rec = n >= 8 ? (u8 *)malloc((size_t)n) : NULL;
    if (rec != NULL && bw_arc_read_member(&arc, (u32)tid, rec, (u32)n) >= 8) {
        species = (rec[4] | rec[5] << 8) & 0x3FF;
    }
    free(rec);
    bw_arc_close(&arc);
    return species >= 1 && species <= 649 ? species : 0;
}

/* Whether a trainer has a line mid-battle (the trainer messages' table
 * a/0/9/0: u16 trainer, u16 kind; kinds 17 to 24 are the battle's own: its
 * last Pokemon, a Pokemon low, the first hit), so comes back on the field to
 * say it (effect 63).  Cached for the last trainer asked. */
static int trainer_speaks_mid(int tid)
{
    static int last = -1, answer;
    BwArcFile arc;
    long n;
    u8 *t;
    long i;

    if (tid == last) {
        return answer;
    }
    last = tid;
    answer = 0;
    if (tid < 0 || bw_arc_open(&arc, "a/0/9/0") != 0) {
        return 0;
    }
    n = bw_arc_member_size(&arc, 0);
    t = n >= 4 ? (u8 *)malloc((size_t)n) : NULL;
    if (t != NULL && bw_arc_read_member(&arc, 0, t, (u32)n) >= 4) {
        for (i = 0; i + 4 <= n; i += 4) {
            int who = t[i] | t[i + 1] << 8, kind = t[i + 2] | t[i + 3] << 8;

            if (who == tid && kind >= 17 && kind <= 24) {
                answer = 1;
                break;
            }
        }
    }
    free(t);
    bw_arc_close(&arc);
    return answer;
}

/* A trainer's battle picture: the foe's front (a/0/7/2) by its class, through
 * the ARM9's graphics index table (the identity when it cannot be read), or
 * the player's own back (a/0/7/3; tid is its set: 0 the boy, 1 the girl).  Eight files a set; the last is
 * the palette. */
static void trainer_load(Sprite *sp, int tid, int back)
{
    BwArcFile arc;
    G2dRig rig;
    int cls = back ? tid : trainer_class(tid), set;
    const u8 *tab = (const u8 *)bw_arm9_rodata(0x020A023Cu, 0x6Au);

    memset(sp, 0, sizeof *sp);
    if (cls < 0) {
        return;
    }
    set = back ? cls : cls > 0x69 ? 0 : tab != NULL ? tab[cls] : cls;
    if (bw_arc_open(&arc, back ? "a/0/7/3" : "a/0/7/2") != 0) {
        return;
    }
    if (g2d_rig_load(&rig, &arc, (u32)set * 8u, 8) == 0) {
        pose_sprite(sp, &rig, 1);
        g2d_rig_free(&rig);
    }
    bw_arc_close(&arc);
}

/* A trainer's animated picture (TrAnim): the same set trainer_load shows,
 * its rig kept, and its multi-cell animation (the set's RAMN member). */
static void tranim_free(TrAnim *an)
{
    int q, t;

    for (q = 0; q < TR_SEQS; q++) {
        for (t = 0; t < TR_TIME; t++) {
            free(an->frame[q][t].rgba);
        }
    }
    if (an->have_rig) {
        g2d_rig_free(&an->rig);
    }
    if (an->have_nmar) {
        g2d_nanr_free(&an->nmar);
    }
    free(an->nmar_buf);
    memset(an, 0, sizeof *an);
    an->key = -1;
}

static void tranim_load(TrAnim *an, int tid, int back)
{
    BwArcFile arc;
    int cls = back ? tid : trainer_class(tid), set, key;
    const u8 *tab = (const u8 *)bw_arm9_rodata(0x020A023Cu, 0x6Au);
    G2dPose pose;
    u32 m;

    set = cls < 0 ? -1 : back ? cls : cls > 0x69 ? 0 : tab != NULL ? tab[cls] : cls;
    key = set < 0 ? -1 : (back ? 1000 : 0) + set;
    if (key == an->key) {
        return;
    }
    tranim_free(an);
    if (set < 0 || bw_arc_open(&arc, back ? "a/0/7/3" : "a/0/7/2") != 0) {
        return;
    }
    an->key = key;
    if (g2d_rig_load(&an->rig, &arc, (u32)set * 8u, 8) == 0) {
        an->have_rig = 1;
        for (m = (u32)set * 8u; m < (u32)set * 8u + 8u && !an->have_nmar; m++) {
            long n = bw_arc_member_size(&arc, m);
            u8 *raw = n > 0 ? (u8 *)malloc((size_t)n) : NULL, *dec = NULL;
            u32 dn = 0;

            if (raw != NULL && bw_arc_read_member(&arc, m, raw, (u32)n) == n) {
                dec = g2d_decompress(raw, (u32)n, &dn);
                if (dec == NULL) {
                    dec = raw;              /* stored plain */
                    dn = (u32)n;
                    raw = NULL;
                }
                if (dn >= 4 && memcmp(dec, "RAMN", 4) == 0 && g2d_nmar_parse(&an->nmar, dec, dn) == 0) {
                    an->have_nmar = 1;
                    an->nmar_buf = dec;         /* the bank reads its bytes */
                    dec = NULL;
                }
            }
            free(raw);
            free(dec);
        }
        /* the standing picture (as trainer_load's): its origin and size */
        if (trainer_compose(&an->rig, 0, 0, &pose) == 0) {
            an->ox0 = (float)pose.ox;
            an->oy0 = (float)pose.oy;
            an->w0 = (float)pose.width;
            an->h0 = (float)pose.height;
            g2d_pose_free(&pose);
        } else {
            an->w0 = 0.0f;
        }
    }
    bw_arc_close(&arc);
}

/* The trainer's picture `t` frames into animation `seq` (NULL when there
 * is none: the standing picture shows). */
static const TrFrame *tranim_frame(TrAnim *an, int seq, int t)
{
    TrFrame *f;
    G2dFrame mf;
    G2dPose pose;
    int mc = 0;
    float px = 0.0f, py = 0.0f;
    u32 i;

    if (!an->have_rig || an->w0 <= 0.0f || seq < 0 || seq >= TR_SEQS) {
        return NULL;
    }
    if (!an->have_nmar || seq >= an->nmar.num_sequences) {
        seq = 0;                        /* BTLV_MCSS_SetAnimIndexIfValid: kept as it is */
    }
    t = t < 0 ? 0 : t >= TR_TIME ? TR_TIME - 1 : t;
    f = &an->frame[seq][t];
    if (f->rgba != NULL || f->tried) {
        return f->rgba != NULL ? f : NULL;
    }
    f->tried = 1;
    if (an->have_nmar
        && g2d_nanr_frame_data(&an->nmar, (u32)seq, g2d_nanr_frame_at_time(&an->nmar, (u32)seq, (u32)t), &mf) == 0) {
        mc = mf.cell_idx;
        px = (float)mf.px;
        py = (float)mf.py;
    }
    if (trainer_compose(&an->rig, mc, (u32)t, &pose) != 0) {
        return NULL;
    }
    f->w = (int)pose.width;
    f->h = (int)pose.height;
    f->rgba = (u8 *)malloc((size_t)f->w * (size_t)f->h * 4u);
    if (f->rgba != NULL) {
        g2d_pose_to_rgba(&pose, &an->rig.palette, f->rgba);
        for (i = 0; i < (u32)(f->w * f->h); i++) {
            f->rgba[i * 4 + 3] = pose.indices[i] == 0 ? 0 : 255;
        }
        /* the multi-cell's move (MCSS_Draw: the pose's offset) carries the
         * picture with it: the foot is the standing picture's, at the same
         * place from the origin */
        f->ax = (float)pose.ox + px + (an->w0 * 0.5f - an->ox0);
        f->ay = (float)pose.oy + py + (an->h0 - an->oy0);
    }
    g2d_pose_free(&pose);
    return f->rgba != NULL ? f : NULL;
}

static void dir_of(const char *path, char *out, size_t n);

/* The player's gender for your trainer: the editor's own settings file beside
 * the game's (spot_editor.ini, "player=0|1"), else the player's save (the
 * trainer card's gender byte), else the boy. */
static void gender_path(char *out, size_t n)
{
    char dir[600];

    dir_of(eidos_settings_path(), dir, sizeof dir);
    snprintf(out, n, "%s/spot_editor.ini", dir);
}

static int gender_read(void)
{
    char path[700];
    const char *saves = getenv("BW_SAVE_DIR"), *app = getenv("APPDATA");
    FILE *f;
    int g = -1, c;

    gender_path(path, sizeof path);
    f = fopen(path, "r");
    if (f != NULL) {
        char line[128];

        while (fgets(line, sizeof line, f) != NULL) {
            if (sscanf(line, "player=%d", &c) == 1 && (c == 0 || c == 1)) {
                g = c;
            }
        }
        fclose(f);
    }
    if (g < 0) {
        if (saves != NULL && saves[0] != '\0') {
            snprintf(path, sizeof path, "%s/pokeblack.sav", saves);
        } else {
            snprintf(path, sizeof path, "%s/eidos/saves/pokeblack.sav", app != NULL ? app : ".");
        }
        f = fopen(path, "rb");
        if (f != NULL) {
            c = fseek(f, 0x19421L, SEEK_SET) == 0 ? fgetc(f) : -1;     /* the trainer card's gender byte */
            g = c == 1 ? 1 : 0;
            fclose(f);
        }
    }
    return g == 1 ? 1 : 0;
}

/* The place shown last (spot_editor.ini, "place=ID"): the editor opens on
 * it.  Empty when none was kept. */
static char g_last_place[48];

static void last_place_read(void)
{
    char path[700], line[128];
    FILE *f;

    g_last_place[0] = '\0';
    gender_path(path, sizeof path);
    f = fopen(path, "r");
    if (f == NULL) {
        return;
    }
    while (fgets(line, sizeof line, f) != NULL) {
        if (strncmp(line, "place=", 6) == 0) {
            snprintf(g_last_place, sizeof g_last_place, "%.47s", line + 6);
            g_last_place[strcspn(g_last_place, "\r\n")] = '\0';
        }
    }
    fclose(f);
}

static void gender_write(void)
{
    char path[700];
    FILE *f;

    gender_path(path, sizeof path);
    f = fopen(path, "w");
    if (f != NULL) {
        if (A.gender >= 0) {
            fprintf(f, "player=%d\n", A.gender);
        }
        if (g_last_place[0] != '\0') {
            fprintf(f, "place=%s\n", g_last_place);
        }
        fclose(f);
    }
}

/* Keep row r's place as the one to open on next time. */
static void last_place_keep(const BmapSpot *r)
{
    if (r == NULL || strcmp(r->id, g_last_place) == 0) {
        return;
    }
    snprintf(g_last_place, sizeof g_last_place, "%s", r->id);
    if (A.gender < 0) {
        A.gender = gender_read();       /* written back with it */
    }
    gender_write();
}

/* The ally's picture: its back, from a/0/7/3 (0 and 1 the player's, 2
 * Professor Juniper, 3 Cheren, 4 and 5 the player's throwing), by its class
 * ("Pokemon Trainer", 37: Cheren); another class shows its front. */
static void ally_load(Sprite *sp, int tid)
{
    if (tid < 0) {
        memset(sp, 0, sizeof *sp);
    } else if (trainer_class(tid) == 37) {
        trainer_load(sp, 3, 1);
    } else {
        trainer_load(sp, tid, 0);
    }
}

/* The trainers' pictures for the row on show. */
static void trainer_refresh(void)
{
    const BmapSpot *r = cur();
    int tid = r != NULL && r->kind == 'T' ? r->trainer_id : -1;

    if (A.gender < 0) {
        A.gender = gender_read();
    }
    if (A.tr_foe_id != tid) {
        free(A.tr_foe.rgba);
        trainer_load(&A.tr_foe, tid, 0);
        A.tr_foe_id = tid;
        tranim_load(&A.an_foe, tid, 0);
    }
    if (A.tr_own_gender != A.gender) {
        free(A.tr_own.rgba);
        trainer_load(&A.tr_own, A.gender, 1);
        A.tr_own_gender = A.gender;
        tranim_load(&A.an_own, A.gender, 1);
    }
    tid = r != NULL ? r->ally_id : -1;
    if (A.tr_ally_id != tid) {
        free(A.tr_ally.rgba);
        ally_load(&A.tr_ally, tid);
        A.tr_ally_id = tid;
    }
}

/* ------------------------------------------------------------------ */
/* Drawing helpers                                                     */
/* ------------------------------------------------------------------ */

static EidosUiCtx C;

static int text(int x, int y, u32 col, const char *fmt, ...)
{
    char buf[256];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    return ui_text(&C, x, y, buf, col, -1, FONT, FK);
}

static int text_w(const char *s)
{
    return ui_text_w(s, FONT) * FK;
}

static int hover(int x, int y, int w, int h)
{
    return A.mx >= x && A.my >= y && A.mx < x + w && A.my < y + h;
}

static void outline(int x, int y, int w, int h, u32 col)
{
    ui_fill(&C, x, y, w, 1, col, 255);
    ui_fill(&C, x, y + h - 1, w, 1, col, 255);
    ui_fill(&C, x, y, 1, h, col, 255);
    ui_fill(&C, x + w - 1, y, 1, h, col, 255);
}

/* A button; returns 1 when clicked. */
static int button(int x, int y, int w, int h, const char *label, int on)
{
    int hv = hover(x, y, w, h);
    u32 bg = on ? C_ACCENT : hv ? C_LINE : C_PANEL2;

    ui_fill(&C, x, y, w, h, bg, 255);
    outline(x, y, w, h, on ? C_ACCENT : C_LINE);
    text(x + (w - text_w(label)) / 2, y + (h - 10 * FK) / 2, on ? 0x0B1220u : C_TEXT, "%s", label);
    return hv && A.released && A.drag == 0;
}

/* A number shown at (x, y) that a click lets the user type over: `label`
 * before it, `shown` the number as text.  Returns 1 with the number typed
 * in `out` once Enter is pressed (or the user clicks elsewhere); Esc
 * gives up.  `id` tells the fields apart (any number but 0). */
static int num_field(int id, int x, int y, const char *label, const char *shown, float *out)
{
    int lw = label[0] != '\0' ? text_w(label) + 8 : 0, w = text_w(shown) + 10;

    if (A.edit == id) {
        int bw = text_w(A.edit_buf) + U(20) > U(70) ? text_w(A.edit_buf) + U(20) : U(70);
        char *end = NULL;
        double v;

        if (lw > 0) {
            text(x, y, C_MUTED, "%s", label);
        }
        ui_fill(&C, x + lw - 4, y - 4, bw, 10 * FK + 6, 0x111318u, 255);
        outline(x + lw - 4, y - 4, bw, 10 * FK + 6, C_ACCENT);
        text(x + lw, y, C_TEXT, "%s_", A.edit_buf);
        if (A.edit_done || (A.released && !hover(x + lw - 4, y - 4, bw, 10 * FK + 6))) {
            A.edit = 0;
            A.edit_done = 0;
            v = strtod(A.edit_buf, &end);
            if (end != A.edit_buf) {
                *out = (float)v;
                return 1;
            }
        }
        return 0;
    }
    text(x, y, C_TEXT, "%s%s%s", label, lw > 0 ? " " : "", shown);
    if (hover(x + lw - 4, y - 4, w, 10 * FK + 6)) {
        outline(x + lw - 4, y - 4, w, 10 * FK + 6, C_LINE);
        if (A.released && A.drag == 0) {
            A.edit = id;
            A.edit_done = 0;
            snprintf(A.edit_buf, sizeof A.edit_buf, "%s", shown[0] == '+' ? shown + 1 : shown);
        }
    }
    return 0;
}

static void remember(void);
static void save_now(void);

/* `src` (sw x sh) stretched over dw x dh at (dx, dy), its clear pixels
 * skipped; `box` (the source's opaque box, opaque_box) or NULL limits the
 * work to the part that can draw. */
static void blit_rgba_box(int dx, int dy, const u8 *src, int sw, int sh, int dw, int dh, const int *box)
{
    EidosUiCanvas *cv = &A.canvas;
    static int *col;
    static int col_n;
    int x, y, x0 = 0, x1 = dw, y0 = 0, y1 = dh;

    if (src == NULL || sw <= 0 || sh <= 0 || dw <= 0 || dh <= 0) {
        return;
    }
    if (box != NULL) {
        if (box[0] >= box[2] || box[1] >= box[3]) {
            return;
        }
        /* the destination pixels whose source falls in the box */
        x0 = box[0] * dw / sw;
        x1 = (box[2] * dw + sw - 1) / sw;
        y0 = box[1] * dh / sh;
        y1 = (box[3] * dh + sh - 1) / sh;
        if (x1 > dw) x1 = dw;
        if (y1 > dh) y1 = dh;
    }
    if (dx + x0 < 0) x0 = -dx;
    if (dx + x1 > cv->w) x1 = cv->w - dx;
    if (dy + y0 < 0) y0 = -dy;
    if (dy + y1 > cv->h) y1 = cv->h - dy;
    if (x0 >= x1 || y0 >= y1) {
        return;
    }
    if (col_n < dw) {
        free(col);
        col = (int *)malloc((size_t)dw * sizeof(int));
        col_n = col != NULL ? dw : 0;
        if (col == NULL) {
            return;
        }
    }
    for (x = x0; x < x1; x++) {
        col[x] = x * sw / dw;
    }
    for (y = y0; y < y1; y++) {
        const u8 *srow = src + (size_t)(y * sh / dh) * (size_t)sw * 4u;
        u8 *d = cv->rgba + ((size_t)(dy + y) * (size_t)cv->w + (size_t)(dx + x0)) * 4u;

        for (x = x0; x < x1; x++, d += 4) {
            const u8 *s = srow + (size_t)col[x] * 4u;

            if (s[3] != 0) {
                d[0] = s[0];
                d[1] = s[1];
                d[2] = s[2];
                d[3] = 255;
            }
        }
    }
}

static void blit_rgba(int dx, int dy, const u8 *src, int sw, int sh, int dw, int dh)
{
    blit_rgba_box(dx, dy, src, sw, sh, dw, dh, NULL);
}

static void ring(float x, float y, float r, u32 col)
{
    int ir = (int)r;

    ui_ellipse(&C, (int)x - ir, (int)y - ir, ir * 2 + 1, ir * 2 + 1, UI_NONE, col, 255);
    ui_ellipse(&C, (int)x - ir + 1, (int)y - ir + 1, ir * 2 - 1, ir * 2 - 1, UI_NONE, col, 255);
}

/* ------------------------------------------------------------------ */
/* The map                                                             */
/* ------------------------------------------------------------------ */

static void map_to_px(float mx, float mz, float *x, float *y)
{
    *x = (float)A.map_x + (float)A.map_s * 0.5f + (mx - A.view_x) * A.zoom / 16.0f;
    *y = (float)A.map_y + (float)A.map_s * 0.5f + (mz - A.view_z) * A.zoom / 16.0f;
}

static void px_to_map(int x, int y, float *mx, float *mz)
{
    *mx = A.view_x + ((float)x - (float)A.map_x - (float)A.map_s * 0.5f) * 16.0f / A.zoom;
    *mz = A.view_z + ((float)y - (float)A.map_y - (float)A.map_s * 0.5f) * 16.0f / A.zoom;
}

static u32 tile_colour(const BmapScene *b, float x, float z)
{
    int t = bmap_tile(b, x, z), e = bmap_tile_encounter(b, x, z);

    switch (t) {
    case BMAP_TILE_OPEN: return e ? 0x5C9A4Eu : 0xB3A98Cu;
    case BMAP_TILE_WATER: return e ? 0x3C7CB4u : 0x5A7896u;
    case BMAP_TILE_BLOCKED: return 0x43454Cu;
    default: return 0x101114u;
    }
}

static void battle_point_map(float bx, float bz, float *mx, float *mz)
{
    const BmapScene *s = &A.placed;
    float ca = cosf(s->place_a), sa = sinf(s->place_a), k = s->place_k > 0.0f ? s->place_k : BMAP_PLACE_K;
    float x = bx / k, z = (bz - OWN_Z) / k;

    *mx = s->place_x + ca * x + sa * z;
    *mz = s->place_z - sa * x + ca * z;
}

static void tile_box(int tx0, int tz0, int tx1, int tz1, u32 col)
{
    float x0, y0, x1, y1;

    map_to_px((float)tx0 * 16.0f, (float)tz0 * 16.0f, &x0, &y0);
    map_to_px((float)(tx1 + 1) * 16.0f, (float)(tz1 + 1) * 16.0f, &x1, &y1);
    outline((int)x0, (int)y0, (int)(x1 - x0), (int)(y1 - y0), col);
    outline((int)x0 + 1, (int)y0 + 1, (int)(x1 - x0) - 2, (int)(y1 - y0) - 2, col);
}

static void arrow(float x, float y, int deg, float len, u32 col)
{
    float a = (float)deg * 3.14159265f / 180.0f;
    float ex = x - sinf(a) * len, ey = y - cosf(a) * len;

    ui_line(&C, (int)x, (int)y, (int)ex, (int)ey, col, 255);
    ui_line(&C, (int)x + 1, (int)y, (int)ex + 1, (int)ey, col, 255);
}

static void draw_map(void)
{
    const BmapSpot *r = cur();
    EidosUiRect saved;
    int x, y, step;
    float mx, mz;

    ui_fill(&C, A.map_x, A.map_y, A.map_s, A.map_s, 0x0C0D10u, 255);
    if (r == NULL) {
        return;
    }
    if (A.follow && A.drag == 0) {      /* still while dragging: the spot follows the pointer */
        A.view_x = r->mode == BMAP_SPOT_FIXED ? r->sx : A.tx;
        A.view_z = r->mode == BMAP_SPOT_FIXED ? r->sz : A.tz;
    }
    ui_push(&C, 0, 0, &saved);
    ui_clip(&C, A.map_x, A.map_y, A.map_s, A.map_s);
    step = (int)A.zoom;
    if (A.have_base && A.base_zone == r->zone) {
        float ox, oy;
        int t0x, t0z, n = A.map_s / (step > 0 ? step : 1) + 2;

        px_to_map(A.map_x, A.map_y, &mx, &mz);
        t0x = (int)floorf(mx / 16.0f);
        t0z = (int)floorf(mz / 16.0f);
        for (y = 0; y <= n; y++) {
            for (x = 0; x <= n; x++) {
                float cx = ((float)(t0x + x) + 0.5f) * 16.0f, cz = ((float)(t0z + y) + 0.5f) * 16.0f;

                map_to_px((float)(t0x + x) * 16.0f, (float)(t0z + y) * 16.0f, &ox, &oy);
                ui_fill(&C, (int)ox, (int)oy, step - (step > 5), step - (step > 5), tile_colour(&A.base, cx, cz),
                        255);
            }
        }
    }
    if (r->kind == 'T') {
        static const int dv[4][2] = { { 0, -1 }, { 0, 1 }, { -1, 0 }, { 1, 0 } };
        int d = r->dir >= 0 && r->dir < 4 ? r->dir : 1, i;
        float ox, oy;

        for (i = 1; i <= (r->range > 0 ? r->range : 1); i++) {
            tile_box(r->tx + dv[d][0] * i, r->tz + dv[d][1] * i, r->tx + dv[d][0] * i, r->tz + dv[d][1] * i,
                     0xFF9AA2u);
        }
        map_to_px((float)r->tx * 16.0f, (float)r->tz * 16.0f, &ox, &oy);
        ui_fill(&C, (int)ox, (int)oy, step, step, C_FOE, 255);
    } else {
        tile_box(r->rx0, r->rz0, r->rx1, r->rz1, C_WARN);
    }
    if (r->mode == BMAP_SPOT_ZONE) {
        tile_box(r->ax0, r->az0, r->ax1, r->az1, C_GOOD);
    }
    if (A.drag == 3) {
        float x0, y0;

        map_to_px(A.drag_x, A.drag_z, &x0, &y0);
        outline((int)fminf(x0, (float)A.mx), (int)fminf(y0, (float)A.my), (int)fabsf((float)A.mx - x0),
                (int)fabsf((float)A.my - y0), C_GOOD);
    }
    if (A.have_placed) {
        static const float box[4][2] = { { -17, -13 }, { 17, -13 }, { 17, 34 }, { -17, 34 } };
        float px[4], py[4], ox, oy, fx, fy, ex, ey, ax, az;
        int i;

        for (i = 0; i < 4; i++) {
            battle_point_map(box[i][0], box[i][1], &ax, &az);
            map_to_px(ax, az, &px[i], &py[i]);
        }
        for (i = 0; i < 4; i++) {
            ui_line(&C, (int)px[i], (int)py[i], (int)px[(i + 1) % 4], (int)py[(i + 1) % 4], 0x8C7E3Au, 255);
        }
        battle_point_map(OWN_X, OWN_Z, &ax, &az);
        map_to_px(ax, az, &ox, &oy);
        battle_point_map(FOE_X, FOE_Z - A.placed.foe_dz, &ax, &az);
        map_to_px(ax, az, &fx, &fy);
        for (i = 0; i < 2; i++) {       /* both cameras */
            float ce[3];

            memcpy(ce, k_cam[i][0], sizeof ce);
            {
                float ct[3];

                memcpy(ct, k_cam[i][1], sizeof ct);
                bmap_battle_camera(ce, ct, &A.placed);
            }
            battle_point_map(ce[0], ce[2], &ax, &az);
            map_to_px(ax, az, &ex, &ey);
            ui_line(&C, (int)ex, (int)ey, (int)ox, (int)oy, C_CAM, i == 0 ? 200 : 110);
            ui_line(&C, (int)ex, (int)ey, (int)fx, (int)fy, C_CAM, i == 0 ? 200 : 110);
            ui_ellipse(&C, (int)ex - 5, (int)ey - 5, 11, 11, i == 0 ? C_CAM : UI_NONE, C_CAM, 255);
        }
        ui_line(&C, (int)ox, (int)oy, (int)fx, (int)fy, C_TEXT, 120);
        ring(ox, oy, 9.0f, C_OWN);
        ring(fx, fy, 9.0f, C_FOE);
    }
    {
        /* the row's other views: where each puts your Pokemon, and its angle
         * (a click on one picks it) */
        int nv = bmap_spots_view_count(A.row), v;

        for (v = 0; nv > 1 && v < nv; v++) {
            int k = bmap_spots_view_at(A.row, v);
            const BmapSpot *q = bmap_spots_get(k);
            float qx, qy;

            if (q->mode != BMAP_SPOT_FIXED || k == A.row) {
                continue;
            }
            map_to_px(q->sx, q->sz, &qx, &qy);
            ring(qx, qy, 9.0f, 0x7C94C8u);
            text((int)qx + 12, (int)qy - 8, 0xA9BBE0u, "%d", v + 1);
            if (q->angle >= 0.0f) {
                arrow(qx, qy, (int)q->angle, 18.0f, 0x7C94C8u);
            }
        }
        if (nv > 1) {
            float qx, qy;

            map_to_px(r->mode == BMAP_SPOT_FIXED ? r->sx : A.tx, r->mode == BMAP_SPOT_FIXED ? r->sz : A.tz, &qx, &qy);
            text((int)qx + 12, (int)qy - 22, C_OWN, "%d", view_no(A.row) + 1);
        }
    }
    {
        float ox, oy;

        map_to_px(A.tx, A.tz, &ox, &oy);
        ui_ellipse(&C, (int)ox - 5, (int)oy - 5, 11, 11, C_TEXT, UI_INK, 255);
        arrow(ox, oy, A.tf, 16.0f, C_TEXT);
    }
    ui_pop(&C, 0, 0, &saved);
    outline(A.map_x, A.map_y, A.map_s, A.map_s, C_LINE);
}

/* Mouse on the map: left drag moves (the fixed spot, else the player),
 * right drag turns toward the pointer, Shift+left drag draws the safe area,
 * middle drag pans, the wheel zooms. */
static void map_input(void)
{
    BmapSpot *r = cur();
    float mx, mz;
    int inside = hover(A.map_x, A.map_y, A.map_s, A.map_s);

    if (r == NULL) {
        return;
    }
    px_to_map(A.mx, A.my, &mx, &mz);
    if (inside && A.wheel != 0) {
        A.zoom = clampf(A.zoom * (A.wheel > 0 ? 1.25f : 0.8f), 3.0f, 40.0f);
    }
    if (inside && A.pressed && A.drag == 0) {
        int nv = bmap_spots_view_count(A.row), v, hit = -1;

        for (v = 0; v < nv; v++) {
            int k = bmap_spots_view_at(A.row, v);
            const BmapSpot *q = bmap_spots_get(k);
            float qx, qy;

            map_to_px(q->sx, q->sz, &qx, &qy);
            if (k != A.row && q->mode == BMAP_SPOT_FIXED && fabsf(qx - (float)A.mx) < 11.0f
                && fabsf(qy - (float)A.my) < 11.0f) {
                hit = k;
            }
        }
        if (hit >= 0) {
            select_row(hit);
            A.follow = 0;
            return;
        }
        {
            float px, py;

            map_to_px(A.tx, A.tz, &px, &py);
            if (fabsf(px - (float)A.mx) < 11.0f && fabsf(py - (float)A.my) < 11.0f) {
                A.drag = 5;             /* the player marker */
                return;
            }
        }
        if (A.shift) {
            A.drag = 3;
            A.drag_x = mx;
            A.drag_z = mz;
        } else {
            A.drag = 1;
            remember();
        }
    }
    if (inside && A.rpressed && A.drag == 0) {
        float px, py;

        map_to_px(A.tx, A.tz, &px, &py);
        if (fabsf(px - (float)A.mx) < 11.0f && fabsf(py - (float)A.my) < 11.0f) {
            A.drag = 6;                 /* turn the player marker */
        } else {
            A.drag = 2;
            remember();
        }
    }
    if (inside && A.mdown && A.drag == 0) {
        A.drag = 4;
        A.follow = 0;
        A.drag_mx = A.mx;
        A.drag_my = A.my;
        A.drag_vx = A.view_x;
        A.drag_vz = A.view_z;
    }
    switch (A.drag) {
    case 1:
        if (A.down) {
            float qx = floorf(mx / 4.0f) * 4.0f + 2.0f, qz = floorf(mz / 4.0f) * 4.0f + 2.0f;

            if (r->mode == BMAP_SPOT_FIXED && !A.ctrl) {
                if (r->sx != qx || r->sz != qz) {
                    r->sx = qx;
                    r->sz = qz;
                    A.need_place = 1;
                }
            } else if (A.tx != qx || A.tz != qz) {
                A.tx = qx;
                A.tz = qz;
                A.need_place = 1;
            }
        } else {
            A.drag = 0;
            if (r->mode == BMAP_SPOT_FIXED && !A.ctrl) {
                save_now();
            }
        }
        break;
    case 2:
        if (A.rdown) {
            float cx = r->mode == BMAP_SPOT_FIXED ? r->sx : A.tx, cz = r->mode == BMAP_SPOT_FIXED ? r->sz : A.tz;
            int deg = norm_deg((float)(atan2(-(double)(mx - cx), -(double)(mz - cz)) * 180.0 / 3.14159265358979));

            deg = A.shift ? (deg + 22) / 45 * 45 % 360 : (deg + 2) / 5 * 5 % 360;
            if (r->mode == BMAP_SPOT_AUTO || (r->mode == BMAP_SPOT_ZONE && r->angle < 0.0f)) {
                if (A.tf != deg) {
                    A.tf = deg;
                    A.need_place = 1;
                }
            } else if ((int)r->angle != deg) {
                r->angle = (float)deg;
                A.need_place = 1;
            }
        } else {
            A.drag = 0;
            if (r->mode != BMAP_SPOT_AUTO) {
                save_now();
            }
        }
        break;
    case 3:
        if (!A.down) {
            remember();
            r->ax0 = (int)floorf(fminf(A.drag_x, mx) / 16.0f);
            r->az0 = (int)floorf(fminf(A.drag_z, mz) / 16.0f);
            r->ax1 = (int)floorf(fmaxf(A.drag_x, mx) / 16.0f);
            r->az1 = (int)floorf(fmaxf(A.drag_z, mz) / 16.0f);
            if (r->mode != BMAP_SPOT_ZONE) {
                r->mode = BMAP_SPOT_ZONE;
                r->angle = -1.0f;
            }
            A.drag = 0;
            save_now();
        }
        break;
    case 5:
        /* the player: where the battle starts (the view the game takes
         * from there; Preview shows the view on show) */
        if (A.down) {
            float qx = floorf(mx / 4.0f) * 4.0f + 2.0f, qz = floorf(mz / 4.0f) * 4.0f + 2.0f;

            if (A.tx != qx || A.tz != qz) {
                A.tx = qx;
                A.tz = qz;
                A.need_place = 1;
            }
        } else {
            A.drag = 0;
        }
        break;
    case 6:
        if (A.rdown) {
            int deg = norm_deg((float)(atan2(-(double)(mx - A.tx), -(double)(mz - A.tz)) * 180.0 / 3.14159265358979));

            deg = A.shift ? (deg + 22) / 45 * 45 % 360 : (deg + 2) / 5 * 5 % 360;
            if (A.tf != deg) {
                A.tf = deg;
                A.need_place = 1;
            }
        } else {
            A.drag = 0;
        }
        break;
    case 4:
        if (A.mdown) {
            A.view_x = A.drag_vx - (float)(A.mx - A.drag_mx) * 16.0f / A.zoom;
            A.view_z = A.drag_vz - (float)(A.my - A.drag_my) * 16.0f / A.zoom;
        } else {
            A.drag = 0;
        }
        break;
    default:
        break;
    }
}

/* Mouse on the overworld picture: left drag moves over the ground, right
 * drag turns the camera around the point it looks at, the wheel moves it
 * closer or further.  Moving off the map that is loaded loads it there. */
/* The views follow a setting changed while dragging (the placement itself
 * is redone on release). */
static void intro_live(const BmapSpot *r)
{
    if (A.have_placed) {
        A.placed.intro = r->intro;
        bmap_place_view_live(&A.placed, &r->cam, r->trainer_foe, r->trainer_own);
        A.battle[0].dirty = 1;
        A.battle[1].dirty = 1;
    }
}

/* A camera view: a drag turns the camera (across) and raises it (up and
 * down), a right drag slides it (across) and tilts it, the wheel zooms;
 * Ctrl for fine moves.  vi 1 is the intro camera's view (its settings), 0
 * the command camera's (the camera, for every battle camera). */
static void view_input(int vi)
{
    View3d *v = &A.battle[vi];
    BmapSpot *r = cur();
    BmapIntro *in;
    int inside = hover(v->x, v->y, v->w, v->h), id = vi == 1 ? 8 : 10;
    float k = A.ctrl ? 0.2f : 1.0f;

    if (r == NULL || (vi == 0 && A.play)) {
        return;
    }
    in = vi == 1 ? &r->intro : &r->cam;
    if (inside && A.wheel != 0) {
        remember();
        in->zoom = clampf(in->zoom * powf(A.ctrl ? 1.01f : 1.06f, (float)A.wheel), 0.05f, 50.0f);
        save_now();
    }
    if (inside && A.drag == 0 && (A.pressed || A.rpressed)) {
        remember();
        A.drag = A.pressed ? id : id + 1;
        A.drag_mx = A.mx;
        A.drag_my = A.my;
        A.drag_x = A.pressed ? in->yaw : in->side;
        A.drag_z = A.pressed ? in->lift : in->tilt;
    }
    if (A.drag == id) {
        if (A.down) {
            in->yaw = wrap_deg(A.drag_x + (float)(A.mx - A.drag_mx) * 0.25f * k);
            in->lift = A.drag_z - (float)(A.my - A.drag_my) * 0.03f * k;
            intro_live(r);
        } else {
            A.drag = 0;
            save_now();
        }
    } else if (A.drag == id + 1) {
        if (A.rdown) {
            in->side = A.drag_x + (float)(A.mx - A.drag_mx) * 0.03f * k;
            in->tilt = clampf(A.drag_z - (float)(A.my - A.drag_my) * 0.15f * k, -85.0f, 85.0f);
            intro_live(r);
        } else {
            A.drag = 0;
            save_now();
        }
    }
}

static void ow_input(void)
{
    View3d *v = &A.over;
    int inside = hover(v->x, v->y, v->w, v->h);

    if (inside && A.wheel != 0) {
        A.ow_dist = clampf(A.ow_dist * (A.wheel > 0 ? 0.85f : 1.18f), 40.0f, 1600.0f);
        v->dirty = 1;
    }
    if (inside && A.drag == 0 && (A.pressed || A.rpressed)) {
        A.drag = A.pressed ? 6 : 7;
        A.drag_mx = A.mx;
        A.drag_my = A.my;
        A.drag_vx = A.ow_tx;
        A.drag_vz = A.ow_tz;
        A.drag_x = A.ow_yaw;
        A.drag_z = A.ow_pitch;
    }
    if (A.drag == 6) {
        if (A.down) {
            /* the ground under the pointer stays under it, about */
            float per = 2.0f * A.ow_dist * tanf(A.ow_fov * 3.14159265f / 180.0f) / (float)(v->h > 0 ? v->h : 1);
            float dx = (float)(A.mx - A.drag_mx) * per, dy = (float)(A.my - A.drag_my) * per;
            float yw = A.ow_yaw * 3.14159265f / 180.0f, sp = sinf(A.ow_pitch * 3.14159265f / 180.0f);

            dy /= sp > 0.3f ? sp : 0.3f;
            A.ow_tx = A.drag_vx - dx * cosf(yw) - dy * sinf(yw);
            A.ow_tz = A.drag_vz + dx * sinf(yw) - dy * cosf(yw);
            v->dirty = 1;
        } else {
            float ddx = A.ow_tx - A.ob_cx, ddz = A.ow_tz - A.ob_cz;

            A.drag = 0;
            if (ddx * ddx + ddz * ddz > 240.0f * 240.0f) {
                A.ow_build = 1;
            }
        }
    } else if (A.drag == 7) {
        if (A.rdown) {
            A.ow_yaw = A.drag_x - (float)(A.mx - A.drag_mx) * 0.4f;
            A.ow_pitch = clampf(A.drag_z + (float)(A.my - A.drag_my) * 0.3f, 8.0f, 88.0f);
            v->dirty = 1;
        } else {
            A.drag = 0;
        }
    }
}

/* Turn the battle around: the same line seen from its other end.  The
 * foe's place becomes the own Pokemon's, the angle turns half a circle. */
static void reverse_battle(void)
{
    BmapSpot *r = cur();
    float fx, fz;

    if (r == NULL || !A.have_placed) {
        return;
    }
    remember();
    battle_point_map(FOE_X, FOE_Z - A.placed.foe_dz, &fx, &fz);
    if (r->mode == BMAP_SPOT_ZONE) {
        int base = r->angle >= 0.0f ? (int)r->angle : A.tf;

        r->angle = (float)norm_deg((float)base + 180.0f);
        A.tx = fx;
        A.tz = fz;
    } else {
        r->mode = BMAP_SPOT_FIXED;
        r->sx = floorf(fx / 4.0f) * 4.0f + 2.0f;
        r->sz = floorf(fz / 4.0f) * 4.0f + 2.0f;
        r->angle = (float)norm_deg(A.placed.place_a * 180.0f / 3.14159265f + 180.0f);
    }
    save_now();
    say("Reversed");
}

/* ------------------------------------------------------------------ */
/* The views                                                           */
/* ------------------------------------------------------------------ */

static void fit_view(View3d *v, int x, int y, int w, int h)
{
    int vw = w, vh = w * 9 / 16;

    if (vh > h) {
        vh = h;
        vw = h * 16 / 9;
    }
    v->x = x + (w - vw) / 2;
    v->y = y + (h - vh) / 2;
    if (v->w != vw || v->h != vh) {
        v->w = vw;
        v->h = vh;
        v->rw = vw;
        v->rh = vh;
        v->dirty = 1;
    }
}

/* The GPU draws of a frame, in order, under the interface canvas: the views'
 * 3D pictures, the sprites and the map in front of them.  The canvas has a
 * clear hole where a view shows (gpu_hole). */
typedef struct GpuOp {
    unsigned tex;
    int mode, x, y, w, h, flip;     /* mode 3: a sprite faded and blended (fx, alpha) */
    int clip[4];
    float fx[4], alpha;
} GpuOp;
static GpuOp g_ops[1024];
static const char *g_shot_path;     /* frame_shot: where this frame is written */
static int g_nops;

static void gpu_op(unsigned tex, int mode, int x, int y, int w, int h, int flip, const View3d *clip)
{
    GpuOp *o;

    if (tex == 0 || g_nops >= (int)(sizeof g_ops / sizeof g_ops[0])) {
        return;
    }
    o = &g_ops[g_nops++];
    o->tex = tex;
    o->mode = mode;
    o->x = x;
    o->y = y;
    o->w = w;
    o->h = h;
    o->flip = flip;
    o->clip[0] = clip->x;
    o->clip[1] = clip->y;
    o->clip[2] = clip->w;
    o->clip[3] = clip->h;
}

/* A view's rectangle of the canvas, clear: the GPU's pictures show there. */
static void gpu_hole(const View3d *v)
{
    EidosUiCanvas *cv = &A.canvas;
    int y, x0 = v->x < 0 ? 0 : v->x, x1 = v->x + v->w > cv->w ? cv->w : v->x + v->w;

    for (y = v->y < 0 ? 0 : v->y; y < v->y + v->h && y < cv->h && x1 > x0; y++) {
        memset(cv->rgba + ((size_t)y * (size_t)cv->w + (size_t)x0) * 4u, 0, (size_t)(x1 - x0) * 4u);
    }
}

/* A picture's GPU texture: the pictures (sprites, NPCs) are known by their
 * pixels' address and size, and a few of their bytes in case an address is
 * used again. */
typedef struct SpriteTex {
    const u8 *rgba;
    int w, h;
    u32 sum;
    unsigned tex;
} SpriteTex;
static SpriteTex g_stex[512];
static int g_nstex;

static u32 sprite_sum(const u8 *rgba, int w, int h)
{
    size_t n = (size_t)w * (size_t)h * 4u, i, step = n / 64u + 1u;
    u32 h32 = 2166136261u;

    for (i = 0; i < n; i += step) {
        h32 = (h32 ^ rgba[i]) * 16777619u;
    }
    return h32;
}

static unsigned sprite_tex(const u8 *rgba, int w, int h)
{
    u32 sum = sprite_sum(rgba, w, h);
    int i, lru;

    for (i = 0; i < g_nstex; i++) {
        if (g_stex[i].rgba == rgba && g_stex[i].w == w && g_stex[i].h == h && g_stex[i].sum == sum) {
            return g_stex[i].tex;
        }
    }
    if (g_nstex < (int)(sizeof g_stex / sizeof g_stex[0])) {
        lru = g_nstex++;
        g_stex[lru].tex = 0;
    } else {
        lru = (int)(sum % (u32)g_nstex);
    }
    g_stex[lru].tex = eidos_gl_view_upload(A.gl, g_stex[lru].tex, rgba, w, h, 0);
    g_stex[lru].rgba = rgba;
    g_stex[lru].w = w;
    g_stex[lru].h = h;
    g_stex[lru].sum = sum;
    return g_stex[lru].tex;
}

/* A picture through the view: its point (ax, ay) (picture pixels) at p,
 * `upx` battle units a picture pixel; alpha 0..31, and its colours faded
 * fade/16 of the way to fade_color (BGR555), as MCSS draws them (the GPU
 * does it: eidos_gl_view_draw_fx). */
static void draw_sprite(const View3d *v, const u8 *rgba, int w, int h, float ax, float ay, const float p[3], float upx,
                        int alpha, int fade, int fade_color)
{
    float sx, sy, unit, k;
    int dw, dh;

    if (rgba == NULL || w <= 0 || h <= 0 || alpha <= 0 || !project(v, p, &sx, &sy, &unit)) {
        return;
    }
    k = upx * unit;                     /* window pixels a picture pixel */
    dh = (int)((float)h * k);
    dw = (int)((float)w * k);
    if (dw <= 0 || dh <= 0 || dw > 4000 || dh > 4000) {
        return;
    }
    gpu_op(sprite_tex(rgba, w, h), alpha < 31 || fade > 0 ? 3 : 1, (int)(sx - ax * k), (int)(sy - ay * k), dw, dh, 0, v);
    if (alpha < 31 || fade > 0) {
        GpuOp *o = &g_ops[g_nops - 1];

        o->fx[0] = (float)(fade_color & 31) / 31.0f;
        o->fx[1] = (float)(fade_color >> 5 & 31) / 31.0f;
        o->fx[2] = (float)(fade_color >> 10 & 31) / 31.0f;
        o->fx[3] = (float)(fade > 16 ? 16 : fade) / 16.0f;
        o->alpha = (float)(alpha > 31 ? 31 : alpha) / 31.0f;
    }
}

/* A picture standing at p (its bottom centre) through the view, `bh` battle
 * units tall. */
static void draw_standing(const View3d *v, const u8 *rgba, int w, int h, const float p[3], float bh)
{
    if (h > 0) {
        draw_sprite(v, rgba, w, h, (float)w * 0.5f, (float)h, p, bh / (float)h, 31, 0, 0);
    }
}

/* The place's offset of the foe's trainer, or of yours: right, up, back. */
static const float *view_offsets(int foe)
{
    const BmapSpot *r = cur();

    return foe ? r->trainer_foe : r->trainer_own;
}

/* Where the send-out puts the player's trainer before the camera (framed
 * bottom left on the DS; sdk/battle_view.c, own_trainer_point). */
static const float k_sendout[3] = { -15.0f, -11.0f, 0.0f };

/* A battle view: 0 the command camera's, 1 the intro camera's.  A scene
 * that plays moves its camera and its Pokemon.  The sprites are the size
 * the game gives them, as with the game's own backdrop (sdk/battle_view.c,
 * MCSS_Draw): only what the place moves changes it. */
static void draw_battle_view(int i)
{
    View3d *v = &A.battle[i];
    const ClipFrame *cf = play_frame();
    /* the Pokemon: the single battle's two, the double's four (ov094's slot
     * tables) or where a played camera has them, all on the place's foe
     * side (bmap_battle_warp) */
    static const float dbl[4][3] = { { -1.69f, 0.4f, 6.38f }, { 2.5f, 0.4f, 7.62f },
                                     { 2.43f, 0.4f, -10.38f }, { -2.2f, 0.4f, -11.56f } };
    float p[16][3], d[16], sc[16];
    const Sprite *sp[16];
    int kind[16];                   /* 0 a Pokemon, 1 your trainer, 2 the foe's, 3 your ally */
    /* a played scene's own changes (play_frame): size, alpha, fade, animation */
    float fx_size[16];
    int fx_alpha[16], fx_fade[16], fx_color[16], fx_anim[16], fx_t[16];
    int fixed[16] = { 0 };          /* a trainer at an effect script's own point (moved by the warp) */
    int sendout[16] = { 0 };        /* your trainer at the send-out's point: over the map */
    float game_eye[3], game_at[3];  /* the game's own camera, before the place's changes */
    int n = 0, a, b;

    if (!A.have_placed) {
        return;
    }
    if (cf != NULL) {
        memcpy(v->eye, cf->eye, sizeof v->eye);
        memcpy(v->at, cf->at, sizeof v->at);
        memcpy(game_eye, cf->eye, sizeof game_eye);
        memcpy(game_at, cf->at, sizeof game_at);
        v->dirty = 1;
        for (a = 0; a < cf->np && n < 16; a++) {
            int tr = cf->slot[a] > 7, ally = cf->slot[a] == 12 && A.tr_ally.rgba != NULL;

            if (tr && !ally && (cf->slot[a] > 13 || (cf->slot[a] & 1 ? A.tr_foe.rgba : A.tr_own.rgba) == NULL)) {
                continue;
            }
            memcpy(p[n], cf->pos[a], sizeof p[n]);
            if (tr && !(cf->slot[a] & 1) && !ally) {
                /* the player's trainer away from its slot (the send-out's
                 * own point, -15 -11 0) is drawn over the map there, as the
                 * game does (sdk/battle_view.c, own_trainer_point) */
                float slot[3], dd = 0.0f, e;
                int k;

                if (bcam_sim_slot(cf->slot[a], slot) == 0) {
                    for (k = 0; k < 3; k++) {
                        e = p[n][k] - slot[k];
                        dd += e * e;
                    }
                    sendout[n] = dd > 0.25f;
                }
            }
            kind[n] = ally ? 3 : tr ? 1 + (cf->slot[a] & 1) : 0;
            sc[n] = slot_scale(cf->slot[a]);
            fx_size[n] = cf->size[a];
            fx_alpha[n] = cf->alpha[a];
            fx_fade[n] = cf->fade[a];
            fx_color[n] = cf->fade_color[a];
            fx_anim[n] = ally ? -1 : cf->anim[a];
            fx_t[n] = cf->anim_t[a];
            sp[n++] = ally ? &A.tr_ally : tr ? ((cf->slot[a] & 1) ? &A.tr_foe : &A.tr_own)
                    : (cf->slot[a] & 1) ? &A.foe : &A.own;
        }
    } else {
        memcpy(v->eye, k_cam[i][0], sizeof v->eye);
        memcpy(v->at, k_cam[i][1], sizeof v->at);
        memcpy(game_eye, k_cam[i][0], sizeof game_eye);
        memcpy(game_at, k_cam[i][1], sizeof game_at);
        for (a = 0; a < 16; a++) {
            fx_size[a] = 1.0f;
            fx_alpha[a] = 31;
            fx_fade[a] = fx_color[a] = 0;
            fx_anim[a] = -1;            /* the standing picture */
            fx_t[a] = 0;
        }
        if (!A.hide_mon) {
            if (A.doubles) {
                for (a = 0; a < 4; a++) {
                    memcpy(p[n], dbl[a], sizeof p[n]);
                    sp[n++] = a >= 2 ? &A.foe : &A.own;
                }
            } else {
                p[0][0] = FOE_X; p[0][1] = 0.4f; p[0][2] = FOE_Z;
                sp[0] = &A.foe;
                p[1][0] = OWN_X; p[1][1] = 0.4f; p[1][2] = OWN_Z;
                sp[1] = &A.own;
                n = 2;
            }
            for (a = 0; a < n; a++) {
                kind[a] = 0;
                /* a double's four stand in the order of its slots 2 to 5 */
                sc[a] = A.doubles ? slot_scale(a == 0 ? 2 : a == 1 ? 4 : a == 2 ? 3 : 5) : slot_scale(a == 0 ? 1 : 0);
            }
        }
        if (A.show_tr) {
            /* the trainers at their own slots (8 yours, 9 the foe's), with
             * the Pokemon unless those are hidden: yours (and an ally) in
             * the intro camera, the foe's in the command camera */
            for (a = 1; a >= 0; a--) {
                const Sprite *t = a ? &A.tr_foe : &A.tr_own;

                if ((a == 0) != (i == 1)) {
                    continue;
                }

                /* the player's own trainer at its slot in the room, as the
                 * game draws it (sdk/battle_view.c, own_trainer_point): the
                 * spot its Pokemon is sent out to (the Pokemon button hides
                 * that) */
                if (t->rgba != NULL && n < 16 && bcam_sim_slot(8 + a, p[n]) == 0) {
                    if (a == 0 && i == 1) {
                        /* the intro's camera: at the send-out's point */
                        memcpy(p[n], k_sendout, sizeof p[n]);
                        sendout[n] = 1;
                    }
                    sp[n] = t;
                    kind[n] = 1 + a;
                    sc[n] = slot_scale(8 + a);
                    n++;
                }
            }
            if (i == 1 && A.tr_ally.rgba != NULL && n < 16 && bcam_sim_slot(12, p[n]) == 0) {
                /* your ally at its own slot (12), beside you */
                sp[n] = &A.tr_ally;
                kind[n] = 3;
                sc[n] = slot_scale(12);
                n++;
            }
            {
                /* and where it comes back to mid-battle, when it has a line
                 * there: effect 63's point (0.74, 0.4, -9) and its slide in
                 * (to 4.24), the game's own numbers */
                const BmapSpot *rr = cur();

                if (i == 0 && rr != NULL && rr->kind == 'T' && A.tr_foe.rgba != NULL && n < 16
                    && trainer_speaks_mid(rr->trainer_id)) {
                    p[n][0] = 3021.0f / 4096.0f + 22528.0f / 4096.0f - 8192.0f / 4096.0f;
                    p[n][1] = 1638.0f / 4096.0f;
                    p[n][2] = -36864.0f / 4096.0f;
                    sp[n] = &A.tr_foe;
                    kind[n] = 2;
                    fixed[n] = 1;
                    sc[n] = slot_scale(9);
                    n++;
                }
            }
        }
    }
    for (a = 0; a < n; a++) {
        /* its height as the game draws it (sdk/battle_view.c, MCSS_Draw):
         * the game's size, a pixel of its scale two of the DS's 192 lines
         * (a Pokemon's back twice its front).  A trainer is that size
         * before any camera (its depth before the game's camera here); a
         * Pokemon is a thing in the room, bmap_mon_size, the game's size at
         * the command camera and the scripts' (0.5) all the way back */
        float dg = kind[a] != 0 ? bmap_view_depth(game_eye, game_at, p[a]) : bmap_mon_size(p[a]);

        sc[a] = (sp[a] == &A.own ? 2.0f : 1.0f) * (dg > 0.5f ? dg : 0.5f)
                * (2.0f * BATTLE_FOV_SIN / BATTLE_FOV_COS / 192.0f) / SPRITE_U;
        /* and the scripts' own change of it (a recall, a send-out's pop) */
        sc[a] *= fx_size[a];
    }
    for (a = 0; a < n; a++) {
        if (kind[a] != 0 && !fixed[a]) {
            /* a trainer at its slot: the game moves the slot tables flat
             * (sdk/battle_view.c, slot_delta), the foe's side up by dy and
             * back by dz; the warp below is for the Pokemon (equal at the
             * foe's slot) and for an effect's own points */
            if (kind[a] == 2) {
                p[a][1] += A.placed.foe_dy;
                p[a][2] -= A.placed.foe_dz;
            }
        } else {
            bmap_battle_warp(p[a], A.placed.foe_dy, A.placed.foe_dz);
        }
        if (kind[a] != 0) {
            /* the place's offset for this trainer: right, up, back */
            const float *o = kind[a] == 3 ? cur()->trainer_ally : view_offsets(kind[a] == 2);

            p[a][0] += o[0];
            p[a][1] += o[1];
            p[a][2] -= o[2];
            if (kind[a] == 2 && cur()->trainer_size > 0.0f) {
                sc[a] *= cur()->trainer_size;      /* "Indoor": the game's trainer height */
            }
            if (kind[a] == 1 && cur()->trainer_own_size > 0.0f) {
                sc[a] *= cur()->trainer_own_size;  /* the player's own trainer, the same */
            }
            if (kind[a] == 3 && cur()->trainer_ally_size > 0.0f) {
                sc[a] *= cur()->trainer_ally_size; /* the ally, the same */
            }
        }
    }
    bmap_battle_camera(v->eye, v->at, &A.placed);      /* as the game draws it */
    v->fsin = BATTLE_FOV_SIN;
    v->fcos = BATTLE_FOV_COS;
    if (v->dirty) {
        float p4[16][4];
        int a4;

        for (a4 = 0; a4 < n && a4 < 16; a4++) {
            memcpy(p4[a4], p[a4], 3 * sizeof(float));
            p4[a4][3] = kind[a4] == 0 ? 6.0f : (float)sp[a4]->h * SPRITE_U * sc[a4];   /* a Pokemon's box */
            if (sendout[a4]) {
                p4[a4][3] = 0.0f;   /* over the map, as over the game's own backdrop */
            }
        }
        render_view(v, &A.placed, A.battle_gen, (const float (*)[4])p4, n, 1);
    }
    gpu_hole(v);
    gpu_op(v->tex, 0, v->x, v->y, v->w, v->h, 1, v);
    /* the sprites, the deepest first: the game's depth (MCSS_Draw: a
     * sprite's place through the camera drawn with this frame, along its
     * line of sight), so the order follows every move of the camera */
    for (a = 0; a < n; a++) {
        d[a] = bmap_view_depth(v->eye, v->at, p[a]);
    }
    for (a = 0; a < n; a++) {
        int far = -1;
        const TrFrame *tf = NULL;
        float upx;

        for (b = 0; b < n; b++) {
            if (d[b] > -1e8f && (far < 0 || d[b] > d[far])) {
                far = b;
            }
        }
        upx = SPRITE_U * sc[far];       /* battle units a picture pixel */
        if (fx_anim[far] >= 0 && (kind[far] == 1 || kind[far] == 2)) {
            /* a trainer the scene animates (its throw) */
            tf = tranim_frame(kind[far] == 2 ? &A.an_foe : &A.an_own, fx_anim[far], fx_t[far]);
        }
        if (tf != NULL) {
            draw_sprite(v, tf->rgba, tf->w, tf->h, tf->ax, tf->ay, p[far], upx, fx_alpha[far], fx_fade[far],
                        fx_color[far]);
        } else if (sp[far]->h > 0) {
            draw_sprite(v, sp[far]->rgba, sp[far]->w, sp[far]->h, (float)sp[far]->w * 0.5f, (float)sp[far]->h, p[far],
                        upx, fx_alpha[far], fx_fade[far], fx_color[far]);
        }
        /* the map standing in front of this one, before the nearer ones
         * (the game's second backdrop pass, plat_gl3d.c) */
        if (far < v->nfronts && v->has_front[far] && !sendout[far]) {
            gpu_op(v->ftex[far], 0, v->x, v->y, v->w, v->h, 1, v);
        }
        d[far] = -1e9f;
    }
    if (cf == NULL) {
        text(v->x + U(10), v->y + v->h - U(26), 0xFFFFFFu, "Drag: turn, height   Right: slide, tilt   Wheel: zoom");
    }
    outline(v->x, v->y, v->w, v->h, C_LINE);
}

/* The overworld view: the field camera, moved freely, the zone's
 * characters, the player and the battle's two places. */
static void draw_field_view(void)
{
    const BmapSpot *r = cur();
    View3d *v = &A.over;
    float p, yw, at[3], eye[3], sx, sy, unit, pt[3];

    if (A.ow_build) {
        build_field();
    }
    if (!A.have_field || r == NULL) {
        return;
    }
    p = A.ow_pitch * 3.14159265f / 180.0f;
    yw = A.ow_yaw * 3.14159265f / 180.0f;
    at[0] = A.ow_tx;
    at[2] = A.ow_tz;
    at[1] = bmap_ground_y(&A.obase, A.ow_tx, A.ow_tz) + A.ow_lift;
    eye[0] = at[0] + A.ow_dist * cosf(p) * sinf(yw);
    eye[1] = at[1] + A.ow_dist * sinf(p);
    eye[2] = at[2] + A.ow_dist * cosf(p) * cosf(yw);
    map_to_battle(&A.field, at[0], at[1], at[2], v->at);
    map_to_battle(&A.field, eye[0], eye[1], eye[2], v->eye);
    v->fsin = sinf(A.ow_fov * 3.14159265f / 180.0f);
    v->fcos = cosf(A.ow_fov * 3.14159265f / 180.0f);
    if (v->dirty) {
        /* the characters standing in the view (the nearest the camera
         * first): the map in front of each is drawn again over it, as in
         * the battle views */
        static float all[257][4], dd[257];
        float sp[G3D_BACKDROP_SPRITES][4];
        int na = 0, ns, a2, b2;

        for (a2 = -1; a2 < A.nnpc && na < 257; a2++) {
            float q[4], d0, d1, d2;

            if (a2 < 0) {
                map_to_battle(&A.field, A.tx, bmap_ground_y(&A.obase, A.tx, A.tz), A.tz, q);
                q[3] = A.hero_h * A.field.place_k;
            } else {
                if (A.npc_zone != r->zone || A.npcs[a2].rgba == NULL) {
                    continue;
                }
                map_to_battle(&A.field, A.npcs[a2].x, A.npcs[a2].y, A.npcs[a2].z, q);
                q[3] = A.npcs[a2].world_h * A.field.place_k;
            }
            if (q[3] <= 0.0f) {
                continue;
            }
            d0 = q[0] - v->eye[0];
            d1 = q[1] - v->eye[1];
            d2 = q[2] - v->eye[2];
            memcpy(all[na], q, sizeof q);
            dd[na++] = d0 * d0 + d1 * d1 + d2 * d2;
        }
        for (a2 = 1; a2 < na; a2++) {               /* nearest first */
            for (b2 = a2; b2 > 0 && dd[b2] < dd[b2 - 1]; b2--) {
                float tq[4], td = dd[b2];

                memcpy(tq, all[b2], sizeof tq);
                memcpy(all[b2], all[b2 - 1], sizeof tq);
                memcpy(all[b2 - 1], tq, sizeof tq);
                dd[b2] = dd[b2 - 1];
                dd[b2 - 1] = td;
            }
        }
        ns = na < G3D_BACKDROP_SPRITES ? na : G3D_BACKDROP_SPRITES;
        memcpy(sp, all, (size_t)ns * sizeof sp[0]);
        render_view(v, &A.field, A.field_gen, (const float (*)[4])sp, ns, 0);
    }
    gpu_hole(v);
    gpu_op(v->tex, 0, v->x, v->y, v->w, v->h, 1, v);
    /* the zone's NPCs, the farthest first, as the field draws them */
    if (A.nnpc > 0 && A.npc_zone == r->zone) {
        int order[256], n = 0, a, b;
        float dist[256];

        for (a = 0; a < A.nnpc && n < 256; a++) {
            float d0, d1;

            if (A.npcs[a].rgba == NULL) {
                continue;
            }
            map_to_battle(&A.field, A.npcs[a].x, A.npcs[a].y, A.npcs[a].z, pt);
            d0 = pt[0] - v->eye[0];
            d1 = pt[2] - v->eye[2];
            dist[n] = d0 * d0 + d1 * d1;
            order[n++] = a;
        }
        for (a = 1; a < n; a++) {           /* farthest first */
            for (b = a; b > 0 && dist[b] > dist[b - 1]; b--) {
                float td = dist[b];
                int to = order[b];

                dist[b] = dist[b - 1];
                order[b] = order[b - 1];
                dist[b - 1] = td;
                order[b - 1] = to;
            }
        }
        for (a = 0; a < n; a++) {
            const BmapNpc *np = &A.npcs[order[a]];

            map_to_battle(&A.field, np->x, np->y, np->z, pt);
            draw_standing(v, np->rgba, np->w, np->h, pt, np->world_h * A.field.place_k);
        }
    }
    /* the player, the trainer, the battle's two places */
    map_to_battle(&A.field, A.tx, bmap_ground_y(&A.obase, A.tx, A.tz), A.tz, pt);
    {
        /* the player, facing the way the test player faces */
        int f = ((A.tf % 360 + 360) % 360 + 45) / 90 % 4;      /* 0 north, 1 west, 2 south, 3 east */
        static const int to_dir[4] = { 0, 2, 1, 3 };          /* up, left, down, right */
        const Sprite *h = &A.hero[to_dir[f]];

        if (h->rgba != NULL) {
            draw_standing(v, h->rgba, h->w, h->h, pt, A.hero_h * A.field.place_k);
        } else if (project(v, pt, &sx, &sy, &unit)) {
            ring(sx, sy, 8.0f, C_TEXT);
        }
    }
    if (v->has_front[16]) {
        gpu_op(v->ftex[16], 0, v->x, v->y, v->w, v->h, 1, v);    /* the map in front of them */
    }
    if (r->kind == 'T') {
        map_to_battle(&A.field, (float)r->tx * 16.0f + 8.0f,
                      bmap_ground_y(&A.obase, (float)r->tx * 16.0f + 8.0f, (float)r->tz * 16.0f + 8.0f),
                      (float)r->tz * 16.0f + 8.0f, pt);
        if (project(v, pt, &sx, &sy, &unit)) {
            ring(sx, sy, 8.0f, C_FOE);
        }
    }
    if (A.have_placed) {
        float ax, az;

        battle_point_map(OWN_X, OWN_Z, &ax, &az);
        map_to_battle(&A.field, ax, A.placed.place_g[0], az, pt);
        if (project(v, pt, &sx, &sy, &unit)) {
            ring(sx, sy, 6.0f, C_OWN);
        }
        battle_point_map(FOE_X, FOE_Z - A.placed.foe_dz, &ax, &az);
        map_to_battle(&A.field, ax, A.placed.place_g[1], az, pt);
        if (project(v, pt, &sx, &sy, &unit)) {
            ring(sx, sy, 6.0f, C_FOE);
        }
    }
    outline(v->x, v->y, v->w, v->h, C_LINE);
    text(v->x + U(10), v->y + v->h - U(26), 0xFFFFFFu, "Drag: move   Right drag: turn   Wheel: zoom");
}

/* ------------------------------------------------------------------ */
/* The layout                                                          */
/* ------------------------------------------------------------------ */

/*
 * The window:
 *
 *   top      the place, Single / Double, what happened last, Checked, Undo,
 *            Preview, Play battle
 *   left     the places: search, filters, how far the checking is, the list
 *   centre   one view at a time (Command camera, Intro camera, Overworld);
 *            under a battle view the play bar: the scene (Intro, Mid-battle,
 *            Idle, a move), Play and a timeline
 *   right    the inspector, scrolled by the wheel: Placement, Camera (of the
 *            view on show), Views, Trainers, Pokemon shown, More, the map
 *            from above
 */

enum { TAB_COMMAND, TAB_INTRO, TAB_FIELD };
static const char *k_tabs[3] = { "Command camera", "Intro camera", "Overworld" };

#define TOP_H     U(60)
#define SL_ROW    U(52)               /* a slider's height: its label line and track */
#define DRAG_SLIDER 20              /* A.drag while a slider is dragged (A.drag_id which) */

static int chip_w(const char *label)
{
    return text_w(label) + U(22);
}

/* A button sized to its label. */
static int chip(int x, int y, const char *label, int on)
{
    return button(x, y, chip_w(label), U(30), label, on);
}

/* `s` cut to `w` pixels, "..." marking the cut. */
static void clip_label(char *s, int w)
{
    while (text_w(s) > w && strlen(s) > 4) {
        s[strlen(s) - 4] = '\0';
        strcat(s, "...");
    }
}

/* A section heading across the inspector; returns the y under it. */
static int section(int x, int y, int w, const char *title)
{
    text(x, y, C_ACCENT, "%s", title);
    ui_fill(&C, x, y + U(24), w, 1, C_LINE, 255);
    return y + U(36);
}

/*
 * A setting as a slider: the label, the value (a click types it) and a
 * track from lo to hi under them.  A press on the track sets it there and a
 * drag moves it, in steps of `step`; a typed value may pass the ends.
 * `wrap` keeps degrees in -180 .. 180.  Returns 1 when *v changed.
 */
static int slider(int id, int x, int y, int w, const char *label, const char *fmt, float *v, float lo, float hi,
                  float step, int wrap)
{
    char shown[24];
    float typed, nv = *v, frac;
    int changed = 0, ty = y + U(32), kx, on = A.drag == DRAG_SLIDER && A.drag_id == id;

    snprintf(shown, sizeof shown, fmt, (double)*v);
    text(x, y, C_MUTED, "%s", label);
    if (num_field(id, x + w - text_w(shown) - U(4), y, "", shown, &typed)) {
        nv = typed;
        changed = 1;
    }
    frac = hi > lo ? (clampf(*v, lo, hi) - lo) / (hi - lo) : 0.0f;
    kx = x + (int)(frac * (float)w);
    ui_fill(&C, x, ty, w, U(4), C_PANEL2, 255);
    ui_fill(&C, x, ty, kx - x, U(4), C_ACCENT, 255);
    ui_ellipse(&C, kx - U(7), ty - U(5), U(14), U(14), on || hover(x - U(6), ty - U(10), w + U(12), U(24)) ? C_ACCENT : 0xC9CDD5u, UI_NONE,
               255);
    if (A.drag == 0 && A.pressed && hover(x - U(6), ty - U(10), w + U(12), U(24))) {
        A.drag = DRAG_SLIDER;
        A.drag_id = id;
        A.drag_undo = 0;
        on = 1;
    }
    if (on && A.down) {
        nv = lo + clampf((float)(A.mx - x) / (float)(w > 0 ? w : 1), 0.0f, 1.0f) * (hi - lo);
        nv = roundf(nv / step) * step;
        changed = 1;
    }
    if (!changed) {
        return 0;
    }
    if (wrap) {
        nv = fmodf(nv, 360.0f);
        nv = nv > 180.0f ? nv - 360.0f : nv <= -180.0f ? nv + 360.0f : nv;
    }
    if (nv == *v) {
        return 0;
    }
    *v = nv;
    return 1;
}

/* A change from a slider to the place: kept for Undo once a drag, saved
 * when the drag ends (slider_release), else at once. */
static void slider_changed(void)
{
    if (A.drag == DRAG_SLIDER) {
        if (!A.drag_undo) {
            remember();
            A.drag_undo = 1;
        }
        A.drag_dirty = 1;
    }
}

static void slider_release(void)
{
    if (A.drag == DRAG_SLIDER && !A.down) {
        A.drag = 0;
        if (A.drag_dirty) {
            A.drag_dirty = 0;
            save_now();
        }
    }
}

/* A slider for a camera or trainer setting of the place; the views follow
 * it live while it is dragged. */
static void setting(int id, int x, int y, int w, const char *label, const char *fmt, float *v, float lo, float hi,
                    float step, int wrap)
{
    float nv = *v;

    if (!slider(id, x, y, w, label, fmt, &nv, lo, hi, step, wrap)) {
        return;
    }
    if (A.drag == DRAG_SLIDER) {
        slider_changed();
        *v = nv;
        intro_live(cur());
    } else {
        remember();
        *v = nv;
        save_now();
    }
}

/* ---- the Progress sheet ---- */

/* The places in the order the story reaches them, then after the credits,
 * then the extras; a name is a label's part before its ':'.  An entry
 * starting with '#' is a section title. */
static const char *const k_story[] = {
    "#Story",
    "Nuvema Town", "Route 1", "Accumula Town", "Route 2", "Striaton City", "Striaton Gym",
    "Dreamyard", "Route 3", "Wellspring Cave", "Nacrene City", "Nacrene Gym",
    "Pinwheel Forest", "Castelia Gate", "Castelia City", "Castelia Gym", "Route 4",
    "Desert Resort", "Relic Castle", "Nimbasa City", "Nimbasa Gym", "Route 5",
    "Driftveil City", "Driftveil Gym", "Cold Storage", "Route 6", "Mistralton Cave",
    "Chargestone Cave", "Mistralton City", "Mistralton Gym", "Route 7", "Celestial Tower",
    "Twist Mountain", "Icirrus City", "Icirrus Gym", "Dragonspiral Tower", "Route 8",
    "Moor of Icirrus", "Tubeline Bridge", "Route 9", "Shopping Mall", "Opelucid City",
    "Opelucid Gym", "Route 10", "Victory Road", "Pok\xC3\xA9mon League", "N's Castle",
    "#After the credits",
    "Route 11", "Village Bridge", "Route 12", "Lacunosa Town", "Route 13", "Giant Chasm",
    "Undella Town", "Undella Bay", "Route 14", "Abundant Shrine", "Route 15", "Bridge Gate",
    "Route 16", "Lostlorn Forest", "Route 17", "Route 18", "P2 Laboratory",
    "Challenger's Cave",
    "#Extras",
    "Black City", "White Forest", "Rumination Field", "Guidance Chamber", "Trial Chamber",
    "Liberty Garden", "Unity Tower",
};
#define N_STORY ((int)(sizeof k_story / sizeof k_story[0]))

/* Whether row r's place is `name` (the label up to its ':'). */
static int place_is(const BmapSpot *r, const char *name)
{
    size_t n = strlen(name);

    return strncmp(r->label, name, n) == 0 && r->label[n] == ':';
}

/* Row r's place in k_story (N_STORY when the sheet does not name it). */
static int story_rank(const BmapSpot *r)
{
    int i;

    for (i = 0; i < N_STORY; i++) {
        if (k_story[i][0] != '#' && place_is(r, k_story[i])) {
            return i;
        }
    }
    return N_STORY;
}

/* A place's trainers and wild areas: checked and all. */
static void place_counts(const char *name, int *td, int *tt, int *wd, int *wt)
{
    int k;

    for (k = 0; k < bmap_spots_count(); k++) {
        const BmapSpot *r = bmap_spots_get(k);

        if (r->view != 0 || !place_is(r, name)) {
            continue;
        }
        if (r->kind == 'T') {
            (*tt)++;
            *td += r->ok != 0;
        } else {
            (*wt)++;
            *wd += r->ok != 0;
        }
    }
}

/* The sheet's box: under the top bar, at the right. */
static void progress_box(int *x, int *y, int *w, int *h)
{
    *w = U(560);
    *x = A.W - U(16) - *w;
    *y = TOP_H + U(4);
    *h = A.H - *y - U(16);
}

/* The sheet over everything (drawn last): one line a place with its
 * trainers and wild areas checked out of all, counted from the table as it
 * is now.  A click on a place goes to its first row not checked yet. */
static void draw_progress(void)
{
    int x, y, w, h, rowh = U(26), i, line = 0, shown, first;
    int ctd = U(300), ctt = U(370), cwd = U(440), cwt = U(510);

    if (!A.prog_open) {
        return;
    }
    progress_box(&x, &y, &w, &h);
    if (A.released && !hover(x, y, w, h) && !A.prog_btn) {
        A.prog_open = 0;                /* a click outside closes it */
        return;
    }
    shown = (h - U(64)) / rowh;
    if (hover(x, y, w, h) && A.wheel != 0) {
        A.prog_scroll -= A.wheel * 3;
    }
    A.prog_scroll = A.prog_scroll > N_STORY - shown ? N_STORY - shown : A.prog_scroll;
    A.prog_scroll = A.prog_scroll < 0 ? 0 : A.prog_scroll;
    ui_fill(&C, x, y, w, h, C_PANEL, 255);
    outline(x, y, w, h, C_LINE);
    text(x + U(12), y + U(30), C_MUTED, "Place");
    text(x + ctd, y + U(10), C_MUTED, "Trainers");
    text(x + cwd, y + U(10), C_MUTED, "Wild areas");
    text(x + ctd, y + U(30), C_MUTED, "Done");
    text(x + ctt, y + U(30), C_MUTED, "All");
    text(x + cwd, y + U(30), C_MUTED, "Done");
    text(x + cwt, y + U(30), C_MUTED, "All");
    ui_fill(&C, x + U(8), y + U(54), w - U(16), 1, C_LINE, 255);
    first = A.prog_scroll;
    for (i = first; i < N_STORY && line < shown; i++, line++) {
        int ly = y + U(60) + line * rowh, k, td = 0, tt = 0, wd = 0, wt = 0, go = -1, go_any = -1;
        const char *name = k_story[i];

        if (name[0] == '#') {
            /* the section's own count: its places up to the next title */
            int j, pct;

            for (j = i + 1; j < N_STORY && k_story[j][0] != '#'; j++) {
                place_counts(k_story[j], &td, &tt, &wd, &wt);
            }
            pct = tt + wt > 0 ? (td + wd) * 100 / (tt + wt) : 0;
            text(x + U(12), ly + U(6), C_ACCENT, "%s  %d%%", name + 1, pct);
            text(x + ctd, ly + U(6), tt > 0 && td == tt ? C_GOOD : C_ACCENT, "%d", td);
            text(x + ctt, ly + U(6), C_ACCENT, "%d", tt);
            text(x + cwd, ly + U(6), wt > 0 && wd == wt ? C_GOOD : C_ACCENT, "%d", wd);
            text(x + cwt, ly + U(6), C_ACCENT, "%d", wt);
            continue;
        }
        for (k = 0; k < bmap_spots_count(); k++) {
            const BmapSpot *r = bmap_spots_get(k);

            if (r->view != 0 || !place_is(r, name)) {
                continue;
            }
            if (r->kind == 'T') {
                tt++;
                td += r->ok != 0;
            } else {
                wt++;
                wd += r->ok != 0;
            }
            go_any = go_any < 0 ? k : go_any;
            go = go < 0 && !r->ok ? k : go;
        }
        if (hover(x + U(4), ly, w - U(8), rowh)) {
            ui_fill(&C, x + U(4), ly, w - U(8), rowh, C_PANEL2, 255);
            if (A.released && go_any >= 0) {
                select_row(go >= 0 ? go : go_any);
                A.prog_open = 0;
                return;
            }
        }
        if (tt + wt > 0 && td + wd == tt + wt) {
            /* all of the place checked: crossed out, still there to click */
            text(x + U(12), ly + U(6), C_MUTED, "%s", name);
            text(x + ctd, ly + U(6), C_MUTED, "%d", td);
            text(x + ctt, ly + U(6), C_MUTED, "%d", tt);
            text(x + cwd, ly + U(6), C_MUTED, "%d", wd);
            text(x + cwt, ly + U(6), C_MUTED, "%d", wt);
            ui_fill(&C, x + U(10), ly + U(6) + 5 * FK, cwt + U(24) - U(10), FK > 2 ? 2 : 1, C_MUTED, 255);
            continue;
        }
        text(x + U(12), ly + U(6), C_TEXT, "%s", name);
        text(x + ctd, ly + U(6), tt > 0 && td == tt ? C_GOOD : C_TEXT, "%d", td);
        text(x + ctt, ly + U(6), C_MUTED, "%d", tt);
        text(x + cwd, ly + U(6), wt > 0 && wd == wt ? C_GOOD : C_TEXT, "%d", wd);
        text(x + cwt, ly + U(6), C_MUTED, "%d", wt);
    }
}

/* ---- the top bar ---- */

static void draw_top(int LW, int IW)
{
    BmapSpot *r = cur();
    int rx = A.W - U(16), x = LW, i;

    ui_fill(&C, 0, 0, A.W, TOP_H, C_PANEL, 255);
    ui_fill(&C, 0, TOP_H - 1, A.W, 1, C_LINE, 255);
    if (r != NULL) {
        char lab[96], kind[64];

        snprintf(lab, sizeof lab, "%s", r->label);
        clip_label(lab, LW - U(32));
        text(U(16), U(7), C_TEXT, "%s", lab);
        snprintf(kind, sizeof kind, "%s %s, zone %u", r->kind == 'T' ? "Trainer" : "Wild", r->id, r->zone);
        clip_label(kind, LW - U(32));
        text(U(16), U(31), C_MUTED, "%s", kind);
    }
    /* the actions, from the right */
    rx -= chip_w("Stop preview");
    if (A.emb != NULL) {
        if (chip(rx, U(15), "Stop preview", 1)) {
            preview_stop();
        }
    } else if (r != NULL && button(rx, U(15), chip_w("Stop preview"), U(30), "Preview", 0)) {
        preview_in_game(1);
    }
    rx -= chip_w("Undo") + U(8);
    if (chip(rx, U(15), "Undo", 0)) {
        undo();
    }
    if (r != NULL) {
        BmapSpot *b = bmap_spots_get(first_view(A.row));
        const char *lab = b->ok ? "Checked" : "Mark checked";

        rx -= chip_w("Mark checked") + U(8);
        if (button(rx, U(15), chip_w("Mark checked"), U(30), lab, b->ok)) {
            b->ok = !b->ok;
            save_now();
            refilter();
        }
    }
    /* the Progress sheet: every place in story order, checked out of all */
    rx -= chip_w("Progress") + U(8);
    A.prog_btn = hover(rx, U(15), chip_w("Progress"), U(30));
    if (button(rx, U(15), chip_w("Progress"), U(30), "Progress", A.prog_open)) {
        A.prog_open = !A.prog_open;
        A.prog_scroll = 0;
    }
    /* dark grass (a wild area marked double): a wild battle there is a
     * single or, 40 times in 100, a double (FIELD_ENCOUNT_InitParam), with
     * the same camera; the switch shows either, and Preview plays the one
     * on show */
    if (r != NULL && r->kind == 'A' && r->doubles) {
        rx -= chip_w("Double") + U(16);
        if (chip(rx, U(15), "Double", A.doubles)) {
            A.doubles = 1;
            A.dark_single = 0;
            A.battle[0].dirty = A.battle[1].dirty = 1;
        }
        rx -= chip_w("Single") + U(4);
        if (chip(rx, U(15), "Single", !A.doubles)) {
            A.doubles = 0;
            A.dark_single = 1;
            A.battle[0].dirty = A.battle[1].dirty = 1;
        }
    }
    /* what happened last */
    if (A.status[0] != '\0' && SDL_GetTicks() - A.status_at < 4000) {
        char st[160];

        snprintf(st, sizeof st, "%s", A.status);
        clip_label(st, rx - U(20) - x);
        if (rx - U(20) - x > U(60)) {
            text(x, U(19), C_GOOD, "%s", st);
        }
    }
    (void)IW;
    (void)i;
}

/* ---- the map from above ---- */

/* The map at (x, y), w across, as tall as `room` lets it (hidden under 160
 * pixels: scroll to it), with its key under it.  Returns the y under all. */
static int top_view(int x, int y, int w, int room)
{
    int kx = x;

    A.map_x = x;
    A.map_y = y;
    A.map_s = room < w ? room : w;
    if (A.map_s < 160 || A.map_y < TOP_H) {
        A.map_s = 0;
    } else {
        draw_map();
        if (!A.follow && button(x + A.map_s - U(92), y + U(6), U(86), U(26), "Center", 0)) {
            A.follow = 1;
        }
    }
    y += w + U(10);
    /* the key, and the mouse on the map */
    ring((float)kx + 7.0f, (float)y + 10.0f, 6.0f, C_OWN);
    kx = text(kx + U(18), y, C_MUTED, "Yours") + U(14);
    ring((float)kx + 7.0f, (float)y + 10.0f, 6.0f, C_FOE);
    kx = text(kx + U(18), y, C_MUTED, "Foe") + U(14);
    ui_ellipse(&C, kx + 1, y + U(4), U(11), U(11), C_CAM, UI_INK, 255);
    kx = text(kx + U(18), y, C_MUTED, "Camera") + U(14);
    ui_ellipse(&C, kx + 1, y + U(4), U(11), U(11), C_TEXT, UI_INK, 255);
    text(kx + U(18), y, C_MUTED, "Player");
    y += U(26);
    text(x, y, C_MUTED, "Drag: move  Right: turn");
    y += U(26);
    text(x, y, C_MUTED, "On the player: the same");
    y += U(26);
    text(x, y, C_MUTED, "Shift drag: safe area");
    y += U(26);
    text(x, y, C_MUTED, "Wheel: zoom  Middle: pan");
    return y + U(30);
}

/* the height top_view takes, w across */
static int top_view_h(int w)
{
    return w + U(10) + 4 * U(26) + U(30);
}

/* ---- the places ---- */

static void draw_places(int x0, int y0, int w0, int h0)
{
    int x = x0 + U(12), y = y0 + U(12), w = w0 - U(24), i, rows, list_y, list_h, row_h = U(30), done = 0, n = 0;

    ui_fill(&C, x0, y0, w0, h0, C_PANEL, 255);
    ui_fill(&C, x0 + w0 - 1, y0, 1, h0, C_LINE, 255);
    for (i = 0; i < bmap_spots_count(); i++) {
        const BmapSpot *q = bmap_spots_get(i);

        n += q->view == 0;
        done += q->view == 0 && q->ok;
    }
    /* the search: typing anywhere goes to it, Esc clears it */
    ui_fill(&C, x, y, w, U(34), 0x111318u, 255);
    outline(x, y, w, U(34), A.search[0] != '\0' ? C_ACCENT : C_LINE);
    if (A.search[0] != '\0') {
        text(x + U(10), y + U(7), C_TEXT, "%s_", A.search);
    } else {
        text(x + U(10), y + U(7), C_MUTED, "Type to search");
    }
    text(x + w - U(10) - text_w("000"), y + U(7), C_MUTED, "%d", A.nvis);
    y += U(44);
    /* the filters, wrapping */
    {
        int fx = x;

        for (i = 0; i < F_COUNT; i++) {
            int bw = chip_w(k_filters[i]);

            if (fx + bw > x + w) {
                fx = x;
                y += U(36);
            }
            if (chip(fx, y, k_filters[i], A.filter == i)) {
                A.filter = i;
                A.scroll = 0;
                refilter();
            }
            fx += bw + U(6);
        }
        y += U(44);
    }
    /* how far the checking is */
    text(x, y, C_MUTED, "%d of %d checked", done, n);
    y += U(26);
    ui_fill(&C, x, y, w, U(4), C_PANEL2, 255);
    ui_fill(&C, x, y, n > 0 ? w * done / n : 0, U(4), C_GOOD, 255);
    y += U(14);
    /* the list, to the bottom (or to the map, on a big window) */
    list_y = y;
    rows = (y0 + h0 - U(8) - (A.map_left ? top_view_h(w) + U(12) : 0) - list_y) / row_h;
    if (rows < 3) {
        rows = 3;
    }
    list_h = rows * row_h;
    if (hover(x, list_y, w, list_h) && A.wheel != 0) {
        A.scroll -= A.wheel * 3;
    }
    if (A.scroll > A.nvis - rows) {
        A.scroll = A.nvis - rows;
    }
    if (A.scroll < 0) {
        A.scroll = 0;
    }
    for (i = 0; i < rows && A.scroll + i < A.nvis; i++) {
        int ri = A.vis[A.scroll + i], ry = list_y + i * row_h;
        const BmapSpot *q = bmap_spots_get(ri);
        int sel = ri == first_view(A.row), hv = hover(x, ry, w, row_h);
        u32 dot = q->ok ? C_GOOD : changed_row(q) ? C_ACCENT : 0x4A4F5Bu;
        char lab[96];

        if (sel || hv) {
            ui_fill(&C, x - U(4), ry, w + U(8), row_h - 2, sel ? 0x2C3A55u : 0x22262Eu, 255);
        }
        ui_ellipse(&C, x + U(4), ry + U(10), U(9), U(9), dot, UI_NONE, 255);
        snprintf(lab, sizeof lab, "%s", q->label);
        clip_label(lab, w - U(30));
        text(x + U(22), ry + U(4), sel ? C_TEXT : 0xC9CDD5u, "%s", lab);
        if (hv && A.released && A.drag == 0) {
            select_row(ri);
        }
    }
    if (A.nvis > rows) {
        int bh = list_h * rows / A.nvis, by = list_y + (list_h - bh) * A.scroll / (A.nvis - rows);

        ui_fill(&C, x + w + U(6), by, U(3), bh, C_LINE, 255);
    }
    if (A.map_left) {
        top_view(x, y0 + h0 - top_view_h(w), w, w);
    }
}

/* ---- the centre: the view and the play bar ---- */

/* The play bar's height: two rows, three when the move picker needs its own. */
static int play_h(int w0)
{
    return w0 >= U(900) ? U(104) : U(144);
}

/* The scene buttons, Play and the timeline under a battle view. */
static void draw_play_bar(int x0, int y0, int w0)
{
    static const char *k_kinds[6] = { "Intro", "Mid-battle", "Idle", "Move", "Switch", "Ball" };
    int x = x0 + U(14), y = y0 + U(12), kind, k, step = A.shift ? 10 : 1;
    const BmapSpot *row = cur();

    ui_fill(&C, x0, y0, w0, play_h(w0), C_PANEL, 255);
    /* the scene: the place's intro (wild or trainer), a trainer back mid-
     * battle, the idle drift, a move, your switch, a ball (wild only) */
    kind = A.scene <= 1 ? 0 : A.scene == SCENE_MID ? 1 : A.scene == 2 ? 2 : A.scene == SCENE_SWITCH ? 4
         : A.scene == SCENE_BALL ? 5 : 3;
    for (k = 0; k < 6; k++) {
        if (k == 5 && row != NULL && row->kind == 'T') {
            continue;                   /* no ball at a trainer's Pokemon */
        }
        if (chip(x, y, k_kinds[k], kind == k)) {
            const BmapSpot *r = cur();

            A.scene = k == 0 ? (r != NULL && r->kind == 'T' ? 1 : 0) : k == 1 ? SCENE_MID : k == 2 ? 2
                    : k == 4 ? SCENE_SWITCH : k == 5 ? SCENE_BALL
                    : A.last_move >= 3 && A.last_move < SCENE_COUNT ? A.last_move : 3;
            A.play = 1;
            A.play_t0 = SDL_GetTicks();
            A.battle[0].dirty = A.battle[1].dirty = 1;
            kind = k;
        }
        x += chip_w(k_kinds[k]) + U(6);
    }
    if (play_h(w0) > U(104)) {
        x = x0 + U(14);
        y += U(40);
    } else {
        x += U(18);
    }
    if (kind == 3) {
        /* the move: the arrows (Shift: ten moves), who uses it */
        const char *nm = scene_name(A.scene);
        int nw = U(240);

        if (button(x, y, U(30), U(30), "<", 0)) {
            A.scene = A.scene - step < 3 ? SCENE_COUNT - 1 : A.scene - step;
            A.last_move = A.scene;
            A.play_t0 = SDL_GetTicks();
        }
        ui_fill(&C, x + U(34), y, nw, U(30), 0x111318u, 255);
        outline(x + U(34), y, nw, U(30), C_LINE);
        text(x + U(34) + (nw - text_w(nm)) / 2, y + U(5), C_TEXT, "%s", nm);
        if (button(x + U(38) + nw, y, U(30), U(30), ">", 0)) {
            A.scene = A.scene + step >= SCENE_COUNT ? 3 : A.scene + step;
            A.last_move = A.scene;
            A.play_t0 = SDL_GetTicks();
        }
        x += U(80) + nw;
        if (button(x, y, chip_w("Yours attacks"), U(30), A.foe_attacks ? "Foe attacks" : "Yours attacks", 0)) {
            A.foe_attacks = !A.foe_attacks;
            A.play_t0 = SDL_GetTicks();
        }
    }
    /* Play, the timeline */
    y = y0 + play_h(w0) - U(52);
    if (button(x0 + U(14), y, U(80), U(40), A.play ? "Stop" : "Play", A.play)) {
        A.play = !A.play;
        A.play_t0 = SDL_GetTicks();
        A.battle[0].dirty = A.battle[1].dirty = 1;
    }
    {
        int tx0 = x0 + U(112), tx1 = x0 + w0 - U(24) - text_w("000 / 000"), at = 0, n = 0;

        if (A.play) {
            scene_frames();
            n = A.nsim;
        }
        if (n > 0) {
            at = A.play_at >= 0 ? A.play_at % n : (int)((SDL_GetTicks() - A.play_t0) * 60u / 1000u) % n;
        }
        ui_fill(&C, tx0, y + U(18), tx1 - tx0, U(4), C_PANEL2, 255);
        if (n > 1) {
            int kx = tx0 + (tx1 - tx0) * at / (n - 1);

            ui_fill(&C, tx0, y + U(18), kx - tx0, U(4), C_ACCENT, 255);
            ui_ellipse(&C, kx - U(7), y + U(13), U(14), U(14), C_ACCENT, UI_NONE, 255);
        }
        /* a press or a drag on the timeline: play on from that frame */
        if (n > 1 && A.down && A.drag == 0 && A.play_at < 0 && hover(tx0 - U(6), y, tx1 - tx0 + U(12), U(40))) {
            int f = (int)((float)(A.mx - tx0) / (float)(tx1 - tx0) * (float)(n - 1) + 0.5f);

            f = f < 0 ? 0 : f >= n ? n - 1 : f;
            A.play_t0 = SDL_GetTicks() - (Uint32)f * 1000u / 60u;
        }
        if (n > 0) {
            text(tx1 + U(12), y + U(9), C_MUTED, "%d / %d", at + 1, n);
        } else {
            text(tx1 + U(12), y + U(9), C_MUTED, "%s", A.play ? "none" : "");
        }
    }
}

static void draw_centre(int x0, int y0, int w0, int h0)
{
    int x = x0 + U(14), y = y0 + U(12), i, ph = play_h(w0);

    trainer_refresh();
    ui_fill(&C, x0, y0, w0, h0, C_BG, 255);
    for (i = 0; i < 3; i++) {
        if (chip(x, y, k_tabs[i], A.tab == i)) {
            A.tab = i;
            A.battle[0].dirty = A.battle[1].dirty = A.over.dirty = 1;
        }
        x += chip_w(k_tabs[i]) + U(6);
    }
    if (A.tab == TAB_FIELD) {
        /* the season and the time of day it shows, while they fit */
        x += U(12);
        for (i = 0; i < 8; i++) {
            const char *lab = i < 4 ? k_seasons[i] : k_hour_names[i - 4];
            int on = i < 4 ? A.season == i : A.hour == i - 4;

            if (x + chip_w(lab) > x0 + w0 - U(14)) {
                break;
            }
            if (chip(x, y, lab, on)) {
                if (i < 4) {
                    A.season = i;
                } else {
                    A.hour = i - 4;
                }
                A.need_place = 1;
            }
            x += chip_w(lab) + (i == 3 ? U(16) : U(4));
        }
    }
    y += U(42);
    /* only the view on show takes the mouse: the others have no place */
    for (i = 0; i < 2; i++) {
        if (A.tab != i) {
            A.battle[i].w = A.battle[i].h = 0;
        }
    }
    if (A.tab != TAB_FIELD) {
        A.over.w = A.over.h = 0;
        fit_view(&A.battle[A.tab], x0 + U(14), y, w0 - U(28), y0 + h0 - ph - U(12) - y);
    } else {
        fit_view(&A.over, x0 + U(14), y, w0 - U(28), y0 + h0 - U(56) - y);
    }
    if (A.need_place && cur() != NULL) {
        place_all();
    }
    if (A.emb != NULL) {
        /* the game, played here: in the battle views' own frame (their
         * 16:9 and their place), the play bar's room left empty */
        View3d frame;

        memset(&frame, 0, sizeof frame);
        fit_view(&frame, x0 + U(14), y, w0 - U(28), y0 + h0 - ph - U(12) - y);
        A.battle[0].w = A.battle[0].h = A.battle[1].w = A.battle[1].h = A.over.w = A.over.h = 0;
        draw_embed(frame.x, frame.y, frame.w, frame.h);
    } else if (A.tab == TAB_FIELD) {
        draw_field_view();
        if (button(x0 + U(14), y0 + h0 - U(44), chip_w("Field camera"), U(30), "Field camera", 0)) {
            A.ow_reset = 1;
            A.need_place = 1;
        }
    } else {
        draw_battle_view(A.tab);
        draw_play_bar(x0, y0 + h0 - ph, w0);
    }
    if (!A.have_base) {
        text(x0 + U(24), y + U(24), C_MUTED, "Loading...");
    }
}

/* ---- the inspector ---- */

static void draw_inspector(int x0, int y0, int w0, int h0)
{
    BmapSpot *r = cur();
    int x = x0 + U(18), w = w0 - U(44), half = (w - U(20)) / 2, third = (w - U(32)) / 3, i, y, top, keep_mx = A.mx;

    ui_fill(&C, x0, y0, w0, h0, C_PANEL, 255);
    ui_fill(&C, x0, y0, 1, h0, C_LINE, 255);
    if (r == NULL) {
        return;
    }
    /* the wheel scrolls it (not over the map, where it zooms) */
    if (A.wheel != 0 && A.drag == 0 && hover(x0, y0, w0, h0) && !hover(A.map_x, A.map_y, A.map_s, A.map_s)) {
        A.insp_scroll -= A.wheel * U(60);
    }
    if (A.insp_scroll > A.insp_h - h0) {
        A.insp_scroll = A.insp_h - h0;
    }
    if (A.insp_scroll < 0) {
        A.insp_scroll = 0;
    }
    /* scrolled under the top bar, its controls take no clicks */
    if (A.drag == 0 && !hover(x0, y0, w0, h0)) {
        A.mx = -10000;
    }
    top = y0 + U(16) - A.insp_scroll;
    y = top;
    /* Placement */
    y = section(x, y, w, "PLACEMENT");
    {
        int bw = (w - U(12)) / 3;

        if (button(x, y, bw, U(30), "Auto", r->mode == BMAP_SPOT_AUTO)) set_mode(BMAP_SPOT_AUTO);
        if (button(x + bw + U(6), y, bw, U(30), "Fixed spot", r->mode == BMAP_SPOT_FIXED)) set_mode(BMAP_SPOT_FIXED);
        if (button(x + 2 * (bw + U(6)), y, bw, U(30), "Safe area", r->mode == BMAP_SPOT_ZONE)) set_mode(BMAP_SPOT_ZONE);
        y += U(42);
    }
    {
        /* the angle: the player's facing in auto (or a safe area that
         * follows the player), else the place's own */
        int follows = r->mode == BMAP_SPOT_AUTO || r->angle < 0.0f, q, qx = x;
        float ang = (float)(follows ? A.tf : (int)r->angle), was = ang;
        char lab[48];
        static const int k_quick[3] = { -90, 90, 180 };
        static const char *k_quick_label[3] = { "-90", "+90", "180" };

        snprintf(lab, sizeof lab, "Facing %s%s", dir_name((int)ang), follows ? ", the player's" : "");
        slider(1, x, y, w, lab, "%.0f", &ang, 0.0f, 355.0f, 5.0f, 0);
        y += SL_ROW;
        for (q = 0; q < 3; q++) {
            if (button(qx, y, U(60), U(28), k_quick_label[q], 0)) {
                ang += (float)k_quick[q];
            }
            qx += U(66);
        }
        ang = (float)norm_deg(ang);
        if (r->mode == BMAP_SPOT_ZONE && r->angle >= 0.0f && button(qx + U(6), y, chip_w("Follow player"), U(28),
                                                                     "Follow player", 0)) {
            remember();
            r->angle = -1.0f;
            save_now();
        } else if (ang != was) {
            if (follows) {
                A.tf = (int)ang;
                A.need_place = 1;
            } else if (A.drag == DRAG_SLIDER) {
                slider_changed();
                r->angle = ang;
                A.need_place = 1;
            } else {
                remember();
                r->angle = ang;
                save_now();
            }
        }
        y += U(42);
    }
    {
        float apart = 17.0f + r->foe_dz;

        if (slider(2, x, y, w, "Foe distance", "%.1f", &apart, 5.0f, 29.0f, 0.5f, 0)) {
            if (A.drag == DRAG_SLIDER) {
                slider_changed();
                r->foe_dz = clampf(apart - 17.0f, -12.0f, 12.0f);
                A.need_place = 1;
            } else {
                remember();
                r->foe_dz = clampf(apart - 17.0f, -12.0f, 12.0f);
                save_now();
            }
        }
        y += SL_ROW + U(8);
    }
    /* Camera: the one of the view on show (the command camera turns every
     * battle camera; the overworld tab edits it too) */
    {
        BmapIntro *in = A.tab == TAB_INTRO ? &r->intro : &r->cam;
        int id = A.tab == TAB_INTRO ? 10 : 20;

        y = section(x, y, w, A.tab == TAB_INTRO ? "INTRO CAMERA" : "COMMAND CAMERA");
        setting(id + 0, x, y, half, "Zoom", "%.2f", &in->zoom, 0.25f, 4.0f, 0.05f, 0);
        setting(id + 1, x + half + U(20), y, half, "Turn", "%+.0f", &in->yaw, -180.0f, 180.0f, 1.0f, 1);
        y += SL_ROW;
        setting(id + 2, x, y, half, "Height", "%+.1f", &in->lift, -20.0f, 20.0f, 0.1f, 0);
        setting(id + 3, x + half + U(20), y, half, "Slide", "%+.1f", &in->side, -20.0f, 20.0f, 0.1f, 0);
        y += SL_ROW;
        setting(id + 4, x, y, half, "Tilt", "%+.0f", &in->tilt, -85.0f, 85.0f, 1.0f, 0);
        setting(id + 5, x + half + U(20), y, half, "Aim", "%+.0f", &in->aim, -180.0f, 180.0f, 1.0f, 1);
        y += SL_ROW;
        if (chip(x, y, "Reset camera", 0) && !bmap_intro_none(in)) {
            remember();
            memset(in, 0, sizeof *in);
            in->zoom = 1.0f;
            if (A.tab == TAB_INTRO) {
                BmapSpot d = *r;

                bmap_spot_defaults(&d);     /* the intro's own defaults */
                *in = d.intro;
            }
            save_now();
            say(A.tab == TAB_INTRO ? "Intro camera reset" : "Camera reset");
        }
        y += U(46);
    }
    /* Views: the game takes the one nearest the player */
    {
        int nv = bmap_spots_view_count(A.row), vn = view_no(A.row), v, bx = x;

        y = section(x, y, w, "VIEWS");
        for (v = 0; v < nv; v++) {
            char num[16];

            snprintf(num, sizeof num, "%d", v + 1);
            if (button(bx, y, U(34), U(30), num, v == vn) && v != vn) {
                select_row(bmap_spots_view_at(A.row, v));
                r = cur();
            }
            bx += U(40);
            /* as many views as wanted: the numbers wrap to the next line */
            if (bx + U(34) > x + w && v + 1 < nv) {
                bx = x;
                y += U(36);
            }
        }
        if (bx + chip_w("Add") + U(6) + chip_w("Delete") > x + w) {
            bx = x;
            y += U(36);
        }
        if (chip(bx, y, "Add", 0)) {
            int nr = bmap_spots_view_add(A.row, vn);

            if (nr >= 0) {
                A.nundo = 0;
                select_row(nr);
                if (cur()->mode != BMAP_SPOT_FIXED) {
                    set_mode(BMAP_SPOT_FIXED);
                }
                save_now();
                say("View %d added: drag the spot and turn it", nv + 1);
            }
            r = cur();
        }
        bx += chip_w("Add") + U(6);
        if (nv > 1 && chip(bx, y, "Delete", 0)) {
            int first = first_view(A.row);

            if (bmap_spots_view_delete(first, vn) == 0) {
                A.nundo = 0;
                select_row(bmap_spots_view_at(first, vn > 0 ? vn - 1 : 0));
                save_now();
                say("View deleted");
            }
            r = cur();
        }
        y += U(40);
        if (nv > 1) {
            /* the game scores the views on the player's place and facing
             * (bmap_spots_pick_view) */
            int pick = bmap_spots_pick_view(A.row, A.tx, A.tz, deg_to_facing((float)A.tf));

            text(x, y + U(5), pick == A.row ? C_GOOD : C_MUTED, "From the player marker: view %d",
                 view_no(pick) + 1);
            y += U(40);
        }
        y += U(6);
    }
    /* Trainers: their places (a change shows them) */
    y = section(x, y, w, "TRAINERS");
    {
        int bx = x + chip_w("Trainers") + chip_w("Pokemon") + U(22);

        if (chip(x, y, "Trainers", A.show_tr)) {
            A.show_tr = !A.show_tr;
            A.battle[0].dirty = A.battle[1].dirty = 1;
        }
        if (chip(x + chip_w("Trainers") + U(6), y, "Pokemon", !A.hide_mon)) {
            A.hide_mon = !A.hide_mon;
            A.battle[0].dirty = A.battle[1].dirty = 1;
        }
        for (i = 0; i < 2; i++) {
            const char *lab = i ? "Girl" : "Boy";

            if (chip(bx, y, lab, A.gender == i) && A.gender != i) {
                A.gender = i;
                gender_write();
                A.show_tr = 1;
                A.battle[0].dirty = A.battle[1].dirty = 1;
            }
            bx += chip_w(lab) + U(6);
        }
        y += U(42);
    }
    {
        /* the foe trainer's height: the base, or a little taller where a
         * room's furniture makes the base look short */
        float sz = r->trainer_size > 0.0f ? r->trainer_size : 1.0f;
        int custom = A.size_custom || (fabsf(sz - 1.0f) > 0.001f && fabsf(sz - BMAP_TRAINER_INDOOR) > 0.001f);
        int indoor = !custom && sz > 1.0f, bx = x;

        text(x, y + U(5), C_MUTED, "Size");
        bx += text_w("Size") + U(16);
        if (chip(bx, y, "Outdoor", !custom && !indoor) && (custom || indoor)) {
            remember();
            r->trainer_size = 1.0f;
            A.size_custom = 0;
            A.show_tr = 1;
            save_now();
        }
        bx += chip_w("Outdoor") + U(6);
        if (chip(bx, y, "Indoor", indoor) && !indoor) {
            remember();
            r->trainer_size = BMAP_TRAINER_INDOOR;
            A.size_custom = 0;
            A.show_tr = 1;
            save_now();
        }
        bx += chip_w("Indoor") + U(6);
        if (chip(bx, y, "Custom", custom) && !custom) {
            A.size_custom = 1;          /* the slider, from the size it has */
        }
        y += U(42);
        if (custom) {
            /* any height: a click on the number types it */
            float before = r->trainer_size;

            r->trainer_size = sz;
            setting(36, x, y, w, "Height", "%.2f", &r->trainer_size, 0.8f, 1.5f, 0.01f, 0);
            if (r->trainer_size <= 0.0f) {
                r->trainer_size = before > 0.0f ? before : 1.0f;
            }
            if (r->trainer_size != sz) {
                A.show_tr = 1;
                A.battle[0].dirty = A.battle[1].dirty = 1;
            }
            y += SL_ROW;
        }
    }
    for (i = 1; i >= 0; i--) {
        float *tr = i ? r->trainer_foe : r->trainer_own, before = tr[0] + 3.0f * tr[1] + 7.0f * tr[2];

        text(x, y, C_TEXT, "%s", i ? "Foe trainer" : "Your trainer");
        y += U(28);
        setting(30 + 3 * i, x, y, third, "Right", "%+.1f", &tr[0], -10.0f, 10.0f, 0.1f, 0);
        setting(31 + 3 * i, x + third + U(16), y, third, "Up", "%+.1f", &tr[1], -10.0f, 10.0f, 0.1f, 0);
        setting(32 + 3 * i, x + 2 * (third + U(16)), y, third, "Back", "%+.1f", &tr[2], -10.0f, 10.0f, 0.1f, 0);
        if (before != tr[0] + 3.0f * tr[1] + 7.0f * tr[2]) {
            A.show_tr = 1;
        }
        y += SL_ROW + U(4);
        if (i == 0) {
            /* your trainer's height, as the foe's Size: a click on the
             * number types it */
            float sz = r->trainer_own_size > 0.0f ? r->trainer_own_size : 1.0f;

            r->trainer_own_size = sz;
            setting(37, x, y, w, "Size", "%.2f", &r->trainer_own_size, 0.5f, 2.0f, 0.01f, 0);
            if (r->trainer_own_size <= 0.0f) {
                r->trainer_own_size = sz;
            }
            if (r->trainer_own_size != sz) {
                A.show_tr = 1;
                A.battle[0].dirty = A.battle[1].dirty = 1;
            }
            y += SL_ROW + U(4);
        }
    }
    if (r->ally_id >= 0) {
        float *tr = r->trainer_ally, before = tr[0] + 3.0f * tr[1] + 7.0f * tr[2] + 11.0f * r->trainer_ally_size;

        text(x, y, C_TEXT, "Ally trainer");
        y += U(28);
        setting(38, x, y, third, "Right", "%+.1f", &tr[0], -10.0f, 10.0f, 0.1f, 0);
        setting(39, x + third + U(16), y, third, "Up", "%+.1f", &tr[1], -10.0f, 10.0f, 0.1f, 0);
        setting(40, x + 2 * (third + U(16)), y, third, "Back", "%+.1f", &tr[2], -10.0f, 10.0f, 0.1f, 0);
        y += SL_ROW + U(4);
        if (r->trainer_ally_size <= 0.0f) {
            r->trainer_ally_size = 1.0f;
        }
        setting(41, x, y, w, "Size", "%.2f", &r->trainer_ally_size, 0.5f, 2.0f, 0.01f, 0);
        if (r->trainer_ally_size <= 0.0f) {
            r->trainer_ally_size = 1.0f;
        }
        if (before != tr[0] + 3.0f * tr[1] + 7.0f * tr[2] + 11.0f * r->trainer_ally_size) {
            A.show_tr = 1;
            A.battle[0].dirty = A.battle[1].dirty = 1;
        }
        y += SL_ROW + U(4);
    }
    /* the Pokemon shown in the views (Shift: ten at a time) */
    y = section(x, y + U(4), w, "POKEMON SHOWN");
    for (i = 0; i < 2; i++) {
        static const char *who[2] = { "Foe", "Yours" };
        int step = A.shift ? 10 : 1, ch = 0, bx = x + half + U(20);
        char num[16];

        snprintf(num, sizeof num, "#%d", A.species[i]);
        text(x, y + U(5), C_MUTED, "%s", who[i]);
        if (button(bx, y, U(30), U(30), "-", 0)) {
            A.species[i] = A.species[i] - step < 1 ? 649 : A.species[i] - step;
            ch = 1;
        }
        text(bx + U(30) + (half - U(60) - text_w(num)) / 2, y + U(5), C_TEXT, "%s", num);
        if (button(bx + half - U(30), y, U(30), U(30), "+", 0)) {
            A.species[i] = A.species[i] + step > 649 ? 1 : A.species[i] + step;
            ch = 1;
        }
        if (ch) {
            Sprite *s = i == 0 ? &A.foe : &A.own;

            free(s->rgba);
            sprite_load(s, A.species[i], i == 1);
            A.battle[0].dirty = A.battle[1].dirty = 1;
        }
        y += U(38);
    }
    /* More */
    y = section(x, y + U(8), w, "MORE");
    {
        int bx = x;

        if (chip(bx, y, "Back to auto", 0)) {
            remember();
            r->mode = BMAP_SPOT_AUTO;
            r->foe_level = 1;
            memset(&r->intro, 0, sizeof r->intro);
            r->intro.zoom = 1.0f;
            memset(&r->cam, 0, sizeof r->cam);
            r->cam.zoom = 1.0f;
            memset(r->trainer_foe, 0, sizeof r->trainer_foe);
            memset(r->trainer_own, 0, sizeof r->trainer_own);
            bmap_spot_defaults(r);      /* the foe distance, own trainer, intro */
            r->trainer_size = 1.0f;
            r->trainer_own_size = 1.0f;
            memset(r->trainer_ally, 0, sizeof r->trainer_ally);
            r->trainer_ally_size = 1.0f;
            save_now();
            say("Back to auto");
        }
        bx += chip_w("Back to auto") + U(6);
        if (chip(bx, y, "Default", 0)) {
            BmapSpot d;

            if (bmap_spots_default(r->id, &d) == 0) {
                remember();
                d.ok = r->ok;
                d.view = r->view;
                *r = d;
                save_now();
                say("Back to the default");
            }
        }
        bx += chip_w("Default") + U(6);
        if (chip(bx, y, "Reverse", 0)) {
            reverse_battle();
        }
        y += U(42);
    }
    /* what to know about the place: a Pokemon on a wall, the ground step */
    if (A.have_placed && A.have_base) {
        float step = fabsf(A.placed.place_g[1] - A.placed.place_g[0]) / 16.0f, fx, fz;
        int to, tf;

        battle_point_map(FOE_X, FOE_Z - A.placed.foe_dz, &fx, &fz);
        to = bmap_tile(&A.base, A.placed.place_x, A.placed.place_z);
        tf = bmap_tile(&A.base, fx, fz);
        if (to == BMAP_TILE_BLOCKED || to == BMAP_TILE_NONE) {
            text(x, y, C_BAD, "Yours stands on a wall or off the map");
        } else if (tf == BMAP_TILE_BLOCKED || tf == BMAP_TILE_NONE) {
            text(x, y, C_BAD, "The foe stands on a wall or off the map");
        } else {
            text(x, y, C_MUTED, "Ground step %.1f tiles", (double)step);
        }
        y += U(26);
    }
    text(x, y, C_MUTED, "%s", r->mode == BMAP_SPOT_ZONE ? "Kept in the box where it starts"
                              : r->mode == BMAP_SPOT_AUTO ? "Moved to a clear spot where it starts"
                              : "Every battle here uses this spot");
    y += U(36);
    /* the map from above, unless it is under the list */
    if (!A.map_left) {
        y = section(x, y, w, "TOP VIEW");
        y = top_view(x, y, w, y0 + h0 - U(12) - y);
    }
    A.insp_h = y - top;
    A.mx = keep_mx;
    /* the scroll bar */
    if (A.insp_h > h0) {
        int bh = h0 * h0 / A.insp_h, by = y0 + (h0 - bh) * A.insp_scroll / (A.insp_h - h0);

        ui_fill(&C, x0 + w0 - U(8), by, U(3), bh, C_LINE, 255);
    }
}

/* ------------------------------------------------------------------ */
/* Input and the loop                                                  */
/* ------------------------------------------------------------------ */

static void nudge(float dx, float dz)
{
    BmapSpot *r = cur();

    if (r->mode == BMAP_SPOT_FIXED) {
        remember();
        r->sx += dx;
        r->sz += dz;
        save_now();
    } else {
        A.tx += dx;
        A.tz += dz;
        A.need_place = 1;
    }
}

static void on_key(const SDL_KeyboardEvent *k)
{
    BmapSpot *r = cur();
    float step = (k->mod & SDL_KMOD_SHIFT) ? 16.0f : 4.0f;
    int p = vis_pos(A.row);

    if (A.emb != NULL) {
        return;                         /* the game's keys (draw_embed) */
    }
    if (A.edit != 0) {
        /* typing a number: Enter takes it, Esc gives up */
        if (k->key == SDLK_RETURN || k->key == SDLK_KP_ENTER) {
            A.edit_done = 1;
        } else if (k->key == SDLK_ESCAPE) {
            A.edit = 0;
        } else if (k->key == SDLK_BACKSPACE && A.edit_buf[0] != '\0') {
            A.edit_buf[strlen(A.edit_buf) - 1] = '\0';
        }
        return;
    }
    if ((k->mod & SDL_KMOD_CTRL) && k->key == SDLK_Z) {
        undo();
        return;
    }
    switch (k->key) {
    case SDLK_ESCAPE:
        A.search[0] = '\0';
        refilter();
        break;
    case SDLK_BACKSPACE:
        if (A.search[0] != '\0') {
            A.search[strlen(A.search) - 1] = '\0';
            refilter();
        }
        break;
    case SDLK_PAGEDOWN:
    case SDLK_TAB:
        if (p >= 0 && p + 1 < A.nvis) {
            select_row(A.vis[p + 1]);
        } else if (A.nvis > 0) {
            select_row(A.vis[0]);
        }
        break;
    case SDLK_PAGEUP:
        if (p > 0) {
            select_row(A.vis[p - 1]);
        }
        break;
    case SDLK_RETURN:
        if (r != NULL) {
            BmapSpot *b = bmap_spots_get(first_view(A.row));

            b->ok = !b->ok;
            save_now();
            if (b->ok && A.filter == F_TODO) {
                refilter();
                if (A.nvis > 0) {
                    select_row(A.vis[p >= 0 && p < A.nvis ? p : 0]);
                }
            }
        }
        break;
    case SDLK_UP: nudge(0.0f, -step); break;
    case SDLK_DOWN: nudge(0.0f, step); break;
    case SDLK_LEFT: nudge(-step, 0.0f); break;
    case SDLK_RIGHT: nudge(step, 0.0f); break;
    default:
        break;
    }
}

static void pump(void)
{
    SDL_Event ev;

    A.pressed = A.released = A.rpressed = A.wheel = 0;
    while (SDL_PollEvent(&ev)) {
        switch (ev.type) {
        case SDL_EVENT_QUIT:
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
            A.quit = 1;
            break;
        case SDL_EVENT_MOUSE_MOTION:
            A.mx = (int)ev.motion.x;
            A.my = (int)ev.motion.y;
            break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            A.mx = (int)ev.button.x;
            A.my = (int)ev.button.y;
            if (ev.button.button == SDL_BUTTON_LEFT) {
                A.down = 1;
                A.pressed = 1;
            } else if (ev.button.button == SDL_BUTTON_RIGHT) {
                A.rdown = 1;
                A.rpressed = 1;
            } else if (ev.button.button == SDL_BUTTON_MIDDLE) {
                A.mdown = 1;
            }
            break;
        case SDL_EVENT_MOUSE_BUTTON_UP:
            if (ev.button.button == SDL_BUTTON_LEFT) {
                A.down = 0;
                A.released = 1;
            } else if (ev.button.button == SDL_BUTTON_RIGHT) {
                A.rdown = 0;
            } else if (ev.button.button == SDL_BUTTON_MIDDLE) {
                A.mdown = 0;
            }
            break;
        case SDL_EVENT_MOUSE_WHEEL:
            A.wheel += ev.wheel.integer_y;
            break;
        case SDL_EVENT_TEXT_INPUT:
            if (A.emb != NULL) {
                break;                  /* the game's keys */
            }
            if (A.edit != 0) {
                if (strlen(A.edit_buf) + strlen(ev.text.text) < sizeof A.edit_buf - 1) {
                    strcat(A.edit_buf, ev.text.text);
                }
            } else if (strlen(A.search) + strlen(ev.text.text) < sizeof A.search - 1) {
                strcat(A.search, ev.text.text);
                A.scroll = 0;
                refilter();
            }
            break;
        case SDL_EVENT_KEY_DOWN:
            on_key(&ev.key);
            break;
        default:
            break;
        }
    }
    {
        SDL_Keymod m = SDL_GetModState();

        A.shift = (m & SDL_KMOD_SHIFT) != 0;
        A.ctrl = (m & SDL_KMOD_CTRL) != 0;
    }
}

/* The frame on the GPU: the views' pictures and the sprites in their order,
 * then the interface canvas over them; `shot` (a BMP path) also writes the
 * window out before it is shown. */
static void gpu_flush(const char *shot)
{
    static unsigned canvas_tex;
    static int canvas_w, canvas_h;
    int i;

    eidos_gl_view_begin(A.gl, A.W, A.H, C_BG);
    for (i = 0; i < g_nops; i++) {
        const GpuOp *o = &g_ops[i];

        if (o->mode == 3) {
            eidos_gl_view_draw_fx(A.gl, o->tex, o->x, o->y, o->w, o->h, o->clip, o->fx, o->alpha);
        } else {
            eidos_gl_view_draw(A.gl, o->tex, o->mode, o->x, o->y, o->w, o->h, o->flip, o->clip);
        }
    }
    g_nops = 0;
    canvas_tex = eidos_gl_view_upload(A.gl, canvas_tex, A.canvas.rgba, A.canvas.w, A.canvas.h,
                                      canvas_w == A.canvas.w && canvas_h == A.canvas.h);
    canvas_w = A.canvas.w;
    canvas_h = A.canvas.h;
    eidos_gl_view_draw(A.gl, canvas_tex, 2, 0, 0, A.W, A.H, 0, NULL);
    if (shot != NULL) {
        u8 *px = (u8 *)malloc((size_t)A.W * (size_t)A.H * 4u);
        SDL_Surface *sf = px != NULL && eidos_gl_view_read(A.gl, px) == 0
                          ? SDL_CreateSurfaceFrom(A.W, A.H, SDL_PIXELFORMAT_RGBA32, px, A.W * 4) : NULL;

        if (sf != NULL) {
            SDL_SaveBMP(sf, shot);
            SDL_DestroySurface(sf);
        }
        free(px);
    }
    eidos_gl_view_end(A.gl);
}

static void frame(void)
{
    struct { int mx, my, released, pressed, rpressed, wheel, on; } hold = { 0, 0, 0, 0, 0, 0, 0 };
    int LW, IW;
    Uint64 t0 = SDL_GetPerformanceCounter();
    static int prof = -1;

    if (prof < 0) {
        prof = getenv("EIDOS_SPOTS_PROF") != NULL;
    }
    SDL_GetWindowSize(A.win, &A.W, &A.H);
    /* 1440p and up: the text a half bigger, the room between a quarter
     * (all of the inspector shows), the map under the list */
    g_fk = A.H >= 1300 ? 3 : 2;
    g_u = g_fk == 3 ? 5 : 4;
    A.map_left = g_fk == 3;
    LW = A.W * 20 / 100;
    LW = LW < U(300) ? U(300) : LW > U(420) ? U(420) : LW;
    IW = A.W * 25 / 100;
    IW = IW < U(400) ? U(400) : IW > U(500) ? U(500) : IW;
    ui_begin(&C, &A.canvas, A.W, A.H, 1);
    {
        /* under the open Progress sheet, nothing else takes the pointer */
        int px, py, pw, ph;

        progress_box(&px, &py, &pw, &ph);
        if (A.prog_open && hover(px, py, pw, ph)) {
            hold.mx = A.mx;
            hold.my = A.my;
            hold.released = A.released;
            hold.pressed = A.pressed;
            hold.rpressed = A.rpressed;
            hold.wheel = A.wheel;
            hold.on = 1;
            A.mx = A.my = -100000;
            A.released = A.pressed = A.rpressed = A.wheel = 0;
        }
    }
    map_input();
    ow_input();
    view_input(0);
    view_input(1);
    /* the top bar last: the inspector scrolls under it */
    draw_centre(LW, TOP_H, A.W - LW - IW, A.H - TOP_H);
    draw_places(0, TOP_H, LW, A.H - TOP_H);
    draw_inspector(A.W - IW, TOP_H, IW, A.H - TOP_H);
    draw_top(LW, IW);
    if (hold.on) {
        A.mx = hold.mx;
        A.my = hold.my;
        A.released = hold.released;
        A.pressed = hold.pressed;
        A.rpressed = hold.rpressed;
        A.wheel = hold.wheel;
    }
    draw_progress();
    slider_release();
    gpu_flush(g_shot_path);
    if (prof) {
        /* EIDOS_SPOTS_PROF: each frame's time on stderr */
        fprintf(stderr, "frame %.2f ms\n",
                (double)(SDL_GetPerformanceCounter() - t0) * 1000.0 / (double)SDL_GetPerformanceFrequency());
    }
}

/* --script FILE (tests): one step a line, each followed by frames:
 *   click X Y | rclick X Y | drag X1 Y1 X2 Y2 | rdrag X1 Y1 X2 Y2 |
 *   key NAME | text STRING | shot PATH */
static void push_button(int type, int button, int x, int y)
{
    SDL_Event ev;

    memset(&ev, 0, sizeof ev);
    ev.type = (Uint32)type;
    ev.button.button = (Uint8)button;
    ev.button.down = type == SDL_EVENT_MOUSE_BUTTON_DOWN;
    ev.button.x = (float)x;
    ev.button.y = (float)y;
    ev.button.windowID = SDL_GetWindowID(A.win);
    SDL_PushEvent(&ev);
}

static void push_motion(int x, int y)
{
    SDL_Event ev;

    memset(&ev, 0, sizeof ev);
    ev.type = SDL_EVENT_MOUSE_MOTION;
    ev.motion.x = (float)x;
    ev.motion.y = (float)y;
    SDL_PushEvent(&ev);
}

/* A frame drawn and written out (tests). */
static void frame_shot(const char *path)
{
    g_shot_path = path;
    frame();
    g_shot_path = NULL;
}

static void step(void)
{
    pump();
    frame();
}

static void run_script(const char *path)
{
    FILE *f = fopen(path, "r");
    char line[256];

    if (f == NULL) {
        return;
    }
    while (fgets(line, sizeof line, f) != NULL) {
        int x1, y1, x2, y2, right = 0;
        char arg[200];

        line[strcspn(line, "\r\n")] = '\0';
        if (sscanf(line, "click %d %d", &x1, &y1) == 2 || (right = sscanf(line, "rclick %d %d", &x1, &y1) == 2)) {
            int b = right ? SDL_BUTTON_RIGHT : SDL_BUTTON_LEFT;

            push_motion(x1, y1);
            push_button(SDL_EVENT_MOUSE_BUTTON_DOWN, b, x1, y1);
            step();
            push_button(SDL_EVENT_MOUSE_BUTTON_UP, b, x1, y1);
            step();
        } else if (sscanf(line, "drag %d %d %d %d", &x1, &y1, &x2, &y2) == 4
                   || (right = sscanf(line, "rdrag %d %d %d %d", &x1, &y1, &x2, &y2) == 4)) {
            int b = right ? SDL_BUTTON_RIGHT : SDL_BUTTON_LEFT, i;

            push_motion(x1, y1);
            push_button(SDL_EVENT_MOUSE_BUTTON_DOWN, b, x1, y1);
            step();
            for (i = 1; i <= 4; i++) {
                push_motion(x1 + (x2 - x1) * i / 4, y1 + (y2 - y1) * i / 4);
                step();
            }
            push_button(SDL_EVENT_MOUSE_BUTTON_UP, b, x2, y2);
            step();
        } else if (sscanf(line, "key %199s", arg) == 1) {
            SDL_Event ev;

            memset(&ev, 0, sizeof ev);
            ev.type = SDL_EVENT_KEY_DOWN;
            ev.key.down = true;
            ev.key.key = SDL_GetKeyFromName(arg);
            SDL_PushEvent(&ev);
            step();
        } else if (sscanf(line, "text %199s", arg) == 1) {
            SDL_Event ev;
            static char text[200];      /* SDL3 events point at their text */

            memset(&ev, 0, sizeof ev);
            ev.type = SDL_EVENT_TEXT_INPUT;
            snprintf(text, sizeof text, "%s", arg);
            ev.text.text = text;
            SDL_PushEvent(&ev);
            step();
        } else if (sscanf(line, "wait %d", &x1) == 1) {
            SDL_Delay((Uint32)x1);
            step();
        } else if (sscanf(line, "shot %199s", arg) == 1) {
            step();
            frame_shot(arg);
        } else if (sscanf(line, "scene %d", &x1) == 1) {
            A.scene = x1;                           /* played, frame 0 */
            A.play = 1;
            A.play_at = 0;
            step();
        } else if (sscanf(line, "frame %d", &x1) == 1) {
            A.play_at = x1;
            step();
        } else if (sscanf(line, "camdump %199s", arg) == 1) {
            /* each frame of the scene: the game's camera, then as this
             * place draws it (bmap_battle_camera) */
            FILE *o = fopen(arg, "w");
            int k;

            scene_frames();
            for (k = 0; o != NULL && k < A.nsim; k++) {
                float e[3], t[3];

                memcpy(e, A.sim[k].eye, sizeof e);
                memcpy(t, A.sim[k].target, sizeof t);
                bmap_battle_camera(e, t, &A.placed);
                fprintf(o, "%d %.3f %.3f %.3f %.3f %.3f %.3f %.3f %.3f %.3f %.3f %.3f %.3f\n", k,
                        (double)A.sim[k].eye[0], (double)A.sim[k].eye[1], (double)A.sim[k].eye[2],
                        (double)A.sim[k].target[0], (double)A.sim[k].target[1], (double)A.sim[k].target[2],
                        (double)e[0], (double)e[1], (double)e[2], (double)t[0], (double)t[1], (double)t[2]);
            }
            if (o != NULL) {
                fclose(o);
            }
        }
    }
    fclose(f);
}

static const char *scene_name(int sc)
{
    static char buf[64];
    const char *n;

    if (sc == 0) {
        return "Wild intro";
    }
    if (sc == 1) {
        return "Trainer intro";
    }
    if (sc == 2) {
        return "Idle";
    }
    if (sc == SCENE_MID) {
        return "Mid-battle";
    }
    if (sc == SCENE_SWITCH) {
        return "Switch";
    }
    if (sc == SCENE_BALL) {
        return "Ball";
    }
    n = bcam_sim_move_name(sc - 2);
    snprintf(buf, sizeof buf, "%d %s", sc - 2, n != NULL ? n : "");
    return buf;
}

/* The frames of the scene chosen, worked out by the battle camera
 * simulator from the game's own scripts (none recorded). */
static void scene_frames(void)
{
    int kind, id, att = A.foe_attacks ? 1 : 0;

    if (A.sim != NULL && A.sim_scene == A.scene && A.sim_attacker == att) {
        return;
    }
    free(A.sim);
    free(A.sim_rest);
    A.sim = NULL;
    A.sim_rest = NULL;
    A.nsim = 0;
    kind = A.scene == 0 ? BCAM_WILD_INTRO : A.scene == 1 ? BCAM_TRAINER_INTRO : A.scene == 2 ? BCAM_IDLE
         : A.scene == SCENE_MID ? BCAM_TRAINER_MID : A.scene == SCENE_SWITCH ? BCAM_SWITCH
         : A.scene == SCENE_BALL ? BCAM_BALL : BCAM_MOVE;
    id = A.scene >= 3 && A.scene < SCENE_COUNT ? A.scene - 2 : A.scene == SCENE_BALL ? 4 : -1;
    if (bcam_sim_run(kind, id, att, !att, &A.sim, &A.nsim) != 0) {
        A.sim = NULL;
        A.nsim = 0;
        say("No camera for %s", scene_name(A.scene));
    }
    if (A.nsim > 0 && (A.sim_rest = (float *)malloc((size_t)A.nsim * 14u * sizeof(float))) != NULL) {
        /* the size at rest of each sprite, frame by frame from the scene's
         * start, as the game works it out (sdk/battle_view.c) */
        BmapRest r;
        int k, q;

        memset(&r, 0, sizeof r);
        bmap_rest_reset(&r);
        for (k = 0; k < A.nsim; k++) {
            const BcamFrame *b = &A.sim[k];

            bmap_rest_frame(&r, b->eye);
            for (q = 0; q < 14; q++) {
                int on = q < 8 ? (b->poke_mask >> q & 1u) : (b->trainer_mask >> (q - 8) & 1u);

                A.sim_rest[k * 14 + q] = bmap_rest_sprite(&r, q, on ? b->size[q] : 0.0f);
            }
        }
    }
    A.sim_scene = A.scene;
    A.sim_attacker = att;
}

/* The frame on show while a scene plays (it loops until Stop), or NULL. */
static const ClipFrame *play_frame(void)
{
    static ClipFrame cf;
    const BcamFrame *b;
    int i, k;

    if (!A.play) {
        return NULL;
    }
    scene_frames();
    if (A.nsim <= 0) {
        return NULL;
    }
    i = A.play_at >= 0 ? A.play_at % A.nsim : (int)((SDL_GetTicks() - A.play_t0) * 60u / 1000u) % A.nsim;
    b = &A.sim[i];
    memcpy(cf.eye, b->eye, sizeof cf.eye);
    memcpy(cf.at, b->target, sizeof cf.at);
    cf.np = 0;
    for (k = 0; k < 14; k++) {
        int q = cf.np;

        if (!(k < 8 ? b->poke_mask & (1u << k) : b->trainer_mask & (1u << (k - 8))) || (b->vanish_mask & (1u << k))) {
            continue;                   /* not on the field, or hidden (the send-out's wait) */
        }
        cf.slot[q] = k;
        memcpy(cf.pos[q], k < 8 ? b->poke[k] : b->trainer[k - 8], sizeof cf.pos[0]);
        /* the scripts' size: a Pokemon's over its size at rest (the
         * camera's fake taken out, as the game does), a trainer's as is */
        if (k < 8 && A.sim_rest != NULL) {
            float rest = A.sim_rest[i * 14 + k];

            cf.size[q] = b->size[k] / (rest > 0.05f ? rest : 1.0f);
        } else {
            cf.size[q] = b->size[k];
        }
        cf.alpha[q] = b->alpha[k];
        cf.fade[q] = b->fade[k];
        cf.fade_color[q] = b->fade_color[k];
        cf.anim[q] = b->anim[k];
        cf.anim_t[q] = b->anim_t[k];
        cf.np++;
    }
    return &cf;
}

/* ------------------------------------------------------------------ */
/* Preview in game                                                     */
/* ------------------------------------------------------------------ */

static int copy_file(const char *from, const char *to)
{
    FILE *a = fopen(from, "rb"), *b;
    char buf[65536];
    size_t n;

    if (a == NULL) {
        return -1;
    }
    b = fopen(to, "wb");
    if (b == NULL) {
        fclose(a);
        return -1;
    }
    while ((n = fread(buf, 1, sizeof buf, a)) > 0) {
        fwrite(buf, 1, n, b);
    }
    fclose(a);
    return fclose(b) == 0 ? 0 : -1;
}

static void dir_of(const char *path, char *out, size_t n)
{
    const char *slash = strrchr(path, '/'), *bs = strrchr(path, '\\');

    if (bs != NULL && (slash == NULL || bs > slash)) {
        slash = bs;
    }
    snprintf(out, n, "%.*s", slash != NULL ? (int)(slash - path) : 0, path);
}

/* The game itself in a window of its own, straight into a battle at this
 * place: the place's trainer for a trainer place, else a wild Pokemon of
 * the foe species chosen, the Pokemon chosen on the player's side.  A
 * preview uses the move chosen again and again (A pressed every second);
 * `play` leaves the battle to the player (keys, pad, mouse), to the end.
 * It runs on a copy of the player's save, settings and spots in a folder of
 * its own, so nothing of the player's changes; fast until the battle shows. */
static void preview_in_game(int play)
{
#ifdef _WIN32
    const BmapSpot *r = cur();
    char tmp[600], dir[600], from[700], to[700], exe[700], input[700], v[160];
    const char *base = getenv("TEMP"), *saves = getenv("BW_SAVE_DIR"), *app = getenv("APPDATA");
    FILE *f;
    int frame;

    if (r == NULL || base == NULL) {
        return;
    }
    snprintf(tmp, sizeof tmp, "%s\\eidos_spots_preview", base);
    (void)_mkdir(tmp);
    /* the copies: the save, the settings (the one-screen layout), the spots */
    if (saves != NULL && saves[0] != '\0') {
        snprintf(from, sizeof from, "%s\\pokeblack.sav", saves);
    } else {
        snprintf(from, sizeof from, "%s\\eidos\\saves\\pokeblack.sav", app != NULL ? app : ".");
    }
    snprintf(to, sizeof to, "%s\\pokeblack.sav", tmp);
    (void)copy_file(from, to);          /* the battle builds its own player: a save is optional */
    dir_of(eidos_settings_path(), dir, sizeof dir);
    snprintf(from, sizeof from, "%s\\settings.ini", dir);
    snprintf(to, sizeof to, "%s\\settings.ini", tmp);
    (void)copy_file(from, to);
    snprintf(to, sizeof to, "%s\\battle_spots.tsv", tmp);
    (void)copy_file(bmap_spots_user_path(), to);
    /* the input: the title's Continue, then (a preview) A every second
     * once the battle runs */
    snprintf(input, sizeof input, "%s\\preview_in.txt", tmp);
    if ((f = fopen(input, "w")) != NULL) {
        /* a preview's own presses only: the battle starts at once (no title) */
        for (frame = 120; !play && frame < 400000; frame += 60) {
            fprintf(f, "%d press A 4\n", frame);
        }
        fclose(f);
    }
    /* the battle and the place */
    if (r->trainer_id >= 100000) {
        /* a legendary: its species and level (battle_spots.py) */
        snprintf(v, sizeof v, "wild %d %d", r->trainer_id % 1000, r->trainer_id / 1000 % 100);
    } else if (r->kind == 'T' && r->trainer_id >= 0) {
        snprintf(v, sizeof v, "trainer %d", r->trainer_id);
    } else {
        /* dark grass: the double or the single, as the switch shows it */
        snprintf(v, sizeof v, "wild %d 50 %d", A.species[0], A.doubles ? 2 : 1);
    }
    _putenv_s("EIDOS_DEBUG_BATTLE", v);
    {
        float y;

        if (row_height(r, &y)) {
            snprintf(v, sizeof v, "%u %.0f %.0f %u %d %d 0 %.2f", r->zone, (double)A.tx, (double)A.tz,
                     deg_to_facing((float)A.tf), A.season, k_hours[A.hour], (double)y);
        } else {
            snprintf(v, sizeof v, "%u %.0f %.0f %u %d %d", r->zone, (double)A.tx, (double)A.tz,
                     deg_to_facing((float)A.tf), A.season, k_hours[A.hour]);
        }
    }
    _putenv_s("EIDOS_DEBUG_BMAP_AT", v);
    /* the view on show, whichever the game would take from this place */
    snprintf(v, sizeof v, "%d", view_no(A.row));
    _putenv_s("EIDOS_DEBUG_VIEW", v);
    /* a party of four (the one on show first) and a bag of balls and
     * healing items, to try switching and items; every badge, so every
     * Pokemon obeys */
    snprintf(v, sizeof v, "%d,497,503,571", A.species[1]);
    _putenv_s("EIDOS_DEBUG_PARTY", v);
    _putenv_s("EIDOS_DEBUG_LEVEL", "50");
    _putenv_s("EIDOS_DEBUG_ITEMS", "4:30,3:30,2:30,17:20,26:20,25:20,23:10,28:10,27:10");
    _putenv_s("EIDOS_DEBUG_UNLOCK", "1");
    if (A.scene >= 3 && A.scene < SCENE_COUNT && !play) {
        snprintf(v, sizeof v, "0:%d:0", A.scene - 2);
        _putenv_s("EIDOS_DEBUG_MOVES", v);
    }
    /* a preview skips to the battle; a battle to play shows its encounter
     * from the start */
    /* the battle alone: no logos, no title, no field (engine/ui/ui.c) */
    _putenv_s("EIDOS_BATTLE_ONLY", "1");
    _putenv_s("BW_SAVE_DIR", tmp);
    {
        /* shown in the middle view, not a window of its own */
        char name[96];

        preview_stop();
        A.emb = spots_embed_open(name, sizeof name);
        if (A.emb != NULL) {
            _putenv_s("EIDOS_EMBED", name);
        }
    }
    /* eidos_boot beside this program */
    {
        const char *self = SDL_GetBasePath();   /* this program's folder, with its slash */

        snprintf(exe, sizeof exe, "%seidos_boot.exe", self != NULL ? self : "");
    }
    A.emb_proc = _spawnl(_P_NOWAIT, exe, "\"eidos_boot.exe\"", "--layout", "one", "--window", "--input", input, NULL);
    if (A.emb_proc <= 0) {
        A.emb_proc = 0;
        preview_stop();
        say("Could not start %s", exe);
    } else {
        say("Starting the battle");
    }
    _putenv_s("EIDOS_EMBED", "");
    /* this program's own environment as it was */
    _putenv_s("EIDOS_DEBUG_BATTLE", "");
    _putenv_s("EIDOS_DEBUG_BMAP_AT", "");
    _putenv_s("EIDOS_DEBUG_PARTY", "");
    _putenv_s("EIDOS_DEBUG_LEVEL", "");
    _putenv_s("EIDOS_DEBUG_ITEMS", "");
    _putenv_s("EIDOS_DEBUG_UNLOCK", "");
    _putenv_s("EIDOS_DEBUG_VIEW", "");
    _putenv_s("EIDOS_DEBUG_MOVES", "");
    _putenv_s("EIDOS_BATTLE_ONLY", "");
    if (saves != NULL) {
        char keep[600];

        snprintf(keep, sizeof keep, "%s", saves);
        _putenv_s("BW_SAVE_DIR", keep);
    } else {
        _putenv_s("BW_SAVE_DIR", "");
    }
#else
    say("Preview in game runs on Windows for now");
#endif
}

/* The game played here ends: asked to, then ended if it lingers. */
static void preview_stop(void)
{
    if (A.emb != NULL) {
        A.emb->quit = 1;
    }
    if (A.emb_proc > 0) {
        spots_embed_kill(A.emb_proc);
        A.emb_proc = 0;
    }
    if (A.emb != NULL) {
        spots_embed_close(A.emb);
        A.emb = NULL;
    }
    free(A.emb_rgba);
    A.emb_rgba = NULL;
    A.emb_w = A.emb_h = 0;
    A.emb_frame = 0;
}

/* The game at (x, y), w x h: it renders at this size, its picture shows
 * here, and the keyboard and the mouse (over the picture) go to it. */
static void draw_embed(int x, int y, int w, int h)
{
    EidosEmbedShm *s = A.emb;
    int i;

    if (s == NULL) {
        return;
    }
    if (!spots_embed_alive(A.emb_proc)) {
        preview_stop();
        say("The game ended");
        return;
    }
    s->beat++;
    s->want_w = (uint32_t)(w > EIDOS_EMBED_MAX_W ? EIDOS_EMBED_MAX_W : w);
    s->want_h = (uint32_t)(h > EIDOS_EMBED_MAX_H ? EIDOS_EMBED_MAX_H : h);
    /* the keyboard, while this window has it */
    {
        const bool *keys = SDL_GetKeyboardState(NULL);
        int focus = SDL_GetKeyboardFocus() == A.win;

        for (i = 0; i < 16; i++) {
            uint32_t word = 0;
            int b;

            for (b = 0; b < 32; b++) {
                int sc = i * 32 + b;

                if (focus && keys != NULL && sc < SDL_SCANCODE_COUNT && keys[sc]) {
                    word |= 1u << b;
                }
            }
            s->keys[i] = word;
        }
    }
    /* the newest picture */
    if (s->frame != A.emb_frame && s->w > 0 && s->h > 0) {
        int pw = (int)s->w, ph = (int)s->h;
        size_t px = (size_t)pw * (size_t)ph;

        if (pw != A.emb_w || ph != A.emb_h) {
            free(A.emb_rgba);
            A.emb_rgba = (u8 *)malloc(px * 4u);
            A.emb_w = pw;
            A.emb_h = ph;
        }
        if (A.emb_rgba != NULL) {
            const u8 *src = s->pixels;
            size_t k;

            for (k = 0; k < px; k++) {
                A.emb_rgba[k * 4u] = src[k * 3u];
                A.emb_rgba[k * 4u + 1u] = src[k * 3u + 1u];
                A.emb_rgba[k * 4u + 2u] = src[k * 3u + 2u];
                A.emb_rgba[k * 4u + 3u] = 255;
            }
        }
        A.emb_frame = s->frame;
    }
    ui_fill(&C, x, y, w, h, 0x000000u, 255);
    if (A.emb_rgba != NULL) {
        /* as large as fits, its own shape kept */
        int dw = w, dh = A.emb_h * w / A.emb_w;

        if (dh > h) {
            dh = h;
            dw = A.emb_w * h / A.emb_h;
        }
        blit_rgba(x + (w - dw) / 2, y + (h - dh) / 2, A.emb_rgba, A.emb_w, A.emb_h, dw, dh);
        /* the mouse over the picture, in its pixels */
        {
            int px0 = x + (w - dw) / 2, py0 = y + (h - dh) / 2;

            if (hover(px0, py0, dw, dh)) {
                s->mx = (A.mx - px0) * A.emb_w / dw;
                s->my = (A.my - py0) * A.emb_h / dh;
                s->buttons = (A.down ? 1u : 0u) | (A.mdown ? 2u : 0u) | (A.rdown ? 4u : 0u);
                s->wheel += A.wheel;
            } else {
                s->mx = s->my = -1;
                s->buttons = 0;
            }
        }
    } else {
        text(x + U(24), y + U(24), C_MUTED, "Starting the game...");
    }
    outline(x, y, w, h, C_LINE);
}

/* --check-floors (tests): every trainer whose ground has another floor
 * over or under it, with the floor the trainer's height picks and the one
 * the lowest-surface rule would. */
static int floors_report(void)
{
    BmapScene base;
    unsigned zone = 0xFFFFFFFFu;
    float cx = 0.0f, cz = 0.0f;
    int i, have = 0, found = 0;

    memset(&base, 0, sizeof base);
    for (i = 0; i < bmap_spots_count(); i++) {
        const BmapSpot *r = bmap_spots_get(i);
        float x, z, y, g_ref, g_low;

        if (r->kind != 'T' || r->view != 0) {
            continue;
        }
        x = (float)r->tx * 16.0f + 8.0f;
        z = (float)r->tz * 16.0f + 8.0f;
        if (!have || zone != r->zone || (x - cx) * (x - cx) + (z - cz) * (z - cz) > 200.0f * 200.0f) {
            if (have) {
                bmap_free(&base);
                have = 0;
            }
            if (bmap_build(&base, r->zone, x, z, 0, 12, 0) != 0) {
                continue;
            }
            have = 1;
            zone = r->zone;
            cx = x;
            cz = z;
        }
        A.row = i;
        if (!row_height(r, &y)) {
            continue;
        }
        bmap_clear_ref(&base);
        g_low = bmap_ground_y(&base, x, z);
        bmap_set_ref(&base, y);
        g_ref = bmap_ground_y(&base, x, z);
        if (fabsf(g_ref - g_low) > 4.0f) {
            printf("%s\t%s\ttrainer y %.0f\tits floor %.0f\tlowest rule %.0f\n", r->id, r->label, (double)y,
                   (double)g_ref, (double)g_low);
            found++;
        }
    }
    if (have) {
        bmap_free(&base);
    }
    printf("%d trainers stand over or under another floor\n", found);
    return 0;
}

/* --check-area (tests): every wild place on Auto, placed as the battle
 * would be from the area's start; the ones where a Pokemon stands off the
 * area's grass. */
static int area_report(void)
{
    int i, bad = 0, total = 0;

    for (i = 0; i < bmap_spots_count(); i++) {
        const BmapSpot *r = bmap_spots_get(i);
        BmapPlaceSpec sp;
        float y, ox, oz, fx, fz;
        int in_o, in_f;

        if (r->kind == 'T' || r->mode != BMAP_SPOT_AUTO || r->view != 0) {
            continue;
        }
        A.row = i;
        A.tx = r->px;
        A.tz = r->pz;
        A.tf = r->pf;
        current_spec(&sp);
        ensure_base(&sp);
        if (!A.have_base) {
            continue;
        }
        scene_drop(&A.placed, &A.have_placed);
        if (row_height(r, &y)) {
            bmap_set_ref(&A.base, y);
        } else {
            bmap_clear_ref(&A.base);
        }
        if (bmap_copy(&A.placed, &A.base) != 0) {
            continue;
        }
        bmap_place_spec(&A.placed, &sp);
        A.have_placed = 1;
        battle_point_map(OWN_X, OWN_Z, &ox, &oz);
        battle_point_map(FOE_X, FOE_Z - A.placed.foe_dz, &fx, &fz);
        {
            int wild = 0;
            float x, z;

            for (z = sp.area[1] + 8.0f; z < sp.area[3] && !wild; z += 16.0f) {
                for (x = sp.area[0] + 8.0f; x < sp.area[2] && !wild; x += 16.0f) {
                    wild = bmap_tile_encounter(&A.base, x, z) != 0;
                }
            }
            in_o = (!wild || bmap_tile_encounter(&A.base, ox, oz)) && ox >= sp.area[0] && ox <= sp.area[2] && oz >= sp.area[1] && oz <= sp.area[3];
            in_f = (!wild || bmap_tile_encounter(&A.base, fx, fz)) && fx >= sp.area[0] && fx <= sp.area[2] && fz >= sp.area[1] && fz <= sp.area[3];
        }
        total++;
        if (!in_o || !in_f) {
            bad++;
            printf("%s\t%s\t%s%s\n", r->id, r->label, in_o ? "" : "yours off the grass ", in_f ? "" : "foe off the grass");
        }
    }
    printf("%d of %d auto wild places have a Pokemon off the area's grass\n", bad, total);
    return 0;
}

static void usage(void)
{
    printf("usage: eidos_spots [--data-root PATH] [--place ID] [--shot PATH]\n");
}

int main(int argc, char **argv)
{
    BwSetupRequest setup;
    char data_root[BW_SETUP_PATH_MAX];
    int check_floors = 0;
    const char *place = NULL, *shot = NULL, *script = NULL;
    int status = 0, i, win_w, win_h;

    eidos_settings_load();
    memset(&setup, 0, sizeof setup);
    argv = bw_os_utf8_argv(&argc, argv);
    for (i = 1; i < argc; i++) {
        int used = bw_setup_parse_arg(&setup, argc, argv, &i);

        if (used < 0) {
            usage();
            return 2;
        } else if (used > 0) {
            continue;
        } else if (strcmp(argv[i], "--place") == 0 && i + 1 < argc) {
            place = argv[++i];
        } else if (strcmp(argv[i], "--shot") == 0 && i + 1 < argc) {
            shot = argv[++i];
        } else if (strcmp(argv[i], "--script") == 0 && i + 1 < argc) {
            script = argv[++i];
        } else if (strcmp(argv[i], "--check-floors") == 0) {
            check_floors = 1;
        } else if (strcmp(argv[i], "--check-area") == 0) {
            check_floors = 2;
        } else {
            usage();
            return 2;
        }
    }
    if (!eidos_firstrun(&setup, data_root, sizeof data_root, &status)) {
        return status;
    }
    bw_set_data_root(data_root);
    if (check_floors == 1) {
        return floors_report();
    }
    if (check_floors == 2) {
        return area_report();
    }
    {
        /* the edits beside the game's settings: the game reads them too */
        char path[600];
        const char *set = eidos_settings_path();
        const char *slash = set != NULL ? strrchr(set, '/') : NULL, *bs = set != NULL ? strrchr(set, '\\') : NULL;

        if (bs != NULL && (slash == NULL || bs > slash)) {
            slash = bs;
        }
        if (slash != NULL) {
            snprintf(path, sizeof path, "%.*s/battle_spots.tsv", (int)(slash - set), set);
            bmap_spots_set_user_path(path);
        }
    }
    SDL_SetMainReady();
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        fprintf(stderr, "eidos_spots: %s\n", SDL_GetError());
        return 1;
    }
    {
        /* EIDOS_SPOTS_SIZE=WxH (tests): the window's size */
        const char *sz = getenv("EIDOS_SPOTS_SIZE");

        if (sz == NULL || sscanf(sz, "%dx%d", &win_w, &win_h) != 2) {
            win_w = 1680;
            win_h = 960;
        }
    }
    A.win = SDL_CreateWindow("eidos battle spots", win_w, win_h,
                             SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE
                             | (shot != NULL ? SDL_WINDOW_HIDDEN : SDL_WINDOW_MAXIMIZED));
    if (A.win == NULL) {
        fprintf(stderr, "eidos_spots: %s\n", SDL_GetError());
        return 1;
    }
    /* one OpenGL context for the window: the 3D pictures, the sprites and
     * the interface canvas are all drawn by the GPU (gpu_flush) */
    A.gl = eidos_gl_create(A.win);
    if (A.gl == NULL) {
        fprintf(stderr, "eidos_spots: no OpenGL 3.3: %s\n", SDL_GetError());
        return 1;
    }
    SDL_StartTextInput(A.win);
    A.zoom = 10.0f;
    A.follow = 1;
    A.hour = 1;
    A.species[0] = SPECIES_FOE;
    A.species[1] = SPECIES_OWN;
    A.tr_foe_id = -2;
    A.tr_own_gender = -2;
    A.tr_ally_id = -2;
    A.an_foe.key = A.an_own.key = -1;
    A.gender = -1;
    sprite_load(&A.foe, SPECIES_FOE, 0);
    sprite_load(&A.own, SPECIES_OWN, 1);
    for (i = 0; i < 4; i++) {
        float ww = 0.0f, wh = 0.0f;
        unsigned char *px = NULL;
        int pw = 0, ph = 0;

        if (bmap_npc_picture(1, i, &px, &pw, &ph, &ww, &wh) == 0) {     /* the hero */
            A.hero[i].rgba = px;
            A.hero[i].w = pw;
            A.hero[i].h = ph;
            A.hero_h = wh;
        }
    }
    refilter();
    last_place_read();
    if (place == NULL && g_last_place[0] != '\0') {
        place = g_last_place;           /* the place shown last */
    }
    select_row(0);
    if (place != NULL) {
        for (i = 0; i < bmap_spots_count(); i++) {
            if (strcmp(bmap_spots_get(i)->id, place) == 0 && bmap_spots_get(i)->view == 0) {
                select_row(i);
            }
        }
    }
    g_opened = 1;                       /* from now on a place shown is kept */
    A.play = 0;
    A.play_at = -1;
    A.scene = 3;                        /* move 1 */
    say("Saving to %s", bmap_spots_user_path());
    if (shot != NULL) {
        frame();
        frame();
        if (script != NULL) {
            run_script(script);
        }
        frame_shot(shot);
        return 0;
    }
    while (!A.quit) {
        SDL_Event ev;

        if (SDL_WaitEventTimeout(&ev, A.play || A.emb != NULL ? 1 : 50)) {
            SDL_PushEvent(&ev);
        } else if (!A.play && A.emb == NULL && ++A.idle_ticks < 10) {
            continue;       /* nothing happened: the window is as drawn (once in a while it is drawn anyway) */
        }
        A.idle_ticks = 0;
        pump();
        frame();
    }
    preview_stop();
    return 0;
}
