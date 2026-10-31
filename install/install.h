/*
 * install/install.h -- the first-run extractor: the player's ROM in, the
 * data root the port reads out.
 *
 * This is the core of the installer eidos is building towards: a wizard
 * that takes the player's own dump of Pokemon Black (and eventually White)
 * and leaves a playable game behind, on Windows, macOS, Linux, Android and
 * iOS.  Everything platform-specific (file pickers, the progress screen,
 * where the data root lives) belongs to the front end; this library is
 * plain C99 and the standard library, and does the part that is the same
 * everywhere:
 *
 *   1. identify: read the cartridge header, hash the whole image, and say
 *      which game and which dump it is, and whether eidos takes it, in
 *      words a player can act on (bw_rom_identify, bw_rom_verdict);
 *   2. extract: write the tree aletheia's tools/ndsrom.py extract writes
 *      (the NitroFS files under data/, the ARM9/ARM7 binaries, the overlay
 *      files and tables, the banner and the DSi regions), plus what
 *      tools/delink.py derives from it: arm9_dec.bin, the BLZ-decompressed
 *      ARM9 image the port reads its ARM9 constant tables out of, and
 *      overlays_dec/, every compressed ARM9 overlay decompressed
 *      (bw_rom_extract).  The output appears atomically: it is staged
 *      beside the target and renamed into place when complete.
 *
 * The layout is aletheia's on purpose: its asset tools, eidos's oracles and
 * eidos itself already read it, tests/test_extract.c checks the two
 * extractors against each other file for file on a synthetic cartridge,
 * and test_extract_rom does the same on the player's ROM.  What this does
 * not write is ndsrom.py's rebuild bookkeeping (residue/ and its
 * manifest.json), which only matters for turning the tree back into a ROM,
 * and the two files delink.py writes for aletheia's own tools only
 * (arm9i_dec.bin, which needs a DSi key, and ov230d.bin, a disassembler
 * view); docs/installer.md has the list.  It writes eidos_manifest.json
 * instead, recording what was extracted from which image.
 *
 * The ROM is treated as untrusted input: every offset and size is checked
 * against the image before it is used, file names are sanitised exactly as
 * ndsrom.py sanitises them (so no name can leave the output directory or be
 * a reserved Windows name), and a malformed image is an error with a
 * message, never a crash.  Paths are UTF-8 on every platform (on Windows
 * they are converted for the wide-character API), since the one path an
 * installer cannot choose is the one the player's ROM is at.
 *
 * The image is streamed: the ROM is read in blocks for the hash and the
 * copies rather than loaded whole, which matters on a phone.  Only the
 * header, FNT, FAT, overlay tables and ARM9 binary are held in memory.
 */

#ifndef EIDOS_INSTALL_H
#define EIDOS_INSTALL_H

#include <stddef.h>

#include "types.h"

/* Bumped whenever the extracted tree changes shape, so a data root written
 * by an older installer is re-extracted.  2: overlays_dec/ and rom_sha1. */
#define BW_INSTALL_VERSION "2"

/* ------------------------------------------------------------------ */
/* SHA-1                                                               */
/* ------------------------------------------------------------------ */

typedef struct BwSha1 {
    u32 h[5];
    u64 length;        /* bytes hashed so far */
    u8  block[64];
    u32 used;          /* bytes in `block` */
} BwSha1;

void bw_sha1_init(BwSha1 *s);
void bw_sha1_update(BwSha1 *s, const void *data, size_t len);
void bw_sha1_final(BwSha1 *s, u8 digest[20]);
/* Lower-case hex, 41 bytes with the terminator. */
void bw_sha1_hex(const u8 digest[20], char out[41]);

/* ------------------------------------------------------------------ */
/* BLZ (the SDK's backwards LZ, which the ARM9 binary is packed with)  */
/* ------------------------------------------------------------------ */

/*
 * The decompressed size of the BLZ container `in` (the whole container, its
 * footer at the end), or 0 with `err` filled if the footer is not a
 * consistent one.  A container whose footer says "not compressed" (its last
 * word is 0) decompresses to itself.
 */
u32 bw_blz_size(const u8 *in, u32 in_len, char *err, size_t err_len);

/*
 * Decompress `in` into `out`, which must hold exactly bw_blz_size() bytes.
 * 0 on success, -1 with `err` filled on a malformed stream.  The checks are
 * aletheia's tools/blz.py's, in the same order, so the two refuse the same
 * inputs.
 */
int bw_blz_decompress(const u8 *in, u32 in_len, u8 *out, u32 out_len,
                      char *err, size_t err_len);

