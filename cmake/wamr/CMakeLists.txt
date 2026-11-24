# cmake/wamr/CMakeLists.txt -- the WebAssembly runtime code mods run in.
#
# WAMR's own build script (runtime_lib.cmake) sets compile definitions and
# include folders for the folder it is read in; this folder of its own keeps
# them away from the port's sources.  The result is one static library,
# eidos_wamr, whose public include folder holds wasm_export.h.
#
# The runtime is the interpreter only (the fast one): no AOT, no JIT, no WASI
# and no libc for the module, which reaches the game through the port's
# imports alone (docs/mod-code-wasm.md).  The native calls go through WAMR's
# portable C path (invokeNative_general.c), so no assembler is needed on any
# host.

# The port's directory-wide options (-include gamespell.h, the DS struct
# layout flags of the top-level file) are for the game's code; WAMR is built
# with the host's own layout, the one wasm_export.h is read with in
# sdk/mod_wasm.c.
set_property(DIRECTORY PROPERTY COMPILE_OPTIONS "")

if(CMAKE_SYSTEM_NAME STREQUAL "WASI")
    # the wasm32 build: the runtime inside the module (cmake/wamr/wasi)
    set(WAMR_BUILD_PLATFORM "eidos-wasi")
    set(SHARED_PLATFORM_CONFIG ${CMAKE_CURRENT_SOURCE_DIR}/wasi/shared_platform.cmake)
elseif(WIN32)
    set(WAMR_BUILD_PLATFORM "windows")
elseif(APPLE)
    set(WAMR_BUILD_PLATFORM "darwin")
else()
    set(WAMR_BUILD_PLATFORM "linux")
endif()

string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" _eidos_wamr_cpu)
if(_eidos_wamr_cpu MATCHES "arm64|aarch64")
    set(WAMR_BUILD_TARGET "AARCH64")
elseif(CMAKE_SIZEOF_VOID_P EQUAL 4)
    set(WAMR_BUILD_TARGET "X86_32")
else()
    set(WAMR_BUILD_TARGET "X86_64")
endif()

set(WAMR_ROOT_DIR "${EIDOS_WAMR_ROOT}")
set(WAMR_BUILD_INTERP 1)
set(WAMR_BUILD_FAST_INTERP 1)
set(WAMR_BUILD_AOT 0)
set(WAMR_BUILD_JIT 0)
set(WAMR_BUILD_FAST_JIT 0)
set(WAMR_BUILD_LIBC_BUILTIN 0)
set(WAMR_BUILD_LIBC_WASI 0)
set(WAMR_BUILD_MULTI_MODULE 0)
# Reference types on: Rust (1.82 and later) and current clang build wasm32
# with reference-types and an overlong call_indirect table index by
# default, and WAMR refuses such a module without them ("zero byte
# expected").  Bulk memory is WAMR's default (on).
set(WAMR_BUILD_REF_TYPES 1)
set(WAMR_BUILD_SIMD 0)
set(WAMR_BUILD_INVOKE_NATIVE_GENERAL 1)
# Bounds are checked in software on every target.  The hardware check (64-bit
# targets only) traps through signals or, on Windows, MSVC's __try, which
# MinGW does not have, and the port's crash handler owns those signals.
set(WAMR_DISABLE_HW_BOUND_CHECK 1)
set(WAMR_DISABLE_STACK_HW_BOUND_CHECK 1)

include(${WAMR_ROOT_DIR}/build-scripts/runtime_lib.cmake)

# The Windows platform's clock file serves WASI's clock calls only (off
# here), and its own NtQueryTimerResolution declaration clashes with the
# Windows SDK's under cl.exe's 32-bit target.
if(MSVC)
    list(FILTER WAMR_RUNTIME_LIB_SOURCE EXCLUDE REGEX "win_clock\\.c$")
endif()
if(CMAKE_SYSTEM_NAME STREQUAL "WASI")
    # A wasm function has no return register.  WAMR's portable call routine
    # (invokeNative_general.c) returns nothing, and WAMR reads the native's
    # result as what the routine "left in the register": in the module every
    # call a mod made into the port came back 0.  The module takes the port's
    # own routine (wasi/invoke_native.c), which returns the 64 bits the native
    # returned, and a copy of wasm_runtime_common.c whose declaration of the
    # routine says so (the copy is made here, checked: a WAMR whose text has
    # moved stops the configuration).
    set(_eidos_wrc "${WAMR_ROOT_DIR}/core/iwasm/common/wasm_runtime_common.c")
    set(_eidos_wrc_copy "${CMAKE_CURRENT_BINARY_DIR}/wasm_runtime_common.c")
    set(_eidos_decl "void\ninvokeNative(GenericFunctionPointer f, uint32 *args, uint32 sz);")
    file(READ "${_eidos_wrc}" _eidos_wrc_text)
    string(FIND "${_eidos_wrc_text}" "${_eidos_decl}" _eidos_at)
    if(_eidos_at EQUAL -1)
        message(FATAL_ERROR "cmake/wamr: wasm_runtime_common.c does not declare invokeNative where it did (WAMR 2.2.0)")
    endif()
    string(REPLACE "${_eidos_decl}" "uint64\ninvokeNative(GenericFunctionPointer f, uint32 *args, uint32 sz);"
           _eidos_wrc_text "${_eidos_wrc_text}")
    set(_eidos_old "")
    if(EXISTS "${_eidos_wrc_copy}")
        file(READ "${_eidos_wrc_copy}" _eidos_old)
    endif()
    if(NOT "${_eidos_old}" STREQUAL "${_eidos_wrc_text}")
        file(WRITE "${_eidos_wrc_copy}" "${_eidos_wrc_text}")
    endif()
    list(FILTER WAMR_RUNTIME_LIB_SOURCE EXCLUDE REGEX "invokeNative_general\\.c$")
    list(FILTER WAMR_RUNTIME_LIB_SOURCE EXCLUDE REGEX "common/wasm_runtime_common\\.c$")
    list(APPEND WAMR_RUNTIME_LIB_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/wasi/invoke_native.c" "${_eidos_wrc_copy}")
endif()
add_library(eidos_wamr STATIC ${WAMR_RUNTIME_LIB_SOURCE})
target_include_directories(eidos_wamr PUBLIC ${WAMR_ROOT_DIR}/core/iwasm/include)
# A static library: under cl.exe wasm_export.h would mark the API dllimport
# (its default when COMPILING_WASM_RUNTIME_API is not defined).
target_compile_definitions(eidos_wamr PUBLIC WASM_RUNTIME_API_EXTERN=)
# WAMR's own warnings are its own: the port's warning flags stay off it.
target_compile_options(eidos_wamr PRIVATE -w)
if(CMAKE_SYSTEM_NAME STREQUAL "WASI")
    # nothing to link: the platform layer is the module's C library
elseif(WIN32)
    # Winsock and the native API the Windows platform layer calls; the
    # POSIX clock MinGW provides lives in winpthread.
    target_link_libraries(eidos_wamr PUBLIC ws2_32 ntdll)
    if(MINGW)
        target_link_libraries(eidos_wamr PUBLIC -Wl,-Bstatic pthread -Wl,-Bdynamic)
    endif()
else()
    find_package(Threads REQUIRED)
    target_link_libraries(eidos_wamr PUBLIC Threads::Threads m)
    if(NOT APPLE)
        target_link_libraries(eidos_wamr PUBLIC dl)
    endif()
endif()
