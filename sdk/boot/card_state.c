/* The native card library's thread, queues and callbacks grow with host
 * pointers. Keep its state outside the adjacent DS command/cache buffers. */
#if EIDOS_NATIVE_CARD_STATE
#include "card_common.h"
CARDiCommon cardi_common;
#else
/* The legacy matched tree declares the DS layout locally in each unit.
 * It uses this opaque, pointer-aligned storage on the 32-bit reference host. */
union BwLegacyCardState {
    void *alignment;
    unsigned char bytes[0x520];
} cardi_common;
#endif

extern void bw_ds_register(unsigned int address, void *host,
                           unsigned int size, const char *name);

void bw_boot_card_state_reset(void)
{
    unsigned char *bytes = (unsigned char *)&cardi_common;
    unsigned int i;
    for (i = 0; i < sizeof(cardi_common); ++i)
        bytes[i] = 0;
    bw_ds_register(0x02150200u, &cardi_common, 0x520u, "cardi_common");
}
