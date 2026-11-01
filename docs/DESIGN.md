# amiga_speech design

A native AmigaOS 4.1 PPC speech system that is API-compatible with the
classic AmigaOS `translator.library`, `narrator.device` and `Say`, sounds
clearly better than the 1985/1991 68k narrator, runs locally, and uses
classic non-neural techniques.

* **Phase 1 (this repository today):** a host prototype in portable C99,
  built and tested on macOS, producing WAV files. It already
  cross-compiles unchanged for AmigaOS 4 PPC (newlib) and runs on the QEMU
  sam460ex guest (see *Measured on PPC* below).
* **Phase 2:** the OS4 port: `translator.library`, `narrator.device`, `Say`
  and a `SPEAK:` handler, with 68k compatibility.

## 1. Phase 1 pipeline

```
 English text
   │  src/normalize.c   numbers, ordinals, years, money, %, times,
   │                    abbreviations, initialisms, symbols, punctuation
   ▼
 words ──► src/lexicon.c   compact CMU dictionary (data/english.dict)
   │       src/lts.c       CMU Flite LTS trees for unknown words
   │       src/nrl.c       NRL 1976 rules ("classic" rule-only mode)
   ▼
 ARPAbet + stress ──► src/translator.c   map to Amiga phonemes, stress
   │                                     digits, contractions, "the"→DHIY
   ▼
 Amiga phoneme string  ("/HAXLOW5, AY AEM YOHR AXMIY5GAX.")
   │   ── this is the translator.library / narrator.device boundary ──
   ▼
 src/parse.c      phoneme parser (narrator syntax, ND_PhonErr) + allophones
   │              (post-vocalic LX/RX, flaps, aspiration, glottal stops)
 src/prosody.c    durations (Klatt-style rules) + F0 contour, from
   │              rate / pitch / mode / F0enthusiasm / F0perturb
   ├──► src/voice_classic.c + src/klatt.c   Voice A: formant synthesis
   └──► src/voice_natural.c + Flite         Voice B: diphone LPC
   ▼
 src/narrator.c   parameter checks, sentence loop, sampfreq effect
   ▼
 16-bit mono PCM at 22050 Hz through a sink callback
```

Both voices take the **same** parsed phonemes, durations and F0 targets, so
switching voice never changes what is said or its timing, only the sound.

### Translator

