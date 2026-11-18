/*
 * engine/world/bmap_grass.c -- hand-made grass layers of the battle backdrop (bmap_grass.h; research:
 * BMAP_GRASS_FILE names the layers file).
 *
 * The file, a line a statement ('#' starts a comment):
 *   grass NAME [from PICTURE] [over 0|1]
 *                                  the grass sheet NAME (its picture's name) is built of pieces of PICTURE (default:
 *                                  its own picture); each pixel of a piece is built as a quad of its one colour
 *   map                            then the picture's 16 rows: a letter a pixel names the piece it belongs to
 *   fill P X Y SX SY               piece P also has pixel (X, Y), coloured as the picture's pixel (SX, SY): a part of
 *                                  the leaf the picture hides under another, or a blade carried on downward
 *   paint P SX SY                  then 16 rows: each letter is a pixel piece P also has, painted in the colour of
 *                                  the picture's pixel (SX, SY): a larger hidden part drawn by hand (the body
 *                                  going on under the leaves)
 *   ink S X Y                      the letter or digit S is the colour of the picture's pixel (X, Y)
 *   draw P                         then rows up to a line "end": a leaf drawn by hand, a pixel a character (an
 *                                  ink, or '.' for clear), its tip up; the piece is laid on the leaf as a
 *                                  picture, cut in strips every `mesh` rows (grass ... mesh N, default 2)
 *   piece P root X Y axis DEG      where the leaf leaves its stem in the picture, and the way it runs there
 *                                  (degrees: 0 east, 90 south)
 *   put P [at X Y Z] [yaw D] [elev D] [bend D] [roll D] [scale S] [width S] [cup C] [shade S] [ao S] [flip 1]
 *         [thick T] [rim S] [back S]
 *                                  the piece stood in a bush: its root at (X east, Y up, Z south) of the bush's
 *                                  middle (map units), heading `yaw` (0 east, 90 south), rising `elev` degrees at
 *                                  the root, bent down by `bend` degrees at its tip, `scale` map units a pixel,
 *                                  folded along its middle by `cup`, lit `shade`, `ao` at its root; `thick` map
 *                                  units thick: its under side lit `back`, its rim `rim` (of its upper side)
 *   jitter [yaw D] [elev D] [scale S]   each piece differs by up to that much
 *   spot X Z                       a bush stands there in the tile (every put is made at each spot)
 *   bush [pos P] [rot D] [size S]  each bush is moved by up to P, turned by up to D degrees as a whole and sized by
 *                                  up to S (0.15: 15% larger or smaller)
 *   hide NAME                      the sheet NAME is left out (a fringe the pieces replace)
 * A piece put with yaw = its axis, elev 0, bend 0, scale 1 lies on the ground exactly as the picture shows it.
 */

#include "bmap_grass.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GL_GRID 32          /* a piece's pixels lie in a 32 x 32 grid (the picture's 16 rows, and a blade carried on) */
#define GL_PUTS 480
#define D2R 0.0174533f

typedef struct {
    float rx, ry, axis, smax;
    unsigned short n;
    unsigned char px[256], py[256], sx[256], sy[256];
    int bx0, by0, bw, bh, cellx;        /* its box in the grid; its cell's left edge in the atlas */
    int drawn;                          /* drawn in the file (not cut from the picture) */
} GlPiece;

typedef struct {
    int piece, flip;
    float at[3], yaw, elev, bend, roll, scale, width, cup, shade, ao, thick, rim, back;
} GlPut;

struct BmapGrassKind {
    char name[17], from[17];
    int over, nput, rows, mesh, nspot, aw, ah;
    GlPiece pc[26];
    GlPut put[GL_PUTS];
    float jyaw, jelev, jscale, jpos, jrot, jsize, spot[8][2];
    unsigned char ink[128][2], inkset[128];     /* a drawing's colour letters: the picture's pixel of that colour */
};

static BmapGrassKind *g_gl[16];
static int g_ngl, g_gl_loaded;
static char g_gl_hide[8][17];
static int g_gl_nhide;

