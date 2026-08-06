#!/usr/bin/env bash
set -euo pipefail

gccjit_dir=$(dirname "$(find /usr/lib/gcc -name libgccjit.so 2>/dev/null | head -1)")
: "${gccjit_dir:?libgccjit.so not found}"
export CPPFLAGS="-I$gccjit_dir/include${CPPFLAGS:+ $CPPFLAGS}"
export LDFLAGS="-L$gccjit_dir${LDFLAGS:+ $LDFLAGS}"
make -C src bench >/dev/null
python3 scripts/bench_diff.py --instructions 1000000 >/dev/null
