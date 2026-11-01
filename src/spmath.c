// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

#include "spmath.h"

#define LN2   0.69314718055994530942
#define LN10  2.30258509299404568402
#define PI    3.14159265358979323846
#define TWOPI 6.28318530717958647692

static double exp_d(double x)
{
    double r, t, s;
    int k, i;
    if (x > 700.0) x = 700.0;
    if (x < -700.0) return 0.0;
    /* x = k ln2 + r, |r| <= ln2/2 */
    k = (int)(x / LN2 + (x >= 0 ? 0.5 : -0.5));
    r = x - (double)k * LN2;
    /* Taylor series of e^r to r^13 */
    s = 1.0;
    t = 1.0;
    for (i = 1; i <= 13; i++) {
        t *= r / (double)i;
        s += t;
    }
    /* multiply by 2^k */
    while (k > 0) { int e = k > 30 ? 30 : k; s *= (double)(1L << e); k -= e; }
    while (k < 0) { int e = -k > 30 ? 30 : -k; s /= (double)(1L << e); k += e; }
    return s;
}

float sp_expf(float x) { return (float)exp_d((double)x); }
float sp_exp10f(float x) { return (float)exp_d((double)x * LN10); }

/* sin on [-pi/2, pi/2] */
static double sin_core(double x)
{
    double x2 = x * x, t = x, s = x;
    int i;
    for (i = 1; i <= 9; i++) {
        t *= -x2 / (double)((2 * i) * (2 * i + 1));
        s += t;
    }
    return s;
}

static double sin_d(double x)
{
    double n = x / TWOPI;
    long k = (long)(n >= 0 ? n + 0.5 : n - 0.5);
    x -= (double)k * TWOPI;              /* now in [-pi, pi] */
    if (x > PI / 2) x = PI - x;
    else if (x < -PI / 2) x = -PI - x;
    return sin_core(x);
}

float sp_sinf(float x) { return (float)sin_d((double)x); }
float sp_cosf(float x) { return (float)sin_d((double)x + PI / 2); }

float sp_sqrtf(float xf)
{
    double x = xf, g;
    int i;
    if (x <= 0.0) return 0.0f;
    /* Newton from a guess above the root (converges monotonically) */
    g = x > 1.0 ? x : 1.0;
    for (i = 0; i < 60; i++) {
        double ng = 0.5 * (g + x / g);
        if (ng == g) break;
        g = ng;
    }
    return (float)g;
}
