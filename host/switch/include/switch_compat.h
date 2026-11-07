/*
 * host/switch/include/switch_compat.h -- included before every host file of
 * the Switch build (-include): what the Switch's C library (devkitPro's
 * newlib on libnx) lacks or names otherwise.
 */
#ifndef HOST_SWITCH_COMPAT_H
#define HOST_SWITCH_COMPAT_H

/* No signals on Horizon: wasm2c's runtime saves no signal mask, so the
 * plain jumps are the same thing. */
#include <setjmp.h>
#define sigsetjmp(buf, save) setjmp(buf)
#define siglongjmp(buf, val) longjmp(buf, val)

#endif
