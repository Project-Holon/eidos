/*
 * tests/test_vfs_sdat.c -- the sound archive append merge (sdk/vfs_sdat.c).
 *
 * The synthetic half builds a tiny archive in memory (2 sequences, 2 banks,
 * 2 wave archives, 1 player, all named), merges two mods' additions and
 * parses the result with the port's own reader (engine/audio/sdat.c).  It
 * checks that the counts grew, that every original record, name and file is
 * unchanged, that new ids follow the mod load order, that the SYMB names are
 * `<mod>:<name>`, and that the only original bytes that differ are the
 * header and the two block heads.  The grammar, the error paths and the
 * no-mod copy are covered too.
 *
 * The data half (argv[1] is the data root; skipped without the file) merges
 * one wave archive, one bank and one sequence into the real archive and runs
 * the same preservation checks over its 2076 sequences, 1986 banks and 3874
 * files.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "audio.h"
#include "vfs_sdat.h"
#include "port_test.h"

static void put32(u8 *p, u32 v)
{
    p[0] = (u8)v;
    p[1] = (u8)(v >> 8);
    p[2] = (u8)(v >> 16);
    p[3] = (u8)(v >> 24);
}

static void put16(u8 *p, u32 v)
{
    p[0] = (u8)v;
    p[1] = (u8)(v >> 8);
}

/* ------------------------------------------------------------------ */
/* The synthetic archive                                               */
/* ------------------------------------------------------------------ */

#define TINY_MAX 4096

typedef struct {
    u8  d[TINY_MAX];
    u32 size;
} Tiny;

static const char *g_names[8][2] = {
    { "SEQ_A", "SEQ_B" }, { 0, 0 }, { "BANK_A", "BANK_B" },
    { "WAVE_A", "WAVE_B" }, { "PLAYER_0", 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }
};
static const u32 g_counts[8] = { 2, 0, 2, 2, 1, 0, 0, 0 };

/* A file's bytes: 4-byte magic, then a pattern from a seed. */
static u32 make_file(u8 *dst, const char *magic, u8 seed, u32 len)
{
    u32 i;
    memcpy(dst, magic, 4);
    for (i = 4; i < len; i++) {
        dst[i] = (u8)(seed * 13u + i * 5u);
    }
    return len;
}

