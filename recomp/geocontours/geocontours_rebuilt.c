/* libgeolibcontours — 6 C++ flat-this leaf methods, native recompile.
 * Decompiled with IDA 9.2 off genuine i386 libgeolibcontours.so. Each reads/writes
 * flat fields of the object passed as `this` by raw byte offset (u32 reads stay
 * 4-byte on both arches); PocketsDefined compares the head field to `this` itself
 * (self-referential empty-list test) — reproduced as a pointer compare so the
 * harness builds the self-reference per-arch. C++ mangled symbols. */
typedef unsigned int  u32;
typedef unsigned char u8;
#define U(p,o) (*(u32*)((char*)(p)+(o)))
#define B(p,o) (*(u8*)((char*)(p)+(o)))

void *_ZN6Geolib14ContourElement20disallowFeedAdaptionEv(void *th){ B(th,31)=1; return th; }
int   _ZNK6Geolib14ContourElement21isFeedAdaptionAllowedEv(void *th){ return B(th,31) ^ 1; }
_Bool _ZNK6Geolib7Contour18IsToolCorrPastOrToEv(void *th){ return (u32)(U(th,8)-2) <= 1; }
_Bool _ZNK6Geolib7Contour12IsCorrFwdBwdEv(void *th){ return (u32)(U(th,8)-2) <= 2; }
int   _ZNK6Geolib13PocketContour12OffsetResult7isEmptyEv(void *th){
  int v=0; if ( U(th,0)==U(th,4) ) v = (U(th,16)==U(th,12)); return v;
}
_Bool _ZNK6Geolib14PocketContours14PocketsDefinedEv(void *th){ return *(void**)th != th; }
