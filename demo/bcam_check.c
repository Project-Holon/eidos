/*
 * demo/bcam_check.c -- checks the battle camera simulator (engine/world/bcam_sim.c)
 * against a recording of the game's camera.
 *
 * A recording is one line per game frame:
 *     frame ex ey ez tx ty tz [pSLOT x y z]...
 * in battle units.  The simulator's frames are aligned on the first frame the
 * camera leaves its start position, in the recording and in the simulation, and
 * compared from there.  The largest eye and target difference is printed, with
 * the first frame that differs by more than 0.01.
 *
 *   bcam_check <data root> <recording> <kind> <id> [from-frame [attacker defender]]
 *
 * A recording of "-" prints the simulator's frames instead.
 *
 * kind is move, wild, trainer or idle.  from-frame is the recording frame the
 * search for the start of the scene begins at (default 0).  attacker and
 * defender default to 0 and 1.
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bcam_sim.h"
#include "port.h"

typedef struct {
    int frame;
    float eye[3], target[3];
    float poke[8][3];
    unsigned mask;
} Rec;

static Rec *rec_load(const char *path, int *count)
{
    FILE *f = fopen(path, "r");
    char line[1024];
    Rec *r = NULL;
    int n = 0, cap = 0;

    if (!f)
        return NULL;
    while (fgets(line, sizeof(line), f)) {
        Rec x;
        char *p = line;
        int used = 0;

        memset(&x, 0, sizeof(x));
        if (sscanf(p, "%d %f %f %f %f %f %f%n", &x.frame, &x.eye[0], &x.eye[1], &x.eye[2],
                   &x.target[0], &x.target[1], &x.target[2], &used) < 7)
            continue;
        p += used;
        for (;;) {
            int slot, u = 0;
            float a, b, c;

            if (sscanf(p, " p%d %f %f %f%n", &slot, &a, &b, &c, &u) < 4)
                break;
            p += u;
            if (slot >= 0 && slot < 8) {
                x.poke[slot][0] = a;
                x.poke[slot][1] = b;
                x.poke[slot][2] = c;
                x.mask |= 1u << slot;
            }
        }
        if (n == cap) {
            cap = cap ? cap * 2 : 1024;
            r = (Rec *)realloc(r, (size_t)cap * sizeof(Rec));
        }
        r[n++] = x;
    }
    fclose(f);
    *count = n;
    return r;
}

static int same(const float *a, const float *b)
{
    return fabsf(a[0] - b[0]) < 0.0006f && fabsf(a[1] - b[1]) < 0.0006f && fabsf(a[2] - b[2]) < 0.0006f;
}

/* First index >= from whose camera differs from the previous frame. */
static int rec_start(const Rec *r, int n, int from)
{
    int i;

    for (i = from + 1; i < n; i++)
        if (!same(r[i].eye, r[i - 1].eye) || !same(r[i].target, r[i - 1].target))
            return i;
    return -1;
}

static int sim_start(const BcamFrame *f, int n, int chained)
{
    static const float def_eye[3] = { 6.7f, 6.7f, 17.3f }, def_tgt[3] = { 0.0f, 2.6f, 0.0f };
    int i;

    if (!chained && n > 0 && (!same(f[0].eye, def_eye) || !same(f[0].target, def_tgt)))
        return 0;
    for (i = 1; i < n; i++)
        if (memcmp(f[i].eye, f[i - 1].eye, sizeof(f[i].eye)) || memcmp(f[i].target, f[i - 1].target, sizeof(f[i].target)))
            return i;
    return -1;
}

