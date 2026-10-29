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
#define FK        2                 /* text at twice the game font's size  */
#define LINE_H    22

/* the battle: slots (battle units), the scale, the cameras */
#define OWN_X     0.5f
#define OWN_Z     7.0f
#define FOE_X     0.3f
#define FOE_Z     (-10.0f)
#define SPRITE_U  (1.0f / 13.5f)    /* battle units per sprite pixel at scale 1 */
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
} ClipFrame;

typedef struct View3d {
    int x, y, w, h;                 /* where the picture goes (window)     */
    int rw, rh;                     /* its render size                     */
    u8 *rgba;
    u8 *front;                      /* the map in front of the Pokemon (rgba, 0 alpha elsewhere) */
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
    int gender;                     /* your trainer: 0 the boy, 1 the girl (spot_editor.ini), -1 not read yet */
    int tr_own_gender;              /* the one tr_own shows, -2 none yet       */
    int species[2];                 /* the sprites shown: foe, yours         */
    /* the battle camera played (engine/world/bcam_sim.c): the scene chosen
     * (0 the wild intro, 1 the trainer intro, 2 idle, 3 and on the moves
     * from 1), who attacks, and the frames worked out for it */
    int scene, foe_attacks, play;
    int play_at;                    /* tests: the frame shown while playing, -1 by the clock */
    Uint32 play_t0;
    BcamFrame *sim;
    int nsim, sim_scene, sim_attacker;
    int doubles;                    /* show a double battle                  */
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
    int drag;                       /* 1 move, 2 turn, 3 area, 4 pan, 5 list scroll */
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
static const char *scene_name(int sc);
static void scene_frames(void);
#define SCENE_MOVES 559             /* Black's moves */
#define SCENE_COUNT (3 + SCENE_MOVES)
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

static void refilter(void)
{
    int n = bmap_spots_count(), i;

    free(A.vis);
    A.vis = (int *)malloc((size_t)(n > 0 ? n : 1) * sizeof *A.vis);
    A.nvis = 0;
    for (i = 0; A.vis != NULL && i < n; i++) {
        const BmapSpot *r = bmap_spots_get(i);
        int keep = r->view == 0;

        switch (A.filter) {
        case F_TRAINERS: keep = r->kind == 'T' && strstr(r->label, "(story)") == NULL && r->trainer_id < 100000; break;
        case F_LEGEND: keep = r->trainer_id >= 100000; break;
        case F_STORY: keep = strstr(r->label, "(story)") != NULL; break;
        case F_WILD: keep = r->kind == 'A'; break;
        case F_TODO: keep = !r->ok; break;
        case F_CHANGED: keep = changed_row(r) || bmap_spots_view_count(i) > 1; break;
        default: break;
        }
        if (keep && A.search[0] != '\0') {
            keep = ci_contains(r->label, A.search) || ci_contains(r->id, A.search);
        }
        if (keep) {
            A.vis[A.nvis++] = i;
        }
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

static void select_row(int row)
{
    BmapSpot *r;

    if (row < 0 || row >= bmap_spots_count()) {
        return;
    }
    A.row = row;
    r = cur();
    A.tx = r->px;
    A.tz = r->pz;
    A.tf = r->pf;
    A.follow = 1;
    A.need_place = 1;
    A.ow_reset = 1;
    A.show_tr = 0;
    A.tr_foe_id = -2;
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
        if (A.npcs[i].is_trainer && A.npcs[i].gx == r->tx && A.npcs[i].gz == r->tz) {
            *y = A.npcs[i].y;
            return 1;
        }
    }
    /* a leader: its battle starts from a script, the header does not call
     * it a trainer; whoever stands on the row's tile */
    for (i = 0; i < A.nnpc; i++) {
        if (A.npcs[i].gx == r->tx && A.npcs[i].gz == r->tz) {
            *y = A.npcs[i].y;
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
        float y;

        if (row_height(r, &y)) {
            bmap_set_ref(&A.base, y);
        } else {
            bmap_clear_ref(&A.base);
        }
    }
    if (bmap_copy(&A.placed, &A.base) == 0) {
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
        (void)bmap_npc_add_models(&A.obase, A.npcs, A.nnpc);   /* the NPCs drawn as 3D models */
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

static void render_view(View3d *v, const BmapScene *s, u32 gen, const float (*poke)[4], int npoke)
{
    G3dSnapshot snap;
    size_t need = (size_t)v->rw * (size_t)v->rh * 4u;
    int i;

    if (v->rw <= 0 || v->rh <= 0) {
        return;
    }
    v->rgba = (u8 *)realloc(v->rgba, need);
    if (v->rgba == NULL) {
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
    if (eidos_gl_render3d(A.gl, &snap, v->rw, v->rh) != 0 || eidos_gl_read3d(A.gl, v->rgba, v->rw, v->rh) != 0) {
        memset(v->rgba, 0, need);
    }
    /* the map in front of the Pokemon, alone over a clear background: laid
     * over the sprites, as the game's second pass draws it over its own */
    free(v->front);
    v->front = NULL;
    if (poke != NULL && npoke > 0 && (v->front = (u8 *)malloc(need)) != NULL) {
        for (i = 0; i < npoke && i < G3D_BACKDROP_SPRITES; i++) {
            memcpy(snap.backdrop.poke[i], poke[i], 4 * sizeof(float));    /* where, and its box's height */
        }
        snap.backdrop.n_poke = (u32)(npoke < G3D_BACKDROP_SPRITES ? npoke : G3D_BACKDROP_SPRITES);
        snap.backdrop.front_only = 1;
        if (eidos_gl_render3d(A.gl, &snap, v->rw, v->rh) != 0 || eidos_gl_read3d(A.gl, v->front, v->rw, v->rh) != 0) {
            free(v->front);
            v->front = NULL;
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
static int trainer_compose(const G2dRig *rig, G2dPose *out)
{
    enum { MAXU = 64, CANVAS = 320, ORG = 160 };
    const G2dMcGroup *grp = &rig->nmcr.groups[0];
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

        if (g2d_nanr_frame_data(&rig->nanr, nd->anim_seq_idx, 0, &fr) != 0 || fr.cell_idx >= rig->ncer.num_cells) {
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
        && (trainer ? trainer_compose(rig, &pose)
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

/* A trainer's class: byte 1 of its record in a/0/9/2.  -1 when unknown. */
static int trainer_class(int tid)
{
    BwArcFile arc;
    u8 *rec;
    long n;
    int cls = -1;

    if (tid < 0 || bw_arc_open(&arc, "a/0/9/2") != 0) {
        return -1;
    }
    n = bw_arc_member_size(&arc, (u32)tid);
    rec = n >= 2 ? (u8 *)malloc((size_t)n) : NULL;
    if (rec != NULL && bw_arc_read_member(&arc, (u32)tid, rec, (u32)n) >= 2) {
        cls = rec[1];
    }
    free(rec);
    bw_arc_close(&arc);
    return cls;
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
        if (fscanf(f, "player=%d", &g) != 1 || (g != 0 && g != 1)) {
            g = -1;
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

static void gender_write(void)
{
    char path[700];
    FILE *f;

    gender_path(path, sizeof path);
    f = fopen(path, "w");
    if (f != NULL) {
        fprintf(f, "player=%d\n", A.gender);
        fclose(f);
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
    }
    if (A.tr_own_gender != A.gender) {
        free(A.tr_own.rgba);
        trainer_load(&A.tr_own, A.gender, 1);
        A.tr_own_gender = A.gender;
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

/* A stepper's step: Ctrl the fine one, Shift the coarse one. */
static float step_of(float normal, float fine, float coarse)
{
    return A.ctrl ? fine : A.shift ? coarse : normal;
}

/* A number shown at (x, y) that a click lets the user type over: `label`
 * before it, `shown` the number as text.  Returns 1 with the number typed
 * in `out` once Enter is pressed (or the user clicks elsewhere); Esc
 * gives up.  `id` tells the fields apart (any number but 0). */
static int num_field(int id, int x, int y, const char *label, const char *shown, float *out)
{
    int lw = label[0] != '\0' ? text_w(label) + 8 : 0, w = text_w(shown) + 10;

    if (A.edit == id) {
        int bw = text_w(A.edit_buf) + 20 > 70 ? text_w(A.edit_buf) + 20 : 70;
        char *end = NULL;
        double v;

        if (lw > 0) {
            text(x, y, C_MUTED, "%s", label);
        }
        ui_fill(&C, x + lw - 4, y - 4, bw, 26, 0x111318u, 255);
        outline(x + lw - 4, y - 4, bw, 26, C_ACCENT);
        text(x + lw, y, C_TEXT, "%s_", A.edit_buf);
        if (A.edit_done || (A.released && !hover(x + lw - 4, y - 4, bw, 26))) {
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
    if (hover(x + lw - 4, y - 4, w, 26)) {
        outline(x + lw - 4, y - 4, w, 26, C_LINE);
        if (A.released && A.drag == 0) {
            A.edit = id;
            A.edit_done = 0;
            snprintf(A.edit_buf, sizeof A.edit_buf, "%s", shown[0] == '+' ? shown + 1 : shown);
        }
    }
    return 0;
}

/* the width num_field takes for `label` and `shown` */
static int num_field_w(const char *label, const char *shown)
{
    return text_w(label) + 8 + text_w(shown);
}

static void remember(void);
static void save_now(void);

/* - [label value] + for a setting of the place: steps of `step` (Ctrl
 * `fine`, Shift `coarse`), the value typed in on a click.  `lo` < `hi`
 * keeps it in [lo, hi]; `wrap` keeps degrees in -180 .. 180.  Returns the
 * width it took. */
static int stepper(int id, int x, int y, const char *label, const char *fmt, float *v, float step, float fine,
                   float coarse, float lo, float hi, int wrap)
{
    char shown[24];
    float typed, d = step_of(step, fine, coarse), nv = *v;
    int w, changed = 0;

    snprintf(shown, sizeof shown, fmt, (double)*v);
    w = num_field_w(label, shown);
    if (button(x, y, 30, 28, "-", 0)) {
        nv = *v - d;
        changed = 1;
    }
    if (num_field(id, x + 38, y + 4, label, shown, &typed)) {
        nv = typed;
        changed = 1;
    }
    if (button(x + 46 + w, y, 30, 28, "+", 0)) {
        nv = *v + d;
        changed = 1;
    }
    if (changed) {
        if (wrap) {
            nv = fmodf(nv, 360.0f);
            nv = nv > 180.0f ? nv - 360.0f : nv <= -180.0f ? nv + 360.0f : nv;
        }
        if (lo < hi) {
            nv = nv < lo ? lo : nv > hi ? hi : nv;
        }
        if (nv != *v) {
            remember();
            *v = nv;
            save_now();
        }
    }
    return 46 + w + 30;
}

static void blit_rgba(int dx, int dy, const u8 *src, int sw, int sh, int dw, int dh)
{
    EidosUiCanvas *cv = &A.canvas;
    int x, y;

    for (y = 0; y < dh; y++) {
        int py = dy + y, sy = y * sh / dh;

        if (py < 0 || py >= cv->h) {
            continue;
        }
        for (x = 0; x < dw; x++) {
            int px = dx + x, sx = x * sw / dw;
            const u8 *s = src + ((size_t)sy * (size_t)sw + (size_t)sx) * 4u;
            u8 *d;

            if (px < 0 || px >= cv->w || s[3] == 0) {
                continue;
            }
            d = cv->rgba + ((size_t)py * (size_t)cv->w + (size_t)px) * 4u;
            d[0] = s[0];
            d[1] = s[1];
            d[2] = s[2];
            d[3] = 255;
        }
    }
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
        A.drag = 2;
        remember();
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
/* The left column                                                     */
/* ------------------------------------------------------------------ */

static void draw_left(int L)
{
    BmapSpot *r = cur();
    int x = 16, y = 14, w = L - 32, i, done = 0, n = 0;
    int rows, list_y, list_h, row_h = 26;

    ui_fill(&C, 0, 0, L, A.H, C_PANEL, 255);
    ui_fill(&C, L - 1, 0, 1, A.H, C_LINE, 255);
    for (i = 0; i < bmap_spots_count(); i++) {
        const BmapSpot *q = bmap_spots_get(i);

        n += q->view == 0;
        done += q->view == 0 && q->ok;
    }
    /* the place */
    if (r != NULL) {
        char kind[64];

        ui_text(&C, x, y, r->label, C_TEXT, -1, UI_FONT_LARGE, 2);
        y += 36;
        snprintf(kind, sizeof kind, "%s  %s  zone %u", r->kind == 'T' ? "Trainer" : "Wild area", r->id, r->zone);
        text(x, y, C_MUTED, "%s", kind);
        text(L - 16 - text_w("000 / 000 checked"), y, C_MUTED, "%d / %d checked", done, n);
        y += 30;
    }
    if (button(x, y, 90, 30, "< Prev", 0)) {
        int p = vis_pos(A.row);

        if (p > 0) {
            select_row(A.vis[p - 1]);
        }
    }
    if (button(x + 98, y, 90, 30, "Next >", 0)) {
        int p = vis_pos(A.row);

        if (p >= 0 && p + 1 < A.nvis) {
            select_row(A.vis[p + 1]);
        } else if (A.nvis > 0) {
            select_row(A.vis[0]);
        }
    }
    if (r != NULL && button(x + w - 150, y, 150, 30, bmap_spots_get(first_view(A.row))->ok ? "Checked" : "Mark checked",
                            bmap_spots_get(first_view(A.row))->ok)) {
        BmapSpot *b = bmap_spots_get(first_view(A.row));

        b->ok = !b->ok;
        save_now();
        refilter();
    }
    y += 42;
    /* the filters and the search */
    {
        int fx = x;

        for (i = 0; i < F_COUNT; i++) {
            int bw = text_w(k_filters[i]) + 14;

            if (button(fx, y, bw, 28, k_filters[i], A.filter == i)) {
                A.filter = i;
                A.scroll = 0;
                refilter();
            }
            fx += bw + 4;
        }
        y += 36;
        ui_fill(&C, x, y, w, 30, 0x111318u, 255);
        outline(x, y, w, 30, A.search[0] != '\0' ? C_ACCENT : C_LINE);
        if (A.search[0] != '\0') {
            text(x + 8, y + 5, C_TEXT, "%s_", A.search);
        } else {
            text(x + 8, y + 5, C_MUTED, "Type to search places (Esc clears)");
        }
        text(x + w - 8 - text_w("000 places"), y + 5, C_MUTED, "%d places", A.nvis);
        y += 36;
    }
    /* the list */
    list_y = y;
    rows = (int)((A.H - list_y - 374 - (A.H - list_y - 374) * 6 / 10) / row_h);   /* 374: the controls under the map */
    if (rows < 4) {
        rows = 4;
    }
    if (rows > 14) {
        rows = 14;
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
    ui_fill(&C, x, list_y, w, list_h, 0x16181Du, 255);
    for (i = 0; i < rows && A.scroll + i < A.nvis; i++) {
        int ri = A.vis[A.scroll + i], ry = list_y + i * row_h;
        const BmapSpot *q = bmap_spots_get(ri);
        int sel = ri == first_view(A.row), hv = hover(x, ry, w, row_h);
        u32 dot = q->ok ? C_GOOD : changed_row(q) ? C_ACCENT : 0x4A4F5Bu;
        char lab[96];

        if (sel || hv) {
            ui_fill(&C, x, ry, w, row_h, sel ? 0x2C3A55u : 0x22262Eu, 255);
        }
        ui_ellipse(&C, x + 8, ry + 9, 9, 9, dot, UI_NONE, 255);
        snprintf(lab, sizeof lab, "%s", q->label);
        while (text_w(lab) > w - 40 && strlen(lab) > 4) {
            lab[strlen(lab) - 4] = '\0';
            strcat(lab, "...");
        }
        text(x + 26, ry + 3, sel ? C_TEXT : 0xC9CDD5u, "%s", lab);
        if (hv && A.released && A.drag == 0) {
            select_row(ri);
        }
    }
    if (A.nvis > rows) {
        int bh = list_h * rows / A.nvis, by = list_y + (list_h - bh) * A.scroll / (A.nvis - rows);

        ui_fill(&C, x + w - 4, by, 4, bh, C_LINE, 255);
    }
    y = list_y + list_h + 12;
    /* the map */
    A.map_x = x;
    A.map_y = y;
    A.map_s = A.H - y - 366;
    if (A.map_s > w - 150) {
        A.map_s = w - 150;
    }
    if (A.map_s < 160) {
        A.map_s = 160;
    }
    draw_map();
    if (!A.follow && button(x + A.map_s - 92, y + 6, 86, 26, "Center", 0)) {
        A.follow = 1;
    }
    {
        /* the legend and the mouse, beside the map */
        int lx = x + A.map_s + 14, ly = y + 4;

        if (lx + 140 < x + w) {
            ring((float)lx + 8.0f, (float)ly + 10.0f, 7.0f, C_OWN);
            text(lx + 22, ly, C_MUTED, "Yours");
            ring((float)lx + 8.0f, (float)ly + 34.0f, 7.0f, C_FOE);
            text(lx + 22, ly + 24, C_MUTED, "Foe");
            ui_ellipse(&C, lx + 3, ly + 52, 11, 11, C_CAM, UI_INK, 255);
            text(lx + 22, ly + 48, C_MUTED, "Camera");
            ui_ellipse(&C, lx + 3, ly + 76, 11, 11, C_TEXT, UI_INK, 255);
            text(lx + 22, ly + 72, C_MUTED, "Player");
            ui_fill(&C, lx + 3, ly + 100, 11, 11, C_FOE, 255);
            text(lx + 22, ly + 96, C_MUTED, "Trainer");
            ui_fill(&C, lx + 3, ly + 124, 11, 11, 0x5C9A4Eu, 255);
            text(lx + 22, ly + 120, C_MUTED, "Grass");
            ly += 156;
            {
                static const char *help[5][2] = { { "Drag", "move" }, { "Right drag", "turn" },
                                                  { "Shift drag", "safe area" }, { "Wheel", "zoom" },
                                                  { "Middle drag", "pan" } };
                int hi;

                for (hi = 0; hi < 5 && ly + 40 <= y + A.map_s; hi++, ly += 44) {
                    text(lx, ly, C_MUTED, "%s", help[hi][0]);
                    text(lx, ly + 19, C_TEXT, "%s", help[hi][1]);
                }
            }
        }
    }
    y += A.map_s + 12;
    /* the controls */
    if (r == NULL) {
        return;
    }
    text(x, y + 6, C_MUTED, "Place");
    {
        int bx = x + 110, bw = (w - 110 - 12) / 3;

        if (button(bx, y, bw, 30, "Auto", r->mode == BMAP_SPOT_AUTO)) set_mode(BMAP_SPOT_AUTO);
        if (button(bx + bw + 6, y, bw, 30, "Fixed spot", r->mode == BMAP_SPOT_FIXED)) set_mode(BMAP_SPOT_FIXED);
        if (button(bx + 2 * (bw + 6), y, bw, 30, "Safe area", r->mode == BMAP_SPOT_ZONE)) set_mode(BMAP_SPOT_ZONE);
    }
    y += 38;
    text(x, y + 6, C_MUTED, "Angle");
    {
        int bx = x + 110;
        int ang = r->mode == BMAP_SPOT_AUTO || r->angle < 0.0f ? A.tf : (int)r->angle;
        const char *who = r->mode == BMAP_SPOT_AUTO || r->angle < 0.0f ? "player faces" : "fixed";

        int minus = button(bx, y, 40, 30, "-", 0), plus = button(bx + 46, y, 40, 30, "+", 0);
        char as[16];
        float typed;

        snprintf(as, sizeof as, "%d", ang);
        if (num_field(1, bx + 96, y + 5, "", as, &typed)) {
            if (r->mode == BMAP_SPOT_AUTO || r->angle < 0.0f) {
                A.tf = norm_deg(typed);
                A.need_place = 1;
            } else {
                remember();
                r->angle = (float)norm_deg(typed);
                save_now();
            }
        }
        int quick = 0, qx = bx, q;
        static const int k_quick[4] = { 180, 90, -90, -180 };
        static const char *k_quick_label[4] = { "-180", "-90", "+90", "+180" };

        /* quarter and half turns, either way, on a row of their own */
        for (q = 0; q < 4; q++) {
            if (button(qx, y + 34, 64, 28, k_quick_label[q], 0)) {
                quick = k_quick[q];
            }
            qx += 70;
        }
        if (minus || plus || quick != 0) {
            int d = quick != 0 ? quick : (int)step_of(5.0f, 1.0f, 45.0f) * (minus ? 1 : -1);    /* minus turns left: north to west */

            if (r->mode == BMAP_SPOT_AUTO || r->angle < 0.0f) {
                A.tf = norm_deg((float)(A.tf + d));
                A.need_place = 1;
            } else {
                remember();
                r->angle = (float)norm_deg(r->angle + (float)d);
                save_now();
            }
        }
        text(bx + 96 + text_w(as) + 12, y + 5, C_TEXT, "%s (%s)", dir_name(ang), who);
        if (r->mode == BMAP_SPOT_ZONE && r->angle >= 0.0f && button(x + w - 150, y, 150, 30, "Follow player", 0)) {
            remember();
            r->angle = -1.0f;
            save_now();
        }
    }
    y += 72;
    text(x, y + 6, C_MUTED, "Foe");
    {
        int bx = x + 110;
        char d[48];

        text(bx, y + 5, C_MUTED, "on its ground");
        if (button(bx + 128, y, 40, 30, "-", 0)) {
            remember();
            r->foe_dz = clampf(r->foe_dz - step_of(0.5f, 0.1f, 2.0f), -12.0f, 12.0f);   /* 5 to 29 apart */
            save_now();
        }
        snprintf(d, sizeof d, "%.1f", (double)(17.0f + r->foe_dz));
        {
            float typed;

            if (num_field(2, bx + 176, y + 5, "", d, &typed)) {
                remember();
                r->foe_dz = clampf(typed - 17.0f, -12.0f, 12.0f);
                save_now();
            }
        }
        if (button(bx + 182 + text_w(d), y, 40, 30, "+", 0)) {
            remember();
            r->foe_dz = clampf(r->foe_dz + step_of(0.5f, 0.1f, 2.0f), -12.0f, 12.0f);
            save_now();
        }
        text(bx + 230 + text_w(d), y + 5, C_MUTED, "apart");
    }
    y += 38;
    /* the intro camera, apart from the command camera (the intro view
     * shows it; dragging that view changes it too) */
    text(x, y + 6, C_MUTED, "Intro");
    {
        int bx = x + 110, col = (w - 110) / 2;
        BmapIntro *in = &r->intro;

        stepper(10, bx, y, "zoom", "%.2f", &in->zoom, 0.1f, 0.01f, 0.5f, 0.05f, 50.0f, 0);
        stepper(11, bx + col, y, "turn", "%+.0f", &in->yaw, 5.0f, 1.0f, 30.0f, 0.0f, 0.0f, 1);
        y += 34;
        stepper(12, bx, y, "height", "%+.1f", &in->lift, 0.5f, 0.1f, 2.0f, 0.0f, 0.0f, 0);
        stepper(13, bx + col, y, "slide", "%+.1f", &in->side, 0.5f, 0.1f, 2.0f, 0.0f, 0.0f, 0);
        y += 34;
        stepper(14, bx, y, "tilt", "%+.0f", &in->tilt, 2.0f, 0.5f, 10.0f, -85.0f, 85.0f, 0);
        stepper(15, bx + col, y, "aim", "%+.0f", &in->aim, 2.0f, 0.5f, 10.0f, 0.0f, 0.0f, 1);
        if (button(x, y - 34, 90, 28, "Reset", 0) && !bmap_intro_none(in)) {
            remember();
            memset(in, 0, sizeof *in);
            in->zoom = 1.0f;
            save_now();
            say("Intro camera reset");
        }
    }
    y += 38;
    if (button(x, y, 150, 30, "Back to auto", 0)) {
        remember();
        r->mode = BMAP_SPOT_AUTO;
        r->foe_level = 1;
        r->foe_dz = 0.0f;
        memset(&r->intro, 0, sizeof r->intro);
        r->intro.zoom = 1.0f;
        memset(&r->cam, 0, sizeof r->cam);
        r->cam.zoom = 1.0f;
        memset(r->trainer_foe, 0, sizeof r->trainer_foe);
        memset(r->trainer_own, 0, sizeof r->trainer_own);
        save_now();
        say("Back to auto");
    }
    if (button(x + 158, y, 120, 30, "Default", 0)) {
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
    if (button(x + 286, y, 90, 30, "Undo", 0)) {
        undo();
    }
    if (button(x + 384, y, 110, 30, "Reverse", 0)) {
        reverse_battle();
    }
    /* the warnings and the save state */
    y += 40;
    if (A.have_placed && A.have_base) {
        float step = fabsf(A.placed.place_g[1] - A.placed.place_g[0]) / 16.0f, fx, fz;
        int to, tf;

        battle_point_map(FOE_X, FOE_Z - A.placed.foe_dz, &fx, &fz);
        to = bmap_tile(&A.base, A.placed.place_x, A.placed.place_z);
        tf = bmap_tile(&A.base, fx, fz);
        if (to == BMAP_TILE_BLOCKED || to == BMAP_TILE_NONE) {
            text(x, y, C_BAD, "Your Pokemon stands on a wall or outside the map");
        } else if (tf == BMAP_TILE_BLOCKED || tf == BMAP_TILE_NONE) {
            text(x, y, C_BAD, "The foe stands on a wall or outside the map");
        } else {
            text(x, y, C_MUTED, "Ground step %.1f tiles", (double)step);
        }
    }
    if (A.status[0] != '\0' && SDL_GetTicks() - A.status_at < 4000) {
        text(x, y + 24, C_GOOD, "%s", A.status);
    } else if (r->mode == BMAP_SPOT_ZONE) {
        /* how a safe area places a battle */
        text(x, y + 24, C_MUTED, "Yours: where the battle starts (drag the player), kept in the box");
    } else if (r->mode == BMAP_SPOT_AUTO) {
        text(x, y + 24, C_MUTED, "Yours: where the battle starts (drag the player), moved to a clear spot");
    } else {
        text(x, y + 24, C_MUTED, "Every battle here uses this spot and angle");
    }
}

/* ------------------------------------------------------------------ */
/* The right column                                                    */
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

/* A picture standing at p (its bottom centre) through the view, `bh` battle
 * units tall. */
static void draw_standing(const View3d *v, const u8 *rgba, int w, int h, const float p[3], float bh);

static void draw_standing(const View3d *v, const u8 *rgba, int w, int h, const float p[3], float bh)
{
    float sx, sy, unit;
    int dw, dh;
    Sprite tmp, *s = &tmp;

    tmp.w = w;
    tmp.h = h;
    tmp.rgba = (u8 *)rgba;
    if (s->rgba == NULL || h <= 0 || !project(v, p, &sx, &sy, &unit)) {
        return;
    }
    dh = (int)(bh * unit);
    dw = dh * w / h;
    if (dw <= 0 || dh <= 0 || dw > 4000 || dh > 4000) {
        return;
    }
    {
        EidosUiRect saved;

        ui_push(&C, 0, 0, &saved);
        ui_clip(&C, v->x, v->y, v->w, v->h);
        /* the canvas clip does not reach blit_rgba: clip by hand */
        {
            int x0 = (int)sx - dw / 2, y0 = (int)sy - dh;
            EidosUiCanvas *cv = &A.canvas;
            int yy, xx;

            for (yy = 0; yy < dh; yy++) {
                int py = y0 + yy, ty = yy * s->h / dh;

                if (py < v->y || py >= v->y + v->h || py >= cv->h) {
                    continue;
                }
                for (xx = 0; xx < dw; xx++) {
                    int px = x0 + xx, txx = xx * s->w / dw;
                    const u8 *src = s->rgba + ((size_t)ty * (size_t)s->w + (size_t)txx) * 4u;
                    u8 *d;

                    if (px < v->x || px >= v->x + v->w || px >= cv->w || src[3] == 0) {
                        continue;
                    }
                    d = cv->rgba + ((size_t)py * (size_t)cv->w + (size_t)px) * 4u;
                    d[0] = src[0];
                    d[1] = src[1];
                    d[2] = src[2];
                }
            }
        }
        ui_pop(&C, 0, 0, &saved);
    }
}

/* The place's offset of the foe's trainer, or of yours: right, up, back. */
static const float *view_offsets(int foe)
{
    const BmapSpot *r = cur();

    return foe ? r->trainer_foe : r->trainer_own;
}

#define STRIP_ROWS 5                /* the rows of settings under the two battle views */

/* The views of the place, the command camera and the trainers' places: rows
 * of buttons and steppers at (x, y), RW wide. */
static void draw_strip(int x, int y, int RW)
{
    BmapSpot *r = cur();
    int nv, vn, v, bx = x, i, col;
    float *tr;

    if (r == NULL) {
        return;
    }
    /* the views of the place: the game takes the one nearest the player */
    nv = bmap_spots_view_count(A.row);
    vn = view_no(A.row);
    text(bx, y + 5, C_MUTED, "Views");
    bx += 110;
    for (v = 0; v < nv; v++) {
        char num[16];

        snprintf(num, sizeof num, "%d", v + 1);
        if (button(bx, y, 34, 28, num, v == vn) && v != vn) {
            select_row(bmap_spots_view_at(A.row, v));
        }
        bx += 38;
    }
    if (nv < BMAP_VIEWS_MAX && button(bx, y, 100, 28, "Add view", 0)) {
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
    }
    bx += 108;
    if (nv > 1 && button(bx, y, 90, 28, "Delete", 0)) {
        int first = first_view(A.row);

        if (bmap_spots_view_delete(first, vn) == 0) {
            A.nundo = 0;
            select_row(bmap_spots_view_at(first, vn > 0 ? vn - 1 : 0));
            save_now();
            say("View deleted");
        }
    }
    bx += 98;
    if (nv > 1) {
        BmapSpot *b = bmap_spots_get(first_view(A.row));
        int pick = bmap_spots_pick_view(A.row, A.tx, A.tz, deg_to_facing((float)A.tf));

        if (button(bx, y, 150, 28, b->pick_angle ? "By angle" : "By spot", 0)) {
            remember();
            b->pick_angle = !b->pick_angle;
            save_now();
        }
        bx += 160;
        text(bx, y + 5, pick == A.row ? C_GOOD : C_MUTED, "Game takes view %d for the player", view_no(pick) + 1);
    } else {
        text(bx, y + 5, C_MUTED, "One view: add more to switch by where the player stands");
    }
    y += 34;
    /* the command camera */
    text(x, y + 5, C_MUTED, "Camera");
    bx = x + 110;
    col = 14;
    bx += stepper(20, bx, y, "zoom", "%.2f", &r->cam.zoom, 0.1f, 0.01f, 0.5f, 0.05f, 50.0f, 0) + col;
    bx += stepper(21, bx, y, "turn", "%+.0f", &r->cam.yaw, 5.0f, 1.0f, 30.0f, 0.0f, 0.0f, 1) + col;
    bx += stepper(22, bx, y, "height", "%+.1f", &r->cam.lift, 0.5f, 0.1f, 2.0f, 0.0f, 0.0f, 0) + col;
    stepper(23, bx, y, "slide", "%+.1f", &r->cam.side, 0.5f, 0.1f, 2.0f, 0.0f, 0.0f, 0);
    y += 34;
    bx = x + 110;
    bx += stepper(24, bx, y, "tilt", "%+.0f", &r->cam.tilt, 2.0f, 0.5f, 10.0f, -85.0f, 85.0f, 0) + col;
    bx += stepper(25, bx, y, "aim", "%+.0f", &r->cam.aim, 2.0f, 0.5f, 10.0f, 0.0f, 0.0f, 1) + col;
    if (button(bx, y, 100, 28, "Reset", 0) && !bmap_intro_none(&r->cam)) {
        remember();
        memset(&r->cam, 0, sizeof r->cam);
        r->cam.zoom = 1.0f;
        save_now();
        say("Camera reset");
    }
    text(bx + 112, y + 5, C_MUTED, "Drag a view: turn, height. Right drag: slide, tilt. Wheel: zoom");
    y += 34;
    /* the trainers' places (a change shows the trainers) */
    for (i = 1; i >= 0; i--) {
        float before;

        tr = i ? r->trainer_foe : r->trainer_own;
        before = tr[0] + 3.0f * tr[1] + 7.0f * tr[2];
        text(x, y + 5, C_MUTED, i ? "Foe trainer" : "Your trainer");
        bx = x + 110;
        bx += stepper(26 + 3 * i, bx, y, "right", "%+.1f", &tr[0], 0.5f, 0.1f, 2.0f, -40.0f, 40.0f, 0) + col;
        bx += stepper(27 + 3 * i, bx, y, "up", "%+.1f", &tr[1], 0.5f, 0.1f, 2.0f, -40.0f, 40.0f, 0) + col;
        bx += stepper(28 + 3 * i, bx, y, "back", "%+.1f", &tr[2], 0.5f, 0.1f, 2.0f, -40.0f, 40.0f, 0) + col;
        if (before != tr[0] + 3.0f * tr[1] + 7.0f * tr[2]) {
            A.show_tr = 1;
        }
        if (i == 1 && button(bx, y, 190, 28, "Show trainers", A.show_tr)) {
            A.show_tr = !A.show_tr;
            A.battle[0].dirty = A.battle[1].dirty = 1;
        }
        if (i == 0) {                       /* the player's own picture: boy or girl */
            if (button(bx, y, 74, 28, "Boy", A.gender == 0) || button(bx + 80, y, 74, 28, "Girl", A.gender == 1)) {
                A.gender = hover(bx, y, 74, 28) ? 0 : 1;
                gender_write();
                A.show_tr = 1;
                A.battle[0].dirty = A.battle[1].dirty = 1;
            }
        }
        y += 34;
    }
    (void)RW;
}

static void draw_right(int L)
{
    int RW = A.W - L, half = A.H * 58 / 100, hx = L, head = 40, i;
    const BmapSpot *r = cur();

    trainer_refresh();
    ui_fill(&C, L, 0, RW, A.H, C_BG, 255);
    /* the battle */
    {
        /* the two cameras side by side: the command menu's, the intro's */
        int cw = (RW - 24) / 2;


        text(hx + 16, 10, C_TEXT, "Command camera");
        {
            /* the sprites shown, and single or double */
            int sx0 = hx + 16, side, ry = half - 36;   /* a row under the two views */
            static const char *who[2] = { "Foe", "Yours" };

            if (button(sx0, ry, 90, 28, A.doubles ? "Double" : "Single", A.doubles)) {
                A.doubles = !A.doubles;
            }
            sx0 += 100;
            for (side = 0; side < 2; side++) {
                char num[16];
                int step = A.shift ? 10 : 1, ch = 0;

                snprintf(num, sizeof num, "#%d", A.species[side]);
                sx0 = text(sx0, ry + 4, C_MUTED, "%s", who[side]) + 6;
                if (button(sx0, ry, 28, 28, "-", 0)) {
                    A.species[side] = A.species[side] - step < 1 ? 649 : A.species[side] - step;
                    ch = 1;
                }
                sx0 = text(sx0 + 34, ry + 4, C_TEXT, "%s", num) + 6;
                if (button(sx0, ry, 28, 28, "+", 0)) {
                    A.species[side] = A.species[side] + step > 649 ? 1 : A.species[side] + step;
                    ch = 1;
                }
                sx0 += 40;
                if (ch) {
                    Sprite *s = side == 0 ? &A.foe : &A.own;

                    free(s->rgba);
                    sprite_load(s, A.species[side], side == 1);
                }
            }
        }
        text(hx + 16 + cw + 8, 10, C_TEXT, "Intro camera");
        int vh = half - head - 80 - 34 * STRIP_ROWS - 6;

        fit_view(&A.battle[0], hx + 8, head, cw, vh);
        fit_view(&A.battle[1], hx + 16 + cw, head, cw, vh);
        draw_strip(hx + 16, half - 76 - 34 * STRIP_ROWS, RW - 32);
        {
            /* the battle camera, played in the left view: the scene chosen
             * with the arrows (Shift: ten moves), looping until Stop */
            int px = hx + 16, py = half - 72, nw = 230, step = A.shift ? 10 : 1;
            const char *nm = scene_name(A.scene);

            px = text(px, py + 4, C_MUTED, "Play") + 10;
            if (button(px, py, 30, 28, "<", 0)) {
                A.scene = (A.scene - step + SCENE_COUNT) % SCENE_COUNT;
                A.play_t0 = SDL_GetTicks();
            }
            ui_fill(&C, px + 36, py, nw, 28, 0x111318u, 255);
            outline(px + 36, py, nw, 28, C_LINE);
            text(px + 36 + (nw - text_w(nm)) / 2, py + 4, C_TEXT, "%s", nm);
            if (button(px + 42 + nw, py, 30, 28, ">", 0)) {
                A.scene = (A.scene + step) % SCENE_COUNT;
                A.play_t0 = SDL_GetTicks();
            }
            px += 84 + nw;
            if (button(px, py, 80, 28, A.play ? "Stop" : "Play", A.play)) {
                A.play = !A.play;
                A.play_t0 = SDL_GetTicks();
                A.battle[0].dirty = 1;
            }
            px += 88;
            if (A.scene >= 3 && button(px, py, 130, 28, A.foe_attacks ? "Foe attacks" : "Yours attacks", 0)) {
                A.foe_attacks = !A.foe_attacks;
                A.play_t0 = SDL_GetTicks();
            }
            px += 138;
            if (button(px, py, 170, 28, "Preview in game", 0)) {
                preview_in_game(0);
            }
            px += 178;
            if (button(px, py, 130, 28, "Play battle", 0)) {
                preview_in_game(1);
            }
        }
    }
    /* the field */
    text(hx + 16, half + 10, C_TEXT, "Overworld");
    {
        int bx = hx + 150;

        for (i = 0; i < 4; i++) {
            int bw = text_w(k_seasons[i]) + 16;

            if (button(bx, half + 6, bw, 28, k_seasons[i], A.season == i)) {
                A.season = i;
                A.need_place = 1;
            }
            bx += bw + 4;
        }
        bx += 12;
        for (i = 0; i < 4; i++) {
            int bw = text_w(k_hour_names[i]) + 16;

            if (button(bx, half + 6, bw, 28, k_hour_names[i], A.hour == i)) {
                A.hour = i;
                A.need_place = 1;
            }
            bx += bw + 4;
        }
    }
    fit_view(&A.over, hx + 8, half + head, RW - 16, A.H - half - head - 8);
    if (button(hx + RW - 190, half + 6, 174, 28, "Field camera", 0)) {
        A.ow_reset = 1;
        A.need_place = 1;
    }
    ui_fill(&C, L, half, RW, 1, C_LINE, 255);

    if (A.need_place && r != NULL) {
        place_all();
    }
    /* the battle picture */
    for (i = 0; A.have_placed && i < 2; i++) {
        View3d *v = &A.battle[i];
        const ClipFrame *cf = i == 0 ? play_frame() : NULL;
        /* the Pokemon: the single battle's two, the double's four (ov094's
         * slot tables) or where a played camera has them, all on the
         * place's foe side (bmap_battle_warp) */
        static const float dbl[4][3] = { { -1.69f, 0.4f, 6.38f }, { 2.5f, 0.4f, 7.62f },
                                         { 2.43f, 0.4f, -10.38f }, { -2.2f, 0.4f, -11.56f } };
        float p[16][3], d[16], sc[16];
        const Sprite *sp[16];
        int kind[16];                   /* 0 a Pokemon, 1 your trainer, 2 the foe's */
        int n = 0, a, b;

        if (cf != NULL) {
            memcpy(v->eye, cf->eye, sizeof v->eye);
            memcpy(v->at, cf->at, sizeof v->at);
            v->dirty = 1;
            for (a = 0; a < cf->np && n < 16; a++) {
                int tr = cf->slot[a] > 7;

                if (tr && (cf->slot[a] > 13 || (cf->slot[a] & 1 ? A.tr_foe.rgba : A.tr_own.rgba) == NULL)) {
                    continue;
                }
                memcpy(p[n], cf->pos[a], sizeof p[n]);
                kind[n] = tr ? 1 + (cf->slot[a] & 1) : 0;
                sc[n] = slot_scale(cf->slot[a]);
                sp[n++] = tr ? ((cf->slot[a] & 1) ? &A.tr_foe : &A.tr_own) : (cf->slot[a] & 1) ? &A.foe : &A.own;
            }
        } else {
            memcpy(v->eye, k_cam[i][0], sizeof v->eye);
            memcpy(v->at, k_cam[i][1], sizeof v->at);
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
            if (A.show_tr) {
                /* a trainer stands where its side's Pokemon would: the trainer
                 * comes first in the battle (slot 8 yours, slot 9 the foe's) */
                for (a = 1; a >= 0; a--) {
                    const Sprite *t = a ? &A.tr_foe : &A.tr_own;
                    const Sprite *mon = a ? &A.foe : &A.own;
                    int m, at = n;

                    if (t->rgba == NULL || bcam_sim_slot(8 + a, p[n]) != 0) {
                        continue;
                    }
                    for (m = 0; m < n; m++) {
                        if (sp[m] == mon) {
                            memmove(&p[m], &p[m + 1], (size_t)(n - m - 1) * sizeof p[0]);
                            memmove(&sp[m], &sp[m + 1], (size_t)(n - m - 1) * sizeof sp[0]);
                            memmove(&kind[m], &kind[m + 1], (size_t)(n - m - 1) * sizeof kind[0]);
                            memmove(&sc[m], &sc[m + 1], (size_t)(n - m - 1) * sizeof sc[0]);
                            n--;
                            m--;
                            at = n;
                        }
                    }
                    if (bcam_sim_slot(8 + a, p[at]) == 0) {
                        sp[at] = t;
                        kind[at] = 1 + a;
                        sc[at] = slot_scale(8 + a);
                        n = at + 1;
                    }
                }
            }
        }
        for (a = 0; a < n; a++) {
            bmap_battle_warp(p[a], A.placed.foe_dy, A.placed.foe_dz);
            if (kind[a] != 0) {
                /* the place's offset for this trainer: right, up, back */
                const float *o = view_offsets(kind[a] == 2);

                p[a][0] += o[0];
                p[a][1] += o[1];
                p[a][2] -= o[2];
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
            }
            render_view(v, &A.placed, A.battle_gen, (const float (*)[4])p4, n);
        }
        if (v->rgba != NULL) {
            blit_rgba(v->x, v->y, v->rgba, v->rw, v->rh, v->w, v->h);
        }
        /* the sprites, the farthest from the camera first */
        for (a = 0; a < n; a++) {
            float dx = p[a][0] - v->eye[0], dz = p[a][2] - v->eye[2];

            d[a] = dx * dx + dz * dz;
        }
        for (a = 0; a < n; a++) {
            int far = -1;

            for (b = 0; b < n; b++) {
                if (d[b] >= 0.0f && (far < 0 || d[b] > d[far])) {
                    far = b;
                }
            }
            draw_standing(v, sp[far]->rgba, sp[far]->w, sp[far]->h, p[far], (float)sp[far]->h * SPRITE_U * sc[far]);
            d[far] = -1.0f;
        }
        /* the map standing in front of them, over the sprites (the game's
         * second backdrop pass, plat_gl3d.c) */
        if (v->front != NULL && n > 0) {
            blit_rgba(v->x, v->y, v->front, v->rw, v->rh, v->w, v->h);
        }
        if (i == 1 || (i == 0 && cf == NULL)) {
            text(v->x + 10, v->y + v->h - 26, 0xFFFFFFu, "Drag: turn, height   Right: slide, tilt   Wheel: zoom");
        }
        outline(v->x, v->y, v->w, v->h, C_LINE);
    }
    /* the field picture: the overworld camera, moved freely */
    if (A.ow_build) {
        build_field();
    }
    if (A.have_field && r != NULL) {
        View3d *v = &A.over;
        float p = A.ow_pitch * 3.14159265f / 180.0f, yw = A.ow_yaw * 3.14159265f / 180.0f;
        float at[3], eye[3];
        float sx, sy, unit, pt[3];

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
             * first): the map in front of each is drawn again over it, as
             * in the battle views */
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
            render_view(v, &A.field, A.field_gen, (const float (*)[4])sp, ns);
        }
        if (v->rgba != NULL) {
            blit_rgba(v->x, v->y, v->rgba, v->rw, v->rh, v->w, v->h);
        }
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
        if (v->front != NULL) {
            blit_rgba(v->x, v->y, v->front, v->rw, v->rh, v->w, v->h);    /* the map in front of them */
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
        text(v->x + 10, v->y + v->h - 26, 0xFFFFFFu, "Drag: move   Right drag: turn   Wheel: zoom");
    }
    if (!A.have_base) {
        text(hx + 24, head + 24, C_MUTED, "Loading...");
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

static void frame(void)
{
    int L;
    SDL_Surface *src, *dst;

    SDL_GetWindowSize(A.win, &A.W, &A.H);
    L = A.W * 36 / 100;
    if (L < 560) {
        L = 560;
    }
    ui_begin(&C, &A.canvas, A.W, A.H, 1);
    map_input();
    ow_input();
    view_input(0);
    view_input(1);
    draw_right(L);
    draw_left(L);
    dst = SDL_GetWindowSurface(A.win);
    src = SDL_CreateSurfaceFrom(A.W, A.H, SDL_PIXELFORMAT_RGBA32, A.canvas.rgba, A.W * 4);
    if (dst != NULL && src != NULL) {
        SDL_SetSurfaceBlendMode(src, SDL_BLENDMODE_NONE);
        SDL_BlitSurface(src, NULL, dst, NULL);
        SDL_UpdateWindowSurface(A.win);
    }
    SDL_DestroySurface(src);
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
            SDL_SaveBMP(SDL_GetWindowSurface(A.win), arg);
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
    A.sim = NULL;
    A.nsim = 0;
    kind = A.scene == 0 ? BCAM_WILD_INTRO : A.scene == 1 ? BCAM_TRAINER_INTRO : A.scene == 2 ? BCAM_IDLE : BCAM_MOVE;
    id = A.scene == 2 ? -1 : A.scene >= 3 ? A.scene - 2 : -1;
    if (bcam_sim_run(kind, id, att, !att, &A.sim, &A.nsim) != 0) {
        A.sim = NULL;
        A.nsim = 0;
        say("No camera for %s", scene_name(A.scene));
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
    for (k = 0; k < 8; k++) {
        if (b->poke_mask & (1u << k)) {
            cf.slot[cf.np] = k;
            memcpy(cf.pos[cf.np], b->poke[k], sizeof cf.pos[0]);
            cf.np++;
        }
    }
    for (k = 0; k < 6; k++) {
        if (b->trainer_mask & (1u << k)) {
            cf.slot[cf.np] = 8 + k;
            memcpy(cf.pos[cf.np], b->trainer[k], sizeof cf.pos[0]);
            cf.np++;
        }
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
    if (copy_file(from, to) != 0) {
        say("No save to preview with (%s)", from);
        return;
    }
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
        fprintf(f, "1000 press START 4\n1100 press A 4\n1400 press A 4\n");
        for (frame = 2400; !play && frame < 400000; frame += 60) {
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
        snprintf(v, sizeof v, "wild %d 50", A.species[0]);
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
    snprintf(v, sizeof v, "%d", A.species[1]);
    _putenv_s("EIDOS_DEBUG_PARTY", v);
    _putenv_s("EIDOS_DEBUG_LEVEL", "50");
    if (A.scene >= 3 && !play) {
        snprintf(v, sizeof v, "0:%d:0", A.scene - 2);
        _putenv_s("EIDOS_DEBUG_MOVES", v);
    }
    /* a preview skips to the battle; a battle to play shows its encounter
     * from the start */
    _putenv_s("EIDOS_FAST_UNTIL_BATTLE", play ? "start" : "1");
    _putenv_s("BW_SAVE_DIR", tmp);
    /* eidos_boot beside this program */
    {
        const char *self = SDL_GetBasePath();   /* this program's folder, with its slash */

        snprintf(exe, sizeof exe, "%seidos_boot.exe", self != NULL ? self : "");
    }
    if (_spawnl(_P_NOWAIT, exe, "\"eidos_boot.exe\"", "--layout", "one", "--window", "--input", input, NULL) < 0) {
        say("Could not start %s", exe);
    } else {
        say(play ? "Starting the game: the battle is yours in about 20 seconds"
                 : "Starting the game: the battle shows in about 20 seconds");
    }
    /* this program's own environment as it was */
    _putenv_s("EIDOS_DEBUG_BATTLE", "");
    _putenv_s("EIDOS_DEBUG_BMAP_AT", "");
    _putenv_s("EIDOS_DEBUG_PARTY", "");
    _putenv_s("EIDOS_DEBUG_LEVEL", "");
    _putenv_s("EIDOS_DEBUG_MOVES", "");
    _putenv_s("EIDOS_FAST_UNTIL_BATTLE", "");
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
    int status = 0, i;

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
    A.glwin = SDL_CreateWindow("eidos spots gl", 64, 64, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    A.gl = A.glwin != NULL ? eidos_gl_create(A.glwin) : NULL;
    if (A.gl == NULL) {
        fprintf(stderr, "eidos_spots: no OpenGL 3.3: %s\n", SDL_GetError());
        return 1;
    }
    A.win = SDL_CreateWindow("eidos battle spots", 1680, 960,
                             SDL_WINDOW_RESIZABLE | (shot != NULL ? SDL_WINDOW_HIDDEN : SDL_WINDOW_MAXIMIZED));
    if (A.win == NULL) {
        fprintf(stderr, "eidos_spots: %s\n", SDL_GetError());
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
    select_row(0);
    if (place != NULL) {
        for (i = 0; i < bmap_spots_count(); i++) {
            if (strcmp(bmap_spots_get(i)->id, place) == 0 && bmap_spots_get(i)->view == 0) {
                select_row(i);
            }
        }
    }
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
        SDL_SaveBMP(SDL_GetWindowSurface(A.win), shot);
        return 0;
    }
    while (!A.quit) {
        SDL_Event ev;

        if (SDL_WaitEventTimeout(&ev, A.play ? 1 : 50)) {
            SDL_PushEvent(&ev);
        }
        pump();
        frame();
    }
    return 0;
}
