/* libEp90_Gtlib — 11 NEW leaf classifiers, native ARM64/x86_64 recompile.
 * Decompiled with IDA 9.2 (idalib) off the genuine i386 libEp90_Gtlib.so.
 *
 * These are multi-level pointer-chasers: an akopf/geotec node links via a
 * pointer at +16 (and +20) to child nodes, ending at a type field at +84
 * (flags at +92, a discriminator at +28). The genuine i386 code reads those
 * raw BYTE offsets; we mirror them with a per-arch named-field struct so the
 * compiler reproduces the i386 4-byte-pointer layout under -m32 and the
 * 8-byte layout natively — the functions navigate by field NAME, identical
 * logic on both. (per-arch-native-object technique; cf. dkomp/metaval.)
 */
#include <stddef.h>

typedef struct node {
  char pad0[16];      /* 0..15  */
  struct node *p16;   /* +16  child pointer            */
  struct node *p20;   /* +20  second pointer (IsTasche)*/
  char pad1[4];       /* +24                            */
  int  f28;           /* +28  discriminator (Rohr/Stange) */
  char pad2[52];      /* +32..83                        */
  int  typ;           /* +84  element/group type        */
  int  pad3;          /* +88                            */
  int  flags;         /* +92  status flags              */
} node;

/* --- akopf type-classifiers (3-level chain a->p16->p16->typ) --- */
int _Z16GTFIND_IsAbflachP5akopf(node *a){
  int v=0; if(a){ node *m=a->p16; if(m){ node *l=m->p16; if(l) v=(l->typ==59);} } return v; }
int _Z17GTFIND_IsMehrkantP5akopf(node *a){
  int v=0; if(a){ node *m=a->p16; if(m){ node *l=m->p16; if(l) v=(l->typ==58);} } return v; }
int _Z15GTFIND_IsMusterP5akopf(node *a){
  int v=0; if(a){ node *m=a->p16; if(m){ node *l=m->p16; if(l) v=(l->typ==20);} } return v; }

_Bool _Z14GTFIND_IsFigurP5akopf(node *a){
  if(a){ node *m=a->p16; if(m){ node *l=m->p16; if(l){ unsigned v=l->typ;
    if(v<=0x13) return ((1u<<v)&0xD2800u)!=0; } } } return 0; }

/* akopf 2-level: a->p16->typ */
int _Z16GTFIND_IsBohrungP5akopf(node *a){
  int v=0; if(a){ node *m=a->p16; if(m) v=(m->typ==21); } return v; }

/* Rohr/Stange: gate on a->f28 in {36,6}, then a->p16->p16->typ == 6 / 7 */
_Bool _Z13GTFIND_IsRohrP5akopf(node *a){
  _Bool r=0; if(a){ r=(a->f28==36||a->f28==6);
    if(r){ node *m=a->p16; r=0; if(m){ node *l=m->p16; if(l) return l->typ==6; } } } return r; }
_Bool _Z15GTFIND_IsStangeP5akopf(node *a){
  _Bool r=0; if(a){ r=(a->f28==36||a->f28==6);
    if(r){ node *m=a->p16; r=0; if(m){ node *l=m->p16; if(l) return l->typ==7; } } } return r; }

/* IsTasche: a->p16 && a->p20 && a->p16->typ==1 && !Figur && !Abflach && !Mehrkant */
_Bool _Z15GTFIND_IsTascheP5akopf(node *a){
  return a && a->p16 && a->p20 && a->p16->typ==1
    && !_Z14GTFIND_IsFigurP5akopf(a)
    && !(unsigned char)_Z16GTFIND_IsAbflachP5akopf(a)
    && (unsigned char)_Z17GTFIND_IsMehrkantP5akopf(a)==0; }

/* geotec: g->p16->typ == basvar arg */
int _Z20GTFIND_IsRucksackTypP6geotec9basvar_at(node *g, int bv){
  int v=0; if(g){ node *c=g->p16; if(c) return c->typ==bv; } return v; }

/* geotec: IsVariante(g,1) (== g->typ==1) then a flags bit */
int _Z20GTFIND_IsGeoKomplettP6geotec(node *g){
  int r=(g && g->typ==1); if((char)r) return (g->flags>>1)&1; return r; }
int _Z17GTFIND_IsGeoErrorP6geotec(node *g){
  int r=(g && g->typ==1); if((char)r) return (g->flags>>3)&1; return r; }

/* geotec line/circle: IsVariante(g,1) then flags bit 5 / 6 */
int _Z13GTFIND_IsLineP6geotec(node *g){
  int r=(g && g->typ==1); if((char)r) return (g->flags>>5)&1; return r; }
int _Z13GTFIND_IsCircP6geotec(node *g){
  int r=(g && g->typ==1); if((char)r) return (g->flags>>6)&1; return r; }
