// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>
//
// Host version of the AmigaOS Say command, writing a WAV file.

#include "common.h"
#include "wavout.h"
#include "amiga_speech/narrator.h"
#include <time.h>

static void usage(void)
{
    fprintf(stderr,
        "usage: say [options] \"text\"\n"
        "  -v classic|natural   voice (default natural)\n"
        "  -r RATE              words per minute 40-400 (150)\n"
        "  -p PITCH             baseline pitch Hz 65-320 (110)\n"
        "  -m natural|robotic|manual   F0 mode\n"
        "  -s male|female       vocal tract (classic voice)\n"
        "  -f SAMPFREQ          narrator sampfreq 5000-28000 (22200 = normal)\n"
        "  -V VOLUME            0-64 (64)\n"
        "  -e ENTHUSIASM        F0 enthusiasm, 32nds (32)\n"
        "  -P PERTURB           F0 perturbation 0-255 (0)\n"
        "  -a ARTICULATE        transition time %% (100)\n"
        "  -c PCT:PHON          centralize vowels towards PHON, e.g. 50:AO\n"
        "  -t classic           translate with the rule-only (NRL) translator\n"
        "  -x                   input is already narrator phonemes\n"
        "  -i FILE              read text from FILE\n"
        "  -o OUT.wav           output file (default say.wav)\n"
        "  -q                   do not print the phoneme string\n"
        "  -T                   print timing (real-time factor)\n");
}

int main(int argc, char **argv)
{
    nar_params p;
    const char *outp = "say.wav", *infile = NULL;
    int ai = 1, phon = 0, quiet = 0, timing = 0, tmode = TR_MODE_DICT, r;
    char *text, *ph, cent[4] = { 0 };
    pcmbuf b = { 0 };
    size_t err = 0;
    clock_t c0, c1;

    nar_params_default(&p);
    while (ai < argc && argv[ai][0] == '-' && argv[ai][1]) {
        const char *o = argv[ai];
        const char *v = (ai + 1 < argc) ? argv[ai + 1] : NULL;
        int takes = 1;
        if (!strcmp(o, "-x")) { phon = 1; takes = 0; }
        else if (!strcmp(o, "-q")) { quiet = 1; takes = 0; }
        else if (!strcmp(o, "-T")) { timing = 1; takes = 0; }
        else if (!strcmp(o, "-h")) { usage(); return 0; }
        else if (!v) { usage(); return 2; }
        else if (!strcmp(o, "-v")) p.voice = !strcmp(v, "classic") ? NAR_VOICE_CLASSIC : NAR_VOICE_NATURAL;
        else if (!strcmp(o, "-r")) p.rate = (uint16_t)atoi(v);
        else if (!strcmp(o, "-p")) p.pitch = (uint16_t)atoi(v);
        else if (!strcmp(o, "-m")) p.mode = !strcmp(v, "robotic") ? ROBOTICF0 : !strcmp(v, "manual") ? MANUALF0 : NATURALF0;
        else if (!strcmp(o, "-s")) p.sex = !strcmp(v, "female") ? FEMALE : MALE;
        else if (!strcmp(o, "-f")) p.sampfreq = (uint16_t)atoi(v);
        else if (!strcmp(o, "-V")) p.volume = (uint16_t)atoi(v);
        else if (!strcmp(o, "-e")) p.F0enthusiasm = (uint8_t)atoi(v);
        else if (!strcmp(o, "-P")) p.F0perturb = (uint8_t)atoi(v);
        else if (!strcmp(o, "-a")) p.articulate = (uint8_t)atoi(v);
        else if (!strcmp(o, "-c")) {
            const char *colon = strchr(v, ':');
            p.centralize = (uint8_t)atoi(v);
            if (colon) { strncpy(cent, colon + 1, 3); p.centphon = cent; }
        }
        else if (!strcmp(o, "-t")) tmode = !strcmp(v, "classic") ? TR_MODE_CLASSIC : TR_MODE_DICT;
        else if (!strcmp(o, "-i")) infile = v;
        else if (!strcmp(o, "-o")) outp = v;
        else { usage(); return 2; }
        ai += 1 + takes;
    }
    if (infile) {
        text = read_file(infile);
        if (!text) { perror(infile); return 1; }
    } else {
        if (ai >= argc) { usage(); return 2; }
        text = join_args(argc, argv, ai);
    }
    if (phon) {
        ph = strdup(text);
    } else {
        if (tmode == TR_MODE_DICT && load_dictionary(argv[0]) != 0)
            fprintf(stderr, "say: no dictionary found, letter-to-sound only\n");
        ph = translate_all(text, tmode);
    }
    if (!quiet) printf("%s\n", ph);

    if (p.voice == NAR_VOICE_NATURAL) {
        long vsz = 0;
        unsigned char *vimg = load_data_file(argv[0], "kal16.voice", &vsz);
        if (!vimg || nar_set_voice_image(vimg, (size_t)vsz) != 0)
            fprintf(stderr, "say: no natural voice data (kal16.voice), using the classic voice\n");
    }
    c0 = clock();
    r = nar_speak(&p, ph, strlen(ph), pcm_sink, &b, &err);
    c1 = clock();
    if (r) {
        fprintf(stderr, "say: narrator error %d", r);
        if (r == ND_PhonErr) fprintf(stderr, " at offset %lu: \"%.12s\"", (unsigned long)err, ph + err);
        fprintf(stderr, "\n");
        return 1;
    }
    if (wav_write(outp, b.s, b.n, NAR_OUTPUT_RATE) != 0) { perror(outp); return 1; }
    if (timing) {
        double cpu = (double)(c1 - c0) / CLOCKS_PER_SEC;
        double aud = (double)b.n / NAR_OUTPUT_RATE;
        fprintf(stderr, "audio %.2fs  cpu %.3fs  RTF %.4f\n", aud, cpu, aud > 0 ? cpu / aud : 0);
    }
    free(ph);
    free(text);
    free(b.s);
    return 0;
}
