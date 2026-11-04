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

typedef struct { int tex; u8 alpha, cull, translucent; int bb; float pivot[3]; u8 far; unsigned short uvanim; } TriTag;

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
    BmapUvAnim *uva;        /* the texture animations, and what each is of */
    const u8 **uva_key;
    size_t nuva, uva_cap;
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

static int add_tri(Ctx *c, const BmapVertex t[3], const TriTag *tag, const float pos[3][3], int ground, int level_ok)
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
    c->tag[c->ntag++] = *tag;
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

static int add_model(Ctx *c, Model *md, const char *setkey, Buf set, const Aff *base, int ground,
                     const BldAnim *anim)
{
    unsigned di;


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
                bv[k].obj = ground == 1 ? 0u : c->obj;   /* 2: a placed model, its own number */
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
                if (add_tri(c, bv, &tag, (const float (*)[3])wp, ground, level_ok) != 0) {
                    free(tl.v);
                    return -1;
                }
            }
        }
        free(tl.v);
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
        add_walk(c, bb, from == 16u ? 2u : 1u, cx, cz);
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
            rc = add_model(c, &ar->bmodels[mi], bkey, bb_of(&ar->btex), &place, 2,
                           &ar->banims[mi]);   /* placed */
            if (rc != 0) {
                break;
            }
        }
    }
    blob_free(&b);
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


int bmap_build(BmapScene *s, unsigned zone, float px, float pz, int season, int hour, int minute)
{
    Ctx *c = (Ctx *)calloc(1, sizeof *c);
    Blob headers, matrix;
    Buf hb, mb;
    unsigned pcx, pcz, area, matrix_id, mode, w, h, cell;
    unsigned i;
    int rc = -1;
    Area *player_area;

    memset(s, 0, sizeof *s);
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
            cz_zone = rd32(mb, 8u + w * h * 4u + cell * 4u);
            if ((size_t)(cz_zone + 1u) * 48u <= hb.n) {
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
    }
    if (add_gimmicks(c, zone, px, pz) != 0) {
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
