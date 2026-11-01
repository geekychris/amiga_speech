#!/bin/sh
# Build the AmigaOS 4 parts in the walkero GCC 11 container.
cd "$(dirname "$0")/.." || exit 1
IMG=${IMG:-walkero/amigagccondocker:os4-gcc11}
exec docker run --rm -v "$PWD":/work -w /work/os4 "$IMG" make "$@"
