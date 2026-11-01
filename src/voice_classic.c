// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>
//
// Voice A "Classic": phonemes + prosody -> formant tracks -> Klatt synth.
//
// Coarticulation follows the Holmes / Klatt "dominance" idea: at each
// phoneme boundary the higher-ranked phoneme (stops > fricatives > nasals >
// liquids/glides > vowels) sets a locus-like boundary value
//     b = T_dom + k * (T_other - T_dom)
// and transitions of class-dependent length run from the boundary into
// both neighbours. Diphthongs glide between two targets. Source amplitudes
// (voicing, aspiration, frication, bursts) are switched per sub-segment:
// stops are split into closure, burst and aspiration.

#include "speech.h"
#include "klatt.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define FRAME_MS 2.0f
#define OUT_GAIN 5200.0f
/* formant bandwidth scale: wider than the textbook values sounds less
   "ringing" and measured more intelligible (tests/intelligibility.py) */
#define BW_SCALE 1.4f

typedef struct {
    float T[3], E[3], B[3];     /* formant targets (start/end), bandwidths */
    float L, R;                 /* formant interval, ms */
    float cl_end;               /* stops: closure end (ms) */
    float burst_end;            /* stops: burst end (ms) */
} seg_track;

typedef struct {
    float b[3], bw[3];          /* boundary values */
    float tl_left, tl_right;    /* transition lengths into left / right seg */
} boundary;

typedef struct {
    seg_track s[SP_MAX_SEGS];
    boundary  bd[SP_MAX_SEGS];  /* bd[i] = boundary between i and i+1 */
    float buf[256];
} classic_work;

static float db(float d)
{
    if (d <= 0.0f) return 0.0f;
    return powf(10.0f, (d - 60.0f) / 20.0f);
}

static float smooth01(float x)
{
    if (x <= 0.0f) return 0.0f;
    if (x >= 1.0f) return 1.0f;
    return x * x * (3.0f - 2.0f * x);
}

static int rank_of(int ph)
{
    return ph_table[ph].rank;
}

static int burst_ms(int ph)
{
    switch (ph) {
    case PH_P: case PH_B: return 7;
    case PH_T: case PH_D: return 11;
    case PH_K: case PH_G: return 24;
    default: return 0;
    }
}

/* class-dependent locus coefficients and transition lengths for the
   dominant phoneme of a boundary */
static void dom_params(int ph, float k[3], float *ext, float *in)
{
    int f = ph_table[ph].flags;
    if (f & PF_STOP) {
        k[0] = 0.25f; k[1] = 0.45f; k[2] = 0.4f; *ext = 50.0f; *in = 0.0f;
        if (ph == PH_K || ph == PH_G) { k[1] = 0.6f; k[2] = 0.55f; } /* velar pinch */
    } else if (f & PF_FRIC) {
        k[0] = 0.3f; k[1] = 0.5f; k[2] = 0.5f; *ext = 40.0f; *in = 15.0f;
    } else if (f & PF_NASAL) {
        k[0] = 0.15f; k[1] = 0.4f; k[2] = 0.4f; *ext = 40.0f; *in = 0.0f;
    } else if (f & (PF_LIQUID | PF_GLIDE)) {
        k[0] = 0.2f; k[1] = 0.25f; k[2] = 0.25f; *ext = 70.0f; *in = 40.0f;
    } else { /* ER as a rhotic vowel */
        k[0] = 0.4f; k[1] = 0.4f; k[2] = 0.35f; *ext = 50.0f; *in = 40.0f;
    }
}

static const ph_targets *centre_targets(const nar_params *p)
{
    static const char *ok[] = { "IY","IH","EH","AE","AA","AH","AO","OW","UH","ER","UW" };
    int i, j;
    if (!p->centphon) return NULL;
    for (i = 0; i < 11; i++) {
        if (strcmp(p->centphon, ok[i]) == 0) {
            for (j = 1; j < PH_COUNT; j++)
                if (strcmp(ph_table[j].name, ok[i]) == 0)
                    return &ph_table[j].t;
        }
    }
    return NULL;
}