static void gl_finish(BmapGrassKind *g)
{
    int pi, k, c, x = 0, h = 1;

    for (pi = 0; pi < 26; pi++) {
        GlPiece *pc = &g->pc[pi];
        const float ax = cosf(pc->axis), ay = sinf(pc->axis);
        int x0 = GL_GRID, y0 = GL_GRID, x1 = -1, y1 = -1;

        pc->smax = 0.5f;
        for (k = 0; k < pc->n; k++) {
            for (c = 0; c < 4; c++) {
                const float sv = (pc->px[k] + (c == 1 || c == 2) - pc->rx) * ax + (pc->py[k] + (c >= 2) - pc->ry) * ay;

                pc->smax = sv > pc->smax ? sv : pc->smax;
            }
            x0 = pc->px[k] < x0 ? pc->px[k] : x0;
            y0 = pc->py[k] < y0 ? pc->py[k] : y0;
            x1 = pc->px[k] > x1 ? pc->px[k] : x1;
            y1 = pc->py[k] > y1 ? pc->py[k] : y1;
        }
        if (pc->n == 0) {
            continue;
        }
        pc->bx0 = x0;
        pc->by0 = y0;
        pc->bw = x1 - x0 + 1;
        pc->bh = y1 - y0 + 1;
        pc->cellx = x + 1;              /* (a clear pixel between the cells) */
        x += pc->bw + 2;
        h = pc->bh + 2 > h ? pc->bh + 2 : h;
    }
    for (g->aw = 8; g->aw < x + 1; g->aw *= 2) {
    }
    for (g->ah = 8; g->ah < h; g->ah *= 2) {
    }
}

