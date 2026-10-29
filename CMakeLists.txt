#
# CMakeLists.txt -- the native port of the matched game code (eidos).
#
#   cmake -S . -B build -DALETHEIA_ROOT=/path/to/aletheia -DBW_DATA_ROOT=extract/data
#   cmake --build build
#   ctest --test-dir build --output-on-failure
#
# Nothing here compiles the DS build.  The matched sources under
# ${ALETHEIA_ROOT}/src/matched/ are compiled unchanged for the host, through
# a build-tree copy in which tools/dsaddr.py has turned each cast of a fixed
# DS address into a call of bw_ds_ptr (cmake/dsaddr.cmake; a file with no
# such cast is copied byte for byte); the aletheia repository's own DS
# build still compiles the same files with mwccarm through its
# tools/csrc.py.  A matched file that has to change to build here has to be
# re-verified against the ROM in the same commit, in the aletheia
# repository (README.md, "Portability rules").
#

cmake_minimum_required(VERSION 3.16)
project(eidos VERSION 0.1.7 LANGUAGES C)

# The version players and the lobby see (docs/distribution.md).  A release
# build (EIDOS_RELEASE=ON, the release workflow) is the plain number; any
# other build adds the commit it was built from, so two builds of different
# commits never play together by mistake.
option(EIDOS_RELEASE "A release build: the version without the commit" OFF)
set(EIDOS_VERSION_STRING "${PROJECT_VERSION}")
if(NOT EIDOS_RELEASE)
    execute_process(COMMAND git rev-parse --short=10 HEAD
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        OUTPUT_VARIABLE _eidos_commit OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET RESULT_VARIABLE _eidos_commit_rc)
    if(_eidos_commit_rc EQUAL 0 AND _eidos_commit)
        set(EIDOS_VERSION_STRING "${PROJECT_VERSION}+${_eidos_commit}")
    else()
        set(EIDOS_VERSION_STRING "${PROJECT_VERSION}+dev")
    endif()
endif()
message(STATUS "eidos: version ${EIDOS_VERSION_STRING}")

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
# Strict C99 on glibc hides the POSIX calls the port's Linux code uses
# (nanosleep, clock_gettime, setenv, getaddrinfo); _DEFAULT_SOURCE shows
# them again, as macOS and the BSDs do by default.  The language stays C99.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_compile_definitions(_DEFAULT_SOURCE)
endif()

# Where the aletheia decompilation checkout lives.  eidos consumes matched
# sources, layout/ manifests and tools/assets/ scripts from it as an external
# checkout; nothing from aletheia is copied into this repository.  The
# default assumes the two repositories are cloned as siblings; pass this
# explicitly when they are not (see README.md).
# The sibling checkout may keep the repository's original folder name
# (bw_decomp); both spellings are tried before giving up.
set(_aletheia_default "${CMAKE_CURRENT_SOURCE_DIR}/../aletheia")
if(NOT EXISTS "${_aletheia_default}/src/matched"
   AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../bw_decomp/src/matched")
    set(_aletheia_default "${CMAKE_CURRENT_SOURCE_DIR}/../bw_decomp")
endif()
set(ALETHEIA_ROOT "${_aletheia_default}" CACHE PATH
    "Path to the aletheia decompilation repository checkout")
get_filename_component(ALETHEIA_ROOT "${ALETHEIA_ROOT}" ABSOLUTE)
if(NOT EXISTS "${ALETHEIA_ROOT}/src/matched")
    message(FATAL_ERROR "ALETHEIA_ROOT=${ALETHEIA_ROOT} does not contain src/matched; pass -DALETHEIA_ROOT=<path to the aletheia checkout>")
endif()

# Which of aletheia's trees the boot and the overlays compile
# (docs/game-tree.md): `matched` (src/matched, one function or unit per file)
# or `game` (src/game, one original source file per file).  Every generator
# reads it from the environment, so it is set here for the configure-time
# tools and passed to the build-time ones with `cmake -E env`.
set(EIDOS_SOURCE_TREE "game" CACHE STRING
    "aletheia tree the boot compiles: matched (src/matched) or game (src/game)")
set_property(CACHE EIDOS_SOURCE_TREE PROPERTY STRINGS matched game)
if(EIDOS_SOURCE_TREE STREQUAL "game" AND NOT EXISTS "${ALETHEIA_ROOT}/src/game")
    message(FATAL_ERROR "EIDOS_SOURCE_TREE=game but ${ALETHEIA_ROOT} has no src/game")
endif()
set(ENV{EIDOS_SOURCE_TREE} "${EIDOS_SOURCE_TREE}")
set(EIDOS_TREE_ENV ${CMAKE_COMMAND} -E env EIDOS_SOURCE_TREE=${EIDOS_SOURCE_TREE})

# Where the player's extracted assets live.  Relative paths are taken against
# the aletheia checkout, so -DBW_DATA_ROOT=extract/data does the obvious
# thing when aletheia is where the assets were extracted.
set(BW_DATA_ROOT "${ALETHEIA_ROOT}/extract/data" CACHE STRING
    "Directory holding the extracted NitroFS tree (the one with a/0/0/2)")
if(IS_ABSOLUTE "${BW_DATA_ROOT}")
    set(BW_DATA_ROOT_ABS "${BW_DATA_ROOT}")
else()
    set(BW_DATA_ROOT_ABS "${ALETHEIA_ROOT}/${BW_DATA_ROOT}")
endif()
# Forward slashes only: the path is also compiled in as a C string literal
# (EIDOS_DEFAULT_DATA_ROOT in eidos_view and eidos_boot), where a
# Windows-style C:\Users\... reads as escape sequences.  Windows accepts '/'.
file(TO_CMAKE_PATH "${BW_DATA_ROOT_ABS}" BW_DATA_ROOT_ABS)

find_package(Python3 COMPONENTS Interpreter REQUIRED)

if(CMAKE_SYSTEM_NAME STREQUAL "WASI" AND EIDOS_SOURCE_TREE STREQUAL "game")
    set(_wasm_layout_dir "${CMAKE_BINARY_DIR}/wasm32-layout")
    execute_process(COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/wasm32_layout.py"
        --aletheia-root "${ALETHEIA_ROOT}" --out "${_wasm_layout_dir}"
        RESULT_VARIABLE _wasm_layout_result)
    if(NOT _wasm_layout_result EQUAL 0)
        message(FATAL_ERROR "WASI packet layout generation failed")
    endif()
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/wasm32_layout.py"
        "${ALETHEIA_ROOT}/src/game/ov125/gds_rap_types.h")
    add_compile_options("-ivfsoverlay${_wasm_layout_dir}/headers.json")
endif()

if(APPLE)
    # Apple ld needs an explicit -U for optional symbols with no provider.
    # Apply to executable links only; libraries retain their weak imports.
    set(CMAKE_C_LINK_EXECUTABLE
        "\"${Python3_EXECUTABLE}\" \"${CMAKE_CURRENT_SOURCE_DIR}/tools/macos_link.py\" --nm \"${CMAKE_NM}\" -- ${CMAKE_C_LINK_EXECUTABLE}")
endif()

