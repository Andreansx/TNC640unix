/*
 * verify_geometri.c — one harness, compiled two ways (i386 vs the real .so under
 * qemu; arm64 vs the reimplementation). Builds a flat geotec element and drives
 * the three coordinate-type classifiers across masks, gate states, and field
 * encodings. Predicates return bool -> read as _Bool (only al is ABI-defined).
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>

extern _Bool IsPolareLaenge(unsigned long, const void*)  __asm__("_Z14IsPolareLaengemP6geotec");
extern _Bool IsCartInkrement(unsigned long, const void*) __asm__("_Z15IsCartInkrementmP6geotec");
extern _Bool IsPolarerWinkel(unsigned long, const void*) __asm__("_Z15IsPolarerWinkelmP6geotec");

static int II = 0;
static void I(int v){ printf("I %d %d\n", II++, v); }

int main(void)
{
    static const unsigned long masks[] = { 0x4000, 0x8000, 0x200000, 0x400000, 0x1000, 0x10000 };
    /* field encodings whose &0x126 hit each classifier value and misses */
    static const uint32_t fld[] = { 0, 2, 4, 0x20, 0x100, 6, 0x24, 0x126, 0xffffffffu, 0x22 };
    unsigned char g[0x100];

    for (unsigned m = 0; m < sizeof(masks)/sizeof(masks[0]); m++) {
        uint32_t mk = (uint32_t)masks[m];
        for (int gate58 = 0; gate58 <= 1; gate58++)       /* 0x58 carries the mask? */
        for (int gate5c = 0; gate5c <= 1; gate5c++)       /* 0x5c carries the mask? */
        for (unsigned f = 0; f < sizeof(fld)/sizeof(fld[0]); f++) {
            memset(g, 0, sizeof g);
            uint32_t v58 = gate58 ? mk : 0u;
            uint32_t v5c = gate5c ? mk : 0u;
            memcpy(g + 0x58, &v58, 4);
            memcpy(g + 0x5c, &v5c, 4);
            /* set every candidate coordinate field to the test encoding */
            memcpy(g + 0xd8, &fld[f], 4);
            memcpy(g + 0xdc, &fld[f], 4);
            memcpy(g + 0xf0, &fld[f], 4);
            memcpy(g + 0xf4, &fld[f], 4);
            I(IsPolareLaenge(masks[m], g));
            I(IsCartInkrement(masks[m], g));
            I(IsPolarerWinkel(masks[m], g));
        }
    }
    return 0;
}
