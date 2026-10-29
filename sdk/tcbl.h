/*
 * sdk/tcbl.h -- the task-callback list (aletheia's tcbl.c), as the port has
 * to hold it on a 64-bit host, and the port's thin helper over it.
 *
 * This is the game's own scheduler.  One manager owns a fixed pool of nodes;
 * a caller registers a callback with a priority and gets a node back; one
 * pass of the runner walks the live nodes in priority order and calls each
 * callback with the node and its work area.  engine/frame/ runs that pass as
 * the frame loop's step, which is what puts matched game code inside the
 * frame.
 *
 * Which functions are matched, and which the port supplies, is the whole
 * subject of this header:
 *
 *   sub_0203101C   construct        MATCHED  src/matched/sub_0203101C.c
 *   sub_02030FC8   build free list  MATCHED  src/matched/sub_02030FC8.c
 *   sub_020310C4   one pass         MATCHED  src/matched/sub_020310C4.c
 *   sub_02030EF0   row address      ILP32 matched / LP64 stubs/reimpl_
 *   sub_02030F34   free-list push   ILP32 matched / LP64 stubs/reimpl_
 *   func_02030F04  free-list pop    ILP32 matched / LP64 stubs/reimpl_
 *   sub_02030FB4   work accessor    ILP32 matched / LP64 stubs/reimpl_
 *   sub_02030F50   reset a node     ILP32 matched / LP64 stubs/reimpl_
 *   sub_02030F70   unlink a node    ILP32 matched / LP64 stubs/reimpl_
 *   sub_020311B8   register         ILP32 matched / LP64 stubs/reimpl_
 *   sub_02031194   destroy          ILP32 matched / LP64 stubs/reimpl_
 *   sub_0203115C   destroy all      ILP32 matched / LP64 stubs/reimpl_
 *
 * Wave 26 matched the five the port used to supply, so **under ILP32, the
 * reference configuration, the whole of tcbl.c is aletheia's own C and
 * nothing of the task-callback list is a reimplementation.**  On an LP64
 * host nine of the twelve are the port's own, and that is a finding rather
 * than a preference.  See "Why nine matched files are out of the LP64 build"
 * in docs/runtime.md; the short form is below, because the struct in this header
 * is the direct consequence.
 *
 * ---------------------------------------------------------------------
 * What wave 26 changed about that, and why it made LP64 worse not better
 * ---------------------------------------------------------------------
 *
 * The five new sources declare the node as six members with `struct Tcbl
 * *owner` at +0x00 and `void *work` at +0x10.  The three that were already
 * in the build declare the same two fields as `u32 unk_00` and `u32 unk_10`,
 * which is what a matcher writes for a word it does not have to interpret.
 *
 * **On the DS, and on any ILP32 target, those two declarations are the same
 * object**: 0x18 for the node and 0x38 for the manager either way, so the
 * whole family agrees and all twelve files compile as written.  On an LP64
 * host they are not the same object at all -- the pointer form is 48 and 96
 * bytes, the u32 form 40 and 88 -- and the family stops agreeing with
 * itself.  Something has to pick one, and the constructor and the runner
 * pick it: sub_0203101C allocates `sizeof(struct Tcbl)` as *its* compiler
 * sees it and sub_020310C4 walks the node with *its* offsets, so the u32
 * form is what an LP64 build has to hold, and the five pointer-form sources
 * join the four offset-reaching accessors outside it.
 *
 * This is not a defect in either set of declarations: each is byte-correct
 * for the DS, which is the only target that has to be satisfied in aletheia.
 * It is the pointer-width policy earning its keep a second time, and it is
 * the sharpest argument yet for ILP32 being the reference configuration:
 * the port did not choose between two matched readings of the same struct,
 * it configured a target on which there is only one.
 *
 * ---------------------------------------------------------------------
 * The layout, and the two places the DS's and the host's disagree
 * ---------------------------------------------------------------------
 *
 * Every offset below is the one the matched declarations produce when a
 * host compiler lays them out, which is what the matched code in this build
 * actually uses.  The DS offsets are in the right-hand column.
 *
 * The struct below is the **u32 form**, the one src/matched/sub_0203101C.c
 * and src/matched/sub_020310C4.c declare, because those two are in every
 * configuration and decide the layout (see above).  Under ILP32 it is also
 * the pointer form, byte for byte, which is why the five wave-26 sources
 * compile against it as written there.
 *
 *   node    host  DS
 *   owner      0  0x00   a pointer to the manager.  sub_0203101C and
 *                        sub_020310C4 declare +0 as `u32 unk_00` and never
 *                        read it; on an LP64 host that u32 is followed by
 *                        four bytes of padding, and the port widens the pair
 *                        into one 8-byte pointer.  On ILP32 the five
 *                        wave-26 sources declare it as the pointer it is.
 *   prev       8  0x04
 *   next      16  0x08
 *   key       24  0x0c   the priority the list is sorted ascending on
 *   work      28  0x10   **stays a u32 on an LP64 host.**  It is a pointer
 *                        to a separately allocated work area, null when the
 *                        work fits inside the node, and src/matched/
 *                        sub_020311B8.c declares it as one.  There is no
 *                        padding to borrow here the way there is at +0, so
 *                        the LP64 build cannot hold both readings: the port
 *                        refuses an oversize work request rather than
 *                        storing a truncated pointer, and every node on that
 *                        host carries its work area inside itself.  Under
 *                        ILP32 the field is four bytes either way and the
 *                        matched registrar allocates the block as the DS
 *                        does.
 *   func      32  0x14
 *   (work area follows)  40  0x18
 *
 *   manager host  DS
 *   count      0  0x00
 *   work_size  4  0x04
 *   heap_id    8  0x08   the home heap, kept at construction; sub_020311B8
 *                        allocates an oversize work block from it
 *   unk_0c    12  0x0c
 *   destroyed 16  0x10   the runner's "the node being run asked to die"
 *   table     24  0x14
 *   sentinel  32  0x18   a whole node, embedded; the empty list is it
 *                        pointing at itself
 *   cursor    72  0x30   the node the runner is inside, published so a
 *                        callback can fix it up
 *   free_head 80  0x34
 *   sizeof    88  0x38   src/matched/sub_0203101C.c allocates sizeof(struct
 *                        Tcbl), so the matched constructor asks for 88 here
 *                        and 0x38 on the DS, each correct for its target
 *
 * **The node header grew from 0x18 to 40 bytes, and the matched constructor
 * does not know that.**  src/matched/sub_0203101C.c sizes the pool as
 * `(((workSize + 3) & ~3) + 0x18) * count` with 0x18 written out, and
 * sub_02030EF0's row stride is `workSize + 0x18` to match.  Both constants
 * are the DS node header.  Rather than keep the constructor out of the build
 * over one literal, the port pays the difference at the only place it can:
 * bw_tcbl_create adds EIDOS_TCBL_HEADER_BIAS to the caller's work size
 * before handing it to the matched constructor, so the rows the matched
 * arithmetic produces are exactly `40 + work` bytes apart and the last one
 * ends exactly at the end of the table.  The port's own row-address
 * function uses the same `work_size + 0x18` formula the DS one does, which
 * is why the two stay in step.
 */

