#include <stdint.h>
#include "port.h"
#include "dsmem.h"
#include "port_test.h"

int main(void)
{
    static u8 command[96];
    void *memory;
    bw_port_reset();
    memory = bw_ds_ptr(0x02030000u);
    bw_ds_register(0x021501A0u, command, sizeof(command), "wire test command");
    BW_CHECK_EQ("numeric backup offset", bw_ds_wire_addr(0x70000), 0x70000);
    BW_CHECK_EQ("null address", bw_ds_wire_addr(0), 0);
    BW_CHECK_EQ("backed native pointer", bw_ds_wire_addr((uintptr_t)memory), 0x02030000u);
    BW_CHECK_EQ("registered native pointer", bw_ds_wire_addr((uintptr_t)command), 0x021501A0u);
    BW_REQUIRE("registered pointer round trip",
               bw_ds_ptr(bw_ds_wire_addr((uintptr_t)(command + 28))) == command + 28);
    BW_REPORT("test_wire_address");
}
