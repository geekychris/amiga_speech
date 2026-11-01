// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>
//
// Text normalisation: numbers (cardinals, decimals, ordinals, years,
// decades, times, money, percentages), abbreviations, acronyms and
// initialisms, symbols, and punctuation -> narrator pauses.
// Works one whitespace-delimited token at a time with fixed buffers.

#include "normalize.h"
#include <string.h>

#define TOKMAX 128

typedef struct {
    const lx_dict *dict;
    nt_fn fn;
    void *user;
    size_t src;
    int last_punct;   /* last thing emitted was punctuation (or nothing) */
    int stop;
} nctx;

static int lower(int c) { return (c >= 'A' && c <= 'Z') ? c + 32 : c; }
static int is_up(int c) { return c >= 'A' && c <= 'Z'; }
static int is_lo(int c) { return c >= 'a' && c <= 'z'; }
static int is_al(int c) { return is_up(c) || is_lo(c); }
static int is_dig(int c) { return c >= '0' && c <= '9'; }

static int emit(nctx *c, int kind, const char *t)
{
    if (c->stop) return c->stop;
    if (kind == NT_PUNCT) {
        if (c->last_punct) return 0;      /* collapse runs; none at start */
        c->last_punct = 1;
    } else {
        c->last_punct = 0;
    }
    c->stop = c->fn(c->user, kind, t, c->src);
    return c->stop;
}

/* emit each space-separated word of s */
static void emit_words(nctx *c, const char *s)
{
    char w[48];
    while (*s && !c->stop) {
        int n = 0;
        while (*s == ' ') s++;
        while (*s && *s != ' ' && n < 47) w[n++] = *s++;
        w[n] = 0;
        if (n) emit(c, NT_WORD, w);
    }
}

static void spell(nctx *c, const char *s, size_t n)
{
    size_t i;
    for (i = 0; i < n && !c->stop; i++) {
        char l[2];
        if (is_al((unsigned char)s[i])) {
            l[0] = (char)lower((unsigned char)s[i]);
            l[1] = 0;
            emit(c, NT_LETTER, l);
        } else if (is_dig((unsigned char)s[i])) {
            static const char *d[] = { "zero","one","two","three","four","five","six","seven","eight","nine" };
            emit(c, NT_WORD, d[s[i] - '0']);
        }
    }
}

/* ---- numbers ------------------------------------------------------------ */

static const char *ones[] = { "zero","one","two","three","four","five","six","seven",
    "eight","nine","ten","eleven","twelve","thirteen","fourteen","fifteen",
    "sixteen","seventeen","eighteen","nineteen" };
static const char *tens[] = { "","","twenty","thirty","forty","fifty","sixty",
    "seventy","eighty","ninety" };

static void cat(char *buf, size_t size, const char *w)
{
    size_t l = strlen(buf);
    if (l && l + 1 < size) { buf[l++] = ' '; buf[l] = 0; }
    while (*w && l + 1 < size) buf[l++] = *w++;
    buf[l] = 0;
}

static void under1000(unsigned n, char *buf, size_t size)
{
    if (n >= 100) {
        cat(buf, size, ones[n / 100]);
        cat(buf, size, "hundred");
        n %= 100;
        if (!n) return;
    }
    if (n >= 20) {
        cat(buf, size, tens[n / 10]);
        if (n % 10) cat(buf, size, ones[n % 10]);
    } else {
        cat(buf, size, ones[n]);
    }
}

static void cardinal_ull(unsigned long long n, char *buf, size_t size)
{
    static const char *scale[] = { "", "thousand", "million", "billion", "trillion" };
    unsigned grp[5];
    int i, ng = 0;
    if (n == 0) { cat(buf, size, "zero"); return; }
    while (n && ng < 5) { grp[ng++] = (unsigned)(n % 1000); n /= 1000; }
    for (i = ng - 1; i >= 0; i--) {
        if (!grp[i]) continue;
        under1000(grp[i], buf, size);
        if (i) cat(buf, size, scale[i]);
    }
}

