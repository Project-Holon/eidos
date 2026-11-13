/*
 * tools/rmrun.c -- runs the remade models' builders (engine/world/remade/)
 * without the game, for quick work on a builder.
 *
 *   rmrun IN_DIR OUT_DIR NAME...
 *
 * IN_DIR holds what the game gives each builder, written once by the game
 * with BMAP_REMADE_IN=dir (bmap_data.c, remade_make): NAME.rmi.  For each
 * NAME the builder the table names (engine/world/bmap_remade.c) runs with its
 * settings, and what it made is written to OUT_DIR/NAME.bmc, the record the
 * game writes with BMAP_REMADE_DUMP=dir: the same bytes.
 *
 * Build (any C compiler, the builders and the table only):
 *   gcc -O1 -I engine/world -I engine/world/remade tools/rmrun.c
 *       engine/world/bmap_remade.c engine/world/remade/[a-z]*.c -lm -o rmrun
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bmap_remade.h"
#include "remade.h"

/* (a read or a write that went wrong: where, and out) */
#define FAIL do { fprintf(stderr, "%s: rmrun.c line %d\n", name, __LINE__); goto bad; } while (0)

static int cmp_u32(const void *a, const void *b)
{
    unsigned x = *(const unsigned *)a, y = *(const unsigned *)b;

    return x < y ? -1 : x > y;
}

static int rd(void *p, size_t sz, size_t n, FILE *f)
{
    return fread(p, sz, n, f) == n;
}

