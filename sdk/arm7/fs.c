/*
 * sdk/arm7/fs.c -- the ARM7's side of the Slot-1 backup commands, PXI tag
 * 11 (the SDK's PXI_FIFO_TAG_FS, which libcard uses for its requests), and
 * the backup chip behind it: the game's save, kept in a host file.
 *
 * Why a stand-in: sdk/arm7/arm7.h.  On the DS the ARM7 drives the card's
 * SPI backup chip; the ARM9 only fills a shared command block and asks.
 *
 * The protocol, from the matched ARM9 side (aletheia src/matched/arm9_sdk/,
 * libcard.TWL.HYB.a card_backup.o and card_common.o):
 *
 *   - CARDi_InitCommand (func_020760B4.c) points cardi_common.cmd at the
 *     0x60-byte block cardi_arg and clears it.
 *   - CARDi_Request (func_020763F0.c) sends the request type as one word
 *     with the error bit SET (libcard's own "this is a request" flag); for
 *     request 0 (initialise) a second word follows, also flagged: the
 *     block's address.  It then sleeps until a flagged word on tag 11 comes
 *     back (CARDi_OnFifoRecv, func_020763BC.c, ignores the data) and reads
 *     the block's `result`, retrying while it is 4 (timeout).
 *   - The block is CARDiCommandArg (CARDi_IdentifyBackupCore2.c, DWARF):
 *     result +0x00, type +0x04, id +0x08, src +0x0C, dst +0x10, len +0x14,
 *     and the CARDBackupTypeInfo spec from +0x18 (total_size +0x18,
 *     sect_size +0x1C, subsect_size +0x20, page_size +0x24, initial_status
 *     +0x54, caps +0x58).  Every field is 32 bits wide, so the layout is
 *     the same at both pointer widths.
 *   - CARD_IdentifyBackup (func_02076B2C.c) fills the spec from the
 *     CARDBackupType word on the ARM9 (CARDi_IdentifyBackupCore2), then
 *     sends IDENTIFY (2) and reads one byte at address 0 (READ, 6) as a
 *     probe (CARDi_IdentifyBackupCore, func_02076A20.c).
 *   - CARDi_RequestStreamCommandCore (func_02076540.c) splits a transfer
 *     into requests of at most 256 bytes through the ARM9's bounce buffer
 *     CARDi_backup_cache_page_buf: for a read the ARM7 writes the buffer
 *     (dst), for a write or program it reads it (src), and in "send and
 *     verify" mode every write is followed by VERIFY (9).  Erases carry a
 *     backup address and a length and no memory.
 *   - Request numbers are the SDK's CARDRequest; caps bit n of the spec
 *     says request n is available (CARD_BACKUP_CAPS_READ = 0x40 is request
 *     6, ... ERASE_SUBSECTOR = 0x8000 is request 15), which is how this file
 *     decides what a chip supports.  Results are CARDResult: 0 success,
 *     1 failure (a verify mismatch), 2 invalid parameter, 3 unsupported,
 *     6 no response (no chip identified).  The reply word is CARD_REQ_ACK
 *     (1), flagged.
 *
 * Pokemon Black identifies its chip as 0x00FF1302 (arm9/func_02033534.c):
 * device 2 (flash), size bit 0x13 (512 KiB), vendor 0xFF.  It reads with
 * READ and writes with WRITE + VERIFY (arm9/func_020336B8.c: request 7,
 * mode 2).
 *
 * The command block and the bounce buffer reach the ARM7 by name, not by
 * the address in the initialise word: that is a host pointer squeezed into
 * 26 bits, which does not survive the trip on a 64-bit host.  Unless a
 * caller binds them (bw_arm7_fs_bind, the tests), they are the ARM9's own
 * objects at their DS addresses, cardi_arg at 0x021501A0 and
 * CARDi_backup_cache_page_buf at 0x02150740, which docs/armdata.md binds
 * every extern name to.  The 32-bit src/dst a request carries for memory is
 * matched against the bound buffer; on a 32-bit host any other value is a
 * host pointer and is used as one (sdk/arm7/arm7.h, bw_arm7_resolve).
 *
 * The chip.  The image lives in memory, loaded at IDENTIFY from
 * <save dir>/pokeblack.sav (sdk/save.h), a raw image of the chip, the
 * format emulators use: a longer file (a DeSmuME .dsv trailer) is read up
 * to the chip's size, a shorter or missing one is padded with 0xFF (erased
 * flash, which the game reads as "no save").  Nothing is written until the
 * game changes a byte.  Flash program clears bits (AND), write replaces,
 * erase sets 0xFF, addresses wrap at the chip's size as the SPI address
 * bits do.  A modified image goes back to the file whole and atomically
 * (bw_save_write_atomic) once requests have stopped for the flush delay
 * (default 1 second, checked at every ARM7 step), at bw_arm7_reset, at
 * exit, and on bw_arm7_backup_flush.  A delay of 0 writes after every
 * modifying request.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "arm7.h"
#include "dsmem.h"
#include "pxi.h"
#include "save.h"

#define CARDI_ARG_DS            0x021501A0u
#define CARDI_CACHE_BUF_DS      0x02150740u
#define CARDI_CACHE_BUF_SIZE    256u

/* CARDiCommandArg offsets.                                                 */
#define CMD_RESULT          0x00
#define CMD_TYPE            0x04
#define CMD_SRC             0x0C
#define CMD_DST             0x10
#define CMD_LEN             0x14
#define CMD_TOTAL_SIZE      0x18
#define CMD_SECT_SIZE       0x1C
#define CMD_SUBSECT_SIZE    0x20
#define CMD_PAGE_SIZE       0x24
#define CMD_CAPS            0x58