void nt_cardinal(unsigned long n, char *buf, size_t size)
{
    buf[0] = 0;
    cardinal_ull(n, buf, size);
}

/* turn the last word of buf into its ordinal form */
static void make_ordinal(char *buf, size_t size)
{
    static const struct { const char *c, *o; } irr[] = {
        { "one","first" }, { "two","second" }, { "three","third" }, { "five","fifth" },
        { "eight","eighth" }, { "nine","ninth" }, { "twelve","twelfth" },
    };
    char *last = strrchr(buf, ' ');
    size_t i, l;
    last = last ? last + 1 : buf;
    for (i = 0; i < sizeof irr / sizeof irr[0]; i++) {
        if (strcmp(last, irr[i].c) == 0) {
            *last = 0;
            l = strlen(buf);
            if (l && buf[l - 1] == ' ') buf[l - 1] = 0;
            cat(buf, size, irr[i].o);
            return;
        }
    }
    l = strlen(buf);
    if (l && buf[l - 1] == 'y') {
        buf[l - 1] = 0;
        strncat(buf, "ieth", size - strlen(buf) - 1);
    } else {
        strncat(buf, "th", size - strlen(buf) - 1);
    }
}

void nt_ordinal(unsigned long n, char *buf, size_t size)
{
    nt_cardinal(n, buf, size);
    make_ordinal(buf, size);
}

void nt_year(unsigned long n, char *buf, size_t size)
{
    unsigned hi = (unsigned)(n / 100), lo = (unsigned)(n % 100);
    buf[0] = 0;
    if (n >= 2000 && n < 2010) {            /* two thousand (five) */
        cardinal_ull(n, buf, size);
        return;
    }
    if (n < 1000 || n > 9999) { cardinal_ull(n, buf, size); return; }
    under1000(hi, buf, size);
    if (lo == 0) cat(buf, size, "hundred");
    else if (lo < 10) { cat(buf, size, "oh"); cat(buf, size, ones[lo]); }
    else under1000(lo, buf, size);
}

/* plural of the last word: "nineties", "sixes" */
static void make_plural(char *buf, size_t size)
{
    size_t l = strlen(buf);
    if (l && buf[l - 1] == 'y') { buf[l - 1] = 0; strncat(buf, "ies", size - strlen(buf) - 1); }
    else if (l && (buf[l - 1] == 'x' || buf[l - 1] == 's')) strncat(buf, "es", size - strlen(buf) - 1);
    else strncat(buf, "s", size - strlen(buf) - 1);
}

