/*
 * sdk/move_anims.c -- the animation a move plays (sdk/move_anims.h).
 *
 * A move's animation is the battle effect script of its id (a/0/6/6 member
 * = the move id, read by the battle view; the boundary between move scripts
 * and the other effects stays at the game's 0x231).  A mod may ship a script
 * of its own for any move, a new one included: a file a/0/6/6/<move id> in
 * the mod, or a member its code puts in memory (bw_vfs_mem_set).  A move
 * plays, in order: its move_anims.txt line's script, its own (the game's,
 * or a mod's), the script of the move its handler line names
 * (move_handlers.txt), Pound's.
 */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "battle_handlers.h"
#include "bw_limits.h"
#include "mod_v6.h"
#include "move_anims.h"
#include "plat_dir.h"
#include "type_icons.h"
#include "vfs.h"
#include "vfs_sdat.h"

#define SCRIPT_ARCHIVE "a/0/6/6"
#define SOUND_ARCHIVE  "wb_sound_data.sdat"

typedef struct Line {
    u16 move;
    u16 anim;
} Line;

static Line *g_lines;
static u32 g_count, g_cap;
static u8 *g_own;               /* a bit a move id: a mod ships its script */
static u32 g_own_n;             /* move ids the bits cover */

/* The moves asked for through a request (bw_move_anim_request): the
 * request's low 15 bits are an index into this list. */
static u16 *g_req;
static u32 g_req_count, g_req_cap;

/* A particle file's pools (particles.txt): a mod's line, else the game's. */
typedef struct Pools {
    u16 member;
    u16 emitters;
    u16 particles;
} Pools;

static Pools *g_pools;
static u32 g_pools_n, g_pools_cap;

/* A script of `move` in a mod's memory layer (mod API v6). */
static int has_mem_script(u32 move)
{
    const char *rel;
    u32 n, index;

    for (n = 0; bw_vfs_mem_list(n, &rel, &index); n++) {
        if (index == move && rel != NULL && strcmp(rel, SCRIPT_ARCHIVE) == 0) {
            return 1;
        }
    }
    return 0;
}

/* A mod's script of `move` (a file a/0/6/6/<move> in a mod, or a member in
 * memory). */
static int has_own_script(u32 move)
{
    return (move < g_own_n && (g_own[move >> 3] >> (move & 7)) & 1) || has_mem_script(move);
}

static int own_entry(void *ctx, const char *name)
{
    long n = 0;
    int k = 0;

    (void)ctx;
    while (name[k] >= '0' && name[k] <= '9' && k < 6) {
        n = n * 10 + (name[k++] - '0');
    }
    if (k > 0 && (name[k] == '\0' || name[k] == '.') && (u32)n < g_own_n) {
        g_own[n >> 3] |= (u8)(1u << (n & 7));
    }
    return 0;
}

/* The scripts the mods ship, up to the move limit. */
static void find_own_scripts(void)
{
    u32 m;

    free(g_own);
    g_own_n = bw_limit_get(BW_PORT_LIMIT_MOVE_LAST) + 1u;
    if ((g_own = (u8 *)calloc((g_own_n + 7u) / 8u, 1)) == NULL) {
        g_own_n = 0;
        return;
    }
    for (m = 0; m < bw_mod_count(); m++) {
        const char *root = bw_mod_root_at(m);
        char path[1200];

        if (root != NULL && strlen(root) + 16 < sizeof path) {
            sprintf(path, "%s/" SCRIPT_ARCHIVE, root);
            (void)bw_plat_list_dir(path, own_entry, NULL);
        }
    }
}
static u32 g_preview, g_preview_a, g_preview_b;

void bw_move_anims_clear(void)
{
    free(g_lines);
    g_lines = NULL;
    g_count = g_cap = 0;
    free(g_req);
    g_req = NULL;
    g_req_count = g_req_cap = 0;
    free(g_pools);
    g_pools = NULL;
    g_pools_n = g_pools_cap = 0;
}

static int fail(char *why, int size, const char *text)
{
    if (why != NULL && size > 0) {
        snprintf(why, (size_t)size, "%s", text);
    }
    return -1;
}

