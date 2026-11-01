// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>
//
// translator.library for AmigaOS 4 (native PPC): Translate() as in the
// classic library (same arguments, same overflow/resume return value), with
// the amiga_speech translator core (CMU dictionary + letter-to-sound).
// 68k programs call LVO -30 (A0 inString, D0 inLength, A1 outBuffer,
// D1 outLength) through an EmuTrap stub.
//
// The dictionary is loaded on the first Translate() call from
// DEVS:narrator/english.dict (directory overridable with
// ENV:Narrator/DataDir); without it every word goes through letter-to-sound.
// Translate() runs on its own 64 KB stack (NewStackRun) because 68k callers
// often have only a few KB, and is serialised with a semaphore.

#include <exec/exec.h>
#include <exec/emulation.h>
#include <exec/resident.h>
#include <dos/dos.h>
#include <interfaces/exec.h>
#include <interfaces/dos.h>
#include <interfaces/translator.h>

#include "amiga_speech/translator.h"

#define LIBNAME     "translator.library"
#define LIBVERSION  50
#define LIBREVISION 1
#define VSTRING     "translator.library 50.1 (01.11.2026) amiga_speech\r\n"

struct TranslatorBase {
    struct Library lib;
    BPTR seglist;
};

struct ExecIFace *IExec;
struct DOSIFace *IDOS;
static struct Library *DOSBase;
extern struct ExecIFace *ml_IExec;

static struct SignalSemaphore g_sem;
static uint8 *g_dict;
static int g_dict_tried;

static int load_dictionary_from(const char *dir)
{
    char path[260];
    BPTR fh;
    int i, ok = 0;
    int64 sz;
    for (i = 0; dir[i] && i < 200; i++) path[i] = dir[i];
    path[i] = 0;
    if (!IDOS->AddPart(path, "english.dict", sizeof path)) return 0;
    fh = IDOS->Open(path, MODE_OLDFILE);
    if (!fh) return 0;
    sz = IDOS->GetFileSize(fh);
    if (sz > 16 && sz < 16 * 1024 * 1024) {
        g_dict = IExec->AllocVecTags((uint32)sz, AVT_Type, MEMF_SHARED, TAG_END);
        if (g_dict && IDOS->Read(fh, g_dict, (int32)sz) == (int32)sz &&
            tr_set_dictionary(g_dict, (size_t)sz) == 0) {
            ok = 1;
        } else if (g_dict) {
            IExec->FreeVec(g_dict);
            g_dict = NULL;
        }
    }
    IDOS->Close(fh);
    return ok;
}

/* english.dict: ENV:Narrator/DataDir, then SPEECH:, DEVS:narrator, DEVS:, LIBS: */
static void load_dictionary(void)
{
    static const char *const dirs[] = { "SPEECH:", "DEVS:narrator", "DEVS:", "LIBS:", NULL };
    char dir[200];
    struct Process *me = (struct Process *)IExec->FindTask(NULL);
    APTR oldwin;
    int i;
    if (g_dict_tried) return;
    if (me->pr_Task.tc_Node.ln_Type != NT_PROCESS) return;     /* DOS needs a process */
    g_dict_tried = 1;
    oldwin = me->pr_WindowPtr;
    me->pr_WindowPtr = (APTR)-1;
    if (!(IDOS->GetVar("Narrator/DataDir", dir, sizeof dir, 0) > 0 && load_dictionary_from(dir)))
        for (i = 0; dirs[i]; i++)
            if (load_dictionary_from(dirs[i])) break;
    me->pr_WindowPtr = oldwin;
}

/* runs on the NewStackRun stack */
static int32 tramp(CONST_STRPTR in, LONG inlen, STRPTR out, LONG outlen, LONG *result)
{
    *result = Translate((const char *)in, inlen, (char *)out, outlen);
    return 0;
}

