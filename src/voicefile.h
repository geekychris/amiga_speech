// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>
//
// Diphone voice data file ("AMSV"), loaded at run time instead of linking
// Flite's voice as C arrays. Big-endian on disk; used in place on
// big-endian machines (PPC) and byte-swapped once on little-endian hosts.
//
// Layout (all offsets from the start of the file, all aligned to 4):
//   0  "AMSV"
//   4  u16 version (1), u16 header bytes (64)
//   8  u32 sample_rate
//  12  u32 num_channels      (LPC order)
//  16  u32 num_sts           (frames)
//  20  u32 coeff_min         (IEEE-754 float bits)
//  24  u32 coeff_range       (IEEE-754 float bits)
//  28  u32 num_diphones
//  32  u32 off_index         num_diphones x { u32 name_off, u16 start_pm,
//                            u8 pb_pm, u8 end_pm } (8 bytes)
//  36  u32 off_names         NUL-terminated names, sorted (strcmp order)
//  40  u32 off_frames        u16 frames[num_sts * num_channels]
//  44  u32 off_resoffs       u32 resoffs[num_sts + 1]
//  48  u32 off_residuals     u8 residuals (µ-law)
//  52  u32 residual_bytes
//  56  u32 file_bytes
//  60  char name[4]          short voice id, e.g. "kal"

#ifndef SPEECH_VOICEFILE_H
#define SPEECH_VOICEFILE_H

#include <stddef.h>
#include <stdint.h>

#define AMSV_HEADER 64

struct cst_diphone_db_struct;

/* Prepare an image (swaps in place on little-endian hosts; the buffer must
   be writable and stay valid). Returns the diphone database or NULL if the
   image is malformed. Index entries are allocated once with malloc. */
const struct cst_diphone_db_struct *amsv_open(uint8_t *image, size_t size);
void amsv_close(const struct cst_diphone_db_struct *db);

#endif
