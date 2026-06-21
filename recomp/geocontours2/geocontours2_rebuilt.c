/* libgeolibcontours — 2 AssumePropertiesOf flat-copy methods, native recompile.
 * Decompiled with IDA 9.2 off genuine i386 libgeolibcontours.so. Copy selected
 * inline fields from a source object (a2) into this; raw byte offsets. */
typedef unsigned int u32;
#define U(p,o) (*(u32*)((char*)(p)+(o)))
#define B(p,o) (*(unsigned char*)((char*)(p)+(o)))

void *_ZN6Geolib7Contour18AssumePropertiesOfERKS0_(void *t, const void *a2){
  B(t,4)  = B(a2,4);
  B(t,12) = B(a2,12);
  U(t,8)  = U(a2,8);
  return t;
}
int _ZN6Geolib13PocketContour18AssumePropertiesOfERKS0_(void *t, const void *a2){
  _ZN6Geolib7Contour18AssumePropertiesOfERKS0_(t, a2);
  int was_zero = (B(t,60)==0);
  B(t,64) = B(a2,64);
  U(t,68) = U(a2,68); U(t,72) = U(a2,72); U(t,76) = U(a2,76);
  U(t,80) = U(a2,80); U(t,84) = U(a2,84);
  int result = B(a2,88);
  B(t,88) = (unsigned char)result;
  if ( !was_zero ) B(t,60) = 0;
  return result;
}
