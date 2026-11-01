// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>
//
// Portable translator core: English text -> classic narrator phonemes.
// Translate() has the translator.library signature and return convention.

#ifndef AMIGA_SPEECH_TRANSLATOR_H
#define AMIGA_SPEECH_TRANSLATOR_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TR_NotUsed  (-1)
#define TR_NoMem    (-2)
#define TR_MakeBad  (-4)

/* translation modes (extension) */
#define TR_MODE_DICT    0   /* normalise + CMU dictionary + CMU LTS (default) */
#define TR_MODE_CLASSIC 1   /* normalise + NRL letter-to-sound rules only */

/* Give the translator its dictionary image (an "AMSD" file loaded into
   memory; the memory must stay valid). Returns 0 or -1 if malformed.
   Without a dictionary, TR_MODE_DICT uses letter-to-sound for every word. */
int tr_set_dictionary(const uint8_t *image, size_t size);

/*
 * translator.library/Translate: returns 0 on success. If outBuffer would
 * overflow, translation stops at a word boundary and the result is the
 * negated offset into inString where translation stopped; call again from
 * there. outBuffer is always NUL-terminated.
 */
long Translate(const char *inString, long inLength, char *outBuffer, long outLength);

long tr_translate_mode(const char *in, long inlen, char *out, long outlen, int mode);

/* Expose the stages for tests and tools. */
/* text -> normalised words, one per line kind:text (see normalize.c) */
long tr_normalize_debug(const char *in, long inlen, char *out, long outlen);
/* single lowercase word -> ARPAbet ("HH AH0 L OW1"); source: 'D'ictionary,
   'L'TS, 'N'RL, 'S'pelled. Returns number of phones. */
int tr_word_arpabet(const char *word, int mode, char *out, int outlen, char *source);

#ifdef __cplusplus
}
#endif

#endif
