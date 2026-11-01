#!/bin/sh
# Regression check: fixed inputs must produce byte-identical WAVs.
# Hashes live in tests/data/golden.sha256; regenerate deliberately with
#   UPDATE=1 sh tests/regress.sh
# Note: float results can differ across CPUs/compilers/libm, so the golden
# file is per platform (this one was made on macOS arm64, Apple clang).
set -e
cd "$(dirname "$0")/.."
out=build/regress
rm -rf $out
mkdir -p $out
T="Hello, I am your Amiga. In 1985, it cost \$1,295 -- do you remember?"
build/say -q -v natural -o $out/natural.wav "$T"
build/say -q -v classic -o $out/classic.wav "$T"
build/say -q -v classic -t classic -o $out/classic_nrl.wav "$T"
build/say -q -v classic -m robotic -s female -p 180 -r 200 -o $out/classic_robot_f.wav "$T"
build/say -q -v natural -m manual -e 64 -P 80 -f 16000 -o $out/natural_manual.wav "$T"
build/say -q -v classic -c 60:AO -a 150 -o $out/classic_central.wav "$T"
build/translate "$T" > $out/translate.txt
build/translate -c "$T" > $out/translate_classic.txt
(cd $out && shasum -a 256 *.wav *.txt) > $out/now.sha256
if [ -n "$UPDATE" ]; then
    cp $out/now.sha256 tests/data/golden.sha256
    echo "regress: golden hashes updated"
elif diff -u tests/data/golden.sha256 $out/now.sha256; then
    echo "regress: all outputs identical to golden"
else
    echo "regress: OUTPUT CHANGED (UPDATE=1 to accept)"; exit 1
fi
# determinism within this build
build/say -q -v natural -o build/regress_repeat.wav "$T"
cmp -s $out/natural.wav build/regress_repeat.wav && echo "regress: repeat render identical"
