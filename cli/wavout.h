// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>
//
// Host-only helpers: collect samples in memory, write a 16-bit mono WAV.

#ifndef CLI_WAVOUT_H
#define CLI_WAVOUT_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    int16_t *s;
    size_t n, cap;
} pcmbuf;

static int pcm_sink(void *user, const int16_t *s, size_t n)
{
    pcmbuf *b = (pcmbuf *)user;
    if (b->n + n > b->cap) {
        size_t nc = b->cap ? b->cap * 2 : 65536;
        int16_t *ns;
        while (nc < b->n + n) nc *= 2;
        ns = (int16_t *)realloc(b->s, nc * sizeof *ns);
        if (!ns) return 1;
        b->s = ns;
        b->cap = nc;
    }
    memcpy(b->s + b->n, s, n * sizeof *s);
    b->n += n;
    return 0;
}

static void put32(FILE *f, uint32_t v)
{
    fputc((int)(v & 255), f); fputc((int)((v >> 8) & 255), f);
    fputc((int)((v >> 16) & 255), f); fputc((int)((v >> 24) & 255), f);
}

static void put16(FILE *f, uint16_t v)
{
    fputc(v & 255, f); fputc((v >> 8) & 255, f);
}

/* little-endian RIFF regardless of host byte order */
static int wav_write(const char *path, const int16_t *s, size_t n, uint32_t rate)
{
    FILE *f = fopen(path, "wb");
    size_t i;
    if (!f) return -1;
    fwrite("RIFF", 1, 4, f); put32(f, (uint32_t)(36 + n * 2));
    fwrite("WAVEfmt ", 1, 8, f); put32(f, 16); put16(f, 1); put16(f, 1);
    put32(f, rate); put32(f, rate * 2); put16(f, 2); put16(f, 16);
    fwrite("data", 1, 4, f); put32(f, (uint32_t)(n * 2));
    for (i = 0; i < n; i++) put16(f, (uint16_t)s[i]);
    return fclose(f);
}

#endif
