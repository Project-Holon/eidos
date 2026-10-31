/*
 * install/ndsrom.c -- identify a DS cartridge image and extract it.
 *
 * A C transcription of what aletheia's tools/ndsrom.py `extract` writes
 * (its module docstring is the format reference: the header fields, the
 * overlay table entry, the FAT and the FNT), plus the ARM9 decompression
 * tools/delink.py does into arm9_dec.bin.  It refuses what ndsrom.py
 * refuses, with the same checks in the same order, and where ndsrom.py
 * would crash rather than refuse (a cyclic FNT recurses until Python gives
 * up) this refuses with a message.
 *
 * The work is done in two passes.  The first reads the tables and builds
 * the complete list of regions to write, checking every one against the
 * image and against each other (ndsrom.py's RegionMap: nothing may run past
 * the end, nothing may overlap).  Only if the whole plan is sound does the
 * second pass write anything, so a malformed image leaves no files behind.
 * The second pass writes into a staging directory that is renamed into
 * place only when complete (see bw_rom_extract in install.h).
 *
 * Beyond ndsrom.py, it writes what tools/delink.py derives from the same
 * image: arm9_dec.bin and overlays_dec/ (the ARM9 binary and every
 * compressed ARM9 overlay, BLZ-decompressed), which the port and its tests
 * read.  bw_rom_identify and bw_rom_verdict decide whether an image is the
 * dump eidos supports and say why not in words a player can act on.
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "install.h"

#define HEADER_SNAPSHOT     0x1000u
#define OVERLAY_ENTRY_SIZE  32u
#define FAT_ENTRY_SIZE      8u
#define FNT_MAIN_ENTRY_SIZE 8u
#define PATH_MAX_LEN        1024u
#define COPY_BLOCK          (256u * 1024u)

/* ------------------------------------------------------------------ */
/* Small helpers                                                       */
/* ------------------------------------------------------------------ */

static u16 rd16(const u8 *p) { return (u16)(p[0] | (p[1] << 8)); }
static u32 rd32(const u8 *p)
{
    return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}

static void say(char *err, size_t err_len, const char *fmt, ...)
{
    va_list ap;

    if (err != NULL && err_len > 0) {
        va_start(ap, fmt);
        vsnprintf(err, err_len, fmt, ap);
        va_end(ap);
    }
}

/* ------------------------------------------------------------------ */
/* The name mangling, exactly ndsrom.py's sanitize_name                 */
/* ------------------------------------------------------------------ */

static const char *const WIN_RESERVED[] = {
    "CON", "PRN", "AUX", "NUL",
    "COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7", "COM8", "COM9",
    "LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9",
};

void bw_fnt_sanitize(const u8 *raw, u32 len, char *out, size_t out_size)
{
    char tmp[260];
    char stem[260];
    size_t n = 0;
    size_t i;
    size_t k;

    if (out_size == 0) {
        return;
    }
    /* `[\x00-\x1f<>:"/\\|?*]` -> '_', and every byte a strict ASCII decode
     * refuses (>= 0x80) -> U+FFFD -> '_'. */
    for (i = 0; i < len && n < sizeof(tmp) - 2; i++) {
        u8 c = raw[i];
        if (c < 0x20 || c >= 0x80 || strchr("<>:\"/\\|?*", c) != NULL) {
            tmp[n++] = '_';
        } else {
            tmp[n++] = (char)c;
        }
    }
    /* rstrip(" .") */
    while (n > 0 && (tmp[n - 1] == ' ' || tmp[n - 1] == '.')) {
        n--;
    }
    if (n == 0) {
        tmp[n++] = '_';
    }
    tmp[n] = '\0';

    /* name.split(".")[0].upper() in the reserved set -> "_" + name */
    for (k = 0; k < n && tmp[k] != '.'; k++) {
        char c = tmp[k];
        stem[k] = (c >= 'a' && c <= 'z') ? (char)(c - 32) : c;
    }
    stem[k] = '\0';
    for (i = 0; i < sizeof(WIN_RESERVED) / sizeof(WIN_RESERVED[0]); i++) {
        if (strcmp(stem, WIN_RESERVED[i]) == 0) {
            snprintf(out, out_size, "_%s", tmp);
            return;
        }
    }
    snprintf(out, out_size, "%s", tmp);
}

/* ------------------------------------------------------------------ */
/* Reading the image                                                   */
/* ------------------------------------------------------------------ */

typedef struct Rom {
    FILE *fp;
    u32   size;
    u8    hdr[HEADER_SNAPSHOT];
} Rom;

static int rom_read(Rom *r, u32 off, void *dst, u32 len)
{
    if (off > r->size || len > r->size - off) {
        return -1;
    }
    if (len == 0) {
        return 0;
    }
    if (fseek(r->fp, (long)off, SEEK_SET) != 0) {
        return -1;
    }
    return fread(dst, 1, len, r->fp) == len ? 0 : -1;
}

/* What a player most often picks instead of the .nds itself: the archive
 * it was shipped in.  Recognised by magic so the message can say so. */
static const char *archive_kind(const u8 *p, u32 n)
{
    if (n >= 4 && p[0] == 'P' && p[1] == 'K' && p[2] == 3 && p[3] == 4) {
        return "a ZIP archive";
    }
    if (n >= 6 && memcmp(p, "7z\xBC\xAF\x27\x1C", 6) == 0) {
        return "a 7-Zip archive";
    }
    if (n >= 4 && memcmp(p, "Rar!", 4) == 0) {
        return "a RAR archive";
    }
    if (n >= 2 && p[0] == 0x1F && p[1] == 0x8B) {
        return "a gzip archive";
    }
    return NULL;
}

static int rom_open(Rom *r, const char *path, char *err, size_t err_len)
{
    u64 size;
    u8 magic[8];
    size_t got;
    const char *kind;

    memset(r, 0, sizeof(*r));
    r->fp = bw_fopen_utf8(path, "rb");
    if (r->fp == NULL) {
        say(err, err_len, "cannot open %s", path);
        return -1;
    }
    if (bw_file_size(r->fp, &size) != 0) {
        say(err, err_len, "cannot determine the size of %s", path);
        goto fail;
    }
    got = fread(magic, 1, sizeof(magic), r->fp);
    kind = archive_kind(magic, (u32)got);
    if (kind != NULL) {
        say(err, err_len, "%s is %s, not a cartridge image: unpack it and "
            "choose the .nds file inside", path, kind);
        goto fail;
    }
    if (size > BW_ROM_MAX_SIZE) {
        say(err, err_len, "%s is %llu bytes, larger than any DS cartridge: "
            "it is not a cartridge image", path, (unsigned long long)size);
        goto fail;
    }
    r->size = (u32)size;
    if (r->size < HEADER_SNAPSHOT) {
        say(err, err_len, "%s is 0x%X bytes, smaller than a cartridge header",
            path, (unsigned)r->size);
        goto fail;
    }
    if (rom_read(r, 0, r->hdr, HEADER_SNAPSHOT) != 0) {
        say(err, err_len, "cannot read the header of %s", path);
        goto fail;
    }
    return 0;

fail:
    fclose(r->fp);
    r->fp = NULL;
    return -1;
}

