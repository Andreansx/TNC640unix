/*
 * libEp90_Drehcyc_rebuilt.c
 *
 * BEHAVIORAL reimplementation (native ARM64) of a leaf function of the
 * proprietary i386 libEp90_Drehcyc.so (turning-cycle geometry):
 *   - is_aufmass_aktiv : is a machining "allowance" (Aufmass) active?
 *
 * NOTE on the rest of the library: libEp90_Drehcyc is a function-pointer-table
 * architecture — most exported symbols are runtime-registered FORWARDER thunks
 * (`jmp *GOT[...]`), whose real bodies are supplied by the host at load time and
 * therefore can't be reimplemented standalone. is_aufmass_aktiv is one of the
 * genuine self-contained leaves (real x87 body, no indirect call).
 *
 * `aufmass_rt` is passed BY VALUE. The disassembly (0xbcb0) shows the field
 * offsets within the by-value struct: flag @+0, m1 @+4, m2 @+0xc (i386 4-byte
 * double alignment), with the explicit tol argument following. The struct is
 * declared so each arch's by-value ABI carries identical logical content.
 *
 * Source of truth: work/re/out/libEp90_Drehcyc.so.decomp.c (Ghidra) + disasm.
 */
#include <math.h>
#include "drehcyc_layout.h"

/* is_aufmass_aktiv(am, tol):
 *   (am.flag & 0x30) == 0  -> false
 *   |am.m1| < tol          -> (tol <= |am.m2|)
 *   else                   -> true                                     */
int is_aufmass_aktiv(aufmass_rt am, double tol) __asm__("_Z16is_aufmass_aktiv10aufmass_rtd");
int is_aufmass_aktiv(aufmass_rt am, double tol)
{
    if ((am.flag & 0x30) == 0) return 0;
    if (fabs(am.m1) < tol) return tol <= fabs(am.m2);
    return 1;
}
