#include <stdio.h>
#include <string.h>
#include <unistd.h>
typedef unsigned int u32; typedef unsigned char u8;
extern void *_ZN6Geolib14ContourElement20disallowFeedAdaptionEv(void*);
extern int   _ZNK6Geolib14ContourElement21isFeedAdaptionAllowedEv(void*);
extern _Bool _ZNK6Geolib7Contour18IsToolCorrPastOrToEv(void*);
extern _Bool _ZNK6Geolib7Contour12IsCorrFwdBwdEv(void*);
extern int   _ZNK6Geolib13PocketContour12OffsetResult7isEmptyEv(void*);
extern _Bool _ZNK6Geolib14PocketContours14PocketsDefinedEv(void*);
#define U(p,o) (*(u32*)((char*)(p)+(o)))
int main(void){
  char buf[128]; unsigned char th[64] __attribute__((aligned(16))), other[64];
  /* disallowFeedAdaption + isFeedAdaptionAllowed: byte@31 */
  for(int v=0;v<4;v++){
    memset(th,0,64); th[31]=(u8)(v==3?255:v);
    int allowed=_ZNK6Geolib14ContourElement21isFeedAdaptionAllowedEv(th);
    _ZN6Geolib14ContourElement20disallowFeedAdaptionEv(th);
    int n=sprintf(buf,"fa v=%d allowed=%d after31=%d\n", v, allowed, th[31]); write(1,buf,n);
  }
  /* IsToolCorrPastOrTo / IsCorrFwdBwd: dword@8 */
  for(u32 v=0; v<8; v++){
    memset(th,0,64); U(th,8)=v;
    int n=sprintf(buf,"corr v=%u pto=%d fb=%d\n", v,
      (int)_ZNK6Geolib7Contour18IsToolCorrPastOrToEv(th),
      (int)_ZNK6Geolib7Contour12IsCorrFwdBwdEv(th)); write(1,buf,n);
  }
  /* OffsetResult::isEmpty: dwords @0,@4,@12,@16 */
  static const u32 W[]={0,1,7};
  for(int a=0;a<3;a++)for(int b=0;b<3;b++)for(int c=0;c<3;c++)for(int d=0;d<3;d++){
    memset(th,0,64); U(th,0)=W[a]; U(th,4)=W[b]; U(th,12)=W[c]; U(th,16)=W[d];
    int n=sprintf(buf,"oe %u%u%u%u=%d\n",W[a],W[b],W[c],W[d],
      _ZNK6Geolib13PocketContour12OffsetResult7isEmptyEv(th)); write(1,buf,n);
  }
  /* PocketsDefined: head field == this (empty) vs other */
  memset(th,0,64); *(void**)th=th;       /* self -> not defined */
  int n=sprintf(buf,"pd self=%d\n", (int)_ZNK6Geolib14PocketContours14PocketsDefinedEv(th)); write(1,buf,n);
  *(void**)th=other;                       /* other -> defined */
  n=sprintf(buf,"pd other=%d\n", (int)_ZNK6Geolib14PocketContours14PocketsDefinedEv(th)); write(1,buf,n);
  return 0;
}
