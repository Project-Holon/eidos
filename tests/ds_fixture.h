/* Pointers in synthetic DS records retain the game's four-byte layout. */
#ifndef EIDOS_TEST_DS_FIXTURE_H
#define EIDOS_TEST_DS_FIXTURE_H

#include <string.h>
#include "dsmem.h"

static u32 fixture_address(const void *pointer)
{
    static u32 next = 0x02700000u;
    u32 address;

    if (pointer == NULL) return 0;
    address = bw_ds_addr_of(pointer);
    if (address == 0) {
        address = next;
        next += 0x100u;
        /* Map the base token; recipes read the fixture's bytes directly. */
        bw_ds_register(address, (void *)pointer, 1, "UI fixture");
    }
    return address;
}

static void wp(void *record, u32 offset, const void *pointer)
{
    u32 address = fixture_address(pointer);
    memcpy((unsigned char *)record + offset, &address, sizeof(address));
}

#endif
