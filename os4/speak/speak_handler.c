// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>
//
// speak-handler: the SPEAK: DOS device for AmigaOS 4 (amiga_speech).
// Anything written to SPEAK: is spoken, a sentence at a time:
//     echo "Hello, I am your Amiga." >SPEAK:
//     type README >SPEAK:opt/s180/p140/f
// Options after "opt/" (or any path part), separated by '/':
//     s<n> rate (wpm)   p<n> pitch (Hz)   v<n> volume 0-64
//     m male  f female  r robot  n natural  c classic voice  x phonemes
//
// Mountlist (DEVS:DOSDrivers/SPEAK):
//     Handler = L:speak-handler
//     StackSize = 65536
//     Priority = 5
//     GlobVec = -1
//
// Built with -nostartfiles; uses only exec/dos and the two speech
// components through their interfaces.

#include <exec/exec.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <dos/filehandler.h>
#include <devices/narrator.h>
#include <interfaces/exec.h>
#include <interfaces/dos.h>
#include <interfaces/translator.h>

struct ExecIFace *IExec;
struct DOSIFace *IDOS;
extern struct ExecIFace *ml_IExec;
static struct Library *DOSBase;

static const char ver[] __attribute__((used)) = "$VER: speak-handler 50.1 (01.11.2026) amiga_speech";

#define BUFMAX 4000

typedef struct {
    struct narrator_rb *rb;
    struct MsgPort *port;
    struct Library *trbase;
    struct TranslatorIFace *itr;
    int phonemes;
    int len;
    char buf[BUFMAX + 1];
    char out[8192];
} speaker;

static int lower(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }

static int num(const char *s) { int v = 0; while (*s >= '0' && *s <= '9') v = v * 10 + (*s++ - '0'); return v; }

static void say_now(speaker *s, const char *text, int len)
{
    if (len <= 0) return;
    if (s->phonemes || !s->itr) {
        s->rb->message.io_Command = CMD_WRITE;
        s->rb->message.io_Data = (APTR)text;
        s->rb->message.io_Length = (uint32)len;
        IExec->DoIO((struct IORequest *)s->rb);
        return;
    }
    while (len > 0) {
        LONG r = s->itr->Translate(text, len, s->out, sizeof s->out);
        if (s->out[0]) {
            s->rb->message.io_Command = CMD_WRITE;
            s->rb->message.io_Data = s->out;
            s->rb->message.io_Length = 0;
            while (s->out[s->rb->message.io_Length]) s->rb->message.io_Length++;
            IExec->DoIO((struct IORequest *)s->rb);
        }
        if (r >= 0 || (r == -1 && !s->out[0])) break;
        text += -r;
        len -= (int)-r;
    }
}

/* speak complete sentences from the buffer, keep the rest */
static void flush(speaker *s, int all)
{
    int i, cut = -1;
    for (i = 0; i < s->len; i++) {
        char c = s->buf[i];
        if ((c == '.' || c == '?' || c == '!' || c == '\n') &&
            (i + 1 >= s->len || s->buf[i + 1] == ' ' || s->buf[i + 1] == '\n' || c == '\n'))
            cut = i + 1;
    }
    if (all || s->len >= BUFMAX) cut = s->len;
    if (cut <= 0) return;
    say_now(s, s->buf, cut);
    for (i = cut; i < s->len; i++) s->buf[i - cut] = s->buf[i];
    s->len -= cut;
}

static speaker *open_speaker(const char *name)
{
    speaker *s = IExec->AllocVecTags(sizeof *s, AVT_Type, MEMF_SHARED, AVT_ClearWithValue, 0, TAG_END);
    const char *p;
    int classic = 0;
    if (!s) return NULL;
    /* parse options: everything after the colon, split on '/' */
    for (p = name; *p && *p != ':'; p++) ;
    if (*p == ':') p++;
    while (*p) {
        const char *e = p;
        while (*e && *e != '/') e++;
        if (e - p == 1 && lower(*p) == 'c') classic = 1;
        p = *e ? e + 1 : e;
    }
    s->port = IExec->AllocSysObjectTags(ASOT_PORT, TAG_END);
    s->rb = IExec->AllocSysObjectTags(ASOT_IOREQUEST, ASOIOR_Size, sizeof(struct narrator_rb),
                                      ASOIOR_ReplyPort, s->port, TAG_END);
    if (!s->port || !s->rb) goto fail;
    s->rb->flags = NDF_NEWIORB;
    if (IExec->OpenDevice("narrator.device", classic ? 1 : 0, (struct IORequest *)s->rb, 0)) goto fail;
    for (p = name; *p && *p != ':'; p++) ;
    if (*p == ':') p++;
    while (*p) {
        const char *e = p;
        while (*e && *e != '/') e++;
        switch (lower(*p)) {
        case 's': if (p[1] >= '0' && p[1] <= '9') s->rb->rate = (UWORD)num(p + 1); break;
        case 'p': if (p[1] >= '0' && p[1] <= '9') s->rb->pitch = (UWORD)num(p + 1); break;
        case 'v': if (p[1] >= '0' && p[1] <= '9') s->rb->volume = (UWORD)num(p + 1); break;
        case 'm': if (e - p == 1) s->rb->sex = MALE; break;
        case 'f': if (e - p == 1) s->rb->sex = FEMALE; break;
        case 'r': if (e - p == 1) s->rb->mode = ROBOTICF0; break;
        case 'n': if (e - p == 1) s->rb->mode = NATURALF0; break;
        case 'x': if (e - p == 1) s->phonemes = 1; break;
        }
        p = *e ? e + 1 : e;
    }
    s->trbase = IExec->OpenLibrary("translator.library", 50);
    if (s->trbase)
        s->itr = (struct TranslatorIFace *)IExec->GetInterface(s->trbase, "main", 1, NULL);
    return s;
fail:
    if (s->rb) IExec->FreeSysObject(ASOT_IOREQUEST, s->rb);
    if (s->port) IExec->FreeSysObject(ASOT_PORT, s->port);
    IExec->FreeVec(s);
    return NULL;
}

