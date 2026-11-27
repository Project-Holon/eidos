/*
 * tests/test_mod_handoff.c -- the save handoff: the game save and the mod's
 * own data passed from one player's eidos to another's.
 *
 * The layer is sdk/mod_handoff.c; the contract it keeps is the one of
 * docs/mod-services.md, "Save handoff":
 *
 *   - the game save and the mod's own data go out together: one blob with a
 *     32-byte head of its own, carrying the name of the mod that sends it,
 *     the game save's bytes and the mod's own payload, with a checksum of
 *     each;
 *   - the blob is cut into pieces of the size a session message may carry
 *     (4 KB less a 20-byte head), each with its own number and the length
 *     of the whole, so a piece that never arrives is seen;
 *   - the pieces go out over the frames, a few a frame, and one transfer is
 *     on its way at a time: a second one is refused;
 *   - the receiving side puts the blob back together, asks the port's own
 *     confirmation, and only then installs the save: the game save written,
 *     the mod's own data written beside it with the checksum of the save
 *     that was installed, the mod's own handler told, and the port asked to
 *     reload;
 *   - the port's confirmation is the port's own user interface, which is
 *     not built yet: with no confirmation a save that arrives is reported
 *     and dropped, and nothing is installed;
 *   - a handoff with no session says so.
 *
 * What is stood in for:
 *
 *   - the peer session (sdk/mod_net.c): the port fills the transport under
 *     the relay client, so the session's own calls are what the handoff
 *     uses.  The test's session keeps the pieces that were sent and hands
 *     them back to the port's own session handler, the way the relay would,
 *     and it answers the session's own answers (no session, and the like);
 *   - the backup chip (sdk/arm7/fs.c): the game's save is the port's own
 *     file under the save folder, and the chip's image is that file's bytes
 *     with the rest of the chip erased.  The test's chip is the file and a
 *     size, and it remembers that the port dropped the image.
 *
 * The saved-data layer is the port's own (sdk/mod_savedata.c): the mod's own
 * data that travels is read out of the real file, and the data that is
 * installed is written to the real file.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mod_handoff.h"
#include "mod_net.h"
#include "mod_state.h"
#include "save.h"
#include "types.h"

#include "port_test.h"

#ifdef _WIN32
#include <direct.h>
#define BW_RMDIR(p) _rmdir(p)
#else
#include <unistd.h>
#define BW_RMDIR(p) rmdir(p)
#endif

/*
 * The saved data layer's own calls (sdk/mod_savedata.c).  That file holds
 * its prototypes until the header which will carry them for the rest of the
 * port is written, so the test declares the ones it uses; they are the ones
 * the file's own header comment names.
 */
void        bw_mod_savedata_set_namespace(const char *ns);
const char *bw_mod_savedata_namespace(void);
void        bw_mod_savedata_set_save(const char *name);
u32         bw_mod_savedata_crc(const void *data, u32 size);
s32         bw_mod_savedata_load(void *buf, u32 size);
int         bw_mod_savedata_store(const void *buf, u32 size);
int         bw_mod_savedata_in_step(void);

/* ------------------------------------------------------------------ */
/* The tree the test writes                                            */
/* ------------------------------------------------------------------ */

/* The save folder the test owns, under the test's own working directory. */
#define T_SAVE_DIR "mod_handoff_tmp"

/* The game's save: the port's own file under the save folder. */
#define T_SAVE_FILE "pokeblack.sav"

/* The name of the game save the mod's own data belongs to. */
#define T_SAVE_NAME "pokeblack"

/* The namespace of the mod that sends the save. */
#define T_NS "testmod"

/* The mod's own data: the bytes that travel. */
#define T_STATE "the mod's own bytes"

/* The mod's own data: the bytes that are on the disk instead. */
#define T_OTHER "the mod's other bytes"

/* The chip's size: the port's own chip is 512 KB, the test's is small. */
#define T_CHIP_SIZE 1024u

/* A bigger chip, for the phase where the blob is cut into pieces. */
#define T_CHIP_BIG  (32u * 1024u)

