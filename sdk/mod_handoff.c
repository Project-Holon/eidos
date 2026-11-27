/*
 * sdk/mod_handoff.c -- the save handoff: a save passed from one player's
 * eidos to another's (docs/mod-services.md, "Save handoff"; docs/mod-modes.md,
 * section 3, "Save handoff"; docs/modding.md, section 3).
 *
 * What this file is
 * -----------------
 *
 * One of the port's services, and the two halves of it:
 *
 *   - the sending half: the current game save (the port's own file, the
 *     backup chip's image under the save folder, sdk/arm7/fs.c) and the
 *     mod's own saved data (the mod's payload, sdk/mod_savedata.c) are
 *     packed into one blob and carried to the peer through the peer session
 *     (sdk/mod_net.c);
 *   - the receiving half: the blob is put back together, the port's own
 *     confirmation is asked for, and the save is installed: the game save
 *     written, and the mod's own data written beside it with the checksum
 *     of the save that was installed.
 *
 * The rules it keeps
 * ------------------
 *
 * (docs/mod-services.md, "Save handoff"; the words are the document's)
 *
 *   - "Sends the current game save and the mod's saved data to another
 *     player in the session."
 *   - "The receiver's port shows a confirmation, then installs the save and
 *     reloads."
 *   - "A Taglocke passes the run from player to player this way."
 *
 * Why the bytes travel in pieces
 * ------------------------------
 *
 * A message of the peer session is up to 4 KB of the sender's own bytes
 * (sdk/mod_net.h, BW_MOD_NET_MESSAGE_MAX) and the game's save is half a
 * megabyte, so the blob is cut into pieces of the size a message may carry
 * and put back together on the other side.  Every piece carries its own
 * number and the length of the whole, so a piece that never arrives is
 * seen: the pieces that follow it do not fit and the save is dropped.
 *
 * The pieces go out over the frames, not in one burst: the peer session
 * keeps what it read in a queue of 64 KB and drops the oldest records when
 * it is full (sdk/mod_net.c), and a whole save in one burst would be
 * dropped by the receiving side before it was ever seen.  The port's frame
 * tick (bw_mod_handoff_frame) sends a few pieces a frame, so the queue
 * stays shallow and nothing is dropped.  A port that does not drive the
 * frame tick gets the whole save in one call, which is enough for a save
 * that fits the queue.
 *
 * Who is the port and who is the mod
 * ----------------------------------
 *
 * The handoff is the port's own traffic: the game save belongs to the port
 * and the port installs it, so the pieces travel under the port's own key
 * (the empty namespace) and the port's own session handler takes them in.
 * The mod's own messages keep their own key and reach the mod's own
 * session handler, the way the peer sessions work (sdk/mod_net.c, "Who is
 * calling").  The port's handler is put in place by bw_mod_handoff_bind.
 *
 * The mod's own data
 * ------------------
 *
 * What travels is the mod's payload: the bytes after the port's own header
 * (sdk/mod_state.h).  The header is the port's, and it is written here on
 * the other side, with the checksum of the save that was installed, so the
 * mod's file is in step with the save it belongs to (docs/mod-state.md,
 * section 2).
 *
 * The name that travels with the blob is the name of the mod that sent it,
 * and the name is the port's own namespace (the loader puts a mod's
 * namespace in force around every call it makes into that mod).  The name
 * comes from the network, so it is never used to build a path here: the
 * saved-data layer is the one that checks it (bw_mod_savedata_set_namespace
 * refuses a name that is not one path component).
 *
 * The checksum of the save in force
 * --------------------------------
 *
 * One source: the saved-data layer's rule (sdk/mod_savedata.c), which is
 * the CRC-32 of the save's bytes as the chip holds them.  The chip's image
 * (sdk/arm7/fs.c) is the save file's first <chip size> bytes with the rest
 * of the chip erased (0xFF), so the rule gives the same number for the
 * chip's image and for the file it came from.
 *
 * The handoff cannot read the chip's image after it installs a save: it
 * drops the image, so the game loads the file that was written and not the
 * image it had.  It therefore gives the layer the bytes it wrote and the
 * chip's size, and the layer takes their checksum the one way.  The answer
 * the layer reads from the chip after the reload is the same number, so a
 * mod's file cannot be out of step with the save in force because of a file
 * with a trailer or one shorter than the chip.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "types.h"
#include "mod_handoff.h"
#include "mod_net.h"
#include "mod_state.h"
#include "save.h"

/* ------------------------------------------------------------------ */
/* What the other layers provide                                       */
/* ------------------------------------------------------------------ */

/*
 * The saved-data layer (sdk/mod_savedata.c): the mod that is calling, the
 * mod's own payload, and the checksum the port and the layer share.  That
 * file holds its prototypes until the header which will carry them for the
 * rest of the port is written, so they are declared here.  They are weak,
 * so a link without the layer still succeeds: the handoff then carries the
 * game save alone (sdk/mod_frame.c does the same).
 */
#if defined(__GNUC__) || defined(__clang__)
#define BW_PORT_WEAK __attribute__((weak))
#else
#define BW_PORT_WEAK
#endif

extern const char *bw_mod_savedata_namespace(void) BW_PORT_WEAK;
extern void        bw_mod_savedata_set_namespace(const char *ns) BW_PORT_WEAK;
extern void        bw_mod_savedata_set_save_check(u32 check) BW_PORT_WEAK;
extern void        bw_mod_savedata_set_save_bytes(const void *save, u32 len,
                                                  u32 chip_size) BW_PORT_WEAK;
