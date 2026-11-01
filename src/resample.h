// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>
//
// Streaming windowed-sinc resampler (fixed memory, mono int16).

#ifndef SPEECH_RESAMPLE_H
#define SPEECH_RESAMPLE_H

#include <stddef.h>
#include <stdint.h>
#include "amiga_speech/narrator.h"

#define RS_TAPS 16          /* half-width in input samples */
#define RS_HIST (2 * RS_TAPS + 512)
#define RS_PHASES 128

typedef struct {
    double step;            /* input samples per output sample */
    double pos;             /* position of next output in hist[] coords */
    float  cutoff;          /* normalised low-pass (<= 1) */
    float  hist[RS_HIST];
    float  table[RS_PHASES][2 * RS_TAPS]; /* polyphase kernel */
    int    nhist;
    nar_sink_fn sink;
    void  *user;
    int    aborted;
} resampler;

void rs_init(resampler *r, double in_rate, double out_rate,
             nar_sink_fn sink, void *user);
/* push input; returns non-zero if the downstream sink aborted */
int  rs_push(resampler *r, const int16_t *in, size_t n);
/* flush the tail (pads with zeros) */
int  rs_flush(resampler *r);

#endif
