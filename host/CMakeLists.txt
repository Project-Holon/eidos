# host/CMakeLists.txt -- eidos's 64-bit program for the wasm32 build
# (docs/lp64.md, "The wasm32 configuration's architecture").
#
# Stage one is eidos's own CMake project configured for wasm32 (wasi-sdk's
# toolchain file): it builds eidos_boot.wasm.  This project is stage two,
# with the platform's own compiler: wasm2c turns the module into C (game.h
# and game_*.c, split so they compile in parallel), and that C, wasm2c's
# runtime, host/ and SDL3 make the program.
#
#   cmake -S host -B build-host -DEIDOS_WASM_MODULE=<stage one>/eidos_boot.wasm
#         -DWABT_ROOT=<wabt source tree> -DWASM2C=<wasm2c> -DSDL3_PREFIX=<SDL3>
#         -DWASM_OPT=<wasm-opt> [-DEIDOS_HOST_OPT=-O2]
#
# Android and iOS build SDL3 from its source tree with the program
# (-DEIDOS_HOST_SDL_SOURCE=<SDL3 source> in place of SDL3_PREFIX): on
# Android the program is libmain.so, which SDL's SDLActivity loads (the
# Gradle project in android/), and on iOS an app bundle (ios/).  Both draw
# with OpenGL ES 3.0 (HOST_GLES, host/sdl.c).

cmake_minimum_required(VERSION 3.16)
project(eidos_host C)

set(EIDOS_WASM_MODULE "" CACHE FILEPATH "the stage one module (eidos_boot.wasm)")
set(WABT_ROOT "" CACHE PATH "wabt's source tree (its wasm2c/ runtime)")
set(WASM2C "" CACHE FILEPATH "wasm2c")
set(WASM_OPT "" CACHE FILEPATH "binaryen's wasm-opt")
set(EIDOS_HOST_OPT "-O2" CACHE STRING "wasm-opt's optimisation level (-O0 to debug)")
option(EIDOS_HOST_FPCAST
       "--fpcast-emu (OFF: a diagnostic build that logs calls through pointers of another type)" ON)
set(SDL3_PREFIX "" CACHE PATH "SDL3: a prefix with include/SDL3 and the library")
set(EIDOS_HOST_SDL_SOURCE "" CACHE PATH "SDL3's source tree, built with the program (Android, iOS)")
set(EIDOS_HOST_PARTS 32 CACHE STRING "how many C files wasm2c writes")
option(EIDOS_HOST_WINDOWED "Windows: a windowed program with no console (a release)" OFF)
# Consoles (the Switch, the Vita): no room for wasm2c's 8 GiB of guard pages
# and no fault signals, so every access is checked and the memory is a heap
# block of a fixed budget, grown once at start (host/main.c).  Buildable on
# a desktop to measure what the checks cost.
option(EIDOS_HOST_BOUNDS_CHECKS "explicit bounds checks and a fixed memory budget (consoles)" OFF)
set(EIDOS_HOST_MEMORY_MB 512 CACHE STRING "the module's memory budget with EIDOS_HOST_BOUNDS_CHECKS (MiB)")

foreach(_v EIDOS_WASM_MODULE WASM2C WASM_OPT)
    if(NOT EXISTS "${${_v}}")
        message(FATAL_ERROR "${_v}: nothing at '${${_v}}'")
    endif()
endforeach()
if(NOT EXISTS "${WABT_ROOT}/wasm2c/wasm-rt-impl.c")
    message(FATAL_ERROR "WABT_ROOT: no wasm2c runtime under '${WABT_ROOT}'")
endif()

set(_root ${CMAKE_CURRENT_SOURCE_DIR}/..)
find_package(Python3 COMPONENTS Interpreter REQUIRED)

if(EIDOS_HOST_SDL_SOURCE)
    # SDLActivity loads libSDL3.so itself on Android; iOS links it in
    if(ANDROID)
        set(SDL_SHARED ON CACHE BOOL "" FORCE)
        set(SDL_STATIC OFF CACHE BOOL "" FORCE)
    else()
        set(SDL_SHARED OFF CACHE BOOL "" FORCE)
        set(SDL_STATIC ON CACHE BOOL "" FORCE)
    endif()
    set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
    add_subdirectory(${EIDOS_HOST_SDL_SOURCE} ${CMAKE_BINARY_DIR}/sdl3 EXCLUDE_FROM_ALL)
    set(_sdl_include ${EIDOS_HOST_SDL_SOURCE}/include)
