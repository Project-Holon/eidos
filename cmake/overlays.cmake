#
# cmake/overlays.cmake -- compile game overlays into an executable
# (tools/ovlink.py, sdk/overlay_image.c; docs/overlays.md is the design).
#
#   eidos_overlays(<name>
#       TARGET <target>            the executable (or library) that links them
#       OVERLAYS <ids | ALL | TITLE>
#       [ROOT <dir>]               the aletheia tree (default ALETHEIA_ROOT)
#       [LINK_SOURCES <src>...]    every other C source in the link, so that
#                                  only names nothing defines get stubs
#       [ARM9_STUBS])              stub the ARM9 functions the overlays call
#                                  that LINK_SOURCES does not define (a link
#                                  without the boot)
#
# At configure time tools/ovlink.py plans the set and writes, under
# ${CMAKE_BINARY_DIR}/ovl/<name>/, one forced-include header per translation
# unit, the per-overlay C (stubs, data image, static initialisers) and the
# trampolines.  Each matched source goes through tools/dsaddr.py into the
# same directory (not the shared dsaddr/ tree: the same file may be
# compiled by two sets with different headers) and is compiled with its
# header.  Everything lands in an object library <name>_ovl whose objects
# are added to TARGET.
#
# The program installs the set with
#     extern const BwOvlImageSet bw_ovl_set_<name>;
#     bw_ovl_install(&bw_ovl_set_<name>, flags);
#
# Nothing here reads a game byte.
#

set(EIDOS_OVLINK_TOOL ${CMAKE_CURRENT_LIST_DIR}/../tools/ovlink.py)
get_filename_component(EIDOS_OVLINK_TOOL "${EIDOS_OVLINK_TOOL}" ABSOLUTE)
get_filename_component(_eidos_ovl_tools "${EIDOS_OVLINK_TOOL}" DIRECTORY)

set(EIDOS_OVLINK_PLATFORM_ARGS)
if(CMAKE_SYSTEM_NAME STREQUAL "WASI" AND DEFINED EIDOS_SDL AND NOT EIDOS_SDL)
    # Headless diagnostic modules have no platform settings UI. The shared
    # SDL host supports the regular settings patches in full-game builds.
    list(APPEND EIDOS_OVLINK_PLATFORM_ARGS --no-platform-ui)
endif()

# The warnings a matched overlay source raises under GCC 14 and Clang that
# only an edit of the source could silence (the same set cmake/boot.cmake
# gives the boot's matched files, plus the ones the overlays add): each is
# reproduced exactly from the DS code, none is a host porting problem.
#   implicit-int                 `func_02165E8C(Rec *r)`: C89's implicit int
#   builtin-declaration-mismatch the file's own memcpy/memset prototypes
#   char-subscripts, address, sequence-point, comment: as mwcc accepted them
if(MSVC)
    set(EIDOS_OVL_MATCHED_OPTIONS ${EIDOS_MATCHED_COMPILE_OPTIONS} ${EIDOS_MATCHED_PRAGMA_OPTION})
    if(EIDOS_CLANG_CL)
        list(APPEND EIDOS_OVL_MATCHED_OPTIONS
            -Wno-unused-label -Wno-implicit-int -Wno-sign-compare -Wno-parentheses
            -Wno-unused-function -Wno-implicit-fallthrough -Wno-switch
            -Wno-return-type -Wno-tautological-constant-out-of-range-compare
            -Wno-incompatible-library-redeclaration -Wno-pointer-sign
            -Wno-int-conversion -Wno-incompatible-pointer-types
            -Wno-int-to-pointer-cast -Wno-pointer-to-int-cast
            -Wno-unused-but-set-variable -Wno-missing-field-initializers
            -Wno-shift-count-overflow -Wno-constant-conversion -Wno-comment
            -Wno-deprecated-non-prototype)
    else()
        #   C4431  missing type specifier, int assumed (C89 implicit int)
        #   C4013  undefined; assuming extern returning int (none left after
        #          ovlink's declarations, kept off for the builtin case)
        #   C4102  unreferenced label   C4057/C4047/C4024/C4133 pointer forms
        #   C4018/C4389 signed/unsigned compare   C4146 unary minus on unsigned
        #   C4706  assignment in a condition      C4701/C4703 maybe-uninitialised
        #   C4715  not all control paths return   C4127 constant condition
        #   C4310  cast truncates a constant      C4305 truncation
        #   C4312/C4311 pointer/integer widths    C4152 function/data pointer
        #   C4054/C4055 the same, older           C4113 parameter lists differ
        #   C4028  parameter differs from declaration   C4090 const qualifiers
        #   C4029  declared parameter list differs      C4098 void returns a value
        #   C4204/C4221 non-constant initialisers       C4005 macro redefinition
        #   C4456/C4457/C4459 a local hiding another local, a parameter, a global
        #   C4210  a function declared inside a body    C4218 no type, int assumed
        #   C4306  cast to a wider pointer              C4702 unreachable code
        list(APPEND EIDOS_OVL_MATCHED_OPTIONS
            /wd4431 /wd4013 /wd4102 /wd4057 /wd4047 /wd4024 /wd4133 /wd4018
            /wd4389 /wd4146 /wd4706 /wd4701 /wd4703 /wd4715 /wd4127 /wd4310
            /wd4305 /wd4312 /wd4311 /wd4152 /wd4054 /wd4055 /wd4113 /wd4028
            /wd4090 /wd4029 /wd4098 /wd4204 /wd4221 /wd4005 /wd4295 /wd4334
            /wd4456 /wd4457 /wd4459 /wd4210 /wd4218 /wd4306 /wd4702)
    endif()
