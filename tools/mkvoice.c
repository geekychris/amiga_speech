// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>
//
// mkvoice: write Flite's compiled-in cmu_us_kal16 diphone voice as an
// "AMSV" data file (see src/voicefile.h).   mkvoice out.voice

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "cst_sts.h"
#include "cst_diphone.h"

extern const cst_diphone_db cmu_us_kal16_db;

static void be32(uint8_t *p, uint32_t v) { p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v; }
static void be16(uint8_t *p, uint32_t v) { p[0] = v >> 8; p[1] = v; }
static size_t al4(size_t x) { return (x + 3) & ~(size_t)3; }

int main(int argc, char **argv)
{
    const cst_diphone_db *db = &cmu_us_kal16_db;
    const cst_sts_list *s = db->sts;
    uint32_t nd = (uint32_t)db->num_entries, nsts = (uint32_t)s->num_sts, nch = (uint32_t)s->num_channels;
    size_t names = 0, oi, on, of, oro, ores, rbytes, total, i, p;
    uint8_t *img;
    uint32_t u;
    FILE *f;

    if (argc != 2) { fprintf(stderr, "usage: mkvoice out.voice\n"); return 2; }
    if (!s->frames || !s->resoffs || s->sts || s->sts_paged) { fprintf(stderr, "unsupported layout\n"); return 1; }
    for (i = 0; i < nd; i++) names += strlen(db->diphones[i].name) + 1;
    rbytes = s->resoffs[nsts];
    oi = 64;
    on = oi + nd * 8;
    of = al4(on + names);
    oro = of + (size_t)nsts * nch * 2;
    ores = oro + ((size_t)nsts + 1) * 4;
    total = al4(ores + rbytes);
    img = calloc(1, total);
    memcpy(img, "AMSV", 4); be16(img + 4, 1); be16(img + 6, 64);
    be32(img + 8, (uint32_t)s->sample_rate); be32(img + 12, nch); be32(img + 16, nsts);
    memcpy(&u, &s->coeff_min, 4); be32(img + 20, u);
    memcpy(&u, &s->coeff_range, 4); be32(img + 24, u);
    be32(img + 28, nd); be32(img + 32, (uint32_t)oi); be32(img + 36, (uint32_t)on);
    be32(img + 40, (uint32_t)of); be32(img + 44, (uint32_t)oro); be32(img + 48, (uint32_t)ores);
    be32(img + 52, (uint32_t)rbytes); be32(img + 56, (uint32_t)total);
    memcpy(img + 60, "kal", 3);
    for (i = 0, p = 0; i < nd; i++) {
        const cst_diphone_entry *e = &db->diphones[i];
        size_t l = strlen(e->name) + 1;
        if (i && strcmp(db->diphones[i - 1].name, e->name) >= 0) { fprintf(stderr, "index not sorted\n"); return 1; }
        be32(img + oi + i * 8, (uint32_t)p);
        be16(img + oi + i * 8 + 4, e->start_pm);
        img[oi + i * 8 + 6] = e->pb_pm;
        img[oi + i * 8 + 7] = e->end_pm;
        memcpy(img + on + p, e->name, l);
        p += l;
    }
    for (i = 0; i < (size_t)nsts * nch; i++) be16(img + of + i * 2, s->frames[i]);
    for (i = 0; i <= nsts; i++) be32(img + oro + i * 4, s->resoffs[i]);
    memcpy(img + ores, s->residuals, rbytes);
    f = fopen(argv[1], "wb");
    if (!f || fwrite(img, 1, total, f) != total || fclose(f)) { perror(argv[1]); return 1; }
    fprintf(stderr, "mkvoice: %u diphones, %u frames x %u, %lu residual bytes -> %lu bytes\n",
            nd, nsts, nch, (unsigned long)rbytes, (unsigned long)total);
    return 0;
}
