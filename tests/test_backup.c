/*
 * tests/test_backup.c -- the save chip (PXI tag 11), the firmware flash
 * (tag 4), the microphone (tag 9) and the absent wireless (tag 10), from the
 * ARM9's side.
 *
 * The machine is the boot's (os_test_boot: bw_port_reset, the frame loop,
 * the OS layer), and the PXI is brought up by the matched PXI_Init.  Two
 * kinds of check:
 *
 *   - on the wire, at both pointer widths: words built by hand from the
 *     protocols sdk/arm7/fs.c, nvram.c and mic.c document, sent with the
 *     matched PXI_SendWordByFifo, answered by the responders, delivered to a
 *     recording ARM9 callback.  The buffers are the ones the responders are
 *     bound to, which is how they are found on a 64-bit host.
 *   - through the game's own SDK code, on a 32-bit host only: aletheia's
 *     matched CARD (CARD_IdentifyBackup, CARDi_RequestStreamCommand and the
 *     task, request and callback layer under them), NVRAM (init, read,
 *     write, verify and their callbacks, including the alarm-driven status
 *     polling of a write), MIC (init, start and stop) and WM (WM_Init's
 *     core) files, compiled as written.  Those files keep buffer addresses
 *     in u32 fields and words, which only a 32-bit host can do
 *     (docs/pointer-width.md); the CMake entry compiles them only there.
 *
 * The save files go to test_backup_saves/ in the working directory (the
 * build tree under ctest); the player's own save folder is never touched.
 * With BW_SAVE set to a real 512 KiB .sav, a last check identifies it and
 * reads it back byte for byte (skipped otherwise).
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "arm7/arm7.h"
#include "firmware.h"
#include "os_test.h"
#include "port_test.h"
#include "pxi.h"
#include "save.h"

#define SAVE_DIR "test_backup_saves"
#define BW_TYPE  0x00FF1302u        /* Pokemon Black: 512 KiB flash         */
#define CHIP     0x80000u

/* Matched ARM9 SDK code, declared as the matched files do.                 */
typedef void (*Arm9Cb)(int tag, u32 data, BOOL err);
extern int  PXI_SendWordByFifo(int fifotag, u32 data, BOOL err);
extern void PXI_SetFifoRecvCallback(int fifotag, Arm9Cb callback);
extern BOOL func_020885EC(int fifotag, int proc);      /* PXI_IsCallbackReady */
extern void func_02088498(void);                        /* PXI_Init           */

/* tests/backup_doubles.c */
void bw_test_doubles_init(void);

#if BW_TEST_CARD_MATCHED
extern union { u8 b[2048]; void *p; u32 w; u64 d; } cardi_common;
extern union { u8 b[0x60]; u64 align; } cardi_arg;
extern u8 CARDi_backup_cache_page_buf[256];
extern u32 nvramTemp[8];
/* The 0x02150EA0 section base, which aletheia now spells as a struct whose
 * nvramTemp member is at +0x20; the ARM7's temp buffer is that member. */
struct NvramBss { u32 nvramInitialized; u8 pad_4[0x1C]; u32 nvramTemp[8]; };
extern struct NvramBss nvramBss;
extern void CARDi_InitCommand(void);                        /* CARDi_InitCommand  */
extern BOOL CARD_IdentifyBackup(int type);                    /* CARD_IdentifyBackup */
extern BOOL CARDi_RequestStreamCommand(u32 src, u32 dst,
                          u32 len, void (*cb)(void *), void *arg,
                          BOOL async, int req_type, int req_retry,
                          int req_mode);        /* CARDi_RequestStreamCommand */
typedef void (*NvCb)(int result, void *arg);
extern void NVRAMi_Init(void);                        /* NVRAMi_Init        */
extern int  NVRAMi_IsInitialized(void);                 /* backup_doubles.c   */
extern int  NVRAMi_ReadAsync(u32 a, u32 n, void *p, NvCb cb,
                          void *arg);                   /* NVRAMi_ReadAsync   */
extern int  NVRAMi_WriteAsync(u32 a, u32 n, void *p, NvCb cb,
                          void *arg);                   /* NVRAMi_WriteAsync  */