# src/game names many functions otherwise than src/matched did; the port's
# own C keeps the old spellings and is compiled with a header of #defines to
# the src/game names (tools/gamespell.py).  The header follows the tree's
# content, not its path: the same checkout can hold another revision, and a
# revision renames the functions the port calls, so a header written for one
# of them leaves the port's own copy of a function under a name nothing
# calls.  gamespell.py stamps the header with a fingerprint of everything it
# was made from (the tree's files, the port's own sources,
# tools/gamespell_extra.txt and the tool itself) and --stamp prints the
# fingerprint of the same inputs now, so the header is written again exactly
# when one of them changed.  The write is the slow part (it walks the tree's
# src/matched), so it must not run when nothing changed.
if(EIDOS_SOURCE_TREE STREQUAL "game")
    set(EIDOS_GAMESPELL_H ${CMAKE_BINARY_DIR}/gamespell.h)
    set(_gs_dirs ${CMAKE_CURRENT_SOURCE_DIR}/sdk ${CMAKE_CURRENT_SOURCE_DIR}/stubs
                 ${CMAKE_CURRENT_SOURCE_DIR}/tests ${CMAKE_CURRENT_SOURCE_DIR}/demo
                 ${CMAKE_CURRENT_SOURCE_DIR}/engine/platform)
    execute_process(
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/gamespell.py
                --stamp ${ALETHEIA_ROOT} -o ${EIDOS_GAMESPELL_H} ${_gs_dirs}
        RESULT_VARIABLE _gs_rc
        OUTPUT_VARIABLE _gs_stamp
        OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(NOT _gs_rc EQUAL 0)
        message(FATAL_ERROR "tools/gamespell.py --stamp failed (${_gs_rc})")
    endif()
    set(_gs_rewrite FALSE)
    if(NOT EXISTS ${EIDOS_GAMESPELL_H})
        set(_gs_rewrite TRUE)
    else()
        # What the header says it was made from, against what the same
        # inputs say now.
        file(READ ${EIDOS_GAMESPELL_H} _gs_head)
        string(REGEX MATCH "/\\* inputs: ([0-9a-f]+) \\*/" _gs_match "${_gs_head}")
        set(_gs_saved_stamp "${CMAKE_MATCH_1}")
        if(EXISTS "${EIDOS_GAMESPELL_H}.inputs")
            file(READ "${EIDOS_GAMESPELL_H}.inputs" _gs_saved_stamp)
            string(STRIP "${_gs_saved_stamp}" _gs_saved_stamp)
        endif()
        if(NOT "${_gs_saved_stamp}" STREQUAL "${_gs_stamp}")
            set(_gs_rewrite TRUE)
        endif()
        if(${CMAKE_CURRENT_SOURCE_DIR}/tools/gamespell_extra.txt IS_NEWER_THAN ${EIDOS_GAMESPELL_H})
            set(_gs_rewrite TRUE)
        endif()
    endif()
    if(_gs_rewrite)
        execute_process(
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/gamespell.py
                    ${ALETHEIA_ROOT} -o ${EIDOS_GAMESPELL_H} ${_gs_dirs}
            RESULT_VARIABLE _gs_rc)
        if(NOT _gs_rc EQUAL 0)
            message(FATAL_ERROR "tools/gamespell.py failed (${_gs_rc})")
        endif()
    endif()
    if(MSVC)
        # cl.exe and clang-cl spell a forced include /FI
        add_compile_options("/FI${EIDOS_GAMESPELL_H}")
    else()
        add_compile_options("SHELL:-include ${EIDOS_GAMESPELL_H}")
    endif()
    # Game bytes some src/game files include from build/gen/ (tools/aletheiagen.py).
    get_filename_component(_eidos_extract_dir "${BW_DATA_ROOT_ABS}" DIRECTORY)
    if(EXISTS "${_eidos_extract_dir}/arm9_dec.bin")
        execute_process(
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/aletheiagen.py
                    ${ALETHEIA_ROOT} ${_eidos_extract_dir}
            RESULT_VARIABLE _ag_rc)
        if(NOT _ag_rc EQUAL 0)
            message(FATAL_ERROR "tools/aletheiagen.py failed (${_ag_rc})")
        endif()
    endif()
endif()

# Whether the player's extracted assets are there at all.  The fixture
# generator and the four oracles read them at build time, and seven tests
# read them at run time; without them the build still has to succeed and
# every synthetic test still has to run, so those pieces are switched on
# only when the data root holds a NitroFS tree.  A test that needs data and
# has none is registered DISABLED rather than left out, so ctest names it
# ("Not Run (Disabled)") instead of it silently vanishing from the count.
if(EXISTS "${BW_DATA_ROOT_ABS}/a" AND IS_DIRECTORY "${BW_DATA_ROOT_ABS}/a")
    set(_eidos_data_default ON)
else()
    set(_eidos_data_default OFF)
endif()
option(EIDOS_DATA_TESTS
       "Generate the fixtures and oracles and run the tests that need extracted game data"
       ${_eidos_data_default})
if(EIDOS_DATA_TESTS)
    message(STATUS "Game data: ${BW_DATA_ROOT_ABS}; data-driven tests enabled")
else()
    message(STATUS "Game data: none at ${BW_DATA_ROOT_ABS}; the data-driven tests "
                   "are built where they can be and registered as disabled "
                   "(pass -DBW_DATA_ROOT=<extract/data> to enable them)")
endif()

# ---------------------------------------------------------------------------
# Pointer width
# ---------------------------------------------------------------------------
#
# The DS is LP32.  Every struct the game's own layout owns was laid out by a
# compiler with 4-byte pointers, and a matched source that reaches a field of
# such a struct through an explicit byte offset is only correct on a target
# that agrees.  docs/pointer-width.md measures how much matched code that is
# and records the policy; the mechanism is this one switch.
#
# ON:  the nine EXACT tcbl sources that depend on a four-byte pointer
#      (sub_02030EF0, sub_02030F34, func_02030F04, sub_02030FB4, and the
#      wave-26 five sub_02030F50, sub_02030F70, sub_020311B8, sub_02031194,
#      sub_0203115C) are compiled AS WRITTEN, straight out of aletheia's
#      src/matched/, and the stubs/reimpl_ stand-ins are left out of the
#      library.  Correct only where sizeof(void *) == 4.
# OFF: the stand-ins are compiled and the matched nine stay out, which is the
#      state README.md's "Why nine matched files are out of the LP64 build"
#      describes.
#
# The first four reach a field through a DS byte offset.  The other five
# declare the node's +0x00 and +0x10 as pointers where sub_0203101C and
# sub_020310C4, which are in the build unconditionally, declare them as u32:
# the same struct on the DS and on any ILP32 target, a different one on LP64.
#
# The default is decided at configure time from the target's pointer width,
# so `-DCMAKE_TOOLCHAIN_FILE=cmake/ilp32-toolchain.cmake` is the whole of
# what an ILP32 build has to say.
#
if(EIDOS_ILP32_REQUESTED AND NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR
        "cmake/ilp32-toolchain.cmake was given but the compiler produces "
        "${CMAKE_SIZEOF_VOID_P}-byte pointers.  Is the 32-bit runtime installed "
        "(gcc-multilib on Debian/Ubuntu), or does CMAKE_C_FLAGS override -m32?")
endif()
if(CMAKE_SIZEOF_VOID_P EQUAL 4)
    set(_eidos_tcbl_matched_default ON)
else()
    set(_eidos_tcbl_matched_default OFF)
endif()
option(EIDOS_TCBL_MATCHED
       "Compile the nine pointer-width-dependent tcbl sources as written (needs 32-bit pointers)"
       ${_eidos_tcbl_matched_default})
set(_native_tcbl_default OFF)
if(EIDOS_SOURCE_TREE STREQUAL "game" AND CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(_native_tcbl_default ON)
endif()
option(EIDOS_NATIVE_TCBL "Use the pointer-typed src/game scheduler" ${_native_tcbl_default})
if(EIDOS_TCBL_MATCHED AND NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR
        "EIDOS_TCBL_MATCHED=ON needs a target with 32-bit pointers; "
        "this one has sizeof(void *) = ${CMAKE_SIZEOF_VOID_P}.  Configure with "
        "-DCMAKE_TOOLCHAIN_FILE=cmake/ilp32-toolchain.cmake, or leave the "
        "option at its default.")
endif()

# The same question for the heap's floor.  ON puts the NitroSystem expanded
# heap into the build as written: the heap environment constructor
# (sub_0202FC48), heap creation, the free-list walks and the carve, the
# coalescing free and the child-heap binder, and leaves out the stand-ins
# that take blocks from the host allocator instead.  The expanded heap's
# block header is 0x10 bytes with two pointers in it and every one of these
# files does its address arithmetic in int, so ON needs 32-bit pointers
# exactly as EIDOS_TCBL_MATCHED does.
option(EIDOS_HEAP_MATCHED
       "Compile the expanded heap and the heap constructor as written (needs 32-bit pointers)"
       ${_eidos_tcbl_matched_default})
if(EIDOS_HEAP_MATCHED AND NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR
        "EIDOS_HEAP_MATCHED=ON needs a target with 32-bit pointers; "
        "this one has sizeof(void *) = ${CMAKE_SIZEOF_VOID_P}.  Configure with "
        "-DCMAKE_TOOLCHAIN_FILE=cmake/ilp32-toolchain.cmake, or leave the "
        "option at its default.")
endif()
message(STATUS "Pointer width: ${CMAKE_SIZEOF_VOID_P} bytes; heap floor: "
               "${EIDOS_HEAP_MATCHED} (ON = the matched expanded heap, "
               "OFF = host blocks)")
message(STATUS "Pointer width: ${CMAKE_SIZEOF_VOID_P} bytes; tcbl sources: "
               "${EIDOS_TCBL_MATCHED} (ON = the matched nine, "
               "OFF = stubs/reimpl_)")

# ---------------------------------------------------------------------------
# Compiler settings
# ---------------------------------------------------------------------------
#
# `-char signed` is part of the pinned DS compiler configuration (see
# aletheia's docs/AGENT-BRIEF.md); the host must agree, because matched code
# that stores into a plain `char` and compares it against a negative value
# behaves differently otherwise.  sdk/types.h checks it at compile time.
#
# MSVC (cl.exe and clang-cl) is configured in cmake/msvc.cmake: its plain
# char is already signed, so it gets no flag for it (in particular not /J,
# which makes char unsigned).
#
if(MSVC)
    include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/msvc.cmake)
else()
    add_compile_options(-Wall -Wextra -fsigned-char)
    # A local the game reads before it writes it (GF_STANDARD_SeedLcg64
    # hashes a 20-byte buffer it never fills) gets whatever the host stack
    # held, which differs between compilers and platforms: the 32-bit and
    # 64-bit builds then seeded their random numbers differently from the
    # same input.  Zero is one answer for every GCC and Clang build, so a
    # run repeats exactly on every platform the release covers (the DS's own
    # value was stack garbage too).  MSVC has no such switch: its builds
    # (development only, never packaged) can still seed differently.
    include(CheckCCompilerFlag)
    check_c_compiler_flag(-ftrivial-auto-var-init=zero EIDOS_HAVE_AUTO_VAR_INIT)
    if(EIDOS_HAVE_AUTO_VAR_INIT)
        add_compile_options(-ftrivial-auto-var-init=zero)
    endif()
    # MinGW lays structs out the way MSVC does by default: a 64-bit member is
    # aligned to 8 and bitfields pack by their declared type.  mwccarm aligns
    # a 64-bit member to 4 and packs bitfields as GCC does, so the game's
    # structs came out with other sizes and offsets on Windows (BattleRecHeader
    # is 0x44 bytes on the DS and was 0x48; aletheia's src/game size checks
    # catch it).  -mno-ms-bitfields gives the DS layout.  It applies to every
    # file the port compiles; the Win32 and SDL structs the platform layer
    # passes have no member it moves.
    if(WIN32)
        add_compile_options(-mno-ms-bitfields)
    endif()
    # The same for a 64-bit member's alignment: MinGW aligns long long and
    # double to 8 in a struct, mwccarm to 4 (aletheia's src/game size checks:
    # BattleRecSave is 0x18A4 on the DS, 0x18A8 under MinGW's default).  The
    # files that hand structs to the C runtime, Win32 or SDL (struct stat,
    # struct statvfs, LARGE_INTEGER, SDL_Event) keep the native alignment;
    # they share no 64-bit member with the game.  The installer's fsutil.c
    # is one of them: it reads a file's size through fstat, and a struct
    # stat laid out the DS way puts st_size four bytes off the one the
    # runtime wrote (a 256 MiB cartridge read as 2^60 bytes).
    if((WIN32 AND CMAKE_SIZEOF_VOID_P EQUAL 4) OR CMAKE_SYSTEM_NAME STREQUAL "WASI")
        # (the wasm32 build has no such flag: sdk/wasm_layout.h does it)
        if(CMAKE_SYSTEM_NAME STREQUAL "WASI")
            add_compile_options("SHELL:-include ${CMAKE_CURRENT_SOURCE_DIR}/sdk/wasm_layout.h"
                                -Wno-pragma-pack)
        else()
            add_compile_options(-mno-align-double)
        endif()
        file(GLOB _eidos_native_align
             ${CMAKE_CURRENT_SOURCE_DIR}/engine/platform/*.c
             ${CMAKE_CURRENT_SOURCE_DIR}/install/*.c
             ${CMAKE_CURRENT_SOURCE_DIR}/sdk/plat_*.c
             ${CMAKE_CURRENT_SOURCE_DIR}/sdk/mod_wasm.c)
        # The tests that include install/install.h or install/setup.h share
        # its u64 members (BwSha1.length) with the installer, so they take
        # its alignment.  The other tests share structs with the engine and
        # keep the DS one (test_audio crashed with the native alignment).
        list(APPEND _eidos_native_align
             ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_dataroot.c
             ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_extract.c
             ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_picker.c
             ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_settings.c
             ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_wizard.c)
        if(CMAKE_SYSTEM_NAME STREQUAL "WASI")
            set_property(SOURCE ${_eidos_native_align} APPEND PROPERTY
                COMPILE_DEFINITIONS BW_WASM_NATIVE_LAYOUT=1)
            # the port's own sources (not the game's): sdk/wasm_layout.h
            # keeps the C library's structs natural for them
            file(GLOB_RECURSE _eidos_port_sources
                 ${CMAKE_CURRENT_SOURCE_DIR}/sdk/*.c
                 ${CMAKE_CURRENT_SOURCE_DIR}/engine/*.c
                 ${CMAKE_CURRENT_SOURCE_DIR}/demo/*.c
                 ${CMAKE_CURRENT_SOURCE_DIR}/install/*.c)
            set_property(SOURCE ${_eidos_port_sources} APPEND PROPERTY
                COMPILE_DEFINITIONS BW_WASM_PORT_SOURCE=1)
        else()
            set_source_files_properties(${_eidos_native_align} PROPERTIES
                COMPILE_OPTIONS -malign-double)
        endif()
    endif()
    # The matched sources are written to reproduce mwccarm's output, not to
    # please a modern warning set.  They are compiled with the same warnings
    # as the port's own code, but the ones that are inherent to the style are
    # turned off for them specifically, further down.
endif()

# ---------------------------------------------------------------------------
# The curated list of matched translation units
# ---------------------------------------------------------------------------
#
# Grown mechanically, never by hand-guessing:
#
#     python tools/closure.py --seed-func func_02018A7C
#     python tools/closure.py --seed-func func_02018A7C --cmake
#
# prints the transitive closure through matched files and, separately, every
# name that is still open (needs a stubs/ entry) or is a global object
# (needs a sdk/game_globals.c entry).  Add a file here only when the closure
# report says what it needs and that need is met.
#
set(BW_MATCHED_SOURCES
    # --- archive reading -------------------------------------------------
    ${ALETHEIA_ROOT}/src/matched/arm9/func_02049578.c   # member index -> absolute position
    ${ALETHEIA_ROOT}/src/matched/arm9/func_020495F4.c   # stream read
    ${ALETHEIA_ROOT}/src/matched/arm9/func_0204944C.c   # read a range inside a member
    ${ALETHEIA_ROOT}/src/matched/arm9/func_02048D00.c   # open by id, read whole member, close
    ${ALETHEIA_ROOT}/src/matched/arm9_sdk/func_02048F4C.c   # the two-argument front of it
    ${ALETHEIA_ROOT}/src/matched/arm9/unit_02048DDC.c   # open an archive by id; resolve a member
    ${ALETHEIA_ROOT}/src/matched/arm9/sub_02034734.c    # the assert sink the readers call

    # --- message text ----------------------------------------------------
    ${ALETHEIA_ROOT}/src/matched/arm9/func_02045E3C.c   # the message-bank string cipher

    # --- personal data and the stat formula ------------------------------
    ${ALETHEIA_ROOT}/src/matched/arm9/unit_0201AFD4.c   # species + form -> member index
    ${ALETHEIA_ROOT}/src/matched/arm9/unit_0201ADA4.c   # read the 0x3C-byte record
    ${ALETHEIA_ROOT}/src/matched/arm9/unit_02019C54.c   # one-entry memo over that read
    ${ALETHEIA_ROOT}/src/matched/arm9/unit_02019B04.c   # nature multiplier
    ${ALETHEIA_ROOT}/src/matched/arm9/sub_02018A7C.c    # the six-stat recalculation

    # --- the heap layer --------------------------------------------------
    #
    # python tools/closure.py --seed-func sub_0203101C
    #
    # The whole chain from the game's allocation entry point down to the
    # expanded-heap allocator it bottoms out in, and the release side beside
    # it.  Every one of these is LP64-clean: none of them reaches a field of
    # a pointer-bearing struct through a DS byte offset, and the one 0x1c
    # that does appear is a block header with no pointers in it, so it is
    # 0x1c on any target.
    ${ALETHEIA_ROOT}/src/matched/arm9/unit_0202FBAC.c   # the four heap-record accessors
    ${ALETHEIA_ROOT}/src/matched/arm9/sub_02030098.c    # the raw allocator
    ${ALETHEIA_ROOT}/src/matched/arm9/sub_02030344.c    # checked heap id -> arena dump
    ${ALETHEIA_ROOT}/src/matched/arm9/sub_02030428.c    # heap id -> arena
    ${ALETHEIA_ROOT}/src/matched/arm9/sub_0203046C.c    # heap id -> live block count
    ${ALETHEIA_ROOT}/src/matched/arm9/sub_020304B8.c    # the block header's heap id
    ${ALETHEIA_ROOT}/src/matched/arm9/sub_020304C0.c    # the block header's provenance record
    ${ALETHEIA_ROOT}/src/matched/arm9/sub_02030734.c    # the allocation entry point
    ${ALETHEIA_ROOT}/src/matched/arm9_sdk/sub_020307B0.c    # the release entry point
    ${ALETHEIA_ROOT}/src/matched/arm9/sub_02030880.c    # stamp the provenance pair
    ${ALETHEIA_ROOT}/src/matched/arm9/sub_020308B8.c    # read it back and validate it
    ${ALETHEIA_ROOT}/src/matched/arm9/sub_02030958.c    # the out-of-memory reporter
    ${ALETHEIA_ROOT}/src/matched/arm9/sub_02030984.c    # the watched-allocation audit
    ${ALETHEIA_ROOT}/src/matched/arm9/sub_020309B8.c    # the free-side audit
    ${ALETHEIA_ROOT}/src/matched/arm9/sub_02030A00.c    # the watch filter
    ${ALETHEIA_ROOT}/src/matched/arm9/sub_02030A2C.c    # the free-side watch filter

    # The expanded heap's front door: round the request and pick the end of
    # the heap by the sign of the alignment.  Arena-independent, so it is in
    # every configuration; what it dispatches to is decided by
    # EIDOS_HEAP_MATCHED below.
    ${ALETHEIA_ROOT}/src/matched/arm9/func_02060CA0.c   # round the request, pick the end

    # --- tcbl.c, the game's task-callback list ---------------------------
    #
    # The scheduler engine/frame/ runs as the frame loop's step.  These three
    # are in every configuration, and on an LP64 target they are also what
    # decides the host layout of the node and the manager: the constructor
    # allocates sizeof(struct Tcbl) as its own compiler sees it and the
    # runner walks the node with its own offsets.
    #
    # Nine other EXACT files of this family are not in this list.  Four
    # (sub_02030EF0, sub_02030F34, func_02030F04, sub_02030FB4) reach a field
    # of the node or the manager through a DS byte offset.  Five more, all
    # matched in wave 26 (sub_02030F50, sub_02030F70, sub_020311B8,
    # sub_02031194, sub_0203115C), declare the node's +0x00 and +0x10 as
    # pointers where the three above declare them as u32, which is the same
    # struct on the DS and on any ILP32 target and a different one on LP64.
    # All nine are added below, or replaced by stubs/reimpl_ stand-ins, by
    # EIDOS_TCBL_MATCHED.  README.md, "Why nine matched files are out of the
    # LP64 build", records the reason for each.
    ${ALETHEIA_ROOT}/src/matched/arm9/sub_0203101C.c    # the constructor
    ${ALETHEIA_ROOT}/src/matched/arm9/sub_02030FC8.c    # build the free list
    ${ALETHEIA_ROOT}/src/matched/arm9/sub_020310C4.c    # one pass of the runner

    # --- the interrupt controller's matched half -------------------------
    #
    # Register and table traffic the OS layer's dispatcher agrees with
    # (sdk/os_interrupt.c); sdk/pxi.c's PXI_InitFifo calls both, so every
    # link that reaches bw_port_reset needs them.
    ${ALETHEIA_ROOT}/src/matched/arm9_sdk/OS_SetIrqFunction.c   # OS_IRQTable and the callback slots
    ${ALETHEIA_ROOT}/src/matched/arm9_sdk/OS_EnableIrqMask.c    # IE |= mask, under IME off
)

# ---------------------------------------------------------------------------
# The shim and the stubs
# ---------------------------------------------------------------------------

# The overlay registry's table (sdk/overlay.h), generated from aletheia's
# layout.json and manifest shards and this build's own source list by
# tools/overlay_table.py.  Generated rather than committed because its
# coverage columns move with every aletheia wave and every source added
# here.  It is regenerated when the script, layout.json, symbols_sdk.txt,
# any overlay shard present at configure time, or the build's source list
# changes; a shard added by a new wave is picked up at the next configure.
set(BW_OVERLAY_TABLE ${CMAKE_CURRENT_BINARY_DIR}/overlay_table.c)
set(BW_BUILD_SOURCE_LIST ${CMAKE_CURRENT_BINARY_DIR}/eidos_sources.txt)
file(GLOB BW_OVERLAY_SHARDS ${ALETHEIA_ROOT}/layout/matched/ov*/*.json)

# The IPC FIFO model and the ARM7 behind it (sdk/pxi.h, sdk/arm7/arm7.h).
set(BW_PXI_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/pxi.c           # FIFO pair, IPCSYNC, PXI_InitFifo, the IRQ 18 handler
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/arm7/arm7.c     # ARM7 power-on and step, OS tag 12, OS lock sync
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/arm7/pm.c       # power manager, tag 8
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/arm7/rtc.c      # real-time clock, tag 5
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/arm7/ctrdg.c    # Slot-2, tag 13
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/arm7/tp.c       # touch panel, tag 6
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/arm7/card.c     # Slot-1 pull-out, tag 14
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/arm7/fs.c       # Slot-1 backup requests and the save chip, tag 11
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/arm7/snd.c      # sound command lists, tag 7
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/arm7/nvram.c    # firmware flash, tag 4
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/arm7/mic.c      # microphone, tag 9 (silent)
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/arm7/tp.c       # touch panel, tag 6
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/arm7/pad.c      # X, Y, DEBUG, hinge in HW_BUTTON_XY_BUF
)

set(BW_SDK_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/fs.c            # the ROM file system onto the extract
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/overlay.c       # overlays: compiled in, checked on load
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/overlay_image.c # compiled overlays: window copy, names, trampolines
    ${BW_OVERLAY_TABLE}                             # generated, see below
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/mi.c            # MI_CpuCopy/Fill family
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/os_alloc.c      # OS heap onto malloc
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/os_arena.c      # OS_AllocFromArenaLo, the heaps' arena
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/os_irq.c        # OS_DisableInterrupts/Restore/Terminate
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/gx.c            # 0x02030524, the heap manager's error halfword
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/g3_fifo.c       # GX_SendFifo48B/64B, GXi_NopClearFifo128_ onto hw3d
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/mi_gx.c         # MI copies and GX command DMA onto hw3d
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/mi_dma.c        # MI_WaitDma/MI_StopDma on the DMA registers
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/tcbl.c          # the port's helper over tcbl.c
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/vfs.c           # data root, mod list, path resolution
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/vfs_narc.c      # member-level NARC overrides, the merged archive
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/vfs_msg.c       # text bank line files (`<bank>.msg`)
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/vfs_sdat.c      # the sound archive append merge
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/inflate.c       # DEFLATE, for packaged mods
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/mod_bw.c        # `.bw` mod packages, unpacked to a cache
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/bw_limits.c     # content limits mods raise (bw_limit_get)
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/mod_table.c     # a mod's own data tables (`table` lines)
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/mod_hooks.c     # on/off switch for port-side code hooks (docs/hooks.md)
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/mod_slot.c      # the mod seam's slot registry (docs/mod-interface.md)
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/mod_event.c     # the event dispatch: bindings, handlers, the scope stack
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/mod_context.c   # the contexts the payloads read (docs/mod-payloads.md)
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/mod_state.c     # the state calls over the game's accessors
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/mod_event_session.c  # the session and save events
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/mod_event_items.c    # the item, money and heal events
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/mod_event_mon.c      # the pokemon area's event rows
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/mod_event_field.c    # the world and the encounters' event rows
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/mod_event_battle.c   # the battle area's event rows
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/mod_message.c        # the message service (docs/mod-services.md)
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/mod_timer.c         # the timer, the splits and the mod's own random source
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/mod_export.c        # a Pokemon's export and import
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/mod_net.c          # the peer sessions and the external control
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/mp.c               # multiplayer: the lobby, the start and the pause
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/tls_roots.c        # the roots a TLS relay is checked against
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/save_slots.c       # several single-player saves
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/mod_frame.c        # the mod layer's frame tick
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/mod_avatar.c       # the field avatars
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/mod_restart.c      # the run restart
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/mod_handoff.c      # the save handoff
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/mod_menu.c        # the mod menu's model
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/mod_panel.c       # the run-status panel's model
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/mod_input.c       # the input injection service
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/mod_manifest.c   # the run-time manifest: the record, the parse, the apply
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/mod_savedata.c   # a mod's own saved data: the file, the envelope, the versions
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/mod_loader.c     # the loader: the library, the host table, the handlers
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/mod_wasm.c       # code mods built for wasm (WAMR, docs/mod-code-wasm.md)
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/mod_title.c     # title menu hook state (docs/hooks.md)
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/mod_text.c           # the port's text path: the sink that shows a queued message
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/mod_script.c         # the script command hooks: the field VM and the battle effect VM
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/mod_rule.c           # the rule interpreter (docs/mod-rules-language.md)
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/plat_dir.c      # path kind and directory listing, Win32 and POSIX
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/plat_save.c     # save folder, mkdir, sync, replace: Win32 and POSIX
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/save.c          # the save folder and the atomic whole-file write
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/firmware.c      # user settings: firmware.ini, 0x02FFFC80, flash blocks
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/narc.c          # NARC framing, ARM9 rodata
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/game_globals.c  # the ARM9 globals the matched code names
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/aliases.c       # sub_/func_ spelling forwarders
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/dsmem.c         # the DS address space: registry, regions, I/O, VRAM
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/host_vm.c       # reserve/commit for main RAM (the one OS-specific file)
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/os_tick.c       # OS_GetTick onto the frame loop, OS_InitTick
    # The OS layer the boot runs on (docs/boot.md, "Threads, interrupts and
    # the frame").  os_interrupt/os_alarm/os_system read the register file,
    # so they need sdk/dsmem.c; nothing in the library pulls them in until a
    # caller of theirs is linked.
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/plat_thread.c   # host mutex/condvar/thread, Win32 and POSIX
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/plat_crash.c    # a host crash reported on stderr, Win32 and POSIX
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/os_thread.c     # OSThread on host threads, one run token
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/os_interrupt.c  # IE/IF/IME, the IRQ handler, OS_Halt, OS_WaitIrq
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/os_alarm.c      # alarms on the tick
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/os_system.c     # console/boot type, MPU, ARM7 sync
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/os_argument.c   # the deliver argument
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/panic.c
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/port_reset.c    # bw_port_reset: dsmem, OS, PXI power-on and their joins
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/cp.c            # the divider and square-root unit, computed on access
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/input.c         # buttons, hinge, stylus: host + script onto the hardware
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/armdata.c       # the ARM9 image's data: copy, relocate, bind names
    ${BW_PXI_SOURCES}
)

# sdk/asm/: portable C for the matched SDK and runtime functions whose bodies
# are mwcc `asm` (sdk/asm/asmsdk.h).  One file per matched source; each
# names the source it replaces.  These need nothing beyond this library, so
# they are in it.
set(BW_ASMSDK_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/asm/MI_CpuCopy8.c
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/asm/MI_CpuFill8.c
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/asm/MIi_CpuClear16.c
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/asm/MIi_CpuClear32.c
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/asm/MIi_CpuCopy16.c
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/asm/MIi_CpuCopy32.c
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/asm/func_020834C0.c   # MI_UncompressLZ8
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/asm/MTX_Rot22_.c
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/asm/s32_div_f.c       # _s32_div_f, _u32_div_f
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/asm/func_02084EB8.c   # OS_GetLockID
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/asm/func_02084F10.c   # OS_ReleaseLockID
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/asm/OS_GetMacAddress.c
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/asm/CP_SaveContext.c
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/asm/CPi_RestoreContext.c
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/asm/CTRDG_IsExistedAtInit.c
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/asm/CTRDGi_IsTaskAvailable_02010054.c
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/asm/func_02034F74.c
)
list(APPEND BW_SDK_SOURCES ${BW_ASMSDK_SOURCES})

# The rest call matched game code or name game globals, so they link with
# the boot closure (EIDOS_BOOT), in place of the matched asm sources they
# replace; tests/test_asmsdk_glue.c checks them against recording doubles.
set(BW_ASMSDK_BOOT_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/asm/func_02084D98.c   # OS_UnLockCartridge
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/asm/ArrayDeleteAt.c
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/asm/NHTTPi_GetConnection.c
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/asm/OS_GetInitArenaLo.c
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/asm/OS_GetInitArenaHi.c
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/asm/SNDi_IncAlarmId.c
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/asm/func_02030BA4.c
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/asm/func_0203129C.c
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/asm/func_02034F84.c
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/asm/func_0203516C.c
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/asm/func_020351D0.c
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/asm/func_02049238.c
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/asm/func_0208C86C.c
    # TWL-only targets: panic if a NITRO boot ever reaches them.
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/asm/func_020053E4.c
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/asm/func_02005420.c
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/asm/func_02005428.c
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/asm/func_02005454.c
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/asm/func_02005B6C.c
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/asm/func_027312B4.c   # OSi_InitArenaEx_ltdmain
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/asm/func_0273131C.c   # OSi_SearchTitleIdFromList
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/asm/func_027313E8.c   # PMi_PreSleepForNdma
)

# One symbol per file, on purpose: a static-library member is only pulled in
# when nothing else defines its symbol, which is how tests/test_stats.c
# supplies its own record accessors without displacing anything else.
set(BW_STUB_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/stubs/reimpl_02045A98.c  # end-of-string code unit
    ${CMAKE_CURRENT_SOURCE_DIR}/stubs/reimpl_0204960C.c  # archive stream seek
    ${CMAKE_CURRENT_SOURCE_DIR}/stubs/reimpl_vm_control.c  # the script VMs' dispatch, with the port's hook
    ${CMAKE_CURRENT_SOURCE_DIR}/stubs/stub_02017E1C.c
    ${CMAKE_CURRENT_SOURCE_DIR}/stubs/stub_02017E40.c
    ${CMAKE_CURRENT_SOURCE_DIR}/stubs/stub_02018598.c

    # The out-of-memory report.  EXACT in aletheia, and its closure reaches
    # the game's debug print (func_0209128C, open) and a module of report
    # strings; it runs one call before OS_Terminate, after an allocation has
    # already failed, so it stays a stub.
    ${CMAKE_CURRENT_SOURCE_DIR}/stubs/stub_02030B50.c
)

# --- the heap layer's floor ----------------------------------------------
#
# python tools/closure.py --seed-func sub_0202FC48
# python tools/closure.py --seed-func sub_0202FDC4
# python tools/closure.py --seed-func func_02030164
#
# Exactly one of the two lists enters the library, by EIDOS_HEAP_MATCHED.
#
# Two EXACT files in this chain are not compiled as written even under
# ILP32: func_02060888.c (the carve) and func_02060E30.c (the free) let
# func_02060764 write a two-word region starting at one scalar local and
# then read the second word through the *next* declared local (`end`, and
# `&end - 1`), which is right under mwccarm's frame layout and nowhere
# else.  stubs/reimpl_02060888.c and stubs/reimpl_02060E30.c are their
# bodies with the pair declared as an array; README.md, "The heap
# environment", has the finding.
set(BW_HEAP_MATCHED
    ${ALETHEIA_ROOT}/src/matched/arm9/sub_0202FC48.c    # the heap environment constructor
    ${ALETHEIA_ROOT}/src/matched/arm9/sub_0202FDC4.c    # carve a child heap from a parent
    ${ALETHEIA_ROOT}/src/matched/arm9/func_0202FED8.c   # bind it to a spare entry
    ${ALETHEIA_ROOT}/src/matched/arm9/func_02060C5C.c   # create an expanded heap over a region
    ${ALETHEIA_ROOT}/src/matched/arm9/func_02060814.c   # the expanded heap's head
    ${ALETHEIA_ROOT}/src/matched/arm9/func_020606D0.c   # the common heap head, root list
    ${ALETHEIA_ROOT}/src/matched/arm9/func_020606A8.c   # the list that contains a heap
    ${ALETHEIA_ROOT}/src/matched/arm9/unit_02060614.c   # list walk, find by address
    ${ALETHEIA_ROOT}/src/matched/arm9/func_02060468.c   # init a list
    ${ALETHEIA_ROOT}/src/matched/arm9/func_02060480.c   # first append
    ${ALETHEIA_ROOT}/src/matched/arm9/func_020604AC.c   # append
    ${ALETHEIA_ROOT}/src/matched/arm9/func_020607E8.c   # write a block header
    ${ALETHEIA_ROOT}/src/matched/arm9/func_02060764.c   # a block's extent
    ${ALETHEIA_ROOT}/src/matched/arm9/func_02060790.c   # unlink a block
    ${ALETHEIA_ROOT}/src/matched/arm9/func_020607B8.c   # link a block after another
    ${CMAKE_CURRENT_SOURCE_DIR}/stubs/reimpl_02060888.c  # the carve (see above)
    ${ALETHEIA_ROOT}/src/matched/arm9/func_02060A08.c   # the head-end walk
    ${ALETHEIA_ROOT}/src/matched/arm9/func_02060ABC.c   # the tail-end walk
    ${ALETHEIA_ROOT}/src/matched/arm9/func_02030164.c   # the raw free
    ${CMAKE_CURRENT_SOURCE_DIR}/stubs/reimpl_02060E30.c  # the expanded heap's free (see above)
    ${ALETHEIA_ROOT}/src/matched/arm9/func_02060B6C.c   # return a region, coalescing
    ${ALETHEIA_ROOT}/src/matched/arm9/func_02060E6C.c   # total free bytes
    ${ALETHEIA_ROOT}/src/matched/arm9/func_02060E94.c   # largest allocatable block
    ${ALETHEIA_ROOT}/src/matched/arm9/func_02091588.c   # abs, under the above
    ${ALETHEIA_ROOT}/src/matched/arm9/func_02060F90.c   # an allocated block's size
)
set(BW_HEAP_REIMPL
    ${CMAKE_CURRENT_SOURCE_DIR}/stubs/reimpl_02060A08.c  # the head-end walk
    ${CMAKE_CURRENT_SOURCE_DIR}/stubs/reimpl_02060ABC.c  # the tail-end walk
    ${CMAKE_CURRENT_SOURCE_DIR}/stubs/reimpl_02030164.c  # the raw free
    ${CMAKE_CURRENT_SOURCE_DIR}/stubs/stub_02060E6C.c
    ${CMAKE_CURRENT_SOURCE_DIR}/stubs/stub_02060E94.c
    ${CMAKE_CURRENT_SOURCE_DIR}/stubs/stub_02060F90.c
)
if(EIDOS_HEAP_MATCHED)
    list(APPEND BW_MATCHED_SOURCES ${BW_HEAP_MATCHED})
else()
    list(APPEND BW_STUB_SOURCES ${BW_HEAP_REIMPL})
endif()

# The nine files the pointer width decides between.  Exactly one of the two
# lists enters the library; the symbols are the same either way, so nothing
# above or below this line knows which it got.
set(BW_TCBL_MATCHED
    ${ALETHEIA_ROOT}/src/matched/arm9/sub_02030EF0.c    # row address
    ${ALETHEIA_ROOT}/src/matched/arm9/sub_02030F34.c    # free-list push
    ${ALETHEIA_ROOT}/src/matched/arm9/func_02030F04.c   # free-list pop
    ${ALETHEIA_ROOT}/src/matched/arm9/sub_02030FB4.c    # the work accessor
    ${ALETHEIA_ROOT}/src/matched/arm9/sub_02030F50.c    # reset a node
    ${ALETHEIA_ROOT}/src/matched/arm9/sub_02030F70.c    # unlink one and free it
    ${ALETHEIA_ROOT}/src/matched/arm9/sub_020311B8.c    # register a callback
    ${ALETHEIA_ROOT}/src/matched/arm9/sub_02031194.c    # destroy one node
    ${ALETHEIA_ROOT}/src/matched/arm9/sub_0203115C.c    # destroy every node
)
set(BW_TCBL_REIMPL
    ${CMAKE_CURRENT_SOURCE_DIR}/stubs/reimpl_02030EF0.c  # row address
    ${CMAKE_CURRENT_SOURCE_DIR}/stubs/reimpl_02030F34.c  # free-list push
    ${CMAKE_CURRENT_SOURCE_DIR}/stubs/reimpl_02030F04.c  # free-list pop
    ${CMAKE_CURRENT_SOURCE_DIR}/stubs/reimpl_02030FB4.c  # the work accessor
    ${CMAKE_CURRENT_SOURCE_DIR}/stubs/reimpl_02030F50.c  # reset a node
    ${CMAKE_CURRENT_SOURCE_DIR}/stubs/reimpl_02030F70.c  # unlink one and free it
    ${CMAKE_CURRENT_SOURCE_DIR}/stubs/reimpl_020311B8.c  # register a callback
    ${CMAKE_CURRENT_SOURCE_DIR}/stubs/reimpl_02031194.c  # destroy one node
    ${CMAKE_CURRENT_SOURCE_DIR}/stubs/reimpl_0203115C.c  # destroy every node
)

if(EIDOS_NATIVE_TCBL)
    list(REMOVE_ITEM BW_MATCHED_SOURCES
        ${ALETHEIA_ROOT}/src/matched/arm9/sub_0203101C.c
        ${ALETHEIA_ROOT}/src/matched/arm9/sub_02030FC8.c
        ${ALETHEIA_ROOT}/src/matched/arm9/sub_020310C4.c)
    list(APPEND BW_MATCHED_SOURCES ${ALETHEIA_ROOT}/src/game/arm9/tcbl.c)
elseif(EIDOS_TCBL_MATCHED)
    list(APPEND BW_MATCHED_SOURCES ${BW_TCBL_MATCHED})
else()
    list(APPEND BW_STUB_SOURCES ${BW_TCBL_REIMPL})
endif()

# --- fixed DS addresses ----------------------------------------------------
# Every matched source is compiled from a build-tree copy that
# tools/dsaddr.py has rewritten (docs/boot.md, "Fixed addresses").  From
# here on BW_MATCHED_SOURCES names the copies, and a per-source property on
# a matched file is set on eidos_dsaddr_path() of it.
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/dsaddr.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/armdata.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/overlays.cmake)
set(BW_MATCHED_ORIGINALS ${BW_MATCHED_SOURCES})
eidos_dsaddr(BW_MATCHED_SOURCES ${BW_MATCHED_SOURCES})

