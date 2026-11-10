/*
 * engine/platform/texpack.c -- HD textures for the GPU 3D (Display, "HD
 * textures" and "Dump textures", both off by default).
 *
 * Everything the DS draws in 3D is a texture: the world, the people and
 * Pokemon of the field, the battle Pokemon and trainers.  Each texture the
 * game uploads is named by a hash of what it looks like (its decoded RGBA
 * and size), so the same image has the same name on every run and every
 * computer.
 *
 *   textures/dump/<hash>.png    written once per new texture while "Dump
 *                               textures" is on: the originals, to paint
 *                               over or upscale
 *   textures/auto/<hash>-<w>.png  an Auto copy, saved once made, so a
 *                               later run reads it instead of upscaling
 *   textures/<hash>.png         a replacement, used while "HD textures" is
 *                               Pack or Auto: the same picture at 2x, 3x,
 *                               4x... the DS size (a whole multiple); a file
 *                               of another size is reported and ignored
 *
 * With "HD textures" at Auto, a texture with no replacement is upscaled by
 * pixup.c on worker threads (most of the cores) and saved.  It is drawn at
 * the DS size until its copy is ready, so a map change that brings many new
 * textures never waits.
 *
 * The folder sits beside the program.  The PNG reading and writing below
 * is the port's own (through sdk/inflate.c; any colour type and bit depth,
 * not interlaced); a file it cannot read is reported and ignored.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "settings.h"
#include "texpack.h"
#include "pixup.h"
#include "plat_thread.h"
#include "inflate.h"

#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#define mkdir_one(p) _mkdir(p)
#define SEP "\\"
#else
#include <dirent.h>
#include <sys/stat.h>
#define mkdir_one(p) mkdir((p), 0777)
#define SEP "/"
#endif

typedef struct {
    uint64_t hash;
    int state;          /* 0 not read yet, 1 loaded, -1 unusable */
    int w, h;
    unsigned char *rgba;
} PackEntry;

static char g_dir[1024];
static PackEntry *g_pack;
static int g_npack, g_cap;
static uint64_t *g_dumped;
static int g_ndumped, g_capdumped;
static int g_ready;

/* The automatic copies, newest first; the oldest go when the cache passes
 * AUTO_BYTES. */
typedef struct {
    uint64_t hash;
    int w, h;
    unsigned char *rgba;
    unsigned stamp;
} AutoEntry;

#define AUTO_SLOTS 1024
#define AUTO_BYTES (160u << 20)
#define AUTO_JOBS  192

static AutoEntry g_auto[AUTO_SLOTS];
static size_t g_auto_bytes;
static unsigned g_stamp;

/* ------------------------------------------------------------------ */
/* PNG                                                                 */
/* ------------------------------------------------------------------ */

