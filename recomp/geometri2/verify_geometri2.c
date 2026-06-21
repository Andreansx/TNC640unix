/* differential harness: builds a geotec dword array, sweeps mask/field/gate. */
#include <stdio.h>
#include <unistd.h>
typedef unsigned int u32;
extern int _Z15IsCartInkrementmP6geotec(u32,const u32*);
extern int _Z14IsPolareLaengemP6geotec(u32,const u32*);
extern int _Z25IsPolaresLaengenInkrementmP6geotec(u32,const u32*);
extern int _Z15IsPolarerWinkelmP6geotec(u32,const u32*);
extern int _Z24IsPolaresWinkelInkrementmP6geotec(u32,const u32*);
int main(void){
  static const u32 MASK[]={0x4000,0x8000,0x200000,0x400000,0x10000,0,0xC000};
  static const u32 GATE22[]={0xffffffff,0x4000,0x200000,0};
  static const u32 GATE23[]={0,0x4000,0x200000,0xffffffff};
  static const u32 FV[]={0,2,4,6,0x20,0x22,0x24,0x26,0x100,0x126,0x6,0x122};
  char buf[256]; long idx=0;
  for(unsigned gi=0; gi<4; gi++)
  for(unsigned hi=0; hi<4; hi++)
  for(unsigned mi=0; mi<7; mi++)
  for(unsigned fi=0; fi<12; fi++){
    u32 g[80]={0};
    g[22]=GATE22[gi]; g[23]=GATE23[hi];
    g[54]=g[55]=g[60]=g[61]=FV[fi];
    u32 m=MASK[mi];
    int bp=sprintf(buf,"%ld", idx);
    bp+=sprintf(buf+bp," ci=%d", _Z15IsCartInkrementmP6geotec(m,g));
    bp+=sprintf(buf+bp," pl=%d", _Z14IsPolareLaengemP6geotec(m,g));
    bp+=sprintf(buf+bp," pli=%d",_Z25IsPolaresLaengenInkrementmP6geotec(m,g));
    bp+=sprintf(buf+bp," pw=%d", _Z15IsPolarerWinkelmP6geotec(m,g));
    bp+=sprintf(buf+bp," pwi=%d\n",_Z24IsPolaresWinkelInkrementmP6geotec(m,g));
    write(1,buf,bp); idx++;
  }
  return 0;
}
