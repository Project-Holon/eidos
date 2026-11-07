/*
 * sdk/wasm_layout.h -- the DS's struct layout in the wasm32 build, included
 * before every source of the module (CMakeLists.txt).
 *
 * The DS's compiler aligns a 64-bit member (long long, double) to 4 bytes,
 * and so does the i686 build (-mno-align-double); wasm32 aligns it to 8.
 * A struct with one (GdsPacketDownload's key, an OSTick, a tick counter in
 * a save record) would be laid out differently from the DS, and the game's
 * own checks say so.  #pragma pack(4) caps every member's alignment at 4,
 * which is the DS's rule: nothing smaller changes, and the game declares no
 * member aligned beyond 4.  The files that hand structs to the C library or
 * to SDL keep the natural layout (BW_WASM_NATIVE_LAYOUT, CMakeLists.txt:
 * the same list the i686 build compiles with -malign-double).
 *
 * The C library's own structs (struct timespec, struct stat: 64-bit
 * members the library itself was compiled with at 8) must keep their
 * layout, so in the port's own sources (BW_WASM_PORT_SOURCE, CMakeLists.txt)
 * their headers are included first, before the pack; a later #include of
 * them is a no-op.  The game's sources declare their own C library (MSL's
 * FILE, rand, struct tm) and include none of these.
 */

#ifndef BW_PORT_WASM_LAYOUT_H
#define BW_PORT_WASM_LAYOUT_H

#if (defined(__wasm__) || defined(__vita__)) && !defined(BW_WASM_NATIVE_LAYOUT)
#if defined(BW_WASM_PORT_SOURCE)
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#endif

#pragma pack(4)
#endif

#endif /* BW_PORT_WASM_LAYOUT_H */