static void gl_load(void)
{
    const char *path = getenv("BMAP_GRASS_FILE");
    FILE *f;
    char line[512];
    BmapGrassKind *g = NULL;
    int maprow = -1, paint = -1, paintx = 0, painty = 0, drawp = -1, drawrow = 0, i;

    if (g_gl_loaded) {
        return;
    }
    g_gl_loaded = 1;
    f = path != NULL ? fopen(path, "r") : NULL;
    if (f == NULL) {
        return;
    }
    while (fgets(line, sizeof line, f) != NULL) {
        char *tok[40], *q, *hash = strchr(line, '#');
        int nt = 0;

        if (drawp >= 0 && g != NULL) {          /* (a row of a drawing, or its end) */
            GlPiece *pc = &g->pc[drawp];

            if (strncmp(line, "end", 3) == 0) {
                drawp = -1;
                continue;
            }
            for (i = 0; line[i] != '\0' && line[i] != '\n' && line[i] != '\r' && i < GL_GRID; i++) {
                const unsigned char ch = (unsigned char)line[i];

                if (ch < 128 && g->inkset[ch] && pc->n < 256 && drawrow < GL_GRID) {
                    pc->px[pc->n] = (unsigned char)i;
                    pc->py[pc->n] = (unsigned char)drawrow;
                    pc->sx[pc->n] = g->ink[ch][0];
                    pc->sy[pc->n] = g->ink[ch][1];
                    pc->n++;
                }
            }
            drawrow++;
            continue;
        }
        if (maprow >= 0 && g != NULL) {         /* (a row of the map: taken whole) */
            for (i = 0; line[i] != '\0' && line[i] != '\n' && line[i] != '\r' && i < GL_GRID; i++) {
                if (line[i] >= 'A' && line[i] <= 'Z') {
                    GlPiece *pc = &g->pc[(paint >= 0 ? paint : line[i] - 'A')];

                    if (pc->n < 256) {
                        pc->px[pc->n] = (unsigned char)i;
                        pc->py[pc->n] = (unsigned char)maprow;
                        pc->sx[pc->n] = (unsigned char)(paint >= 0 ? paintx : i);
                        pc->sy[pc->n] = (unsigned char)(paint >= 0 ? painty : maprow);
                        pc->n++;
                    }
                }
            }
            maprow = maprow + 1 < g->rows ? maprow + 1 : -1;
            if (maprow < 0) {
                paint = -1;
            }
            continue;
        }
        if (hash != NULL) {
            *hash = '\0';
        }
        for (q = strtok(line, " \t\r\n"); q != NULL && nt < 40; q = strtok(NULL, " \t\r\n")) {
            tok[nt++] = q;
        }
        if (nt == 0) {
            continue;
        }
        if (strcmp(tok[0], "grass") == 0 && nt >= 2 && g_ngl < 16) {
            g = (BmapGrassKind *)calloc(1, sizeof *g);
            if (g == NULL) {
                break;
            }
            g_gl[g_ngl++] = g;
            snprintf(g->name, sizeof g->name, "%s", tok[1]);
            g->over = 1;
            g->rows = 16;
            g->mesh = 2;
            for (i = 2; i + 1 < nt; i += 2) {
                if (strcmp(tok[i], "from") == 0) snprintf(g->from, sizeof g->from, "%s", tok[i + 1]);
                if (strcmp(tok[i], "over") == 0) g->over = atoi(tok[i + 1]);
                if (strcmp(tok[i], "mesh") == 0) g->mesh = atoi(tok[i + 1]) > 0 ? atoi(tok[i + 1]) : 2;
            }
        } else if (strcmp(tok[0], "hide") == 0 && nt >= 2 && g_gl_nhide < 8) {
            snprintf(g_gl_hide[g_gl_nhide++], 17, "%s", tok[1]);
        } else if (g == NULL) {
            continue;
        } else if (strcmp(tok[0], "ink") == 0 && nt >= 4 && (unsigned char)tok[1][0] < 128) {
            g->ink[(unsigned char)tok[1][0]][0] = (unsigned char)atoi(tok[2]);
            g->ink[(unsigned char)tok[1][0]][1] = (unsigned char)atoi(tok[3]);
            g->inkset[(unsigned char)tok[1][0]] = 1;
        } else if (strcmp(tok[0], "draw") == 0 && nt >= 2 && tok[1][0] >= 'A' && tok[1][0] <= 'Z') {
            drawp = tok[1][0] - 'A';
            drawrow = 0;
            g->pc[drawp].drawn = 1;
        } else if (strcmp(tok[0], "map") == 0) {
            maprow = 0;
        } else if (strcmp(tok[0], "paint") == 0 && nt >= 4 && tok[1][0] >= 'A' && tok[1][0] <= 'Z') {
            paint = tok[1][0] - 'A';
            paintx = atoi(tok[2]);
            painty = atoi(tok[3]);
            maprow = 0;
        } else if (strcmp(tok[0], "fill") == 0 && nt >= 6 && tok[1][0] >= 'A' && tok[1][0] <= 'Z') {
            GlPiece *pc = &g->pc[tok[1][0] - 'A'];
            const int x = atoi(tok[2]), yv = atoi(tok[3]);

            if (pc->n < 256 && x >= 0 && yv >= 0 && x < GL_GRID && yv < GL_GRID) {
                pc->px[pc->n] = (unsigned char)x;
                pc->py[pc->n] = (unsigned char)yv;
                pc->sx[pc->n] = (unsigned char)atoi(tok[4]);
                pc->sy[pc->n] = (unsigned char)atoi(tok[5]);
                pc->n++;
            }
        } else if (strcmp(tok[0], "piece") == 0 && nt >= 2 && tok[1][0] >= 'A' && tok[1][0] <= 'Z') {
            GlPiece *pc = &g->pc[tok[1][0] - 'A'];

            for (i = 2; i < nt; i++) {
                if (strcmp(tok[i], "root") == 0 && i + 2 < nt) {
                    pc->rx = (float)atof(tok[i + 1]);
                    pc->ry = (float)atof(tok[i + 2]);
                    i += 2;
                } else if (strcmp(tok[i], "axis") == 0 && i + 1 < nt) {
                    pc->axis = (float)atof(tok[i + 1]) * D2R;
                    i++;
                }
            }
        } else if (strcmp(tok[0], "spot") == 0 && nt >= 3 && g->nspot < 8) {
            g->spot[g->nspot][0] = (float)atof(tok[1]);
            g->spot[g->nspot][1] = (float)atof(tok[2]);
            g->nspot++;
        } else if (strcmp(tok[0], "bush") == 0) {
            for (i = 1; i + 1 < nt; i += 2) {
                if (strcmp(tok[i], "pos") == 0) g->jpos = (float)atof(tok[i + 1]);
                if (strcmp(tok[i], "rot") == 0) g->jrot = (float)atof(tok[i + 1]);
                if (strcmp(tok[i], "size") == 0) g->jsize = (float)atof(tok[i + 1]);
            }
        } else if (strcmp(tok[0], "jitter") == 0) {
            for (i = 1; i + 1 < nt; i += 2) {
                if (strcmp(tok[i], "yaw") == 0) g->jyaw = (float)atof(tok[i + 1]);
                if (strcmp(tok[i], "elev") == 0) g->jelev = (float)atof(tok[i + 1]);
                if (strcmp(tok[i], "scale") == 0) g->jscale = (float)atof(tok[i + 1]);
            }
        } else if (strcmp(tok[0], "put") == 0 && nt >= 2 && tok[1][0] >= 'A' && tok[1][0] <= 'Z' && g->nput < GL_PUTS) {
            GlPut *u = &g->put[g->nput++];

            memset(u, 0, sizeof *u);
            u->piece = tok[1][0] - 'A';
            u->scale = u->width = u->shade = u->ao = 1.0f;
            u->rim = 0.72f;
            u->back = 0.94f;
            u->yaw = -1000.0f;                  /* (not given: the piece's own way in the picture) */
            for (i = 2; i + 1 < nt; i++) {
                const float v = (float)atof(tok[i + 1]);

                if (strcmp(tok[i], "at") == 0 && i + 3 < nt) {
                    u->at[0] = v;
                    u->at[1] = (float)atof(tok[i + 2]);
                    u->at[2] = (float)atof(tok[i + 3]);
                    i += 3;
                    continue;
                }
                if (strcmp(tok[i], "yaw") == 0) u->yaw = v;
                else if (strcmp(tok[i], "elev") == 0) u->elev = v;
                else if (strcmp(tok[i], "bend") == 0) u->bend = v;
                else if (strcmp(tok[i], "roll") == 0) u->roll = v;
                else if (strcmp(tok[i], "scale") == 0) u->scale = v;
                else if (strcmp(tok[i], "width") == 0) u->width = v;
                else if (strcmp(tok[i], "cup") == 0) u->cup = v;
                else if (strcmp(tok[i], "shade") == 0) u->shade = v;
                else if (strcmp(tok[i], "ao") == 0) u->ao = v;
                else if (strcmp(tok[i], "thick") == 0) u->thick = v;
                else if (strcmp(tok[i], "rim") == 0) u->rim = v;
                else if (strcmp(tok[i], "back") == 0) u->back = v;
                else if (strcmp(tok[i], "flip") == 0) u->flip = v != 0.0f;
                else continue;
                i++;
            }
        }
    }
    fclose(f);
    for (i = 0; i < g_ngl; i++) {
        gl_finish(g_gl[i]);
    }
    if (getenv("BMAP_GRASS_LOG") != NULL) {
        fprintf(stderr, "bmap grass layers: %d grasses, %d hidden sheets from %s\n", g_ngl, g_gl_nhide, path);
    }
}

