// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>
//
// narrator.device for AmigaOS 4 (native PPC), compatible with the classic
// V37 narrator.device: struct narrator_rb / mouth_rb from
// <devices/narrator.h>, CMD_WRITE, CMD_READ (mouths, word and syllable
// sync), CMD_FLUSH, CMD_RESET, CMD_STOP, CMD_START, AbortIO. 68k programs
// reach it through a 68k jump table of EmuTrap stubs.
//
// Requests are queued to a device process, which runs the portable speech
// core (src/) and plays the result through ahi.device. Units: 0 = default
// voice (ENV:Narrator/Voice, "natural" unless set to "classic"), 1 = the
// classic formant voice.
//
// ENV variables (all optional):
//   Narrator/Voice     natural | classic    default voice for unit 0
//   Narrator/DataDir   where kal16.voice is (default DEVS:narrator)
//   Narrator/AHIUnit   AHI unit number (default 0)
//   Narrator/Capture   file name: append 16-bit big-endian mono PCM at
//                      22050 Hz to this file instead of playing (tests)
//
// Built freestanding (-nostdlib): os4/common/minilibc.c supplies the C
// functions the core needs.

#include <exec/exec.h>
#include <exec/emulation.h>
#include <exec/resident.h>
#include <dos/dos.h>
#include <dos/dostags.h>
#include <dos/var.h>
#include <devices/ahi.h>
#include <devices/narrator.h>
#include <interfaces/exec.h>
#include <interfaces/dos.h>

#include "amiga_speech/narrator.h"
#include <string.h>

#ifndef DEVNAME
#define DEVNAME     "narrator.device"
#endif
#define DEVVERSION  50
#define DEVREVISION 1
#define DEVDATE     "01.11.2026"
#define VSTRING     "narrator.device 50.1 (" DEVDATE ") amiga_speech\r\n"

#ifdef NARDEBUG
#define TRACE(...) IExec->DebugPrintF("[narrator] " __VA_ARGS__)
#else
#define TRACE(...) ((void)0)
#endif

#define PLAY_SAMPLES 4096          /* per AHI buffer (~186 ms) */
#define NUM_EVENTS   2048

struct NarratorBase {
    struct Library lib;
    BPTR           seglist;
};

/* one per OpenDevice(); io_Unit points at it so reads match writes */
struct NarUser {
    uint32 magic;
    uint32 unit;
};
#define USER_MAGIC 0x4e415255u     /* "NARU" */

struct ExecIFace *IExec;
struct DOSIFace  *IDOS;
static struct Library *DOSBase;
extern struct ExecIFace *ml_IExec;

/* request queues, protected by Forbid() */
static struct List  g_writes;
static struct List  g_reads;
static struct IORequest *g_cur;            /* write being spoken */
static volatile int g_abort, g_stopped, g_quit;
static struct Task *g_task;                /* device process */
static int8         g_sigbit = -1;
static struct NarratorBase *g_base;

/* ------------------------------------------------------------------ */
/* helpers                                                             */
/* ------------------------------------------------------------------ */

static void reply(struct IORequest *io)
{
    if (!(io->io_Flags & IOF_QUICK))
        IExec->ReplyMsg(&io->io_Message);
}

static int in_list(struct List *l, struct Node *n)
{
    struct Node *x;
    for (x = l->lh_Head; x->ln_Succ; x = x->ln_Succ)
        if (x == n) return 1;
    return 0;
}

static void wake(void)
{
    if (g_task && g_sigbit >= 0)
        IExec->Signal(g_task, 1UL << g_sigbit);
}

/* reply every pending read of `unit` (NULL = all) with ND_NoWrite;
   call under Forbid */
static void end_reads(struct Unit *unit)
{
    struct Node *n, *nx;
    for (n = g_reads.lh_Head; (nx = n->ln_Succ); n = nx) {
        struct IORequest *r = (struct IORequest *)n;
        if (!unit || r->io_Unit == unit) {
            IExec->Remove(n);
            r->io_Error = ND_NoWrite;
            reply(r);
        }
    }
}

