// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>
//
// The handful of C library functions the speech core and the vendored
// Flite subset need, for code that runs without a C runtime (inside
// narrator.device / translator.library on AmigaOS 4). Memory comes from
// IExec->AllocVecTags(MEMF_SHARED), so blocks may be freed by any task.
// The owner must set ml_IExec before anything allocates.

#include <exec/types.h>
#include <exec/memory.h>
#include <interfaces/exec.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

struct ExecIFace *ml_IExec;

/* ---- memory ---- */

typedef struct { size_t size; size_t pad; } mhdr;   /* keeps 8-byte alignment */

void *malloc(size_t n)
{
    mhdr *h;
    if (!ml_IExec) return NULL;
    h = (mhdr *)ml_IExec->AllocVecTags((uint32)(n + sizeof(mhdr)), AVT_Type, MEMF_SHARED, TAG_END);
    if (!h) return NULL;
    h->size = n;
    return h + 1;
}

void free(void *p)
{
    if (p && ml_IExec) ml_IExec->FreeVec((mhdr *)p - 1);
}

void *memset(void *d, int c, size_t n);
void *memcpy(void *d, const void *s, size_t n);

void *calloc(size_t a, size_t b)
{
    void *p = malloc(a * b);
    if (p) memset(p, 0, a * b);
    return p;
}

void *realloc(void *p, size_t n)
{
    void *q;
    size_t old;
    if (!p) return malloc(n);
    old = ((mhdr *)p - 1)->size;
    if (n <= old) return p;
    q = malloc(n);
    if (!q) return NULL;
    memcpy(q, p, old);
    free(p);
    return q;
}

/* ---- memory and strings ---- */

void *memset(void *d, int c, size_t n)
{
    unsigned char *p = (unsigned char *)d;
    while (n--) *p++ = (unsigned char)c;
    return d;
}

void *memcpy(void *d, const void *s, size_t n)
{
    unsigned char *a = (unsigned char *)d;
    const unsigned char *b = (const unsigned char *)s;
    if (!(((uintptr_t)a | (uintptr_t)b | n) & 3)) {
        uint32_t *wa = (uint32_t *)a;
        const uint32_t *wb = (const uint32_t *)b;
        n >>= 2;
        while (n--) *wa++ = *wb++;
        return d;
    }
    while (n--) *a++ = *b++;
    return d;
}

void *memmove(void *d, const void *s, size_t n)
{
    unsigned char *a = (unsigned char *)d;
    const unsigned char *b = (const unsigned char *)s;
    if (a == b || n == 0) return d;
    if (a < b) { while (n--) *a++ = *b++; }
    else { a += n; b += n; while (n--) *--a = *--b; }
    return d;
}

int memcmp(const void *x, const void *y, size_t n)
{
    const unsigned char *a = (const unsigned char *)x, *b = (const unsigned char *)y;
    for (; n; n--, a++, b++) if (*a != *b) return *a - *b;
    return 0;
}

size_t strlen(const char *s) { const char *p = s; while (*p) p++; return (size_t)(p - s); }

int strcmp(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

int strncmp(const char *a, const char *b, size_t n)
{
    for (; n; n--, a++, b++) {
        if (*a != *b) return (unsigned char)*a - (unsigned char)*b;
        if (!*a) return 0;
    }
    return 0;
}

char *strcpy(char *d, const char *s) { char *r = d; while ((*d++ = *s++)) ; return r; }

char *strncpy(char *d, const char *s, size_t n)
{
    size_t i = 0;
    for (; i < n && s[i]; i++) d[i] = s[i];
    for (; i < n; i++) d[i] = 0;
    return d;
}

char *strcat(char *d, const char *s) { strcpy(d + strlen(d), s); return d; }

char *strncat(char *d, const char *s, size_t n)
{
    char *p = d + strlen(d);
    while (n-- && *s) *p++ = *s++;
    *p = 0;
    return d;
}

char *strchr(const char *s, int c)
{
    for (;; s++) { if (*s == (char)c) return (char *)s; if (!*s) return NULL; }
}

char *strrchr(const char *s, int c)
{
    const char *r = NULL;
    for (;; s++) { if (*s == (char)c) r = s; if (!*s) return (char *)r; }
}

char *strstr(const char *h, const char *n)
{
    size_t l = strlen(n);
    if (!l) return (char *)h;
    for (; *h; h++) if (*h == *n && strncmp(h, n, l) == 0) return (char *)h;
    return NULL;
}

int atoi(const char *s)
{
    int neg = 0, v = 0;
    while (*s == ' ' || *s == '\t') s++;
    if (*s == '-' || *s == '+') neg = (*s++ == '-');
    while (*s >= '0' && *s <= '9') v = v * 10 + (*s++ - '0');
    return neg ? -v : v;
}

double atof(const char *s)
{
    double v = 0, f = 0.1;
    int neg = 0;
    while (*s == ' ' || *s == '\t') s++;
    if (*s == '-' || *s == '+') neg = (*s++ == '-');
    while (*s >= '0' && *s <= '9') v = v * 10 + (*s++ - '0');
    if (*s == '.') for (s++; *s >= '0' && *s <= '9'; s++, f *= 0.1) v += (*s - '0') * f;
    return neg ? -v : v;
}

static unsigned long ml_seed = 1;
int rand(void) { ml_seed = ml_seed * 1103515245UL + 12345UL; return (int)((ml_seed >> 16) & 0x7fff); }

/* formatted output: see ml_printf.c */

int abs(int x) { return x < 0 ? -x : x; }
long labs(long x) { return x < 0 ? -x : x; }
double fabs(double x) { return x < 0 ? -x : x; }
float fabsf(float x) { return x < 0 ? -x : x; }