* **Normaliser** (`normalize.c`): one whitespace token at a time with fixed
  buffers (no allocation). Cardinals up to trillions, decimals ("three
  point one four"), ordinals (21st), years (1985 → "nineteen eighty five",
  2005 → "two thousand five", 1990s → "nineteen nineties"), money in $, £,
  € with cents/pence, percentages, times (3:30, 4:05, 5:00 → "o'clock"),
  negative numbers, digit strings of 10+ digits read digit by digit,
  abbreviations (Mr., Dr., St., etc., months, units such as KB and MHz),
  initialisms (all-caps words of up to 3 letters or not in the dictionary
  are spelled: "CPU", "AHI"), camelCase splitting ("AmigaOS" → "Amiga O S"),
  letter/digit splitting ("A500", "OS4"), symbols (&, +, =, @, %, #), UTF-8
  and cp1252 quotes and dashes. Punctuation becomes narrator punctuation:
  `. !` → `.`, `?` → `?`, `, ; : ) (` → `,`, dashes → `-`.
* **Dictionary**: CMU Pronouncing Dictionary 0.7b (134k entries). The first
  pronunciation of each plain word is kept. `tools/mkdict` drops every word
  whose pronunciation the CMU LTS model already predicts exactly (38,030 of
  them) because the translator falls back to the same model at run time.
  Result: 86,896 entries in **958 KB** (front-coded in blocks of 32 words,
  one byte per phone, high-bit terminators, big-endian header and block
  index, binary search on block heads then a scan of ≤32 entries). Unpruned
  it would be 1,245 KB; gzip of the pruned file is 546 KB (not used; the
  file is read as is).
* **Letter to sound**: CMU Flite's cmulex LTS decision trees (153 KB),
  which return stress too. Possessives use stem + /Z/, /S/ or /IH Z/.
* **Classic mode** (`-t classic`): the NRL rules only (Elovitz et al.,
  NRL Report 7948, 1976), in John Wasser's public-domain rule table, stress
  on the first full vowel. This is roughly the 1980s rule-only approach and
  is there for comparison.
* **To Amiga phonemes**: AH0 → AX, AH1/2 → AH, unstressed IH in
  polysyllables → IX, AO before R → OH, HH → /H, JH → J, NG → NX,
  contractions UL/UN/UM/IL/IN/IM where the consonant ends the syllable,
  "the" before a vowel → DHIY. Stress: primary 4, secondary 1, the last
  primary before each punctuation mark 5, function words unstressed
  (polysyllabic ones 2), pronouns unstressed, spelled letters 3.

### Prosody (shared)

* **Durations**: inherent/minimum durations per phoneme and multiplicative
  rules in the style of Klatt (1979): phrase-final lengthening, non-final
  and non-word-final shortening, polysyllabic shortening, unstressed
  shortening (and halved minimum), emphasis (stress ≥ 7), the effect of a
  following voiced/voiceless consonant, cluster shortening, aspiration time
  for voiceless stops. `rate` scales durations; pauses scale half as much.
  Calibrated so rate 150 is ~150 wpm on prose (the V37 device measured 174
  wpm at its default 150).
* **F0**: per phrase (split at `, - . ?`) a declining baseline (≤10%/s,
  later phrases start lower), smooth accent bumps on stressed vowels whose
  height is `0.04 × stress digit × F0enthusiasm/32` (reduced toward the end
  of the sentence in NATURALF0, not in MANUALF0), boundary tones (fall for
  `.`, rise from the last stressed syllable for `?`, continuation rises for
  `,` and `-`), and an optional smoothed random walk for F0perturb.
  ROBOTICF0 is flat. The contour is sampled on a 10 ms grid (coarser for
  very long sentences, at most 1200 points).

### Voice A "Classic": formant synthesis

`klatt.c` is a cascade/parallel formant synthesizer in the style of Klatt
(1980), written from the published descriptions:

* source: KLGLOTT88-style polynomial glottal flow, differentiated (lip
  radiation), open quotient 0.62 (female 0.7), one-pole spectral tilt,
  ~0.5% jitter and ~3% shimmer, aspiration noise that is louder in the open
  phase;
* cascade: nasal pole/zero pair, F1-F6 plus fixed poles at 5.9/6.9/7.9 kHz
  (higher-pole correction; without them nothing above 5 kHz survives at a
  22 kHz rate);
* parallel branch for frication and bursts: first-differenced noise
  through F2-F6 with per-phoneme amplitudes plus a bypass path;
* optional peaking gains for A1adj-A3adj;
* a +6 dB high shelf on the voiced path and +12 dB on the frication branch
  so sibilants sit 5-10 dB under the vowel peak, as in natural speech.

`voice_classic.c` builds formant tracks with Holmes/Klatt-style dominance
coarticulation: each boundary gets a locus-like value from the dominant
phoneme (stops > fricatives > nasals > liquids/glides > vowels) and
class-dependent transition lengths (`articulate` scales them); velars
converge F2 and F3 toward ~2 kHz (the "velar pinch"); diphthongs
glide between two targets; stops are closure (with a voice bar if voiced),
burst and aspiration (65 ms before a stressed vowel); affricates are
closure, a release transient, then frication. Parameters are
updated every 2 ms; source amplitudes are smoothed over ~3 ms. FEMALE scales
the formant targets and the source. Formant bandwidths are 1.4× the
textbook values, which measured more intelligible.

Compared with the V37 narrator it keeps the same model family (a formant
synthesizer driven by phoneme targets and rules) but with 16-bit output,
a better glottal source with aspiration and jitter, six formants plus
higher-pole correction, smoother coarticulation and the newer prosody.

### Voice B "Natural": diphone concatenation

`voice_natural.c` turns the phone sequence into diphone units of CMU
Flite's `cmu_us_kal16` database (1,623 diphones of a male US speaker,
16 kHz, LPC coefficients plus µ-law residuals) and calls Flite's
residual-excited LPC resynthesis (`join_units_modified_lpc`, fixed-point).
Pitch marks are placed from our F0 contour and units are stretched to our
durations, so Flite's own text processing, duration and intonation models
are not used. Flite's consonant-cluster diphones (`b_-_l`, ...) are used
inside words. Output is resampled 16 → 22.05 kHz with a 32-tap polyphase
windowed-sinc filter (`resample.c`).

