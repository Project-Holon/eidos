/*
 * engine/world/remade/mesh.c -- a map chunk's own mesh (a room: its floor, its
 * walls, the furniture drawn into it) remade piece by piece.
 *
 * A room's mesh is one model, made for the field's steep camera: a wall the
 * camera never faced is not there, a seat is a picture on the floor, a
 * console a card leaning back.  Each piece is the job of a builder the
 * standing models already have, so the settings are a list of builders, each
 * on a box of the room:
 *
 *     NAME[@X0:X1:Y0:Y1:Z0:Z1][|SETTINGS];NAME...
 *
 * NAME is tower, cushion, box or lower (the box's triangles moved down: `by`); the box (the mesh's own space) takes the
 * triangles wholly inside it (no box: the whole room); SETTINGS are that
 * builder's own, as it takes them for a standing model.  Each builder sees
 * only its box, so a cushion's picture is a cushion's picture, not a room.
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "remade.h"

#define MESH_PARTS 16

/* `lower|by=D`: the box's triangles moved down D units, each in its own picture and light (a table drawn into the
 * room standing on the floor the field showed, over a floor the mesh lays lower). */
static void rm_lower(const RmBatch *b, unsigned nb, const RmTex *tex, unsigned ntex, const char *opt, RmOut *out)
{
    const double by = rm_optf(opt, "by", 0.0);
    unsigned i, j;

    (void)tex;
    (void)ntex;
    if (by == 0.0) {
        return;
    }
    for (i = 0; i < nb; i++) {
        for (j = 0; j < b[i].n; j++) {
            const RmTri *t = &b[i].tri[j];
            double p[3][3], uv[3][2];
            unsigned first = out->ntri, k;
            int a;

            for (a = 0; a < 3; a++) {
                p[a][0] = t->p[a][0];
                p[a][1] = t->p[a][1] - by;
                p[a][2] = t->p[a][2];
                uv[a][0] = t->uv[a][0];
                uv[a][1] = t->uv[a][1];
            }
            {
                int cull = b[i].cull;

                for (k = 0; k < out->ntwo; k++) {
                    if (out->two[k] == t->id) {
                        cull = 0;       /* (shown from both sides: so is its copy) */
                    }
                }
                rm_tri(out, t->id, -1, cull, t->rgb, p[0], p[1], p[2], uv[0], uv[1], uv[2], 0, 0);
            }
            for (k = first; k < out->ntri; k++) {
                out->tri[k].lit = 1;
            }
            rm_drop(out, t->id);
        }
    }
}

static RmBuild *mesh_builder(const char *name)
{
    if (strcmp(name, "tower") == 0) {
        return rm_tower;
    }
    if (strcmp(name, "cushion") == 0) {
        return rm_cushion;
    }
    if (strcmp(name, "box") == 0) {
        return rm_box;
    }
    if (strcmp(name, "lower") == 0) {
        return rm_lower;
    }
    return NULL;
}

/* One builder on the triangles of the box `bx` (NULL: all of them). */
static void mesh_part(const RmBatch *b, unsigned nb, const RmTex *tex, unsigned ntex, RmBuild *fn, const double *bx,
                      const char *opt, RmOut *out)
{
    RmBatch *sb;
    RmTri *st;
    unsigned i, j, k, total = 0, nsb = 0, at = 0;

    if (bx == NULL) {
        fn(b, nb, tex, ntex, opt, out);
        return;
    }
    for (i = 0; i < nb; i++) {
        total += b[i].n;
    }
    sb = (RmBatch *)calloc(nb ? nb : 1u, sizeof *sb);
    st = (RmTri *)calloc(total ? total : 1u, sizeof *st);
    if (sb == NULL || st == NULL) {
        free(sb);
        free(st);
        out->fail = 1;
        return;
    }
    for (i = 0; i < nb; i++) {
        unsigned first = at;

        for (j = 0; j < b[i].n; j++) {
            const RmTri *t = &b[i].tri[j];
            int in = 1;

            for (k = 0; k < 3u && in; k++) {
                in = t->p[k][0] >= bx[0] - 0.05 && t->p[k][0] <= bx[1] + 0.05 && t->p[k][1] >= bx[2] - 0.05 &&
                     t->p[k][1] <= bx[3] + 0.05 && t->p[k][2] >= bx[4] - 0.05 && t->p[k][2] <= bx[5] + 0.05;
            }
            if (in) {
                st[at++] = *t;
            }
        }
        if (at > first) {
            sb[nsb] = b[i];
            sb[nsb].tri = &st[first];
            sb[nsb].n = at - first;
            nsb++;
        }
    }
    if (nsb > 0) {
        fn(sb, nsb, tex, ntex, opt, out);
    }
    free(sb);
    free(st);
}

void rm_mesh(const RmBatch *b, unsigned nb, const RmTex *tex, unsigned ntex, const char *opt, RmOut *out)
{
    const char *p = opt;

    while (p != NULL && *p != '\0') {
        const char *end = strchr(p, ';');
        size_t len = end != NULL ? (size_t)(end - p) : strlen(p);
        char part[2048], name[16], *set, *at;
        double bx[6];
        int has_box = 0;
        RmBuild *fn;

        if (len >= sizeof part) {
            len = sizeof part - 1u;
        }
        memcpy(part, p, len);
        part[len] = '\0';
        set = strchr(part, '|');
        if (set != NULL) {
            *set++ = '\0';
        }
        at = strchr(part, '@');
        if (at != NULL) {
            *at++ = '\0';
            has_box = sscanf(at, "%lf:%lf:%lf:%lf:%lf:%lf", &bx[0], &bx[1], &bx[2], &bx[3], &bx[4], &bx[5]) == 6;
        }
        snprintf(name, sizeof name, "%.15s", part);
        fn = mesh_builder(name);
        if (fn != NULL && (at == NULL || has_box)) {
            const unsigned made = out->ntri;

            mesh_part(b, nb, tex, ntex, fn, has_box ? bx : NULL, set != NULL ? set : "", out);
            if (fn == rm_lower && has_box) {
                /* (the parts the earlier builders made in the box go down with it: a bed's new side, its seal) */
                const double by = rm_optf(set != NULL ? set : "", "by", 0.0);
                unsigned i, k, a;

                for (i = 0; i < made; i++) {
                    RmNew *t = &out->tri[i];
                    int in = 1;

                    for (k = 0; k < 3u && in; k++) {
                        in = t->p[k][0] >= bx[0] - 0.05 && t->p[k][0] <= bx[1] + 0.05 && t->p[k][1] >= bx[2] - 0.05 &&
                             t->p[k][1] <= bx[3] + 0.05 && t->p[k][2] >= bx[4] - 0.05 && t->p[k][2] <= bx[5] + 0.05;
                    }
                    if (in) {
                        for (a = 0; a < 3u; a++) {
                            t->p[a][1] = (float)(t->p[a][1] - by);
                        }
                    }
                }
            }
        }
        p = end != NULL ? end + 1 : NULL;
    }
}
