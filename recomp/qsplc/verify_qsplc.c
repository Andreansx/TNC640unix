#include <stdio.h>
#include <string.h>
#include <unistd.h>
typedef unsigned int u32;
#define U(p,o) (*(u32*)((char*)(p)+(o)))
extern int _ZNK3qic20PlcServerReplyObject5stateEv(void*);
extern int _ZNK3qic20PlcServerReplyObject15operationHandleEv(void*);
extern int _ZNK3qic20PlcServerReplyObject5valueEv(void*);
extern int _ZNK3qic13PlcController16connectionHandleEv(void*);
int main(void){
  char buf[120];
  static const int V[]={0,1,-1,12345,-9999,0x7fffffff};
  for(int k=0;k<6;k++){
    unsigned char t[64] __attribute__((aligned(16))); memset(t,0,64);
    U(t,16)=V[k]; U(t,28)=V[k]*2+1; U(t,44)=V[k]^0x55; U(t,56)=V[k]+100;
    int n=sprintf(buf,"%d st=%d op=%d val=%d ch=%d\n",k,
      _ZNK3qic20PlcServerReplyObject5stateEv(t),_ZNK3qic20PlcServerReplyObject15operationHandleEv(t),
      _ZNK3qic20PlcServerReplyObject5valueEv(t),_ZNK3qic13PlcController16connectionHandleEv(t)); write(1,buf,n);
  }
  return 0;
}