int bw_move_anims_parse_line(const char *text, char *why, int why_size)
{
    char line[256], *hash, *e;
    const char *s;
    long move, anim;
    u32 i;

    snprintf(line, sizeof line, "%s", text);
    if ((hash = strchr(line, '#')) != NULL) {
        *hash = '\0';
    }
    s = line;
    while (isspace((unsigned char)*s)) {
        s++;
    }
    if (*s == '\0') {
        return 1;
    }
    move = strtol(s, &e, 10);
    if (e == s || move < 1 || (u32)move > bw_limit_get(BW_PORT_LIMIT_MOVE_LAST)) {
        return fail(why, why_size, "expected `<move id> = <a move with a script>`");
    }
    s = e;
    while (isspace((unsigned char)*s)) {
        s++;
    }
    if (*s != '=') {
        return fail(why, why_size, "expected `=` after the move id");
    }
    s++;
    anim = strtol(s, &e, 10);
    if (e == s || anim < 1 || ((u32)anim > BW_ANIM_LAST && !has_own_script((u32)anim))) {
        return fail(why, why_size, "the animation is a move with a script: the game's 1 to 559, or one a mod ships");
    }
    while (isspace((unsigned char)*e)) {
        e++;
    }
    if (*e != '\0') {
        return fail(why, why_size, "one animation per line");
    }
    for (i = 0; i < g_count; i++) {
        if (g_lines[i].move == move) {
            g_lines[i].anim = (u16)anim;    /* a later line (a later mod's) wins */
            return 0;
        }
    }
    if (g_count == g_cap) {
        u32 cap = g_cap != 0 ? g_cap * 2 : 32;
        Line *grown = (Line *)realloc(g_lines, cap * sizeof *grown);

        if (grown == NULL) {
            return fail(why, why_size, "out of memory");
        }
        g_lines = grown;
        g_cap = cap;
    }
    g_lines[g_count].move = (u16)move;
    g_lines[g_count].anim = (u16)anim;
    g_count++;
    return 0;
}

/* Every enabled mod's eidos/tables/<name>, a line at a time. */
static void read_tables(const char *name, int (*parse)(const char *line, char *why, int why_size))
{
    u32 m;

    for (m = 0; m < bw_mod_count(); m++) {
        const char *root = bw_mod_root_at(m);
        char path[1200], line[256], why[200];
        FILE *f;
        int n = 0;

        if (root == NULL || strlen(root) + strlen(name) + 20 > sizeof path) {
            continue;
        }
        sprintf(path, "%s/eidos/tables/%s", root, name);
        if ((f = fopen(path, "rb")) == NULL) {
            continue;
        }
        while (fgets(line, sizeof line, f) != NULL) {
            n++;
            if (parse(line, why, sizeof why) < 0) {
                fprintf(stderr, "eidos: %s:%d: %s; the line is left out\n", path, n, why);
            }
        }
        fclose(f);
    }
}

void bw_move_anims_load(void)
{
    bw_move_anims_clear();
    find_own_scripts();
    read_tables("move_anims.txt", bw_move_anims_parse_line);
    read_tables("particles.txt", bw_anim_particles_parse_line);
}

u32 bw_move_anim(u32 move)
{
    u32 i, base;

    for (i = 0; i < g_count; i++) {
        if (g_lines[i].move == move) {
            return g_lines[i].anim;
        }
    }
    if ((move >= 1 && move <= BW_ANIM_LAST) || has_own_script(move)) {
        return move;                        /* its own: the game's, or a mod's */
    }
    base = (u32)bw_bh_base(BW_BH_MOVE, (int)move);
    if (base != move && base >= 1 && (base <= BW_ANIM_LAST || has_own_script(base))) {
        return base;                        /* the move it works like */
    }
    return 1;                               /* Pound's, not another effect */
}