static uint32_t be32(const unsigned char *p)
{
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

static void put_be32(unsigned char *p, uint32_t v)
{
    p[0] = (unsigned char)(v >> 24);
    p[1] = (unsigned char)(v >> 16);
    p[2] = (unsigned char)(v >> 8);
    p[3] = (unsigned char)v;
}

static int paeth(int a, int b, int c)
{
    int p = a + b - c;
    int pa = p > a ? p - a : a - p;
    int pb = p > b ? p - b : b - p;
    int pc = p > c ? p - c : c - p;

    return pa <= pb && pa <= pc ? a : pb <= pc ? b : c;
}

/* One sample of a row: depth 1, 2, 4, 8 or 16 bits (16 keeps the high
 * byte), grey scaled to 0..255; a palette index as it is. */
static int sample(const unsigned char *row, size_t i, int depth, int scale)
{
    int v;

    if (depth == 16) {
        return row[i * 2];
    }
    if (depth == 8) {
        return row[i];
    }
    v = (row[(i * (size_t)depth) >> 3] >> (8 - depth - (int)((i * (size_t)depth) & 7))) &
        ((1 << depth) - 1);
    return scale ? v * 255 / ((1 << depth) - 1) : v;
}

/* A PNG (grey, RGB, palette, grey and alpha, or RGBA, any bit depth; not
 * interlaced) as RGBA, or NULL. */
static unsigned char *png_read(const char *path, int *out_w, int *out_h)
{
    static const unsigned char sig[8] = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };
    unsigned char pal[256][4];
    unsigned char *file = NULL, *z = NULL, *raw = NULL, *rgba = NULL;
    size_t size, at, zlen = 0, rawlen, stride;
    long n;
    uint32_t w = 0, h = 0, x, y;
    int type = -1, depth = 0, chans, i;
    size_t bpp;
    FILE *f = fopen(path, "rb");

    if (f == NULL) {
        return NULL;
    }
    if (fseek(f, 0, SEEK_END) != 0 || (n = ftell(f)) < 8 || fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return NULL;
    }
    size = (size_t)n;
    file = (unsigned char *)malloc(size);
    z = (unsigned char *)malloc(size);
    if (file == NULL || z == NULL || fread(file, 1, size, f) != size ||
        memcmp(file, sig, 8) != 0) {
        goto fail;
    }
    memset(pal, 255, sizeof(pal));
    for (at = 8; at + 12 <= size;) {
        uint32_t len = be32(file + at);
        const unsigned char *d = file + at + 8;

        if (len > size - at - 12) {
            goto fail;
        }
        if (memcmp(file + at + 4, "IHDR", 4) == 0 && len >= 13) {
            w = be32(d);
            h = be32(d + 4);
            depth = d[8];
            type = d[9];
            if ((depth != 1 && depth != 2 && depth != 4 && depth != 8 && depth != 16) ||
                d[12] != 0 || w == 0 || h == 0 || w > 8192 || h > 8192) {
                goto fail;
            }
        } else if (memcmp(file + at + 4, "PLTE", 4) == 0) {
            for (i = 0; i < 256 && (uint32_t)i * 3 + 2 < len; i++) {
                pal[i][0] = d[i * 3];
                pal[i][1] = d[i * 3 + 1];
                pal[i][2] = d[i * 3 + 2];
            }
        } else if (memcmp(file + at + 4, "tRNS", 4) == 0 && type == 3) {
            for (i = 0; i < 256 && (uint32_t)i < len; i++) {
                pal[i][3] = d[i];
            }
        } else if (memcmp(file + at + 4, "IDAT", 4) == 0) {
            memcpy(z + zlen, d, len);
            zlen += len;
        } else if (memcmp(file + at + 4, "IEND", 4) == 0) {
            break;
        }
        at += 12 + (size_t)len;
    }
    chans = type == 0 ? 1 : type == 2 ? 3 : type == 3 ? 1 : type == 4 ? 2 : type == 6 ? 4 : 0;
    if (chans == 0 || zlen < 6) {
        goto fail;
    }
    if (depth < 8 && type != 0 && type != 3) {
        goto fail;
    }
    stride = ((size_t)w * (size_t)chans * (size_t)depth + 7) / 8;
    bpp = depth < 8 ? 1 : (size_t)chans * (size_t)depth / 8;   /* filter distance */
    rawlen = (stride + 1) * h;
    raw = (unsigned char *)malloc(rawlen);
    rgba = (unsigned char *)malloc((size_t)w * h * 4);
    /* the zlib stream: a 2-byte header, the deflate data, the Adler-32 */
    if (raw == NULL || rgba == NULL ||
        bw_inflate(z + 2, zlen - 2, raw, rawlen, NULL) != BW_INFLATE_OK) {
        goto fail;
    }
    for (y = 0; y < h; y++) {
        unsigned char *row = raw + y * (stride + 1);
        unsigned char *cur = row + 1;
        const unsigned char *up = y > 0 ? raw + (y - 1) * (stride + 1) + 1 : NULL;
        size_t k;

        for (k = 0; k < stride; k++) {
            int a = k >= bpp ? cur[k - bpp] : 0;
            int b = up != NULL ? up[k] : 0;
            int c = up != NULL && k >= bpp ? up[k - bpp] : 0;

            switch (row[0]) {
            case 0: break;
            case 1: cur[k] = (unsigned char)(cur[k] + a); break;
            case 2: cur[k] = (unsigned char)(cur[k] + b); break;
            case 3: cur[k] = (unsigned char)(cur[k] + ((a + b) >> 1)); break;
            case 4: cur[k] = (unsigned char)(cur[k] + paeth(a, b, c)); break;
            default: goto fail;
            }
        }
        for (x = 0; x < w; x++) {
            size_t at0 = (size_t)x * (size_t)chans;
            unsigned char *o = rgba + ((size_t)y * w + x) * 4;

            if (type == 3) {
                memcpy(o, pal[sample(cur, at0, depth, 0)], 4);
            } else if (chans <= 2) {
                o[0] = o[1] = o[2] = (unsigned char)sample(cur, at0, depth, 1);
                o[3] = chans == 2 ? (unsigned char)sample(cur, at0 + 1, depth, 1) : 255;
            } else {
                o[0] = (unsigned char)sample(cur, at0, depth, 1);
                o[1] = (unsigned char)sample(cur, at0 + 1, depth, 1);
                o[2] = (unsigned char)sample(cur, at0 + 2, depth, 1);
                o[3] = chans == 4 ? (unsigned char)sample(cur, at0 + 3, depth, 1) : 255;
            }
        }
    }
    free(file);
    free(z);
    free(raw);
    fclose(f);
    *out_w = (int)w;
    *out_h = (int)h;
    return rgba;

fail:
    free(file);
    free(z);
    free(raw);
    free(rgba);
    fclose(f);
    return NULL;
}