extern int  NVRAMi_VerifyAsync(u32 a, u32 n, void *p, NvCb cb,
                          void *arg);                   /* NVRAMi_VerifyAsync */
typedef struct {
    int type;
    void *buffer;
    u32 size;
    u32 rate;
    BOOL loop_enable;
    void (*full_callback)(int result, void *arg);
    void *full_arg;
} MicAutoParam;
extern void MIC_Init(void);                        /* MIC_Init           */
extern int  MIC_StartAutoSamplingAsync(const MicAutoParam *param,
                          void (*cb)(int result, void *arg), void *arg);
extern int  MIC_StopAutoSamplingAsync(void (*cb)(int result, void *arg), void *arg);
extern int  WmInitCore(void *buf, u16 dma, u32 size); /* WmInitCore */
#endif

/* ------------------------------------------------------------------ */
/* The recording ARM9 callback                                         */
/* ------------------------------------------------------------------ */

#define REC_MAX 64
static struct { int tag; u32 data; int err; } g_rec[REC_MAX];
static int g_nrec;

static void recorder(int tag, u32 data, BOOL err)
{
    if (g_nrec < REC_MAX) {
        g_rec[g_nrec].tag = tag;
        g_rec[g_nrec].data = data;
        g_rec[g_nrec].err = err ? 1 : 0;
    }
    g_nrec++;
}

static void remove_saves(void)
{
    remove(SAVE_DIR "/pokeblack.sav");
    remove(SAVE_DIR "/pokeblack.sav.tmp");
    remove(SAVE_DIR "/firmware.bin");
    remove(SAVE_DIR "/other.sav");
    remove(SAVE_DIR "/short.sav");
}

static long file_size(const char *path)
{
    FILE *f = fopen(path, "rb");
    long n;

    if (f == NULL)
        return -1;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fclose(f);
    return n;
}

static int file_equals(const char *path, const u8 *want, u32 size)
{
    static u8 buf[CHIP + 4096];
    long n = bw_save_read(path, buf, sizeof buf);

    return n == (long)size && memcmp(buf, want, size) == 0;
}

/* ------------------------------------------------------------------ */
/* The save chip on the wire                                           */
/* ------------------------------------------------------------------ */

static union { u8 b[0x60]; u64 align; } g_cmd;
static u8 g_cache[256];

static u32 cmd_get(int off)
{
    u32 v;
    memcpy(&v, g_cmd.b + off, 4);
    return v;
}

static void cmd_set(int off, u32 v)
{
    memcpy(g_cmd.b + off, &v, 4);
}

/* One CARDi_Request as the matched one sends it; the block's result.      */
static u32 card_request(int req)
{
    g_nrec = 0;
    cmd_set(0x00, 0xEEEEEEEEu);
    (void)PXI_SendWordByFifo(BW_PXI_TAG_FS, (u32)req, 1);
    return cmd_get(0x00);
}

static u32 card_stream(int req, u32 src, u32 dst, u32 len)
{
    cmd_set(0x0C, src);
    cmd_set(0x10, dst);
    cmd_set(0x14, len);
    return card_request(req);
}

static u32 cache_addr(void)
{
    return (u32)(uintptr_t)g_cache;
}