static void fill_targets(const sp_utt *u, const nar_params *p, classic_work *w)
{
    int i, newi = (p->flags & NDF_NEWIORB) != 0;
    const ph_targets *ct = newi && p->centralize ? centre_targets(p) : NULL;
    float cen = ct ? (float)p->centralize / 100.0f : 0.0f;
    float fs[3];
    fs[0] = 1.0f + (newi ? 0.05f * (float)p->F1adj : 0.0f);
    fs[1] = 1.0f + (newi ? 0.05f * (float)p->F2adj : 0.0f);
    fs[2] = 1.0f + (newi ? 0.05f * (float)p->F3adj : 0.0f);
    if (p->sex == FEMALE) { fs[0] *= 1.12f; fs[1] *= 1.17f; fs[2] *= 1.15f; }
    for (i = 0; i < 3; i++) if (fs[i] < 0.2f) fs[i] = 0.2f;

    for (i = 0; i < u->nseg; i++) {
        const sp_seg *g = &u->seg[i];
        const ph_targets *t = &ph_table[g->ph].t;
        seg_track *s = &w->s[i];
        int j;
        s->T[0] = t->f1; s->T[1] = t->f2; s->T[2] = t->f3;
        s->E[0] = t->e1 ? t->e1 : t->f1;
        s->E[1] = t->e2 ? t->e2 : t->f2;
        s->E[2] = t->e3 ? t->e3 : t->f3;
        s->B[0] = t->b1; s->B[1] = t->b2; s->B[2] = t->b3;
        if (ct && ph_is_vowel(g->ph)) {
            const float cv[3] = { ct->f1, ct->f2, ct->f3 };
            for (j = 0; j < 3; j++) {
                s->T[j] += (cv[j] - s->T[j]) * cen;
                s->E[j] += (cv[j] - s->E[j]) * cen;
            }
        }
        for (j = 0; j < 3; j++) { s->T[j] *= fs[j]; s->E[j] *= fs[j]; }
    }
    /* rank-0 segments (pauses, /H, Q, QX) borrow a neighbour's targets:
       /H and leading silence the next sounding segment, others the
       previous one */
    for (i = 0; i < u->nseg; i++) {
        int src = -1, j;
        if (rank_of(u->seg[i].ph) != 0) continue;
        if (u->seg[i].ph == PH_HH || i == 0 ||
            (i > 0 && rank_of(u->seg[i - 1].ph) == 0 && u->seg[i].ph != PH_PAU)) {
            for (j = i + 1; j < u->nseg; j++)
                if (rank_of(u->seg[j].ph) != 0) { src = j; break; }
        }
        if (src < 0)
            for (j = i - 1; j >= 0; j--)
                if (rank_of(u->seg[j].ph) != 0) { src = j; break; }
        if (src < 0)
            for (j = i + 1; j < u->nseg; j++)
                if (rank_of(u->seg[j].ph) != 0) { src = j; break; }
        if (src >= 0) {
            seg_track *s = &w->s[i], *o = &w->s[src];
            int k;
            for (k = 0; k < 3; k++) {
                float v = (src > i) ? o->T[k] : o->E[k];
                s->T[k] = s->E[k] = v;
                s->B[k] = o->B[k];
            }
            if (u->seg[i].ph == PH_HH) { s->B[0] = 300; s->B[1] *= 1.5f; s->B[2] *= 1.5f; }
        } else {
            seg_track *s = &w->s[i];
            s->T[0] = s->E[0] = 500; s->T[1] = s->E[1] = 1500; s->T[2] = s->E[2] = 2500;
            s->B[0] = 100; s->B[1] = 100; s->B[2] = 150;
        }
    }
}

