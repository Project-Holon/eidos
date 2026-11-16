/*
 * tests/test_mod_v6.c -- the services mod API version 6 added that run
 * without the game: members in memory (closed and open archives), text
 * entries (one and two blocks) and a mod's own files.
 *
 * The tree is synthetic and written under the build folder: a data root
 * with one NARC (a/0/0/0) and one text archive (a/0/0/2), and an empty mod
 * folder whose memory layer the checks fill.  The real-data half reads a
 * text bank of the player's own a/0/0/2 when a data root is given.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "port.h"
#include "fs.h"
#include "vfs.h"
#include "save.h"
#include "plat_save.h"
#include "mod_v6.h"
#include "port_test.h"

extern BOOL FS_SeekFile(void *file, s32 offset, u32 origin);
extern s32  FS_ReadFile(void *file, void *dst, s32 len);
extern BOOL FS_CloseFile(void *file);

#define TREE  "test_mod_v6_tree"
#define BASE  TREE "/data"
#define MOD   TREE "/mod"
#define FILES TREE "/files"

static int write_file(const char *path, const void *data, u32 size)
{
    FILE *fp = fopen(path, "wb");

    if (fp == NULL) {
        return -1;
    }
    if (size != 0 && fwrite(data, 1, size, fp) != size) {
        fclose(fp);
        return -1;
    }
    return fclose(fp) == 0 ? 0 : -1;
}

/* A NARC of `n` members, member i of i+1 bytes, each byte 0x10 * i + k. */
static u8 *make_narc(u32 n, u32 *size)
{
    static u8 bytes[16][16];
    const u8 *data[16];
    u32 sizes[16];
    u32 i, k;
    u8 *img = NULL;

    for (i = 0; i < n; i++) {
        for (k = 0; k <= i; k++) {
            bytes[i][k] = (u8)(0x10 * i + k);
        }
        data[i] = bytes[i];
        sizes[i] = i + 1;
    }
    if (bw_narc_pack(data, sizes, n, NULL, 0, &img, size) != 0) {
        return NULL;
    }
    return img;
}

/* An empty bank of `blocks` blocks, then the entries set one by one. */
static u8 *make_bank(u32 blocks, u32 entries, u32 *size)
{
    u8 empty[32];
    u8 *bank = NULL, *next;
    u32 bank_size, k, e;
    u16 line[8];

    memset(empty, 0, sizeof(empty));
    empty[0] = (u8)blocks;
    for (k = 0; k < blocks; k++) {
        bw_wr32(empty + 12 + 4 * k, 12 + 4 * blocks);
    }
    bw_wr32(empty + 12 + 4 * blocks, 4);
    bank = (u8 *)malloc(sizeof(empty));
    memcpy(bank, empty, sizeof(empty));
    bank_size = 12 + 4 * blocks + 4;
    for (k = 0; k < blocks; k++) {
        for (e = 0; e < entries; e++) {
            line[0] = (u16)('A' + e);
            line[1] = (u16)('0' + k);
            line[2] = 0xF000;          /* a control code goes through */
            line[3] = 0x0001;
            if (bw_msg_bank_set(bank, bank_size, e, k, line, 4, &next, &bank_size) != 0) {
                free(bank);
                return NULL;
            }
            free(bank);
            bank = next;
        }
    }
    *size = bank_size;
    return bank;
}

static int build_tree(void)
{
    u8 *img;
    u32 size;
    u8 *banks[2];
    u32 bank_sizes[2];
    int rc;

    if (bw_plat_mkdirs(BASE "/a/0/0") != 0 || bw_plat_mkdirs(MOD) != 0 ||
        bw_plat_mkdirs(FILES) != 0) {
        return -1;
    }
    img = make_narc(4, &size);
    if (img == NULL || write_file(BASE "/a/0/0/0", img, size) != 0) {
        return -1;
    }
    free(img);
    banks[0] = make_bank(1, 3, &bank_sizes[0]);
    banks[1] = make_bank(1, 2, &bank_sizes[1]);
    if (banks[0] == NULL || banks[1] == NULL ||
        bw_narc_pack((const u8 *const *)banks, bank_sizes, 2, NULL, 0, &img, &size) != 0) {
        return -1;
    }
    rc = write_file(BASE "/a/0/0/2", img, size);
    free(img);
    free(banks[0]);
    free(banks[1]);
    return rc;
}