u32 bw_move_anim_treated(u32 move)
{
    u32 base, anim;

    if (move >= 1 && move <= BW_ANIM_LAST) {
        return move;                        /* a game move: itself */
    }
    base = (u32)bw_bh_base(BW_BH_MOVE, (int)move);
    if (base != move && base >= 1 && base <= BW_ANIM_LAST) {
        return base;                        /* the game move it works like */
    }
    anim = bw_move_anim(move);
    return anim >= 1 && anim <= BW_ANIM_LAST ? anim : BW_ANIM_NO_MOVE;
}

/* ------------------------------------------------------------------ */
/* Requests                                                            */
/* ------------------------------------------------------------------ */

u32 bw_move_anim_request(u32 move)
{
    u32 anim = bw_move_anim(move);
    u32 treated = bw_move_anim_treated(move);
    u32 i;

    if (anim == treated && anim < BW_ANIM_MOVE_END) {
        return anim;                        /* the game's own path */
    }
    for (i = 0; i < g_req_count; i++) {
        if (g_req[i] == move) {
            return BW_ANIM_REQUEST | i;
        }
    }
    if (g_req_count == g_req_cap) {
        u32 cap = g_req_cap != 0 ? g_req_cap * 2 : 32;
        u16 *grown;

        if (cap > BW_ANIM_REQUEST) {
            cap = BW_ANIM_REQUEST;
        }
        if (g_req_count == cap || (grown = (u16 *)realloc(g_req, cap * sizeof *grown)) == NULL) {
            /* (no room: the script's move is asked for as itself, a game
             * move as the game asks, else the plain move script range) */
            return anim < BW_ANIM_MOVE_END ? anim : 1;
        }
        g_req = grown;
        g_req_cap = cap;
    }
    g_req[g_req_count] = (u16)move;
    return BW_ANIM_REQUEST | g_req_count++;
}

/* The move a request was made for, or 0. */
static u32 request_move(u32 request)
{
    u32 i = request & ~BW_ANIM_REQUEST;

    return (request & BW_ANIM_REQUEST) != 0 && i < g_req_count ? g_req[i] : 0;
}

u32 bw_move_anim_request_move(u32 request)
{
    if ((request & BW_ANIM_REQUEST) == 0) {
        return request >= 1 && request <= BW_ANIM_LAST ? request : 0;
    }
    return request_move(request);
}

u32 bw_move_anim_request_script(u32 request)
{
    u32 move = request_move(request);

    if ((request & BW_ANIM_REQUEST) == 0) {
        return request;
    }
    return move != 0 ? bw_move_anim(move) : 1;
}

u32 bw_move_anim_request_treated(u32 request)
{
    u32 move = request_move(request);

    if ((request & BW_ANIM_REQUEST) == 0) {
        return request;
    }
    return move != 0 ? bw_move_anim_treated(move) : 1;
}

/* ------------------------------------------------------------------ */
/* PlaySoundByName                                                     */
/* ------------------------------------------------------------------ */

typedef struct SoundName {
    char name[2 * BW_SDATM_NAME_MAX + 2];
    int id;
} SoundName;

static SoundName g_sounds[32];      /* the last names asked for */
static u32 g_sound_next;

int bw_anim_sound_id(const char *name)
{
    const u8 *img;
    u32 size, handle, id, i;
    int rc, found = -1;

    if (name == NULL || name[0] == '\0' || strlen(name) >= sizeof g_sounds[0].name) {
        return -1;
    }
    for (i = 0; i < sizeof g_sounds / sizeof g_sounds[0]; i++) {
        if (strcmp(g_sounds[i].name, name) == 0) {
            return g_sounds[i].id;
        }
    }
    /* the merged archive (sdk/vfs_sdat.h): it holds every mod's names; with
     * no mod sound there is none, and the name is not there either */
    rc = bw_vfs_acquire(SOUND_ARCHIVE, &img, &size, &handle);
    if (rc == 1) {
        if (bw_sdat_lookup(img, size, BW_SDATM_SEQ, name, &id) == 0) {
            found = (int)id;
        }
        bw_vfs_release(handle);
    }
    if (found < 0) {
        fprintf(stderr, "eidos: PlaySoundByName: no sound sequence `%s` (a mod's sdat.txt `seq` as <mod>:<name>)\n",
                name);
    }
    i = g_sound_next++ % (sizeof g_sounds / sizeof g_sounds[0]);
    snprintf(g_sounds[i].name, sizeof g_sounds[i].name, "%s", name);
    g_sounds[i].id = found;
    return found;
}

