/*
 * engine/world/bmap_npc.c -- the NPCs of a zone, as the field draws them
 * (engine/world/bmap_npc.h).
 *
 * The data path, from the game's own files:
 *
 *   zone header  a/0/1/2 member 0, 48 bytes per zone: u16 event data id at +0x16
 *   event data   a/1/2/5 member id: u32 size, u8 counts [bg, npc, connect,
 *                pos], bg records of 0x14 bytes, then npc records of 0x24
 *                bytes (MmdlHeader): u16 objId, objCode, moveCode, eventType,
 *                eventFlag, eventId, dir at +0 .. +0xC; u32 posType at +0x18
 *                (0 grid); u16 gridX, gridZ at +0x1C; fx32 y at +0x20
 *   parameters   a/0/4/8 member 0: u32 count, then 0x1C-byte records indexed
 *                by FLDMMDL_ObjCodeToTableIndex(objCode): drawType at +2
 *                (1 billboard, 2 3D model, 0 none), drawProcNo +3, mdlSize
 *                +7, animId +9, offsets +0xD..+0xF (signed bytes, whole map
 *                units), u16 sprite file id at +0x10
 *   sprite       a/0/4/9 member = sprite file id: a BTX0 of 32x32 (mdlSize 0),
 *                16x16 (1) or 64x64 (2) frames, one 16-colour texture each
 *
 * Several NPCs can share one tile when their event flags exclude each other
 * (the Striaton leaders): the save decides which one shows.  There is no
 * save here, so only the first NPC of a tile is kept.
 *
 * A grid NPC stands at x = gridX * 16 + 8, z = gridZ * 16 + 8 (the middle of
 * its tile), y = the header's fx32 y.  Which frame it shows: the billboard
 * animation table (ov021, sBlactAnimeTable) holds, per animation id and
 * direction, a command list; its first command is the standing frame, with
 * 0x4000 set when the picture is mirrored.  The table below keeps that first
 * command per direction.  The billboard quad (shape 2) is 2 wide and 2 tall,
 * scaled by the size table (8192, 4096, 16384 = 2, 1, 4) and by the board
 * scale of 7, and stands on the position.
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bmap_npc.h"
#include "port.h"
#include "types.h"

/* bmap_data.c: a model file placed into a built scene. */
int bmap_add_bmd(BmapScene *s, const unsigned char *bmd, size_t bmd_n,
                 const unsigned char *btx, size_t btx_n, const char *key,
                 float x, float y, float z, float turns);

/* bmap_data.c: one cell of a billboard BTX0, a malloc'd cw*ch*4 picture. */
int bmap_btx_cell(const unsigned char *data, size_t size, int cw, int ch,
                  unsigned cell, unsigned char **out);

#define PARAM_RECORD   0x1C
#define EVENT_NPC_SIZE 0x24
#define BOARD_SCALE    7.0f
#define MIRROR         0x40
#define NO_FRAME       0xFF

/* First command of the animation list of each direction (up, down, left,
 * right) per animation id: the frame, plus MIRROR.  Read from sBlactAnimeTable;
 * a direction past the table's list count uses list 0, as the table does. */
static const unsigned char k_stand[30][4] = {
    { 0xFF, 0xFF, 0xFF, 0xFF }, { 0x00, 0x03, 0x06, 0x09 },
    { 0xFF, 0xFF, 0xFF, 0xFF }, { 0xFF, 0xFF, 0xFF, 0xFF },
    { 0x00, 0x01, 0x02, 0x42 }, { 0x00, 0x02, 0x04, 0x44 },
    { 0x00, 0x03, 0x06, 0x09 }, { 0x0C, 0x0D, 0x0E, 0x0F },
    { 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x00 },
    { 0x04, 0x00, 0x04, 0x04 }, { 0x00, 0x00, 0x00, 0x00 },
    { 0x00, 0x02, 0x04, 0x44 }, { 0x00, 0x02, 0x04, 0x44 },
    { 0x00, 0x02, 0x04, 0x06 }, { 0x00, 0x04, 0x08, 0x48 },
    { 0x00, 0x08, 0x00, 0x00 }, { 0x00, 0x04, 0x08, 0x0C },
    { 0x00, 0x04, 0x00, 0x0C }, { 0x00, 0x00, 0x00, 0x00 },
    { 0x00, 0x03, 0x06, 0x09 }, { 0x00, 0x00, 0x08, 0x10 },
    { 0x04, 0x00, 0x08, 0x04 }, { 0x00, 0x03, 0x06, 0x09 },
    { 0x00, 0x03, 0x06, 0x09 }, { 0x00, 0x03, 0x06, 0x09 },
    { 0x00, 0x03, 0x06, 0x09 }, { 0x00, 0x03, 0x06, 0x09 },
    { 0x00, 0x03, 0x06, 0x09 }, { 0x08, 0x08, 0x08, 0x08 },
};