/* The chip's buffer: the biggest image the test's chip holds. */
#define T_CHIP_MAX  (64u * 1024u)

/* How many pieces the test's session keeps. */
#define T_PIECES_MAX 32

/* ------------------------------------------------------------------ */
/* The handoff's own format (sdk/mod_handoff.c)                       */
/* ------------------------------------------------------------------ */

/*
 * The blob's head is 32 bytes (docs/mod-services.md, "Save handoff"), and
 * the piece's head is 20 (the header's own number, sdk/mod_handoff.h).
 * The offsets in them are the port's own, and the test reads them the way
 * the port writes them, one byte at a time (sdk/types.h).
 */
#define T_BLOB_HEAD    32u
#define T_BLOB_NS       8u
#define T_BLOB_SAVE    12u
#define T_BLOB_STATE   16u
#define T_BLOB_SAVECRC 20u
#define T_BLOB_STCRC   24u
#define T_BLOB_TOTAL   28u

#define T_PIECE_SEQ     8u
#define T_PIECE_COUNT  12u
#define T_PIECE_TOTAL  16u

/* ------------------------------------------------------------------ */
/* The helpers                                                         */
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

/* The path of the game's save file. */
static int save_path(char *out, size_t size)
{
    return bw_save_path(out, size, T_SAVE_FILE);
}

/* The path of a mod's own state file. */
static int state_path(char *out, size_t size, const char *ns)
{
    char rel[256];
    int n = snprintf(rel, sizeof rel, "mods/%s/%s.bin", ns, T_SAVE_NAME);

    if (n < 0 || (size_t)n >= sizeof rel) {
        return -1;
    }
    return bw_save_path(out, size, rel);
}

/* Whether a file is there. */
static int file_exists(const char *path)
{
    FILE *fp = fopen(path, "rb");

    if (fp == NULL) {
        return 0;
    }
    fclose(fp);
    return 1;
}

/* Write a file of bytes, the port's own way: the whole file, atomically, and
 * the folder it is in is made by the write. */
static int write_file(const char *path, const void *data, size_t size)
{
    return bw_save_write_atomic(path, data, (u32)size);
}

/* Read a whole file, or -1 when there is none. */
static long read_file(const char *path, u8 *buf, size_t size)
{
    FILE *fp = fopen(path, "rb");
    size_t n;

    if (fp == NULL) {
        return -1;
    }
    n = fread(buf, 1, size, fp);
    fclose(fp);
    return (long)n;
}

/* Whether the file holds exactly these bytes. */
static int file_holds(const char *path, const void *data, size_t size)
{
    static u8 buf[T_CHIP_MAX];
    long n;

    if (size > sizeof buf) {
        return 0;
    }
    n = read_file(path, buf, sizeof buf);
    if (n < 0 || (size_t)n != size) {
        return 0;
    }
    return memcmp(buf, data, size) == 0;
}

/* The mod's own payload in its file, or -1 when there is none. */
static long read_payload(const char *path, u8 *buf, size_t size)
{
    u8 head[BW_MOD_STATE_HEADER_SIZE];
    FILE *fp;
    size_t n;

    fp = fopen(path, "rb");
    if (fp == NULL) {
        return -1;
    }
    if (fread(head, 1, sizeof head, fp) != sizeof head) {
        fclose(fp);
        return -1;
    }
    n = fread(buf, 1, size, fp);
    fclose(fp);
    return (long)n;
}

/* Take the tree an earlier run left away. */
static void clean_tree(void)
{
    static const char *const files[] = {
        T_SAVE_DIR "/" T_SAVE_FILE,
        T_SAVE_DIR "/" T_SAVE_FILE ".tmp",
        T_SAVE_DIR "/mods/" T_NS "/" T_SAVE_NAME ".bin",
        T_SAVE_DIR "/mods/" T_NS "/" T_SAVE_NAME ".bin.tmp"
    };
    static const char *const dirs[] = {
        T_SAVE_DIR "/mods/" T_NS,
        T_SAVE_DIR "/mods",
        T_SAVE_DIR
    };
    size_t i;

    for (i = 0; i < sizeof files / sizeof files[0]; i++) {
        remove(files[i]);
    }
    for (i = 0; i < sizeof dirs / sizeof dirs[0]; i++) {
        BW_RMDIR(dirs[i]);
    }
}