static void test_card_wire(void)
{
    const u8 *img;
    u32 size, i, caps;
    int ok;

    bw_arm7_fs_bind(g_cmd.b, g_cache, sizeof g_cache);
    bw_arm7_backup_set_path(NULL);
    bw_arm7_backup_set_flush_delay(0);
    PXI_SetFifoRecvCallback(BW_PXI_TAG_FS, recorder);

    /* Initialise: two flagged words, one flagged ACK.                       */
    g_nrec = 0;
    (void)PXI_SendWordByFifo(BW_PXI_TAG_FS, 0, 1);
    BW_CHECK_EQ("card: init waits for the block address", g_nrec, 0);
    (void)PXI_SendWordByFifo(BW_PXI_TAG_FS, 0x1234, 1);
    BW_CHECK_EQ("card: init acknowledged", g_nrec, 1);
    BW_CHECK_EQ("card: ACK data", g_rec[0].data, 1);
    BW_CHECK_EQ("card: ACK flagged", g_rec[0].err, 1);
    BW_CHECK_EQ("card: block address kept", bw_arm7_fs_init_word(), 0x1234);

    /* Nothing identified yet.                                               */
    BW_CHECK_EQ("card: read before identify",
                card_stream(6, 0, cache_addr(), 1), 6);

    /* The spec CARDi_IdentifyBackupCore2 writes for 0x00FF1302.            */
    memset(g_cmd.b, 0, sizeof g_cmd.b);
    caps = 0x3F | 0x2000 | 0x40 | 0x100 | 0x200 | 0x800 | 0x80 | 0x400;
    cmd_set(0x04, BW_TYPE);
    cmd_set(0x18, CHIP);
    cmd_set(0x1C, 0x10000);
    cmd_set(0x24, 0x100);
    cmd_set(0x58, caps);
    BW_CHECK_EQ("card: identify", card_request(2), 0);
    BW_CHECK_EQ("card: identified type", bw_arm7_backup_type(), BW_TYPE);
    img = bw_arm7_backup_image(&size);
    BW_CHECK_EQ("card: image size", size, CHIP);
    BW_CHECK_EQ("card: blank chip reads erased", img != NULL && img[0] == 0xFF
                && img[CHIP - 1] == 0xFF, 1);
    BW_CHECK_EQ("card: nothing written for a read-only boot",
                file_size(SAVE_DIR "/pokeblack.sav"), -1);

    /* Write across the end of the chip: the address wraps.                 */
    for (i = 0; i < 16; i++)
        g_cache[i] = (u8)(0xA0 + i);
    BW_CHECK_EQ("card: write", card_stream(7, cache_addr(), CHIP - 8, 16), 0);
    BW_CHECK_EQ("card: write lands", img[CHIP - 8] == 0xA0 && img[CHIP - 1] == 0xA7, 1);
    BW_CHECK_EQ("card: write wraps", img[0] == 0xA8 && img[7] == 0xAF, 1);
    BW_CHECK_EQ("card: verify", card_stream(9, cache_addr(), CHIP - 8, 16), 0);
    g_cache[3] ^= 0xFF;
    BW_CHECK_EQ("card: verify mismatch", card_stream(9, cache_addr(), CHIP - 8, 16), 1);
    BW_CHECK_EQ("card: file written whole", file_equals(SAVE_DIR "/pokeblack.sav",
                                                       img, CHIP), 1);
    BW_CHECK_EQ("card: no temporary left", file_size(SAVE_DIR "/pokeblack.sav.tmp"), -1);

    /* Read back through the bounce buffer.                                  */
    memset(g_cache, 0, sizeof g_cache);
    BW_CHECK_EQ("card: read", card_stream(6, CHIP - 8, cache_addr(), 16), 0);
    BW_CHECK_EQ("card: read data", g_cache[0] == 0xA0 && g_cache[15] == 0xAF, 1);

    /* Program clears bits only (flash).                                     */
    g_cache[0] = 0x0F;
    BW_CHECK_EQ("card: program", card_stream(8, cache_addr(), 0x100, 1), 0);
    g_cache[0] = 0xF3;
    (void)card_stream(8, cache_addr(), 0x100, 1);
    BW_CHECK_EQ("card: program ANDs", img[0x100], 0x03);

    /* Erase: page, then the 64 KiB sector around an address.               */
    BW_CHECK_EQ("card: erase page", card_stream(10, 0, 0x100, 0x100), 0);
    BW_CHECK_EQ("card: page erased", img[0x100], 0xFF);
    g_cache[0] = 0x00;
    (void)card_stream(7, cache_addr(), 0x12345, 1);
    BW_CHECK_EQ("card: erase sector", card_stream(11, 0, 0x10000, 0x10000), 0);
    ok = 1;
    for (i = 0x10000; i < 0x20000; i++)
        ok &= img[i] == 0xFF;
    BW_CHECK_EQ("card: sector erased", ok, 1);
    BW_CHECK_EQ("card: neighbour kept", img[0], 0xA8);

    /* What the spec does not offer, and what no request is.                */
    BW_CHECK_EQ("card: chip erase unsupported", card_request(12), 3);
    BW_CHECK_EQ("card: request 3 unsupported", card_request(3), 3);
    g_cache[0] = 0x55;
    BW_CHECK_EQ("card: read status", card_stream(13, 0, cache_addr(), 1), 0);
    BW_CHECK_EQ("card: status ready", g_cache[0], 0);

#if UINTPTR_MAX > 0xFFFFFFFFu
    /* A 64-bit host cannot reach memory it was given as 32 bits.            */
    BW_CHECK_EQ("card: unknown buffer refused",
                card_stream(6, 0, 0x10u, 4), 2);
#endif

    /* Delayed write: nothing on disk until the delay or a flush.           */
    bw_arm7_backup_set_flush_delay(30);
    g_cache[0] = 0x42;
    (void)card_stream(7, cache_addr(), 0x200, 1);
    BW_CHECK_EQ("card: change pending", bw_arm7_backup_dirty(), 1);
    bw_arm7_fs_step();
    BW_CHECK_EQ("card: not yet written", bw_arm7_backup_dirty(), 1);
    BW_CHECK_EQ("card: flush", bw_arm7_backup_flush(), 0);
    BW_CHECK_EQ("card: flushed", bw_arm7_backup_dirty(), 0);
    BW_CHECK_EQ("card: file has it", file_equals(SAVE_DIR "/pokeblack.sav",
                                                img, CHIP), 1);
    bw_arm7_backup_set_flush_delay(0);
}