/* Parse a numeric token. Returns 1 if t was fully handled. */
static int number_token(nctx *c, const char *t, int standalone)
{
    char out[256];
    const char *p = t;
    int neg = 0, money = 0, ndig = 0, commas = 0, i;
    unsigned long long ip = 0;
    const char *frac = NULL;
    int fraclen = 0;
    const char *intstart;
    out[0] = 0;

    if (*p == '-' || *p == '+') { neg = (*p == '-'); p++; }
    if (*p == '$') { money = 1; p++; }
    else if ((unsigned char)p[0] == 0xC2 && (unsigned char)p[1] == 0xA3) { money = 2; p += 2; }
    else if ((unsigned char)p[0] == 0xA3) { money = 2; p++; }
    else if ((unsigned char)p[0] == 0xE2 && (unsigned char)p[1] == 0x82 && (unsigned char)p[2] == 0xAC) { money = 3; p += 3; }
    if (!is_dig((unsigned char)*p)) return 0;

    /* time h:mm */
    if (!money && is_dig((unsigned char)p[0]) &&
        ((p[1] == ':' && is_dig((unsigned char)p[2]) && is_dig((unsigned char)p[3]) && !p[4]) ||
         (is_dig((unsigned char)p[1]) && p[2] == ':' && is_dig((unsigned char)p[3]) && is_dig((unsigned char)p[4]) && !p[5]))) {
        unsigned h = 0, m;
        while (is_dig((unsigned char)*p)) h = h * 10 + (unsigned)(*p++ - '0');
        p++;
        m = (unsigned)((p[0] - '0') * 10 + (p[1] - '0'));
        if (h <= 24 && m < 60) {
            under1000(h, out, sizeof out);
            if (m == 0) cat(out, sizeof out, "o'clock");
            else if (m < 10) { cat(out, sizeof out, "oh"); cat(out, sizeof out, ones[m]); }
            else under1000(m, out, sizeof out);
            emit_words(c, out);
            return 1;
        }
        return 0;
    }

    intstart = p;
    while (is_dig((unsigned char)*p) || (*p == ',' && is_dig((unsigned char)p[1]) && ndig)) {
        if (*p == ',') { commas++; p++; continue; }
        if (ndig < 19) ip = ip * 10 + (unsigned long long)(*p - '0');
        ndig++;
        p++;
    }
    if (*p == '.' && is_dig((unsigned char)p[1])) {
        frac = ++p;
        while (is_dig((unsigned char)*p)) { p++; fraclen++; }
    }

    /* long digit strings, leading zeros (codes, phone numbers): digits */
    if (ndig > 15 || (ndig >= 10 && !commas) ||
        (ndig > 1 && intstart[0] == '0' && !commas && !frac && !money)) {
        if (*p) return 0;
        if (neg) emit(c, NT_WORD, "minus");
        spell(c, intstart, (size_t)(p - intstart));
        return 1;
    }

    if (neg) cat(out, sizeof out, "minus");

    if (money) {
        unsigned long cents = 0;
        static const char *unit1[] = { "", "dollar", "pound", "euro" };
        static const char *unitn[] = { "", "dollars", "pounds", "euros" };
        static const char *sub1[] = { "", "cent", "penny", "cent" };
        static const char *subn[] = { "", "cents", "pence", "cents" };
        if (frac) {
            cents = (unsigned long)(frac[0] - '0') * 10;
            if (fraclen > 1) cents += (unsigned long)(frac[1] - '0');
        }
        if (ip || !cents) {
            cardinal_ull(ip, out, sizeof out);
            cat(out, sizeof out, ip == 1 ? unit1[money] : unitn[money]);
        }
        if (cents) {
            if (ip) cat(out, sizeof out, "and");
            cardinal_ull(cents, out, sizeof out);
            cat(out, sizeof out, cents == 1 ? sub1[money] : subn[money]);
        }
        if (*p) return 0;
        emit_words(c, out);
        return 1;
    }

    /* suffixes */
    if (!frac && !commas && ndig == 4 && ip >= 1100 && ip <= 2099 &&
        (*p == 0 || strcmp(p, "s") == 0 || strcmp(p, "'s") == 0) && standalone && !neg) {
        char y[128];
        nt_year((unsigned long)ip, y, sizeof y);
        if (*p) make_plural(y, sizeof y);
        cat(out, sizeof out, y);
        emit_words(c, out);
        return 1;
    }
    if (!frac && (strcmp(p, "st") == 0 || strcmp(p, "nd") == 0 || strcmp(p, "rd") == 0 ||
                  strcmp(p, "th") == 0 || strcmp(p, "ST") == 0 || strcmp(p, "ND") == 0 ||
                  strcmp(p, "RD") == 0 || strcmp(p, "TH") == 0)) {
        cardinal_ull(ip, out, sizeof out);
        make_ordinal(out, sizeof out);
        emit_words(c, out);
        return 1;
    }
    cardinal_ull(ip, out, sizeof out);
    if (frac) {
        cat(out, sizeof out, "point");
        for (i = 0; i < fraclen; i++) cat(out, sizeof out, ones[frac[i] - '0']);
    }
    if (!frac && (strcmp(p, "s") == 0 || strcmp(p, "'s") == 0)) {   /* "the 80s" */
        make_plural(out, sizeof out);
        p += strlen(p);
    }
    if (*p == '%') { cat(out, sizeof out, "percent"); p++; }
    if (*p) return 0;
    emit_words(c, out);
    return 1;
}