/* The way the game's archive handle reads a member: the FAT offset and the
 * image base from the open, the FAT entry again on each load
 * (ARC_TOOL_ReadHandleHeader, ARC_TOOL_ReadHandleMember). */
typedef struct Handle {
    u8  file[BW_FSFILE_SIZE];
    u32 fat;
    u32 base;
    u32 count;
} Handle;

static int handle_open(Handle *h, const char *rel)
{
    u8 head[16];
    u32 btaf, btnf;
    u8 t[8];

    if (bw_fs_open_rel(h->file, rel) != 0) {
        return -1;
    }
    FS_SeekFile(h->file, 0, 0);
    FS_ReadFile(h->file, head, 16);
    h->fat = bw_rd16(head + 12);
    FS_SeekFile(h->file, (s32)(h->fat + 4), 0);
    FS_ReadFile(h->file, t, 8);
    btaf = bw_rd32(t);
    h->count = bw_rd16(t + 4);
    FS_SeekFile(h->file, (s32)(h->fat + btaf + 4), 0);
    FS_ReadFile(h->file, t, 4);
    btnf = bw_rd32(t);
    h->base = h->fat + btaf + btnf + 8;
    return 0;
}

static int handle_read(Handle *h, u32 index, u8 *out, u32 max)
{
    u8 t[8];
    u32 start, end;

    FS_SeekFile(h->file, (s32)(h->fat + 0xC + index * 8), 0);
    FS_ReadFile(h->file, t, 8);
    start = bw_rd32(t);
    end = bw_rd32(t + 4);
    if (end - start > max) {
        return -1;
    }
    FS_SeekFile(h->file, (s32)(h->base + start), 0);
    return (int)FS_ReadFile(h->file, out, (s32)(end - start));
}

