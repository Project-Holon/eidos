/*
 * sdk/boot/card.c -- CARDi_IdentifyBackupCore2 (card_backup.o, 0x020766D8)
 * for the boot.
 *
 * CARD_IdentifyBackup's body (src/matched/arm9_sdk/func_02076B2C.c) calls
 * it to describe the backup chip before the identify command runs.  The
 * vendor object keeps it static and aletheia matches it static
 * (src/matched/arm9_sdk/CARDi_IdentifyBackupCore2.c, compiled by nothing
 * else in the boot), so the port supplies it with external linkage
 * (sdk/boot/gx.c says why sdk/boot/ exists).  It fills the command block's
 * backup description from a CARDBackupType word:
 *
 *   type = device | size bit << 8 | vendor << 16; size = 1 << size bit.
 *   EEPROM 512 B, 8 KB, 64 KB, 128 KB: page 16/32/128/256 bytes, address
 *     width 1/2/2/3, program time 5/5/10/5 ms, sector = page, status 0xF0
 *     for the 512-byte part else 0;
 *   FLASH 256 KB..1 MB, 2 MB, 4 MB, 8 MB: write and erase timings per
 *     size (and per vendor at 8 MB), 64 KB sectors, 256-byte pages,
 *     address width 3, program time 5 ms;
 *   FRAM 8 KB or 32 KB: page = sector = size, address width 2;
 *   anything else: unsupported (the type reverts to NOT_USE and the
 *     command's result is CARD_RESULT_UNSUPPORTED).
 *
 * cardi_common's first field is the native command pointer. Its storage
 * lives in card_state.c; the shared command itself keeps its DS wire layout.
 */

#include <string.h>

#include "../dsmem.h"
#include "../port.h"

#define CARDI_COMMON 0x02150200u

#define CAPS_AVAILABLE       0x003Fu
#define CAPS_READ            0x0040u
#define CAPS_WRITE           0x0080u
#define CAPS_PROGRAM         0x0100u
#define CAPS_VERIFY          0x0200u
#define CAPS_ERASE_PAGE      0x0400u
#define CAPS_ERASE_SECTOR    0x0800u
#define CAPS_ERASE_CHIP      0x1000u
#define CAPS_READ_STATUS     0x2000u
#define CAPS_WRITE_STATUS    0x4000u
#define CAPS_ERASE_SUBSECTOR 0x8000u

#define DEVICE_EEPROM 1
#define DEVICE_FLASH  2
#define DEVICE_FRAM   3

#define CARD_RESULT_UNSUPPORTED 3

typedef struct BwCardBackupSpec {
    u32 total_size;
    u32 sect_size;
    u32 subsect_size;
    u32 page_size;
    u32 addr_width;
    u32 program_page;
    u32 write_page;
    u32 write_page_total;
    u32 erase_chip;
    u32 erase_chip_total;
    u32 erase_sector;
    u32 erase_sector_total;
    u32 erase_subsector;
    u32 erase_subsector_total;
    u32 erase_page;
    u8  initial_status;
    u8  padding1[3];
    u32 caps;
    u8  padding2[4];
} BwCardBackupSpec;

typedef struct BwCardCommandArg {
    int result;
    u32 type;
    u32 id;
    u32 src;
    u32 dst;
    u32 len;
    BwCardBackupSpec spec;
} BwCardCommandArg;

void CARDi_IdentifyBackupCore2(u32 type);

static BwCardCommandArg *card_cmd(void)
{
    return *(BwCardCommandArg **)bw_ds_ptr(CARDI_COMMON);
}

static int eeprom(BwCardBackupSpec *s, u32 size)
{
    switch (size) {
    case 0x000200:
        s->page_size = 0x10; s->addr_width = 1; s->program_page = 5;
        s->initial_status = 0xF0;
        break;
    case 0x002000:
        s->page_size = 0x20; s->addr_width = 2; s->program_page = 5;
        s->initial_status = 0x00;
        break;
    case 0x010000:
        s->page_size = 0x80; s->addr_width = 2; s->program_page = 10;
        s->initial_status = 0x00;
        break;
    case 0x020000:
        s->page_size = 0x100; s->addr_width = 3; s->program_page = 5;
        s->initial_status = 0x00;
        break;
    default:
        return 0;
    }
    s->sect_size = s->page_size;
    s->caps |= CAPS_READ | CAPS_PROGRAM | CAPS_VERIFY | CAPS_WRITE_STATUS;
    return 1;
}

