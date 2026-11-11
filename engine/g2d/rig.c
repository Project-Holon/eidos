/*
 * engine/g2d/rig.c -- loading one poseable multi-part sprite bundle out
 * of a rig archive (a/0/0/4 and its siblings).
 *
 * A rig archive stores, per species/form index N, a fixed-size block of
 * sub-files (20 of them in a/0/0/4).  The members are NOT identified by a
 * fixed local-offset table: the block is scanned and every member is
 * classified by its own post-decompression magic, which is what lets the
 * same reader handle archives with different block layouts.  The rules,
 * the same ones aletheia's tools/assets/rig.py applies:
 *
 *   - the LARGEST RGCN member is the main tile sheet (a/0/0/4 duplicates
 *     both of its sheets, and the largest is always the main one; ties
 *     go to the lowest local index, which is the first copy);
 *   - the first RECN is the cell bank, the first RCMN the node table,
 *     the first RNAN the cell animation;
 *   - every RLCN is concatenated, in local order, into one palette-bank
 *     table, which is what an OAM's palette nibble indexes into.
 *
 * A member whose compression stream does not decode is skipped rather
 * than fatal.  The archive carries unused duplicates with corrupt or
 * truncated streams; letting one of those sink a block would lose
 * species that decode perfectly well from their real members.
 */

#include <stdlib.h>
#include <string.h>

#include "g2d.h"

#define FOURCC(a, b, c, d) \
    ((u32)(a) | ((u32)(b) << 8) | ((u32)(c) << 16) | ((u32)(d) << 24))

static int rig_own(G2dRig *rig, u8 *buf)
{
    u8 **grown = (u8 **)realloc(rig->owned, (rig->owned_count + 1) * sizeof(u8 *));

    if (grown == NULL) {
        free(buf);
        return -1;
    }
    rig->owned = grown;
    rig->owned[rig->owned_count++] = buf;
    return 0;
}

/*
 * One member's post-decompression bytes, owned by the rig.  Returns NULL
 * (with *size 0) when the member is unreadable or its container does not
 * decode: the caller treats that exactly as "unrecognized magic".
 */
static const u8 *rig_member(G2dRig *rig, BwArcFile *arc, u32 index, u32 *size)
{
    long raw_size = bw_arc_member_size(arc, index);
    u8 *raw;
    u8 *dec;
    u32 dec_size = 0;

    *size = 0;
    if (raw_size <= 0) {
        return NULL;
    }
    raw = (u8 *)malloc((size_t)raw_size);
    if (raw == NULL) {
        return NULL;
    }
    if (bw_arc_read_member(arc, index, raw, (u32)raw_size) != raw_size) {
        free(raw);
        return NULL;
    }
    dec = g2d_decompress(raw, (u32)raw_size, &dec_size);
    if (dec != NULL) {
        free(raw);
        if (rig_own(rig, dec) != 0) {
            return NULL;
        }
        *size = dec_size;
        return dec;
    }
    /*
     * No container this decoder handles.  If the first byte merely looked
     * like a compression tag, the member is used as it is; that is the
     * same outcome the asset tool reaches by catching its decoder's error.
     */
    if (rig_own(rig, raw) != 0) {
        return NULL;
    }
    *size = (u32)raw_size;
    return raw;
}