static int members(void)
{
    static u8 big[17u * 1024u * 1024u];
    u8 buf[64];
    const u8 data[5] = { 0xA1, 0xA2, 0xA3, 0xA4, 0xA5 };
    const u8 data2[2] = { 0xB1, 0xB2 };
    Handle h, h2;
    int n;

    printf("members in memory: a closed archive\n");
    BW_CHECK_EQ("get the game's member 2", bw_mod_vfs_member_get("a/0/0/0", 2, buf, sizeof(buf)), 3);
    BW_CHECK_EQ("its first byte", buf[0], 0x20);
    BW_CHECK_EQ("set member 2", bw_mod_vfs_member_set_for(MOD, "a/0/0/0", 2, data, 5), 0);
    BW_CHECK_EQ("get sees the change", bw_mod_vfs_member_get("/a/0/0/0", 2, buf, sizeof(buf)), 5);
    BW_CHECK_EQ("the bytes", buf[4], 0xA5);
    BW_CHECK_EQ("set past the end of a closed archive",
                bw_mod_vfs_member_set_for(MOD, "a/0/0/0", 6, data2, 2), 0);
    BW_CHECK_EQ("the new member", bw_mod_vfs_member_get("a/0/0/0", 6, buf, sizeof(buf)), 2);
    BW_CHECK_EQ("the gap is an empty member", bw_mod_vfs_member_get("a/0/0/0", 5, buf, sizeof(buf)), 0);
    BW_CHECK_EQ("no such archive", bw_mod_vfs_member_set_for(MOD, "a/9/9/9", 0, data, 5), -1);
    BW_CHECK_EQ("a path that climbs out", bw_mod_vfs_member_set_for(MOD, "../a/0/0/0", 0, data, 5), -1);
    BW_CHECK_EQ("past the NARC limit", bw_mod_vfs_member_set_for(MOD, "a/0/0/0", 0x10000u, data, 5), -1);
    BW_CHECK_EQ("clear member 6", bw_mod_vfs_member_clear_for(MOD, "a/0/0/0", 6), 1);
    BW_CHECK_EQ("the archive is back to 4 members", bw_mod_vfs_member_get("a/0/0/0", 6, buf, sizeof(buf)), -1);
    bw_mod_vfs_frame();

    printf("members in memory: the limits\n");
    memset(big, 0x5A, sizeof(big));
    BW_CHECK_EQ("16 MiB for one mod: 17 MiB refused",
                bw_mod_vfs_member_set_for(MOD, "a/0/0/0", 3, big, sizeof(big)), -1);
    BW_CHECK_EQ("the refused member is not there",
                bw_mod_vfs_member_get("a/0/0/0", 3, buf, sizeof(buf)), 4);
    BW_CHECK_EQ("15 MiB fits",
                bw_mod_vfs_member_set_for(MOD, "a/0/0/0", 3, big, 15u * 1024u * 1024u), 0);
    BW_CHECK_EQ("another 2 MiB does not",
                bw_mod_vfs_member_set_for(MOD, "a/0/0/0", 1, big, 2u * 1024u * 1024u), -1);
    BW_CHECK_EQ("replacing the 15 MiB one counts its old size",
                bw_mod_vfs_member_set_for(MOD, "a/0/0/0", 3, big, 16u * 1024u * 1024u - 64u), 0);
    BW_CHECK_EQ("clear it", bw_mod_vfs_member_clear_for(MOD, "a/0/0/0", 3), 1);
    bw_mod_vfs_frame();

    printf("members in memory: an archive that is open\n");
    BW_REQUIRE("open it as the game does", handle_open(&h, "a/0/0/0") == 0);
    BW_REQUIRE("open it a second time", handle_open(&h2, "a/0/0/0") == 0);
    BW_CHECK_EQ("the handle sees 4 members", h.count, 4);
    n = handle_read(&h, 2, buf, sizeof(buf));
    BW_CHECK_EQ("member 2 as set before the open", n, 5);
    BW_CHECK_EQ("member past the end of an open archive refused",
                bw_mod_vfs_member_set_for(MOD, "a/0/0/0", 4, data2, 2), -1);
    BW_CHECK_EQ("set member 1 while it is open",
                bw_mod_vfs_member_set_for(MOD, "a/0/0/0", 1, data2, 2), 0);
    BW_CHECK_EQ("a swap is queued", bw_mod_vfs_pending(), 1);
    n = handle_read(&h, 1, buf, sizeof(buf));
    BW_CHECK_EQ("before the frame the old member", n, 2);
    BW_CHECK_EQ("old bytes", buf[0], 0x10);
    bw_mod_vfs_frame();
    BW_CHECK_EQ("the swap ran", bw_mod_vfs_pending(), 0);
    n = handle_read(&h, 1, buf, sizeof(buf));
    BW_CHECK_EQ("after the frame the handle reads the new member", n, 2);
    BW_CHECK_EQ("new bytes", buf[0], 0xB1);
    n = handle_read(&h2, 1, buf, sizeof(buf));
    BW_CHECK_EQ("the second open file too", buf[1], 0xB2);
    n = handle_read(&h, 2, buf, sizeof(buf));
    BW_CHECK_EQ("an unchanged member reads as before", buf[0], 0xA1);
    n = handle_read(&h, 0, buf, sizeof(buf));
    BW_CHECK_EQ("member 0 as before", n, 1);
    BW_CHECK_EQ("clear every member of the mod", bw_mod_vfs_member_clear_for(MOD, "", 0) >= 2, 1);
    bw_mod_vfs_frame();
    n = handle_read(&h, 2, buf, sizeof(buf));
    BW_CHECK_EQ("cleared: the game's own member 2 again", n, 3);
    BW_CHECK_EQ("its byte", buf[0], 0x20);
    FS_CloseFile(h.file);
    FS_CloseFile(h2.file);
    BW_CHECK_EQ("set past the end once it is closed",
                bw_mod_vfs_member_set_for(MOD, "a/0/0/0", 4, data2, 2), 0);
    BW_REQUIRE("open again", handle_open(&h, "a/0/0/0") == 0);
    BW_CHECK_EQ("the archive grew", h.count, 5);
    FS_CloseFile(h.file);
    (void)bw_mod_vfs_member_clear_for(MOD, "", 0);
    bw_mod_vfs_frame();
    return 0;
}

