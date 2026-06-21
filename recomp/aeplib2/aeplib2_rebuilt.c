/* libEp90_Aeplib — 3 Bam list-mutators (linked-list walkers), native recompile.
 * Decompiled with IDA 9.2 off genuine i386 libEp90_Aeplib.so. Walk a list from
 * a1 via the +12 link, applying =/|=/&=~ a3 to the +132 field of each node, using
 * a2 as a sentinel (temporarily nulling a2->link to stop the walk at a2, then
 * restoring it). Per-arch named-field struct reproduces the i386 +12/+132 layout
 * under -m32 and the natural layout otherwise; nodes navigated by name. */
typedef struct node {
  char pad0[12];        /* 0..11 */
  struct node *link;    /* i386 +12 */
  char pad1[116];       /* i386 +16..131 */
  int  bam;             /* i386 +132 */
} node;

int _Z10AEP_SetBamP6geotecS0_m(node *a1, node *a2, unsigned a3){
  node *r=a1, *v4=a2->link;
  for ( a2->link=0; r; r=r->link ) r->bam = a3;
  a2->link=v4; return 0;
}
int _Z10AEP_AddBamP6geotecS0_m(node *a1, node *a2, unsigned a3){
  node *r=a1, *v4=a2->link;
  for ( a2->link=0; r; r=r->link ) r->bam |= a3;
  a2->link=v4; return 0;
}
int _Z10AEP_DelBamP6geotecS0_m(node *a1, node *a2, unsigned a3){
  node *r=a1, *v4=a2->link;
  for ( a2->link=0; r; r=r->link ) r->bam &= ~a3;
  a2->link=v4; return 0;
}
