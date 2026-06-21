/* libgeoextendedcontour — GeometryTools::normalize_angle, native recompile.
 * Decompiled with IDA 9.2 off genuine i386 libgeoextendedcontour.so. The inline
 * SSE 2^52 round-toward-zero sequence is exactly floor(); one-sided fold
 * (subtract one period if above half). IEEE ops -> bit-exact on x86 (and ARM). */
#include <math.h>
double _ZN13GeometryTools15normalize_angleEdb(double angle, _Bool deg){
  double half   = deg ? 180.0 : 3.141592653589793;
  double period = deg ? 360.0 : 6.283185307179586;
  double q  = floor(angle / period);
  double v8 = angle - q * period;
  if ( v8 > half ) return v8 - period;
  return v8;
}