/* An emulator's file: longer (a .dsv trailer) or shorter than the chip.     */
static void test_card_files(void)
{
    static u8 data[CHIP + 122];
    const u8 *img;
    u32 size, i;

    for (i = 0; i < sizeof data; i++)
        data[i] = (u8)(i * 7 + (i >> 9));
    BW_CHECK_EQ("files: write .dsv-shaped", bw_save_write_atomic(
                    SAVE_DIR "/other.sav", data, sizeof data), 0);
    bw_arm7_backup_set_path(SAVE_DIR "/other.sav");
    memset(g_cmd.b, 0, sizeof g_cmd.b);
    cmd_set(0x04, BW_TYPE);
    cmd_set(0x18, CHIP);
    cmd_set(0x1C, 0x10000);
    cmd_set(0x24, 0x100);
    cmd_set(0x58, 0x3FFFu);
    (void)card_request(2);
    img = bw_arm7_backup_image(&size);
    BW_CHECK_EQ("files: long file read to the chip size",
                size == CHIP && memcmp(img, data, CHIP) == 0, 1);

    BW_CHECK_EQ("files: write short", bw_save_write_atomic(
                    SAVE_DIR "/short.sav", data, 1000), 0);
    bw_arm7_backup_set_path(SAVE_DIR "/short.sav");
    (void)card_request(2);
    img = bw_arm7_backup_image(&size);
    BW_CHECK_EQ("files: short file kept", memcmp(img, data, 1000) == 0, 1);
    BW_CHECK_EQ("files: short file padded erased",
                img[1000] == 0xFF && img[CHIP - 1] == 0xFF, 1);
    bw_arm7_backup_set_path(NULL);
}

/* BW_SAVE: a real save, identified and read back byte for byte.            */
static void test_real_save(void)
{
    static u8 data[CHIP];
    const char *path = getenv("BW_SAVE");
    const u8 *img;
    u32 size;
    long n;

    if (path == NULL || path[0] == '\0') {
        printf("  skip real save (BW_SAVE not set)\n");
        return;
    }
    n = bw_save_read(path, data, CHIP);
    if (n < (long)CHIP) {
        printf("  FAIL BW_SAVE=%s is not a 512 KiB save\n", path);
        bw_failures++;
        return;
    }
    bw_arm7_fs_bind(g_cmd.b, g_cache, sizeof g_cache);
    bw_arm7_backup_set_flush_delay(3600);   /* read only: never write it   */
    bw_arm7_backup_set_path(path);
    memset(g_cmd.b, 0, sizeof g_cmd.b);
    cmd_set(0x04, BW_TYPE);
    cmd_set(0x18, CHIP);
    cmd_set(0x58, 0x3FFFu);
    (void)card_request(2);
    img = bw_arm7_backup_image(&size);
    BW_CHECK_EQ("real save: chip is the file", size == CHIP
                && memcmp(img, data, CHIP) == 0, 1);
    memset(g_cache, 0, sizeof g_cache);
    BW_CHECK_EQ("real save: read", card_stream(6, 0x100, cache_addr(), 256), 0);
    BW_CHECK_EQ("real save: read data", memcmp(g_cache, data + 0x100, 256), 0);
    BW_CHECK_EQ("real save: untouched", bw_arm7_backup_dirty(), 0);
    bw_arm7_backup_set_path(NULL);
    bw_arm7_backup_set_flush_delay(0);
}