extern s32         bw_mod_savedata_load(void *buf, u32 size) BW_PORT_WEAK;
extern int         bw_mod_savedata_store(const void *buf, u32 size) BW_PORT_WEAK;
extern int         bw_mod_savedata_info(BwModStateHeader *out) BW_PORT_WEAK;
extern u32         bw_mod_savedata_crc(const void *data, u32 size) BW_PORT_WEAK;

/*
 * The backup chip (sdk/arm7/fs.c): the game's save in the port's own file.
 * The chip's image is dropped before the file is written, so the game
 * loads the file that was written and not the image it had.  Weak, for the
 * same reason: a link without the ARM7 layer still works, and the file is
 * then the whole save.
 */
extern void bw_arm7_backup_set_path(const char *path) BW_PORT_WEAK;
extern int  bw_arm7_backup_flush(void) BW_PORT_WEAK;
extern const u8 *bw_arm7_backup_image(u32 *size) BW_PORT_WEAK;

/* ------------------------------------------------------------------ */
/* The limits                                                          */
/* ------------------------------------------------------------------ */

/* The game's save: the file under the save folder (sdk/arm7/fs.c). */
#define BW_MOD_HANDOFF_SAVE_NAME "pokeblack.sav"

/* The chip's size: Pokemon Black's flash chip is 512 KB. */
#define BW_MOD_HANDOFF_CHIP_MAX (512u * 1024u)

/* The longest path the layer builds. */
#define BW_MOD_HANDOFF_PATH_MAX 1200

/* The longest namespace the layer keeps (the saved-data layer's own limit
 * is 64; the loader's is 96). */
#define BW_MOD_HANDOFF_NS_MAX 96

/* How many mods may hold a save handler at once (the loader holds
 * sixteen mods with code). */
#define BW_MOD_HANDOFF_MODS_MAX 16

/* How many peers may be sending a save at once (the peer session's own
 * limit). */
#define BW_MOD_HANDOFF_PEERS_MAX 16

/* The size of one piece: the head, and the blob's bytes that follow it.
 * A message of the peer session is 4 KB at most. */
#define BW_MOD_HANDOFF_PIECE_DATA \
    (BW_MOD_NET_MESSAGE_MAX - BW_MOD_HANDOFF_PIECE_HEAD)

/*
 * How many pieces go out in one frame.  The peer session's queue holds
 * 64 KB of records and the frame drains it, so a few pieces a frame keep
 * the queue shallow and nothing is dropped.
 */
#define BW_MOD_HANDOFF_PIECES_PER_FRAME 4u

/* ------------------------------------------------------------------ */
/* The format                                                          */
/* ------------------------------------------------------------------ */

/* The magics: the blob's own head, and the head of every piece. */
#define BW_MOD_HANDOFF_BLOB_MAGIC  "EIDB"
#define BW_MOD_HANDOFF_PIECE_MAGIC "EIDC"

/* The blob's head: the magic, the version, the flags, the length of the
 * name, the length of the game save, the length of the mod's own payload,
 * the checksum of each, and the length of the whole blob. */
#define BW_MOD_HANDOFF_BLOB_HEAD 32u

/* The offsets in the blob's head. */
#define BW_HANDOFF_BLOB_VERSION 4u
#define BW_HANDOFF_BLOB_FLAGS   6u
#define BW_HANDOFF_BLOB_NS      8u
#define BW_HANDOFF_BLOB_SAVE   12u
#define BW_HANDOFF_BLOB_STATE  16u
#define BW_HANDOFF_BLOB_SAVECRC 20u
#define BW_HANDOFF_BLOB_STCRC  24u
#define BW_HANDOFF_BLOB_TOTAL  28u

/* The offsets in a piece's head. */
#define BW_HANDOFF_PIECE_VERSION 4u
#define BW_HANDOFF_PIECE_FLAGS   6u
#define BW_HANDOFF_PIECE_SEQ     8u
#define BW_HANDOFF_PIECE_COUNT  12u
#define BW_HANDOFF_PIECE_TOTAL  16u

/* ------------------------------------------------------------------ */
/* The state                                                           */
/* ------------------------------------------------------------------ */

/*
 * One mod's own handler: the mod that is told when a save of its own name
 * arrives.  The key is the mod's namespace, the way the peer session keys
 * its own handlers (sdk/mod_net.c).
 */
typedef struct BwHandoffMod {
    char          ns[BW_MOD_HANDOFF_NS_MAX + 1];
    BwSaveHandler fn;
    int           used;
} BwHandoffMod;

/*
 * One save that is arriving: the pieces of one blob, from one peer.  The
 * buffer holds the whole blob, so the pieces can be put back together in
 * the order they arrive.
 */
typedef struct BwHandoffStream {
    int  used;
    u32  peer;
    u8  *buf;
    u32  len;        /* the bytes in the buffer                       */
    u32  cap;        /* the bytes the buffer holds (the whole blob)   */
    u32  next_seq;   /* the piece the stream expects                 */
    u32  count;      /* the pieces in the whole                       */
} BwHandoffStream;

/*
 * One save that is on its way: the blob and the peer it goes to.  The
 * pieces go out over the frames, so the blob is kept until the last piece
 * is sent.
 */
typedef struct BwHandoffSend {
    int  used;
    u32  peer;
    u8  *blob;
    u32  blob_len;
    u32  seq;        /* the piece that goes next                     */
    u32  count;      /* the pieces in the whole                     */
} BwHandoffSend;

