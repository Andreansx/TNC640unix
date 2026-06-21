#include <stdio.h>
#include <string.h>
#include <unistd.h>
typedef struct { char pad[12]; void *d; } Obj;
extern int   _ZNK8topology4Axis10physAxisIdEv(void*);
extern int   _ZNK8topology4Axis10progAxisIdEv(void*);
extern int   _ZNK8topology4Axis13kinematicRoleEv(void*);
extern int   _ZNK8topology4Axis10motionTypeEv(void*);
extern _Bool _ZNK8topology4Axis11isAuxiliaryEv(void*);
extern int   _ZNK8topology7Channel2idEv(void*);
int main(void){
  char buf[128];
  for(int v=0; v<6; v++){
    unsigned char d[64] __attribute__((aligned(16))); memset(d,0,64);
    *(int*)(d+16)=v*7-3; *(int*)(d+24)=v*11+1; *(int*)(d+28)=v*101;
    *(int*)(d+32)=v%5; *(int*)(d+36)=v%3; d[48]=(unsigned char)(v&1);
    Obj o; memset(&o,0,sizeof o); o.d=d;
    int n=sprintf(buf,"%d pa=%d pr=%d kr=%d mt=%d aux=%d cid=%d\n", v,
      _ZNK8topology4Axis10physAxisIdEv(&o), _ZNK8topology4Axis10progAxisIdEv(&o),
      _ZNK8topology4Axis13kinematicRoleEv(&o), _ZNK8topology4Axis10motionTypeEv(&o),
      (int)_ZNK8topology4Axis11isAuxiliaryEv(&o), _ZNK8topology7Channel2idEv(&o)); write(1,buf,n);
  }
  return 0;
}
