// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

#include "lexicon.h"
#include <string.h>

const char *const lx_arpa[LX_NPHONES] = {
    "AA","AE","AH","AO","AW","AY","EH","ER","EY","IH","IY","OW","OY","UH","UW",
    "B","CH","D","DH","F","G","HH","JH","K","L","M","N","NG","P","R","S","SH",
    "T","TH","V","W","Y","Z","ZH"
};

int lx_code(int phone, int stress)
{
    if (phone < LX_NVOWELS) {
        if (stress < 0) stress = 0;
        if (stress > 2) stress = 2;
        return 1 + phone * 3 + stress;
    }
    return 46 + phone - LX_NVOWELS;
}

static int up(int c) { return (c >= 'a' && c <= 'z') ? c - 32 : c; }

int lx_parse_phone(const char *s, int len)
{
    int i, stress = 0;
    char name[3] = { 0, 0, 0 };
    if (len > 0 && s[len - 1] >= '0' && s[len - 1] <= '2') {
        stress = s[len - 1] - '0';
        len--;
    }
    if (len < 1 || len > 2) return -1;
    name[0] = (char)up((unsigned char)s[0]);
    if (len == 2) name[1] = (char)up((unsigned char)s[1]);
    for (i = 0; i < LX_NPHONES; i++)
        if (strcmp(lx_arpa[i], name) == 0)
            return lx_code(i, stress);
    return -1;
}

static uint32_t be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

int lx_dict_open(lx_dict *d, const uint8_t *buf, size_t size)
{
    memset(d, 0, sizeof *d);
    if (size < 16 || memcmp(buf, "AMSD", 4) != 0) return -1;
    if (buf[4] != 0 || buf[5] != 1) return -1;
    d->base = buf;
    d->size = size;
    d->blocksize = ((uint32_t)buf[6] << 8) | buf[7];
    d->nentries = be32(buf + 8);
    d->nblocks = be32(buf + 12);
    if (16 + (size_t)d->nblocks * 4 > size || d->blocksize == 0) return -1;
    d->offsets = buf + 16;
    d->data = d->offsets + d->nblocks * 4;
    return 0;
}

/* compare word with the first (unshared) entry of block b */
static int cmp_block(const lx_dict *d, uint32_t b, const char *w)
{
    const uint8_t *p = d->data + be32(d->offsets + b * 4) + 1; /* skip prefix 0 */
    for (;;) {
        int c = *p & 0x7f, last = *p & 0x80;
        int wc = (unsigned char)*w;
        if (wc != c) return wc < c ? -1 : 1;
        w++;
        p++;
        if (last) return *w ? 1 : 0;
    }
}

int lx_dict_lookup(const lx_dict *d, const char *word, uint8_t *out, int max)
{
    uint32_t lo = 0, hi, b, i, n;
    const uint8_t *p, *end;
    char cur[64];
    int len = 0;

    if (!d->base || !d->nblocks) return 0;
    hi = d->nblocks;
    /* last block whose first word <= word */
    while (hi - lo > 1) {
        uint32_t m = (lo + hi) / 2;
        if (cmp_block(d, m, word) >= 0) lo = m; else hi = m;
    }
    b = lo;
    p = d->data + be32(d->offsets + b * 4);
    end = (b + 1 < d->nblocks) ? d->data + be32(d->offsets + (b + 1) * 4)
                               : d->base + d->size;
    n = d->blocksize;
    for (i = 0; i < n && p < end; i++) {
        int shared = *p++, k = 0, c;
        len = shared;
        do {
            c = *p++;
            if (len < 63) cur[len++] = (char)(c & 0x7f);
        } while (!(c & 0x80));
        cur[len] = 0;
        c = strcmp(cur, word);
        if (c == 0) {
            do {
                c = *p++;
                if (k < max) out[k++] = (uint8_t)(c & 0x7f);
            } while (!(c & 0x80));
            return k;
        }
        if (c > 0) return 0;
        while (!(*p++ & 0x80))
            ;
    }
    return 0;
}
