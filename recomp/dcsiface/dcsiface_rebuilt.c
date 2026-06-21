/* libEp90_Dcsinterface — 5 DcsInterface:: leaf methods, native recompile.
 * Decompiled with IDA 9.2 off genuine i386 libEp90_Dcsinterface.so. The axis
 * accessors read a byte at a computed offset into the (flat) DcsInterface object
 * passed as `this`; raw-offset access on a flat buffer is arch-independent. The
 * Kern* openers ignore `this` and write a constant. C++ mangled symbols. */
typedef unsigned char u8;

int _ZN12DcsInterface9_cfgYAxisEv(void *th){
  return *((u8*)th + 29828);
}
int _ZN12DcsInterface16_isAxisAvailableEi(void *th, int a2){
  return *((u8*)th + a2 + 29844);
}
int _ZN12DcsInterface18_isAxisAvailableChEii(void *th, int a2, int a3){
  return *((u8*)th + 24*a2 + a3 + 30156);
}
int _ZN12DcsInterface11KernOpenSpmEPi(void *th, int *a2){ (void)th; *a2 = 200; return 1; }
int _ZN12DcsInterface11KernOpenWkzEPi(void *th, int *a2){ (void)th; *a2 = 200; return 1; }