/* Cell size and billboard scale (fx12) per model size: sBlactCellSize and
 * sBlactDrawSize. */
static const int k_cell[3] = { 32, 16, 64 };
static const int k_draw[3] = { 8192, 4096, 16384 };

static unsigned rd16(const u8 *p) { return (unsigned)p[0] | ((unsigned)p[1] << 8); }

static unsigned rd32(const u8 *p) { return rd16(p) | (rd16(p + 2) << 16); }

/* The whole member of an archive, malloc'd; NULL when missing. */
static u8 *member(const char *path, u32 id, long *size)
{
    BwArcFile arc;
    long n;
    u8 *buf;

    if (bw_arc_open(&arc, path) != 0) {
        return NULL;
    }
    n = bw_arc_member_size(&arc, id);
    buf = n > 0 ? (u8 *)malloc((size_t)n) : NULL;
    if (buf != NULL && bw_arc_read_member(&arc, id, buf, (u32)n) != n) {
        free(buf);
        buf = NULL;
    }
    bw_arc_close(&arc);
    *size = n;
    return buf;
}

/* FLDMMDL_ObjCodeToTableIndex */
static unsigned param_index(unsigned code)
{
    if (code < 0xDFu) {
        return code;
    }
    if (code >= 0x1000u && code < 0x1236u) {
        return (code - 0xF21u) & 0xFFFFu;
    }
    if (code >= 0x2000u && code < 0x200Au) {
        return (code - 0x1CEBu) & 0xFFFFu;
    }
    return 0xAu;
}

static void mirror(u8 *rgba, int w, int h)
{
    int x, y, c;

    for (y = 0; y < h; y++) {
        u8 *row = rgba + (size_t)y * (size_t)w * 4u;

        for (x = 0; x < w / 2; x++) {
            for (c = 0; c < 4; c++) {
                u8 t = row[x * 4 + c];

                row[x * 4 + c] = row[(w - 1 - x) * 4 + c];
                row[(w - 1 - x) * 4 + c] = t;
            }
        }
    }
}

/* The standing picture of one object code facing `dir`.  Returns 0 and a
 * malloc'd picture with its size, or -1 (model, invisible, alias, missing
 * file); `off` gets the object code's own offset (map units). */
static int picture(unsigned code, int dir, const u8 *params, long psize, u8 **rgba,
                   int *w, int *h, float *world_w, float *world_h, float off[3])
{
    const u8 *r;
    unsigned idx = param_index(code), anim, msize;
    unsigned char st;
    long size;
    u8 *btx;
    int cw, rc;

    if (code >= 162u && code <= 177u) {
        return -1;                      /* an event variable decides the code */
    }
    if (4 + (long)(idx + 1u) * PARAM_RECORD > psize) {
        return -1;
    }
    r = params + 4 + idx * PARAM_RECORD;
    if (r[2] != 1u) {                   /* not a billboard */
        return -1;
    }
    msize = r[7];
    anim = r[9];
    if (msize > 2u || anim >= 30u || k_stand[anim][dir & 3] == NO_FRAME) {
        return -1;
    }
    st = k_stand[anim][dir & 3];
    cw = k_cell[msize];
    btx = member("a/0/4/9", rd16(r + 0x10), &size);
    if (btx == NULL) {
        return -1;
    }
    rc = bmap_btx_cell(btx, (size_t)size, cw, cw, st & 0x3Fu, rgba);
    free(btx);
    if (rc != 0) {
        return -1;
    }
    if (st & MIRROR) {
        mirror(*rgba, cw, cw);
    }
    *w = *h = cw;
    *world_w = *world_h = 2.0f * ((float)k_draw[msize] / 4096.0f) * BOARD_SCALE;
    off[0] = (float)(signed char)r[0x0D];
    off[1] = (float)(signed char)r[0x0E];
    off[2] = (float)(signed char)r[0x0F];
    return 0;
}

