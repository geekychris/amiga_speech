// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>
#ifndef PROTO_TRANSLATOR_H
#define PROTO_TRANSLATOR_H
#include <interfaces/translator.h>
extern struct Library *TranslatorBase;
extern struct TranslatorIFace *ITranslator;
#ifdef __USE_INLINE__
#define Translate(a, b, c, d) ITranslator->Translate((a), (b), (c), (d))
#endif
#endif