static LONG lib_Translate(struct TranslatorIFace *Self, CONST_STRPTR in, LONG inlen,
                          STRPTR out, LONG outlen)
{
    LONG result = 0;
    (void)Self;
    IExec->ObtainSemaphore(&g_sem);
    load_dictionary();
    if (IExec->NewStackRunTags((APTR)tramp, NSR_StackSize, 65536,
                               NSR_Arg1, in, NSR_Arg2, inlen, NSR_Arg3, out,
                               NSR_Arg4, outlen, NSR_Arg5, &result, TAG_END) == -1)
        tramp(in, inlen, out, outlen, &result);         /* no memory for a stack */
    IExec->ReleaseSemaphore(&g_sem);
    return result;
}

/* ---- manager interface ---- */

static uint32 mgr_Obtain(struct LibraryManagerInterface *Self) { return ++Self->Data.RefCount; }
static uint32 mgr_Release(struct LibraryManagerInterface *Self) { return --Self->Data.RefCount; }

static struct Library *mgr_Open(struct LibraryManagerInterface *Self, uint32 version)
{
    struct TranslatorBase *b = (struct TranslatorBase *)Self->Data.LibBase;
    (void)version;
    b->lib.lib_OpenCnt++;
    b->lib.lib_Flags &= ~LIBF_DELEXP;
    return &b->lib;
}

static BPTR mgr_Expunge(struct LibraryManagerInterface *Self)
{
    struct TranslatorBase *b = (struct TranslatorBase *)Self->Data.LibBase;
    BPTR seg;
    if (b->lib.lib_OpenCnt) {
        b->lib.lib_Flags |= LIBF_DELEXP;
        return ZERO;
    }
    seg = b->seglist;
    IExec->Remove(&b->lib.lib_Node);
    tr_set_dictionary(NULL, 0);
    if (g_dict) IExec->FreeVec(g_dict);
    g_dict = NULL;
    if (IDOS) IExec->DropInterface((struct Interface *)IDOS);
    if (DOSBase) IExec->CloseLibrary(DOSBase);
    IDOS = NULL;
    DOSBase = NULL;
    IExec->DeleteLibrary(&b->lib);
    return seg;
}

static BPTR mgr_Close(struct LibraryManagerInterface *Self)
{
    struct TranslatorBase *b = (struct TranslatorBase *)Self->Data.LibBase;
    if (--b->lib.lib_OpenCnt == 0 && (b->lib.lib_Flags & LIBF_DELEXP))
        return mgr_Expunge(Self);
    return ZERO;
}

static uint32 main_Obtain(struct TranslatorIFace *Self) { return ++Self->Data.RefCount; }
static uint32 main_Release(struct TranslatorIFace *Self) { return --Self->Data.RefCount; }

/* ---- 68k jump table ---- */

static struct Interface *iface_of(uint32 *r, int main)
{
    struct Library *b = (struct Library *)r[REG68K_A6 / 4];
    struct ExtendedLibrary *x = (struct ExtendedLibrary *)((uint32)b + b->lib_PosSize);
    return main ? x->MainIFace : (struct Interface *)x->ILibrary;
}

static uint32 s68_Open(uint32 *r)
{
    return (uint32)mgr_Open((struct LibraryManagerInterface *)iface_of(r, 0), r[REG68K_D0 / 4]);
}
static uint32 s68_Close(uint32 *r) { return (uint32)mgr_Close((struct LibraryManagerInterface *)iface_of(r, 0)); }
static uint32 s68_Expunge(uint32 *r) { return (uint32)mgr_Expunge((struct LibraryManagerInterface *)iface_of(r, 0)); }
static uint32 s68_Reserved(uint32 *r) { (void)r; return 0; }
static uint32 s68_Translate(uint32 *r)
{
    return (uint32)lib_Translate((struct TranslatorIFace *)iface_of(r, 1),
                                 (CONST_STRPTR)r[REG68K_A0 / 4], (LONG)r[REG68K_D0 / 4],
                                 (STRPTR)r[REG68K_A1 / 4], (LONG)r[REG68K_D1 / 4]);
}

