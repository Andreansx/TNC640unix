#include <stdio.h>
#include <string.h>
#include <unistd.h>
extern double _ZN13GeometryTools15normalize_angleEdb(double,_Bool);
static unsigned long long B(double d){ unsigned long long h; memcpy(&h,&d,8); return h; }
int main(void){
  char buf[96];
  /* radians + degrees, wide sweep incl. boundaries */
  for(int deg=0; deg<2; deg++){
    for(long i=-2000; i<=2000; i++){
      double a = deg ? (double)i*0.37 : (double)i*0.0123;
      int n=sprintf(buf,"%d %ld %016llx\n", deg, i, B(_ZN13GeometryTools15normalize_angleEdb(a,(_Bool)deg)));
      write(1,buf,n);
    }
  }
  return 0;
}
