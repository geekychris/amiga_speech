#!/bin/sh
# render_set.sh SET OUTDIR [extra say options]: render tests/data/SET.txt
# with the natural, classic and classic+NRL configurations.
set -e
cd "$(dirname "$0")/.."
set_name=$1; out=$2; shift 2
mkdir -p "$out"
i=0
while IFS= read -r line; do
    [ -z "$line" ] && continue
    i=$((i + 1)); n=$(printf "%02d" $i)
    build/say -q -v natural "$@" -o "$out/${n}_natural.wav" "$line"
    build/say -q -v classic "$@" -o "$out/${n}_classic.wav" "$line"
    build/say -q -v classic -t classic "$@" -o "$out/${n}_classic_nrl.wav" "$line"
done < "tests/data/$set_name.txt"
