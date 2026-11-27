/*
 * sdk/vfs_sdat.c -- the sound archive append merge (sdk/vfs_sdat.h has the
 * model, the declaration grammar and the numbering rule).
 *
 * Why the old tables are left in place.  Every INFO and SYMB offset is
 * relative to its block and points at records or strings that sit anywhere
 * in the block.  Growing a record table in place would shift everything
 * behind it and every pointer would need rewriting, with a generic walk that
 * has to know the size of every record kind (PLAYER and GROUP records vary).
 * Appending a second, longer copy of the table at the end of the block needs
 * none of that: the old records and strings keep their offsets, the new
 * table repeats the old pointers and adds new ones, and one header offset
 * is redirected.  The cost is a few kilobytes of dead space per grown kind.
 *
 * Record layouts (verified against the shipped archive; the same decodes are
 * in engine/audio/sdat.c):
 *   SEQ     12 bytes: file id u32, bank u16, volume, cpr, ppr, player, 2 pad
 *   BANK    12 bytes: file id u32, four wave archive ids u16 (0xFFFF unused)
 *   WAVARC   4 bytes: file id u32
 * FAT entries are 16 bytes: absolute offset, size, two zero words.
 *
 * Reads are explicit byte shifts; no pointer casts of file bytes.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vfs_sdat.h"

#define KINDS      8
#define FILE_MAX   (16u * 1024u * 1024u)
#define IMAGE_MAX  0x7F000000u
#define ALIGN_FILE 0x20u

static u32 rd32(const u8 *p)
{
    return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}

static void wr32(u8 *p, u32 v)
{
    p[0] = (u8)v;
    p[1] = (u8)(v >> 8);
    p[2] = (u8)(v >> 16);
    p[3] = (u8)(v >> 24);
}

/* ------------------------------------------------------------------ */
/* The declaration parser                                              */
/* ------------------------------------------------------------------ */

static void set_err(char *err, size_t n, int line, const char *msg, const char *what)
{
    if (err != NULL && n > 0) {
        snprintf(err, n, "sdat.txt line %d: %.80s%s%.100s", line, msg,
                 what[0] != '\0' ? ": " : "", what);
    }
}

static int name_char_ok(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
        || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.';
}

void bw_sdat_mod_key(const char *mod_name, char *out, size_t size)
{
    size_t i, n = 0;

    if (out == NULL || size == 0) {
        return;
    }
    for (i = 0; mod_name != NULL && mod_name[i] != '\0' && n + 1 < size
                && n + 1 < BW_SDATM_NAME_MAX; i++) {
        out[n++] = name_char_ok(mod_name[i]) ? mod_name[i] : '_';
    }
    if (n == 0 && size > 3) {
        memcpy(out, "mod", 3);   /* (a mod has a name: its folder's at least) */
        n = 3;
    }
    out[n] = '\0';
}

/* `ref` with the part before its last ':' made a mod key, into `out` (an
 * entry's own name holds no ':', a mod's name may). */
static void key_qualified(const char *ref, char *out, size_t size)
{
    const char *colon = strrchr(ref, ':');
    char *mod;
    char key[BW_SDATM_NAME_MAX];
    size_t n;

    if (colon == NULL) {
        snprintf(out, size, "%s", ref);
        return;
    }
    n = (size_t)(colon - ref);
    mod = (char *)malloc(n + 1);
    if (mod == NULL) {
        snprintf(out, size, "%s", ref);
        return;
    }
    memcpy(mod, ref, n);
    mod[n] = '\0';
    bw_sdat_mod_key(mod, key, sizeof(key));
    free(mod);
    snprintf(out, size, "%s%s", key, colon);
}

static int name_ok(const char *s)
{
    size_t i, n = strlen(s);

    if (n == 0 || n >= BW_SDATM_NAME_MAX) {
        return 0;
    }
    for (i = 0; i < n; i++) {
        if (!name_char_ok(s[i])) {
            return 0;
        }
    }
    return 1;
}

static int num_in(const char *s, u32 max, u32 *out)
{
    char *end;
    unsigned long v;

    if (s[0] < '0' || s[0] > '9') {
        return 0;
    }
    v = strtoul(s, &end, 10);
    if (*end != '\0' || v > max) {
        return 0;
    }
    *out = (u32)v;
    return 1;
}

void bw_sdat_decl_free(BwSdatDecl *decl)
{
    if (decl != NULL) {
        free(decl->entries);
        decl->entries = NULL;
        decl->count = 0;
    }
}

