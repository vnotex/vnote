#!/bin/bash
#
# local_run.sh - Build (Debug) then launch VNote with the right runtime env.
# Usage: ./local_run.sh [extra vnote args...]
#
set -e

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
QT_DIR="${QT_DIR:-$HOME/Qt/6.9.3/gcc_64}"
BUILD_DIR="$ROOT/build-debug"
BIN="$BUILD_DIR/src/vnote"

export PATH="$QT_DIR/bin:$PATH"
export LD_LIBRARY_PATH="$ROOT/build-debug/libs/vtextedit/src:$QT_DIR/lib:${LD_LIBRARY_PATH:-}"

if [ ! -f "$BUILD_DIR/build.ninja" ]; then
  echo "Error: $BUILD_DIR not configured. Run: cmake -B build-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug" >&2
  exit 1
fi

ninja -C "$BUILD_DIR" vnote

exec "$BIN" "$@"
