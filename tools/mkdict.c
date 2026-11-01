// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>
//
// mkdict: compile cmudict.dict into the compact "AMSD" image read by
// src/lexicon.c. Words whose pronunciation the CMU LTS model already
// predicts exactly are left out (the translator falls back to the same LTS
// at run time), unless -noprune is given.
//
//   mkdict [-noprune] [-block N] cmudict.dict out.dict

#include "lexicon.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char *word;
    uint8_t ph[LX_MAX_PHONES];
    int nph;
} entry;

static int cmp_entry(const void *a, const void *b)
{
    return strcmp(((const entry *)a)->word, ((const entry *)b)->word);
}

static void be32(FILE *f, uint32_t v)
{
    fputc((int)(v >> 24), f); fputc((int)((v >> 16) & 255), f);
    fputc((int)((v >> 8) & 255), f); fputc((int)(v & 255), f);
}

int main(int argc, char **argv)
{
    int prune = 1, block = 32, ai = 1;
    FILE *in, *out;
    char line[512];
    entry *e = NULL;
    size_t n = 0, cap = 0, i, kept = 0, pruned = 0, skipped = 0;
    uint8_t *data;
    size_t dsize = 0, dcap;
    uint32_t *offs, nblocks = 0;

    while (ai < argc && argv[ai][0] == '-') {
        if (strcmp(argv[ai], "-noprune") == 0) prune = 0;
        else if (strcmp(argv[ai], "-block") == 0 && ai + 1 < argc) block = atoi(argv[++ai]);
        ai++;
    }
    if (argc - ai != 2) {
        fprintf(stderr, "usage: mkdict [-noprune] [-block N] cmudict.dict out.dict\n");
        return 2;
    }
    in = fopen(argv[ai], "r");
    if (!in) { perror(argv[ai]); return 1; }
    while (fgets(line, sizeof line, in)) {
        char *w = strtok(line, " \t\r\n"), *p;
        entry x;
        int ok = 1;
        if (!w || !*w || w[0] == ';') continue;
        if (strchr(w, '(')) continue;              /* alternate pronunciation */
        for (p = w; *p; p++) {
            if (*p >= 'A' && *p <= 'Z') *p = (char)(*p + 32);
            if (!((*p >= 'a' && *p <= 'z') || *p == '\'')) ok = 0;
        }
        if (!ok || strlen(w) > 60) { skipped++; continue; }
        x.nph = 0;
        while ((p = strtok(NULL, " \t\r\n")) != NULL) {
            int c;
            if (p[0] == '#') break;                /* comment */
            c = lx_parse_phone(p, (int)strlen(p));
            if (c < 0 || x.nph >= LX_MAX_PHONES) { ok = 0; break; }
            x.ph[x.nph++] = (uint8_t)c;
        }
        if (!ok || !x.nph) { skipped++; continue; }
        if (prune) {
            uint8_t l[LX_MAX_PHONES];
            int ln = strchr(w, '\'') ? 0 : lx_lts_cmu(w, l, LX_MAX_PHONES);
            if (ln == x.nph && memcmp(l, x.ph, (size_t)ln) == 0) { pruned++; continue; }
        }
        x.word = strdup(w);
        if (n == cap) { cap = cap ? cap * 2 : 65536; e = realloc(e, cap * sizeof *e); }
        e[n++] = x;
    }
    fclose(in);
    qsort(e, n, sizeof *e, cmp_entry);

    dcap = n * 24 + 1024;
    data = malloc(dcap);
    offs = malloc(((n + (size_t)block - 1) / (size_t)block + 1) * sizeof *offs);
    for (i = 0; i < n; i++) {
        const char *w = e[i].word;
        size_t shared = 0, wl = strlen(w), k;
        if (i > 0 && strcmp(w, e[i - 1].word) == 0) continue;   /* duplicate */
        if (kept % (size_t)block == 0) {
            offs[nblocks++] = (uint32_t)dsize;
        } else {
            const char *pw = e[i - 1].word;
            while (shared < wl && pw[shared] == w[shared] && shared < 255) shared++;
            if (shared == wl) shared--;    /* keep at least one suffix byte */
        }
        if (dsize + wl + LX_MAX_PHONES + 4 > dcap) { dcap *= 2; data = realloc(data, dcap); }
        data[dsize++] = (uint8_t)shared;
        for (k = shared; k < wl; k++)
            data[dsize++] = (uint8_t)(w[k] | (k + 1 == wl ? 0x80 : 0));
        for (k = 0; k < (size_t)e[i].nph; k++)
            data[dsize++] = (uint8_t)(e[i].ph[k] | (k + 1 == (size_t)e[i].nph ? 0x80 : 0));
        kept++;
    }

    out = fopen(argv[ai + 1], "wb");
    if (!out) { perror(argv[ai + 1]); return 1; }
    fwrite("AMSD", 1, 4, out);
    fputc(0, out); fputc(1, out);
    fputc((block >> 8) & 255, out); fputc(block & 255, out);
    be32(out, (uint32_t)kept);
    be32(out, nblocks);
    for (i = 0; i < nblocks; i++) be32(out, offs[i]);
    fwrite(data, 1, dsize, out);
    fclose(out);
    fprintf(stderr, "mkdict: %lu entries kept, %lu predicted by LTS (pruned), %lu skipped; "
            "%lu bytes (%u blocks of %d)\n",
            (unsigned long)kept, (unsigned long)pruned, (unsigned long)skipped,
            (unsigned long)(16 + nblocks * 4 + dsize), nblocks, block);
    return 0;
}
