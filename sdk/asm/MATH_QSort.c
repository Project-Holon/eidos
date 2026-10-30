/*
 * sdk/asm/MATH_QSort.c -- MATH_QSort at 0x02081BE4.
 *
 * Replaces the asm MATH_QSort (src/game/arm9/qsort.c; TwlSDK libmath
 * qsort.o).  The same non-recursive quicksort, step for step, so elements
 * that compare equal end up in the same order as on the DS: an explicit
 * stack of (first, last) element pointers; a range of two elements is
 * compared and swapped; a longer one moves its middle element (picked as
 * the asm picks it, the byte span shifted right by the bit length of the
 * width, times the width) to the front as the pivot, partitions, puts the
 * pivot in place, and pushes the larger side first so the smaller one is
 * done next.  The bag's sort (ov070 itemmenu.c) uses it.
 */

#include "asmsdk.h"

typedef s32 (*MathQSortCompare)(void *a, void *b);

void MATH_QSort(void *head, u32 num, u32 width, MathQSortCompare comp, void *stackBuf);

/* the bit length of v: 32 - clz(v) */
static u32 bit_length(u32 v)
{
    u32 n = 0;

    while (v != 0) {
        n++;
        v >>= 1;
    }
    return n;
}

static void swap_elems(u8 *a, u8 *b, u32 width)
{
    u32 k;

    for (k = 0; k < width; k++) {
        u8 t = a[k];

        a[k] = b[k];
        b[k] = t;
    }
}

void MATH_QSort(void *head, u32 num, u32 width, MathQSortCompare comp, void *stackBuf)
{
    u8 *local[2 * 33];
    u8 **base, **top;
    u32 shift;

    (void)stackBuf;     /* the host's own stack is deep enough: 2 per bit of num */
    if ((s32)num <= 1) {
        return;
    }
    base = top = local;
    *top++ = (u8 *)head;
    *top++ = (u8 *)head + (num - 1) * width;
    shift = bit_length(width);
    while (top != base) {
        u8 *hi = top[-1], *lo = top[-2], *i, *j;

        top -= 2;
        if ((u32)(hi - lo) == width) {
            if (comp(lo, hi) > 0) {
                swap_elems(lo, hi, width);
            }
            continue;
        }
        swap_elems(lo + (((u32)(hi - lo) >> shift) * width), lo, width);
        i = lo + width;
        j = hi;
        for (;;) {
            while (i < hi && comp(i, lo) < 0) {
                i += width;
            }
            while (comp(j, lo) > 0) {
                j -= width;
            }
            if (i >= j) {
                break;
            }
            swap_elems(i, j, width);
            i += width;
            j -= width;
            if (i > j) {
                break;
            }
        }
        swap_elems(lo, j, width);
        if (j - lo > hi - j) {
            if (lo < j - width) {
                *top++ = lo;
                *top++ = j - width;
            }
            if (j + width < hi) {
                *top++ = j + width;
                *top++ = hi;
            }
        } else {
            if (j + width < hi) {
                *top++ = j + width;
                *top++ = hi;
            }
            if (lo < j - width) {
                *top++ = lo;
                *top++ = j - width;
            }
        }
    }
}