static void build_tiny(Tiny *t)
{
    u8 *d = t->d;
    u32 symb = 0x40, symb_size, info, info_size, fat, fat_size, file, k, i, pos, recs;
    u32 tab[8], stab[8], str, fpos;
    u32 file_len[6] = { 32, 36, 40, 44, 48, 52 };
    const char *magics[6] = { "SSEQ", "SSEQ", "SBNK", "SBNK", "SWAR", "SWAR" };

    memset(t, 0, sizeof(*t));
    /* SYMB: header, 8 tables, strings. */
    pos = symb + 8 + 8 * 4;
    for (k = 0; k < 8; k++) {
        stab[k] = pos - symb;
        put32(d + symb + 8 + k * 4, stab[k]);
        put32(d + pos, g_counts[k]);
        pos += 4 + g_counts[k] * 4;
    }
    str = pos;
    for (k = 0; k < 8; k++) {
        for (i = 0; i < g_counts[k]; i++) {
            put32(d + symb + stab[k] + 4 + i * 4, g_names[k][i] ? str - symb : 0);
            if (g_names[k][i]) {
                strcpy((char *)d + str, g_names[k][i]);
                str += (u32)strlen(g_names[k][i]) + 1;
            }
        }
    }
    str = (str + 3) & ~3u;
    memcpy(d + symb, "SYMB", 4);
    symb_size = str - symb;
    put32(d + symb + 4, symb_size);

    /* INFO. */
    info = str;
    pos = info + 8 + 8 * 4;
    for (k = 0; k < 8; k++) {
        tab[k] = pos - info;
        put32(d + info + 8 + k * 4, tab[k]);
        put32(d + pos, g_counts[k]);
        pos += 4 + g_counts[k] * 4;
    }
    recs = pos;
    for (i = 0; i < 2; i++) { /* SEQ: file i, bank i, volume, cpr, ppr, player */
        put32(d + info + tab[0] + 4 + i * 4, recs - info);
        put32(d + recs, i);
        put16(d + recs + 4, i);
        d[recs + 6] = (u8)(100 + i);
        d[recs + 7] = (u8)(10 + i);
        d[recs + 8] = (u8)(20 + i);
        d[recs + 9] = (u8)(1 + i);
        recs += 12;
    }
    for (i = 0; i < 2; i++) { /* BANK: file 2+i, wave arc i */
        put32(d + info + tab[2] + 4 + i * 4, recs - info);
        put32(d + recs, 2 + i);
        put16(d + recs + 4, i);
        put16(d + recs + 6, 0xFFFF);
        put16(d + recs + 8, 0xFFFF);
        put16(d + recs + 10, 0xFFFF);
        recs += 12;
    }
    for (i = 0; i < 2; i++) { /* WAVARC: file 4+i */
        put32(d + info + tab[3] + 4 + i * 4, recs - info);
        put32(d + recs, 4 + i);
        recs += 4;
    }
    put32(d + info + tab[4] + 4, recs - info); /* PLAYER */
    d[recs] = 8;
    put16(d + recs + 2, 0x00FF);
    put32(d + recs + 4, 10200);
    recs += 8;
    memcpy(d + info, "INFO", 4);
    info_size = recs - info;
    put32(d + info + 4, info_size);

    /* FAT then FILE. */
    fat = recs;
    fat_size = 12 + 6 * 16;
    file = fat + fat_size;
    fpos = file + 8;
    for (i = 0; i < 6; i++) {
        make_file(d + fpos, magics[i], (u8)(i + 1), file_len[i]);
        put32(d + fat + 12 + i * 16, fpos);
        put32(d + fat + 12 + i * 16 + 4, file_len[i]);
        fpos += (file_len[i] + 3) & ~3u;
    }
    memcpy(d + fat, "FAT ", 4);
    put32(d + fat + 4, fat_size);
    put32(d + fat + 8, 6);
    memcpy(d + file, "FILE", 4);
    put32(d + file + 4, fpos - file);
    t->size = fpos;

    memcpy(d, "SDAT", 4);
    put32(d + 4, 0x0100FEFFu);
    put32(d + 8, t->size);
    put16(d + 0x0C, 0x40);
    put16(d + 0x0E, 4);
    put32(d + 0x10, symb);
    put32(d + 0x14, symb_size);
    put32(d + 0x18, info);
    put32(d + 0x1C, info_size);
    put32(d + 0x20, fat);
    put32(d + 0x24, fat_size);
    put32(d + 0x28, file);
    put32(d + 0x2C, fpos - file);
}

/* ------------------------------------------------------------------ */
/* Preservation: nothing original changed                              */
/* ------------------------------------------------------------------ */

static int in_range(u32 i, u32 lo, u32 hi) { return i >= lo && i < hi; }

/* Every original byte is equal except the header's size and block fields and
 * the size and table-offset fields at the head of INFO and SYMB. */
static int bytes_preserved(const u8 *base, u32 bs, const u8 *out)
{
    u32 info = (u32)base[0x18] | ((u32)base[0x19] << 8) | ((u32)base[0x1A] << 16)
             | ((u32)base[0x1B] << 24);
    u32 symb = (u32)base[0x10] | ((u32)base[0x11] << 8) | ((u32)base[0x12] << 16)
             | ((u32)base[0x13] << 24);
    u32 i, bad = 0;

    for (i = 0; i < bs; i++) {
        if (base[i] == out[i]) {
            continue;
        }
        if (in_range(i, 0x08, 0x0C) || in_range(i, 0x10, 0x28)
            || in_range(i, info + 4, info + 0x28) || in_range(i, symb + 4, symb + 0x28)) {
            continue;
        }
        bad++;
    }
    return bad == 0;
}