static int run(const char *in, const char *outd, const char *name)
{
    char path[1024];
    FILE *f;
    unsigned n2[2], i, j, ntri = 0;
    RmTex *tx = NULL;
    RmBatch *bt = NULL;
    RmTri *tri = NULL;
    long at;
    RmOut o;
    const BmapRemade *e = bmap_remade_find(name);
    int ok = 0;

    memset(&o, 0, sizeof o);
    if (e == NULL) {
        fprintf(stderr, "%s: not in the table\n", name);
        return 1;
    }
    snprintf(path, sizeof path, "%s/%s.rmi", in, name);
    f = fopen(path, "rb");
    if (f == NULL) {
        fprintf(stderr, "%s: no %s (run the game once with BMAP_REMADE_IN)\n", name, path);
        return 1;
    }
    if (fread(path, 1, 4, f) != 4 || memcmp(path, "RMI1", 4) != 0 || !rd(n2, 4, 2, f)) {
        FAIL;
    }
    tx = (RmTex *)calloc(n2[1] + 1u, sizeof *tx);
    bt = (RmBatch *)calloc(n2[0] + 1u, sizeof *bt);
    if (tx == NULL || bt == NULL) {
        FAIL;
    }
    for (i = 0; i < n2[1]; i++) {
        int wh[2], has;

        if (!rd(wh, 4, 2, f) || !rd(&has, 4, 1, f)) {
            FAIL;
        }
        tx[i].w = wh[0];
        tx[i].h = wh[1];
        if (has) {
            unsigned char *px = (unsigned char *)malloc((size_t)wh[0] * wh[1] * 4u);

            if (px == NULL || !rd(px, 4, (size_t)wh[0] * wh[1], f)) {
                free(px);
                FAIL;
            }
            tx[i].rgba = px;
        }
    }
    /* (the triangles: counted first, then read into one list the batches point into, as the game has them) */
    at = ftell(f);
    for (i = 0; i < n2[0]; i++) {
        unsigned n;
        unsigned char skip[4 + 8 + 12];

        if (!rd(&n, 4, 1, f) || !rd(skip, 1, sizeof skip, f) || fseek(f, (long)n * 68L, SEEK_CUR) != 0) {
            FAIL;
        }
        ntri += n;
    }
    tri = (RmTri *)calloc(ntri + 1u, sizeof *tri);
    if (tri == NULL || fseek(f, at, SEEK_SET) != 0) {
        FAIL;
    }
    for (i = 0, ntri = 0; i < n2[0]; i++) {
        RmBatch *x = &bt[i];
        unsigned char f8[8];

        if (!rd(&x->n, 4, 1, f) || !rd(&x->tex, 4, 1, f) || !rd(f8, 1, 8, f) || !rd(x->pivot, 4, 3, f)) {
            FAIL;
        }
        x->cull = f8[0];
        x->bb = f8[1];
        memcpy(x->rgb, f8 + 2, 3);
        x->tri = &tri[ntri];
        for (j = 0; j < x->n; j++) {
            RmTri *q = &tri[ntri++];
            unsigned char c4[4];

            if (!rd(q->p, 4, 9, f) || !rd(q->uv, 4, 6, f) || !rd(&q->id, 4, 1, f) || !rd(c4, 1, 4, f)) {
                FAIL;
            }
            memcpy(q->rgb, c4, 3);
        }
    }
    fclose(f);
    f = NULL;
    e->build(bt, n2[0], tx, n2[1], e->opt, &o);
    fprintf(stderr, "remade %s: %u new, %u left out, %u turned, %u both sides, flags %u%s\n", name, o.ntri, o.ndrop,
            o.nturn, o.ntwo, o.flags, o.fail ? ", FAILED" : "");
    if (o.drop != NULL) qsort(o.drop, o.ndrop, sizeof *o.drop, cmp_u32);
    if (o.two != NULL) qsort(o.two, o.ntwo, sizeof *o.two, cmp_u32);
    if (o.turn != NULL) qsort(o.turn, o.nturn, sizeof *o.turn, cmp_u32);
    snprintf(path, sizeof path, "%s/%s.bmc", outd, name);
    f = fopen(path, "wb");
    if (f == NULL) {
        FAIL;
    }
    {
        unsigned n5[5] = { (unsigned)o.ntex, o.ntwo, o.ndrop, o.nturn, o.ntri };
        int k;

        fwrite("BMC2", 1, 4, f);
        fwrite(&o.flags, 4, 1, f);
        fwrite(n5, 4, 5, f);
        for (k = 0; k < o.ntex; k++) {
            int wh[2] = { o.tex[k].w, o.tex[k].h };
            unsigned char fl[4] = { 0, 0, 0, 0 };

            fwrite(wh, 4, 2, f);
            fwrite(fl, 1, 4, f);
            fwrite(o.tex[k].rgba, 4, (size_t)wh[0] * wh[1], f);
        }
        fwrite(o.two, 4, o.ntwo, f);
        fwrite(o.drop, 4, o.ndrop, f);
        fwrite(o.turn, sizeof(RmTurn), o.nturn, f);
        for (i = 0; i < o.ntri; i++) {
            const RmNew *x = &o.tri[i];
            unsigned char f8[8] = { x->cull, x->lit, x->rgb[0], x->rgb[1], x->rgb[2], x->kind, x->alpha, 0 };

            fwrite(&x->src, 4, 1, f);
            fwrite(&x->tex, 4, 1, f);
            fwrite(f8, 1, 8, f);
            fwrite(x->p, 4, 9, f);
            fwrite(x->uv, 4, 6, f);
            fwrite(x->n, 4, 3, f);
        }
    }
    ok = 1;
bad:
    if (f != NULL) {
        fclose(f);
    }
    if (!ok) {
        fprintf(stderr, "%s: failed\n", name);
    }
    for (i = 0; tx != NULL && i < n2[1]; i++) {
        free((void *)tx[i].rgba);
    }
    free(tx);
    free(bt);
    free(tri);
    return !ok;
}

int main(int argc, char **argv)
{
    int i, rc = 0;

    if (argc < 4) {
        fprintf(stderr, "usage: rmrun IN_DIR OUT_DIR NAME...\n");
        return 2;
    }
    for (i = 3; i < argc; i++) {
        rc |= run(argv[1], argv[2], argv[i]);
    }
    return rc;
}