/* does `unit` have a write that wants reads (mouths or sync)? */
static int write_wants_reads(struct Unit *unit)
{
    struct Node *n;
    struct narrator_rb *w = (struct narrator_rb *)g_cur;
    if (w && w->message.io_Unit == unit &&
        (w->mouths || (w->flags & (NDF_WORDSYNC | NDF_SYLSYNC))))
        return 1;
    for (n = g_writes.lh_Head; n->ln_Succ; n = n->ln_Succ) {
        w = (struct narrator_rb *)n;
        if (w->message.io_Unit == unit &&
            (w->mouths || (w->flags & (NDF_WORDSYNC | NDF_SYLSYNC))))
            return 1;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* device process: environment, voice data                              */
/* ------------------------------------------------------------------ */

static uint8 *g_voice_img;
static uint32 g_voice_size;
static int    g_voice_tried;

static void getenv_str(const char *name, char *buf, int size, const char *def)
{
    if (IDOS->GetVar(name, buf, size, 0) <= 0) {
        int i;
        for (i = 0; def[i] && i < size - 1; i++) buf[i] = def[i];
        buf[i] = 0;
    }
}

static void load_voice(void)
{
    char dir[200], path[260];
    BPTR fh;
    int i, j;
    if (g_voice_tried) return;
    g_voice_tried = 1;
    getenv_str("Narrator/DataDir", dir, sizeof dir, "DEVS:narrator");
    for (i = 0; dir[i] && i < 200; i++) path[i] = dir[i];
    path[i] = 0;
    if (!IDOS->AddPart(path, "kal16.voice", sizeof path)) return;
    fh = IDOS->Open(path, MODE_OLDFILE);
    if (!fh) {
        IExec->DebugPrintF("[narrator] no %s, using the classic voice\n", path);
        return;
    }
    {
        int64 sz = IDOS->GetFileSize(fh);
        if (sz > 64 && sz < 64 * 1024 * 1024) {
            g_voice_img = IExec->AllocVecTags((uint32)sz, AVT_Type, MEMF_SHARED, TAG_END);
            if (g_voice_img && IDOS->Read(fh, g_voice_img, (int32)sz) == (int32)sz) {
                g_voice_size = (uint32)sz;
                if (nar_set_voice_image(g_voice_img, g_voice_size) != 0) {
                    IExec->DebugPrintF("[narrator] %s is not a voice file\n", path);
                    IExec->FreeVec(g_voice_img);
                    g_voice_img = NULL;
                }
            } else if (g_voice_img) {
                IExec->FreeVec(g_voice_img);
                g_voice_img = NULL;
            }
        }
    }
    IDOS->Close(fh);
    (void)j;
}

/* ------------------------------------------------------------------ */
/* mouths and sync                                                      */
/* ------------------------------------------------------------------ */

typedef struct { uint32 pos; uint8 ph, flags; } nevent;

typedef struct {
    struct narrator_rb *rb;
    /* events */
    nevent ev[NUM_EVENTS];
    int ev_head, ev_tail;
    uint8 width, height, pending;   /* current shape, pending sync bits */
    /* audio */
    struct MsgPort    *port;
    struct AHIRequest *io[2];
    int16  *buf[2];
    int     cur, fill;
    struct AHIRequest *link;
    uint32  submitted;             /* samples handed to AHI */
    BPTR    capture;
    int     ahi_open;
    int     pri_set;
    int8    old_pri;
} speaker;

/* mouth shape per phoneme: width, height (classic range: the RKM example
   draws width*3 by height*2/3 pixels) */
static void mouth_for(int ph, uint8 *w, uint8 *h)
{
    const char *n = nar_phoneme_name(ph);
    uint8 W = 16, H = 8;
    if (!n[0]) { W = 16; H = 0; }
    else if (!strcmp(n, "AA") || !strcmp(n, "AO") || !strcmp(n, "AW") || !strcmp(n, "AY")) { W = 20; H = 36; }
    else if (!strcmp(n, "AE") || !strcmp(n, "AH") || !strcmp(n, "EH")) { W = 22; H = 28; }
    else if (!strcmp(n, "IY") || !strcmp(n, "IH") || !strcmp(n, "IX") || !strcmp(n, "EY") || !strcmp(n, "Y")) { W = 26; H = 14; }
    else if (!strcmp(n, "UW") || !strcmp(n, "OW") || !strcmp(n, "UH") || !strcmp(n, "OY") ||
             !strcmp(n, "W") || !strcmp(n, "WH") || !strcmp(n, "OH")) { W = 10; H = 18; }
    else if (!strcmp(n, "ER") || !strcmp(n, "AX") || !strcmp(n, "R") || !strcmp(n, "RX")) { W = 16; H = 18; }
    else if (!strcmp(n, "M") || !strcmp(n, "B") || !strcmp(n, "P")) { W = 18; H = 0; }
    else if (!strcmp(n, "F") || !strcmp(n, "V")) { W = 20; H = 4; }
    else if (!strcmp(n, "SH") || !strcmp(n, "ZH") || !strcmp(n, "CH") || !strcmp(n, "J")) { W = 12; H = 8; }
    *w = W;
    *h = H;
}

static void on_event(void *user, uint32_t pos, int ph, int flags)
{
    speaker *s = (speaker *)user;
    int nt = (s->ev_tail + 1) % NUM_EVENTS;
    if (nt == s->ev_head) s->ev_head = (s->ev_head + 1) % NUM_EVENTS;   /* drop oldest */
    s->ev[s->ev_tail].pos = pos;
    s->ev[s->ev_tail].ph = (uint8)ph;
    s->ev[s->ev_tail].flags = (uint8)flags;
    s->ev_tail = nt;
}

/* hand the events up to sample position `pos` to waiting CMD_READs */
static void deliver(speaker *s, uint32 pos)
{
    struct narrator_rb *rb = s->rb;
    uint8 want = 0;
    if (rb->mouths) want |= 1;
    if (rb->flags & NDF_NEWIORB) {
        if (rb->flags & NDF_WORDSYNC) want |= 2;
        if (rb->flags & NDF_SYLSYNC) want |= 4;
    }
    while (s->ev_head != s->ev_tail && s->ev[s->ev_head].pos <= pos) {
        nevent *e = &s->ev[s->ev_head];
        uint8 w, h;
        mouth_for(e->ph, &w, &h);
        if (w != s->width || h != s->height) {
            s->width = w;
            s->height = h;
            s->pending |= 1;
        }
        if (e->flags & NAR_EV_WORD) s->pending |= 2;
        if (e->flags & NAR_EV_SYL) s->pending |= 4;
        s->ev_head = (s->ev_head + 1) % NUM_EVENTS;
    }
    if (!want || !(s->pending & want)) return;
    IExec->Forbid();
    {
        struct Node *n;
        for (n = g_reads.lh_Head; n->ln_Succ; n = n->ln_Succ) {
            struct mouth_rb *m = (struct mouth_rb *)n;
            if (m->voice.message.io_Unit != rb->message.io_Unit) continue;
            IExec->Remove(n);
            m->width = s->width;
            m->height = s->height;
            m->sync = s->pending & want;
            m->voice.message.io_Error = 0;
            reply((struct IORequest *)m);
            s->pending = 0;
            break;
        }
    }
    IExec->Permit();
}

/* ------------------------------------------------------------------ */
/* audio output                                                         */
/* ------------------------------------------------------------------ */

static int ahi_open(speaker *s)
{
    char ubuf[16];
    int unit;
    if (s->ahi_open) return 1;
    getenv_str("Narrator/AHIUnit", ubuf, sizeof ubuf, "0");
    unit = ubuf[0] >= '0' && ubuf[0] <= '3' ? ubuf[0] - '0' : 0;
    s->port = IExec->AllocSysObjectTags(ASOT_PORT, TAG_END);
    if (!s->port) return 0;
    s->io[0] = IExec->AllocSysObjectTags(ASOT_IOREQUEST, ASOIOR_Size, sizeof(struct AHIRequest),
                                         ASOIOR_ReplyPort, s->port, TAG_END);
    if (!s->io[0]) return 0;
    s->io[0]->ahir_Version = 4;
    if (IExec->OpenDevice(AHINAME, unit, (struct IORequest *)s->io[0], 0) != 0) {
        IExec->FreeSysObject(ASOT_IOREQUEST, s->io[0]);
        s->io[0] = NULL;
        return 0;
    }
    s->io[1] = IExec->AllocSysObjectTags(ASOT_IOREQUEST, ASOIOR_Duplicate, s->io[0], TAG_END);
    if (!s->io[1]) return 0;
    s->ahi_open = 1;
    return 1;
}

static void ahi_close(speaker *s)
{
    if (s->io[1]) IExec->FreeSysObject(ASOT_IOREQUEST, s->io[1]);
    if (s->io[0]) {
        if (s->ahi_open) IExec->CloseDevice((struct IORequest *)s->io[0]);
        IExec->FreeSysObject(ASOT_IOREQUEST, s->io[0]);
    }
    if (s->port) IExec->FreeSysObject(ASOT_PORT, s->port);
    s->io[0] = s->io[1] = NULL;
    s->port = NULL;
    s->ahi_open = 0;
}

static void play_submit(speaker *s)
{
    struct AHIRequest *io = s->io[s->cur];
    io->ahir_Std.io_Command = CMD_WRITE;
    io->ahir_Std.io_Data = s->buf[s->cur];
    io->ahir_Std.io_Length = (uint32)s->fill * 2;
    io->ahir_Std.io_Offset = 0;
    io->ahir_Frequency = NAR_OUTPUT_RATE;
    io->ahir_Type = AHIST_M16S;
    io->ahir_Volume = 0x10000;
    io->ahir_Position = 0x8000;
    io->ahir_Link = s->link;
    IExec->SendIO((struct IORequest *)io);
    if (s->link)
        IExec->WaitIO((struct IORequest *)s->link);   /* previous done = this started */
    deliver(s, s->submitted + (uint32)s->fill);
    s->submitted += (uint32)s->fill;
    s->link = io;
    s->cur ^= 1;
    s->fill = 0;
}

static void play_drain(speaker *s, int abort)
{
    if (s->fill && !abort) play_submit(s);
    if (s->link) {
        if (abort) IExec->AbortIO((struct IORequest *)s->link);
        IExec->WaitIO((struct IORequest *)s->link);
        s->link = NULL;
    }
    s->fill = 0;
}

static int speak_sink(void *user, const int16_t *smp, size_t n)
{
    speaker *s = (speaker *)user;
    if (!s->pri_set) {
        /* the classic device raised its priority once speech starts */
        int8 pri = (s->rb->flags & NDF_NEWIORB) ? s->rb->priority : DEFPRIORITY;
        if (pri > 20) pri = 20;           /* see DESIGN.md: capped */
        s->old_pri = IExec->SetTaskPri(g_task, pri);
        s->pri_set = 1;
    }
    while (g_stopped && !g_abort)
        IExec->Wait(1UL << g_sigbit);
    if (g_abort) return 1;
    if (s->capture) {
        IDOS->Write(s->capture, smp, (int32)(n * 2));
        s->submitted += (uint32)n;
        deliver(s, s->submitted);
        return 0;
    }
    if (!s->ahi_open) return 0;          /* no audio: discard */
    while (n > 0) {
        size_t room = PLAY_SAMPLES - (size_t)s->fill, k = n < room ? n : room, i;
        for (i = 0; i < k; i++) s->buf[s->cur][s->fill + (int)i] = smp[i];
        s->fill += (int)k;
        smp += k;
        n -= k;
        if (s->fill == PLAY_SAMPLES) {
            play_submit(s);
            if (g_abort) return 1;
        }
    }
    return 0;
}

static speaker *g_spk;

static void handle_write(struct narrator_rb *rb)
{
    speaker *s = g_spk;
    struct NarUser *u = (struct NarUser *)rb->message.io_Unit;
    nar_params p;
    char vbuf[16], cap[200];
    size_t err = 0;
    int r;

    nar_params_default(&p);
    p.rate = rb->rate;
    p.pitch = rb->pitch;
    p.mode = rb->mode;
    p.sex = rb->sex;
    p.volume = rb->volume;
    p.sampfreq = rb->sampfreq;
    p.mouths = rb->mouths;
    p.flags = rb->flags;
    if (rb->flags & NDF_NEWIORB) {
        p.F0enthusiasm = rb->F0enthusiasm;
        p.F0perturb = rb->F0perturb;
        p.F1adj = rb->F1adj; p.F2adj = rb->F2adj; p.F3adj = rb->F3adj;
        p.A1adj = rb->A1adj; p.A2adj = rb->A2adj; p.A3adj = rb->A3adj;
        p.articulate = rb->articulate;
        p.centralize = rb->centralize;
        p.centphon = rb->centphon;
        p.AVbias = rb->AVbias;
        p.AFbias = rb->AFbias;
    }
    getenv_str("Narrator/Voice", vbuf, sizeof vbuf, "natural");
    p.voice = (u && u->unit == 1) || vbuf[0] == 'c' || vbuf[0] == 'C'
              ? NAR_VOICE_CLASSIC : NAR_VOICE_NATURAL;
    if (p.voice == NAR_VOICE_NATURAL) load_voice();

    s->rb = rb;
    s->ev_head = s->ev_tail = 0;
    s->pending = 0;
    s->width = s->height = 0;
    s->submitted = 0;
    s->fill = 0;
    s->cur = 0;
    s->link = NULL;
    s->pri_set = 0;
    s->capture = 0;
    getenv_str("Narrator/Capture", cap, sizeof cap, "");
    if (cap[0]) {
        s->capture = IDOS->Open(cap, MODE_READWRITE);
        if (s->capture) IDOS->ChangeFilePosition(s->capture, 0, OFFSET_END);
    } else if (!ahi_open(s)) {
        IExec->DebugPrintF("[narrator] cannot open ahi.device\n");
    }

    r = nar_speak_ex(&p, (const char *)rb->message.io_Data, rb->message.io_Length,
                     speak_sink, s, on_event, s, &err);
    if (s->capture) {
        IDOS->Close(s->capture);
        s->capture = 0;
    } else {
        play_drain(s, g_abort);
    }
    deliver(s, 0xffffffffu);
    if (s->pri_set) IExec->SetTaskPri(g_task, s->old_pri);

    IExec->Forbid();
    rb->message.io_Actual = rb->message.io_Length;
    if (g_abort) rb->message.io_Error = IOERR_ABORTED;
    else rb->message.io_Error = (BYTE)(r < 0 ? r : 0);
    g_cur = NULL;
    g_abort = 0;
    if (!write_wants_reads(rb->message.io_Unit))
        end_reads(rb->message.io_Unit);
    reply((struct IORequest *)&rb->message);
    IExec->Permit();
}

static int32 proc_main(STRPTR args, int32 len, struct ExecBase *sysbase)
{
    speaker *s;
    (void)args; (void)len; (void)sysbase;

    s = IExec->AllocVecTags(sizeof *s, AVT_Type, MEMF_SHARED, AVT_ClearWithValue, 0, TAG_END);
    if (s) {
        s->buf[0] = IExec->AllocVecTags(PLAY_SAMPLES * 2, AVT_Type, MEMF_SHARED, TAG_END);
        s->buf[1] = IExec->AllocVecTags(PLAY_SAMPLES * 2, AVT_Type, MEMF_SHARED, TAG_END);
    }
    IExec->Forbid();
    g_spk = s;
    g_sigbit = IExec->AllocSignal(-1);
    IExec->Permit();

    TRACE("process up, sigbit %ld\n", (long)g_sigbit);
    while (s && s->buf[0] && s->buf[1] && g_sigbit >= 0) {
        struct narrator_rb *rb = NULL;
        int quit = 0;
        IExec->Forbid();
        if (g_writes.lh_Head->ln_Succ && !g_stopped) {
            rb = (struct narrator_rb *)IExec->RemHead(&g_writes);
            g_cur = (struct IORequest *)&rb->message;
            g_abort = 0;
        } else if (g_quit && !g_writes.lh_Head->ln_Succ) {
            quit = 1;
        }
        IExec->Permit();
        if (rb) {
            TRACE("write %lx len %lu\n", (unsigned long)rb, (unsigned long)rb->message.io_Length);
            handle_write(rb);
            TRACE("write done err %ld\n", (long)rb->message.io_Error);
            continue;
        }
        if (quit) break;
        IExec->Wait((1UL << g_sigbit) | SIGBREAKF_CTRL_C);
    }

    /* shut down (only Expunge asks for this, with nobody holding the
       device open): free everything, then leave under Forbid so the code
       is not unloaded before we are gone */
    TRACE("process exiting\n");
    IExec->Forbid();
    end_reads(NULL);
    IExec->Permit();
    if (s) {
        ahi_close(s);
        if (s->buf[0]) IExec->FreeVec(s->buf[0]);
        if (s->buf[1]) IExec->FreeVec(s->buf[1]);
        IExec->FreeVec(s);
    }
    nar_set_voice_image(NULL, 0);
    if (g_voice_img) IExec->FreeVec(g_voice_img);
    g_voice_img = NULL;
    g_voice_tried = 0;
    IExec->Forbid();
    if (g_sigbit >= 0) IExec->FreeSignal(g_sigbit);
    g_sigbit = -1;
    g_spk = NULL;
    g_task = NULL;
    g_quit = 0;
    return 0;
}

static int start_process(void)
{
    struct Process *pr;
    if (g_task) {
        g_quit = 0;            /* re-opened while an expunge was pending */
        return 1;
    }
    pr = IDOS->CreateNewProcTags(
        NP_Entry, proc_main,
        NP_Name, DEVNAME,
        NP_Priority, 0,
        NP_StackSize, 64 * 1024,
        NP_Child, FALSE,
        NP_Input, ZERO,
        NP_Output, ZERO,
        NP_CloseInput, FALSE,
        NP_CloseOutput, FALSE,
        NP_CurrentDir, ZERO,
        NP_CopyVars, FALSE,
        NP_Path, ZERO,
        TAG_END);
    TRACE("CreateNewProc -> %lx\n", (unsigned long)pr);
    if (!pr) return 0;
    g_task = (struct Task *)pr;
    return 1;
}

/* ------------------------------------------------------------------ */
/* device interface                                                     */
/* ------------------------------------------------------------------ */

static uint32 dev_Obtain(struct DeviceManagerInterface *Self) { return ++Self->Data.RefCount; }
static uint32 dev_Release(struct DeviceManagerInterface *Self) { return --Self->Data.RefCount; }

static struct NarratorBase *dev_Open(struct DeviceManagerInterface *Self,
                                     struct IORequest *io, uint32 unit, uint32 flags)
{
    struct NarratorBase *base = (struct NarratorBase *)Self->Data.LibBase;
    struct narrator_rb *rb = (struct narrator_rb *)io;
    struct NarUser *u;
    (void)flags;

    TRACE("open unit %lu flags %lx\n", (unsigned long)unit, (unsigned long)((struct narrator_rb *)io)->flags);
    io->io_Error = 0;
    if (unit > 1) {
        io->io_Error = ND_UnitErr;
        io->io_Device = NULL;
        return NULL;
    }
    u = IExec->AllocVecTags(sizeof *u, AVT_Type, MEMF_SHARED, TAG_END);
    if (!u) {
        io->io_Error = ND_NoMem;
        io->io_Device = NULL;
        return NULL;
    }
    u->magic = USER_MAGIC;
    u->unit = unit;

    /* defaults, as the classic device filled them in */
    rb->rate = DEFRATE;
    rb->pitch = DEFPITCH;
    rb->mode = DEFMODE;
    rb->sex = DEFSEX;
    rb->volume = DEFVOL;
    rb->sampfreq = DEFFREQ;
    rb->mouths = 0;
    if (rb->flags & NDF_NEWIORB) {
        rb->F0enthusiasm = DEFF0ENTHUS;
        rb->F0perturb = DEFF0PERT;
        rb->F1adj = rb->F2adj = rb->F3adj = 0;
        rb->A1adj = rb->A2adj = rb->A3adj = 0;
        rb->articulate = DEFARTIC;
        rb->centralize = DEFCENTRAL;
        rb->centphon = NULL;
        rb->AVbias = rb->AFbias = 0;
        rb->priority = DEFPRIORITY;
    }

    if (!start_process()) {
        IExec->FreeVec(u);
        io->io_Error = IOERR_OPENFAIL;
        io->io_Device = NULL;
        return NULL;
    }
    io->io_Unit = (struct Unit *)u;
    io->io_Device = (struct Device *)base;
    base->lib.lib_OpenCnt++;
    base->lib.lib_Flags &= ~LIBF_DELEXP;
    return base;
}

static BPTR dev_Expunge(struct DeviceManagerInterface *Self);

static BPTR dev_Close(struct DeviceManagerInterface *Self, struct IORequest *io)
{
    struct NarratorBase *base = (struct NarratorBase *)Self->Data.LibBase;
    struct NarUser *u = (struct NarUser *)io->io_Unit;
    if (u && u->magic == USER_MAGIC) {
        u->magic = 0;
        IExec->FreeVec(u);
    }
    io->io_Unit = (struct Unit *)-1;
    io->io_Device = (struct Device *)-1;
    TRACE("close, opencnt %ld\n", (long)base->lib.lib_OpenCnt - 1);
    if (--base->lib.lib_OpenCnt == 0 && (base->lib.lib_Flags & LIBF_DELEXP))
        return dev_Expunge(Self);
    return ZERO;
}

static BPTR dev_Expunge(struct DeviceManagerInterface *Self)
{
    struct NarratorBase *base = (struct NarratorBase *)Self->Data.LibBase;
    BPTR seg;
    TRACE("expunge, opencnt %ld task %lx\n", (long)base->lib.lib_OpenCnt, (unsigned long)g_task);
    if (base->lib.lib_OpenCnt > 0 || g_task) {
        /* the process holds the voice data; ask it to free it and exit,
           the next expunge (e.g. avail flush) removes the code */
        if (base->lib.lib_OpenCnt == 0 && g_task) {
            g_quit = 1;
            IExec->Signal(g_task, SIGBREAKF_CTRL_C);
        }
        base->lib.lib_Flags |= LIBF_DELEXP;
        return ZERO;
    }
    seg = base->seglist;
    IExec->Remove(&base->lib.lib_Node);
    if (IDOS) IExec->DropInterface((struct Interface *)IDOS);
    if (DOSBase) IExec->CloseLibrary(DOSBase);
    IDOS = NULL;
    DOSBase = NULL;
    IExec->DeleteLibrary(&base->lib);
    return seg;
}

static void dev_BeginIO(struct DeviceManagerInterface *Self, struct IORequest *io)
{
    (void)Self;
    io->io_Message.mn_Node.ln_Type = NT_MESSAGE;
    io->io_Error = 0;
    switch (io->io_Command) {
    case CMD_WRITE:
        io->io_Flags &= ~IOF_QUICK;
        IExec->Forbid();
        IExec->AddTail(&g_writes, &io->io_Message.mn_Node);
        wake();
        IExec->Permit();
        return;
    case CMD_READ:
        IExec->Forbid();
        if (write_wants_reads(io->io_Unit)) {
            io->io_Flags &= ~IOF_QUICK;
            IExec->AddTail(&g_reads, &io->io_Message.mn_Node);
            IExec->Permit();
            return;
        }
        IExec->Permit();
        io->io_Error = ND_NoWrite;
        break;
    case CMD_FLUSH:
    case CMD_RESET:
        IExec->Forbid();
        while (g_writes.lh_Head->ln_Succ) {
            struct IORequest *w = (struct IORequest *)IExec->RemHead(&g_writes);
            w->io_Error = IOERR_ABORTED;
            reply(w);
        }
        if (g_cur) g_abort = 1;
        end_reads(NULL);
        if (io->io_Command == CMD_RESET) g_stopped = 0;
        wake();
        IExec->Permit();
        break;
    case CMD_STOP:
        g_stopped = 1;
        break;
    case CMD_START:
        g_stopped = 0;
        IExec->Forbid();
        wake();
        IExec->Permit();
        break;
    default:
        io->io_Error = IOERR_NOCMD;
        break;
    }
    reply(io);
}

static int32 dev_AbortIO(struct DeviceManagerInterface *Self, struct IORequest *io)
{
    (void)Self;
    IExec->Forbid();
    if (in_list(&g_writes, &io->io_Message.mn_Node) || in_list(&g_reads, &io->io_Message.mn_Node)) {
        IExec->Remove(&io->io_Message.mn_Node);
        io->io_Error = IOERR_ABORTED;
        reply(io);
    } else if (io == g_cur) {
        g_abort = 1;
        wake();
    }
    IExec->Permit();
    return 0;
}

/* ------------------------------------------------------------------ */
/* 68k jump table: EmuTraps into the PPC functions                      */
/* ------------------------------------------------------------------ */

static struct DeviceManagerInterface *self_of(uint32 *r)
{
    struct Library *b = (struct Library *)r[REG68K_A6 / 4];
    struct ExtendedLibrary *x = (struct ExtendedLibrary *)((uint32)b + b->lib_PosSize);
    return x->IDevice;
}

static uint32 s68_Open(uint32 *r)
{
    return (uint32)dev_Open(self_of(r), (struct IORequest *)r[REG68K_A1 / 4],
                            r[REG68K_D0 / 4], r[REG68K_D1 / 4]);
}
static uint32 s68_Close(uint32 *r) { return (uint32)dev_Close(self_of(r), (struct IORequest *)r[REG68K_A1 / 4]); }
static uint32 s68_Expunge(uint32 *r) { return (uint32)dev_Expunge(self_of(r)); }
static uint32 s68_Reserved(uint32 *r) { (void)r; return 0; }
static uint32 s68_BeginIO(uint32 *r) { dev_BeginIO(self_of(r), (struct IORequest *)r[REG68K_A1 / 4]); return 0; }
static uint32 s68_AbortIO(uint32 *r) { return (uint32)dev_AbortIO(self_of(r), (struct IORequest *)r[REG68K_A1 / 4]); }

static const struct EmuTrap t68_Open     = { TRAPINST, TRAPTYPE,   s68_Open };
static const struct EmuTrap t68_Close    = { TRAPINST, TRAPTYPE,   s68_Close };
static const struct EmuTrap t68_Expunge  = { TRAPINST, TRAPTYPE,   s68_Expunge };
static const struct EmuTrap t68_Reserved = { TRAPINST, TRAPTYPE,   s68_Reserved };
static const struct EmuTrap t68_BeginIO  = { TRAPINST, TRAPTYPENR, s68_BeginIO };
static const struct EmuTrap t68_AbortIO  = { TRAPINST, TRAPTYPE,   s68_AbortIO };

static const APTR vectors68k[] = {
    (APTR)&t68_Open, (APTR)&t68_Close, (APTR)&t68_Expunge, (APTR)&t68_Reserved,
    (APTR)&t68_BeginIO, (APTR)&t68_AbortIO, (APTR)-1
};

/* ------------------------------------------------------------------ */
/* PPC interface, init, resident                                        */
/* ------------------------------------------------------------------ */

static const APTR manager_vectors[] = {
    (APTR)dev_Obtain, (APTR)dev_Release, NULL, NULL,
    (APTR)dev_Open, (APTR)dev_Close, (APTR)dev_Expunge, NULL,
    (APTR)dev_BeginIO, (APTR)dev_AbortIO, (APTR)-1
};

static const struct TagItem manager_tags[] = {
    { MIT_Name, (uint32)"__device" },
    { MIT_VectorTable, (uint32)manager_vectors },
    { MIT_Version, 1 },
    { TAG_END, 0 }
};

static const APTR interfaces[] = { (APTR)manager_tags, NULL };

static struct Library *dev_Init(struct Library *lib, BPTR seglist, struct ExecIFace *iexec)
{
    struct NarratorBase *base = (struct NarratorBase *)lib;
    IExec = iexec;
    ml_IExec = iexec;
    base->lib.lib_Node.ln_Type = NT_DEVICE;
    base->lib.lib_Node.ln_Pri = 0;
    base->lib.lib_Node.ln_Name = (STRPTR)DEVNAME;
    base->lib.lib_Flags = LIBF_SUMUSED | LIBF_CHANGED;
    base->lib.lib_Version = DEVVERSION;
    base->lib.lib_Revision = DEVREVISION;
    base->lib.lib_IdString = (STRPTR)VSTRING;
    base->seglist = seglist;
    DOSBase = IExec->OpenLibrary("dos.library", 50);
    if (!DOSBase) return NULL;
    IDOS = (struct DOSIFace *)IExec->GetInterface(DOSBase, "main", 1, NULL);
    if (!IDOS) { IExec->CloseLibrary(DOSBase); return NULL; }
    IExec->NewList(&g_writes);
    IExec->NewList(&g_reads);
    g_base = base;
    return lib;
}

static const struct TagItem init_tags[] = {
    { CLT_DataSize, sizeof(struct NarratorBase) },
    { CLT_InitFunc, (uint32)dev_Init },
    { CLT_Interfaces, (uint32)interfaces },
    { CLT_Vector68K, (uint32)vectors68k },
    { TAG_END, 0 }
};

const char verstag_narrator[] __attribute__((used)) = "\0$VER: " VSTRING;

/* global: the link keeps it (and everything it reaches) with -u nar_romtag */
struct Resident nar_romtag __attribute__((used)) = {
    RTC_MATCHWORD,
    &nar_romtag,
    &nar_romtag + 1,
    RTF_NATIVE | RTF_AUTOINIT,
    DEVVERSION,
    NT_DEVICE,
    0,
    (STRPTR)DEVNAME,
    (STRPTR)VSTRING,
    (APTR)init_tags
};

int32 _start(void)
{
    return RETURN_FAIL;     /* a device, not a program */
}
