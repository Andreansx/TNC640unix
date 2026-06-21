/* libQsIpoController — 7 functional-safety axis-state predicates + 1 getter.
 * Decompiled with IDA 9.2 off genuine i386 libQsIpoController.so. FsAxisStateHelper
 * packs safety flags into inline dwords @0/@4 (bit-extractions); the composite
 * predicates AND several flags. NC-safety relevant. C++ mangled. */
typedef unsigned int u32;
#define U(p,o) (*(u32*)((char*)(p)+(o)))

int _ZNK17FsAxisStateHelper16isAxisWithSafetyEv(void *t){ return U(t,0) >> 31; }
int _ZNK17FsAxisStateHelper18isAxisSgReferencedEv(void *t){ return (U(t,0) >> 28) & 1; }
int _ZNK17FsAxisStateHelper22isAxisSgPositionTestedEv(void *t){ return (U(t,0) >> 29) & 1; }
int _ZNK17FsAxisStateHelper19axisHasSgSafeAbsPosEv(void *t){ return (U(t,4) >> 27) & 1; }
int _ZNK17FsAxisStateHelper23doesAxisRequireCheckingEv(void *t){
  if ( (unsigned char)_ZNK17FsAxisStateHelper16isAxisWithSafetyEv(t)
    && (unsigned char)_ZNK17FsAxisStateHelper18isAxisSgReferencedEv(t)
    && (U(t,0) & 0x40000000) != 0
    && !(unsigned char)_ZNK17FsAxisStateHelper22isAxisSgPositionTestedEv(t) )
    return _ZNK17FsAxisStateHelper19axisHasSgSafeAbsPosEv(t);
  return 0;
}
int _ZNK17FsAxisStateHelper18hasAxisBeenCheckedEv(void *t){
  if ( (unsigned char)_ZNK17FsAxisStateHelper16isAxisWithSafetyEv(t)
    && (unsigned char)_ZNK17FsAxisStateHelper18isAxisSgReferencedEv(t)
    && (unsigned char)_ZNK17FsAxisStateHelper22isAxisSgPositionTestedEv(t) )
    return _ZNK17FsAxisStateHelper19axisHasSgSafeAbsPosEv(t);
  return 0;
}
int _ZNK3qic20IpoServerReplyObject2idEv(void *t){ return U(t,12); }
