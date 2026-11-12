/*
 * engine/world/bmap_data.c -- the overworld map as a lit, textured mesh in
 * world space (engine/world/bmap.h).
 *
 * The data path, from the game's own files:
 *
 *   zone header  a/0/1/2 member 0, 48 bytes per zone: u16 areaId at +2,
 *                u16 matrixId at +4
 *   matrix       a/0/0/9 member matrixId: u16 mode at 0, width at 4, height
 *                at 6, u32 chunk id per cell at +8 (0xFFFFFFFF = empty);
 *                mode 1 adds a u32 zone id per cell after the chunk ids
 *   area record  a/0/1/3 flat file, 10 bytes each, index areaId + season
 *                for 2 <= areaId < 0xD2: u16 modelSetId, u16 texSetId,
 *                u8 itaAnime, u8 itpAnime, u8 indoor, u8 lightType
 *   ground       a/0/0/8 member = chunk id: BMD0 at the u32 offset at +4,
 *                placed objects in the last section (u32 count, then 16
 *                bytes each: fx32 x, y, -z, u16 turns, u16 model id with
 *                its bytes swapped); textures: a/0/1/4 member texSetId
 *   buildings    a/2/3/0 (outdoor) or a/2/2/9 (indoor), member m from the
 *                area id; textures in a/1/7/7 or a/1/7/6, same member
 *   light        a/0/6/1 member lightType: 0x34-byte keyframes, the first
 *                whose time lies after "now" (FIELD_LIGHT_Create)
 *   animations   a building's definition (the member's first table) holds
 *                its animations at +0x10: u8 kind, then four s32 offsets
 *                from there (-1: none).  Kind 1 plays every one, looping
 *                (FIELD_BUILDMODEL_InitAnimSetAll); a BTA0 among them moves
 *                its materials' texture coordinates (BmapUvAnim)
 *
 * The BMD0 reader is its own (engine/g3d/nsbmd.c guesses materials and
 * palettes; this one reads the material, texture pairing and palette
 * pairing records the model holds).  A chunk model is centred on its cell.
 * A tile is 16 map units, a cell 32 tiles.
 *
 * Lighting is baked: vertex colour = material emission + sum over the
 * lights its polygon attribute enables of ambient * light + diffuse * light
 * * max(0, -(direction . normal)).  Billboard shapes (trees, grass) are
 * built facing +z and get their own draw with the pivot, so the placer can
 * turn them to the camera.
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bmap.h"
#include "bmap_gimmick.h"
#include "bmap_remade.h"
#include "port.h"
#include "types.h"

#define TILE        16.0f
#define CELL_UNITS  512.0f
#define REACH       2560.0f  /* cells within this distance of the player load */
#define NEAR_REACH  640.0f   /* cells beyond this are far (BmapDraw.far, no ground index) */
#define MAX_STEP    64.0f    /* ground higher than this above the lowest is a roof */
#define NO_CHUNK    0xFFFFFFFFu
#define AREA_RECORDS 282

/* ------------------------------------------------------------------ */
/* Bytes                                                               */
/* ------------------------------------------------------------------ */

typedef struct { const u8 *p; size_t n; } Buf;

static u32 rd16(Buf b, size_t at)
{
    return at + 2u <= b.n ? (u32)b.p[at] | ((u32)b.p[at + 1] << 8) : 0u;
}

static u32 rd32(Buf b, size_t at)
{
    return at + 4u <= b.n ? (u32)b.p[at] | ((u32)b.p[at + 1] << 8) |
                            ((u32)b.p[at + 2] << 16) | ((u32)b.p[at + 3] << 24)
                          : 0u;
}

static int rds16(Buf b, size_t at) { return (int)(short)rd16(b, at); }
static int rds32(Buf b, size_t at) { return (int)rd32(b, at); }

static Buf sub(Buf b, size_t off)
{
    Buf r;

    r.p = off <= b.n ? b.p + off : b.p + b.n;
    r.n = off <= b.n ? b.n - off : 0u;
    return r;
}

static void *grow(void *p, size_t *cap, size_t n, size_t esz)
{
    if (n + 1u > *cap) {
        size_t c = *cap ? *cap * 2u : 1024u;
        void *q = realloc(p, c * esz);

        if (q == NULL) {
            return NULL;
        }
        *cap = c;
        return q;
    }
    return p;
}

/* ------------------------------------------------------------------ */
/* Affine transforms, row vectors: v' = v.x r[0] + v.y r[1] + v.z r[2] + t */
/* ------------------------------------------------------------------ */

typedef struct { float r[3][3]; float t[3]; } Aff;

static Aff aff_identity(void)
{
    Aff a;

    memset(&a, 0, sizeof a);
    a.r[0][0] = a.r[1][1] = a.r[2][2] = 1.0f;
    return a;
}

static void aff_apply(const Aff *a, const float v[3], float o[3])
{
    int k;

    for (k = 0; k < 3; k++) {
        o[k] = a->t[k] + v[0] * a->r[0][k] + v[1] * a->r[1][k] + v[2] * a->r[2][k];
    }
}

/* `a` first, then `b`. */
static Aff aff_then(const Aff *a, const Aff *b)
{
    Aff o;
    int i, k;

    for (i = 0; i < 3; i++) {
        for (k = 0; k < 3; k++) {
            o.r[i][k] = a->r[i][0] * b->r[0][k] + a->r[i][1] * b->r[1][k] +
                        a->r[i][2] * b->r[2][k];
        }
    }
    aff_apply(b, a->t, o.t);
    return o;
}

static Aff aff_yaw(float turns, const float t[3])
{
    float s = sinf(turns * 6.2831853f), c = cosf(turns * 6.2831853f);
    Aff a = aff_identity();

    a.r[0][0] = c;  a.r[0][2] = -s;
    a.r[2][0] = s;  a.r[2][2] = c;
    memcpy(a.t, t, sizeof a.t);
    return a;
}

/* ------------------------------------------------------------------ */
/* G3D containers and dictionaries                                     */
/* ------------------------------------------------------------------ */

static int container_block(Buf d, const char *magic, Buf *out)
{
    u32 count = rd16(d, 0x0E), i;

    for (i = 0; i < count; i++) {
        u32 off = rd32(d, 0x10u + i * 4u), size;
        Buf b = sub(d, off);

        if (b.n >= 8u && memcmp(b.p, magic, 4) == 0) {
            size = rd32(b, 4);
            out->p = b.p;
            out->n = (size == 0u || size > b.n) ? b.n : size;
            return 0;
        }
    }
    return -1;
}

typedef struct {
    Buf b;
    size_t base;
    unsigned count;
    size_t entry, rec, names;
} Dict;

static int dict_parse(Buf b, size_t off, Dict *d)
{
    unsigned count;
    size_t size, subh;

    if (off + 8u > b.n || b.p[off] != 0) {
        return -1;
    }
    count = b.p[off + 1];
    size = rd16(b, off + 2);
    if (rd16(b, off + 4) != 8u || rd16(b, off + 6) != 8u + (count + 1u) * 4u ||
        size < count * 16u) {
        return -1;
    }
    subh = off + 8u + (count + 1u) * 4u;
    d->b = b;
    d->base = off;
    d->count = count;
    d->entry = rd16(b, subh);
    d->rec = subh + 4u;
    d->names = off + size - count * 16u;
    if (d->rec + count * d->entry > b.n || d->names + count * 16u > b.n) {
        return -1;
    }
    return 0;
}

static u32 dict_off(const Dict *d, unsigned i)
{
    return d->entry >= 4u ? rd32(d->b, d->rec + i * d->entry) : 0u;
}

static void dict_name(const Dict *d, unsigned i, char out[17])
{
    memcpy(out, d->b.p + d->names + i * 16u, 16u);
    out[16] = 0;
}

/* ------------------------------------------------------------------ */
/* Textures                                                            */
/* ------------------------------------------------------------------ */

typedef struct {
    int w, h;
    u8 *rgba;
    int partial, cutout;
} Image;

static const int k_fmt_colors[8] = { 256, 32, 4, 16, 256, 256, 8, 256 };

static size_t fmt_bytes(int fmt, int w, int h)
{
    size_t n = (size_t)w * (size_t)h;

    switch (fmt) {
    case 2: return n / 4u;
    case 3: return n / 2u;
    case 1: case 4: case 6: return n;
    case 7: return n * 2u;
    default: return 0u;
    }
}

static void pal_color(Buf pal, unsigned i, u8 o[3])
{
    if (i * 2u + 2u <= pal.n) {
        u32 c = rd16(pal, i * 2u);

        o[0] = (u8)((c & 31u) * 255u / 31u);
        o[1] = (u8)(((c >> 5) & 31u) * 255u / 31u);
        o[2] = (u8)(((c >> 10) & 31u) * 255u / 31u);
    } else {
        o[0] = 255; o[1] = 0; o[2] = 255;
    }
}

static unsigned max_index(int fmt, int w, int h, Buf tx)
{
    size_t n = (size_t)w * (size_t)h, i;
    unsigned m = 0u, v;

    for (i = 0; i < n; i++) {
        switch (fmt) {
        case 2: v = (tx.p[i / 4u] >> ((i % 4u) * 2u)) & 3u; break;
        case 3: v = (tx.p[i / 2u] >> ((i % 2u) * 4u)) & 15u; break;
        case 4: v = tx.p[i]; break;
        case 1: v = tx.p[i] & 0x1Fu; break;
        case 6: v = tx.p[i] & 7u; break;
        default: v = 0u; break;
        }
        if (v > m) {
            m = v;
        }
    }
    return m;
}

/* Finds texture `name` in a BTX0/BMD0 file, decodes it with palette
 * `palname` (the model's pairing), else `<name>_pl`, else the palette that
 * covers every index it uses. */
static int decode_block(Buf blk, const char *name, const char *palname, Image *img)
{
    Dict td, pd;
    u32 tex_off = rd32(blk, 0x14), pal_off = rd32(blk, 0x24);
    unsigned i, ti = 0xFFFFu, pi = 0xFFFFu;
    char nm[17], want[40];
    size_t rec;
    u32 params, off;
    int w, h, fmt, c0, need;
    Buf tx, pal;
    size_t px, npx;

    if (tex_off > pal_off || pal_off > blk.n || dict_parse(blk, 0x3C, &td) != 0) {
        return -1;
    }
    if (dict_parse(blk, 0x3C + rd16(blk, 0x3C + 2), &pd) != 0 || td.entry != 8u) {
        return -1;
    }
    for (i = 0; i < td.count; i++) {
        dict_name(&td, i, nm);
        if (strcmp(nm, name) == 0) {
            ti = i;
            break;
        }
    }
    if (ti == 0xFFFFu) {
        return 1;                               /* not in this block */
    }
    rec = td.rec + ti * 8u;
    off = rd16(blk, rec) * 8u;
    params = rd16(blk, rec + 2);
    w = 8 << ((params >> 4) & 7u);
    h = 8 << ((params >> 7) & 7u);
    fmt = (int)((params >> 10) & 7u);
    c0 = (int)((params >> 13) & 1u);
    npx = (size_t)w * (size_t)h;
    if (fmt == 0 || fmt == 5) {
        return -1;                              /* none, and 4x4 (unused here) */
    }
    tx = sub(blk, tex_off + off);
    if (tx.n < fmt_bytes(fmt, w, h) || tex_off + off + fmt_bytes(fmt, w, h) > pal_off) {
        return -1;
    }
    memset(&pal, 0, sizeof pal);
    if (fmt != 7) {
        need = k_fmt_colors[fmt] * 2;
        if (palname != NULL && palname[0]) {
            for (i = 0; i < pd.count && pi == 0xFFFFu; i++) {
                dict_name(&pd, i, nm);
                if (strcmp(nm, palname) == 0) {
                    pi = i;
                }
            }
        }
        if (pi == 0xFFFFu) {
            snprintf(want, sizeof want, "%s_pl", name);
            for (i = 0; i < pd.count && pi == 0xFFFFu; i++) {
                dict_name(&pd, i, nm);
                if (strcmp(nm, want) == 0) {
                    pi = i;
                }
            }
        }
        if (pi == 0xFFFFu) {
            unsigned used = max_index(fmt, w, h, tx), best = 0xFFFFu;
            int bestd = 0x7FFFFFFF;

            for (i = 0; i < pd.count; i++) {
                size_t po = pal_off + rd16(blk, pd.rec + i * 4u) * 8u;
                size_t avail = po < blk.n ? blk.n - po : 0u;
                int cols = (int)((avail < (size_t)need ? avail : (size_t)need) / 2u);
                int dd = abs(cols - k_fmt_colors[fmt]);

                if ((unsigned)cols > used && dd < bestd) {
                    bestd = dd;
                    best = i;
                }
            }
            pi = best != 0xFFFFu ? best : 0u;
        }
        if (pd.count > 0u) {
            pal = sub(blk, pal_off + rd16(blk, pd.rec + pi * 4u) * 8u);
            if (pal.n > (size_t)need) {
                pal.n = (size_t)need;
            }
        }
    }
    img->w = w;
    img->h = h;
    img->rgba = (u8 *)malloc(npx * 4u);
    if (img->rgba == NULL) {
        return -1;
    }
    for (px = 0; px < npx; px++) {
        u8 c[3] = { 0, 0, 0 }, a = 255;
        unsigned idx;

        switch (fmt) {
        case 7: {
            u32 v = rd16(tx, px * 2u);

            c[0] = (u8)((v & 31u) * 255u / 31u);
            c[1] = (u8)(((v >> 5) & 31u) * 255u / 31u);
            c[2] = (u8)(((v >> 10) & 31u) * 255u / 31u);
            a = (v & 0x8000u) ? 255 : 0;
            break;
        }
        case 1:
            idx = tx.p[px] & 0x1Fu;
            a = (u8)((tx.p[px] >> 5) * 255u / 7u);
            pal_color(pal, idx, c);
            break;
        case 6:
            idx = tx.p[px] & 7u;
            a = (u8)((tx.p[px] >> 3) * 255u / 31u);
            pal_color(pal, idx, c);
            break;
        default: {
            unsigned bits = fmt == 2 ? 2u : fmt == 3 ? 4u : 8u, per = 8u / bits;

            idx = (tx.p[px / per] >> ((px % per) * bits)) & ((1u << bits) - 1u);
            a = (c0 && idx == 0u) ? 0 : 255;
            pal_color(pal, idx, c);
            break;
        }
        }
        img->rgba[px * 4u] = c[0];
        img->rgba[px * 4u + 1u] = c[1];
        img->rgba[px * 4u + 2u] = c[2];
        img->rgba[px * 4u + 3u] = a;
        if (a == 0) {
            img->cutout = 1;
        } else if (a != 255) {
            img->partial = 1;
        }
    }
    return 0;
}

/* Every TEX0 block in a file (a BTX0 or BMD0 container, possibly embedded). */
static int decode_in(Buf data, const char *name, const char *palname, Image *img)
{
    size_t at = 0;

    while (at + 16u <= data.n) {
        if ((memcmp(data.p + at, "BTX0", 4) == 0 || memcmp(data.p + at, "BMD0", 4) == 0) &&
            rd16(data, at + 4) == 0xFEFFu) {
            Buf blk, c = sub(data, at);
            u32 size = rd32(data, at + 8);

            if (container_block(c, "TEX0", &blk) == 0) {
                int r = decode_block(blk, name, palname, img);

                if (r <= 0) {
                    return r;
                }
            }
            at += size > 16u ? size : 16u;
            continue;
        }
        at += 4u;
    }
    return -1;
}

/* ------------------------------------------------------------------ */
/* BMD0 models                                                         */
/* ------------------------------------------------------------------ */

typedef struct {
    char name[17];
    char tex[17], pal[17];
    int has_tex, has_pal;
    unsigned alpha, cullbits;
    unsigned polymode;      /* the hardware's polygon mode: 3 is a shadow volume, not a surface */
    int vertex_color;
    int repeat[2], flip[2];
    float diffuse[3], ambient[3], emission[3];
    unsigned light_mask;
} Mat;

typedef struct { u32 off, size; } Shape;

typedef struct { int shape, mat, node, bb; } DrawCmd;

/* A node's own translation, rotation and scale (what a joint animation replaces). */
typedef struct { float t[3], rot[9], sc[3]; } NodeSrt;

typedef struct {
    Buf m;                  /* the model record, offsets inside it are from here */
    float pos_scale;
    Aff *world;
    NodeSrt *srt;           /* per node, for model_pose */
    int *parent;            /* per node, -1: none       */
    unsigned nnodes;
    Mat *mats;
    unsigned nmats;
    Shape *shapes;
    unsigned nshapes;
    DrawCmd *draws;
    unsigned ndraws;
} Model;

static void model_free(Model *md)
{
    free(md->world);
    free(md->srt);
    free(md->parent);
    free(md->mats);
    free(md->shapes);
    free(md->draws);
    memset(md, 0, sizeof *md);
}

static void rgb555(u32 c, float o[3])
{
    o[0] = (float)(c & 31u) / 31.0f;
    o[1] = (float)((c >> 5) & 31u) / 31.0f;
    o[2] = (float)((c >> 10) & 31u) / 31.0f;
}

static int parse_nodes(Model *md)
{
    Buf m = md->m;
    Dict d;
    unsigned i;
    Aff *local;
    int *parent;

    if (dict_parse(m, 0x40, &d) != 0) {
        return -1;
    }
    md->nnodes = d.count;
    md->world = (Aff *)calloc(d.count ? d.count : 1u, sizeof(Aff));
    local = (Aff *)calloc(d.count ? d.count : 1u, sizeof(Aff));
    parent = (int *)malloc((d.count ? d.count : 1u) * sizeof(int));
    md->srt = (NodeSrt *)calloc(d.count ? d.count : 1u, sizeof(NodeSrt));
    if (md->world == NULL || md->srt == NULL || local == NULL || parent == NULL) {
        free(local);
        free(parent);
        return -1;
    }
    for (i = 0; i < d.count; i++) {
        size_t a = d.base + dict_off(&d, i), o;
        u32 flags = rd16(m, a);
        int first = rds16(m, a + 2), k;
        float t[3] = { 0, 0, 0 }, sc[3] = { 1, 1, 1 };
        Aff r = aff_identity();

        o = a + 4u;
        if ((flags & 1u) == 0u) {
            for (k = 0; k < 3; k++) {
                t[k] = (float)rds32(m, o + (size_t)k * 4u) / 4096.0f;
            }
            o += 12u;
        }
        if ((flags & 2u) == 0u) {
            float e[9] = { 0 };

            if ((flags & 8u) == 0u) {
                e[0] = (float)first / 4096.0f;
                for (k = 1; k < 9; k++) {
                    e[k] = (float)rds16(m, o + (size_t)(k - 1) * 2u) / 4096.0f;
                }
                o += 16u;
            } else {
                unsigned pivot = (flags >> 4) & 15u;
                float av = (float)rds16(m, o) / 4096.0f;
                float bv = (float)rds16(m, o + 2u) / 4096.0f;

                if (pivot < 9u) {
                    unsigned pr = pivot / 3u, pc = pivot % 3u, n = 0u, q;
                    float vals[4], cv = (flags & 0x200u) ? -bv : bv;
                    float dv = (flags & 0x400u) ? -av : av;

                    vals[0] = av; vals[1] = bv; vals[2] = cv; vals[3] = dv;
                    e[pivot] = (flags & 0x100u) ? -1.0f : 1.0f;
                    for (q = 0; q < 9u; q++) {
                        if (q / 3u != pr && q % 3u != pc) {
                            e[q] = vals[n++];
                        }
                    }
                }
                o += 4u;
            }
            for (k = 0; k < 9; k++) {
                r.r[k / 3][k % 3] = e[k];
            }
        }
        if ((flags & 4u) == 0u) {
            for (k = 0; k < 3; k++) {
                sc[k] = (float)rds32(m, o + (size_t)k * 4u) / 4096.0f;
            }
        }
        memcpy(md->srt[i].t, t, sizeof t);
        memcpy(md->srt[i].sc, sc, sizeof sc);
        for (k = 0; k < 9; k++) {
            md->srt[i].rot[k] = r.r[k / 3][k % 3];
        }
        for (k = 0; k < 3; k++) {
            int j;

            for (j = 0; j < 3; j++) {
                r.r[k][j] *= sc[k];
            }
        }
        memcpy(r.t, t, sizeof t);
        local[i] = r;
        parent[i] = -1;
    }
    /* the render commands name the parents and the draws */
    for (i = 0; i < d.count; i++) {
        md->world[i] = local[i];
    }
    free(local);
    {
        Buf mm = md->m;
        size_t at = rd32(mm, 0x04);
        int node = 0, material = 0, visible = 1, bb = 0;
        unsigned cap = 0, guard = 0;

        md->draws = NULL;
        md->ndraws = 0;
        while (at < mm.n && guard++ < 100000u) {
            u32 cmd = mm.p[at], op = cmd & 0x1Fu;
            size_t len;

            if (op == 0x01u) {
                break;
            }
            switch (op) {
            case 0x00: case 0x0B: len = 0; break;
            case 0x02:
                node = mm.p[at + 1];
                visible = mm.p[at + 2] != 0;
                bb = 0;
                len = 2;
                break;
            case 0x07: bb = 1; len = 1; break;
            case 0x08: bb = 2; len = 1; break;
            case 0x03: len = 1; break;
            case 0x04: material = mm.p[at + 1]; len = 1; break;
            case 0x05: {
                int shape = mm.p[at + 1];

                if (visible && (unsigned)shape < md->nshapes && (unsigned)material < md->nmats &&
                    (unsigned)node < md->nnodes) {
                    size_t cc = cap;
                    DrawCmd *nd = (DrawCmd *)grow(md->draws, &cc, md->ndraws, sizeof *nd);

                    if (nd == NULL) {
                        free(parent);
                        return -1;
                    }
                    md->draws = nd;
                    cap = (unsigned)cc;
                    nd[md->ndraws].shape = shape;
                    nd[md->ndraws].mat = material;
                    nd[md->ndraws].node = node;
                    nd[md->ndraws].bb = bb;
                    md->ndraws++;
                }
                len = 1;
                break;
            }
            case 0x06: {
                unsigned n = mm.p[at + 1], par = mm.p[at + 2];

                if (n < md->nnodes && par < md->nnodes && n != par) {
                    parent[n] = (int)par;
                }
                len = 3u + ((cmd & 0x20u) != 0u) + ((cmd & 0x40u) != 0u);
                break;
            }
            case 0x09: len = 2u + 3u * mm.p[at + 2]; break;
            case 0x0A: len = 12; break;
            case 0x0C: case 0x0D: len = 2; break;
            default: len = (size_t)-1; break;
            }
            if (len == (size_t)-1) {
                break;
            }
            at += 1u + len;
        }
    }
    {
        unsigned pass, n = d.count < 8u ? d.count : 8u;
        Aff *loc = (Aff *)malloc((d.count ? d.count : 1u) * sizeof(Aff));

        if (loc == NULL) {
            free(parent);
            return -1;
        }
        memcpy(loc, md->world, d.count * sizeof(Aff));
        for (pass = 0; pass < n; pass++) {
            for (i = 0; i < d.count; i++) {
                if (parent[i] >= 0) {
                    md->world[i] = aff_then(&loc[i], &md->world[parent[i]]);
                }
            }
        }
        free(loc);
    }
    md->parent = parent;
    return 0;
}