static void build_tracks(const sp_utt *u, const nar_params *p, classic_work *w)
{
    int i;
    float art = (p->flags & NDF_NEWIORB) ? (float)p->articulate / 100.0f : 1.0f;
    if (art < 0.1f) art = 0.1f;

    /* formant intervals: stops hand over to the next segment at release */
    for (i = 0; i < u->nseg; i++) {
        const sp_seg *g = &u->seg[i];
        seg_track *s = &w->s[i];
        float t0 = (float)g->t0, d = (float)g->dur;
        s->L = (i == 0) ? 0.0f : w->s[i - 1].R;
        s->R = t0 + d;
        s->cl_end = s->burst_end = t0 + d;
        if (ph_table[g->ph].flags & PF_AFFR) {
            s->cl_end = t0 + d * 0.5f;
            s->burst_end = t0 + d;
        } else if (g->ph == PH_DX) {
            s->cl_end = s->burst_end = t0 + d;
        } else if (ph_table[g->ph].flags & PF_STOP) {
            float b = (float)burst_ms(g->ph), a = (float)g->asp;
            float cl = d - b - a;
            if (cl < d * 0.3f) cl = d * 0.3f;
            s->cl_end = t0 + cl;
            s->burst_end = s->cl_end + b;
            if (s->burst_end > t0 + d) s->burst_end = t0 + d;
            /* formant transition into the next sound starts at release,
               except before a pause */
            if (i + 1 < u->nseg && u->seg[i + 1].ph != PH_PAU)
                s->R = s->cl_end;
        }
        if (s->R < s->L + 1.0f) s->R = s->L + 1.0f;
    }

    for (i = 0; i + 1 < u->nseg; i++) {
        int a = u->seg[i].ph, b = u->seg[i + 1].ph;
        int ra = rank_of(a), rb = rank_of(b), k;
        seg_track *A = &w->s[i], *Bs = &w->s[i + 1];
        boundary *bd = &w->bd[i];
        for (k = 0; k < 3; k++)
            bd->bw[k] = 0.5f * (A->B[k] + Bs->B[k]);
        if (ra == 0 || rb == 0) {
            /* borrowed targets: continuous, no transition needed */
            for (k = 0; k < 3; k++)
                bd->b[k] = (ra == 0) ? Bs->T[k] : A->E[k];
            bd->tl_left = bd->tl_right = 0.0f;
            if (a == PH_HH || b == PH_HH) {
                bd->tl_left = bd->tl_right = 0.0f;
            }
        } else if (ra == rb) {
            int vowel = ph_is_vowel(a);
            for (k = 0; k < 3; k++)
                bd->b[k] = 0.5f * (A->E[k] + Bs->T[k]);
            bd->tl_left = bd->tl_right = vowel ? 45.0f : 20.0f;
        } else {
            float kk[3], ext, in;
            int dom_left = ra > rb;
            int dph = dom_left ? a : b;
            const float *Td = dom_left ? A->E : Bs->T;
            const float *To = dom_left ? Bs->T : A->E;
            dom_params(dph, kk, &ext, &in);
            for (k = 0; k < 3; k++)
                bd->b[k] = Td[k] + kk[k] * (To[k] - Td[k]);
            if (dom_left) { bd->tl_left = in; bd->tl_right = ext; }
            else          { bd->tl_left = ext; bd->tl_right = in; }
        }
        bd->tl_left *= art;
        bd->tl_right *= art;
    }
}

/* formant value of segment i at time t */
static void formants_at(const sp_utt *u, const classic_work *w, int i, float t,
                        float f[3], float bw[3])
{
    const seg_track *s = &w->s[i];
    float len = s->R - s->L, tlL = 0, tlR = 0, wl = 0, wr = 0, frac = 0;
    int k, diph = (ph_table[u->seg[i].ph].flags & PF_DIPH) != 0;
    const boundary *bl = (i > 0) ? &w->bd[i - 1] : NULL;
    const boundary *br = (i + 1 < u->nseg) ? &w->bd[i] : NULL;
    if (bl) tlL = bl->tl_right;
    if (br) tlR = br->tl_left;
    if (tlL + tlR > len && tlL + tlR > 0) {
        float sc = len / (tlL + tlR);
        tlL *= sc; tlR *= sc;
    }
    if (bl) wl = (tlL > 0.5f) ? 1.0f - smooth01((t - s->L) / tlL) : 0.0f;
    if (br) wr = (tlR > 0.5f) ? smooth01((t - (s->R - tlR)) / tlR) : 0.0f;
    if (diph && len > 0)
        frac = smooth01(((t - s->L) / len - 0.2f) / 0.65f);
    for (k = 0; k < 3; k++) {
        float tgt0 = s->T[k], tgt1 = s->E[k];
        float tg = tgt0 + (tgt1 - tgt0) * frac;
        float v = tg;
        if (bl) v += (bl->b[k] - tgt0) * wl;
        if (br) v += (br->b[k] - tgt1) * wr;
        f[k] = v;
        bw[k] = s->B[k];
        if (bl && wl > 0) bw[k] += (bl->bw[k] - s->B[k]) * wl;
        if (br && wr > 0) bw[k] += (br->bw[k] - s->B[k]) * wr;
    }
}

