/*
 * verify_productid.c — one harness, compiled two ways (i386 vs the real .so;
 * arm64 vs the reimplementation). Drives every control-mark value through all
 * predicates. Bool predicates are read as _Bool (only al is ABI-defined; the
 * i386 CONCAT31 codegen leaves irrelevant bytes in the upper eax). Output is
 * fully deterministic, so a plain diff is the proof.
 */
#include <stdio.h>

extern void  SetControlMarkForTest(int) __asm__("_ZN9ProductId21SetControlMarkForTestEi");
extern int   GetControlMark(void)       __asm__("_ZN9ProductId14GetControlMarkEv");
extern int   GetNcState(void)           __asm__("_ZN9ProductId10GetNcStateEv");
extern _Bool IsTurningProduct(void)     __asm__("_ZN9ProductId16IsTurningProductEv");
extern _Bool IsCncPilotProduct(void)    __asm__("_ZN9ProductId17IsCncPilotProductEv");
extern _Bool IsManualPlusProduct(void)  __asm__("_ZN9ProductId19IsManualPlusProductEv");
extern _Bool IsMillTurnProduct(void)    __asm__("_ZN9ProductId17IsMillTurnProductEv");
extern _Bool IsAnalogProduct(void)      __asm__("_ZN9ProductId15IsAnalogProductEv");
extern int   IsTNC7Generation(void)     __asm__("_ZN9ProductId16IsTNC7GenerationEv");
extern int   IsExportVersion(void)      __asm__("_ZN9ProductId15IsExportVersionEv");
extern int   IsProgStationVersion(void) __asm__("_ZN9ProductId20IsProgStationVersionEv");
extern int   IsVirtualMachine(void)     __asm__("_ZN9ProductId16IsVirtualMachineEv");
extern _Bool IsPhysicalMachine(void)    __asm__("_ZN9ProductId17IsPhysicalMachineEv");

int main(void)
{
    /* drive the control mark across the full meaningful range and some edges */
    for (int v = -2; v <= 0x40; v++) {
        SetControlMarkForTest(v);
        printf("info=%d cm=%d turn=%d cnc=%d mp=%d mt=%d an=%d tnc7=%d\n",
               v, GetControlMark(),
               IsTurningProduct(), IsCncPilotProduct(), IsManualPlusProduct(),
               IsMillTurnProduct(), IsAnalogProduct(), IsTNC7Generation());
    }
    /* config-sourced predicates: only the load-time default state is reachable
     * without the coupled Update(); both sides reproduce it. */
    printf("ncstate=%d export=%d progstation=%d virtual=%d physical=%d\n",
           GetNcState(), IsExportVersion(), IsProgStationVersion(),
           IsVirtualMachine(), IsPhysicalMachine());
    return 0;
}
