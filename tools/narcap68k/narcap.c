// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>
//
// narcap: capture what the ORIGINAL 68k narrator.device says, for the
// before/after comparison. Runs on classic AmigaOS (68k).
//
// For each line of an input text file it calls translator.library
// Translate(), saves the phoneme string, and speaks it with narrator.device
// while audio.device's BeginIO is patched: every CMD_WRITE buffer (8-bit
// signed samples + period + channel mask) is copied to a capture buffer and
// completed at once instead of being played, so nothing is heard and the
// narrator runs as fast as it can synthesize. Other audio commands pass
// through. Output per line: NN.raw (records) and NN.txt (phonemes).
//
//   narcap <textfile> <outdir> [PHON]
//
// With PHON the lines are already narrator phonemes (no translation).
//
// NN.raw records: ULONG length, UWORD period, UWORD unit, then `length`
// sample bytes (padded to even). Converted to WAV by tools/narcap2wav.py.

#include <exec/types.h>
#include <exec/memory.h>
#include <exec/execbase.h>
#include <devices/audio.h>
#include <devices/narrator.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/translator.h>
#include <string.h>

#define CAP_SIZE (3L * 1024 * 1024)
#define DEV_BEGINIO (-30)

struct Library *TranslatorBase;
static UBYTE *cap_buf;
static ULONG cap_len;
static ULONG cap_writes;
static UWORD cap_overflow;
APTR orig_beginio;

/* called from the asm stub with the IORequest; returns 1 if handled */
LONG capture_io(struct IOAudio *io)
{
    if (io->ioa_Request.io_Command != CMD_WRITE)
        return 0;
    if (io->ioa_Data && io->ioa_Length) {
        ULONG need = 8 + ((io->ioa_Length + 1) & ~1UL);
        if (cap_len + need <= CAP_SIZE) {
            UBYTE *p = cap_buf + cap_len;
            *(ULONG *)p = io->ioa_Length;
            *(UWORD *)(p + 4) = io->ioa_Period;
            *(UWORD *)(p + 6) = (UWORD)(ULONG)io->ioa_Request.io_Unit;
            CopyMem(io->ioa_Data, p + 8, io->ioa_Length);
            cap_len += need;
        } else {
            cap_overflow = 1;
        }
        cap_writes++;
    }
    io->ioa_Request.io_Error = 0;
    if (!(io->ioa_Request.io_Flags & IOF_QUICK))
        ReplyMsg(&io->ioa_Request.io_Message);
    return 1;
}

/* BeginIO replacement: A1 = IORequest, A6 = device. */
extern void patch_beginio(void);
__asm__(
    "    .text\n"
    "    .even\n"
    "    .globl _patch_beginio\n"
    "_patch_beginio:\n"
    "    movem.l d0-d1/a0-a1,-(sp)\n"
    "    move.l  a1,-(sp)\n"
    "    jsr     _capture_io\n"
    "    addq.l  #4,sp\n"
    "    tst.l   d0\n"
    "    movem.l (sp)+,d0-d1/a0-a1\n"
    "    bne.s   1f\n"
    "    move.l  _orig_beginio,-(sp)\n"
    "1:  rts\n");

static void put_num(char *s, int n)
{
    s[0] = (char)('0' + (n / 10) % 10);
    s[1] = (char)('0' + n % 10);
}

int main(int argc, char **argv)
{
    struct MsgPort *port = NULL;
    struct narrator_rb *nio = NULL;
    struct IOAudio *aio = NULL;
    struct Device *audiodev = NULL;
    static UBYTE chans[4] = { 3, 5, 10, 12 };
    static char line[2048], phon[8192], path[256];
    BPTR in = 0;
    int n = 0, nar_open = 0, aud_open = 0;
    LONG rc;

    if (argc != 3 && argc != 4) {
        Printf("usage: narcap <textfile> <outdir> [PHON]\n");
        return 5;
    }
    TranslatorBase = OpenLibrary("translator.library", 0);
    cap_buf = AllocMem(CAP_SIZE, MEMF_ANY);
    port = CreateMsgPort();
    if (!TranslatorBase || !cap_buf || !port) {
        Printf("narcap: setup failed\n");
        goto out;
    }
    nio = (struct narrator_rb *)CreateIORequest(port, sizeof(struct narrator_rb));
    aio = (struct IOAudio *)CreateIORequest(port, sizeof(struct IOAudio));
    if (!nio || !aio) goto out;

    /* find audio.device's base by opening it (no channel allocation) */
    aio->ioa_Length = 0;
    if (OpenDevice("audio.device", 0, (struct IORequest *)aio, 0) != 0) {
        Printf("narcap: audio.device\n");
        goto out;
    }
    aud_open = 1;
    audiodev = aio->ioa_Request.io_Device;

    nio->flags = NDF_NEWIORB;
    if (OpenDevice("narrator.device", 0, (struct IORequest *)nio, 0) != 0) {
        Printf("narcap: narrator.device\n");
        goto out;
    }
    nar_open = 1;

    Forbid();
    orig_beginio = SetFunction((struct Library *)audiodev, DEV_BEGINIO, (APTR)patch_beginio);
    Permit();

    in = Open(argv[1], MODE_OLDFILE);
    if (!in) { Printf("narcap: cannot open %s\n", (LONG)argv[1]); goto unpatch; }

    while (FGets(in, line, sizeof line - 1)) {
        LONG len = strlen(line);
        BPTR f;
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) line[--len] = 0;
        if (!len) continue;
        n++;
        memset(phon, 0, sizeof phon);
        if (argc == 4) {
            strcpy(phon, line);
            rc = 0;
        } else {
            rc = Translate(line, len, phon, sizeof phon - 1);
        }
        cap_len = 0;
        cap_writes = 0;
        cap_overflow = 0;
        nio->ch_masks = chans;
        nio->nm_masks = sizeof chans;
        nio->message.io_Command = CMD_WRITE;
        nio->message.io_Data = phon;
        nio->message.io_Length = strlen(phon);
        DoIO((struct IORequest *)nio);

        strcpy(path, argv[2]);
        AddPart(path, "00.raw", sizeof path);
        put_num(path + strlen(path) - 6, n);
        if ((f = Open(path, MODE_NEWFILE)) != 0) {
            Write(f, cap_buf, cap_len);
            Close(f);
        }
        path[strlen(path) - 3] = 't'; path[strlen(path) - 2] = 'x'; path[strlen(path) - 1] = 't';
        if ((f = Open(path, MODE_NEWFILE)) != 0) {
            Write(f, phon, strlen(phon));
            Write(f, "\n", 1);
            Close(f);
        }
        Printf("%ld: tr=%ld err=%ld writes=%ld bytes=%ld%s\n", (LONG)n, rc,
               (LONG)nio->message.io_Error, (LONG)cap_writes, (LONG)cap_len,
               (LONG)(cap_overflow ? " OVERFLOW" : ""));
    }
    Close(in);

unpatch:
    Forbid();
    SetFunction((struct Library *)audiodev, DEV_BEGINIO, orig_beginio);
    Permit();
out:
    if (nar_open) CloseDevice((struct IORequest *)nio);
    if (aud_open) CloseDevice((struct IORequest *)aio);
    if (nio) DeleteIORequest((struct IORequest *)nio);
    if (aio) DeleteIORequest((struct IORequest *)aio);
    if (port) DeleteMsgPort(port);
    if (cap_buf) FreeMem(cap_buf, CAP_SIZE);
    if (TranslatorBase) CloseLibrary(TranslatorBase);
    return 0;
}
