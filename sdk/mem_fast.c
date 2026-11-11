/*
 * sdk/mem_fast.c -- memcpy and memcmp for the whole program.
 *
 * The game brings its own C library, and its memcpy and memcmp have the
 * standard names (aletheia's msl_string_mem.c): a loop that moves one byte
 * at a time, and one that compares one byte at a time.  Linked as they
 * are, they are the program's memcpy and memcmp, the port's and the
 * engine's too: every screen copy, every VRAM gather, every "did it
 * change" compare went a byte at a time (in the field about half of the
 * render thread's time).
 *
 * These two take their place: the boot's link step cuts a game function
 * the port defines (tools/bootlink.py, boot_cuts.txt), so the game's two are
 * not compiled and its own callers reach these.  They do exactly what the
 * game's do, for every input, so the game sees no difference:
 *
 *   memcpy   The game's loop copies upward from the first byte.  When the
 *            destination does not start inside the source that is a
 *            memmove, and the host's memmove does it.  When it does (an
 *            overlapping copy, which the C standard leaves undefined) the
 *            loop repeats the source's first bytes; that case is the same
 *            loop here.
 *   memcmp   0 when equal, else -1 or 1 by the first differing bytes as
 *            unsigned values: the game's results, found eight bytes at a
 *            time.
 *
 * Compiled with -fno-builtin (CMakeLists.txt) so the compiler keeps the
 * loops and the memmove call as written.
 */

#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* Eight bytes read at any address, whatever the bytes were stored as. */
#if defined(_MSC_VER) && !defined(__clang__)
/* Microsoft's compiler (development builds only): the two names are its
 * intrinsics, defined here all the same, and its own spelling of an
 * unaligned read. */
#pragma function(memcpy, memcmp)
typedef unsigned __int64 __unaligned Word;
#else
typedef uint64_t __attribute__((aligned(1), may_alias)) Word;
#endif

void *memcpy(void *dest, const void *src, size_t n)
{
    if ((uintptr_t)dest - (uintptr_t)src < (uintptr_t)n && dest != src) {
        /* the destination starts inside the source: the game's loop, byte
         * by byte upward */
        unsigned char *d = (unsigned char *)dest;
        const unsigned char *s = (const unsigned char *)src;

        while (n-- != 0) {
            *d++ = *s++;
        }
        return dest;
    }
    return memmove(dest, src, n);
}

int memcmp(const void *buf1, const void *buf2, size_t n)
{
    const unsigned char *a = (const unsigned char *)buf1;
    const unsigned char *b = (const unsigned char *)buf2;

    /* the equal head, eight bytes at a time (four of them a turn while
     * there is room) */
    while (n >= 32) {
        const Word *x = (const Word *)a, *y = (const Word *)b;

        if (((x[0] ^ y[0]) | (x[1] ^ y[1]) | (x[2] ^ y[2]) | (x[3] ^ y[3])) != 0) {
            break;
        }
        a += 32;
        b += 32;
        n -= 32;
    }
    while (n >= 8) {
        if (*(const Word *)a != *(const Word *)b) {
            break;
        }
        a += 8;
        b += 8;
        n -= 8;
    }
    while (n != 0) {
        if (*a != *b) {
            return *a < *b ? -1 : 1;
        }
        a++;
        b++;
        n--;
    }
    return 0;
}
