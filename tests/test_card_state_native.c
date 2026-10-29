#include <stddef.h>
#include "card_common.h"
#include "port_test.h"

extern CARDiCommon cardi_common;
void bw_boot_card_state_reset(void);

static unsigned address, extent, registrations;
static void *backing;
void bw_ds_register(unsigned a, void *host, unsigned size, const char *name)
{
    (void)name;
    address = a;
    extent = size;
    backing = host;
    ++registrations;
}

int main(void)
{
    CARDiCommandArg command;
    unsigned i;
    unsigned char *bytes = (unsigned char *)&cardi_common;
    BW_CHECK_EQ("wire command remains 96 bytes", sizeof(command), 96);
    BW_CHECK_EQ("wire source offset", offsetof(CARDiCommandArg, src), 12);
    BW_CHECK_EQ("wire destination offset", offsetof(CARDiCommandArg, dst), 16);
    BW_CHECK_EQ("wire backup spec offset", offsetof(CARDiCommandArg, spec), 24);
    BW_CHECK_EQ("native command pointer first", offsetof(CARDiCommon, cmd), 0);
    for (i = 0; i < sizeof(cardi_common); ++i)
        bytes[i] = 0xA5;
    bw_boot_card_state_reset();
    for (i = 0; i < sizeof(cardi_common); ++i)
        BW_CHECK_EQ("native state cleared", bytes[i], 0);
    BW_CHECK_EQ("registered DS address", address, 0x02150200u);
    BW_CHECK_EQ("registration preserves DS extent", extent, 0x520);
    BW_REQUIRE("registration uses separate native storage", backing == &cardi_common);
    cardi_common.cmd = &command;
    cardi_common.flag = 1;
    bw_boot_card_state_reset();
    BW_REQUIRE("restart clears native command", cardi_common.cmd == NULL);
    BW_CHECK_EQ("restart clears native flag", cardi_common.flag, 0);
    BW_CHECK_EQ("registered each run", registrations, 2);
    BW_REPORT("test_card_state_native");
}
