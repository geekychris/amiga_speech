// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>
//
// Say for AmigaOS 4 (amiga_speech). Accepts the classic Say switches
//   Say [-m] [-f] [-r] [-n] [-s<rate>] [-p<pitch>] [-x <file>] <text>
// and keywords (options are only recognised before the text)
//   Say [TEXT] ... [FILE <f>] [RATE <n>] [PITCH <n>] [FEMALE] [MALE]
//       [ROBOT] [NATURAL] [VOICE classic|natural] [PHONEMES] [VOLUME <n>]
// Text is translated with translator.library and spoken by narrator.device.
// VOICE classic uses narrator unit 1. PHONEMES speaks the text as narrator
// phonemes. Long text is translated in pieces (Translate()'s resume rule).

#include <exec/exec.h>
#include <devices/narrator.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/translator.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char ver[] __attribute__((used)) = "$VER: Say 50.1 (01.11.2026) amiga_speech";

struct Library *TranslatorBase;
struct TranslatorIFace *ITranslator;

static char *read_file(const char *name)
{
    BPTR fh = Open(name, MODE_OLDFILE);
    int64 sz;
    char *b;
    if (!fh) return NULL;
    sz = GetFileSize(fh);
    b = malloc((size_t)sz + 1);
    if (b && Read(fh, b, (int32)sz) == (int32)sz) b[sz] = 0;
    else { free(b); b = NULL; }
    Close(fh);
    return b;
}

static void usage(void)
{
    printf("Say [-m] [-f] [-r] [-n] [-s<rate>] [-p<pitch>] [-x <file>] <text>\n"
           "    [FILE <f>] [RATE <n>] [PITCH <n>] [VOLUME <n>] [FEMALE] [MALE] [ROBOT]\n"
           "    [NATURAL] [VOICE classic|natural] [PHONEMES]\n"
           "Example: Say Hello, I am your Amiga.\n");
}

static void setup_hint(void)
{
    printf("Install narrator.device and translator.library (amiga_speech 50+), or for this\n"
           "session run: Execute DH1:speech/Speech-Setup\n");
}

static int speak(struct narrator_rb *rb, const char *ph)
{
    rb->message.io_Command = CMD_WRITE;
    rb->message.io_Data = (APTR)ph;
    rb->message.io_Length = strlen(ph);
    DoIO((struct IORequest *)rb);
    return rb->message.io_Error;
}

int main(int argc, char **argv)
{
    int rate = DEFRATE, pitch = DEFPITCH, mode = NATURALF0, sex = MALE, volume = DEFVOL;
    int classic = 0, phonemes = 0, i, rc = 0;
    const char *file = NULL;
    char *text = NULL;
    size_t tlen = 0;
    struct MsgPort *port;
    struct narrator_rb *rb;

    for (i = 1; i < argc; i++) {
        const char *a = argv[i];
        const char *v = i + 1 < argc ? argv[i + 1] : NULL;
        /* options only before the text: once a word of text has been seen,
           everything is text ("Say This is the natural voice." must not
           take "natural" as the NATURAL keyword) */
        if (text) {
            size_t l = strlen(a);
            text = realloc(text, tlen + l + 2);
            text[tlen++] = ' ';
            memcpy(text + tlen, a, l + 1);
            tlen += l;
            continue;
        }
        if (!strcmp(a, "-m") || !strcasecmp(a, "MALE")) sex = MALE;
        else if (!strcmp(a, "-f") || !strcasecmp(a, "FEMALE")) sex = FEMALE;
        else if (!strcmp(a, "-r") || !strcasecmp(a, "ROBOT")) mode = ROBOTICF0;
        else if (!strcmp(a, "-n") || !strcasecmp(a, "NATURAL")) mode = NATURALF0;
        else if (!strncmp(a, "-s", 2) && a[2]) rate = atoi(a + 2);
        else if (!strncmp(a, "-p", 2) && a[2]) pitch = atoi(a + 2);
        else if ((!strcmp(a, "-x") || !strcasecmp(a, "FILE")) && v) { file = v; i++; }
        else if (!strcasecmp(a, "RATE") && v) { rate = atoi(v); i++; }
        else if (!strcasecmp(a, "PITCH") && v) { pitch = atoi(v); i++; }
        else if (!strcasecmp(a, "VOLUME") && v) { volume = atoi(v); i++; }
        else if (!strcasecmp(a, "VOICE") && v) { classic = (v[0] == 'c' || v[0] == 'C'); i++; }
        else if (!strcasecmp(a, "PHONEMES")) phonemes = 1;
        else if (!strcmp(a, "?") || !strcmp(a, "-h") || !strcasecmp(a, "HELP")) {
            usage();
            return 0;
        } else if (a[0] == '-' && a[1] && !(a[1] >= '0' && a[1] <= '9')) {
            printf("Say: unknown option %s\n", a);
            usage();
            return RETURN_WARN;
        } else {
            size_t l = strlen(a);
            text = realloc(text, tlen + l + 2);
            if (tlen) text[tlen++] = ' ';
            memcpy(text + tlen, a, l + 1);
            tlen += l;
        }
    }
    if (file) {
        free(text);
        text = read_file(file);
        if (!text) { PrintFault(IoErr(), file); return RETURN_FAIL; }
    }
    if (!text || !text[0]) {
        usage();
        return RETURN_WARN;
    }

    port = AllocSysObjectTags(ASOT_PORT, TAG_END);
    rb = AllocSysObjectTags(ASOT_IOREQUEST, ASOIOR_Size, sizeof(struct narrator_rb),
                            ASOIOR_ReplyPort, port, TAG_END);
    if (!port || !rb) return RETURN_FAIL;
    rb->flags = NDF_NEWIORB;
    if (OpenDevice("narrator.device", classic ? 1 : 0, (struct IORequest *)rb, 0)) {
        printf("Say: cannot open narrator.device (error %ld)\n", (long)rb->message.io_Error);
        setup_hint();
        return RETURN_FAIL;
    }
    rb->rate = rate;
    rb->pitch = pitch;
    rb->mode = mode;
    rb->sex = sex;
    rb->volume = volume;

    if (phonemes) {
        rc = speak(rb, text);
    } else {
        TranslatorBase = OpenLibrary("translator.library", 50);
        ITranslator = TranslatorBase ? (struct TranslatorIFace *)
            GetInterface(TranslatorBase, "main", 1, NULL) : NULL;
        if (!ITranslator) {
            printf("Say: cannot open translator.library version 50\n");
            setup_hint();
            rc = -1;
        } else {
            static char out[4096];
            size_t off = 0, len = strlen(text);
            while (off < len && rc == 0) {
                LONG r = Translate(text + off, (LONG)(len - off), out, sizeof out);
                if (out[0]) rc = speak(rb, out);
                if (r >= 0) break;
                if (r == -1 && out[0] == 0) break;   /* a word longer than the buffer */
                off += (size_t)(-r);
                if (CheckSignal(SIGBREAKF_CTRL_C)) break;
            }
            DropInterface((struct Interface *)ITranslator);
        }
        if (TranslatorBase) CloseLibrary(TranslatorBase);
    }
    if (rc) printf("Say: narrator error %d\n", rc);
    CloseDevice((struct IORequest *)rb);
    FreeSysObject(ASOT_IOREQUEST, rb);
    FreeSysObject(ASOT_PORT, port);
    free(text);
    return rc ? RETURN_ERROR : RETURN_OK;
}
