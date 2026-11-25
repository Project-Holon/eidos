/*
 * engine/avatar/avatar_field.c -- the other players' figures in the field,
 * each in that player's own look (avatar.h).
 *
 * The field keys a sprite's texture and palette by object code: every
 * figure with the player's code (1 Hilbert, 4 Hilda) shares one texture
 * and one palette, so all of them would show this game's own look.  So the
 * port adds private codes, 0x3000 and up, each with a sprite of its own:
 *
 *   - a/0/4/9 gets copies of the walking sprite (member 6 Hilbert, 9 Hilda)
 *     past its last member, each with its palette's entry 0 set to a tag.
 *     Entry 0 is the see-through colour: it is never drawn, so the copy
 *     looks the same, and the tag tells the recolour whose look it is;
 *   - a/0/4/8's parameter table gets a row per private code: the walking
 *     player's row, with the copy as its sprite;
 *   - FLDMMDL_ObjCodeToTableIndex finds those rows
 *     (stubs/reimpl_fldmmdl_objcode.c).
 *
 * Both go through the port's own file layer (sdk/vfs.h, root "") once, at
 * start-up, before any field opens the archives: a field keeps an archive
 * as it was when it opened it.  After that a slot only changes which look
 * its tag stands for.  8 slots a body: the players a field can show at
 * once (more players share the slots of the looks they have in common).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "avatar.h"
#include "avatar_int.h"
#include "port.h"
#include "vfs.h"

#define SLOTS_PER_BODY 8
#define SLOTS (2 * SLOTS_PER_BODY)
#define PRIVATE_CODE 0x3000u
#define TAG_BASE 0x7FE0u            /* entry 0 of a copy: TAG_BASE + slot   */
#define ROW 0x1Cu

/* stubs/reimpl_fldmmdl_objcode.c */
extern u32 g_bw_objcode_extra_base;
extern u32 g_bw_objcode_extra_count;

static struct {
    int used;
    u32 age;
    AvatarLook look;
} g_slot[SLOTS];
static u32 g_age;
static int g_registered;            /* 1 the copies are in place, -1 they could not be */

static u32 rd16(const u8 *p) { return (u32)p[0] | ((u32)p[1] << 8); }
static u32 rd32(const u8 *p) { return rd16(p) | (rd16(p + 2) << 16); }
static void wr16(u8 *p, u32 v) { p[0] = (u8)v; p[1] = (u8)(v >> 8); }

static u8 *read_member(const char *arc_path, u32 member, u32 *size, u32 *count)
{
    BwArcFile arc;
    long n;
    u8 *buf = NULL;

    if (bw_arc_open(&arc, arc_path) != 0) {
        return NULL;
    }
    if (count != NULL) {
        *count = arc.count;
    }
    n = member < arc.count ? bw_arc_member_size(&arc, member) : -1;
    if (n > 0 && (buf = (u8 *)malloc((size_t)n)) != NULL && bw_arc_read_member(&arc, member, buf, (u32)n) != n) {
        free(buf);
        buf = NULL;
    }
    bw_arc_close(&arc);
    *size = n > 0 ? (u32)n : 0;
    return buf;
}

/* Where a BTX0 file's first palette's colours are, or 0. */
static u32 btx_palette_at(const u8 *d, u32 n)
{
    u32 t = 0, i, info, data, num, p;

    if (n >= 4 && memcmp(d, "TEX0", 4) != 0) {
        u32 blocks = n >= 0x10 ? rd16(d + 0x0E) : 0;

        for (i = 0; i < blocks && 0x10 + 4 * i + 4 <= n; i++) {
            u32 o = rd32(d + 0x10 + 4 * i);

            if (o + 4 <= n && memcmp(d + o, "TEX0", 4) == 0) {
                t = o;
                break;
            }
        }
        if (t == 0) {
            return 0;
        }
    }
    if (t + 0x3C > n) {
        return 0;
    }
    info = t + rd32(d + t + 0x34);
    data = t + rd32(d + t + 0x38);
    if (info + 12 > n) {
        return 0;
    }
    num = d[info + 1];
    p = info + 12 + num * 4;
    if (num == 0 || p + 8 > n) {
        return 0;
    }
    i = data + (rd16(d + p + 4) << 3);
    return i + 32 <= n ? i : 0;
}

