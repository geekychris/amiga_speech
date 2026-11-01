// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>
//
// On-target tests for narrator.device (native PPC program).
//   nartest [DEVICE=name]       run the test suite, print PASS/FAIL lines
//   nartest SPEAK "phonemes" [UNIT n] [RATE n] [PITCH n] [ROBOT] [FEMALE]
// Run with ENV:Narrator/Capture set so nothing is played.

#include <exec/exec.h>
#include <devices/narrator.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <devices/timer.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static const char *devname = "narrator.device";
static int fails, passes;

static void check(int ok, const char *what, long a, long b)
{
    if (ok) passes++; else fails++;
    printf("%s %s (%ld, %ld)\n", ok ? "PASS" : "FAIL", what, a, b);
}

static struct MsgPort *port, *rport;

/* timer.device wait (Delay() is avoided: see amiga_mcp CLAUDE.md) */
static void sleep_ms(int ms)
{
    struct MsgPort *tp = AllocSysObjectTags(ASOT_PORT, TAG_END);
    struct TimeRequest *tr = AllocSysObjectTags(ASOT_IOREQUEST, ASOIOR_Size, sizeof(struct TimeRequest),
                                                ASOIOR_ReplyPort, tp, TAG_END);
    if (tr && OpenDevice(TIMERNAME, UNIT_MICROHZ, (struct IORequest *)tr, 0) == 0) {
        tr->Request.io_Command = TR_ADDREQUEST;
        tr->Time.Seconds = ms / 1000;
        tr->Time.Microseconds = (ms % 1000) * 1000;
        DoIO((struct IORequest *)tr);
        CloseDevice((struct IORequest *)tr);
    }
    if (tr) FreeSysObject(ASOT_IOREQUEST, tr);
    if (tp) FreeSysObject(ASOT_PORT, tp);
}

static struct narrator_rb *open_nar(int unit, int newiorb, long *err)
{
    struct narrator_rb *rb = (struct narrator_rb *)
        AllocSysObjectTags(ASOT_IOREQUEST, ASOIOR_Size, sizeof(struct narrator_rb),
                           ASOIOR_ReplyPort, port, TAG_END);
    if (!rb) return NULL;
    rb->flags = newiorb ? NDF_NEWIORB : 0;
    *err = OpenDevice(devname, unit, (struct IORequest *)rb, 0);
    if (*err) {
        *err = rb->message.io_Error;
        FreeSysObject(ASOT_IOREQUEST, rb);
        return NULL;
    }
    return rb;
}

static void close_nar(struct narrator_rb *rb)
{
    CloseDevice((struct IORequest *)rb);
    FreeSysObject(ASOT_IOREQUEST, rb);
}

static long speak(struct narrator_rb *rb, const char *ph)
{
    rb->message.io_Command = CMD_WRITE;
    rb->message.io_Data = (APTR)ph;
    rb->message.io_Length = strlen(ph);
    DoIO((struct IORequest *)rb);
    return rb->message.io_Error;
}

static const char *SENT = "/HEH4LOW, AY AEM YOHR AHMIY5GAH. DHIHS IHZ AHMIY5GAH SPIY5KIHNX.";