else()
    set(_sdl_include ${SDL3_PREFIX}/include)
endif()

# wasm-opt, then wasm2c.  The game calls functions through pointers of
# other prototypes (a handler stored as one type, called as another), which
# the DS allows and a wasm call_indirect traps on; --fpcast-emu makes every
# indirect call take the same 20 64-bit arguments, so a callee ignores the
# extra ones and a missing one is 0 (docs/lp64.md).  The module's widest
# function has 17 parameters.  -g keeps the function names, which become
# the C functions' names and so a crash report's.
if(EIDOS_HOST_FPCAST)
    set(_eidos_fpcast --fpcast-emu --pass-arg=max-func-params@20)
else()
    set(_eidos_fpcast)
endif()
add_custom_command(
    OUTPUT ${CMAKE_BINARY_DIR}/game.opt.wasm
    COMMAND ${WASM_OPT} ${EIDOS_WASM_MODULE} ${_eidos_fpcast} ${EIDOS_HOST_OPT} -g
            -o ${CMAKE_BINARY_DIR}/game.opt.wasm
    DEPENDS ${EIDOS_WASM_MODULE}
    COMMENT "wasm-opt --fpcast-emu ${EIDOS_HOST_OPT}")
set(_mod "${CMAKE_BINARY_DIR}/game.opt.wasm")
set(_parts)
math(EXPR _last "${EIDOS_HOST_PARTS} - 1")
foreach(_i RANGE 0 ${_last})
    list(APPEND _parts ${CMAKE_BINARY_DIR}/game_${_i}.c)
endforeach()
# Then its one patch (tools/wasm2c_patch.py: a call to a DS code address).
add_custom_command(
    OUTPUT ${_parts} ${CMAKE_BINARY_DIR}/game.h ${CMAKE_BINARY_DIR}/game-impl.h
    COMMAND ${WASM2C} ${_mod} -n game --num-outputs=${EIDOS_HOST_PARTS}
            -o ${CMAKE_BINARY_DIR}/game.c
    COMMAND ${Python3_EXECUTABLE} ${_root}/tools/wasm2c_patch.py
            ${CMAKE_BINARY_DIR}/game-impl.h
    DEPENDS ${_mod} ${_root}/tools/wasm2c_patch.py
    COMMENT "wasm2c")

# The OpenGL imports, from the module's own list of them.
add_custom_command(
    OUTPUT ${CMAKE_BINARY_DIR}/gl_names.txt
    COMMAND ${Python3_EXECUTABLE} ${_root}/tools/wasm_imports.py ${_mod} env gl
            -o ${CMAKE_BINARY_DIR}/gl_names.txt
    DEPENDS ${_mod} ${_root}/tools/wasm_imports.py)
add_custom_command(
    OUTPUT ${CMAKE_BINARY_DIR}/gl_imports.c
    COMMAND ${Python3_EXECUTABLE} ${_root}/tools/wasm_gl_imports.py
            --sdl-include ${_sdl_include} --names ${CMAKE_BINARY_DIR}/gl_names.txt
            -o ${CMAKE_BINARY_DIR}/gl_imports.c
    DEPENDS ${CMAKE_BINARY_DIR}/gl_names.txt ${_root}/tools/wasm_gl_imports.py)

set(_host_sources
    ${_parts}
    ${CMAKE_BINARY_DIR}/gl_imports.c
    ${WABT_ROOT}/wasm2c/wasm-rt-impl.c
    ${WABT_ROOT}/wasm2c/wasm-rt-mem-impl.c
    ${WABT_ROOT}/wasm2c/wasm-rt-exceptions-impl.c
    ${CMAKE_CURRENT_SOURCE_DIR}/main.c
    ${CMAKE_CURRENT_SOURCE_DIR}/fiber.c
    ${CMAKE_CURRENT_SOURCE_DIR}/par.c
    ${CMAKE_CURRENT_SOURCE_DIR}/net.c
    ${CMAKE_CURRENT_SOURCE_DIR}/wasi.c
    ${CMAKE_CURRENT_SOURCE_DIR}/sdl.c
    ${CMAKE_CURRENT_SOURCE_DIR}/sdl_gpu.c
    ${_root}/install/picker.c
    ${_root}/install/location.c
    ${_root}/sdk/plat_save.c
    ${_root}/sdk/plat_dir.c)
