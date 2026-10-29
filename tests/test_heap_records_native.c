/* The record populated by src/game must match the SDK and matched readers. */
#include "port.h"
#include "port_test.h"

struct NativeHeapRecord {
    void *heap, *parent, *block;
    u16 count;
    u8 external, pad;
};
struct NativeHeapManager {
    struct NativeHeapRecord *records;
    u8 *ids;
    u16 count, static_count, record_count, error;
};
extern struct NativeHeapManager g_EnvModule;
void *GFL_HEAP_GetHeap(u32 id);
void *GFL_HEAP_GetParentHeap(u32 id);
void *GFL_HEAP_GetBlock(u32 id);
u16 GFL_HEAP_GetAllocCount(u32 id);

int main(void)
{
    int a, b, parent, block;
    u8 ids[2] = {0, 1};
    struct NativeHeapRecord records[2] = {
        { &a, NULL, NULL, 3, 0, 0 },
        { &b, &parent, &block, 5, 0, 0 }
    };
    g_EnvModule.records = records;
    g_EnvModule.ids = ids;
    g_EnvModule.count = 2;
    BW_REQUIRE("SDK record stride", bw_heap_handle(1) == &b);
    BW_REQUIRE("game record stride", GFL_HEAP_GetHeap(1) == &b);
    BW_REQUIRE("parent pointer width", GFL_HEAP_GetParentHeap(1) == &parent);
    BW_REQUIRE("block pointer width", GFL_HEAP_GetBlock(1) == &block);
    BW_CHECK_EQ("allocation count offset", GFL_HEAP_GetAllocCount(1), 5);
    BW_REPORT("test_heap_records_native");
}
