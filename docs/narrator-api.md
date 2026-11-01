# The classic Amiga speech API (what we are compatible with)

This summarises the interface of the classic AmigaOS speech system
(`translator.library` V37 and `narrator.device` V37) that amiga_speech
reproduces, and notes how the phase 1 prototype handles each part.

Sources: the AmigaOS Developer wiki pages *Narrator Device* and *Translator
Library* (the RKM Devices chapter), the V37 include file `devices/narrator.h`
(© 1990-91 Joseph Katz / Mark Barton, freely distributable) and the
`translator.doc` autodoc, as shipped in the AmigaOS 4.1 SDK 54.25. Behaviour
of the original was also checked by running the real 68k `narrator.device`
37.7 and `translator.library` 37.1 under FS-UAE (see `tools/narcap68k`).

## translator.library

```c
LONG Translate(STRPTR inString, LONG inLength, STRPTR outBuffer, LONG outLength);
```

* Converts English text to a narrator phoneme string.
* Returns 0 on success. The only error is an output overflow: translation
  then stops at a word boundary *before* the overflow and the result is the
  negated offset into `inString` where it stopped; call again from
  `inString + (-rc)`.
* Error codes in `libraries/translator.h`: `TR_NotUsed` (-1), `TR_NoMem`
  (-2), `TR_MakeBad` (-4).

Prototype: `include/amiga_speech/translator.h`, same signature and return
convention (`Translate()`), plus `tr_translate_mode()` for the rule-only
"classic" mode. Unit test `test_translate_overflow` checks the resume
protocol with a 24-byte buffer.

## narrator.device: phoneme input

`CMD_WRITE` with `io_Data` pointing at an ASCII phoneme string and
`io_Length` its length. Parsing stops at NUL, at `#` (backward
compatibility), or after `io_Length` characters. The device speaks a
sentence at a time.

### Phonemes

| Class | Codes (example word) |
|---|---|
| Vowels | `IY` beet, `IH` bit, `EH` bet, `AE` bat, `AA` hot, `AH` under, `AO` talk, `UH` look, `ER` bird, `OH` border, `AX` about\*, `IX` solid\* |
| Diphthongs | `EY` made, `AY` hide, `OY` boil, `AW` power, `OW` low, `UW` crew |
| Liquids/glides | `R` red, `L` long, `W` wag, `Y` yellow |
| Nasals | `M` men, `N` no, `NX` sing |
| Fricatives | `S` soon, `SH` shy, `TH` thin, `F` fed, `Z` zoo, `ZH` pleasure, `DH` then, `V` very, `WH` when, `/H` hole, `/C` loch |
| Affricates | `CH` check, `J` judge |
| Plosives | `B` but, `P` put, `D` dog, `T` toy, `K` keg, `G` guest |
| Special | `DX` flap (pity), `LX` post-vocalic L (fall), `RX` post-vocalic R (star), `Q` glottal stop, `QX` silent vowel |
| Contractions | `UL`=AXL, `IL`=IXL, `UM`=AXM, `IM`=IXM, `UN`=AXN, `IN`=IXN |

\* `AX` and `IX` are reduced vowels and should not carry stress.

Codes are one or two letters; `/H` and `/C` start with a slash. The
prototype matches two-character codes first, so `IHN` is `IH N` while `IN`
is the contraction. Lower case is accepted. A bare `H` is a spelling error
(the code is `/H`), as on the real device.

### Stress and intonation

A digit `1`-`9` directly after a vowel marks a stressed syllable; the
value sets the size of the pitch accent. A digit after anything other than
a vowel is an error (`ND_PhonErr`). Suggested values from the narrator
documentation: exclamations 9, adverbs and quantifiers 7, nouns and
adjectives 5, verbs 4, pronouns 3, secondary stress 1-2, function words
none.

Our translator writes primary stress 4, secondary 1, the last primary
stress before each punctuation mark 5 (the nuclear accent), polysyllabic
function words 2, pronouns and monosyllabic function words none, spelled
letters 3. The real translator.library 37.1 output for the test sentences is
saved next to the samples (`samples/NN_amiga68k_phonemes.txt`) for
comparison.

### Punctuation

| Mark | Meaning | Prototype |
|---|---|---|
| `.` | sentence end, final fall | 380 ms pause, falling boundary tone |
| `?` | yes/no question, final rise | 380 ms pause, rise from the last stressed syllable |
| `,` | clause boundary, pause with a rise | 230 ms pause, +12% continuation rise |
| `-` | phrase boundary, smaller rise | 160 ms pause, +5% rise |
| `( )` | noun phrase brackets (intonation hint) | parsed and marked; not yet used by the F0 model |
| (none) | the device appends a dash | same |

`!`, `;` and `:` are not part of the documented set; the prototype treats
`!` like `.` and `;`/`:` like `,`.

## narrator.device: struct narrator_rb (V37)

