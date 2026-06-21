/*
 * libEp90_Aequi_rebuilt.c
 *
 * BEHAVIORAL reimplementation (native ARM64) of the leaf functions of the
 * proprietary i386 libEp90_Aequi.so (equidistant/offset-curve helpers):
 *   - get_laengentoleranz / AEQ_GetLaengentoleranz : length-tolerance accessors
 *     (identity on the 2nd argument in this build);
 *   - anz_same_level : count the nodes of an akopf "same level" singly-linked
 *     list, walking the next-pointer at field offset +4 (a pointer chaser, so
 *     the harness builds the list per-arch and the integer count is compared).
 *
 * Source of truth: work/re/out/libEp90_Aequi.so.decomp.c (Ghidra).
 */

/* akopf node: a "next" pointer at field offset +4(i386)/+8(arm). Mirroring the
 * field order reproduces the i386 byte offset and gives ARM its native one. */
typedef struct Akopf { void *f0; struct Akopf *next; } Akopf;

/* get_laengentoleranz(d1, d2) -> d2 */
double get_laengentoleranz(double d1, double d2) __asm__("_Z19get_laengentoleranzdd");
double get_laengentoleranz(double d1, double d2) { (void)d1; return d2; }

/* AEQ_GetLaengentoleranz(d1, d2) -> d2 */
double AEQ_GetLaengentoleranz(double d1, double d2) __asm__("_Z22AEQ_GetLaengentoleranzdd");
double AEQ_GetLaengentoleranz(double d1, double d2) { (void)d1; return d2; }

/* anz_same_level(head) -> number of nodes reachable via ->next */
int anz_same_level(const void *head) __asm__("_Z14anz_same_levelP5akopf");
int anz_same_level(const void *head)
{
    int n = 0;
    for (const Akopf *p = (const Akopf *)head; p != 0; p = p->next)
        n++;
    return n;
}