/* Parse one line (already cut at the comment, trimmed, non-empty). */
static int parse_line(char *line, int lineno, BwSdatEntry *e, char *err, size_t en)
{
    char *tok[16];
    int n = 0, i;
    char *p = line;

    while (*p != '\0') {
        while (*p == ' ' || *p == '\t') {
            *p++ = '\0';
        }
        if (*p == '\0') {
            break;
        }
        if (n == 16) {
            set_err(err, en, lineno, "too many words", "");
            return -1;
        }
        tok[n++] = p;
        while (*p != '\0' && *p != ' ' && *p != '\t') {
            p++;
        }
    }
    memset(e, 0, sizeof(*e));
    e->volume = 127;
    e->cpr = 64;
    e->ppr = 64;
    e->player = 0;
    if (n < 4 || strcmp(tok[2], "=") != 0) {
        set_err(err, en, lineno, "expected `<kind> <name> = <file>`", "");
        return -1;
    }
    if (strcmp(tok[0], "wavarc") == 0) {
        e->kind = BW_SDATM_WAVARC;
    } else if (strcmp(tok[0], "bank") == 0) {
        e->kind = BW_SDATM_BANK;
    } else if (strcmp(tok[0], "seq") == 0) {
        e->kind = BW_SDATM_SEQ;
    } else {
        set_err(err, en, lineno, "unknown kind", tok[0]);
        return -1;
    }
    if (!name_ok(tok[1])) {
        set_err(err, en, lineno, "bad name", tok[1]);
        return -1;
    }
    strcpy(e->name, tok[1]);
    if (strlen(tok[3]) >= BW_SDATM_FILE_MAX) {
        set_err(err, en, lineno, "file name too long", tok[3]);
        return -1;
    }
    strcpy(e->file, tok[3]);

    for (i = 4; i < n; i++) {
        char *eq = strchr(tok[i], '=');
        const char *key, *val;
        u32 v;

        if (eq == NULL) {
            set_err(err, en, lineno, "expected key=value", tok[i]);
            return -1;
        }
        *eq = '\0';
        key = tok[i];
        val = eq + 1;
        if (strcmp(key, "wavarc") == 0 && e->kind == BW_SDATM_BANK) {
            const char *q = val;
            while (1) {
                const char *comma = strchr(q, ',');
                size_t len = comma != NULL ? (size_t)(comma - q) : strlen(q);
                if (len == 0 || len >= BW_SDATM_REF_MAX
                    || e->nrefs == BW_SDATM_REFS_MAX) {
                    set_err(err, en, lineno, "bad wavarc list (1 to 4 refs)", val);
                    return -1;
                }
                memcpy(e->refs[e->nrefs], q, len);
                e->refs[e->nrefs][len] = '\0';
                e->nrefs++;
                if (comma == NULL) {
                    break;
                }
                q = comma + 1;
            }
        } else if (strcmp(key, "bank") == 0 && e->kind == BW_SDATM_SEQ) {
            if (val[0] == '\0' || strlen(val) >= BW_SDATM_REF_MAX) {
                set_err(err, en, lineno, "bad bank ref", val);
                return -1;
            }
            strcpy(e->refs[0], val);
            e->nrefs = 1;
        } else if (e->kind == BW_SDATM_SEQ
                   && (strcmp(key, "volume") == 0 || strcmp(key, "cpr") == 0
                       || strcmp(key, "ppr") == 0 || strcmp(key, "player") == 0)) {
            u32 max = strcmp(key, "player") == 0 ? 7u : 127u;
            if (!num_in(val, max, &v)) {
                set_err(err, en, lineno, "value out of range", val);
                return -1;
            }
            if (strcmp(key, "volume") == 0) {
                e->volume = v;
            } else if (strcmp(key, "cpr") == 0) {
                e->cpr = v;
            } else if (strcmp(key, "ppr") == 0) {
                e->ppr = v;
            } else {
                e->player = v;
            }
        } else {
            set_err(err, en, lineno, "key not valid for this kind", key);
            return -1;
        }
    }
    if (e->kind == BW_SDATM_BANK && e->nrefs == 0) {
        set_err(err, en, lineno, "bank needs wavarc=", "");
        return -1;
    }
    if (e->kind == BW_SDATM_SEQ && e->nrefs == 0) {
        set_err(err, en, lineno, "seq needs bank=", "");
        return -1;
    }
    return 0;
}