/* CARDRequest.                                                             */
enum {
    REQ_INIT = 0, REQ_ACK = 1, REQ_IDENTIFY = 2,
    REQ_READ_BACKUP = 6, REQ_WRITE_BACKUP, REQ_PROGRAM_BACKUP,
    REQ_VERIFY_BACKUP, REQ_ERASE_PAGE_BACKUP, REQ_ERASE_SECTOR_BACKUP,
    REQ_ERASE_CHIP_BACKUP, REQ_READ_STATUS, REQ_WRITE_STATUS,
    REQ_ERASE_SUBSECTOR_BACKUP
};

/* CARDResult.                                                              */
enum {
    RES_SUCCESS = 0, RES_FAILURE = 1, RES_INVALID_PARAM = 2,
    RES_UNSUPPORTED = 3, RES_NO_RESPONSE = 6
};

#define DEVICE_FLASH  2

static struct {
    /* Protocol.                                                            */
    u32 requests;
    u32 last;
    int expect_arg;
    u32 init_word;                  /* the 26 bits the initialise word had  */
    /* Bindings (NULL: the ARM9 objects at their DS addresses).             */
    u8 *cmd;
    u8 *cache;
    u32 cache_size;
    /* The chip.                                                            */
    u32 type;                       /* CARDBackupType, 0 = none identified  */
    int device;
    u32 total, sect, subsect, page, caps;
    u8  status;
    u8 *image;
    u32 image_size;
    int loaded;
    int dirty;
    time_t changed_at;
    int flush_delay;
    int flush_delay_set;
    int atexit_done;
    char path[1100];
    int path_set;
    u32 writes;                     /* successful file writes               */
    u32 failures;                   /* failed writes since the last success */
    int hold;                       /* writes stay in memory (save slots)   */
} g_fs;

/* ------------------------------------------------------------------ */
/* The command block                                                   */
/* ------------------------------------------------------------------ */

static u8 *cmd_block(void)
{
    return g_fs.cmd != NULL ? g_fs.cmd : (u8 *)bw_ds_try_ptr(CARDI_ARG_DS);
}

static u32 cmd_rd(const u8 *b, int off)
{
    u32 v;

    memcpy(&v, b + off, 4);         /* the ARM9 wrote it as a host u32      */
    return v;
}

static void cmd_wr(u8 *b, int off, u32 v)
{
    memcpy(b + off, &v, 4);
}

/* The ARM9 memory a request's 32-bit src or dst names.                     */
static u8 *memory(u32 value, u32 len)
{
    u8 *cache = g_fs.cache;
    u32 cache_size = g_fs.cache_size;

    if (cache == NULL) {
        cache = (u8 *)bw_ds_try_ptr(CARDI_CACHE_BUF_DS);
        cache_size = CARDI_CACHE_BUF_SIZE;
    }
    return (u8 *)bw_arm7_resolve(value, len, cache, cache_size);
}

