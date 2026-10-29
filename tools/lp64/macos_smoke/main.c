#include <stdio.h>
#include "scheduler.h"
#include "wasm-rt-impl.h"

int main(void)
{
    w2c_scheduler instance;
    unsigned failure, width, checks;
    wasm_rt_init();
    wasm2c_scheduler_instantiate(&instance);
    failure = w2c_scheduler_run_checks(&instance);
    width = w2c_scheduler_pointer_width(&instance);
    checks = w2c_scheduler_total_checks(&instance);
    printf("scheduler wasm2c: host pointers %zu bytes, game pointers %u bytes, "
           "%u checks, failure line %u\n", sizeof(void *), width, checks, failure);
    wasm2c_scheduler_free(&instance);
    wasm_rt_free();
    return failure != 0 || width != 4;
}
