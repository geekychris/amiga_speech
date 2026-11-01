// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

#include "klatt.h"
#include <math.h>
#include <string.h>

#define PI_F 3.14159265f

static float knoise(klatt *k)
{
    k->rng = k->rng * 1664525u + 1013904223u;
    return (float)((int32_t)(k->rng >> 8) - 0x800000) / (float)0x800000;
}

/* two-pole resonator, unity gain at DC */
static void res_set(kl_res *r, float f, float bw, float sr)
{
    float rr = expf(-PI_F * bw / sr);
    r->c = -rr * rr;
    r->b = 2.0f * rr * cosf(2.0f * PI_F * f / sr);
    r->a = 1.0f - r->b - r->c;
}

/* two-pole resonator, unity gain at its centre frequency */
static void res_set_peak(kl_res *r, float f, float bw, float sr)
{
    float th = 2.0f * PI_F * f / sr, re, im;
    float rr = expf(-PI_F * bw / sr);
    r->c = -rr * rr;
    r->b = 2.0f * rr * cosf(th);
    /* |1 - b e^-jw - c e^-2jw| at w = th */
    re = 1.0f - r->b * cosf(th) - r->c * cosf(2.0f * th);
    im = r->b * sinf(th) + r->c * sinf(2.0f * th);
    r->a = sqrtf(re * re + im * im);
}

static inline float res_run(kl_res *r, float x)
{
    float y = r->a * x + r->b * r->y1 + r->c * r->y2;
    r->y2 = r->y1;
    r->y1 = y;
    return y;
}

void kl_init(klatt *k, float sample_rate, uint32_t seed)
{
    memset(k, 0, sizeof *k);
    k->sr = sample_rate;
    k->rng = seed ? seed : 1u;
    k->T0 = sample_rate / 110.0f;
    k->Te = k->T0 * 0.5f;
    k->period_pos = 0.0f;
    k->shimmer = 1.0f;
    /* higher-pole correction: fixed poles above F6 keep the spectrum from
       collapsing above 5 kHz at a 22 kHz sample rate */
    res_set(&k->hp[0], 5900.0f, 500.0f, sample_rate);
    res_set(&k->hp[1], 6900.0f, 700.0f, sample_rate);
    res_set(&k->hp[2], 7900.0f, 900.0f, sample_rate);
}

static void set_coeffs(klatt *k, const kl_frame *fr)
{
    int i;
    float nyq = k->sr * 0.45f;
    for (i = 0; i < KL_NCASC; i++) {
        float f = fr->f[i] < nyq ? fr->f[i] : nyq;
        res_set(&k->casc[i], f, fr->b[i], k->sr);
    }
    for (i = 1; i < KL_NPAR; i++) {
        float f = fr->f[i] < nyq ? fr->f[i] : nyq;
        res_set_peak(&k->par[i], f, fr->b[i], k->sr);
    }
    res_set(&k->np, fr->fnp, fr->bnp, k->sr);
    {
        kl_res z;
        res_set(&z, fr->fnz, fr->bnz, k->sr);
        k->nz_a = 1.0f / z.a;
        k->nz_b = -z.b / z.a;
        k->nz_c = -z.c / z.a;
    }
    for (i = 0; i < 3; i++)
        res_set_peak(&k->peak[i], fr->f[i], fr->b[i] * 1.5f + 40.0f, k->sr);
}

void kl_render(klatt *k, const kl_frame *prev, const kl_frame *fr,
               int n, float *out)
{
    int i, j;
    float inv = n > 0 ? 1.0f / (float)n : 0.0f;
    int use_adj = fr->adj[0] != 1.0f || fr->adj[1] != 1.0f || fr->adj[2] != 1.0f;

    set_coeffs(k, fr);

    for (i = 0; i < n; i++) {
        float w = (float)i * inv;
        float av = prev->av + (fr->av - prev->av) * w;
        float ah = prev->ah + (fr->ah - prev->ah) * w;
        float af = prev->af + (fr->af - prev->af) * w;
        float ab = prev->ab + (fr->ab - prev->ab) * w;
        float f0 = prev->f0 + (fr->f0 - prev->f0) * w;
        float glot = 0.0f, src, casc, par, noise, open;

        /* --- glottal source (KLGLOTT88-style flow, differentiated) --- */
        if (k->period_pos >= k->T0) {
            float jit, T0;
            k->period_pos -= k->T0;
            if (f0 < 30.0f) f0 = 30.0f;
            T0 = k->sr / f0;
            jit = 1.0f + 0.006f * knoise(k);      /* ~0.5% jitter */
            k->T0 = T0 * jit;
            k->Te = k->T0 * fr->oq;
            k->shimmer = 1.0f + 0.04f * knoise(k); /* ~3% shimmer */
        }
        open = (k->period_pos < k->Te) ? 1.0f : 0.0f;
        {
            float u = 0.0f;
            if (open > 0.0f) {
                float t = k->period_pos / k->Te;     /* 0..1 */
                u = (27.0f / 4.0f) * t * t * (1.0f - t); /* peak 1 at t=2/3 */
            }
            glot = (u - k->u_prev) * k->Te * (1.0f / 6.75f);
            k->u_prev = u;
        }
        k->period_pos += 1.0f;
        /* spectral tilt */
        k->tilt_y = (1.0f - fr->tilt) * glot + fr->tilt * k->tilt_y;
        glot = k->tilt_y * (1.0f + fr->tilt * 2.0f);

        /* noise: slightly low-passed white */
        {
            float wn = knoise(k);
            k->noise_lp = 0.6f * wn + 0.4f * k->noise_lp;
            noise = k->noise_lp;
        }

        src = glot * av * k->shimmer;
        /* aspiration, louder during the open phase */
        src += noise * ah * (av > 0.01f ? (0.35f + 0.65f * open) : 1.0f);

        /* --- cascade: nasal pole, nasal zero, F5..F1 --- */
        casc = res_run(&k->np, src);
        {
            float y = k->nz_a * casc + k->nz_b * k->nz_x1 + k->nz_c * k->nz_x2;
            k->nz_x2 = k->nz_x1;
            k->nz_x1 = casc;
            casc = y;
        }
        for (j = 2; j >= 0; j--)
            casc = res_run(&k->hp[j], casc);
        for (j = KL_NCASC - 1; j >= 0; j--)
            casc = res_run(&k->casc[j], casc);

        if (use_adj) {
            float e = casc;
            for (j = 0; j < 3; j++)
                e += (fr->adj[j] - 1.0f) * res_run(&k->peak[j], casc);
            casc = e;
        }

        /* --- parallel branch for frication / bursts --- */
        {
            float fn0 = noise * af, fn;
            /* first difference: frication spectrum rises with frequency */
            fn = fn0 - k->fric_prev;
            k->fric_prev = fn0;
            /* voiced frication is modulated by the glottal cycle */
            if (av > 0.01f) fn *= 0.5f + 0.5f * open;
            par = 0.0f;
            for (j = 1; j < KL_NPAR; j++) {
                float a = prev->pa[j] + (fr->pa[j] - prev->pa[j]) * w;
                float y = res_run(&k->par[j], fn);
                par += (j & 1) ? -a * y : a * y;
            }
            par += ab * fn;
        }
        out[i] = casc + par;
    }
}