/*
 * The field sprite of a trainer.
 *
 * The event files do not always say what an opponent looks like: a story fight's opponent is walked in or added by
 * its script, and a stadium's visitor has an object code that the game reads from an event variable.  The game's own
 * data still tells, by the trainer's class: every trainer that stands in an event file as a trainer (event type 1)
 * has the script 3000 + its trainer number (5000 + the number: the second of a pair), and its trainer record
 * (a/0/9/2) names its class.  Over all the event files (a/1/2/5) each class wears one object code (83 classes; where
 * a class has more than one, the commonest holds: Cheren's stands once as a gift box).  The classes that never
 * stand as plain trainers are named here by the object code of the character, known by the places the game puts it:
 *   38 Bianca -> 134 (Nacrene, Castelia, Nimbasa, Icirrus, the Dreamyard, Chargestone Cave, Routes 1, 3 and 10)
 *   40, 47 N -> 103 (Accumula, Nimbasa, Chargestone Cave, Dragonspiral Tower, the League, his castle)
 *   101 Ghetsis -> 100 (Accumula, Castelia, Driftveil, Opelucid, the Relic Castle, the castle)
 *   55 the Opelucid leader -> 92, Drayden (Black's; White's is Iris, 93)
 * A Pokemon met in a scripted fight wears the object code its zone's event file gives it: Landorus (645) -> 120
 * (the Abundant Shrine), Musharna (518) -> 127 (the Dreamyard).
 */
#define CLASS_MAX 256
static short g_class_code[CLASS_MAX];
static int g_class_built;

static void class_codes_build(void)
{
    static const short named[][2] = { { 38, 134 }, { 40, 103 }, { 47, 103 }, { 101, 100 }, { 55, 92 } };
    static unsigned short vote[CLASS_MAX][4][2];        /* a class's object codes and how often each */
    unsigned file, i, k;

    g_class_built = 1;
    memset(vote, 0, sizeof vote);       /* (built again for the other version's event files) */
    for (i = 0; i < CLASS_MAX; i++) {
        g_class_code[i] = -1;
    }
    for (file = 0; file < 2048u; file++) {
        long esize = 0, tsize = 0;
        u8 *ev = member("a/1/2/5", file, &esize);
        unsigned nbg, nnpc;

        if (ev == NULL) {
            break;
        }
        nbg = esize >= 8 ? ev[4] : 0u;
        nnpc = esize >= 8 ? ev[5] : 0u;
        for (i = 0; i < nnpc && 8 + (long)nbg * 0x14 + (long)(i + 1u) * EVENT_NPC_SIZE <= esize; i++) {
            const u8 *r = ev + 8 + nbg * 0x14u + i * EVENT_NPC_SIZE;
            const unsigned code = rd16(r + 2), script = rd16(r + 10);
            const int tid = script >= 5000u && script < 6000u ? (int)script - 5000 : script >= 3000u && script < 4000u ? (int)script - 3000 : -1;
            u8 *tr;
            unsigned cls;

            if (rd16(r + 6) != 1u || tid <= 0 || (code >= 162u && code <= 177u)) {
                continue;
            }
            tr = member("a/0/9/2", (u32)tid, &tsize);
            cls = tr != NULL && tsize > 1 ? tr[1] : CLASS_MAX;
            free(tr);
            if (cls >= CLASS_MAX) {
                continue;
            }
            for (k = 0; k < 4u && vote[cls][k][1] != 0u && vote[cls][k][0] != code; k++) {
            }
            if (k < 4u) {
                vote[cls][k][0] = (unsigned short)code;
                vote[cls][k][1]++;
            }
        }
        free(ev);
    }
    for (i = 0; i < CLASS_MAX; i++) {
        unsigned best = 0;

        for (k = 0; k < 4u; k++) {
            if (vote[i][k][1] > best) {
                best = vote[i][k][1];
                g_class_code[i] = (short)vote[i][k][0];
            }
        }
    }
    for (i = 0; i < sizeof named / sizeof named[0]; i++) {
        g_class_code[named[i][0]] = named[i][1];
    }
}

