#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$(mktemp)"
trap 'rm -f "$OUT"' EXIT
g++ -std=c++17 -Wall -Wextra -Werror \
  -I "$ROOT/tests/stubs" \
  "$ROOT/src/rds/RdsDecoder.cpp" \
  "$ROOT/tests/rds_decoder_vectors.cpp" \
  -o "$OUT"
"$OUT"