# ---------------------------------------------------------------------------
# The engine
# ---------------------------------------------------------------------------
#
# eidos's own code, not a shim over an SDK name and not game code: the
# host-side reimplementation of hardware the DS had and a PC does not.
# engine/g2d/ is the 2D (OBJ/OAM) software compositor; every rule it
# implements is attested by a matched source, by aletheia's docs/ASSETS.md
# or by public NDS hardware documentation, and the file headers cite which.
# No SDL, no third-party code, standard library only.
#
set(BW_ENGINE_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/engine/g2d/lzss.c       # LZ77/LZ11 containers
    ${CMAKE_CURRENT_SOURCE_DIR}/engine/g2d/resources.c  # NCGR/NCLR/NCER/NANR/NMCR
    ${CMAKE_CURRENT_SOURCE_DIR}/engine/g2d/affine.c     # PA..PD, the per-pixel resample
    ${CMAKE_CURRENT_SOURCE_DIR}/engine/g2d/compose.c    # the OBJ compositor
    ${CMAKE_CURRENT_SOURCE_DIR}/engine/g2d/rig.c        # rig block loading
    ${CMAKE_CURRENT_SOURCE_DIR}/engine/g2d/bg.c         # BG layers, the frame compositor
    ${CMAKE_CURRENT_SOURCE_DIR}/engine/g2d/bg_affine.c  # rotation/scaling BG layers
    ${CMAKE_CURRENT_SOURCE_DIR}/engine/g2d/window.c     # windows, alpha and brightness
    ${CMAKE_CURRENT_SOURCE_DIR}/engine/g2d/hwcompose.c  # both screens from the hardware state
    ${CMAKE_CURRENT_SOURCE_DIR}/engine/g2d/par.c        # the compositors' worker pool
    ${CMAKE_CURRENT_SOURCE_DIR}/engine/frame/frame.c    # the vblank frame loop and the OS tick
    ${CMAKE_CURRENT_SOURCE_DIR}/engine/frame/prof.c     # the per-phase frame profiler

    # engine/g3d/ -- the geometry engine and the software rasteriser, the
    # host's stand-in for the DS 3D hardware.  No SDL here either: the
    # parsing and the vertex pipeline are checked against aletheia's own
    # OBJ export by tests/test_g3d.c, and the rasteriser writes an RGBA
    # buffer in exactly the shape g2d_frame_compose does.
    ${CMAKE_CURRENT_SOURCE_DIR}/engine/g3d/mtx.c        # 20.12 fixed point, the matrix stack
    ${CMAKE_CURRENT_SOURCE_DIR}/engine/g3d/texture.c    # the seven GX texture formats
    ${CMAKE_CURRENT_SOURCE_DIR}/engine/g3d/nsbmd.c      # NSBMD/NSBTX, dictionaries, shapes
    ${CMAKE_CURRENT_SOURCE_DIR}/engine/g3d/gx.c         # the display-list interpreter
    ${CMAKE_CURRENT_SOURCE_DIR}/engine/g3d/raster.c     # the software rasteriser
    ${CMAKE_CURRENT_SOURCE_DIR}/engine/g3d/light.c      # the lighting equation, for hw3d.c
    ${CMAKE_CURRENT_SOURCE_DIR}/engine/g3d/hw3d.c       # the 3D hardware: FIFO, ports, registers, swap

    # engine/world/ -- the overworld map as a lit, textured mesh (battle backdrop).
    ${CMAKE_CURRENT_SOURCE_DIR}/engine/world/bmap_data.c  # zone -> chunks -> world-space mesh
    ${CMAKE_CURRENT_SOURCE_DIR}/engine/world/bmap_gimmick.c  # the gym puzzles' models

    # engine/audio/ -- the SDAT sequencer and mixer, the host's stand-in for
    # the ARM7 sound core.  No SDL here either: the platform layer's audio
    # callback calls into this, and tests/test_audio.c drives it headless
    # against aletheia's tools/assets/sseqsynth.py.
    ${CMAKE_CURRENT_SOURCE_DIR}/engine/audio/sdat.c     # SYMB/INFO/FAT/FILE
    ${CMAKE_CURRENT_SOURCE_DIR}/engine/audio/sbnk.c     # instrument tables
    ${CMAKE_CURRENT_SOURCE_DIR}/engine/audio/swav.c     # SWAR/SWAV, PCM8/PCM16/ADPCM
    ${CMAKE_CURRENT_SOURCE_DIR}/engine/audio/sseq.c     # the SSEQ interpreter
    ${CMAKE_CURRENT_SOURCE_DIR}/engine/audio/mixer.c    # one voice: resample, ADSR, PSG
    ${CMAKE_CURRENT_SOURCE_DIR}/engine/audio/player.c   # render and stream a sequence

    # The ARM7's sound driver: SND commands from tag 7, the shared work, the
    # alarms (sdk/arm7/snd.h), the sequencer (snd_seq.c) and the sound
    # hardware model (snd_hw.c).  Not in BW_PXI_SOURCES: the transport
    # (sdk/arm7/snd.c) links without them, and test_pxi does.
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/arm7/snd_driver.c
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/arm7/snd_seq.c
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/arm7/snd_hw.c
)

add_library(eidos STATIC
    ${BW_MATCHED_SOURCES}
    ${BW_SDK_SOURCES}
    ${BW_STUB_SOURCES}
    ${BW_ENGINE_SOURCES}
)
# The engine runs per pixel and per sample: the screen composer, the 3D
# rasteriser, the mixer and the sound driver.  Unoptimised (the default
# here, no CMAKE_BUILD_TYPE), composing both screens costs about 150 ms, so
# a windowed boot shows 6 frames a second and looks frozen.  With no build
# type or Debug, GCC and Clang compile the engine with -O2 and keep frame pointers
# (sdk/plat_crash.c walks them); the matched game code stays as it is.
if((NOT CMAKE_BUILD_TYPE OR CMAKE_BUILD_TYPE STREQUAL "Debug")
   AND NOT CMAKE_CONFIGURATION_TYPES
   AND CMAKE_C_COMPILER_ID MATCHES "GNU|Clang" AND NOT MSVC)
    set_property(SOURCE ${BW_ENGINE_SOURCES} APPEND PROPERTY
        COMPILE_OPTIONS -O2 -fno-omit-frame-pointer)
endif()
# The source list the overlay table reads: every source of the library, as
# the generator expression sees it once all target_sources calls are in.
file(GENERATE OUTPUT ${BW_BUILD_SOURCE_LIST}
     CONTENT "$<JOIN:$<TARGET_PROPERTY:eidos,SOURCES>,\n>\n")
add_custom_command(
    OUTPUT ${BW_OVERLAY_TABLE}
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/overlay_table.py
            --aletheia-root ${ALETHEIA_ROOT}
            --sources ${BW_BUILD_SOURCE_LIST}
            --out ${BW_OVERLAY_TABLE}
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/tools/overlay_table.py
            ${ALETHEIA_ROOT}/layout/layout.json
            ${ALETHEIA_ROOT}/layout/symbols_sdk.txt
            ${BW_OVERLAY_SHARDS}
            ${BW_BUILD_SOURCE_LIST}
    COMMENT "Generating the overlay table from ${ALETHEIA_ROOT}/layout"
    VERBATIM)

# Port code that has to know which heap floor it is sitting on (the
# initialiser, the forwarders, the tests) reads this.
# In the library the sound transport installs the engine/audio driver at
# ARM7 power-on (sdk/arm7/snd.c); test_pxi compiles the transport alone.
target_compile_definitions(eidos PRIVATE BW_ARM7_SND_DRIVER=1)

# The wasm runtime code mods run in (docs/mod-code-wasm.md): WAMR, fetched
# and checked by tools/fetch_wamr.py into the user's local data folder.
# Without it the port builds and runs; a wasm mod is refused with a message.
if(DEFINED ENV{EIDOS_WAMR_CACHE})
    file(TO_CMAKE_PATH "$ENV{EIDOS_WAMR_CACHE}" _eidos_wamr_home)
elseif(WIN32)
    file(TO_CMAKE_PATH "$ENV{LOCALAPPDATA}/eidos/wamr" _eidos_wamr_home)
elseif(APPLE)
    set(_eidos_wamr_home "$ENV{HOME}/Library/Caches/eidos/wamr")
elseif(DEFINED ENV{XDG_CACHE_HOME})
    set(_eidos_wamr_home "$ENV{XDG_CACHE_HOME}/eidos/wamr")
else()
    set(_eidos_wamr_home "$ENV{HOME}/.cache/eidos/wamr")
endif()
set(EIDOS_WAMR_ROOT "${_eidos_wamr_home}/wasm-micro-runtime-WAMR-2.2.0" CACHE PATH
    "WAMR 2.2.0 source folder (tools/fetch_wamr.py)")
option(EIDOS_WASM "Build the wasm code-mod runtime when WAMR is present" ON)
if(EIDOS_WASM AND EXISTS "${EIDOS_WAMR_ROOT}/build-scripts/runtime_lib.cmake")
    # The port grows every module's memory by a page (sdk/mod_wasm.c); a
    # tree without fetch_wamr.py's memory change would refuse modules that
    # never grow their memory.
    file(STRINGS "${EIDOS_WAMR_ROOT}/core/iwasm/interpreter/wasm_loader.c"
         _eidos_wamr_keep LIMIT_COUNT 1 REGEX "eidos: the port grows every module")
    if(NOT _eidos_wamr_keep)
        message(FATAL_ERROR "${EIDOS_WAMR_ROOT} lacks the memory change: "
                "run python tools/fetch_wamr.py again")
    endif()
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/cmake/wamr ${CMAKE_BINARY_DIR}/wamr)
    target_link_libraries(eidos PUBLIC eidos_wamr)
    set_property(SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/sdk/mod_wasm.c APPEND PROPERTY
        COMPILE_DEFINITIONS EIDOS_HAVE_WAMR=1)
    message(STATUS "eidos: wasm code mods on (${EIDOS_WAMR_ROOT})")
else()
    message(STATUS "eidos: wasm code mods off (run tools/fetch_wamr.py)")
endif()

# TLS for the multiplayer relay (docs/multiplayer.md, "TLS"): Mbed TLS 3.6,
# fetched and checked by tools/fetch_mbedtls.py into the user's local data
# folder.  Without it the port builds and runs; a tls:// relay is refused
# with a message.
if(DEFINED ENV{EIDOS_MBEDTLS_CACHE})
    file(TO_CMAKE_PATH "$ENV{EIDOS_MBEDTLS_CACHE}" _eidos_mbedtls_home)
elseif(WIN32)
    file(TO_CMAKE_PATH "$ENV{LOCALAPPDATA}/eidos/mbedtls" _eidos_mbedtls_home)
elseif(APPLE)
    set(_eidos_mbedtls_home "$ENV{HOME}/Library/Caches/eidos/mbedtls")
elseif(DEFINED ENV{XDG_CACHE_HOME})
    set(_eidos_mbedtls_home "$ENV{XDG_CACHE_HOME}/eidos/mbedtls")
else()
    set(_eidos_mbedtls_home "$ENV{HOME}/.cache/eidos/mbedtls")
endif()
set(EIDOS_MBEDTLS_ROOT "${_eidos_mbedtls_home}/mbedtls-3.6.7" CACHE PATH
    "Mbed TLS 3.6.7 source folder (tools/fetch_mbedtls.py)")
option(EIDOS_TLS "Build TLS for the relay when Mbed TLS is present" ON)
if(EIDOS_TLS AND EXISTS "${EIDOS_MBEDTLS_ROOT}/library/ssl_tls.c")
    set(ENABLE_PROGRAMS OFF CACHE BOOL "" FORCE)
    set(ENABLE_TESTING OFF CACHE BOOL "" FORCE)
    set(MBEDTLS_FATAL_WARNINGS OFF CACHE BOOL "" FORCE)
    set(GEN_FILES OFF CACHE BOOL "" FORCE)
    set(USE_SHARED_MBEDTLS_LIBRARY OFF CACHE BOOL "" FORCE)
    set(USE_STATIC_MBEDTLS_LIBRARY ON CACHE BOOL "" FORCE)
    add_subdirectory(${EIDOS_MBEDTLS_ROOT} ${CMAKE_BINARY_DIR}/mbedtls EXCLUDE_FROM_ALL)
    if(CMAKE_SYSTEM_NAME STREQUAL "WASI")
        # no platform entropy or sockets there (cmake/mbedtls_wasm.h)
        target_compile_definitions(mbedcrypto PUBLIC
            "MBEDTLS_USER_CONFIG_FILE=\"${CMAKE_CURRENT_SOURCE_DIR}/cmake/mbedtls_wasm.h\"")
        # The C library's headers before sdk/wasm_layout.h's pack(4), as for
        # the port's own files; its structs get the layout sdk/mod_net.c sees
        foreach(_t mbedcrypto mbedx509 mbedtls everest p256m)
            if(TARGET ${_t})
                target_compile_definitions(${_t} PRIVATE BW_WASM_PORT_SOURCE=1)
            endif()
        endforeach()
    endif()
    target_link_libraries(eidos PUBLIC mbedtls mbedx509 mbedcrypto)
    set_property(SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/sdk/mod_net.c APPEND PROPERTY
        COMPILE_DEFINITIONS EIDOS_HAVE_TLS=1)
    message(STATUS "eidos: TLS for the relay on (${EIDOS_MBEDTLS_ROOT})")
else()
    message(STATUS "eidos: TLS for the relay off (run tools/fetch_mbedtls.py)")
endif()

target_compile_definitions(eidos PUBLIC EIDOS_VERSION="${EIDOS_VERSION_STRING}")
# The public relay a session uses when the player has set none
# (docs/distribution.md): `tls://name:port`.  Empty: no default.
set(EIDOS_DEFAULT_RELAY "" CACHE STRING "The public relay (tls://name:port), empty for none")
set_property(SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/engine/platform/settings.c APPEND PROPERTY
    COMPILE_DEFINITIONS "EIDOS_DEFAULT_RELAY=\"${EIDOS_DEFAULT_RELAY}\"")

if(EIDOS_HEAP_MATCHED)
    target_compile_definitions(eidos PUBLIC EIDOS_HEAP_MATCHED=1)
else()
    target_compile_definitions(eidos PUBLIC EIDOS_HEAP_MATCHED=0)
endif()
# sdk/aliases.c and sdk/tcbl.c decide a handful of callee spellings by whether
# the nine pointer-width-dependent tcbl sources are the matched ones
# (EIDOS_TCBL_MATCHED=1) or the stubs/reimpl_ stand-ins (0).
if(EIDOS_TCBL_MATCHED OR EIDOS_NATIVE_TCBL)
    target_compile_definitions(eidos PUBLIC EIDOS_TCBL_MATCHED=1)
else()
    target_compile_definitions(eidos PUBLIC EIDOS_TCBL_MATCHED=0)
endif()
if(EIDOS_NATIVE_TCBL)
    target_compile_definitions(eidos PUBLIC EIDOS_NATIVE_TCBL=1)
endif()
target_include_directories(eidos PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk
    ${CMAKE_CURRENT_SOURCE_DIR}/engine/g2d
    ${CMAKE_CURRENT_SOURCE_DIR}/engine/g3d
    ${CMAKE_CURRENT_SOURCE_DIR}/engine/frame
    ${CMAKE_CURRENT_SOURCE_DIR}/engine/audio
)

# engine/audio is the only part of the engine that needs libm (pow for the
# semitone ratio, fmod for the square's phase, ceil/round/fabs).  On a
# toolchain that keeps the maths functions in a separate library it has to
# be named; where they are in libc, find_library simply finds nothing and
# this is a no-op.
#
# find_library is not the right question: it searches the host's default
# library directories whatever the target ABI is, so under -m32 on a 64-bit
# Linux host it answers for the wrong libm (or for none at all, and the link
# fails on pow/round/ceil).  Asking the compiler whether `-lm` links a call
# to pow() answers it for the target being built.
include(CheckLibraryExists)
check_library_exists(m pow "" BW_HAVE_LIBM)
if(BW_HAVE_LIBM)
    target_link_libraries(eidos PUBLIC m)
endif()

# sdk/plat_thread.c: pthreads off Windows (Threads::Threads is empty where
# the C library already has them, and on Win32).  The wasm32 build has no
# threads of its own: they are the host's fibers (sdk/wasm_host.h).
if(NOT CMAKE_SYSTEM_NAME STREQUAL "WASI")
    set(THREADS_PREFER_PTHREAD_FLAG ON)
    find_package(Threads REQUIRED)
    target_link_libraries(eidos PUBLIC Threads::Threads)
endif()

# engine/audio is required to agree with its Python oracle *exactly* (see
# engine/audio/audio.h and tests/test_audio.c), and on 32-bit x86 the
# default floating point unit is x87, which evaluates double expressions in
# 80-bit registers and rounds to 64 bits only when a value is spilled.  That
# is not IEEE binary64 arithmetic, and it made every ILP32 render differ
# from the oracle's in the last ulp of a gain or a note-off time.  Asking
# for SSE2 instead gives the same 64-bit rounding at every step that every
# other target already has.  It is applied to the audio sources only: the
# rest of the engine is integer arithmetic and does not care.
#
# MSVC needs nothing: its 32-bit x86 compiler has defaulted to /arch:SSE2
# since Visual Studio 2012, so double arithmetic is already done in SSE2.
include(CheckCCompilerFlag)
set(BW_AUDIO_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/engine/audio/sdat.c
    ${CMAKE_CURRENT_SOURCE_DIR}/engine/audio/sbnk.c
    ${CMAKE_CURRENT_SOURCE_DIR}/engine/audio/swav.c
    ${CMAKE_CURRENT_SOURCE_DIR}/engine/audio/sseq.c
    ${CMAKE_CURRENT_SOURCE_DIR}/engine/audio/mixer.c
    ${CMAKE_CURRENT_SOURCE_DIR}/engine/audio/player.c
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/arm7/snd_driver.c
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/arm7/snd_seq.c
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/arm7/snd_hw.c
)
if(CMAKE_SIZEOF_VOID_P EQUAL 4 AND NOT MSVC)
    check_c_compiler_flag("-msse2 -mfpmath=sse" BW_HAVE_SSE2_MATH)
    if(BW_HAVE_SSE2_MATH)
        set_source_files_properties(${BW_AUDIO_SOURCES} PROPERTIES
            COMPILE_OPTIONS "-msse2;-mfpmath=sse")
        message(STATUS "engine/audio: SSE2 double arithmetic (x87 excess "
                       "precision would break the oracle comparison)")
    else()
        message(WARNING "engine/audio: no -msse2 on this 32-bit compiler; "
                        "test_audio may differ from the oracle in the last ulp")
    endif()
endif()

# ---------------------------------------------------------------------------
# SDL3, optional
# ---------------------------------------------------------------------------
#
# Needed only by engine/platform/ and the eidos_view demo.  Where it is
# looked for, the opt-in configure-time fetch (EIDOS_SDL_FETCH) and the link
# probe are in cmake/sdl3.cmake.
#
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/sdl3.cmake)

# ---------------------------------------------------------------------------
# The platform layer
# ---------------------------------------------------------------------------

if(EIDOS_SDL_FOUND)
    add_library(eidos_platform STATIC
        ${CMAKE_CURRENT_SOURCE_DIR}/engine/platform/platform.c
        ${CMAKE_CURRENT_SOURCE_DIR}/engine/platform/geometry.c
        ${CMAKE_CURRENT_SOURCE_DIR}/engine/platform/font.c
        ${CMAKE_CURRENT_SOURCE_DIR}/engine/platform/settings.c
        ${CMAKE_CURRENT_SOURCE_DIR}/engine/platform/settings_ui.c
        ${CMAKE_CURRENT_SOURCE_DIR}/engine/platform/mod_options_proc.c
        ${CMAKE_CURRENT_SOURCE_DIR}/engine/platform/mod_panel_draw.c
        ${CMAKE_CURRENT_SOURCE_DIR}/engine/platform/plat_replace.c
        ${CMAKE_CURRENT_SOURCE_DIR}/engine/platform/plat_gl.c
        ${CMAKE_CURRENT_SOURCE_DIR}/engine/platform/plat_gl3d.c
        # the one-screen layout: scenes, recipes, the interface kit
        ${CMAKE_CURRENT_SOURCE_DIR}/engine/ui/ui.c
        ${CMAKE_CURRENT_SOURCE_DIR}/engine/ui/recipes.c
        ${CMAKE_CURRENT_SOURCE_DIR}/engine/ui/recipe_settings.c
        ${CMAKE_CURRENT_SOURCE_DIR}/engine/ui/recipe_title.c
        ${CMAKE_CURRENT_SOURCE_DIR}/engine/ui/recipe_battle.c
        ${CMAKE_CURRENT_SOURCE_DIR}/engine/ui/recipe_bag.c
        ${CMAKE_CURRENT_SOURCE_DIR}/engine/ui/recipe_shop.c
        ${CMAKE_CURRENT_SOURCE_DIR}/engine/ui/recipe_dex.c
        ${CMAKE_CURRENT_SOURCE_DIR}/engine/ui/recipe_party.c
        ${CMAKE_CURRENT_SOURCE_DIR}/engine/ui/recipe_menus.c
        ${CMAKE_CURRENT_SOURCE_DIR}/engine/ui/recipe_pc.c
        ${CMAKE_CURRENT_SOURCE_DIR}/engine/ui/recipe_input.c
        ${CMAKE_CURRENT_SOURCE_DIR}/engine/ui/recipe_field.c
        ${CMAKE_CURRENT_SOURCE_DIR}/engine/ui/recipe_starter.c
        ${CMAKE_CURRENT_SOURCE_DIR}/engine/ui/kit.c
        ${CMAKE_CURRENT_SOURCE_DIR}/engine/ui/assets.c
        ${CMAKE_CURRENT_SOURCE_DIR}/engine/ui/assets_icons.c
        ${CMAKE_CURRENT_SOURCE_DIR}/engine/ui/assets_sprites.c
    )
    target_include_directories(eidos_platform PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/engine/platform
        ${CMAKE_CURRENT_SOURCE_DIR}/engine/ui
        ${CMAKE_CURRENT_SOURCE_DIR}/engine/world
    )
    target_link_libraries(eidos_platform PUBLIC eidos eidos_setup eidos_sdl3)
    # The GPU presenter (plat_gl.c) calls the GL 1.1 core entry points directly.
    if(CMAKE_SYSTEM_NAME STREQUAL "WASI")
        # the wasm32 build: GL is the host's, like SDL3 (host/)
    elseif(WIN32)
        target_link_libraries(eidos_platform PUBLIC opengl32)
    elseif(APPLE)
        target_link_libraries(eidos_platform PUBLIC "-framework OpenGL")
    else()
        target_link_libraries(eidos_platform PUBLIC GL)
    endif()

    # engine/platform/mod_options_proc.c is the port's mod-menu processor; it
    # calls the game's UI/BG/archive functions by the address spellings the
    # port wrote it with, and aletheia's cleanup renamed those definitions
    # (sub_020457B0 -> STRBUF_Create, ...).  The boot is the only link that
    # pulls the file in (the overlays' settings hooks reach it), and the boot
    # defines the descriptive names, so each spelling is renamed at build time
    # (the mechanism the tests use for the same problem).  A full rewrite of
    # the file onto the new names is the durable fix; the map is keyed on the
    # address so it is mechanical (see sdk/aliases.c's header).
    if(TARGET eidos_platform)
        set(_eidos_modopts_defs
            "sub_020457B0=STRBUF_Create"
            "func_02045994=STRBUF_SetStringN"
            "sub_0201D304=PRINTSYS_MeasureWidestLine"
            "sub_02045808=STRBUF_Delete"
            "sub_02045770=BMP_WIN_GetBmp"
            "sub_0201C724=PRINTSYS_PrintQueColor"
            "sub_02043B5C=BMP_Clear"
            "sub_02045334=BMP_WIN_TransferChars"
            "sub_02045374=BMP_WIN_MakeScreen"
            "sub_02045730=BMP_WIN_GetBgLayer"
            "sub_02041B6C=BG_SYS_MarkTileDirty"
            "func_020413B0=BG_SYS_FillTileMap"
            "func_020419E4=BG_SYS_Update"
            "func_0204A648=CLACT_VBlankSystem"
            "func_0203F8F4=BG_SYS_Create"
            "sub_02045088=BMP_WIN_Init"
            "func_0203FC28=BG_SYS_SetGraphicsMode"
            "func_0203FCA0=BG_SYS_InitBgLayer"
            "func_020414C4=BG_SYS_InitSlot"
            "func_020414EC=BG_SYS_ClearSlot"
            "sub_02040588=BG_SYS_SetPlaneVisible"
            "func_02040C14=BG_SYS_Init"
            "sub_020490F4=ARC_TOOL_CreateHandle"
            "sub_02049B68=ARC_UTIL_UploadPaletteFromHandleWhole"
            "sub_02049658=ARC_UTIL_UploadBgCharFromHandle"
            "sub_020498F4=ARC_UTIL_UploadBgPaletteFromHandle"
            "func_02049238=ARC_TOOL_DeleteHandle"
            "sub_020450F0=BMP_WIN_Create"
            "func_0201D7EC=GF_FONT_Create"
            "sub_0201C440=PRINTSYS_QUE_Create"
            "sub_020409B4=BG_SYS_PushTile"
            "func_0207D534=GXx_SetMasterBrightness_"
            "sub_02043534=BG_SYS_SetMainVisiblePlanes"
            "sub_020435F4=BG_SYS_SetSubVisiblePlanes"
            "sub_0204335C=BG_SYS_ClearGraphicsMemory"
            "func_020433E0=BG_SYS_SetVramBanks"
            "func_0204363C=BG_SYS_SetLcdSwap"
            "func_0204361C=BG_SYS_ResetSubDisplay"
            "sub_020434CC=BG_SYS_ClearMainPlanes"
            "func_02043588=BG_SYS_ClearSubPlanes"
            "func_0201DD60=GF_FONT_InitTextColor"
            "func_0201DD78=GF_FONT_SetTextColor"
            "sub_0203101C=TCBL_Create"
            "sub_020056A0=GFL_USE_AddMainTask"
            "func_0204E720=FADE_Start"
            "sub_020452E8=BMP_WIN_Delete"
            "sub_0201C4C0=PRINTSYS_QUE_Delete"
            "func_0201D83C=GF_FONT_Delete"
            "sub_02030EAC=TCB_DeleteTask"
            "sub_02031140=TCBL_Destroy"
            "sub_020450C8=BMP_WIN_Exit"
            "func_0203F9B4=BG_SYS_Delete"
            "func_0203159C=PROCSYS_AllocProcWork"
            "func_0204E804=FADE_IsActive"
            "sub_0203633C=KEY_GetRepeat"
            "sub_020362DC=KEY_GetTrg"
            "sub_020061E4=GFL_USE_PlaySeDefaultVolume"
            "sub_020310C4=TCBL_Main"
            "func_0204A600=CLACT_DrawSystem"
            "sub_020315D4=PROCSYS_FreeProcWork"
            "func_020306F0=GFL_HEAP_DeleteChecked"
            "func_020312F8=PROCSYS_ReplaceGlobal")
        set_property(SOURCE
            ${CMAKE_CURRENT_SOURCE_DIR}/engine/platform/mod_options_proc.c
            APPEND PROPERTY COMPILE_DEFINITIONS "${_eidos_modopts_defs}")
    endif()
