# amiga_speech

A new speech system for **AmigaOS 4.1 PPC** that is API-compatible with the
classic `translator.library`, `narrator.device` and `Say`, sounds much
better than the original 68k narrator, runs locally, and uses classic
non-neural techniques (dictionary + rules, formant synthesis, diphone
concatenation).

This is **phase 1**: a host prototype in portable C99 that writes WAV
files so the voices can be judged before the OS4 port. The same code
already cross-compiles for OS4 (newlib) and runs on the QEMU sam460ex guest.
See [docs/DESIGN.md](docs/DESIGN.md) for the architecture and the OS4 plan,
[docs/narrator-api.md](docs/narrator-api.md) for the classic API and
phoneme set, and [docs/results.md](docs/results.md) for measurements.

## Two voices behind one narrator interface

* **Natural**: diphone concatenation with residual-excited LPC over CMU
  Flite's `cmu_us_kal16` database, driven by our own phonemes, durations
  and intonation.
* **Classic**: a Klatt-style cascade/parallel formant synthesizer (the same
  family as the original narrator) with a modern glottal source,
  coarticulation and prosody.

Both read the classic narrator phoneme strings (`/HEH4LOW`) and honour the
`narrator_rb` settings: rate, pitch, natural/robotic/manual F0, sex,
volume, sampfreq, F0 enthusiasm/perturbation, formant and amplitude
adjustments, articulation, centralisation.

## Results (Whisper small.en as a test oracle, 279 words)

| | Word error rate |
|---|---|
| Natural voice | **3.6%** |
| Classic voice | **9.0%** |
| Classic voice, 1976 NRL rules instead of the dictionary | 14.0% |
| Original 68k narrator.device 37.7 + translator.library 37.1 (FS-UAE) | 17.6% |

Real-time factor on the QEMU sam460ex guest: classic 0.12, natural 0.079.
Data: dictionary 958 KB, diphone voice 4.0 MB, code ~110 KB.

## Listen

`samples/` has 8 sentences, each as:

* `NN_natural.wav`: Natural voice
* `NN_classic.wav`: Classic voice
* `NN_classic_nrl.wav`: Classic voice with the rule-only translator
* `NN_amiga68k.wav`: the original 68k narrator (8-bit, captured from FS-UAE),
  with `NN_amiga68k_phonemes.txt` holding what translator.library produced

The sentences are in `tests/data/sentences.txt`.

## Build and use (host)

```sh
make            # build/say, build/translate, data/english.dict
make test       # unit tests + output regression hashes
make samples    # re-render samples/

build/translate "Hello, I am your Amiga."
# /HAXLOW5, AY AEM YOHR AXMIY5GAX.
build/say -v natural -o hello.wav "Hello, I am your Amiga."
build/say -v classic -m robotic -p 90 -o robot.wav "I am a robot."
build/say -x -v classic -o x.wav "DHIHS IHZ AHMIY5GAH SPIY5KIHNX."
build/say -h        # all options
```

`translate -c` uses the rule-only translator, `-n` shows the normalised
words, `-w` shows each word's ARPAbet and whether it came from the
dictionary or letter-to-sound.

The intelligibility oracle needs `faster-whisper` in a virtualenv:
`python3 tests/intelligibility.py --model small.en` (after `make samples`).
`tools/narcap68k/` is the 68k capture tool for the original narrator (build
with the `amigadev/crosstools:m68k-amigaos` image; convert with
`tools/narcap2wav.py`).

## Layout

```
include/amiga_speech/  narrator.h, translator.h   portable public API
src/                   translator, normaliser, lexicon, LTS, parser,
                       prosody, Klatt synth, both voices, resampler
cli/                   host say / translate
tools/                 mkdict, sample/render scripts, narcap68k
tests/                 unit tests, regression hashes, intelligibility oracle
third_party/           Flite subset, cmudict, NRL rules (see its README)
docs/                  DESIGN.md, narrator-api.md, results.md
```

## Licence

MIT for our code (© 2026 Chris Collins). Third-party parts keep their
licences: Flite (BSD-style), CMU dictionary (BSD-style), NRL rules (public
domain). See [third_party/README.md](third_party/README.md).