```c
struct narrator_rb {
    struct IOStdReq message;
    UWORD rate;          /* words/minute, 40-400, default 150 */
    UWORD pitch;         /* baseline F0 in Hz, 65-320, default 110 */
    UWORD mode;          /* NATURALF0 0, ROBOTICF0 1, MANUALF0 2 */
    UWORD sex;           /* MALE 0, FEMALE 1 */
    UBYTE *ch_masks;     /* audio channel allocation maps */
    UWORD nm_masks;      /* number of maps */
    UWORD volume;        /* 0-64, default 64 */
    UWORD sampfreq;      /* 5000-28000, default 22200 */
    UBYTE mouths;        /* non-zero: produce mouth shapes for CMD_READ */
    UBYTE chanmask;      /* internal */
    UBYTE numchan;       /* internal */
    UBYTE flags;         /* NDF_NEWIORB 1, NDF_WORDSYNC 2, NDF_SYLSYNC 4 */
    /* fields below are only used when NDF_NEWIORB is set */
    UBYTE F0enthusiasm;  /* F0 excursion, 32nds; default 32 */
    UBYTE F0perturb;     /* random F0 wobble 0-255; default 0 */
    BYTE  F1adj, F2adj, F3adj;  /* formant frequency, +-5% steps */
    BYTE  A1adj, A2adj, A3adj;  /* formant amplitude, dB */
    UBYTE articulate;    /* transition time %, default 100 */
    UBYTE centralize;    /* 0-100 % pull of vowels towards centphon */
    char  *centphon;     /* IY IH EH AE AA AH AO OW UH ER UW */
    BYTE  AVbias, AFbias;/* voicing / frication amplitude, dB */
    BYTE  priority;      /* task priority while speaking, default 100 */
    BYTE  pad1;
};
struct mouth_rb { struct narrator_rb voice; UBYTE width, height, shape, sync; };
```

On PPC the SDK header packs the structure with `#pragma pack(2)`, matching
the 68k layout; the OS4 device must use exactly this layout so 68k programs
running under emulation and native PPC programs share it.

Error codes: `ND_NoMem` -2, `ND_NoAudLib` -3, `ND_MakeBad` -4, `ND_UnitErr`
-5, `ND_CantAlloc` -6, `ND_Unimpl` -7, `ND_NoWrite` -8, `ND_Expunged` -9,
`ND_PhonErr` -20, `ND_RateErr` -21, `ND_PitchErr` -22, `ND_SexErr` -23,
`ND_ModeErr` -24, `ND_FreqErr` -25, `ND_VolErr` -26, `ND_DCentErr` -27,
`ND_CentPhonErr` -28. Bounds: `MINRATE` 40, `MAXRATE` 400, `MINPITCH` 65,
`MAXPITCH` 320, `MINFREQ` 5000, `MAXFREQ` 28000, `MINVOL` 0, `MAXVOL` 64,
`MINCENT` 0, `MAXCENT` 100. Defaults: `DEFPITCH` 110, `DEFRATE` 150,
`DEFVOL` 64, `DEFFREQ` 22200, `DEFSEX` MALE, `DEFMODE` NATURALF0,
`DEFARTIC` 100, `DEFCENTRAL` 0, `DEFF0PERT` 0, `DEFF0ENTHUS` 32,
`DEFPRIORITY` 100.

`CMD_READ` with a `mouth_rb` returns mouth width/height (and, with
`NDF_WORDSYNC`/`NDF_SYLSYNC`, word and syllable sync events in `sync`) while
speech is playing.

### How the prototype honours each field

The portable core takes a `nar_params` (in
`include/amiga_speech/narrator.h`) carrying the speech fields under the same
names; the device wrapper will copy them from the IORB.

| Field | Classic voice (formant) | Natural voice (diphone) |
|---|---|---|
| rate | segment durations scaled; 150 = ~150 wpm on prose | same durations |
| pitch | F0 baseline | same F0 contour drives LPC pitch marks |
| mode | NATURAL: declination + accents; ROBOTIC: monotone; MANUAL: accents fixed, no position effects | same |
| sex | FEMALE raises formants (F1 x1.12, F2 x1.17, F3 x1.15), breathier source | not yet (single male database) |
| volume | linear gain, 64 = full | same |
| sampfreq | the original played its 22200 Hz-tuned output at another rate; reproduced by resampling (pitch, formants and tempo shift together) | same |
| F0enthusiasm | scales accent heights | same |
| F0perturb | low-frequency random F0 wobble | same |
| F1-3adj | formant frequencies +-5% per step | not applicable |
| A1-3adj | peaking gain at F1-F3 | not applicable |
| articulate | transition lengths | not applicable |
| centralize/centphon | vowel targets pulled toward the chosen vowel | not applicable |
| AVbias/AFbias | voicing / frication levels | not applicable |
| mouths, sync, ch_masks, priority | device wrapper (phase 2) | device wrapper |
| without NDF_NEWIORB | V37 fields ignored (defaults), MANUALF0 rejected | same |

Validation (`nar_params_check`) returns the same `ND_*` codes as the device
for out-of-range values; `test_params` covers each.