/* ------------------------------------------------------------------ */
/* The firmware flash and the microphone on the wire                   */
/* ------------------------------------------------------------------ */

static union { u8 b[32]; u64 align; } g_temp;

#define SPI_START 0x02000000u
#define SPI_END   0x01000000u

static u32 spi_reply(int tag)
{
    int i;

    for (i = g_nrec - 1; i >= 0 && i < REC_MAX; i--) {
        if (g_rec[i].tag == tag)
            return g_rec[i].data;
    }
    return 0xFFFFFFFFul;
}

static void test_nvram_wire(void)
{
    u32 p = (u32)(uintptr_t)g_temp.b;
    u8 block[0x100];

    bw_arm7_nvram_bind(g_temp.b);
    bw_arm7_nvram_set_path(NULL);
    PXI_SetFifoRecvCallback(BW_PXI_TAG_NVRAM, recorder);

    /* Read 32 bytes of the header at 0x20 into the bound buffer.           */
    g_nrec = 0;
    (void)PXI_SendWordByFifo(4, SPI_START | 0x2300 | 0x00, 0);
    (void)PXI_SendWordByFifo(4, 0x00010000 | 0x0020, 0);
    (void)PXI_SendWordByFifo(4, 0x00020000, 0);
    (void)PXI_SendWordByFifo(4, 0x00030000 | 32, 0);
    (void)PXI_SendWordByFifo(4, 0x00040000 | (p >> 16), 0);
    (void)PXI_SendWordByFifo(4, SPI_END | 0x00050000 | (p & 0xFFFF), 0);
    BW_CHECK_EQ("nvram: one reply", g_nrec, 1);
    BW_CHECK_EQ("nvram: reply word", g_rec[0].data, SPI_START | SPI_END | 0xA300);
    BW_CHECK_EQ("nvram: user settings offset", g_temp.b[0] | (g_temp.b[1] << 8),
                0x3FE00 / 8);
    BW_CHECK_EQ("nvram: MAC in the header", g_temp.b[0x16],
                bw_firmware_settings()->mac[0]);

    /* The user settings block is the settings in force.                    */
    (void)PXI_SendWordByFifo(4, SPI_START | 0x2300 | 0x03, 0);
    (void)PXI_SendWordByFifo(4, 0x00010000 | 0xFE00, 0);
    (void)PXI_SendWordByFifo(4, 0x00020000, 0);
    (void)PXI_SendWordByFifo(4, 0x00030000 | 32, 0);
    (void)PXI_SendWordByFifo(4, 0x00040000 | (p >> 16), 0);
    (void)PXI_SendWordByFifo(4, SPI_END | 0x00050000 | (p & 0xFFFF), 0);
    bw_firmware_user_block(bw_firmware_settings(), 1, block);
    BW_CHECK_EQ("nvram: user block", memcmp(g_temp.b, block, 32), 0);

    /* Status register: ready; then write enable sets WEL.                  */
    g_nrec = 0;
    g_temp.b[0] = 0xEE;
    (void)PXI_SendWordByFifo(4, SPI_START | 0x2200 | (p >> 24), 0);
    (void)PXI_SendWordByFifo(4, 0x00010000 | ((p >> 8) & 0xFFFF), 0);
    (void)PXI_SendWordByFifo(4, SPI_END | 0x00020000 | ((p << 8) & 0xFF00), 0);
    BW_CHECK_EQ("nvram: status read", g_temp.b[0], 0);
    (void)PXI_SendWordByFifo(4, SPI_START | SPI_END | 0x2000, 0);
    BW_CHECK_EQ("nvram: WREN answered", spi_reply(4), SPI_START | SPI_END | 0xA000);
    (void)PXI_SendWordByFifo(4, SPI_START | 0x2200 | (p >> 24), 0);
    (void)PXI_SendWordByFifo(4, 0x00010000 | ((p >> 8) & 0xFFFF), 0);
    (void)PXI_SendWordByFifo(4, SPI_END | 0x00020000 | ((p << 8) & 0xFF00), 0);
    BW_CHECK_EQ("nvram: WEL set", g_temp.b[0], 0x02);
    (void)PXI_SendWordByFifo(4, SPI_START | SPI_END | 0x2100, 0);
    (void)PXI_SendWordByFifo(4, SPI_START | SPI_END | 0x7700, 0);
    BW_CHECK_EQ("nvram: unknown command", spi_reply(4),
                SPI_START | SPI_END | 0xF700 | 1);
}

