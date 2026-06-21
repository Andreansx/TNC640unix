/* libEp90_Geometri — coordinate-type classifier family (5 fns), native recompile.
 * Decompiled with IDA 9.2 off genuine i386 libEp90_Geometri.so. These are flat
 * dword-array readers (no stored pointers): geotec* is indexed as u32[]; a mask
 * arg selects a coordinate field (idx 54/55/60/61), gated by idx 22/23, then the
 * masked (&0x126) value is compared to a per-classifier constant. Pure leaf.
 * Extends recomp/geometri (had IsPolareLaenge/IsCartInkrement/IsPolarerWinkel);
 * new: IsPolaresLaengenInkrement (6), IsPolaresWinkelInkrement (34). */
typedef unsigned int u32;

static int classify(u32 a1, const u32 *a2, int K){
  int v2 = 0;
  if ( (a1 & a2[23]) == 0 && (a1 & a2[22]) != 0 ){
    int v3;
    if ( a1 == 0x200000 )      v3 = a2[60] & 0x126;
    else if ( a1 > 0x200000 ){ if ( a1 != 0x400000 ) return v2; v3 = a2[61] & 0x126; }
    else if ( a1 == 0x4000 )   v3 = a2[54] & 0x126;
    else if ( a1 == 0x8000 )   v3 = a2[55] & 0x126;
    else return v2;
    v2 = (v3 == K);
  }
  return v2;
}
int _Z15IsCartInkrementmP6geotec(u32 a1, const u32 *a2){ return classify(a1,a2,2); }
int _Z14IsPolareLaengemP6geotec(u32 a1, const u32 *a2){ return classify(a1,a2,4); }
int _Z25IsPolaresLaengenInkrementmP6geotec(u32 a1, const u32 *a2){ return classify(a1,a2,6); }
int _Z15IsPolarerWinkelmP6geotec(u32 a1, const u32 *a2){ return classify(a1,a2,32); }
int _Z24IsPolaresWinkelInkrementmP6geotec(u32 a1, const u32 *a2){ return classify(a1,a2,34); }
