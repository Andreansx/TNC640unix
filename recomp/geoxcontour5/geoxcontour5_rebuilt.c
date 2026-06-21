/* libgeoextendedcontour — GeometryTools::isAngleBetween, native recompile.
 * Decompiled with IDA 9.2 off genuine i386 libgeoextendedcontour.so. Pure
 * comparison predicate over 3 angle refs (a,b,c) and a tolerance d; only exact
 * +/- and compares -> byte-identical. First ref is the implicit `this`. */
_Bool _ZN13GeometryTools14isAngleBetweenERKdS1_S1_d(const double *a, const double *b, const double *c, double d){
  double v4 = *a - d;
  double v5 = *c;
  double v6 = d + *b;
  if ( (v5 > v4) == (v6 > v5) && v6 > v4 ) return 1;
  if ( v5 <= v4 && v6 > v5 ) return v6 <= v4;
  _Bool result = (*c > v4 && v6 <= *c);
  if ( result ) return v6 <= v4;
  return result;
}
