#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Chris Collins <chris@hitorro.com>
#
# Run the on-target tests on the AmigaOS 4 QEMU guest through amiga_mcp's
# devbench (REST API), holding the emulator lock and showing the
# "CLAUDE IS TESTING" banner for the whole session.
#
# Everything goes to RAM:speech; DEVS: and LIBS: get RAM:speech added with
# Assign ADD, ENV:Narrator/Capture makes narrator.device write PCM to a file
# instead of playing it. All of it is undone at the end.
#
#   tools/os4_test.py [--url http://localhost:3010] [--keep] [steps...]
#   steps: device (nartest), regress (render + pull + compare), say68k

import argparse, json, os, sys, time, urllib.request

SKIP_VOICE = False
PREFIX = "RAM:speech"          # where the programs/data are on the guest
DEVICE = "narrator.device"
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
AMIGA_MCP = os.path.join(os.path.dirname(ROOT), "amiga_mcp")
sys.path.insert(0, os.path.join(AMIGA_MCP, "scripts"))


class Bench:
    def __init__(self, url):
        self.url = url.rstrip("/")

    def _post(self, path, body, timeout=300):
        req = urllib.request.Request(self.url + path, json.dumps(body).encode(),
                                     {"Content-Type": "application/json"})
        with urllib.request.urlopen(req, timeout=timeout) as r:
            return json.loads(r.read().decode())

    def script(self, text, timeout=60):
        r = self._post("/api/script", {"script": text}, timeout=timeout + 30)
        return r.get("output", "") if r.get("status") == "ok" else "ERROR: %s" % r

    def push(self, src, dest):
        for attempt in range(3):
            try:
                r = self._post("/api/transfer", {"source": src, "dest": dest, "direction": "push"},
                               timeout=900)
            except Exception as e:          # devbench busy / slow guest
                r = {"message": repr(e)}
            if r.get("success"):
                return r
            print("  push retry:", r.get("message"))
            time.sleep(10)
        raise RuntimeError("push failed: %s" % src)

    def pull(self, src, dest):
        r = self._post("/api/transfer", {"source": src, "dest": dest, "direction": "pull"})
        if not r.get("success"):
            raise RuntimeError("pull failed: %s %s" % (src, r))
        return r


def setup_script():
    return ("makedir RAM:speech\n"
            "setenv Narrator/DataDir %s\n"
            "setenv Narrator/Capture RAM:speech/cap.raw\n"
            "assign DEVS: %s ADD\n"
            "assign LIBS: %s ADD\n" % (PREFIX, PREFIX, PREFIX))


def teardown_script():
    return ("unsetenv Narrator/DataDir\n"
            "unsetenv Narrator/Capture\n"
            "assign DEVS: %s REMOVE\n"
            "assign LIBS: %s REMOVE\n"
            "avail flush\n" % (PREFIX, PREFIX))


def crash_check(bench, step):
    out = bench.script("status")
    if "GrimReaper" in out:
        print("!!! %s: Grim Reaper is running (a crash) - see the guest screen" % step)
        return False
    return True


def files_to_push():
    b = os.path.join(ROOT, "os4", "build")
    out = [(os.path.join(b, f), "RAM:speech/" + f) for f in sorted(os.listdir(b)) if f.endswith(".device")]
    out.append((os.path.join(b, "nartest"), "RAM:speech/nartest"))
    if not SKIP_VOICE:
        out.append((os.path.join(ROOT, "data", "kal16.voice"), "RAM:speech/kal16.voice"))
    out.append((os.path.join(ROOT, "data", "english.dict"), "RAM:speech/english.dict"))
    out.append((os.path.join(ROOT, "os4", "speak", "SPEAK.mountlist"), "RAM:speech/SPEAK.mountlist"))
    for extra in ("translator.library", "trtest", "Say", "speak-handler"):
        p = os.path.join(b, extra)
        if os.path.exists(p):
            out.append((p, "RAM:speech/" + extra))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--url", default="http://localhost:3010")
    ap.add_argument("--keep", action="store_true", help="leave RAM:speech and assigns")
    ap.add_argument("--nopush", action="store_true")
    ap.add_argument("--novoice", action="store_true", help="kal16.voice is already in RAM:speech")
    ap.add_argument("--device", default="narrator.device")
    ap.add_argument("--staged", action="store_true",
                    help="use the files staged on DH1:speech by tools/stage_hdf.py")
    ap.add_argument("steps", nargs="*", default=["device"])
    a = ap.parse_args()
    from claude_busy import claude_testing
    global SKIP_VOICE, DEVICE, PREFIX
    SKIP_VOICE, DEVICE = a.novoice, a.device
    if a.staged:
        PREFIX = "DH1:speech"
        a.nopush = True
    bench = Bench(a.url)
    with claude_testing(a.url + "/mcp", "speech tests (narrator.device)"):
        print(bench.script(setup_script()))
        if not a.nopush:
            for src, dst in files_to_push():
                r = bench.push(src, dst)
                print("pushed", os.path.basename(src), r.get("message", ""))
        try:
            for step in a.steps:
                mod = STEPS[step]
                mod(bench)
                if not crash_check(bench, step):
                    break
        finally:
            if not a.keep:
                print(bench.script(teardown_script()))