static int png_chunk(FILE *f, const char *type, const unsigned char *data, uint32_t n)
{
    unsigned char head[8];
    unsigned char tail[4];
    uint32_t crc;

    put_be32(head, n);
    memcpy(head + 4, type, 4);
    crc = bw_crc32(0, head + 4, 4);
    crc = bw_crc32(crc, data, n);
    put_be32(tail, crc);
    return fwrite(head, 1, 8, f) == 8 && (n == 0 || fwrite(data, 1, n, f) == n) &&
        fwrite(tail, 1, 4, f) == 4;
}

/* Deflate with the fixed codes: greedy matches over a 32 KB window.  Pixel
 * art compresses well this way (runs of a colour, rows repeated by an
 * upscale), and sdk/inflate.c reads it back. */
typedef struct {
    unsigned char *p;
    size_t at;
    uint32_t bits;
    int n;
} BitOut;

static const unsigned short k_lbase[29] = {
    3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
    35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258
};
static const unsigned char k_lext[29] = {
    0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
    3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0
};
static const unsigned short k_dbase[30] = {
    1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
    257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145,
    8193, 12289, 16385, 24577
};
static const unsigned char k_dext[30] = {
    0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6,
    7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13
};

static void put_bits(BitOut *o, uint32_t v, int n)
{
    o->bits |= v << o->n;
    o->n += n;
    while (o->n >= 8) {
        o->p[o->at++] = (unsigned char)o->bits;
        o->bits >>= 8;
        o->n -= 8;
    }
}

/* A Huffman code goes out first bit first. */
static void put_code(BitOut *o, uint32_t code, int n)
{
    uint32_t r = 0;
    int i;

    for (i = 0; i < n; i++) {
        r = (r << 1) | ((code >> i) & 1u);
    }
    put_bits(o, r, n);
}

static void put_sym(BitOut *o, int v)
{
    if (v < 144) {
        put_code(o, 0x30u + (uint32_t)v, 8);
    } else if (v < 256) {
        put_code(o, 0x190u + (uint32_t)(v - 144), 9);
    } else if (v < 280) {
        put_code(o, (uint32_t)(v - 256), 7);
    } else {
        put_code(o, 0xC0u + (uint32_t)(v - 280), 8);
    }
}