int main(int argc, char **argv)
{
    int kind, id, from = 0, att = 0, def = 1;
    int nrec = 0, nsim = 0, rs, ss, i, len, first_bad = -1, skip = 0, compared = 0;
    float me = 0, mt = 0, mp = 0;
    Rec *rec;
    BcamFrame *sim = NULL;
    int rc;

    if (argc < 5) {
        fprintf(stderr, "usage: bcam_check <data root> <recording> move|wild|trainer|idle|switch|ball <id> [from [att def]]\n");
        return 2;
    }
    bw_set_data_root(argv[1]);
    if (!strcmp(argv[3], "name")) {
        const char *n = bcam_sim_move_name(atoi(argv[4]));

        printf("move %s: %s\n", argv[4], n ? n : "(none)");
        return n ? 0 : 1;
    }
    if (!strcmp(argv[3], "move"))         kind = BCAM_MOVE;
    else if (!strcmp(argv[3], "wild"))    kind = BCAM_WILD_INTRO;
    else if (!strcmp(argv[3], "trainer")) kind = BCAM_TRAINER_INTRO;
    else if (!strcmp(argv[3], "switch"))  kind = BCAM_SWITCH;
    else if (!strcmp(argv[3], "ball"))    kind = BCAM_BALL;
    else                                  kind = BCAM_IDLE;
    id = atoi(argv[4]);
    if (argc > 5) from = atoi(argv[5]);
    if (argc > 6) att = atoi(argv[6]);
    if (argc > 7) def = atoi(argv[7]);

    if (getenv("BCAM_WEIGHT")) {
        bcam_sim_set_weight(0, atoi(getenv("BCAM_WEIGHT")));
        bcam_sim_set_weight(1, atoi(getenv("BCAM_WEIGHT")));
    }
    if (!strcmp(argv[2], "-")) {
        /* no recording: print the simulator's frames */
        int i;

        if (bcam_sim_run(kind, id, att, def, &sim, &nsim) != 0) {
            return 1;
        }
        for (i = 0; i < nsim; i++) {
            int k;

            printf("%d %.3f %.3f %.3f %.3f %.3f %.3f", i, sim[i].eye[0], sim[i].eye[1], sim[i].eye[2],
                   sim[i].target[0], sim[i].target[1], sim[i].target[2]);
            /* each sprite on the field: slot, size, alpha, fade, animation, hidden */
            for (k = 0; k < 14; k++) {
                if (k < 8 ? sim[i].poke_mask >> k & 1u : sim[i].trainer_mask >> (k - 8) & 1u) {
                    printf(" s%d %.3f a%d f%d m%d/%d%s", k, sim[i].size[k], sim[i].alpha[k], sim[i].fade[k],
                           sim[i].anim[k], sim[i].anim_t[k], sim[i].vanish_mask >> k & 1u ? " hidden" : "");
                }
            }
            putchar('\n');
        }
        return 0;
    }
    rec = rec_load(argv[2], &nrec);
    if (!rec) {
        fprintf(stderr, "cannot read %s\n", argv[2]);
        return 1;
    }
    rc = bcam_sim_run(kind, id, att, def, &sim, &nsim);
    if (rc != 0) {
        fprintf(stderr, "bcam_sim_run failed (%d)\n", rc);
        return 1;
    }
    {
        int k;

        for (k = 0; k < nrec && rec[k].frame < from; k++)
            ;
        from = k;
    }
    rs = rec_start(rec, nrec, from);
    ss = sim_start(sim, nsim, kind != BCAM_MOVE && id > 0);
    if (rs < 0 || ss < 0) {
        printf("no camera movement (recording %d, simulation %d)\n", rs, ss);
        return 1;
    }
    if (getenv("BCAM_ALIGN") && rs > 0) {
        /* a scene that was already running when the recording began: find the simulated frame that
         * equals the recording's frame before the first change, and continue from the next one */
        int k, found = -1;

        for (k = 0; k + 1 < nsim; k++)
            if (same(sim[k].eye, rec[rs - 1].eye) && same(sim[k].target, rec[rs - 1].target)
                && same(sim[k + 1].eye, rec[rs].eye) && same(sim[k + 1].target, rec[rs].target)) {
                found = k + 1;
                break;
            }
        if (found < 0) {
            printf("no frame of the simulation matches the start of the recording\n");
            return 1;
        }
        ss = found;
    }
    len = nsim - ss;
    for (i = 0; i < len; i++) {
        const Rec *a;
        const BcamFrame *b = &sim[ss + i];
        float e = 0, t = 0, p = 0;
        int k, s;

        /* the recording numbers its frames: a gap in them is skipped, not compared */
        while (rs + i + skip < nrec && rec[rs + i + skip].frame < rec[rs].frame + i)
            skip++;
        if (rs + i + skip >= nrec)
            break;
        if (rec[rs + i + skip].frame != rec[rs].frame + i)
            continue;
        a = &rec[rs + i + skip];
        compared++;

        for (k = 0; k < 3; k++) {
            float d = fabsf(a->eye[k] - b->eye[k]);
            float g = fabsf(a->target[k] - b->target[k]);

            if (d > e) e = d;
            if (g > t) t = g;
        }
        for (s = 0; s < 2; s++) {
            if ((a->mask & (1u << s)) && (b->poke_mask & (1u << s)))
                for (k = 0; k < 3; k++) {
                    float d = fabsf(a->poke[s][k] - b->poke[s][k]);

                    if (d > p) p = d;
                }
        }
        if (e > me) me = e;
        if (t > mt) mt = t;
        if (p > mp) mp = p;
        if (first_bad < 0 && (e > 0.01f || t > 0.01f))
            first_bad = i;
        if (getenv("BCAM_DUMP") && i >= atoi(getenv("BCAM_DUMP")) && i < atoi(getenv("BCAM_DUMP")) + 12)
            printf("  %d rec %d  eye %.3f %.3f %.3f | sim %.3f %.3f %.3f  tgt %.3f %.3f %.3f | sim %.3f %.3f %.3f\n",
                   i, a->frame, a->eye[0], a->eye[1], a->eye[2], b->eye[0], b->eye[1], b->eye[2],
                   a->target[0], a->target[1], a->target[2], b->target[0], b->target[1], b->target[2]);
    }
    printf("%s %d: start rec %d sim %d, %d frames compared (sim %d)  eye %.4f target %.4f poke %.4f  first >0.01: %d\n",
           argv[3], id, rec[rs].frame, ss, compared, nsim - ss, me, mt, mp, first_bad);
    free(rec);
    free(sim);
    return 0;
}