/* Pairing dictionary: names -> the material ids that use them. */
static void apply_pairs(Buf b, size_t dict_at, Mat *mats, unsigned nmats, int pal)
{
    Dict d;
    unsigned i, k;

    if (dict_parse(b, dict_at, &d) != 0 || d.entry < 3u) {
        return;
    }
    for (i = 0; i < d.count; i++) {
        size_t r = d.rec + i * d.entry;
        u32 list = rd16(b, r);
        unsigned count = r + 2u < b.n ? b.p[r + 2] : 0u;
        char nm[17];

        dict_name(&d, i, nm);
        for (k = 0; k < count; k++) {
            unsigned id = list + k < b.n ? b.p[list + k] : 0xFFFFu;

            if (id < nmats) {
                if (pal) {
                    memcpy(mats[id].pal, nm, 17u);
                    mats[id].has_pal = 1;
                } else {
                    memcpy(mats[id].tex, nm, 17u);
                    mats[id].has_tex = 1;
                }
            }
        }
    }
}

static int parse_materials(Model *md)
{
    Buf m = md->m;
    Buf b = sub(m, rd32(m, 0x08));
    Dict d;
    unsigned i;

    if (dict_parse(b, 4, &d) != 0) {
        return -1;
    }
    md->nmats = d.count;
    md->mats = (Mat *)calloc(d.count ? d.count : 1u, sizeof(Mat));
    if (md->mats == NULL) {
        return -1;
    }
    apply_pairs(b, rd16(b, 0), md->mats, d.count, 0);
    apply_pairs(b, rd16(b, 2), md->mats, d.count, 1);
    for (i = 0; i < d.count; i++) {
        size_t a = dict_off(&d, i);
        u32 diff_amb = rd32(b, a + 4), spec_emi = rd32(b, a + 8);
        u32 poly = rd32(b, a + 0x0C), tex = rd32(b, a + 0x14);
        Mat *mt = &md->mats[i];

        dict_name(&d, i, mt->name);
        mt->cullbits = (poly >> 6) & 3u;
        mt->alpha = (poly >> 16) & 31u;
        mt->polymode = (poly >> 4) & 3u;
        mt->vertex_color = (diff_amb & 0x8000u) != 0u;
        mt->repeat[0] = (tex & 0x10000u) != 0u;
        mt->repeat[1] = (tex & 0x20000u) != 0u;
        mt->flip[0] = (tex & 0x40000u) != 0u;
        mt->flip[1] = (tex & 0x80000u) != 0u;
        rgb555(diff_amb, mt->diffuse);
        rgb555(diff_amb >> 16, mt->ambient);
        rgb555(spec_emi >> 16, mt->emission);
        mt->light_mask = poly & 15u;
    }
    return 0;
}

static int parse_shapes(Model *md)
{
    Buf m = md->m;
    size_t base = rd32(m, 0x0C);
    Dict d;
    unsigned i;

    if (dict_parse(m, base, &d) != 0) {
        return -1;
    }
    md->nshapes = d.count;
    md->shapes = (Shape *)calloc(d.count ? d.count : 1u, sizeof(Shape));
    if (md->shapes == NULL) {
        return -1;
    }
    for (i = 0; i < d.count; i++) {
        size_t a = base + dict_off(&d, i);

        md->shapes[i].off = (u32)(a + rd32(m, a + 8));
        md->shapes[i].size = rd32(m, a + 12);
    }
    return 0;
}

/* Parses the first model of a BMD0 file (bytes of the whole file). */
static int model_parse(Buf data, Model *md)
{
    Buf mdl;
    Dict models;

    memset(md, 0, sizeof *md);
    if (data.n < 16u || memcmp(data.p, "BMD0", 4) != 0 ||
        container_block(data, "MDL0", &mdl) != 0 || dict_parse(mdl, 8, &models) != 0 ||
        models.count == 0u) {
        return -1;
    }
    md->m = sub(mdl, dict_off(&models, 0));
    md->pos_scale = (float)rds32(md->m, 0x1C) / 4096.0f;
    /* nodes need shapes and materials counted first (render commands) */
    if (parse_materials(md) != 0 || parse_shapes(md) != 0 || parse_nodes(md) != 0) {
        model_free(md);
        return -1;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Display lists to triangles                                          */
/* ------------------------------------------------------------------ */

typedef struct { float pos[3], uv[2], col[3], nrm[3]; } Vtx;

static int gx_words(unsigned op)
{
    switch (op) {
    case 0x00: case 0x11: case 0x15: case 0x41: return 0;
    case 0x10: case 0x12: case 0x13: case 0x14: return 1;
    case 0x16: case 0x18: return 16;
    case 0x17: case 0x19: return 12;
    case 0x1A: return 9;
    case 0x1B: case 0x1C: return 3;
    case 0x23: return 2;
    case 0x34: return 32;
    case 0x70: return 3;
    case 0x71: return 2;
    case 0x40: case 0x50: case 0x60: case 0x72: return 1;
    default:
        if ((op >= 0x20 && op <= 0x22) || (op >= 0x24 && op <= 0x2B) ||
            (op >= 0x30 && op <= 0x33)) {
            return 1;
        }
        return -1;
    }
}

static float sext10(u32 v)
{
    return (float)((int)((v & 0x3FFu) << 22) >> 22);
}

static float s16f(u32 v, int hi)
{
    return (float)(short)(hi ? (v >> 16) : v);
}

typedef struct { Vtx *v; size_t n, cap; } TriList;

static int tri_push(TriList *l, const Vtx *a, const Vtx *b, const Vtx *c)
{
    Vtx *nv = (Vtx *)grow(l->v, &l->cap, l->n + 2u, sizeof(Vtx));

    if (nv == NULL) {
        return -1;
    }
    l->v = nv;
    l->v[l->n++] = *a;
    l->v[l->n++] = *b;
    l->v[l->n++] = *c;
    return 0;
}

static int decode_list(Buf dl, TriList *out)
{
    size_t at = 0, n = 0, rcap = 0;
    Vtx cur, *run = NULL;
    u32 mode = 0;
    int ok = 0;

    memset(&cur, 0, sizeof cur);
    cur.col[0] = cur.col[1] = cur.col[2] = 1.0f;
    while (at + 4u <= dl.n) {
        u32 cmds = rd32(dl, at);
        int k;

        at += 4u;
        for (k = 0; k < 4; k++) {
            unsigned op = (cmds >> (k * 8)) & 0xFFu;
            int nw = gx_words(op), vertex = 0, i;
            u32 args[32];

            if (nw < 0) {
                goto done;
            }
            if (at + (size_t)nw * 4u > dl.n) {
                goto done;
            }
            for (i = 0; i < nw; i++) {
                args[i] = rd32(dl, at + (size_t)i * 4u);
            }
            at += (size_t)nw * 4u;
            switch (op) {
            case 0x20: rgb555(args[0], cur.col); break;
            case 0x21:
                cur.nrm[0] = sext10(args[0]) / 512.0f;
                cur.nrm[1] = sext10(args[0] >> 10) / 512.0f;
                cur.nrm[2] = sext10(args[0] >> 20) / 512.0f;
                break;
            case 0x22:
                cur.uv[0] = s16f(args[0], 0) / 16.0f;
                cur.uv[1] = s16f(args[0], 1) / 16.0f;
                break;
            case 0x23:
                cur.pos[0] = s16f(args[0], 0) / 4096.0f;
                cur.pos[1] = s16f(args[0], 1) / 4096.0f;
                cur.pos[2] = s16f(args[1], 0) / 4096.0f;
                vertex = 1;
                break;
            case 0x24:
                cur.pos[0] = sext10(args[0]) / 64.0f;
                cur.pos[1] = sext10(args[0] >> 10) / 64.0f;
                cur.pos[2] = sext10(args[0] >> 20) / 64.0f;
                vertex = 1;
                break;
            case 0x25:
                cur.pos[0] = s16f(args[0], 0) / 4096.0f;
                cur.pos[1] = s16f(args[0], 1) / 4096.0f;
                vertex = 1;
                break;
            case 0x26:
                cur.pos[0] = s16f(args[0], 0) / 4096.0f;
                cur.pos[2] = s16f(args[0], 1) / 4096.0f;
                vertex = 1;
                break;
            case 0x27:
                cur.pos[1] = s16f(args[0], 0) / 4096.0f;
                cur.pos[2] = s16f(args[0], 1) / 4096.0f;
                vertex = 1;
                break;
            case 0x28:
                cur.pos[0] += sext10(args[0]) / 4096.0f;
                cur.pos[1] += sext10(args[0] >> 10) / 4096.0f;
                cur.pos[2] += sext10(args[0] >> 20) / 4096.0f;
                vertex = 1;
                break;
            case 0x40: mode = args[0] & 3u; n = 0; break;
            case 0x41: n = 0; break;
            default: break;
            }
            if (vertex) {
                Vtx *nr = (Vtx *)grow(run, &rcap, n, sizeof(Vtx));

                if (nr == NULL) {
                    ok = -1;
                    goto done;
                }
                run = nr;
                run[n++] = cur;
                switch (mode) {
                case 0:
                    if (n % 3u == 0u && tri_push(out, &run[n - 3], &run[n - 2], &run[n - 1])) {
                        ok = -1;
                        goto done;
                    }
                    break;
                case 1:
                    if (n % 4u == 0u &&
                        (tri_push(out, &run[n - 4], &run[n - 3], &run[n - 2]) ||
                         tri_push(out, &run[n - 4], &run[n - 2], &run[n - 1]))) {
                        ok = -1;
                        goto done;
                    }
                    break;
                case 2:
                    if (n >= 3u) {
                        int r = (n % 2u == 1u)
                                    ? tri_push(out, &run[n - 3], &run[n - 2], &run[n - 1])
                                    : tri_push(out, &run[n - 2], &run[n - 3], &run[n - 1]);

                        if (r) {
                            ok = -1;
                            goto done;
                        }
                    }
                    break;
                default:
                    if (n >= 4u && n % 2u == 0u &&
                        (tri_push(out, &run[n - 4], &run[n - 3], &run[n - 1]) ||
                         tri_push(out, &run[n - 4], &run[n - 1], &run[n - 2]))) {
                        ok = -1;
                        goto done;
                    }
                    break;
                }
            }
        }
    }
done:
    free(run);
    return ok;
}

/* ------------------------------------------------------------------ */
/* Light                                                               */
/* ------------------------------------------------------------------ */

typedef struct {
    float dir[4][3], col[4][3];
    u8 clear[3], fog[3];
} Lights;

static void vnorm(float v[3])
{
    float l = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);

    if (l < 1e-9f) {
        l = 1e-9f;
    }
    v[0] /= l; v[1] /= l; v[2] /= l;
}

static void lights_fixed(Lights *l)
{
    memset(l, 0, sizeof *l);
    l->dir[0][0] = -0.47f; l->dir[0][1] = -0.87f; l->dir[0][2] = -0.07f;
    vnorm(l->dir[0]);
    l->dir[1][2] = 1.0f;
    l->col[0][0] = 0.75f; l->col[0][1] = 0.75f; l->col[0][2] = 0.65f;
    l->col[1][0] = l->col[1][1] = l->col[1][2] = 0.2f;
    l->clear[0] = 150; l->clear[1] = 190; l->clear[2] = 235;
    memcpy(l->fog, l->clear, 3u);
}

static float shade_dot(const float a[3], const float b[3])
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

static void shade(const Lights *l, const Mat *m, const float n[3], float out[3])
{
    int i, k;

    for (k = 0; k < 3; k++) {
        out[k] = m->emission[k];
    }
    for (i = 0; i < 4; i++) {
        float d;

        if (((m->light_mask >> i) & 1u) == 0u) {
            continue;
        }
        d = -shade_dot(l->dir[i], n);
        if (d < 0.0f) {
            d = 0.0f;
        }
        for (k = 0; k < 3; k++) {
            out[k] += m->diffuse[k] * l->col[i][k] * d + m->ambient[k] * l->col[i][k];
        }
    }
    for (k = 0; k < 3; k++) {
        out[k] = out[k] < 0.0f ? 0.0f : out[k] > 1.0f ? 1.0f : out[k];
    }
}

/* _TimeZoneLightTable: the hour each of 5 time zones starts, per season. */
static int zone_hour(int season, unsigned tz)
{
    static const u8 k_fallback[4][5] = {
        { 5, 10, 17, 20, 0 }, { 4, 9, 19, 21, 0 }, { 6, 10, 17, 20, 0 }, { 7, 11, 17, 19, 0 }
    };
    const u8 *t = (const u8 *)bw_arm9_rodata(0x0209DF10u, 20u);

    if (tz >= 5u) {
        return 0;
    }
    return t != NULL ? t[(season & 3) * 5 + (int)tz] : k_fallback[season & 3][tz];
}

/* ------------------------------------------------------------------ */
/* Files                                                               */
/* ------------------------------------------------------------------ */

typedef struct { char path[24]; BwArcFile f; } ArcSlot;

typedef struct { u8 *p; size_t n; } Blob;

static void blob_free(Blob *b)
{
    free(b->p);
    b->p = NULL;
    b->n = 0;
}

/* A building's looping texture animations (BTA0 files, up to four). */
typedef struct { Buf bta[4]; unsigned n; } BldAnim;

typedef struct Area {
    unsigned id;
    int loaded, indoor;
    unsigned texset, light_type;
    Blob btex;                  /* building texture set (BTX0) */
    Blob bfile;                 /* building member */
    Model *bmodels;
    unsigned *bids;
    BldAnim *banims;
    unsigned nb;
} Area;

typedef struct { int w, h; u8 *rgba; int partial, cutout; char key[96]; } TexEnt;

typedef struct { int ent; u8 flags; } TexRef;

/* water: a see-through sheet; placed: a placed model's (a building, a gym's
 * platform), floor only where the player's height is known (bmap_set_ref) */
typedef struct { float p[3][3]; u8 water, placed; } GTri;

/* One chunk's walk grid (bmap_tile): a class per tile, BMAP_TILE_*. */
typedef struct { int cx, cz; u8 cls[32 * 32]; } WalkChunk;

typedef struct { int tex; u8 alpha, cull, translucent; int bb; float pivot[3]; u8 far; unsigned short uvanim; u8 bob; } TriTag;

typedef struct {
    ArcSlot arcs[16];
    unsigned narcs;
    Blob area_file;
    Lights lights;
    int season;
    Area areas[64];
    unsigned nareas;
    Blob tsets[64];             /* ground texture sets by texset id (key) */
    unsigned tset_id[64];
    unsigned ntsets;
    TexEnt *ents;
    size_t nents, ent_cap;
    BmapTexture *tex;
    TexRef *texrefs;
    size_t ntex, tex_cap;
    BmapVertex *v;
    TriTag *tag;
    size_t nv, vcap, ntag, tcap;
    GTri *gt;
    size_t ngt, gcap;
    u8 cur_far;                 /* the chunk being added lies beyond NEAR_REACH */
    WalkChunk *walk;            /* the near chunks' walk grids               */
    size_t nwalk, walk_cap;
    unsigned obj;           /* the object add_model is adding (0: ground)  */
    u8 cur_bob;             /* the triangles being added rise and fall (a live object) */
    float bob_y[BMAP_BOB_MAX];
    unsigned bob_n;
    BmapUvAnim *uva;        /* the texture animations, and what each is of */
    const u8 **uva_key;
    size_t nuva, uva_cap;
    /* research (BMAP_JOIN): other zones' maps laid beside this one (see add_tri_research) */
    struct {
        unsigned zone;
        float a[3], b[2], cs, sn;   /* the joined map's point b lies on this map's point a, turned about it */
        float l[2], n[2], width;    /* the line between the two (a point, the joined map's side), the strip's half width (0: no limit) */
    } joins[8];
    int njoin, join_mode;           /* what both maps draw: 0 cut at the line, 1 whole objects go, 2 nothing goes */
    int join_cur;                   /* -1: this map's own triangles; >= 0: that join's; -2: neither (added after) */
} Ctx;

static ArcSlot *arc_get(Ctx *c, const char *path)
{
    unsigned i;

    for (i = 0; i < c->narcs; i++) {
        if (strcmp(c->arcs[i].path, path) == 0) {
            return &c->arcs[i];
        }
    }
    if (c->narcs >= 16u) {
        return NULL;
    }
    snprintf(c->arcs[c->narcs].path, sizeof c->arcs[0].path, "%s", path);
    if (bw_arc_open(&c->arcs[c->narcs].f, path) != 0) {
        return NULL;
    }
    return &c->arcs[c->narcs++];
}

static int arc_load(Ctx *c, const char *path, u32 idx, Blob *out)
{
    ArcSlot *a = arc_get(c, path);
    long n;

    out->p = NULL;
    out->n = 0;
    if (a == NULL) {
        return -1;
    }
    n = bw_arc_member_size(&a->f, idx);
    if (n <= 0) {
        return -1;
    }
    out->p = (u8 *)malloc((size_t)n);
    if (out->p == NULL) {
        return -1;
    }
    if (bw_arc_read_member(&a->f, idx, out->p, (u32)n) != n) {
        blob_free(out);
        return -1;
    }
    out->n = (size_t)n;
    return 0;
}

static int read_flat(const char *rel, Blob *out)
{
    char path[512];
    FILE *f;
    long n;

    out->p = NULL;
    out->n = 0;
    if (bw_data_path(path, sizeof path, rel) != 0 || (f = fopen(path, "rb")) == NULL) {
        return -1;
    }
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n <= 0 || (out->p = (u8 *)malloc((size_t)n)) == NULL ||
        fread(out->p, 1u, (size_t)n, f) != (size_t)n) {
        blob_free(out);
        fclose(f);
        return -1;
    }
    out->n = (size_t)n;
    fclose(f);
    return 0;
}

static Buf bb_of(const Blob *b)
{
    Buf r;

    r.p = b->p;
    r.n = b->n;
    return r;
}

/* ------------------------------------------------------------------ */
/* Areas and light                                                     */
/* ------------------------------------------------------------------ */

static int area_record(Ctx *c, unsigned area, unsigned *texset, int *indoor, unsigned *light)
{
    unsigned index = (area >= 2u && area < 0xD2u) ? area + (unsigned)(c->season & 3) : area;
    Buf f = bb_of(&c->area_file);

    if (index >= AREA_RECORDS) {
        index = 0;
    }
    if ((size_t)(index + 1u) * 10u > f.n) {
        return -1;
    }
    *texset = rd16(f, index * 10u + 2u);
    *indoor = f.p[index * 10u + 6u] != 0u;
    *light = f.p[index * 10u + 7u];
    return 0;
}

static void load_lights(Ctx *c, unsigned light_type, int hour, int minute)
{
    Blob t;
    unsigned keys, k, pick = 0u;
    int seconds = hour * 3600 + minute * 60, found = 0;

    lights_fixed(&c->lights);
    if (arc_load(c, "a/0/6/1", light_type, &t) != 0) {
        return;
    }
    keys = (unsigned)(t.n / 0x34u);
    for (k = 0; k < keys && !found; k++) {
        Buf r = sub(bb_of(&t), k * 0x34u);
        int when = zone_hour(c->season, rd16(r, 0)) * 3600 + rds16(r, 2) * 60;

        if (when > seconds) {
            pick = k;
            found = 1;
        }
    }
    if (keys > 0u) {
        Buf r = sub(bb_of(&t), pick * 0x34u);
        int i, k2;
        float tmp[3];

        memset(&c->lights, 0, sizeof c->lights);
        rgb555(rd16(r, 0x32), tmp);
        for (k2 = 0; k2 < 3; k2++) {
            c->lights.clear[k2] = (u8)(tmp[k2] * 255.0f);
        }
        rgb555(rd16(r, 0x30), tmp);
        for (k2 = 0; k2 < 3; k2++) {
            c->lights.fog[k2] = (u8)(tmp[k2] * 255.0f);
        }
        for (i = 0; i < 4; i++) {
            if (r.p[4 + i] == 0) {
                continue;
            }
            for (k2 = 0; k2 < 3; k2++) {
                c->lights.dir[i][k2] = (float)rds16(r, 16u + (size_t)i * 6u + (size_t)k2 * 2u) / 4096.0f;
            }
            vnorm(c->lights.dir[i]);
            rgb555(rd16(r, 8u + (size_t)i * 2u), c->lights.col[i]);
        }
    }
    blob_free(&t);
}

/* The zone's own fog (a/0/8/1: member 13 maps zone -> record of offset and
 * slope in depth-buffer units).  The game fogs by depth relative to the field
 * camera, so the depths turn into distances from the camera, then into
 * distances from the player (the camera looks at the player from `distance`). */