/* ------------------------------------------------------------------ */
/* Identifying a ROM                                                   */
/* ------------------------------------------------------------------ */

typedef enum BwGame {
    BW_GAME_UNKNOWN = 0,
    BW_GAME_BLACK,          /* Pokemon Black, any region (game code IRB?) */
    BW_GAME_WHITE           /* not yet supported; recognised by game code */
} BwGame;

typedef struct BwRomInfo {
    char   title[13];       /* header 0x000, NUL-terminated */
    char   gamecode[5];     /* header 0x00C */
    char   maker[3];        /* header 0x010 */
    u8     unit_code;       /* header 0x012; bit 1 = DSi (TWL) aware */
    u32    rom_size;        /* bytes in the file */
    u32    capacity;        /* the chip size the header declares (0x014),
                               0 if implausible */
    int    header_crc_ok;   /* header 0x15E is the CRC-16 of 0x000..0x15D */
    int    trimmed;         /* rom_size < capacity, header checksum good */
    u8     sha1[20];
    char   sha1_hex[41];
    /* For a trimmed image: the SHA-1 of the file padded with 0xFF back to
     * `capacity`, which is what the untrimmed dump hashes to when the
     * trimmer cut only padding.  "" otherwise. */
    char   padded_sha1_hex[41];
    BwGame game;            /* from the game code */
    int    verified;        /* 1 if the image (or, trimmed, its padded
                               form) is a known good dump */
} BwRomInfo;

/* The one dump aletheia is built against (README.md there). */
#define BW_BLACK_SHA1 "26ad0b9967aa279c4a266ee69f52b9b2332399a5"
#define BW_BLACK_SIZE 268435456u

/* Whether a ROM's SHA-1 is one of the known good dumps the port plays:
 * English Black, and the White and European cartridges it lays out as
 * English Black's (sdk/langmap.h). */
int bw_rom_known_sha1(const char *sha1_hex);

/* The largest image identify and extract will open: twice the largest DS
 * or DSi cartridge (4 Gbit).  Anything bigger is not a cartridge dump. */
#define BW_ROM_MAX_SIZE 0x40000000u

/*
 * Progress, and the way to stop.  Return 0 to go on; anything else stops
 * the work at the next block or file, and the call returns BW_CANCELLED
 * with `err` "cancelled".  A cancelled extraction leaves nothing behind:
 * its staging folder is removed exactly as on an error.
 */
typedef int (*BwProgressFn)(void *ctx, u32 done, u32 total, const char *what);

/* What bw_rom_identify and bw_rom_extract return when `progress` asked
 * them to stop (every other failure is -1). */
#define BW_CANCELLED (-2)

/*
 * Read the header and hash the whole image.  0 if the file is readable and
 * looks like a DS cartridge image at all (it is not an archive, and a
 * header fits); -1 with `err` filled otherwise, in words a player can act
 * on (BW_CANCELLED if `progress` stopped it).  Whether it is a game the
 * port supports is bw_rom_verdict's question.  `progress` (may be NULL) is told how far the hash has got,
 * with `what` "hashing".
 */
int bw_rom_identify(const char *rom_path, BwRomInfo *info,
                    BwProgressFn progress, void *ctx,
                    char *err, size_t err_len);

/*
 * What to tell the player about an identified image.  Every front end
 * shows the same message and makes the same decision: only BW_VERDICT_OK
 * is extracted without an explicit override.
 */
typedef enum BwRomVerdict {
    BW_VERDICT_OK = 0,      /* the known good dump of Pokemon Black, or it
                               trimmed of padding only */
    BW_VERDICT_NOT_DS,      /* the header is not a DS header */
    BW_VERDICT_OTHER_GAME,  /* a DS game, not Pokemon Black */
    BW_VERDICT_UNSUPPORTED, /* Black or White of a region not played (Japan, Korea) */
    BW_VERDICT_DAMAGED,     /* Black, but the header checksum is wrong or
                               the image is cut short */
    BW_VERDICT_MODIFIED     /* Black, whole, but not the known dump:
                               damaged, patched or another revision */
} BwRomVerdict;

BwRomVerdict bw_rom_verdict(const BwRomInfo *info, char *msg, size_t msg_len);

/* ------------------------------------------------------------------ */
/* Extracting                                                          */
/* ------------------------------------------------------------------ */

