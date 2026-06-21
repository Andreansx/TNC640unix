/* libQsHerosFrameworkLibrary — 7 PIMPL getter leaves, native recompile.
 * Decompiled with IDA 9.2 off genuine i386 libQsHerosFrameworkLibrary.so.
 * d-pointer offset differs per class (HerosFramework @0, HerosQueue @8,
 * RequestAdministration @12); reads via *(void**)(this+off) are per-arch-native.
 * HerosQueue::eventMask is a 2-level chase (d->slot40->+8). C++ mangled. */
typedef unsigned char u8;
#define PTR(t,o) (*(void**)((char*)(t)+(o)))
#define I(p,o)   (*(int*)((char*)(p)+(o)))
#define B(p,o)   (*(u8*)((char*)(p)+(o)))

int   _ZNK14HerosFramework9eventMaskEv(void *t){ return I(PTR(t,0),0); }
_Bool _ZNK14HerosFramework18isUnreliableClientEv(void *t){ return B(PTR(t,0),38); }
int   _ZNK10HerosQueue2idEv(void *t){ return I(PTR(t,8),4); }
_Bool _ZNK10HerosQueue5validEv(void *t){ return I(PTR(t,8),4) != -1; }
_Bool _ZNK10HerosQueue11isSuspendedEv(void *t){ return B(PTR(t,8),56); }
int   _ZNK10HerosQueue9eventMaskEv(void *t){ return I(PTR(PTR(t,8),40),8); }
int   _ZN21RequestAdministration12requestCountEv(void *t){ void *d=PTR(t,12); return I(d,12) - I(d,8); }
