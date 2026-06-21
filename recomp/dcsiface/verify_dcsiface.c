#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
typedef unsigned char u8;
extern int _ZN12DcsInterface9_cfgYAxisEv(void*);
extern int _ZN12DcsInterface16_isAxisAvailableEi(void*,int);
extern int _ZN12DcsInterface18_isAxisAvailableChEii(void*,int,int);
extern int _ZN12DcsInterface11KernOpenSpmEPi(void*,int*);
extern int _ZN12DcsInterface11KernOpenWkzEPi(void*,int*);
int main(void){
  size_t N=40000; u8 *th=malloc(N);
  for(size_t i=0;i<N;i++) th[i]=(u8)((i*131u+7u)&0xff);  /* distinct-ish per offset */
  char buf[128];
  int n=sprintf(buf,"cfgY %d\n", _ZN12DcsInterface9_cfgYAxisEv(th)); write(1,buf,n);
  for(int a=0;a<48;a++){ n=sprintf(buf,"ax %d %d\n", a, _ZN12DcsInterface16_isAxisAvailableEi(th,a)); write(1,buf,n); }
  for(int a=0;a<16;a++) for(int b=0;b<25;b++){
    n=sprintf(buf,"axch %d %d %d\n", a,b, _ZN12DcsInterface18_isAxisAvailableChEii(th,a,b)); write(1,buf,n); }
  int o1=-1,o2=-1; int r1=_ZN12DcsInterface11KernOpenSpmEPi(th,&o1);
  int r2=_ZN12DcsInterface11KernOpenWkzEPi(th,&o2);
  n=sprintf(buf,"spm r=%d o=%d  wkz r=%d o=%d\n", r1,o1,r2,o2); write(1,buf,n);
  free(th); return 0;
}
