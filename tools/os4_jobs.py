#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Chris Collins <chris@hitorro.com>
#
# Generate the AmigaDOS job scripts for the on-target tests into
# os4/build/jobs/ (staged to DH1:speech/jobs/ by tools/stage_hdf.py).
# Every line redirects its own output to RAM:speech/<job>.log and the last
# line echoes JOBDONE, which tools/os4_test.py waits for.

import os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
P = "DH1:speech"


def job(name, lines):
    log = "RAM:speech/%s.log" % name
    out = []
    for l in lines:
        if l.startswith(("stack", "setenv", "unsetenv", "delete", "wait", "assign")) or ">" in l:
            out.append(l)
        else:
            out.append(l + " >>" + log)
    out.append("echo JOBDONE >>" + log)
    return "\n".join(out) + "\n"


def capture(raw, cmd):
    return ["delete %s QUIET" % raw, "setenv Narrator/Capture %s" % raw, cmd,
            "setenv Narrator/Capture RAM:speech/cap.raw"]


def sentences():
    return [l.strip().replace('"', "") for l in open(os.path.join(ROOT, "tests/data/sentences.txt")) if l.strip()]


def main():
    d = os.path.join(ROOT, "os4", "build", "jobs")
    os.makedirs(d, exist_ok=True)
    jobs = {
        "device": ["stack 200000", P + "/nartest"],
        "translator": [P + "/trtest"],
        "say": capture("RAM:speech/say1.raw", P + '/Say "Hello, I am your Amiga. In 1985 it cost $1,295."')
             + capture("RAM:speech/say2.raw", P + '/Say -f -s200 VOICE classic "This is the classic voice."')
             + ["list RAM:speech/say#?.raw"],
        "say68k": ["version " + P + "/Say68k FULL"]
             + capture("RAM:speech/say68a.raw", P + '/Say68k "Hello, I am your Amiga. Do you remember me?"')
             + capture("RAM:speech/say68b.raw", P + '/Say68k -f -s200 "The old sixty eight thousand Say talks to the new device."')
             + ["list RAM:speech/say68#?.raw"],
        "speak": ["delete RAM:speech/say3.raw QUIET", "setenv Narrator/Capture RAM:speech/say3.raw",
                  "copy " + P + "/jobs/SPEAK.ml RAM:speech/SPEAK",
                  "mount SPEAK: FROM RAM:speech/SPEAK",
                  'echo "Hello from the speak handler. Is it working?" >SPEAK:opt/s170',
                  "wait 3", "setenv Narrator/Capture RAM:speech/cap.raw",
                  "list RAM:speech/say3.raw"],
        "slt": ["stack 1000000", P + "/fltime kal16 " + P + "/harvard.txt RAM:speech/kal.wav",
                P + "/fltime slt " + P + "/harvard.txt RAM:speech/slt.wav"],
    }
    reg = []
    for i, s in enumerate(sentences(), 1):
        for v in ("natural", "classic"):
            reg += capture("RAM:speech/r%02d%s.raw" % (i, v[0]), P + '/Say VOICE %s "%s"' % (v, s))
    jobs["regress"] = reg
    for name, lines in jobs.items():
        open(os.path.join(d, name + ".job"), "w", encoding="latin-1").write(job(name, lines))
    # "Mount X: FROM file" wants the device name and a '#' terminator
    # (the DOSDrivers form without them makes Mount hang)
    open(os.path.join(d, "SPEAK.ml"), "w").write(
        "SPEAK:\n  Handler = %s/speak-handler\n  StackSize = 65536\n  Priority = 5\n  GlobVec = -1\n#\n" % P)
    print("wrote", sorted(os.listdir(d)))


if __name__ == "__main__":
    main()
