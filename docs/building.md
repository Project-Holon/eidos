# Building

## Building

Requirements: CMake 3.16+, a C99 compiler (GCC, Clang or MSVC), Python 3.11+
for the fixture generator, a sibling checkout of aletheia, and an
`extract/` (in the aletheia checkout, by default) produced from your own
ROM. The standard library is the only hard dependency; SDL3 is optional and
is needed only for `engine/platform/` and the `eidos_view` demo.

**SDL3 without an installer.** `tools/fetch_sdl.py` downloads SDL
**3.4.18** from the official libsdl-org GitHub release, verifies it against
a SHA-256 recorded in the script (the same values as the asset digests
GitHub publishes for that release), and unpacks it into a per-user cache
**outside the repository**: `%LOCALAPPDATA%\eidos\sdl` on Windows,
`~/Library/Caches/eidos/sdl` on macOS, `$XDG_CACHE_HOME/eidos/sdl` (or
`~/.cache/eidos/sdl`) elsewhere, or `EIDOS_SDL_CACHE` when set. Nothing is
ever written into the checkout, and every checkout and git worktree on the
machine shares the one download. Standard library only. It knows three
flavours of the same release:

| flavour | archive | for |
| --- | --- | --- |
| `mingw` | `SDL3-devel-3.4.18-mingw.tar.gz` | MinGW-w64, x64 and x86 |
| `vc` | `SDL3-devel-3.4.18-VC.zip` | `cl.exe` and `clang-cl`, x64 and x86 |
| `source` | `SDL3-3.4.18.tar.gz` | anything else: built with CMake, with this build's own compiler, flags and toolchain file, into a prefix named after them |

```
python tools/fetch_sdl.py                 # the host's default (mingw on Windows, source elsewhere)
python tools/fetch_sdl.py --flavor vc     # one flavour; --flavor all for every one
python tools/fetch_sdl.py --check         # report what the cache holds
```

**How CMake finds it** (`cmake/sdl3.cmake`): it asks the script for the
flavour this toolchain links (`vc` under MSVC and clang-cl, `mingw` under
MinGW-w64, `source` otherwise; `-DEIDOS_SDL_FLAVOR=` overrides) at this
build's pointer width, which never touches the network; then an older
fetch's `third_party/SDL3-*` inside the checkout, if one is there; then an
SDL3 CMake package (a system package, Homebrew, vcpkg, or `-DSDL3_DIR`);
then pkg-config's `sdl3.pc`. With none of them it says so and skips the
platform layer and the demo: **every other target still builds, and every
test but `test_view` still runs.** `-DEIDOS_SDL=OFF` forces that path, which
is how the no-SDL build is checked. On Windows `SDL3.dll` is copied next to
`eidos_view.exe` after it links.

**`-DEIDOS_SDL_FETCH=ON`** adds a last step: when nothing was found, run
the script at configure time (download and verify, and for the `source`
flavour build and install), then look in the cache again. It is **off by
default** on purpose. A configure that reaches the network on its own is
wrong exactly where reproducibility matters: distribution packaging and CI
sandboxes forbid it, an offline machine would wait out a five-minute
timeout, and what a build links should not depend on whether a download
happened to succeed. The explicit fetch is one command, and after it the
cache is found automatically. `tools/check.py` turns it on for the Windows
configurations, so the whole matrix is one command on a fresh machine.

The version and the hashes are pinned in the script rather than resolved at
run time, because a recorded hash is the only part of a download a
compromised mirror cannot satisfy; upgrading is changing the version and
the three hashes (`--print-sha FILE` prints one) and re-running. An
unpacked tree is used only if it carries the stamp the script writes after a
verified, complete extraction, so an interrupted one is never picked up.

```
# in the aletheia checkout:
python tools/ndsrom.py extract "<your-rom>.nds" -o extract

# in this repository:
python tools/fetch_sdl.py                 # Windows; on Linux/macOS install the package instead
python tools/fetch_wamr.py                # optional: the runtime wasm code mods run in
cmake -S . -B build -DALETHEIA_ROOT=../aletheia
cmake --build build
ctest --test-dir build --output-on-failure
```

### The wasm runtime

