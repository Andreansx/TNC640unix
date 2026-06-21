#include <stdio.h>
#include <string.h>
#include <unistd.h>
typedef unsigned int u32;
#define U(p,o) (*(u32*)((char*)(p)+(o)))
extern int _ZNK17FsAxisStateHelper16isAxisWithSafetyEv(void*);
extern int _ZNK17FsAxisStateHelper18isAxisSgReferencedEv(void*);
extern int _ZNK17FsAxisStateHelper22isAxisSgPositionTestedEv(void*);
extern int _ZNK17FsAxisStateHelper19axisHasSgSafeAbsPosEv(void*);
extern int _ZNK17FsAxisStateHelper23doesAxisRequireCheckingEv(void*);
extern int _ZNK17FsAxisStateHelper18hasAxisBeenCheckedEv(void*);
extern int _ZNK3qic20IpoServerReplyObject2idEv(void*);
int main(void){
  char buf[160];
  for(u32 i=0;i<16;i++) for(int s=0;s<2;s++) for(int id=0;id<2;id++){
    unsigned char t[32] __attribute__((aligned(16))); memset(t,0,32);
    U(t,0)=i<<28; U(t,4)= s? (1u<<27):0; U(t,12)= id? 99:0;
    int n=sprintf(buf,"%u %d %d ws=%d ref=%d pt=%d abs=%d req=%d chk=%d id=%d\n", i,s,id,
      _ZNK17FsAxisStateHelper16isAxisWithSafetyEv(t),_ZNK17FsAxisStateHelper18isAxisSgReferencedEv(t),
      _ZNK17FsAxisStateHelper22isAxisSgPositionTestedEv(t),_ZNK17FsAxisStateHelper19axisHasSgSafeAbsPosEv(t),
      _ZNK17FsAxisStateHelper23doesAxisRequireCheckingEv(t),_ZNK17FsAxisStateHelper18hasAxisBeenCheckedEv(t),
      _ZNK3qic20IpoServerReplyObject2idEv(t)); write(1,buf,n);
  }
  return 0;
}
