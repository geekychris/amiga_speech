#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Chris Collins <chris@hitorro.com>
#
# TEST ORACLE ONLY - never part of the product. Transcribes the rendered
# samples with a local Whisper model (faster-whisper) and reports word error
# rate per voice. Reference and hypothesis are both passed through our own
# text normaliser (build/translate -n) so "1985" and "nineteen eighty five"
# compare equal.
#
#   python3 tests/intelligibility.py [--model base.en] [--json out.json]

import argparse, json, os, re, subprocess, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

def norm(text):
    out = subprocess.run([os.path.join(ROOT, "build", "translate"), "-n", text],
                         capture_output=True, text=True).stdout
    words = []
    for w in out.split():
        if w in {".", ",", "?", "-"}:
            continue
        w = w.lstrip("#").replace("'", "")
        words.append(w)
    return words

def wer(ref, hyp):
    d = [[0] * (len(hyp) + 1) for _ in range(len(ref) + 1)]
    for i in range(len(ref) + 1): d[i][0] = i
    for j in range(len(hyp) + 1): d[0][j] = j
    for i in range(1, len(ref) + 1):
        for j in range(1, len(hyp) + 1):
            d[i][j] = min(d[i-1][j] + 1, d[i][j-1] + 1,
                          d[i-1][j-1] + (ref[i-1] != hyp[j-1]))
    return d[len(ref)][len(hyp)]

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--model", default="base.en")
    ap.add_argument("--json")
    ap.add_argument("--voices", default="natural,classic,classic_nrl")
    ap.add_argument("--set", default="sentences", help="sentences | harvard")
    ap.add_argument("--dir", default="samples")
    ap.add_argument("--quiet", action="store_true")
    a = ap.parse_args()
    from faster_whisper import WhisperModel
    m = WhisperModel(a.model, device="cpu", compute_type="int8")
    sents = [l.strip() for l in open(os.path.join(ROOT, "tests/data/%s.txt" % a.set)) if l.strip()]
    res = {}
    for v in a.voices.split(","):
        errs = total = 0
        rows = []
        for i, s in enumerate(sents, 1):
            f = os.path.join(ROOT, a.dir, "%02d_%s.wav" % (i, v))
            if not os.path.exists(f):
                continue
            segs, _ = m.transcribe(f, language="en", beam_size=5, temperature=0.0,
                                   condition_on_previous_text=False)
            hyp = " ".join(x.text.strip() for x in segs)
            r, h = norm(s), norm(hyp)
            e = wer(r, h)
            errs += e; total += len(r)
            rows.append({"n": i, "ref": s, "hyp": hyp, "errors": e, "words": len(r)})
            if not a.quiet or e:
                print("%-12s %02d %5.1f%%  %s" % (v, i, 100.0 * e / max(1, len(r)), hyp))
        if total:
            res[v] = {"wer": errs / total, "errors": errs, "words": total, "rows": rows}
            print("%-12s WER %.1f%% (%d/%d)\n" % (v, 100.0 * errs / total, errs, total))
    if a.json:
        json.dump({"model": a.model, "results": res}, open(a.json, "w"), indent=1)

if __name__ == "__main__":
    main()