static void rom_close(Rom *r)
{
    if (r->fp != NULL) {
        fclose(r->fp);
        r->fp = NULL;
    }
}

static u32 H32(const Rom *r, u32 off) { return rd32(r->hdr + off); }

/* ------------------------------------------------------------------ */
/* Identify                                                            */
/* ------------------------------------------------------------------ */

static void copy_field(char *dst, const u8 *src, u32 n)
{
    u32 i;

    for (i = 0; i < n; i++) {
        u8 c = src[i];
        dst[i] = (c >= 0x20 && c < 0x7F) ? (char)c : (c == 0 ? '\0' : '?');
    }
    dst[n] = '\0';
    /* rstrip NULs, as ndsrom.py does for the title */
    while (n > 0 && dst[n - 1] == '\0') {
        n--;
    }
}

/* The header checksum at 0x15E: CRC-16 (polynomial 0xA001 reflected,
 * initial value 0xFFFF) of bytes 0x000..0x15D, which the console's boot
 * code checks before it will run a cartridge. */
static u16 header_crc(const u8 *h)
{
    u32 crc = 0xFFFFu;
    u32 i, b;

    for (i = 0; i < 0x15Eu; i++) {
        crc ^= h[i];
        for (b = 0; b < 8; b++) {
            crc = (crc & 1u) ? (crc >> 1) ^ 0xA001u : crc >> 1;
        }
    }
    return (u16)crc;
}

/* The SHA-1 of every dump eidos knows to be good. */
static const struct {
    const char *gamecode;
    u32         size;
    const char *sha1;
} KNOWN_DUMPS[] = {
    { "IRBO", BW_BLACK_SIZE, BW_BLACK_SHA1 },
    /* the other cartridges the port lays out as English Black's
     * (sdk/langmap.h), aletheia docs/ROMS.md */
    { "IRAO", BW_BLACK_SIZE, "bc696a0dfb448c7b3a8a206f0f8214411a039208" },
    { "IRBF", BW_BLACK_SIZE, "a559da54839663d8f8f3f03382e5665a61873d22" },
    { "IRAF", BW_BLACK_SIZE, "28f075915dd445cbfd902f1e1a94d5048b03a095" },
    { "IRBD", BW_BLACK_SIZE, "f5688bd617be7e67a150732f42cfae6ae104b2cb" },
    { "IRAD", BW_BLACK_SIZE, "fb752b211351f8480e3e1949e11a1d7741f05f5a" },
    { "IRBI", BW_BLACK_SIZE, "699f8c6a8390b5e30236b6c386bc3c4bd7bdc44e" },
    { "IRAI", BW_BLACK_SIZE, "2e33164d26236dd499f75d1de3123d85e822d502" },
    { "IRBS", BW_BLACK_SIZE, "68b55b82f5a033f9e9666c232f429e31ed658e54" },
    { "IRAS", BW_BLACK_SIZE, "4cc963ca925409ff0f12f3c417d1cbe1094bf934" },
};

static int is_known(const char *gamecode, const char *sha1_hex)
{
    size_t i;

    for (i = 0; i < sizeof(KNOWN_DUMPS) / sizeof(KNOWN_DUMPS[0]); i++) {
        if (strcmp(gamecode, KNOWN_DUMPS[i].gamecode) == 0
            && strcmp(sha1_hex, KNOWN_DUMPS[i].sha1) == 0) {
            return 1;
        }
    }
    return 0;
}

int bw_rom_known_sha1(const char *sha1_hex)
{
    size_t i;

    for (i = 0; i < sizeof(KNOWN_DUMPS) / sizeof(KNOWN_DUMPS[0]); i++) {
        if (strcmp(sha1_hex, KNOWN_DUMPS[i].sha1) == 0) {
            return 1;
        }
    }
    return 0;
}

/* Hash the open image from the start; for a trimmed one also the padded
 * form (`padded` NULL to skip).  0 or -1 with `err` filled. */
static int hash_image(Rom *r, const char *path, u32 pad_to, BwSha1 *sha,
                      BwSha1 *padded, BwProgressFn progress, void *ctx,
                      char *err, size_t err_len)
{
    u8 *buf = (u8 *)malloc(COPY_BLOCK);
    u32 total = (padded != NULL && pad_to > r->size) ? pad_to : r->size;
    u32 done = 0;

    if (buf == NULL) {
        say(err, err_len, "out of memory");
        return -1;
    }
    bw_sha1_init(sha);
    if (fseek(r->fp, 0, SEEK_SET) != 0) {
        say(err, err_len, "cannot rewind %s", path);
        free(buf);
        return -1;
    }
    while (done < r->size) {
        u32 n = r->size - done < COPY_BLOCK ? r->size - done : COPY_BLOCK;
        if (fread(buf, 1, n, r->fp) != n) {
            say(err, err_len, "read error in %s at 0x%X", path, (unsigned)done);
            free(buf);
            return -1;
        }
        bw_sha1_update(sha, buf, n);
        done += n;
        if (progress != NULL && progress(ctx, done, total, "hashing") != 0) {
            say(err, err_len, "cancelled");
            free(buf);
            return BW_CANCELLED;
        }
    }
    if (padded != NULL && pad_to > r->size) {
        *padded = *sha;
        memset(buf, 0xFF, COPY_BLOCK);
        while (done < pad_to) {
            u32 n = pad_to - done < COPY_BLOCK ? pad_to - done : COPY_BLOCK;
            bw_sha1_update(padded, buf, n);
            done += n;
            if (progress != NULL && progress(ctx, done, total, "hashing") != 0) {
                say(err, err_len, "cancelled");
                free(buf);
                return BW_CANCELLED;
            }
        }
    }
    free(buf);
    return 0;
}

int bw_rom_identify(const char *rom_path, BwRomInfo *info,
                    BwProgressFn progress, void *ctx,
                    char *err, size_t err_len)
{
    Rom r;
    BwSha1 sha, padded;
    u8 cap;

    memset(info, 0, sizeof(*info));
    if (rom_open(&r, rom_path, err, err_len) != 0) {
        return -1;
    }
    copy_field(info->title, r.hdr + 0x00, 12);
    copy_field(info->gamecode, r.hdr + 0x0C, 4);
    copy_field(info->maker, r.hdr + 0x10, 2);
    info->unit_code = r.hdr[0x12];
    info->rom_size = r.size;
    info->header_crc_ok = header_crc(r.hdr) == rd16(r.hdr + 0x15E);
    cap = r.hdr[0x14];
    info->capacity = cap <= 13 ? 0x20000u << cap : 0;   /* 128 KiB .. 1 GiB */
    info->trimmed = info->header_crc_ok && info->capacity != 0
                    && r.size < info->capacity;

    {
        int hrc = hash_image(&r, rom_path, info->capacity, &sha,
                             info->trimmed ? &padded : NULL, progress, ctx,
                             err, err_len);
        if (hrc != 0) {
            rom_close(&r);
            return hrc;
        }
    }
    rom_close(&r);
    bw_sha1_final(&sha, info->sha1);
    bw_sha1_hex(info->sha1, info->sha1_hex);
    if (info->trimmed) {
        u8 d[20];
        bw_sha1_final(&padded, d);
        bw_sha1_hex(d, info->padded_sha1_hex);
    }

    /* The game codes: IRB? is Black, IRA? is White (the last letter is the
     * region). */
    if (memcmp(info->gamecode, "IRB", 3) == 0) {
        info->game = BW_GAME_BLACK;
    } else if (memcmp(info->gamecode, "IRA", 3) == 0) {
        info->game = BW_GAME_WHITE;
    }
    info->verified = is_known(info->gamecode, info->sha1_hex)
        || (info->trimmed && is_known(info->gamecode, info->padded_sha1_hex));
    return 0;
}

