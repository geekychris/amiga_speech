// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>
//
// Classic narrator phoneme-string parser and allophone rules.
//
// Input syntax (as documented for narrator.device):
//   phoneme codes (IY, /H, NX, ...), contractions (UL = AXL, IN = IXN, ...),
//   stress digits 1-9 directly after a vowel, spaces between words,
//   punctuation: . ? (sentence end), , (clause), - (phrase), ( ) noun
//   phrase. Parsing stops at NUL, '#' or the given length.

#include "speech.h"
#include <string.h>

static int is_space(int c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

static void add(sp_utt *u, int ph, int punct, int flags)
{
    sp_seg *g = &u->seg[u->nseg++];
    memset(g, 0, sizeof *g);
    g->ph = (uint8_t)ph;
    g->punct = (uint8_t)punct;
    g->flags = (uint8_t)flags;
}

static void end_word(sp_utt *u)
{
    if (u->nseg > 0 && u->seg[u->nseg - 1].ph != PH_PAU)
        u->seg[u->nseg - 1].flags |= SF_WEND;
}

/* word/syllable bookkeeping after parsing */
static void mark_words(sp_utt *u)
{
    int i = 0;
    while (i < u->nseg) {
        int j, k, nv = 0, lastv = -1;
        if (u->seg[i].ph == PH_PAU) { i++; continue; }
        /* word = i .. j-1 */
        for (j = i; j < u->nseg && u->seg[j].ph != PH_PAU; j++) {
            if (ph_is_vowel(u->seg[j].ph)) { nv++; lastv = j; }
            if (u->seg[j].flags & SF_WEND) { j++; break; }
        }
        /* the word-final syllable starts at the consonant(s) before the last
           vowel: take at most one onset consonant */
        if (lastv >= 0) {
            int s0 = lastv;
            if (s0 > i && !ph_is_vowel(u->seg[s0 - 1].ph))
                s0--;
            for (k = s0; k < j; k++)
                u->seg[k].flags |= SF_WFINALSYL;
        }
        if (nv > 1)
            for (k = i; k < j; k++)
                u->seg[k].flags |= SF_POLY;
        u->seg[i].flags |= SF_WSTART;
        u->seg[j - 1].flags |= SF_WEND;
        /* phrase-final: next non-word thing is a punctuated pause */
        if (j < u->nseg && u->seg[j].ph == PH_PAU && u->seg[j].punct) {
            for (k = i; k < j; k++)
                if (u->seg[k].flags & SF_WFINALSYL)
                    u->seg[k].flags |= SF_FINALSYL;
        }
        i = j;
    }
}

int sp_parse_sentence(const char *s, size_t len, size_t *pos, sp_utt *u,
                      size_t *err_pos)
{
    size_t i = *pos;
    int in_np = 0, done = 0;

    while (i < len && s[i] && s[i] != '#' && is_space((unsigned char)s[i]))
        i++;
    if (i >= len || !s[i] || s[i] == '#') {
        *pos = i;
        return 0;
    }
    u->nseg = 0;
    u->nf0 = 0;
    u->total_ms = 0;
    u->final_punct = '-';
    add(u, PH_PAU, 0, 0);

    while (!done && i < len && s[i] && s[i] != '#') {
        int c = (unsigned char)s[i];
        if (is_space(c)) {
            end_word(u);
            i++;
            continue;
        }
        if (c == '.' || c == '?' || c == '!') {
            end_word(u);
            add(u, PH_PAU, c == '?' ? '?' : '.', 0);
            u->final_punct = (char)(c == '?' ? '?' : '.');
            i++;
            done = 1;
            break;
        }
        if (c == ',' || c == '-' || c == ';' || c == ':') {
            end_word(u);
            if (u->seg[u->nseg - 1].ph == PH_PAU)  /* collapse ", -" */
                u->seg[u->nseg - 1].punct = (uint8_t)(c == '-' ? '-' : ',');
            else
                add(u, PH_PAU, c == '-' ? '-' : ',', 0);
            i++;
            continue;
        }
        if (c == '(' || c == ')') {
            end_word(u);
            in_np = (c == '(');
            i++;
            continue;
        }
        if (c >= '0' && c <= '9') {
            sp_seg *last = u->nseg ? &u->seg[u->nseg - 1] : NULL;
            if (c == '0' || !last || !ph_is_vowel(last->ph)) {
                if (err_pos) *err_pos = i;
                return ND_PhonErr;
            }
            last->stress = (uint8_t)(c - '0');
            i++;
            continue;
        }
        {
            int used = 0, second = -1;
            int ph = ph_lookup(s + i, (int)(len - i), &used, &second);
            if (ph < 0) {
                if (err_pos) *err_pos = i;
                return ND_PhonErr;
            }
            /* split a too-long sentence at a word boundary */
            if (u->nseg >= SP_MAX_SEGS - 8 &&
                (u->seg[u->nseg - 1].flags & SF_WEND)) {
                break;
            }
            if (u->nseg >= SP_MAX_SEGS - 4) {
                /* a single "word" longer than the buffer */
                end_word(u);
                break;
            }
            add(u, ph, 0, in_np ? SF_NP : 0);
            if (second >= 0) {
                /* contraction stress digit goes on the vowel; the digit
                   follows the 2-char code, e.g. UL3 is unusual but legal */
                add(u, second, 0, in_np ? SF_NP : 0);
                if (i + used < len && s[i + used] >= '1' && s[i + used] <= '9') {
                    u->seg[u->nseg - 2].stress = (uint8_t)(s[i + used] - '0');
                    used++;
                }
            }
            i += (size_t)used;
        }
    }
    end_word(u);
    if (u->seg[u->nseg - 1].ph != PH_PAU)
        add(u, PH_PAU, '-', 0);
    else if (!u->seg[u->nseg - 1].punct)
        u->seg[u->nseg - 1].punct = '-';
    if (!done && u->seg[u->nseg - 1].punct)
        u->final_punct = (char)u->seg[u->nseg - 1].punct;
    if (u->final_punct == ',')
        u->final_punct = '-';
    add(u, PH_PAU, 0, 0);  /* trailing edge silence */
    mark_words(u);
    *pos = i;
    return 1;
}

static int voiceless_stop(int ph)
{
    return ph == PH_P || ph == PH_T || ph == PH_K || ph == PH_CH;
}

/* glottal stop between a vowel-final word and a vowel-initial word
   ("the Amiga", "go on"), as narrator does */
static void insert_glottal_stops(sp_utt *u)
{
    int i;
    for (i = 1; i + 1 < u->nseg && u->nseg < SP_MAX_SEGS - 1; i++) {
        sp_seg *a = &u->seg[i], *b = &u->seg[i + 1];
        if ((a->flags & SF_WEND) && (b->flags & SF_WSTART) &&
            ph_is_vowel(a->ph) && ph_is_vowel(b->ph)) {
            memmove(&u->seg[i + 2], &u->seg[i + 1], (size_t)(u->nseg - i - 1) * sizeof(sp_seg));
            memset(&u->seg[i + 1], 0, sizeof(sp_seg));
            u->seg[i + 1].ph = PH_Q;
            u->seg[i + 1].flags = u->seg[i].flags & SF_NP;
            u->nseg++;
            i++;
        }
    }
}

void sp_allophones(sp_utt *u)
{
    int i;
    insert_glottal_stops(u);
    for (i = 1; i < u->nseg - 1; i++) {
        sp_seg *g = &u->seg[i], *pv = &u->seg[i - 1], *nx = &u->seg[i + 1];
        int prev_v = ph_is_vowel(pv->ph) && !(g->flags & SF_WSTART);
        int next_v = ph_is_vowel(nx->ph) && !(g->flags & SF_WEND);

        /* post-vocalic L and R */
        if (g->ph == PH_L && prev_v && !next_v)
            g->ph = PH_LX;
        else if (g->ph == PH_R && prev_v && !next_v)
            g->ph = PH_RX;

        /* flap: T/D between vowels inside a word, before an unstressed
           vowel ("pity", "butter") */
        if ((g->ph == PH_T || g->ph == PH_D) && prev_v && next_v &&
            nx->stress == 0 && pv->ph != PH_PAU)
            g->ph = PH_DX;

        /* aspiration of voiceless stops before a sonorant, not after S */
        if (voiceless_stop(g->ph)) {
            int after_s = (pv->ph == PH_S) && !(g->flags & SF_WSTART);
            int son = (ph_table[nx->ph].flags & PF_SONOR) != 0;
            if (son && !after_s)
                g->flags |= SF_ASPIRATE;
        }
    }
}
