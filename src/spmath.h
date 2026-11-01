// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>
//
// The few transcendental functions the synthesizers need, implemented here
// so the core has no libm dependency (it runs inside a device on AmigaOS 4
// without a C runtime) and gives the same results on every platform.
// Computed in double; accurate to well under 1e-7 relative.

#ifndef SPEECH_SPMATH_H
#define SPEECH_SPMATH_H

#include <stdint.h>

float sp_expf(float x);
float sp_exp10f(float x);   /* 10^x */
float sp_sinf(float x);
float sp_cosf(float x);
float sp_sqrtf(float x);
/* round to nearest, halves away from zero */
static inline long sp_lround(float x) { return x >= 0.0f ? (long)(x + 0.5f) : -(long)(0.5f - x); }

#endif
