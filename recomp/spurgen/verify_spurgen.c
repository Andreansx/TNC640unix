#include <stdio.h>
#include <string.h>
#include <unistd.h>
extern int _Z5SwapNPci(char*,int);
extern double *Box_erweitern(double*,double*);
int main(void){
  char buf[128];
  /* SwapN: sweep length and content patterns */
  for(int len=0; len<=18; len++)
  for(int pat=0; pat<3; pat++){
    char a[32];
    for(int i=0;i<32;i++) a[i]=(char)(pat==0? i : pat==1? (i*7+1) : (31-i));
    int r=_Z5SwapNPci(a,len);
    int n=sprintf(buf,"swap len=%d pat=%d r=%d:", len,pat,r);
    for(int i=0;i<24;i++) n+=sprintf(buf+n,"%02x", (unsigned char)a[i]);
    buf[n++]='\n'; write(1,buf,n);
  }
  /* Box_erweitern: sweep box pairs */
  static const double V[]={-5,-1,0,1,3.5,7};
  long idx=0;
  for(int i=0;i<6;i++) for(int j=0;j<6;j++){
    double a[4]={V[i],V[j],V[i],V[j]};
    double b[4]={V[j],V[i],V[j],V[i]};
    Box_erweitern(a,b);
    unsigned long long h[4]; for(int k=0;k<4;k++) memcpy(&h[k],&a[k],8);
    int n=sprintf(buf,"box %ld %016llx %016llx %016llx %016llx\n", idx, h[0],h[1],h[2],h[3]);
    write(1,buf,n); idx++;
  }
  return 0;
}
