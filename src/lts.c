// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>
//
// Letter-to-sound fallback through CMU Flite's cmulex LTS decision trees
// (third_party/flite, BSD-style licence). Only the tree walker (cst_lts.c)
// and the model tables are used.

#include "lexicon.h"
#include <string.h>

#include "cst_val.h"

/* Flite's cst_val layout depends on WORDS_BIGENDIAN (configure sets it);
   without it, lists are corrupted on big-endian CPUs (found on PPC). */
#if defined(__BIG_ENDIAN__) && !defined(WORDS_BIGENDIAN)
#error "define WORDS_BIGENDIAN when building for a big-endian CPU"
#endif
#include "cst_lts.h"

extern const char *const cmu_lts_phone_table[];
extern const cst_lts_addr cmu_lts_letter_index[];
extern const cst_lts_model cmu_lts_model[];

static cst_lts_rules rules;
static int rules_ready;

static void init_rules(void)
{
    if (rules_ready) return;
    rules.name = "cmu";
    rules.letter_index = cmu_lts_letter_index;
    rules.models = cmu_lts_model;
    rules.phone_table = cmu_lts_phone_table;
    rules.context_window_size = 4;
    rules.context_extra_feats = 1;
    rules.letter_table = 0;
    rules_ready = 1;
}

int lx_lts_cmu(const char *word, uint8_t *out, int max)
{
    char w[64];
    int i, n = 0;
    cst_val *ph;
    const cst_val *v;

    for (i = 0; word[i] && i < 63; i++) {
        char c = word[i];
        if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
        if (c < 'a' || c > 'z') return 0;
        w[i] = c;
    }
    w[i] = 0;
    if (!w[0]) return 0;
    init_rules();
    ph = lts_apply(w, "", &rules);
    for (v = ph; v && n < max; v = val_cdr(v)) {
        const char *s = val_string(val_car(v));
        /* the model has merged phones such as "w-ey1": split on '-' */
        while (*s && n < max) {
            const char *e = strchr(s, '-');
            int len = e ? (int)(e - s) : (int)strlen(s);
            int code = lx_parse_phone(s, len);
            if (code > 0) out[n++] = (uint8_t)code;
            if (!e) break;
            s = e + 1;
        }
    }
    delete_val(ph);
    /* consonant-only words get no stress; vowel stresses come from the model */
    return n;
}