static int flash(BwCardBackupSpec *s, u32 size, u32 vendor)
{
    switch (size) {
    case 0x040000:
    case 0x080000:
    case 0x100000:
        s->write_page = 25; s->write_page_total = 300;
        s->erase_page = 300; s->erase_sector = 5000;
        s->caps |= CAPS_WRITE | CAPS_ERASE_PAGE;
        break;
    case 0x200000:
        s->write_page = 23; s->write_page_total = 300;
        s->erase_sector = 500; s->erase_sector_total = 5000;
        s->erase_chip = 10000; s->erase_chip_total = 60000;
        s->initial_status = 0x00;
        s->caps |= CAPS_WRITE | CAPS_ERASE_PAGE | CAPS_ERASE_CHIP | CAPS_WRITE_STATUS;
        break;
    case 0x400000:
        s->erase_sector = 600; s->erase_sector_total = 3000;
        s->erase_subsector = 70; s->erase_subsector_total = 150;
        s->erase_chip = 23000; s->erase_chip_total = 800000;
        s->initial_status = 0x00;
        s->subsect_size = 0x1000;
        s->caps |= CAPS_ERASE_SUBSECTOR | CAPS_ERASE_CHIP | CAPS_WRITE_STATUS;
        break;
    case 0x800000:
        if (vendor == 0) {
            s->erase_sector = 1000; s->erase_sector_total = 3000;
            s->erase_chip = 68000; s->erase_chip_total = 160000;
            s->initial_status = 0x00;
            s->caps |= CAPS_ERASE_CHIP | CAPS_WRITE_STATUS;
        } else if (vendor == 1) {
            s->erase_sector = 1000; s->erase_sector_total = 3000;
            s->erase_chip = 68000; s->erase_chip_total = 160000;
            s->initial_status = 0x84;
            s->caps |= CAPS_ERASE_CHIP | CAPS_WRITE_STATUS;
        }
        break;
    default:
        return 0;
    }
    s->sect_size = 0x010000;
    s->page_size = 0x0100;
    s->addr_width = 3;
    s->program_page = 5;
    s->caps |= CAPS_READ | CAPS_PROGRAM | CAPS_VERIFY | CAPS_ERASE_SECTOR;
    return 1;
}

static int fram(BwCardBackupSpec *s, u32 size)
{
    if (size != 0x002000 && size != 0x008000) {
        return 0;
    }
    s->page_size = size;
    s->sect_size = size;
    s->addr_width = 2;
    s->initial_status = 0x00;
    s->caps |= CAPS_READ | CAPS_PROGRAM | CAPS_VERIFY | CAPS_WRITE_STATUS;
    return 1;
}

void CARDi_IdentifyBackupCore2(u32 type)
{
    BwCardCommandArg *p = card_cmd();
    BwCardBackupSpec *s = &p->spec;
    u32 size;
    u32 device;
    u32 vendor;
    int ok;

    memset(s, 0, sizeof *s);
    p->type = type;
    s->caps = CAPS_AVAILABLE | CAPS_READ_STATUS;
    if (type == 0) {
        return;
    }
    /* An ARM register shift of 32 or more gives 0, which no case accepts. */
    size = ((type >> 8) & 0xFFu) < 32u ? 1u << ((type >> 8) & 0xFFu) : 0u;
    device = type & 0xFFu;
    vendor = (type >> 16) & 0xFFu;
    s->total_size = size;
    s->initial_status = 0xFF;
    if (device == DEVICE_EEPROM) {
        ok = eeprom(s, size);
    } else if (device == DEVICE_FLASH) {
        ok = flash(s, size, vendor);
    } else if (device == DEVICE_FRAM) {
        ok = fram(s, size);
    } else {
        ok = 0;
    }
    if (!ok) {
        p->type = 0;
        s->total_size = 0;
        card_cmd()->result = CARD_RESULT_UNSUPPORTED;
    }
}
