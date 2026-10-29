/*
 * sub_02030F70 -- unlink one task-callback node and return it to the free
 * list.  REIMPLEMENTATION, not a match.
 *
 * **This file is the LP64 stand-in only.**  aletheia matched 0x02030F70 in
 * wave 26 (src/matched/sub_02030F70.c, EXACT), and under ILP32 that source is
 * what the build compiles: this file is left out of the library
 * entirely.  It survives because the matched source declares the node's
 * +0x00 and +0x10 as pointers while src/matched/sub_0203101C.c and
 * src/matched/sub_020310C4.c declare them as u32, which is one struct on
 * the DS and two different ones on a 64-bit host (sdk/tcbl.h).
 *
 * Sixty-eight bytes, one branch, two calls (aletheia
 * asm/arm9/src/013_02030F70.s, 0x02030F70):
 *
 *     push  {r4, lr}
 *     mov   r4, r0
 *     ldr   r0, [r4, #0x10]      the owned work pointer
 *     mov   r1, #0
 *     str   r1, [r4, #0x14]      node->func = 0
 *     cmp   r0, #0
 *     beq   .unlink
 *     bl    sub_020307B0         release it if the node owns one
 *   .unlink:
 *     ldmib r4, {r0, r2}         r0 = node->prev, r2 = node->next
 *     str   r2, [r0, #8]         prev->next = next
 *     ldr   r2, [r4, #4]
 *     ldr   r0, [r4, #8]
 *     mov   r1, r4
 *     str   r2, [r0, #4]         next->prev = prev
 *     ldr   r0, [r4]             the node's owner
 *     bl    func_02030F34        push the node onto the free list
 *     pop   {r4, pc}
 *
 * The node layout is the one stubs/reimpl_02030F50.c documents and the
 * matched family corroborates.  Three things are worth naming:
 *
 *  - `node->func = 0` happens first and unconditionally.  It is what makes a
 *    destroyed node harmless if something still holds it: the runner would
 *    call a null pointer rather than stale code, which is a crash the DS
 *    would show immediately rather than a wrong frame.
 *  - The work area is released only when the node *owns* one.  A node whose
 *    request fitted the list's per-node work size has a null pointer at
 *    +0x10 and its work lives inside the node (src/matched/sub_02030FB4.c),
 *    so there is nothing to free.  On this host that is the only case there
 *    is: the port never lets a node own a separate block, because +0x10 is
 *    a u32 in every matched declaration of the node (sdk/tcbl.h).  The arm
 *    is kept because it is what the DS does, and it stays reachable the day
 *    the field can hold a pointer.
 *  - The push onto the free list goes to the owner at +0, whose free-list
 *    head is the last word of the manager.  The unlink needs no "is this
 *    node linked" guard because sub_02030F50 left an unlinked node pointing
 *    at the sentinel from both sides.
 *
 * Callers: src/matched/sub_020310C4.c (the runner, for a node that asked to
 * die inside its own callback) and, in the port, stubs/reimpl_02031194.c and
 * stubs/reimpl_0203115C.c.
 */

#include "stubs.h"
#include "../sdk/tcbl.h"

extern void func_020307B0(void *block);
extern void sub_02030F34(void *list, void *node);

void sub_02030F70(void *node)
{
    EidosTcblNode *n = (EidosTcblNode *)node;

    n->func = NULL;
    if (n->work != 0u) {
        /* The original does not clear the field afterwards: every path that
         * hands the node out again writes +0x10 first. */
        func_020307B0((void *)(uintptr_t)n->work);
    }

    n->prev->next = n->next;
    n->next->prev = n->prev;

    sub_02030F34(n->owner, n);
}