static void load_fog(Ctx *c, unsigned zone, Buf headers, BmapScene *s)
{
    Blob t, r, cam;
    unsigned i, setting = rd16(headers, zone * 48u + 0x1Cu) >> 9;

    s->fog_on = 0;
    if (arc_load(c, "a/0/8/1", 13u, &t) != 0) {
        return;
    }
    for (i = 0; i + 4u <= t.n; i += 4u) {
        Buf tb = bb_of(&t);

        if (rd16(tb, i) == zone && arc_load(c, "a/0/8/1", rd16(tb, i + 2u), &r) == 0) {
            Buf rb = bb_of(&r);
            unsigned offset = rd32(rb, 0), slope = rd32(rb, 4);

            if (offset < 0x7FFFu && arc_load(c, "a/0/6/0", 0u, &cam) == 0) {
                Buf cb = bb_of(&cam);

                if ((setting + 1u) * 0x2Cu <= cb.n) {
                    Buf rec = sub(cb, setting * 0x2Cu);
                    float dist = (float)rds32(rec, 0);
                    float n = (float)rds32(rec, 0x14) / 4096.0f;
                    float f = (float)rds32(rec, 0x18) / 4096.0f;
                    float step = (float)(0x400u >> (slope > 10u ? 10u : slope));
                    float k1, base, sl, zs, ze;

                    if (n < 0.5f) {
                        n = 0.5f;
                    }
                    if (f < n + 1.0f) {
                        f = n + 1.0f;
                    }
                    k1 = (f + n) / (f - n);
                    base = 16383.0f * (1.0f + k1);
                    sl = 16383.0f * 2.0f * f * n / (f - n);
                    zs = sl / (base - (float)offset);
                    ze = sl / (base - (float)offset - 32.0f * step);
                    if (rec.p[0x11] != 0u) {            /* W buffer: depth is 8 * distance */
                        zs = (float)offset / 8.0f;
                        ze = ((float)offset + 32.0f * step) / 8.0f;
                    }
                    if (rec.p[0x11] == 0u && (base - (float)offset - 32.0f * step <= 0.0f || ze < zs)) {
                        ze = f;
                    }
                    s->fog_start = zs - dist;
                    s->fog_end = ze - dist;
                    if (s->fog_start < 0.0f) {
                        s->fog_start = 0.0f;
                    }
                    if (s->fog_end < s->fog_start + 1.0f) {
                        s->fog_end = s->fog_start + 1.0f;
                    }
                    s->fog_on = 1;
                }
                blob_free(&cam);
            }
            blob_free(&r);
            break;
        }
    }
    blob_free(&t);
}

static Blob *ground_set(Ctx *c, unsigned texset)
{
    unsigned i;

    for (i = 0; i < c->ntsets; i++) {
        if (c->tset_id[i] == texset) {
            return &c->tsets[i];
        }
    }
    if (c->ntsets >= 64u) {
        return NULL;
    }
    c->tset_id[c->ntsets] = texset;
    memset(&c->tsets[c->ntsets], 0, sizeof c->tsets[0]);
    arc_load(c, "a/0/1/4", texset, &c->tsets[c->ntsets]);
    return &c->tsets[c->ntsets++];
}

static unsigned building_member(unsigned area, unsigned count)
{
    unsigned m = area >= 0xD2u ? area - 0xD2u : area >= 2u ? (area - 2u) / 4u : 0u;

    return m < count ? m : 0u;
}

static Area *area_get(Ctx *c, unsigned area)
{
    unsigned i, n, k;
    Area *a;
    const char *arc, *tarc;
    ArcSlot *s;
    unsigned member;
    Buf m;

    for (i = 0; i < c->nareas; i++) {
        if (c->areas[i].id == area) {
            return &c->areas[i];
        }
    }
    if (c->nareas >= 64u) {
        return NULL;
    }
    a = &c->areas[c->nareas++];
    memset(a, 0, sizeof *a);
    a->id = area;
    if (area_record(c, area, &a->texset, &a->indoor, &a->light_type) != 0) {
        return a;
    }
    arc = a->indoor ? "a/2/2/9" : "a/2/3/0";
    tarc = a->indoor ? "a/1/7/6" : "a/1/7/7";
    s = arc_get(c, arc);
    if (s == NULL) {
        return a;
    }
    member = building_member(area, s->f.count);
    if (arc_load(c, arc, member, &a->bfile) != 0) {
        return a;
    }
    arc_load(c, tarc, member, &a->btex);
    m = bb_of(&a->bfile);
    n = m.n > 2u ? m.p[2] / 2u : 0u;
    a->bmodels = (Model *)calloc(n ? n : 1u, sizeof(Model));
    a->bids = (unsigned *)calloc(n ? n : 1u, sizeof(unsigned));
    a->banims = (BldAnim *)calloc(n ? n : 1u, sizeof(BldAnim));
    if (a->bmodels == NULL || a->bids == NULL || a->banims == NULL) {
        return a;
    }
    for (k = 0; k < n; k++) {
        u32 def = rd32(m, 4u + k * 4u), file = rd32(m, 4u + (n + k) * 4u);
        Model md;

        if (file < m.n && model_parse(sub(m, file), &md) == 0) {
            BldAnim *an = &a->banims[a->nb];
            size_t at = def + 0x10u;
            unsigned j;

            a->bmodels[a->nb] = md;
            a->bids[a->nb] = rd16(m, def);
            if (at < m.n && m.p[at] == 1u) {        /* kind 1: every one, looping */
                for (j = 0; j < 4u; j++) {
                    int off = rds32(m, at + 4u + j * 4u);

                    if (off != -1 && at + (size_t)off + 4u <= m.n &&
                        memcmp(m.p + at + off, "BTA0", 4) == 0) {
                        an->bta[an->n++] = sub(m, at + (size_t)off);
                    }
                }
            }
            a->nb++;
        }
    }
    return a;
}

static void area_free(Area *a)
{
    unsigned i;

    for (i = 0; i < a->nb; i++) {
        model_free(&a->bmodels[i]);
    }
    free(a->bmodels);
    free(a->bids);
    free(a->banims);
    blob_free(&a->btex);
    blob_free(&a->bfile);
}

/* ------------------------------------------------------------------ */
/* Scene assembly                                                      */
/* ------------------------------------------------------------------ */

/* A decoded texture, cached by (set, name, palette). */
static int tex_ent(Ctx *c, const char *setkey, Buf set, const Mat *mt)
{
    char key[96];
    size_t i;
    Image img;
    int r;

    snprintf(key, sizeof key, "%s|%s|%s", setkey, mt->tex, mt->has_pal ? mt->pal : "");
    for (i = 0; i < c->nents; i++) {
        if (strcmp(c->ents[i].key, key) == 0) {
            return c->ents[i].rgba != NULL ? (int)i : -1;
        }
    }
    {
        TexEnt *ne = (TexEnt *)grow(c->ents, &c->ent_cap, c->nents, sizeof *ne);

        if (ne == NULL) {
            return -1;
        }
        c->ents = ne;
    }
    memset(&img, 0, sizeof img);
    r = decode_in(set, mt->tex, mt->has_pal ? mt->pal : NULL, &img);
    memset(&c->ents[c->nents], 0, sizeof c->ents[0]);
    snprintf(c->ents[c->nents].key, sizeof c->ents[0].key, "%s", key);
    if (r == 0) {
        c->ents[c->nents].w = img.w;
        c->ents[c->nents].h = img.h;
        c->ents[c->nents].rgba = img.rgba;
        c->ents[c->nents].partial = img.partial;
        c->ents[c->nents].cutout = img.cutout;
    }
    c->nents++;
    return r == 0 ? (int)(c->nents - 1u) : -1;
}

/* The scene texture for (entry, wrap flags). */
static int tex_ref(Ctx *c, int ent, const Mat *mt)
{
    u8 flags = (u8)((mt->repeat[0] ? 1 : 0) | (mt->repeat[1] ? 2 : 0) |
                    (mt->flip[0] ? 4 : 0) | (mt->flip[1] ? 8 : 0));
    size_t i;
    BmapTexture *nt;
    TexRef *nr;
    TexEnt *e = &c->ents[ent];

    for (i = 0; i < c->ntex; i++) {
        if (c->texrefs[i].ent == ent && c->texrefs[i].flags == flags) {
            return (int)i;
        }
    }
    {
        size_t cap1 = c->tex_cap, cap2 = c->tex_cap;

        nt = (BmapTexture *)grow(c->tex, &cap1, c->ntex, sizeof *nt);
        if (nt == NULL) {
            return -1;
        }
        c->tex = nt;
        nr = (TexRef *)grow(c->texrefs, &cap2, c->ntex, sizeof *nr);
        if (nr == NULL) {
            return -1;
        }
        c->texrefs = nr;
        c->tex_cap = cap1;
    }
    c->tex[c->ntex].w = e->w;
    c->tex[c->ntex].h = e->h;
    c->tex[c->ntex].rgba = (u8 *)malloc((size_t)e->w * (size_t)e->h * 4u);
    if (c->tex[c->ntex].rgba == NULL) {
        return -1;
    }
    memcpy(c->tex[c->ntex].rgba, e->rgba, (size_t)e->w * (size_t)e->h * 4u);
    c->tex[c->ntex].repeat_s = (u8)mt->repeat[0];
    c->tex[c->ntex].repeat_t = (u8)mt->repeat[1];
    c->tex[c->ntex].flip_s = (u8)mt->flip[0];
    c->tex[c->ntex].flip_t = (u8)mt->flip[1];
    c->texrefs[c->ntex].ent = ent;
    c->texrefs[c->ntex].flags = flags;
    return (int)c->ntex++;
}

/* A texture SRT track at a whole frame (NNSi_G3dAnmGetTexSRTScalarData,
 * NNSi_G3dAnmGetTexSRTRotData): bit 29 an inline value, bits 31:30 every
 * key, every second or every fourth frame (the frames between the mean of
 * two keys or a 3:1 blend, up to the last interpolated frame in bits 15:0),
 * bit 28 s16 keys.  The keys lie at `off` from the animation.  A rotation
 * key is a (sin, cos) pair of s16 in one word; `rot` takes those. */
static void srt_keys(Buf a, u32 info, u32 off, unsigned f, int rot, int *k0, int *k1, int *w)
{
    unsigned last = info & 0xFFFFu, q;

    (void)a;
    (void)off;
    (void)rot;
    *w = 4;
    *k0 = *k1 = (int)f;
    if ((info & 0xC0000000u) == 0u) {
        return;
    }
    if (info & 0x40000000u) {                   /* every second */
        if ((f & 1u) == 0u) {
            *k0 = *k1 = (int)(f >> 1);
        } else if (f > last) {
            *k0 = *k1 = (int)((last >> 1) + 1u);
        } else {
            *k0 = (int)(f >> 1);
            *k1 = *k0 + 1;
            *w = 2;
        }
        return;
    }
    q = f & 3u;                                 /* every fourth */
    if (q == 0u) {
        *k0 = *k1 = (int)(f >> 2);
    } else if (f > last) {
        *k0 = *k1 = (int)(q + (last >> 2));
    } else if (q == 2u) {
        *k0 = (int)(f >> 2);
        *k1 = *k0 + 1;
        *w = 2;
    } else if (q == 1u) {
        *k0 = (int)(f >> 2);
        *k1 = *k0 + 1;
        *w = 3;                                 /* 3:1 toward the first */
    } else {
        *k0 = (int)(f >> 2) + 1;
        *k1 = (int)(f >> 2);
        *w = 3;                                 /* 3:1 toward the next */
    }
}

static int srt_blend(int v0, int v1, int w)
{
    return w == 4 ? v0 : w == 2 ? (v0 + v1) >> 1 : (v0 * 3 + v1) >> 2;
}

static float srt_scalar(Buf a, u32 info, u32 off, unsigned f)
{
    int k0, k1, w;

    if (info & 0x20000000u) {
        return (float)(int)off / 4096.0f;
    }
    srt_keys(a, info, off, f, 0, &k0, &k1, &w);
    if (info & 0x10000000u) {
        return (float)srt_blend(rds16(a, off + (size_t)k0 * 2u), rds16(a, off + (size_t)k1 * 2u), w) / 4096.0f;
    }
    return (float)srt_blend(rds32(a, off + (size_t)k0 * 4u), rds32(a, off + (size_t)k1 * 4u), w) / 4096.0f;
}

static void srt_rot(Buf a, u32 info, u32 off, unsigned f, float *sn, float *cs)
{
    int k0, k1, w;

    if (info & 0x20000000u) {
        *sn = (float)(short)(off & 0xFFFFu) / 4096.0f;
        *cs = (float)(short)(off >> 16) / 4096.0f;
        return;
    }
    srt_keys(a, info, off, f, 1, &k0, &k1, &w);
    *sn = (float)srt_blend(rds16(a, off + (size_t)k0 * 4u), rds16(a, off + (size_t)k1 * 4u), w) / 4096.0f;
    *cs = (float)srt_blend(rds16(a, off + (size_t)k0 * 4u + 2u), rds16(a, off + (size_t)k1 * 4u + 2u), w) / 4096.0f;
}

/* The texture animation of material `name` in a building's BTA0 files, as
 * 1 + its index in c->uva (made once per material and file), or 0.  The
 * matrix is the one the hardware gets (NNSi_G3dTexMtxScaleRotTrans4x4 and
 * its cheaper siblings, which are the same formula with a term left out),
 * in coordinates over the texture's size, which is what the vertices hold. */
static unsigned short uv_anim(Ctx *c, const BldAnim *an, const char *name)
{
    unsigned bi;

    for (bi = 0; an != NULL && bi < an->n; bi++) {
        Buf f = an->bta[bi];
        size_t blk = rd32(f, 16);
        Dict anims, mats;
        unsigned ai, mi;

        if (blk + 8u > f.n || memcmp(f.p + blk, "SRT0", 4) != 0 || dict_parse(f, blk + 8u, &anims) != 0) {
            continue;
        }
        for (ai = 0; ai < anims.count; ai++) {
            Buf a = sub(f, blk + dict_off(&anims, ai));
            unsigned frames = rd16(a, 4);

            if (frames == 0u || dict_parse(a, 8, &mats) != 0 || mats.entry < 40u) {
                continue;
            }
            for (mi = 0; mi < mats.count; mi++) {
                char nm[17];
                size_t r = mats.rec + (size_t)mi * mats.entry, k;
                BmapUvAnim *ua;
                float *m;
                unsigned fr;

                dict_name(&mats, mi, nm);
                if (strcmp(nm, name) != 0) {
                    continue;
                }
                for (k = 0; k < c->nuva; k++) {
                    if (c->uva_key[k] == a.p + r) {
                        return (unsigned short)(k + 1u);
                    }
                }
                if (c->nuva >= 0xFFFEu) {
                    return 0;
                }
                if (c->nuva == c->uva_cap) {
                    size_t cap = c->uva_cap ? c->uva_cap * 2u : 8u;
                    BmapUvAnim *nu = (BmapUvAnim *)realloc(c->uva, cap * sizeof *nu);
                    const u8 **nk;

                    if (nu == NULL) {
                        return 0;
                    }
                    c->uva = nu;
                    nk = (const u8 **)realloc((void *)c->uva_key, cap * sizeof *nk);
                    if (nk == NULL) {
                        return 0;
                    }
                    c->uva_key = nk;
                    c->uva_cap = cap;
                }
                m = (float *)malloc((size_t)frames * 6u * sizeof *m);
                if (m == NULL) {
                    return 0;
                }
                for (fr = 0; fr < frames; fr++) {
                    float ss = srt_scalar(a, rd32(a, r + 0), rd32(a, r + 4), fr);
                    float st = srt_scalar(a, rd32(a, r + 8), rd32(a, r + 12), fr);
                    float ts = srt_scalar(a, rd32(a, r + 24), rd32(a, r + 28), fr);
                    float tt = srt_scalar(a, rd32(a, r + 32), rd32(a, r + 36), fr);
                    float sn, cs;
                    float *o = m + (size_t)fr * 6u;

                    srt_rot(a, rd32(a, r + 16), rd32(a, r + 20), fr, &sn, &cs);
                    o[0] = ss * cs;
                    o[1] = ss * sn;
                    o[2] = ss * (1.0f - sn - cs) * 0.5f - ss * ts;
                    o[3] = -st * sn;
                    o[4] = st * cs;
                    o[5] = (2.0f + st * sn - st * cs - st) * 0.5f + st * tt;
                }
                ua = &c->uva[c->nuva];
                ua->frames = frames;
                ua->m = m;
                c->uva_key[c->nuva++] = a.p + r;
                return (unsigned short)c->nuva;
            }
        }
    }
    return 0;
}

static int add_tri_raw(Ctx *c, const BmapVertex t[3], const TriTag *tag, const float pos[3][3], int ground, int level_ok);

/*
 * Research (BMAP_JOIN): the map a door leads to, laid beside this one.
 *
 * Both maps draw the ground round the door, each in its own way (a street's
 * map goes on past its door with a stub of road and a picture of a skyline;
 * the plaza's map has a thin passage where the street is).  What happens to
 * that doubled part is BMAP_JOIN_MODE:
 *   line    both maps are cut at the door's line: this map ends there, the
 *           joined one begins there (inside the strip's width, when it has one)
 *   whole   nothing is cut through: an object (a building, a tree) goes or
 *           stays as a whole, by its middle; only the ground is cut at the line
 *   none    nothing goes: both maps as they are
 */
static float jn_d(const Ctx *c, int j, float x, float z)        /* over 0: the joined map's side of the line */
{
    return (x - c->joins[j].l[0]) * c->joins[j].n[0] + (z - c->joins[j].l[1]) * c->joins[j].n[1];
}

static float jn_l(const Ctx *c, int j, float x, float z)        /* along the line, from its point */
{
    return -(x - c->joins[j].l[0]) * c->joins[j].n[1] + (z - c->joins[j].l[1]) * c->joins[j].n[0];
}

/* Does this map's own point lie where a joined map takes over? */
static int jn_base_gone(const Ctx *c, float x, float z)
{
    int j;

    for (j = 0; j < c->njoin; j++) {
        if (jn_d(c, j, x, z) > 0.0f && (c->joins[j].width <= 0.0f || fabsf(jn_l(c, j, x, z)) < c->joins[j].width)) {
            return 1;
        }
    }
    return 0;
}

/* Does a joined map's point (in this map's frame) lie in its own part? */
static int jn_join_kept(const Ctx *c, int j, float x, float z)
{
    return jn_d(c, j, x, z) >= 0.0f && (c->joins[j].width <= 0.0f || fabsf(jn_l(c, j, x, z)) <= c->joins[j].width);
}

/* The part of a polygon where ax * x + az * z + k >= 0 (at most one corner more). */
static int jn_clip(const BmapVertex *in, int n, BmapVertex *out, float ax, float az, float k0)
{
    int i, m = 0;

    for (i = 0; i < n; i++) {
        const BmapVertex *p = &in[i], *q = &in[(i + 1) % n];
        float dp = ax * p->x + az * p->z + k0, dq = ax * q->x + az * q->z + k0;

        if (dp >= 0.0f) {
            out[m++] = *p;
        }
        if ((dp >= 0.0f) != (dq >= 0.0f)) {
            float f = dp / (dp - dq);
            BmapVertex v = *p;

            v.x += f * (q->x - p->x); v.y += f * (q->y - p->y); v.z += f * (q->z - p->z);
            v.u += f * (q->u - p->u); v.v += f * (q->v - p->v);
            v.r = (u8)(p->r + f * ((float)q->r - (float)p->r) + 0.5f);
            v.g = (u8)(p->g + f * ((float)q->g - (float)p->g) + 0.5f);
            v.b = (u8)(p->b + f * ((float)q->b - (float)p->b) + 0.5f);
            out[m++] = v;
        }
    }
    return m;
}

static int jn_emit(Ctx *c, const BmapVertex *p, int n, const TriTag *tag, int ground, int level_ok)
{
    int i, k;

    for (i = 1; i + 1 < n; i++) {
        BmapVertex t[3];
        float wp[3][3];

        t[0] = p[0]; t[1] = p[i]; t[2] = p[i + 1];
        for (k = 0; k < 3; k++) {
            wp[k][0] = t[k].x; wp[k][1] = t[k].y; wp[k][2] = t[k].z;
        }
        if (add_tri_raw(c, t, tag, (const float (*)[3])wp, ground, level_ok) != 0) {
            return -1;
        }
    }
    return 0;
}

#define JN_CORNERS 16
#define JN_PIECES 48

static int add_tri_research(Ctx *c, const BmapVertex t[3], const TriTag *tag, int ground, int level_ok)
{
    BmapVertex v[3];
    TriTag tg = *tag;
    int k, j = c->join_cur;

    memcpy(v, t, sizeof v);
    if (j >= 0) {
        /* a joined map's triangle: to its place in this map's frame */
        for (k = 0; k < 3; k++) {
            float dx = t[k].x - c->joins[j].b[0], dz = t[k].z - c->joins[j].b[1];

            v[k].x = c->joins[j].a[0] + c->joins[j].cs * dx - c->joins[j].sn * dz;
            v[k].y = t[k].y + c->joins[j].a[1];
            v[k].z = c->joins[j].a[2] + c->joins[j].sn * dx + c->joins[j].cs * dz;
        }
        if (tg.bb != 0) {
            float dx = tg.pivot[0] - c->joins[j].b[0], dz = tg.pivot[2] - c->joins[j].b[1];

            tg.pivot[0] = c->joins[j].a[0] + c->joins[j].cs * dx - c->joins[j].sn * dz;
            tg.pivot[1] += c->joins[j].a[1];
            tg.pivot[2] = c->joins[j].a[2] + c->joins[j].sn * dx + c->joins[j].cs * dz;
        }
        tg.far = 1;
        ground = 0;
        level_ok = 0;
        if (c->join_mode == 2 || (c->join_mode == 1 && v[0].obj != 0u && tg.bb == 0)) {
            return jn_emit(c, v, 3, &tg, 0, 0);     /* (whole: its object is judged when the map is in) */
        }
        if (tg.bb != 0) {
            return jn_join_kept(c, j, tg.pivot[0], tg.pivot[2]) ? jn_emit(c, v, 3, &tg, 0, 0) : 0;
        }
        {
            BmapVertex a[JN_CORNERS], b[JN_CORNERS];
            const float *n = c->joins[j].n, *l = c->joins[j].l;
            float w = c->joins[j].width;
            int m = jn_clip(v, 3, a, n[0], n[1], -(l[0] * n[0] + l[1] * n[1]));

            if (m >= 3 && w > 0.0f) {
                /* inside the strip: -w <= along <= w, along = -(x - lx) nz + (z - lz) nx */
                float k0 = l[0] * n[1] - l[1] * n[0];

                m = jn_clip(a, m, b, -n[1], n[0], k0 + w);
                m = m >= 3 ? jn_clip(b, m, a, n[1], -n[0], -k0 + w) : 0;
            }
            return m >= 3 ? jn_emit(c, a, m, &tg, 0, 0) : 0;
        }
    }
    /* this map's own triangle */
    if (c->join_mode == 2 || (c->join_mode == 1 && v[0].obj != 0u && tg.bb == 0)) {
        return jn_emit(c, v, 3, &tg, ground, level_ok);
    }
    if (tg.bb != 0) {
        return jn_base_gone(c, tg.pivot[0], tg.pivot[2]) ? 0 : jn_emit(c, v, 3, &tg, ground, level_ok);
    }
    {
        /* what is left of it outside every join's part: pieces, cut join by join */
        static BmapVertex pc[2][JN_PIECES][JN_CORNERS];
        int cnt[2][JN_PIECES], np[2] = { 1, 0 }, cur = 0, q;

        memcpy(pc[0][0], v, sizeof v);
        cnt[0][0] = 3;
        for (j = 0; j < c->njoin; j++) {
            const float *n = c->joins[j].n, *l = c->joins[j].l;
            float w = c->joins[j].width, kd = -(l[0] * n[0] + l[1] * n[1]), ka = l[0] * n[1] - l[1] * n[0];
            int nx = 1 - cur;

            np[nx] = 0;
            for (q = 0; q < np[cur]; q++) {
                BmapVertex rest[JN_CORNERS];
                int m, mr;

                /* this map's side of the line stays */
                if (np[nx] < JN_PIECES && (m = jn_clip(pc[cur][q], cnt[cur][q], pc[nx][np[nx]], -n[0], -n[1], -kd)) >= 3) {
                    cnt[nx][np[nx]++] = m;
                }
                if (w <= 0.0f) {
                    continue;
                }
                /* past the line, what lies beside the strip stays too */
                mr = jn_clip(pc[cur][q], cnt[cur][q], rest, n[0], n[1], kd);
                if (mr >= 3 && np[nx] < JN_PIECES && (m = jn_clip(rest, mr, pc[nx][np[nx]], -n[1], n[0], ka - w)) >= 3) {
                    cnt[nx][np[nx]++] = m;
                }
                if (mr >= 3 && np[nx] < JN_PIECES && (m = jn_clip(rest, mr, pc[nx][np[nx]], n[1], -n[0], -ka - w)) >= 3) {
                    cnt[nx][np[nx]++] = m;
                }
            }
            cur = nx;
        }
        for (q = 0; q < np[cur]; q++) {
            if (jn_emit(c, pc[cur][q], cnt[cur][q], &tg, ground, level_ok) != 0) {
                return -1;
            }
        }
    }
    return 0;
}