if(ANDROID OR IOS)
    # the ROM dialog on phones
    list(APPEND _host_sources ${CMAKE_CURRENT_SOURCE_DIR}/pick_mobile.c)
endif()
if(IOS)
    enable_language(OBJC)
    list(APPEND _host_sources ${CMAKE_CURRENT_SOURCE_DIR}/pick_ios.m
                              ${CMAKE_CURRENT_SOURCE_DIR}/ios_view.m)
    # the app's icon (assets/icon.png at the sizes Info.plist.in names),
    # at the top of the bundle
    file(GLOB _ios_icons ${_root}/ios/icons/*.png)
    set_source_files_properties(${_ios_icons} PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    list(APPEND _host_sources ${_ios_icons})
endif()
if(ANDROID)
    add_library(eidos SHARED ${_host_sources})
    set_target_properties(eidos PROPERTIES OUTPUT_NAME main)
elseif(IOS)
    # the app's version: the game's (project(eidos VERSION ...) of the
    # top-level CMakeLists.txt)
    file(STRINGS ${_root}/CMakeLists.txt _ver REGEX "^project[(]eidos VERSION ")
    string(REGEX REPLACE ".*VERSION ([0-9.]+).*" "\\1" EIDOS_APP_VERSION "${_ver}")
    add_executable(eidos MACOSX_BUNDLE ${_host_sources})
    set_target_properties(eidos PROPERTIES
        MACOSX_BUNDLE_INFO_PLIST ${_root}/ios/Info.plist.in
        MACOSX_BUNDLE_GUI_IDENTIFIER org.projectholon.eidos
        MACOSX_BUNDLE_BUNDLE_NAME eidos
        MACOSX_BUNDLE_SHORT_VERSION_STRING ${EIDOS_APP_VERSION}
        MACOSX_BUNDLE_BUNDLE_VERSION ${EIDOS_APP_VERSION}
        XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER org.projectholon.eidos
        XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2"
        # the icons stay separate PNGs, as Info.plist.in names them
        XCODE_ATTRIBUTE_COMBINE_HIDPI_IMAGES NO)
else()
    add_executable(eidos ${_host_sources})
endif()
target_include_directories(eidos PRIVATE
    ${CMAKE_BINARY_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${_root}/install
    ${_root}/sdk
    ${WABT_ROOT}/wasm2c
    ${_sdl_include})
if(ANDROID OR IOS)
    target_compile_definitions(eidos PRIVATE HOST_GLES=1)
endif()
if(IOS)
    # the ROM dialog (host/pick_ios.m)
    target_link_libraries(eidos PRIVATE "-framework UIKit" "-framework UniformTypeIdentifiers")
endif()
if(ANDROID)
    # 16 KB pages (Android 15 and later devices): the NDK aligns the LOAD
    # segments for them with ANDROID_SUPPORT_FLEXIBLE_PAGE_SIZES, which
    # android/build.py passes; SDL3's library is linked the same way
    target_link_options(eidos PRIVATE -Wl,-z,max-page-size=16384)
    # plain char is unsigned on Android's ARM ABIs; sdk/types.h wants the
    # DS's signed one for the port's own sources compiled here
    target_compile_options(eidos PRIVATE -fsigned-char)
endif()
# Each fiber has its own native stack, so wasm2c's stack-depth count (one
# per thread) does not apply; a trap ends the program (no recovery across
# fibers), and guard pages check the memory, which then never moves (the
# SDL surfaces made from module pixels rely on it, host/sdl.c).
if(NOT EIDOS_HOST_FPCAST)
    target_compile_definitions(eidos PRIVATE HOST_INDIRECT_LOG=1)
endif()
target_compile_definitions(eidos PRIVATE
    WASM_RT_NONCONFORMING_UNCHECKED_STACK_EXHAUSTION=1
    WASM_RT_SKIP_SIGNAL_RECOVERY=1
    WASM_RT_TRAP_HANDLER=host_trap)
if(EIDOS_HOST_BOUNDS_CHECKS)
    target_compile_definitions(eidos PRIVATE
        WASM_RT_USE_MMAP=0
        WASM_RT_MEMCHECK_BOUNDS_CHECK=1
        HOST_MEMORY_BUDGET_MB=${EIDOS_HOST_MEMORY_MB})
endif()
if(IOS)
    # iOS gives an app 5 GiB of address space, not the 8 GiB wasm2c's guard
    # pages reserve (more needs an entitlement a free team cannot have).  A
    # 32-bit address is under 4 GiB, so a copy of the memory runtime that
    # reserves 4 GiB and 1 GiB for the offsets keeps the guard pages, and no
    # access is checked one by one.
    file(READ ${WABT_ROOT}/wasm2c/wasm-rt-mem-impl.c _mem_impl)
    string(REPLACE "0x200000000ul" "0x140000000ul" _mem_impl "${_mem_impl}")
    file(WRITE ${CMAKE_BINARY_DIR}/wasm-rt-mem-impl-ios.c "${_mem_impl}")
    get_target_property(_srcs eidos SOURCES)
    list(TRANSFORM _srcs REPLACE ".*/wasm2c/wasm-rt-mem-impl\\.c$" "${CMAKE_BINARY_DIR}/wasm-rt-mem-impl-ios.c")
    set_property(TARGET eidos PROPERTY SOURCES ${_srcs})
endif()
# The generated files are large; GCC's assembly between its stages would
# otherwise go through the temporary folder (hundreds of MB per file).
if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
    target_compile_options(eidos PRIVATE -pipe)
endif()
if(WIN32)
    # The icon (assets/eidos.rc), and for a release no console window:
    # `eidos --console` opens one (host/main.c, console_open).  With GCC the
    # icon is a plain build step: a toolchain with only prefixed names (CI's)
    # has no windres for CMake's RC language to find, overriding that
    # compiler makes CMake reset its cache on every configure, and windres
    # runs its preprocessor as `gcc`.  windres and the preprocessor both come
    # from the compiler's own folder.
    if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
        get_filename_component(_cc_dir "${CMAKE_C_COMPILER}" DIRECTORY)
        find_program(EIDOS_HOST_WINDRES NAMES x86_64-w64-mingw32-windres windres
            HINTS "${_cc_dir}")
        if(NOT EIDOS_HOST_WINDRES)
            message(FATAL_ERROR "eidos host: no windres next to ${CMAKE_C_COMPILER} or on the PATH")
        endif()
        set(_icon_obj ${CMAKE_CURRENT_BINARY_DIR}/eidos_rc.o)
        add_custom_command(OUTPUT ${_icon_obj}
            COMMAND ${EIDOS_HOST_WINDRES} --preprocessor=${CMAKE_C_COMPILER}
                    --preprocessor-arg=-E --preprocessor-arg=-xc
                    --preprocessor-arg=-DRC_INVOKED -O coff -i ${_root}/assets/eidos.rc -o ${_icon_obj}
            DEPENDS ${_root}/assets/eidos.rc
            WORKING_DIRECTORY ${_root}/assets
            VERBATIM)
        target_sources(eidos PRIVATE ${_icon_obj})
    else()
        enable_language(RC)
        target_sources(eidos PRIVATE ${_root}/assets/eidos.rc)
    endif()
    if(EIDOS_HOST_WINDOWED)
        target_link_options(eidos PRIVATE -mwindows)
    endif()
    find_library(_sdl3 NAMES SDL3 libSDL3.dll.a PATHS ${SDL3_PREFIX}/lib NO_DEFAULT_PATH)
    target_link_libraries(eidos PRIVATE ${_sdl3} bcrypt ole32 shell32 uuid comdlg32 ws2_32 dbghelp)
    # WSAPoll (host/net.c) is Vista's
    set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/net.c PROPERTIES
        COMPILE_DEFINITIONS _WIN32_WINNT=0x0601)
    add_custom_command(TARGET eidos POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${SDL3_PREFIX}/bin/SDL3.dll $<TARGET_FILE_DIR:eidos>)
elseif(EIDOS_HOST_SDL_SOURCE)
    target_link_libraries(eidos PRIVATE SDL3::SDL3 m)
    if(ANDROID)
        target_link_libraries(eidos PRIVATE dl log android)
    endif()
else()
    find_library(_sdl3 NAMES SDL3 PATHS ${SDL3_PREFIX}/lib ${SDL3_PREFIX}/lib64)
    target_link_libraries(eidos PRIVATE ${_sdl3} m dl)
    if(APPLE)
        # the ROM picker's Cocoa dialog (install/picker.c calls it)
        enable_language(OBJC)
        target_sources(eidos PRIVATE ${_root}/install/picker_mac.m)
        target_link_libraries(eidos PRIVATE "-framework Cocoa")
    endif()
endif()
