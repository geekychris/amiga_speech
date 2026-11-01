// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>
//
// Portable narrator core: the part of narrator.device that turns a classic
// Amiga phoneme string plus narrator_rb-style settings into 16-bit PCM.
// No OS calls; the OS4 device wraps this (see docs/DESIGN.md).
//
// The constants mirror the V37 <devices/narrator.h> values so that code
// written against the real device can be compiled against this core.

#ifndef AMIGA_SPEECH_NARRATOR_H
#define AMIGA_SPEECH_NARRATOR_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Constants from the V37 narrator.device interface ------------------ */
/* (skipped when the SDK's <devices/narrator.h> is already included) */
#ifndef DEVICES_NARRATOR_H

#define NDB_NEWIORB  0
#define NDB_WORDSYNC 1
#define NDB_SYLSYNC  2
#define NDF_NEWIORB  (1 << NDB_NEWIORB)
#define NDF_WORDSYNC (1 << NDB_WORDSYNC)
#define NDF_SYLSYNC  (1 << NDB_SYLSYNC)

#define ND_NoMem       (-2)
#define ND_NoAudLib    (-3)
#define ND_MakeBad     (-4)
#define ND_UnitErr     (-5)
#define ND_CantAlloc   (-6)
#define ND_Unimpl      (-7)
#define ND_NoWrite     (-8)
#define ND_Expunged    (-9)
#define ND_PhonErr     (-20)
#define ND_RateErr     (-21)
#define ND_PitchErr    (-22)
#define ND_SexErr      (-23)
#define ND_ModeErr     (-24)
#define ND_FreqErr     (-25)
#define ND_VolErr      (-26)
#define ND_DCentErr    (-27)
#define ND_CentPhonErr (-28)

#define DEFPITCH    110
#define DEFRATE     150
#define DEFVOL      64
#define DEFFREQ     22200
#define MALE        0
#define FEMALE      1
#define NATURALF0   0
#define ROBOTICF0   1
#define MANUALF0    2
#define DEFSEX      MALE
#define DEFMODE     NATURALF0
#define DEFARTIC    100
#define DEFCENTRAL  0
#define DEFF0PERT   0
#define DEFF0ENTHUS 32
#define DEFPRIORITY 100

#define MINRATE     40
#define MAXRATE     400
#define MINPITCH    65
#define MAXPITCH    320
#define MINFREQ     5000
#define MAXFREQ     28000
#define MINVOL      0
#define MAXVOL      64
#define MINCENT     0
#define MAXCENT     100

#endif /* DEVICES_NARRATOR_H */

/* ---- Extensions (not in the classic device) ---------------------------- */

#define NAR_VOICE_CLASSIC 0   /* formant synthesizer ("voice A") */
#define NAR_VOICE_NATURAL 1   /* diphone concatenation ("voice B") */

/* Rate the core renders at before any sampfreq effect is applied. */
#define NAR_OUTPUT_RATE 22050

/*
 * Settings: the speech-relevant fields of struct narrator_rb, with the same
 * names, meanings and ranges. Audio-channel fields (ch_masks, nm_masks,
 * chanmask, numchan) and priority belong to the device wrapper and are not
 * here. `voice` is our extension (the device would take it from a prefs
 * file or a spare field; see DESIGN.md).
 */
typedef struct nar_params {
    uint16_t rate;          /* words/minute, MINRATE..MAXRATE */
    uint16_t pitch;         /* baseline F0 in Hz, MINPITCH..MAXPITCH */
    uint16_t mode;          /* NATURALF0 / ROBOTICF0 / MANUALF0 */
    uint16_t sex;           /* MALE / FEMALE */
    uint16_t volume;        /* 0..64 */
    uint16_t sampfreq;      /* "tuning" rate, MINFREQ..MAXFREQ; 22200 = normal */
    uint8_t  mouths;        /* accepted, unused by the host prototype */
    uint8_t  flags;         /* NDF_NEWIORB etc. Without NEWIORB the V37
                               fields below are ignored (defaults used). */
    uint8_t  F0enthusiasm;  /* 32nds, 32 = normal */
    uint8_t  F0perturb;     /* 0..255 */
    int8_t   F1adj, F2adj, F3adj;   /* +-5% steps */
    int8_t   A1adj, A2adj, A3adj;   /* dB */
    uint8_t  articulate;    /* transition time %, 100 = normal */
    uint8_t  centralize;    /* 0..100 % pull towards centphon */
    const char *centphon;   /* "IY","IH","EH","AE","AA","AH","AO","OW","UH","ER","UW" */
    int8_t   AVbias, AFbias;/* dB */
    uint8_t  voice;         /* NAR_VOICE_* (extension) */
} nar_params;

/* Fill in the device defaults (what OpenDevice() leaves in the IORB). */
void nar_params_default(nar_params *p);

/* Validate like CMD_WRITE does; returns 0 or an ND_* error code. */
int nar_params_check(const nar_params *p);

/* Audio sink: called with consecutive blocks of mono 16-bit samples at
   NAR_OUTPUT_RATE. Return non-zero to abort (like AbortIO). */
typedef int (*nar_sink_fn)(void *user, const int16_t *samples, size_t count);

/*
 * Speak a phoneme string (CMD_WRITE). `len` bytes are parsed or up to a
 * NUL or '#'. Returns 0 or an ND_* error code. On ND_PhonErr, *err_pos (if
 * non-NULL) receives the offset of the bad phoneme.
 */
int nar_speak(const nar_params *p, const char *phonemes, size_t len,
              nar_sink_fn sink, void *user, size_t *err_pos);

/* Timing events for mouths and word/syllable sync (CMD_READ). Called
   before the samples they refer to are delivered; sample_pos counts
   samples delivered to the sink since the start of the call. */
#define NAR_EV_WORD 0x01      /* first phoneme of a word */
#define NAR_EV_SYL  0x02      /* syllable nucleus (vowel) */
typedef void (*nar_event_fn)(void *user, uint32_t sample_pos, int phoneme, int flags);

/* nar_speak with an event callback (ev may be NULL). Phoneme ids are the
   narrator phonemes in table order; nar_phoneme_name() gives the code. */
int nar_speak_ex(const nar_params *p, const char *phonemes, size_t len,
                 nar_sink_fn sink, void *user, nar_event_fn ev, void *ev_user,
                 size_t *err_pos);
const char *nar_phoneme_name(int phoneme);   /* "" for silence */

/* Natural voice data: an "AMSV" voice file image (DEVS:narrator/kal16.voice)
   read into memory by the caller. The buffer must be writable (it is
   byte-swapped in place on little-endian hosts) and stay valid until it is
   replaced or NULL is passed. Returns 0, or -1 if the image is malformed.
   Without a voice the natural voice falls back to the classic one. */
int nar_set_voice_image(uint8_t *image, size_t size);
int nar_have_natural(void);

/* Check a phoneme string without synthesizing (returns 0 or ND_PhonErr). */
int nar_check_phonemes(const char *phonemes, size_t len, size_t *err_pos);

#ifdef __cplusplus
}
#endif

#endif
