#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
FTX="$ROOT/src/digital/ftx"
LIB="$FTX/ft8_lib"
BUILD="$(mktemp -d)"
trap 'rm -rf "$BUILD"' EXIT
CC="$(command -v cc)"
CXX="$(command -v c++)"
for source in \
  "$LIB/ft8/constants.c" "$LIB/ft8/crc.c" "$LIB/ft8/decode.c" \
  "$LIB/ft8/encode.c" "$LIB/ft8/ldpc.c" "$LIB/ft8/message.c" \
  "$LIB/ft8/text.c" "$LIB/common/monitor.c" "$LIB/fft/kiss_fft.c" "$LIB/fft/kiss_fftr.c"; do
  object="$BUILD/$(basename "$source" .c).o"
  "$CC" -std=c11 -O2 -w -I"$LIB" -c "$source" -o "$object"
done
"$CXX" -std=c++17 -O2 -Wall -Wextra -Werror \
  -I"$ROOT/tests/stubs" -I"$FTX" -I"$LIB" \
  "$FTX/FtxDecoder.cpp" "$ROOT/tests/ftx_decoder_vectors.cpp" "$BUILD"/*.o -lm -o "$BUILD/ftx_decoder_vectors"
"$BUILD/ftx_decoder_vectors"
