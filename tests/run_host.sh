#!/bin/sh
set -eu
zx_root=$(cd "$(dirname "$0")/.." && pwd)
mkdir -p "$zx_root/tests/.build"
zx_cc=${CC:-cc}
zx_cxx=${CXX:-c++}
"$zx_cc" -std=c99 -O2 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
  -c "$zx_root/src/vendor/z80.c" -o "$zx_root/tests/.build/z80.o"
"$zx_cxx" -std=c++17 -O2 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer \
  "$zx_root/tests/core.cpp" "$zx_root/src/core/Spectrum.cpp" \
  "$zx_root/src/core/Tape.cpp" "$zx_root/src/core/Snapshot.cpp" \
  "$zx_root/tests/.build/z80.o" -o "$zx_root/tests/.build/core"
"$zx_root/tests/.build/core" "$zx_root/tests/.build/synthetic-screen.ppm"