/* ---- words ---------------------------------------------------------------- */

static const struct { const char *ab, *ex; int needs_dot; } abbrevs[] = {
    { "mr", "mister", 0 }, { "mrs", "missus", 0 }, { "ms", "miz", 0 },
    { "dr", "doctor", 0 }, { "prof", "professor", 1 }, { "st", "saint", 1 },
    { "jr", "junior", 1 }, { "sr", "senior", 1 }, { "vs", "versus", 0 },
    { "etc", "et cetera", 0 }, { "approx", "approximately", 1 },
    { "dept", "department", 1 }, { "vol", "volume", 1 }, { "fig", "figure", 1 },
    { "inc", "incorporated", 1 }, { "ltd", "limited", 1 }, { "corp", "corporation", 1 },
    { "mt", "mount", 1 }, { "ave", "avenue", 1 }, { "blvd", "boulevard", 1 },
    { "jan", "january", 1 }, { "feb", "february", 1 }, { "apr", "april", 1 },
    { "jun", "june", 1 }, { "jul", "july", 1 }, { "aug", "august", 1 },
    { "sep", "september", 1 }, { "sept", "september", 1 }, { "oct", "october", 1 },
    { "nov", "november", 1 }, { "dec", "december", 1 }, { "no", "number", 2 },
    { "e.g", "for example", 0 }, { "i.e", "that is", 0 }, { "a.m", "a m", 0 },
    { "p.m", "p m", 0 }, { "kb", "kilobytes", 0 }, { "mb", "megabytes", 0 },
    { "mhz", "megahertz", 0 }, { "khz", "kilohertz", 0 }, { "hz", "hertz", 0 },
};

static int has_vowel(const char *s, size_t n)
{
    size_t i;
    for (i = 0; i < n; i++)
        if (strchr("aeiouyAEIOUY", s[i])) return 1;
    return 0;
}

static int in_dict(nctx *c, const char *w)
{
    uint8_t ph[LX_MAX_PHONES];
    return c->dict && lx_dict_lookup(c->dict, w, ph, LX_MAX_PHONES) > 0;
}

static void word_piece(nctx *c, const char *s, size_t n)
{
    char w[64];
    size_t i, caps = 0, letters = 0;
    if (n == 0) return;
    if (n > 60) n = 60;
    for (i = 0; i < n; i++) {
        w[i] = (char)lower((unsigned char)s[i]);
        if (is_up((unsigned char)s[i])) caps++;
        if (is_al((unsigned char)s[i])) letters++;
    }
    w[n] = 0;
    if (letters == 0) return;
    for (i = 0; i < sizeof abbrevs / sizeof abbrevs[0]; i++)
        if (abbrevs[i].needs_dot == 0 && strcmp(w, abbrevs[i].ab) == 0 && n >= 2 &&
            (strchr("kmh", w[0]) != NULL)) {
            emit_words(c, abbrevs[i].ex);   /* units: KB, MHz */
            return;
        }
    /* all-caps: short ones and unknown ones are initialisms */
    if (caps == letters && letters >= 2) {
        int known = in_dict(c, w);
        if (letters <= 3 && strcmp(w, "ok") != 0) { spell(c, s, n); return; }
        if (!known) { spell(c, s, n); return; }
    }
    if (letters == 1 && n == 1) {
        /* lone letter: "a" and "I" are words, others are spelled */
        if (w[0] == 'a' || w[0] == 'i') emit(c, NT_WORD, w);
        else emit(c, NT_LETTER, w);
        return;
    }
    if (!has_vowel(s, n) && !in_dict(c, w)) { spell(c, s, n); return; }
    emit(c, NT_WORD, w);
}