endif()

# The matched files are compiled with the port's warning set but without the
# ones that are inherent to reproducing a 2010 compiler's output: they declare
# each extern locally with the prototype that call site needs, so prototypes
# for the same function differ between translation units by design, and a
# local that is never read is sometimes what fixes the register allocation
# (src/matched/func_02060814.c's `feature`).  A fourth: an SDK file that
# reaches past the end of the object its data binding names (the DS's BSS
# runs on into the next one; src/matched/arm9_sdk/func_0208B548.c's MicBss)
# is how the DS code addresses it, and is reported as -Warray-bounds.
# Under MSVC the same three are spelled in cmake/msvc.cmake.
if(MSVC)
    set_source_files_properties(${BW_MATCHED_SOURCES} PROPERTIES
        COMPILE_OPTIONS "${EIDOS_MATCHED_COMPILE_OPTIONS}")
else()
    set_source_files_properties(${BW_MATCHED_SOURCES} PROPERTIES
        COMPILE_OPTIONS "-Wno-unused-parameter;-Wno-unused-variable;-Wno-missing-prototypes;-Wno-array-bounds")
endif()

# Two matched files name one DS object under two different names: the DS
# build links both relocations to one address, a host linker would make two
# objects.  Each is compiled with its spelling defined to the other file's,
# which renames the reference at build time and leaves the file itself
# untouched (portability rule 1).  closure.py lists both pairs as `data`
# names at the same address.
#
#   0x02146B64  sRootList (func_020606D0.c)   g_02146B64 (func_020606A8.c)
#   0x02146954  g_EnvModule (everywhere)      g_02146954 (func_0202FED8.c)
if(EIDOS_HEAP_MATCHED)
    eidos_dsaddr_path(_src ${ALETHEIA_ROOT}/src/matched/arm9/func_020606A8.c)
    set_source_files_properties(${_src}
        PROPERTIES COMPILE_DEFINITIONS "g_02146B64=sRootList")
    eidos_dsaddr_path(_src ${ALETHEIA_ROOT}/src/matched/arm9/func_0202FED8.c)
    set_source_files_properties(${_src}
        PROPERTIES COMPILE_DEFINITIONS "g_02146954=g_EnvModule")
endif()

# The same "one DS object, two spellings" problem, one address further out:
# the archive readers and the personal-data memo name ARM9 data objects that
# sdk/game_globals.c owns under the port's own spellings, and aletheia's
# cleanup waves renamed the matched side to the ROM's symbol names
# (arc_tool_file, ARC_TOOL_gArchiveTable, _pPersonalArc, gPersonalCache).
# Each matched file is compiled with its spelling defined to the port object's
# (portability rule 1, as above).  The message strings never reach behaviour:
# they are the arguments of an assertion that only fires on an out-of-range
# member, which is why sdk/game_globals.c can supply its own text.
#
# STRBUF_GetEosChar is the same shape one step removed: aletheia matched
# src/matched/arm9_sdk/func_02045A98.c, which defines STRBUF_GetEosChar, while
# the port still compiles stubs/reimpl_02045A98.c under the address name, so
# func_02045E3C.c's call is renamed onto the port's definition.
#
# FS_OpenFile is the same shape for a function: aletheia renamed the archive
# reader's open call in unit_02048DDC.c to FS_OpenFile, but the port's
# FS_OpenFile (sdk/fs.c) takes a host path while that call passes a DS address
# from the archive name table.  The port's DS-address reader is
# CRYPTOi__BN_lshift1 (sdk/fs.c), so the matched call is renamed onto it and
# the host-path FS_OpenFile is left alone.
foreach(_eidos_data_alias
    "arm9/func_02049578.c|arc_tool_file=func_02049578_File,arc_tool_msg_member_bounds=func_02049578_Msg"
    "arm9/func_0204944C.c|arc_tool_file=func_0204944C_File,arc_tool_msg_member_bounds=func_0204944C_Msg"
    "arm9/unit_02048DDC.c|ARC_TOOL_gArchiveTable=g_02146A68,arc_tool_msg_member_range=func_02048DDC_Msg,arc_tool_file=func_02048DDC_File,FS_OpenFile=CRYPTOi__BN_lshift1"
    "arm9/unit_0201AFD4.c|_pPersonalArc=g_0214652C"
    "arm9/unit_0201ADA4.c|_pPersonalArc=g_0214652C"
    "arm9/unit_02019C54.c|gPersonalCache=g_02146270"
    "arm9/func_02045E3C.c|STRBUF_GetEosChar=func_02045A98")
    string(REPLACE "|" ";" _eidos_kv "${_eidos_data_alias}")
    list(GET _eidos_kv 0 _eidos_rel)
    list(GET _eidos_kv 1 _eidos_defs)
    # The defines are ','-separated above: a ';' inside a list element would
    # split the element and drop every define after the first.
    string(REPLACE "," ";" _eidos_defs "${_eidos_defs}")
    eidos_dsaddr_path(_eidos_src
        ${ALETHEIA_ROOT}/src/matched/${_eidos_rel})
    set_source_files_properties(${_eidos_src}
        PROPERTIES COMPILE_DEFINITIONS "${_eidos_defs}")
endforeach()

# ---------------------------------------------------------------------------
# Generated fixtures
# ---------------------------------------------------------------------------

set(BW_FIXTURES ${CMAKE_CURRENT_BINARY_DIR}/bw_fixtures.h)

add_custom_command(
    OUTPUT ${BW_FIXTURES}
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/fixtures.py
            --aletheia-root ${ALETHEIA_ROOT}
            --data-root ${BW_DATA_ROOT_ABS}
            --out ${BW_FIXTURES}
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/tools/fixtures.py
            ${ALETHEIA_ROOT}/tools/assets/text.py
            ${ALETHEIA_ROOT}/tools/assets/narc.py
    COMMENT "Generating port test fixtures from ${BW_DATA_ROOT_ABS}"
    VERBATIM
)
add_custom_target(bw_fixtures DEPENDS ${BW_FIXTURES})

# ---------------------------------------------------------------------------
# The 2D engine's pixel oracle
# ---------------------------------------------------------------------------
#
# tools/rig_oracle.py imports aletheia's tools/assets/rig.py (nothing is
# copied here, and aletheia is not modified) and dumps the rest pose of
# each species as a raw RGBA file next to the PNG rig.py itself writes.
# tests/test_g2d.c composes the same poses through engine/g2d and compares
# them pixel for pixel.  The dumps are game data derived from the player's
# own ROM: they are written into the build directory only, never here.
#
# The four reference species are identity poses at t=0 and are compared
# pixel for pixel.  Species 50 and 88 are the two genuinely non-identity SRT
# poses the asset lane's sweep found (a half-height scale and a rotation);
# the test renders them through the affine path and reports the divergence
# from the oracle rather than demanding equality, because the engine
# transforms in the hardware's fixed point and rig.py transforms in doubles
# (README.md, "the fixed-point question").
set(BW_G2D_SPECIES 25 6 94 150)
set(BW_G2D_AFFINE_SPECIES 50 88)
set(BW_G2D_ORACLE_DIR ${CMAKE_CURRENT_BINARY_DIR}/oracle/rig)

set(BW_G2D_ORACLE_FILES "")
foreach(sp ${BW_G2D_SPECIES} ${BW_G2D_AFFINE_SPECIES})
    # rig_oracle.py names its output with a four-digit species number.
    string(LENGTH "${sp}" _len)
    math(EXPR _pad "4 - ${_len}")
    set(_name "${sp}")
    while(_pad GREATER 0)
        set(_name "0${_name}")
        math(EXPR _pad "${_pad} - 1")
    endwhile()
    list(APPEND BW_G2D_ORACLE_FILES ${BW_G2D_ORACLE_DIR}/${_name}_pose.rgba)
endforeach()

add_custom_command(
    OUTPUT ${BW_G2D_ORACLE_FILES}
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/rig_oracle.py
            --aletheia-root ${ALETHEIA_ROOT}
            --data-root ${BW_DATA_ROOT_ABS}
            --out ${BW_G2D_ORACLE_DIR}
            ${BW_G2D_SPECIES} ${BW_G2D_AFFINE_SPECIES}
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/tools/rig_oracle.py
            ${ALETHEIA_ROOT}/tools/assets/rig.py
            ${ALETHEIA_ROOT}/tools/assets/narc.py
            ${ALETHEIA_ROOT}/tools/assets/battle_sprite.py
    COMMENT "Generating the g2d pixel oracle with aletheia's rig.py"
    VERBATIM
)
add_custom_target(bw_g2d_oracle DEPENDS ${BW_G2D_ORACLE_FILES})

# ---------------------------------------------------------------------------
# The BG layer's pixel oracle
# ---------------------------------------------------------------------------
#
# tools/bg_oracle.py imports aletheia's tools/assets/background.py the same
# way, renders each screen below and writes the PNG, a raw RGBA dump and
# screens.txt, the manifest naming the NCGR/NCLR sub-file its coverage rule
# paired with each NSCR.  tests/test_bg.c renders the same three sub-files
# through engine/g2d/bg.c and compares pixel for pixel.
#
# The eight screens are picked for coverage rather than for looks:
#
#   titledemo.narc#0   256x256  8bpp  the Diamond/Pearl title logo the asset
#                                     lane identified; 255 distinct colours
#   a/1/4/3#22         512x192  4bpp  the battle-background terrain layer
#   a/1/3/7#26         256x256  4bpp  the "VS. RECORDER" UI banner
#   a/0/4/6#11         256x512  4bpp  the menu window frame, the tall size
#   a/0/3/4#17         512x256  8bpp  the wide size, in 256-colour mode
#   a/1/5/9#108        512x512  4bpp  the largest screen size
#   a/1/2/0#4          256x192  4bpp  55 distinct colours, the widest 4bpp
#                                     palette spread found in the scan
#   a/1/9/2#2          256x256  4bpp  29 distinct colours, the Wi-Fi grid
#
# so that all four hardware screen sizes, both colour modes and two
# authoring sizes that are not hardware sizes at all are covered.
#
# The entries are quoted because an unquoted '#' starts a CMake comment.
set(BW_BG_SCREENS
    "titledemo.narc#0"
    "a/1/4/3#22"
    "a/1/3/7#26"
    "a/0/4/6#11"
    "a/0/3/4#17"
    "a/1/5/9#108"
    "a/1/2/0#4"
    "a/1/9/2#2"
)
set(BW_BG_ORACLE_DIR ${CMAKE_CURRENT_BINARY_DIR}/oracle/bg)

set(BW_BG_ORACLE_FILES ${BW_BG_ORACLE_DIR}/screens.txt)
foreach(scr ${BW_BG_SCREENS})
    string(REPLACE ".narc" "" _n "${scr}")
    string(REPLACE "/" "_" _n "${_n}")
    string(REPLACE "#" "_" _n "${_n}")
    list(APPEND BW_BG_ORACLE_FILES ${BW_BG_ORACLE_DIR}/${_n}.rgba)
endforeach()

add_custom_command(
    OUTPUT ${BW_BG_ORACLE_FILES}
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/bg_oracle.py
            --aletheia-root ${ALETHEIA_ROOT}
            --data-root ${BW_DATA_ROOT_ABS}
            --out ${BW_BG_ORACLE_DIR}
            ${BW_BG_SCREENS}
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/tools/bg_oracle.py
            ${ALETHEIA_ROOT}/tools/assets/background.py
            ${ALETHEIA_ROOT}/tools/assets/narc.py
            ${ALETHEIA_ROOT}/tools/assets/battle_sprite.py
    COMMENT "Generating the BG pixel oracle with aletheia's background.py"
    VERBATIM
)
add_custom_target(bw_bg_oracle DEPENDS ${BW_BG_ORACLE_FILES})

# ---------------------------------------------------------------------------
# The 3D engine's geometry oracle
# ---------------------------------------------------------------------------
#
# tools/g3d_oracle.py imports aletheia's tools/assets/nsbmd.py and
# tools/assets/map_shapes.py (nothing is copied here, and aletheia is not
# modified) and writes, per model, the per-shape textured OBJ that lane
# exports, plus models.txt: the manifest naming each shape's texture and
# the (width, height) the OBJ's `vt` lines were divided by, which is what
# lets tests/test_g3d.c compare texture coordinates in the sixteenths of
# a texel the TEXCOORD command actually carries.
#
# Two models, picked for coverage rather than for looks:
#
#   pl_00_00   a/0/0/8#120   an interior, 40 shapes, the model the asset
#                            lane's own shape-dictionary crack was
#                            confirmed against
#   map24_23   a/0/0/8#0     an outdoor terrain chunk, 26 shapes
#
# The manifest also carries the texture-format survey over a/0/1/4, the
# shared archive every map model draws from, which is what README.md's
# engine/g3d section quotes for which formats actually occur.
set(BW_G3D_MODELS pl_00_00:120 map24_23:0)
set(BW_G3D_ORACLE_DIR ${CMAKE_CURRENT_BINARY_DIR}/oracle/g3d)
set(BW_G3D_ORACLE_FILES
    ${BW_G3D_ORACLE_DIR}/models.txt
    ${BW_G3D_ORACLE_DIR}/pl_00_00.obj
    ${BW_G3D_ORACLE_DIR}/map24_23.obj
)

add_custom_command(
    OUTPUT ${BW_G3D_ORACLE_FILES}
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/g3d_oracle.py
            --aletheia-root ${ALETHEIA_ROOT}
            --data-root ${BW_DATA_ROOT_ABS}
            --out ${BW_G3D_ORACLE_DIR}
            ${BW_G3D_MODELS}
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/tools/g3d_oracle.py
            ${ALETHEIA_ROOT}/tools/assets/nsbmd.py
            ${ALETHEIA_ROOT}/tools/assets/map_shapes.py
            ${ALETHEIA_ROOT}/tools/assets/narc.py
    COMMENT "Generating the g3d geometry oracle with aletheia's map_shapes.py"
    VERBATIM
)
add_custom_target(bw_g3d_oracle DEPENDS ${BW_G3D_ORACLE_FILES})

# ---------------------------------------------------------------------------
# The audio engine's oracle
# ---------------------------------------------------------------------------
#
# tools/audio_oracle.py imports aletheia's tools/assets/sseqsynth.py, the
# sample-based SSEQ renderer that lane wrote against the ROM's own SDAT, and
# writes one .wav and one .notes.txt per sequence plus seqs.txt, the
# manifest tests/test_audio.c reads.  Nothing is copied here and aletheia is
# not modified; the renders are game data and are written into the build
# directory only.
#
# The three sequences cover the three channel kinds the mixer implements:
#
#   1062  SEQ_BGM_GYM          PCM waves out of a real SWAR (the gym theme)
#   1146  SEQ_BGM_BATTLEPINCH  PSG square, alongside PCM on other tracks
#   1379  SEQ_SE_KOUKA_H       PSG noise
#
# The window is deliberately short.  It is the same window on both sides, it
# is long enough to reach several tempo segments and dozens of note-ons, and
# the oracle is a pure-Python mixer: a full-length render would put minutes
# into every build for no extra coverage.
set(BW_AUDIO_SEQS 1062 1146 1379)
set(BW_AUDIO_SECONDS 10.0)
set(BW_AUDIO_ORACLE_DIR ${CMAKE_CURRENT_BINARY_DIR}/oracle/audio)

set(BW_AUDIO_ORACLE_FILES ${BW_AUDIO_ORACLE_DIR}/seqs.txt)
foreach(sq ${BW_AUDIO_SEQS})
    list(APPEND BW_AUDIO_ORACLE_FILES ${BW_AUDIO_ORACLE_DIR}/seq${sq}.wav)
endforeach()

add_custom_command(
    OUTPUT ${BW_AUDIO_ORACLE_FILES}
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/audio_oracle.py
            --aletheia-root ${ALETHEIA_ROOT}
            --data-root ${BW_DATA_ROOT_ABS}
            --out ${BW_AUDIO_ORACLE_DIR}
            --max-seconds ${BW_AUDIO_SECONDS}
            ${BW_AUDIO_SEQS}
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/tools/audio_oracle.py
            ${ALETHEIA_ROOT}/tools/assets/sseqsynth.py
            ${ALETHEIA_ROOT}/tools/assets/sdat.py
            ${ALETHEIA_ROOT}/tools/assets/sbnk.py
            ${ALETHEIA_ROOT}/tools/assets/sseq.py
            ${ALETHEIA_ROOT}/tools/assets/swav.py
            ${ALETHEIA_ROOT}/tools/assets/adpcm.py
    COMMENT "Generating the audio oracle with aletheia's sseqsynth.py"
    VERBATIM
)
add_custom_target(bw_audio_oracle DEPENDS ${BW_AUDIO_ORACLE_FILES})

# ---------------------------------------------------------------------------
# The installer core: identify and extract the player's ROM (install/)
# ---------------------------------------------------------------------------
#
# Plain C99 and the standard library, no SDL and nothing of the game: the
# part of the first-run wizard that is the same on every platform.
add_library(eidos_install STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/install/sha1.c
    ${CMAKE_CURRENT_SOURCE_DIR}/install/blz.c
    ${CMAKE_CURRENT_SOURCE_DIR}/install/ndsrom.c
    ${CMAKE_CURRENT_SOURCE_DIR}/install/fsutil.c
)
target_include_directories(eidos_install PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/install
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk)
add_executable(eidos_extract ${CMAKE_CURRENT_SOURCE_DIR}/install/extract_main.c)
target_link_libraries(eidos_extract PRIVATE eidos_install)
if(WIN32)
    # CommandLineToArgvW, for UTF-8 paths on the command line.
    target_link_libraries(eidos_extract PRIVATE shell32)
endif()

# First run without a window (install/setup.c): where the data root lives
# on each platform, whether the one there is playable, the config file,
# --rom/--reset-data/--move-data; and the platform's own file dialog
# (install/picker.c, NSOpenPanel in install/picker_mac.m on macOS).  No SDL,
# so every configuration builds and tests it, MSVC included; the SDL
# wizard on top of it is with the platform layer below (eidos_wizard).
add_library(eidos_setup STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/install/setup.c
    ${CMAKE_CURRENT_SOURCE_DIR}/install/location.c
    ${CMAKE_CURRENT_SOURCE_DIR}/install/picker.c
)
target_link_libraries(eidos_setup PUBLIC eidos_install)
if(WIN32)
    # SHGetKnownFolderPath and CommandLineToArgvW (shell32), CoTaskMemFree
    # and the COM dialog (ole32), GetOpenFileNameW (comdlg32).
    target_link_libraries(eidos_setup PUBLIC shell32 ole32 comdlg32)
elseif(APPLE)
    enable_language(OBJC)
    target_sources(eidos_setup PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/install/picker_mac.m)
    target_link_libraries(eidos_setup PUBLIC "-framework Cocoa")
else()
    # dlopen of libdbus-1 for the desktop portal; no link-time D-Bus.
    target_link_libraries(eidos_setup PUBLIC ${CMAKE_DL_LIBS})
endif()

# A synthetic cartridge and aletheia's own extraction of it, generated from a
# fixed seed (no game data): tools/synth_rom.py.
set(BW_SYNTH_DIR ${CMAKE_CURRENT_BINARY_DIR}/synth)
add_custom_command(
    OUTPUT ${BW_SYNTH_DIR}/synth.nds ${BW_SYNTH_DIR}/oracle_files.txt
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/synth_rom.py
            --aletheia-root ${ALETHEIA_ROOT} --out ${BW_SYNTH_DIR}
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/tools/synth_rom.py
            ${ALETHEIA_ROOT}/tools/ndsrom.py
            ${ALETHEIA_ROOT}/tools/blz.py
    COMMENT "Generating the synthetic cartridge and its ndsrom.py extraction"
    VERBATIM
)
add_custom_target(bw_synth DEPENDS ${BW_SYNTH_DIR}/synth.nds)

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------

