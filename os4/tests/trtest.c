// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>
//
// trtest: on-target tests of translator.library through its PPC interface.
//   trtest                 run the tests
//   trtest "text"          print Translate()'s output
#include <exec/exec.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/translator.h>
#include <stdio.h>
#include <string.h>

struct Library *TranslatorBase;
struct TranslatorIFace *ITranslator;
static int passes, fails;

static void expect(const char *in, const char *want)
{
    char out[1024];
    LONG r = Translate(in, strlen(in), out, sizeof out);
    int ok = r == 0 && strcmp(out, want) == 0;
    if (ok) passes++; else fails++;
    printf("%s Translate(\"%s\") = %ld \"%s\"%s%s\n", ok ? "PASS" : "FAIL", in, (long)r, out,
           ok ? "" : " want ", ok ? "" : want);
}

int main(int argc, char **argv)
{
    TranslatorBase = OpenLibrary("translator.library", 50);
    if (!TranslatorBase) { printf("FAIL cannot open translator.library v50\n"); return 20; }
    ITranslator = (struct TranslatorIFace *)GetInterface(TranslatorBase, "main", 1, NULL);
    if (!ITranslator) { printf("FAIL no main interface\n"); return 20; }
    if (argc > 1) {
        char out[4096];
        LONG r = Translate(argv[1], strlen(argv[1]), out, sizeof out);
        printf("%ld %s\n", (long)r, out);
    } else {
        /* same expectations as the host unit tests */
        expect("Hello, I am your Amiga.", "/HAXLOW5, AY AEM YOHR AXMIY5GAX.");
        expect("Do you enjoy using your Amiga?", "DUW YUW EH1NJOY4 YUW4ZIXNX YOHR AXMIY5GAX?");
        expect("the apple.", "DHIY AE5PUL.");
        expect("In 1985 it cost $3.50.", "IHN NAY4NTIY4N EY4TIY FAY4V IHT KAA4ST THRIY4 DAA4LERZ UND FIH4FTIY SEH5NTS.");
        /* overflow: stops at a word boundary with a negative offset */
        {
            const char *in = "The quick brown fox jumps over the lazy dog.";
            char small[20];
            LONG r = Translate(in, strlen(in), small, sizeof small);
            int ok = r < 0 && in[-r - 1] == ' ' && strlen(small) < sizeof small;
            if (ok) passes++; else fails++;
            printf("%s overflow returns %ld (\"%s\")\n", ok ? "PASS" : "FAIL", (long)r, small);
        }
        printf("trtest: %d passed, %d failed\n", passes, fails);
    }
    DropInterface((struct Interface *)ITranslator);
    CloseLibrary(TranslatorBase);
    return fails ? 10 : 0;
}
