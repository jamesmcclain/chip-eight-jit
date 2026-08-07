#!/bin/sh
# Compile an immutable CHIP-8 ROM to a native AOT executable.
# Prerequisite: `make -C src chip8-aot aot-runtime-objs` has already run.
set -eu

if [ "$#" -ne 2 ]; then
    echo "Usage: $0 ROM OUTPUT" >&2
    exit 64
fi

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
rom=$1
output=$2
compiler="$root/src/chip8-aot"
clang=${CLANG:-clang}

for file in "$compiler" "$root/src/aot_runtime.o" "$root/src/chip8.o" "$root/src/ncurses_io.o"; do
    if [ ! -e "$file" ]; then
        echo "aot_compile.sh: missing $file; run make first" >&2
        exit 1
    fi
done

ir=$(mktemp "${TMPDIR:-/tmp}/chip8-aot.XXXXXX.ll")
trap 'rm -f "$ir"' EXIT HUP INT TERM
"$compiler" "$rom" -o "$ir"
"$clang" "$ir" "$root/src/aot_runtime.o" "$root/src/chip8.o" \
    "$root/src/ncurses_io.o" -lncurses -o "$output"
