#!/bin/sh
set -eu
zx_root=$(cd "$(dirname "$0")/.." && pwd)
zx_source=${1:-$zx_root}
zx_build="$zx_root/tests/.build/benchmark"
mkdir -p "$zx_build"
${CC:-cc} -std=c99 -O2 -c "$zx_source/src/vendor/z80.c" -o "$zx_build/z80.o"
${CXX:-c++} -std=c++17 -O2 -I "$zx_source/src/core" "$zx_root/tests/benchmark.cpp" \
  "$zx_source/src/core/Spectrum.cpp" "$zx_source/src/core/Tape.cpp" "$zx_build/z80.o" -o "$zx_build/run"
"$zx_build/run"
