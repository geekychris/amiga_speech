// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>
//
// Rule-only letter-to-sound for the "classic" translator mode: the NRL
// rules (NRL Report 7948, Elovitz, Johnson, McHugh & Shore, 1976) in John
// A. Wasser's public-domain table (third_party/nrl/english.c). This matcher
// is a re-implementation of the rule semantics described in that file.
// The rules give no stress; we put primary stress on the first full vowel,
// roughly what a 1985 rule-only translator could do.

#include "lexicon.h"
#include <string.h>

typedef char *Rule[4];
extern Rule *nrl_rules[];

static int isvow(int c) { return c == 'A' || c == 'E' || c == 'I' || c == 'O' || c == 'U'; }
static int iscons(int c) { return c >= 'A' && c <= 'Z' && !isvow(c); }
static int isalpha_u(int c) { return c >= 'A' && c <= 'Z'; }

static int leftmatch(const char *pat, const char *word, int idx)
{
    int n = (int)strlen(pat);
    const char *p = pat + n - 1;
    int t = idx;  /* index of char before the match */
    for (; n > 0; p--, n--) {
        int c = (unsigned char)*p;
        int w = t >= 0 ? (unsigned char)word[t] : 0;
        if (isalpha_u(c) || c == '\'' || c == ' ') {
            if (c != w) return 0;
            t--;
            continue;
        }
        switch (c) {
        case '#': if (!isvow(w)) return 0; t--; while (t >= 0 && isvow((unsigned char)word[t])) t--; break;
        case ':': while (t >= 0 && iscons((unsigned char)word[t])) t--; break;
        case '^': if (!iscons(w)) return 0; t--; break;
        case '.': if (!w || !strchr("BDVGJLMNRWZ", w)) return 0; t--; break;
        case '+': if (w != 'E' && w != 'I' && w != 'Y') return 0; t--; break;
        default: return 0;
        }
    }
    return 1;
}

static int rightmatch(const char *pat, const char *word, int idx)
{
    const char *p;
    int t = idx;
    for (p = pat; *p; p++) {
        int c = (unsigned char)*p;
        int w = (unsigned char)word[t];
        if (isalpha_u(c) || c == '\'' || c == ' ') {
            if (c != w) return 0;
            t++;
            continue;
        }
        switch (c) {
        case '#': if (!isvow(w)) return 0; t++; while (isvow((unsigned char)word[t])) t++; break;
        case ':': while (iscons((unsigned char)word[t])) t++; break;
        case '^': if (!iscons(w)) return 0; t++; break;
        case '.': if (!w || !strchr("BDVGJLMNRWZ", w)) return 0; t++; break;
        case '+': if (w != 'E' && w != 'I' && w != 'Y') return 0; t++; break;
        case '%':
            /* suffix: ER, E, ES, ED, ING, ELY */
            if (w == 'E') {
                const char *s = word + t + 1;
                if (strncmp(s, "LY", 2) == 0) t += 3;
                else if (*s == 'R' || *s == 'S' || *s == 'D') t += 2;
                else t += 1;
            } else if (strncmp(word + t, "ING", 3) == 0) {
                t += 3;
            } else return 0;
            break;
        default: return 0;
        }
    }
    return 1;
}

/* NRL output symbols -> ARPAbet codes */
static int emit(const char *o, uint8_t *out, int n, int max)
{
    while (*o && n < max) {
        char name[3] = { 0, 0, 0 };
        int code;
        if (*o == ' ') { o++; continue; }
        if (o[0] >= 'A' && o[0] <= 'Z' && o[1] >= 'A' && o[1] <= 'Z') {
            name[0] = o[0]; name[1] = o[1];
            o += 2;
        } else {
            name[0] = (char)(*o >= 'a' && *o <= 'z' ? *o - 32 : *o);
            o += 1;
        }
        if (strcmp(name, "J") == 0) { name[0] = 'J'; name[1] = 'H'; }
        if (strcmp(name, "H") == 0) { name[0] = 'H'; name[1] = 'H'; }
        if (strcmp(name, "WH") == 0) { name[0] = 'W'; name[1] = 0; }
        if (strcmp(name, "AX") == 0) { name[1] = 'H'; }   /* AX = AH0 */
        code = lx_parse_phone(name, (int)strlen(name));
        if (code > 0) out[n++] = (uint8_t)code;
    }
    return n;
}

int lx_lts_nrl(const char *word, uint8_t *out, int max)
{
    char w[68];
    int i, len, idx, n = 0, stressed = 0;

    w[0] = ' ';
    for (len = 0; word[len] && len < 64; len++) {
        char c = word[len];
        if (c >= 'a' && c <= 'z') c = (char)(c - 32);
        w[1 + len] = c;
    }
    w[1 + len] = ' ';
    w[2 + len] = 0;

    idx = 1;
    while (w[idx] && w[idx] != ' ' && n < max) {
        int type = isalpha_u((unsigned char)w[idx]) ? w[idx] - 'A' + 1 : 0;
        Rule *r = nrl_rules[type];
        int matched = 0;
        for (; (*r)[1]; r++) {
            const char *m = (*r)[1];
            int k = idx;
            while (*m && *m == w[k]) { m++; k++; }
            if (*m) continue;
            if (!leftmatch((*r)[0], w, idx - 1)) continue;
            if (!rightmatch((*r)[2], w, k)) continue;
            n = emit((*r)[3], out, n, max);
            idx = k;
            matched = 1;
            break;
        }
        if (!matched) idx++;
    }
    /* primary stress on the first full vowel (else the first vowel) */
    for (i = 0; i < n; i++) {
        if (lx_is_vowel(out[i]) && lx_phone(out[i]) != 2 /* AH */) {
            out[i] = (uint8_t)lx_code(lx_phone(out[i]), 1);
            stressed = 1;
            break;
        }
    }
    if (!stressed)
        for (i = 0; i < n; i++)
            if (lx_is_vowel(out[i])) { out[i] = (uint8_t)lx_code(lx_phone(out[i]), 1); break; }
    return n;
}
