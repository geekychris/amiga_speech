// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>
//
// translator.library only uses Flite's LTS; Flite's value type table still
// names the destructors of synthesis types it never creates there.
void val_delete_utterance(void *v) { (void)v; }
void val_delete_wave(void *v) { (void)v; }
void val_delete_track(void *v) { (void)v; }
void val_delete_lpcres(void *v) { (void)v; }
