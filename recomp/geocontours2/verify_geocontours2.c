#include <stdio.h>
#include <string.h>
#include <unistd.h>
typedef unsigned int u32;
#define U(p,o) (*(u32*)((char*)(p)+(o)))
#define B(p,o) (*(unsigned char*)((char*)(p)+(o)))
extern void *_ZN6Geolib7Contour18AssumePropertiesOfERKS0_(void*,const void*);
extern int   _ZN6Geolib13PocketContour18AssumePropertiesOfERKS0_(void*,const void*);
int main(void){
  char buf[200]; unsigned char t[128] __attribute__((aligned(16))), s[128];
  for(int v=0; v<4; v++){
    /* source pattern */
    for(int i=0;i<128;i++) s[i]=(unsigned char)((i*37+v*11+3)&0xff);
    /* Contour::AssumePropertiesOf */
    memset(t,0xAA,128);
    _ZN6Geolib7Contour18AssumePropertiesOfERKS0_(t,s);
    int n=sprintf(buf,"c %d b4=%02x d8=%08x b12=%02x\n", v, B(t,4),U(t,8),B(t,12)); write(1,buf,n);
    /* PocketContour::AssumePropertiesOf, both this.byte60 states */
    for(int z=0; z<2; z++){
      memset(t,0xAA,128); B(t,60)=(unsigned char)(z?0:0x55);
      int r=_ZN6Geolib13PocketContour18AssumePropertiesOfERKS0_(t,s);
      n=sprintf(buf,"p %d z=%d r=%d b4=%02x d8=%08x b12=%02x b64=%02x d68=%08x d84=%08x b88=%02x b60=%02x\n",
        v,z,r,B(t,4),U(t,8),B(t,12),B(t,64),U(t,68),U(t,84),B(t,88),B(t,60)); write(1,buf,n);
    }
  }
  return 0;
}
