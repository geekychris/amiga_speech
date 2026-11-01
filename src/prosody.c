// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>
//
// Prosody shared by both voices:
//  * segment durations: a compact version of Klatt's (1979) duration rules
//    (phrase-final lengthening, non-final / polysyllabic / unstressed
//    shortening, post-vocalic voicing effect, cluster shortening), scaled to
//    the requested words-per-minute rate;
//  * F0: per-phrase declination, accent peaks on stressed vowels (height
//    from the stress digit and F0enthusiasm), continuation rises at commas
//    and dashes, a final fall for '.', a final rise for '?', optional random
//    perturbation (F0perturb). ROBOTICF0 = monotone, MANUALF0 = accents
//    independent of position.

#include "speech.h"
#include <math.h>
#include <string.h>

/* Calibration so that RATE 150 speaks ~150 words per minute on ordinary
   prose (measured on tests/data/sentences.txt #5; the V37 device runs
   ~16% fast: 174 wpm at its default 150). */
#define RATE_CAL 1.12f

static int is_cons(int ph)
{
    return ph != PH_PAU && !ph_is_vowel(ph) && !(ph_table[ph].flags & PF_SILENT);
}

static int pause_ms(int punct)
{
    switch (punct) {
    case '.': return 380;
    case '?': return 380;
    case ',': return 230;
    case '-': return 160;
    default:  return 40;   /* utterance edges */
    }
}

static void durations(sp_utt *u, const nar_params *p)
{
    int i;
    float rate = (float)(p->rate ? p->rate : DEFRATE);
    float scale = RATE_CAL * (float)DEFRATE / rate;
    uint32_t t = 0;

    for (i = 0; i < u->nseg; i++) {
        sp_seg *g = &u->seg[i];
        const ph_info *pi = &ph_table[g->ph];
        float prc = 100.0f, inh = pi->inh, mn = pi->min, d;
        int vowel = ph_is_vowel(g->ph);
        sp_seg *nx = (i + 1 < u->nseg) ? &u->seg[i + 1] : NULL;
        sp_seg *pv = (i > 0) ? &u->seg[i - 1] : NULL;

        g->asp = 0;
        if (g->ph == PH_PAU) {
            d = (float)pause_ms(g->punct);
            /* pauses stretch less than speech with rate */
            g->dur = (uint16_t)(d * (0.5f + 0.5f * scale));
            if (!g->punct) g->dur = 40;
            g->t0 = t;
            t += g->dur;
            continue;
        }

        if (vowel) {
            if (!(g->flags & SF_FINALSYL))
                prc *= 0.62f;                      /* non-phrase-final */
            else
                prc *= 1.25f;                      /* phrase-final lengthening */
            if (!(g->flags & SF_WFINALSYL))
                prc *= 0.88f;                      /* non-word-final */
            if (g->flags & SF_POLY)
                prc *= 0.82f;                      /* polysyllabic */
            if (g->stress == 0) {
                prc *= 0.72f;                      /* unstressed */
                mn *= 0.5f;
            } else if (g->stress >= 7) {
                prc *= 1.3f;                       /* emphatic */
            }
            /* post-vocalic consonant voicing */
            if (nx && is_cons(nx->ph) && (nx->flags & 0) == 0) {
                float k = 1.0f;
                int nf = ph_table[nx->ph].flags;
                if ((nf & PF_FRIC) && (nf & PF_VOICED)) k = 1.45f;
                else if ((nf & PF_STOP) && (nf & PF_VOICED)) k = 1.2f;
                else if (nf & PF_NASAL) k = 0.9f;
                else if ((nf & (PF_STOP | PF_FRIC)) && !(nf & PF_VOICED)) k = 0.75f;
                if (!(g->flags & SF_FINALSYL))
                    k = 1.0f + (k - 1.0f) * 0.5f;
                prc *= k;
            }
            if (nx && ph_is_vowel(nx->ph))
                prc *= 1.15f;                      /* hiatus */
        } else {
            if (!(g->flags & SF_WSTART))
                prc *= 0.85f;                      /* non-initial consonant */
            if (g->flags & SF_FINALSYL)
                prc *= 1.2f;
            if (pv && nx && is_cons(pv->ph) && is_cons(nx->ph) &&
                !(g->flags & (SF_WSTART | SF_WEND)))
                prc *= 0.6f;                       /* inside a cluster */
            else if ((pv && is_cons(pv->ph) && !(g->flags & SF_WSTART)) ||
                     (nx && is_cons(nx->ph) && !(g->flags & SF_WEND)))
                prc *= 0.8f;                       /* cluster edge */
            /* unstressed syllable onset */
            if (nx && ph_is_vowel(nx->ph) && nx->stress == 0)
                prc *= 0.85f;
        }

        d = mn + (inh - mn) * prc / 100.0f;
        if (g->flags & SF_ASPIRATE) {
            /* aspiration: longer before stressed vowels */
            float a = (nx && nx->stress) ? 65.0f : 40.0f;
            if (nx && !ph_is_vowel(nx->ph)) a = 45.0f; /* "pl", "tr" */
            d += a;
            g->asp = (uint16_t)(a * scale + 0.5f);
        }
        d *= scale;
        if (d < 12.0f) d = 12.0f;
        g->dur = (uint16_t)(d + 0.5f);
        if (g->asp > g->dur / 2) g->asp = (uint16_t)(g->dur / 2);
        g->t0 = t;
        t += g->dur;
    }
    u->total_ms = t;
}

