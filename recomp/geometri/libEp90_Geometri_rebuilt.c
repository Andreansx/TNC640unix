/*
 * libEp90_Geometri_rebuilt.c
 *
 * BEHAVIORAL reimplementation (native ARM64) of the coordinate-type classifier
 * leaves of the proprietary i386 libEp90_Geometri.so: IsPolareLaenge,
 * IsCartInkrement, IsPolarerWinkel. Each takes a coordinate mask and a geotec
 * element, reads flag fields by FLAT offset (no pointer indirection, so a shared
 * byte buffer drives both arches), and classifies the coordinate's encoding.
 * The three are identical apart from the final compare value (4 / 2 / 0x20).
 *
 * Source of truth: work/re/out/libEp90_Geometri.so.decomp.c (Ghidra).
 */
#include <string.h>
#include <stdint.h>

static uint32_t GU(const void *p, int off){ uint32_t v; memcpy(&v, (const unsigned char*)p+off, 4); return v; }

/* common gate + field selection; returns the masked field, or 0xFFFFFFFF if the
 * element doesn't carry this coordinate (so no compare value can match it). */
static uint32_t geo_coord_field(uint32_t mask, const void *g)
{
    if (((GU(g, 0x5c) & mask) != 0) || ((GU(g, 0x58) & mask) == 0))
        return 0xFFFFFFFFu;
    if (mask == 0x200000) return GU(g, 0xf0) & 0x126;
    if (mask < 0x200001) {
        if (mask == 0x4000) return GU(g, 0xd8) & 0x126;
        if (mask == 0x8000) return GU(g, 0xdc) & 0x126;
        return 0xFFFFFFFFu;
    }
    if (mask == 0x400000) return GU(g, 0xf4) & 0x126;
    return 0xFFFFFFFFu;
}

/* IsPolareLaenge(mask, g) — is this coordinate a polar length? (field & 0x126 == 4) */
int IsPolareLaenge(unsigned long mask, const void *g) __asm__("_Z14IsPolareLaengemP6geotec");
int IsPolareLaenge(unsigned long mask, const void *g)
{
    return geo_coord_field((uint32_t)mask, g) == 4;
}

/* IsCartInkrement(mask, g) — is this coordinate a cartesian increment? (== 2) */
int IsCartInkrement(unsigned long mask, const void *g) __asm__("_Z15IsCartInkrementmP6geotec");
int IsCartInkrement(unsigned long mask, const void *g)
{
    return geo_coord_field((uint32_t)mask, g) == 2;
}

/* IsPolarerWinkel(mask, g) — is this coordinate a polar angle? (== 0x20) */
int IsPolarerWinkel(unsigned long mask, const void *g) __asm__("_Z15IsPolarerWinkelmP6geotec");
int IsPolarerWinkel(unsigned long mask, const void *g)
{
    return geo_coord_field((uint32_t)mask, g) == 0x20;
}