/* ------------------------------------------------------------------ */
/* Particle files                                                      */
/* ------------------------------------------------------------------ */

/*
 * The battle's particle system (CmdLoadParticleResource, arm9
 * PARTICLE_CreateSystem with 24 emitters and 200 particles) takes from its
 * work buffer, with no check of its end: the system's head 0x4C, an emitter
 * 0x9C, a particle 0x44, then the file's loader 0x20 a resource block, 8 a
 * behavior and 0x14 a texture block (arm9 PARTICLE_AllocWorkSlotN, the
 * loader SPL_EMITTER_FreeNodeToHeap).  A resource block in the file is the
 * 0x58-byte header, then the sub-blocks and behaviors its flags name.  The
 * game gives 0x4800; its largest file takes 784 of the 1012 bytes left.
 * The handles of the file's textures and palettes past the system's 64 are
 * held by the port (sdk/particle_ext.c).
 */
#define PTC_HEAD        0x4Cu
#define PTC_EMITTER     0x9Cu
#define PTC_PARTICLE    0x44u
#define PTC_GAME_WORK   0x4800u

void bw_anim_particle_pools(u32 member, u32 *emitters, u32 *particles)
{
    u32 i;

    *emitters = BW_ANIM_GAME_EMITTERS;
    *particles = BW_ANIM_GAME_PARTICLES;
    for (i = 0; i < g_pools_n; i++) {
        if (g_pools[i].member == member) {
            *emitters = g_pools[i].emitters;
            *particles = g_pools[i].particles;
        }
    }
}

/* `emitters <n>` or `particles <n>` from `s`: 1 read, 0 not that key, -1
 * a bad number. */
static int pool_key(const char **s, const char *key, u32 max, u32 *out)
{
    size_t n = strlen(key);
    char *e;
    long v;

    if (strncmp(*s, key, n) != 0 || !isspace((unsigned char)(*s)[n])) {
        return 0;
    }
    v = strtol(*s + n, &e, 10);
    if (e == *s + n || v < 1 || (u32)v > max) {
        return -1;
    }
    *out = (u32)v;
    *s = e;
    return 1;
}

int bw_anim_particles_parse_line(const char *text, char *why, int why_size)
{
    char line[256], *hash, *e;
    const char *s;
    long member;
    u32 emitters = BW_ANIM_GAME_EMITTERS, particles = BW_ANIM_GAME_PARTICLES, i;
    int any = 0;

    snprintf(line, sizeof line, "%s", text);
    if ((hash = strchr(line, '#')) != NULL) {
        *hash = '\0';
    }
    s = line;
    while (isspace((unsigned char)*s)) {
        s++;
    }
    if (*s == '\0') {
        return 1;
    }
    member = strtol(s, &e, 10);
    if (e == s || member < 0 || member > 0xFFFF) {
        return fail(why, why_size, "expected `<particle file> = emitters <n> particles <n>`");
    }
    s = e;
    while (isspace((unsigned char)*s)) {
        s++;
    }
    if (*s++ != '=') {
        return fail(why, why_size, "expected `=` after the particle file");
    }
    for (;;) {
        int rc;

        while (isspace((unsigned char)*s)) {
            s++;
        }
        if (*s == '\0') {
            break;
        }
        if ((rc = pool_key(&s, "emitters", BW_ANIM_MAX_EMITTERS, &emitters)) == 0) {
            rc = pool_key(&s, "particles", BW_ANIM_MAX_PARTICLES, &particles);
        }
        if (rc == 0) {
            return fail(why, why_size, "the keys are `emitters <n>` and `particles <n>`");
        }
        if (rc < 0) {
            return fail(why, why_size, "emitters is 1 to 1000, particles 1 to 10000");
        }
        any = 1;
    }
    if (!any) {
        return fail(why, why_size, "expected `emitters <n>`, `particles <n>` or both");
    }
    /* never fewer than the game's: its own effects count on them */
    emitters = emitters > BW_ANIM_GAME_EMITTERS ? emitters : BW_ANIM_GAME_EMITTERS;
    particles = particles > BW_ANIM_GAME_PARTICLES ? particles : BW_ANIM_GAME_PARTICLES;
    for (i = 0; i < g_pools_n; i++) {
        if (g_pools[i].member == member) {
            break;                          /* a later line (a later mod's) wins */
        }
    }
    if (i == g_pools_n) {
        if (g_pools_n == g_pools_cap) {
            u32 cap = g_pools_cap != 0 ? g_pools_cap * 2 : 16;
            Pools *grown = (Pools *)realloc(g_pools, cap * sizeof *grown);

            if (grown == NULL) {
                return fail(why, why_size, "out of memory");
            }
            g_pools = grown;
            g_pools_cap = cap;
        }
        g_pools_n++;
    }
    g_pools[i].member = (u16)member;
    g_pools[i].emitters = (u16)emitters;
    g_pools[i].particles = (u16)particles;
    return 0;
}

