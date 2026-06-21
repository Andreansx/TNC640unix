/* libgeoextendedcontour — 16 C++ flat-this accessor leaves, native recompile.
 * Decompiled with IDA 9.2 off genuine i386 libgeoextendedcontour.so. ValueRange<T>
 * and friends store their fields inline; each accessor reads them by raw byte
 * offset (u32 stay 4-byte, double 8-byte on both arches). Exact double sub for
 * span; DBL_MAX / 0xFFFFFFFF sentinels for empty. C++ mangled symbols. */
typedef unsigned int u32;
#include <float.h>
#define U(p,o) (*(u32*)((char*)(p)+(o)))
#define D(p,o) (*(double*)((char*)(p)+(o)))

/* Geolib::ValueRange<uint> */
int   _ZNK6Geolib10ValueRangeIjE3minEv(void *t){ return U(t,0); }
int   _ZNK6Geolib10ValueRangeIjE3maxEv(void *t){ return U(t,4); }
int   _ZNK6Geolib10ValueRangeIjE4spanEv(void *t){ return U(t,4) - U(t,0); }
_Bool _ZNK6Geolib10ValueRangeIjE5emptyEv(void *t){ return U(t,0)==0xFFFFFFFFu && U(t,4)==0xFFFFFFFFu; }
_Bool _ZNK6Geolib10ValueRangeIjE5validEv(void *t){ return U(t,0) <= U(t,4); }

/* Geolib::ValueRange<double> */
double _ZNK6Geolib10ValueRangeIdE3minEv(void *t){ return D(t,0); }
double _ZNK6Geolib10ValueRangeIdE3maxEv(void *t){ return D(t,8); }
double _ZNK6Geolib10ValueRangeIdE4spanEv(void *t){ return D(t,8) - D(t,0); }
_Bool  _ZNK6Geolib10ValueRangeIdE5emptyEv(void *t){ return D(t,0)==DBL_MAX && D(t,8)==DBL_MAX; }
_Bool  _ZNK6Geolib10ValueRangeIdE5validEv(void *t){ return D(t,8) >= D(t,0); }

/* Geolib::SplittableValueRange */
double _ZNK6Geolib20SplittableValueRange15get_range_startEv(void *t){ return D(t,0); }
double _ZNK6Geolib20SplittableValueRange13get_range_endEv(void *t){ return D(t,8); }
double _ZNK6Geolib20SplittableValueRange20get_sample_step_sizeEv(void *t){ return D(t,16); }
int    _ZNK6Geolib20SplittableValueRange21get_number_of_samplesEv(void *t){ return U(t,24); }

/* FixedGridHash */
double _ZNK13FixedGridHash9cell_sizeEv(void *t){ return D(t,28); }
int    _ZNK13FixedGridHash10cell_countEv(void *t){ return U(t,0); }
