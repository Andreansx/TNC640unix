#include <stdio.h>
#include <unistd.h>
extern _Bool _ZN13GeometryTools21is_value_inside_rangeEddd(double,double,double);
int main(void){
  static const double V[]={-1e9,-3.5,-1,0,0.5,1,3.5,7,1e9};
  char buf[96];
  for(int a=0;a<9;a++)for(int b=0;b<9;b++)for(int c=0;c<9;c++){
    int n=sprintf(buf,"%d %d %d = %d\n",a,b,c,
      (int)_ZN13GeometryTools21is_value_inside_rangeEddd(V[a],V[b],V[c])); write(1,buf,n);
  }
  return 0;
}
