// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

#include "resample.h"
#include <math.h>
#include <string.h>

#define PI_D 3.14159265358979323846

static void build_table(resampler *r);

void rs_init(resampler *r, double in_rate, double out_rate,
             nar_sink_fn sink, void *user)
{
    memset(r, 0, sizeof *r);
    r->step = in_rate / out_rate;
    r->cutoff = (float)(r->step > 1.0 ? 0.95 / r->step : 0.95);
    r->sink = sink;
    r->user = user;
    build_table(r);
    /* start with RS_TAPS zeros of history so the first output is centred */
    r->nhist = RS_TAPS;
    r->pos = (double)RS_TAPS;
}

static float kernel(float x, float fc);

static void build_table(resampler *r)
{
    int ph, j;
    for (ph = 0; ph < RS_PHASES; ph++) {
        float frac = (float)ph / RS_PHASES;
        for (j = -RS_TAPS + 1; j <= RS_TAPS; j++)
            r->table[ph][j + RS_TAPS - 1] = kernel((float)j - frac, r->cutoff);
    }
}

static float kernel(float x, float fc)
{
    /* sinc low-pass with a Blackman window over |x| < RS_TAPS */
    float a = (float)PI_D * x * fc, s, wv;
    if (x > -1e-6f && x < 1e-6f) s = fc;
    else s = sinf(a) / ((float)PI_D * x);
    wv = 0.42f + 0.5f * cosf((float)PI_D * x / RS_TAPS)
         + 0.08f * cosf(2.0f * (float)PI_D * x / RS_TAPS);
    return s * wv;
}

static int produce(resampler *r)
{
    int16_t out[128];
    int n = 0;
    while (r->pos + RS_TAPS < (double)r->nhist) {
        int c = (int)r->pos, j;
        int ph = (int)((r->pos - c) * RS_PHASES + 0.5);
        const float *kt;
        float acc = 0.0f;
        if (ph >= RS_PHASES) { ph = 0; c++; }
        kt = r->table[ph];
        for (j = -RS_TAPS + 1; j <= RS_TAPS; j++) {
            int idx = c + j;
            if (idx < 0 || idx >= r->nhist) continue;
            acc += r->hist[idx] * kt[j + RS_TAPS - 1];
        }
        if (acc > 32767.0f) acc = 32767.0f;
        if (acc < -32768.0f) acc = -32768.0f;
        out[n++] = (int16_t)lrintf(acc);
        if (n == 128) {
            if (r->sink(r->user, out, (size_t)n)) return r->aborted = 1;
            n = 0;
        }
        r->pos += r->step;
    }
    if (n && r->sink(r->user, out, (size_t)n)) return r->aborted = 1;
    /* discard consumed history, keep RS_TAPS before the read point */
    {
        int keep_from = (int)r->pos - RS_TAPS;
        if (keep_from > 0) {
            memmove(r->hist, r->hist + keep_from,
                    (size_t)(r->nhist - keep_from) * sizeof(float));
            r->nhist -= keep_from;
            r->pos -= keep_from;
        }
    }
    return 0;
}

int rs_push(resampler *r, const int16_t *in, size_t n)
{
    while (n > 0 && !r->aborted) {
        size_t room = (size_t)(RS_HIST - r->nhist), i;
        size_t take = n < room ? n : room;
        for (i = 0; i < take; i++)
            r->hist[r->nhist + (int)i] = (float)in[i];
        r->nhist += (int)take;
        in += take;
        n -= take;
        if (produce(r)) return 1;
    }
    return r->aborted;
}

int rs_flush(resampler *r)
{
    int16_t z[RS_TAPS * 2];
    memset(z, 0, sizeof z);
    return rs_push(r, z, RS_TAPS * 2);
}
