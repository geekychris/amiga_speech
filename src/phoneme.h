// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>
//
// The classic Amiga narrator phoneme set and per-phoneme data shared by
// both voices (timing for prosody, formant targets for the classic voice,
// unit names for the natural voice).

#ifndef SPEECH_PHONEME_H
#define SPEECH_PHONEME_H

#include <stdint.h>

enum {
    PH_PAU = 0,
    /* vowels */
    PH_IY, PH_IH, PH_EH, PH_AE, PH_AA, PH_AH, PH_AO, PH_UH, PH_ER, PH_OH,
    PH_AX, PH_IX,
    /* diphthongs */
    PH_EY, PH_AY, PH_OY, PH_AW, PH_OW, PH_UW,
    /* consonants */
    PH_R, PH_L, PH_W, PH_Y, PH_M, PH_N, PH_NX, PH_SH, PH_S, PH_TH, PH_F,
    PH_ZH, PH_Z, PH_DH, PH_V, PH_WH, PH_CH, PH_J, PH_HH, PH_KX,
    PH_B, PH_P, PH_D, PH_T, PH_K, PH_G,
    /* special symbols */
    PH_DX, PH_LX, PH_RX, PH_Q, PH_QX,
    PH_COUNT
};

/* phoneme class flags */
#define PF_VOWEL   0x0001
#define PF_DIPH    0x0002
#define PF_VOICED  0x0004
#define PF_STOP    0x0008
#define PF_FRIC    0x0010
#define PF_NASAL   0x0020
#define PF_LIQUID  0x0040
#define PF_GLIDE   0x0080
#define PF_AFFR    0x0100
#define PF_SILENT  0x0200
#define PF_REDUCED 0x0400   /* AX, IX: never stressed */
#define PF_ASPIR   0x0800   /* /H: takes the next vowel's formants */
#define PF_SONOR   (PF_VOWEL | PF_NASAL | PF_LIQUID | PF_GLIDE)

/* Formant target set for the classic voice (male, Hz). */
typedef struct {
    int16_t f1, f2, f3;     /* steady-state (or diphthong start) */
    int16_t e1, e2, e3;     /* diphthong end targets; 0 = same as start */
    int16_t b1, b2, b3;
    int8_t  av;             /* voicing amplitude, dB (0 = off) */
    int8_t  af;             /* frication amplitude, dB */
    int8_t  ah;             /* aspiration amplitude, dB */
    int8_t  a2, a3, a4, a5, a6, ab; /* parallel (frication) amplitudes, dB */
    int16_t fnz;            /* nasal zero (0 = no nasal coupling) */
} ph_targets;

typedef struct {
    const char *name;       /* Amiga code: "IY", "/H", ... */
    uint16_t flags;
    uint16_t inh;           /* inherent duration, ms (Klatt 1979 style) */
    uint16_t min;           /* minimum duration, ms */
    uint8_t  rank;          /* coarticulation dominance */
    const char *unit;       /* natural-voice phone name ("" = none) */
    ph_targets t;
} ph_info;

extern const ph_info ph_table[PH_COUNT];

/* Look up a phoneme code at s (1 or 2 chars). Returns the phoneme id and
   sets *used to the number of chars consumed, or -1 if no match. Also
   recognises the contractions (UL, IL, UM, IM, UN, IN), returning the
   vowel and setting *second to the consonant (else -1). */
int ph_lookup(const char *s, int avail, int *used, int *second);

static inline int ph_is_vowel(int ph) { return (ph_table[ph].flags & PF_VOWEL) != 0; }

#endif