enable_testing()

# The two fixture tests compile the generated header in, so without game
# data there is nothing to build them against; they are still registered, as
# disabled, so the count stays honest.
foreach(t test_text test_stats)
    if(EIDOS_DATA_TESTS)
        add_executable(${t} ${CMAKE_CURRENT_SOURCE_DIR}/tests/${t}.c ${BW_FIXTURES})
        add_dependencies(${t} bw_fixtures)
        target_include_directories(${t} PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
        target_link_libraries(${t} PRIVATE eidos)
        add_test(NAME ${t} COMMAND ${t} ${BW_DATA_ROOT_ABS})
    else()
        add_test(NAME ${t} COMMAND ${CMAKE_COMMAND} -E false)
        set_tests_properties(${t} PROPERTIES DISABLED TRUE)
    endif()
endforeach()

# The oracle tests read their oracle at run time rather than compiling it
# in, so they are built in every configuration (a compile error in one is
# still caught without game data) and only their oracle and their ctest
# entry depend on the data being there.
function(eidos_data_test name)
    if(EIDOS_DATA_TESTS)
        add_dependencies(${name} ${ARGV1})
    else()
        set_tests_properties(${name} PROPERTIES DISABLED TRUE)
    endif()
endfunction()

# The 2D engine test needs the oracle rather than the fixture header, and
# takes the oracle directory as its second argument.
add_executable(test_g2d ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_g2d.c)
target_include_directories(test_g2d PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
target_link_libraries(test_g2d PRIVATE eidos)
add_test(NAME test_g2d COMMAND test_g2d ${BW_DATA_ROOT_ABS} ${BW_G2D_ORACLE_DIR})

# Every overlay function a port file calls by name is in its symbols
# block, so the overlay link gives it a plain name (tools/check_port_names.py).
file(GLOB _eidos_port_c ${CMAKE_CURRENT_SOURCE_DIR}/sdk/*.c
     ${CMAKE_CURRENT_SOURCE_DIR}/engine/platform/*.c ${CMAKE_CURRENT_SOURCE_DIR}/demo/*.c)
add_test(NAME check_port_names COMMAND ${Python3_EXECUTABLE}
         ${CMAKE_CURRENT_SOURCE_DIR}/tools/check_port_names.py ${ALETHEIA_ROOT} ${_eidos_port_c})
eidos_data_test(test_g2d bw_g2d_oracle)

# The BG test reads its screen list out of the oracle's own manifest, so the
# set of screens is changed in one place (BW_BG_SCREENS above).
add_executable(test_bg ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_bg.c)
target_include_directories(test_bg PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
target_link_libraries(test_bg PRIVATE eidos)
add_test(NAME test_bg COMMAND test_bg ${BW_DATA_ROOT_ABS} ${BW_BG_ORACLE_DIR})
eidos_data_test(test_bg bw_bg_oracle)

# The 3D engine test has two halves.  The geometry half reads the OBJ and
# the manifest the oracle wrote and compares every vertex and every
# texture coordinate of two real map models against this engine's own
# display-list decode.  The raster half renders one of them from a fixed
# camera into a BMP in the build directory and asserts non-trivial
# content, plus a set of synthetic checks whose expected pixels are
# computed on paper.  There is no DS frame to compare a picture against
# and the test does not pretend otherwise.
add_executable(test_g3d ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_g3d.c)
target_include_directories(test_g3d PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
target_link_libraries(test_g3d PRIVATE eidos)
add_test(NAME test_g3d COMMAND test_g3d ${BW_DATA_ROOT_ABS} ${BW_G3D_ORACLE_DIR}
                               ${CMAKE_CURRENT_BINARY_DIR}/g3d_house.bmp)
eidos_data_test(test_g3d bw_g3d_oracle)

# The map mesh: builds Route 1 and Accumula Town and renders them to PPM
# files (second argument) for a look; needs only the data root.
add_executable(test_bmap ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_bmap.c)
target_include_directories(test_bmap PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
target_link_libraries(test_bmap PRIVATE eidos)
add_test(NAME test_bmap COMMAND test_bmap ${BW_DATA_ROOT_ABS} ${CMAKE_CURRENT_BINARY_DIR})

# The audio test reads its sequence list out of the audio oracle's own
# manifest, so the set of sequences is changed in one place
# (BW_AUDIO_SEQS above), and writes its own note dump next to the oracle's
# so a failure leaves both files on disk to diff by hand.
add_executable(test_audio ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_audio.c)
target_include_directories(test_audio PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
target_link_libraries(test_audio PRIVATE eidos)
add_test(NAME test_audio COMMAND test_audio ${BW_DATA_ROOT_ABS} ${BW_AUDIO_ORACLE_DIR})
eidos_data_test(test_audio bw_audio_oracle)

# The ARM7 sound driver through the PXI wire (sdk/arm7/snd.h): synthetic
# command lists exercising the sequencer, the shared channels and the
# hardware model, needing no data; and real sequences and a real wave
# copied into main RAM from the SDAT, which needs the data root.
add_executable(test_snd ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_snd.c)
target_include_directories(test_snd PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
target_link_libraries(test_snd PRIVATE eidos)
add_test(NAME test_snd COMMAND test_snd)
add_test(NAME test_snd_data COMMAND test_snd ${BW_DATA_ROOT_ABS})
if(NOT EIDOS_DATA_TESTS)
    set_tests_properties(test_snd_data PROPERTIES DISABLED TRUE)
endif()

# The game's own SND library (aletheia's matched snd_*.o functions) driving
# the same driver.  4-byte pointers only: the matched files' views of
# snd_command.o's .bss agree with each other only there (the test's header
# says how).  Each file defines its address spelling and calls the SDK
# spelling of its neighbours, so the SDK names are defined to the address
# names on the command line, which leaves the files untouched.
if(CMAKE_SIZEOF_VOID_P EQUAL 4)
    set(BW_SND_TEST_MATCHED
        arm9_sdk/PXI_SendWordByFifo.c
        arm9_sdk/PXI_SetFifoRecvCallback.c
        arm9_sdk/func_020885EC.c            # PXI_IsCallbackReady
        arm9_sdk/func_020898A8.c            # SND_CommandInit
        arm9_sdk/func_02089978.c            # SND_RecvCommandReply
        arm9_sdk/func_02089A78.c            # SND_AllocCommand
        arm9_sdk/func_02089B00.c            # SND_PushCommand
        arm9_sdk/SND_FlushCommand.c
        arm9_sdk/func_02089CD4.c            # SND_WaitForCommandProc
        arm9_sdk/func_02089D40.c            # SND_GetCurrentCommandTag
        arm9_sdk/func_02089D6C.c            # SND_IsFinishedCommandTag
        arm9_sdk/func_02089DBC.c            # SND_CountFreeCommand
        arm9_sdk/func_02089DF8.c            # SND_CountReservedCommand
        arm9_sdk/func_02089E34.c            # SND_CountWaitingCommand
        arm9_sdk/func_02089E50.c            # PxiFifoCallback
        arm9_sdk/func_02089E74.c            # InitPXI
        arm9_sdk/func_02089ED8.c            # RequestCommandProc
        arm9_sdk/func_02089F00.c            # AllocCommand
        arm9_sdk/func_02089F48.c            # IsCommandAvailable
        arm9_sdk/func_02089F88.c            # SND_AlarmInit
        arm9_sdk/func_02089FE0.c            # SNDi_SetAlarmHandler
        arm9_sdk/func_0208A010.c            # SNDi_CallAlarmHandler
        arm9_sdk/func_0208A07C.c            # SND_GetChannelStatus
        arm9_sdk/func_0208A0A4.c            # SND_ReadTrackInfo
        arm9_sdk/func_0208A1AC.c            # SNDi_GetFinishedCommandTag
        arm9_sdk/func_0208A1D0.c            # SNDi_InitSharedWork
        arm9_sdk/func_0208A2BC.c            # SND_AssignWaveArc
        arm9_sdk/func_02089804.c            # PushCommand_impl
        arm9_sdk/func_02089350.c            # SND_PrepareSeq
        arm9_sdk/func_02089330.c            # SND_StopSeq
        arm9_sdk/func_020897B4.c            # SNDi_SetPlayerParam
        arm9_sdk/func_0208970C.c            # SND_SetMasterVolume
        arm9_sdk/func_02089540.c            # SND_SetupAlarm
        arm9_sdk/func_020894B4.c            # SND_StartTimer
        arm9_sdk/func_02089794.c            # SND_ReadDriverInfo
    )
    set(_bw_snd_matched)
    foreach(_rel ${BW_SND_TEST_MATCHED})
        list(APPEND _bw_snd_matched ${ALETHEIA_ROOT}/src/matched/${_rel})
    endforeach()
    # The library's own rewrite of these, if a later wave adds them to it,
    # would collide; until then the test owns the copies.
    eidos_dsaddr(_bw_snd_matched ${_bw_snd_matched})
    if(NOT MSVC)
        set_source_files_properties(${_bw_snd_matched} PROPERTIES
            COMPILE_OPTIONS "-Wno-unused-parameter;-Wno-unused-variable;-Wno-missing-prototypes;-Wno-array-bounds")
    else()
        set_source_files_properties(${_bw_snd_matched} PROPERTIES
            COMPILE_OPTIONS "${EIDOS_MATCHED_COMPILE_OPTIONS}")
    endif()
    add_executable(test_snd_matched
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_snd_matched.c
        ${_bw_snd_matched})
    # These renames are the test's own: the same matched copies (PXI_SendWordByFifo,
    # PXI_SetFifoRecvCallback, func_020885EC, func_02089ED8) are compiled by
    # test_pxi, test_tp and test_backup, which want the natural names aletheia
    # gives them.  set_source_files_properties is directory-global, so a rename
    # there would leak into those targets (it did: test_pxi lost
    # PXI_IsCallbackReady and RequestCommandProc).  target_compile_definitions
    # keeps the rename inside this target.
    target_compile_definitions(test_snd_matched PRIVATE
        "InitPXI=func_02089E74;SNDi_InitSharedWork=func_0208A1D0;SND_AllocCommand=func_02089A78;SND_PushCommand=func_02089B00;SND_RecvCommandReply=func_02089978;SNDi_GetFinishedCommandTag=func_0208A1AC;IsCommandAvailable=func_02089F48;AllocCommand=func_02089F00;SND_CountWaitingCommand=func_02089E34;SND_CountFreeCommand=func_02089DBC;SND_CountReservedCommand=func_02089DF8;RequestCommandProc=func_02089ED8;SND_IsFinishedCommandTag=func_02089D6C;PxiFifoCallback=func_02089E50;SNDi_CallAlarmHandler=func_0208A010;PushCommand_impl=func_02089804;SNDi_SetAlarmHandler=func_02089FE0;PXI_IsCallbackReady=func_020885EC;sCommandBss=sFreeList")
    target_include_directories(test_snd_matched PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    target_link_libraries(test_snd_matched PRIVATE eidos)
    add_test(NAME test_snd_matched COMMAND test_snd_matched)
    set_tests_properties(test_snd_matched PROPERTIES TIMEOUT 60)
endif()

# The synthetic engine tests need neither game data nor an oracle: every
# expected pixel in them is computed on paper from the hardware rule under
# test.  They are the counterweight to the oracle comparisons, which can
# only ever show that two implementations agree.
foreach(t test_affine test_bg_affine test_window test_frame test_tcbl test_heap test_vfs test_arcid test_compose3d test_dsmem)
    add_executable(${t} ${CMAKE_CURRENT_SOURCE_DIR}/tests/${t}.c)
    target_include_directories(${t} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    target_link_libraries(${t} PRIVATE eidos)
    add_test(NAME ${t} COMMAND ${t})
endforeach()

if(EIDOS_SOURCE_TREE STREQUAL "game" AND CMAKE_SIZEOF_VOID_P EQUAL 8)
    foreach(t test_arena_native test_dma_native test_heap_records_native test_wire_address test_globals_native)
        add_executable(${t} ${CMAKE_CURRENT_SOURCE_DIR}/tests/${t}.c)
        target_include_directories(${t} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
        target_link_libraries(${t} PRIVATE eidos)
        add_test(NAME ${t} COMMAND ${t})
    endforeach()
endif()
add_test(NAME test_native_port_tools COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_macos_port_tools.py)

# The mod layer's own tests need neither game data nor an oracle either.
# test_mod_surface hashes the frozen ABI surface out of the headers and
# compares it with the number stored beside the version in sdk/mod_api.h
# (docs/mod-abi.md, section 3); test_mod_event drives the dispatch with a
# fake binding and two handlers; test_mod_state reads the state API a mod
# binds to (sdk/mod_state.h) and the contract its calls keep;
# test_mod_declare checks the declared set at run time
# (docs/mod-state.md, section 4); test_mod_messages checks the message
# service's queue (docs/mod-services.md); test_mod_text checks the port's
# text path, the sink the queued text is shown through; test_mod_script
# checks the script command hooks (docs/mod-events.md, section 5);
# test_mod_rule checks the rule interpreter (docs/mod-rules-language.md);
# test_mod_timer checks the timer, the splits and the mod's own random
# source, and test_mod_export the export and import of a Pokemon
# (docs/mod-services.md); test_mod_menu checks the mod menu's model (the
# list, the switch, the order, the clear action, the orphan report and the
# settings); test_mod_panel checks the run-status panel's model and its
# picture, and test_mod_input the input injection; test_mod_handoff checks
# the save handoff (the blob, the pieces, the confirmation and the install)
# and test_mod_restart the run restart (the flags, the two removals and the
# port's own handler); test_mod_avatar checks the field avatars (the pool's
# headroom, the walk and the reset); test_mod_net checks the peer sessions
# and the external control (the relay protocol, the session, the frame and
# the control's endpoint).
foreach(t test_mod_surface test_mod_event test_mod_state test_mod_declare test_mod_messages
          test_mod_text test_mod_script test_mod_rule test_mod_timer test_mod_export
          test_mod_menu test_mod_input test_mod_handoff test_mod_restart
          test_mod_avatar test_mod_net)
    add_executable(${t} ${CMAKE_CURRENT_SOURCE_DIR}/tests/${t}.c)
    target_include_directories(${t} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/sdk ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    target_link_libraries(${t} PRIVATE eidos)
    add_test(NAME ${t} COMMAND ${t})
endforeach()

# Isolate the restart test's backup-chip double from the real ARM7 chip.
# Apple ld can pull that archive member to satisfy other weak references.
target_sources(test_mod_restart PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/sdk/mod_restart.c)
target_compile_definitions(test_mod_restart PRIVATE
    bw_arm7_backup_set_path=test_restart_backup_set_path)

# test_mod_panel drives the run-status panel's model and its picture.  The
# picture and the arrangement it is placed in are the platform layer's own
# files, which the eidos library does not carry, so the test takes them
# itself; none of them needs SDL.
add_executable(test_mod_panel ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_mod_panel.c
    ${CMAKE_CURRENT_SOURCE_DIR}/engine/platform/mod_panel_draw.c
    ${CMAKE_CURRENT_SOURCE_DIR}/engine/platform/geometry.c
    ${CMAKE_CURRENT_SOURCE_DIR}/engine/platform/font.c)
target_include_directories(test_mod_panel PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk ${CMAKE_CURRENT_SOURCE_DIR}/tests
    ${CMAKE_CURRENT_SOURCE_DIR}/engine/platform)
target_link_libraries(test_mod_panel PRIVATE eidos)
add_test(NAME test_mod_panel COMMAND test_mod_panel)

# test_one_layout checks the one-screen layout's geometry (the world view's
# factors and rectangle, the fallback viewport) and the geometry engine's
# clip matrix scale.  The geometry is the platform layer's own file, which
# needs no SDL.
add_executable(test_one_layout ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_one_layout.c
    ${CMAKE_CURRENT_SOURCE_DIR}/engine/platform/mod_panel_draw.c
    ${CMAKE_CURRENT_SOURCE_DIR}/engine/platform/geometry.c
    ${CMAKE_CURRENT_SOURCE_DIR}/engine/platform/font.c)
target_include_directories(test_one_layout PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk ${CMAKE_CURRENT_SOURCE_DIR}/tests
    ${CMAKE_CURRENT_SOURCE_DIR}/engine/platform
    ${CMAKE_CURRENT_SOURCE_DIR}/engine/g3d)
target_link_libraries(test_one_layout PRIVATE eidos)
add_test(NAME test_one_layout COMMAND test_one_layout)

# The rest of the mod layer (docs/mod-implementation.md, section 6, steps 6
# and 7).  test_mod_manifest drives the manifest's parse, the namespace, the
# declared check and the override grammar; test_mod_savedata drives the
# envelope, the versions and the write discipline; test_mod_loader loads a
# real library.
#
# The library the loader's test loads is built from the test's own file: with
# BW_MOD_TEST_LIBRARY defined it is a tiny shared library that binds one
# handler to one event and records what it was handed (docs/mod-abi.md,
# section 1).  It links against nothing: a mod reaches the port through the
# host table alone.
add_library(test_mod_lib MODULE ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_mod_loader.c)
target_compile_definitions(test_mod_lib PRIVATE BW_MOD_LIBRARY=1)
target_compile_definitions(test_mod_lib PRIVATE BW_MOD_TEST_LIBRARY=1)
target_include_directories(test_mod_lib PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk ${CMAKE_CURRENT_SOURCE_DIR}/tests)

foreach(t test_mod_loader test_mod_manifest test_mod_savedata)
    add_executable(${t} ${CMAKE_CURRENT_SOURCE_DIR}/tests/${t}.c)
    target_include_directories(${t} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/sdk ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    target_link_libraries(${t} PRIVATE eidos)
endforeach()
add_dependencies(test_mod_loader test_mod_lib)
# The loader's test needs the library it loads, so the path is the one the
# build made, the way tests/test_view.c is given the viewer it drives.
add_test(NAME test_mod_loader COMMAND test_mod_loader $<TARGET_FILE:test_mod_lib>)
add_test(NAME test_mod_manifest COMMAND test_mod_manifest)
add_test(NAME test_mod_savedata COMMAND test_mod_savedata)

# test_mod_endtoend walks the whole mod path at once: a real library, a real
# manifest, the port's own loader and a real event reaching a handler through
# the port's own dispatch.  The library it loads is built from the test's own
# file, the way test_mod_loader's is: with BW_MOD_TEST_LIBRARY defined it is
# a tiny shared library that binds a handler to an event and records what it
# was handed.
add_library(test_mod_e2e_lib MODULE ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_mod_endtoend.c)
target_compile_definitions(test_mod_e2e_lib PRIVATE BW_MOD_LIBRARY=1)
target_compile_definitions(test_mod_e2e_lib PRIVATE BW_MOD_TEST_LIBRARY=1)
target_include_directories(test_mod_e2e_lib PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk ${CMAKE_CURRENT_SOURCE_DIR}/tests)

add_executable(test_mod_endtoend ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_mod_endtoend.c)
target_include_directories(test_mod_endtoend PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk ${CMAKE_CURRENT_SOURCE_DIR}/tests)
target_link_libraries(test_mod_endtoend PRIVATE eidos)
add_dependencies(test_mod_endtoend test_mod_e2e_lib)
add_test(NAME test_mod_endtoend COMMAND test_mod_endtoend $<TARGET_FILE:test_mod_e2e_lib>)

# test_mod_wasm loads a hand-assembled wasm module through the same loader
# (sdk/mod_wasm.c): the handlers it binds refuse or let an event go on, a
# trap lets it go on, and a broken module is refused.  Without the runtime it
# checks that the module is refused.
add_executable(test_mod_wasm ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_mod_wasm.c)
target_include_directories(test_mod_wasm PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk ${CMAKE_CURRENT_SOURCE_DIR}/tests)
target_link_libraries(test_mod_wasm PRIVATE eidos)
# test_mp drives multiplayer's lobby, start and pause (sdk/mp.c) with a
# network of its own, as the host's game and then as a joiner's.
add_executable(test_mp ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_mp.c)
target_include_directories(test_mp PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk ${CMAKE_CURRENT_SOURCE_DIR}/tests)
target_link_libraries(test_mp PRIVATE eidos)
add_test(NAME test_mp COMMAND test_mp)

add_test(NAME test_mod_wasm COMMAND test_mod_wasm)
# With the runtime, the test also registers an import of its own (an event
# fired from inside a handler).
if(TARGET eidos_wamr)
    target_compile_definitions(test_mod_wasm PRIVATE EIDOS_HAVE_WAMR=1)
endif()

# The registry's overlap check panics, and a panic aborts the process, so it
# is exercised in a run of its own that passes only if the panic happened.
add_test(NAME test_dsmem_overlap COMMAND ${CMAKE_COMMAND}
    -DEXE=$<TARGET_FILE:test_dsmem> -DARG=--overlap
    "-DPATTERN=PANIC in bw_ds_register at 0x02300010"
    -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/expect_panic.cmake)

# The hardware-state compositor is checked two ways: synthetic register and
# VRAM states whose expected pixels are computed on paper, and, with game
# data, the title screen and a rig pose loaded into VRAM, palette and OAM
# the way the SDK would and required to come out pixel-identical to the
# asset path.  The states are written into the real address space
# (sdk/dsmem.c); without game data the second half reports itself skipped.
add_executable(test_hwcompose ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_hwcompose.c)
target_include_directories(test_hwcompose PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
target_link_libraries(test_hwcompose PRIVATE eidos)
add_test(NAME test_hwcompose COMMAND test_hwcompose ${BW_DATA_ROOT_ABS})

# The 3D hardware interface (engine/g3d/hw3d.c) over the real sdk/dsmem.c:
# one display list through the FIFO, the ports and the SDK's sender shims,
# pixel-identical to the interpreter's own rendering, plus GXSTAT, the test
# results and the swap.  Synthetic, no game data.
add_executable(test_g3dhw ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_g3dhw.c)
target_include_directories(test_g3dhw PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
target_link_libraries(test_g3dhw PRIVATE eidos)
add_test(NAME test_g3dhw COMMAND test_g3dhw)

add_executable(test_hires ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_hires.c)
target_include_directories(test_hires PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
target_link_libraries(test_hires PRIVATE eidos)
add_test(NAME test_hires COMMAND test_hires)

# The FS shim, the overlay registry and member-level mod overrides:
# synthetic checks always, and the real-data half when the data root's
# parent holds the extract's tables.
foreach(t test_fs test_overlay test_vfs_members)
    add_executable(${t} ${CMAKE_CURRENT_SOURCE_DIR}/tests/${t}.c)
    target_include_directories(${t} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    target_link_libraries(${t} PRIVATE eidos)
    add_test(NAME ${t} COMMAND ${t} ${BW_DATA_ROOT_ABS})
endforeach()
# Mods that add content: text bank lines, the sound archive merge and
# `.bw` packages.  The first two read the real data when it is there.
foreach(t test_vfs_msg test_vfs_sdat test_mod_bw)
    add_executable(${t} ${CMAKE_CURRENT_SOURCE_DIR}/tests/${t}.c)
    target_include_directories(${t} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests
                                            ${CMAKE_CURRENT_SOURCE_DIR}/engine/audio)
    target_link_libraries(${t} PRIVATE eidos)
endforeach()
add_test(NAME test_vfs_msg COMMAND test_vfs_msg ${BW_DATA_ROOT_ABS})
add_test(NAME test_vfs_sdat COMMAND test_vfs_sdat ${BW_DATA_ROOT_ABS})
add_test(NAME test_mod_bw COMMAND test_mod_bw WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
# A mod's own data tables: the file format and the reads.
add_executable(test_mod_table ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_mod_table.c)
target_include_directories(test_mod_table PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
target_link_libraries(test_mod_table PRIVATE eidos)
add_test(NAME test_mod_table COMMAND test_mod_table WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
# The language-neutral API description (sdk/api/v5, tools/apigen.py): its
# payload offsets compile against the headers, and the committed files are
# the generator's current output.
add_executable(test_api_layout ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_api_layout.c)
target_include_directories(test_api_layout PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/sdk)
target_link_libraries(test_api_layout PRIVATE eidos)
add_test(NAME test_api_layout COMMAND test_api_layout)
add_test(NAME apigen_check
         COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/apigen.py --check
                 --aletheia ${ALETHEIA_ROOT})
# Loading an overlay whose code is not in the build must stop at the load and
# say which overlay and which function.  The process aborts, so the check
# runs through tests/expect_panic.cmake (see its header for why).
add_test(NAME test_overlay_refuses
    COMMAND ${CMAKE_COMMAND} -DCMD=$<TARGET_FILE:test_overlay> -DARG1=--load -DARG2=9
            "-DEXPECT=PANIC in FS_LoadOverlay at 0x02155160: overlay 9 [(]0x02155160[.][.]0x021A8260[)] is not in this build: first missing function [A-Za-z_0-9]+ at 0x021"
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/expect_panic.cmake)

# --- PXI and the ARM7 -------------------------------------------------------
#
# test_pxi drives the FIFO model and every ARM7 responder from the game's own
# matched ARM9 SDK code, on the port's real address space and OS layer
# (bw_port_reset), so it compiles a set of aletheia's matched sources itself
# and links the eidos library for everything else.  The sources go through
# the same tools/dsaddr.py rewrite as the library's (cmake/dsaddr.cmake); a
# source the library already compiles is left to the library, so no file
# gets two rewrite rules.  tests/pxi_doubles.c holds only the ARM9 data
# objects and spelling aliases those files name that the library does not
# define yet.
set(BW_PXI_TEST_MATCHED
    arm9/func_02088498.c                # PXI_Init
    arm9_sdk/PXI_SendWordByFifo.c
    arm9_sdk/PXI_SetFifoRecvCallback.c
    arm9_sdk/func_020885EC.c            # PXI_IsCallbackReady
    arm9_sdk/OS_SetIrqFunction.c
    arm9_sdk/OS_EnableIrqMask.c
    # OS_ResetRequestIrqMask and OSi_SyncWithOtherProc (func_02084AB0) are
    # the OS layer's shims (sdk/os_interrupt.c, sdk/os_system.c), as in the
    # boot, so their matched files are not compiled here.
    arm9_sdk/func_02087B78.c            # the OS's PXI init
    arm9_sdk/OSi_SendToPxi.c
    arm9_sdk/func_02084A38.c            # OSi_CommonCallback
    arm9_sdk/func_02084B90.c            # OS_InitLock
    arm9_sdk/PMi_SendPxiData.c
    arm9_sdk/PMi_TryToSendPxiData.c
    arm9_sdk/PM_SendUtilityCommandAsync.c
    arm9_sdk/PMi_CallCallbackAndUnlock.c
    arm9_sdk/func_0208B840.c            # PMi_DummyCallback
    arm9_sdk/func_02088788.c            # RTC_Init
    arm9_sdk/RtcCommonCallback.c
    arm9_sdk/RtcBCD2HEX.c
    arm9_sdk/RTC_GetDayOfWeek.c
    arm9_sdk/RTCi_WriteRawStatus2Async.c
    arm9_sdk/func_02088F2C.c            # RtcSendPxiCommand
    arm9_sdk/func_02088944.c            # RTC_GetDateTimeAsync
    arm9_sdk/func_020911A0.c            # CTRDG_DummyInit
    arm9_sdk/func_02091150.c            # CTRDGi_SendtoPxi
    arm9_sdk/func_020911D4.c            # CTRDGi_DummyCallback
    arm9_sdk/func_020763BC.c            # CARDi_OnFifoRecv
    arm9_sdk/func_02089ED8.c            # SND RequestCommandProc
)
# pm.o's init and callback view its .bss through 4-byte-pointer offsets
# (and under a second name, s_02153000), so they join only on ILP32.
if(CMAKE_SIZEOF_VOID_P EQUAL 4)
    list(APPEND BW_PXI_TEST_MATCHED
        arm9_sdk/func_0208B8B0.c        # PM_Init
        arm9_sdk/func_0208B950.c        # PMi_CommonCallback
        arm9_sdk/PMi_WaitBusy.c
        arm9_sdk/PM_SendUtilityCommand.c
        arm9_sdk/PM_GetAmp.c)
    set(_bw_pxi_pm_matched 1)
else()
    set(_bw_pxi_pm_matched 0)
endif()
set(_bw_pxi_test_originals)
foreach(_rel ${BW_PXI_TEST_MATCHED})
    set(_src ${ALETHEIA_ROOT}/src/matched/${_rel})
    if(NOT _src IN_LIST BW_MATCHED_ORIGINALS)
        list(APPEND _bw_pxi_test_originals ${_src})
    endif()
endforeach()
eidos_dsaddr(_bw_pxi_test_sources ${_bw_pxi_test_originals})
if(NOT MSVC)
    set_source_files_properties(${_bw_pxi_test_sources} PROPERTIES
        COMPILE_OPTIONS "-Wno-unused-parameter;-Wno-unused-variable;-Wno-missing-prototypes;-Wno-unknown-pragmas;-Wno-array-bounds")
else()
    set_source_files_properties(${_bw_pxi_test_sources} PROPERTIES
        COMPILE_OPTIONS "${EIDOS_MATCHED_COMPILE_OPTIONS};${EIDOS_MATCHED_PRAGMA_OPTION}")
endif()
add_executable(test_pxi
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_pxi.c
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/pxi_doubles.c
    ${_bw_pxi_test_sources})
target_include_directories(test_pxi PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk ${CMAKE_CURRENT_SOURCE_DIR}/tests)
target_compile_definitions(test_pxi PRIVATE BW_TEST_PM_MATCHED=${_bw_pxi_pm_matched})
target_link_libraries(test_pxi PRIVATE eidos)
if(_bw_pxi_pm_matched)
    # One object, two names (see "Two matched files name one DS object").
    eidos_dsaddr_path(_a ${ALETHEIA_ROOT}/src/matched/arm9_sdk/func_0208B8B0.c)
    eidos_dsaddr_path(_b ${ALETHEIA_ROOT}/src/matched/arm9_sdk/func_0208B950.c)
    set_source_files_properties(${_a} ${_b}
        PROPERTIES COMPILE_DEFINITIONS "s_02153000=g_02153000")
endif()
add_test(NAME test_pxi COMMAND test_pxi)
set_tests_properties(test_pxi PROPERTIES TIMEOUT 30)

# --- Firmware settings, the save chip, the firmware flash, MIC and WM -------
#
# Both tests compile a set of aletheia's matched sources as test_pxi does
# (the dsaddr rewrite, the library's own copies left to it).  test_firmware:
# the owner-info readers, at both widths.  test_backup: the PXI senders at
# both widths, and on a 32-bit host the CARD, NVRAM, MIC and WM files whose
# buffer addresses travel as u32 (docs/pointer-width.md).  Each file defines
# its address spelling and calls its neighbours by their SDK names, so the
# SDK names are defined to the address names on the test's command line, as
# for test_snd_matched; the files themselves are untouched.
set(BW_FIRMWARE_TEST_MATCHED
    arm9_sdk/func_02087C88.c            # OS_GetOwnerInfo
    arm9_sdk/func_02087D28.c            # OS_GetOwnerInfoEx
    arm9_sdk/func_0208A998.c            # TP_GetUserInfo
)
set(BW_BACKUP_TEST_MATCHED
    arm9/func_02088498.c                # PXI_Init
    arm9_sdk/PXI_SendWordByFifo.c
    arm9_sdk/PXI_SetFifoRecvCallback.c
    arm9_sdk/func_020885EC.c            # PXI_IsCallbackReady
    arm9_sdk/OS_SetIrqFunction.c
    arm9_sdk/OS_EnableIrqMask.c
)
if(CMAKE_SIZEOF_VOID_P EQUAL 4)
    list(APPEND BW_BACKUP_TEST_MATCHED
        arm9_sdk/func_020760B4.c        # CARDi_InitCommand
        arm9_sdk/func_020763BC.c        # CARDi_OnFifoRecv
        arm9_sdk/func_020763F0.c        # CARDi_Request
        arm9_sdk/func_02076540.c        # CARDi_RequestStreamCommandCore
        arm9_sdk/func_02076A20.c        # CARDi_IdentifyBackupCore
        arm9_sdk/func_02076A70.c        # CARDi_BeginBackupCommand
        arm9_sdk/func_02076AC0.c        # CARDi_RequestStreamCommand
        arm9_sdk/func_02076B2C.c        # CARD_IdentifyBackup
        arm9_sdk/CARDi_IdentifyBackupCore2.c
        arm9_sdk/func_02076170.c        # CARDi_ExecuteOldTypeTask
        arm9_sdk/func_02076230.c        # CARDi_WaitForTask
        arm9_sdk/func_020762A8.c        # CARDi_EndTask
        arm9_sdk/func_02083644.c        # NVRAMi_Init
        arm9_sdk/func_02083704.c        # NVRAMi_ReadAsync
        arm9_sdk/func_02083934.c        # NVRAMi_WriteAsync
        arm9_sdk/func_02083EB8.c        # NVRAMi_VerifyAsync
        arm9_sdk/NVRAMi_Callback.c
        arm9_sdk/NVRAMi_ReadCallback.c
        arm9_sdk/NVRAMi_WriteCallback.c
        arm9_sdk/NVRAMi_VerifyCallback.c
        arm9_sdk/NVRAMi_ReadDataBytes.c
        arm9_sdk/NVRAMi_PageWrite.c
        arm9_sdk/NVRAMi_ReadStatusRegister.c
        arm9_sdk/NVRAMi_WriteEnable.c
        arm9_sdk/NVRAMi_WriteDisable.c
        arm9_sdk/NVRAMi_SoftwareReset.c
        arm9_sdk/NVRAMi_VerifyDataArray.c
        arm9_sdk/NVRAMi_AlarmHandler.c
        arm9_sdk/func_0208B24C.c        # MIC_Init
        arm9_sdk/func_0208B548.c        # MicCommonCallback
        arm9_sdk/func_0208B650.c        # MicStartAutoSampling
        arm9_sdk/func_0208B75C.c        # MicStopAutoSampling
        arm9_sdk/func_0208B2BC.c        # MIC_StartAutoSamplingAsync
        arm9_sdk/func_0208B424.c        # MIC_StopAutoSamplingAsync
        arm9_sdk/func_0208E038.c        # WmInitCore
    )
    set(_bw_backup_card_matched 1)
else()
    set(_bw_backup_card_matched 0)
endif()
foreach(_set FIRMWARE BACKUP)
    set(_originals)
    foreach(_rel ${BW_${_set}_TEST_MATCHED})
        set(_src ${ALETHEIA_ROOT}/src/matched/${_rel})
        if(NOT _src IN_LIST BW_MATCHED_ORIGINALS)
            list(APPEND _originals ${_src})
        endif()
    endforeach()
    eidos_dsaddr(_bw_${_set}_sources ${_originals})
    if(NOT MSVC)
        set_source_files_properties(${_bw_${_set}_sources} PROPERTIES
            COMPILE_OPTIONS "-Wno-unused-parameter;-Wno-unused-variable;-Wno-missing-prototypes;-Wno-unknown-pragmas;-Wno-array-bounds")
    else()
        set_source_files_properties(${_bw_${_set}_sources} PROPERTIES
            COMPILE_OPTIONS "${EIDOS_MATCHED_COMPILE_OPTIONS};${EIDOS_MATCHED_PRAGMA_OPTION}")
    endif()
endforeach()

add_executable(test_firmware
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_firmware.c
    ${_bw_FIRMWARE_sources})
target_include_directories(test_firmware PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk ${CMAKE_CURRENT_SOURCE_DIR}/tests)
target_link_libraries(test_firmware PRIVATE eidos)
add_test(NAME test_firmware COMMAND test_firmware)

add_executable(test_backup
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_backup.c
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/backup_doubles.c
    ${_bw_BACKUP_sources})
target_include_directories(test_backup PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk ${CMAKE_CURRENT_SOURCE_DIR}/tests)
target_compile_definitions(test_backup PRIVATE
    BW_TEST_CARD_MATCHED=${_bw_backup_card_matched}
    PXI_Init=func_02088498
    PXI_InitFifo=func_020884A4
    PXI_IsCallbackReady=func_020885EC
    CARDi_OnFifoRecv=func_020763BC
    CARDi_Request=func_020763F0
    CARDi_RequestStreamCommandCore=func_02076540
    CARDi_IdentifyBackupCore=func_02076A20
    CARDi_BeginBackupCommand=func_02076A70
    CARDi_ExecuteOldTypeTask=func_02076170
    CARDi_WaitForTask=func_02076230
    CARDi_EndTask=func_020762A8
    MicCommonCallback=func_0208B548
    MicStartAutoSampling=func_0208B650
    MicStopAutoSampling=func_0208B75C)
target_link_libraries(test_backup PRIVATE eidos)
if(_bw_backup_card_matched)
    # Per file: CARDi_IdentifyBackupCore2 is static in the vendor object
    # and CARD_IdentifyBackup calls it from another; mic.o's .bss has two
    # names (see "Two matched files name one DS object"); NVRAMi_WriteCallback
    # reaches nvramTemp through the section base (tests/backup_doubles.c);
    # cl.exe has no __attribute__, which one CARD file spells an alignment
    # with (tests/msvc_no_attribute.h says why dropping it is right).
    eidos_dsaddr_path(_f ${ALETHEIA_ROOT}/src/matched/arm9_sdk/CARDi_IdentifyBackupCore2.c)
    set_property(SOURCE ${_f} APPEND PROPERTY COMPILE_DEFINITIONS "static=")
    foreach(_m func_0208B24C func_0208B424)
        eidos_dsaddr_path(_f ${ALETHEIA_ROOT}/src/matched/arm9_sdk/${_m}.c)
        set_property(SOURCE ${_f} APPEND PROPERTY COMPILE_DEFINITIONS "mic_bss=g_02152FE0")
    endforeach()
    eidos_dsaddr_path(_f ${ALETHEIA_ROOT}/src/matched/arm9_sdk/NVRAMi_WriteCallback.c)
    set_property(SOURCE ${_f} APPEND PROPERTY COMPILE_DEFINITIONS "g_02150EA0=(*bw_test_nvram_base)")
    # Two reports on the style of the vendor source, not on its behaviour:
    # a switch over the lock state with no case for "off" (NVRAMi_Callback),
    # and `sizeof(fifoBuf[0]) / sizeof(u16)` read as an element count
    # (WmInitCore, where it is the halfword count it means).
    if(NOT MSVC OR EIDOS_CLANG_CL)
        eidos_dsaddr_path(_f ${ALETHEIA_ROOT}/src/matched/arm9_sdk/NVRAMi_Callback.c)
        set_property(SOURCE ${_f} APPEND PROPERTY COMPILE_OPTIONS "-Wno-switch")
        eidos_dsaddr_path(_f ${ALETHEIA_ROOT}/src/matched/arm9_sdk/func_0208E038.c)
        set_property(SOURCE ${_f} APPEND PROPERTY COMPILE_OPTIONS "-Wno-sizeof-array-div")
    endif()
    if(MSVC AND NOT EIDOS_CLANG_CL)
        eidos_dsaddr_path(_f ${ALETHEIA_ROOT}/src/matched/arm9_sdk/func_02076540.c)
        set_property(SOURCE ${_f} APPEND PROPERTY COMPILE_OPTIONS
                     "/FI${CMAKE_CURRENT_SOURCE_DIR}/tests/msvc_no_attribute.h")
    endif()
endif()
add_test(NAME test_backup COMMAND test_backup)
set_tests_properties(test_backup PROPERTIES TIMEOUT 60)

# --- Input: the keypad, the hinge and the touch panel ------------------------
#
# test_input drives sdk/input.c from host state and a script and reads the
# result back with the game's own per-frame pad update (key.c's
# func_02035F38, compiled as written), and checks the divider unit
# (sdk/cp.c) the touch calibration runs on.
#
# test_tp drives the touch-panel responder (sdk/arm7/tp.c) from aletheia's
# matched ARM9 TP library and the game's touch pass.  tp.o's files view its
# .bss through layouts that agree only with 4-byte pointers (TP_WaitBusy and
# TP_CheckError at fixed offsets 0x3a and 0x38), so on a 64-bit target only
# the four calibration files join and the protocol is checked at the FIFO.
# The PXI files are test_pxi's rewritten copies (the same build directory),
# or the library's where it has them.
function(eidos_matched_test_sources out_var)
    set(_new)
    set(_all)
    foreach(_rel ${ARGN})
        set(_src ${ALETHEIA_ROOT}/src/matched/${_rel})
        if(_src IN_LIST BW_MATCHED_ORIGINALS)
            continue()
        endif()
        if(_src IN_LIST _bw_pxi_test_originals)
            eidos_dsaddr_path(_copy ${_src})
            list(APPEND _all ${_copy})
        else()
            list(APPEND _new ${_src})
        endif()
    endforeach()
    eidos_dsaddr(_copies ${_new})
    if(NOT MSVC)
        set_source_files_properties(${_copies} PROPERTIES COMPILE_OPTIONS
            "-Wno-unused-parameter;-Wno-unused-variable;-Wno-missing-prototypes;-Wno-unknown-pragmas;-Wno-array-bounds")
    else()
        set_source_files_properties(${_copies} PROPERTIES COMPILE_OPTIONS
            "${EIDOS_MATCHED_COMPILE_OPTIONS};${EIDOS_MATCHED_PRAGMA_OPTION}")
    endif()
    list(APPEND _all ${_copies})
    set(${out_var} ${_all} PARENT_SCOPE)
endfunction()

eidos_matched_test_sources(_bw_input_test_sources arm9/func_02035F38.c)
add_executable(test_input
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_input.c
    ${_bw_input_test_sources})
target_include_directories(test_input PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk ${CMAKE_CURRENT_SOURCE_DIR}/tests)
target_link_libraries(test_input PRIVATE eidos)
add_test(NAME test_input COMMAND test_input)
set_tests_properties(test_input PROPERTIES TIMEOUT 30)

set(BW_TP_TEST_MATCHED
    arm9/func_02088498.c                # PXI_Init
    arm9_sdk/PXI_SendWordByFifo.c
    arm9_sdk/PXI_SetFifoRecvCallback.c
    arm9_sdk/func_020885EC.c            # PXI_IsCallbackReady
    arm9_sdk/func_0208A998.c            # TP_GetUserInfo
    arm9_sdk/func_0208AA2C.c            # TP_SetCalibrateParam
    arm9_sdk/func_0208AF18.c            # TP_CalcCalibrateParam
    arm9_sdk/func_0208B104.c            # TP_GetCalibratedPoint
)
if(CMAKE_SIZEOF_VOID_P EQUAL 4)
    list(APPEND BW_TP_TEST_MATCHED
        arm9_sdk/func_0208A924.c        # TP_Init
        arm9_sdk/func_0208AB1C.c        # TP_RequestSamplingAsync
        arm9_sdk/func_0208ABB8.c        # TP_WaitRawResult
        arm9_sdk/func_0208AC0C.c        # TP_RequestAutoSamplingStartAsync
        arm9_sdk/func_0208AD14.c        # TP_RequestAutoSamplingStopAsync
        arm9_sdk/func_0208ADB4.c        # TP_GetLatestRawPointInAuto
        arm9_sdk/func_0208AF08.c        # TP_GetLatestIndexInAuto
        arm9_sdk/func_0208B220.c        # TP_WaitBusy
        arm9_sdk/func_0208B238.c        # TP_CheckError
        arm9_sdk/TPi_TpCallback.c
        arm9/func_020358A0.c)           # the game's touch pass
    set(_bw_tp_matched 1)
else()
    set(_bw_tp_matched 0)
endif()
eidos_matched_test_sources(_bw_tp_test_sources ${BW_TP_TEST_MATCHED})
add_executable(test_tp
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_tp.c
    ${_bw_tp_test_sources})
target_include_directories(test_tp PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk ${CMAKE_CURRENT_SOURCE_DIR}/tests)
target_compile_definitions(test_tp PRIVATE BW_TEST_TP_MATCHED=${_bw_tp_matched})
target_link_libraries(test_tp PRIVATE eidos)
if(_bw_tp_matched)
    # TP_WaitBusy and TP_CheckError name tp.o's .bss `tpState`; the rest
    # name it by address.  One object, two names.
    eidos_dsaddr_path(_a ${ALETHEIA_ROOT}/src/matched/arm9_sdk/func_0208B220.c)
    eidos_dsaddr_path(_b ${ALETHEIA_ROOT}/src/matched/arm9_sdk/func_0208B238.c)
    set_source_files_properties(${_a} ${_b}
        PROPERTIES COMPILE_DEFINITIONS "tpState=g_02152FA4")
    if(MSVC)
        # As written: TPi_TpCallback's raw-sample union holds an anonymous
        # struct (C11; C4201 at /W4), and the game's touch pass reads `cal`
        # whose fields the compiler cannot prove written on every path
        # (C4701).
        eidos_dsaddr_path(_c ${ALETHEIA_ROOT}/src/matched/arm9_sdk/TPi_TpCallback.c)
        eidos_dsaddr_path(_d ${ALETHEIA_ROOT}/src/matched/arm9/func_020358A0.c)
        set_property(SOURCE ${_c} APPEND PROPERTY COMPILE_OPTIONS /wd4201)
        set_property(SOURCE ${_d} APPEND PROPERTY COMPILE_OPTIONS /wd4701)
    endif()
endif()
add_test(NAME test_tp COMMAND test_tp)
set_tests_properties(test_tp PROPERTIES TIMEOUT 30)


# The OS layer's tests, on the real address space (sdk/dsmem.c) and the
# port's power-on (bw_port_reset).  test_os_irq also compiles aletheia's
# matched OS_GetIrqFunction.c as written, the table reader beside the
# library's OS_SetIrqFunction.c that the shim's dispatcher has to agree
# with.
foreach(t test_os_thread test_os_irq test_os_alarm)
    add_executable(${t} ${CMAKE_CURRENT_SOURCE_DIR}/tests/${t}.c)
    target_include_directories(${t} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    target_link_libraries(${t} PRIVATE eidos)
    add_test(NAME ${t} COMMAND ${t})
    set_tests_properties(${t} PROPERTIES TIMEOUT 60)
endforeach()
target_sources(test_os_irq PRIVATE
    ${ALETHEIA_ROOT}/src/matched/arm9_sdk/OS_GetIrqFunction.c)

# sdk/asm/: the asm functions' C.  test_asmsdk checks the half in the
# library on the real address space (and, with game data, MI_UncompressLZ8
# on real archive members); test_asmsdk_glue links the boot half alone
# against recording doubles of the game code it calls.
add_executable(test_asmsdk ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_asmsdk.c)
target_include_directories(test_asmsdk PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
target_link_libraries(test_asmsdk PRIVATE eidos)
add_test(NAME test_asmsdk COMMAND test_asmsdk ${BW_DATA_ROOT_ABS})
add_executable(test_asmsdk_glue
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_asmsdk_glue.c
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/asm/func_02034F74.c
    ${BW_ASMSDK_BOOT_SOURCES})
target_include_directories(test_asmsdk_glue PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk ${CMAKE_CURRENT_SOURCE_DIR}/tests)
add_test(NAME test_asmsdk_glue COMMAND test_asmsdk_glue)

add_executable(test_extract ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_extract.c)
target_include_directories(test_extract PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
target_link_libraries(test_extract PRIVATE eidos_install)
add_dependencies(test_extract bw_synth)
add_test(NAME test_extract COMMAND test_extract ${BW_SYNTH_DIR})

# The data root's location, check, config file, reset and move, and the
# headless first run, on synthetic trees and the synthetic cartridge.  The
# test sets its own eidos home, so it never reads or writes the real one;
# BW_DATA_ROOT is cleared because it outranks everything the test sets up.
# Not named test_setup: 32-bit Windows treats an executable with "setup"
# (or "install", "update") in its name as an installer and demands
# elevation before it will start it (UAC installer detection).
add_executable(test_dataroot ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_dataroot.c)
target_include_directories(test_dataroot PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
target_link_libraries(test_dataroot PRIVATE eidos_setup)
add_dependencies(test_dataroot bw_synth)
add_test(NAME test_dataroot
         COMMAND test_dataroot ${BW_SYNTH_DIR} ${CMAKE_CURRENT_BINARY_DIR}/setup_work)
set_tests_properties(test_dataroot PROPERTIES ENVIRONMENT "BW_DATA_ROOT=;EIDOS_HOME=")

# The native file dialog opened for real and answered by a helper thread
# (Windows).  It shows a window on the desktop, so it runs only with
# EIDOS_TEST_DIALOGS=1 and is skipped otherwise.
add_executable(test_picker ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_picker.c)
target_include_directories(test_picker PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
target_link_libraries(test_picker PRIVATE eidos_setup)
add_test(NAME test_picker
         COMMAND test_picker ${CMAKE_CURRENT_BINARY_DIR}/picker_work)
set_tests_properties(test_picker PROPERTIES SKIP_REGULAR_EXPRESSION "SKIPPED:" TIMEOUT 120)

# The extractor on the player's real ROM, named by BW_ROM at test time,
# checked file for file against the extract/ tree that holds BW_DATA_ROOT
# (aletheia's own ndsrom.py + delink.py output).  Skipped, not failed,
# without BW_ROM or without that tree; see tests/extract_rom.cmake.
get_filename_component(BW_EXTRACT_ROOT "${BW_DATA_ROOT_ABS}" DIRECTORY)
add_test(NAME test_extract_rom
         COMMAND ${CMAKE_COMMAND} -DEXTRACT=$<TARGET_FILE:eidos_extract>
                 -DREFERENCE=${BW_EXTRACT_ROOT}
                 -DWORK=${CMAKE_CURRENT_BINARY_DIR}/rom_extract
                 -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/extract_rom.cmake)
set_tests_properties(test_extract_rom PROPERTIES
    SKIP_REGULAR_EXPRESSION "SKIPPED:" TIMEOUT 900)

# --- overlays compiled in (tools/ovlink.py, sdk/overlay_image.c) -------------
#
# test_ovlink: the synthetic tree in tests/ovl_synth/ (two overlays sharing a
# window, a third calling into it, an ARM9 caller), built with the same
# eidos_overlays() the boot uses; the overlay files are byte patterns the
# test makes.  No game data.
add_executable(test_ovlink
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_ovlink.c
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/ovl_synth/src/matched/arm9/func_02100000.c)
target_include_directories(test_ovlink PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
target_link_libraries(test_ovlink PRIVATE eidos)
eidos_overlays(synth
    TARGET test_ovlink
    ROOT ${CMAKE_CURRENT_SOURCE_DIR}/tests/ovl_synth
    OVERLAYS 0 1 2 230
    LINK_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/tests/ovl_synth/src/matched/arm9/func_02100000.c)
add_test(NAME test_ovlink COMMAND test_ovlink)
add_test(NAME test_ovlink_stub COMMAND ${CMAKE_COMMAND}
    -DCMD=$<TARGET_FILE:test_ovlink> -DARG1=--stub
    "-DEXPECT=PANIC in overlay stub at 0x02200030: overlay 0: function 0x02200030 is not in this build"
    -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/expect_panic.cmake)
add_test(NAME test_ovlink_none COMMAND ${CMAKE_COMMAND}
    -DCMD=$<TARGET_FILE:test_ovlink> -DARG1=--none
    "-DEXPECT=PANIC in overlay trampoline at 0x02200000: func_02200000: called with no overlay resident"
    -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/expect_panic.cmake)

# test_ovlink_real: ov088 and ov179, every matched source of each, linked
# against the eidos library with the ARM9 functions it lacks stubbed, then
# loaded from the player's extract through FS_LoadOverlay.  Always built (the
# metadata is always there); without the extract it reports itself skipped.
set(BW_OVL_LIBRARY_SOURCES
    ${BW_MATCHED_ORIGINALS} ${BW_SDK_SOURCES} ${BW_STUB_SOURCES} ${BW_ENGINE_SOURCES})
set(_real_overlay_default ON)
if(EIDOS_SOURCE_TREE STREQUAL "game" AND NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    # startmenu_types.h asserts DS pointer layouts. Keep the real overlay
    # test available explicitly, but do not build it as a native LP64 test.
    set(_real_overlay_default OFF)
endif()
option(EIDOS_REAL_OVERLAY_TESTS "Build the real title overlay tests" ${_real_overlay_default})
if(EIDOS_REAL_OVERLAY_TESTS)
add_executable(test_ovlink_real ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_ovlink_real.c)
target_include_directories(test_ovlink_real PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
target_link_libraries(test_ovlink_real PRIVATE eidos)
eidos_overlays(title_test
    TARGET test_ovlink_real
    OVERLAYS 88 179
    LINK_SOURCES ${BW_OVL_LIBRARY_SOURCES}
    ARM9_STUBS)
add_test(NAME test_ovlink_real COMMAND test_ovlink_real ${BW_DATA_ROOT_ABS})
set_tests_properties(test_ovlink_real PROPERTIES SKIP_REGULAR_EXPRESSION "SKIPPED:")
add_test(NAME test_ovlink_real_stub COMMAND ${CMAKE_COMMAND}
    -DCMD=$<TARGET_FILE:test_ovlink_real> -DARG1=${BW_DATA_ROOT_ABS} -DARG2=--arm9
    "-DEXPECT=PANIC in overlay ARM9 stub at 0x0202F0B0: TIME_ICON_Update: ARM9 function an overlay calls"
    -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/expect_panic.cmake)
if(NOT EIDOS_DATA_TESTS)
    set_tests_properties(test_ovlink_real_stub PROPERTIES DISABLED TRUE)
endif()
else()
    add_test(NAME test_ovlink_real COMMAND ${CMAKE_COMMAND} -E false)
    add_test(NAME test_ovlink_real_stub COMMAND ${CMAKE_COMMAND} -E false)
    set_tests_properties(test_ovlink_real test_ovlink_real_stub PROPERTIES DISABLED TRUE)
endif()

if(APPLE)
    add_test(NAME test_macos_link COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_macos_link.py)
    add_test(NAME test_host_wasi COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_host_wasi.py)
    add_test(NAME test_host_fiber COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_host_fiber.py)
endif()

# --- the ARM9 image's data (sdk/armdata.c, tools/armdata.py) ----------------
#
# test_armdata_gen: the generator against a synthetic aletheia tree written
# by the test itself (no game data, no real metadata).
add_test(NAME test_armdata_gen
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_armdata.py
            --tool=${CMAKE_CURRENT_SOURCE_DIR}/tools/armdata.py)

# test_armdata: the runtime against a synthetic image and hand-written
# tables (copy, zero, bind, relocate, the mismatch and LP64 refusals).
add_executable(test_armdata ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_armdata.c)
target_include_directories(test_armdata PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
target_link_libraries(test_armdata PRIVATE eidos)
add_test(NAME test_armdata COMMAND test_armdata)
add_test(NAME test_armdata_trap COMMAND ${CMAKE_COMMAND}
    -DCMD=$<TARGET_FILE:test_armdata> -DARG1=--trap
    "-DEXPECT=PANIC in armdata at 0x02012344: called through a pointer"
    -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/expect_panic.cmake)

# test_armdata_real: tables generated from aletheia's real metadata for a
# handful of matched sources, four of which are compiled here with the
# forced-include header, then loaded from the player's own arm9_dec.bin.
# The metadata is always there, so it always builds; without the image the
# run reports itself skipped.
set(BW_ARMDATA_REAL_MATCHED
    ${ALETHEIA_ROOT}/src/matched/arm9/func_0202F9F4.c   # the three entries of
    ${ALETHEIA_ROOT}/src/matched/arm9/func_0202F9F8.c   # the proc table at
    ${ALETHEIA_ROOT}/src/matched/arm9/func_0202FAB0.c   # 0x020A0480
    ${ALETHEIA_ROOT}/src/matched/arm9/func_02005440.c   # stores to g_020A9E80
)
eidos_dsaddr(BW_ARMDATA_REAL_COPIES ${BW_ARMDATA_REAL_MATCHED})
set(BW_ARMDATA_REAL_NAMED                               # names only, not compiled
    ${ALETHEIA_ROOT}/src/matched/arm9/sub_0200545C.c     # _ArcPathTable 0x020A6C58
    ${ALETHEIA_ROOT}/src/matched/arm9/unit_0200560C.c    # sub_02005610_Sys  } one address
    ${ALETHEIA_ROOT}/src/matched/arm9/unit_020056A0.c    # sub_020056A0_Sys  }
    ${ALETHEIA_ROOT}/src/matched/arm9_sdk/func_0208459C.c   # SDK_AUTOLOAD_DTCM_START
    ${ALETHEIA_ROOT}/src/matched/arm9_sdk/func_02085264.c   # SDK_IRQ_STACKSIZE, port threads
    ${ALETHEIA_ROOT}/src/matched/arm9_sdk/func_02030524.c   # d_02146954 = g_EnvModule
)
string(REPLACE ";" "
" _armdata_real "${BW_ARMDATA_REAL_COPIES};${BW_ARMDATA_REAL_NAMED}")
file(GENERATE OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/test_armdata_real_sources.txt
     CONTENT "$<JOIN:$<TARGET_PROPERTY:eidos,SOURCES>,
>
${_armdata_real}
")
eidos_armdata(test_armdata_real
    SOURCES_LIST ${CMAKE_CURRENT_BINARY_DIR}/test_armdata_real_sources.txt
    MATCHED ${BW_ARMDATA_REAL_COPIES})
if(NOT MSVC)
    set_property(SOURCE ${BW_ARMDATA_REAL_COPIES} APPEND PROPERTY COMPILE_OPTIONS
        "-Wno-unused-parameter;-Wno-unused-variable;-Wno-missing-prototypes;-Wno-array-bounds")
else()
    set_property(SOURCE ${BW_ARMDATA_REAL_COPIES} APPEND PROPERTY COMPILE_OPTIONS
        "${EIDOS_MATCHED_COMPILE_OPTIONS}")
endif()
add_executable(test_armdata_real
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_armdata_real.c
    ${BW_ARMDATA_REAL_COPIES}
    ${test_armdata_real_C})
target_include_directories(test_armdata_real PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
target_link_libraries(test_armdata_real PRIVATE eidos)
add_test(NAME test_armdata_real COMMAND test_armdata_real ${BW_DATA_ROOT_ABS})

# tools/dsaddr.py: every cast form, the prelude against sdk/dsmem.h on this
# compiler, every matched source in this build parsed after its rewrite,
# and a dry run over the whole of aletheia's src/matched (idempotent, no
# line moves).  The source list is the one this configuration compiles.
string(REPLACE ";" "\n" _dsaddr_list "${BW_MATCHED_ORIGINALS}")
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/dsaddr_sources.txt "${_dsaddr_list}\n")
if(MSVC)
    set(_dsaddr_cc_id MSVC)
else()
    set(_dsaddr_cc_id GNU)
endif()
add_test(NAME test_dsaddr
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_dsaddr.py
            --aletheia-root=${ALETHEIA_ROOT}
            --sources=${CMAKE_CURRENT_BINARY_DIR}/dsaddr_sources.txt
            --cc=${CMAKE_C_COMPILER}
            --cc-id=${_dsaddr_cc_id}
            "--cflags=${CMAKE_C_FLAGS}")

# tools/closure.py's source reader and tools/bootlink.py's #if evaluation
# and implicit declarations, on synthetic C (no aletheia sources needed).
add_test(NAME test_closure
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_closure.py)

# ---------------------------------------------------------------------------
# eidos_view -- the demo, and the first visible output
# ---------------------------------------------------------------------------
#
# The top screen is titledemo.narc member 0 over the NCGR/NCLR pair
# background.py's coverage rule paired with it (members 1 and 2, the same
# triple tests/test_bg.c checks pixel for pixel); the bottom screen is one
# composed rig pose out of a/0/0/4.  Both come from the player's own extract/;
# nothing is bundled.
#
# test_view runs it with SDL_VIDEODRIVER=dummy and SDL_AUDIODRIVER=dummy, so
# ctest checks the picture on a machine with no display and no sound card.
#
if(EIDOS_SDL_FOUND)
    # The first-run setup window (engine/platform/wizard.c) and its bitmap
    # font, over install/setup.h.  eidos_view finds its data the way the
    # game will, through eidos_firstrun: --data-root, BW_DATA_ROOT, the
    # saved setting, the platform default, else this window.
    add_library(eidos_wizard STATIC
        ${CMAKE_CURRENT_SOURCE_DIR}/engine/platform/wizard.c
    )
    target_include_directories(eidos_wizard PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/engine/platform)
    # The port's bitmap font is the platform layer's now: the first-run
    # window and the run-status panel both draw with it.
    target_link_libraries(eidos_wizard PUBLIC eidos_setup eidos_sdl3 eidos_platform)

    add_executable(eidos_view ${CMAKE_CURRENT_SOURCE_DIR}/demo/eidos_view.c)
    target_link_libraries(eidos_view PRIVATE eidos_platform eidos_wizard)

    # SDL3.dll has to sit next to the executable on Windows: there is no
    # installer and nothing on PATH.
    if(EIDOS_SDL_RUNTIME)
        add_custom_command(TARGET eidos_view POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${EIDOS_SDL_RUNTIME}" "$<TARGET_FILE_DIR:eidos_view>"
            COMMENT "Copying SDL3.dll next to eidos_view")
    endif()

    # eidos_spots -- the battle spot editor (docs/battle-backdrop.md): the
    # map of any battle place, placed as the battle backdrop places it, with
    # the battle and the field previews and the controls to set the spot.
    # A tool for working on eidos, not part of the game: not built by default
    # and not packaged (build it with `ninja eidos_spots.exe`).
    add_executable(eidos_spots EXCLUDE_FROM_ALL ${CMAKE_CURRENT_SOURCE_DIR}/tools/spot-editor/eidos_spots.c
        ${CMAKE_CURRENT_SOURCE_DIR}/engine/world/bmap_place.c
        ${CMAKE_CURRENT_SOURCE_DIR}/engine/world/bmap_spots.c
        ${CMAKE_CURRENT_SOURCE_DIR}/engine/world/bmap_gimmick.c
        ${CMAKE_CURRENT_SOURCE_DIR}/engine/world/bmap_data.c
        ${CMAKE_CURRENT_SOURCE_DIR}/engine/world/bmap_npc.c
        ${CMAKE_CURRENT_SOURCE_DIR}/engine/world/bcam_sim.c
        ${CMAKE_CURRENT_SOURCE_DIR}/engine/ui/kit.c
        ${CMAKE_CURRENT_SOURCE_DIR}/engine/ui/assets.c
        ${CMAKE_CURRENT_SOURCE_DIR}/engine/ui/assets_icons.c
        ${CMAKE_CURRENT_SOURCE_DIR}/engine/ui/assets_sprites.c)
    # the kit's own copy: the interface layer it calls into (engine/ui/ui.c)
    # is the game's, so the editor answers its two hooks itself
    target_include_directories(eidos_spots PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/engine/world
        ${CMAKE_CURRENT_SOURCE_DIR}/engine/ui
        ${CMAKE_CURRENT_SOURCE_DIR}/engine/g3d
        ${CMAKE_CURRENT_SOURCE_DIR}/engine/platform
        ${CMAKE_CURRENT_SOURCE_DIR}/install)
    target_link_libraries(eidos_spots PRIVATE eidos_platform eidos_wizard)
    # bmap_npc_dump -- writes the NPC pictures of a zone as BMP files (a check
    # for engine/world/bmap_npc.c)
    add_executable(bmap_npc_dump EXCLUDE_FROM_ALL ${CMAKE_CURRENT_SOURCE_DIR}/demo/bmap_npc_dump.c
        ${CMAKE_CURRENT_SOURCE_DIR}/engine/world/bmap_npc.c
        ${CMAKE_CURRENT_SOURCE_DIR}/engine/world/bmap_gimmick.c
        ${CMAKE_CURRENT_SOURCE_DIR}/engine/world/bmap_data.c)
    target_include_directories(bmap_npc_dump PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/engine/world
        ${CMAKE_CURRENT_SOURCE_DIR}/engine/platform
        ${CMAKE_CURRENT_SOURCE_DIR}/install)
    target_link_libraries(bmap_npc_dump PRIVATE eidos_platform)
    # bcam_check -- compares the battle camera simulator (engine/world/bcam_sim.c)
    # with a recording of the game's camera
    add_executable(bcam_check EXCLUDE_FROM_ALL ${CMAKE_CURRENT_SOURCE_DIR}/demo/bcam_check.c
        ${CMAKE_CURRENT_SOURCE_DIR}/engine/world/bcam_sim.c)
    target_include_directories(bcam_check PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/engine/world
        ${CMAKE_CURRENT_SOURCE_DIR}/engine/platform
        ${CMAKE_CURRENT_SOURCE_DIR}/install)
    target_link_libraries(bcam_check PRIVATE eidos_platform)
    if(EIDOS_SDL_RUNTIME)
        add_custom_command(TARGET eidos_spots POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${EIDOS_SDL_RUNTIME}" "$<TARGET_FILE_DIR:eidos_spots>"
            COMMENT "Copying SDL3.dll next to eidos_spots")
    endif()

    add_executable(test_view ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_view.c)
    target_include_directories(test_view PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    target_link_libraries(test_view PRIVATE eidos)
    add_dependencies(test_view eidos_view)
    add_test(NAME test_view
             COMMAND test_view $<TARGET_FILE:eidos_view> ${BW_DATA_ROOT_ABS}
                     ${CMAKE_CURRENT_BINARY_DIR}/eidos_view.bmp)
    set_tests_properties(test_view PROPERTIES
        ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy;EIDOS_SETTINGS=${CMAKE_CURRENT_BINARY_DIR}/test_view_settings.ini")
    if(NOT EIDOS_DATA_TESTS)
        set_tests_properties(test_view PROPERTIES DISABLED TRUE)
    endif()

    # The platform layer's input tables and window-to-panel mapping, under
    # the dummy video driver; no game data.
    add_executable(test_platform_input
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_platform_input.c)
    target_include_directories(test_platform_input PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    target_link_libraries(test_platform_input PRIVATE eidos_platform)
    if(EIDOS_SDL_RUNTIME)
        add_custom_command(TARGET test_platform_input POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${EIDOS_SDL_RUNTIME}" "$<TARGET_FILE_DIR:test_platform_input>"
            COMMENT "Copying SDL3.dll next to test_platform_input")
    endif()
    add_test(NAME test_platform_input COMMAND test_platform_input)
    set_tests_properties(test_platform_input PROPERTIES
        ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy")

    # The GPU 3D rasteriser against the software one (a hidden OpenGL 3.3
    # window; skips where there is no window system).
    add_executable(test_gpu3d ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_gpu3d.c)
    target_include_directories(test_gpu3d PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    target_link_libraries(test_gpu3d PRIVATE eidos_platform)
    if(EIDOS_SDL_RUNTIME)
        add_custom_command(TARGET test_gpu3d POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${EIDOS_SDL_RUNTIME}" "$<TARGET_FILE_DIR:test_gpu3d>"
            COMMENT "Copying SDL3.dll next to test_gpu3d")
    endif()
    add_test(NAME test_gpu3d COMMAND test_gpu3d)

    # The settings backend: geometry and touch mapping for every layout and
    # scale, bindings and capture, settings.ini, overrides, the volume gain.
    add_executable(test_settings
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_settings.c)
    target_include_directories(test_settings PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    target_link_libraries(test_settings PRIVATE eidos_platform)
    if(EIDOS_SDL_RUNTIME)
        add_custom_command(TARGET test_settings POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${EIDOS_SDL_RUNTIME}" "$<TARGET_FILE_DIR:test_settings>"
            COMMENT "Copying SDL3.dll next to test_settings")
    endif()
    add_test(NAME test_settings COMMAND test_settings)
    set_tests_properties(test_settings PROPERTIES
        ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy;EIDOS_SETTINGS=${CMAKE_CURRENT_BINARY_DIR}/test_settings_default.ini")

    # The settings screen's model: tabs, rows, navigation, revert, commit.
    add_executable(test_settings_ui
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_settings_ui.c)
    target_include_directories(test_settings_ui PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    target_link_libraries(test_settings_ui PRIVATE eidos_platform)
    if(EIDOS_SDL_RUNTIME)
        add_custom_command(TARGET test_settings_ui POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${EIDOS_SDL_RUNTIME}" "$<TARGET_FILE_DIR:test_settings_ui>"
            COMMENT "Copying SDL3.dll next to test_settings_ui")
    endif()
    add_test(NAME test_settings_ui COMMAND test_settings_ui)
    set_tests_properties(test_settings_ui PROPERTIES
        ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy;EIDOS_SETTINGS=${CMAKE_CURRENT_BINARY_DIR}/test_settings_ui_default.ini")

    # The setup window under the dummy video driver, driven by scripted
    # events: an archive dropped and refused, a mouse click on "Choose
    # another file", the synthetic cartridge refused as not the original
    # game, then installed with the test-only override, Start; Escape and
    # closing the window.  With BW_ROM set it also installs the real ROM
    # through a drop, with no override.
    add_executable(test_wizard ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_wizard.c)
    target_include_directories(test_wizard PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    target_link_libraries(test_wizard PRIVATE eidos_wizard)
    add_dependencies(test_wizard bw_synth eidos_view)
    add_test(NAME test_wizard
             COMMAND test_wizard ${BW_SYNTH_DIR} ${CMAKE_CURRENT_BINARY_DIR}/wizard_work)
    set_tests_properties(test_wizard PROPERTIES TIMEOUT 600
        ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy;BW_DATA_ROOT=;EIDOS_HOME=")

    # The whole first run on the player's real ROM, without a window:
    # eidos_view --rom makes the data in a scratch eidos home and draws
    # from it; the next start finds it through the saved setting;
    # --move-data and --reset-data.  Skipped without BW_ROM.
    add_test(NAME test_firstrun_rom
             COMMAND ${CMAKE_COMMAND} -DVIEW=$<TARGET_FILE:eidos_view>
                     -DWORK=${CMAKE_CURRENT_BINARY_DIR}/firstrun_rom
                     -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/firstrun_rom.cmake)
    set_tests_properties(test_firstrun_rom PROPERTIES
        SKIP_REGULAR_EXPRESSION "SKIPPED:" TIMEOUT 900
        ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy;BW_DATA_ROOT=")
endif()

# ---------------------------------------------------------------------------
# eidos_boot -- the game's own ARM9 boot (docs/boot.md)
# ---------------------------------------------------------------------------
#
# demo/eidos_boot.c runs NitroMain on the OS layer, the frame loop as its
# halt hook.  It is always built; what it links depends on EIDOS_BOOT:
#
#   OFF (default)  only the parts that link today.  The executable reports
#                  that the boot is not linked and exits 0.
#   ON             NitroMain and the closure of its matched callees, with
#                  the names, data bindings and stubs tools/bootlink.py and
#                  tools/armdata.py generate to make it link.
#                  cmake/boot.cmake has the details, including the one list
#                  of matched files the port's shims replace.
option(EIDOS_BOOT
       "Link the game's own boot (NitroMain and its closure) into eidos_boot" OFF)

add_executable(eidos_boot ${CMAKE_CURRENT_SOURCE_DIR}/demo/eidos_boot.c)
target_include_directories(eidos_boot PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/engine/platform)
target_link_libraries(eidos_boot PRIVATE eidos)
# A release build (docs/release.md) compiles in no data root: the build
# machine's path means nothing to a player, so the game looks in the user
# folder (docs/installer.md) and asks for the ROM when there is none.
if(EIDOS_RELEASE)
    set(_eidos_default_data_root "")
else()
    set(_eidos_default_data_root "${BW_DATA_ROOT_ABS}")
endif()
target_compile_definitions(eidos_boot PRIVATE
    EIDOS_DEFAULT_DATA_ROOT="${_eidos_default_data_root}")
# A release build on Windows is a windowed program: no console opens beside
# the game.  `eidos --console` opens one for a report (sdk/plat_crash.h).
# Development builds keep theirs.
if(WIN32 AND EIDOS_RELEASE)
    target_link_options(eidos_boot PRIVATE -mwindows)
endif()
# The wasm32 build (docs/lp64.md): the module the host program runs.  Its
# own data and heap start at 128 MiB, so the DS's windows below are free
# for the identity mapping (sdk/host_vm.c); what it calls of SDL3 and GL is
# left to the host as imports (--import-undefined: an undefined function
# becomes an import, an undefined object stays an error, where
# --allow-undefined would give it address 0); the host keeps each fiber's
# stack pointer.
if(CMAKE_SYSTEM_NAME STREQUAL "WASI")
    set_target_properties(eidos_boot PROPERTIES SUFFIX ".wasm")
    target_link_options(eidos_boot PRIVATE
        "-Wl,--global-base=134217728"
        "-Wl,--initial-memory=268435456"
        "-Wl,--max-memory=2147483648"
        "-Wl,-z,stack-size=8388608"
        "-Wl,--import-undefined"
        "-Wl,--export=__stack_pointer"
        "-Wl,--export=__wrap_malloc"
        "-Wl,--export=__wrap_free"
        "-Wl,--no-gc-sections")
    # A call whose prototype differs from the definition's links to a
    # trapping stub; tools/wasm_sigfix.py makes each stub call the
    # definition the way the DS would.  The definitions stay in the module
    # (no section garbage collection) for the calls that only reach them
    # through such a stub.
    # The heap behind the host's lock, and a sleep that lets the other
    # fibers run (sdk/wasm_heap.c).
    target_sources(eidos_boot PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/sdk/wasm_heap.c)
    foreach(_f malloc calloc realloc free posix_memalign aligned_alloc nanosleep)
        target_link_options(eidos_boot PRIVATE "-Wl,--wrap=${_f}")
    endforeach()
    add_custom_command(TARGET eidos_boot POST_BUILD
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/wasm_sigfix.py
                $<TARGET_FILE:eidos_boot> -o $<TARGET_FILE:eidos_boot>
        VERBATIM)
endif()
# The executable's icon on Windows (assets/eidos.rc; tools/mkicon.py makes
# assets/icon.ico from assets/icon.png).
if(WIN32)
    enable_language(RC)
    target_sources(eidos_boot PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/assets/eidos.rc)
endif()

# A linker map, so a crash report's `exe+0x...` names its function.  The
# boot's crash filter (sdk/plat_crash.c) reports the executable offset; the
# map is what turns it into a symbol (a crash log without one names no
# function).  GNU ld uses -Map, Apple ld uses -map, MSVC uses /MAP:.
# It lands at <build>/eidos_boot.map.
if(MSVC)
    target_link_options(eidos_boot PRIVATE
        "/MAP:${CMAKE_BINARY_DIR}/eidos_boot.map")
elseif(APPLE)
    target_link_options(eidos_boot PRIVATE
        "-Wl,-map,${CMAKE_BINARY_DIR}/eidos_boot.map")
else()
    target_link_options(eidos_boot PRIVATE
        "-Wl,-Map,${CMAKE_BINARY_DIR}/eidos_boot.map")
endif()

# The game's keyboard opened for the port's own texts asks its own
# question (engine/platform/mod_options_proc.c, "The keyboard's
# question"): the message read goes through the port first.  GNU ld only;
# elsewhere the keyboard keeps the game's question.
# Apple ld has no --wrap support. Keep all four GNU-only hooks below out
# of Mach-O builds, including their __real_* references.
if(NOT MSVC AND NOT APPLE AND TARGET eidos_platform)
    target_link_options(eidos_boot PRIVATE "-Wl,--wrap=MSGDATA_ReadString")
    target_compile_definitions(eidos_platform PRIVATE EIDOS_WRAP_MSGDATA=1)
endif()

# The one-screen interface's sounds play on the game's own main thread:
# PM_VOICE_StopFinished runs once a frame in the game's frame loop
# (GFL_USE_UpdateFrameSubsystems, right after the sound update), and the
# interface's queued sounds go through it first (engine/ui/ui.c,
# eidos_ui_game_frame).  GNU ld only, and only with the game's boot.
if(NOT MSVC AND NOT APPLE AND EIDOS_BOOT AND TARGET eidos_platform)
    target_link_options(eidos_boot PRIVATE "-Wl,--wrap=PM_VOICE_StopFinished")
    target_compile_definitions(eidos_platform PRIVATE EIDOS_WRAP_PMVOICE=1)
endif()

# Instant text (Extras): the game's text streams start, and change speed,
# through the port first (engine/platform/text_instant.c).  GNU ld only, and
# only with the game's boot and the settings in the link.
if(NOT MSVC AND NOT APPLE AND EIDOS_BOOT AND TARGET eidos_platform)
    target_sources(eidos_boot PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/engine/platform/text_instant.c)
    target_include_directories(eidos_boot PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/engine/platform)
    target_link_options(eidos_boot PRIVATE
        "-Wl,--wrap=PRINTSYS_PrintStream"
        "-Wl,--wrap=PRINTSYS_PrintStreamCallBack"
        "-Wl,--wrap=PRINTSYS_PrintStreamSetSpeed")
endif()

# The one-screen layout's battle backdrop (docs/battle-backdrop.md): the
# game's scenery draws are wrapped (sdk/battle_view.c) and the real map is
# built and placed beside them (engine/world).  GNU ld only, with the game's boot
# and the settings in the link.
if(NOT MSVC AND NOT APPLE AND EIDOS_BOOT AND TARGET eidos_platform)
    target_sources(eidos_boot PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/sdk/battle_view.c
        ${CMAKE_CURRENT_SOURCE_DIR}/engine/world/bmap_place.c
        ${CMAKE_CURRENT_SOURCE_DIR}/engine/world/bmap_spots.c
        ${CMAKE_CURRENT_SOURCE_DIR}/engine/world/bmap_gimmick.c)
    target_sources(eidos_boot PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/engine/world/bmap_data.c)
    target_include_directories(eidos_boot PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/engine/platform
        ${CMAKE_CURRENT_SOURCE_DIR}/engine/ui
        ${CMAKE_CURRENT_SOURCE_DIR}/engine/world)
    target_link_options(eidos_boot PRIVATE
        "-Wl,--wrap=G3D_SYSTEM_DrawObject"
        "-Wl,--wrap=G3D_CAMERA_Reset"
        "-Wl,--wrap=MCSS_Draw")
endif()

# A fixed image base on Windows.  The DS address space sits at its own
# addresses (main RAM at host 0x02000000, sdk/dsmem.c); with address space
# layout randomisation the loader may put the 17 MB executable across that
# range, and the boot then falls back to movable blocks, which the game's raw
# DS pointers do not survive (a crash at frame 0 on some starts).  Without it
# the image stays at its preferred base (0x400000 for a 32-bit image), below
# the DS range.
if(WIN32)
    if(MSVC)
        target_link_options(eidos_boot PRIVATE "/DYNAMICBASE:NO")
    else()
        target_link_options(eidos_boot PRIVATE "-Wl,--disable-dynamicbase")
    endif()
endif()

# --- Overlays compiled into eidos_boot (docs/overlays.md) --------------------
#
# EIDOS_OVERLAYS names the overlays tools/ovlink.py compiles into eidos_boot:
# ids separated by ';' (9;10;88), ALL, or TITLE (the title path: ov009, ov010,
# ov011, ov013, ov017, ov228, ov088, ov179; docs/title-closure.md).  Empty
# means TITLE when EIDOS_BOOT is ON and none otherwise.  With
# EIDOS_OVERLAYS_ARM9_STUBS the ARM9 functions the overlays call that the
# link lacks are panicking stubs (tools/ovlink.py --arm9-stubs); off, they
# are link errors.  eidos_boot installs the set with
# bw_ovl_install(&bw_ovl_set_boot, 0).
#
# With the boot the two links are planned together: a first ovlink pass
# names the matched ARM9 files that define what the overlays call
# (EIDOS_OVERLAY_SEEDS, added to the boot's closure); the boot is planned;
# then the overlays are linked against the boot's closure, and the boot's
# generator is told the names the overlays define outside their windows
# (EIDOS_OVERLAY_SHIMS: the trampolines, the remaining ARM9 stubs).
set(EIDOS_OVERLAYS "" CACHE STRING
    "Overlays compiled into eidos_boot: ids separated by ';', ALL, TITLE, or empty")
option(EIDOS_OVERLAYS_ARM9_STUBS
       "Stub the ARM9 functions the compiled overlays call and the link lacks" ON)
set(_eidos_ovls "${EIDOS_OVERLAYS}")
if(_eidos_ovls STREQUAL "" AND EIDOS_BOOT)
    set(_eidos_ovls TITLE)
endif()
set(EIDOS_OVERLAY_SEEDS)
set(EIDOS_OVERLAY_SHIMS)
if(EIDOS_BOOT AND NOT _eidos_ovls STREQUAL "")
    eidos_overlays_seeds(EIDOS_OVERLAY_SEEDS
        OVERLAYS ${_eidos_ovls}
        LINK_SOURCES ${BW_OVL_LIBRARY_SOURCES})
    set(EIDOS_OVERLAY_SHIMS
        ${CMAKE_BINARY_DIR}/ovl/boot/ovl_dispatch.c
        ${CMAKE_BINARY_DIR}/ovl/boot/ovl_arm9_stubs.c)
endif()

if(EIDOS_BOOT)
    include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/boot.cmake)

    # A unit source and an mwcc `asm` body that name one function used to
    # collide here: the asm file named MYSTATUS_NameToStrbuf (0x02008500) and
    # src/matched/arm9/unit_020084BC.c defines it as C, so bootlink.py emitted
    # a panic stub with that name beside the unit's definition.  tools/bootlink.py
    # now drops the stub when a compiled unit already defines the name and
    # renames the reference onto the definition, so no per-file workaround is
    # needed here.

    # The game's own C library defines the same names as the host's.  aletheia
    # splits the MSL into src/matched/arm9/func_*.c: the strtod/strtol family
    # (strtod, atof, strtoul, strtol, strtoll, atoi, atoll), the stdio it kept
    # (fclose, fread, fseek, ftell, rewind, sprintf, vsnprintf), the string
    # routines (strlen, strcpy, strcmp, strchr, strstr, strncpy, strcat,
    # strncmp, strcspn, memmove, memchr), qsort, rand, srand, the wide
    # strings (fwide, wcscpy, wcslen) and the maths (fabs, floor, copysign,
    # cos, sin, pow, scalbn, nan, frexp, ldexp).  A statically linked C
    # library (the wasm32 build) refuses two definitions outright.  The game's strtoul reads __ctype_map, a
    # DS address the port binds only when it installs the ARM9 image
    # (tools/armdata.py), the game's rewind/fseek lock a DS mutex, and the
    # game's fclose expects the game's own FILE: a port caller that mixes the
    # host's fopen with the game's fclose, or the host's fseek with the game's
    # rewind (sdk/fs.c), or calls strtoul before run_boot installs the image
    # (demo/eidos_boot.c for --watchdog), faults.  One name, two definitions:
    # the game's spellings are renamed for the boot's matched units only, so
    # the game's code keeps its own library and the port's code (demo/, sdk/,
    # engine/) gets the host's.  The wrappers are the boot's alone
    # (cmake/boot.cmake), so nothing else sees the rename.  `abs` is left out:
    # no boot unit defines it, so the game's callers already reach the host's.
    set_source_files_properties(${EIDOS_BOOT_MATCHED_WRAPPERS} PROPERTIES
        COMPILE_DEFINITIONS
        "atof=MSL_atof;atoi=MSL_atoi;atoll=MSL_atoll;copysign=MSL_copysign;cos=MSL_cos;fabs=MSL_fabs;fclose=MSL_fclose;floor=MSL_floor;fread=MSL_fread;frexp=MSL_frexp;fseek=MSL_fseek;ftell=MSL_ftell;fwide=MSL_fwide;ldexp=MSL_ldexp;memchr=MSL_memchr;memmove=MSL_memmove;nan=MSL_nan;pow=MSL_pow;qsort=MSL_qsort;rand=MSL_rand;rewind=MSL_rewind;scalbn=MSL_scalbn;sin=MSL_sin;sprintf=MSL_sprintf;srand=MSL_srand;strcat=MSL_strcat;strchr=MSL_strchr;strcmp=MSL_strcmp;strcpy=MSL_strcpy;strcspn=MSL_strcspn;strlen=MSL_strlen;strncmp=MSL_strncmp;strncpy=MSL_strncpy;strstr=MSL_strstr;strtod=MSL_strtod;strtol=MSL_strtol;strtoll=MSL_strtoll;strtoul=MSL_strtoul;vsnprintf=MSL_vsnprintf;wcscpy=MSL_wcscpy;wcslen=MSL_wcslen")
endif()

if(NOT _eidos_ovls STREQUAL "")
    set(_ovl_link ${BW_OVL_LIBRARY_SOURCES})
    if(EIDOS_BOOT)
        # engine/platform/settings_ui.c and settings.c live in eidos_platform,
        # not in a port source list: named here so the overlays' eidos_sui_*
        # and eidos_settings_* calls resolve to them (docs/settings-design.md).
        list(APPEND _ovl_link ${EIDOS_BOOT_MATCHED} ${EIDOS_BOOT_PORT_SOURCES}
            ${EIDOS_BOOT_ASM_SUBST}
            ${CMAKE_CURRENT_SOURCE_DIR}/engine/platform/settings_ui.c
            ${CMAKE_CURRENT_SOURCE_DIR}/engine/platform/settings.c
            ${CMAKE_CURRENT_SOURCE_DIR}/engine/platform/mod_options_proc.c)
    endif()
    set(_ovl_stub_opt)
    if(EIDOS_OVERLAYS_ARM9_STUBS)
        set(_ovl_stub_opt ARM9_STUBS)
    endif()
    eidos_overlays(boot
        TARGET eidos_boot
        OVERLAYS ${_eidos_ovls}
        LINK_SOURCES ${_ovl_link}
        ${_ovl_stub_opt})
    target_compile_definitions(eidos_boot PRIVATE EIDOS_OVERLAYS=1)
    message(STATUS "EIDOS_OVERLAYS: ${_eidos_ovls}, ${boot_OVL_UNITS} matched sources")

    # test_ovlink_title: every overlay of the set loaded from the extract in
    # turn, its tables checked against the player's files.  Without the
    # boot only: with it the overlays' ARM9 callees are the boot's, and
    # test_boot_run loads the set the way the game does.
    if(NOT EIDOS_BOOT)
        add_executable(test_ovlink_title
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_ovlink_title.c
            $<TARGET_OBJECTS:boot_ovl>)
        target_include_directories(test_ovlink_title PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
        target_link_libraries(test_ovlink_title PRIVATE eidos)
        add_test(NAME test_ovlink_title COMMAND test_ovlink_title ${BW_DATA_ROOT_ABS})
        set_tests_properties(test_ovlink_title PROPERTIES SKIP_REGULAR_EXPRESSION "SKIPPED:")
    endif()
endif()

# --- No modal dialogs under MSVC ---------------------------------------------
#
# The Microsoft runtime answers abort(), a failed assert() and a debug-CRT
# report with a window that waits for a click, which stalls an unattended
# test run (several tests abort on purpose: tests/expect_panic.cmake).
# sdk/plat_nodialog.c sends every such report to stderr from a CRT
# initialiser; it goes into each executable as an object of its own, because
# nothing references it and a library member would never be linked.  This
# block stays last so it sees every executable defined above.
option(EIDOS_NO_CRT_DIALOGS
       "MSVC: send CRT error reports to stderr instead of modal dialogs" ON)
if(MSVC AND EIDOS_NO_CRT_DIALOGS)
    get_property(_eidos_targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
    foreach(_t ${_eidos_targets})
        get_target_property(_type ${_t} TYPE)
        if(_type STREQUAL "EXECUTABLE")
            target_sources(${_t} PRIVATE
                ${CMAKE_CURRENT_SOURCE_DIR}/sdk/plat_nodialog.c)
        endif()
    endforeach()
endif()

# No test may touch the player's settings.ini: point every test at a file in
# the build directory (engine/platform/settings.c reads EIDOS_SETTINGS).
get_property(_eidos_all_tests DIRECTORY PROPERTY TESTS)
foreach(_t IN LISTS _eidos_all_tests)
    set_property(TEST ${_t} APPEND PROPERTY ENVIRONMENT
        "EIDOS_SETTINGS=${CMAKE_CURRENT_BINARY_DIR}/test_${_t}.ini")
endforeach()

# The tests on the player's real ROM (test_extract_rom, test_firstrun_rom,
# test_wizard's real install) read BW_ROM at test time.  -DBW_ROM=<path> at
# configure time passes it to them, so a build that has the ROM does not
# skip them.  Without it they read BW_ROM from the environment ctest runs in.
set(BW_ROM "" CACHE FILEPATH
    "the player's Pokemon Black ROM, for the tests that install it (optional)")
# test_picker opens the native file dialog on the desktop and drives it from
# a helper thread.  It runs only when asked: -DEIDOS_TEST_DIALOGS=ON, or
# EIDOS_TEST_DIALOGS=1 in the environment when ctest runs.
option(EIDOS_TEST_DIALOGS "run the test that opens the file dialog on the desktop" OFF)
if(NOT "${BW_ROM}" STREQUAL "")
    foreach(_t test_extract_rom test_firstrun_rom test_wizard)
        if(TEST ${_t})
            set_property(TEST ${_t} APPEND PROPERTY ENVIRONMENT "BW_ROM=${BW_ROM}")
        endif()
    endforeach()
endif()
if(EIDOS_TEST_DIALOGS AND TEST test_picker)
    set_property(TEST test_picker APPEND PROPERTY ENVIRONMENT "EIDOS_TEST_DIALOGS=1")
endif()

# Tests that compile their own copies of src/matched files under their own
# renames keep the src/matched spellings: tools/gamespell.py's header steps
# aside for them.
if(EIDOS_SOURCE_TREE STREQUAL "game")
    foreach(_t test_armdata_real test_backup test_firmware test_input test_pxi
               test_snd_matched test_tp)
        if(TARGET ${_t})
            target_compile_definitions(${_t} PRIVATE EIDOS_NO_GAMESPELL)
        endif()
    endforeach()
endif()

# The battle spot table (engine/world/bmap_spots.c, docs/battle-backdrop.md):
# reading the built-in rows, finding a battle's row, the placement of each
# mode, saving the user's rows.  No game data.
add_executable(test_bmap_spots ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_bmap_spots.c
    ${CMAKE_CURRENT_SOURCE_DIR}/engine/world/bmap_spots.c)
target_include_directories(test_bmap_spots PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/tests ${CMAKE_CURRENT_SOURCE_DIR}/engine/world)
if(BW_HAVE_LIBM)
    target_link_libraries(test_bmap_spots PRIVATE m)
endif()
add_test(NAME test_bmap_spots COMMAND test_bmap_spots)

# The interface kit's test (engine/ui/kit.c, assets.c, assets_icons.c, assets_sprites.c): the
# synthetic checks need no game data; the data test needs the data root and
# writes a contact sheet.  The kit's sources are compiled into the test
# directly, so it builds without SDL.  The kit calls two hooks of the
# interface layer (eidos_ui_hit, eidos_ui_press), which the test defines.
add_executable(test_ui_kit ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_ui_kit.c
    ${CMAKE_CURRENT_SOURCE_DIR}/engine/ui/kit.c
    ${CMAKE_CURRENT_SOURCE_DIR}/engine/ui/assets.c
    ${CMAKE_CURRENT_SOURCE_DIR}/engine/ui/assets_icons.c
    ${CMAKE_CURRENT_SOURCE_DIR}/engine/ui/assets_sprites.c)
target_include_directories(test_ui_kit PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk ${CMAKE_CURRENT_SOURCE_DIR}/tests
    ${CMAKE_CURRENT_SOURCE_DIR}/engine/ui)
target_link_libraries(test_ui_kit PRIVATE eidos)
add_test(NAME test_ui_kit COMMAND test_ui_kit ${BW_DATA_ROOT_ABS})

# The name entry and Xtransceiver recipes (engine/ui/recipe_input.c): the test
# builds stand-ins for the game's structs, runs the recipes at several canvas
# sizes, checks the taps their clicks make and writes the pictures when
# UI_INPUT_OUT names a directory.
add_executable(test_ui_input ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_ui_input.c
    ${CMAKE_CURRENT_SOURCE_DIR}/engine/ui/recipe_input.c
    ${CMAKE_CURRENT_SOURCE_DIR}/engine/ui/kit.c
    ${CMAKE_CURRENT_SOURCE_DIR}/engine/ui/assets.c
    ${CMAKE_CURRENT_SOURCE_DIR}/engine/ui/assets_icons.c
    ${CMAKE_CURRENT_SOURCE_DIR}/engine/ui/assets_sprites.c)
target_include_directories(test_ui_input PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk ${CMAKE_CURRENT_SOURCE_DIR}/tests
    ${CMAKE_CURRENT_SOURCE_DIR}/engine/ui)
target_link_libraries(test_ui_input PRIVATE eidos)
add_test(NAME test_ui_input COMMAND test_ui_input ${BW_DATA_ROOT_ABS})

# The PC boxes recipe (engine/ui/recipe_pc.c): the test includes the recipe's
# source with BW_PC_TEST, builds stand-ins for the game's structs and
# functions (a box task, a party, two boxes, the window frames, the bottom
# screen's pixels), runs the recipe at several canvas sizes, checks the taps
# its clicks and drags make and writes the pictures when UI_PC_OUT names a
# directory.
add_executable(test_ui_pc ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_ui_pc.c
    ${CMAKE_CURRENT_SOURCE_DIR}/engine/ui/kit.c
    ${CMAKE_CURRENT_SOURCE_DIR}/engine/ui/assets.c
    ${CMAKE_CURRENT_SOURCE_DIR}/engine/ui/assets_icons.c
    ${CMAKE_CURRENT_SOURCE_DIR}/engine/ui/assets_sprites.c)
target_include_directories(test_ui_pc PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk ${CMAKE_CURRENT_SOURCE_DIR}/tests
    ${CMAKE_CURRENT_SOURCE_DIR}/engine/ui)
target_link_libraries(test_ui_pc PRIVATE eidos)
add_test(NAME test_ui_pc COMMAND test_ui_pc ${BW_DATA_ROOT_ABS})
