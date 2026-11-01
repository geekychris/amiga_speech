// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>
//
// Text normalisation: raw English -> a stream of speakable tokens.

#ifndef SPEECH_NORMALIZE_H
#define SPEECH_NORMALIZE_H

#include <stddef.h>
#include "lexicon.h"

enum {
    NT_WORD = 0,    /* lowercase word to pronounce */
    NT_LETTER,      /* single letter to spell (text[0] = 'a'..'z') */
    NT_PUNCT        /* text[0] = '.', '?', ',', '-' */
};

/* Callback per token. src is the offset of the raw input token the word
   came from (several words can share one). Return non-zero to stop. */
typedef int (*nt_fn)(void *user, int kind, const char *text, size_t src);

/* dict (may be NULL) is used to tell acronyms from words. Returns 0, or
   the callback's non-zero value if it stopped early. */
int nt_normalize(const char *in, size_t len, const lx_dict *dict,
                 nt_fn fn, void *user);

/* number helpers (exposed for tests): append words for n to buf */
void nt_cardinal(unsigned long n, char *buf, size_t size);
void nt_ordinal(unsigned long n, char *buf, size_t size);
void nt_year(unsigned long n, char *buf, size_t size);

#endif