/* BMAP_JOIN_MODE whole: the objects among the triangles from `first` on go or stay as a whole, by the middle of
 * their corners (which < 0: this map's own objects; else that join's). */
static void jn_drop_whole(Ctx *c, size_t first, int which)
{
    size_t i = first, out = first, n = c->ntag;

    while (i < n) {
        size_t e = i, k;
        unsigned obj = c->v[i * 3u].obj;
        double sx = 0.0, sz = 0.0;
        int keep = 1;

        while (e < n && c->v[e * 3u].obj == obj && (c->tag[e].bb != 0) == (c->tag[i].bb != 0)) {
            e++;
        }
        if (obj != 0u && c->tag[i].bb == 0) {
            for (k = i * 3u; k < e * 3u; k++) {
                sx += c->v[k].x;
                sz += c->v[k].z;
            }
            sx /= (double)((e - i) * 3u);
            sz /= (double)((e - i) * 3u);
            keep = which < 0 ? !jn_base_gone(c, (float)sx, (float)sz) : jn_join_kept(c, which, (float)sx, (float)sz);
        }
        if (keep) {
            if (out != i) {
                memmove(&c->v[out * 3u], &c->v[i * 3u], (e - i) * 3u * sizeof *c->v);
                memmove(&c->tag[out], &c->tag[i], (e - i) * sizeof *c->tag);
            }
            out += e - i;
        }
        i = e;
    }
    c->ntag = out;
    c->nv = out * 3u;
}

static int add_tri(Ctx *c, const BmapVertex t[3], const TriTag *tag, const float pos[3][3], int ground, int level_ok)
{
    if (c->njoin > 0 && c->join_cur != -2) {
        return add_tri_research(c, t, tag, ground, level_ok);
    }
    return add_tri_raw(c, t, tag, pos, ground, level_ok);
}

static int add_tri_raw(Ctx *c, const BmapVertex t[3], const TriTag *tag, const float pos[3][3], int ground, int level_ok)
{
    BmapVertex *nv;
    TriTag *ng;
    size_t cap1 = c->vcap, cap2 = c->tcap;

    nv = (BmapVertex *)grow(c->v, &cap1, c->nv + 3u, sizeof *nv);
    if (nv == NULL) {
        return -1;
    }
    c->v = nv;
    c->vcap = cap1;
    ng = (TriTag *)grow(c->tag, &cap2, c->ntag, sizeof *ng);
    if (ng == NULL) {
        return -1;
    }
    c->tag = ng;
    c->tcap = cap2;
    memcpy(&c->v[c->nv], t, 3u * sizeof *t);
    c->nv += 3u;
    c->tag[c->ntag] = *tag;
    c->tag[c->ntag++].bob = c->cur_bob;
    if (ground && level_ok && !tag->far) {
        GTri *g = (GTri *)grow(c->gt, &c->gcap, c->ngt, sizeof *g);

        if (g == NULL) {
            return -1;
        }
        c->gt = g;
        memcpy(g[c->ngt].p, pos, sizeof g[0].p);
        g[c->ngt].water = tag->translucent;
        g[c->ngt].placed = (u8)(ground == 2);
        c->ngt++;
    }
    return 0;
}

/*
 * A remade model (bmap_remade.h): what its builder made of it, applied to
 * every copy the map places.  A triangle of the model is named by its draw
 * command and its place in that command's list: ((draw + 1) << 16) | triangle.
 *   two    these triangles are shown from both sides
 *   drop   these are left out (a flat picture replaced by a body)
 *   turn   a copy of this triangle turned half round about the upright
 *          through (cx, cz) in the model's own space, lit as the game lights it
 *   tri    a new triangle in the model's space, with the material of a draw
 *          command: lit by the game with its normal, or in the colour given;
 *          its texture the draw's own or one of the record's
 */
typedef RmTurn DoneTurn;        /* a turned copy (remade/remade.h) */
typedef RmNew DoneTri;          /* a new triangle (remade/remade.h) */
typedef struct Done {
    char key[24];
    int state;                  /* 1 made, -1 the builder made nothing */
    int ntex;
    struct { int w, h; u8 rs, rt; u8 *rgba; int scene; } tex[8];
    u32 ntwo, ndrop, nturn, ntri;
    u32 flags;                  /* 1: the model as a whole rests on the floor under it (rest_bodies) */
    u32 *two, *drop;
    DoneTurn *turn;
    DoneTri *tri;
    const void *owner;          /* the build its scene textures belong to */
} Done;

static Done g_dones[512];
static unsigned g_ndones;
static const Done *g_done;      /* the completion of the model add_model is adding */
static int g_research_ids;      /* the models dump: a vertex's object number is its triangle's name */

static int remade_make(struct Done *d, const BmapRemade *e, Model *md, const char *setkey, Buf set);

static int done_has(const u32 *a, u32 n, u32 id)
{
    u32 lo = 0, hi = n;

    while (lo < hi) {
        u32 mid = (lo + hi) / 2u;

        if (a[mid] < id) lo = mid + 1u; else hi = mid;
    }
    return lo < n && a[lo] == id;
}

/*
 * The remade form of building model `id` of pack `pack`, or NULL: a flat
 * picture of the map (a rock, a seat, a bottle, a gate) as a body for the
 * battle's cameras (bmap_remade.h).  Made once, by the model's builder, from
 * the model `md` standing alone with its textures (`setkey`, `set`), and
 * kept for every copy and every later scene.  EIDOS_NO_REMADE=1 leaves
 * every model as the map has it.
 */
static const Done *done_get(const void *owner, int pack, unsigned id, Model *md, const char *setkey, Buf set)
{
    char key[24];
    unsigned i;
    Done *d = NULL;

    if (getenv("EIDOS_NO_REMADE") != NULL) {
        return NULL;
    }
    snprintf(key, sizeof key, "b%d_%05u", pack, id);
    for (i = 0; i < g_ndones && d == NULL; i++) {
        if (strcmp(g_dones[i].key, key) == 0) {
            d = &g_dones[i];
        }
    }
    if (d == NULL) {
        const BmapRemade *e = bmap_remade_find(key);

        if (e == NULL || g_ndones >= 512u) {
            return NULL;                /* (not remade: nothing is kept for it) */
        }
        d = &g_dones[g_ndones++];
        memset(d, 0, sizeof *d);
        snprintf(d->key, sizeof d->key, "%s", key);
        d->state = remade_make(d, e, md, setkey, set) ? 1 : -1;
    }
    if (d->state != 1) {
        return NULL;
    }
    if (d->owner != owner) {            /* a new build: its textures are not in this scene yet */
        int t;

        d->owner = owner;
        for (t = 0; t < d->ntex; t++) {
            d->tex[t].scene = -1;
        }
    }
    return d;
}

/* A remade model's own texture as a scene texture. */
static int done_tex(Ctx *c, Done *d, int t)
{
    size_t cap1 = c->tex_cap, cap2 = c->tex_cap;
    BmapTexture *nt;
    TexRef *nr;

    if (t < 0 || t >= d->ntex) {
        return -1;
    }
    if (d->tex[t].scene >= 0) {
        return d->tex[t].scene;
    }
    nt = (BmapTexture *)grow(c->tex, &cap1, c->ntex, sizeof *nt);
    if (nt == NULL) {
        return -1;
    }
    c->tex = nt;
    nr = (TexRef *)grow(c->texrefs, &cap2, c->ntex, sizeof *nr);
    if (nr == NULL) {
        return -1;
    }
    c->texrefs = nr;
    c->tex_cap = cap1;
    memset(&c->tex[c->ntex], 0, sizeof c->tex[0]);
    c->tex[c->ntex].w = d->tex[t].w;
    c->tex[c->ntex].h = d->tex[t].h;
    c->tex[c->ntex].rgba = (u8 *)malloc((size_t)d->tex[t].w * (size_t)d->tex[t].h * 4u);
    if (c->tex[c->ntex].rgba == NULL) {
        return -1;
    }
    memcpy(c->tex[c->ntex].rgba, d->tex[t].rgba, (size_t)d->tex[t].w * (size_t)d->tex[t].h * 4u);
    c->tex[c->ntex].repeat_s = d->tex[t].rs;
    c->tex[c->ntex].repeat_t = d->tex[t].rt;
    c->texrefs[c->ntex].ent = -1;
    c->texrefs[c->ntex].flags = 0;
    d->tex[t].scene = (int)c->ntex;
    return (int)c->ntex++;
}

static int add_model(Ctx *c, Model *md, const char *setkey, Buf set, const Aff *base, int ground,
                     const BldAnim *anim)
{
    unsigned di;
    Done *dn = (Done *)g_done;


    for (di = 0; di < md->ndraws; di++) {
        const DrawCmd *dc = &md->draws[di];
        const Mat *mt = &md->mats[dc->mat];
        Aff node;
        TriList tl;
        size_t ti;
        int ent = -1, tex = -1;
        int translucent, lit;
        TriTag tag;
        Buf dl;

        if (mt->alpha == 0u || mt->cullbits == 0u) {
            continue;
        }
        if (mt->polymode == 3u) {
            continue;                   /* a shadow volume (a dark cave's mask round the player): not a surface */
        }
        if (mt->has_tex) {
            ent = tex_ent(c, setkey, set, mt);
            if (ent >= 0) {
                tex = tex_ref(c, ent, mt);
            }
        }
        translucent = mt->alpha < 31u || (ent >= 0 && c->ents[ent].partial);
        node = aff_then(&md->world[dc->node], base);
        lit = mt->light_mask != 0u && dc->bb == 0;
        memset(&tag, 0, sizeof tag);
        tag.tex = tex;
        tag.alpha = (u8)mt->alpha;
        tag.cull = (u8)(mt->cullbits == 3u ? 0 : mt->cullbits == 2u ? 1 : 2);
        tag.translucent = (u8)translucent;
        tag.bb = dc->bb;
        tag.far = c->cur_far;
        tag.uvanim = anim != NULL ? uv_anim(c, anim, mt->name) : 0;
        if (dc->bb) {
            static const float zero[3] = { 0, 0, 0 };

            int ax, ay;

            aff_apply(&node, zero, tag.pivot);
            /* A billboard replaces the node's rotation by the camera's: keep
             * the translation (the pivot) and the scale only, so the card's
             * offsets from the pivot carry no stale rotation. */
            for (ax = 0; ax < 3; ax++) {
                float sc = sqrtf(node.r[ax][0] * node.r[ax][0] + node.r[ax][1] * node.r[ax][1] +
                                 node.r[ax][2] * node.r[ax][2]);

                for (ay = 0; ay < 3; ay++) {
                    node.r[ax][ay] = ax == ay ? sc : 0.0f;
                }
            }
        }
        dl = sub(md->m, md->shapes[dc->shape].off);
        if (dl.n > md->shapes[dc->shape].size) {
            dl.n = md->shapes[dc->shape].size;
        }
        memset(&tl, 0, sizeof tl);
        if (decode_list(dl, &tl) != 0) {
            free(tl.v);
            return -1;
        }
        for (ti = 0; ti + 3u <= tl.n; ti += 3u) {
            BmapVertex bv[3];
            float wp[3][3];
            int k, shaded_all = 1;
            u32 tri_id = ((u32)(di + 1u) << 16) | (u32)(ti / 3u);
            TriTag tag1 = tag;

            if (dn != NULL && done_has(dn->drop, dn->ndrop, tri_id)) {
                continue;
            }
            if (dn != NULL && done_has(dn->two, dn->ntwo, tri_id)) {
                tag1.cull = 0;
            }
            if (dn != NULL && dn->nturn > 0 && dc->bb == 0) {
                /* its turned copy: the same corners turned half round in the model's
                 * space, the normals with them, lit the same way */
                u32 lo = 0, hi = dn->nturn;

                while (lo < hi) {
                    u32 mid = (lo + hi) / 2u;

                    if (dn->turn[mid].id < tri_id) lo = mid + 1u; else hi = mid;
                }
                if (lo < dn->nturn && dn->turn[lo].id == tri_id) {
                    const Aff *nw = &md->world[dc->node];
                    BmapVertex tv[3];
                    float tp[3][3];
                    int tw = tex >= 0 ? c->tex[tex].w : 1, th = tex >= 0 ? c->tex[tex].h : 1;

                    for (k = 0; k < 3; k++) {
                        const Vtx *v = &tl.v[ti + (size_t)k];
                        float lp[3], mp[3], col[3], mn[3], n[3];
                        int q, shaded = 0;

                        lp[0] = v->pos[0] * md->pos_scale;
                        lp[1] = v->pos[1] * md->pos_scale;
                        lp[2] = v->pos[2] * md->pos_scale;
                        aff_apply(nw, lp, mp);
                        mp[0] = 2.0f * dn->turn[lo].cx - mp[0];
                        mp[2] = 2.0f * dn->turn[lo].cz - mp[2];
                        aff_apply(base, mp, tp[k]);
                        col[0] = v->col[0]; col[1] = v->col[1]; col[2] = v->col[2];
                        if (lit && (v->nrm[0] != 0.0f || v->nrm[1] != 0.0f || v->nrm[2] != 0.0f)) {
                            for (q = 0; q < 3; q++) {
                                mn[q] = v->nrm[0] * nw->r[0][q] + v->nrm[1] * nw->r[1][q] + v->nrm[2] * nw->r[2][q];
                            }
                            mn[0] = -mn[0];
                            mn[2] = -mn[2];
                            for (q = 0; q < 3; q++) {
                                n[q] = mn[0] * base->r[0][q] + mn[1] * base->r[1][q] + mn[2] * base->r[2][q];
                            }
                            vnorm(n);
                            shade(&c->lights, mt, n, col);
                            shaded = 1;
                        }
                        tv[k].x = tp[k][0]; tv[k].y = tp[k][1]; tv[k].z = tp[k][2];
                        tv[k].u = v->uv[0] / (float)tw;
                        tv[k].v = v->uv[1] / (float)th;
                        if (mt->vertex_color || shaded) {
                            for (q = 0; q < 3; q++) {
                                (&tv[k].r)[q] = (u8)(col[q] * 255.0f + 0.5f);
                            }
                        } else {
                            tv[k].r = tv[k].g = tv[k].b = 255;
                        }
                        tv[k].a = (u8)(mt->alpha * 255u / 31u);
                        tv[k].obj = ground == 1 ? 0u : c->obj;
                        if ((dn->flags & 1u) && !g_research_ids && ground != 1) {
                            tv[k].obj |= 0xC0000000u;   /* (it rests on the floor with the model) */
                        }
                    }
                    if (add_tri(c, tv, &tag, (const float (*)[3])tp, 0, 0) != 0) {
                        free(tl.v);
                        return -1;
                    }
                }
            }

            for (k = 0; k < 3; k++) {
                const Vtx *v = &tl.v[ti + (size_t)k];
                float lp[3], col[3];
                int shaded = 0, q;
                int tw = tex >= 0 ? c->tex[tex].w : 1, th = tex >= 0 ? c->tex[tex].h : 1;

                lp[0] = v->pos[0] * md->pos_scale;
                lp[1] = v->pos[1] * md->pos_scale;
                lp[2] = v->pos[2] * md->pos_scale;
                aff_apply(&node, lp, wp[k]);
                col[0] = v->col[0]; col[1] = v->col[1]; col[2] = v->col[2];
                if (lit && (v->nrm[0] != 0.0f || v->nrm[1] != 0.0f || v->nrm[2] != 0.0f)) {
                    float n[3];

                    for (q = 0; q < 3; q++) {
                        n[q] = v->nrm[0] * node.r[0][q] + v->nrm[1] * node.r[1][q] +
                               v->nrm[2] * node.r[2][q];
                    }
                    vnorm(n);
                    shade(&c->lights, mt, n, col);
                    shaded = 1;
                }
                if (!shaded) {
                    shaded_all = 0;
                }
                bv[k].x = wp[k][0];
                bv[k].y = wp[k][1];
                bv[k].z = wp[k][2];
                bv[k].u = v->uv[0] / (float)tw;
                bv[k].v = v->uv[1] / (float)th;
                if (mt->vertex_color || shaded) {
                    for (q = 0; q < 3; q++) {
                        (&bv[k].r)[q] = (u8)(col[q] * 255.0f + 0.5f);
                    }
                } else {
                    bv[k].r = bv[k].g = bv[k].b = 255;
                }
                bv[k].a = (u8)(mt->alpha * 255u / 31u);
                bv[k].obj = g_research_ids ? tri_id : ground == 1 ? 0u : c->obj;   /* 2: a placed model, its own number */
                if (dn != NULL && (dn->flags & 1u) && !g_research_ids && ground != 1) {
                    bv[k].obj |= 0xC0000000u;   /* research: the whole model rests on the floor (rest_bodies) */
                }
            }
            (void)shaded_all;
            {
                float e1[3], e2[3], nn[3], len;
                int level_ok = 0;

                for (k = 0; k < 3; k++) {
                    e1[k] = wp[1][k] - wp[0][k];
                    e2[k] = wp[2][k] - wp[0][k];
                }
                nn[0] = e1[1] * e2[2] - e1[2] * e2[1];
                nn[1] = e1[2] * e2[0] - e1[0] * e2[2];
                nn[2] = e1[0] * e2[1] - e1[1] * e2[0];
                len = sqrtf(nn[0] * nn[0] + nn[1] * nn[1] + nn[2] * nn[2]);
                level_ok = len > 0.0f && fabsf(nn[1]) >= 0.5f * len && dc->bb == 0 &&
                           !(ent >= 0 && c->ents[ent].cutout);
                if (add_tri(c, bv, &tag1, (const float (*)[3])wp, ground, level_ok) != 0) {
                    free(tl.v);
                    return -1;
                }
            }
        }
        free(tl.v);
        if (dn != NULL) {
            /* the completion's new triangles that take this draw command's material */
            u32 xi;

            for (xi = 0; xi < dn->ntri; xi++) {
                const DoneTri *x = &dn->tri[xi];
                BmapVertex bv[3];
                float wp[3][3], n[3], col[3];
                TriTag xt = tag;
                int k, q;

                if ((x->src >> 16) != di + 1u) {
                    continue;
                }
                xt.bb = 0;
                xt.cull = x->cull;
                xt.uvanim = 0;
                if (x->kind == 1) {
                    /* a piece that keeps the battle's own directions (a shadow that falls the way the
                     * Pokemon's shadows fall, toward the foe's side): placed about the object's place with
                     * its own axes as the battle's, as a camera-facing picture is, but never turned
                     * (billboard kind 3: bmap_place_spec scales its offsets, the renderers leave it) */
                    static const float zero[3] = { 0, 0, 0 };

                    xt.bb = 3;
                    aff_apply(base, zero, xt.pivot);
                }
                if (x->tex >= 0) {
                    xt.tex = done_tex(c, dn, x->tex);
                    xt.translucent = 0;
                }
                if (x->alpha != 0) {
                    /* a see-through piece (a shadow, as the game draws a Pokemon's: half see-through) */
                    xt.alpha = x->alpha;
                    xt.translucent = 1;
                }
                for (q = 0; q < 3; q++) {
                    n[q] = x->n[0] * base->r[0][q] + x->n[1] * base->r[1][q] + x->n[2] * base->r[2][q];
                }
                vnorm(n);
                col[0] = x->rgb[0] / 255.0f; col[1] = x->rgb[1] / 255.0f; col[2] = x->rgb[2] / 255.0f;
                if (x->lit && mt->light_mask != 0u) {
                    shade(&c->lights, mt, n, col);
                }
                for (k = 0; k < 3; k++) {
                    aff_apply(base, x->p[k], wp[k]);
                    if (x->kind == 1) {
                        for (q = 0; q < 3; q++) {
                            wp[k][q] = xt.pivot[q] + x->p[k][q];
                        }
                    }
                    bv[k].x = wp[k][0]; bv[k].y = wp[k][1]; bv[k].z = wp[k][2];
                    bv[k].u = x->uv[k][0];
                    bv[k].v = x->uv[k][1];
                    for (q = 0; q < 3; q++) {
                        (&bv[k].r)[q] = (u8)(col[q] * 255.0f + 0.5f);
                    }
                    bv[k].a = x->alpha != 0 ? (u8)(x->alpha * 255u / 31u) : 255;
                    /* (the top bit: a completion's own triangles, which the battle's search for flat
                     * cut-out cards passes over: they are a body, and there are thousands) */
                    bv[k].obj = ground == 1 ? 0u : (c->obj | 0x80000000u);
                    if ((x->kind == 2 || (dn->flags & 1u)) && ground != 1) {
                        bv[k].obj |= 0x40000000u;   /* a body that rests on the floor under it (rest_bodies) */
                    }
                }
                if (add_tri(c, bv, &xt, (const float (*)[3])wp, 0, 0) != 0) {
                    return -1;
                }
            }
        }
    }
    return 0;
}

