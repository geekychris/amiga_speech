# Phase 1 results

All numbers from the commit that adds this file, on an Apple M-series Mac
(macOS, Apple clang) unless stated.

## Intelligibility (word error rate)

Whisper is used **only as a test oracle** (`tests/intelligibility.py`,
faster-whisper, int8 on CPU, beam 5, English-only models). Reference and
transcript both go through our own normaliser so "1985" and "nineteen eighty
five" compare equal. Lower is better.

Test material: `tests/data/sentences.txt` (8 sentences: an Amiga phrase,
a pangram, a yes/no question, numbers and money, a 33-word sentence, hard
words, times and ordinals, product names; 120 words) and
`tests/data/harvard.txt` (Harvard sentences lists 1-2; 159 words).

| Configuration | small.en sentences | small.en Harvard | small.en total | base.en total |
|---|---|---|---|---|
| **Natural** (our translator + diphone voice) | 4.2% | 3.1% | **3.6%** | 11.8% |
| **Classic** (our translator + formant voice) | 8.3% | 9.4% | **9.0%** | 14.7% |
| Classic voice + rule-only NRL translator | 14.2% | 13.8% | 14.0% | 18.6% |
| **Original** 68k narrator.device 37.7 + translator.library 37.1 | 21.7% | 14.5% | **17.6%** | 28.0% |

The original was captured from the real 68k device running in FS-UAE
(AmiKit, OS 3.x) with `tools/narcap68k` (audio.device BeginIO patched to
copy the 8-bit buffers; nothing played). It is the 1991 V37 narrator, the
5-formant successor of the 1985 V33 one, so the "before" here is the best
classic narrator there was.

Synthesizer-only comparison (Harvard, Whisper base.en, earlier build of
the classic voice, same phoneme strings into both devices):

| Phonemes from | original narrator | our classic | our natural |
|---|---|---|---|
| original translator.library | 29.6% | 24.5% | 10.1% |
| our translator | 34.0% | 22.0% | 12.6% |

So both of our synthesizers beat the 1991 synthesizer on identical input,
and our translator beats translator.library (the CMU dictionary and LTS vs
1985 rules).

Speaking rate: at rate 150 our voices measure 147-149 wpm on the 33-word
sentence; the original device measures 174 wpm at its default 150.

## Speed (real-time factor = CPU time / audio time)

| Machine | Classic | Natural |
|---|---|---|
| Apple M-series, macOS, 52 s of Harvard text | 0.0003 | 0.0006 |
| QEMU sam460ex (emulated 460EX), AmigaOS 4.1, 12 s sentence, `-mcpu=powerpc` newlib build | 0.12 | 0.079 |

The QEMU guest is a CPU emulator and slower than a real 1.15 GHz sam460, so
real hardware has more headroom; an X5000 more still. The OS4 binary was
built with the walkero gcc 11 toolchain, `-mcpu=powerpc` (no `dlmzb`).
Output on PPC: natural voice bit-identical to the Mac; classic voice within
±1 LSB (SNR 92.7 dB) because of float/libm rounding.

## Sizes

| Item | Size |
|---|---|
| `data/english.dict` (CMU dict, 86,896 entries after LTS pruning) | 957,670 bytes (unpruned 1,244,954; raw cmudict 3,618,488) |
| CMU LTS model (PPC object) | 153 KB |
| kal16 diphone voice (PPC objects: residuals 3,261 KB, LPC 606 KB, indices 101 KB) | 3,968 KB |
| Flite engine subset (PPC) | 45 KB |
| Our code incl. classic voice and NRL table (PPC) | 63 KB |
| Peak RSS on the Mac, 52 s of text (whole process, includes 4 MB of linked voice data) | natural 12 MB, classic 8 MB |