static int text(void)
{
    u8 *bank, *next;
    u32 size, next_size;
    u16 out[16];
    const u16 hello[3] = { 'H', 'i', 0xFFFE };

    printf("text: a bank of two blocks (Japanese)\n");
    bank = make_bank(2, 3, &size);
    BW_REQUIRE("make it", bank != NULL);
    BW_CHECK_EQ("block 0 entry 1 length", bw_msg_bank_get(bank, size, 1, 0, out, 16), 4);
    BW_CHECK_EQ("its first unit", out[0], 'B');
    BW_CHECK_EQ("its control code", out[2], 0xF000);
    BW_CHECK_EQ("block 1 entry 2", bw_msg_bank_get(bank, size, 2, 1, out, 16), 4);
    BW_CHECK_EQ("its block mark", out[1], '1');
    BW_CHECK_EQ("past the last block", bw_msg_bank_get(bank, size, 0, 2, out, 16), -1);
    BW_CHECK_EQ("set entry 5 of block 1 grows both blocks",
                bw_msg_bank_set(bank, size, 5, 1, hello, 3, &next, &next_size), 0);
    BW_CHECK_EQ("the new count", bw_rd16(next + 2), 6);
    BW_CHECK_EQ("block 1 entry 5", bw_msg_bank_get(next, next_size, 5, 1, out, 16), 3);
    BW_CHECK_EQ("its newline", out[2], 0xFFFE);
    BW_CHECK_EQ("block 0 entry 5 is empty", bw_msg_bank_get(next, next_size, 5, 0, out, 16), 0);
    BW_CHECK_EQ("block 0 entry 0 untouched", bw_msg_bank_get(next, next_size, 0, 0, out, 16), 4);
    BW_CHECK_EQ("its unit", out[0], 'A');
    BW_CHECK_EQ("the header names the larger section", bw_rd32(next + 4) >= 4u + 8u * 6u, 1);
    free(next);
    free(bank);

    printf("text: entries through the archive\n");
    BW_CHECK_EQ("the game's line", bw_mod_text_get(2, 0, 2, 0, out, 16), 4);
    BW_CHECK_EQ("its unit", out[0], 'C');
    BW_CHECK_EQ("set a line", bw_mod_text_set_for(MOD, 2, 0, 1, 0, hello, 2), 0);
    BW_CHECK_EQ("read it back", bw_mod_text_get(2, 0, 1, 0, out, 16), 2);
    BW_CHECK_EQ("its units", out[1], 'i');
    BW_CHECK_EQ("the line next to it is kept", bw_mod_text_get(2, 0, 2, 0, out, 16), 4);
    BW_CHECK_EQ("a new line past the end", bw_mod_text_set_for(MOD, 2, 1, 9, 0, hello, 3), 0);
    BW_CHECK_EQ("read the new line", bw_mod_text_get(2, 1, 9, 0, out, 16), 3);
    BW_CHECK_EQ("a cut read gives the whole length", bw_mod_text_get(2, 1, 9, 0, out, 1), 3);
    BW_CHECK_EQ("a new bank past the archive's end", bw_mod_text_set_for(MOD, 2, 2, 0, 0, hello, 3), 0);
    BW_CHECK_EQ("read it", bw_mod_text_get(2, 2, 0, 0, out, 16), 3);
    BW_CHECK_EQ("archive 4 is not text", bw_mod_text_get(4, 0, 0, 0, out, 16), -1);
    (void)bw_mod_vfs_member_clear_for(MOD, "", 0);
    BW_CHECK_EQ("cleared: the game's line again", bw_mod_text_get(2, 0, 1, 0, out, 16), 4);
    bw_mod_vfs_frame();
    return 0;
}