static u32 rd16(const u8 *p)
{
    return (u32)p[0] | (u32)p[1] << 8;
}

static u32 rd32(const u8 *p)
{
    return rd16(p) | rd16(p + 2) << 16;
}

u32 bw_anim_particle_work(const u8 *file, u32 size, u32 emitters, u32 particles)
{
    u32 res, tex, at, i, i2, need, flags, beh;

    if (file == NULL || size < 0x20) {
        return 0;
    }
    res = rd16(file + 8);
    tex = rd16(file + 0xA);
    need = PTC_HEAD + PTC_EMITTER * emitters + PTC_PARTICLE * particles + 0x14u * tex;
    at = 0x20;
    for (i = 0; i < res; i++) {
        u32 len = 0x58;

        if (at + 0x58 > size) {
            return 0;
        }
        flags = rd32(file + at);
        len += (flags >> 8 & 1) ? 0x0C : 0;     /* scale animation */
        len += (flags >> 9 & 1) ? 0x0C : 0;     /* colour animation */
        len += (flags >> 10 & 1) ? 0x08 : 0;    /* alpha animation */
        len += (flags >> 11 & 1) ? 0x0C : 0;    /* texture animation */
        len += (flags >> 16 & 1) ? 0x14 : 0;    /* child resource */
        len += (flags >> 24 & 1) ? 0x08 : 0;    /* gravity */
        len += (flags >> 25 & 1) ? 0x08 : 0;    /* random */
        len += (flags >> 26 & 1) ? 0x10 : 0;    /* magnet */
        len += (flags >> 27 & 1) ? 0x04 : 0;    /* spin */
        len += (flags >> 28 & 1) ? 0x08 : 0;    /* collision plane */
        len += (flags >> 29 & 1) ? 0x10 : 0;    /* convergence */
        beh = 0;
        for (i2 = 24; i2 <= 29; i2++) {         /* the behaviors: bits 24 to 29 */
            beh += flags >> i2 & 1;
        }
        need += 0x20u + 8u * beh;
        at += len;
    }
    need = (need + 3u) & ~3u;
    return need > PTC_GAME_WORK ? need : PTC_GAME_WORK;
}

/* ------------------------------------------------------------------ */
/* Type colours (ParticleColor)                                        */
/* ------------------------------------------------------------------ */

#define BUTTON_ARCHIVE  "a/0/1/1"
#define BUTTON_SHADE    8u          /* the button's base shade: palette row 0, colour 8 */

static u16 bgr555(u32 rgb24)
{
    return (u16)((((rgb24 >> 3) & 31u) << 10) | (((rgb24 >> 11) & 31u) << 5) | ((rgb24 >> 19) & 31u));
}

/* Colour `index` of row 0 of an NCLR's palette, or -1. */
static int nclr_color(const u8 *d, u32 size, u32 index)
{
    u32 at;

    for (at = 0x10; at + 0x18 <= size;) {
        u32 len = rd32(d + at + 4);

        if (memcmp(d + at, "TTLP", 4) == 0) {
            u32 data = at + 8 + rd32(d + at + 0x14);     /* (the size is at +0x10) */

            return data + 2 * index + 2 <= size ? (int)rd16(d + data + 2 * index) : -1;
        }
        if (len < 8) {
            break;
        }
        at += len;
    }
    return -1;
}

