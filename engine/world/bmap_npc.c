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

        if (rd32(r + 0x18) != 0u) {     /* a rail placed one */
            continue;
        }
        for (k = 0; k < n; k++) {       /* one NPC per tile, see the header */
            if ((unsigned)list[k].gx == rd16(r + 0x1C) && (unsigned)list[k].gz == rd16(r + 0x1E)) {
                break;
            }
        }
        if (k < n) {
            continue;
        }
        p->gx = rd16(r + 0x1C);
        p->gz = rd16(r + 0x1E);
        p->model_file = p->model_tex = -1;
        p->obj_code = rd16(r + 2);
        p->is_trainer = rd16(r + 6) == 1u;
        p->dir = (int)rd16(r + 0x0C) & 3;
        p->x = (float)rd16(r + 0x1C) * 16.0f + 8.0f;
        p->z = (float)rd16(r + 0x1E) * 16.0f + 8.0f;
        p->y = (float)(int)rd32(r + 0x20) / 4096.0f;
        load_picture(p, params, psize);
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
