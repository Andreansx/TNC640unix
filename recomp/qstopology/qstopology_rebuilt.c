/* libQsSubsystemTopologyLibrary — 6 PIMPL getter leaves, native recompile.
 * Decompiled with IDA 9.2 off genuine i386 libQsSubsystemTopologyLibrary.so.
 * Each reads this->d->field (single-pointer chase): d is the private-impl
 * pointer at this+12 (i386). Per-arch named-field struct reproduces the i386
 * 4-byte-ptr layout under -m32; field reads off d are flat. C++ mangled. */
typedef struct { char pad[12]; void *d; } Obj;   /* d @12 (i386) / @16 (x86_64) */
#define DI(t,o) (*(int*)((char*)((Obj*)(t))->d + (o)))
#define DB(t,o) (*(unsigned char*)((char*)((Obj*)(t))->d + (o)))

int   _ZNK8topology4Axis10physAxisIdEv(void *t){ return DI(t,16); }
int   _ZNK8topology4Axis10progAxisIdEv(void *t){ return DI(t,24); }
int   _ZNK8topology4Axis13kinematicRoleEv(void *t){ return DI(t,32); }
int   _ZNK8topology4Axis10motionTypeEv(void *t){ return DI(t,36); }
_Bool _ZNK8topology4Axis11isAuxiliaryEv(void *t){ return DB(t,48); }
int   _ZNK8topology7Channel2idEv(void *t){ return DI(t,28); }