/* ------------------------------------------------------------------ */
/* The host file                                                       */
/* ------------------------------------------------------------------ */

static const char *save_path(void)
{
    if (!g_fs.path_set
        && bw_save_path(g_fs.path, sizeof g_fs.path, "pokeblack.sav") != 0)
        g_fs.path[0] = '\0';
    return g_fs.path;
}

static void at_exit_flush(void)
{
    (void)bw_arm7_backup_flush();
}

static int flush_delay(void)
{
    return g_fs.flush_delay_set ? g_fs.flush_delay : 1;
}

int bw_arm7_backup_flush(void)
{
    const char *path;

    if (!g_fs.dirty || g_fs.hold)
        return 0;
    path = save_path();
    if (path[0] == '\0') {
        fprintf(stderr, "eidos: backup: no save folder; the save stays in "
                        "memory (set BW_SAVE_DIR)\n");
        g_fs.dirty = 0;
        return -1;
    }
    if (bw_save_write_atomic(path, g_fs.image, g_fs.image_size) != 0) {
        if (g_fs.failures++ == 0) {
            fprintf(stderr, "eidos: backup: cannot write %s; will retry\n", path);
        }
        g_fs.changed_at = time(NULL);
        return -1;
    }
    if (g_fs.failures != 0) {
        fprintf(stderr, "eidos: backup: written %s after %u failed tries\n", path, g_fs.failures);
    }
    g_fs.failures = 0;
    g_fs.dirty = 0;
    g_fs.writes++;
    return 0;
}

/* Make the image the chip's size, from the file when there is one.        */
static void load_image(u32 size)
{
    long n;

    if (g_fs.loaded && g_fs.image_size == size)
        return;
    (void)bw_arm7_backup_flush();
    free(g_fs.image);
    g_fs.image = (u8 *)malloc(size);
    if (g_fs.image == NULL) {
        g_fs.image_size = 0;
        g_fs.loaded = 0;
        return;
    }
    g_fs.image_size = size;
    memset(g_fs.image, 0xFF, size);
    n = bw_save_read(save_path(), g_fs.image, size);
    if (n >= 0 && (u32)n < size)
        memset(g_fs.image + n, 0xFF, size - (u32)n);
    g_fs.loaded = 1;
    g_fs.dirty = 0;
    if (!g_fs.atexit_done) {
        g_fs.atexit_done = 1;
        atexit(at_exit_flush);
    }
}

static void changed(void)
{
    g_fs.dirty = 1;
    g_fs.changed_at = time(NULL);
    if (flush_delay() == 0)
        (void)bw_arm7_backup_flush();
}

/* ------------------------------------------------------------------ */
/* The chip                                                            */
/* ------------------------------------------------------------------ */

static u32 wrap(u32 addr)
{
    return addr & (g_fs.image_size - 1u);
}

static void identify(const u8 *b)
{
    g_fs.type = cmd_rd(b, CMD_TYPE);
    g_fs.device = (int)(g_fs.type & 0xFF);
    g_fs.total = cmd_rd(b, CMD_TOTAL_SIZE);
    g_fs.sect = cmd_rd(b, CMD_SECT_SIZE);
    g_fs.subsect = cmd_rd(b, CMD_SUBSECT_SIZE);
    g_fs.page = cmd_rd(b, CMD_PAGE_SIZE);
    g_fs.caps = cmd_rd(b, CMD_CAPS);
    g_fs.status = 0;
    if (g_fs.type == 0 || g_fs.total == 0
        || (g_fs.total & (g_fs.total - 1u)) != 0) {
        g_fs.type = 0;              /* CARD_BACKUP_TYPE_NOT_USE             */
        return;
    }
    load_image(g_fs.total);
    if (!g_fs.loaded)
        g_fs.type = 0;
}

/* Fill the whole `unit`-byte blocks covering [addr, addr + len) with 0xFF. */
static void erase_units(u32 addr, u32 len, u32 unit)
{
    u32 a, end;

    if (unit == 0 || len == 0 || unit > g_fs.image_size
        || (unit & (unit - 1u)) != 0)
        return;
    a = addr - addr % unit;
    end = addr + len;
    for (; a < end; a += unit)
        memset(g_fs.image + wrap(a), 0xFF, unit);
    changed();
}