int avatar_field_register(void)
{
    static const u32 walk_member[2] = { 6, 9 };     /* Hilbert, Hilda */
    static const u32 walk_code[2] = { 1, 4 };
    static u32 seen;
    u32 tsize = 0, sprites = 0, rows, k;
    u8 *table, *grown;

    if (seen != bw_data_generation()) {
        /* The run went to the other version of the game (sdk/port.h,
         * bw_data_generation).  The grown table and the copied sprites were
         * made from the old game's archives and lie over the new game's:
         * they are taken away, so the two are read as they are and made
         * again, and no slot holds a look any more. */
        seen = bw_data_generation();
        if (g_registered != 0) {
            (void)bw_vfs_mem_clear("", "a/0/4/8", BW_VFS_MEM_ALL);
            (void)bw_vfs_mem_clear("", "a/0/4/9", BW_VFS_MEM_ALL);
            bw_vfs_flush_path("a/0/4/8");
            bw_vfs_flush_path("a/0/4/9");
            memset(g_slot, 0, sizeof g_slot);
            g_age = 0;
            g_registered = 0;
        }
    }
    if (g_registered != 0) {
        return g_registered > 0 ? 0 : -1;
    }
    g_registered = -1;
    table = read_member("a/0/4/8", 0, &tsize, NULL);
    if (table == NULL || tsize < 4 + ROW * 5) {
        free(table);
        return -1;
    }
    rows = (tsize - 4) / ROW;
    grown = (u8 *)malloc(tsize + ROW * SLOTS);
    if (grown == NULL) {
        free(table);
        return -1;
    }
    memcpy(grown, table, tsize);
    {
        u32 dummy = 0;
        u8 *probe = read_member("a/0/4/9", 0, &dummy, &sprites);

        free(probe);
    }
    for (k = 0; k < SLOTS; k++) {
        int body = k >= SLOTS_PER_BODY;
        u32 size = 0, pal, member = sprites + k;
        u8 *copy = read_member("a/0/4/9", walk_member[body], &size, NULL);
        u8 *row = grown + tsize + k * ROW;

        if (copy == NULL || (pal = btx_palette_at(copy, size)) == 0) {
            free(copy);
            free(table);
            free(grown);
            return -1;
        }
        /* the see-through colour carries the slot's tag */
        wr16(copy + pal, TAG_BASE + k);
        (void)bw_vfs_mem_set("", "a/0/4/9", member, copy, size);
        free(copy);
        /* the walking player's row, with this code and this sprite */
        memcpy(row, table + 4 + walk_code[body] * ROW, ROW);
        wr16(row + 0x00, PRIVATE_CODE + k);
        wr16(row + 0x10, member);
    }
    /* the table's first word is its row count */
    if (rd32(grown) == rows) {
        u32 n = rows + SLOTS;

        grown[0] = (u8)n;
        grown[1] = (u8)(n >> 8);
        grown[2] = (u8)(n >> 16);
        grown[3] = (u8)(n >> 24);
    }
    (void)bw_vfs_mem_set("", "a/0/4/8", 0, grown, tsize + ROW * SLOTS);
    bw_vfs_flush_path("a/0/4/8");
    bw_vfs_flush_path("a/0/4/9");
    free(table);
    free(grown);
    g_bw_objcode_extra_base = rows;
    g_bw_objcode_extra_count = SLOTS;
    g_registered = 1;
    if (getenv("EIDOS_AVATAR_LOG") != NULL) {
        fprintf(stderr, "eidos: avatar: %u private codes from 0x%X: rows %u.., sprites a/0/4/9 %u..\n",
                (unsigned)SLOTS, PRIVATE_CODE, (unsigned)rows, (unsigned)sprites);
    }
    return 0;
}

u32 avatar_field_code(const u8 look_bytes[5])
{
    AvatarLook look;
    int base, k, best = -1;

    avatar_look_from_bytes(look_bytes, &look);
    if (avatar_field_register() != 0) {
        return look.body ? 4u : 1u;         /* the game's own code */
    }
    base = look.body ? SLOTS_PER_BODY : 0;
    for (k = base; k < base + SLOTS_PER_BODY; k++) {
        if (g_slot[k].used && memcmp(&g_slot[k].look, &look, sizeof look) == 0) {
            g_slot[k].age = ++g_age;
            return PRIVATE_CODE + (u32)k;
        }
    }
    /* a free slot, else the one asked for longest ago */
    for (k = base; k < base + SLOTS_PER_BODY; k++) {
        if (!g_slot[k].used) {
            best = k;
            break;
        }
        if (best < 0 || g_slot[k].age < g_slot[best].age) {
            best = k;
        }
    }
    g_slot[best].used = 1;
    g_slot[best].look = look;
    g_slot[best].age = ++g_age;
    avatar_bump_version();                  /* its pictures are made again */
    return PRIVATE_CODE + (u32)best;
}

int avatar_field_is_code(u32 code)
{
    return code >= PRIVATE_CODE && code < PRIVATE_CODE + SLOTS;
}

int avatar_field_tag_look(u16 entry0, AvatarLook *out)
{
    u32 k = (u32)entry0 - TAG_BASE;

    if (entry0 < TAG_BASE || k >= SLOTS || !g_slot[k].used) {
        return 0;
    }
    *out = g_slot[k].look;
    return 1;
}
