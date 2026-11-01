// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>
//
// translator.library core: normalised words -> ARPAbet (CMU dictionary,
// CMU LTS fallback, or NRL rules in classic mode) -> narrator phonemes with
// stress digits.

#include "amiga_speech/translator.h"
#include "lexicon.h"
#include "normalize.h"
#include <string.h>

static lx_dict g_dict;
static int g_have_dict;

int tr_set_dictionary(const uint8_t *image, size_t size)
{
    g_have_dict = 0;
    if (!image) return 0;
    if (lx_dict_open(&g_dict, image, size) != 0) return -1;
    g_have_dict = 1;
    return 0;
}

/* ---- word classes for stress ------------------------------------------- */

static const char *const function_words[] = {
    "a","an","the","and","or","but","nor","so","yet","of","to","in","on","at",
    "by","for","from","with","as","into","onto","than","then","if","that",
    "is","am","are","was","were","be","been","being","do","does","did","has",
    "have","had","will","would","shall","should","can","could","may","might",
    "must","its","it's","not","there","their","this","these","those","such",
    "upon","via","per","whose","who","which","what's","i'm","you're","we're",
    "they're","he's","she's","i'll","you'll","we'll","i've","you've","we've",
    "isn't","aren't","wasn't","don't","doesn't","didn't","can't","won't",
    "about","over","under","after","before","through","because","while",
    NULL
};
static const char *const pronouns[] = {
    "i","me","my","mine","you","your","yours","he","him","his","she","her",
    "hers","it","we","us","our","ours","they","them","myself","yourself",
    "himself","herself","itself","ourselves","themselves",
    NULL
};

static int in_list(const char *const *l, const char *w)
{
    for (; *l; l++)
        if (strcmp(*l, w) == 0) return 1;
    return 0;
}

/* ---- letters ------------------------------------------------------------- */

static const char *const letter_arpa[26] = {
    "EY1", "B IY1", "S IY1", "D IY1", "IY1", "EH1 F", "JH IY1", "EY1 CH",
    "AY1", "JH EY1", "K EY1", "EH1 L", "EH1 M", "EH1 N", "OW1", "P IY1",
    "K Y UW1", "AA1 R", "EH1 S", "T IY1", "Y UW1", "V IY1",
    "D AH1 B AH0 L Y UW0", "EH1 K S", "W AY1", "Z IY1"
};

static int parse_arpa_string(const char *s, uint8_t *out, int max)
{
    int n = 0;
    while (*s && n < max) {
        const char *e;
        int code;
        while (*s == ' ') s++;
        e = s;
        while (*e && *e != ' ') e++;
        if (e > s) {
            code = lx_parse_phone(s, (int)(e - s));
            if (code > 0) out[n++] = (uint8_t)code;
        }
        s = e;
    }
    return n;
}

/* ---- word -> phones -------------------------------------------------------- */

static int lookup_or_lts(const char *w, uint8_t *out, int max, char *src)
{
    int n = 0;
    if (g_have_dict) n = lx_dict_lookup(&g_dict, w, out, max);
    if (n > 0) { *src = 'D'; return n; }
    n = lx_lts_cmu(w, out, max);
    *src = 'L';
    return n;
}

static int word_codes(const char *w, int mode, uint8_t *out, int max, char *src)
{
    char clean[64];
    size_t i, k = 0, len = strlen(w);
    int n;

    if (mode == TR_MODE_CLASSIC) {
        *src = 'N';
        return lx_lts_nrl(w, out, max);
    }
    if (g_have_dict) {
        n = lx_dict_lookup(&g_dict, w, out, max);
        if (n > 0) { *src = 'D'; return n; }
    }
    /* possessive: stem + /Z/, /S/ or /IH Z/ */
    if (len > 2 && w[len - 2] == '\'' && w[len - 1] == 's') {
        char stem[64];
        int last;
        memcpy(stem, w, len - 2);
        stem[len - 2] = 0;
        n = lookup_or_lts(stem, out, max - 2, src);
        if (n > 0) {
            last = lx_phone(out[n - 1]);
            if (last == 30 || last == 37 || last == 31 || last == 38 || last == 16 || last == 22) {
                out[n++] = (uint8_t)lx_code(9, 0);            /* IH0 Z */
                out[n++] = (uint8_t)lx_code(37, 0);
            } else if (last == 28 || last == 32 || last == 23 || last == 19 || last == 33) {
                out[n++] = (uint8_t)lx_code(30, 0);           /* S */
            } else {
                out[n++] = (uint8_t)lx_code(37, 0);           /* Z */
            }
            return n;
        }
    }
    for (i = 0; i < len && k < 63; i++)
        if (w[i] >= 'a' && w[i] <= 'z') clean[k++] = w[i];
    clean[k] = 0;
    return lookup_or_lts(clean, out, max, src);
}