/* ---- F0 ---------------------------------------------------------------- */

typedef struct { float t0, t1; int punct; } phrase;

static float smoothstep(float x)
{
    if (x <= 0.0f) return 0.0f;
    if (x >= 1.0f) return 1.0f;
    return x * x * (3.0f - 2.0f * x);
}

static void f0_contour(sp_utt *u, const nar_params *p)
{
    float P = (float)(p->pitch ? p->pitch : DEFPITCH);
    float enth = (p->flags & NDF_NEWIORB) ? (float)p->F0enthusiasm / 32.0f : 1.0f;
    float pert = (p->flags & NDF_NEWIORB) ? (float)p->F0perturb / 255.0f : 0.0f;
    int mode = p->mode;
    phrase ph[64];
    int nph = 0, i, k;
    float T = (float)u->total_ms;
    float step;
    float speech_start = 0, speech_end = T;
    int last_stressed = -1, last_vowel = -1;
    sp_rng rng = { 0x5eed1985u };
    float wobble = 0.0f, wob_v = 0.0f;

    u->nf0 = 0;
    if (T <= 0) return;

    /* phrases between punctuated pauses */
    {
        float start = -1;
        for (i = 0; i < u->nseg; i++) {
            sp_seg *g = &u->seg[i];
            if (g->ph != PH_PAU) {
                if (start < 0) start = (float)g->t0;
            } else if (start >= 0 && (g->punct || i == u->nseg - 1)) {
                if (nph < 64) {
                    ph[nph].t0 = start;
                    ph[nph].t1 = (float)g->t0;
                    ph[nph].punct = g->punct ? g->punct : '-';
                    nph++;
                }
                start = -1;
            }
        }
        if (nph == 0) {
            ph[0].t0 = 0; ph[0].t1 = T; ph[0].punct = '-'; nph = 1;
        }
        speech_start = ph[0].t0;
        speech_end = ph[nph - 1].t1;
    }
    for (i = 0; i < u->nseg; i++) {
        if (ph_is_vowel(u->seg[i].ph)) {
            last_vowel = i;
            if (u->seg[i].stress) last_stressed = i;
        }
    }

    step = 10.0f;
    while (T / step > (float)(SP_MAX_F0 - 4)) step *= 2.0f;

    for (k = 0; ; k++) {
        float t = (float)k * step, f = P, base, acc = 0.0f;
        int pi = 0;
        if (t > T) t = T;
        for (pi = 0; pi < nph - 1 && t > ph[pi].t1 + 1.0f; pi++)
            ;
        if (mode == ROBOTICF0) {
            f = P;
        } else {
            phrase *q = &ph[pi];
            float len = q->t1 - q->t0, x;
            float top, bot;
            if (len < 1) len = 1;
            x = (t - q->t0) / len;
            if (x < 0) x = 0;
            if (x > 1) x = 1;
            if (mode == MANUALF0) {
                top = 1.0f; bot = 1.0f;
            } else {
                /* declination; later phrases start a little lower */
                float reset = 1.0f - 0.04f * (float)pi;
                /* slope limited to ~10% per second so short phrases don't dive */
                float drop = 0.12f;
                if (len < 1200.0f) drop *= len / 1200.0f;
                top = 1.05f * reset;
                bot = top - drop;
            }
            base = P * (top + (bot - top) * x);

            /* accents on stressed vowels */
            for (i = 0; i < u->nseg; i++) {
                sp_seg *g = &u->seg[i];
                float vs, ve, pk, a, h;
                if (!ph_is_vowel(g->ph) || !g->stress) continue;
                vs = (float)g->t0;
                ve = vs + (float)g->dur;
                if (t < vs - 120.0f || t > ve + 200.0f) continue;
                h = 0.04f * (float)g->stress * enth;
                if (mode == NATURALF0) {
                    float pos = (vs - speech_start) / (speech_end - speech_start + 1.0f);
                    h *= 1.0f - 0.35f * pos;
                    if (u->final_punct == '?' && i == last_stressed)
                        h *= 0.3f;   /* question nucleus is low, then rises */
                }
                pk = vs + 0.4f * (ve - vs);
                if (t <= pk)
                    a = smoothstep((t - (vs - 80.0f)) / (pk - vs + 80.0f));
                else
                    a = 1.0f - 0.8f * smoothstep((t - pk) / (ve - pk + 120.0f));
                a *= h;
                if (a > acc) acc = a;
            }
            f = base * (1.0f + acc);

            /* boundary tones over the phrase's last syllable */
            {
                float tail0 = q->t1 - 260.0f, y;
                if (pi == nph - 1 && last_vowel >= 0) {
                    float lv = (float)u->seg[last_vowel].t0;
                    if (u->final_punct == '?' && last_stressed >= 0)
                        tail0 = (float)u->seg[last_stressed].t0;
                    else if (lv < q->t1) tail0 = lv - 40.0f;
                }
                if (tail0 < q->t0) tail0 = q->t0;
                y = (t - tail0) / (q->t1 - tail0 + 1.0f);
                if (y > 0) {
                    float r = smoothstep(y);
                    switch (q->punct) {
                    case '.': f *= 1.0f - 0.2f * r; break;
                    case '?': f *= 1.0f + 0.45f * r * (enth < 2 ? enth : 2); break;
                    case ',': f *= 1.0f + 0.12f * r; break;
                    case '-': f *= 1.0f + 0.05f * r; break;
                    default: break;
                    }
                }
            }
        }
        /* F0 perturbation: smoothed random walk */
        if (pert > 0.0f) {
            wob_v = 0.85f * wob_v + 0.15f * sp_randf(&rng);
            wobble = 0.9f * wobble + wob_v;
            f *= 1.0f + 0.25f * pert * wobble;
        }
        if (f < 40.0f) f = 40.0f;
        if (f > 600.0f) f = 600.0f;
        u->f0[u->nf0].t = (uint32_t)t;
        u->f0[u->nf0].hz = f;
        u->nf0++;
        if (t >= T || u->nf0 >= SP_MAX_F0) break;
    }
}

void sp_prosody(sp_utt *u, const nar_params *p)
{
    durations(u, p);
    f0_contour(u, p);
}
