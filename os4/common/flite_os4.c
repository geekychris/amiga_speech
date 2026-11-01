// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>
//
// Replacements for Flite's cst_error.c (stdio based) when Flite runs
// inside an AmigaOS 4 device or library: messages go to the debug log.

#include <exec/types.h>
#include <interfaces/exec.h>
#include <stdarg.h>
#include <stdio.h>

extern struct ExecIFace *ml_IExec;
int vsnprintf(char *buf, size_t size, const char *f, va_list ap);

int cst_errmsg(const char *fmt, ...)
{
    char b[200];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(b, sizeof b, fmt, ap);
    va_end(ap);
    if (ml_IExec) ml_IExec->DebugPrintF("[speech] %s", b);
    return 0;
}

/* Flite is compiled with -DDIE_ON_ERROR, so cst_error() is abort(). The
   speech core never reaches it with valid data; park the task rather than
   crash the machine. */
void abort(void)
{
    if (ml_IExec) {
        ml_IExec->DebugPrintF("[speech] Flite: fatal error, task halted\n");
        ml_IExec->Wait(0);
    }
    for (;;) ;
}
