/*
 * libProductId_rebuilt.c
 *
 * BEHAVIORAL reimplementation (native ARM64) of the leaf C++ class methods of
 * the proprietary i386 libProductId.so — the product / control-mark identity
 * predicates. Genuine C++ members (mangled symbols), so outside the
 * byte-identical set; proven OBSERVABLY EQUIVALENT against the real i386 .so
 * under qemu-i386.
 *
 * The control-mark predicates read a file-local "info" word (the
 * (anonymous namespace)::info in the original) which `SetControlMarkForTest`
 * writes — so the harness drives every product code 0..N and checks all
 * predicates. The handful of config-sourced globals (NC state, export /
 * prog-station / virtual-machine flags) are normally filled by the coupled
 * ProductId::Update()/Init() from the HeROS config bus; with the static ctors
 * neutered they stay at their load-time value (0), which both sides reproduce.
 *
 * Source of truth: work/re/out/libProductId.so.decomp.c (Ghidra).
 *
 * Register-leak note: several predicates compile to CONCAT31(<junk>, <bool>) on
 * i386 (e.g. `mov eax,0x13f00; sete al`), leaving deterministic-but-irrelevant
 * bytes in the upper 24 bits of eax. These are C++ `bool` returns, so only al
 * is defined; the harness reads them as _Bool. We return the clean boolean.
 */
#include <stdint.h>

/* ---- control mark, driven by SetControlMarkForTest (the (anon)::info word) ---- */
static int s_info = 0;

/* ---- config-sourced globals (left at load-time 0; ctors neutered) ---- */
static unsigned char s_ncExport      = 0;   /* DAT_14088 byte 0 */
static unsigned char s_ncProgStation = 0;   /* DAT_14088 byte 1 */
static unsigned char s_ncVirtual     = 0;   /* DAT_1408a        */
static int           s_ncState       = 0;   /* DAT_1408c        */

void ProductId_SetControlMarkForTest(int v) __asm__("_ZN9ProductId21SetControlMarkForTestEi");
void ProductId_SetControlMarkForTest(int v) { s_info = v; }

int ProductId_GetControlMark(void) __asm__("_ZN9ProductId14GetControlMarkEv");
int ProductId_GetControlMark(void) { return s_info; }

int ProductId_GetNcState(void) __asm__("_ZN9ProductId10GetNcStateEv");
int ProductId_GetNcState(void) { return s_ncState; }

/* IsTurningProduct: (unsigned)(info - 0xb) < 2   -> info in {0xb,0xc} */
int ProductId_IsTurningProduct(void) __asm__("_ZN9ProductId16IsTurningProductEv");
int ProductId_IsTurningProduct(void) { return (unsigned)(s_info - 0xb) < 2u; }

/* IsCncPilotProduct: info == 0xc */
int ProductId_IsCncPilotProduct(void) __asm__("_ZN9ProductId17IsCncPilotProductEv");
int ProductId_IsCncPilotProduct(void) { return s_info == 0xc; }

/* IsManualPlusProduct: info == 0xb */
int ProductId_IsManualPlusProduct(void) __asm__("_ZN9ProductId19IsManualPlusProductEv");
int ProductId_IsManualPlusProduct(void) { return s_info == 0xb; }

/* IsMillTurnProduct: info == 0x10 || info == 0x16 */
int ProductId_IsMillTurnProduct(void) __asm__("_ZN9ProductId17IsMillTurnProductEv");
int ProductId_IsMillTurnProduct(void) { return s_info == 0x10 || s_info == 0x16; }

/* IsAnalogProduct: (unsigned)(info - 6) < 3   -> info in {6,7,8} */
int ProductId_IsAnalogProduct(void) __asm__("_ZN9ProductId15IsAnalogProductEv");
int ProductId_IsAnalogProduct(void) { return (unsigned)(s_info - 6) < 3u; }

/* IsTNC7Generation: 0 for the legacy set, else 1 */
int ProductId_IsTNC7Generation(void) __asm__("_ZN9ProductId16IsTNC7GenerationEv");
int ProductId_IsTNC7Generation(void)
{
    switch (s_info) {
    case 0: case 6: case 7: case 8: case 0xb: case 0xc:
    case 0xe: case 0x10: case 0x14: case 0x1a: case 0x1b:
        return 0;
    default:
        return 1;
    }
}

/* IsExportVersion: low byte of DAT_14088 */
int ProductId_IsExportVersion(void) __asm__("_ZN9ProductId15IsExportVersionEv");
int ProductId_IsExportVersion(void) { return s_ncExport; }

/* IsProgStationVersion: byte 1 of DAT_14088 */
int ProductId_IsProgStationVersion(void) __asm__("_ZN9ProductId20IsProgStationVersionEv");
int ProductId_IsProgStationVersion(void) { return s_ncProgStation; }

/* IsVirtualMachine: DAT_1408a */
int ProductId_IsVirtualMachine(void) __asm__("_ZN9ProductId16IsVirtualMachineEv");
int ProductId_IsVirtualMachine(void) { return s_ncVirtual; }

/* IsPhysicalMachine: IsVirtualMachine() ^ 1 */
int ProductId_IsPhysicalMachine(void) __asm__("_ZN9ProductId17IsPhysicalMachineEv");
int ProductId_IsPhysicalMachine(void) { return (unsigned)ProductId_IsVirtualMachine() ^ 1u; }