static int execute(int req, const u8 *b)
{
    u32 src, dst, len, i;
    u8 *mem;

    if (req == REQ_IDENTIFY) {
        identify(b);
        return RES_SUCCESS;
    }
    if (req < REQ_READ_BACKUP || req > REQ_ERASE_SUBSECTOR_BACKUP)
        return RES_UNSUPPORTED;
    if (g_fs.type == 0)
        return RES_NO_RESPONSE;
    if ((g_fs.caps & (1u << req)) == 0)
        return RES_UNSUPPORTED;

    src = cmd_rd(b, CMD_SRC);
    dst = cmd_rd(b, CMD_DST);
    len = cmd_rd(b, CMD_LEN);

    switch (req) {
    case REQ_READ_BACKUP:
        if ((mem = memory(dst, len)) == NULL)
            return RES_INVALID_PARAM;
        for (i = 0; i < len; i++)
            mem[i] = g_fs.image[wrap(src + i)];
        return RES_SUCCESS;
    case REQ_WRITE_BACKUP:
    case REQ_PROGRAM_BACKUP:
        if ((mem = memory(src, len)) == NULL)
            return RES_INVALID_PARAM;
        for (i = 0; i < len; i++) {
            u8 *cell = &g_fs.image[wrap(dst + i)];

            /* Flash program can only clear bits; EEPROM and FRAM program
             * is a plain write, as is flash's page write.                  */
            if (req == REQ_PROGRAM_BACKUP && g_fs.device == DEVICE_FLASH)
                *cell &= mem[i];
            else
                *cell = mem[i];
        }
        if (len != 0)
            changed();
        return RES_SUCCESS;
    case REQ_VERIFY_BACKUP:
        if ((mem = memory(src, len)) == NULL)
            return RES_INVALID_PARAM;
        for (i = 0; i < len; i++) {
            if (g_fs.image[wrap(dst + i)] != mem[i])
                return RES_FAILURE;
        }
        return RES_SUCCESS;
    case REQ_ERASE_PAGE_BACKUP:
        erase_units(dst, len, g_fs.page);
        return RES_SUCCESS;
    case REQ_ERASE_SECTOR_BACKUP:
        erase_units(dst, len, g_fs.sect);
        return RES_SUCCESS;
    case REQ_ERASE_SUBSECTOR_BACKUP:
        erase_units(dst, len, g_fs.subsect);
        return RES_SUCCESS;
    case REQ_ERASE_CHIP_BACKUP:
        memset(g_fs.image, 0xFF, g_fs.image_size);
        changed();
        return RES_SUCCESS;
    case REQ_READ_STATUS:
        if ((mem = memory(dst, 1)) == NULL)
            return RES_INVALID_PARAM;
        mem[0] = g_fs.status;       /* never busy: every write is done      */
        return RES_SUCCESS;
    case REQ_WRITE_STATUS:
        if ((mem = memory(src, 1)) == NULL)
            return RES_INVALID_PARAM;
        g_fs.status = mem[0];
        return RES_SUCCESS;
    default:
        return RES_UNSUPPORTED;
    }
}

/* ------------------------------------------------------------------ */
/* PXI                                                                 */
/* ------------------------------------------------------------------ */

static void fs_callback(int tag, u32 data, int err)
{
    u8 *b;

    (void)tag;
    (void)err;
    if (g_fs.expect_arg) {
        g_fs.expect_arg = 0;            /* the command block's address      */
        g_fs.init_word = data;
        b = cmd_block();
        if (b != NULL)
            cmd_wr(b, CMD_RESULT, RES_SUCCESS);
    } else {
        g_fs.requests++;
        g_fs.last = data;
        if (data == REQ_INIT) {
            g_fs.expect_arg = 1;
            return;
        }
        b = cmd_block();
        if (b != NULL)
            cmd_wr(b, CMD_RESULT, (u32)execute((int)data, b));
    }
    /* CARD_REQ_ACK, flagged as libcard flags its own words.                */
    (void)bw_pxi_arm7_send(BW_PXI_TAG_FS, REQ_ACK, 1);
}

void bw_arm7_fs_init(void)
{
    g_fs.requests = 0;
    g_fs.last = 0;
    g_fs.expect_arg = 0;
    g_fs.init_word = 0;
    g_fs.type = 0;                      /* a power-on identifies again      */
    g_fs.caps = 0;
    g_fs.status = 0;
    bw_pxi_arm7_set_callback(BW_PXI_TAG_FS, fs_callback);
}