/* Every original record, name and file reads the same in both parses. */
static int records_preserved(const BwSdat *a, const BwSdat *b)
{
    u32 i, n;
    BwBlob fa, fb;

    n = bw_sdat_count(a, BW_SDAT_SEQ);
    for (i = 0; i < n; i++) {
        BwSdatSeqRecord x, y;
        int ra = bw_sdat_seq_record(a, i, &x), rb = bw_sdat_seq_record(b, i, &y);
        /* Field by field: the structs have padding bytes. */
        if (ra != rb) return 0;
        if (ra == 0 && (x.file_id != y.file_id || x.bank != y.bank || x.volume != y.volume
                        || x.channel_pressure != y.channel_pressure
                        || x.player_priority != y.player_priority
                        || x.player_no != y.player_no)) return 0;
    }
    n = bw_sdat_count(a, BW_SDAT_BANK);
    for (i = 0; i < n; i++) {
        BwSdatBankRecord x, y;
        int ra = bw_sdat_bank_record(a, i, &x), rb = bw_sdat_bank_record(b, i, &y);
        if (ra != rb || (ra == 0 && memcmp(&x, &y, sizeof(x)) != 0)) return 0;
    }
    n = bw_sdat_count(a, BW_SDAT_WAVARC);
    for (i = 0; i < n; i++) {
        u32 x = 0, y = 0;
        int ra = bw_sdat_wavarc_fileid(a, i, &x), rb = bw_sdat_wavarc_fileid(b, i, &y);
        if (ra != rb || x != y) return 0;
    }
    n = bw_sdat_count(a, BW_SDAT_PLAYER);
    if (bw_sdat_count(b, BW_SDAT_PLAYER) != n) return 0;
    for (i = 0; i < n; i++) {
        BwSdatPlayerRecord x, y;
        int ra = bw_sdat_player_record(a, i, &x), rb = bw_sdat_player_record(b, i, &y);
        if (ra != rb) return 0;
        if (ra == 0 && (x.seq_max != y.seq_max || x.channel_bitmask != y.channel_bitmask
                        || x.heap_size != y.heap_size)) return 0;
    }
    for (i = 0; i < 8; i++) {
        u32 j;
        n = bw_sdat_count(a, (BwSdatKind)i);
        for (j = 0; j < n; j++) {
            const char *na = bw_sdat_name(a, (BwSdatKind)i, j);
            const char *nb = bw_sdat_name(b, (BwSdatKind)i, j);
            if ((na == NULL) != (nb == NULL) || (na != NULL && strcmp(na, nb) != 0)) return 0;
        }
    }
    for (i = 0; i < a->fat_count; i++) {
        if (bw_sdat_raw_file(a, i, &fa) != 0 || bw_sdat_raw_file(b, i, &fb) != 0
            || fa.size != fb.size || memcmp(fa.data, fb.data, fa.size) != 0) return 0;
    }
    return 1;
}

/* ------------------------------------------------------------------ */
/* Cases                                                               */
/* ------------------------------------------------------------------ */

static int g_failed_here;