static const char *region_name(char c)
{
    switch (c) {
    case 'O': return "USA/Europe";
    case 'E': return "USA";
    case 'P': return "European";
    case 'J': return "Japanese";
    case 'K': return "Korean";
    case 'D': return "German";
    case 'F': return "French";
    case 'I': return "Italian";
    case 'S': return "Spanish";
    default:  return "another region's";
    }
}

BwRomVerdict bw_rom_verdict(const BwRomInfo *info, char *msg, size_t msg_len)
{
    if (info->verified) {
        const char *game = info->gamecode[2] == 'A' ? "Pokemon White" : "Pokemon Black";
        const char *lang = info->gamecode[3] == 'O' ? "" : region_name(info->gamecode[3]);

        say(msg, msg_len, info->trimmed
            ? "%s%s%s%s, the known good dump (trimmed, but only of padding)"
            : "%s%s%s%s, the known good dump", game, lang[0] != '\0' ? " (" : "", lang,
            lang[0] != '\0' ? ")" : "");
        return BW_VERDICT_OK;
    }
    if (info->game == BW_GAME_UNKNOWN) {
        if (!info->header_crc_ok) {
            say(msg, msg_len, "This file is not a Nintendo DS cartridge image "
                "(its header checksum does not match). Choose the .nds file "
                "of your Pokemon Black dump.");
            return BW_VERDICT_NOT_DS;
        }
        say(msg, msg_len, "This is a DS game, but not Pokemon Black: its title "
            "is \"%s\" and its game code %s. eidos needs Pokemon Black "
            "(game code IRBO).", info->title, info->gamecode);
        return BW_VERDICT_OTHER_GAME;
    }
    if (strchr("OFDIS", info->gamecode[3]) == NULL) {
        say(msg, msg_len, "This is the %s release of Pokemon %s (game code %s). "
            "eidos plays the English, French, German, Italian and Spanish "
            "releases for now.", region_name(info->gamecode[3]),
            info->game == BW_GAME_WHITE ? "White" : "Black", info->gamecode);
        return BW_VERDICT_UNSUPPORTED;
    }
    if (!info->header_crc_ok) {
        say(msg, msg_len, "This looks like Pokemon Black, but its header is "
            "damaged (the header checksum does not match). Dump the "
            "cartridge again.");
        return BW_VERDICT_DAMAGED;
    }
    if (info->trimmed) {
        say(msg, msg_len, "This dump of Pokemon Black is shorter than the "
            "cartridge (%u of %u bytes) and does not match the known good "
            "dump even when padded back, so more than padding is missing. "
            "Dump the cartridge again, without trimming.",
            (unsigned)info->rom_size, (unsigned)info->capacity);
        return BW_VERDICT_DAMAGED;
    }
    say(msg, msg_len, "This is Pokemon Black, but not the known good dump "
        "(SHA-1 %s, expected %s%s). It is damaged, patched (a ROM hack or a "
        "translation) or a different revision. eidos needs an unmodified "
        "dump.", info->sha1_hex, BW_BLACK_SHA1,
        info->rom_size > info->capacity ? "; it is also larger than the "
        "cartridge" : "");
    return BW_VERDICT_MODIFIED;
}

/* ------------------------------------------------------------------ */
/* The plan                                                            */
/* ------------------------------------------------------------------ */

typedef struct Region {
    u32  offset;
    u32  size;
    char path[PATH_MAX_LEN];   /* relative, '/'-separated; "" = not written */
} Region;

typedef struct Plan {
    Region *r;
    u32     count;
    u32     cap;
} Plan;

static int plan_add(Plan *p, const Rom *rom, const char *name, u32 offset,
                    u32 size, const char *path, char *err, size_t err_len)
{
    Region *reg;

    if (size != 0 && (offset > rom->size || size > rom->size - offset)) {
        say(err, err_len, "region '%s' at 0x%X+0x%X runs past the end of the ROM (0x%X)",
            name, (unsigned)offset, (unsigned)size, (unsigned)rom->size);
        return -1;
    }
    if (p->count == p->cap) {
        u32 cap = p->cap ? p->cap * 2 : 256;
        Region *n = (Region *)realloc(p->r, cap * sizeof(Region));
        if (n == NULL) {
            say(err, err_len, "out of memory");
            return -1;
        }
        p->r = n;
        p->cap = cap;
    }
    reg = &p->r[p->count++];
    reg->offset = offset;
    reg->size = size;
    if (size == 0) {
        reg->path[0] = '\0';               /* recorded, not written */
    } else if (snprintf(reg->path, sizeof(reg->path), "%s", path)
               >= (int)sizeof(reg->path)) {
        say(err, err_len, "path too long: %s", path);
        return -1;
    }
    return 0;
}

static int cmp_region(const void *a, const void *b)
{
    const Region *x = *(const Region *const *)a;
    const Region *y = *(const Region *const *)b;

    if (x->offset != y->offset) {
        return x->offset < y->offset ? -1 : 1;
    }
    return x->size < y->size ? -1 : (x->size > y->size);
}

/* ndsrom.py's occupied_intervals: no two non-empty regions may overlap. */
static int plan_check_overlaps(const Plan *p, char *err, size_t err_len)
{
    const Region **v;
    u32 n = 0;
    u32 i;
    u32 end = 0;
    int have = 0;

    v = (const Region **)malloc((p->count + 1) * sizeof(*v));
    if (v == NULL) {
        say(err, err_len, "out of memory");
        return -1;
    }
    for (i = 0; i < p->count; i++) {
        if (p->r[i].size != 0) {
            v[n++] = &p->r[i];
        }
    }
    qsort(v, n, sizeof(*v), cmp_region);
    for (i = 0; i < n; i++) {
        if (have && v[i]->offset < end) {
            say(err, err_len, "overlapping regions detected near 0x%X (previous region ends at 0x%X)",
                (unsigned)v[i]->offset, (unsigned)end);
            free((void *)v);
            return -1;
        }
        if (!have || v[i]->offset + v[i]->size > end) {
            end = v[i]->offset + v[i]->size;
        }
        have = 1;
    }
    free((void *)v);
    return 0;
}

/* ------------------------------------------------------------------ */
/* The FNT walk (ndsrom.py's parse_fnt)                                 */
/* ------------------------------------------------------------------ */

