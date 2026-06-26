#!/usr/bin/env bash
# Builds projectM-4 as static libs for the retro-handheld (arm64/GLES) target.
# Override CC/CXX/CMAKE_TOOLCHAIN_FILE in the environment for cross builds.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
SRC="$HERE/src"
BUILD="$HERE/build"
OUT_LIB="$HERE/lib"
OUT_INC="$HERE/include"

if [ ! -f "$SRC/CMakeLists.txt" ]; then
  echo "ERROR: $SRC/CMakeLists.txt missing. Run: git submodule update --init --recursive" >&2
  exit 1
fi

# Apply the rockbox patches to the projectM submodule (idempotent).  These are
# re-applied on every build because `git submodule update` resets the submodule
# working tree to the pinned commit, discarding any previously-applied patch.
#   patches/0001-composite-into-bound-fbo.patch : make projectm_opengl_render_frame
#     composite into the caller's bound draw FBO (we render at 1/4 res into an
#     off-screen FBO, then upscale) instead of the hard-coded default framebuffer.
shopt -s nullglob
for p in "$HERE"/patches/*.patch; do
  name="$(basename "$p")"
  if git -C "$SRC" apply --reverse --check "$p" >/dev/null 2>&1; then
    echo "projectM patch already applied: $name"
  elif git -C "$SRC" apply --check "$p" >/dev/null 2>&1; then
    git -C "$SRC" apply "$p"
    echo "projectM patch applied: $name"
  else
    echo "ERROR: projectM patch does not apply cleanly: $name" >&2
    exit 1
  fi
done
shopt -u nullglob

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
  cp -r "$SRC/include/projectM-4" "$OUT_INC/"
# CMake generates projectM_export.h and version.h into the build tree (not the
# source tree), so the copy above misses them.  Pull any generated headers from
# the build tree to complete the public include set.
find "$BUILD" -path '*/projectM-4/projectM_export.h' -exec cp {} "$OUT_INC/projectM-4/" \;
find "$BUILD" -path '*/projectM-4/version.h'         -exec cp {} "$OUT_INC/projectM-4/" \;
echo "projectM build complete:"
ls -l "$OUT_LIB"