/* ---- ARPAbet -> Amiga ---------------------------------------------------- */

enum { ST_CONTENT, ST_FUNCTION, ST_PRONOUN, ST_LETTER };

static int amiga_word(const uint8_t *c, int n, int cls, char *out, int max)
{
    int i, len = 0, nv = 0, has_primary = 0, first_v = -1;
    for (i = 0; i < n; i++)
        if (lx_is_vowel(c[i])) {
            nv++;
            if (first_v < 0) first_v = i;
            if (lx_stress(c[i]) == 1) has_primary = 1;
        }
    for (i = 0; i < n; i++) {
        int ph = lx_phone(c[i]), st = lx_stress(c[i]);
        const char *nm = lx_arpa[ph];
        char dig = 0;
        int next_v = (i + 2 < n) ? lx_is_vowel(c[i + 2]) : 0;
        if (len + 4 >= max) break;
        if (lx_is_vowel(c[i])) {
            /* no primary at all (LTS on a short word): stress the first vowel */
            if (!has_primary && i == first_v && cls == ST_CONTENT) st = 1;
            if (ph == 2) nm = st == 0 ? "AX" : "AH";
            else if (ph == 9 && st == 0 && nv > 1) nm = "IX";
            else if (ph == 3 && i + 1 < n && lx_phone(c[i + 1]) == 29) nm = "OH";
            /* contractions UL UN UM IL IN IM (consonant not starting a syllable) */
            if ((strcmp(nm, "AX") == 0 || strcmp(nm, "IX") == 0) && i + 1 < n && !next_v) {
                int cp = lx_phone(c[i + 1]);
                char cc = cp == 24 ? 'L' : cp == 26 ? 'N' : cp == 25 ? 'M' : 0;
                if (cc) {
                    out[len++] = nm[0] == 'A' ? 'U' : 'I';
                    out[len++] = cc;
                    i++;
                    continue;
                }
            }
            switch (cls) {
            case ST_CONTENT: dig = st == 1 ? '4' : st == 2 ? '1' : 0; break;
            case ST_FUNCTION: dig = (st == 1 && nv > 1) ? '2' : 0; break;
            case ST_PRONOUN: dig = 0; break;
            case ST_LETTER: dig = st == 1 ? '3' : 0; break;
            }
        } else {
            if (ph == 21) nm = "/H";
            else if (ph == 22) nm = "J";
            else if (ph == 27) nm = "NX";
        }
        out[len++] = nm[0];
        if (nm[1]) out[len++] = nm[1];
        if (dig) out[len++] = dig;
    }
    out[len] = 0;
    return len;
}

/* ---- Translate ------------------------------------------------------------- */

typedef struct {
    char *out;
    long outlen, pos;
    int mode;
    size_t cur_src;
    long tok_start;     /* output position where the current input token began */
    long phrase_start;
    int started;
    size_t stop_src;
    long the_pos;       /* output offset of a preceding "DHAX", or -1 */
} tctx;

static void nuclear_accent(tctx *t)
{
    long i;
    for (i = t->pos - 1; i >= t->phrase_start; i--) {
        if (t->out[i] == '4') { t->out[i] = '5'; break; }
        if (t->out[i] == '5') break;
    }
}

static int put(tctx *t, const char *s, int space_before)
{
    long need = (long)strlen(s) + (space_before ? 1 : 0);
    if (t->pos + need + 1 > t->outlen) return 1;
    if (space_before) t->out[t->pos++] = ' ';
    memcpy(t->out + t->pos, s, strlen(s));
    t->pos += (long)strlen(s);
    t->out[t->pos] = 0;
    return 0;
}

