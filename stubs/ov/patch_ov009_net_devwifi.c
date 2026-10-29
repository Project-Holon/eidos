/*
 * patches: src/game/ov009/net_devwifi.c GFL_NET_DEVWIFI_InitDwc
 *
 * GFL_NET_DEVWIFI_InitDwc is the Wi-Fi mode's initLibrary method in
 * GFLNetModeOps (src/game/arm9/net_types.h). The caller in GFL_NET_SetupHeaps
 * (src/game/arm9/net.c) calls ops->initLibrary(parentHeapId, zero) with two
 * arguments. Under WebAssembly call_indirect type checking, calling a 1-parameter
 * function via a 2-parameter function pointer traps with a signature mismatch.
 * Accepting both arguments matches the GFLNetModeOps.initLibrary signature.
 */

extern void DWC_RAPCOMMON_InitDwcWithNotice(int heapId);

void GFL_NET_DEVWIFI_InitDwc(unsigned int heapId, unsigned int zero)
{
    (void)zero;
    DWC_RAPCOMMON_InitDwcWithNotice((int)heapId);
}