Mapping from Amiga phonemes to kal16 phones: one-to-one for the ARPAbet
set; `OH` → ao, `IX` → ih, `WH` → w, `/C` → hh, `DX` → d, `LX` → l,
`RX` → r, `QX` and `Q` → a short pause (no glottal-stop unit exists).
narrator fields that only make sense for a formant model (F1-3adj, A1-3adj,
articulate, centralize, AV/AFbias, sex) do not affect this voice yet.

### Fixed memory, portability

* Core code uses no OS calls: C99, `<stdint.h>`, `<math.h>` (float),
  `malloc` of a few fixed-size work areas per call (`sp_utt` ≈ 19 KB per
  sentence, classic tracks ≈ 51 KB, resampler ≈ 19 KB). One sentence (up to
  600 phonemes, longer ones are split at a word boundary) is processed at a
  time and audio is delivered through a callback in small blocks, so
  memory does not grow with text length. The natural voice allocates one
  sentence of Flite LPC data (a few hundred KB for a long sentence).
* All file formats are explicitly big-endian; WAV writing is explicitly
  little-endian. A deterministic LCG is the only random source, so output
  is reproducible.
* The output is bit-identical between macOS arm64 and PPC for the natural
  voice and within ±1 LSB (SNR 92.7 dB) for the classic voice (float/libm
  rounding).

## 2. Results (phase 1)

Intelligibility is measured with a local Whisper model used **only as a
test oracle** (`tests/intelligibility.py`, faster-whisper in a scratch
venv; it is not part of the product). Word error rate on 8 test sentences
(numbers, a question, a long sentence, an Amiga phrase, hard words; 120
words) and 20 Harvard sentences (159 words). "Original" is the real 68k
narrator.device 37.7 + translator.library 37.1 running in FS-UAE, captured
by `tools/narcap68k`.

| Configuration | WER, Whisper small.en | WER, Whisper base.en |
|---|---|---|
| Natural voice | 3.6% | 11.8% |
| Classic voice | 9.0% | 14.7% |
| Classic voice + NRL rule-only translator | 14.0% | 18.6% |
| Original 68k narrator 37.7 + translator 37.1 | 17.6% | 28.0% |

Details, per-set numbers and a synthesizer-only comparison are in
`docs/results.md`.

Speed (CPU time / audio time): ~0.0005 on an Apple M-series Mac. Measured on
the QEMU sam460ex guest (an emulated 460EX, slower than the real 1.15 GHz
chip): classic voice RTF 0.12, natural voice 0.079 for a 12 s sentence.
Both voices are therefore comfortably real-time on a sam460 and trivially
so on an X5000; the classic voice could also run on a fast 68060.

Sizes (PPC objects, `ppc-amigaos-size`):

| Part | Size |
|---|---|
| our code: translator + normaliser + narrator core + classic voice + NRL table | 63 KB |
| Flite engine subset (HRG, values, LPC/residual synthesis, LTS walker) | 45 KB |
| CMU LTS model | 153 KB |
| `english.dict` (pruned CMU dictionary) | 958 KB |
| kal16 diphone voice: residuals 3,261 KB + LPC 606 KB + indices 101 KB | 3,968 KB |

## 3. Planned OS4 architecture (phase 2)

```
 68k program ──(68k jump table: EmuTrap stubs)──┐
 PPC program ──(ITranslator->Translate)─────────┤──► translator.library (PPC)
                                                │      core: normalize/lexicon/lts/translator
 68k program ──(OpenDevice, DoIO via 68k stubs)─┤
 PPC program ──(IExec->DoIO)────────────────────┴──► narrator.device (PPC)
                                                        unit 0, device task
                                                        core: parse/prosody/voices
                                                        └──► ahi.device (16-bit stream)
 Say (PPC)  ── Translate() + DoIO(narrator)
 SPEAK: handler ── DOS packets → Translate() → narrator
```

### translator.library

* A native PPC library (OS4 library model: `ExtendedLibrary`, a `main`
  interface with `Translate()` and the four standard manager methods),
  version 50.