static void test_suite(void)
{
    struct narrator_rb *rb;
    long err = 0;

    rb = open_nar(5, 1, &err);
    check(!rb && err == ND_UnitErr, "unit 5 rejected with ND_UnitErr", err, ND_UnitErr);
    if (rb) close_nar(rb);

    rb = open_nar(0, 1, &err);
    check(rb != NULL, "OpenDevice unit 0", err, 0);
    if (!rb) return;
    check(rb->rate == DEFRATE && rb->pitch == DEFPITCH && rb->volume == DEFVOL &&
          rb->sampfreq == DEFFREQ && rb->F0enthusiasm == DEFF0ENTHUS && rb->articulate == DEFARTIC,
          "OpenDevice fills defaults", rb->rate, rb->pitch);

    check(speak(rb, SENT) == 0, "CMD_WRITE speaks", rb->message.io_Error, 0);
    check(rb->message.io_Actual == strlen(SENT), "io_Actual = length", rb->message.io_Actual, strlen(SENT));

    rb->rate = 20;
    check(speak(rb, SENT) == ND_RateErr, "rate 20 -> ND_RateErr", rb->message.io_Error, ND_RateErr);
    rb->rate = DEFRATE;
    rb->pitch = 500;
    check(speak(rb, SENT) == ND_PitchErr, "pitch 500 -> ND_PitchErr", rb->message.io_Error, ND_PitchErr);
    rb->pitch = DEFPITCH;
    rb->centralize = 50; rb->centphon = "XX";
    check(speak(rb, SENT) == ND_CentPhonErr, "bad centphon -> ND_CentPhonErr", rb->message.io_Error, ND_CentPhonErr);
    rb->centralize = 0; rb->centphon = NULL;
    check(speak(rb, "HEHLOW.") == ND_PhonErr, "bare H -> ND_PhonErr", rb->message.io_Error, ND_PhonErr);
    check(speak(rb, "K5AET.") == ND_PhonErr, "digit after consonant -> ND_PhonErr", rb->message.io_Error, ND_PhonErr);

    /* unknown command */
    rb->message.io_Command = 99;
    DoIO((struct IORequest *)rb);
    check(rb->message.io_Error == IOERR_NOCMD, "unknown command -> IOERR_NOCMD", rb->message.io_Error, IOERR_NOCMD);

    /* read without a write */
    {
        struct mouth_rb *m = (struct mouth_rb *)AllocSysObjectTags(ASOT_IOREQUEST,
            ASOIOR_Size, sizeof(struct mouth_rb), ASOIOR_ReplyPort, rport, TAG_END);
        m->voice = *rb;
        m->voice.message.io_Message.mn_ReplyPort = rport;
        m->voice.message.io_Command = CMD_READ;
        DoIO((struct IORequest *)m);
        check(m->voice.message.io_Error == ND_NoWrite, "CMD_READ without write -> ND_NoWrite",
              m->voice.message.io_Error, ND_NoWrite);

        /* mouths + word/syllable sync during an asynchronous write */
        {
            int reads = 0, words = 0, syls = 0, shapes = 0, maxw = 0, maxh = 0;
            rb->mouths = 1;
            rb->flags = NDF_NEWIORB | NDF_WORDSYNC | NDF_SYLSYNC;
            rb->message.io_Command = CMD_WRITE;
            rb->message.io_Data = (APTR)SENT;
            rb->message.io_Length = strlen(SENT);
            SendIO((struct IORequest *)rb);
            for (;;) {
                m->voice.message.io_Command = CMD_READ;
                m->voice.message.io_Error = 0;
                DoIO((struct IORequest *)m);
                if (m->voice.message.io_Error == ND_NoWrite) break;
                if (m->voice.message.io_Error) { reads = -1000; break; }
                reads++;
                if (m->sync & 1) shapes++;
                if (m->sync & 2) words++;
                if (m->sync & 4) syls++;
                if (m->width > maxw) maxw = m->width;
                if (m->height > maxh) maxh = m->height;
                if (reads > 2000) break;
            }
            WaitIO((struct IORequest *)rb);
            check(rb->message.io_Error == 0, "async write with reads completes", rb->message.io_Error, 0);
            check(reads >= 10, "mouth reads returned events", reads, 10);
            check(words >= 3 && syls >= 6, "word and syllable sync events", words, syls);
            check(shapes >= 5 && maxw > 0 && maxh > 0, "mouth shapes change", shapes, maxh);
            rb->mouths = 0;
            rb->flags = NDF_NEWIORB;
        }
        FreeSysObject(ASOT_IOREQUEST, m);
    }

    /* AbortIO of a write in progress, and CMD_FLUSH of a queued one */
    {
        static char longtext[4000];
        struct narrator_rb *rb2;
        int i;
        longtext[0] = 0;
        for (i = 0; i < 40; i++) strcat(longtext, "DHIHS IHZ AX LAO4NX SEH4NTUNS. ");
        rb->message.io_Command = CMD_WRITE;
        rb->message.io_Data = longtext;
        rb->message.io_Length = strlen(longtext);
        SendIO((struct IORequest *)rb);
        sleep_ms(100);
        AbortIO((struct IORequest *)rb);
        WaitIO((struct IORequest *)rb);
        check(rb->message.io_Error == IOERR_ABORTED, "AbortIO -> IOERR_ABORTED", rb->message.io_Error, IOERR_ABORTED);

        rb2 = (struct narrator_rb *)AllocSysObjectTags(ASOT_IOREQUEST, ASOIOR_Duplicate, rb, TAG_END);
        SendIO((struct IORequest *)rb);          /* long write */
        rb2->message.io_Command = CMD_WRITE;
        rb2->message.io_Data = (APTR)SENT;
        rb2->message.io_Length = strlen(SENT);
        SendIO((struct IORequest *)rb2);         /* queued behind it */
        {
            struct IOStdReq *f = (struct IOStdReq *)AllocSysObjectTags(ASOT_IOREQUEST,
                ASOIOR_Duplicate, rb, TAG_END);
            f->io_Command = CMD_FLUSH;
            f->io_Message.mn_ReplyPort = rport;
            DoIO((struct IORequest *)f);
            check(f->io_Error == 0, "CMD_FLUSH", f->io_Error, 0);
            FreeSysObject(ASOT_IOREQUEST, f);
        }
        WaitIO((struct IORequest *)rb);
        WaitIO((struct IORequest *)rb2);
        check(rb->message.io_Error == IOERR_ABORTED && rb2->message.io_Error == IOERR_ABORTED,
              "CMD_FLUSH aborts current and queued writes", rb->message.io_Error, rb2->message.io_Error);
        FreeSysObject(ASOT_IOREQUEST, rb2);
    }

    /* old-style IORB (no NDF_NEWIORB) and the classic unit */
    close_nar(rb);
    rb = open_nar(0, 0, &err);
    check(rb && speak(rb, SENT) == 0, "old-style IORB speaks", err, 0);
    if (rb) {
        rb->mode = MANUALF0;
        check(speak(rb, SENT) == ND_ModeErr, "MANUALF0 needs NDF_NEWIORB", rb->message.io_Error, ND_ModeErr);
        close_nar(rb);
    }
    rb = open_nar(1, 1, &err);
    check(rb && speak(rb, SENT) == 0, "unit 1 (classic voice) speaks", err, 0);
    if (rb) close_nar(rb);
}

