/*
 * sdk/vfs_msg.h -- line level merge for text banks.
 *
 * A text bank (a member of a/0/0/2 or a/0/0/3) holds one string per id, so a
 * mod that adds one species name should not have to ship the whole encoded
 * bank, and two mods that each add a name should not clobber one another.
 * Instead a mod ships a plain text file `<bank index>.msg` in the directory
 * at the message archive's path, for example `<mod>/a/0/0/2/70.msg`.
 *
 * Languages: `<bank index>.<lang>.msg` (`70.fr.msg`; `en`, `fr`, `de`,
 * `it`, `es`) holds lines for the game in that language only, and applies
 * after the same mod's `70.msg`, line by line: a mod translates the lines
 * it has words for, and the rest keep its plain file's.  A file for another
 * language is not read (sdk/vfs_narc.c).
 *
 * File grammar (UTF-8):
 *
 *   - One entry per line: `<index> = <text>`.  The index is decimal.  One
 *     space after the `=` is skipped; the text then runs to the end of the
 *     line and keeps its trailing spaces.  A CR before the LF is dropped.
 *   - A line whose first character is `#` is a comment.  An empty line, or
 *     one holding only spaces and tabs, is ignored.
 *   - The text uses the display form of the kenon tools:
 *       `\n`               the line break unit 0xFFFE
 *       `{KIND}`           control code F000, KIND, argument count 0
 *       `{KIND:a,b,...}`   control code F000, KIND, count, the arguments
 *                          (hex, 1 to 4 digits each, any case; the tools
 *                          write KIND with 4 digits)
 *       `{U+XXXX}`         one raw unit (an unpaired surrogate)
 *       `\\`, `\{`, `\}`   the literal character; any other `\x` is an error
 *       anything else      its UTF-16 code units
 *     The 0xFFFF terminator is added by the encoder, so it cannot be written.
 *   - A line whose text starts with `{packed}` is refused in this version.
 *
 * Merge: later mods win per line.  An index past the bank's count grows
 * the bank and the indices in between become empty strings.  Lines nobody
 * touches keep their original ciphertext bytes.  Changed lines are enciphered
 * with key (u16)((index + 3) * 0x2983), rotated left by 3 after each unit.
 * The bank header and the offset table are rebuilt.
 */

#ifndef BW_VFS_MSG_H
#define BW_VFS_MSG_H

#include <stddef.h>
#include "types.h"

/* A bank holds at most 65535 lines (u16 count), so the top index is 65534. */
#define BW_MSG_MAX_INDEX 65534u

typedef struct BwMsgLine {
    u32  index;
    u16 *units;      /* plain units, no terminator; NULL when len is 0 */
    u32  len;
} BwMsgLine;

/* The lines of one .msg file in file order.  A later line with the same
 * index wins over an earlier one. */
typedef struct BwMsgLines {
    BwMsgLine *lines;
    u32        count;
    u32        cap;
} BwMsgLines;

/*
 * Parse `len` bytes of text.  Returns 0 and fills `out` (release it with
 * bw_msg_lines_free), or -1 with a message in `err` that names the line.
 * `err` may be NULL.  `out` is left empty on failure.
 */
int bw_msg_parse(const char *text, size_t len, BwMsgLines *out,
                 char *err, size_t err_size);

void bw_msg_lines_free(BwMsgLines *lines);

/*
 * Apply `mod_count` parsed files, earliest mod first, to the bank image
 * `bank`.  Returns 0 and a malloc'd image in *out (release it with
 * bw_msg_free), or -1 when the bank is malformed or too large.
 */
int bw_msg_merge(const u8 *bank, u32 bank_size,
                 const BwMsgLines *const *mods, u32 mod_count,
                 u8 **out, u32 *out_size);

void bw_msg_free(u8 *image);

/* The cipher of one line, applied in place.  It is its own inverse. */
void bw_msg_cipher(u16 *units, u32 count, u32 index);

/*
 * Lines the port makes for a mod (sdk/text_from.c), held in memory: for
 * bank `bank` of archive `rel` they apply as the mod's `<bank>.<lang>.msg`
 * would, after its own line files.  A second call for the same mod and
 * bank replaces the first.  Returns 0 or -1 (sdk/vfs_narc.c).
 */
int bw_vfs_lines_set(const char *root, const char *rel, u32 bank, const BwMsgLine *lines, u32 count);

/* Drop every mod's made lines. */
void bw_vfs_lines_clear(void);

#endif
