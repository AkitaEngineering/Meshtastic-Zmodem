#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
OUT="${TMPDIR:-/tmp}/akz-native-tests"
mkdir -p "$OUT"

CXX="${CXX:-g++}"
CXXFLAGS=(-std=c++17 -Wall -Wextra -Werror=return-type -I "$ROOT/tests/native/stubs" -I "$ROOT/src")

echo "Building ZModemEngine regression tests..."
"$CXX" "${CXXFLAGS[@]}" -o "$OUT/engine_reg" \
    "$ROOT/tests/native/ZModemEngineRegression.cpp" \
    "$ROOT/src/utility/ZModemEngine.cpp"
"$OUT/engine_reg"

echo "Building AkitaMeshZmodem transport regression tests..."
"$CXX" "${CXXFLAGS[@]}" -o "$OUT/transport_reg" \
    "$ROOT/tests/native/AkitaMeshZmodemTransportRegression.cpp" \
    "$ROOT/src/AkitaMeshZmodem.cpp" \
    "$ROOT/src/utility/ZModemEngine.cpp"
"$OUT/transport_reg"

echo "All native tests passed."
