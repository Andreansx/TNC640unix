#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <float.h>
typedef unsigned int u32;
#define U(p,o) (*(u32*)((char*)(p)+(o)))
#define D(p,o) (*(double*)((char*)(p)+(o)))
extern int    _ZNK6Geolib10ValueRangeIjE3minEv(void*); extern int _ZNK6Geolib10ValueRangeIjE3maxEv(void*);
extern int    _ZNK6Geolib10ValueRangeIjE4spanEv(void*); extern _Bool _ZNK6Geolib10ValueRangeIjE5emptyEv(void*);
extern _Bool  _ZNK6Geolib10ValueRangeIjE5validEv(void*);
extern double _ZNK6Geolib10ValueRangeIdE3minEv(void*); extern double _ZNK6Geolib10ValueRangeIdE3maxEv(void*);
extern double _ZNK6Geolib10ValueRangeIdE4spanEv(void*); extern _Bool _ZNK6Geolib10ValueRangeIdE5emptyEv(void*);
extern _Bool  _ZNK6Geolib10ValueRangeIdE5validEv(void*);
extern double _ZNK6Geolib20SplittableValueRange15get_range_startEv(void*);
extern double _ZNK6Geolib20SplittableValueRange13get_range_endEv(void*);
extern double _ZNK6Geolib20SplittableValueRange20get_sample_step_sizeEv(void*);
extern int    _ZNK6Geolib20SplittableValueRange21get_number_of_samplesEv(void*);
extern double _ZNK13FixedGridHash9cell_sizeEv(void*); extern int _ZNK13FixedGridHash10cell_countEv(void*);
static unsigned long long B(double d){ unsigned long long h; memcpy(&h,&d,8); return h; }
int main(void){
  char buf[160]; unsigned char t[64] __attribute__((aligned(16)));
  static const u32 UV[]={0,1,5,0xFFFFFFFFu,7};
  for(int a=0;a<5;a++)for(int b=0;b<5;b++){
    memset(t,0,64); U(t,0)=UV[a]; U(t,4)=UV[b];
    int n=sprintf(buf,"ju %u %u min=%d max=%d span=%d e=%d v=%d\n",UV[a],UV[b],
      _ZNK6Geolib10ValueRangeIjE3minEv(t),_ZNK6Geolib10ValueRangeIjE3maxEv(t),_ZNK6Geolib10ValueRangeIjE4spanEv(t),
      (int)_ZNK6Geolib10ValueRangeIjE5emptyEv(t),(int)_ZNK6Geolib10ValueRangeIjE5validEv(t)); write(1,buf,n);
  }
  static const double DV[]={-3.5,0,1,7.25,DBL_MAX};
  for(int a=0;a<5;a++)for(int b=0;b<5;b++){
    memset(t,0,64); D(t,0)=DV[a]; D(t,8)=DV[b];
    int n=sprintf(buf,"jd %d %d min=%016llx max=%016llx span=%016llx e=%d v=%d\n",a,b,
      B(_ZNK6Geolib10ValueRangeIdE3minEv(t)),B(_ZNK6Geolib10ValueRangeIdE3maxEv(t)),B(_ZNK6Geolib10ValueRangeIdE4spanEv(t)),
      (int)_ZNK6Geolib10ValueRangeIdE5emptyEv(t),(int)_ZNK6Geolib10ValueRangeIdE5validEv(t)); write(1,buf,n);
  }
  for(int a=0;a<5;a++)for(int s=0;s<3;s++){
    memset(t,0,64); D(t,0)=DV[a]; D(t,8)=DV[(a+1)%5]; D(t,16)=DV[s]+0.5; U(t,24)=UV[a];
    int n=sprintf(buf,"sp %d %d rs=%016llx re=%016llx step=%016llx ns=%d\n",a,s,
      B(_ZNK6Geolib20SplittableValueRange15get_range_startEv(t)),B(_ZNK6Geolib20SplittableValueRange13get_range_endEv(t)),
      B(_ZNK6Geolib20SplittableValueRange20get_sample_step_sizeEv(t)),_ZNK6Geolib20SplittableValueRange21get_number_of_samplesEv(t)); write(1,buf,n);
  }
  for(int a=0;a<5;a++){
    memset(t,0,64); U(t,0)=UV[a]; D(t,28)=DV[a]+1.25;
    int n=sprintf(buf,"fg %d cnt=%d size=%016llx\n",a,_ZNK13FixedGridHash10cell_countEv(t),B(_ZNK13FixedGridHash9cell_sizeEv(t))); write(1,buf,n);
  }
  return 0;
}
