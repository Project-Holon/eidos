#!/bin/sh
# host/switch/build.sh -- the Switch program (eidos.nro) from the 64-bit
# route's generated C (docs/consoles.md).  Runs where devkitPro's devkitA64,
# libnx and the Switch portlibs are (devkitPro's devkitpro/devkita64 image):
#
#   sh host/switch/build.sh <host build dir> <wabt source> <SDL3 include dir> <out dir>
#
# <host build dir> is a 64-bit host build made with EIDOS_HOST_BOUNDS_CHECKS
# (tools/build64.py's stage two): its game_*.c, game.h and gl_imports.c are
# the game.  SDL3's headers give the host its types (host/switch/sw2.h).
set -e
SRC=$1
WABT=$2/wasm2c
SDL3=$3
OUT=$4
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
HERE=$ROOT/host/switch
DKA=$DEVKITPRO/devkitA64/bin/aarch64-none-elf
PORT=$DEVKITPRO/portlibs/switch
JOBS=${JOBS:-8}
mkdir -p $OUT/game $OUT/host

ARCH="-march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE"
# The module's memory (docs/consoles.md, "Memory checks"): guard pages by
# default (host/switch/vmem.c), no check in the game's code; "bounds" keeps
# an explicit check on every access, for comparisons.
case ${EIDOS_SWITCH_MEMCHECK:-guard} in
    guard)  MEMCHECK="-DWASM_RT_MEMCHECK_GUARD_PAGES=1 -DWASM_RT_USE_MMAP=1" ;;
    bounds) MEMCHECK="-DWASM_RT_MEMCHECK_BOUNDS_CHECK=1 -DWASM_RT_USE_MMAP=0" ;;
    *) echo "EIDOS_SWITCH_MEMCHECK is guard or bounds"; exit 1 ;;
esac
COMMON="-O2 -pipe $ARCH -ffunction-sections -fdata-sections -D__SWITCH__ \
 -I$DEVKITPRO/libnx/include -I$PORT/include -fsigned-char \
 -DHOST_MEMORY_BUDGET_MB=${EIDOS_HOST_MEMORY_MB:-512} $MEMCHECK \
 -DWASM_RT_NONCONFORMING_UNCHECKED_STACK_EXHAUSTION=1 -DWASM_RT_SKIP_SIGNAL_RECOVERY=1 \
 -DWASM_RT_TRAP_HANDLER=host_trap \
 -I$HERE/include -include $HERE/include/switch_compat.h -I$SRC -I$WABT"
HOSTF="$COMMON -DHOST_GL_ES_FALLBACK=1 -I$ROOT/host -I$ROOT/install -I$ROOT/sdk -I$SDL3 -I$HERE"
export DKA COMMON HOSTF OUT

# 1. SDL2 with its names moved aside (S2_SDL_*), so the host's SDL3 names
#    are the layer's (host/switch/sw2.h)
$DKA-nm -g --defined-only $PORT/lib/libSDL2.a 2>/dev/null | awk '$3 ~ /^SDL_/ {print $3}' \
    | sort -u > $OUT/sdl2_names.txt
awk '{print $1 " S2_" $1}' $OUT/sdl2_names.txt > $OUT/sdl2_rename.txt
awk '{print "#define " $1 " S2_" $1}' $OUT/sdl2_names.txt > $OUT/sdl2_rename.h
$DKA-objcopy --redefine-syms=$OUT/sdl2_rename.txt $PORT/lib/libSDL2.a $OUT/libSDL2_s2.a
echo "SDL2: $(wc -l < $OUT/sdl2_names.txt) names renamed"

# 2. the game (only what changed; all of it when the flags changed, as
#    the memory mode does)
start=$(date +%s)
if [ "$(cat $OUT/game/flags.txt 2>/dev/null)" != "$COMMON" ]; then
    rm -f $OUT/game/*.o
    printf '%s' "$COMMON" > $OUT/game/flags.txt
fi
ls $SRC/game_*.c | xargs -P $JOBS -I{} sh -c '
  o=$OUT/game/$(basename {} .c).o
  [ $o -nt {} ] || $DKA-gcc $COMMON -c {} -o $o || echo "FAIL {}"'
echo "game: $(( $(date +%s) - start )) s"

# 3. the runtime, the host and the layer
for f in $SRC/gl_imports.c $WABT/wasm-rt-impl.c $WABT/wasm-rt-mem-impl.c \
         $WABT/wasm-rt-exceptions-impl.c \
         $ROOT/host/main.c $ROOT/host/fiber.c $ROOT/host/par.c $ROOT/host/net.c \
         $ROOT/host/wasi.c $ROOT/host/sdl.c $ROOT/host/sdl_gpu.c $ROOT/host/pick_switch.c \
         $ROOT/install/location.c $ROOT/sdk/plat_save.c $ROOT/sdk/plat_dir.c \
         $HERE/sdl3_on_sdl2.c $HERE/sdl3_gpu_stubs.c $HERE/vmem.c; do
    $DKA-gcc $HOSTF -c $f -o $OUT/host/$(basename $f .c).o
done
$DKA-gcc $COMMON -I$HERE -include $OUT/sdl2_rename.h -c $HERE/sdl2_side.c -o $OUT/host/sdl2_side.o

# 4. the program: SDL2's own libraries (Mesa's EGL and GLES, libnx) by its
#    pkg-config, with the renamed SDL2 in its place
LIBS=$($PORT/bin/aarch64-none-elf-pkg-config --libs --static sdl2 | sed 's/-lSDL2main//; s/-lSDL2 / /; s/-lSDL2$//')
$DKA-gcc -specs=$DEVKITPRO/libnx/switch.specs $ARCH -Wl,--gc-sections -Wl,-Map,$OUT/eidos.map \
    -o $OUT/eidos.elf $OUT/game/*.o $OUT/host/*.o \
    $OUT/libSDL2_s2.a -L$PORT/lib $LIBS -L$DEVKITPRO/libnx/lib -lnx -lstdc++ -lm 2> $OUT/link.log \
    || { grep -o "undefined reference to .*" $OUT/link.log | sort | uniq -c | sort -rn | head -40; exit 1; }
$DEVKITPRO/tools/bin/nacptool --create "eidos" "Project Holon" "${EIDOS_VERSION:-0.2.4}" $OUT/eidos.nacp
$DEVKITPRO/tools/bin/elf2nro $OUT/eidos.elf $OUT/eidos.nro --nacp=$OUT/eidos.nacp \
    --icon=$DEVKITPRO/libnx/default_icon.jpg
ls -l $OUT/eidos.elf $OUT/eidos.nro