/* ------------------------------------------------------------------ */
/* The peer session, stood in for                                      */
/* ------------------------------------------------------------------ */

/*
 * The peer session (sdk/mod_net.c): the port fills the transport under the
 * relay client, so the session's own calls are what the handoff uses.  The
 * test's session keeps the pieces that were sent, with the key they went
 * out under, and hands them back to the port's own session handler the way
 * the relay would.  The answers are the session's own ones.
 */
static int              g_session;             /* the session is running   */
static BwSessionHandler g_session_handler;     /* the handler that is bound */
static char             g_session_key[64];     /* the key it is bound under */
static u8               g_piece[T_PIECES_MAX][BW_MOD_NET_MESSAGE_MAX];
static u32              g_piece_len[T_PIECES_MAX];
static u32              g_piece_peer[T_PIECES_MAX];
static char             g_piece_key[T_PIECES_MAX][64];
static int              g_piece_count;

int bw_mod_net_session_send(u32 peer, const void *buf, u32 size)
{
    if (!g_session) {
        return BW_MOD_NET_E_NO_SESSION;
    }
    if (buf == NULL || size > BW_MOD_NET_MESSAGE_MAX ||
        g_piece_count >= T_PIECES_MAX) {
        return BW_MOD_NET_E_ARG;
    }
    memcpy(g_piece[g_piece_count], buf, size);
    g_piece_len[g_piece_count] = size;
    g_piece_peer[g_piece_count] = peer;
    copy_text(g_piece_key[g_piece_count], sizeof g_piece_key[0],
              bw_mod_savedata_namespace());
    g_piece_count++;
    return BW_MOD_NET_OK;
}

void bw_mod_net_on_session(BwSessionHandler fn)
{
    if (fn == NULL) {
        return;
    }
    g_session_handler = fn;
    copy_text(g_session_key, sizeof g_session_key,
              bw_mod_savedata_namespace());
}

/* Forget the pieces that were sent, and keep the session and the handler. */
static void pieces_clear(void)
{
    g_piece_count = 0;
}

/* Hand a piece to the port's own session handler. */
static void deliver(u32 peer, int index)
{
    g_session_handler(peer, BW_SESSION_MESSAGE, g_piece[index],
                      g_piece_len[index]);
}

/* ------------------------------------------------------------------ */
/* The backup chip, stood in for                                       */
/* ------------------------------------------------------------------ */

/*
 * The backup chip (sdk/arm7/fs.c): the game's save is the port's own file
 * under the save folder, and the chip's image is that file's bytes with the
 * rest of the chip erased.  The test's chip is the file and a size: the
 * image is read when it is asked for (the chip identifies again after a
 * reset), and the port drops it before it writes a save, so the game loads
 * the file and not the image it had.
 */
static u32 g_chip_size = T_CHIP_SIZE;
static u8  g_chip[T_CHIP_MAX];
static int g_chip_dropped;

void bw_arm7_backup_set_path(const char *path)
{
    (void)path;
    g_chip_dropped = 1;
}

int bw_arm7_backup_flush(void)
{
    return 0;
}

const u8 *bw_arm7_backup_image(u32 *size)
{
    char path[1200];
    long n;

    if (size == NULL) {
        return NULL;
    }
    *size = 0;
    if (bw_save_path(path, sizeof path, T_SAVE_FILE) != 0) {
        return NULL;
    }
    memset(g_chip, 0xFF, sizeof g_chip);
    n = bw_save_read(path, g_chip, g_chip_size);
    if (n < 0) {
        return NULL;               /* no save file: an erased chip */
    }
    *size = g_chip_size;
    return g_chip;
}

/* ------------------------------------------------------------------ */
/* The port's own seams, and the mod's own handler                     */
/* ------------------------------------------------------------------ */