static void close_speaker(speaker *s)
{
    flush(s, 1);
    if (s->itr) IExec->DropInterface((struct Interface *)s->itr);
    if (s->trbase) IExec->CloseLibrary(s->trbase);
    IExec->CloseDevice((struct IORequest *)s->rb);
    IExec->FreeSysObject(ASOT_IOREQUEST, s->rb);
    IExec->FreeSysObject(ASOT_PORT, s->port);
    IExec->FreeVec(s);
}

/* BSTR -> C string */
static void bstr(BSTR b, char *out, int max)
{
    const uint8 *p = (const uint8 *)BADDR(b);
    int n = p ? p[0] : 0, i;
    if (n > max - 1) n = max - 1;
    for (i = 0; i < n; i++) out[i] = (char)p[1 + i];
    out[n] = 0;
}

int32 _start(STRPTR args, int32 len, struct ExecBase *sysbase)
{
    struct Process *me;
    struct DosPacket *pkt;
    struct DeviceNode *dn;
    int opens = 0, running = 1;
    (void)args; (void)len;

    IExec = (struct ExecIFace *)sysbase->MainInterface;
    ml_IExec = IExec;
    DOSBase = IExec->OpenLibrary("dos.library", 50);
    if (!DOSBase) return RETURN_FAIL;
    IDOS = (struct DOSIFace *)IExec->GetInterface(DOSBase, "main", 1, NULL);
    me = (struct Process *)IExec->FindTask(NULL);

    /* startup packet */
    pkt = IDOS->WaitPkt(&me->pr_MsgPort);
    dn = (struct DeviceNode *)BADDR(pkt->dp_Arg3);
    if (dn) dn->dn_Port = &me->pr_MsgPort;
    IDOS->ReplyPkt(pkt, DOSTRUE, 0);

    while (running) {
        pkt = IDOS->WaitPkt(&me->pr_MsgPort);
        switch (pkt->dp_Type) {
        case ACTION_FINDOUTPUT:
        case ACTION_FINDUPDATE:
        case ACTION_FINDINPUT: {
            struct FileHandle *fh = (struct FileHandle *)BADDR(pkt->dp_Arg1);
            char name[256];
            speaker *s;
            if (pkt->dp_Type == ACTION_FINDINPUT) {
                IDOS->ReplyPkt(pkt, DOSFALSE, ERROR_READ_PROTECTED);
                break;
            }
            bstr((BSTR)pkt->dp_Arg3, name, sizeof name);
            s = open_speaker(name);
            if (!s) {
                IDOS->ReplyPkt(pkt, DOSFALSE, ERROR_OBJECT_IN_USE);
                break;
            }
            fh->fh_Arg1 = (int32)s;
            opens++;
            IDOS->ReplyPkt(pkt, DOSTRUE, 0);
            break;
        }
        case ACTION_WRITE: {
            speaker *s = (speaker *)pkt->dp_Arg1;
            const char *b = (const char *)pkt->dp_Arg2;
            int32 n = pkt->dp_Arg3, i;
            for (i = 0; i < n; i++) {
                if (s->len >= BUFMAX) flush(s, 1);
                s->buf[s->len++] = b[i];
            }
            /* the data is copied: let the writer go on while we speak */
            IDOS->ReplyPkt(pkt, n, 0);
            flush(s, 0);
            break;
        }
        case ACTION_END: {
            speaker *s = (speaker *)pkt->dp_Arg1;
            if (s) close_speaker(s);
            opens--;
            IDOS->ReplyPkt(pkt, DOSTRUE, 0);
            break;
        }
        case ACTION_IS_FILESYSTEM:
            IDOS->ReplyPkt(pkt, DOSFALSE, 0);
            break;
        case ACTION_SHUTDOWN:
        case 5: /* ACTION_DIE (obsolete) */
            if (opens > 0) {
                IDOS->ReplyPkt(pkt, DOSFALSE, ERROR_OBJECT_IN_USE);
            } else {
                IDOS->ReplyPkt(pkt, DOSTRUE, 0);
                running = 0;
            }
            break;
        default:
            IDOS->ReplyPkt(pkt, DOSFALSE, ERROR_ACTION_NOT_KNOWN);
            break;
        }
    }
    if (dn) dn->dn_Port = NULL;
    IExec->DropInterface((struct Interface *)IDOS);
    IExec->CloseLibrary(DOSBase);
    return RETURN_OK;
}
