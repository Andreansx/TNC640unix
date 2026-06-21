/* libEp90_SpurGen — 2 pure leaf fns, native ARM64/x86_64 recompile.
 * Decompiled with IDA 9.2 off genuine i386 libEp90_SpurGen.so. (Most SpurGen
 * exports touch unexported globals / the stack-VM and aren't verifiable; these
 * two are self-contained.) */

/* in-place reverse of the first a2 bytes; returns final loop counter (a2/2), or
 * a2 when a2<=1. Reproduced verbatim incl. the i386 pointer walk. */
int _Z5SwapNPci(char *a1, int a2){
  int result = a2;
  if ( a2 > 1 ){
    char *v3 = &a1[a2 - 1];
    result = 0;
    do {
      char v4 = a1[result];
      char v5 = *v3--;
      a1[result++] = v5;
      v3[1] = v4;
    } while ( result < a2 / 2 );
  }
  return result;
}

/* expand bounding box a1 by a2: min of [0],[1]; max of [2],[3]. Returns a1.
 * (double compares are exact, so the i386 long-double promotion is a no-op.) */
double *Box_erweitern(double *a1, double *a2){
  if ( a1[0] > a2[0] ) a1[0] = a2[0];
  if ( a1[1] > a2[1] ) a1[1] = a2[1];
  if ( a2[2] > a1[2] ) a1[2] = a2[2];
  if ( a2[3] > a1[3] ) a1[3] = a2[3];
  return a1;
}
