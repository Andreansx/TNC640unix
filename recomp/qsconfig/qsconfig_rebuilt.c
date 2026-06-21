/* libQsConfigController — 8 getter/predicate leaves, native recompile.
 * Decompiled with IDA 9.2 off genuine i386 libQsConfigController.so. Mix of flat-this
 * byte/dword reads, two PIMPL chases (size: d@20->+4, hasDataError: d@60->+45), and a
 * pure predicate (isSuccessResult). C++ mangled. */
typedef unsigned int u32;
#define U(p,o)   (*(u32*)((char*)(p)+(o)))
#define B(p,o)   (*(unsigned char*)((char*)(p)+(o)))
#define PTR(p,o) (*(void**)((char*)(p)+(o)))

int _ZNK3qic25ConfigControllerOperation9isRunningEv(void *t){ return B(t,12); }
int _ZNK3qic23ConfigControllerPrivate16isInUnitTestModeEv(void *t){ return B(t,44); }
int _ZNK17ConfigTypeManager21isOperationInProgressEv(void *t){ return B(t,80); }
int _ZNK22ConfigUniqueStringList7maxSizeEv(void *t){ return U(t,28); }
int _ZNK22ConfigUniqueStringList13isInitializedEv(void *t){ return B(t,53); }
int _ZNK22ConfigUniqueStringList4sizeEv(void *t){ return U(PTR(t,20),4); }
_Bool _ZN3qic16ConfigController15isSuccessResultEi(int a1){ return a1 == 0; }
int _ZNK3qic16ConfigController12hasDataErrorEv(void *t){ return B(PTR(t,60),45); }
