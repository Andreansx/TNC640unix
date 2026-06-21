/*
 * libxmlreader_partial_rebuilt.c — native re-implementation of the PURE-INTEGER
 * hash + setter leaves of the proprietary i386 libxmlreader.so, recovered via
 * i386 disassembly.
 *
 *   XmlKeyHashBinary(key, len)  - Jenkins one-at-a-time hash over `len` SIGNED
 *                                 bytes (movsx); returns 0 for len==0. cdecl.
 *   XmlHashSetKey / XmlHashSetValueAllocator - single-level field stores.
 */
#include <stdint.h>

unsigned XmlKeyHashBinary(const void *key, int len)
{
    if (len == 0) return 0;
    const signed char *p = (const signed char *)key;
    const signed char *end = p + len;
    uint32_t h = 0;
    do {
        h += (uint32_t)(int32_t)*p;     /* movsx: signed byte */
        p++;
        h += h << 10;
        h ^= h >> 6;
    } while (p != end);
    h += h << 3;                        /* lea h,[h+h*8] == h*9 */
    h ^= h >> 11;
    h += h << 15;
    return h;
}

/* *(h+0xc)=key ; *(h+0x10)=keylen */
void XmlHashSetKey(void *h, unsigned key, unsigned keylen)
{
    *(uint32_t *)((char *)h + 0x0c) = key;
    *(uint32_t *)((char *)h + 0x10) = keylen;
}

/* *(h+0x14)=alloc ; *(h+0x18)=ctx */
void XmlHashSetValueAllocator(void *h, unsigned alloc, unsigned ctx)
{
    *(uint32_t *)((char *)h + 0x14) = alloc;
    *(uint32_t *)((char *)h + 0x18) = ctx;
}