int bmap_grass_count(void)
{
    gl_load();
    return g_ngl;
}

const BmapGrassKind *bmap_grass_find(const char *name)
{
    int i;

    gl_load();
    for (i = 0; i < g_ngl; i++) {
        if (strcmp(g_gl[i]->name, name) == 0) {
            return g_gl[i];
        }
    }
    return NULL;
}

int bmap_grass_hidden(const char *name)
{
    int i;

    gl_load();
    for (i = 0; i < g_gl_nhide; i++) {
        if (strcmp(g_gl_hide[i], name) == 0) {
            return 1;
        }
    }
    return 0;
}

int bmap_grass_over(const BmapGrassKind *g)
{
    return g->over;
}

void bmap_grass_atlas_name(const BmapGrassKind *g, char out[17])
{
    snprintf(out, 17, "%.15s~", g->name);
}

int bmap_grass_atlases(BmapScene *s)
{
    int gi;

    gl_load();
    for (gi = 0; gi < g_ngl; gi++) {
        const BmapGrassKind *g = g_gl[gi];
        const char *want = g->from[0] != '\0' ? g->from : g->name;
        const BmapTexture *src = NULL;
        BmapTexture *nt, *a;
        char an[17];
        unsigned k;
        int pi, q;

        bmap_grass_atlas_name(g, an);
        for (k = 0; k < s->nt; k++) {
            if (strcmp(s->t[k].name, want) == 0 && src == NULL) {
                src = &s->t[k];
            }
            if (strcmp(s->t[k].name, an) == 0) {
                break;                  /* (made already) */
            }
        }
        if (getenv("BMAP_GRASS_LOG") != NULL) {
            fprintf(stderr, "bmap grass atlas %s: picture %s %s, %u textures in the scene\n", an, want,
                    k < s->nt ? "atlas there already" : src == NULL ? "NOT in the scene" : "found", (unsigned)s->nt);
        }
        if (k < s->nt || src == NULL || src->rgba == NULL) {
            continue;
        }
        nt = (BmapTexture *)realloc(s->t, (s->nt + 1u) * sizeof *nt);
        if (nt == NULL) {
            return -1;
        }
        s->t = nt;
        for (k = 0; k < s->nt; k++) {           /* (the picture again: the list moved) */
            if (strcmp(s->t[k].name, want) == 0) {
                src = &s->t[k];
                break;
            }
        }
        a = &s->t[s->nt];
        memset(a, 0, sizeof *a);
        a->w = g->aw;
        a->h = g->ah;
        a->rgba = (unsigned char *)calloc((size_t)a->w * (size_t)a->h, 4u);
        if (a->rgba == NULL) {
            return -1;
        }
        snprintf(a->name, sizeof a->name, "%s", an);
        for (pi = 0; pi < 26; pi++) {
            const GlPiece *pc = &g->pc[pi];

            for (q = 0; q < pc->n; q++) {
                const int dx = pc->cellx + pc->px[q] - pc->bx0, dy = 1 + pc->py[q] - pc->by0;

                if (pc->sx[q] < src->w && pc->sy[q] < src->h && dx >= 0 && dy >= 0 && dx < a->w && dy < a->h) {
                    memcpy(&a->rgba[((size_t)dy * (size_t)a->w + (size_t)dx) * 4u],
                           &src->rgba[((size_t)pc->sy[q] * (size_t)src->w + pc->sx[q]) * 4u], 4u);
                }
            }
        }
        s->nt++;
    }
    return 0;
}