static void put_match(BitOut *o, int len, int dist)
{
    int i = 28, d = 29;

    while (k_lbase[i] > len) {
        i--;
    }
    put_sym(o, 257 + i);
    put_bits(o, (uint32_t)(len - k_lbase[i]), k_lext[i]);
    while (k_dbase[d] > dist) {
        d--;
    }
    put_code(o, (uint32_t)d, 5);
    put_bits(o, (uint32_t)(dist - k_dbase[d]), k_dext[d]);
}

#define WIN_BITS 15
#define WIN      (1 << WIN_BITS)
#define HASH_N   (1 << 15)

/* raw: n bytes.  Returns a malloc'd zlib stream in *out, or -1. */
static int deflate_fixed(const unsigned char *raw, size_t n, unsigned char **out, size_t *out_n)
{
    int *head = (int *)malloc(HASH_N * sizeof(int));
    int *prev = (int *)malloc(WIN * sizeof(int));
    BitOut o;
    uint32_t a = 1, b = 0;
    size_t i;

    o.p = (unsigned char *)malloc(n + n / 8 + 64);
    if (head == NULL || prev == NULL || o.p == NULL) {
        free(head);
        free(prev);
        free(o.p);
        return -1;
    }
    for (i = 0; i < HASH_N; i++) {
        head[i] = -1;
    }
    o.at = 0;
    o.bits = 0;
    o.n = 0;
    o.p[o.at++] = 0x78;
    o.p[o.at++] = 0x01;
    put_bits(&o, 1, 1);             /* the last block */
    put_bits(&o, 1, 2);             /* fixed codes */
    i = 0;
    while (i < n) {
        int best = 0, dist = 0;

        if (i + 3 <= n) {
            unsigned hsh = ((unsigned)raw[i] << 10 ^ (unsigned)raw[i + 1] << 5 ^ raw[i + 2]) &
                (HASH_N - 1);
            int cand = head[hsh], tries = 32;
            size_t max = n - i < 258 ? n - i : 258;

            while (cand >= 0 && i - (size_t)cand < WIN && tries-- > 0) {
                size_t l = 0;

                while (l < max && raw[cand + l] == raw[i + l]) {
                    l++;
                }
                if ((int)l > best) {
                    best = (int)l;
                    dist = (int)(i - (size_t)cand);
                    if (l == max) {
                        break;
                    }
                }
                cand = prev[cand & (WIN - 1)];
            }
        }
        if (best >= 3) {
            size_t k;

            put_match(&o, best, dist);
            for (k = 0; k < (size_t)best; k++, i++) {
                if (i + 3 <= n) {
                    unsigned hsh = ((unsigned)raw[i] << 10 ^ (unsigned)raw[i + 1] << 5 ^
                                    raw[i + 2]) & (HASH_N - 1);

                    prev[i & (WIN - 1)] = head[hsh];
                    head[hsh] = (int)i;
                }
            }
        } else {
            if (i + 3 <= n) {
                unsigned hsh = ((unsigned)raw[i] << 10 ^ (unsigned)raw[i + 1] << 5 ^
                                raw[i + 2]) & (HASH_N - 1);

                prev[i & (WIN - 1)] = head[hsh];
                head[hsh] = (int)i;
            }
            put_sym(&o, raw[i]);
            i++;
        }
    }
    put_sym(&o, 256);
    if (o.n > 0) {
        o.p[o.at++] = (unsigned char)o.bits;
    }
    for (i = 0; i < n; i++) {
        a = (a + raw[i]) % 65521u;
        b = (b + a) % 65521u;
    }
    put_be32(o.p + o.at, (b << 16) | a);
    o.at += 4;
    free(head);
    free(prev);
    *out = o.p;
    *out_n = o.at;
    return 0;
}

