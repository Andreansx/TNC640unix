/* libQsPlcController — 4 flat getter leaves, native recompile.
 * Decompiled with IDA 9.2 off genuine i386 libQsPlcController.so. PlcServerReplyObject
 * state/operationHandle/value + PlcController connectionHandle: inline dword reads. */
typedef unsigned int u32;
#define U(p,o) (*(u32*)((char*)(p)+(o)))
int _ZNK3qic20PlcServerReplyObject5stateEv(void *t){ return U(t,16); }
int _ZNK3qic20PlcServerReplyObject15operationHandleEv(void *t){ return U(t,28); }
int _ZNK3qic20PlcServerReplyObject5valueEv(void *t){ return U(t,44); }
int _ZNK3qic13PlcController16connectionHandleEv(void *t){ return U(t,56); }
