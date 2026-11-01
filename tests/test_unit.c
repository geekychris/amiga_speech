// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>
//
// Unit tests: normalisation, dictionary, LTS, phoneme mapping, Translate()
// overflow protocol, narrator parameter checking, phoneme parsing, and the
// audible effect of the parameters on both voices.

#include "amiga_speech/narrator.h"
#include "amiga_speech/translator.h"
#include "../src/normalize.h"
#if defined(__GNUC__)
#pragma GCC diagnostic ignored "-Wunused-function"
#endif
#include "../cli/common.h"
#include "../cli/wavout.h"
#include <math.h>

static int fails, checks;

#define CHECK(cond, ...) do { checks++; if (!(cond)) { fails++; \
    printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static void expect_str(const char *what, const char *got, const char *want)
{
    checks++;
    if (strcmp(got, want) != 0) {
        fails++;
        printf("FAIL %s\n   got:  \"%s\"\n   want: \"%s\"\n", what, got, want);
    }
}

static void norm(const char *in, const char *want)
{
    char out[1024];
    tr_normalize_debug(in, (long)strlen(in), out, sizeof out);
    expect_str(in, out, want);
}

static void tr(const char *in, const char *want)
{
    char out[1024];
    long r = Translate(in, (long)strlen(in), out, sizeof out);
    CHECK(r == 0, "Translate(%s) = %ld", in, r);
    expect_str(in, out, want);
}

static void word(const char *w, int mode, const char *want, char src_want)
{
    char out[256], src;
    tr_word_arpabet(w, mode, out, sizeof out, &src);
    expect_str(w, out, want);
    if (src_want) CHECK(src == src_want, "%s source %c, want %c", w, src, src_want);
}

static void test_numbers(void)
{
    char b[256];
    nt_cardinal(0, b, sizeof b); expect_str("0", b, "zero");
    nt_cardinal(13, b, sizeof b); expect_str("13", b, "thirteen");
    nt_cardinal(1000000, b, sizeof b); expect_str("1e6", b, "one million");
    nt_cardinal(1234567, b, sizeof b);
    expect_str("1234567", b, "one million two hundred thirty four thousand five hundred sixty seven");
    nt_ordinal(1, b, sizeof b); expect_str("1st", b, "first");
    nt_ordinal(12, b, sizeof b); expect_str("12th", b, "twelfth");
    nt_ordinal(20, b, sizeof b); expect_str("20th", b, "twentieth");
    nt_ordinal(103, b, sizeof b); expect_str("103rd", b, "one hundred third");
    nt_year(1985, b, sizeof b); expect_str("y1985", b, "nineteen eighty five");
    nt_year(1900, b, sizeof b); expect_str("y1900", b, "nineteen hundred");
    nt_year(1905, b, sizeof b); expect_str("y1905", b, "nineteen oh five");
    nt_year(2005, b, sizeof b); expect_str("y2005", b, "two thousand five");
    nt_year(2026, b, sizeof b); expect_str("y2026", b, "twenty twenty six");
}

static void test_normalize(void)
{
    norm("Hello, world!", "hello , world .");
    norm("In 1985 it cost $1,295.", "in nineteen eighty five it cost one thousand two hundred ninety five dollars .");
    norm("$3.50", "three dollars and fifty cents");
    norm("$0.99", "ninety nine cents");
    norm("50% off", "fifty percent off");
    norm("3.14", "three point one four");
    norm("Meet at 3:30 or 4:05 or 5:00.", "meet at three thirty or four oh five or five o'clock .");
    norm("the 21st and 22nd", "the twenty first and twenty second");
    norm("Dr. Smith vs. Mr. Jones", "doctor smith versus mister jones");
    norm("the CPU and NASA", "the #c #p #u and nasa");
    norm("AmigaOS 4.1", "amiga #o #s four point one");
    norm("an A500 with 512KB", "an #a five hundred with five hundred twelve kilobytes");
    norm("the 1980s", "the nineteen eighties");
    norm("call 5551234567", "call five five five one two three four five six seven");
    norm("rock & roll", "rock and roll");
    norm("well -- maybe", "well - maybe");
    norm("Is it?", "is it ?");
    norm("-5 degrees", "minus five degrees");
}

static void test_lexicon(void)
{
    char out[256], src;
    word("hello", TR_MODE_DICT, "HH AH0 L OW1", 'D');
    word("amiga", TR_MODE_DICT, "AH0 M IY1 G AH0", 'D');
    word("colonel", TR_MODE_DICT, "K ER1 N AH0 L", 'D');
    /* not in cmudict: LTS fallback */
    tr_word_arpabet("blorptastic", TR_MODE_DICT, out, sizeof out, &src);
    CHECK(src == 'L' && strlen(out) > 10, "blorptastic -> %c %s", src, out);
    CHECK(strchr(out, '1') != NULL, "LTS gives a primary stress: %s", out);
    /* possessive of a known word */
    word("amiga's", TR_MODE_DICT, "AH0 M IY1 G AH0 Z", 'D');
    word("cat's", TR_MODE_DICT, "K AE1 T S", 0);
    word("box's", TR_MODE_DICT, "B AA1 K S IH0 Z", 0);
    /* a word removed from the compact dictionary because LTS predicts it
       must still come out exactly as cmudict has it */
    word("sat", TR_MODE_DICT, "S AE1 T", 0);
    /* classic NRL rules */
    word("hello", TR_MODE_CLASSIC, "HH EH1 L OW0", 'N');
    word("speech", TR_MODE_CLASSIC, "S P IY1 CH", 'N');
}