static int on_token(void *user, int kind, const char *text, size_t src)
{
    tctx *t = (tctx *)user;
    char buf[200];
    uint8_t codes[LX_MAX_PHONES];
    int n, cls;
    char srcc;

    if (!t->started || src != t->cur_src) {
        t->cur_src = src;
        t->tok_start = t->pos;
        t->started = 1;
    }
    if (kind == NT_PUNCT) {
        char p[2];
        nuclear_accent(t);
        p[0] = text[0];
        p[1] = 0;
        if (put(t, p, 0)) goto overflow;
        t->phrase_start = t->pos;
        t->the_pos = -1;
        return 0;
    }
    if (kind == NT_LETTER) {
        n = parse_arpa_string(letter_arpa[text[0] - 'a'], codes, LX_MAX_PHONES);
        cls = ST_LETTER;
    } else {
        n = word_codes(text, t->mode, codes, LX_MAX_PHONES, &srcc);
        cls = in_list(function_words, text) ? ST_FUNCTION
            : in_list(pronouns, text) ? ST_PRONOUN : ST_CONTENT;
    }
    if (n <= 0) return 0;
    amiga_word(codes, n, cls, buf, (int)sizeof buf);
    /* "the" before a vowel is DHIY */
    if (t->the_pos >= 0 && strchr("AEIOU", buf[0]) &&
        t->the_pos + 4 <= t->pos && memcmp(t->out + t->the_pos, "DHAX", 4) == 0) {
        t->out[t->the_pos + 2] = 'I';
        t->out[t->the_pos + 3] = 'Y';
    }
    t->the_pos = -1;
    if (put(t, buf, t->pos > 0)) goto overflow;
    if (kind == NT_WORD && strcmp(text, "the") == 0 && strcmp(buf, "DHAX") == 0)
        t->the_pos = t->pos - 4;
    return 0;

overflow:
    t->pos = t->tok_start;
    while (t->pos > 0 && t->out[t->pos - 1] == ' ') t->pos--;
    t->out[t->pos] = 0;
    t->stop_src = src;
    return 1;
}

long tr_translate_mode(const char *in, long inlen, char *out, long outlen, int mode)
{
    tctx t;
    int r;
    if (!out || outlen <= 0) return TR_NotUsed;
    out[0] = 0;
    if (!in || inlen <= 0) return 0;
    memset(&t, 0, sizeof t);
    t.out = out;
    t.outlen = outlen;
    t.mode = mode;
    t.the_pos = -1;
    r = nt_normalize(in, (size_t)inlen, g_have_dict ? &g_dict : NULL, on_token, &t);
    if (r) {
        /* stopped at a word boundary: tell the caller where to resume */
        return t.stop_src > 0 ? -(long)t.stop_src : TR_NotUsed;
    }
    nuclear_accent(&t);
    while (t.pos > 0 && out[t.pos - 1] == ' ') out[--t.pos] = 0;
    return 0;
}

long Translate(const char *inString, long inLength, char *outBuffer, long outLength)
{
    return tr_translate_mode(inString, inLength, outBuffer, outLength, TR_MODE_DICT);
}

/* ---- debug / test helpers --------------------------------------------------- */

typedef struct { char *out; long outlen, pos; } dctx;

static int on_debug(void *user, int kind, const char *text, size_t src)
{
    dctx *d = (dctx *)user;
    const char *k = kind == NT_WORD ? "" : kind == NT_LETTER ? "#" : "";
    long need = (long)(strlen(k) + strlen(text) + 1);
    (void)src;
    if (d->pos + need + 1 > d->outlen) return 1;
    if (d->pos) d->out[d->pos++] = ' ';
    memcpy(d->out + d->pos, k, strlen(k)); d->pos += (long)strlen(k);
    memcpy(d->out + d->pos, text, strlen(text)); d->pos += (long)strlen(text);
    d->out[d->pos] = 0;
    return 0;
}

long tr_normalize_debug(const char *in, long inlen, char *out, long outlen)
{
    dctx d;
    d.out = out; d.outlen = outlen; d.pos = 0;
    out[0] = 0;
    return nt_normalize(in, (size_t)inlen, g_have_dict ? &g_dict : NULL, on_debug, &d);
}

int tr_word_arpabet(const char *word, int mode, char *out, int outlen, char *source)
{
    uint8_t codes[LX_MAX_PHONES];
    char s = '?';
    int n = word_codes(word, mode, codes, LX_MAX_PHONES, &s), i, pos = 0;
    out[0] = 0;
    for (i = 0; i < n; i++) {
        const char *nm = lx_arpa[lx_phone(codes[i])];
        int st = lx_stress(codes[i]);
        if (pos + 5 >= outlen) break;
        if (i) out[pos++] = ' ';
        out[pos++] = nm[0];
        if (nm[1]) out[pos++] = nm[1];
        if (st >= 0) out[pos++] = (char)('0' + st);
        out[pos] = 0;
    }
    if (source) *source = s;
    return n;
}
