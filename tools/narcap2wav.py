#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Chris Collins <chris@hitorro.com>
#
# Convert narcap (tools/narcap68k) capture records into 16-bit WAV files.
#   narcap2wav.py in.raw out.wav
# Records: u32 length, u16 period, u16 unit mask, length bytes of signed
# 8-bit samples (padded to even), all big-endian. Narrator writes the same
# buffer to a left and a right channel; consecutive records with identical
# data are kept once.

import struct, sys, wave, collections

def main():
    data = open(sys.argv[1], "rb").read()
    pos, recs = 0, []
    while pos + 8 <= len(data):
        n, period, unit = struct.unpack(">IHH", data[pos:pos + 8])
        samp = data[pos + 8:pos + 8 + n]
        pos += 8 + ((n + 1) & ~1)
        recs.append((period, unit, samp))
    units = collections.Counter(u for _, u, _ in recs)
    out, last = bytearray(), None
    # keep one channel: the lowest unit mask seen
    keep = min(units) if units else 0
    for period, unit, samp in recs:
        if unit != keep:
            continue
        for b in samp:
            v = b - 256 if b > 127 else b
            out += struct.pack("<h", v * 256)
    periods = collections.Counter(p for p, _, _ in recs)
    period = periods.most_common(1)[0][0] if periods else 161
    # NTSC colour clock (FS-UAE's A1200 setting decides; narrator asks for
    # 22200 Hz which is period 161 on NTSC, 160 on PAL)
    rate = round(3579545 / period) if period else 22200
    w = wave.open(sys.argv[2], "wb")
    w.setnchannels(1); w.setsampwidth(2); w.setframerate(rate)
    w.writeframes(bytes(out))
    w.close()
    print("%s: %d records, units %s, period %d -> %d Hz, %.2f s" %
          (sys.argv[2], len(recs), dict(units), period, rate, len(out) / 2 / rate))

if __name__ == "__main__":
    main()