/* The chunk's walk grid: the game's own collision tiles (a/0/0/8 plane
 * sections, from offset 8; field_g3d_tile_plane.c): u16 width, u16 height,
 * then 8 bytes a tile, the attribute word at +4 (terrain id in bits 0..15,
 * bit 16 blocked, bit 18 wild encounters: field_encount.c).  A tile open in
 * either plane of a two-plane chunk (a bridge over a path) is open. */
static int water_terrain(u32 t)
{
    return t == 0x14u || t == 0x17u || t == 0x1Cu || (t >= 0x3Du && t <= 0x3Fu) || t == 0x42u || t == 0x43u;
}

static void add_walk(Ctx *c, Buf bb, unsigned planes, int cx, int cz)
{
    WalkChunk *w = (WalkChunk *)grow(c->walk, &c->walk_cap, c->nwalk + 1u, sizeof *w);
    unsigned pl, i;

    if (w == NULL) {
        return;
    }
    c->walk = w;
    w = &c->walk[c->nwalk];
    w->cx = cx;
    w->cz = cz;
    memset(w->cls, BMAP_TILE_NONE, sizeof w->cls);
    for (pl = 0; pl < planes; pl++) {
        u32 at = rd32(bb, 8u + 4u * pl);

        if ((size_t)at + 4u + 32u * 32u * 8u > bb.n || rd16(bb, at) != 32u || rd16(bb, at + 2u) != 32u) {
            continue;
        }
        for (i = 0; i < 32u * 32u; i++) {
            u32 attr = rd32(bb, at + 8u + i * 8u) & 0x7FFFFFFFu, t = attr & 0xFFFFu;
            u8 k = water_terrain(t) ? BMAP_TILE_WATER
                 : (attr >> 16) & 1u ? BMAP_TILE_BLOCKED : BMAP_TILE_OPEN;
            u8 enc = (attr >> 16) & 4u ? BMAP_TILE_ENCOUNTER : 0u;

            if ((w->cls[i] & 3u) == BMAP_TILE_NONE || k > (w->cls[i] & 3u)) {
                w->cls[i] = (u8)((w->cls[i] & BMAP_TILE_ENCOUNTER) | k);
            }
            w->cls[i] |= enc;
        }
    }
    c->nwalk++;
}

/* Cell chunk: ground model and its placed objects. */
static int add_chunk(Ctx *c, Area *ar, unsigned chunk, int cx, int cz)
{
    Blob b;
    Buf bb;
    Model md;
    float center[3];
    Aff base;
    char setkey[24];
    Blob *gs;
    u32 bmd, from, to, a, e;
    unsigned count, k;
    int rc = 0;

    if (arc_load(c, "a/0/0/8", chunk, &b) != 0) {
        return 0;
    }
    bb = bb_of(&b);
    center[0] = (float)cx * CELL_UNITS + CELL_UNITS / 2.0f;
    center[1] = 0.0f;
    center[2] = (float)cz * CELL_UNITS + CELL_UNITS / 2.0f;
    base = aff_identity();
    memcpy(base.t, center, sizeof center);
    gs = ground_set(c, ar->texset);
    snprintf(setkey, sizeof setkey, "g%u", ar->texset);
    bmd = rd32(bb, 4);
    if (bmd < bb.n && gs != NULL && model_parse(sub(bb, bmd), &md) == 0) {
        rc = add_model(c, &md, setkey, bb_of(gs), &base, 1, NULL);
        model_free(&md);
    }
    if (rc != 0) {
        blob_free(&b);
        return rc;
    }
    if (bb.n >= 2u && bb.p[0] == 'N' && bb.p[1] == 'G') {
        from = 8; to = 12;
    } else if (bb.n >= 2u && bb.p[0] == 'G' && bb.p[1] == 'C') {
        from = 16; to = 20;
    } else {
        from = 12; to = 16;
    }
    if (!c->cur_far && from > 8u) {
        if (c->join_cur < 0) {      /* (research: a joined zone is scenery, not ground to walk on) */
            add_walk(c, bb, from == 16u ? 2u : 1u, cx, cz);
        }
    }
    a = rd32(bb, from);
    e = rd32(bb, to);
    if (e > a && e <= bb.n && ar->nb > 0u) {
        char bkey[40];

        snprintf(bkey, sizeof bkey, "b%d_%u", ar->indoor, ar->id);
        count = rd32(bb, a);
        for (k = 0; k < count && k < (e - a) / 16u; k++) {
            size_t at = a + 4u + (size_t)k * 16u;
            u32 id = rd16(bb, at + 14);
            unsigned mi;
            float t[3];
            Aff place;

            id = ((id & 0xFFu) << 8) | (id >> 8);
            for (mi = 0; mi < ar->nb && ar->bids[mi] != id; mi++) {
            }
            if (mi >= ar->nb) {
                continue;
            }
            t[0] = center[0] + (float)rds32(bb, at) / 4096.0f;
            t[1] = (float)rds32(bb, at + 4) / 4096.0f;
            t[2] = center[2] - (float)rds32(bb, at + 8) / 4096.0f;
            place = aff_yaw((float)rd16(bb, at + 12) / 65536.0f, t);
            c->obj++;
            /* (far chunks keep the picture: 40 tiles away it is a few pixels) */
            g_done = c->cur_far ? NULL : done_get(c, ar->indoor, (unsigned)id, &ar->bmodels[mi], bkey, bb_of(&ar->btex));
            if (getenv("BMAP_MODEL_LOG") != NULL && (!c->cur_far || strcmp(getenv("BMAP_MODEL_LOG"), "all") == 0)) {
                /* ("all": the far ones too: a census of what a zone places) */
                fprintf(stderr, "bmap: model b%d_%05u at %.0f %.0f y %.0f%s\n", ar->indoor, (unsigned)id, (double)t[0], (double)t[2],
                        (double)t[1], g_done != NULL ? " completed" : "");
            }
            rc = add_model(c, &ar->bmodels[mi], bkey, bb_of(&ar->btex), &place, 2,
                           &ar->banims[mi]);   /* placed */
            g_done = NULL;
            if (rc != 0) {
                break;
            }
        }
    }
    blob_free(&b);
    return rc;
}

/* ------------------------------------------------------------------ */
/* Black City and White Forest blocks                                  */
/* ------------------------------------------------------------------ */

static BmapWfbc g_wfbc;
static int g_wfbc_set;

void bmap_set_wfbc(const BmapWfbc *w)
{
    g_wfbc_set = w != NULL;
    if (w != NULL) {
        g_wfbc = *w;
    }
}

/* A whole archive member, malloc'd (NULL when missing). */
static u8 *wfbc_member(const char *path, u32 id, long *size)
{
    BwArcFile arc;
    long n;
    u8 *buf;

    *size = 0;
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
    *size = buf != NULL ? n : 0;
    return buf;
}

/* MATH_Rand32 (NitroSDK): the 64-bit linear congruence, the top word scaled
 * to max */
static u32 wfbc_rand(unsigned long long *x, u32 max)
{
    *x = 0x5D588B656C078965ULL * *x + 0x269EC3ULL;
    return max != 0u ? (u32)(((*x >> 32) * (unsigned long long)max) >> 32) : (u32)(*x >> 32);
}

int bmap_wfbc_grid(unsigned zone, const unsigned char *npc_ids, int n, unsigned player_id, BmapWfbc *out)
{
    long tsize = 0, rsize;
    u8 *table, *rec;
    u32 range = 1, id_sum = 0, rec_sum = 0;
    unsigned long long x;
    int i, gx, gz;

    if (zone != 0u && zone != 424u) {
        return -1;
    }
    /* the block table (sBlockTableMemberIds: 30 Black City, 31 White Forest):
     * an 8x8 layout of 2-byte cells (offsets, type), then u32 ids[type][16] */
    table = wfbc_member("a/1/5/4", zone == 0u ? 30u : 31u, &tsize);
    if (table == NULL || tsize < 128 + 64) {
        free(table);
        return -1;
    }
    /* FIELD_WFBC_BLOCK_InitRand */
    for (i = 0; i < n; i++) {
        rec = wfbc_member("a/1/5/4", npc_ids[i], &rsize);
        if (rec != NULL && rsize >= 0x30) {
            rec_sum += rd16((Buf){ rec, (size_t)rsize }, 0x2E);
        }
        free(rec);
        id_sum += npc_ids[i];
    }
    if (n > 0) {
        range = rec_sum + 2u;
    }
    x = player_id;
    memset(out, 0, sizeof *out);
    /* FIELD_WFBC_BLOCK_BuildGrid */
    for (gz = 0; gz < 8; gz++) {
        for (gx = 0; gx < 8; gx++) {
            u8 pos = table[(gz * 8 + gx) * 2], type = table[(gz * 8 + gx) * 2 + 1];
            int ox = pos & 15, oz = pos >> 4;

            out->type[gz][gx] = type;
            out->ox[gz][gx] = (unsigned char)ox;
            out->oz[gz][gx] = (unsigned char)oz;
            if (type == 0xFFu) {
                continue;
            }
            if (ox != 0 || oz != 0) {
                out->block[gz][gx] = out->block[gz - oz][gx - ox];
            } else {
                u32 variant = (u32)(u8)((u8)id_sum + (u8)(gx + gz)) / (u32)((gx + 3) + (gz + 1)) % 2u;
                u32 index = (u32)(n * 8) / 10u + wfbc_rand(&x, range);
                size_t at = 128u + ((size_t)type * 16u + (index >= 8u ? 7u : index) + variant * 8u) * 4u;

                out->block[gz][gx] = at + 4u <= (size_t)tsize ? (unsigned short)rd32((Buf){ table, (size_t)tsize }, at)
                                                              : 0u;
            }
        }
    }
    free(table);
    return 0;
}

int bmap_wfbc_full(unsigned zone, BmapWfbc *out)
{
    unsigned char ids[10];
    int i;

    for (i = 0; i < 10; i++) {
        ids[i] = (unsigned char)((zone == 0u ? 0 : 15) + i);
    }
    return bmap_wfbc_grid(zone, ids, 10, 0u, out);
}

/* The blocks on the grid cells of matrix cell (cx, cz): each block's walk
 * tiles over the cell's (LAND_DATA_PATCH_OverwriteAttr: a tile's word at
 * +0x14 of its 0x18 bytes), and at a block's origin its buildings
 * (LAND_DATA_PATCH_AddBuildModels: 16-byte entries as a chunk's, moved by
 * the cell's place in the chunk). */
static int add_wfbc(Ctx *c, Area *ar, const BmapWfbc *g, int cx, int cz)
{
    float center[2] = { (float)cx * CELL_UNITS + CELL_UNITS / 2.0f, (float)cz * CELL_UNITS + CELL_UNITS / 2.0f };
    WalkChunk *w = NULL;
    char bkey[40];
    unsigned i;
    int gx, gz, rc = 0;

    for (i = 0; i < c->nwalk; i++) {
        if (c->walk[i].cx == cx && c->walk[i].cz == cz) {
            w = &c->walk[i];
        }
    }
    snprintf(bkey, sizeof bkey, "b%d_%u", ar->indoor, ar->id);
    for (gz = cz * 4; gz < cz * 4 + 4 && gz < 8 && rc == 0; gz++) {
        for (gx = cx * 4; gx < cx * 4 + 4 && gx < 8 && rc == 0; gx++) {
            Blob b;
            Buf bb;
            u32 attr, bm, pw, tx, tz, k, count;
            int dx = (gx * 8) & 31, dz = (gz * 8) & 31;

            if (g->type[gz][gx] == 0xFFu || arc_load(c, "a/1/5/6", g->block[gz][gx], &b) != 0) {
                continue;
            }
            bb = bb_of(&b);
            attr = rd16(bb, 0);
            bm = rd16(bb, 2);
            pw = attr + 2u <= bb.n ? rd16(bb, attr) : 0u;
            for (tz = 0; w != NULL && pw != 0u && tz < 8u; tz++) {
                for (tx = 0; tx < 8u; tx++) {
                    size_t at = attr + 4u + ((g->ox[gz][gx] * 8u + tx) + (g->oz[gz][gx] * 8u + tz) * pw) * 0x18u + 0x14u;
                    u32 a, t;
                    u8 cls;

                    if (at + 4u > bb.n) {
                        continue;
                    }
                    a = rd32(bb, at) & 0x7FFFFFFFu;
                    t = a & 0xFFFFu;
                    cls = water_terrain(t) ? BMAP_TILE_WATER : (a >> 16) & 1u ? BMAP_TILE_BLOCKED : BMAP_TILE_OPEN;
                    w->cls[(dz + (int)tz) * 32 + dx + (int)tx] = (u8)(cls | ((a >> 16) & 4u ? BMAP_TILE_ENCOUNTER : 0u));
                }
            }
            if (g->ox[gz][gx] == 0u && g->oz[gz][gx] == 0u && bm + 4u <= bb.n && ar->nb > 0u) {
                count = rd32(bb, bm);
                for (k = 0; k < count && bm + 4u + (k + 1u) * 16u <= bb.n; k++) {
                    size_t at = bm + 4u + (size_t)k * 16u;
                    u32 id = rd16(bb, at + 14);
                    unsigned mi;
                    float t[3];
                    Aff place;

                    id = ((id & 0xFFu) << 8) | (id >> 8);
                    for (mi = 0; mi < ar->nb && ar->bids[mi] != id; mi++) {
                    }
                    if (mi >= ar->nb) {
                        continue;
                    }
                    t[0] = center[0] + (float)rds32(bb, at) / 4096.0f + (float)dx * TILE;
                    t[1] = (float)rds32(bb, at + 4) / 4096.0f;
                    t[2] = center[1] - (float)rds32(bb, at + 8) / 4096.0f + (float)dz * TILE;
                    place = aff_yaw((float)rd16(bb, at + 12) / 65536.0f, t);
                    c->obj++;
                    g_done = c->cur_far ? NULL : done_get(c, ar->indoor, (unsigned)ar->bids[mi], &ar->bmodels[mi], bkey, bb_of(&ar->btex));
                    rc = add_model(c, &ar->bmodels[mi], bkey, bb_of(&ar->btex), &place, 2, &ar->banims[mi]);
                    g_done = NULL;
                    if (rc != 0) {
                        break;
                    }
                }
            }
            blob_free(&b);
        }
    }
    return rc;
}

/* ------------------------------------------------------------------ */
/* Gym gimmick models                                                  */
/* ------------------------------------------------------------------ */

/* A joint animation (BCA0, as NNSi_G3dAnmCalcJntAnmNode reads it): the pose
 * at a whole frame, without interpolation between frames.  A key array holds
 * every frame, every second or every fourth; the frames between are the
 * mean of two keys or a 3:1 blend, up to the animation's last interpolated
 * frame, and the key after it past that. */
static void bca_pick(u32 info, unsigned frame, unsigned *k0, unsigned *k1, unsigned *w0)
{
    unsigned last = (info & 0x1FFF0000u) >> 16, rem;

    *w0 = 4u;
    if ((info & 0xC0000000u) == 0u) {
        *k0 = *k1 = frame;
    } else if (info & 0x40000000u) {
        if ((frame & 1u) == 0u) {
            *k0 = *k1 = frame >> 1;
        } else if (frame > last) {
            *k0 = *k1 = (last >> 1) + 1u;
        } else {
            *k0 = frame >> 1;
            *k1 = *k0 + 1u;
            *w0 = 2u;
        }
    } else if ((rem = frame & 3u) == 0u) {
        *k0 = *k1 = frame >> 2;
    } else if (frame > last) {
        *k0 = *k1 = (last >> 2) + rem;
    } else if (frame & 1u) {
        *k0 = (frame & 2u) ? (frame >> 2) + 1u : frame >> 2;
        *k1 = (frame & 2u) ? frame >> 2 : *k0 + 1u;
        *w0 = 3u;
    } else {
        *k0 = frame >> 2;
        *k1 = *k0 + 1u;
        *w0 = 2u;
    }
}

/* Translation or scale key `k`: s16 or fx32 by bit 29 of the info word;
 * `per` values a key (a scale key holds the scale and its inverse). */
static float bca_val(Buf a, u32 info, u32 off, unsigned k, unsigned per)
{
    return (info & 0x20000000u) ? (float)rds16(a, off + (size_t)k * per * 2u)
                                : (float)rds32(a, off + (size_t)k * per * 4u);
}

static float bca_track(Buf a, u32 info, u32 off, unsigned frame, unsigned per)
{
    unsigned k0, k1, w0;

    bca_pick(info, frame, &k0, &k1, &w0);
    return ((float)w0 * bca_val(a, info, off, k0, per) + (float)(4u - w0) * bca_val(a, info, off, k1, per)) /
           4.0f / 4096.0f;
}

/* One rotation key into a row-major 3x3.  Returns 1 when only two rows are
 * stored (the third is their cross product). */
static int bca_rot_key(Buf an, u32 rot3, u32 rot9, u32 data, float e[9])
{
    int k;

    memset(e, 0, 9u * sizeof(float));
    if (data & 0x8000u) {
        size_t at = (size_t)rot3 + (size_t)(data & 0x7FFFu) * 6u;
        unsigned head = rd16(an, at), pivot = head & 15u, pr, pc, n = 0u, q;
        float u = (float)rds16(an, at + 2u), v = (float)rds16(an, at + 4u);
        float vals[4];

        if (pivot >= 9u) {
            return 0;
        }
        pr = pivot / 3u;
        pc = pivot % 3u;
        vals[0] = u;
        vals[1] = v;
        vals[2] = (head & 0x20u) ? -v : v;
        vals[3] = (head & 0x40u) ? -u : u;
        e[pivot] = (head & 0x10u) ? -4096.0f : 4096.0f;
        for (q = 0; q < 9u; q++) {
            if (q / 3u != pr && q % 3u != pc) {
                e[q] = vals[n++];
            }
        }
        for (k = 0; k < 9; k++) {
            e[k] /= 4096.0f;
        }
        return 0;
    } else {
        size_t at = (size_t)rot9 + (size_t)(data & 0x7FFFu) * 10u;
        int q[5], packed;

        for (k = 0; k < 5; k++) {
            q[k] = rds16(an, at + (size_t)k * 2u);
        }
        for (k = 0; k < 5; k++) {
            e[k] = (float)(q[k] >> 3) / 4096.0f;
        }
        packed = q[4] & 7;
        packed = (q[0] & 7) | (packed << 3);
        packed = (q[1] & 7) | (packed << 3);
        packed = (q[2] & 7) | (packed << 3);
        packed = (q[3] & 7) | (packed << 3);
        e[5] = (float)((int)((unsigned)packed << 19) >> 19) / 4096.0f;
        return 1;
    }
}

static void rot_row_norm(float *r)
{
    float l = sqrtf(r[0] * r[0] + r[1] * r[1] + r[2] * r[2]);

    if (l > 0.0f) {
        r[0] /= l;
        r[1] /= l;
        r[2] /= l;
    }
}

static void rot_cross(float e[9])
{
    e[6] = e[1] * e[5] - e[2] * e[4];
    e[7] = e[2] * e[3] - e[0] * e[5];
    e[8] = e[0] * e[4] - e[1] * e[3];
}

/* The rotation at `frame` for a rotation track (info, off) or a constant key. */
static void bca_rot(Buf an, u32 rot3, u32 rot9, int constant, u32 info, u32 off, unsigned frame, float e[9])
{
    unsigned k0, k1, w0;
    float f[9];
    int c0, c1, k;

    if (constant) {
        if (bca_rot_key(an, rot3, rot9, info, e)) {
            rot_cross(e);
        }
        return;
    }
    bca_pick(info, frame, &k0, &k1, &w0);
    c0 = bca_rot_key(an, rot3, rot9, rd16(an, off + (size_t)k0 * 2u), e);
    if (w0 == 4u) {
        if (c0) {
            rot_cross(e);
        }
        return;
    }
    c1 = bca_rot_key(an, rot3, rot9, rd16(an, off + (size_t)k1 * 2u), f);
    for (k = 0; k < 9; k++) {
        e[k] = ((float)w0 * e[k] + (float)(4u - w0) * f[k]) / 4.0f;
    }
    rot_row_norm(e);
    rot_row_norm(e + 3);
    if (c0 || c1) {
        rot_cross(e);
    } else {
        rot_row_norm(e + 6);
    }
}

/* Poses the model's nodes from animation 0 of the BCA0 file `bca`, at
 * `frame` (-1: the last frame), and rebuilds the node world matrices.  A node
 * the animation does not list, or lists as unchanged, keeps its own value. */
