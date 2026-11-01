#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Chris Collins <chris@hitorro.com>
#
# Run the on-target tests on the AmigaOS 4 QEMU guest through amiga_mcp's
# devbench, holding the emulator lock and showing the "CLAUDE IS TESTING"
# banner for the whole session.
#
# The binaries, data and job scripts are staged on DH1:speech by
# tools/stage_hdf.py (run tools/os4_jobs.py first). DEVS: and LIBS: get
# DH1:speech added with Assign ADD and ENV:Narrator/Capture makes
# narrator.device write PCM to RAM: instead of playing it; all of it is
# undone at the end. After every step the guest is checked for a Grim Reaper.
#
#   tools/os4_test.py [steps...]
#   steps: device translator say say68k speak regress slt

import argparse, json, os, struct, subprocess, sys, tempfile, time, urllib.request, wave

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(os.path.dirname(ROOT), "amiga_mcp", "scripts"))
P = "DH1:speech"


class Bench:
    def __init__(self, url):
        self.url = url.rstrip("/")

    def _post(self, path, body, timeout=300):
        req = urllib.request.Request(self.url + path, json.dumps(body).encode(),
                                     {"Content-Type": "application/json"})
        with urllib.request.urlopen(req, timeout=timeout) as r:
            return json.loads(r.read().decode())

    def script(self, text, timeout=60):
        try:
            r = self._post("/api/script", {"script": text}, timeout=timeout + 30)
        except Exception as e:
            return "ERROR: %r" % e
        return r.get("output", "") if r.get("status") == "ok" else "ERROR: %s" % r

    def pull(self, src, dest):
        r = self._post("/api/transfer", {"source": src, "dest": dest, "direction": "pull"})
        if not r.get("success"):
            raise RuntimeError("pull failed: %s %s" % (src, r))
        return r


SETUP = """makedir RAM:speech
setenv Narrator/DataDir DH1:speech
setenv Narrator/Capture RAM:speech/cap.raw
assign DEVS: DH1:speech ADD
assign LIBS: DH1:speech ADD
"""
TEARDOWN = """unsetenv Narrator/DataDir
unsetenv Narrator/Capture
assign DEVS: DH1:speech REMOVE
assign LIBS: DH1:speech REMOVE
avail flush
"""


def nudge_mouse(d):
    import socket
    try:
        with socket.create_connection(("127.0.0.1", 2348), timeout=2) as c:
            c.sendall(b"mouse_move %d 0\n" % d)
    except OSError:
        pass


def run_job(bench, name, limit=400):
    """execute DH1:speech/jobs/<name>.job in the background, wait for JOBDONE"""
    log = "RAM:speech/%s.log" % name
    bench.script("delete %s QUIET" % log)
    bench.script("run >NIL: execute %s/jobs/%s.job" % (P, name))
    t0 = time.time()
    out = ""
    flip = 1
    while time.time() - t0 < limit:
        time.sleep(4)
        nudge_mouse(flip)            # keep the screen blanker from starting
        flip = -flip
        out = bench.script("type %s" % log)
        if "JOBDONE" in out:
            return out.replace("JOBDONE", "").rstrip(), True
    return out, False


def show(name, out, ok):
    print("--- %s%s" % (name, "" if ok else " (did not finish)"))
    print(out)


def step_simple(name, limit=400):
    def f(bench):
        out, ok = run_job(bench, name, limit)
        show(name, out, ok)
        return ok
    return f


def pcm_from_guest(bench, raw, tmp):
    local = os.path.join(tmp, raw.split(":")[1].replace("/", "_"))
    bench.pull(raw, local)
    data = open(local, "rb").read()
    return struct.unpack(">%dh" % (len(data) // 2), data)


def save_wav(path, pcm):
    g = wave.open(path, "wb")
    g.setnchannels(1); g.setsampwidth(2); g.setframerate(22050)
    g.writeframes(struct.pack("<%dh" % len(pcm), *pcm))
    g.close()


def step_regress(bench):
    out, ok = run_job(bench, "regress", limit=1500)
    show("regress", out, ok)
    sents = [l.strip().replace('"', "") for l in open(os.path.join(ROOT, "tests/data/sentences.txt")) if l.strip()]
    tmp = tempfile.mkdtemp()
    worst = 0
    for i, s in enumerate(sents, 1):
        for voice in ("natural", "classic"):
            guest = pcm_from_guest(bench, "RAM:speech/r%02d%s.raw" % (i, voice[0]), tmp)
            wav = os.path.join(tmp, "h.wav")
            subprocess.run([os.path.join(ROOT, "build/say"), "-q", "-v", voice, "-o", wav, s], check=True)
            w = wave.open(wav)
            host = struct.unpack("<%dh" % w.getnframes(), w.readframes(w.getnframes()))
            n = min(len(host), len(guest))
            diff = max((abs(host[k] - guest[k]) for k in range(n)), default=0)
            if len(host) != len(guest):
                diff = 99999
            worst = max(worst, diff)
            save_wav(os.path.join(ROOT, "build", "guest_%02d_%s.wav" % (i, voice)), guest)
            print("%02d %-8s host %6d guest %6d samples, max diff %d LSB" % (i, voice, len(host), len(guest), diff))
    print("regress: worst difference %d LSB -> %s" % (worst, "PASS" if worst <= 2 else "FAIL"))
    return ok


def step_say68k(bench):
    out, ok = run_job(bench, "say68k")
    show("say68k", out, ok)
    tmp = tempfile.mkdtemp()
    for raw in ("RAM:speech/say68a.raw", "RAM:speech/say68b.raw"):
        try:
            pcm = pcm_from_guest(bench, raw, tmp)
        except RuntimeError as e:
            print("missing", raw, e)
            continue
        dst = os.path.join(ROOT, "build", "guest_" + raw.split("/")[-1].replace(".raw", ".wav"))
        save_wav(dst, pcm)
        print("%s: %.2f s of speech -> %s" % (raw, len(pcm) / 22050.0, dst))
    return ok


STEPS = {
    "device": step_simple("device"),
    "translator": step_simple("translator"),
    "say": step_simple("say"),
    "speak": step_simple("speak", 120),
    "say68k": step_say68k,
    "regress": step_regress,
    "slt": step_simple("slt", 1500),
}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--url", default="http://localhost:3010")
    ap.add_argument("--keep", action="store_true", help="leave the assigns and variables")
    ap.add_argument("steps", nargs="*", default=["device", "translator", "say", "say68k", "regress"])
    a = ap.parse_args()
    from claude_busy import claude_testing
    bench = Bench(a.url)
    with claude_testing(a.url + "/mcp", "speech tests"):
        print(bench.script(SETUP))
        try:
            for step in a.steps:
                STEPS[step](bench)
                if "GrimReaper" in bench.script("status"):
                    print("!!! Grim Reaper after step %s: a crash, see the guest screen" % step)
                    break
        finally:
            if not a.keep:
                print(bench.script(TEARDOWN))


if __name__ == "__main__":
    main()