int main(int argc, char **argv)
{
    int i;
    port = AllocSysObjectTags(ASOT_PORT, TAG_END);
    rport = AllocSysObjectTags(ASOT_PORT, TAG_END);
    for (i = 1; i < argc; i++)
        if (!strncmp(argv[i], "DEVICE=", 7)) devname = argv[i] + 7;

    if (argc >= 3 && !strcmp(argv[1], "SPEAK")) {
        long err;
        int unit = 0;
        struct narrator_rb *rb;
        for (i = 3; i + 1 < argc; i++)
            if (!strcmp(argv[i], "UNIT")) unit = atoi(argv[i + 1]);
        rb = open_nar(unit, 1, &err);
        if (!rb) { printf("open failed %ld\n", err); return 20; }
        for (i = 3; i < argc; i++) {
            if (!strcmp(argv[i], "RATE") && i + 1 < argc) rb->rate = atoi(argv[++i]);
            else if (!strcmp(argv[i], "PITCH") && i + 1 < argc) rb->pitch = atoi(argv[++i]);
            else if (!strcmp(argv[i], "ROBOT")) rb->mode = ROBOTICF0;
            else if (!strcmp(argv[i], "FEMALE")) rb->sex = FEMALE;
        }
        err = speak(rb, argv[2]);
        printf("speak: %ld\n", err);
        close_nar(rb);
        return err ? 10 : 0;
    }
    setvbuf(stdout, NULL, _IONBF, 0);
    test_suite();
    printf("nartest: %d passed, %d failed\n", passes, fails);
    return fails ? 10 : 0;
}