typedef struct FntCtx {
    const u8 *fnt;
    u32       size;
    u32       dir_count;
    const u32 *fat;         /* start, end pairs */
    u32       fat_count;
    u8       *fid_seen;     /* one byte per FAT entry: 0 free, 1 fs, 2 overlay */
    Plan     *plan;
    const Rom *rom;
    u32       fs_files;
    char     *err;
    size_t    err_len;
} FntCtx;

static int fnt_walk(FntCtx *c, u32 dir, const char *prefix, u32 depth)
{
    u32 base;
    u32 p;
    u32 next_fid;

    if (dir >= c->dir_count) {
        say(c->err, c->err_len, "FNT directory index %u is out of range", (unsigned)dir);
        return -1;
    }
    /* A tree of dir_count directories is at most dir_count deep; anything
     * deeper is a cycle, which ndsrom.py would recurse on forever. */
    if (depth > c->dir_count) {
        say(c->err, c->err_len, "FNT directory %u: the directory tree is cyclic", (unsigned)dir);
        return -1;
    }
    base = dir * FNT_MAIN_ENTRY_SIZE;
    p = rd32(c->fnt + base);
    next_fid = rd16(c->fnt + base + 4);
    if (p >= c->size) {
        say(c->err, c->err_len, "FNT directory %u: subtable offset 0x%X is outside the FNT (size 0x%X)",
            (unsigned)dir, (unsigned)p, (unsigned)c->size);
        return -1;
    }
    for (;;) {
        u8 len;
        u32 name_len;
        char name[160];
        char path[PATH_MAX_LEN];

        if (p >= c->size) {
            say(c->err, c->err_len, "FNT directory %u: subtable ran off the end without a terminator",
                (unsigned)dir);
            return -1;
        }
        len = c->fnt[p++];
        if (len == 0) {
            break;
        }
        name_len = len & 0x7Fu;
        if (name_len == 0) {
            say(c->err, c->err_len, "FNT directory %u: zero-length name at offset 0x%X",
                (unsigned)dir, (unsigned)(p - 1));
            return -1;
        }
        /* Python slices past the end silently and then trips on the next
         * read; stop at the same place with the same verdict. */
        if (name_len > c->size - p) {
            say(c->err, c->err_len, "FNT directory %u: subtable ran off the end without a terminator",
                (unsigned)dir);
            return -1;
        }
        bw_fnt_sanitize(c->fnt + p, name_len, name, sizeof(name));
        p += name_len;
        if (snprintf(path, sizeof(path), "%s%s", prefix, name) >= (int)sizeof(path) - 2) {
            say(c->err, c->err_len, "FNT path too long under %s", prefix);
            return -1;
        }
        if (len & 0x80u) {
            u32 sub;
            if (p + 2 > c->size) {
                say(c->err, c->err_len, "FNT directory %u: truncated subdirectory id", (unsigned)dir);
                return -1;
            }
            sub = rd16(c->fnt + p);
            p += 2;
            strcat(path, "/");
            if (fnt_walk(c, sub & 0x0FFFu, path, depth + 1) != 0) {
                return -1;
            }
        } else {
            u32 fid = next_fid++;
            char rel[PATH_MAX_LEN + 8];

            if (fid < c->fat_count && c->fid_seen[fid] == 1) {
                say(c->err, c->err_len, "FNT lists file id %u more than once", (unsigned)fid);
                return -1;
            }
            if (fid >= c->fat_count) {
                say(c->err, c->err_len, "NitroFS file '%s': file id %u is outside the FAT",
                    path, (unsigned)fid);
                return -1;
            }
            if (c->fid_seen[fid] == 2) {
                say(c->err, c->err_len, "NitroFS file '%s' claims file id %u, which is already used by an overlay",
                    path, (unsigned)fid);
                return -1;
            }
            c->fid_seen[fid] = 1;
            snprintf(rel, sizeof(rel), "data/%s", path);
            if (plan_add(c->plan, c->rom, path, c->fat[2 * fid],
                         c->fat[2 * fid + 1] - c->fat[2 * fid], rel,
                         c->err, c->err_len) != 0) {
                return -1;
            }
            c->fs_files++;
        }
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Writing                                                             */
/* ------------------------------------------------------------------ */

static int join(char *out, size_t n, const char *dir, const char *rel)
{
    size_t dl = strlen(dir);
    int sep = dl > 0 && dir[dl - 1] != '/' && dir[dl - 1] != '\\';

    return snprintf(out, n, "%s%s%s", dir, sep ? "/" : "", rel) < (int)n ? 0 : -1;
}

static int ensure_parent(const char *full)
{
    char dir[PATH_MAX_LEN];
    char *slash;

    snprintf(dir, sizeof(dir), "%s", full);
    slash = strrchr(dir, '/');
    if (slash == NULL || slash == dir) {
        return 0;
    }
    *slash = '\0';
    return bw_mkdirs_utf8(dir);
}

static int write_bytes(const char *out_dir, const char *rel, const void *data,
                       u32 len, char *err, size_t err_len)
{
    char full[PATH_MAX_LEN];
    FILE *fp;

    if (join(full, sizeof(full), out_dir, rel) != 0 || ensure_parent(full) != 0) {
        say(err, err_len, "cannot create the directory for %s", rel);
        return -1;
    }
    fp = bw_fopen_utf8(full, "wb");
    if (fp == NULL) {
        say(err, err_len, "cannot write %s", full);
        return -1;
    }
    if (len != 0 && fwrite(data, 1, len, fp) != len) {
        fclose(fp);
        say(err, err_len, "write error on %s", full);
        return -1;
    }
    if (fclose(fp) != 0) {
        say(err, err_len, "write error on %s", full);
        return -1;
    }
    return 0;
}

static int copy_region(Rom *rom, const char *out_dir, const Region *reg,
                       u8 *buf, char *err, size_t err_len)
{
    char full[PATH_MAX_LEN];
    FILE *fp;
    u32 done = 0;

    if (join(full, sizeof(full), out_dir, reg->path) != 0 || ensure_parent(full) != 0) {
        say(err, err_len, "cannot create the directory for %s", reg->path);
        return -1;
    }
    fp = bw_fopen_utf8(full, "wb");
    if (fp == NULL) {
        say(err, err_len, "cannot write %s", full);
        return -1;
    }
    while (done < reg->size) {
        u32 n = reg->size - done < COPY_BLOCK ? reg->size - done : COPY_BLOCK;
        if (rom_read(rom, reg->offset + done, buf, n) != 0
            || fwrite(buf, 1, n, fp) != n) {
            fclose(fp);
            say(err, err_len, "copy error on %s", full);
            return -1;
        }
        done += n;
    }
    if (fclose(fp) != 0) {
        say(err, err_len, "write error on %s", full);
        return -1;
    }
    return 0;
}

/* A JSON string: printable ASCII as itself, everything else escaped. */
static void json_str(FILE *fp, const char *s)
{
    fputc('"', fp);
    for (; *s; s++) {
        u8 c = (u8)*s;
        if (c == '"' || c == '\\') {
            fprintf(fp, "\\%c", c);
        } else if (c < 0x20 || c >= 0x7F) {
            fprintf(fp, "\\u%04x", c);
        } else {
            fputc(c, fp);
        }
    }
    fputc('"', fp);
}

/* ------------------------------------------------------------------ */
/* The output directory: what is there, staging, the final rename      */
/* ------------------------------------------------------------------ */

#define MANIFEST_NAME "eidos_manifest.json"
#define PARTIAL_MARK  ".eidos_partial"

/* `out_dir` without trailing separators, refusing a drive or file system
 * root (there is no sibling to stage in, and no folder to rename). */
static int output_base(const char *out_dir, char *base, size_t n)
{
    size_t len = strlen(out_dir);

    while (len > 0 && (out_dir[len - 1] == '/' || out_dir[len - 1] == '\\')) {
        len--;
    }
    if (len == 0 || len + 16 >= n
        || (len == 2 && out_dir[1] == ':')) {
        return -1;
    }
    memcpy(base, out_dir, len);
    base[len] = '\0';
    return 0;
}

static int child_exists(const char *dir, const char *name)
{
    char path[PATH_MAX_LEN];

    return join(path, sizeof(path), dir, name) == 0
        && bw_path_kind_utf8(path) == BW_PATH_FILE;
}

enum { MAN_NONE, MAN_SAME, MAN_SAME_ROM, MAN_OTHER_ROM };

/*
 * What `dir`'s eidos_manifest.json says: no manifest of ours, this ROM by
 * this installer version, this ROM by another version, or another ROM (or
 * one whose ROM it does not record).  `other_sha` gets the recorded hash.
 */
static int manifest_state(const char *dir, const char *sha_hex, char other_sha[41])
{
    char path[PATH_MAX_LEN];
    char text[4096];
    size_t n;
    FILE *fp;
    const char *p;
    long version = -1;
    int same_rom = 0;

    other_sha[0] = '\0';
    if (join(path, sizeof(path), dir, MANIFEST_NAME) != 0
        || (fp = bw_fopen_utf8(path, "rb")) == NULL) {
        return MAN_NONE;
    }
    n = fread(text, 1, sizeof(text) - 1, fp);
    fclose(fp);
    text[n] = '\0';
    if (strstr(text, "\"tool\": \"eidos install\"") == NULL) {
        return MAN_NONE;
    }
    if ((p = strstr(text, "\"version\": ")) != NULL) {
        version = strtol(p + 11, NULL, 10);
    }
    if ((p = strstr(text, "\"rom_sha1\": \"")) != NULL && strlen(p + 13) >= 40) {
        memcpy(other_sha, p + 13, 40);
        other_sha[40] = '\0';
        same_rom = strcmp(other_sha, sha_hex) == 0;
    }
    if (!same_rom) {
        return MAN_OTHER_ROM;
    }
    return version == strtol(BW_INSTALL_VERSION, NULL, 10) ? MAN_SAME : MAN_SAME_ROM;
}

/*
 * Clear a staging or set-aside directory left by an earlier run: removed if
 * it is ours (it carries the marker or a manifest), refused otherwise.
 */
static int clear_leftover(const char *path, char *err, size_t err_len)
{
    int kind = bw_path_kind_utf8(path);

    if (kind == BW_PATH_NONE) {
        return 0;
    }
    if (kind != BW_PATH_DIR
        || (!child_exists(path, PARTIAL_MARK) && !child_exists(path, MANIFEST_NAME))) {
        say(err, err_len, "%s is in the way and was not made by eidos; move "
            "it somewhere else and try again", path);
        return -1;
    }
    if (bw_remove_tree_utf8(path) != 0) {
        say(err, err_len, "cannot remove %s, left by an interrupted "
            "extraction; delete it and try again", path);
        return -1;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Extract                                                             */
/* ------------------------------------------------------------------ */

/* One ARM9 overlay table entry, kept for overlays_dec/. */
typedef struct OvRec {
    u32 id;
    u32 offset;             /* the file in the ROM */
    u32 file_size;
    u32 code_size;          /* the decompressed size the table promises */
    u32 last;               /* compressed size (low 24 bits) and flags */
} OvRec;

#define OVL_FLAG_COMPRESSED 0x01u
#define MAX_OVERLAY_SIZE    (8u * 1024u * 1024u)   /* delink.py's MAX_SANE_SIZE */

/*
 * overlays_dec/y9_NNNN.bin for every ARM9 overlay the table flags
 * compressed: tools/delink.py's step 3, with its checks.  Overlays stored
 * uncompressed are not copied again (delink.py points at overlays/ for
 * them too).
 */
static int write_overlay_dec(Rom *rom, const char *dir, const OvRec *o,
                             char *err, size_t err_len)
{
    u32 comp = o->last & 0x00FFFFFFu;
    u8 *raw;
    u8 *img;
    u32 size;
    char rel[64];
    int rc = -1;

    if (comp == 0) {
        say(err, err_len, "overlay %u is flagged compressed but its compressed "
            "size is zero", (unsigned)o->id);
        return -1;
    }
    if (comp > o->file_size) {
        say(err, err_len, "overlay %u: compressed size 0x%X exceeds the file "
            "size 0x%X", (unsigned)o->id, (unsigned)comp, (unsigned)o->file_size);
        return -1;
    }
    if (o->code_size > MAX_OVERLAY_SIZE) {
        say(err, err_len, "overlay %u: implausible size 0x%X",
            (unsigned)o->id, (unsigned)o->code_size);
        return -1;
    }
    raw = (u8 *)malloc(comp);
    img = (u8 *)malloc(o->code_size ? o->code_size : 1u);
    if (raw == NULL || img == NULL) {
        say(err, err_len, "out of memory");
        goto out;
    }
    if (rom_read(rom, o->offset, raw, comp) != 0) {
        say(err, err_len, "cannot read overlay %u", (unsigned)o->id);
        goto out;
    }
    {
        char why[256];
        size = bw_blz_size(raw, comp, why, sizeof(why));
        if (size == 0) {
            say(err, err_len, "overlay %u: BLZ decode failed: %s", (unsigned)o->id, why);
            goto out;
        }
        if (size != o->code_size) {
            say(err, err_len, "overlay %u: footer says the image is 0x%X bytes "
                "but 0x%X was expected", (unsigned)o->id, (unsigned)size,
                (unsigned)o->code_size);
            goto out;
        }
        if (bw_blz_decompress(raw, comp, img, size, why, sizeof(why)) != 0) {
            say(err, err_len, "overlay %u: BLZ decode failed: %s", (unsigned)o->id, why);
            goto out;
        }
    }
    snprintf(rel, sizeof(rel), "overlays_dec/y9_%04u.bin", (unsigned)o->id);
    rc = write_bytes(dir, rel, img, size, err, err_len);
out:
    free(raw);
    free(img);
    return rc;
}

int bw_rom_extract(const char *rom_path, const char *out_dir,
                   const BwExtractOptions *opt, BwExtractResult *result,
                   BwProgressFn progress, void *ctx,
                   char *err, size_t err_len)
{
    static const BwExtractOptions defaults = { NULL, 0, NULL, 0 };
    Rom rom;
    Plan plan = { NULL, 0, 0 };
    BwExtractResult res;
    u8 *fnt = NULL;
    u32 *fat = NULL;
    u8 *fid_seen = NULL;
    u8 *table = NULL;
    u8 *buf = NULL;
    u8 *arm9 = NULL;
    u8 *arm9_dec = NULL;
    OvRec *ovs = NULL;
    u32 ov_count = 0;
    u32 fat_count = 0;
    u32 header_size, unit_code;
    int is_twl;
    int rc = -1;
    u32 i;
    u64 total = 0;
    u64 done = 0;
    u32 arm9_region = 0;
    u32 arm9_size = 0;
    u32 banner_version = 0;
    int have_banner = 0;
    char sha_hex[41];
    char base[PATH_MAX_LEN - 16];
    char staging[PATH_MAX_LEN];
    char old[PATH_MAX_LEN];
    int existing = 0;           /* 0 none, 1 an extraction to replace, 2 an empty dir */
    int staged = 0;
    int cancelled = 0;

    if (opt == NULL) {
        opt = &defaults;
    }
    memset(&res, 0, sizeof(res));
    if (output_base(out_dir, base, sizeof(base)) != 0) {
        say(err, err_len, "\"%s\" cannot hold the extraction: choose a folder "
            "(not a drive root)", out_dir);
        if (result != NULL) {
            *result = res;
        }
        return -1;
    }
    snprintf(staging, sizeof(staging), "%s.partial", base);
    snprintf(old, sizeof(old), "%s.old", base);
    if (rom_open(&rom, rom_path, err, err_len) != 0) {
        if (result != NULL) {
            *result = res;
        }
        return -1;
    }

    /* --- the image's hash, which the output records ----------------- */
    if (opt->rom_sha1_hex != NULL && strlen(opt->rom_sha1_hex) == 40) {
        memcpy(sha_hex, opt->rom_sha1_hex, 41);
    } else {
        BwSha1 sha;
        u8 d[20];
        int hrc = hash_image(&rom, rom_path, 0, &sha, NULL, progress, ctx, err, err_len);
        if (hrc != 0) {
            rc = hrc;
            goto done;
        }
        bw_sha1_final(&sha, d);
        bw_sha1_hex(d, sha_hex);
    }

    /* --- what is already at out_dir --------------------------------- */
    switch (bw_path_kind_utf8(base)) {
    case BW_PATH_NONE:
        break;
    case BW_PATH_FILE:
        say(err, err_len, "%s exists and is a file; choose a folder", base);
        goto done;
    default:
        if (bw_dir_is_empty_utf8(base) == 1) {
            existing = 2;
            break;
        }
        {
            char other[41];
            switch (manifest_state(base, sha_hex, other)) {
            case MAN_SAME:
                res.already = 1;
                rc = 0;
                goto done;
            case MAN_SAME_ROM:
                existing = 1;           /* an older installer's: redo it */
                break;
            case MAN_OTHER_ROM:
                if (!opt->replace) {
                    say(err, err_len, "%s already holds an extraction of a "
                        "different ROM (SHA-1 %s); replace it explicitly or "
                        "choose another folder", base,
                        other[0] ? other : "not recorded");
                    goto done;
                }
                existing = 1;
                break;
            default:
                say(err, err_len, "%s already exists and has files in it that "
                    "eidos did not write; choose a new or empty folder", base);
                goto done;
            }
        }
        break;
    }

    /* --- header sanity (ndsrom.py's order) -------------------------- */
    if (opt->require_gamecode != NULL
        && memcmp(rom.hdr + 0x0C, opt->require_gamecode, 4) != 0) {
        char gc[5];
        copy_field(gc, rom.hdr + 0x0C, 4);
        say(err, err_len, "unexpected game code '%s'; expected '%.4s'",
            gc, opt->require_gamecode);
        goto done;
    }
    unit_code = rom.hdr[0x12];
    is_twl = (unit_code & 0x02u) != 0;
    header_size = H32(&rom, 0x84);
    if (header_size < HEADER_SNAPSHOT) {
        say(err, err_len, "header size 0x%X is implausibly small", (unsigned)header_size);
        goto done;
    }

    /* --- header, arm9, arm7 ------------------------------------------ */
    if (plan_add(&plan, &rom, "header", 0, HEADER_SNAPSHOT, "header.bin", err, err_len))
        goto done;
    if (header_size > HEADER_SNAPSHOT
        && plan_add(&plan, &rom, "header_ext", HEADER_SNAPSHOT,
                    header_size - HEADER_SNAPSHOT, "header_ext.bin", err, err_len))
        goto done;
    arm9_region = plan.count;
    arm9_size = H32(&rom, 0x2C);
    if (plan_add(&plan, &rom, "arm9", H32(&rom, 0x20), arm9_size, "arm9.bin", err, err_len))
        goto done;
    if (plan_add(&plan, &rom, "arm7", H32(&rom, 0x30), H32(&rom, 0x3C), "arm7.bin", err, err_len))
        goto done;

    /* --- FNT / FAT --------------------------------------------------- */
    {
        u32 fnt_off = H32(&rom, 0x40), fnt_size = H32(&rom, 0x44);
        u32 fat_off = H32(&rom, 0x48), fat_size = H32(&rom, 0x4C);

        if (plan_add(&plan, &rom, "fnt", fnt_off, fnt_size, "fnt.bin", err, err_len)
            || plan_add(&plan, &rom, "fat", fat_off, fat_size, "fat.bin", err, err_len))
            goto done;
        if (fat_size % FAT_ENTRY_SIZE) {
            say(err, err_len, "FAT size 0x%X is not a multiple of 8 bytes", (unsigned)fat_size);
            goto done;
        }
        fat_count = fat_size / FAT_ENTRY_SIZE;
        fat = (u32 *)malloc((size_t)fat_count * 8u + 8u);
        fid_seen = (u8 *)calloc((size_t)fat_count + 1u, 1);
        table = (u8 *)malloc((size_t)fat_size + 1u);
        if (fat == NULL || fid_seen == NULL || table == NULL) {
            say(err, err_len, "out of memory");
            goto done;
        }
        if (rom_read(&rom, fat_off, table, fat_size) != 0) {
            say(err, err_len, "cannot read the FAT");
            goto done;
        }
        for (i = 0; i < fat_count; i++) {
            u32 s = rd32(table + 8 * i), e = rd32(table + 8 * i + 4);
            if (e < s) {
                say(err, err_len, "FAT entry %u: end 0x%X precedes start 0x%X",
                    (unsigned)i, (unsigned)e, (unsigned)s);
                goto done;
            }
            if (e > rom.size) {
                say(err, err_len, "FAT entry %u: end 0x%X runs past the end of the ROM",
                    (unsigned)i, (unsigned)e);
                goto done;
            }
            fat[2 * i] = s;
            fat[2 * i + 1] = e;
        }
        free(table);
        table = NULL;

        /* --- overlay tables and overlay files ----------------------- */
        {
            static const struct { const char *tag; u32 off, size; } T[2] = {
                { "y9", 0x50, 0x54 }, { "y7", 0x58, 0x5C },
            };
            u32 t;

            for (t = 0; t < 2; t++) {
                u32 toff = H32(&rom, T[t].off), tsize = H32(&rom, T[t].size);
                char rel[64];
                u32 count;

                if (tsize == 0) {
                    continue;
                }
                if (tsize % OVERLAY_ENTRY_SIZE) {
                    say(err, err_len, "%s table size 0x%X is not a multiple of 32",
                        T[t].tag, (unsigned)tsize);
                    goto done;
                }
                snprintf(rel, sizeof(rel), "%s.bin", T[t].tag);
                if (plan_add(&plan, &rom, T[t].tag, toff, tsize, rel, err, err_len))
                    goto done;
                table = (u8 *)malloc(tsize);
                if (table == NULL || rom_read(&rom, toff, table, tsize) != 0) {
                    say(err, err_len, "cannot read the %s table", T[t].tag);
                    goto done;
                }
                count = tsize / OVERLAY_ENTRY_SIZE;
                if (t == 0) {
                    ovs = (OvRec *)malloc((size_t)count * sizeof(OvRec));
                    if (ovs == NULL) {
                        say(err, err_len, "out of memory");
                        goto done;
                    }
                }
                for (i = 0; i < count; i++) {
                    const u8 *e = table + i * OVERLAY_ENTRY_SIZE;
                    u32 ov_id = rd32(e + 0x00), fid = rd32(e + 0x18);
                    if (fid >= fat_count) {
                        say(err, err_len, "%s overlay %u: file id %u is outside the FAT (%u entries)",
                            T[t].tag, (unsigned)i, (unsigned)fid, (unsigned)fat_count);
                        goto done;
                    }
                    snprintf(rel, sizeof(rel), "overlays/%s_%04u.bin", T[t].tag, (unsigned)ov_id);
                    if (plan_add(&plan, &rom, rel, fat[2 * fid],
                                 fat[2 * fid + 1] - fat[2 * fid], rel, err, err_len))
                        goto done;
                    fid_seen[fid] = 2;
                    res.overlays++;
                    if (t == 0) {
                        OvRec *o = &ovs[ov_count++];
                        o->id = ov_id;
                        o->offset = fat[2 * fid];
                        o->file_size = fat[2 * fid + 1] - fat[2 * fid];
                        o->code_size = rd32(e + 0x08);
                        o->last = rd32(e + 0x1C);
                    }
                }
                free(table);
                table = NULL;
            }
        }

        /* --- NitroFS files ------------------------------------------ */
        {
            FntCtx c;
            u32 dir_count;

            if (fnt_size < FNT_MAIN_ENTRY_SIZE) {
                say(err, err_len, "FNT size 0x%X is too small to hold a root", (unsigned)fnt_size);
                goto done;
            }
            fnt = (u8 *)malloc(fnt_size);
            if (fnt == NULL || rom_read(&rom, fnt_off, fnt, fnt_size) != 0) {
                say(err, err_len, "cannot read the FNT");
                goto done;
            }
            dir_count = rd16(fnt + 6);
            if (dir_count == 0 || dir_count * FNT_MAIN_ENTRY_SIZE > fnt_size) {
                say(err, err_len, "FNT reports %u directories, which does not fit in 0x%X bytes",
                    (unsigned)dir_count, (unsigned)fnt_size);
                goto done;
            }
            c.fnt = fnt;
            c.size = fnt_size;
            c.dir_count = dir_count;
            c.fat = fat;
            c.fat_count = fat_count;
            c.fid_seen = fid_seen;
            c.plan = &plan;
            c.rom = &rom;
            c.fs_files = 0;
            c.err = err;
            c.err_len = err_len;
            if (fnt_walk(&c, 0, "", 0) != 0) {
                goto done;
            }
            res.fs_files = c.fs_files;
        }

        /* --- FAT slots nothing named -------------------------------- */
        for (i = 0; i < fat_count; i++) {
            char rel[32];
            if (fid_seen[i] != 0 || fat[2 * i + 1] <= fat[2 * i]) {
                continue;
            }
            snprintf(rel, sizeof(rel), "orphans/%04u.bin", (unsigned)i);
            if (plan_add(&plan, &rom, rel, fat[2 * i], fat[2 * i + 1] - fat[2 * i],
                         rel, err, err_len))
                goto done;
            res.orphans++;
        }
    }

    /* --- banner ------------------------------------------------------ */
    {
        u32 boff = H32(&rom, 0x68);
        if (boff != 0) {
            u8 v[2];
            u32 bsize;
            u32 hdr_bsize = is_twl ? H32(&rom, 0x208) : 0;

            if (rom_read(&rom, boff, v, 2) != 0) {
                say(err, err_len, "banner offset 0x%X is outside the ROM", (unsigned)boff);
                goto done;
            }
            banner_version = rd16(v);
            switch (banner_version) {
            case 0x0001: bsize = 0x0840; break;
            case 0x0002: bsize = 0x0940; break;
            case 0x0003: bsize = 0x0A40; break;
            case 0x0103: bsize = 0x23C0; break;
            default:     bsize = 0;      break;
            }
            if (bsize == 0) {
                bsize = hdr_bsize ? hdr_bsize : 0x23C0;
            } else if (hdr_bsize && hdr_bsize != bsize) {
                bsize = hdr_bsize;          /* the header is what hardware reads */
            }
            if (plan_add(&plan, &rom, "banner", boff, bsize, "banner.bin", err, err_len))
                goto done;
            have_banner = 1;
        }
    }

    /* --- DSi regions ------------------------------------------------- */
    if (is_twl) {
        if (H32(&rom, 0x1CC) && plan_add(&plan, &rom, "arm9i", H32(&rom, 0x1C0),
                                         H32(&rom, 0x1CC), "arm9i.bin", err, err_len))
            goto done;
        if (H32(&rom, 0x1DC) && plan_add(&plan, &rom, "arm7i", H32(&rom, 0x1D0),
                                         H32(&rom, 0x1DC), "arm7i.bin", err, err_len))
            goto done;
        if (H32(&rom, 0x1F4) && plan_add(&plan, &rom, "digest_sector", H32(&rom, 0x1F0),
                                         H32(&rom, 0x1F4), "digest_sector.bin", err, err_len))
            goto done;
        if (H32(&rom, 0x1FC) && plan_add(&plan, &rom, "digest_block", H32(&rom, 0x1F8),
                                         H32(&rom, 0x1FC), "digest_block.bin", err, err_len))
            goto done;
    }

    if (plan_check_overlaps(&plan, err, err_len) != 0) {
        goto done;
    }

    /* --- arm9_dec.bin: decompress before writing anything ------------ */
    if (!opt->no_decompress && arm9_size != 0) {
        u32 dec_size;

        arm9 = (u8 *)malloc(arm9_size);
        if (arm9 == NULL
            || rom_read(&rom, plan.r[arm9_region].offset, arm9, arm9_size) != 0) {
            say(err, err_len, "cannot read the ARM9 binary");
            goto done;
        }
        dec_size = bw_blz_size(arm9, arm9_size, err, err_len);
        if (dec_size == 0) {
            goto done;
        }
        /* Four times the container is far past any real ratio; a footer
         * that claims more is a corrupt one, not a big image. */
        if (dec_size > 16u * 1024u * 1024u) {
            say(err, err_len, "ARM9 footer claims a 0x%X-byte image", (unsigned)dec_size);
            goto done;
        }
        arm9_dec = (u8 *)malloc(dec_size);
        if (arm9_dec == NULL) {
            say(err, err_len, "out of memory");
            goto done;
        }
        if (bw_blz_decompress(arm9, arm9_size, arm9_dec, dec_size, err, err_len) != 0) {
            goto done;
        }
        res.arm9_dec_size = dec_size;
    }

    /* --- the staging directory --------------------------------------- */
    for (i = 0; i < plan.count; i++) {
        total += plan.r[i].size;
    }
    total += res.arm9_dec_size;
    if (!opt->no_decompress) {
        for (i = 0; i < ov_count; i++) {
            if (((ovs[i].last >> 24) & OVL_FLAG_COMPRESSED) && ovs[i].code_size <= MAX_OVERLAY_SIZE) {
                total += ovs[i].code_size;
            }
        }
    }
    if (clear_leftover(staging, err, err_len) != 0
        || clear_leftover(old, err, err_len) != 0) {
        goto done;
    }
    if (bw_mkdirs_utf8(staging) != 0) {
        say(err, err_len, "cannot create %s", staging);
        goto done;
    }
    staged = 1;
    if (write_bytes(staging, PARTIAL_MARK, "eidos extraction in progress\n", 29,
                    err, err_len) != 0) {
        goto done;
    }

    /* --- write ------------------------------------------------------- */
    buf = (u8 *)malloc(COPY_BLOCK);
    if (buf == NULL) {
        say(err, err_len, "out of memory");
        goto done;
    }
    for (i = 0; i < plan.count; i++) {
        if (plan.r[i].size == 0) {
            continue;
        }
        if (copy_region(&rom, staging, &plan.r[i], buf, err, err_len) != 0) {
            goto done;
        }
        done += plan.r[i].size;
        res.regions++;
        if (progress != NULL
            && progress(ctx, (u32)(done >> 10), (u32)(total >> 10), plan.r[i].path) != 0) {
            say(err, err_len, "cancelled");
            cancelled = 1;
            goto done;
        }
    }
    if (arm9_dec != NULL) {
        if (write_bytes(staging, "arm9_dec.bin", arm9_dec, res.arm9_dec_size, err, err_len) != 0) {
            goto done;
        }
        done += res.arm9_dec_size;
        res.regions++;
        if (progress != NULL
            && progress(ctx, (u32)(done >> 10), (u32)(total >> 10), "arm9_dec.bin") != 0) {
            say(err, err_len, "cancelled");
            cancelled = 1;
            goto done;
        }
    }
    if (!opt->no_decompress) {
        for (i = 0; i < ov_count; i++) {
            if (!((ovs[i].last >> 24) & OVL_FLAG_COMPRESSED)) {
                continue;
            }
            if (write_overlay_dec(&rom, staging, &ovs[i], err, err_len) != 0) {
                goto done;
            }
            done += ovs[i].code_size;
            res.overlays_dec++;
            res.regions++;
            if (progress != NULL
                && progress(ctx, (u32)(done >> 10), (u32)(total >> 10), "overlays_dec") != 0) {
                say(err, err_len, "cancelled");
                cancelled = 1;
                goto done;
            }
        }
    }
    res.bytes = done;

    /* --- eidos_manifest.json, last: its presence means complete ------ */
    {
        char full[PATH_MAX_LEN];
        char title[13], gamecode[5];
        FILE *fp;

        copy_field(title, rom.hdr, 12);
        copy_field(gamecode, rom.hdr + 0x0C, 4);
        if (join(full, sizeof(full), staging, MANIFEST_NAME) != 0
            || (fp = bw_fopen_utf8(full, "w")) == NULL) {
            say(err, err_len, "cannot write " MANIFEST_NAME);
            goto done;
        }
        fprintf(fp, "{\n \"tool\": \"eidos install\",\n \"version\": %s,\n", BW_INSTALL_VERSION);
        fprintf(fp, " \"rom_sha1\": \"%s\",\n", sha_hex);
        fprintf(fp, " \"title\": ");
        json_str(fp, title);
        fprintf(fp, ",\n \"gamecode\": ");
        json_str(fp, gamecode);
        fprintf(fp, ",\n \"unit_code\": %u,\n \"rom_size\": %u,\n",
                (unsigned)unit_code, (unsigned)rom.size);
        fprintf(fp, " \"header_size\": %u,\n", (unsigned)header_size);
        if (have_banner) {
            fprintf(fp, " \"banner_version\": %u,\n", (unsigned)banner_version);
        }
        fprintf(fp, " \"arm9_dec_size\": %u,\n", (unsigned)res.arm9_dec_size);
        fprintf(fp, " \"counts\": {\"fs_files\": %u, \"overlays\": %u, "
                "\"overlays_dec\": %u, \"orphans\": %u, \"files\": %u}\n}\n",
                (unsigned)res.fs_files, (unsigned)res.overlays,
                (unsigned)res.overlays_dec, (unsigned)res.orphans,
                (unsigned)res.regions);
        if (fclose(fp) != 0) {
            say(err, err_len, "write error on " MANIFEST_NAME);
            goto done;
        }
    }

    /* --- commit: the marker goes, the tree takes its name ------------ */
    {
        char mark[PATH_MAX_LEN];
        int moved = 0;

        if (join(mark, sizeof(mark), staging, PARTIAL_MARK) != 0
            || bw_remove_tree_utf8(mark) != 0) {
            say(err, err_len, "cannot remove %s", mark);
            goto done;
        }
        if (existing == 2 && bw_remove_tree_utf8(base) != 0) {
            say(err, err_len, "cannot replace the empty folder %s", base);
            goto done;
        }
        if (existing == 1) {
            if (bw_rename_utf8(base, old) != 0) {
                say(err, err_len, "cannot move the previous extraction %s aside "
                    "(is a file in it open?)", base);
                goto done;
            }
            moved = 1;
        }
        if (bw_rename_utf8(staging, base) != 0) {
            if (moved) {
                (void)bw_rename_utf8(old, base);
            }
            say(err, err_len, "cannot rename %s to %s", staging, base);
            goto done;
        }
        staged = 0;
        if (moved) {
            (void)bw_remove_tree_utf8(old);     /* else the next run does */
        }
    }
    rc = 0;

done:
    if (rc != 0 && staged) {
        (void)bw_remove_tree_utf8(staging);
    }
    if (cancelled) {
        rc = BW_CANCELLED;
    }
    if (result != NULL) {
        *result = res;
    }
    free(plan.r);
    free(fnt);
    free(fat);
    free(fid_seen);
    free(table);
    free(buf);
    free(arm9);
    free(arm9_dec);
    free(ovs);
    rom_close(&rom);
    return rc;
}