else()
    set(EIDOS_OVL_MATCHED_OPTIONS
        -Wno-unused-parameter -Wno-unused-variable -Wno-missing-prototypes
        -Wno-unused-but-set-variable -Wno-implicit-fallthrough
        -Wno-sign-compare -Wno-parentheses -Wno-unused-function
        -Wno-unused-label -Wno-unknown-pragmas -Wno-switch -Wno-return-type
        -Wno-type-limits -Wno-cast-function-type -Wno-implicit-int
        -Wno-builtin-declaration-mismatch -Wno-char-subscripts -Wno-address
        -Wno-sequence-point -Wno-comment
        # a pointer stored to an int, as mwcc took it (GCC 14 makes it an
        # error): ov010_func_02166CD0.c's `int r0 = func_02012F08(a)`
        -Wno-int-conversion
        # a pointer passed where the file's own prototype names another
        # pointer type, as mwcc took it (GCC 14 makes it an error): ov204's
        # INTRO_GRAPHIC_Setup3d as G3D_SYSTEM_Init's callback, its file name
        # string as GFL_HEAP_Alloc's
        -Wno-incompatible-pointer-types)
    if(CMAKE_C_COMPILER_ID MATCHES "Clang")
        # deprecated-non-prototype: the `int name();` ovlink declares for a
        # call mwcc compiled with no declaration in scope.
        list(APPEND EIDOS_OVL_MATCHED_OPTIONS
            -Wno-incompatible-library-redeclaration -Wno-pointer-sign
            -Wno-tautological-constant-out-of-range-compare
            -Wno-deprecated-non-prototype)
    endif()
    # Casts between pointers and integers of another width: with 8-byte
    # pointers the pointer-width question docs/pointer-width.md measures,
    # and at either width a few SDK files cast a u16 or a u64 to a pointer
    # on purpose.
    list(APPEND EIDOS_OVL_MATCHED_OPTIONS
        -Wno-int-to-pointer-cast -Wno-pointer-to-int-cast)
endif()

# eidos_overlays_seeds(<var> OVERLAYS <...> LINK_SOURCES <...>): the matched
# ARM9 files (as paths under src/matched/) that define what the overlays
# call and LINK_SOURCES lacks; the boot adds them to its closure.  A plan
# only: writes under ${CMAKE_BINARY_DIR}/ovl/_seeds and compiles nothing.
# The player's ARM9 image beside the data root (docs/armdata.md): ovlink reads
# the linker veneers out of it (tools/ovlink.py --arm9-image).  Optional.
get_filename_component(_eidos_ovl_extract_dir "${BW_DATA_ROOT_ABS}" DIRECTORY)
set(EIDOS_OVL_ARM9_IMAGE "${_eidos_ovl_extract_dir}/arm9_dec.bin")