static const struct EmuTrap t68_Open      = { TRAPINST, TRAPTYPE, s68_Open };
static const struct EmuTrap t68_Close     = { TRAPINST, TRAPTYPE, s68_Close };
static const struct EmuTrap t68_Expunge   = { TRAPINST, TRAPTYPE, s68_Expunge };
static const struct EmuTrap t68_Reserved  = { TRAPINST, TRAPTYPE, s68_Reserved };
static const struct EmuTrap t68_Translate = { TRAPINST, TRAPTYPE, s68_Translate };

static const APTR vectors68k[] = {
    (APTR)&t68_Open, (APTR)&t68_Close, (APTR)&t68_Expunge, (APTR)&t68_Reserved,
    (APTR)&t68_Translate, (APTR)-1
};

/* ---- interfaces, init, resident ---- */

static const APTR mgr_vectors[] = {
    (APTR)mgr_Obtain, (APTR)mgr_Release, NULL, NULL,
    (APTR)mgr_Open, (APTR)mgr_Close, (APTR)mgr_Expunge, NULL, (APTR)-1
};
static const struct TagItem mgr_tags[] = {
    { MIT_Name, (uint32)"__library" },
    { MIT_VectorTable, (uint32)mgr_vectors },
    { MIT_Version, 1 },
    { TAG_END, 0 }
};

static const APTR main_vectors[] = {
    (APTR)main_Obtain, (APTR)main_Release, NULL, NULL, (APTR)lib_Translate, (APTR)-1
};
static const struct TagItem main_tags[] = {
    { MIT_Name, (uint32)"main" },
    { MIT_VectorTable, (uint32)main_vectors },
    { MIT_Version, 1 },
    { TAG_END, 0 }
};

static const APTR interfaces[] = { (APTR)mgr_tags, (APTR)main_tags, NULL };


/* Nothing zeroes .bss for a -nostartfiles binary we load ourselves (no C
   start-up code), and statics such as Flite's "rules initialised" flags
   must start at zero: clear it before anything else runs. */
extern char __bss_start[], _end[];
static void clear_bss(void)
{
    char *p = __bss_start;
    while (p < _end) *p++ = 0;
}

static struct Library *lib_Init(struct Library *lib, BPTR seglist, struct ExecIFace *iexec)
{
    struct TranslatorBase *b = (struct TranslatorBase *)lib;
    clear_bss();
    IExec = iexec;
    ml_IExec = iexec;
    b->lib.lib_Node.ln_Type = NT_LIBRARY;
    b->lib.lib_Node.ln_Pri = 0;
    b->lib.lib_Node.ln_Name = (STRPTR)LIBNAME;
    b->lib.lib_Flags = LIBF_SUMUSED | LIBF_CHANGED;
    b->lib.lib_Version = LIBVERSION;
    b->lib.lib_Revision = LIBREVISION;
    b->lib.lib_IdString = (STRPTR)VSTRING;
    b->seglist = seglist;
    IExec->InitSemaphore(&g_sem);
    DOSBase = IExec->OpenLibrary("dos.library", 50);
    if (!DOSBase) return NULL;
    IDOS = (struct DOSIFace *)IExec->GetInterface(DOSBase, "main", 1, NULL);
    if (!IDOS) { IExec->CloseLibrary(DOSBase); return NULL; }
    return lib;
}

static const struct TagItem init_tags[] = {
    { CLT_DataSize, sizeof(struct TranslatorBase) },
    { CLT_InitFunc, (uint32)lib_Init },
    { CLT_Interfaces, (uint32)interfaces },
    { CLT_Vector68K, (uint32)vectors68k },
    { TAG_END, 0 }
};

const char verstag_translator[] __attribute__((used)) = "\0$VER: " VSTRING;

struct Resident tr_romtag __attribute__((used)) = {
    RTC_MATCHWORD, &tr_romtag, &tr_romtag + 1,
    RTF_NATIVE | RTF_AUTOINIT, LIBVERSION, NT_LIBRARY, 0,
    (STRPTR)LIBNAME, (STRPTR)VSTRING, (APTR)init_tags
};

int32 _start(void) { return RETURN_FAIL; }
