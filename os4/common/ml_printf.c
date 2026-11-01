// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>
//
// printf family for the freestanding OS4 components (no OS dependencies,
// so tests/test_ml_printf.c can check it against the host C library).

#include <stdarg.h>
#include <stddef.h>

#ifdef ML_PRINTF_PREFIX       /* host test: avoid clashing with libc */
#define vsnprintf ml_vsnprintf
#define snprintf  ml_snprintf
#define vsprintf  ml_vsprintf
#define sprintf   ml_sprintf
#endif

/* %s %d %i %u %x %c %f %g %ld %lu, width and precision (also '*') */

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
        int width = 0, prec = -1, lng = 0, left = 0;
        char padc = ' ';
        char *start;
        if (*f != '%') { put_c(&o, end, *f); continue; }
        f++;
        if (*f == '-') { left = 1; f++; }
        if (*f == '0') { padc = '0'; f++; }
        if (*f == '*') { width = va_arg(ap, int); f++; }
        else while (*f >= '0' && *f <= '9') width = width * 10 + (*f++ - '0');
        if (*f == '.') {
            prec = 0;
            f++;
            if (*f == '*') { prec = va_arg(ap, int); f++; if (prec < 0) prec = -1; }
            else while (*f >= '0' && *f <= '9') prec = prec * 10 + (*f++ - '0');
        }
        while (*f == 'l') { lng = 1; f++; }
        if (left) { padc = ' '; }
        start = o;
        switch (*f) {
        case 's': {
            const char *s = va_arg(ap, const char *);
            int n = 0;
            if (!s) s = "(null)";
            while (s[n] && (prec < 0 || n < prec)) n++;
            if (!left) while (width-- > n) put_c(&o, end, ' ');
            while (n--) put_c(&o, end, *s++);
            break;
        }
        case 'c': put_c(&o, end, (char)va_arg(ap, int)); break;
        case 'd': case 'i': {
            long v = lng ? va_arg(ap, long) : va_arg(ap, int);
            if (v < 0) { put_c(&o, end, '-'); v = -v; if (width) width--; }
            put_u(&o, end, (unsigned long)v, 10, left ? 0 : width, padc);
            break;
        }
        case 'u': put_u(&o, end, lng ? va_arg(ap, unsigned long) : va_arg(ap, unsigned), 10, left ? 0 : width, padc); break;
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
        if (left) {
            int done = (int)(o - start);
            while (done++ < width) put_c(&o, end, ' ');
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