/* One leaf being built: the piece, its put and where the bush stands. */
typedef struct {
    const GlPiece *pc;
    const GlPut *u;
    float aax, aay, dhx, dhz, th0, kb, cr, sr, scale, at3[3];
    float ox, y, oz, pk;
    const float *mxv, *mzv;
} GlLeaf;

/* The battle point of a place (gx, gy) in the piece's grid on the leaf's upper side (F) and its under side (B);
 * returns how far along the leaf the place is (0 its root, 1 its tip). */
static float gl_corner(const GlLeaf *L, float gx, float gy, float F[3], float B[3])
{
    const GlPut *u = L->u;
    const float ddx = gx - L->pc->rx, ddy = gy - L->pc->ry;
    const float sp = ddx * L->aax + ddy * L->aay, sv = sp * L->scale;
    const float tv = (-ddx * L->aay + ddy * L->aax) * (u->flip ? -1.0f : 1.0f);
    float th = L->th0, rr, hh, nx, ny, nz, wx, wy, wz, n2x, n2y, n2z, lat, cupo, Q[3], fc;

    if (sv <= 0.0f || fabsf(L->kb) < 1e-5f) {
        rr = sv * cosf(L->th0);
        hh = sv * sinf(L->th0);
    } else {
        th = L->th0 - L->kb * sv;
        rr = (sinf(L->th0) - sinf(th)) / L->kb;
        hh = (cosf(th) - cosf(L->th0)) / L->kb;
    }
    nx = -L->dhx * sinf(th); ny = cosf(th); nz = -L->dhz * sinf(th);       /* (the leaf's upper side) */
    wx = -L->dhz * L->cr + nx * L->sr; wy = ny * L->sr; wz = L->dhx * L->cr + nz * L->sr;
    n2x = L->dhz * L->sr + nx * L->cr; n2y = ny * L->cr; n2z = -L->dhx * L->sr + nz * L->cr;
    lat = tv * L->scale * u->width;
    cupo = u->cup * fabsf(tv) * L->scale;
    Q[0] = L->at3[0] + L->dhx * rr + wx * lat + n2x * cupo;
    Q[1] = L->at3[1] + hh + wy * lat + n2y * cupo;
    Q[2] = L->at3[2] + L->dhz * rr + wz * lat + n2z * cupo;
    F[0] = L->ox + L->mxv[0] * Q[0] + L->mzv[0] * Q[2];
    F[2] = L->oz + L->mxv[1] * Q[0] + L->mzv[1] * Q[2];
    F[1] = L->y + Q[1] * L->pk;
    Q[0] -= n2x * u->thick;
    Q[1] -= n2y * u->thick;
    Q[2] -= n2z * u->thick;
    B[0] = L->ox + L->mxv[0] * Q[0] + L->mzv[0] * Q[2];
    B[2] = L->oz + L->mxv[1] * Q[0] + L->mzv[1] * Q[2];
    B[1] = L->y + Q[1] * L->pk;
    fc = sp / L->pc->smax;
    return fc < 0.0f ? 0.0f : fc > 1.0f ? 1.0f : fc;
}

