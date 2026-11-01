#!/bin/sh
# Render tests/data/sentences.txt with both voices into samples/.
set -e
cd "$(dirname "$0")/.."
mkdir -p samples
i=0
while IFS= read -r line; do
    [ -z "$line" ] && continue
    i=$((i + 1))
    n=$(printf "%02d" $i)
    build/say -q -v natural -o samples/${n}_natural.wav "$line"
    build/say -q -v classic -o samples/${n}_classic.wav "$line"
    build/say -q -v classic -t classic -o samples/${n}_classic_nrl.wav "$line"
done < tests/data/sentences.txt
echo "wrote $i sentences x 3 renderings to samples/"