#define CHECK(label, cond) do { bw_checks++; if (!(cond)) { bw_failures++; \
    printf("  FAIL %-40s (%s)\n", (label), #cond); g_failed_here = 1; } } while (0)

static int test_parse(void)
{
    BwSdatDecl d;
    char err[200];
    const char *good =
        "# a comment\n"
        "wavarc w1 = w1.swar   # trailing comment\r\n"
        "\n"
        "bank b1 = b1.sbnk wavarc=w1,#5,Other:w\n"
        "seq s1 = s1.sseq bank=#7 volume=100 cpr=3 ppr=4 player=5\n"
        "seq s2 = s2.sseq bank=b1\n";

    BW_REQUIRE("parse good", bw_sdat_parse_decl(good, &d, err, sizeof(err)) == 0);
    BW_CHECK_EQ("entries", d.count, 4);
    BW_CHECK_EQ("kind0", d.entries[0].kind, BW_SDATM_WAVARC);
    BW_CHECK_EQ("bank refs", d.entries[1].nrefs, 3);
    BW_CHECK_EQ("bank ref 1", strcmp(d.entries[1].refs[1], "#5"), 0);
    BW_CHECK_EQ("bank ref 2", strcmp(d.entries[1].refs[2], "Other:w"), 0);
    BW_CHECK_EQ("seq bank ref", strcmp(d.entries[2].refs[0], "#7"), 0);
    BW_CHECK_EQ("seq volume", d.entries[2].volume, 100);
    BW_CHECK_EQ("seq cpr", d.entries[2].cpr, 3);
    BW_CHECK_EQ("seq ppr", d.entries[2].ppr, 4);
    BW_CHECK_EQ("seq player", d.entries[2].player, 5);
    BW_CHECK_EQ("default volume", d.entries[3].volume, 127);
    BW_CHECK_EQ("default cpr", d.entries[3].cpr, 64);
    BW_CHECK_EQ("default player", d.entries[3].player, 0);
    bw_sdat_decl_free(&d);

    BW_CHECK_EQ("dup name", bw_sdat_parse_decl("wavarc a = a\nwavarc a = b\n", &d, err, sizeof(err)), -1);
    BW_CHECK_EQ("bad kind", bw_sdat_parse_decl("song a = a\n", &d, err, sizeof(err)), -1);
    BW_CHECK_EQ("volume range", bw_sdat_parse_decl("seq a = a bank=#0 volume=128\n", &d, err, sizeof(err)), -1);
    BW_CHECK_EQ("player range", bw_sdat_parse_decl("seq a = a bank=#0 player=8\n", &d, err, sizeof(err)), -1);
    BW_CHECK_EQ("no bank", bw_sdat_parse_decl("seq a = a\n", &d, err, sizeof(err)), -1);
    BW_CHECK_EQ("no wavarc", bw_sdat_parse_decl("bank a = a\n", &d, err, sizeof(err)), -1);
    BW_CHECK_EQ("five wavarcs", bw_sdat_parse_decl("bank a = a wavarc=a,b,c,d,e\n", &d, err, sizeof(err)), -1);
    BW_CHECK_EQ("key on wrong kind", bw_sdat_parse_decl("wavarc a = a volume=1\n", &d, err, sizeof(err)), -1);
    BW_CHECK_EQ("missing eq", bw_sdat_parse_decl("wavarc a a\n", &d, err, sizeof(err)), -1);
    BW_CHECK_EQ("bad name", bw_sdat_parse_decl("wavarc a:b = a\n", &d, err, sizeof(err)), -1);
    BW_CHECK_EQ("error text", strstr(err, "line 1") != NULL, 1);
    BW_CHECK_EQ("empty text", bw_sdat_parse_decl("# nothing\n", &d, err, sizeof(err)), 0);
    BW_CHECK_EQ("empty count", d.count, 0);
    bw_sdat_decl_free(&d);
    return g_failed_here;
}

static int test_synthetic(void)
{
    static Tiny t;
    u8 fa_w[40], fa_b[44], fa_s[48], fb_s2[36], fb_s3[36], bad[40];
    BwSdatDecl da, db;
    char err[200];
    BwSdatFile files_a[3], files_b[2];
    BwSdatAdd adds[2];
    u8 *out = NULL;
    u32 out_size = 0, id;
    BwSdat o, orig;
    BwSdatSeqRecord sr;
    BwSdatBankRecord br;
    BwBlob blob;
    u32 wf;
    u32 k;

    build_tiny(&t);
    make_file(fa_w, "SWAR", 21, sizeof(fa_w));
    make_file(fa_b, "SBNK", 22, sizeof(fa_b));
    make_file(fa_s, "SSEQ", 23, sizeof(fa_s));
    make_file(fb_s2, "SSEQ", 24, sizeof(fb_s2));
    make_file(fb_s3, "SSEQ", 25, sizeof(fb_s3));
    make_file(bad, "XXXX", 26, sizeof(bad));

    BW_REQUIRE("parse A", bw_sdat_parse_decl(
        "wavarc w1 = w1.swar\n"
        "seq s1 = s1.sseq bank=b1 volume=100 cpr=11 ppr=12 player=3\n"
        "bank b1 = b1.sbnk wavarc=w1,WAVE_B\n", &da, err, sizeof(err)) == 0);
    BW_REQUIRE("parse B", bw_sdat_parse_decl(
        "seq s2 = s2.sseq bank=A:b1\n"
        "seq s3 = s3.sseq bank=BANK_B volume=7\n", &db, err, sizeof(err)) == 0);
    files_a[0].name = "w1.swar"; files_a[0].data = fa_w; files_a[0].size = sizeof(fa_w);
    files_a[1].name = "b1.sbnk"; files_a[1].data = fa_b; files_a[1].size = sizeof(fa_b);
    files_a[2].name = "s1.sseq"; files_a[2].data = fa_s; files_a[2].size = sizeof(fa_s);
    files_b[0].name = "s2.sseq"; files_b[0].data = fb_s2; files_b[0].size = sizeof(fb_s2);
    files_b[1].name = "s3.sseq"; files_b[1].data = fb_s3; files_b[1].size = sizeof(fb_s3);
    adds[0].mod_name = "A"; adds[0].decl = &da; adds[0].files = files_a; adds[0].file_count = 3;
    adds[1].mod_name = "B"; adds[1].decl = &db; adds[1].files = files_b; adds[1].file_count = 2;

    BW_REQUIRE("merge", bw_vfs_sdat_merge(t.d, t.size, adds, 2, &out, &out_size) == 0);
    BW_REQUIRE("out set", out != NULL && out_size > t.size);
    BW_REQUIRE("open base", bw_sdat_open(&orig, t.d, t.size) == 0);
    BW_REQUIRE("open merged", bw_sdat_open(&o, out, out_size) == 0);

    BW_CHECK_EQ("header file size", (long)((u32)out[8] | ((u32)out[9] << 8) | ((u32)out[10] << 16)
                                          | ((u32)out[11] << 24)), (long)out_size);
    BW_CHECK_EQ("seq count", bw_sdat_count(&o, BW_SDAT_SEQ), 5);
    BW_CHECK_EQ("bank count", bw_sdat_count(&o, BW_SDAT_BANK), 3);
    BW_CHECK_EQ("wavarc count", bw_sdat_count(&o, BW_SDAT_WAVARC), 3);
    BW_CHECK_EQ("player count", bw_sdat_count(&o, BW_SDAT_PLAYER), 1);
    BW_CHECK_EQ("fat count", o.fat_count, 6 + 5);
    CHECK("original bytes", bytes_preserved(t.d, t.size, out));
    CHECK("original records", records_preserved(&orig, &o));

    /* Ids in load order: A then B. */
    BW_CHECK_EQ("A:s1 id", bw_sdat_lookup(out, out_size, BW_SDATM_SEQ, "A:s1", &id), 0);
    BW_CHECK_EQ("A:s1 value", id, 2);
    BW_CHECK_EQ("B:s2 id", bw_sdat_lookup(out, out_size, BW_SDATM_SEQ, "B:s2", &id), 0);
    BW_CHECK_EQ("B:s2 value", id, 3);
    BW_CHECK_EQ("B:s3 id", bw_sdat_lookup(out, out_size, BW_SDATM_SEQ, "B:s3", &id), 0);
    BW_CHECK_EQ("B:s3 value", id, 4);
    BW_CHECK_EQ("A:b1", bw_sdat_lookup(out, out_size, BW_SDATM_BANK, "A:b1", &id), 0);
    BW_CHECK_EQ("A:b1 value", id, 2);
    BW_CHECK_EQ("A:w1", bw_sdat_lookup(out, out_size, BW_SDATM_WAVARC, "A:w1", &id), 0);
    BW_CHECK_EQ("A:w1 value", id, 2);
    BW_CHECK_EQ("vanilla name", bw_sdat_lookup(out, out_size, BW_SDATM_SEQ, "SEQ_B", &id), 0);
    BW_CHECK_EQ("vanilla value", id, 1);
    BW_CHECK_EQ("absent", bw_sdat_lookup(out, out_size, BW_SDATM_SEQ, "A:nope", &id), -1);
    BW_CHECK_EQ("wrong kind", bw_sdat_lookup(out, out_size, BW_SDATM_BANK, "A:s1", &id), -1);
    BW_CHECK_EQ("find via reader", bw_sdat_find(&o, BW_SDAT_SEQ, "B:s3"), 4);
    CHECK("name via reader", strcmp(bw_sdat_name(&o, BW_SDAT_BANK, 2), "A:b1") == 0);

    /* Records. */
    BW_REQUIRE("A:s1 record", bw_sdat_seq_record(&o, 2, &sr) == 0);
    BW_CHECK_EQ("s1 bank", sr.bank, 2);
    BW_CHECK_EQ("s1 volume", sr.volume, 100);
    BW_CHECK_EQ("s1 cpr", sr.channel_pressure, 11);
    BW_CHECK_EQ("s1 ppr", sr.player_priority, 12);
    BW_CHECK_EQ("s1 player", sr.player_no, 3);
    BW_CHECK_EQ("s1 file id", sr.file_id, 6 + 2);   /* wavarc, bank, then seq */
    BW_REQUIRE("B:s2 record", bw_sdat_seq_record(&o, 3, &sr) == 0);
    BW_CHECK_EQ("s2 bank (A:b1)", sr.bank, 2);
    BW_CHECK_EQ("s2 default volume", sr.volume, 127);
    BW_REQUIRE("B:s3 record", bw_sdat_seq_record(&o, 4, &sr) == 0);
    BW_CHECK_EQ("s3 bank (BANK_B)", sr.bank, 1);
    BW_CHECK_EQ("s3 volume", sr.volume, 7);
    BW_REQUIRE("A:b1 record", bw_sdat_bank_record(&o, 2, &br) == 0);
    BW_CHECK_EQ("b1 wave 0 (A:w1)", br.wave_arc[0], 2);
    BW_CHECK_EQ("b1 wave 1 (WAVE_B)", br.wave_arc[1], 1);
    BW_CHECK_EQ("b1 wave 2", br.wave_arc[2], 0xFFFF);
    BW_CHECK_EQ("b1 wave 3", br.wave_arc[3], 0xFFFF);
    BW_REQUIRE("A:w1 file", bw_sdat_wavarc_fileid(&o, 2, &wf) == 0);
    BW_CHECK_EQ("w1 file id", wf, 6);

    /* File bytes. */
    BW_REQUIRE("w1 bytes", bw_sdat_raw_file(&o, 6, &blob) == 0);
    BW_CHECK_EQ("w1 size", blob.size, sizeof(fa_w));
    CHECK("w1 content", memcmp(blob.data, fa_w, sizeof(fa_w)) == 0);
    BW_REQUIRE("b1 bytes", bw_sdat_raw_file(&o, 7, &blob) == 0);
    CHECK("b1 content", blob.size == sizeof(fa_b) && memcmp(blob.data, fa_b, sizeof(fa_b)) == 0);
    BW_REQUIRE("s1 bytes", bw_sdat_raw_file(&o, 8, &blob) == 0);
    CHECK("s1 content", blob.size == sizeof(fa_s) && memcmp(blob.data, fa_s, sizeof(fa_s)) == 0);
    BW_REQUIRE("s3 bytes", bw_sdat_raw_file(&o, 10, &blob) == 0);
    CHECK("s3 content", blob.size == sizeof(fb_s3) && memcmp(blob.data, fb_s3, sizeof(fb_s3)) == 0);
    for (k = 6; k < 11; k++) {
        BW_REQUIRE("new file", bw_sdat_raw_file(&o, k, &blob) == 0);
        CHECK("new file after old data", (size_t)(blob.data - out) >= t.size);
        CHECK("new file aligned", ((blob.data - out) & 3) == 0);
    }
    bw_sdat_close(&o);
    bw_sdat_close(&orig);
    free(out);

    /* Load order matters: B then A gives B's first sequence id 2. */
    {
        BwSdatAdd rev[2];
        rev[0] = adds[1];
        rev[1] = adds[0];
        /* B refers to A:b1, which a reversed list still resolves (ids are
         * assigned before references are read). */
        BW_REQUIRE("merge reversed", bw_vfs_sdat_merge(t.d, t.size, rev, 2, &out, &out_size) == 0);
        BW_CHECK_EQ("B first", bw_sdat_lookup(out, out_size, BW_SDATM_SEQ, "B:s2", &id), 0);
        BW_CHECK_EQ("B:s2 now 2", id, 2);
        BW_CHECK_EQ("A:s1 now 4", bw_sdat_lookup(out, out_size, BW_SDATM_SEQ, "A:s1", &id), 0);
        BW_CHECK_EQ("A:s1 value 4", id, 4);
        free(out);
        out = NULL;
    }

    /* No mods: a plain copy. */
    BW_REQUIRE("merge none", bw_vfs_sdat_merge(t.d, t.size, NULL, 0, &out, &out_size) == 0);
    CHECK("copy", out_size == t.size && memcmp(out, t.d, t.size) == 0);
    free(out);
    out = NULL;

    /* Failures: an unresolved reference, a wrong magic, a missing file. */
    {
        BwSdatDecl dx;
        BwSdatFile fx[1];
        BwSdatAdd ax;

        ax.mod_name = "X"; ax.decl = &dx; ax.files = fx; ax.file_count = 1;
        fx[0].name = "s.sseq"; fx[0].data = fa_s; fx[0].size = sizeof(fa_s);

        BW_REQUIRE("parse X1", bw_sdat_parse_decl("seq s = s.sseq bank=NoSuchBank\n", &dx, err, sizeof(err)) == 0);
        BW_CHECK_EQ("unresolved", bw_vfs_sdat_merge(t.d, t.size, &ax, 1, &out, &out_size), -1);
        CHECK("no output on failure", out == NULL);
        bw_sdat_decl_free(&dx);

        BW_REQUIRE("parse X2", bw_sdat_parse_decl("seq s = s.sseq bank=#2\n", &dx, err, sizeof(err)) == 0);
        BW_CHECK_EQ("bank id out of range", bw_vfs_sdat_merge(t.d, t.size, &ax, 1, &out, &out_size), -1);
        bw_sdat_decl_free(&dx);

        BW_REQUIRE("parse X3", bw_sdat_parse_decl("seq s = s.sseq bank=#1\n", &dx, err, sizeof(err)) == 0);
        fx[0].data = bad; fx[0].size = sizeof(bad);
        BW_CHECK_EQ("wrong magic", bw_vfs_sdat_merge(t.d, t.size, &ax, 1, &out, &out_size), -1);
        fx[0].name = "other.sseq";
        fx[0].data = fa_s; fx[0].size = sizeof(fa_s);
        BW_CHECK_EQ("missing file", bw_vfs_sdat_merge(t.d, t.size, &ax, 1, &out, &out_size), -1);
        fx[0].name = "s.sseq";
        BW_CHECK_EQ("ok after fixes", bw_vfs_sdat_merge(t.d, t.size, &ax, 1, &out, &out_size), 0);
        free(out);
        out = NULL;
        bw_sdat_decl_free(&dx);
    }
    bw_sdat_decl_free(&da);
    bw_sdat_decl_free(&db);
    return g_failed_here;
}

/* ------------------------------------------------------------------ */
/* The real archive                                                    */
/* ------------------------------------------------------------------ */

static int test_real(const char *root)
{
    char path[1024];
    FILE *fh;
    long n;
    u8 *base;
    u8 w[64], b[64], s[64], *out = NULL;
    u32 out_size = 0, id, nseq, nbank, nwav, nfat;
    BwSdat o, orig;
    BwSdatDecl d;
    BwSdatFile f[3];
    BwSdatAdd a;
    char err[200];
    BwSdatSeqRecord sr;
    BwBlob blob;

    snprintf(path, sizeof(path), "%s/wb_sound_data.sdat", root);
    fh = fopen(path, "rb");
    if (fh == NULL) {
        printf("SKIP: real archive not found\n");
        return 0;
    }
    fseek(fh, 0, SEEK_END);
    n = ftell(fh);
    rewind(fh);
    base = (u8 *)malloc((size_t)n);
    BW_REQUIRE("alloc", base != NULL && fread(base, 1, (size_t)n, fh) == (size_t)n);
    fclose(fh);

    make_file(w, "SWAR", 1, sizeof(w));
    make_file(b, "SBNK", 2, sizeof(b));
    make_file(s, "SSEQ", 3, sizeof(s));
    f[0].name = "w"; f[0].data = w; f[0].size = sizeof(w);
    f[1].name = "b"; f[1].data = b; f[1].size = sizeof(b);
    f[2].name = "s"; f[2].data = s; f[2].size = sizeof(s);
    BW_REQUIRE("parse", bw_sdat_parse_decl(
        "wavarc w = w\nbank b = b wavarc=w,#1\nseq s = s bank=b volume=90 player=1\n",
        &d, err, sizeof(err)) == 0);
    a.mod_name = "modx"; a.decl = &d; a.files = f; a.file_count = 3;

    BW_REQUIRE("open real", bw_sdat_open(&orig, base, (size_t)n) == 0);
    nseq = bw_sdat_count(&orig, BW_SDAT_SEQ);
    nbank = bw_sdat_count(&orig, BW_SDAT_BANK);
    nwav = bw_sdat_count(&orig, BW_SDAT_WAVARC);
    nfat = orig.fat_count;
    BW_REQUIRE("merge real", bw_vfs_sdat_merge(base, (u32)n, &a, 1, &out, &out_size) == 0);
    BW_REQUIRE("open merged real", bw_sdat_open(&o, out, out_size) == 0);

    BW_CHECK_EQ("real seq count", bw_sdat_count(&o, BW_SDAT_SEQ), nseq + 1);
    BW_CHECK_EQ("real bank count", bw_sdat_count(&o, BW_SDAT_BANK), nbank + 1);
    BW_CHECK_EQ("real wavarc count", bw_sdat_count(&o, BW_SDAT_WAVARC), nwav + 1);
    BW_CHECK_EQ("real fat count", o.fat_count, nfat + 3);
    CHECK("real original bytes", bytes_preserved(base, (u32)n, out));
    CHECK("real original records", records_preserved(&orig, &o));
    BW_CHECK_EQ("real new seq id", bw_sdat_lookup(out, out_size, BW_SDATM_SEQ, "modx:s", &id), 0);
    BW_CHECK_EQ("real new seq id value", id, nseq);
    BW_REQUIRE("real new record", bw_sdat_seq_record(&o, id, &sr) == 0);
    BW_CHECK_EQ("real new bank", sr.bank, nbank);
    BW_CHECK_EQ("real new volume", sr.volume, 90);
    BW_CHECK_EQ("real new file id", sr.file_id, nfat + 2);
    BW_REQUIRE("real new file", bw_sdat_raw_file(&o, sr.file_id, &blob) == 0);
    CHECK("real new file bytes", blob.size == sizeof(s) && memcmp(blob.data, s, sizeof(s)) == 0);

    bw_sdat_close(&o);
    bw_sdat_decl_free(&d);
    free(out);
    out = NULL;

    /* A sound past player 3's 10200-byte heap, from a mod whose name has a
     * space, and a second mod with the same key: player 3's record grows to
     * what the sound needs, the others stay, the sound heap grows by more
     * than the raise, and both mods' names are found. */
    {
        static u8 big[29168];
        BwSdatDecl d2;
        BwSdatAdd two[2];
        BwSdatPlayerRecord p0, p1;
        u32 i, id2;

        make_file(big, "SWAR", 4, sizeof(big));
        f[0].data = big; f[0].size = sizeof(big);
        BW_REQUIRE("parse big", bw_sdat_parse_decl(
            "wavarc w = w\nbank b = b wavarc=w\nseq s = s bank=b player=3\n", &d, err, sizeof(err)) == 0);
        BW_REQUIRE("parse small", bw_sdat_parse_decl("seq t = s bank=#1\n", &d2, err, sizeof(err)) == 0);
        two[0].mod_name = "Fairy Type"; two[0].decl = &d; two[0].files = f; two[0].file_count = 3;
        two[1].mod_name = "Fairy_Type"; two[1].decl = &d2; two[1].files = f + 2; two[1].file_count = 1;
        BW_REQUIRE("merge big", bw_vfs_sdat_merge(base, (u32)n, two, 2, &out, &out_size) == 0);
        BW_REQUIRE("open merged big", bw_sdat_open(&o, out, out_size) == 0);
        CHECK("big original bytes", bytes_preserved(base, (u32)n, out));
        for (i = 0; i < bw_sdat_count(&orig, BW_SDAT_PLAYER); i++) {
            BW_REQUIRE("players", bw_sdat_player_record(&orig, i, &p0) == 0
                                  && bw_sdat_player_record(&o, i, &p1) == 0);
            if (i == 3) {
                CHECK("player 3 raised", p1.heap_size > sizeof(big) && p0.heap_size == 10200);
                CHECK("heap extra covers it", bw_sdat_sound_heap_extra() > p1.heap_size - p0.heap_size);
            } else {
                CHECK("other players kept", p1.heap_size == p0.heap_size && p1.seq_max == p0.seq_max);
            }
            CHECK("channels kept", p1.channel_bitmask == p0.channel_bitmask);
        }
        BW_CHECK_EQ("key with a space", bw_sdat_lookup(out, out_size, BW_SDATM_SEQ, "Fairy_Type:s", &id), 0);
        BW_CHECK_EQ("name with a space", bw_sdat_lookup(out, out_size, BW_SDATM_SEQ, "Fairy Type:s", &id2), 0);
        BW_CHECK_EQ("same id", id2, id);
        BW_CHECK_EQ("second key", bw_sdat_lookup(out, out_size, BW_SDATM_SEQ, "Fairy_Type_2:t", &id2), 0);
        BW_CHECK_EQ("second id", id2, id + 1);
        bw_sdat_close(&o);
        bw_sdat_decl_free(&d);
        bw_sdat_decl_free(&d2);
        free(out);
        out = NULL;

        /* small sounds raise nothing */
        BW_REQUIRE("parse tiny", bw_sdat_parse_decl("seq t = s bank=#1 player=2\n", &d2, err, sizeof(err)) == 0);
        two[1].mod_name = "A: B";
        BW_REQUIRE("merge tiny", bw_vfs_sdat_merge(base, (u32)n, &two[1], 1, &out, &out_size) == 0);
        BW_REQUIRE("open merged tiny", bw_sdat_open(&o, out, out_size) == 0);
        CHECK("tiny players kept", records_preserved(&orig, &o));
        BW_CHECK_EQ("colon in a mod name", bw_sdat_lookup(out, out_size, BW_SDATM_SEQ, "A: B:t", &id), 0);
        BW_CHECK_EQ("its key", bw_sdat_lookup(out, out_size, BW_SDATM_SEQ, "A__B:t", &id2), 0);
        BW_CHECK_EQ("one id", id2, id);
        bw_sdat_close(&o);
        bw_sdat_decl_free(&d2);
        free(out);
        out = NULL;
    }

    bw_sdat_close(&orig);
    free(base);
    return g_failed_here;
}

int main(int argc, char **argv)
{
    int rc = 0;

    printf("test_vfs_sdat\n");
    rc |= test_parse();
    rc |= test_synthetic();
    if (argc > 1) {
        rc |= test_real(argv[1]);
    } else {
        printf("SKIP: no data root given\n");
    }
    if (rc != 0) {
        bw_failures++;
    }
    BW_REPORT("vfs_sdat");
}