/* RGBA as a PNG (filter none, deflate_fixed). */
static int png_write(const char *path, const unsigned char *rgba, int w, int h)
{
    static const unsigned char sig[8] = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };
    size_t row = 1 + (size_t)w * 4;
    unsigned char ihdr[13];
    unsigned char *raw, *z;
    size_t zlen;
    int y, ok;
    FILE *f;

    raw = (unsigned char *)malloc(row * (size_t)h);
    if (raw == NULL) {
        return -1;
    }
    for (y = 0; y < h; y++) {
        raw[(size_t)y * row] = 0;   /* filter: none */
        memcpy(raw + (size_t)y * row + 1, rgba + (size_t)y * w * 4, row - 1);
    }
    ok = deflate_fixed(raw, row * (size_t)h, &z, &zlen);
    free(raw);
    if (ok != 0) {
        return -1;
    }
    put_be32(ihdr, (uint32_t)w);
    put_be32(ihdr + 4, (uint32_t)h);
    ihdr[8] = 8;                    /* bits */
    ihdr[9] = 6;                    /* RGBA */
    ihdr[10] = ihdr[11] = ihdr[12] = 0;
    f = fopen(path, "wb");
    if (f == NULL) {
        free(z);
        return -1;
    }
    ok = fwrite(sig, 1, 8, f) == 8 && png_chunk(f, "IHDR", ihdr, 13) &&
        png_chunk(f, "IDAT", z, (uint32_t)zlen) && png_chunk(f, "IEND", NULL, 0);
    fclose(f);
    free(z);
    return ok ? 0 : -1;
}

/* ------------------------------------------------------------------ */
/* The pack                                                            */
/* ------------------------------------------------------------------ */

static void base_dir(void)
{
#ifdef _WIN32
    char exe[MAX_PATH];
    DWORD n = GetModuleFileNameA(NULL, exe, sizeof(exe));
    char *slash;

    if (n > 0 && n < sizeof(exe)) {
        exe[n] = '\0';
        slash = strrchr(exe, '\\');
        if (slash != NULL) {
            *slash = '\0';
            snprintf(g_dir, sizeof(g_dir), "%s\\textures", exe);
            return;
        }
    }
#endif
    snprintf(g_dir, sizeof(g_dir), "textures");
}

/* "<16 hex digits>.png" */
static int parse_hash(const char *name, uint64_t *out)
{
    uint64_t h = 0;
    int i;

    for (i = 0; i < 16; i++) {
        char c = name[i];
        int v = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 :
                c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;

        if (v < 0) {
            return 0;
        }
        h = h << 4 | (uint64_t)v;
    }
    if (strcmp(name + 16, ".png") != 0 && strcmp(name + 16, ".PNG") != 0) {
        return 0;
    }
    *out = h;
    return 1;
}

static void add_entry(uint64_t h)
{
    if (g_npack == g_cap) {
        int nc = g_cap ? g_cap * 2 : 256;
        PackEntry *n = (PackEntry *)realloc(g_pack, (size_t)nc * sizeof(*n));

        if (n == NULL) {
            return;
        }
        g_pack = n;
        g_cap = nc;
    }
    memset(&g_pack[g_npack], 0, sizeof(g_pack[0]));
    g_pack[g_npack++].hash = h;
}

static void scan(void)
{
    uint64_t h;
#ifdef _WIN32
    char pat[1100];
    WIN32_FIND_DATAA fd;
    HANDLE f;

    snprintf(pat, sizeof(pat), "%s\\*.png", g_dir);
    f = FindFirstFileA(pat, &fd);
    if (f == INVALID_HANDLE_VALUE) {
        return;
    }
    do {
        if (parse_hash(fd.cFileName, &h)) {
            add_entry(h);
        }
    } while (FindNextFileA(f, &fd));
    FindClose(f);
#else
    DIR *d = opendir(g_dir);
    struct dirent *e;

    if (d == NULL) {
        return;
    }
    while ((e = readdir(d)) != NULL) {
        if (parse_hash(e->d_name, &h)) {
            add_entry(h);
        }
    }
    closedir(d);
#endif
}

