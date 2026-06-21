#include <stdio.h>
#include <unistd.h>
typedef struct node { char pad0[12]; struct node *link; char pad1[116]; int bam; } node;
extern int _Z10AEP_SetBamP6geotecS0_m(node*,node*,unsigned);
extern int _Z10AEP_AddBamP6geotecS0_m(node*,node*,unsigned);
extern int _Z10AEP_DelBamP6geotecS0_m(node*,node*,unsigned);

static void build(node n[5], const int *bam0){
  for(int i=0;i<5;i++){ n[i].bam=bam0[i]; }
  n[0].link=&n[1]; n[1].link=&n[2]; n[2].link=&n[3];
  n[3].link=&n[4];   /* a2(n3)->link = tail(n4) */
  n[4].link=0;
}
static int run(const char*op, int (*fn)(node*,node*,unsigned), const int*bam0, unsigned a3, char*buf){
  node n[5]; build(n,bam0);
  int r=fn(&n[0],&n[3],a3);
  int linkok = (n[3].link==&n[4]);
  return sprintf(buf,"%s a3=%08x r=%d link=%d b=%d,%d,%d,%d,%d\n",
    op,a3,r,linkok,n[0].bam,n[1].bam,n[2].bam,n[3].bam,n[4].bam);
}
int main(void){
  static const int B0[][5]={ {0,0,0,0,0},{1,2,4,8,16},{-1,-1,-1,-1,-1},{0xff,0,0xff,0,0xff} };
  static const unsigned A3[]={0,1,0x12,0xff,0x80000000u,0xdeadbeefu};
  char buf[160];
  for(int p=0;p<4;p++) for(unsigned k=0;k<6;k++){
    int n; const int*b=B0[p]; unsigned a=A3[k];
    n=run("set",_Z10AEP_SetBamP6geotecS0_m,b,a,buf); write(1,buf,n);
    n=run("add",_Z10AEP_AddBamP6geotecS0_m,b,a,buf); write(1,buf,n);
    n=run("del",_Z10AEP_DelBamP6geotecS0_m,b,a,buf); write(1,buf,n);
  }
  return 0;
}