int g2d_rig_load(G2dRig *rig, BwArcFile *arc, u32 block_start, u32 block_size)
{
    const u8 *ncgr = NULL, *ncer = NULL, *nmcr = NULL, *nanr = NULL, *nmar = NULL;
    u32 ncgr_size = 0, ncer_size = 0, nmcr_size = 0, nanr_size = 0, nmar_size = 0;
    u32 local;

    memset(rig, 0, sizeof(*rig));

    for (local = 0; local < block_size; local++) {
        u32 index = block_start + local;
        u32 size = 0;
        const u8 *data;
        u32 magic;

        if (index >= arc->count) {
            break;
        }
        data = rig_member(rig, arc, index, &size);
        if (data == NULL || size < 4) {
            continue;
        }
        magic = bw_rd32(data);
        if (magic == FOURCC('R', 'G', 'C', 'N')) {
            if (size > ncgr_size) {         /* largest wins, first on a tie */
                ncgr = data;
                ncgr_size = size;
            }
        } else if (magic == FOURCC('R', 'E', 'C', 'N')) {
            if (ncer == NULL) { ncer = data; ncer_size = size; }
        } else if (magic == FOURCC('R', 'C', 'M', 'N')) {
            if (nmcr == NULL) { nmcr = data; nmcr_size = size; }
        } else if (magic == FOURCC('R', 'N', 'A', 'N')) {
            if (nanr == NULL) { nanr = data; nanr_size = size; }
        } else if (magic == FOURCC('R', 'A', 'M', 'N')) {
            if (nmar == NULL) { nmar = data; nmar_size = size; }
        } else if (magic == FOURCC('R', 'L', 'C', 'N')) {
            (void)g2d_palette_append_nclr(&rig->palette, data, size);
        }
    }

    if (ncgr == NULL || ncer == NULL || nmcr == NULL) {
        g2d_rig_free(rig);
        return -1;
    }
    if (g2d_ncgr_parse(&rig->ncgr, ncgr, ncgr_size) != 0
        || g2d_ncer_parse(&rig->ncer, ncer, ncer_size) != 0
        || g2d_nmcr_parse(&rig->nmcr, nmcr, nmcr_size) != 0) {
        g2d_rig_free(rig);
        return -1;
    }
    /* The animation is a refinement, not a requirement, for a bare cell
     * layout; without it no node resolves a cell and the pose is empty. */
    if (nanr != NULL && g2d_nanr_parse(&rig->nanr, nanr, nanr_size) == 0) {
        rig->has_nanr = 1;
    }
    /* The multi-cell animation: which multi-cell shows when.  Without it
     * g2d_rig_play shows multi-cell 0 only. */
    if (nmar != NULL && g2d_nmar_parse(&rig->nmar, nmar, nmar_size) == 0) {
        rig->has_nmar = 1;
    }
    return 0;
}

int g2d_rig_load_palette(G2dRig *rig, BwArcFile *arc, u32 index)
{
    u32 size = 0;
    const u8 *data = index < arc->count ? rig_member(rig, arc, index, &size) : NULL;

    if (data == NULL || size < 4 || bw_rd32(data) != FOURCC('R', 'L', 'C', 'N')) {
        return -1;
    }
    return g2d_palette_append_nclr(&rig->palette, data, size);
}

void g2d_rig_free(G2dRig *rig)
{
    u32 i;

    g2d_ncgr_free(&rig->ncgr);
    g2d_ncer_free(&rig->ncer);
    g2d_nmcr_free(&rig->nmcr);
    g2d_nanr_free(&rig->nanr);
    g2d_nanr_free(&rig->nmar);
    g2d_palette_free(&rig->palette);
    for (i = 0; i < rig->owned_count; i++) {
        free(rig->owned[i]);
    }
    free(rig->owned);
    rig->owned = NULL;
    rig->owned_count = 0;
    rig->has_nanr = 0;
    rig->has_nmar = 0;
}

/* ------------------------------------------------------------------ */
/* Playing a rig's multi-cell animation                                */
/* ------------------------------------------------------------------ */

/*
 * The animation controller, as the library runs it (aletheia's
 * nns_g2d_load.c NNS_G2dTickAnimCtrl, NNSi_G2dCellAnimationEdgeHandler and
 * nns_g2d_cell_animation.c NNS_G2dResetCellAnimation), at the speed the
 * game plays a Pokemon at (one tick a call, forward).  A frame of duration
 * 0 takes no time: the tick steps over it at once.  Play modes: 1 once
 * (stops on its last frame), 2 loop, 3 there and back once, 4 there and
 * back for ever.
 */
static u32 ctl_dur(const G2dNanr *bank, const G2dAnimCtl *c)
{
    return g2d_nanr_frame_duration(bank, c->seq, (u32)c->cur);
}

static int ctl_tick(const G2dNanr *bank, G2dAnimCtl *c, u32 ticks);

static void ctl_reset(const G2dNanr *bank, G2dAnimCtl *c)
{
    if (c->num == 0) {
        return;
    }
    c->cur = !c->reverse ? (s32)bank->sequences[c->seq].loop_start : (s32)c->num - 1;
    if (c->cur > (s32)c->num - 1) {
        c->cur = (s32)c->num - 1;
    }
    c->act = c->cur;
    c->time = 0;
    (void)ctl_tick(bank, c, 0);
}

static void ctl_stop_or_loop(const G2dNanr *bank, G2dAnimCtl *c, u32 mode)
{
    if (mode == 2 || mode == 4) {
        ctl_reset(bank, c);
    } else {
        c->active = 0;
    }
}

