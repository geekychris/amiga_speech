/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Chris Collins <chris@hitorro.com>
 *
 * amiga_speech: stand-ins for the parts of Flite this project does not vendor
 * (URL streams, audio streaming, voices, clustergen). Not part of Flite. */
#include "cst_file.h"
#include "cst_val.h"
#include "cst_audio.h"

int cst_urlp(const char *url) { (void)url; return 0; }
cst_file cst_url_open(const char *url) { (void)url; return NULL; }

cst_audio_streaming_info *val_audio_streaming_info(const cst_val *v) { (void)v; return NULL; }
void val_delete_audio_streaming_info(void *v) { (void)v; }
void val_delete_cg_db(void *v) { (void)v; }
void val_delete_voice(void *v) { (void)v; }
