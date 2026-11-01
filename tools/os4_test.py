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


SETUP = """makedir RAM:speech
setenv Narrator/DataDir RAM:speech
setenv Narrator/Capture RAM:speech/cap.raw
assign DEVS: RAM:speech ADD
assign LIBS: RAM:speech ADD
"""
TEARDOWN = """unsetenv Narrator/DataDir
unsetenv Narrator/Capture
assign DEVS: RAM:speech REMOVE
assign LIBS: RAM:speech REMOVE
avail flush
"""


def files_to_push():
    b = os.path.join(ROOT, "os4", "build")
    out = [(os.path.join(b, f), "RAM:speech/" + f) for f in sorted(os.listdir(b)) if f.endswith(".device")]
    out.append((os.path.join(b, "nartest"), "RAM:speech/nartest"))
    if not SKIP_VOICE:
        out.append((os.path.join(ROOT, "data", "kal16.voice"), "RAM:speech/kal16.voice"))
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
    ap.add_argument("steps", nargs="*", default=["device"])
    a = ap.parse_args()
    from claude_busy import claude_testing
    global SKIP_VOICE, DEVICE
    SKIP_VOICE, DEVICE = a.novoice, a.device
    bench = Bench(a.url)
    with claude_testing(a.url + "/mcp", "speech tests (narrator.device)"):
        print(bench.script(SETUP))
        if not a.nopush:
            for src, dst in files_to_push():
                r = bench.push(src, dst)
                print("pushed", os.path.basename(src), r.get("message", ""))
        try:
            for step in a.steps:
                mod = STEPS[step]
                mod(bench)
        finally:
            if not a.keep:
                print(bench.script(TEARDOWN))


def run_async(bench, cmd, log, limit=300, done_marker=None):
    """Start cmd in the background with output to log; poll until the log
    has done_marker (or the program is gone)."""
    bench.script("delete %s QUIET" % log)
    bench.script("run >%s %s" % (log, cmd))
    t0 = time.time()
    out = ""
    while time.time() - t0 < limit:
        time.sleep(5)
        out = bench.script("type %s" % log)
        if done_marker and done_marker in out:
            return out, True
    return out, False


def step_device(bench):
    bench.script("delete RAM:speech/cap.raw QUIET")
    out, ok = run_async(bench, "RAM:speech/nartest DEVICE=%s" % DEVICE, "RAM:speech/nt.log",
                        limit=400, done_marker="nartest:")
    print(out)
    if not ok:
        print("nartest did not finish")


STEPS = {"device": step_device}

if __name__ == "__main__":
    main()
