#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Chris Collins <chris@hitorro.com>
#
# Stage the OS4 test files into DH1:speech/ on the dev HDF (never the
# system HDF, SYS:, DEVS: or LIBS:): holding amiga_mcp's emulator lock,
# stop QEMU, delete-then-write each file with xdftool, start QEMU, wait for
# the bridge and make sure DH1:launcher is running.

import os, subprocess, sys, time, json, urllib.request

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(os.path.dirname(ROOT), "amiga_mcp", "scripts"))
from claude_busy import claude_testing

URL = "http://localhost:3010"
HDF = os.path.expanduser("~/AmigaOS4/amigaos4-dev.hdf")


def post(path, body=None, timeout=180):
    req = urllib.request.Request(URL + path, json.dumps(body or {}).encode(),
                                 {"Content-Type": "application/json"})
    with urllib.request.urlopen(req, timeout=timeout) as r:
        return json.loads(r.read().decode())


def files(extra):
    b = os.path.join(ROOT, "os4", "build")
    out = [os.path.join(ROOT, "data", "kal16.voice"), os.path.join(ROOT, "data", "english.dict")]
    out += [os.path.join(b, f) for f in ("narrator.device", "translator.library", "Say",
                                         "speak-handler", "nartest", "trtest", "taskinfo")]
    return out + extra


def xdf(*args, check=True):
    r = subprocess.run(["xdftool", HDF, "open", "part=0", "+"] + list(args),
                       capture_output=True, text=True)
    if check and r.returncode:
        raise RuntimeError("xdftool %s: %s" % (args, r.stderr.strip()))
    return r.stdout


def main():
    extra = []           # (local, name) pairs given as local=name
    for a in sys.argv[1:]:
        extra.append(a)
    with claude_testing(URL + "/mcp", "speech: staging files on DH1"):
        print("stop:", post("/api/emulator/stop"))
        for _ in range(30):
            if subprocess.run(["pgrep", "-f", "qemu-system-ppc"], capture_output=True).returncode:
                break
            time.sleep(2)
        else:
            raise SystemExit("QEMU still running, not touching the HDF")
        if "speech" not in xdf("list", check=False):
            xdf("makedir", "speech")
        for f in files([]) + extra:
            local, name = (f.split("=", 1) + [None])[:2] if "=" in f else (f, os.path.basename(f))
            xdf("delete", "speech/" + name, check=False)
            xdf("write", local, "speech/" + name)
            print("wrote", name, os.path.getsize(local))
        print(xdf("list", "speech"))
        print("start:", post("/api/emulator/start"))
        for i in range(60):
            time.sleep(5)
            try:
                r = post("/api/script", {"script": "status"}, timeout=40)
            except Exception:
                continue
            if r.get("status") == "ok":
                if "DH1:launcher" not in r.get("output", ""):
                    print("launcher not running, starting it")
                    post("/api/script", {"script": "run >NIL: DH1:launcher"}, timeout=40)
                print("guest up:", r.get("output", "")[:300])
                break
        else:
            print("WARNING: bridge did not come back")


if __name__ == "__main__":
    main()