static float f0_at(const sp_utt *u, float t)
{
    int lo = 0, hi = u->nf0 - 1;
    if (u->nf0 <= 0) return 110.0f;
    if (t <= (float)u->f0[0].t) return u->f0[0].hz;
    if (t >= (float)u->f0[hi].t) return u->f0[hi].hz;
    while (hi - lo > 1) {
        int m = (lo + hi) / 2;
        if ((float)u->f0[m].t <= t) lo = m; else hi = m;
    }
    {
        float t0 = (float)u->f0[lo].t, t1 = (float)u->f0[hi].t;
        float x = (t1 > t0) ? (t - t0) / (t1 - t0) : 0.0f;
        return u->f0[lo].hz + (u->f0[hi].hz - u->f0[lo].hz) * x;
    }
}

/* source amplitudes for segment i at time t */
static void source_at(const sp_utt *u, const classic_work *w, const nar_params *p,
                      int i, float t, kl_frame *fr)
{
    const sp_seg *g = &u->seg[i];
    const ph_info *pi = &ph_table[g->ph];
    const ph_targets *tg = &pi->t;
    const seg_track *s = &w->s[i];
    int newi = (p->flags & NDF_NEWIORB) != 0;
    float avb = newi ? (float)p->AVbias : 0.0f;
    float afb = newi ? (float)p->AFbias : 0.0f;
    float av = 0, af = 0, ah = 0, ab = 0;
    int j, use_par = 0;
    float female = (p->sex == FEMALE) ? 1.0f : 0.0f;

    for (j = 0; j < KL_NPAR; j++) fr->pa[j] = 0.0f;

    if (g->ph == PH_PAU || (pi->flags & PF_SILENT)) {
        /* silence */
    } else if (pi->flags & PF_STOP) {
        if (t < s->cl_end) {
            if (pi->flags & PF_VOICED) av = tg->av;   /* voice bar */
        } else if (t < s->burst_end || (pi->flags & PF_AFFR)) {
            af = tg->af;
            use_par = 1;
            /* affricates start with a release transient */
            if ((pi->flags & PF_AFFR) && t < s->cl_end + 10.0f) af += 6.0f;
            if (pi->flags & PF_VOICED) av = tg->av + 6;
        } else {
            /* aspiration */
            ah = (g->flags & SF_ASPIRATE) ? 60.0f : 0.0f;
        }
        if (g->ph == PH_DX) { av = tg->av; af = 0; use_par = 0; }
    } else if (pi->flags & PF_ASPIR) {
        ah = tg->ah;
    } else {
        av = tg->av;
        if (pi->flags & PF_FRIC) { af = tg->af; use_par = 1; }
        if (ph_is_vowel(g->ph)) {
            av += g->stress ? 1.0f : -1.5f;
            ah = av - 16.0f + 4.0f * female;   /* a little breath */
            /* phrase-final decay, more breathy */
            if ((g->flags & SF_FINALSYL) && i + 1 < u->nseg) {
                float x = (t - (float)g->t0) / (float)(g->dur ? g->dur : 1);
                float d = smooth01((x - 0.5f) * 2.0f);
                av -= 6.0f * d;
                ah += 4.0f * d;
            }
        }
    }
    if (av > 0) av += avb;
    if (af > 0) af += afb;
    if (ah > 0) ah += afb * 0.5f;
    fr->av = db(av);
    fr->af = db(af);
    fr->ah = db(ah);
    if (use_par) {
        fr->pa[1] = db(tg->a2);
        fr->pa[2] = db(tg->a3);
        fr->pa[3] = db(tg->a4);
        fr->pa[4] = db(tg->a5);
        fr->pa[5] = db(tg->a6);
        ab = db(tg->ab);
    }
    fr->ab = ab;
    fr->fnp = 270.0f;
    fr->bnp = 100.0f;
    if (tg->fnz && (pi->flags & PF_NASAL)) {
        fr->fnz = (float)tg->fnz;
        fr->bnz = 120.0f;
        fr->fnp = 260.0f;
        fr->bnp = 60.0f;
    } else {
        fr->fnz = 270.0f;
        fr->bnz = 100.0f;
    }
    /* parallel F6: sibilant peak */
    if (g->ph == PH_S || g->ph == PH_Z) {
        fr->f[5] = 5600.0f * (1.0f + 0.1f * female); fr->b[5] = 900.0f;
    } else {
        fr->f[5] = 4900.0f; fr->b[5] = 1000.0f;
    }
}

