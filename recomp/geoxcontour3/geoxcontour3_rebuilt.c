/* libgeoextendedcontour — GeometryTools::is_value_inside_range, native recompile.
 * Recovered by DISASSEMBLY (hexrays output was garbled): comisd hi,v / jbe ->0,
 * then setnbe(v,lo). Strict open interval lo < v < hi. Args: (value, lo, hi). */
_Bool _ZN13GeometryTools21is_value_inside_rangeEddd(double v, double lo, double hi){
  return hi > v && v > lo;
}