Code mods built for WebAssembly (docs/mod-code-wasm.md) run in WAMR 2.2.0,
the interpreter only. `python tools/fetch_wamr.py` downloads the release,
checks its SHA-256, unpacks it into the per-user cache (the same places as
SDL's, under `eidos/wamr`; `EIDOS_WAMR_CACHE` or `--cache` names another)
and makes three small changes: two that MinGW needs, and one that keeps
a module's memory as the module declared it (WAMR otherwise fixes the size
of a module that never grows its memory, and the port adds a page to every
module).  An older unpack is patched when the script runs again; CMake
stops with a message when the tree lacks the third change. CMake finds it there
(`-DEIDOS_WAMR_ROOT=` names another source folder) and builds it as one
static library, `eidos_wamr`, with the host's own struct layout. The
configure line says `eidos: wasm code mods on` or `off`. Without it, or with
`-DEIDOS_WASM=OFF`, the port builds and runs, and a wasm mod is refused with
a message. No assembler is needed: native calls go through WAMR's portable
C path.

### SDL3 on every platform

None of the following can be tested on the Windows machine this port is
developed on; the package names are the distributions' own.

**Linux.** The development package, found through its CMake package or
pkg-config:

| distribution | package |
| --- | --- |
| Debian 13, Ubuntu 25.04 and later | `libsdl3-dev` |
| Fedora 42 and later | `SDL3-devel` |
| Arch | `sdl3` |
| openSUSE Tumbleweed | `SDL3-devel` |
| Alpine 3.21 and later | `sdl3-dev` |

Older releases (Ubuntu 22.04 and 24.04 among them) have no SDL3 package:
configure with `-DEIDOS_SDL_FETCH=ON` and SDL3 is built from its source
release (below). The configure-time link probe reports an SDL3 that does
not link for this target as not found rather than failing half way
through the build.

**macOS.** `brew install sdl3` (Homebrew) or `sudo port install libsdl3`
(MacPorts). Homebrew installs both a CMake package and `sdl3.pc`; if CMake
does not search Homebrew's prefix (`/opt/homebrew` on Apple silicon),
configure with `-DCMAKE_PREFIX_PATH=$(brew --prefix)`. Only 64-bit builds
exist on current macOS, so the ILP32 configuration is not available there.

For the native core, SDL platform layer, Cocoa ROM picker and synthetic
tests on Apple silicon:

```
cmake -S . -B build-macos -G Ninja \
    -DCMAKE_PREFIX_PATH="$(brew --prefix)" \
    -DALETHEIA_ROOT=../aletheia -DEIDOS_BOOT=OFF
cmake --build build-macos --parallel 4
ctest --test-dir build-macos --output-on-failure --parallel 4
```

The `macOS core` workflow checks this configuration without a ROM. The
real title-overlay tests are disabled by default for the `game` source tree
with 64-bit pointers: the pinned `startmenu_types.h` asserts DS structure
sizes. `-DEIDOS_REAL_OVERLAY_TESTS=ON` explicitly requests those tests;
synthetic overlay tests remain enabled at both pointer widths.

The Mach-O executable link helper (`tools/macos_link.py`) reads optional
weak references from the object files and static archives and passes them
to Apple ld with per-symbol `-U` options. Ordinary missing symbols still
fail the link. This lets core tools run without the game's optional entry
points, just as they do with ELF weak references.

This configuration is separate from the full game release, which remains gated by
`EIDOS_BUILD_64BIT` in the release workflow. The game's 64-bit pointer
layout work is still required for reliable gameplay (see
`docs/pointer-width.md`). Apple ld uses `-map` for the boot map and does
not implement GNU ld's `--wrap`: the custom keyboard prompt, queued UI
sound hook, instant text override and battle backdrop wrappers are disabled
on macOS, as they are on MSVC. The underlying game routines remain linked.

**The source fallback**, on any host with CMake and a C compiler:
configure eidos with `-DEIDOS_SDL_FETCH=ON`. The script downloads the
source release, verifies it, and builds it with the compiler, C flags,
toolchain file and generator of the eidos build that asked, into
`SDL3-3.4.18-<system>-<compiler>-<arch>-<hash>/` in the cache, so a GCC
build, a Clang build and an `-m32` build each get their own. On Linux, SDL
builds the video and audio back ends whose development headers are
installed (X11 or Wayland, ALSA, PulseAudio or PipeWire); with none of them
it still builds, with only the dummy drivers, which is enough for
`test_view` but opens no window.

**Android and iOS.** Not built yet; the route is SDL3's own templates.
The SDL3 source release carries `android-project/` (a Gradle project whose
`SDLActivity` loads the game as `libmain.so`, built with the NDK through
CMake or ndk-build) and `Xcode/` (the iOS template and SDL's own
`SDL.xcodeproj`). Three things differ from the desktop build:

