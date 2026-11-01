// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>
//
// Internal utterance representation shared by the parser, prosody and both
// voices. All storage is fixed size: one sp_utt holds one sentence (or a
// chunk of a very long one, split at a word boundary).

#ifndef SPEECH_SPEECH_H
#define SPEECH_SPEECH_H

#include <stddef.h>
#include <stdint.h>
#include "phoneme.h"
#include "amiga_speech/narrator.h"

#define SP_MAX_SEGS 600
#define SP_MAX_F0   1200

/* segment flags */
#define SF_WSTART  0x01   /* first segment of a word */
#define SF_WEND    0x02   /* last segment of a word */
#define SF_NP      0x04   /* inside ( ) noun phrase */
#define SF_FINALSYL 0x08  /* in the last syllable before a phrase break */
#define SF_POLY    0x10   /* word has more than one syllable */
#define SF_WFINALSYL 0x20 /* in the word's last syllable */
#define SF_ASPIRATE 0x40  /* voiceless stop gets aspiration */
#define SF_FLAP    0x80   /* reserved */

typedef struct {
    uint8_t  ph;       /* PH_* */
    uint8_t  stress;   /* 0 = unstressed, 1..9 */
    uint8_t  punct;    /* for PH_PAU: '.', '?', ',', '-', 0 = edge silence */
    uint8_t  flags;
    uint16_t dur;      /* ms, set by prosody */
    uint16_t asp;      /* ms of aspiration after a voiceless stop (in dur) */
    uint32_t t0;       /* start, ms from utterance start */
} sp_seg;

typedef struct {
    uint32_t t;        /* ms */
    float    hz;
} sp_f0pt;

typedef struct {
    int      nseg;
    sp_seg   seg[SP_MAX_SEGS];
    int      nf0;
    sp_f0pt  f0[SP_MAX_F0];
    uint32_t total_ms;
    char     final_punct; /* '.', '?' or '-' */
} sp_utt;

/* Parser: reads from *pos up to the end of one sentence (or a safe split
   point). Returns 1 if a sentence was produced, 0 at end of input, or an
   ND_* error. */
int sp_parse_sentence(const char *s, size_t len, size_t *pos, sp_utt *u,
                      size_t *err_pos);

/* Apply narrator allophone rules (post-vocalic L/R, flaps, glottal stops,
   aspiration marks). */
void sp_allophones(sp_utt *u);

/* Prosody: durations and F0 targets. */
void sp_prosody(sp_utt *u, const nar_params *p);

/* Voices: render one prosodic utterance, emitting samples at
   NAR_OUTPUT_RATE. Return 0, ND_NoMem, or 1 if the sink aborted. */
int voice_classic_render(const sp_utt *u, const nar_params *p,
                         nar_sink_fn sink, void *user);
int voice_natural_render(const sp_utt *u, const nar_params *p,
                         nar_sink_fn sink, void *user);

/* deterministic PRNG (same output on every platform) */
typedef struct { uint32_t s; } sp_rng;
static inline uint32_t sp_rand(sp_rng *r)
{
    r->s = r->s * 1664525u + 1013904223u;
    return r->s;
}
/* uniform in [-1, 1) */
static inline float sp_randf(sp_rng *r)
{
    return (float)((int32_t)(sp_rand(r) >> 8) - 0x800000) / (float)0x800000;
}

#endif