static void test_mic_wire(void)
{
    u16 v;

    PXI_SetFifoRecvCallback(BW_PXI_TAG_MIC, recorder);
    g_nrec = 0;
    (void)PXI_SendWordByFifo(9, SPI_START | SPI_END | 0x4000, 0);
    BW_CHECK_EQ("mic: one-shot answered", spi_reply(9), SPI_START | SPI_END | 0xC000);
    memcpy(&v, bw_ds_ptr(0x02FFFF94u), 2);
    BW_CHECK_EQ("mic: silence, 8-bit", v, 0x80);
    (void)PXI_SendWordByFifo(9, SPI_START | SPI_END | 0x4001, 0);
    memcpy(&v, bw_ds_ptr(0x02FFFF94u), 2);
    BW_CHECK_EQ("mic: silence, 12-bit", v, 0x800);
    (void)PXI_SendWordByFifo(9, SPI_START | 0x4100, 0);
    (void)PXI_SendWordByFifo(9, SPI_END | 0x60000, 0);
    BW_CHECK_EQ("mic: sampling not available", spi_reply(9),
                SPI_START | SPI_END | 0xC100 | 3);
    (void)PXI_SendWordByFifo(9, SPI_START | SPI_END | 0x4200, 0);
    BW_CHECK_EQ("mic: stop answered", spi_reply(9), SPI_START | SPI_END | 0xC200);
}

/* ------------------------------------------------------------------ */
/* The game's own SDK code (32-bit hosts)                              */
/* ------------------------------------------------------------------ */

#if BW_TEST_CARD_MATCHED
static int g_card_callbacks;

static void card_done(void *arg)
{
    (void)arg;
    g_card_callbacks++;
}