static void init(void)
{
    if (g_ready) {
        return;
    }
    g_ready = 1;
    base_dir();
    if (eidos_settings_get(EIDOS_OPT_TEXTURE_PACK) >= 1) {
        scan();
    }
    if (g_npack > 0) {
        fprintf(stderr, "eidos: HD textures: %d in %s\n", g_npack, g_dir);
    }
}

uint64_t eidos_texpack_hash(const unsigned char *rgba, int w, int h)
{
    uint64_t x = 1469598103934665603ull;
    size_t i, n = (size_t)w * (size_t)h * 4u;

    x = (x ^ (uint64_t)w) * 1099511628211ull;
    x = (x ^ (uint64_t)h) * 1099511628211ull;
    for (i = 0; i < n; i += 4) {
        /* fully transparent texels are one colour, whatever their RGB */
        uint32_t v = rgba[i + 3] == 0 ? 0u :
            (uint32_t)rgba[i] | (uint32_t)rgba[i + 1] << 8 |
            (uint32_t)rgba[i + 2] << 16 | (uint32_t)rgba[i + 3] << 24;

        x = (x ^ v) * 1099511628211ull;
    }
    return x;
}

static void dump(uint64_t h, const unsigned char *rgba, int w, int ht)
{
    char path[1200];
    FILE *f;
    int i;

    for (i = 0; i < g_ndumped; i++) {
        if (g_dumped[i] == h) {
            return;
        }
    }
    if (g_ndumped == g_capdumped) {
        int nc = g_capdumped ? g_capdumped * 2 : 1024;
        uint64_t *n = (uint64_t *)realloc(g_dumped, (size_t)nc * sizeof(*n));

        if (n == NULL) {
            return;
        }
        g_dumped = n;
        g_capdumped = nc;
    }
    g_dumped[g_ndumped++] = h;
    mkdir_one(g_dir);
    snprintf(path, sizeof(path), "%s" SEP "dump", g_dir);
    mkdir_one(path);
    snprintf(path, sizeof(path), "%s" SEP "dump" SEP "%016llx.png", g_dir,
             (unsigned long long)h);
    f = fopen(path, "rb");
    if (f != NULL) {
        fclose(f);                  /* from an earlier run */
        return;
    }
    if (png_write(path, rgba, w, ht) != 0) {
        fprintf(stderr, "eidos: HD textures: could not write %s\n", path);
    }
}

/* ------------------------------------------------------------------ */
/* Auto                                                                */
/* ------------------------------------------------------------------ */

/* A texture queued for the workers.  The cache itself is only touched on
 * the drawing thread (collect_done moves a finished copy in), so a copy
 * handed to the GPU is never freed under it. */
typedef struct {
    uint64_t hash;
    int w, h, s;
    unsigned char *in, *up;
    int state;          /* 0 free, 1 queued, 2 running, 3 done */
} AutoJob;

static AutoJob g_jobs[AUTO_JOBS];
static BwMutex *g_job_mx;
static BwCond *g_job_cv;
static int g_workers;   /* 0 not started, 1 running */

/* textures/auto/<hash>-<w>.png: the copy a run before made. */
static void auto_path(char *path, size_t n, uint64_t hash, int w)
{
    snprintf(path, n, "%s" SEP "auto" SEP "%016llx-%d.png", g_dir, (unsigned long long)hash, w);
}

static unsigned char *auto_load(uint64_t hash, int w, int h)
{
    char path[1200];
    unsigned char *rgba;
    int fw = 0, fh = 0;

    auto_path(path, sizeof(path), hash, w);
    rgba = png_read(path, &fw, &fh);
    if (rgba != NULL && (fw != w || fh != h)) {
        free(rgba);
        rgba = NULL;
    }
    return rgba;
}

/* Written under another name and renamed, so a run closed halfway leaves
 * no broken file. */