/* split a token into letter runs / digit runs / separators */
static void general_token(nctx *c, const char *t)
{
    const char *p = t;
    while (*p && !c->stop) {
        const char *s = p;
        if (is_al((unsigned char)*p) || (*p == '\'' && p > t && is_al((unsigned char)p[-1]))) {
            while (is_al((unsigned char)*p) || (*p == '\'' && is_al((unsigned char)p[1]))) {
                p++;
                /* camelCase / "AmigaOS": split at a lower->upper change */
                if (is_lo((unsigned char)p[-1]) && is_up((unsigned char)*p)) break;
            }
            /* "'s" after letters stays with the word */
            if (p[0] == '\'' && (p[1] == 's' || p[1] == 'S') && !is_al((unsigned char)p[2])) p += 2;
            if (p - s == 1 && (is_dig((unsigned char)*p) || (s > t && is_dig((unsigned char)s[-1]))))
                spell(c, s, 1);              /* "A500", "68k" */
            else
                word_piece(c, s, (size_t)(p - s));
        } else if (is_dig((unsigned char)*p)) {
            char num[TOKMAX];
            size_t n = 0;
            while ((is_dig((unsigned char)*p) || ((*p == '.' || *p == ',') && is_dig((unsigned char)p[1])))
                   && n < TOKMAX - 4)
                num[n++] = *p++;
            num[n] = 0;
            if (!number_token(c, num, 0)) spell(c, num, n);
        } else {
            switch (*p) {
            case '&': emit(c, NT_WORD, "and"); break;
            case '+': emit(c, NT_WORD, "plus"); break;
            case '=': emit(c, NT_WORD, "equals"); break;
            case '@': emit(c, NT_WORD, "at"); break;
            case '%': emit(c, NT_WORD, "percent"); break;
            case '#': emit(c, NT_WORD, "number"); break;
            case '/': if (p == t || !p[1]) emit(c, NT_WORD, "slash"); break;
            default: break;  /* hyphens, underscores, dots: just split */
            }
            p++;
        }
    }
}

static int punct_rank(int p)
{
    switch (p) { case '?': return 4; case '.': return 3; case ',': return 2; case '-': return 1; }
    return 0;
}

