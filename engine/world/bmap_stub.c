/*
 * engine/world/bmap_stub.c -- a stand-in for bmap_data.c: a textured ground
 * grid and a ring of boxes around the player, so the battle backdrop (camera,
 * placement, hiding, depth) can be tried without the map data.  Built only
 * when engine/world/bmap_data.c is absent (CMakeLists.txt).
 */

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "bmap.h"

unsigned bmap_tex_gen(void)
{
    static unsigned gen;

    if (++gen == 0) {
        gen = 1;
    }
    return gen;
}

#define TILE 16.0f
#define HALF 20                     /* tiles each way from the player's */

static unsigned g_cap;

static BmapVertex *push(BmapScene *s, float x, float y, float z, float u,
                        float v, unsigned char r, unsigned char g,
                        unsigned char b)
{
    BmapVertex *p;

    if (s->nv + 1u > g_cap) {
        unsigned c = g_cap ? g_cap * 2u : 8192u;
        BmapVertex *n = (BmapVertex *)realloc(s->v, (size_t)c * sizeof(*n));

        if (n == NULL) {
            return NULL;
        }
        s->v = n;
        g_cap = c;
    }
    p = &s->v[s->nv++];
    p->x = x; p->y = y; p->z = z; p->u = u; p->v = v;
    p->r = r; p->g = g; p->b = b; p->a = 255;
    return p;
}

static void quad(BmapScene *s, const float p[4][3], unsigned char r,
                 unsigned char g, unsigned char b)
{
    static const int o[6] = { 0, 1, 2, 0, 2, 3 };
    static const float uv[4][2] = { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } };
    int i;

    for (i = 0; i < 6; i++) {
        push(s, p[o[i]][0], p[o[i]][1], p[o[i]][2], uv[o[i]][0], uv[o[i]][1],
             r, g, b);
    }
}

static void box(BmapScene *s, float x, float z, float w, float h,
                unsigned char r, unsigned char g, unsigned char b)
{
    float x0 = x - w, x1 = x + w, z0 = z - w, z1 = z + w;
    const float side[4][4][3] = {
        { { x0, 0, z1 }, { x1, 0, z1 }, { x1, h, z1 }, { x0, h, z1 } },
        { { x1, 0, z1 }, { x1, 0, z0 }, { x1, h, z0 }, { x1, h, z1 } },
        { { x1, 0, z0 }, { x0, 0, z0 }, { x0, h, z0 }, { x1, h, z0 } },
        { { x0, 0, z0 }, { x0, 0, z1 }, { x0, h, z1 }, { x0, h, z0 } },
    };
    const float top[4][3] = { { x0, h, z1 }, { x1, h, z1 }, { x1, h, z0 },
                              { x0, h, z0 } };
    static const int shade[4] = { 100, 80, 60, 90 };
    int i;

    for (i = 0; i < 4; i++) {
        quad(s, side[i], (unsigned char)(r * shade[i] / 100),
             (unsigned char)(g * shade[i] / 100),
             (unsigned char)(b * shade[i] / 100));
    }
    quad(s, top, r, g, b);
}

int bmap_build(BmapScene *s, unsigned zone, float px, float pz, int season,
               int hour, int minute)
{
    float cx = (float)((int)(px / TILE)) * TILE;
    float cz = (float)((int)(pz / TILE)) * TILE;
    int tx, tz, i, x, y;
    BmapTexture *t;
    BmapDraw *d;

    (void)zone; (void)season; (void)hour; (void)minute;
    memset(s, 0, sizeof(*s));
    g_cap = 0;
    for (tz = -HALF; tz < HALF; tz++) {
        for (tx = -HALF; tx < HALF; tx++) {
            float x0 = cx + (float)tx * TILE, z0 = cz + (float)tz * TILE;
            const float p[4][3] = { { x0, 0, z0 }, { x0 + TILE, 0, z0 },
                                    { x0 + TILE, 0, z0 + TILE },
                                    { x0, 0, z0 + TILE } };
            int odd = (tx + tz) & 1;

            quad(s, p, odd ? 215 : 255, odd ? 235 : 255, odd ? 215 : 255);
        }
    }
    s->nd = 2;
    s->d = (BmapDraw *)calloc(2, sizeof(*s->d));
    t = (BmapTexture *)calloc(1, sizeof(*t));
    if (s->d == NULL || t == NULL) {
        free(s->d);
        free(t);
        free(s->v);
        memset(s, 0, sizeof(*s));
        return -1;
    }
    s->d[0].tex = 0;
    s->d[0].first = 0;
    s->d[0].count = s->nv;
    s->d[0].alpha = 31;
    s->d[0].cull = 0;
    /* a ring of boxes, 3 to 9 tiles out, and one that marks north */
    d = &s->d[1];
    d->first = s->nv;
    for (i = 0; i < 12; i++) {
        double ang = (double)i * 0.5236;
        float rad = TILE * (4.0f + (float)(i % 3) * 2.0f);
        float bx = cx + TILE * 0.5f + rad * (float)cos(ang);
        float bz = cz + TILE * 0.5f + rad * (float)sin(ang);

        box(s, bx, bz, 6.0f, 24.0f + (float)(i % 3) * 12.0f,
            (unsigned char)(60 + 15 * (i % 4)), (unsigned char)(120 + 10 * i),
            (unsigned char)(50 + 20 * (i % 3)));
    }
    box(s, cx + TILE * 0.5f, cz - TILE * 8.5f, 8.0f, 40.0f, 230, 60, 60);
    box(s, cx + TILE * 0.5f, cz + TILE * 8.5f, 8.0f, 40.0f, 60, 60, 230);
    box(s, cx + TILE * 8.5f, cz + TILE * 0.5f, 8.0f, 40.0f, 240, 220, 60);
    box(s, cx - TILE * 8.5f, cz + TILE * 0.5f, 8.0f, 40.0f, 60, 220, 220);
    d->tex = -1;
    d->count = s->nv - d->first;
    d->alpha = 31;
    t->w = t->h = 16;
    t->rgba = (unsigned char *)malloc(16 * 16 * 4);
    if (t->rgba == NULL) {
        free(s->d);
        free(t);
        free(s->v);
        memset(s, 0, sizeof(*s));
        return -1;
    }
    for (y = 0; y < 16; y++) {
        for (x = 0; x < 16; x++) {
            unsigned char *q = t->rgba + (y * 16 + x) * 4;
            int edge = x == 0 || y == 0;
            int n = ((x * 7 + y * 13) & 7);

            q[0] = (unsigned char)(edge ? 70 : 90 + n * 3);
            q[1] = (unsigned char)(edge ? 120 : 170 + n * 4);
            q[2] = (unsigned char)(edge ? 60 : 80 + n * 2);
            q[3] = 255;
        }
    }
    t->repeat_s = t->repeat_t = 1;
    s->t = t;
    s->nt = 1;
    s->tex_gen = bmap_tex_gen();
    s->clear[0] = 150; s->clear[1] = 200; s->clear[2] = 240;
    return 0;
}

void bmap_free(BmapScene *s)
{
    unsigned i;

    for (i = 0; i < s->nt; i++) {
        free(s->t[i].rgba);
    }
    free(s->t);
    free(s->d);
    free(s->v);
    memset(s, 0, sizeof(*s));
}

float bmap_ground_y(const BmapScene *s, float x, float z)
{
    (void)s; (void)x; (void)z;
    return 0.0f;
}