static void ctl_edge(const G2dNanr *bank, G2dAnimCtl *c)
{
    const G2dNanrSequence *s = &bank->sequences[c->seq];
    u32 mode = s->play_mode;

    if (mode == 3 || mode == 4) {
        c->reverse ^= 1;
        if (c->cur <= (s32)s->loop_start - 1) {
            ctl_stop_or_loop(bank, c, mode);
        }
    } else {
        ctl_stop_or_loop(bank, c, mode);
    }
    if (c->cur > (s32)c->num - 1) {
        c->cur = (s32)c->num - 1;
    } else if (c->cur < 0) {
        c->cur = 0;
    }
}

/* Returns whether the frame changed. */
static int ctl_tick(const G2dNanr *bank, G2dAnimCtl *c, u32 ticks)
{
    int changed = 0;

    if (!c->active || c->num == 0) {
        return 0;
    }
    if (ticks != 0) {
        c->spin = 0;                    /* a call from outside (a reset calls with 0) */
    }
    c->time += ticks;
    /* The guard is ours: a looping sequence whose frames all have duration
     * 0 would never leave this loop (the library's would not either). */
    while (c->active && c->time >= ctl_dur(bank, c)) {
        const G2dNanrSequence *s = &bank->sequences[c->seq];

        if (++c->spin > 4u * (u32)c->num + 8u) {
            c->active = 0;
            break;
        }
        changed = 1;
        c->time -= ctl_dur(bank, c);
        if (!c->reverse) {
            c->cur++;
            if (c->cur >= (s32)c->num) {
                ctl_edge(bank, c);
            }
        } else {
            c->cur--;
            if (c->cur <= (s32)s->loop_start - 1) {
                ctl_edge(bank, c);
            }
        }
        if (ctl_dur(bank, c) != 0) {
            c->act = c->cur;
        }
    }
    return changed;
}

/* NNS_G2dInitAnimCtrl and NNS_G2dSetCellAnimationBank: a fresh controller
 * on a sequence. */
static void ctl_init(G2dAnimCtl *c)
{
    memset(c, 0, sizeof(*c));
    c->active = 1;
}

static void ctl_set(const G2dNanr *bank, G2dAnimCtl *c, u32 seq)
{
    c->seq = (u16)seq;
    c->num = (bank != NULL && seq < bank->num_sequences) ? bank->sequences[seq].num_frames : 0;
    c->cur = 0;
    c->act = 0;
    c->time = 0;
    c->spin = 0;
    ctl_reset(bank, c);
}

static u32 ctl_total(const G2dNanr *bank, const G2dAnimCtl *c)
{
    u32 i, total = 0;

    for (i = 0; i < c->num; i++) {
        total += g2d_nanr_frame_duration(bank, c->seq, i);
    }
    return total;
}

/* NNSi_G2dSetMCDataToMCInstanceImpl (the shared-slot kind, the one mcss.c
 * asks for): every slot a node of the multi-cell names starts its node's
 * sequence again; the first node to name a slot decides it. */
static void play_bind(G2dRigPlay *p, u32 group)
{
    const G2dRig *rig = p->rig;
    const G2dNanr *bank = rig->has_nanr ? &rig->nanr : NULL;
    const G2dMcGroup *g;
    u32 i;

    if (group >= rig->nmcr.num_multi_cell) {
        return;
    }
    p->group = (u16)group;
    g = &rig->nmcr.groups[group];
    memset(p->inited, 0, sizeof(p->inited));
    for (i = 0; i < g->num_nodes; i++) {
        const G2dMcNode *node = &g->nodes[i];
        G2dAnimCtl *c = &p->cell[node->cell_anim_slot];

        if (p->inited[node->cell_anim_slot]) {
            continue;
        }
        p->inited[node->cell_anim_slot] = 1;
        ctl_set(bank, c, node->anim_seq_idx);
        c->active = 1;
        if ((node->node_attr & 0xFu) == 1u && c->num != 0) {
            /* the "continue" play mode: on to where the whole animation is */
            u32 len = ctl_total(bank, c);
            u32 mode = bank->sequences[c->seq].play_mode;

            if (mode == 2 || mode == 4) {
                if (len != 0) {
                    (void)ctl_tick(bank, c, p->total % len);
                }
            } else {
                (void)ctl_tick(bank, c, p->total >= len ? len : p->total);
            }
        }
    }
}

/* One NMAR frame of the multi-cell controller's sequence: the multi-cell
 * and its pose. */
static int play_mc_frame(const G2dRigPlay *p, s32 frame, G2dFrame *out)
{
    if (!p->rig->has_nmar || p->mc.num == 0
        || g2d_nanr_frame_data(&p->rig->nmar, p->mc.seq, (u32)frame, out) != 0) {
        memset(out, 0, sizeof(*out));
        out->sx = 4096;
        out->sy = 4096;
        return -1;
    }
    return 0;
}

