// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>
//
// Voice B "Natural": diphone concatenation with CMU Flite's residual-excited
// LPC resynthesis over the cmu_us_kal16 diphone database (BSD-style
// licence, third_party/flite/COPYING).
//
// Flite is used only as a waveform back end: phone sequence, durations and
// F0 come from our own parser and prosody (the same ones that drive the
// classic voice), so both voices sit behind the same narrator interface.
// Unit selection below mirrors Flite's get_diphone_units() but takes the
// consonant/vowel information from our phoneme table instead of a Flite
// phoneset, so none of Flite's text front end is needed.

#include "speech.h"
#include "resample.h"
#include <stdlib.h>
#include <string.h>

#include "cst_hrg.h"
#include "cst_wave.h"
#include "cst_units.h"
#include "cst_sts.h"
#include "cst_diphone.h"
#include "cst_utt_utils.h"
#include "voicefile.h"

static const cst_diphone_db *g_voice;

int nar_set_voice_image(uint8_t *image, size_t size)
{
    if (g_voice) { amsv_close(g_voice); g_voice = NULL; }
    if (!image) return 0;
    g_voice = amsv_open(image, size);
    return g_voice ? 0 : -1;
}

int nar_have_natural(void)
{
    return g_voice != NULL;
}

static int find_diphone(const cst_diphone_db *db, const char *key)
{
    int lo = 0, hi = db->num_entries;
    while (lo < hi) {
        int mid = (lo + hi) / 2;
        int c = strcmp(db->diphones[mid].name, key);
        if (c == 0) return mid;
        if (c > 0) hi = mid; else lo = mid + 1;
    }
    return -1;
}

/* "a-b" or "a_-_b" (names cut to 10 chars, as Flite does) */
static void dname(char *out, const char *a, const char *sep, const char *b)
{
    int n = 0, i;
    for (i = 0; a[i] && i < 10; i++) out[n++] = a[i];
    for (i = 0; sep[i]; i++) out[n++] = sep[i];
    for (i = 0; b[i] && i < 10; i++) out[n++] = b[i];
    out[n] = 0;
}

typedef struct {
    const char *name;
    float end;          /* seconds */
    int cons;           /* consonant (for cluster diphones) */
    int word;           /* word index */
} nphone;

/* Kal's phone set has no flap or glottal stop: map our allophones */
static const char *unit_name(const sp_seg *g)
{
    switch (g->ph) {
    case PH_DX: return "d";
    case PH_Q:  return g->dur >= 20 ? "pau" : "";
    default:    return ph_table[g->ph].unit;
    }
}

typedef struct {
    resampler rs;
    nphone ph[SP_MAX_SEGS + 4];
} natural_work;