#ifndef EIDOS_SDK_TCBL_H
#define EIDOS_SDK_TCBL_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

struct EidosTcblList;

typedef struct EidosTcblNode {
    struct EidosTcblList *owner;
    struct EidosTcblNode *prev;
    struct EidosTcblNode *next;
    u32                   key;
#if EIDOS_NATIVE_TCBL
    void                 *work;
#else
    u32                   work;   /* 0 on an LP64 host; see above */
#endif
    void                (*func)(struct EidosTcblNode *node, void *work);
} EidosTcblNode;

typedef struct EidosTcblList {
    u32            count;
    u32            work_size;
    u16            heap_id;
    u16            pad_0a;
    u32            unk_0c;
    u32            destroyed;
    void          *table;
    EidosTcblNode  sentinel;
    EidosTcblNode *cursor;
    EidosTcblNode *free_head;
} EidosTcblList;

/* The DS node header, written out as a literal in the matched constructor
 * and in the matched row-address function. */
#define EIDOS_TCBL_DS_NODE_HEADER 0x18u

/* What the same header costs on this host, and the difference the port adds
 * to a caller's work size so that the matched arithmetic still fits. */
#define EIDOS_TCBL_NODE_HEADER    ((u32)sizeof(EidosTcblNode))
#if EIDOS_NATIVE_TCBL
#define EIDOS_TCBL_HEADER_BIAS 0u
#else
#define EIDOS_TCBL_HEADER_BIAS \
    (EIDOS_TCBL_NODE_HEADER - EIDOS_TCBL_DS_NODE_HEADER)
#endif

/* ------------------------------------------------------------------ */
/* The port's helper                                                   */
/* ------------------------------------------------------------------ */

/*
 * Construct a list of `count` nodes with `work_size` usable bytes each, on
 * heap `heap`, remembering `home_heap` for later allocations (which is what
 * the matched constructor's second parameter turns out to be; see
 * stubs/reimpl_020311B8.c).  Goes straight through the matched constructor
 * src/matched/sub_0203101C.c with the bias above applied.  NULL if the heap
 * has not been initialised.
 */
EidosTcblList *bw_tcbl_create(u32 heap, u16 home_heap, u32 count,
                              u32 work_size);

/* The usable bytes in one node's work area, i.e. what was asked for,
 * rounded up. */
u32 bw_tcbl_work_size(const EidosTcblList *list);

/*
 * Register `func` at priority `key`, with `work_size` bytes of work area.
 * The list runs ascending on `key`, so a smaller key runs first and equal
 * keys keep registration order.  Returns the node, or NULL when the pool is
 * empty.  Panics on an LP64 host if `work_size` exceeds the list's per-node
 * size, because that host cannot represent a node that owns a separate work
 * block; under ILP32 the matched registrar allocates one from the list's
 * home heap and the matched unlink releases it, as the DS does.
 */
EidosTcblNode *bw_tcbl_register(EidosTcblList *list,
                                void (*func)(EidosTcblNode *, void *),
                                u32 work_size, u32 key);

/* The node's work area, through the same accessor the matched runner uses. */
void *bw_tcbl_work(EidosTcblNode *node);

/* One pass: the matched runner src/matched/sub_020310C4.c. */
void bw_tcbl_run(EidosTcblList *list);

/* Destroy one node, and destroy every node. */
void bw_tcbl_destroy(EidosTcblNode *node);
void bw_tcbl_destroy_all(EidosTcblList *list);

/* Release the manager and its pool. */
void bw_tcbl_free(EidosTcblList *list);

#ifdef __cplusplus
}
#endif

#endif /* EIDOS_SDK_TCBL_H */