static void test_card_matched(void)
{
    static u8 data[1000], back[1000];
    const u8 *img;
    u32 size, i;
    int ok;

    /* The ARM7 finds the game's objects by name.                            */
    bw_arm7_fs_bind(cardi_arg.b, CARDi_backup_cache_page_buf, 256);
    remove(SAVE_DIR "/pokeblack.sav");
    bw_arm7_backup_set_path(NULL);      /* the next IDENTIFY loads afresh   */
    bw_arm7_backup_set_flush_delay(0);

    CARDi_InitCommand();                    /* CARDi_InitCommand                */
    BW_CHECK_EQ("matched: command block bound",
                *(void **)cardi_common.b == (void *)cardi_arg.b, 1);
    BW_CHECK_EQ("matched: identify 0x00FF1302", CARD_IdentifyBackup((int)BW_TYPE), 1);
    BW_CHECK_EQ("matched: ARM7 saw the type", bw_arm7_backup_type(), BW_TYPE);
    BW_CHECK_EQ("matched: init word is the block's low 26 bits",
                bw_arm7_fs_init_word(),
                (u32)(uintptr_t)cardi_arg.b & 0x03FFFFFFu);
    img = bw_arm7_backup_image(&size);
    BW_CHECK_EQ("matched: 512 KiB", size, CHIP);

    for (i = 0; i < sizeof data; i++)
        data[i] = (u8)(i * 13 + 1);
    /* CARD_WriteAndVerifyBackup: request 7, mode 2 (arm9/func_020336B8.c). */
    g_card_callbacks = 0;
    BW_CHECK_EQ("matched: write and verify",
                CARDi_RequestStreamCommand((u32)(uintptr_t)data, 0x1F0, sizeof data,
                              card_done, NULL, 0, 7, 10, 2), 1);
    BW_CHECK_EQ("matched: callback fired", g_card_callbacks, 1);
    BW_CHECK_EQ("matched: chip has it", memcmp(img + 0x1F0, data, sizeof data), 0);
    BW_CHECK_EQ("matched: file has it",
                file_equals(SAVE_DIR "/pokeblack.sav", img, CHIP), 1);

    /* CARD_ReadBackup: request 6, mode 0 (arm9/func_020335E8.c).            */
    memset(back, 0, sizeof back);
    BW_CHECK_EQ("matched: read", CARDi_RequestStreamCommand(0x1F0, (u32)(uintptr_t)back,
                                               sizeof back, NULL, NULL, 0,
                                               6, 1, 0), 1);
    BW_CHECK_EQ("matched: read data", memcmp(back, data, sizeof back), 0);

    /* CARD_EraseBackupSector: request 11, mode 3, the sector size from the
     * spec through CARD_GetBackupSectorSize.                                */
    BW_CHECK_EQ("matched: erase sector", CARDi_RequestStreamCommand(0, 0, 0x10000, NULL,
                                                       NULL, 0, 11, 1, 3), 1);
    ok = 1;
    for (i = 0; i < 0x10000; i++)
        ok &= img[i] == 0xFF;
    BW_CHECK_EQ("matched: sector erased", ok, 1);

    /* An unsupported request reaches the caller as failure.                 */
    BW_CHECK_EQ("matched: chip erase refused", CARDi_RequestStreamCommand(0, 0, CHIP, NULL,
                                                             NULL, 0, 12, 1, 3), 0);
    BW_CHECK_EQ("matched: result unsupported", cardi_arg.b[0], 3);
}

static int g_nv_done, g_nv_result;

/* One frame, then the safe point the game's next frame would reach: the
 * tick advances, so the NVRAM write's status-polling alarm fires.         */
static void frame(void)
{
    eidos_frame_run_one(&g_os_test_loop);
    bw_os_poll();
}

static void nv_done(int result, void *arg)
{
    (void)arg;
    g_nv_done++;
    g_nv_result = result;
}

static int nv_wait(void)
{
    int n;

    for (n = 0; n < 400 && !g_nv_done; n++)
        frame();
    return g_nv_done;
}

static void test_nvram_matched(void)
{
    static u8 buf[0x200], data[300];
    u8 block[0x100];
    const u8 *img;
    u32 size, i;

    bw_arm7_nvram_bind(nvramBss.nvramTemp);
    bw_arm7_nvram_set_path(NULL);
    remove(SAVE_DIR "/firmware.bin");

    NVRAMi_Init();                    /* NVRAMi_Init: spins on tag 4      */
    BW_CHECK_EQ("nvram matched: initialised", NVRAMi_IsInitialized(), 1);

    g_nv_done = 0;
    BW_CHECK_EQ("nvram matched: read issued",
                NVRAMi_ReadAsync(0x3FE00, 0x100, buf, nv_done, NULL), 0);
    BW_CHECK_EQ("nvram matched: read done", nv_wait(), 1);
    BW_CHECK_EQ("nvram matched: read result", g_nv_result, 0);
    bw_firmware_user_block(bw_firmware_settings(), 1, block);
    BW_CHECK_EQ("nvram matched: user settings", memcmp(buf, block, 0x100), 0);

    /* A write across a page boundary: WREN, page write, status polled on an
     * alarm, WREN again for the next page, write disable.                   */
    for (i = 0; i < sizeof data; i++)
        data[i] = (u8)(0x30 + i);
    g_nv_done = 0;
    BW_CHECK_EQ("nvram matched: write issued",
                NVRAMi_WriteAsync(0x3FA80, sizeof data, data, nv_done, NULL), 0);
    BW_CHECK_EQ("nvram matched: write done", nv_wait(), 1);
    BW_CHECK_EQ("nvram matched: write result", g_nv_result, 0);
    img = bw_arm7_nvram_image(&size);
    BW_CHECK_EQ("nvram matched: flash has it",
                memcmp(img + 0x3FA80, data, sizeof data), 0);
    BW_CHECK_EQ("nvram matched: file written at write disable",
                file_equals(SAVE_DIR "/firmware.bin", img, size), 1);

    g_nv_done = 0;
    (void)NVRAMi_VerifyAsync(0x3FA80, sizeof data, data, nv_done, NULL);
    BW_CHECK_EQ("nvram matched: verify done", nv_wait(), 1);
    BW_CHECK_EQ("nvram matched: verify result", g_nv_result, 0);
    data[200] ^= 1;
    g_nv_done = 0;
    (void)NVRAMi_VerifyAsync(0x3FA80, sizeof data, data, nv_done, NULL);
    (void)nv_wait();
    BW_CHECK_EQ("nvram matched: verify mismatch", g_nv_result, 6);
}