static int model_pose(Model *md, Buf bca, int frame)
{
    Buf jnt, an;
    Dict d;
    unsigned nframes, nnode, k, i, pass, n;
    u32 rot3, rot9;
    Aff *loc;
    NodeSrt *srt;

    if (md->srt == NULL || md->parent == NULL || container_block(bca, "JNT0", &jnt) != 0 ||
        dict_parse(jnt, 8, &d) != 0 || d.count == 0u) {
        return -1;
    }
    an = sub(jnt, dict_off(&d, 0));
    nframes = rd16(an, 4);
    nnode = rd16(an, 6);
    rot3 = rd32(an, 12);
    rot9 = rd32(an, 16);
    if (nframes == 0u) {
        return -1;
    }
    if (frame < 0 || (unsigned)frame >= nframes) {
        frame = (int)nframes - 1;
    }
    srt = (NodeSrt *)malloc((md->nnodes ? md->nnodes : 1u) * sizeof *srt);
    loc = (Aff *)malloc((md->nnodes ? md->nnodes : 1u) * sizeof *loc);
    if (srt == NULL || loc == NULL) {
        free(srt);
        free(loc);
        return -1;
    }
    memcpy(srt, md->srt, md->nnodes * sizeof *srt);
    for (k = 0; k < nnode; k++) {
        size_t rec = rd16(an, 20u + (size_t)k * 2u), p = rec + 4u;
        u32 w = rd32(an, rec), fl = w & 0xFFFFFFu;
        unsigned node = w >> 24, ax;
        NodeSrt *s;

        if (node >= md->nnodes) {
            continue;
        }
        s = &srt[node];
        if (fl & 1u) {
            memset(s->t, 0, sizeof s->t);
            memset(s->rot, 0, sizeof s->rot);
            s->rot[0] = s->rot[4] = s->rot[8] = 1.0f;
            s->sc[0] = s->sc[1] = s->sc[2] = 1.0f;
            continue;
        }
        if ((fl & 6u) == 0u) {
            static const u32 cbit[3] = { 8u, 0x10u, 0x20u };

            for (ax = 0; ax < 3u; ax++) {
                if (fl & cbit[ax]) {
                    s->t[ax] = (float)rds32(an, p) / 4096.0f;
                    p += 4u;
                } else {
                    s->t[ax] = bca_track(an, rd32(an, p), rd32(an, p + 4u), (unsigned)frame, 1u);
                    p += 8u;
                }
            }
        } else if (fl & 2u) {
            memset(s->t, 0, sizeof s->t);
        }
        if ((fl & 0xC0u) == 0u) {
            if (fl & 0x100u) {
                bca_rot(an, rot3, rot9, 1, rd32(an, p), 0u, 0u, s->rot);
                p += 4u;
            } else {
                bca_rot(an, rot3, rot9, 0, rd32(an, p), rd32(an, p + 4u), (unsigned)frame, s->rot);
                p += 8u;
            }
        } else if (fl & 0x40u) {
            memset(s->rot, 0, sizeof s->rot);
            s->rot[0] = s->rot[4] = s->rot[8] = 1.0f;
        }
        if ((fl & 0x600u) == 0u) {
            static const u32 sbit[3] = { 0x800u, 0x1000u, 0x2000u };

            for (ax = 0; ax < 3u; ax++) {
                size_t q = p + (size_t)ax * 8u;

                s->sc[ax] = (fl & sbit[ax]) ? (float)rds32(an, q) / 4096.0f
                                            : bca_track(an, rd32(an, q), rd32(an, q + 4u), (unsigned)frame, 2u);
            }
        } else if (fl & 0x200u) {
            s->sc[0] = s->sc[1] = s->sc[2] = 1.0f;
        }
    }
    for (i = 0; i < md->nnodes; i++) {
        Aff r = aff_identity();
        int a, b;

        for (a = 0; a < 3; a++) {
            for (b = 0; b < 3; b++) {
                r.r[a][b] = srt[i].rot[a * 3 + b] * srt[i].sc[a];
            }
        }
        memcpy(r.t, srt[i].t, sizeof r.t);
        loc[i] = r;
        md->world[i] = r;
    }
    n = md->nnodes < 8u ? md->nnodes : 8u;
    for (pass = 0; pass < n; pass++) {
        for (i = 0; i < md->nnodes; i++) {
            if (md->parent[i] >= 0) {
                md->world[i] = aff_then(&loc[i], &md->world[md->parent[i]]);
            }
        }
    }
    free(srt);
    free(loc);
    return 0;
}

/* The models a gym's field gimmick adds on top of the map (bmap_gimmick.c:
 * curtains, doors, ...), in their initial state.  Each is a BMD0 from the
 * gimmick's archive, placed at its world position like a building. */
/* The field's 3D objects as they were when the battle began (bmap_live_set). */
#define LIVE_MAX 160
static BmapLive g_live[LIVE_MAX];
static unsigned g_live_n, g_live_zone = 0xFFFFFFFFu;

void bmap_live_set(unsigned zone, const BmapLive *list, unsigned n)
{
    n = n > LIVE_MAX ? LIVE_MAX : n;
    if (n > 0) {
        memcpy(g_live, list, n * sizeof *list);
    }
    g_live_zone = zone;
    g_live_n = n;
}

static int live_has(unsigned zone)
{
    return g_live_zone == zone && g_live_n > 0u;
}

/* The name of a model file's first model ("" when it has none). */
static void live_model_name(Buf f, char out[20])
{
    Buf mdl;
    Dict d;

    out[0] = '\0';
    if (container_block(f, "MDL0", &mdl) == 0 && dict_parse(mdl, 8, &d) == 0 && d.count != 0u) {
        char nm[17];

        dict_name(&d, 0, nm);
        memcpy(out, nm, 17);
    }
}

/* Models made for the field's steep camera, by name.  Such a model is not over its place: it lies to the north
 * of it and low, and the field's camera shows it over its place, hovering.  The battle's camera is low and
 * turns, so the model goes where the field's camera shows it (over its own place, up by its offset times the
 * tangent of that camera's pitch), and rises and falls as its first animation moves it.
 *   d6_ishi_big01   Chargestone Cave's floating stone (aletheia ov021 fldeff_d06denki.c) */
static int live_field_camera_model(const char *name)
{
    return strcmp(name, "d6_ishi_big01") == 0;
}

/* They stand in the battle's map where the field drew them. */
static int add_live(Ctx *c, unsigned zone, float px, float pz)
{
    static BmapLive live[LIVE_MAX];
    unsigned n = live_has(zone) ? g_live_n : 0u, i, shown = 0;

    memcpy(live, g_live, sizeof live);
    for (i = 0; i < n; i++) {
        Buf mb = { live[i].mdl, live[i].mdl_n }, tb = { live[i].tex, live[i].tex_n };
        Model md;
        char setkey[40], name[20];
        Aff place = aff_identity();
        int rc, fieldcam;
        unsigned a;
        size_t v0, k;

        if (mb.p == NULL || model_parse(mb, &md) != 0) {
            continue;
        }
        live_model_name(mb, name);
        fieldcam = live_field_camera_model(name) && live[i].field_tan > 0.0f;
        if (fieldcam && c->bob_n == 0u && live[i].nanim > 0u && live[i].anim[0].n >= 4u && memcmp(live[i].anim[0].p, "BCA0", 4) == 0) {
            /* its first animation's rise, a value a frame: the model is posed at each frame and built aside;
             * how much higher its lowest corner lies than at frame 0 is that frame's rise */
            Buf ab = { live[i].anim[0].p, live[i].anim[0].n }, jnt;
            unsigned nf = 0, f;
            float y0 = 0.0f;

            if (container_block(ab, "JNT0", &jnt) == 0) {
                Dict dj;

                if (dict_parse(jnt, 8, &dj) == 0 && dj.count != 0u) {
                    nf = rd16(sub(jnt, dict_off(&dj, 0)), 4);
                }
            }
            nf = nf > BMAP_BOB_MAX ? BMAP_BOB_MAX : nf;
            for (f = 0; f < nf; f++) {
                size_t sv = c->nv, st = c->ntag, sg = c->ngt;
                float lo = 1e30f;
                Aff at = aff_identity();

                model_pose(&md, ab, (int)f);
                if (add_model(c, &md, "Lbob", tb.p != NULL ? tb : mb, &at, 0, NULL) != 0) {
                    break;
                }
                for (k = sv; k < c->nv; k++) {
                    lo = fminf(lo, c->v[k].y);
                }
                c->nv = sv;
                c->ntag = st;
                c->ngt = sg;
                if (f == 0u) {
                    y0 = lo;
                }
                c->bob_y[f] = lo - y0;
            }
            c->bob_n = f;
        }
        /* the pose: each joint animation at the frame the field had it on (a model that rises and falls by
         * itself here starts from its animation's first frame) */
        for (a = 0; a < live[i].nanim; a++) {
            Buf ab = { live[i].anim[a].p, live[i].anim[a].n };

            if (ab.p != NULL && ab.n >= 4u && memcmp(ab.p, "BCA0", 4) == 0) {
                model_pose(&md, ab, fieldcam && a == 0u ? 0 : live[i].anim[a].frame);
            }
        }
        memcpy(place.t, live[i].t, sizeof place.t);
        memcpy(place.r, live[i].r, sizeof place.r);
        snprintf(setkey, sizeof setkey, "L%p", (const void *)(tb.p != NULL ? tb.p : mb.p));
        c->cur_far = (u8)(fabsf(live[i].t[0] - px) > NEAR_REACH || fabsf(live[i].t[2] - pz) > NEAR_REACH);
        c->obj++;
        v0 = c->nv;
        c->cur_bob = (u8)(fieldcam && c->bob_n > 1u);
        rc = add_model(c, &md, setkey, tb.p != NULL ? tb : mb, &place, 2, NULL);
        c->cur_bob = 0;
        if (rc == 0 && fieldcam && c->nv > v0) {
            float z0 = 1e30f, z1 = -1e30f, d;

            for (k = v0; k < c->nv; k++) {
                z0 = fminf(z0, c->v[k].z);
                z1 = fmaxf(z1, c->v[k].z);
            }
            d = live[i].t[2] - 0.5f * (z0 + z1);
            for (k = v0; k < c->nv; k++) {
                c->v[k].z += d;
                c->v[k].y += d * live[i].field_tan;
            }
        }
        model_free(&md);
        if (rc != 0) {
            return rc;
        }
        shown++;
        if (getenv("BMAP_PLACE_LOG") != NULL) {
            fprintf(stderr, "bmap: live %s at %.0f %.0f %.0f, %u animations (first at frame %d)%s\n", name, (double)live[i].t[0],
                    (double)live[i].t[1], (double)live[i].t[2], live[i].nanim, live[i].nanim ? live[i].anim[0].frame : -1,
                    fieldcam ? ", made for the field's camera" : "");
        }
    }
    if (getenv("BMAP_PLACE_LOG") != NULL) {
        fprintf(stderr, "bmap: live objects: %u of %u shown in zone %u (noted for zone %u)\n", shown, n, zone, g_live_zone);
    }
    return 0;
}

