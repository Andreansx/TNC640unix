#include <stdio.h>
#include <unistd.h>
extern _Bool _ZN13GeometryTools14isAngleBetweenERKdS1_S1_d(const double*,const double*,const double*,double);
int main(void){
  static const double V[]={-2,-1,-0.5,0,0.25,0.5,1,2};
  char buf[80];
  for(int i=0;i<8;i++)for(int j=0;j<8;j++)for(int k=0;k<8;k++)for(int m=0;m<8;m++){
    double a=V[i],b=V[j],c=V[k],d=V[m];
    int n=sprintf(buf,"%d%d%d%d=%d\n",i,j,k,m,
      (int)_ZN13GeometryTools14isAngleBetweenERKdS1_S1_d(&a,&b,&c,d)); write(1,buf,n);
  }
  return 0;
}
