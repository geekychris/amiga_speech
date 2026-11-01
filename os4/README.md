# amiga_speech for AmigaOS 4

Native PPC replacements for the classic speech system:

| File | Install to | What |
|---|---|---|
| `narrator.device` | `DEVS:` | V37-compatible narrator.device (natural + classic voices) |
| `translator.library` | `LIBS:` | V37-compatible translator.library (`Translate()`) |
| `kal16.voice` | `DEVS:narrator/` | natural voice data (built on the host: `make data/kal16.voice`) |
| `english.dict` | `DEVS:narrator/` | pronunciation dictionary (`data/english.dict`) |
| `Say` | `C:` or `SYS:Utilities/` | Say command |
| `speak-handler` | `L:` | SPEAK: handler, with `speak/SPEAK.mountlist` as `DEVS:DOSDrivers/SPEAK` |
| `Speech-Setup` | anywhere | use everything from one drawer until reboot (no installation) |

### Trying it without installing anything

Copy the files into one drawer (e.g. `DH1:speech`) and, in a Shell:

```
Execute DH1:speech/Speech-Setup
Say Hello, I am your Amiga.
```

`Speech-Setup` (optionally `Execute DH1:speech/Speech-Setup DIR=<drawer>`)
only makes assigns (`SPEECH:`, and adds the drawer to `LIBS:` and `DEVS:`),
adds it to the Shell's path and sets `ENV:Narrator/DataDir`; nothing is
written to disk and everything is gone after a reboot. The device and
library look for `kal16.voice` / `english.dict` in `ENV:Narrator/DataDir`,
`SPEECH:`, `DEVS:narrator`, `DEVS:` (and `LIBS:` for the dictionary); if the
voice file is not found the natural voice falls back to the classic one.

Both the device and the library carry a 68k jump table (EmuTrap stubs), so
68k programs running under emulation (the original `Say`, games, BASIC) use
them like the 1991 ones.

## Build

```sh
make data/kal16.voice data/english.dict     # on the host
os4/build.sh                                # in the walkero gcc 11 container
```

Everything is compiled with `-mcpu=powerpc` (never 440/440fp); the build
fails if any binary contains a `dlmzb` instruction. The device and library
are freestanding (`-nostdlib`): `common/minilibc.c` supplies the few C
functions the speech core needs, on top of `AllocVecTags(MEMF_SHARED)`.
`os4/build.sh XCFLAGS=-DNARDEBUG` adds `DebugPrintF` traces to the device.

## Settings

narrator.device units: **0** = the default voice, **1** = the classic
formant voice. ENV variables (all optional):

| Variable | Meaning |
|---|---|
| `Narrator/Voice` | `natural` (default) or `classic` for unit 0 |
| `Narrator/DataDir` | directory of `kal16.voice` and `english.dict` (default `DEVS:narrator`) |
| `Narrator/AHIUnit` | AHI unit (default 0) |
| `Narrator/Capture` | append the speech as 16-bit big-endian mono PCM at 22050 Hz to this file instead of playing it (used by the tests) |

Differences from the 1991 device: the speaking task's priority is capped at
1 (the original raised itself to 100; with AHI double-buffering a high
priority would only starve other tasks while the next sentence is computed); `ch_masks`/`nm_masks` are accepted
and ignored (AHI allocates channels); mouth width/height values are our own
per-phoneme table (the original values are undocumented).

## On-target results (QEMU sam460ex, AmigaOS 4.1)

* `nartest`: 22/22 (defaults, error codes, mouths and word/syllable sync,
  AbortIO, CMD_FLUSH, old-style IORB, unit 1).
* `trtest`: 5/5, `Translate()` output identical to the host build,
  overflow/resume protocol.
* `regress`: Say → translator.library → narrator.device on the guest,
  8 sentences × 2 voices, compared with the host build: natural voice
  bit-identical, classic voice within 1 LSB.
* the original 68k `Say` 37.4 (1991) under emulation drives the PPC
  library and device (Whisper transcribes the result word for word).
* `SPEAK:` handler: `echo "..." >SPEAK:opt/s170` speaks.
* Speed: stock Flite kal16 runs at RTF 0.10 on the guest; Flite's CMU slt
  clustergen (female) voice at RTF 2.7, i.e. not real time there.

## Testing on the QEMU guest

1. `tools/os4_jobs.py` writes the AmigaDOS job scripts.
2. `tools/stage_hdf.py` holds amiga_mcp's emulator lock, stops QEMU, writes
   the binaries, data and jobs into `DH1:speech/` on the dev HDF with
   xdftool (never the system HDF), and starts QEMU again. (Pushing 5 MB
   through the bridge is too slow.)
3. `tools/os4_test.py` holds the lock and the "CLAUDE IS TESTING" banner,
   adds `DH1:speech` to `DEVS:`/`LIBS:` with `Assign ADD`, sets
   `Narrator/Capture` so nothing is played, runs the jobs, compares the
   captured speech with the host build, checks for Grim Reaper after each
   step, and removes the assigns and variables. Nothing goes into `SYS:`.

```sh
tools/os4_jobs.py && tools/stage_hdf.py && tools/os4_test.py translator device say say68k regress slt speak
```

Debugging: `DumpDebugBuffer` on the guest shows the `DebugPrintF` output
(`[narrator]` traces with `os4/build.sh XCFLAGS=-DNARDEBUG`, Flite errors
as `[speech]`).

* `nartest`: device behaviour (defaults, errors, mouths and sync, AbortIO,
  CMD_FLUSH, old-style IORB, unit 1)
* `trtest`: `Translate()` results identical to the host build, overflow
  protocol
* `regress`: renders the test sentences on the guest and compares the PCM
  with the host build
* `say68k`: the original 68k `Say` (AmigaOS 3.x, under emulation) talking
  to our PPC library and device
