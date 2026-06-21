#include <stdio.h>
#include <string.h>
#include <unistd.h>
typedef unsigned char u8;
#define PTR(t,o) (*(void**)((char*)(t)+(o)))
#define I(p,o)   (*(int*)((char*)(p)+(o)))
#define B(p,o)   (*(u8*)((char*)(p)+(o)))
extern int   _ZNK14HerosFramework9eventMaskEv(void*);
extern _Bool _ZNK14HerosFramework18isUnreliableClientEv(void*);
extern int   _ZNK10HerosQueue2idEv(void*);
extern _Bool _ZNK10HerosQueue5validEv(void*);
extern _Bool _ZNK10HerosQueue11isSuspendedEv(void*);
extern int   _ZNK10HerosQueue9eventMaskEv(void*);
extern int   _ZN21RequestAdministration12requestCountEv(void*);
int main(void){
  char buf[160];
  static const int IV[]={-1,0,1,7,255,-100};
  for(int k=0;k<6;k++){
    int v=IV[k];
    unsigned char hf_d[64] __attribute__((aligned(16))); memset(hf_d,0,64); I(hf_d,0)=v*3; B(hf_d,38)=(u8)(k&1);
    unsigned char hf[32] __attribute__((aligned(16))); memset(hf,0,32); PTR(hf,0)=hf_d;
    unsigned char hq_e[32] __attribute__((aligned(16))); memset(hq_e,0,32); I(hq_e,8)=v*5+2;
    unsigned char hq_d[96] __attribute__((aligned(16))); memset(hq_d,0,96); I(hq_d,4)=v; B(hq_d,56)=(u8)(k%3==0); PTR(hq_d,40)=hq_e;
    unsigned char hq[32] __attribute__((aligned(16))); memset(hq,0,32); PTR(hq,8)=hq_d;
    unsigned char ra_d[32] __attribute__((aligned(16))); memset(ra_d,0,32); I(ra_d,8)=v; I(ra_d,12)=v+13;
    unsigned char ra[32] __attribute__((aligned(16))); memset(ra,0,32); PTR(ra,12)=ra_d;
    int n=sprintf(buf,"%d hfem=%d hfur=%d qid=%d qv=%d qs=%d qem=%d rc=%d\n", k,
      _ZNK14HerosFramework9eventMaskEv(hf), (int)_ZNK14HerosFramework18isUnreliableClientEv(hf),
      _ZNK10HerosQueue2idEv(hq), (int)_ZNK10HerosQueue5validEv(hq), (int)_ZNK10HerosQueue11isSuspendedEv(hq),
      _ZNK10HerosQueue9eventMaskEv(hq), _ZN21RequestAdministration12requestCountEv(ra)); write(1,buf,n);
  }
  return 0;
}
