/* libgeoextendedcontour — 12 C++ setter leaves, native recompile.
 * Decompiled with IDA 9.2 off genuine i386 libgeoextendedcontour.so. Each writes
 * one inline field of the object (`this`) and returns this; raw byte offsets. */
typedef unsigned int u32;
#define D(p,o) (*(double*)((char*)(p)+(o)))
#define U(p,o) (*(u32*)((char*)(p)+(o)))
#define B(p,o) (*(unsigned char*)((char*)(p)+(o)))

void *_ZN13CleaningGroup18min_element_lengthEd(void *t,double a){ D(t,8)=a;  return t; }
void *_ZN13CleaningGroup14min_gap_lengthEd(void *t,double a){ D(t,16)=a; return t; }
void *_ZN13CleaningGroup25max_stretch_length_changeEd(void *t,double a){ D(t,24)=a; return t; }
void *_ZN13CleaningGroup34bridge_gap_with_stretch_if_greaterEd(void *t,double a){ D(t,32)=a; return t; }
void *_ZN13CleaningGroup14colinear_angleEd(void *t,double a){ D(t,40)=a; return t; }
void *_ZN13CleaningGroup23colinear_max_point_distEd(void *t,double a){ D(t,48)=a; return t; }
void *_ZN13CleaningGroup32cocircular_max_circle_differenceEd(void *t,double a){ D(t,56)=a; return t; }
void *_ZN13CleaningGroup17join_respect_tagsEb(void *t,_Bool a){ B(t,64)=(unsigned char)a; return t; }
void *_ZN6Geolib10ValueRangeIjE7set_minEj(void *t,u32 a){ U(t,0)=a; return t; }
void *_ZN6Geolib10ValueRangeIjE7set_maxEj(void *t,u32 a){ U(t,4)=a; return t; }
void *_ZN6Geolib10ValueRangeIdE7set_minEd(void *t,double a){ D(t,0)=a; return t; }
void *_ZN6Geolib10ValueRangeIdE7set_maxEd(void *t,double a){ D(t,8)=a; return t; }
