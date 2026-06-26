#!/usr/bin/env bash
# Builds projectM-4 as static libs for the retro-handheld (arm64/GLES) target.
# Override CC/CXX/CMAKE_TOOLCHAIN_FILE in the environment for cross builds.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
SRC="$HERE/src"
BUILD="$HERE/build"
OUT_LIB="$HERE/lib"
OUT_INC="$HERE/include"

rm -rf "$BUILD"
cmake -S "$SRC" -B "$BUILD" \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_SHARED_LIBS=OFF \
  -DENABLE_GLES=ON \
  -DENABLE_PLAYLIST=OFF \
  -DBUILD_TESTING=OFF \
  -DENABLE_SDL_UI=OFF \
  ${CMAKE_TOOLCHAIN_FILE:+-DCMAKE_TOOLCHAIN_FILE="$CMAKE_TOOLCHAIN_FILE"}
cmake --build "$BUILD" --parallel

mkdir -p "$OUT_LIB" "$OUT_INC"
find "$BUILD" -name 'libprojectM-4.a' -exec cp {} "$OUT_LIB/" \;
# libprojectM_eval.a may be folded into libprojectM-4.a in some build
# configurations; copy it only if it exists as a separate archive.
find "$BUILD" -name 'libprojectM_eval.a' -exec cp {} "$OUT_LIB/" \;
# Headers: prefer the export headers from the source tree.
cp -r "$SRC/src/api/include/projectM-4" "$OUT_INC/" 2>/dev/null || \
  cp -r "$SRC/include/projectM-4" "$OUT_INC/" 2>/dev/null || true
echo "projectM build complete:"
ls -l "$OUT_LIB"
