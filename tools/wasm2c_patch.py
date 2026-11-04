#!/usr/bin/env python3
"""wasm2c_patch.py -- the host program's one change to wasm2c's output.

    wasm2c_patch.py BUILD_DIR/game-impl.h

Matched C sometimes calls a function by its DS address (a literal stored
as a callback, a pointer the data scan could not see).  A native build
catches the execute fault and continues in the function compiled for that
address (sdk/plat_crash.h).  In the wasm32 build a function pointer is a
table index, and an index in the DS address space is far past the table,
so call_indirect would trap.  This rewrites wasm2c's CALL_INDIRECT macro so
an index in that space first goes through host_code_index (host/main.c),
which asks the module (sdk/plat_crash.c, bw_code_index) for the function's
real index.  Every other index costs one compare.  Standard library only.
"""

import sys

OLD = """#define CALL_INDIRECT(table, t, ft, x, ...) \\
  (CHECK_CALL_INDIRECT(table, ft, x),       \\
   DO_CALL_INDIRECT(table, t, x, __VA_ARGS__))"""

NEW = """/* tools/wasm2c_patch.py: a DS code address becomes its function's index */
u32 host_code_index(u32 x);
#define HOST_DS_CODE_LO 0x01FF8000u
#define HOST_DS_CODE_HI 0x02800000u
#define HOST_CODE_INDEX(x) \\
  (UNLIKELY((u32)(x) - HOST_DS_CODE_LO < HOST_DS_CODE_HI - HOST_DS_CODE_LO) \\
       ? ((x) = host_code_index(x)) : (x))
#define CALL_INDIRECT(table, t, ft, x, ...) \\
  (HOST_CODE_INDEX(x),                      \\
   CHECK_CALL_INDIRECT(table, ft, x),       \\
   DO_CALL_INDIRECT(table, t, x, __VA_ARGS__))
#if defined(HOST_INDIRECT_LOG)
/* a build without --fpcast-emu (host/CMakeLists.txt, EIDOS_HOST_FPCAST=OFF):
 * a call through a pointer of another type is logged, once per caller and
 * target, and made anyway, so a run lists every such call it reaches */
void host_indirect_mismatch(const char *caller, void *target);
#undef CHECK_CALL_INDIRECT
#define CHECK_CALL_INDIRECT(table, ft, x)                               \\
  (LIKELY((x) < table.size && table.data[x].func)                       \\
       ? (func_types_eq(ft, table.data[x].func_type)                    \\
              ? 1                                                       \\
              : (host_indirect_mismatch(__func__,                       \\
                                        (void *)table.data[x].func), 1)) \\
       : TRAP(CALL_INDIRECT))
#endif"""


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    path = sys.argv[1]
    text = open(path, encoding="utf-8").read()
    if NEW in text:
        return 0
    if OLD not in text:
        sys.exit("wasm2c_patch: no CALL_INDIRECT definition of the expected "
                 "shape in %s (a new wasm2c?)" % path)
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write(text.replace(OLD, NEW, 1))
    return 0


if __name__ == "__main__":
    sys.exit(main())
