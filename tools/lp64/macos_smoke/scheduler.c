/* Exercise unmodified game code in its original 32-bit pointer model. */
#include "tcb_list.h"

static unsigned checks;
static unsigned calls;
static union { double alignment; unsigned char bytes[8192]; } storage;

#define CHECK(condition) do { ++checks; if (!(condition)) return __LINE__; } while (0)

static void count_task(struct TcbTask *task, void *work)
{
    (void)task;
    calls += *(unsigned *)work;
}

unsigned pointer_width(void) { return sizeof(void *); }
unsigned total_checks(void) { return checks; }

unsigned run_checks(void)
{
    const unsigned capacities[] = {1, 4, 16, 31};
    unsigned c, i, one = 1;
    checks = 0;
    CHECK(sizeof(void *) == 4);
    for (c = 0; c < sizeof(capacities) / sizeof(*capacities); ++c) {
        unsigned capacity = capacities[c];
        unsigned size = TCB_GetTaskOffset(capacity);
        struct TcbSystem *sys;
        CHECK(size == 0x38 + capacity * 0x20);
        CHECK(size + 64 <= sizeof(storage.bytes));
        for (i = 0; i < size + 64; ++i)
            storage.bytes[i] = 0xA5;
        sys = TCB_Init(capacity, (struct TcbSystem *)storage.bytes);
        for (i = 0; i < capacity; ++i)
            CHECK(TCB_AddTask(sys, count_task, &one, i) != 0);
        CHECK(TCB_AddTask(sys, count_task, &one, 0) == 0);
        calls = 0;
        TCB_Main(sys);
        CHECK(calls == capacity);
        TCB_Reset(sys);
        CHECK(TCB_AddTask(sys, count_task, &one, 0) != 0);
        for (i = 0; i < 64; ++i)
            CHECK(storage.bytes[size + i] == 0xA5);
    }
    return 0;
}