static void test_translate(void)
{
    tr("Hello, I am your Amiga.", "/HAXLOW5, AY AEM YOHR AXMIY5GAX.");
    tr("Do you enjoy using your Amiga?", "DUW YUW EH1NJOY4 YUW4ZIXNX YOHR AXMIY5GAX?");
    tr("the apple.", "DHIY AE5PUL.");
    tr("personal", "PER5SIXNUL");
    tr("border", "BOH5RDER");
    tr("judge", "JAH5J");
    tr("sing", "SIH5NX");
}

static void test_translate_overflow(void)
{
    const char *in = "The quick brown fox jumps over the lazy dog, again and again.";
    char small[24], full[512], joined[512] = "";
    long r, off = 0, len = (long)strlen(in);
    int rounds = 0;
    Translate(in, len, full, sizeof full);
    while (off < len && rounds < 50) {
        r = Translate(in + off, len - off, small, sizeof small);
        CHECK((long)strlen(small) < (long)sizeof small, "NUL-terminated within buffer");
        if (joined[0] && small[0]) strcat(joined, " ");
        strcat(joined, small);
        rounds++;
        if (r == 0) break;
        CHECK(r < 0, "overflow returns negative offset (%ld)", r);
        CHECK(in[off - r - 1] == ' ', "resume point is a word boundary");
        off += -r;
    }
    CHECK(rounds > 1, "small buffer needed several calls (%d)", rounds);
    /* the nuclear-accent bump can differ at chunk ends; compare without digits */
    {
        char a[512], b[512];
        int i, j;
        for (i = j = 0; joined[i]; i++) if (joined[i] < '0' || joined[i] > '9') a[j++] = joined[i];
        a[j] = 0;
        for (i = j = 0; full[i]; i++) if (full[i] < '0' || full[i] > '9') b[j++] = full[i];
        b[j] = 0;
        expect_str("chunked translation", a, b);
    }
}

static void test_params(void)
{
    nar_params p;
    nar_params_default(&p);
    CHECK(nar_params_check(&p) == 0, "defaults valid");
    CHECK(p.rate == 150 && p.pitch == 110 && p.volume == 64 && p.sampfreq == 22200, "default values");
    p.rate = 39; CHECK(nar_params_check(&p) == ND_RateErr, "rate low");
    p.rate = 401; CHECK(nar_params_check(&p) == ND_RateErr, "rate high");
    nar_params_default(&p); p.pitch = 64; CHECK(nar_params_check(&p) == ND_PitchErr, "pitch");
    nar_params_default(&p); p.sex = 2; CHECK(nar_params_check(&p) == ND_SexErr, "sex");
    nar_params_default(&p); p.mode = 3; CHECK(nar_params_check(&p) == ND_ModeErr, "mode");
    nar_params_default(&p); p.sampfreq = 4999; CHECK(nar_params_check(&p) == ND_FreqErr, "freq");
    nar_params_default(&p); p.volume = 65; CHECK(nar_params_check(&p) == ND_VolErr, "volume");
    nar_params_default(&p); p.centralize = 101; CHECK(nar_params_check(&p) == ND_DCentErr, "centralize");
    nar_params_default(&p); p.centralize = 50; p.centphon = "XX";
    CHECK(nar_params_check(&p) == ND_CentPhonErr, "centphon");
    p.centphon = "AO"; CHECK(nar_params_check(&p) == 0, "centphon AO");
    /* old-style IORB: V37 fields ignored, so a bad centphon is not an error */
    p.flags = 0; p.centphon = "XX"; CHECK(nar_params_check(&p) == 0, "old IORB ignores V37 fields");
}

static void test_phonemes(void)
{
    size_t e = 0;
    CHECK(nar_check_phonemes("/HEH4LOW.", 9, &e) == 0, "hello ok");
    CHECK(nar_check_phonemes("PERSINUL KAAMBINEYSHIN.", 23, &e) == 0, "contractions");
    CHECK(nar_check_phonemes("(DHAX FOH5RM) - AXV QX DX LX RX Q /C WH.", 39, &e) == 0, "special symbols");
    CHECK(nar_check_phonemes("K5AET.", 6, &e) == ND_PhonErr && e == 1, "digit after consonant (pos %lu)", (unsigned long)e);
    CHECK(nar_check_phonemes("/HEHLOW XQZ.", 12, &e) == ND_PhonErr && e == 8, "unknown code (pos %lu)", (unsigned long)e);
    CHECK(nar_check_phonemes("AA0.", 4, &e) == ND_PhonErr, "stress 0 is invalid");
    CHECK(nar_check_phonemes("HEHLOW.", 7, &e) == ND_PhonErr && e == 0, "bare H is not a phoneme (/H is)");
    CHECK(nar_check_phonemes("AA4#garbage", 11, &e) == 0, "'#' terminates");
    CHECK(nar_check_phonemes("AA4 garbage", 3, &e) == 0, "length limits");
    CHECK(nar_check_phonemes("/hehlow.", 8, &e) == 0, "lower case accepted");
}