static BwHandoffMod     g_mods[BW_MOD_HANDOFF_MODS_MAX];
static BwHandoffStream  g_streams[BW_MOD_HANDOFF_PEERS_MAX];
static BwHandoffSend    g_send;

static BwHandoffConfirmFn g_confirm;
static void              *g_confirm_user;
static BwHandoffReloadFn  g_reload;

/* A whole save whose question is still on its way to the player: the blob
 * and where its parts are, kept until bw_mod_handoff_answer. */
typedef struct BwHandoffWaiting {
    u32 peer;
    u8 *blob;
    u32 ns_len;
    u32 save_len;
    u32 state_len;
} BwHandoffWaiting;

static BwHandoffWaiting g_waiting;

/* The port's own key (the empty namespace): the handoff's pieces travel
 * under it, and the port's session handler takes them in. */
#define BW_MOD_HANDOFF_PORT_KEY ""

/* The port drives the frames: the pieces go out over them. */
static int g_frame_seen;

/* The port's session handler is in place.  The reset empties the peer
 * session's table of handlers (bw_mod_net_reset), so the binding is made
 * again at the first frame after a reset. */
static int g_bound;

/* ------------------------------------------------------------------ */
/* The small helpers                                                   */
/* ------------------------------------------------------------------ */

/* Copy `src` into `dst`, cutting it at the buffer's end. */
static void copy_text(char *dst, size_t size, const char *src)
{
    size_t n;

    if (size == 0) {
        return;
    }
    if (src == NULL) {
        dst[0] = '\0';
        return;
    }
    n = strlen(src);
    if (n >= size) {
        n = size - 1;
    }
    memcpy(dst, src, n);
    dst[n] = '\0';
}

/* The namespace of the mod that is calling, or "" when there is none. */
static const char *current_namespace(void)
{
    const char *ns;

    if (bw_mod_savedata_namespace == NULL) {
        return "";
    }
    ns = bw_mod_savedata_namespace();
    return (ns != NULL) ? ns : "";
}

/*
 * The checksum: the CRC-32 every host library computes (the reflected IEEE
 * polynomial 0xEDB88320).  The saved-data layer owns one (its
 * bw_mod_savedata_crc) and the layer uses it when it is there, so the
 * checksum of the game save in a handoff is the checksum the port and the
 * saved-data layer share.  The copy here is the same algorithm, for a link
 * without that layer.
 */