static int g_mic_done, g_mic_result;

static void mic_done(int result, void *arg)
{
    (void)arg;
    g_mic_done++;
    g_mic_result = result;
}

static void test_mic_wm_matched(void)
{
    static union { u8 b[0x400]; u64 align[4]; } micbuf;
    static union { u8 b[0x2000]; u64 align[4]; } wmbuf;
    MicAutoParam param;

    MIC_Init();                    /* MIC_Init: spins on tag 9         */
    memset(&param, 0, sizeof param);
    param.type = 0;
    param.buffer = (void *)(((uintptr_t)micbuf.b + 31) & ~(uintptr_t)31);
    param.size = 0x200;
    param.rate = 0x1000;
    g_mic_done = 0;
    BW_CHECK_EQ("mic matched: start issued", MIC_StartAutoSamplingAsync(&param, mic_done, NULL), 0);
    BW_CHECK_EQ("mic matched: answered", g_mic_done, 1);
    BW_CHECK_EQ("mic matched: illegal status", g_mic_result, 5);
    g_mic_done = 0;
    BW_CHECK_EQ("mic matched: stop issued", MIC_StopAutoSamplingAsync(mic_done, NULL), 0);
    BW_CHECK_EQ("mic matched: stop answered", g_mic_result == 0 && g_mic_done == 1, 1);

    /* WM_Init's core: no wireless component, the SDK's own answer.          */
    BW_CHECK_EQ("wm matched: WM_ERRCODE_WM_DISABLE",
                WmInitCore((void *)(((uintptr_t)wmbuf.b + 31) & ~(uintptr_t)31),
                              0, 0x1000), 4);
}
#endif

int main(void)
{
    BwFirmwareSettings fw;

    bw_test_doubles_init();
    bw_set_save_dir(SAVE_DIR);
    remove_saves();
    bw_firmware_defaults(&fw);
    fw.mac[0] = 0x02;
    fw.mac[5] = 0x5A;
    bw_firmware_set(&fw);

    os_test_boot();
    func_02088498();                    /* PXI_Init: the ARM7 comes up      */
    BW_CHECK_EQ("boot: save chip served", func_020885EC(11, 1), 1);
    BW_CHECK_EQ("boot: firmware flash served", func_020885EC(4, 1), 1);
    BW_CHECK_EQ("boot: microphone served", func_020885EC(9, 1), 1);
    BW_CHECK_EQ("boot: wireless absent", func_020885EC(10, 1), 0);

    test_card_wire();
    test_card_files();
    test_nvram_wire();
    test_mic_wire();
#if BW_TEST_CARD_MATCHED
    test_card_matched();
    test_nvram_matched();
    test_mic_wm_matched();
#else
    printf("  skip matched CARD/NVRAM/MIC/WM (32-bit hosts only)\n");
#endif
    test_real_save();

    bw_arm7_fs_bind(NULL, NULL, 0);
    bw_arm7_nvram_bind(NULL);
    bw_firmware_set(NULL);
    BW_REPORT("test_backup");
}
