/*
 * libplccond_partial_rebuilt.c — native re-implementation of the PURE-INTEGER
 * leaf subset of the proprietary i386 libplccond.so (PLC condition-expression
 * evaluator), recovered via Ghidra (work/re/out/libplccond.so.decomp.c) +
 * canonical i386 disassembly.
 *
 * SCOPE: most of libplccond is the condition parser/evaluator (heap, config,
 * HeROS message bus) — NOT a leaf. This file reproduces the self-contained
 * ASCII helpers, the operand "is-null" scanner, and the small fixed-capacity
 * operator/operand STACK whose state lives entirely in a caller-provided flat
 * buffer ({int top; uint16 data[513];}, element[i] at byte offset i*2+4).
 *
 * cdecl, confirmed from disassembly. All comparisons are byte-truncated, so the
 * x86 'signed char' vs ARM 'unsigned char' difference is irrelevant here.
 */
#include <stdint.h>
#include <stdbool.h>

/* ---- ASCII case folding (locale-independent, ASCII-only) ----
 * i386: if ((uint8)((int8)c + 0x9f) < 0x1a) c -= 0x20;   ('a'..'z' -> upper)
 * The (int8) cast feeds a byte-truncated compare, so (uint8)c is equivalent. */
int toupper_ASCII(int c)
{
    if ((unsigned char)((unsigned char)c + 0x9fu) < 0x1au) c = c - 0x20;
    return c;
}

/* i386: if ((uint8)((int8)c + 0xbf) < 0x1a) c += 0x20;   ('A'..'Z' -> lower) */
int tolower_ASCII(int c)
{
    if ((unsigned char)((unsigned char)c + 0xbfu) < 0x1au) c = c + 0x20;
    return c;
}

/* return c == '\\' || c == '/' */
bool IsPathSep(char c)
{
    return c == '\\' || c == '/';
}

/* operand is "null" iff it is empty or consists solely of '0' characters. */
int isNull(const char *p)
{
    char c = *p;
    while (1) {
        if (c == '\0') return 1;
        if (c != '0') break;
        c = p[1];
        p = p + 1;
    }
    return 0;
}

/* ---- fixed-capacity uint16 stack over a caller flat buffer ----
 * struct { int32_t top; uint16_t data[]; };  element i lives at byte i*2 + 4.
 * top == -1 means empty; capacity 513 (indices 0..512). */

/* if p==0 -> empty(1); else 1 when top<0, else 0 (sign bit of top). */
unsigned IsStackEmpty(const void *p)
{
    if (p == 0) return 1u;
    return (uint32_t)(*(const int32_t *)p) >> 31;
}

/* top<0 -> 1; else data[top] zero-extended to 32 bits. */
unsigned PeekStack(const void *p)
{
    int32_t top = *(const int32_t *)p;
    if (top < 0) return 1u;
    return *(const uint16_t *)((const char *)p + top * 2 + 4);
}

/* signed test top <= 0x1ff: push (top++, data[top]=v) and return 1; else 0. */
int PushStack(void *p, uint16_t v)
{
    int32_t top = *(int32_t *)p;
    if (top < 0x200) {
        *(int32_t *)p = top + 1;
        *(uint16_t *)((char *)p + (top + 1) * 2 + 4) = v;
    }
    return top < 0x200;
}

/* top>=0 -> ret=data[top] (zero-extended), top--; else ret=0. */
unsigned PopStack(void *p)
{
    unsigned ret = 0;
    int32_t top = *(int32_t *)p;
    if (top >= 0) {
        ret = *(const uint16_t *)((char *)p + top * 2 + 4);
        *(int32_t *)p = top - 1;
    }
    return ret;
}
