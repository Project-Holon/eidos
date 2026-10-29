/*
 * tests/test_tcbl.c -- the game's own scheduler, running on the host.
 *
 * No SDL, no game data, no oracle: this test needs nothing but the library.
 * What it drives is aletheia's tcbl.c through the matched constructor and
 * the matched runner, over the matched heap layer, and what it asserts is
 * what those functions do.
 *
 * The matched code under test, and what each assertion rests on:
 *
 *   src/matched/sub_0203101C.c   the constructor.  Allocates the manager
 *                                (sizeof 0x38 on the DS, 88 here) and the
 *                                node pool, remembers the count, the work
 *                                size and the home heap, builds the free
 *                                list and self-links the sentinel.  Checked
 *                                by: the fields it stored, the pool being
 *                                exactly `count` nodes deep, and the heap's
 *                                own live-block count going up by two.
 *   src/matched/sub_02030FC8.c   the free-list build.  Checked by: exactly
 *                                `count` registrations succeed and the next
 *                                one fails, which is only true if every row
 *                                of the table reached the free list.
 *   src/matched/sub_020310C4.c   one pass of the runner.  Checked by: the
 *                                order callbacks fire in, the argument each
 *                                receives, that a pass over an empty list
 *                                is a no-op, that a node destroyed from
 *                                inside its own callback survives the rest
 *                                of the pass and is gone from the next one,
 *                                and that the cursor is null after a pass.
 *   src/matched/sub_020311B8.c   the registrar.  Checked by: the pool being
 *                                exactly `count` deep, three registrations
 *                                out of key order firing in key order, and a
 *                                re-registration landing between two
 *                                survivors.
 *   src/matched/sub_02031194.c   destroy one node, and
 *   src/matched/sub_0203115C.c   destroy every node.  Checked by: a node
 *                                removed from outside a pass dropping out of
 *                                the order, a node destroying itself from
 *                                inside its own callback, and the heap
 *                                balancing back to zero after teardown.
 *   src/matched/sub_02030F50.c   reset a node, and
 *   src/matched/sub_02030F70.c   unlink one and release its work block.
 *   src/matched/sub_02030FB4.c   the work accessor the runner calls to
 *   (ILP32 only) build the callback's second argument.
 *   src/matched/sub_02030734.c   the allocation entry point, and under it
 *   src/matched/sub_02030098.c   the raw allocator, the provenance stamp
 *   src/matched/sub_02030880.c   and the audit filter.  Checked by: the
 *   src/matched/sub_02030A00.c   block header the raw allocator wrote
 *   src/matched/sub_020304B8.c   (its heap id, read back through the
 *   src/matched/sub_020304C0.c   matched accessor), the provenance name it
 *   src/matched/GFL_HEAP_ReadAllocSiteName.c   stamped ("tcbl.c", validated by the
 *   src/matched/sub_0203046C.c   matched validator), and the live-block
 *   src/matched/unit_0202FBAC.c  count rising and falling.
 *
 * **Under ILP32, the reference configuration, every line this test drives is
 * aletheia's own C.**  The ordering assertion used to be half a statement
 * about matched code -- the runner walking the list forward from the
 * sentinel, which is sub_020310C4 -- and half about the port's transcription
 * of 0x020311B8's listing.  Wave 26 matched the registrar, so in that
 * configuration both halves are matched: the ascending-key insert and the
 * forward walk that turns it into a priority order.  Nothing in this file
 * had to change for that, which was the point of writing it against the
 * behaviour rather than against the stand-in.
 *
 * On an LP64 host nine of tcbl.c's twelve functions are still stubs/reimpl_
 * files, and the ordering assertion is still half the port's there.  The
 * reason is not that they are undecompiled: it is that the family's own
 * declarations of the node disagree about its size once a pointer is eight
 * bytes (sdk/tcbl.h).  Both configurations run this whole test, so the two
 * readings are checked against the same 49 assertions.
 */

#include <stdio.h>
#include <string.h>

#include "port.h"
#include "tcbl.h"
#include "port_test.h"

/* The matched block-header accessors the provenance check reads through. */
extern u16   sub_020304B8(void *p);
extern void *sub_020304C0(void *p);
extern int   GFL_HEAP_ReadAllocSiteName(u8 *dst, const char *src);

#define HEAP_ID    3u
#define POOL_NODES 4u
#define WORK_BYTES 16u