* **68k compatibility**: a 68k jump table whose `Translate` vector (LVO
  -30, registers A0 = inString, D0 = inLength, A1 = outBuffer, D1 =
  outLength, per `pragmas/translator_pragmas.h`) is an `EmuTrap` stub that
  reads the 68k register array and calls the PPC function, exactly as the
  SDK's `Examples/Locale/Charsets/charset_68k.c` does for charset
  libraries. idltool/fdtrans generate the stubs from an SFD/IDL file we
  write (the OS4 SDK has no translator interface).
* The dictionary image is loaded once at first open
  (`LOCALE:Translator/english.dict` or `DEVS:narrator/english.dict`, see
  open questions) into `MEMF_SHARED` memory, read-only, shared by all
  callers. `Translate()` is reentrant: all state is on the caller's stack
  (the normaliser and translator contexts are < 4 KB); the CMU LTS walker
  allocates small Flite lists, which must use a reentrant allocator
  (`AllocVecTags` or a per-call pool) in the port.

### narrator.device

* Native PPC device, unit 0, `struct narrator_rb` / `struct mouth_rb`
  exactly as in `devices/narrator.h` (packed to 2 bytes on PPC), commands
  `CMD_WRITE`, `CMD_READ` (mouths and word/syllable sync), `CMD_FLUSH`,
  `CMD_RESET`, `CMD_STOP`/`CMD_START`, `AbortIO`.
* 68k programs: `BeginIO`/`AbortIO` 68k vectors as EmuTrap stubs (the IORB
  layout is shared, so no translation of the request is needed).
* A device task takes requests from its port, runs `nar_speak()` sentence
  by sentence and streams PCM to **AHI** (`ahi.device`, double-buffered
  `CMD_WRITE` of 16-bit mono at 22050 Hz, AHI's default unit or a
  configurable one). The sink callback blocks while both AHI buffers are
  in flight; `AbortIO` sets an abort flag the sink returns. `ch_masks` /
  `nm_masks` are accepted and ignored (AHI allocates channels). `priority`
  sets the device task's priority while speaking.
* `CMD_READ` with `mouths`: mouth width/height derived from the current
  phoneme (a small table per phoneme, interpolated) and word/syllable sync
  flags from segment flags; delivered when the corresponding audio buffer
  is played.
* Voice selection: default from an env variable / prefs file
  (`ENV:Narrator/voice` = `natural` | `classic`); optionally a unit number
  per voice (unit 0 = default, unit 1 = classic) so programs can pick
  without new IORB fields.
* Voice data: the kal16 database (4 MB) as a big-endian data file, loaded
  on first open (`DEVS:narrator/kal16.voice`); the classic voice needs no
  data file. If the file is missing the device falls back to the classic
  voice instead of failing `OpenDevice`.

### Say and SPEAK:

* `Say`: native rewrite with the classic switches (`-m`/`-f` male/female,
  `-r`/`-n` robotic/natural, `-s` rate, `-p` pitch, `-x file`), plus a
  ReadArgs template (`TEXT/M, FILE/K, RATE/N, PITCH/N, ROBOT/S, FEMALE/S,
  VOICE/K`).
* `SPEAK:` DOS handler (L:speak-handler + DEVS:DOSDrivers/SPEAK mountlist):
  accepts writes, buffers to sentence ends, `Translate()`s and speaks;
  options in the open name, e.g. `SPEAK:opt/r200/p140/f` like the 1.x
  handler.

### Mapping phase 1 → OS4

| Phase 1 file | OS4 component |
|---|---|
| `src/normalize.c`, `lexicon.c`, `lts.c`, `nrl.c`, `translator.c` | translator.library |
| `src/parse.c`, `prosody.c`, `narrator.c`, `phoneme.c` | narrator.device core |
| `src/klatt.c`, `voice_classic.c` | narrator.device, classic voice |
| `src/voice_natural.c`, `resample.c`, `third_party/flite` | narrator.device, natural voice |
| `data/english.dict` | dictionary data file |
| kal16 C arrays | converted to `kal16.voice` data file |
| `cli/say.c`, `cli/translate.c` | Say (rewritten for ReadArgs/AHI) |
| `tests/` | host CI; plus an on-target test run over the devbench bridge |

