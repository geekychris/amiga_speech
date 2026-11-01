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
20 (the original raised itself to 100); `ch_masks`/`nm_masks` are accepted
and ignored (AHI allocates channels); mouth width/height values are our own
per-phoneme table (the original values are undocumented).

## Testing on the QEMU guest

`tools/os4_test.py` (on the host) takes the emulator lock and the
"CLAUDE IS TESTING" banner from amiga_mcp, copies everything to
`RAM:speech`, adds it to `DEVS:` and `LIBS:` with `Assign ADD`, sets
`Narrator/Capture` so nothing is played, runs the on-target tests, and
removes the assigns and variables again. Nothing is installed into `SYS:`.

```sh
tools/os4_test.py device translator say regress say68k
```

* `nartest`: device behaviour (defaults, errors, mouths and sync, AbortIO,
  CMD_FLUSH, old-style IORB, unit 1)
* `trtest`: `Translate()` results identical to the host build, overflow
  protocol
* `regress`: renders the test sentences on the guest and compares the PCM
  with the host build
* `say68k`: the original 68k `Say` (AmigaOS 3.x, under emulation) talking
  to our PPC library and device
