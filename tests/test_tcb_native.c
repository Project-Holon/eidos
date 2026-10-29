#include <stdlib.h>
#include "tcb_list.h"
#include "port_test.h"

static unsigned calls;
static void count_task(struct TcbTask *task, void *work)
{
    (void)task;
    calls += *(unsigned *)work;
}

int main(void)
{
    const unsigned capacities[] = {1, 4, 16, 31};
    unsigned c, i, one = 1;
    for (c = 0; c < sizeof(capacities) / sizeof(*capacities); ++c) {
        unsigned capacity = capacities[c];
        unsigned size = TCB_GetTaskOffset(capacity);
        unsigned char *storage = malloc(size + 64);
        struct TcbSystem *sys;
        BW_REQUIRE("allocation", storage != NULL);
        memset(storage, 0xA5, size + 64);
        sys = TCB_Init(capacity, (struct TcbSystem *)storage);
        for (i = 0; i < capacity; ++i)
            BW_REQUIRE("fill native task pool", TCB_AddTask(sys, count_task, &one, i) != NULL);
        BW_REQUIRE("full task pool", TCB_AddTask(sys, count_task, &one, 0) == NULL);
        calls = 0;
        TCB_Main(sys);
        BW_CHECK_EQ("all callbacks run", calls, capacity);
        TCB_Reset(sys);
        BW_REQUIRE("pool reusable after reset", TCB_AddTask(sys, count_task, &one, 0) != NULL);
        for (i = 0; i < 64; ++i)
            BW_CHECK_EQ("allocation guard intact", storage[size + i], 0xA5);
        free(storage);
    }
    BW_REPORT("test_tcb_native");
}
