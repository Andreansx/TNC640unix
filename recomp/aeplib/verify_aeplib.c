/* differential harness for the 6 Aeplib flat-field leaf functions. */
#include <stdio.h>
#include <string.h>
#include <unistd.h>
typedef unsigned int u32; typedef unsigned char u8;
extern int      _Z23AEPLIB_SchlittenInKanalh(u8);
extern _Bool    _Z12MehrSpindlerh(u8);
extern unsigned _Z14chk_zustellungP6geotec(void*);
extern int      _Z15AEP_VorschubTypP6geotec(void*);
extern int      _Z26AEP_ElementNichtBearbeitenP6geotec(void*);
extern int      _Z14AEP_set_ovsi_0P6geotec(void*);

int main(void){
  char buf[256];
  /* uchar arg sweep (full domain) */
  for(int a=0;a<256;a++){
    int n=sprintf(buf,"u %d sk=%d ms=%d\n", a,
      _Z23AEPLIB_SchlittenInKanalh((u8)a), (int)_Z12MehrSpindlerh((u8)a));
    write(1,buf,n);
  }
  /* geotec field sweep */
  static const int TYP[]={0,1,2}, FL[]={0,1,3}, B120[]={0,0x55,0xff};
  static const u32 DW[][2]={{0,0},{0xffffffffu,0x40},{0x40,0xffffffffu},{0xaaaaaaaau,0x55555555u}};
  long idx=0;
  for(int ti=0;ti<3;ti++) for(int fi=0;fi<3;fi++) for(int b=0;b<41;b++)
  for(int di=0;di<4;di++) for(int bi=0;bi<3;bi++){
    unsigned char g[256] __attribute__((aligned(16)));
    memset(g,0,256);
    *(int*)(g+84)=TYP[ti]; g[94]=(u8)FL[fi]; g[108]=(u8)b;
    *(u32*)(g+216)=DW[di][0]; *(u32*)(g+220)=DW[di][1];
    g[120]=(u8)B120[bi]; *(double*)(g+100)=1.5;
    int n=sprintf(buf,"g %ld cz=%u vt=%d en=%d", idx,
      _Z14chk_zustellungP6geotec(g), _Z15AEP_VorschubTypP6geotec(g),
      _Z26AEP_ElementNichtBearbeitenP6geotec(g));
    unsigned char g2[256] __attribute__((aligned(16)));
    memcpy(g2,g,256);
    int rv=_Z14AEP_set_ovsi_0P6geotec(g2);
    unsigned long long db; memcpy(&db,g2+100,8);
    n+=sprintf(buf+n," ov=%d ov120=%02x ovd=%016llx\n", rv, g2[120], db);
    write(1,buf,n); idx++;
  }
  int n=sprintf(buf,"null cz=%u vt=%d en=%d\n",
    _Z14chk_zustellungP6geotec(0), _Z15AEP_VorschubTypP6geotec(0),
    _Z26AEP_ElementNichtBearbeitenP6geotec(0));
  write(1,buf,n);
  return 0;
}
