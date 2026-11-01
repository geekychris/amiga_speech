// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>
//
// Host CLI helpers: find and load the dictionary, translate long text by
// following Translate()'s resume convention.

#ifndef CLI_COMMON_H
#define CLI_COMMON_H

#include "amiga_speech/translator.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned char *g_dict_image;

static int load_dictionary(const char *argv0)
{
    const char *env = getenv("AMIGA_SPEECH_DATA");
    char path[1024];
    FILE *f = NULL;
    long sz;
    if (env) {
        snprintf(path, sizeof path, "%s/english.dict", env);
        f = fopen(path, "rb");
    }
    if (!f) {
        const char *slash = strrchr(argv0, '/');
        int dl = slash ? (int)(slash - argv0) : 1;
        snprintf(path, sizeof path, "%.*s/../data/english.dict", dl, slash ? argv0 : ".");
        f = fopen(path, "rb");
    }
    if (!f) f = fopen("data/english.dict", "rb");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    g_dict_image = (unsigned char *)malloc((size_t)sz);
    if (!g_dict_image || fread(g_dict_image, 1, (size_t)sz, f) != (size_t)sz) { fclose(f); return -1; }
    fclose(f);
    return tr_set_dictionary(g_dict_image, (size_t)sz);
}

/* Translate all of text (any length) with a fixed 2 KB output buffer,
   resuming where Translate() stops. Caller frees the result. */
static char *translate_all(const char *text, int mode)
{
    size_t len = strlen(text), off = 0, total = 0, cap = 4096;
    char *res = (char *)malloc(cap), buf[2048];
    res[0] = 0;
    while (off < len) {
        long r = tr_translate_mode(text + off, (long)(len - off), buf, sizeof buf, mode);
        size_t bl = strlen(buf);
        if (total + bl + 2 > cap) { cap = (total + bl + 2) * 2; res = (char *)realloc(res, cap); }
        if (total && bl) res[total++] = ' ';
        memcpy(res + total, buf, bl + 1);
        total += bl;
        if (r >= 0) break;
        if (r == TR_NotUsed) break;   /* a single word larger than the buffer */
        off += (size_t)(-r);
    }
    return res;
}

static char *read_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    long sz;
    char *b;
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); sz = ftell(f); fseek(f, 0, SEEK_SET);
    b = (char *)malloc((size_t)sz + 1);
    if (fread(b, 1, (size_t)sz, f) != (size_t)sz) { fclose(f); free(b); return NULL; }
    b[sz] = 0;
    fclose(f);
    return b;
}

static char *join_args(int argc, char **argv, int from)
{
    size_t len = 1;
    int i;
    char *s;
    for (i = from; i < argc; i++) len += strlen(argv[i]) + 1;
    s = (char *)malloc(len);
    s[0] = 0;
    for (i = from; i < argc; i++) { if (i > from) strcat(s, " "); strcat(s, argv[i]); }
    return s;
}

#endif
