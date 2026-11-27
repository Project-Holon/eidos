/*
 * tests/test_vfs_members.c -- member-level NARC overrides and the mod list.
 *
 * The synthetic half needs no game data.  It writes a base tree and several
 * mod trees under the current directory (the ctest build directory), each
 * archive built by bw_narc_pack, and for every mod combination checks the
 * archive the game would see three independent ways:
 *
 *   - sdk/narc.c's reader (bw_arc_open, bw_arc_member_size,
 *     bw_arc_read_member) plus the matched ARC_TOOL_GetHandleMemberOffset on the same handle;
 *   - the matched readers by archive id (ARC_TOOL_OpenArchive + ARC_TOOL_SeekMember, then
 *     ARC_TOOL_ReadMember), over a synthetic ARM9 name table;
 *   - FS_OpenFile of the archive itself, whose whole byte image must equal a
 *     NARC packed from the expected member list.
 *
 * Cases: no mods; an empty mod and a member directory holding only
 * non-member names; one mod replacing a member and adding members past the
 * end (with a hole); two mods overriding the same member in both load
 * orders; a manifest's `order` and `name`; a whole-file replacement
 * superseding earlier member directories and a later member directory
 * applying on top of it; two files naming one index in one mod; member
 * files over a base that is not a NARC; an open file keeping its merged
 * image across a mod list change; the ';' list form.
 *
 * The data half (argv[1] is the data root; skipped without one) repacks
 * every real archive with bw_narc_pack and requires the original bytes
 * back, then overrides one member of a real archive and adds one past its
 * end, and reads both through the game's own path: the real ARM9 name
 * table, ARC_TOOL_OpenArchive/ARC_TOOL_SeekMember/ARC_TOOL_ReadMember, by file id and by ROM
 * range.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "port.h"
#include "fs.h"
#include "vfs.h"
#include "vfs_msg.h"
#include "port_test.h"

int bw_msg_bank_get(const u8 *bank, u32 size, u32 entry, u32 block, u16 *out, u32 max_units);

#ifdef _WIN32
#include <direct.h>
#define MKDIR(p) _mkdir(p)
#define RMDIR(p) _rmdir(p)
#else
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#define MKDIR(p) mkdir((p), 0777)
#define RMDIR(p) rmdir(p)
#endif

/* BW_REQUIRE for a void function. */
#define BW_REQUIRE_VOID(cond)                                              \
    do {                                                                   \
        bw_checks++;                                                       \
        if (!(cond)) {                                                     \
            bw_failures++;                                                 \
            printf("  FAIL %s\n", #cond);                                  \
            return;                                                        \
        }                                                                  \
    } while (0)

struct ArcHandle {
    u8 raw[BW_FSFILE_SIZE];
};

extern void ARC_TOOL_OpenArchive(struct ArcHandle *arc, u32 id);
extern u32  ARC_TOOL_SeekMember(struct ArcHandle *arc, u32 member, u32 base, u32 size);
extern void ARC_TOOL_ReadMember(void *dst, u32 archive_id, u32 member);
extern u32  ARC_TOOL_GetHandleMemberOffset(BwArcFile *arc, u32 member);
extern BOOL FS_OpenFile(void *file, const char *path);
extern BOOL FS_OpenFileDirect(void *file, void *arc, u32 top, u32 bottom, u32 id);
extern BOOL FS_SeekFile(void *file, s32 offset, u32 origin);
extern s32  FS_ReadFile(void *file, void *dst, s32 len);
extern u32  FS_GetFileLength(void *file);
extern BOOL FS_CloseFile(void *file);

static void put32(u8 *p, u32 v)
{
    p[0] = (u8)v;
    p[1] = (u8)(v >> 8);
    p[2] = (u8)(v >> 16);
    p[3] = (u8)(v >> 24);
}

/* ------------------------------------------------------------------ */
/* Members as (length, seed) and the files that hold them              */
/* ------------------------------------------------------------------ */

typedef struct Mem {
    u32 len;
    u8  seed;
} Mem;

static u8 mbyte(u8 seed, u32 i)
{
    return (u8)(seed * 31u + i * 7u + 1u);
}

static u8 *mem_bytes(Mem m)
{
    u8 *p = (u8 *)malloc(m.len + 1);
    u32 i;

    for (i = 0; i < m.len; i++) {
        p[i] = mbyte(m.seed, i);
    }
    return p;
}

/* Pack a member list with bw_narc_pack; the caller frees. */
static u8 *pack(const Mem *m, u32 n, u32 *size)
{
    const u8 *data[16];
    u8 *own[16];
    u32 sizes[16];
    u8 *out = NULL;
    u32 i;

    for (i = 0; i < n; i++) {
        own[i] = mem_bytes(m[i]);
        data[i] = own[i];
        sizes[i] = m[i].len;
    }
    if (bw_narc_pack(data, sizes, n, NULL, 0, &out, size) != 0) {
        out = NULL;
    }
    for (i = 0; i < n; i++) {
        free(own[i]);
    }
    return out;
}

static int write_bytes(const char *path, const void *p, u32 n)
{
    FILE *fp = fopen(path, "wb");

    if (fp == NULL) {
        return -1;
    }
    if (n != 0) {
        fwrite(p, 1, n, fp);
    }
    fclose(fp);
    return 0;
}

static int write_text(const char *path, const char *text)
{
    return write_bytes(path, text, (u32)strlen(text));
}

static int write_mem(const char *path, Mem m)
{
    u8 *p = mem_bytes(m);
    int rc = write_bytes(path, p, m.len);

    free(p);
    return rc;
}

static int write_narc(const char *path, const Mem *m, u32 n)
{
    u32 size;
    u8 *img = pack(m, n, &size);
    int rc;

    if (img == NULL) {
        return -1;
    }
    rc = write_bytes(path, img, size);
    free(img);
    return rc;
}

/* ------------------------------------------------------------------ */
/* The tree                                                            */
/* ------------------------------------------------------------------ */

#define R     "vfsm_tmp"
#define BASE  R "/base"
#define MODS  R "/mods"

static const char *const DIRS[] = {
    R, BASE, BASE "/a", BASE "/a/0", BASE "/a/0/0",
    MODS,
    MODS "/m1", MODS "/m1/a", MODS "/m1/a/0", MODS "/m1/a/0/0", MODS "/m1/a/0/0/0",
    MODS "/m2", MODS "/m2/a", MODS "/m2/a/0", MODS "/m2/a/0/0", MODS "/m2/a/0/0/0",
    MODS "/pri", MODS "/pri/a", MODS "/pri/a/0", MODS "/pri/a/0/0", MODS "/pri/a/0/0/0",
    MODS "/empty", MODS "/empty/a", MODS "/empty/a/0", MODS "/empty/a/0/0",
    MODS "/empty/a/0/0/0",
    MODS "/whole", MODS "/whole/a", MODS "/whole/a/0", MODS "/whole/a/0/0",
    MODS "/dup", MODS "/dup/a", MODS "/dup/a/0", MODS "/dup/a/0/0", MODS "/dup/a/0/0/0",
    MODS "/bad", MODS "/bad/a", MODS "/bad/a/0", MODS "/bad/a/0/0", MODS "/bad/a/0/0/2",
};

static const char *const FILES[] = {
    BASE "/a/0/0/0", BASE "/a/0/0/1", BASE "/a/0/0/2",
    MODS "/m1/eidos_mod.txt", MODS "/m1/a/0/0/0/1.bin", MODS "/m1/a/0/0/0/0004.dat",
    MODS "/m1/a/0/0/0/README.txt", MODS "/m1/a/0/0/0/2.bin.bak",
    MODS "/m2/a/0/0/0/1", MODS "/m2/a/0/0/0/5.bin",
    MODS "/pri/eidos_mod.txt", MODS "/pri/a/0/0/0/1.nclr",
    MODS "/empty/a/0/0/0/README.txt",
    MODS "/whole/a/0/0/0",
    MODS "/dup/a/0/0/0/2.bin", MODS "/dup/a/0/0/0/02.dat",
    MODS "/bad/a/0/0/2/0.bin",
};

static void clean_tree(void)
{
    size_t i;

    for (i = 0; i < sizeof(FILES) / sizeof(FILES[0]); i++) {
        remove(FILES[i]);
    }
    for (i = sizeof(DIRS) / sizeof(DIRS[0]); i > 0; i--) {
        RMDIR(DIRS[i - 1]);
    }
}

/* The base: a/0/0/0 has three members (one empty), a/0/0/1 two, a/0/0/2
 * is a raw file. */
static const Mem B0[] = { { 16, 1 }, { 5, 2 }, { 0, 3 } };
static const Mem B1[] = { { 8, 10 }, { 12, 11 } };
static const Mem W0[] = { { 20, 70 }, { 6, 71 } };    /* mods/whole */
static const Mem M1_1 = { 7, 50 };
static const Mem M1_4 = { 9, 51 };
static const Mem M2_1 = { 3, 60 };
static const Mem M2_5 = { 2, 61 };
static const Mem PRI_1 = { 33, 80 };
static const Mem EMPTY = { 0, 0 };

static int build_tree(void)
{
    size_t i;
    int bad = 0;

    clean_tree();
    for (i = 0; i < sizeof(DIRS) / sizeof(DIRS[0]); i++) {
        MKDIR(DIRS[i]);
    }
    bad |= write_narc(BASE "/a/0/0/0", B0, 3);
    bad |= write_narc(BASE "/a/0/0/1", B1, 2);
    bad |= write_bytes(BASE "/a/0/0/2", "not a narc", 10);

    bad |= write_text(MODS "/m1/eidos_mod.txt", "# first\nname = First mod\n");
    bad |= write_mem(MODS "/m1/a/0/0/0/1.bin", M1_1);
    bad |= write_mem(MODS "/m1/a/0/0/0/0004.dat", M1_4);
    bad |= write_bytes(MODS "/m1/a/0/0/0/README.txt", "notes", 5);
    bad |= write_bytes(MODS "/m1/a/0/0/0/2.bin.bak", "old", 3);

    bad |= write_mem(MODS "/m2/a/0/0/0/1", M2_1);
    bad |= write_mem(MODS "/m2/a/0/0/0/5.bin", M2_5);

    bad |= write_text(MODS "/pri/eidos_mod.txt",
                      "order = 10   # load last\r\nname = Priority\r\n");
    bad |= write_mem(MODS "/pri/a/0/0/0/1.nclr", PRI_1);

    bad |= write_bytes(MODS "/empty/a/0/0/0/README.txt", "nothing", 7);

    bad |= write_narc(MODS "/whole/a/0/0/0", W0, 2);

    bad |= write_bytes(MODS "/dup/a/0/0/0/2.bin", "x", 1);
    bad |= write_bytes(MODS "/dup/a/0/0/0/02.dat", "y", 1);

    bad |= write_bytes(MODS "/bad/a/0/0/2/0.bin", "z", 1);
    return bad ? -1 : 0;
}

/* A synthetic ARM9 image: two path strings and the two-entry table. */
#define IMAGE_SIZE 0x200u
#define STR_OFF    0x40u
#define TABLE_OFF  0x100u

static u8 g_image[IMAGE_SIZE];

static void build_image(void)
{
    memset(g_image, 0, sizeof(g_image));
    memcpy(g_image + STR_OFF, "a/0/0/0", 8);
    memcpy(g_image + STR_OFF + 8, "a/0/0/1", 8);
    put32(g_image + TABLE_OFF, BW_ARM9_LOAD_ADDRESS + STR_OFF);
    put32(g_image + TABLE_OFF + 4, BW_ARM9_LOAD_ADDRESS + STR_OFF + 8);
}

/* ------------------------------------------------------------------ */
/* The three views                                                     */
/* ------------------------------------------------------------------ */

static int same_mem(const u8 *got, Mem m)
{
    u32 i;

    for (i = 0; i < m.len; i++) {
        if (got[i] != mbyte(m.seed, i)) {
            return 0;
        }
    }
    return 1;
}

/*
 * Check archive `rel` (archive id `id` in the synthetic table) against the
 * expected member list, every way the game and the port read it.
 */
static void check_archive(const char *what, const char *rel, u32 id,
                          const Mem *want, u32 n)
{
    static u8 buf[0x400];
    BwArcFile arc;
    struct ArcHandle h;
    u8 file[BW_FSFILE_SIZE];
    u8 *img;
    u8 *got;
    u32 img_size;
    u32 len;
    u32 i;
    int ok;

    printf("%s\n", what);

    /* 1. sdk/narc.c, and ARC_TOOL_GetHandleMemberOffset on the same handle. */
    if (bw_arc_open(&arc, rel) != 0) {
        BW_CHECK_EQ("bw_arc_open", -1, 0);
        return;
    }
    BW_CHECK_EQ("member count", arc.count, n);
    for (i = 0; i < n && i < arc.count; i++) {
        u32 pos;

        BW_CHECK_EQ("bw_arc_member_size", bw_arc_member_size(&arc, i), want[i].len);
        BW_CHECK_EQ("bw_arc_read_member",
                    bw_arc_read_member(&arc, i, buf, sizeof(buf)), want[i].len);
        BW_CHECK_EQ("bw_arc_read_member bytes", same_mem(buf, want[i]), 1);
        pos = ARC_TOOL_GetHandleMemberOffset(&arc, i);
        ok = FS_SeekFile(arc.file, (s32)pos, 0) &&
             FS_ReadFile(arc.file, buf, (s32)want[i].len) == (s32)want[i].len &&
             same_mem(buf, want[i]);
        BW_CHECK_EQ("ARC_TOOL_GetHandleMemberOffset position reads the member", ok, 1);
    }
    bw_arc_close(&arc);

    /* 2. The matched readers by archive id. */
    for (i = 0; i < n; i++) {
        ARC_TOOL_OpenArchive(&h, id);
        len = ARC_TOOL_SeekMember(&h, i, 0, 0);
        BW_CHECK_EQ("ARC_TOOL_SeekMember size", len, want[i].len);
        ok = FS_ReadFile(h.raw, buf, (s32)len) == (s32)len && same_mem(buf, want[i]);
        BW_CHECK_EQ("ARC_TOOL_SeekMember then FS_ReadFile", ok, 1);
        FS_CloseFile(h.raw);

        memset(buf, 0xAA, sizeof(buf));
        ARC_TOOL_ReadMember(buf, id, i);
        BW_CHECK_EQ("ARC_TOOL_ReadMember bytes", same_mem(buf, want[i]), 1);
    }

    /* 3. The archive file itself, byte for byte. */
    img = pack(want, n, &img_size);
    BW_REQUIRE_VOID(img != NULL);
    if (!FS_OpenFile(file, rel)) {
        BW_CHECK_EQ("FS_OpenFile", 0, 1);
        free(img);
        return;
    }
    BW_CHECK_EQ("FS_GetFileLength is the merged size", FS_GetFileLength(file), img_size);
    got = (u8 *)malloc(img_size);
    ok = FS_ReadFile(file, got, (s32)img_size) == (s32)img_size &&
         memcmp(got, img, img_size) == 0;
    BW_CHECK_EQ("the archive's bytes are the packed expectation", ok, 1);
    FS_CloseFile(file);
    free(got);
    free(img);
}

/* ------------------------------------------------------------------ */
/* Synthetic cases                                                     */
/* ------------------------------------------------------------------ */

static int synthetic(void)
{
    const u8 *data;
    u32 size;
    u32 handle;
    u8 file[BW_FSFILE_SIZE];
    BwArcFile a;
    BwArcFile b;
    static u8 buf[64];
    char path[1200];

    BW_REQUIRE("write the tree", build_tree() == 0);
    bw_set_data_root(BASE);
    bw_set_mod_root(NULL);
    (void)bw_fs_set_tables(NULL, 0, NULL, 0);
    build_image();
    bw_arm9_set_image(g_image, IMAGE_SIZE);
    BW_REQUIRE("install the table",
               bw_arc_install_table(BW_ARM9_LOAD_ADDRESS + TABLE_OFF, 2) == 0);

    /* bw_narc_pack's layout, checked by hand once. */
    {
        u8 *img = pack(B0, 3, &size);

        BW_REQUIRE("pack", img != NULL);
        printf("bw_narc_pack layout\n");
        BW_CHECK_EQ("file size field", bw_rd32(img + 8), size);
        BW_CHECK_EQ("BTAF at 0x10", bw_rd32(img + 0x10), 0x46415442);
        BW_CHECK_EQ("BTAF count", bw_rd16(img + 0x18), 3);
        BW_CHECK_EQ("member 1 starts aligned", bw_rd32(img + 0x10 + 12 + 8), 16);
        BW_CHECK_EQ("member 2 starts after padding", bw_rd32(img + 0x10 + 12 + 16), 24);
        BW_CHECK_EQ("padding byte is 0xFF", img[size - 1], 0xFF);
        BW_CHECK_EQ("total", size, 0x10 + 12 + 24 + 16 + 8 + 24);
        free(img);
    }

    {
        static const Mem want[] = { { 16, 1 }, { 5, 2 }, { 0, 3 } };
        check_archive("no mods", "a/0/0/0", 0, want, 3);
        BW_CHECK_EQ("no mods: nothing to merge",
                    bw_vfs_acquire("a/0/0/0", &data, &size, &handle), 0);
    }

    bw_set_mod_root(MODS "/empty");
    {
        static const Mem want[] = { { 16, 1 }, { 5, 2 }, { 0, 3 } };
        BW_CHECK_EQ("empty mod: no member files", bw_vfs_member_overrides("a/0/0/0"), 0);
        BW_CHECK_EQ("empty mod: nothing to merge",
                    bw_vfs_acquire("a/0/0/0", &data, &size, &handle), 0);
        check_archive("empty mod", "a/0/0/0", 0, want, 3);
    }

    bw_set_mod_root(MODS "/m1");
    BW_CHECK_EQ("manifest name", strcmp(bw_mod_name_at(0), "First mod"), 0);
    BW_CHECK_EQ("m1: two member files (README and .bak ignored)",
                bw_vfs_member_overrides("a/0/0/0"), 2);
    {
        const Mem want[] = { B0[0], M1_1, B0[2], EMPTY, M1_4 };
        static const Mem other[] = { { 8, 10 }, { 12, 11 } };
        check_archive("one mod: replace member 1, add member 4", "a/0/0/0", 0, want, 5);
        check_archive("one mod: an archive it does not touch", "a/0/0/1", 1, other, 2);
    }

    bw_set_mod_root(MODS "/m1;" MODS "/m2");
    BW_CHECK_EQ("list form: two mods", bw_mod_count(), 2);
    {
        const Mem want[] = { B0[0], M2_1, B0[2], EMPTY, M1_4, M2_5 };
        check_archive("two mods, m2 later: m2's member 1 wins", "a/0/0/0", 0, want, 6);
    }

    bw_set_mod_root(MODS "/m2; " MODS "/m1");
    {
        const Mem want[] = { B0[0], M1_1, B0[2], EMPTY, M1_4, M2_5 };
        check_archive("two mods, m1 later: m1's member 1 wins", "a/0/0/0", 0, want, 6);
    }

    bw_set_mod_root(MODS "/pri;" MODS "/m1;" MODS "/m2");
    BW_CHECK_EQ("manifest order sorts pri last", strcmp(bw_mod_name_at(2), "Priority"), 0);
    BW_CHECK_EQ("manifest order value", bw_mod_order_at(2), 10);
    BW_CHECK_EQ("bw_mod_root is the winning root",
                strcmp(bw_mod_root(), MODS "/pri"), 0);
    {
        const Mem want[] = { B0[0], PRI_1, B0[2], EMPTY, M1_4, M2_5 };
        check_archive("manifest order: pri wins though listed first", "a/0/0/0", 0, want, 6);
    }

    bw_set_mod_root(MODS "/m1;" MODS "/whole");
    BW_CHECK_EQ("a later whole file supersedes member dirs",
                bw_vfs_acquire("a/0/0/0", &data, &size, &handle), 0);
    BW_CHECK_EQ("bw_data_path names the whole file",
                bw_data_path(path, sizeof(path), "a/0/0/0") == 0 &&
                strcmp(path, MODS "/whole/a/0/0/0") == 0, 1);
    check_archive("whole file after member dir", "a/0/0/0", 0, W0, 2);

    bw_set_mod_root(MODS "/whole;" MODS "/m2");
    {
        const Mem want[] = { W0[0], M2_1, EMPTY, EMPTY, EMPTY, M2_5 };
        check_archive("member dir on top of a whole file", "a/0/0/0", 0, want, 6);
    }

    printf("errors\n");
    bw_set_mod_root(MODS "/dup");
    BW_CHECK_EQ("two files for one index", bw_vfs_member_overrides("a/0/0/0"), -1);
    BW_CHECK_EQ("acquire refuses", bw_vfs_acquire("a/0/0/0", &data, &size, &handle), -1);
    BW_CHECK_EQ("FS_OpenFile fails", FS_OpenFile(file, "a/0/0/0"), 0);
    BW_CHECK_EQ("bw_arc_open fails", bw_arc_open(&a, "a/0/0/0"), -1);

    bw_set_mod_root(MODS "/bad");
    BW_CHECK_EQ("members over a non-NARC",
                bw_vfs_acquire("a/0/0/2", &data, &size, &handle), -1);
    BW_CHECK_EQ("a mod directory over nothing is not an archive",
                bw_vfs_acquire("a/0/0/9", &data, &size, &handle), 0);

    printf("lifetime across a mod list change\n");
    bw_set_mod_root(MODS "/m1");
    BW_REQUIRE("open under m1", bw_arc_open(&a, "a/0/0/0") == 0);
    bw_set_mod_root(MODS "/m2");
    BW_REQUIRE("open under m2", bw_arc_open(&b, "a/0/0/0") == 0);
    BW_CHECK_EQ("the old open keeps m1's member",
                bw_arc_read_member(&a, 1, buf, sizeof(buf)) == (long)M1_1.len &&
                same_mem(buf, M1_1), 1);
    BW_CHECK_EQ("the new open sees m2's member",
                bw_arc_read_member(&b, 1, buf, sizeof(buf)) == (long)M2_1.len &&
                same_mem(buf, M2_1), 1);
    BW_CHECK_EQ("old open count", a.count, 5);
    BW_CHECK_EQ("new open count", b.count, 6);
    bw_arc_close(&a);
    bw_arc_close(&b);
    BW_CHECK_EQ("cache: repeated acquire is the same image",
                bw_vfs_acquire("a/0/0/0", &data, &size, &handle) == 1, 1);
    {
        const u8 *again;
        u32 size2;
        u32 handle2;

        BW_CHECK_EQ("second acquire",
                    bw_vfs_acquire("a/0/0/0", &again, &size2, &handle2), 1);
        BW_CHECK_EQ("same image", again == data && handle2 == handle, 1);
        bw_vfs_release(handle2);
    }
    bw_vfs_release(handle);

    bw_set_mod_root(NULL);
    BW_CHECK_EQ("cleared", bw_mod_count(), 0);
    bw_arm9_set_image(NULL, 0);
    clean_tree();
    return 0;
}

/* ------------------------------------------------------------------ */
/* Real data                                                           */
/* ------------------------------------------------------------------ */

static u8 *slurp(const char *path, u32 *size)
{
    FILE *fp = fopen(path, "rb");
    long end;
    u8 *p;

    *size = 0;
    if (fp == NULL) {
        return NULL;
    }
    fseek(fp, 0, SEEK_END);
    end = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    p = (u8 *)malloc(end > 0 ? (size_t)end : 1);
    if (p != NULL && end > 0 && fread(p, 1, (size_t)end, fp) != (size_t)end) {
        free(p);
        p = NULL;
    }
    fclose(fp);
    *size = (u32)end;
    return p;
}

/* Repack a real archive from its own tables; 1 if the bytes come back. */
static int repacks(const u8 *img, u32 size)
{
    u32 pos = bw_rd16(img + 12);
    u32 fat = 0, btnf = 0, btnf_size = 0, gmif = 0;
    u32 count;
    u32 i;
    const u8 **data;
    u32 *sizes;
    u8 *out;
    u32 out_size;
    int ok;

    while (pos + 8 <= size) {
        u32 magic = bw_rd32(img + pos);
        u32 cs = bw_rd32(img + pos + 4);

        if (cs < 8 || cs > size - pos) {
            return 0;
        }
        if (magic == 0x46415442u) fat = pos;
        if (magic == 0x464E5442u) { btnf = pos; btnf_size = cs; }
        if (magic == 0x46494D47u) gmif = pos;
        pos += cs;
    }
    if (fat == 0 || btnf == 0 || gmif == 0) {
        return 0;
    }
    count = bw_rd16(img + fat + 8);
    data = (const u8 **)calloc(count + 1, sizeof(*data));
    sizes = (u32 *)calloc(count + 1, sizeof(*sizes));
    for (i = 0; i < count; i++) {
        u32 s = bw_rd32(img + fat + 12 + 8 * i);

        data[i] = img + gmif + 8 + s;
        sizes[i] = bw_rd32(img + fat + 16 + 8 * i) - s;
    }
    ok = bw_narc_pack(data, sizes, count, img + btnf, btnf_size, &out, &out_size) == 0 &&
         out_size == size && memcmp(out, img, size) == 0;
    if (ok) {
        free(out);
    }
    free((void *)data);
    free(sizes);
    return ok;
}

#define REAL_ARC   "a/0/1/6"
#define REAL_MOD   "vfsm_real"
#define REAL_OVR   5u

static void real_data(const char *root)
{
    char path[1200];
    char rel[64];
    u32 n;
    u32 i;
    u32 narcs = 0;
    u32 good = 0;
    u8 *arm9;
    u32 arm9_size;
    u32 arc_id = 0xFFFFFFFFu;
    u32 file_id;
    u32 top;
    u32 bottom;
    BwArcFile arc;
    struct ArcHandle h;
    u8 file[BW_FSFILE_SIZE];
    u32 base_count;
    long keep_len;
    u8 *keep;
    u8 *buf;
    const Mem ovr = { 301, 90 };
    const Mem add = { 77, 91 };
    char ovr_path[128];
    char add_path[128];

    bw_set_data_root(root);
    bw_set_mod_root(NULL);
    (void)bw_fs_set_tables(NULL, 0, NULL, 0);
    snprintf(path, sizeof(path), "%s/" REAL_ARC, root);
    if (!bw_fs_tables_available() || !bw_data_exists(REAL_ARC)) {
        printf("real data: skipped (no extract at %s)\n", root);
        return;
    }

    printf("real data: every archive repacks byte for byte\n");
    n = bw_fs_file_count();
    for (i = 0; i < n; i++) {
        u8 *img;
        u32 size;

        if (bw_fs_path_of_id(i, rel, sizeof(rel)) != 0 || strncmp(rel, "a/", 2) != 0 ||
            bw_data_path(path, sizeof(path), rel) != 0) {
            continue;
        }
        img = slurp(path, &size);
        if (img != NULL && size >= 16 && bw_rd32(img) == 0x4352414Eu) {
            narcs++;
            good += (u32)repacks(img, size);
        }
        free(img);
    }
    printf("  %u archives\n", narcs);
    BW_CHECK_EQ("there are archives", narcs > 200, 1);
    BW_CHECK_EQ("all of them repack", good, narcs);

    /* The game's own name table, out of the real ARM9 image. */
    snprintf(path, sizeof(path), "%s/../arm9_dec.bin", root);
    arm9 = slurp(path, &arm9_size);
    if (arm9 == NULL) {
        printf("real data: no arm9_dec.bin, the by-id half is skipped\n");
        return;
    }
    bw_arm9_set_image(arm9, arm9_size);
    free(arm9);
    BW_CHECK_EQ("install the real table",
                bw_arc_install_table(BW_ARC_TABLE_ADDRESS, BW_ARC_TABLE_COUNT), 0);
    for (i = 0; i < BW_ARC_TABLE_COUNT; i++) {
        const u8 *e = (const u8 *)bw_arm9_rodata(BW_ARC_TABLE_ADDRESS + 4 * i, 4);
        const char *s = e == NULL ? NULL
                                  : (const char *)bw_arm9_rodata(bw_rd32(e), sizeof(REAL_ARC));
        if (s != NULL && memcmp(s, REAL_ARC, sizeof(REAL_ARC)) == 0) {
            arc_id = i;
        }
    }
    BW_REQUIRE_VOID(arc_id != 0xFFFFFFFFu);
    printf("real data: %s is archive id %u\n", REAL_ARC, arc_id);

    /* The original member REAL_OVR + 1, to show it is untouched later. */
    BW_REQUIRE_VOID(bw_arc_open(&arc, REAL_ARC) == 0);
    base_count = arc.count;
    keep_len = bw_arc_member_size(&arc, REAL_OVR + 1);
    keep = (u8 *)malloc(keep_len > 0 ? (size_t)keep_len : 1);
    BW_CHECK_EQ("read the neighbour",
                bw_arc_read_member(&arc, REAL_OVR + 1, keep, (u32)keep_len), keep_len);
    bw_arc_close(&arc);
    BW_REQUIRE_VOID(base_count > REAL_OVR + 1);

    /* A mod: member REAL_OVR replaced, one member added past the end. */
    MKDIR(REAL_MOD);
    MKDIR(REAL_MOD "/a");
    MKDIR(REAL_MOD "/a/0");
    MKDIR(REAL_MOD "/a/0/1");
    MKDIR(REAL_MOD "/a/0/1/6");
    snprintf(ovr_path, sizeof(ovr_path), REAL_MOD "/" REAL_ARC "/%u.bin", REAL_OVR);
    snprintf(add_path, sizeof(add_path), REAL_MOD "/" REAL_ARC "/%u.bin", base_count);
    write_mem(ovr_path, ovr);
    write_mem(add_path, add);
    bw_set_mod_root(REAL_MOD);

    printf("real data: one member overridden, one added\n");
    buf = (u8 *)malloc(0x10000);
    BW_REQUIRE_VOID(bw_arc_open(&arc, REAL_ARC) == 0);
    BW_CHECK_EQ("count grew by one", arc.count, base_count + 1);
    BW_CHECK_EQ("bw_arc_read_member: the override",
                bw_arc_read_member(&arc, REAL_OVR, buf, 0x10000) == (long)ovr.len &&
                same_mem(buf, ovr), 1);
    BW_CHECK_EQ("bw_arc_read_member: the neighbour is untouched",
                bw_arc_read_member(&arc, REAL_OVR + 1, buf, 0x10000) == keep_len &&
                memcmp(buf, keep, (size_t)keep_len) == 0, 1);
    bw_arc_close(&arc);

    ARC_TOOL_OpenArchive(&h, arc_id);
    BW_CHECK_EQ("ARC_TOOL_SeekMember: the override's size",
                ARC_TOOL_SeekMember(&h, REAL_OVR, 0, 0), ovr.len);
    BW_CHECK_EQ("FS_ReadFile after it reads the override",
                FS_ReadFile(h.raw, buf, (s32)ovr.len) == (s32)ovr.len && same_mem(buf, ovr), 1);
    FS_CloseFile(h.raw);
    memset(buf, 0, 0x10000);
    ARC_TOOL_ReadMember(buf, arc_id, base_count);
    BW_CHECK_EQ("ARC_TOOL_ReadMember reads the added member", same_mem(buf, add), 1);
    ARC_TOOL_ReadMember(buf, arc_id, REAL_OVR + 1);
    BW_CHECK_EQ("ARC_TOOL_ReadMember reads the neighbour",
                memcmp(buf, keep, (size_t)keep_len), 0);

    /* The same archive by file id and by ROM range is the merged one. */
    {
        u8 *merged;
        u32 merged_size;

        BW_REQUIRE_VOID(bw_vfs_build_merged(REAL_ARC, &merged, &merged_size) == 1);
        BW_REQUIRE_VOID(bw_fs_id_of_path(REAL_ARC, &file_id) == 0);
        BW_CHECK_EQ("open by file id", bw_fs_open_id(file, file_id), 0);
        BW_CHECK_EQ("by file id: merged length", FS_GetFileLength(file), merged_size);
        FS_CloseFile(file);
        BW_REQUIRE_VOID(bw_fs_file_extent(file_id, &top, &bottom) == 0);
        BW_CHECK_EQ("open by ROM range", FS_OpenFileDirect(file, NULL, top, bottom, file_id), 1);
        BW_CHECK_EQ("by ROM range: merged length", FS_GetFileLength(file), merged_size);
        free(buf);
        buf = (u8 *)malloc(merged_size);
        BW_CHECK_EQ("by ROM range: merged bytes",
                    FS_ReadFile(file, buf, (s32)merged_size) == (s32)merged_size &&
                    memcmp(buf, merged, merged_size) == 0, 1);
        FS_CloseFile(file);
        free(merged);
    }

    free(buf);
    free(keep);

    /* Lines the port makes for a mod (sdk/text_from.c) apply after the
     * mod's own line file for the bank, and go with bw_vfs_lines_clear. */
    printf("real data: made lines over a mod's line file\n");
    MKDIR(REAL_MOD "/a/0/0");
    MKDIR(REAL_MOD "/a/0/0/2");
    write_text(REAL_MOD "/a/0/0/2/13.msg", "1716 = Plain\n1717 = Kept\n");
    {
        static const u16 made_units[] = { 'M', 'a', 'd', 'e' };
        BwMsgLine made;
        u8 *img;
        u32 img_size, len;
        const u8 *bank;
        u16 got[16];

        made.index = 1716;
        made.units = (u16 *)made_units;
        made.len = 4;
        BW_REQUIRE_VOID(bw_mod_count() == 1);
        BW_CHECK_EQ("bw_vfs_lines_set", bw_vfs_lines_set(bw_mod_root_at(0), "a/0/0/2", 13, &made, 1), 0);
        BW_REQUIRE_VOID(bw_vfs_build_merged("a/0/0/2", &img, &img_size) == 1);
        BW_REQUIRE_VOID(bw_narc_member(img, img_size, 13, &bank, &len) == 0);
        BW_CHECK_EQ("the made line wins", bw_msg_bank_get(bank, len, 1716, 0, got, 16) == 4 &&
                    got[0] == 'M' && got[3] == 'e', 1);
        BW_CHECK_EQ("the mod's other line stays", bw_msg_bank_get(bank, len, 1717, 0, got, 16) == 4 &&
                    got[0] == 'K', 1);
        free(img);
        bw_vfs_lines_clear();
        BW_REQUIRE_VOID(bw_vfs_build_merged("a/0/0/2", &img, &img_size) == 1);
        BW_REQUIRE_VOID(bw_narc_member(img, img_size, 13, &bank, &len) == 0);
        BW_CHECK_EQ("cleared: the mod's line again", bw_msg_bank_get(bank, len, 1716, 0, got, 16) == 5 &&
                    got[0] == 'P', 1);
        free(img);
    }
    remove(REAL_MOD "/a/0/0/2/13.msg");
    RMDIR(REAL_MOD "/a/0/0/2");
    RMDIR(REAL_MOD "/a/0/0");

    bw_set_mod_root(NULL);
    bw_arm9_set_image(NULL, 0);
    remove(ovr_path);
    remove(add_path);
    RMDIR(REAL_MOD "/a/0/1/6");
    RMDIR(REAL_MOD "/a/0/1");
    RMDIR(REAL_MOD "/a/0");
    RMDIR(REAL_MOD "/a");
    RMDIR(REAL_MOD);
}

int main(int argc, char **argv)
{
    if (synthetic() != 0) {
        BW_REPORT("test_vfs_members");
    }
    if (argc > 1 && argv[1] != NULL && argv[1][0] != '\0') {
        real_data(argv[1]);
    } else {
        printf("real data: skipped (no data root given)\n");
    }
    BW_REPORT("test_vfs_members");
}
