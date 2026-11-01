// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>
//
// CMD_WRITE equivalent: validate settings, parse the phoneme string one
// sentence at a time, run prosody and the selected voice.

#include "speech.h"
#include "resample.h"
#include <stdlib.h>
#include <string.h>

void nar_params_default(nar_params *p)
{
    memset(p, 0, sizeof *p);
    p->rate = DEFRATE;
    p->pitch = DEFPITCH;
    p->mode = DEFMODE;
    p->sex = DEFSEX;
    p->volume = DEFVOL;
    p->sampfreq = DEFFREQ;
    p->flags = NDF_NEWIORB;
    p->F0enthusiasm = DEFF0ENTHUS;
    p->F0perturb = DEFF0PERT;
    p->articulate = DEFARTIC;
    p->centralize = DEFCENTRAL;
    p->centphon = NULL;
    p->voice = NAR_VOICE_NATURAL;
}

static int valid_centphon(const char *c)
{
    static const char *ok[] = { "IY","IH","EH","AE","AA","AH","AO","OW","UH","ER","UW" };
    int i;
    for (i = 0; i < 11; i++)
        if (strcmp(c, ok[i]) == 0) return 1;
    return 0;
}

int nar_params_check(const nar_params *p)
{
    if (p->rate < MINRATE || p->rate > MAXRATE) return ND_RateErr;
    if (p->pitch < MINPITCH || p->pitch > MAXPITCH) return ND_PitchErr;
    if (p->sex != MALE && p->sex != FEMALE) return ND_SexErr;
    if (p->mode != NATURALF0 && p->mode != ROBOTICF0 && p->mode != MANUALF0)
        return ND_ModeErr;
    if (p->sampfreq < MINFREQ || p->sampfreq > MAXFREQ) return ND_FreqErr;
    if (p->volume > MAXVOL) return ND_VolErr;
    if (p->flags & NDF_NEWIORB) {
        if (p->centralize > MAXCENT) return ND_DCentErr;
        if (p->centralize && (!p->centphon || !valid_centphon(p->centphon)))
            return ND_CentPhonErr;
    }
    if (p->voice > NAR_VOICE_NATURAL) return ND_Unimpl;
    return 0;
}

int nar_check_phonemes(const char *ph, size_t len, size_t *err_pos)
{
    sp_utt *u = (sp_utt *)malloc(sizeof *u);
    size_t pos = 0;
    int r;
    if (!u) return ND_NoMem;
    while ((r = sp_parse_sentence(ph, len, &pos, u, err_pos)) > 0)
        ;
    free(u);
    return r < 0 ? r : 0;
}

/* sampfreq != DEFFREQ: the classic device simply played its 22200 Hz-tuned
   samples at another rate, shifting pitch, formants and tempo together.
   We reproduce that by resampling the rendered audio. */
typedef struct {
    resampler rs;
    int active;
    nar_sink_fn sink;
    void *user;
} freq_shift;

static int shift_sink(void *user, const int16_t *s, size_t n)
{
    freq_shift *fs = (freq_shift *)user;
    if (!fs->active) return fs->sink(fs->user, s, n);
    return rs_push(&fs->rs, s, n);
}

int nar_speak(const nar_params *p, const char *ph, size_t len,
              nar_sink_fn sink, void *user, size_t *err_pos)
{
    nar_params eff;
    sp_utt *u;
    freq_shift *fs;
    size_t pos = 0;
    int r, rc = 0;

    r = nar_params_check(p);
    if (r) return r;
    eff = *p;
    if (!(p->flags & NDF_NEWIORB)) {
        /* old-style IORB: V37 fields are not looked at */
        eff.F0enthusiasm = DEFF0ENTHUS;
        eff.F0perturb = DEFF0PERT;
        eff.F1adj = eff.F2adj = eff.F3adj = 0;
        eff.A1adj = eff.A2adj = eff.A3adj = 0;
        eff.articulate = DEFARTIC;
        eff.centralize = DEFCENTRAL;
        eff.AVbias = eff.AFbias = 0;
        if (eff.mode == MANUALF0) return ND_ModeErr;
    }
    /* validate the whole string first, like the device (no partial speech
       on a spelling error) */
    r = nar_check_phonemes(ph, len, err_pos);
    if (r) return r;

    u = (sp_utt *)malloc(sizeof *u);
    fs = (freq_shift *)malloc(sizeof *fs);
    if (!u || !fs) { free(u); free(fs); return ND_NoMem; }
    fs->sink = sink;
    fs->user = user;
    fs->active = (eff.sampfreq != DEFFREQ);
    if (fs->active)
        rs_init(&fs->rs, (double)NAR_OUTPUT_RATE * (double)eff.sampfreq / DEFFREQ,
                (double)NAR_OUTPUT_RATE, sink, user);

    while (rc == 0 && (r = sp_parse_sentence(ph, len, &pos, u, err_pos)) > 0) {
        sp_allophones(u);
        sp_prosody(u, &eff);
        if (eff.voice == NAR_VOICE_CLASSIC)
            rc = voice_classic_render(u, &eff, shift_sink, fs);
        else
            rc = voice_natural_render(u, &eff, shift_sink, fs);
    }
    if (r < 0 && rc == 0) rc = r;
    if (rc == 0 && fs->active) rc = rs_flush(&fs->rs);
    free(u);
    free(fs);
    return rc < 0 ? rc : 0;
}