int bmap_npc_trainer_code(int trainer_id)
{
    long tsize = 0;
    u8 *tr;
    int cls;

    static u32 seen;

    if (trainer_id <= 0 || trainer_id >= 100000) {
        return -1;
    }
    if (seen != bw_data_generation()) {
        seen = bw_data_generation();    /* the other version's data: its own trainers and events */
        g_class_built = 0;
    }
    if (!g_class_built) {
        class_codes_build();
    }
    tr = member("a/0/9/2", (u32)trainer_id, &tsize);
    cls = tr != NULL && tsize > 1 ? tr[1] : -1;
    free(tr);
    return cls >= 0 && cls < CLASS_MAX ? g_class_code[cls] : -1;
}

int bmap_npc_species_code(int species)
{
    return species == 645 ? 120 : species == 518 ? 127 : -1;
}

static void load_picture(BmapNpc *n, const u8 *params, long psize)
{
    float off[3];
    const u8 *r;
    unsigned idx = param_index(n->obj_code);

    if (picture(n->obj_code, n->dir, params, psize, &n->rgba, &n->w, &n->h,
                &n->world_w, &n->world_h, off) == 0) {
        n->x += off[0];
        n->y += off[1];
        n->z += off[2];
        return;
    }
    n->w = n->h = 0;
    n->rgba = NULL;
    /* an object code the game reads from an event variable (162..177: the day's visitor in a stadium, a leader that
     * depends on the version): there is no save here, and without event variables the game itself shows object code
     * 10 (FLDMMDL_GetEventWorkObjCode), so that character's picture stands for it */
    if (n->obj_code >= 162u && n->obj_code <= 177u
        && picture(10u, n->dir, params, psize, &n->rgba, &n->w, &n->h, &n->world_w, &n->world_h, off) == 0) {
        n->x += off[0];
        n->y += off[1];
        n->z += off[2];
        n->stand_in = 1;
        return;
    }
    n->w = n->h = 0;
    n->rgba = NULL;
    /* a 3D object that is drawn: remember its model files */
    if (n->obj_code < 162u || n->obj_code > 177u) {
        r = params + 4 + idx * PARAM_RECORD;
        if (4 + (long)(idx + 1u) * PARAM_RECORD <= psize && r[2] == 2u && r[3] != 0u) {
            n->model_file = (int)rd16(r + 0x10);
            n->model_tex = rd16(r + 0x12) == 0xFFFFu ? -1 : (int)rd16(r + 0x12);
        }
    }
}

int bmap_npc_picture(unsigned obj_code, int dir, unsigned char **rgba, int *w, int *h,
                     float *world_w, float *world_h)
{
    long psize = 0;
    u8 *params = member("a/0/4/8", 0, &psize);
    float off[3];
    int rc;

    if (params == NULL) {
        return -1;
    }
    rc = picture(obj_code, dir, params, psize, rgba, w, h, world_w, world_h, off);
    free(params);
    return rc;
}

/* The place of a rail location of `zone` (map units): line `line`, `grid` cells along it, `width` cells across.
 * The zone's rail file: arm9's _ZoneSpecialKindTable (0x0209E026: 20 words, the zone in the low 11 bits, its kind
 * above) gives the kind, a/1/0/9's member of that kind the file of a/0/7/9 (FIELD_NOGRID_MAPPER_LoadDataFromArc).
 * The file: 7 section offsets; points (0x70 bytes, the place at 0x30), lines (0x48 bytes: start point, end point,
 * key, shape entry, camera, length in cells), shape entries (0x24 bytes: the function, an arc's centre).  The
 * place as FIELD_RAIL_LINEPOS_Straight and FIELD_RAIL_LINEPOS_Arc solve it.  Returns 0 when it cannot be read. */