/* The battle's per-type move button table (ov094 btlv_input.c d_0220A5F0: the
 * a/0/1/1 member of each type, with the added types' after the game's 17
 * when a mod adds types, sdk/type_icons.c). */
extern void *bw_dsref_ov094_d_0220A5F0 __attribute__((weak));

#define GAME_TYPES      17u
#define BUTTON_NORMAL   0xC2u       /* Normal's, d_0220A5F0[0] */

u16 bw_anim_type_color(u32 type)
{
    static u8 pal[2048];
    u32 rgb, button = BUTTON_NORMAL, last = bw_limit_get(BW_PORT_LIMIT_TYPE_LAST);
    int size, c;

    if (bw_type_color(type, &rgb)) {
        return bgr555(rgb);
    }
    if (&bw_dsref_ov094_d_0220A5F0 != NULL && bw_dsref_ov094_d_0220A5F0 != NULL
        && (type < GAME_TYPES || (last >= GAME_TYPES && type <= last))) {
        button = (u32)((const int *)bw_dsref_ov094_d_0220A5F0)[type];
    }
    size = bw_mod_vfs_member_get(BUTTON_ARCHIVE, button, pal, sizeof pal);
    if (size > 0 && (u32)size <= sizeof pal && (c = nclr_color(pal, (u32)size, BUTTON_SHADE)) >= 0) {
        return (u16)c;
    }
    return 0x7FFF;                  /* (no palette: untinted) */
}

/* A script's cell actor slot past the six the battle holds (the effect VM's
 * patch refuses it). */
void bw_anim_log_clact_slot(int slot)
{
    static int said;

    if (!said) {
        said = 1;
        fprintf(stderr, "eidos: an effect script uses cell actor slot %d: the slots are 0 to 5; the command is left out\n",
                slot);
    }
}

/* The work buffers the port gave (a short list: one a live particle file). */
static void **g_bufs;
static u32 g_bufs_n, g_bufs_cap;

void *bw_anim_particle_alloc(u32 size)
{
    void *p;

    if (g_bufs_n == g_bufs_cap) {
        u32 cap = g_bufs_cap != 0 ? g_bufs_cap * 2 : 16;
        void **grown = (void **)realloc(g_bufs, cap * sizeof *grown);

        if (grown == NULL) {
            return NULL;
        }
        g_bufs = grown;
        g_bufs_cap = cap;
    }
    if ((p = malloc(size)) != NULL) {
        g_bufs[g_bufs_n++] = p;
    }
    return p;
}

int bw_anim_particle_free(void *buffer)
{
    u32 i;

    for (i = 0; i < g_bufs_n; i++) {
        if (g_bufs[i] == buffer) {
            free(buffer);
            g_bufs[i] = g_bufs[--g_bufs_n];
            return 1;
        }
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* The effect background                                               */
/* ------------------------------------------------------------------ */

#define EFFVM_GFX_RELOAD_PENDING 0x10u     /* BtlvEffVmFlags bit 4 */

static const volatile u32 *g_effvm_flags;

void bw_anim_effvm_flags(const u32 *flags)
{
    g_effvm_flags = flags;
}

int bw_anim_effect_bg_shown(void)
{
    return g_effvm_flags != NULL && (*g_effvm_flags & EFFVM_GFX_RELOAD_PENDING) != 0;
}

/* ------------------------------------------------------------------ */
/* The preview                                                         */
/* ------------------------------------------------------------------ */

void bw_preview_set(u32 move, u32 attacker, u32 defender)
{
    g_preview = move;
    g_preview_a = attacker;
    g_preview_b = defender;
}

u32 bw_preview_move(void)
{
    return g_preview;
}

u32 bw_preview_attacker(void)
{
    return g_preview_a;
}

u32 bw_preview_defender(void)
{
    return g_preview_b;
}