static int add_gimmicks(Ctx *c, unsigned zone, float px, float pz)
{
    const BmapGimmickModel *gm;
    unsigned n = bmap_gimmick_models(zone, &gm), i;

    for (i = 0; i < n; i++) {
        Blob mb, tb, ab;
        Model md;
        char setkey[40];
        float t[3];
        Aff place;
        int rc;

        memset(&tb, 0, sizeof tb);
        if (arc_load(c, gm[i].arc, gm[i].model, &mb) != 0) {
            continue;
        }
        if (model_parse(bb_of(&mb), &md) != 0) {
            blob_free(&mb);
            continue;
        }
        if (gm[i].tex >= 0) {
            arc_load(c, gm[i].arc, (u32)gm[i].tex, &tb);
        }
        if (gm[i].anim >= 0 && arc_load(c, gm[i].arc, (u32)gm[i].anim, &ab) == 0) {
            model_pose(&md, bb_of(&ab), gm[i].frame);
            blob_free(&ab);
        }
        snprintf(setkey, sizeof setkey, "m%s_%d", gm[i].arc, gm[i].tex >= 0 ? gm[i].tex : (int)gm[i].model);
        t[0] = gm[i].x;
        t[1] = gm[i].y;
        t[2] = gm[i].z;
        place = aff_yaw(gm[i].turns, t);
        c->cur_far = (u8)(fabsf(gm[i].x - px) > NEAR_REACH || fabsf(gm[i].z - pz) > NEAR_REACH);
        c->obj++;
        /* The textures are the model's own unless a separate set is named. */
        rc = add_model(c, &md, setkey, bb_of(tb.p != NULL ? &tb : &mb), &place, 2, NULL);   /* placed */
        model_free(&md);
        blob_free(&tb);
        blob_free(&mb);
        if (rc != 0) {
            return rc;
        }
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Ground heights                                                      */
/* ------------------------------------------------------------------ */

typedef struct {
    GTri *tri;
    unsigned ntri;
    int gx0, gz0, gw, gh;
    unsigned *start, *idx;
    float fallback;
    int has_ref;
    float ref;                  /* bmap_set_ref: the player's height        */
    int wx0, wz0, ww, wh;       /* the walk grid, in tiles                   */
    u8 *walk;
} Priv;

/* Water over the ground: a Pokemon stands on the water, never under it.  A
 * pond's surface is a see-through sheet over a solid one well below it
 * (the plane the water's sky reflection is drawn on: 48 units under
 * Striaton's, 120 under Icirrus's). */
#define WATER_MAX 160.0f

static float ground_at(const Priv *g, float x, float z, int *hit)
{
    int cx = (int)floorf(x / TILE) - g->gx0, cz = (int)floorf(z / TILE) - g->gz0;
    float lowest = 1e30f, found[64], wet[16], best;
    unsigned nf = 0, nw = 0, i, j;

    *hit = 0;
    if (cx < 0 || cz < 0 || cx >= g->gw || cz >= g->gh) {
        return 0.0f;
    }
    j = (unsigned)(cz * g->gw + cx);
    for (i = g->start[j]; i < g->start[j + 1u]; i++) {
        const GTri *t = &g->tri[g->idx[i]];
        const float *a = t->p[0], *b = t->p[1], *cc = t->p[2];
        float area = (b[0] - a[0]) * (cc[2] - a[2]) - (b[2] - a[2]) * (cc[0] - a[0]);
        float w0, w1, w2;

        if (area == 0.0f || (t->placed && !g->has_ref)) {
            continue;                   /* a table top is no floor without a height to go by */
        }
        w0 = ((b[0] - x) * (cc[2] - z) - (b[2] - z) * (cc[0] - x)) / area;
        w1 = ((cc[0] - x) * (a[2] - z) - (cc[2] - z) * (a[0] - x)) / area;
        w2 = 1.0f - w0 - w1;
        if (w0 >= -1e-4f && w1 >= -1e-4f && w2 >= -1e-4f && nf < 64u) {
            float h = w0 * a[1] + w1 * b[1] + w2 * cc[1];

            if (t->water) {
                if (nw < 16u) {
                    wet[nw++] = h;
                }
                continue;
            }
            found[nf++] = h;
            if (h < lowest) {
                lowest = h;
            }
        }
    }
    best = -1e30f;
    if (g->has_ref) {
        /* the surface nearest the player's own height: a room's floor, not
         * the wall tops or a walkway above it */
        float bd = 1e30f;

        for (i = 0; i < nf; i++) {
            float dd = fabsf(found[i] - g->ref);

            if (dd < bd - 0.01f || (dd < bd + 0.01f && found[i] > best)) {
                bd = dd;
                best = found[i];
            }
        }
    } else {
        for (i = 0; i < nf; i++) {
            if (found[i] <= lowest + MAX_STEP && found[i] > best) {
                best = found[i];
            }
        }
    }
    if (nf == 0u && nw == 0u) {
        return 0.0f;
    }
    {
        /* the water over it, the highest within WATER_MAX (or the water
         * alone, nothing solid under it in the map) */
        float top = nf > 0u ? best : -1e30f;

        for (i = 0; i < nw; i++) {
            if (wet[i] > top && (nf == 0u || wet[i] - best <= WATER_MAX)) {
                top = wet[i];
            }
        }
        best = top;
    }
    *hit = 1;
    return best;
}

static void build_walk(Ctx *c, Priv *g)
{
    int x0 = 1 << 30, z0 = 1 << 30, x1 = -(1 << 30), z1 = -(1 << 30);
    size_t i;

    for (i = 0; i < c->nwalk; i++) {
        x0 = c->walk[i].cx < x0 ? c->walk[i].cx : x0;
        z0 = c->walk[i].cz < z0 ? c->walk[i].cz : z0;
        x1 = c->walk[i].cx > x1 ? c->walk[i].cx : x1;
        z1 = c->walk[i].cz > z1 ? c->walk[i].cz : z1;
    }
    if (c->nwalk == 0u) {
        return;
    }
    g->wx0 = x0 * 32;
    g->wz0 = z0 * 32;
    g->ww = (x1 - x0 + 1) * 32;
    g->wh = (z1 - z0 + 1) * 32;
    g->walk = (u8 *)calloc((size_t)g->ww * (size_t)g->wh, 1);    /* BMAP_TILE_NONE */
    if (g->walk == NULL) {
        return;
    }
    for (i = 0; i < c->nwalk; i++) {
        const WalkChunk *w = &c->walk[i];
        int r;

        for (r = 0; r < 32; r++) {
            memcpy(&g->walk[(size_t)((w->cz - z0) * 32 + r) * (size_t)g->ww + (size_t)((w->cx - x0) * 32)],
                   &w->cls[r * 32], 32);
        }
    }
}

static void build_ground(Ctx *c, BmapScene *s, float px, float pz)
{
    Priv *g = (Priv *)calloc(1, sizeof *g);
    float mnx = 1e30f, mnz = 1e30f, mxx = -1e30f, mxz = -1e30f;
    unsigned i, k, cells;
    unsigned *fill;
    int hit;

    if (g == NULL) {
        return;
    }
    s->priv = g;
    g->tri = c->gt;                      /* ownership moves to the scene */
    g->ntri = (unsigned)c->ngt;
    c->gt = NULL;
    for (i = 0; i < g->ntri; i++) {
        for (k = 0; k < 3u; k++) {
            const float *p = g->tri[i].p[k];

            mnx = p[0] < mnx ? p[0] : mnx;
            mxx = p[0] > mxx ? p[0] : mxx;
            mnz = p[2] < mnz ? p[2] : mnz;
            mxz = p[2] > mxz ? p[2] : mxz;
        }
    }
    if (g->ntri == 0u) {
        return;
    }
    g->gx0 = (int)floorf(mnx / TILE);
    g->gz0 = (int)floorf(mnz / TILE);
    g->gw = (int)floorf(mxx / TILE) - g->gx0 + 1;
    g->gh = (int)floorf(mxz / TILE) - g->gz0 + 1;
    cells = (unsigned)(g->gw * g->gh);
    g->start = (unsigned *)calloc(cells + 1u, sizeof(unsigned));
    fill = (unsigned *)calloc(cells + 1u, sizeof(unsigned));
    if (g->start == NULL || fill == NULL) {
        free(fill);
        free(g->start);
        g->start = NULL;
        g->ntri = 0;
        return;
    }
    for (k = 0; k < 2u; k++) {
        unsigned total = 0;

        if (k == 1u) {
            unsigned acc = 0;

            for (i = 0; i < cells; i++) {
                unsigned n = g->start[i];

                g->start[i] = acc;
                acc += n;
            }
            g->start[cells] = acc;
            g->idx = (unsigned *)malloc((acc ? acc : 1u) * sizeof(unsigned));
            if (g->idx == NULL) {
                free(fill);
                free(g->start);
                g->start = NULL;
                g->ntri = 0;
                return;
            }
        }
        for (i = 0; i < g->ntri; i++) {
            const GTri *t = &g->tri[i];
            float lx = 1e30f, hx = -1e30f, lz = 1e30f, hz = -1e30f;
            int x0, x1, z0, z1, x, z;

            for (total = 0; total < 3u; total++) {
                lx = t->p[total][0] < lx ? t->p[total][0] : lx;
                hx = t->p[total][0] > hx ? t->p[total][0] : hx;
                lz = t->p[total][2] < lz ? t->p[total][2] : lz;
                hz = t->p[total][2] > hz ? t->p[total][2] : hz;
            }
            x0 = (int)floorf(lx / TILE) - g->gx0;
            x1 = (int)floorf(hx / TILE) - g->gx0;
            z0 = (int)floorf(lz / TILE) - g->gz0;
            z1 = (int)floorf(hz / TILE) - g->gz0;
            for (z = z0; z <= z1; z++) {
                for (x = x0; x <= x1; x++) {
                    unsigned j = (unsigned)(z * g->gw + x);

                    if (k == 0u) {
                        g->start[j]++;
                    } else {
                        g->idx[g->start[j] + fill[j]++] = i;
                    }
                }
            }
        }
    }
    free(fill);
    g->fallback = ground_at(g, px, pz, &hit);
    build_walk(c, g);
}

float bmap_ground_y(const BmapScene *s, float x, float z)
{
    const Priv *g = (const Priv *)s->priv;
    int hit;
    float h;

    if (g == NULL || g->start == NULL) {
        return 0.0f;
    }
    h = ground_at(g, x, z, &hit);
    return hit ? h : g->fallback;
}

void bmap_set_ref(BmapScene *s, float y)
{
    Priv *g = (Priv *)s->priv;

    if (g != NULL) {
        g->has_ref = 1;
        g->ref = y;
    }
}

float bmap_ground_below(const BmapScene *s, float x, float z, float y, int *hit)
{
    const Priv *g = (const Priv *)s->priv;
    float best = -1e30f;
    int cx, cz;
    unsigned i, j;

    *hit = 0;
    if (g == NULL || g->start == NULL) {
        return 0.0f;
    }
    cx = (int)floorf(x / TILE) - g->gx0;
    cz = (int)floorf(z / TILE) - g->gz0;
    if (cx < 0 || cz < 0 || cx >= g->gw || cz >= g->gh) {
        return 0.0f;
    }
    j = (unsigned)(cz * g->gw + cx);
    for (i = g->start[j]; i < g->start[j + 1u]; i++) {
        const GTri *t = &g->tri[g->idx[i]];
        const float *a = t->p[0], *b = t->p[1], *cc = t->p[2];
        float area = (b[0] - a[0]) * (cc[2] - a[2]) - (b[2] - a[2]) * (cc[0] - a[0]);
        float w0, w1, w2, h;

        if (area == 0.0f || t->water || t->placed) {
            continue;
        }
        w0 = ((b[0] - x) * (cc[2] - z) - (b[2] - z) * (cc[0] - x)) / area;
        w1 = ((cc[0] - x) * (a[2] - z) - (cc[2] - z) * (a[0] - x)) / area;
        w2 = 1.0f - w0 - w1;
        if (w0 < -1e-4f || w1 < -1e-4f || w2 < -1e-4f) {
            continue;
        }
        h = w0 * a[1] + w1 * b[1] + w2 * cc[1];
        if (h <= y + 0.5f && h > best) {
            best = h;
        }
    }
    if (best < -1e29f) {
        return 0.0f;
    }
    *hit = 1;
    return best;
}

void bmap_clear_ref(BmapScene *s)
{
    Priv *g = (Priv *)s->priv;

    if (g != NULL) {
        g->has_ref = 0;
    }
}

int bmap_tile(const BmapScene *s, float x, float z)
{
    const Priv *g = (const Priv *)s->priv;
    int tx, tz;

    if (g == NULL || g->walk == NULL) {
        return BMAP_TILE_NONE;
    }
    tx = (int)floorf(x / TILE) - g->wx0;
    tz = (int)floorf(z / TILE) - g->wz0;
    if (tx < 0 || tz < 0 || tx >= g->ww || tz >= g->wh) {
        return BMAP_TILE_NONE;
    }
    return g->walk[(size_t)tz * (size_t)g->ww + (size_t)tx] & 3;
}

int bmap_tile_encounter(const BmapScene *s, float x, float z)
{
    const Priv *g = (const Priv *)s->priv;
    int tx, tz;

    if (g == NULL || g->walk == NULL) {
        return 0;
    }
    tx = (int)floorf(x / TILE) - g->wx0;
    tz = (int)floorf(z / TILE) - g->wz0;
    if (tx < 0 || tz < 0 || tx >= g->ww || tz >= g->wh) {
        return 0;
    }
    return (g->walk[(size_t)tz * (size_t)g->ww + (size_t)tx] & BMAP_TILE_ENCOUNTER) != 0;
}

/* ------------------------------------------------------------------ */
/* Build                                                               */
/* ------------------------------------------------------------------ */

static const TriTag *g_sort_tags;

/* Orders by draw key; 0 when two triangles belong to the same draw. */
static int key_cmp(const TriTag *x, const TriTag *y)
{
    if (x->translucent != y->translucent) return (int)x->translucent - (int)y->translucent;
    if (x->tex != y->tex) return x->tex < y->tex ? -1 : 1;
    if (x->alpha != y->alpha) return (int)x->alpha - (int)y->alpha;
    if (x->cull != y->cull) return (int)x->cull - (int)y->cull;
    if (x->far != y->far) return (int)x->far - (int)y->far;
    if (x->bb != y->bb) return x->bb - y->bb;
    if (x->uvanim != y->uvanim) return (int)x->uvanim - (int)y->uvanim;
    if (x->bob != y->bob) return (int)x->bob - (int)y->bob;
    if (x->bb) {
        int k;

        for (k = 0; k < 3; k++) {
            if (x->pivot[k] != y->pivot[k]) return x->pivot[k] < y->pivot[k] ? -1 : 1;
        }
    }
    return 0;
}

static int cmp_tri(const void *pa, const void *pb)
{
    unsigned a = *(const unsigned *)pa, b = *(const unsigned *)pb;
    int r = key_cmp(&g_sort_tags[a], &g_sort_tags[b]);

    return r ? r : a < b ? -1 : a > b ? 1 : 0;
}

static int finish(Ctx *c, BmapScene *s)
{
    size_t nt = c->ntag, i;
    unsigned *order = (unsigned *)malloc((nt ? nt : 1u) * sizeof(unsigned));
    BmapVertex *out;

    if (order == NULL) {
        return -1;
    }
    for (i = 0; i < nt; i++) {
        order[i] = (unsigned)i;
    }
    g_sort_tags = c->tag;
    qsort(order, nt, sizeof(unsigned), cmp_tri);
    out = (BmapVertex *)malloc((nt ? nt * 3u : 1u) * sizeof *out);
    s->d = (BmapDraw *)calloc(nt ? nt : 1u, sizeof(BmapDraw));
    if (out == NULL || s->d == NULL) {
        free(out);
        free(order);
        return -1;
    }
    s->nd = 0;
    for (i = 0; i < nt; i++) {
        const TriTag *t = &c->tag[order[i]];
        BmapDraw *d = s->nd ? &s->d[s->nd - 1u] : NULL;
        int same = d != NULL && i > 0 && key_cmp(&c->tag[order[i - 1u]], t) != 0;

        memcpy(&out[i * 3u], &c->v[(size_t)order[i] * 3u], 3u * sizeof *out);
        if (d == NULL || same) {
            d = &s->d[s->nd++];
            d->tex = t->tex;
            d->first = (unsigned)(i * 3u);
            d->count = 0;
            d->alpha = t->alpha;
            d->cull = t->cull;
            d->translucent = t->translucent;
            d->billboard = (unsigned char)t->bb;
            d->far = t->far;
            d->uvanim = t->uvanim;
            d->bob = t->bob;
            memcpy(d->pivot, t->pivot, sizeof d->pivot);
        }
        d->count += 3u;
    }
    free(order);
    free(c->v);
    c->v = NULL;
    s->v = out;
    s->nv = (unsigned)(nt * 3u);
    return 0;
}

static int u32_cmp(const void *a, const void *b)
{
    u32 x = *(const u32 *)a, y = *(const u32 *)b;

    return x < y ? -1 : x > y;
}

/*
 * Builds model `md`'s remade form into `d` (bmap_remade.h).  The model is
 * made as the map makes it, standing alone at its own place under the fixed
 * light, each triangle named ((draw command + 1) << 16) | its place in the
 * command's list; the builder gets its draws and its textures and says which
 * triangles it leaves out and which it adds.  Returns 1 when the builder
 * changed the model.  Research: BMAP_REMADE_DUMP=dir writes what it made as
 * dir/<name>.bmc (the record a comparison with an earlier build reads).
 */
static int remade_make(Done *d, const BmapRemade *e, Model *md, const char *setkey, Buf set)
{
    Ctx *c = (Ctx *)calloc(1, sizeof *c);
    const Done *keep = g_done;
    Aff base = aff_identity();
    BmapScene t;
    RmBatch *bt = NULL;
    RmTri *tri = NULL;
    RmTex *tx = NULL;
    RmOut o;
    unsigned i, j, nb = 0;
    int rc, ok = 0;

    memset(&t, 0, sizeof t);
    memset(&o, 0, sizeof o);
    if (c == NULL) {
        return 0;
    }
    lights_fixed(&c->lights);
    c->obj = 1;
    g_done = NULL;
    g_research_ids = 1;
    rc = add_model(c, md, setkey, set, &base, 2, NULL);
    g_research_ids = 0;
    g_done = keep;
    if (rc != 0 || c->ntag == 0 || finish(c, &t) != 0) {
        goto out;
    }
    bt = (RmBatch *)calloc(t.nd ? t.nd : 1u, sizeof *bt);
    tri = (RmTri *)calloc(t.nv / 3u + 1u, sizeof *tri);
    tx = (RmTex *)calloc(c->ntex ? c->ntex : 1u, sizeof *tx);
    if (bt == NULL || tri == NULL || tx == NULL) {
        goto out;
    }
    for (i = 0; i < c->ntex; i++) {
        tx[i].w = c->tex[i].w;
        tx[i].h = c->tex[i].h;
        tx[i].rgba = c->tex[i].rgba;
    }
    for (i = 0; i < t.nd; i++) {
        const BmapDraw *dr = &t.d[i];
        unsigned n = dr->count / 3u * 3u;
        double sum[3] = { 0.0, 0.0, 0.0 };
        RmBatch *x;

        if (n == 0 || dr->first + n > t.nv) {
            continue;
        }
        x = &bt[nb++];
        x->tri = &tri[dr->first / 3u];
        x->n = n / 3u;
        x->tex = dr->tex;
        x->cull = dr->cull;
        x->bb = dr->billboard;
        memcpy(x->pivot, dr->pivot, sizeof x->pivot);
        for (j = 0; j < n; j++) {
            const BmapVertex *v = &t.v[dr->first + j];
            RmTri *q = &tri[(dr->first + j) / 3u];
            unsigned k = j % 3u;

            q->p[k][0] = v->x; q->p[k][1] = v->y; q->p[k][2] = v->z;
            q->uv[k][0] = v->u; q->uv[k][1] = v->v;
            if (k == 0) {
                q->id = v->obj;
            }
            if (k == 2) {
                q->rgb[0] = (u8)(((unsigned)v[-2].r + v[-1].r + v->r) / 3u);
                q->rgb[1] = (u8)(((unsigned)v[-2].g + v[-1].g + v->g) / 3u);
                q->rgb[2] = (u8)(((unsigned)v[-2].b + v[-1].b + v->b) / 3u);
            }
            sum[0] += v->r; sum[1] += v->g; sum[2] += v->b;
        }
        for (j = 0; j < 3; j++) {
            x->rgb[j] = (u8)(sum[j] / (double)n);
        }
    }
    e->build(bt, nb, tx, (unsigned)c->ntex, e->opt, &o);
    if (o.fail || (o.ntri == 0 && o.ndrop == 0 && o.ntwo == 0 && o.nturn == 0 && o.flags == 0)) {
        goto out;
    }
    qsort(o.drop, o.ndrop, sizeof *o.drop, u32_cmp);
    qsort(o.two, o.ntwo, sizeof *o.two, u32_cmp);
    qsort(o.turn, o.nturn, sizeof *o.turn, u32_cmp);      /* (by the triangle's name, its first member) */
    d->flags = o.flags;
    d->ndrop = o.ndrop;
    d->drop = o.drop != NULL ? o.drop : (u32 *)calloc(1, sizeof(u32));
    d->ntri = o.ntri;
    d->tri = o.tri != NULL ? o.tri : (DoneTri *)calloc(1, sizeof(DoneTri));
    d->ntwo = o.ntwo;
    d->two = o.two != NULL ? o.two : (u32 *)calloc(1, sizeof(u32));
    d->nturn = o.nturn;
    d->turn = o.turn != NULL ? o.turn : (DoneTurn *)calloc(1, sizeof(DoneTurn));
    d->ntex = o.ntex;
    for (i = 0; i < (unsigned)o.ntex; i++) {
        d->tex[i].w = o.tex[i].w;
        d->tex[i].h = o.tex[i].h;
        d->tex[i].rgba = o.tex[i].rgba;
    }
    memset(&o, 0, sizeof o);            /* (its lists are the record's now) */
    ok = d->drop != NULL && d->tri != NULL && d->two != NULL && d->turn != NULL;
    if (ok && getenv("BMAP_REMADE_DUMP") != NULL) {
        char path[600];
        FILE *f;

        snprintf(path, sizeof path, "%s/%s.bmc", getenv("BMAP_REMADE_DUMP"), d->key);
        f = fopen(path, "wb");
        if (f != NULL) {
            u32 n5[5] = { (u32)d->ntex, d->ntwo, d->ndrop, d->nturn, d->ntri };

            fwrite("BMC2", 1, 4, f);
            fwrite(&d->flags, 4, 1, f);
            fwrite(n5, 4, 5, f);
            for (i = 0; i < (unsigned)d->ntex; i++) {
                int wh[2] = { d->tex[i].w, d->tex[i].h };
                u8 fl[4] = { 0, 0, 0, 0 };

                fwrite(wh, 4, 2, f);
                fwrite(fl, 1, 4, f);
                fwrite(d->tex[i].rgba, 4, (size_t)wh[0] * (size_t)wh[1], f);
            }
            fwrite(d->two, 4, d->ntwo, f);
            fwrite(d->drop, 4, d->ndrop, f);
            fwrite(d->turn, sizeof(DoneTurn), d->nturn, f);
            for (i = 0; i < d->ntri; i++) {
                const DoneTri *x = &d->tri[i];
                u8 f8[8] = { x->cull, x->lit, x->rgb[0], x->rgb[1], x->rgb[2], x->kind, x->alpha, 0 };

                fwrite(&x->src, 4, 1, f);
                fwrite(&x->tex, 4, 1, f);
                fwrite(f8, 1, 8, f);
                fwrite(x->p, 4, 9, f);
                fwrite(x->uv, 4, 6, f);
                fwrite(x->n, 4, 3, f);
            }
            fclose(f);
        }
    }
out:
    free(o.drop);
    free(o.two);
    free(o.turn);
    free(o.tri);
    for (i = 0; i < 8u; i++) {
        free(o.tex[i].rgba);
    }
    free(bt);
    free(tri);
    free(tx);
    free(t.v);
    free(t.d);
    for (i = 0; i < c->nents; i++) {
        free(c->ents[i].rgba);
    }
    if (c->tex != NULL) {
        for (i = 0; i < c->ntex; i++) {
            free(c->tex[i].rgba);
        }
    }
    for (i = 0; i < c->narcs; i++) {
        bw_arc_close(&c->arcs[i].f);
    }
    free(c->ents);
    free(c->tex);
    free(c->texrefs);
    free(c->v);
    free(c->tag);
    free(c->gt);
    free(c->walk);
    for (i = 0; i < c->nuva; i++) {
        free(c->uva[i].m);
    }
    free(c->uva);
    free((void *)c->uva_key);
    free(c);
    return ok;
}

/* Research (BMAP_MODELS_DUMP=dir): every building model of the game, each
 * once, as the map builder makes it (its own place and facing, the fixed
 * light): the area packs of the open air (a/2/3/0, textures a/1/7/7) and of
 * the insides (a/2/2/9, a/1/7/6).  One file a model, b<inside>_<id>.bmd, in
 * the scene dump's format (bmap_place.c scene_dump), and a line a model in
 * models.tsv (inside, id, the first pack that holds it, how many packs do). */
static void research_models(const char *dir)
{
    int indoor;
    char path[600];
    FILE *list;

    snprintf(path, sizeof path, "%s/models.tsv", dir);
    list = fopen(path, "w");
    for (indoor = 0; indoor < 2; indoor++) {
        const char *arc = indoor ? "a/2/2/9" : "a/2/3/0", *tarc = indoor ? "a/1/7/6" : "a/1/7/7";
        Ctx *c0 = (Ctx *)calloc(1, sizeof *c0);
        ArcSlot *slot = c0 != NULL ? arc_get(c0, arc) : NULL;
        static unsigned short packs[65536];
        static unsigned short first[65536];
        unsigned member, count = slot != NULL ? slot->f.count : 0u, id;

        memset(packs, 0, sizeof packs);
        for (member = 0; member < count; member++) {
            Blob bfile, btex;
            Buf m;
            unsigned n, k;

            memset(&bfile, 0, sizeof bfile);
            memset(&btex, 0, sizeof btex);
            if (arc_load(c0, arc, member, &bfile) != 0) {
                continue;
            }
            arc_load(c0, tarc, member, &btex);
            m = bb_of(&bfile);
            n = m.n > 2u ? m.p[2] / 2u : 0u;
            for (k = 0; k < n; k++) {
                u32 def = rd32(m, 4u + k * 4u), file = rd32(m, 4u + (n + k) * 4u);
                Model md;
                Ctx *c;
                BmapScene t;
                Aff base = aff_identity();
                char key[32];
                FILE *f;
                unsigned i;

                if (file >= m.n || def + 2u > m.n) {
                    continue;
                }
                id = rd16(m, def);
                if (packs[id]++ != 0) {
                    continue;                       /* dumped from an earlier pack */
                }
                first[id] = (unsigned short)member;
                if (model_parse(sub(m, file), &md) != 0) {
                    continue;
                }
                c = (Ctx *)calloc(1, sizeof *c);
                if (c == NULL) {
                    continue;
                }
                lights_fixed(&c->lights);
                c->obj = 1;
                memset(&t, 0, sizeof t);
                snprintf(key, sizeof key, "r%d_%u", indoor, member);
                g_research_ids = 1;
                if (add_model(c, &md, key, bb_of(&btex), &base, 2, NULL) != 0 || c->ntag == 0 || finish(c, &t) != 0) {
                    g_research_ids = 0;
                    continue;
                }
                g_research_ids = 0;
                snprintf(path, sizeof path, "%s/b%d_%05u.bmd", dir, indoor, id);
                f = fopen(path, "wb");
                if (f != NULL) {
                    float hdr[6] = { 0 }, fog[2] = { 0 };
                    unsigned nt = (unsigned)c->ntex;
                    unsigned char pad[8] = { 0 };
                    int t0[2] = { 0, 0 };
                    static unsigned char grid[128 * 128];

                    fwrite("BMD1", 1, 4, f);
                    fwrite(hdr, sizeof(float), 6, f);
                    fwrite(&t.nv, 4, 1, f);
                    fwrite(&t.nd, 4, 1, f);
                    fwrite(&nt, 4, 1, f);
                    fwrite(pad, 1, 8, f);           /* clear, fog, fog_on, pad */
                    fwrite(fog, sizeof(float), 2, f);
                    for (i = 0; i < t.nv; i++) {
                        fwrite(&t.v[i].x, sizeof(float), 5, f);
                        fwrite(&t.v[i].r, 1, 4, f);
                        fwrite(&t.v[i].obj, 4, 1, f);
                    }
                    for (i = 0; i < t.nd; i++) {
                        const BmapDraw *d = &t.d[i];
                        unsigned char b[8] = { d->alpha, d->cull, d->translucent, d->billboard, d->far, d->grass, 0, 0 };

                        fwrite(&d->tex, 4, 1, f);
                        fwrite(&d->first, 4, 1, f);
                        fwrite(&d->count, 4, 1, f);
                        fwrite(b, 1, 8, f);
                        fwrite(d->pivot, sizeof(float), 3, f);
                    }
                    for (i = 0; i < nt; i++) {
                        const BmapTexture *x = &c->tex[i];
                        int wh[2] = { x->rgba != NULL ? x->w : 0, x->rgba != NULL ? x->h : 0 };
                        unsigned char b[4] = { x->repeat_s, x->repeat_t, x->flip_s, x->flip_t };

                        fwrite(wh, 4, 2, f);
                        fwrite(b, 1, 4, f);
                        if (wh[0] > 0 && wh[1] > 0) {
                            fwrite(x->rgba, 4, (size_t)wh[0] * (size_t)wh[1], f);
                        }
                    }
                    fwrite(t0, sizeof(int), 2, f);
                    fwrite(grid, 1, sizeof grid, f);
                    fclose(f);
                }
            }
        }
        for (id = 0; list != NULL && id < 65536u; id++) {
            if (packs[id] != 0) {
                fprintf(list, "%d\t%u\t%u\t%u\n", indoor, id, first[id], packs[id]);
            }
        }
    }
    if (list != NULL) {
        fclose(list);
    }
}

/*
 * The sea goes on past the map's edge.
 *
 * A map ends where its model ends: the pier's sea is one sheet 61 by 65
 * tiles, and beyond it there is nothing.  The field's steep camera never
 * sees that edge; the battle's low camera does.  So where water lies on the
 * edge of everything loaded (the scene's box), it is continued outward, far
 * past what a battle camera sees: the same surface at the same height, with
 * the same texture laid on as it was (so it joins without a seam and moves
 * with the map's own water), and the bed under it the same way (the
 * surface is see-through: without its bed the far sea had another colour).
 *
 * Water is what the loader already takes for water: a level see-through
 * sheet (material alpha under 31).  The bed is whatever has a level edge on
 * the same stretch of the box's side, up to WATER_MAX under the surface.
 * Land on the box's side is left as it is.  At a corner of the box with
 * water on both sides the two strips meet along the diagonal.
 */
#define SEA_REACH 8192.0f           /* map units past the edge: 512 tiles */

typedef struct { int side; float a, b, y; } SeaEdge;    /* a stretch of water on a side of the box */

/* The side of the box the edge (p, q) lies on: 0 west, 1 east, 2 north, 3 south; -1 none. */
static int sea_side(const BmapVertex *p, const BmapVertex *q, const float lo[2], const float hi[2])
{
    const float e = 0.5f;

    if (fabsf(p->x - lo[0]) < e && fabsf(q->x - lo[0]) < e) return 0;
    if (fabsf(p->x - hi[0]) < e && fabsf(q->x - hi[0]) < e) return 1;
    if (fabsf(p->z - lo[1]) < e && fabsf(q->z - lo[1]) < e) return 2;
    if (fabsf(p->z - hi[1]) < e && fabsf(q->z - hi[1]) < e) return 3;
    return -1;
}

/* Is the point t of a side under (or on: dy 0) a stretch of water of that side? */
static int sea_over(const SeaEdge *se, size_t n, int side, float t, float y, float dy)
{
    size_t i;

    for (i = 0; i < n; i++) {
        if (se[i].side == side && t >= se[i].a - 1.0f && t <= se[i].b + 1.0f && se[i].y - y >= -0.01f && se[i].y - y <= dy) {
            return 1;
        }
    }
    return 0;
}

static int extend_sea(Ctx *c)
{
    size_t ntri = c->ntag, i, nse = 0, cap = 0, made = 0;
    float lo[2] = { 1e30f, 1e30f }, hi[2] = { -1e30f, -1e30f };
    SeaEdge *se = NULL;
    int e, rc = 0;

    for (i = 0; i < c->nv; i++) {
        const BmapVertex *v = &c->v[i];

        lo[0] = fminf(lo[0], v->x); hi[0] = fmaxf(hi[0], v->x);
        lo[1] = fminf(lo[1], v->z); hi[1] = fmaxf(hi[1], v->z);
    }
    /* the water on the box's sides */
    for (i = 0; i < ntri; i++) {
        const TriTag *t = &c->tag[i];
        const BmapVertex *v = &c->v[i * 3u];

        if (t->bb != 0 || !t->translucent || t->alpha >= 31u || fabsf(v[1].y - v[0].y) > 0.01f || fabsf(v[2].y - v[0].y) > 0.01f) {
            continue;
        }
        for (e = 0; e < 3; e++) {
            const BmapVertex *p = &v[e], *q = &v[(e + 1) % 3];
            int side = sea_side(p, q, lo, hi);
            SeaEdge *ns;

            if (side < 0) {
                continue;
            }
            ns = (SeaEdge *)grow(se, &cap, nse + 1u, sizeof *ns);
            if (ns == NULL) {
                free(se);
                return -1;
            }
            se = ns;
            se[nse].side = side;
            se[nse].a = side < 2 ? fminf(p->z, q->z) : fminf(p->x, q->x);
            se[nse].b = side < 2 ? fmaxf(p->z, q->z) : fmaxf(p->x, q->x);
            se[nse].y = p->y;
            nse++;
        }
    }
    /* every level edge on those stretches goes on outward: the water's own, and the bed's under it */
    for (i = 0; i < ntri && nse > 0 && rc == 0; i++) {
        TriTag tag = c->tag[i];
        BmapVertex v[3];
        int water;
        float det, ax, az, bx, bz, ny;

        memcpy(v, &c->v[i * 3u], sizeof v);
        if (tag.bb != 0) {
            continue;
        }
        water = tag.translucent && tag.alpha < 31u && fabsf(v[1].y - v[0].y) <= 0.01f && fabsf(v[2].y - v[0].y) <= 0.01f;
        if (!water && tag.translucent) {
            continue;               /* (a mist sheet, a light: not a bed) */
        }
        ax = v[1].x - v[0].x; az = v[1].z - v[0].z;
        bx = v[2].x - v[0].x; bz = v[2].z - v[0].z;
        det = ax * bz - bx * az;
        if (fabsf(det) < 1e-4f) {
            continue;               /* upright: no place on the ground to lay its texture by */
        }
        ny = az * bx - ax * bz;     /* the way it faces (its normal's height) */
        for (e = 0; e < 3 && rc == 0; e++) {
            BmapVertex p = v[e], q = v[(e + 1) % 3], bv[3], far_p, far_q;
            int side = sea_side(&p, &q, lo, hi), k, tri;
            float tp, tq, out[2] = { 0.0f, 0.0f }, wp[3][3];

            if (side < 0 || fabsf(p.y - q.y) > 0.01f) {
                continue;
            }
            tp = side < 2 ? p.z : p.x;
            tq = side < 2 ? q.z : q.x;
            if (!sea_over(se, nse, side, 0.5f * (tp + tq), p.y, water ? 0.01f : WATER_MAX)) {
                continue;
            }
            out[side < 2 ? 0 : 1] = side == 0 || side == 2 ? -SEA_REACH : SEA_REACH;
            far_p = p;
            far_q = q;
            far_p.x += out[0]; far_p.z += out[1];
            far_q.x += out[0]; far_q.z += out[1];
            /* an end at a corner of the box, with water on the other side there too: the far point goes
             * out both ways (the strip of the other side does the same and meets this one on the diagonal) */
            for (k = 0; k < 2; k++) {
                BmapVertex *end = k == 0 ? &p : &q, *far_end = k == 0 ? &far_p : &far_q;
                float t = side < 2 ? end->z : end->x;
                int lo_c = fabsf(t - lo[side < 2 ? 1 : 0]) < 0.5f, hi_c = fabsf(t - hi[side < 2 ? 1 : 0]) < 0.5f;
                int other = side < 2 ? (lo_c ? 2 : 3) : (lo_c ? 0 : 1);

                if ((lo_c || hi_c) && sea_over(se, nse, other, side < 2 ? end->x : end->z, end->y, water ? 0.01f : WATER_MAX)) {
                    if (side < 2) {
                        far_end->z += lo_c ? -SEA_REACH : SEA_REACH;
                    } else {
                        far_end->x += lo_c ? -SEA_REACH : SEA_REACH;
                    }
                }
            }
            /* the texture goes on as the triangle lays it: its place is linear over the ground */
            for (k = 0; k < 2; k++) {
                BmapVertex *f = k == 0 ? &far_p : &far_q;
                float dx = f->x - v[0].x, dz = f->z - v[0].z;
                float s1 = (dx * bz - bx * dz) / det, s2 = (ax * dz - dx * az) / det;

                f->u = v[0].u + s1 * (v[1].u - v[0].u) + s2 * (v[2].u - v[0].u);
                f->v = v[0].v + s1 * (v[1].v - v[0].v) + s2 * (v[2].v - v[0].v);
            }
            for (tri = 0; tri < 2 && rc == 0; tri++) {
                float e1x, e1z, e2x, e2z;

                if (tri == 0) {
                    bv[0] = p; bv[1] = q; bv[2] = far_q;
                } else {
                    bv[0] = p; bv[1] = far_q; bv[2] = far_p;
                }
                e1x = bv[1].x - bv[0].x; e1z = bv[1].z - bv[0].z;
                e2x = bv[2].x - bv[0].x; e2z = bv[2].z - bv[0].z;
                if ((e1z * e2x - e1x * e2z) * ny < 0.0f) {      /* facing as the triangle it continues */
                    BmapVertex sw = bv[1];

                    bv[1] = bv[2];
                    bv[2] = sw;
                }
                for (k = 0; k < 3; k++) {
                    wp[k][0] = bv[k].x; wp[k][1] = bv[k].y; wp[k][2] = bv[k].z;
                }
                rc = add_tri(c, bv, &tag, (const float (*)[3])wp, 0, 0);
                made++;
            }
        }
    }
    free(se);
    if (getenv("BMAP_PLACE_LOG") != NULL) {
        fprintf(stderr, "bmap: sea: %u stretches of water on the map's edge, %u triangles added\n", (unsigned)nse, (unsigned)made);
    }
    return rc;
}

int bmap_build(BmapScene *s, unsigned zone, float px, float pz, int season, int hour, int minute)
{
    Ctx *c = (Ctx *)calloc(1, sizeof *c);

    if (getenv("BMAP_MODELS_DUMP") != NULL) {
        research_models(getenv("BMAP_MODELS_DUMP"));
        fprintf(stderr, "bmap: models dumped\n");
        exit(0);
    }
    Blob headers, matrix;
    Buf hb, mb;
    unsigned pcx, pcz, area, matrix_id, mode, w, h, cell;
    unsigned i;
    int rc = -1, wfbc_on = 0;
    Area *player_area;
    BmapWfbc wfbc;

    memset(s, 0, sizeof *s);
    /* Black City and White Forest: the save's blocks (bmap_set_wfbc) or a
     * full place */
    if (zone == 0u || zone == 424u) {
        if (g_wfbc_set) {
            wfbc = g_wfbc;
            wfbc_on = 1;
        } else {
            wfbc_on = bmap_wfbc_full(zone, &wfbc) == 0;
        }
    }
    g_wfbc_set = 0;
    if (c == NULL) {
        return -1;
    }
    c->season = season & 3;
    memset(&headers, 0, sizeof headers);
    memset(&matrix, 0, sizeof matrix);
    if (read_flat("a/0/1/3", &c->area_file) != 0 || arc_load(c, "a/0/1/2", 0, &headers) != 0) {
        goto out;
    }
    hb = bb_of(&headers);
    if ((size_t)(zone + 1u) * 48u > hb.n) {
        goto out;
    }
    area = rd16(hb, zone * 48u + 2u);
    matrix_id = rd16(hb, zone * 48u + 4u);
    if (arc_load(c, "a/0/0/9", matrix_id, &matrix) != 0) {
        goto out;
    }
    mb = bb_of(&matrix);
    mode = rd16(mb, 0);
    w = rd16(mb, 4);
    h = rd16(mb, 6);
    if (w == 0u || h == 0u || 8u + (size_t)w * h * 4u > mb.n) {
        goto out;
    }
    player_area = area_get(c, area);
    if (player_area == NULL) {
        goto out;
    }
    load_lights(c, player_area->light_type, hour, minute);
    {
        /* Research: BMAP_JOIN="zone ax ay az bx bz turn lx lz side width[;...]": another zone's whole map beside
         * this one: its point (bx, bz) on this map's (ax, ay, az), turned `turn` degrees about it; the line between
         * the two goes through (lx, lz), the joined map on the side `side` of it (degrees: 0 north, 90 east, 180
         * south); `width`: the strip's half width along the line (0: no limit).  BMAP_JOIN_MODE: line, whole, none. */
        const char *jn = getenv("BMAP_JOIN"), *jm = getenv("BMAP_JOIN_MODE");

        c->join_mode = jm != NULL && strcmp(jm, "whole") == 0 ? 1 : jm != NULL && strcmp(jm, "none") == 0 ? 2 : 0;
        c->join_cur = -1;
        while (jn != NULL && *jn != '\0' && c->njoin < 8) {
            unsigned jz = 0;
            float f[10];

            if (sscanf(jn, "%u %f %f %f %f %f %f %f %f %f %f", &jz, &f[0], &f[1], &f[2], &f[3], &f[4], &f[5], &f[6], &f[7], &f[8], &f[9]) == 11
                && (size_t)(jz + 1u) * 48u <= hb.n) {
                int q = c->njoin++;

                c->joins[q].zone = jz;
                c->joins[q].a[0] = f[0]; c->joins[q].a[1] = f[1]; c->joins[q].a[2] = f[2];
                c->joins[q].b[0] = f[3]; c->joins[q].b[1] = f[4];
                c->joins[q].cs = cosf(f[5] * 3.14159265f / 180.0f);
                c->joins[q].sn = sinf(f[5] * 3.14159265f / 180.0f);
                c->joins[q].l[0] = f[6]; c->joins[q].l[1] = f[7];
                c->joins[q].n[0] = sinf(f[8] * 3.14159265f / 180.0f);
                c->joins[q].n[1] = -cosf(f[8] * 3.14159265f / 180.0f);
                c->joins[q].width = f[9];
            }
            jn = strchr(jn, ';');
            if (jn != NULL) {
                jn++;
            }
        }
    }
    pcx = (unsigned)(px / CELL_UNITS);
    pcz = (unsigned)(pz / CELL_UNITS);
    (void)pcx; (void)pcz;
    for (cell = 0; cell < w * h; cell++) {
        unsigned chunk = rd32(mb, 8u + cell * 4u);
        int cx = (int)(cell % w), cz = (int)(cell / w);
        float dx, dz, x0 = (float)cx * CELL_UNITS, z0 = (float)cz * CELL_UNITS;
        unsigned cz_zone = zone, carea = area;
        Area *ar;

        if (chunk == NO_CHUNK) {
            continue;
        }
        dx = x0 - px > px - (x0 + CELL_UNITS) ? x0 - px : px - (x0 + CELL_UNITS);
        dz = z0 - pz > pz - (z0 + CELL_UNITS) ? z0 - pz : pz - (z0 + CELL_UNITS);
        if (dx > REACH || dz > REACH) {
            continue;
        }
        if (mode == 1u) {
            /* a cell of no zone (0xFFFFFFFF: the filler ground between
             * places, Accumula's south-west) keeps the player's area; its
             * +1 wrapped to 0 and read area 0, whose textures it lacks, so
             * the ground there was a hole */
            cz_zone = rd32(mb, 8u + w * h * 4u + cell * 4u);
            if (cz_zone != 0xFFFFFFFFu && (size_t)cz_zone * 48u + 48u <= hb.n) {
                carea = rd16(hb, cz_zone * 48u + 2u);
            }
        }
        ar = area_get(c, carea);
        if (ar == NULL) {
            continue;
        }
        c->cur_far = (u8)(dx > NEAR_REACH || dz > NEAR_REACH);
        if (add_chunk(c, ar, chunk, cx, cz) != 0) {
            goto out;
        }
        if (wfbc_on && cx < 2 && cz < 2 && add_wfbc(c, ar, &wfbc, cx, cz) != 0) {
            goto out;
        }
    }
    /* (the gym's devices from the game's files, in their starting state, only when the field's own objects were
     * not noted: a battle without a field before it, the spot editor) */
    if (!live_has(zone) && add_gimmicks(c, zone, px, pz) != 0) {
        goto out;
    }
    if (add_live(c, zone, px, pz) != 0) {
        goto out;
    }
    if (c->njoin > 0 && c->join_mode == 1) {
        jn_drop_whole(c, 0, -1);
    }
    {
        int q;

        for (q = 0; q < c->njoin; q++) {
            unsigned jz = c->joins[q].zone;
            Blob jm;
            Area *jar = area_get(c, rd16(hb, jz * 48u + 2u));
            size_t added = c->ntag;

            memset(&jm, 0, sizeof jm);
            if (jar != NULL && arc_load(c, "a/0/0/9", rd16(hb, jz * 48u + 4u), &jm) == 0) {
                Buf jmb = bb_of(&jm);
                unsigned jw = rd16(jmb, 4), jh = rd16(jmb, 6), jc;

                c->join_cur = q;
                for (jc = 0; jw != 0u && jh != 0u && 8u + (size_t)jw * jh * 4u <= jmb.n && jc < jw * jh; jc++) {
                    unsigned ch = rd32(jmb, 8u + jc * 4u);

                    if (ch == NO_CHUNK) {
                        continue;
                    }
                    c->cur_far = 1;
                    if (add_chunk(c, jar, ch, (int)(jc % jw), (int)(jc / jw)) != 0) {
                        blob_free(&jm);
                        goto out;
                    }
                }
                blob_free(&jm);
                if (c->join_mode == 1) {
                    jn_drop_whole(c, added, q);
                }
            }
            fprintf(stderr, "bmap: joined zone %u (mode %d): %u triangles\n", jz, c->join_mode, (unsigned)(c->ntag - added));
        }
    }
    c->join_cur = -2;
    if (extend_sea(c) != 0) {
        goto out;
    }
    s->clear[0] = c->lights.clear[0]; s->clear[1] = c->lights.clear[1]; s->clear[2] = c->lights.clear[2];
    s->fog[0] = c->lights.fog[0]; s->fog[1] = c->lights.fog[1]; s->fog[2] = c->lights.fog[2];
    load_fog(c, zone, hb, s);
    if (finish(c, s) != 0) {
        goto out;
    }
    s->t = c->tex;
    s->nt = (unsigned)c->ntex;
    s->tex_gen = bmap_tex_gen();
    c->tex = NULL;
    memcpy(s->bob_y, c->bob_y, sizeof s->bob_y);
    s->bob_n = c->bob_n;
    s->uva = c->uva;
    s->nuva = (unsigned)c->nuva;
    c->uva = NULL;
    c->nuva = 0;
    if (s->clear[0] < 24u && s->clear[1] < 24u && s->clear[2] < 24u) {
        /* A room or a cave has a black "sky": nothing is drawn beyond its
         * walls.  In battle the camera sees over them, so the space around
         * takes the place's own colour instead (the mean colour of what is
         * near the player, darkened), and the far haze fades into it. */
        double sum[3] = { 0.0, 0.0, 0.0 }, wsum = 0.0;
        unsigned i, j;

        for (i = 0; i < s->nd; i++) {
            const BmapDraw *d = &s->d[i];
            double tc[3] = { 1.0, 1.0, 1.0 };

            if (d->far || d->count == 0 || d->first + d->count > s->nv) {
                continue;
            }
            if (d->tex >= 0 && (unsigned)d->tex < s->nt) {
                const BmapTexture *t = &s->t[d->tex];
                double a = 0.0;
                int q;

                tc[0] = tc[1] = tc[2] = 0.0;
                for (q = 0; q < t->w * t->h; q++) {
                    double w = t->rgba[q * 4 + 3] / 255.0;

                    tc[0] += t->rgba[q * 4] * w;
                    tc[1] += t->rgba[q * 4 + 1] * w;
                    tc[2] += t->rgba[q * 4 + 2] * w;
                    a += w;
                }
                if (a <= 0.0) {
                    continue;
                }
                tc[0] /= a * 255.0; tc[1] /= a * 255.0; tc[2] /= a * 255.0;
            }
            for (j = d->first; j < d->first + d->count; j++) {
                const BmapVertex *v = &s->v[j];
                double dx = v->x - px, dz = v->z - pz;

                if (dx * dx + dz * dz > 400.0 * 400.0) {
                    continue;
                }
                sum[0] += v->r * tc[0];
                sum[1] += v->g * tc[1];
                sum[2] += v->b * tc[2];
                wsum += 1.0;
            }
        }
        if (wsum > 0.0) {
            for (i = 0; i < 3; i++) {
                s->clear[i] = (u8)(sum[i] / wsum * 0.55);
            }
        }
    }
    build_ground(c, s, px, pz);
    rc = 0;
out:
    for (i = 0; i < c->nareas; i++) {
        area_free(&c->areas[i]);
    }
    for (i = 0; i < c->ntsets; i++) {
        blob_free(&c->tsets[i]);
    }
    for (i = 0; i < c->nents; i++) {
        free(c->ents[i].rgba);
    }
    if (c->tex != NULL) {
        for (i = 0; i < c->ntex; i++) {
            free(c->tex[i].rgba);
        }
    }
    for (i = 0; i < c->narcs; i++) {
        bw_arc_close(&c->arcs[i].f);
    }
    free(c->ents);
    free(c->tex);
    free(c->texrefs);
    free(c->v);
    free(c->tag);
    free(c->gt);
    free(c->walk);
    for (i = 0; i < c->nuva; i++) {
        free(c->uva[i].m);
    }
    free(c->uva);
    free((void *)c->uva_key);
    blob_free(&c->area_file);
    blob_free(&headers);
    blob_free(&matrix);
    free(c);
    if (rc != 0) {
        bmap_free(s);
    }
    return rc;
}

unsigned bmap_tex_gen(void)
{
    static unsigned gen;

    if (++gen == 0) {
        gen = 1;
    }
    return gen;
}

void bmap_free(BmapScene *s)
{
    unsigned i;
    Priv *g = (Priv *)s->priv;

    free(s->v);
    free(s->d);
    free(s->prep);
    if (s->shared) {                    /* a copy (bmap_copy): the rest is the original's */
        memset(s, 0, sizeof *s);
        return;
    }
    for (i = 0; i < s->nt; i++) {
        free(s->t[i].rgba);
    }
    free(s->t);
    for (i = 0; i < s->nuva; i++) {
        free(s->uva[i].m);
    }
    free(s->uva);
    if (g != NULL) {
        free(g->tri);
        free(g->start);
        free(g->idx);
        free(g->walk);
        free(g);
    }
    memset(s, 0, sizeof *s);
}

/* ------------------------------------------------------------------ */
/* NPC sprite cells (bmap_npc.c)                                       */
/* ------------------------------------------------------------------ */

/* One cw x ch cell of a field billboard texture file (a BTX0 in a/0/4/9).
 * The file holds one or more textures cut into cells, row by row, textures in
 * dictionary order (the game uploads them back to back and indexes cells
 * across them).  All textures use the file's first palette.  Returns 0 and a
 * malloc'd cw*ch*4 straight-alpha picture, or -1. */
int bmap_btx_cell(const unsigned char *data, size_t size, int cw, int ch,
                  unsigned cell, unsigned char **out)
{
    Buf d, blk;
    Dict td, pd;
    char pal[17], nm[17];
    unsigned ti, first = 0u;
    u8 *copy = (u8 *)malloc(size > 0u ? size : 1u);
    int rc = -1;

    if (copy == NULL) {
        return -1;
    }
    memcpy(copy, data, size);
    d.p = copy;
    d.n = size;
    if (cw <= 0 || ch <= 0 || container_block(d, "TEX0", &blk) != 0 ||
        dict_parse(blk, 0x3C, &td) != 0 ||
        dict_parse(blk, 0x3C + rd16(blk, 0x3C + 2), &pd) != 0 || pd.count == 0u) {
        free(copy);
        return -1;
    }
    /* The field draws billboards with colour 0 transparent whatever the
     * texture's own flag says, so set the flag on every texture first. */
    for (ti = 0; ti < td.count; ti++) {
        u8 *flags = copy + (size_t)(blk.p - copy) + td.rec + ti * 8u + 3u; /* params, high byte */

        *flags |= 0x20u;
    }
    dict_name(&pd, 0, pal);
    for (ti = 0; ti < td.count; ti++) {
        Image img;
        unsigned per, cols;

        dict_name(&td, ti, nm);
        memset(&img, 0, sizeof img);
        if (decode_block(blk, nm, pal, &img) != 0) {
            break;
        }
        cols = (unsigned)(img.w / cw);
        per = cols * (unsigned)(img.h / ch);
        if (per != 0u && cell < first + per) {
            unsigned c = cell - first, x0 = (c % cols) * (unsigned)cw,
                     y0 = (c / cols) * (unsigned)ch, y;
            u8 *o = (u8 *)malloc((size_t)cw * (size_t)ch * 4u);

            if (o == NULL) {
                free(img.rgba);
                break;
            }
            for (y = 0; y < (unsigned)ch; y++) {
                memcpy(o + (size_t)y * (size_t)cw * 4u,
                       img.rgba + ((size_t)(y0 + y) * (size_t)img.w + x0) * 4u,
                       (size_t)cw * 4u);
            }
            free(img.rgba);
            *out = o;
            rc = 0;
            break;
        }
        first += per;
        free(img.rgba);
    }
    free(copy);
    return rc;
}

/* A model file (BMD0) placed into a built scene: the field's 3D objects
 * (bmap_npc.c).  The model stands at (x, y, z) turned `turns` of a full
 * circle about y (0 faces +z), its textures from `btx` (a BTX0) or, when
 * that is empty, from the BMD0 itself.  It is lit by the fixed lights
 * (the zone's own light keyframe is not kept after the build).  The scene
 * must be the original of bmap_build, not a bmap_copy: its vertices, draws
 * and textures are grown in place.  New draws keep the opaque-then-
 * translucent order.  Returns 0 on success. */
int bmap_add_bmd(BmapScene *s, const unsigned char *bmd, size_t bmd_n,
                 const unsigned char *btx, size_t btx_n, const char *key,
                 float x, float y, float z, float turns)
{
    Ctx *c = (Ctx *)calloc(1, sizeof *c);
    Model md;
    Buf data, set;
    BmapScene t;
    Aff base;
    float tr[3];
    BmapVertex *nv;
    BmapDraw *nd;
    BmapTexture *nt;
    unsigned i, ot = 0, o, n, first_tr;
    int rc = -1;

    if (c == NULL || s->shared) {
        free(c);
        return -1;
    }
    data.p = bmd;
    data.n = bmd_n;
    set.p = btx_n ? btx : bmd;
    set.n = btx_n ? btx_n : bmd_n;
    memset(&t, 0, sizeof t);
    lights_fixed(&c->lights);
    if (model_parse(data, &md) != 0) {
        free(c);
        return -1;
    }
    tr[0] = x; tr[1] = y; tr[2] = z;
    base = aff_yaw(turns, tr);
    if (add_model(c, &md, key, set, &base, 0, NULL) == 0 && c->ntag > 0 && finish(c, &t) == 0) {
        nv = (BmapVertex *)realloc(s->v, (s->nv + t.nv) * sizeof *nv);
        if (nv != NULL) {
            s->v = nv;
        }
        nt = (BmapTexture *)realloc(s->t, (s->nt + c->ntex) * sizeof *nt);
        if (nt != NULL) {
            s->t = nt;
        }
        nd = (BmapDraw *)malloc((s->nd + t.nd + 1u) * sizeof *nd);
        if (nv != NULL && nt != NULL && nd != NULL) {
            memcpy(s->v + s->nv, t.v, t.nv * sizeof *nv);
            memcpy(s->t + s->nt, c->tex, c->ntex * sizeof *nt);   /* the scene owns the pixels now */
            ot = s->nt;
            for (i = 0; i < t.nd; i++) {
                t.d[i].first += s->nv;
                if (t.d[i].tex >= 0) {
                    t.d[i].tex += (int)ot;
                }
            }
            /* opaque old, opaque new, translucent old, translucent new */
            for (first_tr = 0; first_tr < s->nd && !s->d[first_tr].translucent; first_tr++) {
            }
            o = 0;
            memcpy(nd + o, s->d, first_tr * sizeof *nd);
            o += first_tr;
            for (n = 0; n < t.nd && !t.d[n].translucent; n++) {
            }
            memcpy(nd + o, t.d, n * sizeof *nd);
            o += n;
            memcpy(nd + o, s->d + first_tr, (s->nd - first_tr) * sizeof *nd);
            o += s->nd - first_tr;
            memcpy(nd + o, t.d + n, (t.nd - n) * sizeof *nd);
            o += t.nd - n;
            free(s->d);
            s->d = nd;
            s->nd = o;
            s->nv += t.nv;
            s->nt += (unsigned)c->ntex;
            s->tex_gen = bmap_tex_gen();
            c->ntex = 0;
            nd = NULL;
            rc = 0;
        } else {
            free(nd);
        }
        free(t.v);
        free(t.d);
    }
    model_free(&md);
    for (i = 0; i < c->ntex; i++) {
        free(c->tex[i].rgba);
    }
    for (i = 0; i < c->nents; i++) {
        free(c->ents[i].rgba);
    }
    free(c->tex);
    free(c->texrefs);
    free(c->ents);
    free(c->v);
    free(c->tag);
    free(c->gt);
    free(c->walk);
    free(c);
    return rc;
}
