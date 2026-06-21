/*
 * libEp90_Errplib_partial_rebuilt.c — native re-implementation of the
 * PURE-INTEGER leaf subset of the proprietary i386 libEp90_Errplib.so
 * (HEIDENHAIN EP90 error-handling library), recovered via Ghidra
 * (work/re/out/libEp90_Errplib.so.decomp.c) + i386 disassembly.
 *
 * SCOPE: most of libEp90_Errplib is NOT a leaf — it builds error strings,
 * touches shared memory (libSharedMemLib), and calls the HeROS message bus
 * (BA_TM_LinkShared, libGMessage*). Only its pure error-code classifiers,
 * the facility-ID table lookup, and a constant predicate are byte-identical-
 * verifiable; those are reproduced here from the exact instruction stream.
 *
 * Calling convention: standard cdecl (args read from [esp+4], [esp+8]) —
 * confirmed from disassembly, NOT regparm.
 *
 * Two classes of return:
 *   - bool predicates: i386 does `sete/setne al` WITHOUT presetting eax, so
 *     the upper 3 bytes of the return register are caller-garbage. Their
 *     contract is therefore `bool` (low byte only) — verified as bool.
 *   - ERR_IsWarning / ERR_IsError: i386 loads eax itself
 *     (`mov eax,[esp+4]; sub; cmp; setbe al`), so the upper 3 bytes are a
 *     DETERMINISTIC leak of (param-N) & 0xFFFFFF00 — reproduced exactly and
 *     verified as a full 32-bit unsigned (cf. libEp90_Bohrcyc BCYC_Angetr_Werkz).
 */
#include <stdint.h>
#include <stdbool.h>

/* ---- warning classifiers (error-class code in param_1) ----
 * class codes: 1 = user warning, 2 = system warning,
 *              3 = user error,   4 = system error.
 * The intern/extern variants additionally key on a 0/non-0 byte.
 */

/* cmp [esp+4],2 ; sete al */
bool ERR_IsSystemWarning(int cls) { return cls == 2; }

/* xor eax,eax ; cmp [esp+4],1 ; jne ret ; cmp byte [esp+8],0 ; setne al */
bool ERR_IsInternWarning(int cls, int flag) { return cls == 1 && (flag & 0xff) != 0; }

/* xor eax,eax ; cmp [esp+4],1 ; jne ret ; cmp byte [esp+8],0 ; sete al */
bool ERR_IsExternWarning(int cls, int flag) { return cls == 1 && (flag & 0xff) == 0; }

/* cmp [esp+4],1 ; sete al */
bool ERR_IsUserWarning(int cls) { return cls == 1; }

/* mov eax,[esp+4] ; sub eax,1 ; cmp eax,1 ; setbe al
 * -> full 32-bit eax = ((cls-1) & 0xFFFFFF00) | ((unsigned)(cls-1) <= 1) */
unsigned ERR_IsWarning(int cls)
{
    uint32_t e = (uint32_t)cls - 1u;
    return (e & 0xFFFFFF00u) | ((e <= 1u) ? 1u : 0u);
}

/* ---- error classifiers ---- */

/* cmp [esp+4],4 ; sete al */
bool ERR_IsSystemError(int cls) { return cls == 4; }

/* xor eax,eax ; cmp [esp+4],3 ; jne ret ; cmp byte [esp+8],0 ; setne al */
bool ERR_IsInternError(int cls, int flag) { return cls == 3 && (flag & 0xff) != 0; }

/* xor eax,eax ; cmp [esp+4],3 ; jne ret ; cmp byte [esp+8],0 ; sete al */
bool ERR_IsExternError(int cls, int flag) { return cls == 3 && (flag & 0xff) == 0; }

/* cmp [esp+4],3 ; sete al */
bool ERR_IsUserError(int cls) { return cls == 3; }

/* mov eax,[esp+4] ; sub eax,3 ; cmp eax,1 ; setbe al
 * -> full 32-bit eax = ((cls-3) & 0xFFFFFF00) | ((unsigned)(cls-3) <= 1) */
unsigned ERR_IsError(int cls)
{
    uint32_t e = (uint32_t)cls - 3u;
    return (e & 0xFFFFFF00u) | ((e <= 1u) ? 1u : 0u);
}

/* ---- facility-ID table lookup ----
 * mov eax,[esp+4] ; xor edx,edx ; sub eax,4 ; cmp eax,0x47 ; ja ret(0)
 * mov edx,[table + eax*4] ; mov eax,edx
 * 72-entry (.rodata @ vaddr 0x3AC0) table lifted verbatim from the real .so.
 * Maps a base error number (errnum-4, range 0..0x47) to a 0x06XX0000 facility id.
 */
static const uint32_t errplib_facility_table[0x48] = {
    0x060c0000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u,
    0x00000000u, 0x00000000u, 0x00000000u, 0x06050000u, 0x00000000u, 0x06520000u,
    0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u,
    0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u,
    0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u,
    0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u,
    0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u,
    0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x06500000u, 0x06000000u,
    0x060a0000u, 0x06020000u, 0x060b0000u, 0x06010000u, 0x06070000u, 0x06080000u,
    0x060f0000u, 0x06100000u, 0x06060000u, 0x06030000u, 0x06090000u, 0x06040000u,
    0x06210000u, 0x06150000u, 0x00000000u, 0x06550000u, 0x06560000u, 0x060e0000u,
    0x00000000u, 0x06060000u, 0x06060000u, 0x06060000u, 0x060d0000u, 0x06120000u,
};

unsigned ERRPLIB_GetFacilityID(int errnum)
{
    uint32_t idx = (uint32_t)errnum - 4u;
    if (idx > 0x47u) return 0u;
    return errplib_facility_table[idx];
}

/* xor eax,eax ; ret  — programming station is never the DataPilot demo */
bool IsDPDemo(void) { return false; }
