#!/bin/sh
# Build and test a clean clone of the current commit, so the golden hashes
# never come from a stale incremental build. Usage: sh tests/clean_check.sh
set -e
top=$(git rev-parse --show-toplevel)
tmp=$(mktemp -d)
git clone -q "$top" "$tmp/repo"
git -C "$tmp/repo" checkout -q "$(git -C "$top" rev-parse HEAD)"
make -C "$tmp/repo" -s >/dev/null
make -C "$tmp/repo" test
rm -rf "$tmp"