/* a quad a b c d with its four places in the atlas */
static void gl_quad(BmapGrassEmit *emit, void *ctx, const float a[3], const float b[3], const float c[3], const float d[3],
                    const float uv[4][2], float shade)
{
    const float t1[3][3] = { { a[0], a[1], a[2] }, { b[0], b[1], b[2] }, { c[0], c[1], c[2] } };
    const float t2[3][3] = { { a[0], a[1], a[2] }, { c[0], c[1], c[2] }, { d[0], d[1], d[2] } };
    const float u1[3][2] = { { uv[0][0], uv[0][1] }, { uv[1][0], uv[1][1] }, { uv[2][0], uv[2][1] } };
    const float u2[3][2] = { { uv[0][0], uv[0][1] }, { uv[2][0], uv[2][1] }, { uv[3][0], uv[3][1] } };

    emit(ctx, t1, u1, shade);
    emit(ctx, t2, u2, shade);
}

void bmap_grass_tile(const BmapGrassKind *g, const BmapTexture *atlas, float ox, float y, float oz, const float mxv[2],
                     const float mzv[2], float pk, unsigned hash, BmapGrassEmit *emit, void *ctx)
{
    int pi, si;

    if (atlas == NULL || atlas->w <= 0 || atlas->h <= 0) {
        return;
    }
    for (si = 0; si < (g->nspot > 0 ? g->nspot : 1); si++) {
        /* the bush as a whole: its place, its turn, its size */
        const unsigned hb = (hash ^ ((unsigned)(si + 7) * 3266489917u)) * 668265263u;
        const float b1 = ((hb >> 3) % 1000) / 1000.0f - 0.5f, b2 = ((hb >> 12) % 1000) / 1000.0f - 0.5f;
        const float b3 = ((hb >> 20) % 1000) / 1000.0f - 0.5f, b4 = ((hb >> 7) % 1000) / 1000.0f - 0.5f;
        const float bphi = g->nspot > 0 ? b3 * 2.0f * g->jrot * D2R : 0.0f, bf = 1.0f + b4 * 2.0f * g->jsize;
        const float bcx = (g->nspot > 0 ? g->spot[si][0] : 0.0f) + b1 * 2.0f * g->jpos;
        const float bcz = (g->nspot > 0 ? g->spot[si][1] : 0.0f) + b2 * 2.0f * g->jpos;
        const float bcs = cosf(bphi), bsn = sinf(bphi);

        for (pi = 0; pi < g->nput; pi++) {
            const GlPut *u = &g->put[pi];
            const GlPiece *pc = &g->pc[u->piece];
            const unsigned hs = (hash ^ ((unsigned)(pi + 1 + si * 500) * 2246822519u)) * 2654435761u;
            const float r1 = ((hs >> 4) % 1000) / 1000.0f - 0.5f, r2 = ((hs >> 13) % 1000) / 1000.0f - 0.5f;
            const float r3 = ((hs >> 21) % 1000) / 1000.0f - 0.5f;
            const float yaw = bphi + (u->yaw < -999.0f ? pc->axis + r1 * 2.0f * g->jyaw * D2R
                                                       : (u->yaw + r1 * 2.0f * g->jyaw) * D2R);
            GlLeaf L;
            float ltot;
            int k;

            if (pc->n == 0) {
                continue;
            }
            L.pc = pc;
            L.u = u;
            L.at3[0] = bcx + (u->at[0] * bcs - u->at[2] * bsn) * bf;
            L.at3[1] = u->at[1] * bf;
            L.at3[2] = bcz + (u->at[0] * bsn + u->at[2] * bcs) * bf;
            L.th0 = (u->elev + r2 * 2.0f * g->jelev) * D2R;
            L.scale = u->scale * bf * (1.0f + r3 * 2.0f * g->jscale);
            L.aax = cosf(pc->axis);
            L.aay = sinf(pc->axis);
            L.dhx = cosf(yaw);
            L.dhz = sinf(yaw);
            ltot = pc->smax * L.scale;
            L.kb = u->bend * D2R / (ltot > 1e-3f ? ltot : 1e-3f);
            L.cr = cosf(u->roll * D2R);
            L.sr = sinf(u->roll * D2R);
            L.ox = ox;
            L.y = y;
            L.oz = oz;
            L.pk = pk;
            L.mxv = mxv;
            L.mzv = mzv;
            if (pc->drawn) {
                /* a drawn leaf: its picture laid over the leaf's surface, cut in strips across the leaf (every
                 * `mesh` rows) and along its middle (the fold of its cup).  The leaf runs along the picture's
                 * columns, so each strip half is flat: its pixels stay square. */
                const int step = g->mesh > 0 ? g->mesh : 2, bx1 = pc->bx0 + pc->bw, by1 = pc->by0 + pc->bh;
                float xm = pc->rx < (float)pc->bx0 ? (float)pc->bx0 : pc->rx > (float)bx1 ? (float)bx1 : pc->rx;
                int gy, h;

                for (gy = pc->by0; gy < by1; gy += step) {
                    const int y1 = gy + step < by1 ? gy + step : by1;

                    for (h = 0; h < 2; h++) {
                        const float x0 = h == 0 ? (float)pc->bx0 : xm, x1 = h == 0 ? xm : (float)bx1;
                        const float cx4[4] = { x0, x1, x1, x0 }, cy4[4] = { (float)gy, (float)gy, (float)y1, (float)y1 };
                        float F[4][3], B[4][3], uv[4][2], fa = 0.0f, lit;
                        int c;

                        if (x1 - x0 < 1e-3f) {
                            continue;
                        }
                        for (c = 0; c < 4; c++) {
                            fa += gl_corner(&L, cx4[c], cy4[c], F[c], B[c]) * 0.25f;
                            uv[c][0] = ((float)pc->cellx + cx4[c] - (float)pc->bx0) / (float)atlas->w;
                            uv[c][1] = (1.0f + cy4[c] - (float)pc->by0) / (float)atlas->h;
                        }
                        lit = u->shade * (u->ao + (1.0f - u->ao) * fa);
                        gl_quad(emit, ctx, F[0], F[1], F[2], F[3], (const float (*)[2])uv, lit);
                    }
                }
                continue;
            }
            {
                unsigned char mask[GL_GRID * GL_GRID];

                memset(mask, 0, sizeof mask);
                for (k = 0; k < pc->n; k++) {
                    mask[pc->py[k] * GL_GRID + pc->px[k]] = 1;
                }
                /* a piece cut from the picture: each pixel is a quad of its one colour (no picture stretched over a
                 * bent surface); a leaf with a thickness also has its under side and, where the piece ends, its rim */
                for (k = 0; k < pc->n; k++) {
                    static const int cx4[4] = { 0, 1, 1, 0 }, cy4[4] = { 0, 0, 1, 1 };
                    static const int nb[4][2] = { { 0, -1 }, { 1, 0 }, { 0, 1 }, { -1, 0 } };
                    const int gx = pc->px[k], gy = pc->py[k];
                    const float uc = ((float)(pc->cellx + gx - pc->bx0) + 0.5f) / (float)atlas->w;
                    const float vc = ((float)(1 + gy - pc->by0) + 0.5f) / (float)atlas->h;
                    const float uv[4][2] = { { uc, vc }, { uc, vc }, { uc, vc }, { uc, vc } };
                    float F[4][3], B[4][3], fa = 0.0f, lit;
                    int c, sd;

                    for (c = 0; c < 4; c++) {
                        fa += gl_corner(&L, (float)(gx + cx4[c]), (float)(gy + cy4[c]), F[c], B[c]) * 0.25f;
                    }
                    lit = u->shade * (u->ao + (1.0f - u->ao) * fa);
                    gl_quad(emit, ctx, F[0], F[1], F[2], F[3], uv, lit);
                    if (u->thick <= 0.0f) {
                        continue;
                    }
                    gl_quad(emit, ctx, B[0], B[1], B[2], B[3], uv, lit * u->back);
                    for (sd = 0; sd < 4; sd++) {
                        const int qx = gx + nb[sd][0], qy = gy + nb[sd][1], c0 = sd, c1 = (sd + 1) % 4;

                        if (qx >= 0 && qy >= 0 && qx < GL_GRID && qy < GL_GRID && mask[qy * GL_GRID + qx]) {
                            continue;
                        }
                        gl_quad(emit, ctx, B[c0], B[c1], F[c1], F[c0], uv, lit * u->rim);
                    }
                }
            }
        }
    }
}