/* ------------------------------------------------------------------ */
/* What the callbacks record                                           */
/* ------------------------------------------------------------------ */

typedef struct Work {
    u32   magic;      /* set before the first pass; the callback checks it */
    u32   id;         /* which callback this node belongs to               */
    u32   calls;      /* how many times it has been run                    */
    u32   suicide;    /* non-zero: destroy this node on that call number   */
} Work;

#define WORK_MAGIC 0x7CB17CB1u

static char  g_log[64];
static int   g_log_len;
static int   g_bad_arg;

static void log_id(u32 id)
{
    if (g_log_len < (int)sizeof(g_log) - 1) {
        g_log[g_log_len++] = (char)('a' + (int)id);
        g_log[g_log_len] = '\0';
    }
}

/*
 * The one callback all three nodes share.  Everything it can check about
 * how it was called, it checks: the runner hands it the node it registered
 * and that node's own work area, and nothing else may arrive here.
 */
static void task(EidosTcblNode *node, void *work)
{
    Work *w = (Work *)work;

    if (w == NULL || w->magic != WORK_MAGIC) {
        g_bad_arg++;
        return;
    }
    /* The second argument must be exactly what the work accessor answers
     * for the node that is the first argument. */
    if (work != bw_tcbl_work(node)) {
        g_bad_arg++;
        return;
    }
    w->calls++;
    log_id(w->id);

    if (w->suicide != 0 && w->calls == w->suicide) {
        bw_tcbl_destroy(node);
    }
}

static void reset_log(void)
{
    g_log[0] = '\0';
    g_log_len = 0;
}

/* ------------------------------------------------------------------ */

