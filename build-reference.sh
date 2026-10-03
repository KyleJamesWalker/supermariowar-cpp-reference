#!/usr/bin/env bash
# Builds the deterministic C++ reference used by supermariowar-rust's parity tools.
set -euo pipefail
root="$(cd "$(dirname "$0")" && pwd)"
git -C "$root" submodule update --init
cmake -S "$root" -B "$root/build" -DNO_NETWORK=ON -DBUILD_TESTS=OFF -DCMAKE_BUILD_TYPE=Release \
    -DDISABLE_DEFAULT_CFLAGS=ON -DCMAKE_CXX_FLAGS="-O2 -ffp-contract=off"
cmake --build "$root/build" --target smw -j"$(sysctl -n hw.ncpu 2>/dev/null || nproc)"
echo "built $root/build/smw"
