// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

#include "voicefile.h"
#include <stdlib.h>
#include <string.h>

#include "cst_sts.h"
#include "cst_diphone.h"

typedef struct {
    cst_diphone_db db;
    cst_sts_list sts;
    cst_diphone_entry *entries;
} amsv_voice;

static uint32_t rd32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static int host_is_le(void)
{
    const uint16_t one = 1;
    return *(const uint8_t *)&one == 1;
}

static float bits_to_float(uint32_t u)
{
    float f;
    memcpy(&f, &u, 4);
    return f;
}

const cst_diphone_db *amsv_open(uint8_t *img, size_t size)
{
    amsv_voice *v;
    uint32_t nd, nsts, nch, oi, on, of, oro, ores, rbytes, i;
    if (size < AMSV_HEADER || memcmp(img, "AMSV", 4) != 0) return NULL;
    if (img[4] != 0 || img[5] != 1) return NULL;
    nch = rd32(img + 12);
    nsts = rd32(img + 16);
    nd = rd32(img + 28);
    oi = rd32(img + 32); on = rd32(img + 36); of = rd32(img + 40);
    oro = rd32(img + 44); ores = rd32(img + 48); rbytes = rd32(img + 52);
    if (rd32(img + 56) != size) return NULL;
    if (oi + (size_t)nd * 8 > size || of + (size_t)nsts * nch * 2 > size ||
        oro + ((size_t)nsts + 1) * 4 > size || ores + (size_t)rbytes > size ||
        ((oi | of | oro) & 3) || on >= size || nch == 0 || nch > 64)
        return NULL;

    /* in-place byte swap of the u16 frames and u32 offsets on LE hosts */
    if (host_is_le() && img[63] != 1) {
        uint8_t *p;
        for (p = img + of, i = 0; i < nsts * nch; i++, p += 2) {
            uint8_t t = p[0]; p[0] = p[1]; p[1] = t;
        }
        for (p = img + oro, i = 0; i <= nsts; i++, p += 4) {
            uint8_t t = p[0]; p[0] = p[3]; p[3] = t;
            t = p[1]; p[1] = p[2]; p[2] = t;
        }
        img[63] = 1;   /* mark swapped (name[3] is reserved) */
    }

    v = (amsv_voice *)calloc(1, sizeof *v);
    if (!v) return NULL;
    v->entries = (cst_diphone_entry *)calloc(nd + 1, sizeof *v->entries);
    if (!v->entries) { free(v); return NULL; }
    for (i = 0; i < nd; i++) {
        const uint8_t *e = img + oi + i * 8;
        uint32_t no = rd32(e);
        if (on + no >= size) { free(v->entries); free(v); return NULL; }
        v->entries[i].name = (const char *)img + on + no;
        v->entries[i].start_pm = (unsigned short)((e[4] << 8) | e[5]);
        v->entries[i].pb_pm = e[6];
        v->entries[i].end_pm = e[7];
    }
    v->sts.frames = (const unsigned short *)(img + of);
    v->sts.resoffs = (const unsigned int *)(img + oro);
    v->sts.residuals = img + ores;
    v->sts.num_sts = (int)nsts;
    v->sts.num_channels = (int)nch;
    v->sts.sample_rate = (int)rd32(img + 8);
    v->sts.coeff_min = bits_to_float(rd32(img + 20));
    v->sts.coeff_range = bits_to_float(rd32(img + 24));
    v->sts.codec = NULL;   /* µ-law residuals */
    v->db.name = "amsv";
    v->db.num_entries = (int)nd;
    v->db.diphones = v->entries;
    v->db.sts = &v->sts;
    return &v->db;
}

void amsv_close(const cst_diphone_db *db)
{
    amsv_voice *v = (amsv_voice *)(void *)db;   /* db is the first member */
    if (!v) return;
    free(v->entries);
    free(v);
}