## 4. Licences

| Component | Licence | Where |
|---|---|---|
| Our code | MIT, © 2026 Chris Collins | everything outside `third_party/` |
| CMU Flite 2.2 subset (engine files, `cmu_lts_model`, `cmu_us_kal16` diphone data) | Flite's BSD-style licence (CMU and contributors; no modifications to the vendored files) | `third_party/flite/` (COPYING, ACKNOWLEDGEMENTS) |
| CMU Pronouncing Dictionary 0.7b | BSD-style (CMU) | `third_party/cmudict/` |
| NRL letter-to-sound rules, John A. Wasser's 1985 table | public domain (US Navy report; Wasser's explicit dedication) | `third_party/nrl/` |
| `amiga_speech_stubs.c` in the Flite tree | MIT, ours (stand-ins for Flite parts not vendored) | `third_party/flite/src/` |

Only Flite's run-time engine and data are used; Flite's GPL'd build-time
tools are not included. The narrator.h constants and structure layout are
interface facts reproduced from the freely distributable header.

Reference only, not used: `flite.device` for OS4 (Fredrik Wikstrom, 2013,
own API, not narrator-compatible).

## 5. Open questions

1. **Default voice.** Natural sounds best; Classic is closer in character
   to what 1985 software expects (and is small). Proposal: natural by
   default, classic via prefs or unit 1.
2. **Female natural voice.** kal16 is male. Options: a second diphone
   database (Flite's diphone voices are all male; a free female diphone set
   would have to be found or recorded), or pitch-plus-formant warping of
   kal16 (resample the LPC spectrum), which degrades quality.
3. **Data file locations.** `DEVS:narrator/` (voices) and
   `LOCALE:Translator/<language>.dict` (dictionaries, ready for other
   languages) vs keeping everything in `DEVS:narrator/`.
4. **Exactness vs quality for old software.** Programs that hand-tuned
   phoneme strings and stress digits for the 1985 device will sound
   different (better, but not identical). Do we need a "V33 timing" mode?
5. **MANUALF0 semantics.** Implemented as position-independent accents; the
   original's exact accent shapes are undocumented.
6. **Parentheses** (noun-phrase marks) are parsed but do not yet shape F0.
7. **Homographs** ("read", "lead", "live") take cmudict's first entry; a
   tiny POS heuristic could help.
8. **Concurrency.** The V37 device serialises speakers by priority; the
   port should queue requests per opener and allow AbortIO mid-sentence.
9. **Mouths**: needed by a few demo programs; the per-phoneme mouth table
   has to be designed (the original values are not documented).
10. **Flite allocation.** Flite uses malloc via `cst_alloc`; in a device
    task this maps to `AllocVecTags`, fine, but the LTS walker inside
    translator.library runs on the caller's task and must be reentrant.

## 6. Next steps (phase 2)

1. **Voice data as files.** Write a converter from Flite's kal16 C arrays
   to one big-endian `kal16.voice` file (index, LPC frames, residuals) and
   load it with `AllocVecTags(MEMF_SHARED)`; drop the 4 MB of linked data.
2. **narrator.device skeleton** (PPC, unit 0): device task, request queue,
   `CMD_WRITE` → `nar_speak()` → AHI double-buffering, `AbortIO`,
   `CMD_FLUSH/RESET/STOP/START`, parameter checks from `nar_params_check()`.
   Then `CMD_READ` mouths/sync.
3. **translator.library** (PPC) with an IDL/SFD and generated 68k EmuTrap
   stubs; dictionary loaded once; make the Flite LTS walker reentrant
   (allocation through a per-call pool).
4. **68k compatibility test**: run the 68k `SYS:Utilities/Say` 37.x and
   `tools/narcap68k` (built unchanged) on OS4 against our PPC device and
   library; check the IORB layout and return codes.
5. **Say** and **SPEAK:** for OS4.
6. **On-target tests** through the devbench bridge: render the test
   sentences on the guest, pull the WAVs back, compare hashes with the host
   build and run the intelligibility oracle on the host.
7. Voice work that remains: a female natural voice, F0 from parentheses
   (noun phrases), homograph handling, more classic-voice tuning (velars
   and affricates are still its weakest sounds).