static void token(nctx *c, char *t, int last_token)
{
    size_t n = strlen(t), i;
    int punct = 0, abbrev = 0;
    char low[TOKMAX];

    /* leading quotes / brackets */
    while (*t && strchr("\"'([{<", *t)) {
        if (*t == '(' || *t == '[' || *t == '{') { punct = 0; emit(c, NT_PUNCT, ","); }
        t++;
        n--;
    }
    /* trailing punctuation */
    while (n > 0 && strchr(".,;:!?)]}>\"'", t[n - 1])) {
        int ch = t[n - 1], p = 0;
        if (ch == '\'' && n >= 2 && t[n - 2] == 's') { n--; t[n] = 0; continue; } /* dogs' */
        if (ch == '.') {
            /* abbreviation or initial? keep the dot with the word */
            size_t k = n - 1;
            for (i = 0; i < k && i < TOKMAX - 1; i++) low[i] = (char)lower((unsigned char)t[i]);
            low[i] = 0;
            for (i = 0; i < sizeof abbrevs / sizeof abbrevs[0]; i++)
                if (strcmp(low, abbrevs[i].ab) == 0 && abbrevs[i].needs_dot != 2) { abbrev = 1; break; }
            if (!abbrev && k == 1 && is_al((unsigned char)t[0])) abbrev = 2;   /* "J." */
            if (!abbrev && k >= 3 && t[k - 2] == '.') abbrev = 2;             /* "U.S.A." */
            if (abbrev) {
                n--;
                t[n] = 0;
                if (last_token || strcmp(low, "etc") == 0) p = '.';
                if (p && punct_rank(p) > punct_rank(punct)) punct = p;
                break;
            }
            p = '.';
        } else if (ch == '!') p = '.';
        else if (ch == '?') p = '?';
        else if (ch == ',' || ch == ';' || ch == ':' || ch == ')' || ch == ']' || ch == '}') p = ',';
        if (punct_rank(p) > punct_rank(punct)) punct = p;
        n--;
        t[n] = 0;
    }

    if (n > 0) {
        for (i = 0; i < n && i < TOKMAX - 1; i++) low[i] = (char)lower((unsigned char)t[i]);
        low[i] = 0;
        if (strcmp(t, "-") == 0 || strcmp(t, "--") == 0 || strcmp(t, "---") == 0) {
            emit(c, NT_PUNCT, "-");
        } else {
            int done = 0;
            for (i = 0; i < sizeof abbrevs / sizeof abbrevs[0]; i++) {
                if (strcmp(low, abbrevs[i].ab) == 0 &&
                    (abbrevs[i].needs_dot == 0 || (abbrevs[i].needs_dot == 1 && abbrev))) {
                    emit_words(c, abbrevs[i].ex);
                    done = 1;
                    break;
                }
            }
            if (!done && (strstr(t, "--") != NULL)) {
                /* "word--word": dash pause inside */
                char *d = strstr(t, "--");
                *d = 0;
                token(c, t, 0);
                emit(c, NT_PUNCT, "-");
                d += 2;
                while (*d == '-') d++;
                token(c, d, last_token);
                done = 1;
            }
            if (!done && abbrev == 2 && strchr(t, '.')) {
                spell(c, t, n);   /* initials, U.S.A. */
                done = 1;
            }
            if (!done && number_token(c, t, 1)) done = 1;
            if (!done) general_token(c, t);
        }
    }
    if (punct) {
        char s[2];
        s[0] = (char)punct;
        s[1] = 0;
        emit(c, NT_PUNCT, s);
    }
}

int nt_normalize(const char *in, size_t len, const lx_dict *dict,
                 nt_fn fn, void *user)
{
    nctx c;
    size_t i = 0;
    memset(&c, 0, sizeof c);
    c.dict = dict;
    c.fn = fn;
    c.user = user;
    c.last_punct = 1;

    while (i < len && in[i] && !c.stop) {
        char t[TOKMAX];
        size_t n = 0, start;
        unsigned char ch = (unsigned char)in[i];
        if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r') { i++; continue; }
        start = i;
        while (i < len && in[i] && n < TOKMAX - 4) {
            ch = (unsigned char)in[i];
            if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r') break;
            if (ch == 0xE2 && i + 2 < len && (unsigned char)in[i + 1] == 0x80) {
                unsigned char k = (unsigned char)in[i + 2];
                if (k == 0x98 || k == 0x99) t[n++] = '\'';
                else if (k == 0x9C || k == 0x9D) t[n++] = '"';
                else if (k == 0x93 || k == 0x94) { t[n++] = '-'; t[n++] = '-'; }
                else if (k == 0xA6) { t[n++] = '.'; }
                i += 3;
                continue;
            }
            if (ch == 0x92) { t[n++] = '\''; i++; continue; }        /* cp1252 ' */
            if (ch == 0x96 || ch == 0x97) { t[n++] = '-'; t[n++] = '-'; i++; continue; }
            if (ch >= 0x80 && !(ch == 0xC2 || ch == 0xA3 || (ch == 0xE2))) { i++; continue; }
            t[n++] = (char)ch;
            i++;
        }
        t[n] = 0;
        c.src = start;
        {
            size_t j = i;
            while (j < len && in[j] && (in[j] == ' ' || in[j] == '\t' || in[j] == '\n' || in[j] == '\r')) j++;
            token(&c, t, j >= len || !in[j]);
        }
    }
    return c.stop;
}