function(eidos_overlays_seeds var)
    cmake_parse_arguments(A "" "ROOT" "OVERLAYS;LINK_SOURCES" ${ARGN})
    if(NOT A_ROOT)
        set(A_ROOT ${ALETHEIA_ROOT})
    endif()
    set(dir ${CMAKE_BINARY_DIR}/ovl/_seeds)
    file(MAKE_DIRECTORY ${dir})
    string(REPLACE ";" "\n" link_txt "${A_LINK_SOURCES}")
    file(WRITE ${dir}/link_sources.txt "${link_txt}\n")
    string(REPLACE ";" "," ids_arg "${A_OVERLAYS}")
    if(EIDOS_HEAP_MATCHED)
        set(heap 1)
    else()
        set(heap 0)
    endif()
    execute_process(
        COMMAND ${Python3_EXECUTABLE} ${EIDOS_OVLINK_TOOL}
                ${EIDOS_OVLINK_PLATFORM_ARGS}
                --aletheia-root ${A_ROOT} --overlays ${ids_arg}
                --link-sources ${dir}/link_sources.txt
                --define EIDOS_HEAP_MATCHED=${heap}
                --arm9-image ${EIDOS_OVL_ARM9_IMAGE}
                --set-name seeds --out-dir ${dir}
        RESULT_VARIABLE rc
        ERROR_VARIABLE err)
    if(NOT rc EQUAL 0)
        message(FATAL_ERROR "eidos_overlays_seeds: tools/ovlink.py failed (${rc}):\n${err}")
    endif()
    file(STRINGS ${dir}/ovl_arm9_seeds.txt seeds)
    list(LENGTH seeds n)
    message(STATUS "Overlays: ${n} matched ARM9 files the overlays call join the boot's closure")
    set(${var} ${seeds} PARENT_SCOPE)
endfunction()

