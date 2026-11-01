// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>
//
// Cascade/parallel formant synthesizer in the style of Klatt (1980) with a
// KLGLOTT88-like glottal source (open quotient, spectral tilt, pitch-
// synchronous aspiration, jitter/shimmer). Written from the published
// descriptions; no code from other synthesizers.

#ifndef SPEECH_KLATT_H
#define SPEECH_KLATT_H

#include <stdint.h>

#define KL_NCASC 6     /* cascade formants F1..F6 */
#define KL_NPAR  6     /* parallel formants F1..F6 (F1 unused for frication) */

typedef struct {
    float f0;                  /* Hz */
    float av, ah, af, ab;      /* linear amplitudes: voicing, aspiration,
                                  frication, parallel bypass */
    float f[KL_NPAR], b[KL_NPAR];
    float pa[KL_NPAR];         /* parallel amplitudes (linear) */
    float fnp, bnp, fnz, bnz;  /* nasal pole / zero */
    float oq;                  /* open quotient 0.3..0.8 */
    float tilt;                /* source low-pass pole 0..0.95 */
    float adj[3];              /* A1..A3 adjustments, linear gain (1 = none) */
} kl_frame;

typedef struct { float a, b, c, y1, y2; } kl_res;

typedef struct {
    float sr;
    kl_res casc[KL_NCASC];
    kl_res par[KL_NPAR];
    kl_res hp[3];              /* fixed higher poles F7..F9 */
    kl_res np;                 /* nasal pole */
    float nz_a, nz_b, nz_c, nz_x1, nz_x2; /* nasal zero (antiresonator) */
    kl_res peak[3];            /* A1..A3 adjustment band-passes */
    /* glottal source state */
    float period_pos;          /* samples into current period */
    float T0;                  /* current period length, samples */
    float Te;                  /* open phase length, samples */
    float u_prev;              /* previous flow sample */
    float tilt_y;
    float shimmer;
    float noise_lp;
    float fric_prev;
    int   voicing;             /* currently inside a voiced stretch */
    uint32_t rng;
} klatt;

void kl_init(klatt *k, float sample_rate, uint32_t seed);

/* Render n samples. Resonator coefficients come from `fr`; amplitudes ramp
   linearly from `prev` to `fr` across the block (prev may equal fr). */
void kl_render(klatt *k, const kl_frame *prev, const kl_frame *fr,
               int n, float *out);

#endif