static void auto_save(uint64_t hash, const unsigned char *rgba, int w, int h)
{
    char path[1200], tmp[1210];

    snprintf(path, sizeof(path), "%s" SEP "auto", g_dir);
    mkdir_one(g_dir);
    mkdir_one(path);
    auto_path(path, sizeof(path), hash, w);
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    if (png_write(tmp, rgba, w, h) != 0) {
        remove(tmp);
        return;
    }
    remove(path);
    if (rename(tmp, path) != 0) {
        remove(tmp);
    }
}

static void auto_worker(void *arg)
{
    (void)arg;
    for (;;) {
        AutoJob *j = NULL;
        unsigned char *up;
        int i;

        bw_mutex_lock(g_job_mx);
        while (j == NULL) {
            for (i = 0; i < AUTO_JOBS; i++) {
                if (g_jobs[i].state == 1) {
                    j = &g_jobs[i];
                    break;
                }
            }
            if (j == NULL) {
                bw_cond_wait(g_job_cv, g_job_mx);
            }
        }
        j->state = 2;
        bw_mutex_unlock(g_job_mx);
        up = auto_load(j->hash, j->w * j->s, j->h * j->s);
        if (up == NULL) {
            up = eidos_pixup_half(j->in, j->w, j->h, j->s);
            if (up != NULL) {
                auto_save(j->hash, up, j->w * j->s, j->h * j->s);
            }
        }
        bw_mutex_lock(g_job_mx);
        j->up = up;
        j->state = 3;
        bw_mutex_unlock(g_job_mx);
    }
}

static void start_workers(void)
{
    int i, n;

    if (g_workers != 0) {
        return;
    }
    g_workers = 1;
    g_job_mx = bw_mutex_create();
    g_job_cv = bw_cond_create();
    /* two cores left for the game, at most eight workers */
    n = (int)bw_cpu_count() - 2;
    if (n < 2) {
        n = 2;
    }
    if (n > 8) {
        n = 8;
    }
    for (i = 0; i < n; i++) {
        (void)bw_thread_start(auto_worker, NULL);
    }
}

/* Into the cache, the oldest out first; takes `up`. */
static void cache_put(uint64_t hash, int w, int h, int s, unsigned char *up)
{
    size_t bytes = (size_t)w * h * s * s * 4;
    int i, free_slot = -1;

    for (i = 0; i < AUTO_SLOTS; i++) {
        if (g_auto[i].rgba == NULL) {
            free_slot = i;
            break;
        }
    }
    while (g_auto_bytes + bytes > AUTO_BYTES || free_slot < 0) {
        int v = -1;

        for (i = 0; i < AUTO_SLOTS; i++) {
            if (g_auto[i].rgba != NULL && (v < 0 || g_auto[i].stamp < g_auto[v].stamp)) {
                v = i;
            }
        }
        if (v < 0) {
            break;
        }
        g_auto_bytes -= (size_t)g_auto[v].w * g_auto[v].h * 4;
        free(g_auto[v].rgba);
        g_auto[v].rgba = NULL;
        if (free_slot < 0) {
            free_slot = v;
        }
    }
    if (free_slot < 0) {
        free(up);
        return;
    }
    g_auto[free_slot].hash = hash;
    g_auto[free_slot].w = w * s;
    g_auto[free_slot].h = h * s;
    g_auto[free_slot].rgba = up;
    g_auto[free_slot].stamp = ++g_stamp;
    g_auto_bytes += bytes;
}

static void collect_done(void)
{
    int i;

    bw_mutex_lock(g_job_mx);
    for (i = 0; i < AUTO_JOBS; i++) {
        AutoJob *j = &g_jobs[i];

        if (j->state != 3) {
            continue;
        }
        free(j->in);
        j->in = NULL;
        if (j->up != NULL) {
            cache_put(j->hash, j->w, j->h, j->s, j->up);
        }
        j->up = NULL;
        j->state = 0;
    }
    bw_mutex_unlock(g_job_mx);
}