static int files(void)
{
    static u8 big[9u * 1024u * 1024u];
    char list[512];
    u8 buf[16];
    u32 got = 0;
    int n;

    printf("mod files\n");
    bw_mod_file_set_base(FILES);
    (void)bw_mod_file_delete_for("ns1", "world.json");
    (void)bw_mod_file_delete_for("ns1", "a");
    (void)bw_mod_file_delete_for("ns1", "b");
    BW_CHECK_EQ("a plain name", bw_mod_file_name_ok("world-1.json"), 1);
    BW_CHECK_EQ("a separator", bw_mod_file_name_ok("a/b"), 0);
    BW_CHECK_EQ("a backslash", bw_mod_file_name_ok("a\\b"), 0);
    BW_CHECK_EQ("a parent", bw_mod_file_name_ok(".."), 0);
    BW_CHECK_EQ("a dot first", bw_mod_file_name_ok(".hidden"), 0);
    BW_CHECK_EQ("a drive", bw_mod_file_name_ok("c:x"), 0);
    BW_CHECK_EQ("a device", bw_mod_file_name_ok("CON"), 0);
    BW_CHECK_EQ("a device with an extension", bw_mod_file_name_ok("nul.txt"), 0);
    BW_CHECK_EQ("a name that only starts like one", bw_mod_file_name_ok("console"), 1);
    BW_CHECK_EQ("empty", bw_mod_file_name_ok(""), 0);
    BW_CHECK_EQ("write", bw_mod_file_write_for("ns1", "world.json", "{\"a\":1}", 7), 0);
    BW_CHECK_EQ("read: the size", bw_mod_file_read_for("ns1", "world.json", buf, sizeof(buf), &got), 7);
    BW_CHECK_EQ("read: the bytes copied", got, 7);
    BW_CHECK_EQ("read: a byte", buf[5], '1');
    BW_CHECK_EQ("a cut read", bw_mod_file_read_for("ns1", "world.json", buf, 2, &got), 7);
    BW_CHECK_EQ("a cut read copies 2", got, 2);
    BW_CHECK_EQ("another mod does not see it", bw_mod_file_read_for("ns2", "world.json", buf, 16, &got), -1);
    BW_CHECK_EQ("a bad name is refused", bw_mod_file_write_for("ns1", "../x", "x", 1), -1);
    n = bw_mod_file_list_for("ns1", list, sizeof(list));
    BW_CHECK_EQ("the list has the head line", strncmp(list, "used 7 quota 16777216\n", 22), 0);
    BW_CHECK_EQ("the list names the file", strstr(list, "world.json 7\n") != NULL, 1);
    BW_CHECK_EQ("the list's length", n, (long)strlen(list));
    BW_CHECK_EQ("a list into a small buffer still says the length",
                bw_mod_file_list_for("ns1", list, 8), n);

    memset(big, 0x33, sizeof(big));
    BW_CHECK_EQ("9 MiB", bw_mod_file_write_for("ns1", "a", big, sizeof(big)), 0);
    BW_CHECK_EQ("9 more MiB pass the quota", bw_mod_file_write_for("ns1", "b", big, sizeof(big)), -1);
    BW_CHECK_EQ("the refused file is not there", bw_mod_file_read_for("ns1", "b", buf, 1, &got), -1);
    BW_CHECK_EQ("overwriting a with 9 MiB counts its old size",
                bw_mod_file_write_for("ns1", "a", big, sizeof(big)), 0);
    BW_CHECK_EQ("a refused overwrite keeps the old file",
                bw_mod_file_write_for("ns1", "world.json", big, 8u * 1024u * 1024u), -1);
    BW_CHECK_EQ("the old file whole", bw_mod_file_read_for("ns1", "world.json", buf, 16, &got), 7);
    BW_CHECK_EQ("delete", bw_mod_file_delete_for("ns1", "a"), 0);
    BW_CHECK_EQ("delete again", bw_mod_file_delete_for("ns1", "a"), -1);
    BW_CHECK_EQ("delete the last", bw_mod_file_delete_for("ns1", "world.json"), 0);
    n = bw_mod_file_list_for("ns1", list, sizeof(list));
    BW_CHECK_EQ("empty again", strcmp(list, "used 0 quota 16777216\n"), 0);
    bw_mod_file_set_base(NULL);
    return 0;
}

static int real_data(const char *root)
{
    u16 out[256];
    int n;

    printf("real data: %s\n", root);
    bw_set_data_root(root);
    bw_set_mod_root(MOD);
    n = bw_mod_text_get(2, 0, 0, 0, out, 256);
    BW_CHECK_EQ("the player's own bank 0 line 0 reads", n >= 0, 1);
    n = bw_mod_text_get(3, 0, 0, 0, out, 256);
    BW_CHECK_EQ("the story text reads", n >= 0, 1);
    {
        u16 mine[4] = { 'T', 'e', 's', 't' };

        BW_CHECK_EQ("set a line of a real bank", bw_mod_text_set_for(MOD, 2, 0, 0, 0, mine, 4), 0);
        BW_CHECK_EQ("read it", bw_mod_text_get(2, 0, 0, 0, out, 256), 4);
        BW_CHECK_EQ("its unit", out[3], 't');
        BW_CHECK_EQ("the next line is the game's", bw_mod_text_get(2, 0, 1, 0, out, 256) >= 0, 1);
        (void)bw_mod_vfs_member_clear_for(MOD, "", 0);
    }
    return 0;
}

int main(int argc, char **argv)
{
    BW_REQUIRE("write the tree", build_tree() == 0);
    bw_set_data_root(BASE);
    bw_set_mod_root(MOD);
    (void)bw_fs_set_tables(NULL, 0, NULL, 0);
    if (members() != 0 || text() != 0 || files() != 0) {
        BW_REPORT("test_mod_v6");
    }
    if (argc > 1 && argv[1] != NULL && argv[1][0] != '\0') {
        real_data(argv[1]);
    } else {
        printf("real data: skipped (no data root given)\n");
    }
    BW_REPORT("test_mod_v6");
}