int main(void)
{
    EidosTcblList *list;
    EidosTcblNode *n_low, *n_mid, *n_high, *n_extra;
    Work *w_low, *w_mid, *w_high;
    long live_before, live_after;
    u32 i;

    printf("tcbl: node header %u bytes on this host, 0x18 on the DS; "
           "manager %u bytes, 0x38 on the DS\n",
           (unsigned)EIDOS_TCBL_NODE_HEADER,
           (unsigned)sizeof(EidosTcblList));

    /* The host layout has to agree with what the matched constructor
     * allocates: it asks for sizeof(struct Tcbl) as its own compiler sees
     * it, and every other matched file in the family sees the same.
     *
     * Both sizes are decided by the target's pointer width and by nothing
     * else, which is the whole of docs/pointer-width.md in two numbers: an
     * ILP32 target reproduces the DS's own 0x18 and 0x38 exactly, and an
     * LP64 one grows the node to 40 bytes and the manager to 88.  On ILP32
     * the header bias is therefore zero, and the nine EXACT sources this
     * build is compiling as written are reaching the right fields. */
    if (sizeof(void *) == 4) {
        BW_CHECK_EQ("manager size (ILP32: the DS's own 0x38)",
                    sizeof(EidosTcblList), 0x38);
        BW_CHECK_EQ("node size (ILP32: the DS's own 0x18)",
                    EIDOS_TCBL_NODE_HEADER, EIDOS_TCBL_DS_NODE_HEADER);
        BW_CHECK_EQ("no header bias to pay on an ILP32 target",
                    EIDOS_TCBL_HEADER_BIAS, 0);
    } else {
#if EIDOS_NATIVE_TCBL
        BW_CHECK_EQ("manager size (native pointers)", sizeof(EidosTcblList), 96);
        BW_CHECK_EQ("node size (native pointers)", EIDOS_TCBL_NODE_HEADER, 48);
        BW_CHECK_EQ("native scheduler sizes its own rows", EIDOS_TCBL_HEADER_BIAS, 0);
#else
        BW_CHECK_EQ("manager size", sizeof(EidosTcblList), 88);
        BW_CHECK_EQ("node size", EIDOS_TCBL_NODE_HEADER, 40);
        BW_CHECK_EQ("header bias the port pays on an LP64 target",
                    EIDOS_TCBL_HEADER_BIAS, 40 - EIDOS_TCBL_DS_NODE_HEADER);
#endif
    }

    bw_heap_init(BW_HEAP_COUNT);
    live_before = bw_heap_live_blocks(HEAP_ID);
    BW_CHECK_EQ("heap starts empty", live_before, 0);

    /* ---------------------------------------------------------------- */
    /* Construction: src/matched/sub_0203101C.c                          */
    /* ---------------------------------------------------------------- */

    list = bw_tcbl_create(HEAP_ID, (u16)HEAP_ID, POOL_NODES, WORK_BYTES);
    BW_REQUIRE("the matched constructor returned a list", list != NULL);

    /* The two allocations it makes, counted by the matched raw allocator. */
    BW_CHECK_EQ("constructor allocated manager and table",
                bw_heap_live_blocks(HEAP_ID), 2);
    BW_CHECK_EQ("count stored", list->count, POOL_NODES);
    BW_CHECK_EQ("home heap stored at +8", list->heap_id, HEAP_ID);
    BW_CHECK_EQ("work size stored, with the host's header bias",
                list->work_size, WORK_BYTES + EIDOS_TCBL_HEADER_BIAS);
    BW_CHECK_EQ("usable work size", bw_tcbl_work_size(list), WORK_BYTES);
    BW_REQUIRE("table allocated", list->table != NULL);
    BW_CHECK_EQ("cursor cleared on the way out", list->cursor == NULL, 1);

    /* The sentinel self-link: an empty list is the sentinel pointing at
     * itself from both sides.  That is the last thing the constructor does
     * before clearing the cursor, and it is what makes the runner's walk
     * terminate. */
    BW_CHECK_EQ("sentinel next is itself",
                list->sentinel.next == &list->sentinel, 1);
    BW_CHECK_EQ("sentinel prev is itself",
                list->sentinel.prev == &list->sentinel, 1);

    /* The block header the matched raw allocator wrote in front of the
     * manager, read back through the matched accessors. */
    {
        u8 name[0x13];

        BW_CHECK_EQ("block header carries the heap id",
                    sub_020304B8(list), HEAP_ID);
        BW_CHECK_EQ("provenance name validates",
                    GFL_HEAP_ReadAllocSiteName(name, (const char *)sub_020304C0(list)), 1);
        printf("provenance stamped by sub_02030880: \"%s\"\n",
               (const char *)name);
        BW_CHECK_EQ("provenance name is tcbl.c",
                    strcmp((const char *)name, "tcbl.c"), 0);
    }

    /* A pass over the empty list must do nothing and must leave the cursor
     * null: sub_020310C4's `while` rather than `do`/`while`. */
    reset_log();
    bw_tcbl_run(list);
    BW_CHECK_EQ("empty pass logs nothing", g_log_len, 0);
    BW_CHECK_EQ("empty pass clears the cursor", list->cursor == NULL, 1);

    /* ---------------------------------------------------------------- */
    /* Registration, out of priority order on purpose                    */
    /* ---------------------------------------------------------------- */

    n_high = bw_tcbl_register(list, task, sizeof(Work), 30u);   /* 'c' */
    n_low  = bw_tcbl_register(list, task, sizeof(Work), 10u);   /* 'a' */
    n_mid  = bw_tcbl_register(list, task, sizeof(Work), 20u);   /* 'b' */
    BW_REQUIRE("three registrations", n_high && n_low && n_mid);

    w_low = (Work *)bw_tcbl_work(n_low);
    w_mid = (Work *)bw_tcbl_work(n_mid);
    w_high = (Work *)bw_tcbl_work(n_high);
    BW_REQUIRE("three work areas", w_low && w_mid && w_high);

    /* The work area of a node that does not own a block is the node's own
     * tail, which is what src/matched/sub_02030FB4.c answers. */
    BW_CHECK_EQ("work area is the node's tail",
                (u8 *)w_low - (u8 *)n_low, (long)EIDOS_TCBL_NODE_HEADER);
    BW_CHECK_EQ("three distinct work areas",
                w_low != w_mid && w_mid != w_high && w_low != w_high, 1);
    BW_CHECK_EQ("registering did not allocate",
                bw_heap_live_blocks(HEAP_ID), 2);

    memset(w_low, 0, sizeof(Work));
    memset(w_mid, 0, sizeof(Work));
    memset(w_high, 0, sizeof(Work));
    w_low->magic = w_mid->magic = w_high->magic = WORK_MAGIC;
    w_low->id = 0;    /* 'a', priority 10 */
    w_mid->id = 1;    /* 'b', priority 20 */
    w_high->id = 2;   /* 'c', priority 30 */

    /* The pool is exactly POOL_NODES deep, which is only true if the matched
     * free-list build walked every row of the table. */
    n_extra = bw_tcbl_register(list, task, sizeof(Work), 40u);
    BW_REQUIRE("a fourth registration fits", n_extra != NULL);
    BW_CHECK_EQ("the pool is empty after four",
                bw_tcbl_register(list, task, sizeof(Work), 50u) == NULL, 1);
    bw_tcbl_destroy(n_extra);
    BW_CHECK_EQ("destroying returns the node to the pool",
                bw_tcbl_register(list, task, sizeof(Work), 50u) != NULL, 1);
    /* Take that one back out again; it has no work area set up. */
    bw_tcbl_destroy(list->sentinel.prev);

    /* ---------------------------------------------------------------- */
    /* The runner: src/matched/sub_020310C4.c                            */
    /* ---------------------------------------------------------------- */

    for (i = 0; i < 3u; i++) {
        char label[48];

        reset_log();
        bw_tcbl_run(list);
        sprintf(label, "pass %u fires in priority order", (unsigned)i);
        BW_CHECK_EQ(label, strcmp(g_log, "abc"), 0);
        BW_CHECK_EQ("cursor cleared after the pass", list->cursor == NULL, 1);
    }
    BW_CHECK_EQ("every callback saw its own node and work area",
                g_bad_arg, 0);
    BW_CHECK_EQ("low ran three times", w_low->calls, 3);
    BW_CHECK_EQ("mid ran three times", w_mid->calls, 3);
    BW_CHECK_EQ("high ran three times", w_high->calls, 3);

    /* ---------------------------------------------------------------- */
    /* Removal from outside a pass                                       */
    /* ---------------------------------------------------------------- */

    bw_tcbl_destroy(n_mid);
    reset_log();
    bw_tcbl_run(list);
    BW_CHECK_EQ("the removed node is gone from the order",
                strcmp(g_log, "ac"), 0);
    BW_CHECK_EQ("mid did not run again", w_mid->calls, 3);
    BW_CHECK_EQ("low ran a fourth time", w_low->calls, 4);
    BW_CHECK_EQ("high ran a fourth time", w_high->calls, 4);

    /* And it went back to the free list, so it can be registered again --
     * this time at a priority between the two survivors. */
    n_mid = bw_tcbl_register(list, task, sizeof(Work), 15u);
    BW_REQUIRE("the node came back", n_mid != NULL);
    w_mid = (Work *)bw_tcbl_work(n_mid);
    memset(w_mid, 0, sizeof(Work));
    w_mid->magic = WORK_MAGIC;
    w_mid->id = 3;   /* 'd' */
    reset_log();
    bw_tcbl_run(list);
    BW_CHECK_EQ("re-registered at 15, it runs between 10 and 30",
                strcmp(g_log, "adc"), 0);

    /* ---------------------------------------------------------------- */
    /* Removal from inside the node's own callback                       */
    /* ---------------------------------------------------------------- */

    /*
     * This is the arm the matched runner exists for: the callback asks to
     * die, the runner sees the flag it set at +0x10, reads the successor out
     * before unlinking, and carries on.  So the rest of the pass must still
     * happen, and the node must be gone from the next one.
     */
    w_mid->suicide = w_mid->calls + 1u;
    reset_log();
    bw_tcbl_run(list);
    BW_CHECK_EQ("the suicide pass still completes", strcmp(g_log, "adc"), 0);
    BW_CHECK_EQ("cursor cleared after it", list->cursor == NULL, 1);

    reset_log();
    bw_tcbl_run(list);
    BW_CHECK_EQ("and the node is gone from the next pass",
                strcmp(g_log, "ac"), 0);
    BW_CHECK_EQ("no callback was mis-called", g_bad_arg, 0);

    /* ---------------------------------------------------------------- */
    /* Teardown, and the heap balances                                   */
    /* ---------------------------------------------------------------- */

    bw_tcbl_destroy_all(list);
    reset_log();
    bw_tcbl_run(list);
    BW_CHECK_EQ("an emptied list runs nothing", g_log_len, 0);

    bw_tcbl_free(list);
    live_after = bw_heap_live_blocks(HEAP_ID);
    BW_CHECK_EQ("the heap balances", live_after, 0);

    BW_REPORT("test_tcbl");
}