/* ---- audio measurements ---- */

static pcmbuf render(const nar_params *p, const char *ph)
{
    pcmbuf b = { 0 };
    size_t e;
    int r = nar_speak(p, ph, strlen(ph), pcm_sink, &b, &e);
    CHECK(r == 0, "nar_speak(%s) = %d", ph, r);
    return b;
}

static double rms(const pcmbuf *b)
{
    double s = 0;
    size_t i;
    for (i = 0; i < b->n; i++) s += (double)b->s[i] * b->s[i];
    return b->n ? sqrt(s / (double)b->n) : 0;
}

/* F0 of the middle 100 ms (inside the sustained vowel) by autocorrelation */
static double f0_of(const pcmbuf *b)
{
    size_t win = NAR_OUTPUT_RATE / 10, best;
    double bestr = 0;
    int lag, bestlag = 0;
    if (b->n < win * 2) return 0;
    best = b->n / 2 - win / 2;
    for (lag = NAR_OUTPUT_RATE / 400; lag < NAR_OUTPUT_RATE / 50; lag++) {
        double r = 0;
        size_t j;
        for (j = 0; j + (size_t)lag < win; j++) r += (double)b->s[best + j] * b->s[best + j + lag];
        if (r > bestr) { bestr = r; bestlag = lag; }
    }
    return bestlag ? (double)NAR_OUTPUT_RATE / bestlag : 0;
}

static uint32_t hash(const pcmbuf *b)
{
    uint32_t h = 2166136261u;
    size_t i;
    for (i = 0; i < b->n; i++) {
        h = (h ^ (uint8_t)(b->s[i] & 255)) * 16777619u;
        h = (h ^ (uint8_t)((b->s[i] >> 8) & 255)) * 16777619u;
    }
    return h;
}

static void test_audio(int voice)
{
    nar_params p;
    const char *ph = "DHIHS IHZ AHMIY5GAH SPIY5KIHNX.";
    const char *vow = "/HAA4AA.";
    pcmbuf a, b;
    double f1, f2;
    const char *vn = voice ? "natural" : "classic";

    nar_params_default(&p);
    p.voice = (uint8_t)voice;

    a = render(&p, ph);
    b = render(&p, ph);
    CHECK(a.n > NAR_OUTPUT_RATE / 2, "%s: produces audio (%lu)", vn, (unsigned long)a.n);
    CHECK(a.n == b.n && hash(&a) == hash(&b), "%s: deterministic", vn);
    free(b.s);

    p.rate = 300; b = render(&p, ph);
    CHECK(b.n < a.n * 0.7, "%s: rate 300 shorter (%lu vs %lu)", vn, (unsigned long)b.n, (unsigned long)a.n);
    free(b.s);
    p.rate = 75; b = render(&p, ph);
    CHECK(b.n > a.n * 1.4, "%s: rate 75 longer", vn);
    free(b.s);
    p.rate = DEFRATE;

    p.volume = 0; b = render(&p, ph);
    CHECK(rms(&b) == 0.0, "%s: volume 0 silent", vn);
    free(b.s);
    p.volume = 32; b = render(&p, ph);
    {
        double r = rms(&b) / rms(&a);
        CHECK(r > 0.4 && r < 0.6, "%s: volume 32 about half (%.2f)", vn, r);
    }
    free(b.s);
    p.volume = DEFVOL;

    p.sampfreq = 11100; b = render(&p, ph);
    CHECK(b.n > a.n * 1.8 && b.n < a.n * 2.2, "%s: sampfreq 11100 plays at half speed", vn);
    free(b.s);
    p.sampfreq = DEFFREQ;
    free(a.s);

    p.mode = ROBOTICF0;
    p.pitch = 100; a = render(&p, vow); f1 = f0_of(&a); free(a.s);
    p.pitch = 200; a = render(&p, vow); f2 = f0_of(&a); free(a.s);
    CHECK(fabs(f1 - 100) < 8 && fabs(f2 - 200) < 15,
          "%s: robotic pitch follows setting (%.1f, %.1f)", vn, f1, f2);
}

int main(int argc, char **argv)
{
    if (load_dictionary(argv[0]) != 0) {
        printf("cannot load data/english.dict (run make)\n");
        return 1;
    }
    test_numbers();
    test_normalize();
    test_lexicon();
    test_translate();
    test_translate_overflow();
    test_params();
    test_phonemes();
    test_audio(NAR_VOICE_CLASSIC);
    test_audio(NAR_VOICE_NATURAL);
    printf("%d/%d checks passed\n", checks - fails, checks);
    (void)argc;
    return fails ? 1 : 0;
}
