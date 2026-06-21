/* libEp90_Aeplib — 6 flat-field leaf fns, native ARM64/x86_64 recompile.
 * Decompiled with IDA 9.2 off genuine i386 libEp90_Aeplib.so. These read/write
 * a geotec by RAW byte offset and chase NO internal pointers, so raw-offset
 * access on a shared flat buffer is arch-independent (no per-arch struct needed).
 */
typedef unsigned int   u32;
typedef unsigned char  u8;
#define I(p,o) (*(int*)((char*)(p)+(o)))
#define U(p,o) (*(u32*)((char*)(p)+(o)))
#define B(p,o) (*(u8*)((char*)(p)+(o)))
#define D(p,o) (*(double*)((char*)(p)+(o)))

/* number of bits to represent a1 (0->0, 1->1, 0xFF->8) */
int _Z23AEPLIB_SchlittenInKanalh(u8 a1){ int v=0; while(a1){ ++v; a1>>=1; } return v; }

/* multi-spindle set membership: a1 in {1,2,6,9,10} */
_Bool _Z12MehrSpindlerh(u8 a1){ return a1<=0xAu && ((1<<a1)&0x646)!=0; }

/* infeed-active: typ==1 && flag bit && a combined bit-6 test */
unsigned _Z14chk_zustellungP6geotec(void *a1){
  if ( a1 && I(a1,84)==1 && (B(a1,94)&1)!=0 )
    return ((U(a1,220) & U(a1,216)) >> 6) & 1;
  return 0;
}

/* feed type: (byte@108) % 10 when typ==1 */
int _Z15AEP_VorschubTypP6geotec(void *a1){
  if ( a1 && I(a1,84)==1 ) return B(a1,108) % 10;
  return 0;
}

/* "do not machine this element": typ==1 && byte@108 in [10,19] */
int _Z26AEP_ElementNichtBearbeitenP6geotec(void *a1){
  int v=0;
  if ( !a1 || I(a1,84)!=1 ) return 0;
  v = (u8)(B(a1,108)-10) <= 9u;
  return v;
}

/* clear oversize-info: double@100=0, byte@120 = (old&0x3F)|0x40; returns new byte */
int _Z14AEP_set_ovsi_0P6geotec(void *a1){
  int result=0;
  if ( a1 ){
    D(a1,100) = 0.0;
    result = (B(a1,120) & 0x3F) | 0x40;
    B(a1,120) = (u8)result;
  }
  return result;
}
