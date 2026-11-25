/*
 * sdk/json_read.c -- a small reader for JSON text (sdk/json_read.h).
 *
 * The check (bw_json_parse) walks the whole text once by the grammar of
 * RFC 8259; every later call can then walk a value without checking again,
 * because a value only ever comes from a checked text.
 */

#include <string.h>

#include "json_read.h"

static const char *skip_ws(const char *p, const char *e)
{
    while (p < e && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) {
        p++;
    }
    return p;
}

static int hex_of(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

/* Past the string that starts at p (its opening quote), or NULL. */
static const char *skip_string(const char *p, const char *e)
{
    p++;
    while (p < e) {
        unsigned char c = (unsigned char)*p;

        if (c == '"') {
            return p + 1;
        }
        if (c < 0x20) {
            return NULL;                /* a control character, a NUL */
        }
        if (c == '\\') {
            if (p + 1 >= e) {
                return NULL;
            }
            if (p[1] == 'u') {
                if (e - p < 6 || hex_of(p[2]) < 0 || hex_of(p[3]) < 0 ||
                    hex_of(p[4]) < 0 || hex_of(p[5]) < 0) {
                    return NULL;
                }
                p += 6;
                continue;
            }
            if (strchr("\"\\/bfnrt", p[1]) == NULL || p[1] == '\0') {
                return NULL;
            }
            p += 2;
            continue;
        }
        p++;
    }
    return NULL;
}

static const char *skip_number(const char *p, const char *e)
{
    const char *d;

    if (p < e && *p == '-') {
        p++;
    }
    d = p;
    while (p < e && *p >= '0' && *p <= '9') {
        p++;
    }
    if (p == d || (p - d > 1 && *d == '0')) {
        return NULL;                    /* no digit, or a leading zero */
    }
    if (p < e && *p == '.') {
        d = ++p;
        while (p < e && *p >= '0' && *p <= '9') {
            p++;
        }
        if (p == d) {
            return NULL;
        }
    }
    if (p < e && (*p == 'e' || *p == 'E')) {
        p++;
        if (p < e && (*p == '+' || *p == '-')) {
            p++;
        }
        d = p;
        while (p < e && *p >= '0' && *p <= '9') {
            p++;
        }
        if (p == d) {
            return NULL;
        }
    }
    return p;
}

static const char *skip_word(const char *p, const char *e, const char *word)
{
    size_t n = strlen(word);

    return ((size_t)(e - p) >= n && memcmp(p, word, n) == 0) ? p + n : NULL;
}

/* Past the value that starts at p (no white space before it), or NULL. */
static const char *skip_value(const char *p, const char *e, int depth)
{
    char open, shut;

    if (p >= e) {
        return NULL;
    }
    switch (*p) {
    case '"':
        return skip_string(p, e);
    case 't':
        return skip_word(p, e, "true");
    case 'f':
        return skip_word(p, e, "false");
    case 'n':
        return skip_word(p, e, "null");
    case '{':
    case '[':
        break;
    default:
        return skip_number(p, e);
    }
    if (depth >= BW_JSON_DEPTH_MAX) {
        return NULL;
    }
    open = *p;
    shut = open == '{' ? '}' : ']';
    p = skip_ws(p + 1, e);
    if (p < e && *p == shut) {
        return p + 1;
    }
    for (;;) {
        if (open == '{') {
            if (p >= e || *p != '"' || (p = skip_string(p, e)) == NULL) {
                return NULL;
            }
            p = skip_ws(p, e);
            if (p >= e || *p != ':') {
                return NULL;
            }
            p = skip_ws(p + 1, e);
        }
        p = skip_value(p, e, depth + 1);
        if (p == NULL) {
            return NULL;
        }
        p = skip_ws(p, e);
        if (p >= e) {
            return NULL;
        }
        if (*p == shut) {
            return p + 1;
        }
        if (*p != ',') {
            return NULL;
        }
        p = skip_ws(p + 1, e);
    }
}

int bw_json_parse(const char *text, size_t size, BwJson *root)
{
    const char *e = text + size;
    const char *p = skip_ws(text, e);
    const char *q;

    root->s = root->e = NULL;
    /* (a UTF-8 byte order mark before the value is let through) */
    if (e - p >= 3 && memcmp(p, "\xEF\xBB\xBF", 3) == 0) {
        p = skip_ws(p + 3, e);
    }
    q = skip_value(p, e, 0);
    if (q == NULL || skip_ws(q, e) != e) {
        return -1;
    }
    root->s = p;
    root->e = q;
    return 0;
}

int bw_json_kind(BwJson v)
{
    if (v.s == NULL || v.s >= v.e) {
        return BW_JSON_NONE;
    }
    switch (*v.s) {
    case '{': return BW_JSON_OBJECT;
    case '[': return BW_JSON_ARRAY;
    case '"': return BW_JSON_STRING;
    case 't': return BW_JSON_TRUE;
    case 'f': return BW_JSON_FALSE;
    case 'n': return BW_JSON_NULL;
    default:  return BW_JSON_NUMBER;
    }
}

/*
 * The next member of an object or item of an array.  *p is past the
 * opening bracket or past the last value given; 1 with the key (objects)
 * and the value, 0 at the end.
 */
static int next_part(const char **p, const char *e, int object, BwJson *key, BwJson *val)
{
    const char *q = skip_ws(*p, e);

    if (q < e && *q == ',') {
        q = skip_ws(q + 1, e);
    }
    if (q >= e || *q == '}' || *q == ']') {
        return 0;
    }
    if (object) {
        key->s = q;
        key->e = q = skip_string(q, e);
        q = skip_ws(q, e) + 1;          /* the colon */
        q = skip_ws(q, e);
    }
    val->s = q;
    val->e = skip_value(q, e, 0);
    *p = val->e;
    return 1;
}

/* Does the string value `k` spell `key`?  (Its escapes are read.) */
static int key_is(BwJson k, const char *key)
{
    char buf[96];

    return bw_json_string(k, buf, sizeof(buf)) == 0 && strcmp(buf, key) == 0;
}

int bw_json_member(BwJson obj, const char *key, BwJson *out)
{
    const char *p;
    BwJson k, v;

    if (bw_json_kind(obj) != BW_JSON_OBJECT) {
        return 0;
    }
    p = obj.s + 1;
    while (next_part(&p, obj.e, 1, &k, &v)) {
        if (key_is(k, key)) {
            *out = v;
            return 1;
        }
    }
    return 0;
}

unsigned bw_json_count(BwJson arr)
{
    const char *p;
    BwJson k, v;
    unsigned n = 0;

    if (bw_json_kind(arr) != BW_JSON_ARRAY) {
        return 0;
    }
    p = arr.s + 1;
    while (next_part(&p, arr.e, 0, &k, &v)) {
        n++;
    }
    return n;
}

int bw_json_item(BwJson arr, unsigned index, BwJson *out)
{
    const char *p;
    BwJson k, v;

    if (bw_json_kind(arr) != BW_JSON_ARRAY) {
        return 0;
    }
    p = arr.s + 1;
    while (next_part(&p, arr.e, 0, &k, &v)) {
        if (index-- == 0) {
            *out = v;
            return 1;
        }
    }
    return 0;
}

/* The character number `c` in UTF-8 at out (up to 4 bytes); its length. */
static size_t utf8_put(unsigned long c, char *out)
{
    if (c < 0x80) {
        out[0] = (char)c;
        return 1;
    }
    if (c < 0x800) {
        out[0] = (char)(0xC0 | (c >> 6));
        out[1] = (char)(0x80 | (c & 0x3F));
        return 2;
    }
    if (c < 0x10000) {
        out[0] = (char)(0xE0 | (c >> 12));
        out[1] = (char)(0x80 | ((c >> 6) & 0x3F));
        out[2] = (char)(0x80 | (c & 0x3F));
        return 3;
    }
    out[0] = (char)(0xF0 | (c >> 18));
    out[1] = (char)(0x80 | ((c >> 12) & 0x3F));
    out[2] = (char)(0x80 | ((c >> 6) & 0x3F));
    out[3] = (char)(0x80 | (c & 0x3F));
    return 4;
}

static unsigned long hex4(const char *p)
{
    return ((unsigned long)hex_of(p[0]) << 12) | ((unsigned long)hex_of(p[1]) << 8) |
           ((unsigned long)hex_of(p[2]) << 4) | (unsigned long)hex_of(p[3]);
}

int bw_json_string(BwJson v, char *out, size_t size)
{
    const char *p, *e;
    size_t k = 0;

    if (size > 0) {
        out[0] = '\0';
    }
    if (bw_json_kind(v) != BW_JSON_STRING || size == 0) {
        return -1;
    }
    p = v.s + 1;
    e = v.e - 1;
    while (p < e) {
        char one[4];
        size_t n = 1;

        if (*p != '\\') {
            /* a whole UTF-8 character, so a cut never splits one */
            unsigned char c = (unsigned char)*p;

            n = c < 0x80 ? 1 : c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : c >= 0xC0 ? 2 : 1;
            if ((size_t)(e - p) < n) {
                n = (size_t)(e - p);
            }
            if (k + n + 1 > size) {
                out[k] = '\0';
                return 1;
            }
            memcpy(out + k, p, n);
            k += n;
            p += n;
            continue;
        }
        if (p[1] == 'u') {
            unsigned long c = hex4(p + 2);

            p += 6;
            if (c >= 0xD800 && c < 0xDC00 && e - p >= 6 && p[0] == '\\' && p[1] == 'u') {
                unsigned long lo = hex4(p + 2);

                if (lo >= 0xDC00 && lo < 0xE000) {
                    c = 0x10000 + ((c - 0xD800) << 10) + (lo - 0xDC00);
                    p += 6;
                }
            }
            if (c >= 0xD800 && c < 0xE000) {
                c = 0xFFFD;             /* half a pair: the replacement character */
            }
            if (c == 0) {
                break;                  /* the text ends at an escaped NUL */
            }
            n = utf8_put(c, one);
        } else {
            switch (p[1]) {
            case 'b': one[0] = '\b'; break;
            case 'f': one[0] = '\f'; break;
            case 'n': one[0] = '\n'; break;
            case 'r': one[0] = '\r'; break;
            case 't': one[0] = '\t'; break;
            default:  one[0] = p[1]; break;
            }
            p += 2;
        }
        if (k + n + 1 > size) {
            out[k] = '\0';
            return 1;
        }
        memcpy(out + k, one, n);
        k += n;
    }
    out[k] = '\0';
    return 0;
}

int bw_json_u64(BwJson v, unsigned long long *out)
{
    const char *p;
    unsigned long long n = 0;

    *out = 0;
    if (bw_json_kind(v) != BW_JSON_NUMBER) {
        return -1;
    }
    for (p = v.s; p < v.e; p++) {
        unsigned d;

        if (*p < '0' || *p > '9') {
            return -1;
        }
        d = (unsigned)(*p - '0');
        if (n > (~0ull - d) / 10ull) {
            return -1;
        }
        n = n * 10ull + d;
    }
    *out = n;
    return 0;
}

int bw_json_bool(BwJson v, int *out)
{
    int kind = bw_json_kind(v);

    *out = kind == BW_JSON_TRUE;
    return (kind == BW_JSON_TRUE || kind == BW_JSON_FALSE) ? 0 : -1;
}