int voice_classic_render(const sp_utt *u, const nar_params *p,
                         nar_sink_fn sink, void *user)
{
    classic_work *w = (classic_work *)malloc(sizeof *w);
    klatt kl;
    kl_frame prev, cur;
    int16_t out[256];
    float sr = (float)NAR_OUTPUT_RATE;
    float vol = (float)p->volume / 64.0f;
    float female = (p->sex == FEMALE) ? 1.0f : 0.0f;
    int newi = (p->flags & NDF_NEWIORB) != 0;
    uint32_t nframes, k;
    int seg_f = 0, seg_a = 0, rc = 0;
    double sample_acc = 0.0;
    float sm_av = 0.0f, sm_ah = 0.0f;

    if (!w) return ND_NoMem;
    fill_targets(u, p, w);
    build_tracks(u, p, w);
    kl_init(&kl, sr, 0x1985u);
    memset(&prev, 0, sizeof prev);

    nframes = (uint32_t)((float)u->total_ms / FRAME_MS);
    for (k = 0; k < nframes && rc == 0; k++) {
        float t = ((float)k + 0.5f) * FRAME_MS;
        float f[3], bw[3];
        int n, i;

        while (seg_f + 1 < u->nseg && t >= w->s[seg_f].R) seg_f++;
        while (seg_a + 1 < u->nseg &&
               t >= (float)(u->seg[seg_a].t0 + u->seg[seg_a].dur)) seg_a++;

        memset(&cur, 0, sizeof cur);
        formants_at(u, w, seg_f, t, f, bw);
        cur.f[0] = f[0]; cur.f[1] = f[1]; cur.f[2] = f[2];
        cur.b[0] = bw[0] * BW_SCALE; cur.b[1] = bw[1] * BW_SCALE; cur.b[2] = bw[2] * BW_SCALE;
        cur.f[3] = 3300.0f * (1.0f + 0.12f * female); cur.b[3] = 250.0f;
        cur.f[4] = 3750.0f * (1.0f + 0.12f * female); cur.b[4] = 200.0f;
        source_at(u, w, p, seg_a, t, &cur);
        /* cascade F6 (source_at chose the parallel F6 for this sound) */
        cur.f[5] = cur.f[5] > 0 ? cur.f[5] : 4900.0f;
        cur.f0 = f0_at(u, t);
        cur.oq = female ? 0.7f : 0.62f;
        cur.tilt = female ? 0.3f : 0.15f;
        if (newi) {
            cur.adj[0] = powf(10.0f, (float)p->A1adj / 20.0f);
            cur.adj[1] = powf(10.0f, (float)p->A2adj / 20.0f);
            cur.adj[2] = powf(10.0f, (float)p->A3adj / 20.0f);
        } else {
            cur.adj[0] = cur.adj[1] = cur.adj[2] = 1.0f;
        }
        /* gentle onset/offset of voicing and aspiration (~3 ms) */
        sm_av += (cur.av - sm_av) * 0.55f;
        sm_ah += (cur.ah - sm_ah) * 0.55f;
        cur.av = sm_av;
        cur.ah = sm_ah;
        if (k == 0) prev = cur;

        sample_acc += (double)sr * FRAME_MS / 1000.0;
        n = (int)sample_acc;
        sample_acc -= n;
        if (n > 256) n = 256;
        kl_render(&kl, &prev, &cur, n, w->buf);
        for (i = 0; i < n; i++) {
            float x = w->buf[i] * OUT_GAIN * vol;
            /* soft limiter above ~-3 dBFS */
            if (x > 23000.0f) x = 23000.0f + (x - 23000.0f) / (1.0f + (x - 23000.0f) / 9000.0f);
            else if (x < -23000.0f) x = -23000.0f + (x + 23000.0f) / (1.0f - (x + 23000.0f) / 9000.0f);
            if (x > 32767.0f) x = 32767.0f;
            if (x < -32768.0f) x = -32768.0f;
            out[i] = (int16_t)lrintf(x);
        }
        if (sink(user, out, (size_t)n)) rc = 1;
        prev = cur;
    }
    free(w);
    return rc;
}