extern void bw_slots_dropped(void) __attribute__((weak));

void bw_arm7_fs_reset(void)
{
    if (g_fs.hold) {
        /* a reset before the held new game was saved: nothing is written */
        g_fs.hold = 0;
        g_fs.dirty = 0;
    }
    (void)bw_arm7_backup_flush();
    /* the new game's slot: kept when its file is on the disk, else the
     * save in force before it */
    if (bw_slots_dropped != NULL)
        bw_slots_dropped();
    g_fs.type = 0;
    g_fs.expect_arg = 0;
}

void bw_arm7_fs_step(void)
{
    if (g_fs.dirty
        && difftime(time(NULL), g_fs.changed_at) >= (double)flush_delay())
        (void)bw_arm7_backup_flush();
}

/* ------------------------------------------------------------------ */
/* Interface                                                           */
/* ------------------------------------------------------------------ */

u32 bw_arm7_fs_requests(void)
{
    return g_fs.requests;
}

u32 bw_arm7_fs_last_request(void)
{
    return g_fs.last;
}

void bw_arm7_fs_bind(void *cmd_block_host, void *cache_buf, u32 cache_size)
{
    g_fs.cmd = (u8 *)cmd_block_host;
    g_fs.cache = (u8 *)cache_buf;
    g_fs.cache_size = cache_buf != NULL ? cache_size : 0;
}

u32 bw_arm7_fs_init_word(void)
{
    return g_fs.init_word;
}

void bw_arm7_backup_set_path(const char *path)
{
    (void)bw_arm7_backup_flush();
    g_fs.hold = 0;                      /* held writes are dropped          */
    if (path == NULL || strlen(path) >= sizeof g_fs.path) {
        g_fs.path_set = 0;
    } else {
        strcpy(g_fs.path, path);
        g_fs.path_set = 1;
    }
    /* The next IDENTIFY reads the new file.                                */
    free(g_fs.image);
    g_fs.image = NULL;
    g_fs.image_size = 0;
    g_fs.loaded = 0;
    g_fs.dirty = 0;
    g_fs.type = 0;
}

/* Another file while the game runs: the chip keeps its type and size, and
 * its contents are read again from the new file at once (save slots). */
void bw_arm7_backup_switch_path(const char *path)
{
    long n;

    (void)bw_arm7_backup_flush();
    g_fs.hold = 0;                      /* held writes are dropped          */
    if (path == NULL || strlen(path) >= sizeof g_fs.path) {
        g_fs.path_set = 0;
    } else {
        strcpy(g_fs.path, path);
        g_fs.path_set = 1;
    }
    if (g_fs.image != NULL && g_fs.image_size > 0) {
        memset(g_fs.image, 0xFF, g_fs.image_size);
        n = bw_save_read(save_path(), g_fs.image, g_fs.image_size);
        if (n >= 0 && (u32)n < g_fs.image_size)
            memset(g_fs.image + n, 0xFF, g_fs.image_size - (u32)n);
        g_fs.loaded = 1;
    }
    g_fs.dirty = 0;
}

/* While held, the chip's writes stay in memory and no file is written;
 * releasing writes what changed.  A path change while held drops it: a
 * new game left before its first save leaves no file behind. */
void bw_arm7_backup_hold(int on)
{
    g_fs.hold = on != 0;
    if (!g_fs.hold)
        (void)bw_arm7_backup_flush();
}

void bw_arm7_backup_set_flush_delay(int seconds)
{
    g_fs.flush_delay = seconds < 0 ? 0 : seconds;
    g_fs.flush_delay_set = 1;
}

const u8 *bw_arm7_backup_image(u32 *size)
{
    if (size != NULL)
        *size = g_fs.loaded ? g_fs.image_size : 0;
    return g_fs.loaded ? g_fs.image : NULL;
}

u32 bw_arm7_backup_type(void)
{
    return g_fs.type;
}

int bw_arm7_backup_dirty(void)
{
    return g_fs.dirty;
}

u32 bw_arm7_backup_writes(void)
{
    return g_fs.writes;
}

/* Failed file writes since the last one that worked: the save is only in
 * memory while this is not 0 (the interface warns the player). */
u32 bw_arm7_backup_failures(void)
{
    return g_fs.failures;
}
