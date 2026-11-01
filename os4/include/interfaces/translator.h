// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>
//
// translator.library main interface (AmigaOS 4, amiga_speech).

#ifndef INTERFACES_TRANSLATOR_H
#define INTERFACES_TRANSLATOR_H

#include <exec/types.h>
#include <exec/exec.h>
#include <exec/interfaces.h>
#include <libraries/translator.h>

struct TranslatorIFace
{
    struct InterfaceData Data;
    uint32 APICALL (*Obtain)(struct TranslatorIFace *Self);
    uint32 APICALL (*Release)(struct TranslatorIFace *Self);
    void APICALL (*Expunge)(struct TranslatorIFace *Self);
    struct Interface * APICALL (*Clone)(struct TranslatorIFace *Self);
    LONG APICALL (*Translate)(struct TranslatorIFace *Self, CONST_STRPTR inString,
                              LONG inLength, STRPTR outBuffer, LONG outLength);
};

#endif
