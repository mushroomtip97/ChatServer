/*
 * Compatibility shim for libsodium prebuilts built against newer MinGW-w64 CRTs.
 * MinGW 13.1.0 (Qt 6.11.1's toolchain) lacks memset_explicit(); libsodium's
 * bundled utils.o references it. A plain memset + compiler barrier is
 * sufficient — this matches the mingw-w64 upstream implementation.
 */
#if defined(__MINGW32__) || defined(__MINGW64__)

#include <string.h>

void *memset_explicit(void *dest, int ch, size_t count)
{
    // NOLINTNEXTLINE(bugprone-unsafe-functions)
    memset(dest, ch, count);
    __asm__ __volatile__("" : : "r"(dest) : "memory");
    return dest;
}

#endif