typedef struct BwExtractOptions {
    /* The game code the image must carry.  NULL accepts any; the default
     * front end passes "IRBO", which is what ndsrom.py insists on. */
    const char *require_gamecode;
    /* Skip arm9_dec.bin and overlays_dec/ (the only steps that decompress
     * anything). */
    int no_decompress;
    /* The image's SHA-1 as bw_rom_identify reported it, so the image is not
     * hashed twice.  NULL: bw_rom_extract hashes it itself. */
    const char *rom_sha1_hex;
    /* Replace an existing eidos extraction of a different ROM in out_dir.
     * Without it that is refused; an extraction of the same ROM by an
     * older installer is always replaced, and one by this installer is
     * left alone (see bw_rom_extract). */
    int replace;
} BwExtractOptions;

typedef struct BwExtractResult {
    u32 fs_files;           /* NitroFS files written under data/ */
    u32 overlays;           /* overlay files written under overlays/ */
    u32 overlays_dec;       /* decompressed overlays under overlays_dec/ */
    u32 orphans;            /* FAT entries nothing named */
    u32 regions;            /* every non-empty file written */
    u64 bytes;              /* bytes written, all files */
    u32 arm9_dec_size;      /* 0 if not written */
    int already;            /* 1: out_dir already held this extraction and
                               nothing was written */
} BwExtractResult;

/*
 * Extract `rom_path` into `out_dir`.  0 on success, -1 with `err` filled on
 * the first problem.
 *
 * The output is atomic.  Everything is written into "<out_dir>.partial"
 * (a sibling, so the final rename stays on one file system), the manifest
 * last, and only a complete tree is renamed to `out_dir`.  A failure never
 * leaves a half-written `out_dir`: on failure the staging directory is
 * removed, and one left by a killed run is recognised by its marker file
 * (or its manifest) and removed by the next run.
 *
 * An existing `out_dir`:
 *   - absent, or an empty directory: extracted into;
 *   - an extraction of the same ROM by this installer version (its
 *     eidos_manifest.json says so): left alone, `result->already` = 1,
 *     success.  This is the "resume": a completed extraction is reused,
 *     an interrupted one starts again (it takes seconds, and a restart
 *     cannot inherit a damaged file);
 *   - an extraction of the same ROM by another installer version:
 *     replaced;
 *   - an extraction of another ROM: replaced if `opt->replace`, refused
 *     otherwise;
 *   - anything else (a file, or a directory with files eidos did not
 *     write): refused.  The installer never deletes what it did not create.
 *
 * `progress` (may be NULL) is called as files are written, with KiB done
 * and KiB total and the file just written; if the image has to be hashed
 * first that is reported in bytes with `what` "hashing".
 */
int bw_rom_extract(const char *rom_path, const char *out_dir,
                   const BwExtractOptions *opt, BwExtractResult *result,
                   BwProgressFn progress, void *ctx,
                   char *err, size_t err_len);

/*
 * The same name mangling ndsrom.py applies to an FNT name, exposed for the
 * tests: `raw` is `len` bytes straight from the FNT, `out` receives a
 * NUL-terminated name safe on every filesystem the port targets.
 */
void bw_fnt_sanitize(const u8 *raw, u32 len, char *out, size_t out_size);

/* ------------------------------------------------------------------ */
/* Files, with UTF-8 paths everywhere (install/fsutil.c)               */
/* ------------------------------------------------------------------ */

#include <stdio.h>

enum { BW_PATH_NONE = 0, BW_PATH_FILE = 1, BW_PATH_DIR = 2 };

FILE *bw_fopen_utf8(const char *path, const char *mode);
/* mkdir -p.  0 on success (or if it already exists). */
int   bw_mkdirs_utf8(const char *path);
/* BW_PATH_NONE, BW_PATH_FILE (anything not a directory) or BW_PATH_DIR. */
int   bw_path_kind_utf8(const char *path);
/* 1 empty, 0 not, -1 not a readable directory. */
int   bw_dir_is_empty_utf8(const char *path);
/* Rename a file or directory; fails (-1) if `to` exists. */
int   bw_rename_utf8(const char *from, const char *to);
/* rm -r.  0 if `path` is gone afterwards (or never existed).  Links and
 * junctions are removed, never followed. */
int   bw_remove_tree_utf8(const char *path);
/* The size of an open file, past 2 GiB where the platform allows. */
int   bw_file_size(FILE *fp, u64 *size);
/* The bytes free for an unprivileged user on the volume that holds
 * `path`, or would hold it: the nearest existing ancestor is asked when
 * `path` does not exist yet.  0 on success, -1 if no ancestor answers. */
int   bw_free_space_utf8(const char *path, u64 *bytes);

#endif /* EIDOS_INSTALL_H */