void g2d_rig_play_start(G2dRigPlay *p, const G2dRig *rig, u32 seq)
{
    G2dFrame mf;
    u32 i;

    memset(p, 0, sizeof(*p));
    p->rig = rig;
    for (i = 0; i < G2D_RIG_SLOTS; i++) {
        ctl_init(&p->cell[i]);
    }
    ctl_init(&p->mc);
    /* NNS_G2dSetAnimSequenceToMCAnimation */
    ctl_set(rig->has_nmar ? &rig->nmar : NULL, &p->mc, seq);
    if (p->mc.num != 0 && g2d_nanr_frame_duration(&rig->nmar, p->mc.seq, (u32)p->mc.act) == 0) {
        return;                         /* nothing bound: as the library */
    }
    play_bind(p, play_mc_frame(p, p->mc.act, &mf) == 0 ? mf.cell_idx : 0u);
}

/* NNS_G2dTickMCAnimation, one tick. */
void g2d_rig_play_tick(G2dRigPlay *p)
{
    const G2dRig *rig = p->rig;
    const G2dNanr *bank = rig->has_nanr ? &rig->nanr : NULL;
    u32 i;

    p->t++;
    if (rig->has_nmar && p->mc.num != 0) {
        u32 step = ctl_dur(&rig->nmar, &p->mc);

        if (ctl_tick(&rig->nmar, &p->mc, 1)) {
            G2dFrame mf;

            p->total = (u16)(p->total + step);
            if (g2d_nanr_frame_duration(&rig->nmar, p->mc.seq, (u32)p->mc.act) != 0
                && play_mc_frame(p, p->mc.act, &mf) == 0) {
                play_bind(p, mf.cell_idx);
            }
            return;                     /* the cell animations wait this tick */
        }
    }
    if (p->group < rig->nmcr.num_multi_cell) {
        u32 n = rig->nmcr.groups[p->group].num_cell_anims;

        for (i = 0; i < n && i < G2D_RIG_SLOTS; i++) {
            (void)ctl_tick(bank, &p->cell[i], 1);
        }
    }
}

int g2d_rig_play_pose(const G2dRigPlay *p, G2dPose *out)
{
    const G2dRig *rig = p->rig;
    const G2dMcGroup *g;
    G2dNodeFrame *nf;
    G2dFrame mf;
    u32 group = p->group, i;
    int rc;

    memset(out, 0, sizeof(*out));
    /* mcss.c draws the frame the controller is on (its pCurrent) */
    if (play_mc_frame(p, p->mc.cur, &mf) == 0 && mf.cell_idx < rig->nmcr.num_multi_cell) {
        group = mf.cell_idx;
    }
    if (group >= rig->nmcr.num_multi_cell || rig->nmcr.groups[group].num_nodes == 0) {
        return -1;
    }
    g = &rig->nmcr.groups[group];
    nf = (G2dNodeFrame *)calloc(g->num_nodes, sizeof(G2dNodeFrame));
    if (nf == NULL) {
        return -1;
    }
    for (i = 0; i < g->num_nodes; i++) {
        const G2dAnimCtl *c = &p->cell[g->nodes[i].cell_anim_slot];

        nf[i].seq = c->seq;
        nf[i].frame = c->num != 0 ? (u16)c->cur : 0xFFFFu;      /* no sequence: no such frame, skipped */
    }
    rc = g2d_compose_pose_frames(&rig->ncgr, &rig->ncer, rig->has_nanr ? &rig->nanr : NULL, g, nf,
                                 rig->palette.count, out);
    free(nf);
    if (rc != 0) {
        return rc;
    }
    /* The animation frame's own pose over the whole picture (MCSS_Draw:
     * scaled and turned about the rig's origin, then moved). */
    if (!g2d_affine_is_identity(mf.sx, mf.sy, mf.rot_z) && out->width > 1) {
        G2dRaster dst;

        if (g2d_affine_resample(out->indices, out->width, out->height, out->ox, out->oy,
                                mf.sx, mf.sy, mf.rot_z, &dst) == 0) {
            free(out->indices);
            out->indices = dst.px;
            out->width = dst.w;
            out->height = dst.h;
            out->ox = dst.ox;
            out->oy = dst.oy;
        }
    }
    out->ox -= mf.px;
    out->oy -= mf.py;
    return 0;
}