static int rail_place(unsigned zone, unsigned line, unsigned grid, int width, float out[3])
{
    const u8 *kt = (const u8 *)bw_arm9_rodata(0x0209E026u, 20u * 2u);
    long rsize = 0, fsize = 0;
    u8 *res, *f;
    unsigned i, kind = 0xFFFFu, file, sec[7], np, nl, ns, s, e, shape, len, fn = 0;
    double a[3], b[3], c[3] = { 0, 0, 0 }, p[3], t, off;
    int k, ok = 0;

    for (i = 0; kt != NULL && i < 20u; i++) {
        if ((rd16(kt + i * 2u) & 0x7FFu) == zone) {
            kind = rd16(kt + i * 2u) >> 11;
        }
    }
    if (kind == 0xFFFFu) {
        return 0;
    }
    res = member("a/1/0/9", kind, &rsize);
    if (res == NULL || rsize < 4 || rd16(res) == 0xFFFFu) {
        free(res);
        return 0;
    }
    file = rd16(res);
    free(res);
    f = member("a/0/7/9", file, &fsize);
    if (f == NULL || fsize < 28) {
        free(f);
        return 0;
    }
    for (i = 0; i < 7u; i++) {
        sec[i] = rd32(f + i * 4u);
    }
    if (sec[2] <= sec[3] && sec[3] <= sec[4] && sec[5] <= sec[6] && (long)sec[6] <= fsize) {
        np = (sec[3] - sec[2]) / 0x70u;
        nl = (sec[4] - sec[3]) / 0x48u;
        ns = (sec[6] - sec[5]) / 0x24u;
        if (line < nl) {
            const u8 *l = f + sec[3] + line * 0x48u;

            s = rd32(l);
            e = rd32(l + 4);
            shape = rd32(l + 0x0C);
            len = rd32(l + 0x14);
            if (s < np && e < np && len > 0u) {
                for (k = 0; k < 3; k++) {
                    a[k] = (double)(int)rd32(f + sec[2] + s * 0x70u + 0x30u + (unsigned)k * 4u) / 4096.0;
                    b[k] = (double)(int)rd32(f + sec[2] + e * 0x70u + 0x30u + (unsigned)k * 4u) / 4096.0;
                }
                if (shape < ns) {
                    fn = rd32(f + sec[5] + shape * 0x24u);
                    for (k = 0; k < 3; k++) {
                        c[k] = (double)(int)rd32(f + sec[5] + shape * 0x24u + 4u + (unsigned)k * 4u) / 4096.0;
                    }
                }
                t = (double)grid / (double)len;
                off = (double)width * 16.0;       /* (a cell across is a cell along: 16 units) */
                if (fn == 0u) {
                    /* a straight line: along it, and across it along (end - start) x up */
                    const double dx = b[0] - a[0], dz = b[2] - a[2], n = sqrt(dx * dx + dz * dz);

                    for (k = 0; k < 3; k++) {
                        p[k] = a[k] + (b[k] - a[k]) * t;
                    }
                    if (n > 1e-9) {
                        p[0] += -dz / n * off;
                        p[2] += dx / n * off;
                    }
                } else {
                    /* an arc about the shape's centre: the direction turned from the start's to the end's, the
                     * radius carried from one to the other; across it along the radius */
                    double fa[3], fb[3], la, lb, ang, d[3], r, rad[3], dir[3], rn, dn, sy;

                    for (k = 0; k < 3; k++) {
                        fa[k] = a[k] - c[k];
                        fb[k] = b[k] - c[k];
                    }
                    la = sqrt(fa[0] * fa[0] + fa[1] * fa[1] + fa[2] * fa[2]);
                    lb = sqrt(fb[0] * fb[0] + fb[1] * fb[1] + fb[2] * fb[2]);
                    if (la > 1e-9 && lb > 1e-9) {
                        double dot = (fa[0] * fb[0] + fa[1] * fb[1] + fa[2] * fb[2]) / (la * lb);

                        dot = dot > 1.0 ? 1.0 : dot < -1.0 ? -1.0 : dot;
                        ang = acos(dot);
                        for (k = 0; k < 3; k++) {
                            d[k] = ang < 1e-6 ? fa[k] / la
                                              : (sin((1.0 - t) * ang) * fa[k] / la + sin(t * ang) * fb[k] / lb) / sin(ang);
                        }
                        r = la + (lb - la) * t;
                        for (k = 0; k < 3; k++) {
                            p[k] = c[k] + d[k] * r;
                            dir[k] = fb[k] - fa[k];
                        }
                        rad[0] = d[0]; rad[1] = 0.0; rad[2] = d[2];
                        rn = sqrt(rad[0] * rad[0] + rad[2] * rad[2]);
                        dn = sqrt(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
                        if (rn > 1e-9 && dn > 1e-9) {
                            sy = rad[2] / rn * dir[0] / dn - rad[0] / rn * dir[2] / dn;   /* (radius x direction).y */
                            p[0] += (sy < 0.0 ? -off : off) * rad[0] / rn;
                            p[2] += (sy < 0.0 ? -off : off) * rad[2] / rn;
                        }
                    } else {
                        for (k = 0; k < 3; k++) {
                            p[k] = a[k] + (b[k] - a[k]) * t;
                        }
                    }
                }
                out[0] = (float)p[0];
                out[1] = (float)p[1];
                out[2] = (float)p[2];
                ok = 1;
            }
        }
    }
    free(f);
    return ok;
}

int bmap_npc_load(unsigned zone, BmapNpc **out, int *count)
{
    long hsize = 0, esize = 0, psize = 0;
    u8 *hdr, *ev = NULL, *params = NULL;
    unsigned id, nbg, nnpc, i;
    int k;
    BmapNpc *list;
    int n = 0;

    *out = NULL;
    *count = 0;
    hdr = member("a/0/1/2", 0, &hsize);
    if (hdr == NULL || (long)(zone + 1u) * 48 > hsize) {
        free(hdr);
        return -1;
    }
    id = rd16(hdr + zone * 48u + 0x16);
    free(hdr);
    ev = member("a/1/2/5", id, &esize);
    if (ev == NULL || esize < 8) {
        free(ev);
        return -1;
    }
    nbg = ev[4];
    nnpc = ev[5];
    if (8 + (long)nbg * 0x14 + (long)nnpc * EVENT_NPC_SIZE > esize) {
        free(ev);
        return -1;
    }
    params = member("a/0/4/8", 0, &psize);
    list = nnpc ? (BmapNpc *)calloc(nnpc, sizeof *list) : NULL;
    if (params == NULL || (nnpc && list == NULL)) {
        free(ev);
        free(params);
        free(list);
        return -1;
    }
    for (i = 0; i < nnpc; i++) {
        const u8 *r = ev + 8 + nbg * 0x14u + i * EVENT_NPC_SIZE;
        BmapNpc *p = &list[n];

        float rp[3] = { 0.0f, 0.0f, 0.0f };
        const int rail = rd32(r + 0x18) != 0u;
        int gx = (int)rd16(r + 0x1C), gz = (int)rd16(r + 0x1E);

        if (rail) {
            /* a rail placed one: the header's words are a line, a cell along it and cells across (MmdlHeader.pos,
             * FLDMMDL_MOVE_RequestRailMove), not a tile */
            if (!rail_place(zone, rd16(r + 0x1C), rd16(r + 0x1E), (int)(short)rd16(r + 0x20), rp)) {
                continue;
            }
            gx = (int)floorf(rp[0] / 16.0f);
            gz = (int)floorf(rp[2] / 16.0f);
        }
        for (k = 0; k < n; k++) {       /* one NPC per tile, see the header */
            if (list[k].gx == gx && list[k].gz == gz) {
                break;
            }
        }
        if (k < n) {
            continue;
        }
        p->gx = gx;
        p->gz = gz;
        p->rail = rail;
        p->model_file = p->model_tex = -1;
        p->obj_code = rd16(r + 2);
        p->is_trainer = rd16(r + 6) == 1u;
        p->dir = (int)rd16(r + 0x0C) & 3;
        if (rail) {
            p->x = rp[0];
            p->y = rp[1];
            p->z = rp[2];
        } else {
            p->x = (float)gx * 16.0f + 8.0f;
            p->z = (float)gz * 16.0f + 8.0f;
            p->y = (float)(int)rd32(r + 0x20) / 4096.0f;
        }
        {
            /* (a trainer whose object code is read from an event variable, a stadium's visitor: its class's
             * field sprite; the code it is listed with stays) */
            const unsigned listed = p->obj_code, script = rd16(r + 10);
            const int tid = script >= 5000u && script < 6000u ? (int)script - 5000 : script >= 3000u && script < 4000u ? (int)script - 3000 : -1;
            const int worn = listed >= 162u && listed <= 177u && p->is_trainer ? bmap_npc_trainer_code(tid) : -1;

            if (worn >= 0) {
                p->obj_code = (unsigned)worn;
            }
            load_picture(p, params, psize);
            p->obj_code = listed;
        }
        n++;
    }
    free(ev);
    free(params);
    if (n == 0) {
        free(list);
        list = NULL;
    }
    *out = list;
    *count = n;
    return 0;
}

int bmap_npc_add_models(BmapScene *s, const BmapNpc *npcs, int n)
{
    /* turns of a circle about y for dir up, down, left, right (0 faces +z) */
    static const float k_turns[4] = { 0.5f, 0.0f, 0.75f, 0.25f };
    int i, bad = 0;

    for (i = 0; i < n; i++) {
        long msize = 0, tsize = 0;
        u8 *mdl, *tex = NULL;
        char key[24];

        if (npcs[i].model_file < 0) {
            continue;
        }
        mdl = member("a/0/4/9", (u32)npcs[i].model_file, &msize);
        if (npcs[i].model_tex >= 0) {
            tex = member("a/0/4/9", (u32)npcs[i].model_tex, &tsize);
        }
        snprintf(key, sizeof key, "npc%d_%d", npcs[i].model_file, npcs[i].model_tex);
        if (mdl == NULL || bmap_add_bmd(s, mdl, (size_t)msize, tex, tex != NULL ? (size_t)tsize : 0u,
                                        key, npcs[i].x, npcs[i].y, npcs[i].z,
                                        k_turns[npcs[i].dir & 3]) != 0) {
            bad++;
        }
        free(mdl);
        free(tex);
    }
    return bad;
}

int bmap_npc_add_residents(unsigned zone, BmapNpc **npcs, int *count)
{
    long psize = 0, tsize = 0, rsize = 0;
    u8 *params, *table, *rec;
    BmapNpc *list;
    int first, i, n = *count;

    if (zone != 0u && zone != 424u) {
        return 0;
    }
    table = member("a/1/5/4", zone == 0u ? 0x22u : 0x23u, &tsize);
    params = member("a/0/4/8", 0, &psize);
    list = (BmapNpc *)realloc(*npcs, (size_t)(n + 10) * sizeof *list);
    if (table == NULL || tsize < 20 || params == NULL || list == NULL) {
        free(table);
        free(params);
        if (list != NULL) {
            *npcs = list;
        }
        return -1;
    }
    *npcs = list;
    first = zone == 0u ? 0 : 15;
    for (i = 0; i < 10; i++) {
        BmapNpc *p = &list[n];

        rec = member("a/1/5/4", (u32)(first + i), &rsize);
        if (rec == NULL || rsize < 2) {
            free(rec);
            continue;
        }
        memset(p, 0, sizeof *p);
        p->gx = table[2 * i];
        p->gz = table[2 * i + 1];
        p->x = (float)p->gx * 16.0f + 8.0f;
        p->z = (float)p->gz * 16.0f + 8.0f;
        p->dir = 1;                     /* facing down, as the header's 0 */
        p->model_file = p->model_tex = -1;
        p->obj_code = rd16(rec);
        p->resident = 1;
        free(rec);
        load_picture(p, params, psize);
        n++;
    }
    free(table);
    free(params);
    *count = n;
    return 0;
}

void bmap_npc_free(BmapNpc *npcs, int count)
{
    int i;

    for (i = 0; i < count; i++) {
        free(npcs[i].rgba);
    }
    free(npcs);
}
