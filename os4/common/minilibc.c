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

/* ---- formatted output: %s %d %i %u %x %c %f %g %ld %lu, width/precision ---- */

static void put_c(char **o, char *end, char c) { if (*o < end) *(*o)++ = c; }

static void put_u(char **o, char *end, unsigned long v, unsigned base, int width, char padc)
{
    char t[24];
    int n = 0;
    do { t[n++] = "0123456789abcdef"[v % base]; v /= base; } while (v);
    while (width-- > n) put_c(o, end, padc);
    while (n) put_c(o, end, t[--n]);
}

int vsnprintf(char *buf, size_t size, const char *f, va_list ap)
{
    char *o = buf, *end = buf + (size ? size - 1 : 0);
    for (; *f; f++) {
        int width = 0, prec = -1, lng = 0;
        char padc = ' ';
        if (*f != '%') { put_c(&o, end, *f); continue; }
        f++;
        if (*f == '-') f++;
        if (*f == '0') { padc = '0'; f++; }
        while (*f >= '0' && *f <= '9') width = width * 10 + (*f++ - '0');
        if (*f == '.') { prec = 0; f++; while (*f >= '0' && *f <= '9') prec = prec * 10 + (*f++ - '0'); }
        while (*f == 'l') { lng = 1; f++; }
        switch (*f) {
        case 's': {
            const char *s = va_arg(ap, const char *);
            int n = 0;
            if (!s) s = "(null)";
            while (s[n] && (prec < 0 || n < prec)) n++;
            while (width-- > n) put_c(&o, end, ' ');
            while (n--) put_c(&o, end, *s++);
            break;
        }
        case 'c': put_c(&o, end, (char)va_arg(ap, int)); break;
        case 'd': case 'i': {
            long v = lng ? va_arg(ap, long) : va_arg(ap, int);
            if (v < 0) { put_c(&o, end, '-'); v = -v; if (width) width--; }
            put_u(&o, end, (unsigned long)v, 10, width, padc);
            break;
        }
        case 'u': put_u(&o, end, lng ? va_arg(ap, unsigned long) : va_arg(ap, unsigned), 10, width, padc); break;
        case 'x': case 'p': put_u(&o, end, lng || *f == 'p' ? va_arg(ap, unsigned long) : va_arg(ap, unsigned), 16, width, padc); break;
        case 'f': case 'g': case 'e': {
            double v = va_arg(ap, double);
            unsigned long ip;
            int i, p = prec < 0 ? 6 : prec;
            if (v < 0) { put_c(&o, end, '-'); v = -v; }
            ip = (unsigned long)v;
            put_u(&o, end, ip, 10, 0, ' ');
            if (p) {
                put_c(&o, end, '.');
                v -= (double)ip;
                for (i = 0; i < p; i++) { v *= 10; put_c(&o, end, (char)('0' + (int)v % 10)); v -= (int)v; }
            }
            break;
        }
        case '%': put_c(&o, end, '%'); break;
        default: break;
        }
    }
    if (size) *o = 0;
    return (int)(o - buf);
}

int snprintf(char *buf, size_t size, const char *f, ...)
{
    va_list ap;
    int r;
    va_start(ap, f);
    r = vsnprintf(buf, size, f, ap);
    va_end(ap);
    return r;
}

int vsprintf(char *buf, const char *f, va_list ap) { return vsnprintf(buf, 4096, f, ap); }

int sprintf(char *buf, const char *f, ...)
{
    va_list ap;
    int r;
    va_start(ap, f);
    r = vsnprintf(buf, 4096, f, ap);
    va_end(ap);
    return r;
}

int abs(int x) { return x < 0 ? -x : x; }
long labs(long x) { return x < 0 ? -x : x; }
double fabs(double x) { return x < 0 ? -x : x; }
float fabsf(float x) { return x < 0 ? -x : x; }
