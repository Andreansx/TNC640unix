#include <stdio.h>
#include <string.h>
#include <unistd.h>
typedef unsigned int u32;
extern void *_ZN13CleaningGroup18min_element_lengthEd(void*,double);
extern void *_ZN13CleaningGroup14min_gap_lengthEd(void*,double);
extern void *_ZN13CleaningGroup25max_stretch_length_changeEd(void*,double);
extern void *_ZN13CleaningGroup34bridge_gap_with_stretch_if_greaterEd(void*,double);
extern void *_ZN13CleaningGroup14colinear_angleEd(void*,double);
extern void *_ZN13CleaningGroup23colinear_max_point_distEd(void*,double);
extern void *_ZN13CleaningGroup32cocircular_max_circle_differenceEd(void*,double);
extern void *_ZN13CleaningGroup17join_respect_tagsEb(void*,_Bool);
extern void *_ZN6Geolib10ValueRangeIjE7set_minEj(void*,u32);
extern void *_ZN6Geolib10ValueRangeIjE7set_maxEj(void*,u32);
extern void *_ZN6Geolib10ValueRangeIdE7set_minEd(void*,double);
extern void *_ZN6Geolib10ValueRangeIdE7set_maxEd(void*,double);
static unsigned long long H8(void*p){ unsigned long long h; memcpy(&h,p,8); return h; }
int main(void){
  char buf[160]; unsigned char t[96] __attribute__((aligned(16)));
  static const double DV[]={-3.5,0,1,7.25,1e308};
  for(int i=0;i<5;i++){ double a=DV[i];
    memset(t,0,96); _ZN13CleaningGroup18min_element_lengthEd(t,a);
    int n=sprintf(buf,"mel %d %016llx\n", i, H8(t+8)); write(1,buf,n);
    memset(t,0,96); _ZN13CleaningGroup14min_gap_lengthEd(t,a); n=sprintf(buf,"mgl %d %016llx\n",i,H8(t+16)); write(1,buf,n);
    memset(t,0,96); _ZN13CleaningGroup25max_stretch_length_changeEd(t,a); n=sprintf(buf,"msl %d %016llx\n",i,H8(t+24)); write(1,buf,n);
    memset(t,0,96); _ZN13CleaningGroup34bridge_gap_with_stretch_if_greaterEd(t,a); n=sprintf(buf,"bgs %d %016llx\n",i,H8(t+32)); write(1,buf,n);
    memset(t,0,96); _ZN13CleaningGroup14colinear_angleEd(t,a); n=sprintf(buf,"can %d %016llx\n",i,H8(t+40)); write(1,buf,n);
    memset(t,0,96); _ZN13CleaningGroup23colinear_max_point_distEd(t,a); n=sprintf(buf,"cmp %d %016llx\n",i,H8(t+48)); write(1,buf,n);
    memset(t,0,96); _ZN13CleaningGroup32cocircular_max_circle_differenceEd(t,a); n=sprintf(buf,"ccd %d %016llx\n",i,H8(t+56)); write(1,buf,n);
    memset(t,0,96); _ZN6Geolib10ValueRangeIdE7set_minEd(t,a); n=sprintf(buf,"dmin %d %016llx\n",i,H8(t+0)); write(1,buf,n);
    memset(t,0,96); _ZN6Geolib10ValueRangeIdE7set_maxEd(t,a); n=sprintf(buf,"dmax %d %016llx\n",i,H8(t+8)); write(1,buf,n);
  }
  for(int b=0;b<3;b++){ memset(t,0,96); _ZN13CleaningGroup17join_respect_tagsEb(t,(_Bool)b); int n=sprintf(buf,"jrt %d %02x\n",b,t[64]); write(1,buf,n); }
  static const u32 UV[]={0,1,0xdeadbeefu,0xFFFFFFFFu};
  for(int i=0;i<4;i++){
    memset(t,0,96); _ZN6Geolib10ValueRangeIjE7set_minEj(t,UV[i]); int n=sprintf(buf,"umin %d %08x\n",i,*(u32*)(t+0)); write(1,buf,n);
    memset(t,0,96); _ZN6Geolib10ValueRangeIjE7set_maxEj(t,UV[i]); n=sprintf(buf,"umax %d %08x\n",i,*(u32*)(t+4)); write(1,buf,n);
  }
  return 0;
}