int bw_sdat_parse_decl(const char *text, BwSdatDecl *out, char *err, size_t err_size)
{
    const char *p = text;
    u32 cap = 0;
    int lineno = 0;

    out->entries = NULL;
    out->count = 0;
    if (err != NULL && err_size > 0) {
        err[0] = '\0';
    }
    while (*p != '\0') {
        const char *eol = strchr(p, '\n');
        size_t len = eol != NULL ? (size_t)(eol - p) : strlen(p);
        char *line = (char *)malloc(len + 1);
        size_t i, start = 0;
        BwSdatEntry e;
        u32 j;

        lineno++;
        if (line == NULL) {
            bw_sdat_decl_free(out);
            set_err(err, err_size, lineno, "out of memory", "");
            return -1;
        }
        memcpy(line, p, len);
        line[len] = '\0';
        p += len;
        if (*p == '\n') {
            p++;
        }
        /* A '#' starts a comment at the line start or after white space. */
        for (i = 0; line[i] != '\0'; i++) {
            if (line[i] == '#' && (i == 0 || line[i - 1] == ' ' || line[i - 1] == '\t')) {
                line[i] = '\0';
                break;
            }
        }
        i = strlen(line);
        while (i > 0 && (line[i - 1] == ' ' || line[i - 1] == '\t' || line[i - 1] == '\r')) {
            line[--i] = '\0';
        }
        while (line[start] == ' ' || line[start] == '\t') {
            start++;
        }
        if (line[start] == '\0') {
            free(line);
            continue;
        }
        if (parse_line(line + start, lineno, &e, err, err_size) != 0) {
            free(line);
            bw_sdat_decl_free(out);
            return -1;
        }
        free(line);
        for (j = 0; j < out->count; j++) {
            if (out->entries[j].kind == e.kind && strcmp(out->entries[j].name, e.name) == 0) {
                set_err(err, err_size, lineno, "duplicate name", e.name);
                bw_sdat_decl_free(out);
                return -1;
            }
        }
        if (out->count == cap) {
            u32 ncap = cap == 0 ? 16 : cap * 2;
            BwSdatEntry *ne = (BwSdatEntry *)realloc(out->entries, ncap * sizeof(*ne));
            if (ne == NULL) {
                set_err(err, err_size, lineno, "out of memory", "");
                bw_sdat_decl_free(out);
                return -1;
            }
            out->entries = ne;
            cap = ncap;
        }
        out->entries[out->count++] = e;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Reading an archive image                                            */
/* ------------------------------------------------------------------ */

typedef struct {
    const u8 *d;
    u32 size;
    u32 symb_off, symb_size, info_off, info_size, fat_off, fat_size;
    int has_symb;
    u32 fat_count;
    u32 count[KINDS];   /* INFO record counts */
    u32 tab[KINDS];     /* INFO-relative table offset, 0 = none */
    u32 stab[KINDS];    /* SYMB-relative name table offset, 0 = none */
} Image;

static const char *block_ok(const u8 *d, u32 size, u32 off, const char *magic,
                            u32 *bsize)
{
    u32 sz;

    if ((size_t)off + 12 > size || memcmp(d + off, magic, 4) != 0) {
        return "block missing";
    }
    sz = rd32(d + off + 4);
    if (sz < 12 || (size_t)off + sz > size) {
        return "block runs past the end";
    }
    *bsize = sz;
    return NULL;
}

static const char *img_parse(Image *im, const u8 *d, u32 size)
{
    const char *e;
    u32 file_off, file_sz, k;

    memset(im, 0, sizeof(*im));
    if (size < 0x40 || memcmp(d, "SDAT", 4) != 0) {
        return "not an SDAT image";
    }
    im->d = d;
    im->size = size;
    im->symb_off = rd32(d + 0x10);
    im->info_off = rd32(d + 0x18);
    im->fat_off = rd32(d + 0x20);
    file_off = rd32(d + 0x28);
    if ((e = block_ok(d, size, im->info_off, "INFO", &im->info_size)) != NULL) {
        return "INFO block bad";
    }
    if ((e = block_ok(d, size, im->fat_off, "FAT ", &im->fat_size)) != NULL) {
        return "FAT block bad";
    }
    if ((e = block_ok(d, size, file_off, "FILE", &file_sz)) != NULL) {
        return "FILE block bad";
    }
    if (rd32(d + 0x14) != 0) {
        if ((e = block_ok(d, size, im->symb_off, "SYMB", &im->symb_size)) != NULL) {
            return "SYMB block bad";
        }
        im->has_symb = 1;
    }
    if (im->fat_size < 12) {
        return "FAT block bad";
    }
    im->fat_count = rd32(d + im->fat_off + 8);
    if (im->fat_count > (im->fat_size - 12) / 16) {
        return "FAT count past its block";
    }
    for (k = 0; k < KINDS; k++) {
        u32 off = k * 4 + 8 <= im->info_size ? rd32(d + im->info_off + 8 + k * 4) : 0;

        if (off != 0 && (size_t)off + 4 <= im->info_size) {
            u32 c = rd32(d + im->info_off + off);
            if ((size_t)off + 4 + (size_t)c * 4 <= im->info_size) {
                im->count[k] = c;
                im->tab[k] = off;
            }
        }
        if (im->has_symb) {
            u32 soff = rd32(d + im->symb_off + 8 + k * 4);
            if (soff != 0 && (size_t)soff + 4 <= im->symb_size) {
                u32 sc = rd32(d + im->symb_off + soff);
                if ((size_t)soff + 4 + (size_t)sc * 4 <= im->symb_size
                    && sc == im->count[k]) {
                    im->stab[k] = soff;
                }
            }
        }
    }
    return NULL;
}

static int symb_find(const Image *im, u32 kind, const char *name, u32 *id)
{
    u32 i;

    if (kind >= KINDS || !im->has_symb || im->stab[kind] == 0) {
        return -1;
    }
    for (i = 0; i < im->count[kind]; i++) {
        u32 so = rd32(im->d + im->symb_off + im->stab[kind] + 4 + i * 4);
        const char *s;
        size_t room;

        if (so == 0 || so >= im->symb_size) {
            continue;
        }
        s = (const char *)(im->d + im->symb_off + so);
        room = im->symb_size - so;
        if (memchr(s, 0, room) != NULL && strcmp(s, name) == 0) {
            *id = i;
            return 0;
        }
    }
    return -1;
}

int bw_sdat_lookup(const u8 *sdat, u32 size, BwSdatMKind kind,
                   const char *qualified_name, u32 *id)
{
    Image im;
    char key[2 * BW_SDATM_NAME_MAX + 2];

    if (qualified_name == NULL || img_parse(&im, sdat, size) != NULL) {
        return -1;
    }
    /* a mod's names are filed under its key: "Fairy Type:zap" finds
     * "Fairy_Type:zap" */
    key_qualified(qualified_name, key, sizeof(key));
    return symb_find(&im, (u32)kind, key, id);
}

/* ------------------------------------------------------------------ */
/* The merge                                                           */
/* ------------------------------------------------------------------ */

typedef struct {
    u32 mod;
    const BwSdatEntry *e;
    u32 id;
    u32 file_id;
    u32 file_off;
    const u8 *data;
    u32 size;
    u32 ref_id[BW_SDATM_REFS_MAX];
    char qname[2 * BW_SDATM_NAME_MAX + 2];
} Item;

typedef struct {
    u8 *p;
    u32 len;
    u32 cap;
    int failed;
} Buf;

static void buf_need(Buf *b, u32 extra)
{
    if (b->failed) {
        return;
    }
    if (extra > IMAGE_MAX || b->len > IMAGE_MAX - extra) {
        b->failed = 1;
        return;
    }
    if (b->len + extra > b->cap) {
        u32 ncap = b->cap == 0 ? 1u << 16 : b->cap;
        u8 *np;
        while (ncap < b->len + extra) {
            ncap = ncap > IMAGE_MAX / 2 ? IMAGE_MAX : ncap * 2;
        }
        np = (u8 *)realloc(b->p, ncap);
        if (np == NULL) {
            b->failed = 1;
            return;
        }
        b->p = np;
        b->cap = ncap;
    }
}

static void buf_put(Buf *b, const void *src, u32 n)
{
    buf_need(b, n);
    if (!b->failed && n > 0) {
        memcpy(b->p + b->len, src, n);
        b->len += n;
    }
}

static void buf_zero(Buf *b, u32 n)
{
    buf_need(b, n);
    if (!b->failed && n > 0) {
        memset(b->p + b->len, 0, n);
        b->len += n;
    }
}

static void buf_align(Buf *b, u32 a)
{
    buf_zero(b, (a - (b->len & (a - 1))) & (a - 1));
}

static void buf_put32(Buf *b, u32 v)
{
    u8 t[4];
    wr32(t, v);
    buf_put(b, t, 4);
}

static void buf_set32(Buf *b, u32 at, u32 v)
{
    if (!b->failed && at + 4 <= b->len) {
        wr32(b->p + at, v);
    }
}

static const char *kind_magic(BwSdatMKind k)
{
    return k == BW_SDATM_SEQ ? "SSEQ" : k == BW_SDATM_BANK ? "SBNK" : "SWAR";
}

static const char *kind_word(BwSdatMKind k)
{
    return k == BW_SDATM_SEQ ? "seq" : k == BW_SDATM_BANK ? "bank" : "wavarc";
}

/* Resolve one reference to an id of `kind`.  `total` is the final count of
 * that kind (original plus added), the bound for a `#<id>` reference. */
static int resolve_ref(const Image *im, const Item *items, u32 nitems,
                       BwSdatMKind kind, u32 mod, const char *ref, u32 total,
                       u32 *id)
{
    u32 i;

    if (ref[0] == '#') {
        u32 v;
        if (!num_in(ref + 1, 0xFFFFFFFFu, &v) || v >= total) {
            return -1;
        }
        *id = v;
        return 0;
    }
    if (strchr(ref, ':') != NULL) {
        char key[2 * BW_SDATM_NAME_MAX + 2];

        key_qualified(ref, key, sizeof(key));
        for (i = 0; i < nitems; i++) {
            if (items[i].e->kind == kind && strcmp(items[i].qname, key) == 0) {
                *id = items[i].id;
                return 0;
            }
        }
        return -1;
    }
    for (i = 0; i < nitems; i++) {
        if (items[i].mod == mod && items[i].e->kind == kind
            && strcmp(items[i].e->name, ref) == 0) {
            *id = items[i].id;
            return 0;
        }
    }
    return symb_find(im, (u32)kind, ref, id);
}

static u32 rec_size(BwSdatMKind k)
{
    return k == BW_SDATM_WAVARC ? 4u : 12u;
}

/* ------------------------------------------------------------------ */
/* Player heaps and the sound heap                                     */
/* ------------------------------------------------------------------ */

/*
 * A sound effect plays out of its player's heap: NNSi_SndArcPlayerStartSeq
 * (arm9 nns_snd_arcplayer.c) loads the bank, the bank's wave archives and
 * the sequence into the heap NNS_SndArcPlayerSetup (nns_snd_arc.c) cut for
 * the player out of the sound heap, as large as the PLAYER record says
 * (10200 bytes for players 1, 2, 3 and 5, 2000 for player 4).  When a file
 * does not fit the start fails and the sound is silent.  So a player that
 * plays a mod's sequence gets a record as large as the largest such
 * sequence needs, and the game's own sequences play as before in a larger
 * heap.  A player with no heap (0, the music, and 6, the fanfares) plays
 * what GFL_USE_LoadBgmData loaded into the sound heap itself.
 *
 * What a file costs a heap: its size, the 0x20 bytes NNSi_SndArcLoadFile
 * asks for on top, rounded to 0x20 by NNS_SndHeapAlloc, the 0x20-byte block
 * head that function puts before it and up to 0x20 bytes of alignment in
 * the frame heap (NNS_FndAllocFromFrmHeapEx with alignment 0x20).
 * PLAYER_HEAD holds a player heap's own heads: the sound heap's 0x10, the
 * frame heap's 0x30 and the first level NNSi_SndHeapAllocLevel takes.
 *
 * Everything the merge adds to the sound heap is given back on top of the
 * game's size (bw_sdat_sound_heap_extra): the grown INFO and FAT blocks,
 * which NNS_SndArcInit loads into it, each raised player's extra bytes and
 * a mod's music past the largest the game has on that player.  The room
 * the game leaves for its music stays what it was.
 */
#define FILE_COST(size)  ((((size) + 0x20u + 0x1Fu) & ~0x1Fu) + 0x40u)
#define PLAYER_HEAD      0x100u
#define PLAYER_KIND      4u     /* the PLAYER record table */
#define PLAYER_REC       8u     /* playable count u8, pad, channel mask u16, heap size u32 */
#define PLAYERS_MAX      32u    /* NNS_SndArcPlayerSetup reads players 0..31 */
#define PLAYER_HEAP_HEAD 0x40u  /* NNS_SndPlayerCreateHeap's 0x14 head, rounded, and the block head */

static u32 g_heap_extra;

u32 bw_sdat_sound_heap_extra(void)
{
    return g_heap_extra;
}

/* An original record of `kind`, at least `len` bytes, or NULL. */
static const u8 *base_record(const Image *im, u32 kind, u32 id, u32 len)
{
    u32 off;

    if (kind >= KINDS || im->tab[kind] == 0 || id >= im->count[kind]) {
        return NULL;
    }
    off = rd32(im->d + im->info_off + im->tab[kind] + 4 + id * 4);
    if (off == 0 || (size_t)off + len > im->info_size) {
        return NULL;
    }
    return im->d + im->info_off + off;
}

static u32 base_file_size(const Image *im, u32 fid)
{
    return fid < im->fat_count ? rd32(im->d + im->fat_off + 12 + fid * 16 + 4) : 0;
}

static const Item *added_item(const Item *items, u32 nitems, BwSdatMKind kind, u32 id)
{
    u32 i;

    for (i = 0; i < nitems; i++) {
        if (items[i].e->kind == kind && items[i].id == id) {
            return &items[i];
        }
    }
    return NULL;
}

static u32 wavarc_cost(const Image *im, const Item *items, u32 nitems, u32 id)
{
    const Item *it = added_item(items, nitems, BW_SDATM_WAVARC, id);
    const u8 *rec;

    if (it != NULL) {
        return FILE_COST(it->size);
    }
    rec = base_record(im, BW_SDATM_WAVARC, id, 4);
    return rec != NULL ? FILE_COST(base_file_size(im, rd32(rec) & 0xFFFFFFu)) : 0;
}

/* What a sequence on bank `bank` with a `seq_size`-byte file needs of a
 * player heap. */
static u32 seq_need(const Image *im, const Item *items, u32 nitems, u32 seq_size, u32 bank)
{
    const Item *it = added_item(items, nitems, BW_SDATM_BANK, bank);
    u32 need = PLAYER_HEAD + FILE_COST(seq_size), r;

    if (it != NULL) {
        need += FILE_COST(it->size);
        for (r = 0; r < it->e->nrefs; r++) {
            need += wavarc_cost(im, items, nitems, it->ref_id[r]);
        }
    } else {
        const u8 *rec = base_record(im, BW_SDATM_BANK, bank, 12);

        if (rec != NULL) {
            need += FILE_COST(base_file_size(im, rd32(rec)));
            for (r = 0; r < 4; r++) {
                u32 w = (u32)rec[4 + r * 2] | (u32)rec[5 + r * 2] << 8;
                if (w != 0xFFFFu) {
                    need += wavarc_cost(im, items, nitems, w);
                }
            }
        }
    }
    return need;
}

/* The largest need of the game's own sequences on `player`. */
static u32 base_player_need(const Image *im, u32 player)
{
    u32 i, best = 0;

    for (i = 0; i < im->count[BW_SDATM_SEQ]; i++) {
        const u8 *rec = base_record(im, BW_SDATM_SEQ, i, 12);
        u32 need;

        if (rec == NULL || rec[9] != player) {
            continue;
        }
        need = seq_need(im, NULL, 0, base_file_size(im, rd32(rec)), (u32)rec[4] | (u32)rec[5] << 8);
        if (need > best) {
            best = need;
        }
    }
    return best;
}

int bw_vfs_sdat_merge(const u8 *base, u32 base_size, const BwSdatAdd *adds,
                      u32 count, u8 **out, u32 *out_size)
{
    static const BwSdatMKind order[3] = { BW_SDATM_WAVARC, BW_SDATM_BANK, BW_SDATM_SEQ };
    Image im;
    Item *items = NULL;
    Buf b;
    u32 nitems = 0, n = 0, m, k, o, i, j;
    u32 next_id[KINDS], added[KINDS], next_file;
    u32 info_new, fat_new, symb_new;
    const char *why;
    const char *msg = NULL;
    u32 raise[PLAYERS_MAX], bgm[PLAYERS_MAX], heap_extra = 0;
    int raised = 0;
    char *keys = NULL;     /* each mod's key, BW_SDATM_NAME_MAX bytes a mod */

    *out = NULL;
    *out_size = 0;
    g_heap_extra = 0;
    memset(raise, 0, sizeof(raise));
    memset(bgm, 0, sizeof(bgm));
    memset(&b, 0, sizeof(b));
    if ((why = img_parse(&im, base, base_size)) != NULL) {
        fprintf(stderr, "sdat merge: base archive: %s\n", why);
        return -1;
    }
    for (m = 0; m < count; m++) {
        nitems += adds[m].decl->count;
    }
    keys = (char *)calloc(count + 1, BW_SDATM_NAME_MAX);
    if (keys == NULL) {
        fprintf(stderr, "sdat merge: out of memory\n");
        return -1;
    }
    if (nitems > 0) {
        items = (Item *)calloc(nitems, sizeof(Item));
        if (items == NULL) {
            fprintf(stderr, "sdat merge: out of memory\n");
            free(keys);
            return -1;
        }
    }
    for (k = 0; k < KINDS; k++) {
        next_id[k] = im.count[k];
        added[k] = 0;
    }
    next_file = im.fat_count;

    /* Assign ids, file ids and bytes: per mod, wave archives, banks, then
     * sequences, each in declaration order. */
    for (m = 0; m < count; m++) {
        const BwSdatAdd *a = &adds[m];
        char *key = keys + (size_t)m * BW_SDATM_NAME_MAX;
        char first[BW_SDATM_NAME_MAX];
        u32 dup = 1;

        bw_sdat_mod_key(a->mod_name, first, sizeof(first));
        memcpy(key, first, sizeof(first));
        /* two mods with one key (`Fairy Type` and `Fairy_Type`): the later
         * one's is made unique in load order, `Fairy_Type_2` */
        for (j = 0; j < m; j++) {
            if (strcmp(keys + (size_t)j * BW_SDATM_NAME_MAX, key) == 0) {
                snprintf(key, BW_SDATM_NAME_MAX, "%.56s_%u", first, (unsigned)++dup);
                fprintf(stderr, "sdat merge: mod `%s`: its sounds are named %s:<name> (another mod has the key)\n",
                        a->mod_name != NULL ? a->mod_name : "", key);
                j = (u32)-1;    /* (check the new key against every earlier one) */
            }
        }
        for (o = 0; o < 3; o++) {
            for (i = 0; i < a->decl->count; i++) {
                const BwSdatEntry *e = &a->decl->entries[i];
                Item *it;

                if (e->kind != order[o]) {
                    continue;
                }
                it = &items[n++];
                it->mod = m;
                it->e = e;
                it->id = next_id[e->kind]++;
                it->file_id = next_file++;
                added[e->kind]++;
                snprintf(it->qname, sizeof(it->qname), "%s:%s", key, e->name);
                for (j = 0; j < a->file_count; j++) {
                    if (strcmp(a->files[j].name, e->file) == 0) {
                        it->data = a->files[j].data;
                        it->size = a->files[j].size;
                        break;
                    }
                }
                if (it->data == NULL) {
                    fprintf(stderr, "sdat merge: %s: file %s not supplied\n",
                            it->qname, e->file);
                    goto fail;
                }
                if (it->size < 16 || it->size > FILE_MAX
                    || memcmp(it->data, kind_magic(e->kind), 4) != 0) {
                    fprintf(stderr, "sdat merge: %s: %s is not a %s file (or too big)\n",
                            it->qname, e->file, kind_word(e->kind));
                    goto fail;
                }
            }
        }
    }
    if (next_id[BW_SDATM_BANK] > 0xFFFFu || next_id[BW_SDATM_WAVARC] >= 0xFFFFu
        || next_id[BW_SDATM_SEQ] > 0xFFFFFFu) {
        fprintf(stderr, "sdat merge: too many entries for the 16-bit record fields\n");
        goto fail;
    }

    /* Resolve references. */
    for (i = 0; i < nitems; i++) {
        Item *it = &items[i];
        const BwSdatEntry *e = it->e;
        BwSdatMKind rk;

        if (e->kind == BW_SDATM_WAVARC) {
            continue;
        }
        rk = e->kind == BW_SDATM_BANK ? BW_SDATM_WAVARC : BW_SDATM_BANK;
        for (j = 0; j < e->nrefs; j++) {
            if (resolve_ref(&im, items, nitems, rk, it->mod, e->refs[j],
                            next_id[rk], &it->ref_id[j]) != 0) {
                fprintf(stderr, "sdat merge: %s: cannot resolve %s `%s`\n",
                        it->qname, kind_word(rk), e->refs[j]);
                goto fail;
            }
        }
    }

    /* The players' heaps (see bw_sdat_sound_heap_extra). */
    for (i = 0; i < nitems; i++) {
        const Item *it = &items[i];
        const u8 *rec;
        u32 p = it->e->player, need;

        if (it->e->kind != BW_SDATM_SEQ || p >= PLAYERS_MAX
            || (rec = base_record(&im, PLAYER_KIND, p, PLAYER_REC)) == NULL) {
            continue;
        }
        need = seq_need(&im, items, nitems, it->size, it->ref_id[0]);
        if (rd32(rec + 4) == 0) {
            bgm[p] = need > bgm[p] ? need : bgm[p];
        } else if (need > rd32(rec + 4) && need > raise[p]) {
            raise[p] = need;
            raised = 1;
        }
    }
    for (k = 0; k < PLAYERS_MAX; k++) {
        if (bgm[k] != 0) {
            u32 game = base_player_need(&im, k);
            heap_extra += bgm[k] > game ? bgm[k] - game : 0;
        }
    }

    /* Emit.  Start with every original byte. */
    buf_put(&b, base, base_size);
    if (nitems == 0) {
        goto done;
    }

    /* 1. New file data. */
    for (i = 0; i < nitems; i++) {
        buf_align(&b, ALIGN_FILE);
        items[i].file_off = b.len;
        buf_put(&b, items[i].data, items[i].size);
    }

    /* 2. INFO: the old block, then new tables and records. */
    buf_align(&b, ALIGN_FILE);
    info_new = b.len;
    buf_put(&b, base + im.info_off, im.info_size);
    for (o = 0; o < 3; o++) {
        BwSdatMKind kind = order[o];
        u32 tab_rel, slot0;

        if (added[kind] == 0) {
            continue;
        }
        buf_align(&b, 4);
        tab_rel = b.len - info_new;
        buf_put32(&b, im.count[kind] + added[kind]);
        for (i = 0; i < im.count[kind]; i++) {
            buf_put32(&b, rd32(base + im.info_off + im.tab[kind] + 4 + i * 4));
        }
        slot0 = b.len;
        buf_zero(&b, added[kind] * 4);
        buf_set32(&b, info_new + 8 + (u32)kind * 4, tab_rel);
        for (i = 0; i < nitems; i++) {
            const Item *it = &items[i];
            u8 rec[12];
            u32 r;

            if (it->e->kind != kind) {
                continue;
            }
            memset(rec, 0, sizeof(rec));
            wr32(rec, it->file_id);
            if (kind == BW_SDATM_SEQ) {
                rec[4] = (u8)it->ref_id[0];
                rec[5] = (u8)(it->ref_id[0] >> 8);
                rec[6] = (u8)it->e->volume;
                rec[7] = (u8)it->e->cpr;
                rec[8] = (u8)it->e->ppr;
                rec[9] = (u8)it->e->player;
            } else if (kind == BW_SDATM_BANK) {
                for (r = 0; r < 4; r++) {
                    u32 v = r < it->e->nrefs ? it->ref_id[r] : 0xFFFFu;
                    rec[4 + r * 2] = (u8)v;
                    rec[5 + r * 2] = (u8)(v >> 8);
                }
            }
            buf_align(&b, 4);
            buf_set32(&b, slot0 + (it->id - im.count[kind]) * 4, b.len - info_new);
            buf_put(&b, rec, rec_size(kind));
        }
    }
    if (raised) {
        /* A copy of the PLAYER table, each raised player on a new record. */
        u32 tab_rel, slot0;

        buf_align(&b, 4);
        tab_rel = b.len - info_new;
        buf_put32(&b, im.count[PLAYER_KIND]);
        slot0 = b.len;
        for (i = 0; i < im.count[PLAYER_KIND]; i++) {
            buf_put32(&b, rd32(base + im.info_off + im.tab[PLAYER_KIND] + 4 + i * 4));
        }
        buf_set32(&b, info_new + 8 + PLAYER_KIND * 4, tab_rel);
        for (k = 0; k < PLAYERS_MAX; k++) {
            const u8 *old;
            u8 rec[PLAYER_REC];

            if (raise[k] == 0) {
                continue;
            }
            old = base_record(&im, PLAYER_KIND, k, PLAYER_REC);
            memcpy(rec, old, PLAYER_REC);
            wr32(rec + 4, raise[k]);
            heap_extra += (raise[k] - rd32(old + 4) + PLAYER_HEAP_HEAD) * (old[0] != 0 ? old[0] : 1u);
            buf_set32(&b, slot0 + k * 4, b.len - info_new);
            buf_put(&b, rec, PLAYER_REC);
        }
    }
    buf_align(&b, 4);
    buf_set32(&b, info_new + 4, b.len - info_new);

    /* 3. FAT: every old entry, then the new ones. */
    buf_align(&b, ALIGN_FILE);
    fat_new = b.len;
    buf_put(&b, "FAT ", 4);
    buf_put32(&b, 12 + (im.fat_count + nitems) * 16);
    buf_put32(&b, im.fat_count + nitems);
    buf_put(&b, base + im.fat_off + 12, im.fat_count * 16);
    for (i = 0; i < nitems; i++) {
        buf_put32(&b, items[i].file_off);
        buf_put32(&b, items[i].size);
        buf_put32(&b, 0);
        buf_put32(&b, 0);
    }

    /* 4. SYMB: the old block (or an empty one), then new name tables. */
    buf_align(&b, ALIGN_FILE);
    symb_new = b.len;
    if (im.has_symb) {
        buf_put(&b, base + im.symb_off, im.symb_size);
    } else {
        buf_put(&b, "SYMB", 4);
        buf_zero(&b, 0x24);
    }
    for (o = 0; o < 3; o++) {
        BwSdatMKind kind = order[o];
        u32 tab_rel, slot0;

        if (added[kind] == 0) {
            continue;
        }
        buf_align(&b, 4);
        tab_rel = b.len - symb_new;
        buf_put32(&b, im.count[kind] + added[kind]);
        for (i = 0; i < im.count[kind]; i++) {
            buf_put32(&b, im.stab[kind] != 0
                              ? rd32(base + im.symb_off + im.stab[kind] + 4 + i * 4)
                              : 0);
        }
        slot0 = b.len;
        buf_zero(&b, added[kind] * 4);
        buf_set32(&b, symb_new + 8 + (u32)kind * 4, tab_rel);
        for (i = 0; i < nitems; i++) {
            const Item *it = &items[i];

            if (it->e->kind != kind) {
                continue;
            }
            buf_set32(&b, slot0 + (it->id - im.count[kind]) * 4, b.len - symb_new);
            buf_put(&b, it->qname, (u32)strlen(it->qname) + 1);
        }
    }
    buf_align(&b, 4);
    buf_set32(&b, symb_new + 4, b.len - symb_new);

    /* 5. The header. */
    buf_set32(&b, 0x08, b.len);
    buf_set32(&b, 0x10, symb_new);
    buf_set32(&b, 0x18, info_new);
    buf_set32(&b, 0x1C, rd32(b.p + info_new + 4));
    buf_set32(&b, 0x20, fat_new);
    buf_set32(&b, 0x24, rd32(b.p + fat_new + 4));
    buf_set32(&b, 0x14, rd32(b.p + symb_new + 4));

    /* The sound heap holds INFO and FAT (NNS_SndArcInit): their growth. */
    heap_extra += FILE_COST(rd32(b.p + info_new + 4) - im.info_size)
                + FILE_COST(rd32(b.p + fat_new + 4) - im.fat_size);

done:
    if (b.failed) {
        msg = "out of memory or image over the size limit";
        goto fail;
    }
    g_heap_extra = heap_extra;
    free(items);
    free(keys);
    *out = b.p;
    *out_size = b.len;
    return 0;

fail:
    if (msg != NULL) {
        fprintf(stderr, "sdat merge: %s\n", msg);
    }
    free(items);
    free(keys);
    free(b.p);
    return -1;
}
