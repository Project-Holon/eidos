# cmake/check_image.cmake -- run after linking the 32-bit game program
# (cmake -DOBJDUMP=... -DEXE=... -P check_image.cmake).
#
# The 32-bit build maps the DS's main memory at its own address, 0x02000000
# (sdk/dsmem.c identity_choose).  Windows loads the program at its image
# base and puts the first thread's stack (its reserve) right after it; if
# image and stack reach 0x02000000 the mapping falls back to movable blocks
# and the game fails early.  This check fails the build first, with the room
# left, so big static buffers go to the heap instead (docs/building.md).

if(NOT OBJDUMP OR NOT EXE)
    message(FATAL_ERROR "check_image: OBJDUMP and EXE are needed")
endif()
execute_process(COMMAND "${OBJDUMP}" -p "${EXE}" OUTPUT_VARIABLE out RESULT_VARIABLE rc)
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "check_image: ${OBJDUMP} -p failed on ${EXE}")
endif()

function(pe_field name var)
    string(REGEX MATCH "${name}[ \t]+([0-9a-fA-F]+)" m "${out}")
    if(NOT m)
        message(FATAL_ERROR "check_image: no ${name} in ${EXE}")
    endif()
    math(EXPR v "0x${CMAKE_MATCH_1}" OUTPUT_FORMAT DECIMAL)
    set(${var} ${v} PARENT_SCOPE)
endfunction()

pe_field("ImageBase" base)
pe_field("SizeOfImage" size)
pe_field("SizeOfStackReserve" stack)

# a 64 KB margin: the loader rounds the stack's place up
math(EXPR end "${base} + ${size} + ${stack} + 0x10000")
math(EXPR limit "0x02000000")
math(EXPR room "${limit} - ${end}")
math(EXPR end_hex "${end}" OUTPUT_FORMAT HEXADECIMAL)
if(room LESS 0)
    message(FATAL_ERROR
        "check_image: ${EXE} is too big for the DS memory's own address: image and stack end at ${end_hex}, "
        "past 0x02000000. Move a large static buffer to the heap (see cmake/check_image.cmake).")
endif()
math(EXPR room_kb "${room} / 1024")
message(STATUS "check_image: ${room_kb} KB left below the DS memory (0x02000000)")
