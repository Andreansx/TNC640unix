/* verify_gtlib2.c — differential harness for the 11 new GTFIND classifiers.
 * Same source drives both sides: linked against the genuine i386 .so (truth)
 * or the rebuilt C (recomp). Builds per-arch node chains and sweeps. */
#include <stdio.h>
#include <stddef.h>
#include <unistd.h>

typedef struct node {
  char pad0[16];
  struct node *p16;
  struct node *p20;
  char pad1[4];
  int  f28;
  char pad2[52];
  int  typ;
  int  pad3;
  int  flags;
} node;

#if __SIZEOF_POINTER__ == 4
_Static_assert(offsetof(node,p16)==16, "p16@16");
_Static_assert(offsetof(node,p20)==20, "p20@20");
_Static_assert(offsetof(node,f28)==28, "f28@28");
_Static_assert(offsetof(node,typ)==84, "typ@84");
_Static_assert(offsetof(node,flags)==92, "flags@92");
#endif

extern int   _Z16GTFIND_IsAbflachP5akopf(node*);
extern int   _Z17GTFIND_IsMehrkantP5akopf(node*);
extern int   _Z15GTFIND_IsMusterP5akopf(node*);
extern _Bool _Z14GTFIND_IsFigurP5akopf(node*);
extern int   _Z16GTFIND_IsBohrungP5akopf(node*);
extern _Bool _Z13GTFIND_IsRohrP5akopf(node*);
extern _Bool _Z15GTFIND_IsStangeP5akopf(node*);
extern _Bool _Z15GTFIND_IsTascheP5akopf(node*);
extern int   _Z20GTFIND_IsRucksackTypP6geotec9basvar_at(node*,int);
extern int   _Z20GTFIND_IsGeoKomplettP6geotec(node*);
extern int   _Z17GTFIND_IsGeoErrorP6geotec(node*);
extern int   _Z13GTFIND_IsLineP6geotec(node*);
extern int   _Z13GTFIND_IsCircP6geotec(node*);

int main(void){
  static const int T1S[]={0,1,6,7,20,21,58,59};
  static const int F28[]={0,6,36};
  static const int FLS[]={0,2,8,0x20,0x40,0x2a};
  char buf[256]; long idx=0;
  for(int scen=0; scen<5; scen++)
  for(unsigned ti=0; ti<sizeof(T1S)/sizeof(int); ti++)
  for(int t2=0; t2<64; t2++)
  for(unsigned fi=0; fi<3; fi++)
  for(unsigned li=0; li<6; li++){
    node n0={0}, n1={0}, n2={0}, n20={0};
    int t0 = (idx & 1) ? 1 : 0;
    int bv = (t2%3==0)?1:(t2%3==1)?6:21;
    n0.typ=t0; n0.f28=F28[fi]; n0.flags=FLS[li];
    n1.typ=T1S[ti];
    n2.typ=t2;
    /* default full chain */
    n0.p16=&n1; n1.p16=&n2; n0.p20=&n20;
    node *root=&n0;
    if(scen==1) n0.p16=0;            /* gruppe link null */
    else if(scen==2) n1.p16=0;       /* element link null */
    else if(scen==3) n0.p20=0;       /* p20 null (IsTasche gate) */
    else if(scen==4) root=0;         /* null root */
    int bp=0;
    bp+=sprintf(buf+bp,"%ld", idx);
    bp+=sprintf(buf+bp," ab=%d",   _Z16GTFIND_IsAbflachP5akopf(root));
    bp+=sprintf(buf+bp," mk=%d",   _Z17GTFIND_IsMehrkantP5akopf(root));
    bp+=sprintf(buf+bp," mu=%d",   _Z15GTFIND_IsMusterP5akopf(root));
    bp+=sprintf(buf+bp," fg=%d",   (int)_Z14GTFIND_IsFigurP5akopf(root));
    bp+=sprintf(buf+bp," bo=%d",   _Z16GTFIND_IsBohrungP5akopf(root));
    bp+=sprintf(buf+bp," ro=%d",   (int)_Z13GTFIND_IsRohrP5akopf(root));
    bp+=sprintf(buf+bp," st=%d",   (int)_Z15GTFIND_IsStangeP5akopf(root));
    bp+=sprintf(buf+bp," ta=%d",   (int)_Z15GTFIND_IsTascheP5akopf(root));
    bp+=sprintf(buf+bp," rt=%d",   _Z20GTFIND_IsRucksackTypP6geotec9basvar_at(root,bv));
    bp+=sprintf(buf+bp," gk=%d",   _Z20GTFIND_IsGeoKomplettP6geotec(root));
    bp+=sprintf(buf+bp," ge=%d",   _Z17GTFIND_IsGeoErrorP6geotec(root));
    bp+=sprintf(buf+bp," li=%d",   _Z13GTFIND_IsLineP6geotec(root));
    bp+=sprintf(buf+bp," ci=%d\n", _Z13GTFIND_IsCircP6geotec(root));
    write(1, buf, bp);
    idx++;
  }
  return 0;
}
