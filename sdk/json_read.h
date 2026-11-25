/*
 * sdk/json_read.h -- a small reader for JSON text (sdk/json_read.c).
 *
 * For the files the game reads from the mods index (sdk/mod_catalog.c): a
 * few hundred bytes each, read once.  The text is checked as a whole first
 * (bw_json_parse); after that a value is a pair of pointers into the text,
 * and the calls below walk it in place.  Nothing is allocated and the text
 * is never changed.
 */

#ifndef BW_PORT_JSON_READ_H
#define BW_PORT_JSON_READ_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* One value of a checked text: its first character and the one past its
 * last. */
typedef struct BwJson {
    const char *s;
    const char *e;
} BwJson;

enum {
    BW_JSON_NONE = 0,
    BW_JSON_OBJECT,
    BW_JSON_ARRAY,
    BW_JSON_STRING,
    BW_JSON_NUMBER,
    BW_JSON_TRUE,
    BW_JSON_FALSE,
    BW_JSON_NULL
};

/* The deepest nesting a text may have. */
#define BW_JSON_DEPTH_MAX 32

/*
 * Check `size` bytes of text as one JSON value (with nothing but white
 * space around it) and give that value.  0, or -1 when the text is not
 * JSON, nests deeper than BW_JSON_DEPTH_MAX or holds a NUL.
 */
int bw_json_parse(const char *text, size_t size, BwJson *root);

/* What a value is (BW_JSON_*). */
int bw_json_kind(BwJson v);

/* The member `key` of an object: 1 and the value, or 0 (no such member,
 * or `obj` is not an object).  A key written twice gives its first. */
int bw_json_member(BwJson obj, const char *key, BwJson *out);

/* How many items an array has (0 when `arr` is not an array), and its
 * item `index`: 1 and the value, or 0. */
unsigned bw_json_count(BwJson arr);
int bw_json_item(BwJson arr, unsigned index, BwJson *out);

/*
 * A string's text in UTF-8, its escapes read ("\n", "é", a surrogate
 * pair).  0 when all of it fits `out` (with its NUL); 1 when it was cut,
 * at a whole character; -1 when `v` is not a string (out is then "").  An
 * escaped NUL ends the text there.
 */
int bw_json_string(BwJson v, char *out, size_t size);

/* A whole number that is not negative: 0 and the number, or -1 (not a
 * number, a sign, a fraction, an exponent, or past 64 bits). */
int bw_json_u64(BwJson v, unsigned long long *out);

/* true or false: 0 and 1 or 0, or -1 when the value is neither. */
int bw_json_bool(BwJson v, int *out);

#ifdef __cplusplus
}
#endif

#endif /* BW_PORT_JSON_READ_H */
