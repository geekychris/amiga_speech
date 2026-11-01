// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>
//
// Pronunciations as compact byte codes, the compressed CMU dictionary
// reader, and the letter-to-sound back ends.
//
// Phone code (1 byte): ARPAbet vowel v (0..14) with stress s (0..2) is
// 1 + v*3 + s (1..45); consonant c (15..38) is 46 + (c - 15) (46..69).

#ifndef SPEECH_LEXICON_H
#define SPEECH_LEXICON_H

#include <stddef.h>
#include <stdint.h>

#define LX_MAX_PHONES 48
#define LX_NVOWELS 15
#define LX_NPHONES 39

extern const char *const lx_arpa[LX_NPHONES];

int lx_code(int phone, int stress);           /* -> code */
static inline int lx_is_vowel(int code) { return code >= 1 && code <= 45; }
static inline int lx_phone(int code) { return code <= 45 ? (code - 1) / 3 : code - 46 + 15; }
static inline int lx_stress(int code) { return code <= 45 ? (code - 1) % 3 : -1; }
/* "AH0" / "ah1" / "hh" -> code, or -1 */
int lx_parse_phone(const char *s, int len);

/* ---- compressed dictionary ("AMSD" file, big-endian) ------------------- */
/*
 * header (16 bytes): "AMSD", u16 version (1), u16 block size,
 *                    u32 entry count, u32 block count
 * u32 block_offset[block count]   (from start of entry data)
 * entry data: per entry
 *     u8  bytes shared with the previous word (0 at block start)
 *     word suffix, ASCII a-z and ', last byte | 0x80
 *     phone codes, last byte | 0x80
 */
typedef struct {
    const uint8_t *base;
    size_t size;
    uint32_t nentries, nblocks, blocksize;
    const uint8_t *offsets;
    const uint8_t *data;
} lx_dict;

int lx_dict_open(lx_dict *d, const uint8_t *buf, size_t size);
/* lowercase word -> number of phone codes written to out (0 = not found) */
int lx_dict_lookup(const lx_dict *d, const char *word, uint8_t *out, int max);

/* ---- letter to sound ---------------------------------------------------- */
/* CMU Flite LTS model (trained on cmudict): lowercase a-z word */
int lx_lts_cmu(const char *word, uint8_t *out, int max);
/* NRL (Elovitz et al. 1976) rules, no stress: uppercase-insensitive */
int lx_lts_nrl(const char *word, uint8_t *out, int max);

#endif