static u32 handoff_crc(const void *data, u32 size)
{
    const u8 *p = (const u8 *)data;
    u32 crc = 0xFFFFFFFFu;
    u32 i;

    if (p == NULL) {
        return 0;
    }
    if (bw_mod_savedata_crc != NULL) {
        return bw_mod_savedata_crc(data, size);
    }
    for (i = 0; i < size; i++) {
        u32 c = (crc ^ p[i]) & 0xFFu;
        u32 k;

        for (k = 0; k < 8u; k++) {
            c = (c & 1u) != 0u ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        }
        crc = c ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFFu;
}

/* ------------------------------------------------------------------ */
/* The mods                                                            */
/* ------------------------------------------------------------------ */

/* The mod with the namespace `ns`, made when it is not there yet. */
static BwHandoffMod *mod_for(const char *ns)
{
    u32 i;

    if (ns == NULL) {
        ns = "";
    }
    for (i = 0; i < BW_MOD_HANDOFF_MODS_MAX; i++) {
        if (g_mods[i].used && strcmp(g_mods[i].ns, ns) == 0) {
            return &g_mods[i];
        }
    }
    for (i = 0; i < BW_MOD_HANDOFF_MODS_MAX; i++) {
        if (g_mods[i].used) {
            continue;
        }
        memset(&g_mods[i], 0, sizeof(g_mods[i]));
        copy_text(g_mods[i].ns, sizeof(g_mods[i].ns), ns);
        g_mods[i].used = 1;
        return &g_mods[i];
    }
    return NULL;
}

/* The mod with the namespace `ns`, or NULL when there is none. */
static BwHandoffMod *mod_lookup(const char *ns)
{
    u32 i;

    if (ns == NULL) {
        return NULL;
    }
    for (i = 0; i < BW_MOD_HANDOFF_MODS_MAX; i++) {
        if (g_mods[i].used && strcmp(g_mods[i].ns, ns) == 0) {
            return &g_mods[i];
        }
    }
    return NULL;
}

/* ------------------------------------------------------------------ */
/* The streams                                                         */
/* ------------------------------------------------------------------ */

/* The stream of the peer `peer`, or NULL when there is none. */
static BwHandoffStream *stream_find(u32 peer)
{
    u32 i;

    for (i = 0; i < BW_MOD_HANDOFF_PEERS_MAX; i++) {
        if (g_streams[i].used && g_streams[i].peer == peer) {
            return &g_streams[i];
        }
    }
    return NULL;
}

/* The stream of the peer `peer`, made when there is none. */
static BwHandoffStream *stream_get(u32 peer)
{
    BwHandoffStream *st = stream_find(peer);
    u32 i;

    if (st != NULL) {
        return st;
    }
    for (i = 0; i < BW_MOD_HANDOFF_PEERS_MAX; i++) {
        if (!g_streams[i].used) {
            st = &g_streams[i];
            memset(st, 0, sizeof(*st));
            st->used = 1;
            st->peer = peer;
            return st;
        }
    }
    return NULL;
}

/* The stream of the peer `peer` goes. */
static void stream_drop(BwHandoffStream *st)
{
    if (st == NULL) {
        return;
    }
    free(st->buf);
    memset(st, 0, sizeof(*st));
}

/* ------------------------------------------------------------------ */
/* The sending half                                                    */
/* ------------------------------------------------------------------ */

/*
 * The game save: the port's own file, the backup chip's image.  The chip
 * is flushed first, so the file is the save the game has, and the file is
 * cut at the chip's size: a file longer than the chip (a DeSmuME trailer)
 * holds more than the game ever saw.
 *
 * A file that is not there is a save that is empty: the game reads an
 * erased chip as "no save", and so does the peer.
 */
static int read_game_save(u8 **out, u32 *out_len)
{
    char path[BW_MOD_HANDOFF_PATH_MAX];
    u8 *buf;
    u32 cap;
    long n;

    *out = NULL;
    *out_len = 0;
    if (bw_arm7_backup_flush != NULL) {
        (void)bw_arm7_backup_flush();
    }
    if (bw_save_path(path, sizeof path, BW_MOD_HANDOFF_SAVE_NAME) != 0) {
        fprintf(stderr, "eidos: handoff: no save folder; there is no save to "
                        "send\n");
        return -1;
    }
    cap = BW_MOD_HANDOFF_CHIP_MAX;
    if (bw_arm7_backup_image != NULL) {
        u32 chip = 0;

        (void)bw_arm7_backup_image(&chip);
        if (chip > 0u && chip < cap) {
            cap = chip;
        }
    }
    buf = (u8 *)malloc(cap > 0u ? (size_t)cap : 1u);
    if (buf == NULL) {
        fprintf(stderr, "eidos: handoff: the save could not be read (out of "
                        "memory)\n");
        return -1;
    }
    n = bw_save_read(path, buf, cap);
    if (n < 0) {
        /* there is no save file: the save that goes is empty */
        free(buf);
        return 0;
    }
    *out = buf;
    *out_len = ((u32)n < cap) ? (u32)n : cap;
    return 0;
}

/*
 * The mod's own saved data: the payload of the mod that is calling, the
 * bytes after the port's own header.  A mod that has no state file yet has
 * no data to send, and that is not a failure.
 */
static int read_mod_data(u8 **out, u32 *out_len)
{
    BwModStateHeader h;
    u8 *buf;
    s32 got;

    *out = NULL;
    *out_len = 0;
    if (bw_mod_savedata_info == NULL || bw_mod_savedata_load == NULL) {
        return 0;
    }
    if (bw_mod_savedata_info(&h) != 0) {
        return 0;                   /* no state file: no data yet */
    }
    if (h.payload_size == 0u) {
        return 0;
    }
    if (h.payload_size > BW_MOD_HANDOFF_BLOB_MAX) {
        fprintf(stderr, "eidos: handoff: the mod's own data is too large "
                        "(%u bytes); it was not sent\n",
                (unsigned int)h.payload_size);
        return -1;
    }
    buf = (u8 *)malloc(h.payload_size);
    if (buf == NULL) {
        fprintf(stderr, "eidos: handoff: the mod's own data could not be "
                        "read (out of memory)\n");
        return -1;
    }
    got = bw_mod_savedata_load(buf, h.payload_size);
    if (got < 0) {
        free(buf);
        fprintf(stderr, "eidos: handoff: the mod's own data could not be "
                        "read\n");
        return -1;
    }
    *out = buf;
    *out_len = (u32)got;
    return 0;
}

/*
 * The blob: the head, the name of the mod that sends, the game save and
 * the mod's own payload.  The two checksums are the port's own CRC-32, so
 * a blob that was damaged on the way is seen and dropped.
 */
static u8 *build_blob(const char *ns, const u8 *save, u32 save_len,
                      const u8 *state, u32 state_len, u32 *out_len)
{
    u32 ns_len = (u32)strlen(ns);
    u32 total = BW_MOD_HANDOFF_BLOB_HEAD + ns_len + save_len + state_len;
    u8 *blob;

    if (total > BW_MOD_HANDOFF_BLOB_MAX) {
        fprintf(stderr, "eidos: handoff: the save is too large (%u bytes); it "
                        "was not sent\n", (unsigned int)total);
        return NULL;
    }
    blob = (u8 *)malloc(total > 0u ? (size_t)total : 1u);
    if (blob == NULL) {
        fprintf(stderr, "eidos: handoff: the save could not be packed (out "
                        "of memory)\n");
        return NULL;
    }
    memset(blob, 0, total);
    memcpy(blob, BW_MOD_HANDOFF_BLOB_MAGIC, 4);
    bw_wr16(blob + BW_HANDOFF_BLOB_VERSION, (u16)BW_MOD_HANDOFF_VERSION);
    bw_wr16(blob + BW_HANDOFF_BLOB_FLAGS, 0);
    bw_wr32(blob + BW_HANDOFF_BLOB_NS, ns_len);
    bw_wr32(blob + BW_HANDOFF_BLOB_SAVE, save_len);
    bw_wr32(blob + BW_HANDOFF_BLOB_STATE, state_len);
    bw_wr32(blob + BW_HANDOFF_BLOB_SAVECRC, handoff_crc(save, save_len));
    bw_wr32(blob + BW_HANDOFF_BLOB_STCRC, handoff_crc(state, state_len));
    bw_wr32(blob + BW_HANDOFF_BLOB_TOTAL, total);
    if (ns_len > 0u) {
        memcpy(blob + BW_MOD_HANDOFF_BLOB_HEAD, ns, ns_len);
    }
    if (save_len > 0u) {
        memcpy(blob + BW_MOD_HANDOFF_BLOB_HEAD + ns_len, save, save_len);
    }
    if (state_len > 0u) {
        memcpy(blob + BW_MOD_HANDOFF_BLOB_HEAD + ns_len + save_len, state,
               state_len);
    }
    *out_len = total;
    return blob;
}

/*
 * The send, under the port's own key: the game save belongs to the port,
 * so the pieces travel as the port's own traffic and the port's own
 * session handler takes them in on the other side.  The mod's own messages
 * keep their own key and reach the mod's own handler.
 */
static int send_as_port(u32 peer, const void *buf, u32 size)
{
    char saved[BW_MOD_HANDOFF_NS_MAX + 1];
    int rc;

    copy_text(saved, sizeof(saved), current_namespace());
    if (bw_mod_savedata_set_namespace != NULL) {
        bw_mod_savedata_set_namespace("");
    }
    rc = bw_mod_net_session_send(peer, buf, size);
    if (bw_mod_savedata_set_namespace != NULL) {
        bw_mod_savedata_set_namespace(saved);
    }
    return rc;
}

/*
 * The pieces of a blob: at most `max` of them, starting at `seq`.  The
 * number of pieces sent, or -1 when the connection failed; `err` then
 * holds the peer session's own answer.
 */
static s32 send_pieces(u32 peer, const u8 *blob, u32 blob_len, u32 seq,
                        u32 count, u32 max, int *err)
{
    u8 piece[BW_MOD_NET_MESSAGE_MAX];
    u32 sent = 0;

    if (err != NULL) {
        *err = BW_MOD_NET_OK;
    }
    while (sent < max && seq < count) {
        u32 off = seq * BW_MOD_HANDOFF_PIECE_DATA;
        u32 len = blob_len - off;
        int rc;

        if (len > BW_MOD_HANDOFF_PIECE_DATA) {
            len = BW_MOD_HANDOFF_PIECE_DATA;
        }
        memset(piece, 0, sizeof(piece));
        memcpy(piece, BW_MOD_HANDOFF_PIECE_MAGIC, 4);
        bw_wr16(piece + BW_HANDOFF_PIECE_VERSION,
                (u16)BW_MOD_HANDOFF_VERSION);
        bw_wr16(piece + BW_HANDOFF_PIECE_FLAGS, 0);
        bw_wr32(piece + BW_HANDOFF_PIECE_SEQ, seq);
        bw_wr32(piece + BW_HANDOFF_PIECE_COUNT, count);
        bw_wr32(piece + BW_HANDOFF_PIECE_TOTAL, blob_len);
        if (len > 0u) {
            memcpy(piece + BW_MOD_HANDOFF_PIECE_HEAD, blob + off, len);
        }
        rc = send_as_port(peer, piece, BW_MOD_HANDOFF_PIECE_HEAD + len);
        if (rc != BW_MOD_NET_OK) {
            fprintf(stderr, "eidos: handoff: the save could not be sent to "
                            "peer %u; the transfer stopped\n",
                    (unsigned int)peer);
            if (err != NULL) {
                *err = rc;
            }
            return -1;
        }
        seq++;
        sent++;
    }
    return (s32)sent;
}

/*
 * The pieces of the transfer that is on its way: at most `max` of them.
 * The transfer goes when the last piece is out.
 */
static int send_transfer(u32 max, int *err)
{
    s32 sent;

    if (err != NULL) {
        *err = BW_MOD_NET_OK;
    }
    if (!g_send.used) {
        return 0;
    }
    sent = send_pieces(g_send.peer, g_send.blob, g_send.blob_len, g_send.seq,
                       g_send.count, max, err);
    if (sent < 0) {
        free(g_send.blob);
        memset(&g_send, 0, sizeof(g_send));
        return -1;
    }
    g_send.seq += (u32)sent;
    if (g_send.seq >= g_send.count) {
        free(g_send.blob);
        memset(&g_send, 0, sizeof(g_send));
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* The receiving half                                                  */
/* ------------------------------------------------------------------ */

/*
 * The chip's size: the size of the chip's image, or the port's own chip
 * size when the chip has not been identified yet.  The save that is
 * installed becomes the chip's image, so the chip's size is what the
 * save's bytes are cut at and padded to (sdk/mod_savedata.c takes the
 * checksum of a save the one way).
 */
static u32 chip_size(void)
{
    u32 size = 0;

    if (bw_arm7_backup_image != NULL) {
        (void)bw_arm7_backup_image(&size);
    }
    if (size == 0u) {
        size = BW_MOD_HANDOFF_CHIP_MAX;
    }
    return size;
}

/*
 * The save is installed: the game save is written and the mod's own data
 * is written beside it, with the checksum of the save that was installed
 * (docs/mod-state.md, section 2).  The mod's own handler is told, and then
 * the port is asked to reload.
 */
static void install_save(u32 peer, const char *name, const u8 *save,
                          u32 save_len, const u8 *state, u32 state_len)
{
    char path[BW_MOD_HANDOFF_PATH_MAX];
    char saved_ns[BW_MOD_HANDOFF_NS_MAX + 1];
    BwSaveHandler fn;
    u32 chip;

    /*
     * The chip's size is taken before the image goes: the save that is
     * installed is the chip's image, and the checksum of the save in force
     * is the checksum of the chip's bytes.
     */
    chip = chip_size();

    /*
     * The chip's own image goes first: the game must load the file that is
     * written below and not the image it had, or the old save would come
     * back with the next IDENTIFY.
     */
    if (bw_arm7_backup_set_path != NULL) {
        (void)bw_arm7_backup_set_path(NULL);
    }
    if (bw_save_path(path, sizeof path, BW_MOD_HANDOFF_SAVE_NAME) != 0) {
        fprintf(stderr, "eidos: handoff: no save folder; the save from peer "
                        "%u was not installed\n", (unsigned int)peer);
        return;
    }
    if (bw_save_write_atomic(path, save, save_len) != 0) {
        fprintf(stderr, "eidos: handoff: the save from peer %u could not be "
                        "written; the old one is kept\n", (unsigned int)peer);
        return;
    }
    /*
     * The mod's own data, with the checksum of the save now in force.  The
     * saved-data layer takes that checksum the one way it takes every
     * checksum: the save's bytes as the chip holds them, so the layer's
     * answer and this one are one answer (a file with a trailer, or one
     * shorter than the chip, cannot make the two disagree).  A link without
     * the layer falls back on the handoff's own checksum of the bytes.
     */
    if (bw_mod_savedata_set_save_bytes != NULL) {
        bw_mod_savedata_set_save_bytes(save, save_len, chip);
    } else if (bw_mod_savedata_set_save_check != NULL) {
        bw_mod_savedata_set_save_check(handoff_crc(save, save_len));
    }
    copy_text(saved_ns, sizeof(saved_ns), current_namespace());
    if (bw_mod_savedata_set_namespace != NULL) {
        bw_mod_savedata_set_namespace(name);
    }
    if (bw_mod_savedata_store != NULL) {
        if (bw_mod_savedata_store(state, state_len) != 0) {
            fprintf(stderr, "eidos: handoff: the data of mod %s could not be "
                            "written\n", name);
        }
    }
    /* The mod's own handler, with its own namespace in force. */
    fn = NULL;
    {
        BwHandoffMod *m = mod_lookup(name);

        if (m != NULL) {
            fn = m->fn;
        }
    }
    if (fn != NULL) {
        fn(peer, save, save_len);
    }
    if (bw_mod_savedata_set_namespace != NULL) {
        bw_mod_savedata_set_namespace(saved_ns);
    }
    /* The port goes on with the save. */
    if (g_reload != NULL) {
        g_reload();
    }
}

/* The blob is whole: it is checked, the player is asked, and the save is
 * installed if the player takes it. */
static void finish_save(u32 peer, BwHandoffStream *st)
{
    const u8 *blob = st->buf;
    u32 blob_len = st->len;
    u32 ns_len;
    u32 save_len;
    u32 state_len;
    int answer;
    u32 save_crc;
    u32 state_crc;
    char name[BW_MOD_HANDOFF_NS_MAX + 1];
    const u8 *save;
    const u8 *state;

    if (blob_len < BW_MOD_HANDOFF_BLOB_HEAD ||
        memcmp(blob, BW_MOD_HANDOFF_BLOB_MAGIC, 4) != 0) {
        fprintf(stderr, "eidos: handoff: the save from peer %u is not a save "
                        "this port knows; it was dropped\n",
                (unsigned int)peer);
        return;
    }
    if (bw_rd16(blob + BW_HANDOFF_BLOB_VERSION) !=
        (u16)BW_MOD_HANDOFF_VERSION) {
        fprintf(stderr, "eidos: handoff: the save from peer %u is of another "
                        "version; it was dropped\n", (unsigned int)peer);
        return;
    }
    ns_len = bw_rd32(blob + BW_HANDOFF_BLOB_NS);
    save_len = bw_rd32(blob + BW_HANDOFF_BLOB_SAVE);
    state_len = bw_rd32(blob + BW_HANDOFF_BLOB_STATE);
    save_crc = bw_rd32(blob + BW_HANDOFF_BLOB_SAVECRC);
    state_crc = bw_rd32(blob + BW_HANDOFF_BLOB_STCRC);
    if (bw_rd32(blob + BW_HANDOFF_BLOB_TOTAL) != blob_len ||
        ns_len > BW_MOD_HANDOFF_NS_MAX ||
        (u64)ns_len + (u64)save_len + (u64)state_len +
            (u64)BW_MOD_HANDOFF_BLOB_HEAD > (u64)blob_len) {
        fprintf(stderr, "eidos: handoff: the save from peer %u does not fit "
                        "together; it was dropped\n", (unsigned int)peer);
        return;
    }
    if (handoff_crc(blob + BW_MOD_HANDOFF_BLOB_HEAD + ns_len, save_len) !=
        save_crc ||
        handoff_crc(blob + BW_MOD_HANDOFF_BLOB_HEAD + ns_len + save_len,
                    state_len) != state_crc) {
        fprintf(stderr, "eidos: handoff: the save from peer %u is damaged; it "
                        "was dropped\n", (unsigned int)peer);
        return;
    }
    memcpy(name, blob + BW_MOD_HANDOFF_BLOB_HEAD, ns_len);
    name[ns_len] = '\0';
    save = blob + BW_MOD_HANDOFF_BLOB_HEAD + ns_len;
    state = save + save_len;

    /* The port's own confirmation: the player is asked whether the save is
     * to be taken. */
    if (g_confirm == NULL) {
        fprintf(stderr, "eidos: handoff: the port has no confirmation yet; "
                        "the save from peer %u was not installed\n",
                (unsigned int)peer);
        return;
    }
    answer = g_confirm(peer, save, save_len, g_confirm_user);
    if (answer == BW_HANDOFF_CONFIRM_LATER) {
        /* The question is asked in its own turn: the blob is kept until
         * the answer comes (bw_mod_handoff_answer). */
        if (g_waiting.blob != NULL) {
            fprintf(stderr, "eidos: handoff: a save is already waiting for "
                            "the player's answer; the save from peer %u was "
                            "dropped\n", (unsigned int)peer);
            return;
        }
        g_waiting.peer = peer;
        g_waiting.blob = st->buf;
        g_waiting.ns_len = ns_len;
        g_waiting.save_len = save_len;
        g_waiting.state_len = state_len;
        st->buf = NULL;             /* the stream's drop leaves it */
        return;
    }
    if (answer == 0) {
        fprintf(stderr, "eidos: handoff: the save from peer %u was refused\n",
                (unsigned int)peer);
        return;
    }
    install_save(peer, name, save, save_len, state, state_len);
}

void bw_mod_handoff_answer(u32 peer, int yes)
{
    char name[BW_MOD_HANDOFF_NS_MAX + 1];
    BwHandoffWaiting w = g_waiting;
    const u8 *save;

    if (w.blob == NULL || w.peer != peer) {
        return;
    }
    memset(&g_waiting, 0, sizeof(g_waiting));
    if (yes == 0) {
        fprintf(stderr, "eidos: handoff: the save from peer %u was refused\n",
                (unsigned int)peer);
    } else {
        memcpy(name, w.blob + BW_MOD_HANDOFF_BLOB_HEAD, w.ns_len);
        name[w.ns_len] = '\0';
        save = w.blob + BW_MOD_HANDOFF_BLOB_HEAD + w.ns_len;
        install_save(peer, name, save, w.save_len, save + w.save_len, w.state_len);
    }
    free(w.blob);
}

/* One piece of a save. */
static void handle_piece(u32 peer, const u8 *data, u32 size)
{
    u32 seq = bw_rd32(data + BW_HANDOFF_PIECE_SEQ);
    u32 count = bw_rd32(data + BW_HANDOFF_PIECE_COUNT);
    u32 blob_len = bw_rd32(data + BW_HANDOFF_PIECE_TOTAL);
    u32 len = size - BW_MOD_HANDOFF_PIECE_HEAD;
    BwHandoffStream *st;

    if (count == 0u || seq >= count || blob_len == 0u ||
        blob_len > BW_MOD_HANDOFF_BLOB_MAX || len > blob_len) {
        fprintf(stderr, "eidos: handoff: a piece of the save from peer %u is "
                        "not usable; it was dropped\n", (unsigned int)peer);
        return;
    }
    if (seq == 0u) {
        st = stream_get(peer);
        if (st == NULL) {
            fprintf(stderr, "eidos: handoff: too many saves are arriving at "
                            "once; the save from peer %u was dropped\n",
                    (unsigned int)peer);
            return;
        }
        stream_drop(st);
        st->used = 1;
        st->peer = peer;
        st->buf = (u8 *)malloc(blob_len);
        if (st->buf == NULL) {
            memset(st, 0, sizeof(*st));
            fprintf(stderr, "eidos: handoff: the save from peer %u could not "
                            "be held (out of memory)\n", (unsigned int)peer);
            return;
        }
        st->cap = blob_len;
        st->len = 0;
        st->next_seq = 0;
        st->count = count;
    } else {
        st = stream_find(peer);
        if (st == NULL) {
            fprintf(stderr, "eidos: handoff: a piece of the save from peer %u "
                            "came without its first piece; it was dropped\n",
                    (unsigned int)peer);
            return;
        }
        if (seq != st->next_seq || count != st->count ||
            blob_len != st->cap) {
            fprintf(stderr, "eidos: handoff: the pieces of the save from peer "
                            "%u do not fit together; the save was dropped\n",
                    (unsigned int)peer);
            stream_drop(st);
            return;
        }
    }
    if (st->len + len > st->cap) {
        fprintf(stderr, "eidos: handoff: the pieces of the save from peer %u "
                        "do not fit together; the save was dropped\n",
                (unsigned int)peer);
        stream_drop(st);
        return;
    }
    if (len > 0u) {
        memcpy(st->buf + st->len, data + BW_MOD_HANDOFF_PIECE_HEAD, len);
    }
    st->len += len;
    st->next_seq++;
    if (st->next_seq >= st->count) {
        finish_save(peer, st);
        stream_drop(st);
    }
}

/*
 * The port's own session handler: where the pieces of a handoff arrive.
 * The handler is called on the game's frame (sdk/mod_net.c), never on a
 * network thread.
 */
static void handoff_session(u32 peer, int kind, const void *buf, u32 size)
{
    const u8 *data = (const u8 *)buf;

    if (kind != (int)BW_SESSION_MESSAGE || data == NULL) {
        return;                     /* a join or a drop: not ours */
    }
    if (size < BW_MOD_HANDOFF_PIECE_HEAD ||
        memcmp(data, BW_MOD_HANDOFF_PIECE_MAGIC, 4) != 0) {
        return;                     /* not a piece of a save */
    }
    handle_piece(peer, data, size);
}

/* ------------------------------------------------------------------ */
/* The services table's calls                                          */
/* ------------------------------------------------------------------ */

/*
 * `save_send`: the current game save and the mod's own saved data go to
 * the peer.  The mod that sends is the one the namespace in force names,
 * so the data that goes is that mod's own.
 *
 * The call returns when the save is on its way: the pieces go out over the
 * frames (bw_mod_handoff_frame), a few a frame, so the peer session's
 * queue does not overflow and drop them.  A port that does not drive the
 * frames gets the whole save in this call.
 */
int bw_mod_handoff_send(u32 peer)
{
    const char *ns = current_namespace();
    u8 *save = NULL;
    u8 *state = NULL;
    u32 save_len = 0;
    u32 state_len = 0;
    u8 *blob;
    u32 blob_len = 0;
    u32 count;
    s32 sent;
    int err = BW_MOD_NET_OK;

    if (read_game_save(&save, &save_len) != 0) {
        return BW_MOD_NET_E_IO;
    }
    if (read_mod_data(&state, &state_len) != 0) {
        free(save);
        return BW_MOD_NET_E_IO;
    }
    if (!g_bound) {
        /* The reset took the binding away: the port's own handler is put
         * back before anything is sent. */
        bw_mod_handoff_bind();
    }
    blob = build_blob(ns, save, save_len, state, state_len, &blob_len);
    free(save);
    free(state);
    if (blob == NULL) {
        return BW_MOD_NET_E_TOO_BIG;
    }
    count = (blob_len + BW_MOD_HANDOFF_PIECE_DATA - 1u) /
            BW_MOD_HANDOFF_PIECE_DATA;
    if (g_send.used) {
        fprintf(stderr, "eidos: handoff: a save is already on its way; this "
                        "one was not sent\n");
        free(blob);
        return BW_MOD_NET_E_BUSY;
    }
    if (!g_frame_seen) {
        /* The port does not drive the frames: the whole save goes now. */
        sent = send_pieces(peer, blob, blob_len, 0, count, count, &err);
        free(blob);
        return (sent < 0) ? err : 0;
    }
    /* The save goes over the frames: the blob is kept until the last piece
     * is sent. */
    g_send.used = 1;
    g_send.peer = peer;
    g_send.blob = blob;
    g_send.blob_len = blob_len;
    g_send.seq = 0;
    g_send.count = count;
    if (send_transfer(BW_MOD_HANDOFF_PIECES_PER_FRAME, &err) != 0) {
        return err;
    }
    return 0;
}

/*
 * `on_save_received`: the mod binds the handler that is told when a save
 * of its own name arrives.  The mod is the one the namespace in force
 * names.
 */
void bw_mod_handoff_on_save(BwSaveHandler fn)
{
    BwHandoffMod *m;

    if (fn == NULL) {
        return;
    }
    m = mod_for(current_namespace());
    if (m != NULL) {
        m->fn = fn;
    }
}

/* ------------------------------------------------------------------ */
/* The port's own calls                                                */
/* ------------------------------------------------------------------ */

void bw_mod_handoff_set_confirm(BwHandoffConfirmFn fn, void *user)
{
    g_confirm = fn;
    g_confirm_user = user;
}

void bw_mod_handoff_set_reload(BwHandoffReloadFn fn)
{
    g_reload = fn;
}

/*
 * The frame tick: the port calls this once a game frame, where it calls
 * the mod layer's own tick (sdk/mod_frame.c's bw_mod_frame_tick).  The
 * pieces of a save that is on its way go out here, a few a frame, so the
 * peer session's queue (64 KB, sdk/mod_net.c) does not overflow and drop
 * them before the other side has read them.
 */
void bw_mod_handoff_frame(void)
{
    int err = BW_MOD_NET_OK;

    g_frame_seen = 1;
    if (!g_bound) {
        /* The reset took the binding away: the peer session's table of
         * handlers is empty again, so the port's own handler is put back
         * here, at the first frame after the reset. */
        bw_mod_handoff_bind();
    }
    (void)send_transfer(BW_MOD_HANDOFF_PIECES_PER_FRAME, &err);
}

/*
 * The binding: the port's own session handler.  The peer session hands a
 * message to the mod whose key it carries, and the port's own key is the
 * empty namespace, so the port's handler is the one that takes the pieces
 * of a handoff in.
 */
void bw_mod_handoff_bind(void)
{
    char saved[BW_MOD_HANDOFF_NS_MAX + 1];

    copy_text(saved, sizeof(saved), current_namespace());
    if (bw_mod_savedata_set_namespace != NULL) {
        bw_mod_savedata_set_namespace(BW_MOD_HANDOFF_PORT_KEY);
    }
    bw_mod_net_on_session(handoff_session);
    if (bw_mod_savedata_set_namespace != NULL) {
        bw_mod_savedata_set_namespace(saved);
    }
    g_bound = 1;
}

/*
 * The reset: the streams that were arriving, the mods' own handlers and the
 * save that was on its way go.  The port's own confirmation and reload
 * stay: they are the port's, and the port installs them again when it
 * wants them.  The binding goes too: the reset empties the peer session's
 * table of handlers, and the binding is made again at the next frame.
 */
void bw_mod_handoff_reset(void)
{
    u32 i;

    for (i = 0; i < BW_MOD_HANDOFF_PEERS_MAX; i++) {
        stream_drop(&g_streams[i]);
    }
    for (i = 0; i < BW_MOD_HANDOFF_MODS_MAX; i++) {
        memset(&g_mods[i], 0, sizeof(g_mods[i]));
    }
    free(g_send.blob);
    memset(&g_send, 0, sizeof(g_send));
    /* (its question went with the message queue's reset) */
    free(g_waiting.blob);
    memset(&g_waiting, 0, sizeof(g_waiting));
    g_bound = 0;
}