def run_async(bench, cmd, log, limit=300, done_marker="JOBDONE"):
    """Run AmigaDOS lines (cmd, a string with newlines) in the background
    via a pushed script file with output to log; poll until JOBDONE.
    (The bridge turns ';' into line breaks, so the job goes in a file.)"""
    import tempfile
    lines = (cmd.rstrip("\n") + "\necho JOBDONE").split("\n")
    # Run's redirection does not reach the commands of an executed script:
    # redirect each line itself (unless it already redirects)
    job = "".join(l + ("" if ">" in l or not l.strip() or l.startswith("stack") else " >>" + log) + "\n"
                  for l in lines)
    fd, tmp = tempfile.mkstemp(suffix=".job")
    os.write(fd, job.replace("@P@", PREFIX).encode("latin-1"))
    os.close(fd)
    bench.push(tmp, "RAM:speech/job.txt")
    os.unlink(tmp)
    bench.script("delete %s QUIET" % log)
    bench.script("run >NIL: execute RAM:speech/job.txt")
    t0 = time.time()
    out = ""
    while time.time() - t0 < limit:
        time.sleep(4)
        out = bench.script("type %s" % log)
        if "JOBDONE" in out:
            return out.replace("JOBDONE", "").rstrip(), True
    return out, False


def step_device(bench):
    bench.script("delete RAM:speech/cap.raw QUIET")
    out, ok = run_async(bench, "stack 200000\n@P@/nartest DEVICE=%s" % DEVICE,
                        "RAM:speech/nt.log", limit=400)
    print(out)
    if not ok:
        print("nartest did not finish")


def step_translator(bench):
    out, ok = run_async(bench, "@P@/trtest", "RAM:speech/tr.log", limit=200)
    print(out)


def capture_say(bench, args, raw, timeout=200):
    """run Say with ENV:Narrator/Capture pointed at raw; returns its output"""
    out, ok = run_async(bench, "delete %s QUIET\nsetenv Narrator/Capture %s\n%s\n"
                        "setenv Narrator/Capture RAM:speech/cap.raw" % (raw, raw, args),
                        "RAM:speech/say.log", limit=timeout)
    return out


def step_say(bench):
    out = capture_say(bench, '@P@/Say "Hello, I am your Amiga. In 1985 it cost $1,295."',
                      "RAM:speech/say1.raw")
    print(out)
    print(bench.script("list RAM:speech/say1.raw"))
    out = capture_say(bench, '@P@/Say -f -s200 VOICE classic "This is the classic voice."',
                      "RAM:speech/say2.raw")
    print(out)
    print(bench.script("list RAM:speech/say2.raw"))
    # SPEAK: handler
    out, ok = run_async(bench, "delete RAM:speech/say3.raw QUIET\n"
                        "setenv Narrator/Capture RAM:speech/say3.raw\n"
                        "echo \"Handler = @P@/speak-handler\" >RAM:speech/SPEAK\n"
                        "echo \"StackSize = 65536\" >>RAM:speech/SPEAK\n"
                        "echo \"Priority = 5\" >>RAM:speech/SPEAK\n"
                        "echo \"GlobVec = -1\" >>RAM:speech/SPEAK\n"
                        "mount SPEAK: FROM RAM:speech/SPEAK\n"
                        "echo \"Hello from the speak handler. Is it working?\" >SPEAK:opt/s170\n"
                        "wait 2\n"
                        "setenv Narrator/Capture RAM:speech/cap.raw\n"
                        "list RAM:speech/say3.raw", "RAM:speech/sp.log", limit=200)
    print(out)


def step_regress(bench):
    import struct, wave, subprocess, tempfile
    sents = [l.strip() for l in open(os.path.join(ROOT, "tests/data/sentences.txt")) if l.strip()]
    tmp = tempfile.mkdtemp()
    worst = 0
    for i, s in enumerate(sents, 1):
        for voice in ("natural", "classic"):
            raw = "RAM:speech/r%02d%s.raw" % (i, voice[0])
            txt = s.replace('"', '')
            capture_say(bench, '@P@/Say VOICE %s "%s"' % (voice, txt), raw)
            local = os.path.join(tmp, "r%02d%s.raw" % (i, voice[0]))
            bench.pull(raw, local)
            data = open(local, "rb").read()
            guest = struct.unpack(">%dh" % (len(data) // 2), data)
            wav = os.path.join(tmp, "h.wav")
            subprocess.run([os.path.join(ROOT, "build/say"), "-q", "-v", voice, "-o", wav, txt], check=True)
            w = wave.open(wav)
            host = struct.unpack("<%dh" % w.getnframes(), w.readframes(w.getnframes()))
            n = min(len(host), len(guest))
            diff = max((abs(host[k] - guest[k]) for k in range(n)), default=0)
            worst = max(worst, diff if len(host) == len(guest) else 99999)
            print("%02d %-8s host %6d guest %6d samples, max diff %d" % (i, voice, len(host), len(guest), diff))
    print("regress: worst difference %d LSB (%s)" % (worst, "PASS" if worst <= 2 else "FAIL"))


def step_say68k(bench):
    say68k = "/Applications/AmiKit.app/Contents/SharedSupport/prefix/drive_c/AmiKit/RabbitHole/InstalledOS/Utilities/Say"
    if PREFIX == "RAM:speech":
        print(bench.push(say68k, "RAM:speech/Say68k").get("message"))
    print(bench.script("version %s/Say68k FULL" % PREFIX))
    out = capture_say(bench, '@P@/Say68k "Hello, I am your Amiga. Do you remember me?"',
                      "RAM:speech/say68.raw")
    print(out)
    print(bench.script("list RAM:speech/say68.raw"))
    out = capture_say(bench, '@P@/Say68k -f -s200 "The sixty eight thousand Say command talks to the new device."',
                      "RAM:speech/say68b.raw")
    print(bench.script("list RAM:speech/say68b.raw"))


STEPS = {"device": step_device, "translator": step_translator, "say": step_say,
         "regress": step_regress, "say68k": step_say68k}

if __name__ == "__main__":
    main()
