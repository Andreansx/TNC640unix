/*
 * libEp90_Gtlib_partial_rebuilt.c — native re-implementation of the
 * PURE-INTEGER single-level leaf classifiers of the proprietary i386
 * libEp90_Gtlib.so (EP90 geometry/Geotec table library), recovered via Ghidra
 * (work/re/out/libEp90_Gtlib.so.decomp.c) + canonical i386 disassembly.
 *
 * SCOPE: most of libEp90_Gtlib walks geotec/akopf linked lists (multi-level
 * pointer chasing — NOT byte-identical-verifiable across 32/64-bit) or does FP
 * geometry. This file reproduces the GTFIND_Is* feature classifiers that read a
 * SINGLE struct field (the type tag at byte offset 0x54 of a geotec node) — pure
 * integer, single dereference, exactly verifiable with a flat node buffer.
 *
 * These are C++ symbols (typed pointer params), so each carries its exact mangled
 * name via an __asm__ label. cdecl, confirmed from disassembly.
 *
 * Two of them leak the i386 return register the way ERR_IsWarning does:
 *   IsVariante      loads eax<-param2 then `sete al`  -> (param2 & ~0xff)|cmp
 *   IsFigurRucksack computes eax<-(1<<tag) then `setne al` -> ((1<<tag)&~0xff)|hit
 * so they are verified as a full 32-bit unsigned. The rest pre-zero eax (xor) and
 * are clean bools.
 */
#include <stdint.h>
#include <stdbool.h>

#define GEOTEC_TAG(p) (*(const int32_t *)((const char *)(p) + 0x54))

/* xor eax,eax ; test ; cmp [p+0x54],CONST ; sete al  (upper bytes zeroed -> clean bool) */
bool gtf_isbohrung(const void *p)  __asm__("_Z16GTFIND_IsBohrungP6geotec");
bool gtf_isfasrun(const void *p)   __asm__("_Z15GTFIND_IsFasRunP6geotec");
bool gtf_isfreistich(const void *p)__asm__("_Z18GTFIND_IsFreistichP6geotec");
bool gtf_iseinstich(const void *p) __asm__("_Z17GTFIND_IsEinstichP6geotec");
bool gtf_isgewinde(const void *p)  __asm__("_Z16GTFIND_IsGewindeP6geotec");

bool gtf_isbohrung(const void *p)  { return p != 0 && GEOTEC_TAG(p) == 0x15; }
bool gtf_isfasrun(const void *p)   { return p != 0 && GEOTEC_TAG(p) == 8; }
bool gtf_isfreistich(const void *p){ return p != 0 && GEOTEC_TAG(p) == 10; }
bool gtf_iseinstich(const void *p) { return p != 0 && GEOTEC_TAG(p) == 9; }
bool gtf_isgewinde(const void *p)  { return p != 0 && GEOTEC_TAG(p) == 4; }

/* mov eax,param2 ; cmp [p+0x54],eax ; sete al  -> full eax leaks param2's top bytes */
unsigned gtf_isvariante(const void *p, int v) __asm__("_Z17GTFIND_IsVarianteP6geotec9basvar_at");
unsigned gtf_isvariante(const void *p, int v)
{
    if (p == 0) return 0u;                       /* je after xor eax,eax */
    uint32_t e = (uint32_t)v;
    return (e & 0xFFFFFF00u) | ((GEOTEC_TAG(p) == v) ? 1u : 0u);
}

/* tag<=0x13 ? eax=1<<tag, setne al on (eax & 0xd2800) : 0
 * -> full eax = ((1<<tag) & ~0xff) | ((1<<tag) & 0xd2800 != 0) */
unsigned gtf_isfigurrucksack(const void *p) __asm__("_Z22GTFIND_IsFigurRucksackP6geotec");
unsigned gtf_isfigurrucksack(const void *p)
{
    if (p == 0) return 0u;
    uint32_t tag = (uint32_t)GEOTEC_TAG(p);
    if (tag > 0x13u) return 0u;
    uint32_t u = 1u << tag;
    return (u & 0xFFFFFF00u) | ((u & 0xd2800u) ? 1u : 0u);
}

/* ---- plane-type (plan_at) classifiers: the arg IS the type code (no struct) ---- */

/* p in {4,5} (p<6 && p>3) or {0xd,0xe} ((p-0xd)<2). `seta al` -> verify as bool. */
bool gtf_isyebene(unsigned p) __asm__("_Z15GTFIND_IsYEbene7plan_at");
bool gtf_isyebene(unsigned p)
{
    if (p < 6u) return p > 3u;
    return (p - 0xdu) < 2u;
}

/* p<=0xd ? eax=1<<p, setne al on (eax & 0x3028) : 0  -> full 32-bit (1<<p leak) */
unsigned gtf_ismantel(unsigned p) __asm__("_Z15GTFIND_IsMantel7plan_at");
unsigned gtf_ismantel(unsigned p)
{
    if (p > 0xdu) return 0u;
    uint32_t u = 1u << p;
    return (u & 0xFFFFFF00u) | ((u & 0x3028u) ? 1u : 0u);
}
