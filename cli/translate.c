// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>
//
// translate [-c] [-n] [-w] "text"   print the narrator phoneme string
//   -c  classic rule-only (NRL) mode
//   -n  print the normalised words instead
//   -w  print each word's ARPAbet and source (D=dict, L=LTS, N=NRL)

#include "common.h"

int main(int argc, char **argv)
{
    int ai = 1, mode = TR_MODE_DICT, norm = 0, words = 0;
    char *text;
    while (ai < argc && argv[ai][0] == '-' && argv[ai][1]) {
        if (strcmp(argv[ai], "-c") == 0) mode = TR_MODE_CLASSIC;
        else if (strcmp(argv[ai], "-n") == 0) norm = 1;
        else if (strcmp(argv[ai], "-w") == 0) words = 1;
        else { fprintf(stderr, "usage: translate [-c] [-n] [-w] text\n"); return 2; }
        ai++;
    }
    if (ai >= argc) { fprintf(stderr, "usage: translate [-c] [-n] [-w] text\n"); return 2; }
    if (load_dictionary(argv[0]) != 0)
        fprintf(stderr, "translate: no dictionary found, letter-to-sound only\n");
    text = join_args(argc, argv, ai);
    if (norm) {
        static char out[65536];
        tr_normalize_debug(text, (long)strlen(text), out, sizeof out);
        printf("%s\n", out);
    } else if (words) {
        int i;
        for (i = ai; i < argc; i++) {
            char a[256], src;
            tr_word_arpabet(argv[i], mode, a, sizeof a, &src);
            printf("%-20s %c  %s\n", argv[i], src, a);
        }
    } else {
        char *ph = translate_all(text, mode);
        printf("%s\n", ph);
        free(ph);
    }
    free(text);
    return 0;
}