/* The confirmation: the player is asked whether the save is to be taken. */
static int g_confirm_calls;
static int g_confirm_answer;
static u32 g_confirm_peer;
static u32 g_confirm_size;

static int confirm_take(u32 peer, const void *buf, u32 size, void *user)
{
    (void)buf;
    (void)user;
    g_confirm_calls++;
    g_confirm_peer = peer;
    g_confirm_size = size;
    return g_confirm_answer;
}

/* The reload: the save is installed and the game goes on with it. */
static int g_reload_calls;

static void reload_run(void)
{
    g_reload_calls++;
}

/* The mod's own handler: the mod that is told when a save of its own name
 * arrives. */
static int g_mod_calls;
static u32 g_mod_peer;
static u32 g_mod_size;
static u8  g_mod_bytes[T_CHIP_MAX];

static void mod_save(u32 peer, const void *buf, u32 size)
{
    g_mod_calls++;
    g_mod_peer = peer;
    g_mod_size = size;
    if (buf != NULL && size > 0 && size <= sizeof g_mod_bytes) {
        memcpy(g_mod_bytes, buf, size);
    }
}

/* ------------------------------------------------------------------ */
/* The test                                                            */
/* ------------------------------------------------------------------ */

int main(void)
{
    static u8 sent[T_CHIP_MAX];
    static u8 other[T_CHIP_MAX];
    u8 payload[64];
    char path[1200];
    u32 i;
    u32 count;

    bw_set_save_dir(T_SAVE_DIR);
    bw_mod_savedata_set_save(T_SAVE_NAME);
    clean_tree();

    /* The game's save: the port's own file under the save folder. */
    for (i = 0; i < T_CHIP_SIZE; i++) {
        sent[i] = (u8)(i & 0xFFu);
    }
    BW_REQUIRE("the save file's path is known",
               save_path(path, sizeof path) == 0);
    BW_REQUIRE("the game save is written",
               write_file(path, sent, T_CHIP_SIZE) == 0);
    /* The mod's own data: the file the layer keeps. */
    bw_mod_savedata_set_namespace(T_NS);
    BW_REQUIRE("the mod's own data is stored",
               bw_mod_savedata_store(T_STATE, (u32)strlen(T_STATE)) == 0);

    printf("a handoff with no session\n");
    g_session = 0;
    BW_CHECK_EQ("the handoff says there is no session",
                bw_mod_handoff_send(1), BW_MOD_NET_E_NO_SESSION);
    BW_CHECK_EQ("and nothing went out", g_piece_count, 0);

    printf("the save goes out\n");
    g_session = 1;
    BW_CHECK_EQ("the save is on its way", bw_mod_handoff_send(2), 0);
    BW_CHECK_EQ("one message went out", g_piece_count, 1);
    BW_CHECK_EQ("it went to the peer", (long)g_piece_peer[0], 2);
    BW_CHECK_EQ("under the port's own key", strcmp(g_piece_key[0], ""), 0);
    BW_REQUIRE("the port's own handler is bound", g_session_handler != NULL);
    BW_CHECK_EQ("under the port's own key as well",
                strcmp(g_session_key, ""), 0);
    {
        const u8 *piece = g_piece[0];
        const u8 *blob;
        u32 len = g_piece_len[0];
        u32 blob_len;
        u32 ns_len;
        u32 save_len;
        u32 state_len;

        BW_REQUIRE("the piece carries a head",
                   len > BW_MOD_HANDOFF_PIECE_HEAD);
        BW_CHECK_EQ("the piece is the port's own", memcmp(piece, "EIDC", 4), 0);
        BW_CHECK_EQ("the piece is the first one",
                    (long)bw_rd32(piece + T_PIECE_SEQ), 0);
        BW_CHECK_EQ("and the only one",
                    (long)bw_rd32(piece + T_PIECE_COUNT), 1);
        blob_len = bw_rd32(piece + T_PIECE_TOTAL);
        BW_CHECK_EQ("the piece carries the whole of the blob", (long)len,
                    (long)(BW_MOD_HANDOFF_PIECE_HEAD + blob_len));
        blob = piece + BW_MOD_HANDOFF_PIECE_HEAD;
        BW_CHECK_EQ("the blob is the port's own", memcmp(blob, "EIDB", 4), 0);
        ns_len = bw_rd32(blob + T_BLOB_NS);
        save_len = bw_rd32(blob + T_BLOB_SAVE);
        state_len = bw_rd32(blob + T_BLOB_STATE);
        BW_CHECK_EQ("the blob says how long it is",
                    (long)bw_rd32(blob + T_BLOB_TOTAL), (long)blob_len);
        BW_CHECK_EQ("and it is the head and the three parts",
                    (long)bw_rd32(blob + T_BLOB_TOTAL),
                    (long)(T_BLOB_HEAD + ns_len + save_len + state_len));
        BW_CHECK_EQ("the name of the mod that sends travels",
                    (long)ns_len, (long)strlen(T_NS));
        BW_CHECK_EQ("and it is the mod's own name",
                    memcmp(blob + T_BLOB_HEAD, T_NS, strlen(T_NS)), 0);
        BW_CHECK_EQ("the game save travels with it", (long)save_len,
                    (long)T_CHIP_SIZE);
        BW_CHECK_EQ("and it is the game's own bytes",
                    memcmp(blob + T_BLOB_HEAD + ns_len, sent, T_CHIP_SIZE), 0);
        BW_CHECK_EQ("the mod's own data travels too", (long)state_len,
                    (long)strlen(T_STATE));
        BW_CHECK_EQ("and it is the mod's own bytes",
                    memcmp(blob + T_BLOB_HEAD + ns_len + save_len, T_STATE,
                           state_len), 0);
        BW_CHECK_EQ("the blob carries the save's checksum",
                    (long)bw_rd32(blob + T_BLOB_SAVECRC),
                    (long)bw_mod_savedata_crc(sent, T_CHIP_SIZE));
        BW_CHECK_EQ("and the mod's own data's checksum",
                    (long)bw_rd32(blob + T_BLOB_STCRC),
                    (long)bw_mod_savedata_crc(T_STATE, state_len));
    }

    printf("the receiving side with no confirmation\n");
    bw_mod_savedata_set_namespace(T_NS);
    bw_mod_handoff_on_save(mod_save);
    /* The port's own reload: the save is installed and the game goes on
     * with it. */
    bw_mod_handoff_set_reload(reload_run);
    /* The player has played on: the disk holds another save and another
     * payload, so an install is seen. */
    memset(other, 0x5A, sizeof other);
    BW_REQUIRE("the game save is written again",
               write_file(path, other, T_CHIP_SIZE) == 0);
    BW_REQUIRE("the mod's own data is stored again",
               bw_mod_savedata_store(T_OTHER, (u32)strlen(T_OTHER)) == 0);
    g_confirm_calls = 0;
    g_reload_calls = 0;
    g_mod_calls = 0;
    deliver(2, 0);
    BW_CHECK_EQ("the port has no confirmation, so nobody is asked",
                g_confirm_calls, 0);
    BW_CHECK_EQ("the save is not installed",
                file_holds(path, other, T_CHIP_SIZE), 1);
    BW_CHECK_EQ("the mod's handler is not told", g_mod_calls, 0);
    BW_CHECK_EQ("and the port is not asked to reload", g_reload_calls, 0);

    printf("the receiving side with a confirmation that refuses\n");
    g_confirm_answer = 0;
    bw_mod_handoff_set_confirm(confirm_take, NULL);
    g_confirm_calls = 0;
    g_reload_calls = 0;
    g_mod_calls = 0;
    deliver(2, 0);
    BW_CHECK_EQ("the player is asked", g_confirm_calls, 1);
    BW_CHECK_EQ("the player is told who sent the save",
                (long)g_confirm_peer, 2);
    BW_CHECK_EQ("and how big the save is", (long)g_confirm_size,
                (long)T_CHIP_SIZE);
    BW_CHECK_EQ("the save is not installed",
                file_holds(path, other, T_CHIP_SIZE), 1);
    BW_CHECK_EQ("the mod's handler is not told", g_mod_calls, 0);
    BW_CHECK_EQ("and the port is not asked to reload", g_reload_calls, 0);

    printf("the receiving side with a confirmation that answers later\n");
    /* The question is asked in its own turn: the save waits for the
     * answer, and a no drops it. */
    g_confirm_answer = BW_HANDOFF_CONFIRM_LATER;
    g_confirm_calls = 0;
    g_reload_calls = 0;
    g_mod_calls = 0;
    deliver(2, 0);
    BW_CHECK_EQ("the player is asked", g_confirm_calls, 1);
    BW_CHECK_EQ("nothing is installed before the answer",
                file_holds(path, other, T_CHIP_SIZE), 1);
    bw_mod_handoff_answer(3, 1);
    BW_CHECK_EQ("another peer's answer installs nothing",
                file_holds(path, other, T_CHIP_SIZE), 1);
    bw_mod_handoff_answer(2, 0);
    BW_CHECK_EQ("the no drops the save",
                file_holds(path, other, T_CHIP_SIZE), 1);
    BW_CHECK_EQ("the mod's handler is not told", g_mod_calls, 0);
    BW_CHECK_EQ("and the port is not asked to reload", g_reload_calls, 0);
    bw_mod_handoff_answer(2, 1);
    BW_CHECK_EQ("an answer with no save waiting installs nothing",
                file_holds(path, other, T_CHIP_SIZE), 1);

    printf("the receiving side with a confirmation that takes it\n");
    g_confirm_answer = 1;
    g_confirm_calls = 0;
    g_reload_calls = 0;
    g_mod_calls = 0;
    g_chip_dropped = 0;
    deliver(2, 0);
    BW_CHECK_EQ("the player is asked", g_confirm_calls, 1);
    BW_CHECK_EQ("the game save is installed",
                file_holds(path, sent, T_CHIP_SIZE), 1);
    BW_REQUIRE("the mod's own file's path is known",
               state_path(path, sizeof path, T_NS) == 0);
    BW_CHECK_EQ("the mod's own file is there", file_exists(path), 1);
    BW_CHECK_EQ("the mod's own data is installed too",
                (long)read_payload(path, payload, sizeof payload),
                (long)strlen(T_STATE));
    BW_CHECK_EQ("and it is the mod's own bytes",
                memcmp(payload, T_STATE, strlen(T_STATE)), 0);
    BW_CHECK_EQ("the mod's handler is told", g_mod_calls, 1);
    BW_CHECK_EQ("with the peer's number", (long)g_mod_peer, 2);
    BW_CHECK_EQ("and the save's bytes", (long)g_mod_size, (long)T_CHIP_SIZE);
    BW_CHECK_EQ("which are the game's own",
                memcmp(g_mod_bytes, sent, T_CHIP_SIZE), 0);
    BW_CHECK_EQ("the port is asked to reload", g_reload_calls, 1);
    BW_CHECK_EQ("the chip's own image is dropped first", g_chip_dropped, 1);
    bw_mod_savedata_set_namespace(T_NS);
    BW_CHECK_EQ("the mod's own file is in step with the save in force",
                bw_mod_savedata_in_step(), 1);

    printf("the receiving side with a confirmation that answers yes later\n");
    /* The port's own confirmation answers later; the yes installs the save
     * from the kept blob. */
    BW_REQUIRE("the save file's path is known",
               save_path(path, sizeof path) == 0);
    BW_REQUIRE("the game save is written again",
               write_file(path, other, T_CHIP_SIZE) == 0);
    g_confirm_answer = BW_HANDOFF_CONFIRM_LATER;
    g_confirm_calls = 0;
    g_reload_calls = 0;
    g_mod_calls = 0;
    deliver(2, 0);
    BW_CHECK_EQ("the player is asked", g_confirm_calls, 1);
    BW_CHECK_EQ("nothing is installed before the answer",
                file_holds(path, other, T_CHIP_SIZE), 1);
    bw_mod_handoff_answer(2, 1);
    BW_CHECK_EQ("the yes installs the game save",
                file_holds(path, sent, T_CHIP_SIZE), 1);
    BW_CHECK_EQ("the mod's handler is told", g_mod_calls, 1);
    BW_CHECK_EQ("with the save's bytes", memcmp(g_mod_bytes, sent, T_CHIP_SIZE), 0);
    BW_CHECK_EQ("the port is asked to reload", g_reload_calls, 1);
    bw_mod_savedata_set_namespace(T_NS);
    BW_CHECK_EQ("the mod's own file is in step with the save in force",
                bw_mod_savedata_in_step(), 1);
    g_confirm_answer = 1;

    printf("the pieces go out over the frames\n");
    /* The port drives the frames: the pieces go out a few a frame. */
    bw_mod_handoff_frame();
    /* A bigger save: the blob does not fit in one piece. */
    g_chip_size = T_CHIP_BIG;
    for (i = 0; i < T_CHIP_BIG; i++) {
        other[i] = (u8)(0xF0u ^ (u8)(i & 0x0Fu));
    }
    BW_REQUIRE("the save file's path is known",
               save_path(path, sizeof path) == 0);
    BW_REQUIRE("the big game save is written",
               write_file(path, other, T_CHIP_BIG) == 0);
    BW_REQUIRE("the mod's own data is stored again",
               bw_mod_savedata_store(T_STATE, (u32)strlen(T_STATE)) == 0);
    pieces_clear();
    BW_CHECK_EQ("the save is on its way", bw_mod_handoff_send(3), 0);
    BW_CHECK_EQ("a few pieces went out", g_piece_count, 4);
    BW_CHECK_EQ("a second transfer is refused",
                bw_mod_handoff_send(3), BW_MOD_NET_E_BUSY);
    count = bw_rd32(g_piece[0] + T_PIECE_COUNT);
    BW_CHECK_EQ("the blob is cut into more than one piece",
                (long)count > 1, 1);
    while (g_piece_count < (int)count && g_piece_count < T_PIECES_MAX) {
        bw_mod_handoff_frame();
    }
    BW_CHECK_EQ("every piece went out", g_piece_count, (long)count);
    for (i = 0; i < count && i < T_PIECES_MAX; i++) {
        BW_CHECK_EQ("the piece has its own number",
                    (long)bw_rd32(g_piece[i] + T_PIECE_SEQ), (long)i);
        BW_CHECK_EQ("and they all carry the whole",
                    (long)bw_rd32(g_piece[i] + T_PIECE_TOTAL),
                    (long)bw_rd32(g_piece[0] + T_PIECE_TOTAL));
    }

    printf("a piece that never arrives\n");
    g_confirm_calls = 0;
    g_reload_calls = 0;
    g_mod_calls = 0;
    /* The first piece on its own is not a save. */
    deliver(3, 1);
    BW_CHECK_EQ("a piece without its first one is dropped",
                g_confirm_calls, 0);
    /* The first piece and the third: the second one never arrived. */
    deliver(3, 0);
    deliver(3, 2);
    BW_CHECK_EQ("the save that does not fit together is dropped",
                g_confirm_calls, 0);
    BW_CHECK_EQ("the mod's handler is not told", g_mod_calls, 0);
    BW_CHECK_EQ("and the port is not asked to reload", g_reload_calls, 0);
    BW_CHECK_EQ("the save on the disk is the one that was there",
                file_holds(path, other, T_CHIP_BIG), 1);

    printf("the reset\n");
    bw_mod_handoff_reset();
    g_confirm_calls = 0;
    g_reload_calls = 0;
    g_mod_calls = 0;
    for (i = 0; i < count && i < T_PIECES_MAX; i++) {
        deliver(3, (int)i);
    }
    BW_CHECK_EQ("the player is asked", g_confirm_calls, 1);
    BW_CHECK_EQ("the save is installed", g_reload_calls, 1);
    BW_CHECK_EQ("but the mods' own handlers are gone", g_mod_calls, 0);

    clean_tree();
    BW_REPORT("test_mod_handoff");
}