- **The entry point.** `engine/platform/platform.c` defines
  `SDL_MAIN_HANDLED` and eidos owns `main()`. On Android and iOS SDL owns
  the process entry (`SDLActivity` calls `SDL_main` in `libmain.so`; on iOS
  SDL runs `UIApplicationMain` and calls `SDL_main` from it), so a mobile
  build compiles the demo's `main` as `SDL_main` through `SDL_main.h` and
  leaves `SDL_MAIN_HANDLED` undefined for that target.
- **The data root.** No command line and no arbitrary paths: the player's
  `extract/` goes under `SDL_GetPrefPath()` (Android: the app's internal
  files directory, iOS: the app's sandbox), which the first-run extractor
  in `install/` would write to.
- **Input.** The DS keypad has no keyboard to come from; the touch screen
  maps to SDL's touch events (`SDL_EVENT_FINGER_DOWN` and friends) on the
  bottom screen's rectangle, and the buttons to an on-screen overlay or a
  gamepad (`SDL_Gamepad`).

**Without game data.** The fixtures and the four oracles are generated from
the extracted ROM at build time, so a tree with no `extract/` used to fail
its build at the first of them. Configure now checks whether the data root
holds a NitroFS tree and sets `EIDOS_DATA_TESTS` from the answer. With no
data the whole library, the demo and every test executable that does not
compile a generated header in still build, the seven synthetic tests run,
and the seven data-driven ones (`test_text`, `test_stats`, `test_g2d`,
`test_bg`, `test_g3d`, `test_audio`, `test_view`) are registered as
disabled, so ctest lists them as `Not Run (Disabled)` rather than dropping
them from the count. `-DEIDOS_DATA_TESTS=ON` forces the data path.

Configure also compiles and links a one-line SDL3 program before it trusts
an SDL3 it found. On a 64-bit Linux host configured with the ILP32
toolchain file, pkg-config reports the 64-bit SDL3, which does not build
under `-m32`; the probe turns that into "SDL3 not found for this target"
and the platform layer is skipped, instead of a compile error half way
through the build. libm is asked for the same way, by linking a call to
`pow` with `-lm` for the target being built, because `find_library` answers
for the host's default ABI and under `-m32` found no libm at all.

`ALETHEIA_ROOT` defaults to `../aletheia` (a sibling checkout next to this
repository) and is documented in `CMakeLists.txt`; pass it explicitly when
your aletheia checkout lives somewhere else, for example
`-DALETHEIA_ROOT=../bw_decomp` for a checkout that still carries its old
folder name. `BW_DATA_ROOT` defaults to `${ALETHEIA_ROOT}/extract/data` and
is resolved against `ALETHEIA_ROOT` when given as a relative path. The
tests also accept the data root as `argv[1]` and fall back to the
`BW_DATA_ROOT` environment variable, so they can be run by hand.

If CMake is not available, `tools/hostbuild.py` builds the same source
lists (read straight out of `CMakeLists.txt`, so the two cannot drift) with
whatever compiler it can find:

```
python tools/hostbuild.py --probe               # report what is installed
python tools/hostbuild.py --aletheia-root ../aletheia            # native build and run
python tools/hostbuild.py --aletheia-root ../aletheia --check    # cross compile and link only
```

`--check` uses the `arm-none-eabi-gcc` already in aletheia's
`tools/gcc-arm/`. It proves every source compiles clean and that the symbol
closure is complete, because the link fails on the first missing name. It
cannot run the tests: what comes out is an ARM ELF and there is no runner
for it. It was the port's only LP32 build until
`cmake/ilp32-toolchain.cmake` arrived; the i686 configuration does the same
thing for pointer width and can also run what it builds, so `--check` is now
a second compiler's opinion rather than the only 32-bit evidence there is.
Current state: 69 objects compiled clean, `test_text`, `test_stats` and
`test_tcbl` linked.

### Checking every configuration at once

```
python tools/check.py                 # every configuration this machine can build
python tools/check.py --only gcc-ilp32 asan-ilp32
python tools/check.py --data-root ../aletheia/extract/data
```

configures, builds and tests each configuration in its own directory and
fails on a build error, a failed test **or a single compiler warning**:
GCC and Clang at both pointer widths, AddressSanitizer + UBSan at both, and
the no-SDL build. A configuration whose compiler or 32-bit runtime is not
installed is reported as skipped with the reason. On this tree, with no
game data (Linux, GCC 13.3, Clang 18.1): all seven ok, 0 warnings, 9 of 9
enabled tests passing in each, the data-driven ones disabled.

**Windows** is in the matrix too, cross-compiled with MinGW-w64
(`mingw-x64`, `mingw-x86`); with Wine installed the x64 tests run as real
`.exe` files through `CMAKE_CROSSCOMPILING_EMULATOR`, and on this machine
all ten pass there, including `test_extract`'s oracle comparison. That run
found that `eidos_extract` took its paths from `argv`, which Windows gives
in the ANSI code page rather than UTF-8, so a ROM under a folder named with
an accent would not open; it now reads the wide command line. It also found
that `cmake/ilp32-toolchain.cmake` preferred `i686-w64-mingw32-gcc` on
every host, so a Linux machine with MinGW installed got a Windows cross
compiler for its "32-bit" build; the search is now split by host OS.
On a Windows host the two MinGW configurations are native builds and run
their tests directly. Every Windows configuration (`mingw-*`, `msvc-*`,
`clangcl-x64`) configures with `-DEIDOS_SDL_FETCH=ON` and fails if it still
has no SDL3, so the platform layer, `eidos_view` and `test_view` are part
of what "ok" means there; the summary's `sdl` column says which
configurations had it.

Two portability fixes came out of the first Clang run: `sdk/os_alloc.c`
computed alignment by subtracting a null pointer, which is undefined and
now goes through `uintptr_t`; and `cmake/ilp32-toolchain.cmake` used to add
`-m32` only when it picked the compiler itself, so naming one
(`-DCMAKE_C_COMPILER=clang`) silently produced a 64-bit build. A named
compiler now gets `-m32` too, and a configure that asked for ILP32 and got
8-byte pointers is a fatal error.

### Building with MSVC

MSVC's settings live in `cmake/msvc.cmake`, which `CMakeLists.txt` includes
whenever CMake reports `MSVC` (both `cl.exe` and `clang-cl`): `/W4 /utf-8`,
the CRT's Annex K deprecations turned off at their source
(`_CRT_SECURE_NO_WARNINGS`, `_CRT_NONSTDC_NO_WARNINGS`), the narrowing
warnings C4244/C4242/C4267 off because GCC and Clang only report those under
`-Wconversion`, which the port does not enable on any compiler, and the
matched sources' three suppressions spelled `/wd4100 /wd4101 /wd4189`
(`-Wno-...` under clang-cl), plus two that only `cl.exe` raises on them:
C4200 (`u8 data[0]`, mwccarm's flexible array member) and C4245 (`u32 x =
-1`, which GCC and Clang report only under `-Wsign-conversion`); the few
matched files with mwccarm's `#pragma unused` also get C4068 off, as they
get `-Wno-unknown-pragmas` under GCC. C4324 (a struct padded to a
`jmp_buf`'s 16-byte alignment) is off everywhere: it reports the padding
the alignment requires, and GCC and Clang have no such warning. Plain `char` is signed under MSVC already, so
nothing is passed for it; `/J`, which an earlier revision passed, makes it
*unsigned*, and `sdk/types.h` now fails the build if plain `char` is not
signed on any compiler.

The pointer width comes from the developer environment, not from a
toolchain file: `vcvarsall.bat x64` builds LP64 (LLP64, strictly), and
`vcvarsall.bat x64_x86` builds the 32-bit reference configuration, whose
executables run on the same machine. Do not pass
`cmake/ilp32-toolchain.cmake` with `cl.exe`.

```
"C:\Program Files\Microsoft Visual Studio\<version>\Community\VC\Auxiliary\Build\vcvarsall.bat" x64_x86
cmake -S . -B build-msvc32 -G Ninja -DALETHEIA_ROOT=..\aletheia -DEIDOS_SDL_FETCH=ON
cmake --build build-msvc32
ctest --test-dir build-msvc32 --output-on-failure
```

`tools/check.py` has the same three as `msvc-x64`, `msvc-x86` and
`clangcl-x64`: it finds Visual Studio with `vswhere`, loads `vcvarsall.bat`
into the child processes only, and skips each with the reason when it
cannot (not Windows, no Visual Studio, no C++ build tools, no clang-cl).
The build needs the Visual Studio components
`Microsoft.VisualStudio.Component.VC.Tools.x86.x64` (the compiler, its
headers and CRT, and `vcvarsall.bat`) and a Windows SDK (the UCRT headers
and `kernel32.lib`); clang-cl is
`Microsoft.VisualStudio.Component.VC.Llvm.Clang`.

**SDL3 under MSVC.** `cl.exe` and `clang-cl` link the `vc` flavour, the
official `SDL3-devel-3.4.18-VC.zip` (`SDL3.lib` and `SDL3.dll` for x64 and
x86); the MinGW import library does not link with `link.exe`. Run
`python tools/fetch_sdl.py --flavor vc` once, or configure with
`-DEIDOS_SDL_FETCH=ON`, and the platform layer, `eidos_view` and `test_view`
build in all three Visual Studio configurations. vcpkg
(`vcpkg install sdl3:x64-windows`, with its toolchain file) or
`-DSDL3_DIR=<somewhere>/cmake` also work, through the `find_package` step.

**No dialogs.** The Microsoft runtime reports `abort()`, a failed
`assert()`, an invalid CRT argument and every debug-CRT error with a modal
window that waits for a click, which stalls an unattended test run (several
tests abort on purpose through `tests/expect_panic.cmake`).
`sdk/plat_nodialog.c` is compiled into every executable of an MSVC or
clang-cl build (option `EIDOS_NO_CRT_DIALOGS`, on by default) and, from a
CRT initialiser before `main`, sends the debug CRT's reports to stderr,
turns off `abort()`'s message box and fault report, installs an
invalid-parameter handler that prints and exits, and sets
`SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX)` so a crash
is an exit code, not a Windows Error Reporting window. `tools/check.py`
sets the same error mode on itself on Windows, which every child inherits.

**Debug runtime.** The check configurations build `CMAKE_BUILD_TYPE=Debug`
(MSVC's default, stated explicitly in `check.py`), so they link the debug
CRT: it checks the heap's guard bytes on every `free` and fills new
allocations with `0xCD`, the closest thing these configurations have to the
sanitizer runs GCC and Clang get. With the dialogs off it costs nothing in
unattended runs.

**Status.** Built and tested on Visual Studio 2026 (18, MSVC 14.50, Windows
SDK 10.0.26100, clang-cl from the same installation): `msvc-x64`,
`msvc-x86` and `clangcl-x64` are all ok in `tools/check.py`, 0 warnings at
`/W4`, with the real game data: 34 of 34 tests on x64 under both compilers
and 35 of 35 on the x86 ILP32 reference (`test_snd_matched` is ILP32-only),
`test_extract_rom` skipped for want of a ROM in each. The first run's
warnings were all in port code or explained above: block-scope `extern`
function declarations (C4210, moved to file scope in `sdk/narc.c`,
`sdk/tcbl.c` and `tests/test_tcbl.c`), constant truncation in a `u16`
mask (C4310, `sdk/arm7/pm.c`), an uninitialised `static const` table
(C4132, `engine/g2d/hwcompose.c`) and a local the compiler could not prove
set (C4701, `engine/g3d/nsbmd.c`).

## Pointer width, and the reference configuration

**ILP32 is the reference configuration for eidos until the field code is in.
LP64 keeps building and keeps passing every test.** `docs/pointer-width.md`
is the measurement this is decided on and states the trade-offs; the short
form is that on a 32-bit target every struct the game's layout owns has the
DS's own bytes, and that 14,301 of the 33,541 matched sources (42.6%) carry
at least one assumption that a pointer is four bytes.

```
cmake -S . -B build32 -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE=cmake/ilp32-toolchain.cmake \
    -DALETHEIA_ROOT=../aletheia
cmake --build build32
ctest --test-dir build32 --output-on-failure
```

`cmake/ilp32-toolchain.cmake` looks for an i686 compiler driver, first in
aletheia's `tools/gcc-host/` (the MinGW-w64 archive ships
`i686-w64-mingw32-gcc` next to the x86_64 one, so `-DALETHEIA_ROOT` is the
whole of what it needs) and then on PATH, and falls back to `-m32` on the
default compiler. It does not set `CMAKE_SYSTEM_NAME`: this is a same-OS,
different-ABI build and ctest runs the executables exactly as it does the
64-bit ones. SDL3 is not a problem either, because the MinGW devel package
`tools/fetch_sdl.py` unpacks carries an `i686-w64-mingw32` tree beside the
64-bit one (and the VC package an `x86` library beside the `x64` one), and
`cmake/sdl3.cmake` picks by pointer width;
`-DEIDOS_SDL=OFF` is there for a host with no 32-bit SDL3.

`CMakeLists.txt` then reads `CMAKE_SIZEOF_VOID_P` and decides one thing:

| `EIDOS_TCBL_MATCHED` | default on | the nine tcbl sources come from |
| --- | --- | --- |
| ON | a 32-bit target | `${ALETHEIA_ROOT}/src/matched/`, compiled as written |
| OFF | a 64-bit target | `stubs/reimpl_` |

Turning it on for a target whose pointers are 8 bytes is a configure-time
fatal error, not a link that silently truncates addresses.

Measured on this machine, same tree, same tests:

| configuration | node / manager | the nine tcbl sources | ctest | test_tcbl |
| --- | --- | --- | --- | --- |
| LP64 (default) | 40 / 88 bytes | `stubs/reimpl_` | 11/11 | 49 checks, 0 failures |
| ILP32 | 24 / 56 bytes | matched, compiled as written | 11/11 | 49 checks, 0 failures |

24 and 56 are `0x18` and `0x38`. `tests/test_tcbl.c` asserts both layouts
rather than one, and on ILP32 it additionally asserts that
`EIDOS_TCBL_HEADER_BIAS` is zero, which is the port's one piece of arithmetic
that exists only to pay for pointer width.

**The exit condition is a number, and it is tracked.** `python
tools/closure.py --pointer-audit` counts the matched sources that hold an
address in an integer, hardcode a struct size or field offset, or cast
between an integer and a pointer. The largest category, 4,766 files, is
explicit padding (`u8 pad[0x14]; u32 active;`), which is not a rule violation
to be corrected but a field offset frozen into a source that does not yet
know what those bytes are. It falls as waves decompile the structs around it,
and when it is small enough to re-verify in a wave or two, LP64 becomes the
reference and ILP32 becomes the fidelity cross-check. That has to happen
before Milestone 9 in any case: macOS and iOS have no 32-bit.

## Portability rules

These are not style preferences. Breaking any of them either breaks the DS
build or makes the port quietly compute different numbers.

**1. A matched file may not change without the DS re-verify in the same
commit, in the aletheia repository.** If a matched source genuinely has to
change to build on a host, the change is only allowed together with, in the
same commit, in aletheia:

```
python tools/build.py --split --pack
python tools/ndsrom.py build extract --override build/pack -o build/pokeblack.nds
python tools/ndsrom.py verify extract build/pokeblack.nds
```

and a PASS. The strong preference is not to touch `src/matched/` at all:
portability belongs in the port layer, as a shim, a wrapper include that
predeclares something before the matched file's own extern, or an alias
forwarder.

**2. 32-bit types are explicit.** The DS is LP32; the host may be LP64 or
LLP64. Port code uses `u8`/`u16`/`u32`/`s32` from `sdk/types.h`, which are
defined from `stdint`, never bare `int` or `long` for a value that has a
width in the game's data.

**3. Fixed point stays as it is.** `fx32` is 1.19.12 integer arithmetic, and
the game's numbers are decided by its truncation and rounding. Never convert
a fixed-point value through a float to "simplify" it; `FX_MUL` and `FX_DIV`
keep the 64-bit intermediate the DS runtime helper provides.

**4. Reads out of extracted files are endian-clean.** Every byte in
`extract/` is little-endian whatever the host is. Use `bw_rd16`/`bw_rd32`;
never cast a `const u8 *` into a `const u32 *` and dereference it, which is a
big-endian bug and an unaligned-access bug at once.

**5. No pointer may be stored in a struct the game's layout owns, in code
the port itself writes.** This is a rule about the shim, not about matched
code: a game-owned struct the game fills in with pointers (the task-callback
node) is the game's business, and the answer to it is the pointer width the
port is configured with, not a rule the port could impose. See "Pointer
width, and the reference configuration" above and `docs/pointer-width.md`.
Where the port owns the bytes it stores an index. The matched `struct
ArcFile` is `u8 file[0x48]; u32 imageBase; u16 fatOffset;
u16 count;`, four-byte aligned. The FS shim owns those 0x48 bytes and stores
an *index* into a table of open `FILE *`, not the pointer, so the struct
neither grows nor needs eight-byte alignment on a 64-bit host. `sdk/fs.c`
asserts the size at compile time.

**6. `char` is signed.** `-char signed` is part of the pinned DS compiler
configuration; the CMake build passes `-fsigned-char` to GCC and Clang so
the host agrees. MSVC's plain `char` is signed by default and gets no flag
(its `/J` would make it unsigned; see "Building with MSVC"). `sdk/types.h`
asserts the signedness at compile time, so a configuration that gets it
wrong fails to build.

## What is substituted, and why

`tests/test_stats.c` runs the real `func_02018A7C`, the real archive chain and
the real nature table, but supplies its own `func_02017E1C` (get field by id),
`func_02017E40` (set field by id) and `func_02018598` (nature) over a flat
id-indexed array. All three are matched; their closure reaches the Pokemon
record's encryption layer (`func_02018D10` / `func_02018D6C`) and the two
large field dispatchers, and building a valid encrypted party record is a
milestone of its own. The interface `func_02018A7C` uses is exactly those
three functions, so the formula under test is untouched by the substitution.
The next milestone closes it.

## Known type mismatches

Two definitions in `sdk/game_globals.c` deliberately differ from the matched
file's own declaration. Both are findings about the matched code, not
conveniences, and both are recorded in that file's header:

- `g_02146270` is declared in `src/matched/unit_02019C54.c` with a 0x10-byte
  payload, but `src/matched/unit_0201ADA4.c` writes 0x3C bytes into it (the
  personal record's size). The declaration is an under-estimate that is
  harmless on the DS, where the object is a global with the rest of the
  record after it. The port defines it at the true size.
- `func_02019B04_Table` is declared `const` in
  `src/matched/unit_02019B04.c`. The port cannot define it const, because its
  contents are game data copied in at run time from the player's own
  `extract/arm9_dec.bin`. A definition whose qualifiers differ from another
  translation unit's declaration is outside what C99 guarantees; it links and
  behaves correctly on every toolchain this port targets, and the alternative
  is editing a matched file.

Two more, both about the task-callback list and both now load bearing.
`sdk/tcbl.h` has the detail; in short, the node's `+0x00` and `+0x10` are
declared `u32` in every matched tcbl file and both hold pointers on the DS.
The port borrows the four bytes of LP64 padding behind `+0x00` to widen the
first into a real pointer (safe: no matched file reads either half), and
refuses the second outright, so no node on this host owns a separately
allocated work area.

A third, not yet load bearing: some matched sources declare
`OS_AllocFromHeap` / `OS_FreeToHeap` with all-`u32` parameters, because on the
DS a pointer is a u32 and the matcher only needs the relocation. On an LP64
host such a declaration cannot carry a pointer. Neither function is in the
current closure; when a TU that declares them that way enters it, the fix is a
wrapper include in the port layer, never an edit to the matched file.

One about the FS shim, not yet load bearing: 0x02078C18 (`FS_OpenFile`)
takes a path. `src/matched/arm9/func_02020DEC.c` and
`ov107_func_021F0850.c` pass a `char *`, `unit_02048DDC.c` passes a `u32` ARM9
address under the name `CRYPTOi__BN_lshift1`, and `func_020461F4.c` passes a
`u32` under `func_02078C18`. `sdk/fs.c` defines the pointer form for the SDK
name and the address spelling, and the `u32` form for `CRYPTOi__BN_lshift1`,
so every caller in the build today gets the type it declared. When
`func_020461F4` enters an LP64 build it needs a wrapper include that routes its
call to the address form.

## Host toolchain on Windows without an installer

eidos builds with any C99 compiler. On a machine without Visual Studio or
MSYS2, the layout used here is three portable archives kept in the aletheia
checkout, under `tools/`, all gitignored there (the same pattern as the ARM
toolchain in aletheia's `tools/gcc-arm/`):

    <aletheia>/tools/gcc-host/xpack-mingw-w64-gcc-<ver>/   MinGW-w64 GCC (xpack release zip)
    <aletheia>/tools/cmake/cmake-<ver>-windows-x86_64/      portable CMake (Kitware release zip)
    <aletheia>/tools/ninja/ninja.exe                        Ninja (release zip)

Configure and test, from this repository's root (Git Bash or PowerShell
equivalents), with `ALETHEIA` pointing at the aletheia checkout:

    ALETHEIA=../aletheia
    GCC=$ALETHEIA/tools/gcc-host/xpack-mingw-w64-gcc-14.2.0-1/bin/x86_64-w64-mingw32-gcc.exe
    $ALETHEIA/tools/cmake/cmake-3.31.6-windows-x86_64/bin/cmake -S . -B build -G Ninja \
        -DALETHEIA_ROOT=$ALETHEIA \
        -DCMAKE_C_COMPILER=$GCC -DCMAKE_MAKE_PROGRAM=$ALETHEIA/tools/ninja/ninja.exe \
        -DBW_DATA_ROOT=$ALETHEIA/extract/data
    $ALETHEIA/tools/cmake/cmake-3.31.6-windows-x86_64/bin/cmake --build build
    $ALETHEIA/tools/cmake/cmake-3.31.6-windows-x86_64/bin/ctest --test-dir build --output-on-failure

First run on this layout (2026-09-16), after the port moved into this
repository: 32 objects, both tests pass (test_text 5/5 strings, test_stats
3 species). With the 2D engine added (2026-09-16): 38 objects, three tests
pass (test_g2d 4/4 species, pixel-identical to the oracle). With the BG
half added (2026-09-16): 39 objects, four tests pass (test_bg 8/8 screens
pixel-identical, plus the scroll, priority and screen-layout checks).
With the affine paths, the windows and the colour effects added
(2026-09-16): 34 library objects, seven tests pass. `test_g2d` now checks
six species, the four identity ones pixel-identical and the two affine
ones measured against the oracle; `test_affine` (46 checks), `test_bg_affine`
(48 checks) and `test_window` (66 checks) are synthetic and need neither
game data nor an oracle.

With the platform layer, the frame skeleton and the demo added
(2026-09-16): 36 library objects plus the platform library, **nine tests
pass** in 1.1 seconds, including `test_view`, which runs `eidos_view` under
the dummy video and audio drivers and checks the BMP it wrote. The same tree
configured with `-DEIDOS_SDL=OFF` builds warning-free and passes the other
eight. A windowed run of `eidos_view --frames 60` takes one second of wall
clock and reports 60 frames, 1002 ms of game time and no pacer
resynchronisation, which is 60 / 59.8261 s to the millisecond.

With the heap layer and the game's task-callback list added (2026-09-16):
**ten tests pass** in 1.4 seconds. `test_tcbl` runs 49 checks over the
matched constructor, the matched free-list build and the matched runner on
the matched heap, and needs neither SDL nor game data; `test_view` now also
checks that two frames of the demo differ on the bottom screen, which is the
animation a task on that list drove, and not at all on the top screen.

With the 3D engine added (2026-09-17): five more library objects, all of
them `engine/g3d/`, and **twelve tests pass** in 1.7 seconds. `test_g3d` ran 68 checks at that point: the geometry half
against the OBJ aletheia's `map_shapes.py` exports (7798 vertices and
2354 faces over two map models, worst deviation 5.000e-07), and the
raster half against hand-computed pixels plus a content check on a
rendered frame. `test_view` also runs `eidos_view --map 120` headless and
checks the 3D layer reached the top screen. The same tree configured
with `cmake/ilp32-toolchain.cmake` passes the same twelve, and
`-DEIDOS_SDL=OFF` builds warning-free and passes the other eleven.

With the node dictionary fully decoded (2026-09-17): `nsbmd.c` reads the
MDL0 node dictionary, its translation, its packed and full rotations and
its scale, and the render-command section's node hierarchy and shape ->
node binding. The record layout is checked across 740 models, rotations
are orthonormal in 1163/1163 nodes, reciprocal scales are exact in
534/537, parents are in range and acyclic in 670/670, and every drawn
shape binds to a node that exists. `g3d_render_model` follows the draw
list applying each shape's node and ancestor transforms. `test_g3d` is
now **133 checks, 0 failures**: the same geometry and raster numbers as
before (the node is the identity for both oracle models) plus the byte
decode of `m_pc01_00_00`, `m_ph01_00_00`, `staffroll_b` and
`batt_stage01`, the synthetic translation/scale/rotation/binding/
composition cases, and a real render of `pl_00_00` under a node pose
override. All fourteen tests still pass in both configurations.
`docs/g3d-nodes.md` records the byte evidence and the two remaining
uninterpreted fields (the second flags byte and the packed rotation's
header `u16`), neither needed for the transform.

With the ILP32 configuration added (2026-09-16): the same tree configured
with `cmake/ilp32-toolchain.cmake` against the `i686-w64-mingw32-gcc` in the
same MinGW-w64 archive builds 96 targets warning-free, finds the i686 SDL3
tree beside the x86_64 one, and passes the same **ten tests** in 1.2 seconds,
with the nine pointer-width-dependent tcbl sources compiled as written and
the node and the manager at the DS's own `0x18` and `0x38`. `tools/hostbuild.py --check`
makes the same choice for the ARM cross build and still compiles 69 objects
clean.
