#include <stdio.h>
#include <string.h>
#include <unistd.h>
typedef unsigned int u32;
#define U(p,o)   (*(u32*)((char*)(p)+(o)))
#define B(p,o)   (*(unsigned char*)((char*)(p)+(o)))
#define PTR(p,o) (*(void**)((char*)(p)+(o)))
extern int _ZNK3qic25ConfigControllerOperation9isRunningEv(void*);
extern int _ZNK3qic23ConfigControllerPrivate16isInUnitTestModeEv(void*);
extern int _ZNK17ConfigTypeManager21isOperationInProgressEv(void*);
extern int _ZNK22ConfigUniqueStringList7maxSizeEv(void*);
extern int _ZNK22ConfigUniqueStringList13isInitializedEv(void*);
extern int _ZNK22ConfigUniqueStringList4sizeEv(void*);
extern _Bool _ZN3qic16ConfigController15isSuccessResultEi(int);
extern int _ZNK3qic16ConfigController12hasDataErrorEv(void*);
int main(void){
  char buf[200];
  for(int k=0;k<8;k++){
    unsigned char t[96] __attribute__((aligned(16))); memset(t,0,96);
    unsigned char d1[32] __attribute__((aligned(16))); memset(d1,0,32);
    unsigned char d2[32] __attribute__((aligned(16))); memset(d2,0,32);
    B(t,12)=(unsigned char)(k*5); B(t,44)=(unsigned char)(k*3+1); B(t,53)=(unsigned char)(k|1);
    B(t,80)=(unsigned char)(k*9); U(t,28)=k*1000-7;
    PTR(t,20)=d2; U(d2,4)=k*77+3;
    PTR(t,60)=d1; B(d1,45)=(unsigned char)(k%2);
    int n=sprintf(buf,"%d run=%d ut=%d op=%d max=%d ini=%d sz=%d de=%d\n", k,
      _ZNK3qic25ConfigControllerOperation9isRunningEv(t),_ZNK3qic23ConfigControllerPrivate16isInUnitTestModeEv(t),
      _ZNK17ConfigTypeManager21isOperationInProgressEv(t),_ZNK22ConfigUniqueStringList7maxSizeEv(t),
      _ZNK22ConfigUniqueStringList13isInitializedEv(t),_ZNK22ConfigUniqueStringList4sizeEv(t),
      _ZNK3qic16ConfigController12hasDataErrorEv(t)); write(1,buf,n);
  }
  for(int a=-2;a<=2;a++){ int n=sprintf(buf,"sr %d %d\n",a,(int)_ZN3qic16ConfigController15isSuccessResultEi(a)); write(1,buf,n); }
  return 0;
}
