/*
 * cmake/wamr/wasi/invoke_native.c -- the call from a mod's code into the
 * port, for the wasm32 build.  It takes the place of WAMR's
 * core/iwasm/common/arch/invokeNative_general.c there.
 *
 * WAMR's portable routine is `void invokeNative(f, argv, argc)`: it calls f
 * with argc words and returns nothing.  WAMR then calls it through pointers
 * cast to the result's type (invokeNative_Int32, _Int64, _Float32,
 * _Float64), and takes as the result what f left in the machine's return
 * register.  A wasm function has no return register: the cast call gave 0,
 * whatever f returned.  Every call a mod made into the port came back 0 in
 * the 64-bit build (the game is one wasm module there, and the runtime is
 * in it).  A mod that asked for a text got the text and the length 0:
 * arkhipelagos read an empty mode at a new game and played the plain game.
 *
 * Here the routine returns what f returned, as 64 bits.  The module is
 * built with every indirect call through one shape (wasm-opt --fpcast-emu,
 * 64 bits a value): f's own result comes back in those 64 bits, this
 * routine hands them on, and the cast call takes them as its type (the low
 * 32 bits for an int32 or a float32, all 64 for an int64 or a float64).
 * cmake/wamr/CMakeLists.txt gives WAMR's declaration the same result type
 * in a copy of wasm_runtime_common.c.
 */

#include "wasm_runtime_common.h"
#include "wasm_exec_env.h"

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-non-prototype"
#pragma clang diagnostic ignored "-Wstrict-prototypes"
#endif

typedef uint64 (*EidosNative)();

uint64
invokeNative(void (*native_code)(), uint32 argv[], uint32 argc)
{
    EidosNative f = (EidosNative)native_code;
    uint32 *a = argv;

    bh_assert(argc >= sizeof(WASMExecEnv *) / sizeof(uint32));

    switch (argc) {
        case 0:  return f();
        case 1:  return f(a[0]);
        case 2:  return f(a[0], a[1]);
        case 3:  return f(a[0], a[1], a[2]);
        case 4:  return f(a[0], a[1], a[2], a[3]);
        case 5:  return f(a[0], a[1], a[2], a[3], a[4]);
        case 6:  return f(a[0], a[1], a[2], a[3], a[4], a[5]);
        case 7:  return f(a[0], a[1], a[2], a[3], a[4], a[5], a[6]);
        case 8:  return f(a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7]);
        case 9:  return f(a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7], a[8]);
        case 10: return f(a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7], a[8], a[9]);
        case 11: return f(a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7], a[8], a[9], a[10]);
        case 12: return f(a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7], a[8], a[9], a[10], a[11]);
        case 13: return f(a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7], a[8], a[9], a[10], a[11], a[12]);
        case 14: return f(a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7], a[8], a[9], a[10], a[11], a[12], a[13]);
        case 15: return f(a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7], a[8], a[9], a[10], a[11], a[12], a[13], a[14]);
        case 16: return f(a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7], a[8], a[9], a[10], a[11], a[12], a[13], a[14], a[15]);
        case 17: return f(a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7], a[8], a[9], a[10], a[11], a[12], a[13], a[14], a[15], a[16]);
        case 18: return f(a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7], a[8], a[9], a[10], a[11], a[12], a[13], a[14], a[15], a[16], a[17]);
        case 19: return f(a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7], a[8], a[9], a[10], a[11], a[12], a[13], a[14], a[15], a[16], a[17], a[18]);
        case 20: return f(a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7], a[8], a[9], a[10], a[11], a[12], a[13], a[14], a[15], a[16], a[17], a[18], a[19]);
        default:
        {
            /* (20 words, as WAMR's own routine: wasm-opt's one shape has 20
             * values, host/CMakeLists.txt max-func-params) */
            WASMExecEnv *exec_env = *(WASMExecEnv **)argv;
            WASMModuleInstanceCommon *module_inst = exec_env->module_inst;

            wasm_runtime_set_exception(module_inst, "the argument number of native function exceeds maximum");
            return 0;
        }
    }
}

#if defined(__clang__)
#pragma clang diagnostic pop
#endif