function(eidos_overlays name)
    cmake_parse_arguments(A "ARM9_STUBS" "TARGET;ROOT" "OVERLAYS;LINK_SOURCES" ${ARGN})
    if(NOT A_TARGET)
        message(FATAL_ERROR "eidos_overlays(${name}): TARGET is required")
    endif()
    if(NOT A_ROOT)
        set(A_ROOT ${ALETHEIA_ROOT})
    endif()
    get_filename_component(A_ROOT "${A_ROOT}" ABSOLUTE)
    if(NOT A_OVERLAYS)
        set(A_OVERLAYS TITLE)
    endif()
    set(dir ${CMAKE_BINARY_DIR}/ovl/${name})
    file(MAKE_DIRECTORY ${dir})

    set(link_list)
    foreach(s ${A_LINK_SOURCES})
        get_filename_component(s "${s}" ABSOLUTE)
        list(APPEND link_list "${s}")
    endforeach()
    string(REPLACE ";" "\n" link_txt "${link_list}")
    file(WRITE ${dir}/link_sources.txt "${link_txt}\n")
    string(REPLACE ";" "," ids_arg "${A_OVERLAYS}")

    set(stub_arg)
    if(A_ARM9_STUBS)
        set(stub_arg --arm9-stubs)
    endif()
    # src/game: read the boot's units as their cut copies (cmake/boot.cmake).
    set(_ovl_cut_arg)
    if(EIDOS_SOURCE_TREE STREQUAL "game" AND EIDOS_GAME_CUT_LIST)
        set(_ovl_cut_arg --cut-list ${EIDOS_GAME_CUT_LIST})
    endif()
    # sdk/ decides some names with `#if EIDOS_HEAP_MATCHED`.
    if(EIDOS_HEAP_MATCHED)
        list(APPEND stub_arg --define EIDOS_HEAP_MATCHED=1)
    else()
        list(APPEND stub_arg --define EIDOS_HEAP_MATCHED=0)
    endif()
    execute_process(
        COMMAND ${Python3_EXECUTABLE} ${EIDOS_OVLINK_TOOL}
                ${EIDOS_OVLINK_PLATFORM_ARGS}
                --aletheia-root ${A_ROOT}
                --overlays ${ids_arg}
                --link-sources ${dir}/link_sources.txt
                --set-name ${name}
                --out-dir ${dir}
                --arm9-image ${EIDOS_OVL_ARM9_IMAGE}
                ${stub_arg} ${_ovl_cut_arg} --summary
        RESULT_VARIABLE rc
        OUTPUT_VARIABLE out
        ERROR_VARIABLE err)
    if(NOT rc EQUAL 0)
        message(FATAL_ERROR "eidos_overlays(${name}): tools/ovlink.py failed (${rc}):\n${err}")
    endif()
    string(STRIP "${out}" out)
    string(REPLACE "\n" "\n   " out "${out}")
    message(STATUS "Overlays ${name}:\n   ${out}")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        ${EIDOS_OVLINK_TOOL}
        ${_eidos_ovl_tools}/armdata.py
        ${_eidos_ovl_tools}/overlay_table.py
        ${_eidos_ovl_tools}/ovlink_exclusions.txt
        ${_eidos_ovl_tools}/../sdk/asm/ov
        ${A_ROOT}/layout/layout.json)
    # Port C for single overlay functions (tools/ovlink.py, --port-ov-dir):
    # the directory exists only while there is some.
    if(EXISTS ${_eidos_ovl_tools}/../stubs/ov)
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
            ${_eidos_ovl_tools}/../stubs/ov)
    endif()
    # The set's shard directories and listings (ovlink writes the list): a
    # matched file added to one of them reruns the plan at the next build.
    file(STRINGS ${dir}/ovl_depends.txt deps)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${deps})

    file(STRINGS ${dir}/ovl_units.txt units)
    set(copies)
    foreach(line ${units})
        string(REPLACE "|" ";" parts "${line}")
        list(GET parts 1 src)
        list(GET parts 2 hdr)
        file(RELATIVE_PATH rel "${A_ROOT}" "${src}")
        if(rel MATCHES "^\\.\\./" OR IS_ABSOLUTE "${rel}")
            # Port C standing in for an asm body (sdk/asm/ov/) or for an
            # unmatched function (stubs/ov/).
            get_filename_component(rel "${src}" NAME)
            set(rel "port/${rel}")
        endif()
        set(copy ${dir}/src/${rel})
        get_filename_component(src_dir "${src}" DIRECTORY)
        # A src/game unit's copy has its asm bodies cut (docs/game-tree.md).
        set(cut_args)
        set(cut_dep)
        if(rel MATCHES "^src/game/")
            set(cut_args --cut-asm)
            set(cut_dep ${EIDOS_GAMECUT_TOOL} ${EIDOS_GAMERETURNS})
            # A port patch (stubs/ov/patch_*) defines some of the unit's
            # functions: tools/ovlink.py lists them in a fourth field.
            list(LENGTH parts nparts)
            if(nparts GREATER 3)
                list(GET parts 3 cut_file)
                list(APPEND cut_args --cut-list ${cut_file} --cut-decls)
                list(APPEND cut_dep ${cut_file})
            endif()
        endif()
        add_custom_command(
            OUTPUT ${copy}
            COMMAND ${Python3_EXECUTABLE} ${EIDOS_DSADDR_TOOL} ${src} -o ${copy} --name ${src} ${cut_args}
            DEPENDS ${src} ${EIDOS_DSADDR_TOOL} ${cut_dep}
            COMMENT "dsaddr ${rel} (overlays ${name})"
            VERBATIM)
        if(MSVC)
            set(fi "/FI${hdr}")
        else()
            set(fi "-include;${hdr}")
        endif()
        set_source_files_properties(${copy} PROPERTIES
            GENERATED TRUE
            INCLUDE_DIRECTORIES "${src_dir}"
            OBJECT_DEPENDS "${hdr}"
            COMPILE_OPTIONS "${fi};${EIDOS_OVL_MATCHED_OPTIONS}")
        list(APPEND copies ${copy})
    endforeach()

    file(STRINGS ${dir}/ovl_c.txt link_c)
    set(gen_c ${link_c} ${dir}/ovl_dispatch.c ${dir}/ovl_arm9_stubs.c)
    add_library(${name}_ovl OBJECT ${copies} ${gen_c})
    target_include_directories(${name}_ovl PRIVATE
        $<TARGET_PROPERTY:eidos,INTERFACE_INCLUDE_DIRECTORIES>)
    target_compile_definitions(${name}_ovl PRIVATE
        $<TARGET_PROPERTY:eidos,INTERFACE_COMPILE_DEFINITIONS>)
    target_sources(${A_TARGET} PRIVATE $<TARGET_OBJECTS:${name}_ovl>)
    list(LENGTH copies n)
    set(${name}_OVL_UNITS ${n} PARENT_SCOPE)
    set(${name}_OVL_DIR ${dir} PARENT_SCOPE)
endfunction()