static const unsigned char *auto_hd(uint64_t hash, const unsigned char *rgba,
                                    int w, int h, int *out_w, int *out_h,
                                    int *later)
{
    int i, s, free_job = -1;

    start_workers();
    collect_done();
    for (i = 0; i < AUTO_SLOTS; i++) {
        if (g_auto[i].rgba != NULL && g_auto[i].hash == hash &&
            g_auto[i].w % w == 0 && g_auto[i].h % h == 0) {
            g_auto[i].stamp = ++g_stamp;
            *out_w = g_auto[i].w;
            *out_h = g_auto[i].h;
            return g_auto[i].rgba;
        }
    }
    /* 4x for the usual sizes, 2x for large ones, none past 1024 */
    if (w > 1024 || h > 1024) {
        return NULL;
    }
    s = (w <= 256 && h <= 256) || (long)w * h <= 32768 ? 4 : 2;
    bw_mutex_lock(g_job_mx);
    for (i = 0; i < AUTO_JOBS; i++) {
        if (g_jobs[i].state != 0 && g_jobs[i].hash == hash && g_jobs[i].w == w) {
            break;
        }
        if (g_jobs[i].state == 0 && free_job < 0) {
            free_job = i;
        }
    }
    if (i == AUTO_JOBS && free_job >= 0) {
        unsigned char *in = (unsigned char *)malloc((size_t)w * h * 4);

        if (in != NULL) {
            memcpy(in, rgba, (size_t)w * h * 4);
            g_jobs[free_job].hash = hash;
            g_jobs[free_job].w = w;
            g_jobs[free_job].h = h;
            g_jobs[free_job].s = s;
            g_jobs[free_job].in = in;
            g_jobs[free_job].up = NULL;
            g_jobs[free_job].state = 1;
            bw_cond_broadcast(g_job_cv);
        }
    }
    bw_mutex_unlock(g_job_mx);
    *later = 1;         /* at the DS size until the worker is done */
    return NULL;
}

/* ------------------------------------------------------------------ */
/* The caller's side                                                   */
/* ------------------------------------------------------------------ */

const unsigned char *eidos_texpack_find(const unsigned char *rgba, int w, int h,
                                        int *out_w, int *out_h, int *later)
{
    uint64_t hash;
    int i, mode, dumping;

    *later = 0;
    if (rgba == NULL || w <= 0 || h <= 0) {
        return NULL;
    }
    mode = eidos_settings_get(EIDOS_OPT_TEXTURE_PACK);
    dumping = eidos_settings_get(EIDOS_OPT_TEXTURE_DUMP) == 1;
    if (mode == 0 && !dumping) {
        return NULL;
    }
    init();
    hash = eidos_texpack_hash(rgba, w, h);
    if (dumping) {
        dump(hash, rgba, w, h);
    }
    for (i = 0; i < g_npack && mode >= 1; i++) {
        PackEntry *e = &g_pack[i];

        if (e->hash != hash) {
            continue;
        }
        if (e->state == 0) {
            char path[1200];

            snprintf(path, sizeof(path), "%s" SEP "%016llx.png", g_dir,
                     (unsigned long long)hash);
            e->rgba = png_read(path, &e->w, &e->h);
            if (e->rgba == NULL || e->w % w || e->h % h || e->w / w != e->h / h ||
                e->w / w < 2 || e->w / w > 16) {
                fprintf(stderr, "eidos: HD textures: %016llx.png is not a PNG a whole "
                        "multiple of %dx%d\n", (unsigned long long)hash, w, h);
                free(e->rgba);
                e->rgba = NULL;
                e->state = -1;
            } else {
                e->state = 1;
            }
        }
        if (e->state != 1) {
            break;
        }
        *out_w = e->w;
        *out_h = e->h;
        return e->rgba;
    }
    if (mode == 2) {
        return auto_hd(hash, rgba, w, h, out_w, out_h, later);
    }
    return NULL;
}