int voice_natural_render(const sp_utt *u, const nar_params *p,
                         nar_sink_fn sink, void *user)
{
    const cst_diphone_db *db = g_voice;
    natural_work *w;
    cst_utterance *utt;
    cst_relation *segr, *unitr, *tgtr;
    cst_item *it;
    cst_wave *wav;
    int i, n = 0, word = 0, rc = 0;
    float t = 0.0f;
    int sr = db->sts->sample_rate;
    float vol = (float)p->volume / 64.0f;

    if (!db) return voice_classic_render(u, p, sink, user);
    w = (natural_work *)malloc(sizeof *w);
    if (!w) return ND_NoMem;

    /* phone list with end times; merge runs of silence */
    for (i = 0; i < u->nseg; i++) {
        const sp_seg *g = &u->seg[i];
        const char *nm = unit_name(g);
        float d = (float)g->dur / 1000.0f;
        if (g->flags & SF_WSTART) word++;
        t += d;
        if (!nm[0]) {
            /* no unit: give the time to the previous phone */
            if (n > 0) w->ph[n - 1].end = t;
            continue;
        }
        if (strcmp(nm, "pau") == 0 && n > 0 && strcmp(w->ph[n - 1].name, "pau") == 0) {
            w->ph[n - 1].end = t;
            continue;
        }
        w->ph[n].name = nm;
        w->ph[n].end = t;
        w->ph[n].cons = !ph_is_vowel(g->ph) && g->ph != PH_PAU;
        w->ph[n].word = word;
        n++;
    }
    if (n == 0 || strcmp(w->ph[0].name, "pau") != 0 || n < 2) {
        free(w);
        return 0;
    }

    utt = new_utterance();
    utt_set_feat(utt, "sts_list", sts_list_val(db->sts));
    utt_set_feat_string(utt, "resynth_type", "fixed");
    segr = utt_relation_create(utt, "Segment");
    unitr = utt_relation_create(utt, "Unit");
    tgtr = utt_relation_create(utt, "Target");

    for (i = 0; i < n; i++) {
        it = relation_append(segr, NULL);
        item_set_string(it, "name", w->ph[i].name);
        item_set_float(it, "end", w->ph[i].end);
    }

    /* diphone units, as in Flite's get_diphone_units() */
    {
        cst_item *s0, *s1;
        int k = 0;
        for (s0 = relation_head(segr); s0 && item_next(s0); s0 = s1, k++) {
            char name[24];
            int e = -1;
            float end0, end1;
            cst_item *un;
            s1 = item_next(s0);
            if (w->ph[k].cons && w->ph[k + 1].cons && w->ph[k].word == w->ph[k + 1].word) {
                dname(name, item_name(s0), "_-_", item_name(s1));
                e = find_diphone(db, name);
            }
            if (e < 0) {
                dname(name, item_name(s0), "-", item_name(s1));
                e = find_diphone(db, name);
            }
            if (e < 0) e = find_diphone(db, "pau-pau");
            if (e < 0) e = 0;
            end0 = item_feat_float(s0, "end");
            end1 = item_feat_float(s1, "end");

            un = relation_append(unitr, NULL);
            item_add_daughter(s0, un);
            item_set_string(un, "name", name);
            item_set_int(un, "target_end", (int)(end0 * sr));
            item_set_int(un, "unit_entry", e);
            item_set_int(un, "unit_start", db->diphones[e].start_pm);
            item_set_int(un, "unit_end", db->diphones[e].start_pm + db->diphones[e].pb_pm);

            un = relation_append(unitr, NULL);
            item_add_daughter(s1, un);
            item_set_string(un, "name", name);
            item_set_int(un, "target_end", (int)(((end0 + end1) / 2.0f) * sr));
            item_set_int(un, "unit_entry", e);
            item_set_int(un, "unit_start", db->diphones[e].start_pm + db->diphones[e].pb_pm);
            item_set_int(un, "unit_end", db->diphones[e].start_pm + db->diphones[e].pb_pm
                                             + db->diphones[e].end_pm);
        }
    }

    /* F0 targets from our prosody; the last target must reach the end */
    {
        float tend = w->ph[n - 1].end;
        float last = -1.0f;
        for (i = 0; i < u->nf0; i++) {
            float pos = (float)u->f0[i].t / 1000.0f;
            if (pos <= last) continue;
            if (pos > tend) pos = tend;
            it = relation_append(tgtr, NULL);
            item_set_float(it, "pos", pos);
            item_set_float(it, "f0", u->f0[i].hz);
            last = pos;
            if (pos >= tend) break;
        }
        if (last < tend) {
            it = relation_append(tgtr, NULL);
            item_set_float(it, "pos", tend);
            item_set_float(it, "f0", u->nf0 ? u->f0[u->nf0 - 1].hz : 100.0f);
        }
    }

    join_units_modified_lpc(utt);
    wav = utt_wave(utt);
    if (wav && wav->num_samples > 0) {
        int16_t buf[256];
        int j = 0, m;
        rs_init(&w->rs, (double)sr, (double)NAR_OUTPUT_RATE, sink, user);
        while (j < wav->num_samples && rc == 0) {
            m = wav->num_samples - j;
            if (m > 256) m = 256;
            for (i = 0; i < m; i++) {
                float x = (float)wav->samples[j + i] * vol * 1.6f;
                if (x > 32767.0f) x = 32767.0f;
                if (x < -32768.0f) x = -32768.0f;
                buf[i] = (int16_t)x;
            }
            rc = rs_push(&w->rs, buf, (size_t)m);
            j += m;
        }
        if (rc == 0) rc = rs_flush(&w->rs);
    }
    delete_utterance(utt);
    free(w);
    return rc;
}
