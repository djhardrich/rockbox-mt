#!/usr/bin/env bash
# Dockerized native-amd64 build of the Rockbox PanicOS-x64 PortMaster pak
# (projectM-4 + Rockbox), for PanicOS / ROCKNIX x64 handhelds.
#
# Reuses the arch-agnostic `retro-handheld` target: building in an amd64 Debian
# Bookworm container (gcc-12) produces an x86_64 rockbox binary + x86_64 projectM
# static libs. On an x86_64 host this is native -- no qemu, so it is fast.
#
# Usage:
#   ./build-panicos-x64.sh             # build (reuses cached image + build dir)
#   ./build-panicos-x64.sh --rebuild   # force a rebuild of the Docker image
#   ./build-panicos-x64.sh --clean     # wipe build-panicos-x64/ before building
#   ./build-panicos-x64.sh --clean --rebuild
#
# Output: Rockbox-x64.pak.zip at the repo root (the x86_64 PortMaster build).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT"

IMAGE="rockbox-panicos-x64-builder"
DOCKERFILE="Dockerfile.panicos-x64"
BUILD_DIR="build-panicos-x64"
CCACHE_DIR="$HOME/.cache/rockbox-panicos-x64-ccache"  # persists across runs/containers
PLATFORM="linux/amd64"
OUTPUT="Rockbox-x64.pak.zip"    # final pak name (x86_64 PortMaster build)
ZIP_IN_BUILD="rockbox-x64.zip"  # name emitted by the portmaster-x64-zip make target

REBUILD=0
CLEAN=0
for arg in "$@"; do
  case "$arg" in
    --rebuild) REBUILD=1 ;;
    --clean)   CLEAN=1 ;;
    -h|--help) sed -n '2,20p' "$0"; exit 0 ;;
    *) echo "!! Unknown argument: $arg" >&2; exit 2 ;;
  esac
done

# --- 1. Docker availability -------------------------------------------------
# Native amd64 build: no qemu/binfmt registration needed (unlike the arm64 pak).
if ! command -v docker >/dev/null 2>&1; then
  echo "!! docker not found on PATH. Install Docker and retry." >&2
  exit 1
fi

# --- 2. Build (or reuse) the builder image ---------------------------------
if [ "$REBUILD" -eq 1 ] || ! docker image inspect "$IMAGE" >/dev/null 2>&1; then
  echo ">> Building image $IMAGE (platform $PLATFORM) ..."
  docker build --platform "$PLATFORM" -t "$IMAGE" -f "$DOCKERFILE" .
else
  echo ">> Reusing cached image $IMAGE (pass --rebuild to force a rebuild)."
fi

# --- 3. Run the full build inside the container ----------------------------
# Mounted as the host user so emitted files are owned by you, not root.
# HOME=/tmp gives git/cmake a writable home. safe.directory avoids git's
# "dubious ownership" refusal on the bind-mounted, foreign-owned tree.
# CCACHE_DIR is a host-side directory bound into the container so compiled
# objects survive across `--rm` containers.
[ "$CLEAN" -eq 1 ] && echo ">> --clean: build dir will be wiped inside the container."
mkdir -p "$CCACHE_DIR"

NPROC_CMD='$(nproc)'
docker run --rm \
  --platform "$PLATFORM" \
  --user "$(id -u):$(id -g)" -e HOME=/tmp \
  -e CCACHE_DIR=/ccache \
  -v "$ROOT":/src -w /src \
  -v "$CCACHE_DIR":/ccache \
  "$IMAGE" bash -euo pipefail -c "
  echo '>> Marking /src as a safe git directory ...'
  git config --global --add safe.directory /src
  git config --global --add safe.directory /src/lib/projectm/src

  echo '>> Ensuring submodules (projectM source) are present ...'
  git submodule update --init --recursive

  echo '>> Building projectM-4 (native x86_64 static libs) ...'
  ./lib/projectm/build-projectm.sh
  echo '>> projectM archives:'
  ls -l lib/projectm/lib/*.a

  # Rockbox builds its host tools (convbdf, bmp2rb, ...) in-place under tools/.
  # A prior build for a different arch (e.g. the arm64 pak) leaves those binaries
  # in the source tree; make would see them up-to-date and execute the wrong-arch
  # binary. Remove them so they are rebuilt for this container's architecture.
  echo '>> Removing any stale host-arch build tools (rebuilt for x86_64) ...'
  git clean -fdx -- tools/ || true

  if [ ${CLEAN} -eq 1 ]; then
    echo '>> --clean: wiping ${BUILD_DIR} ...'
    rm -rf ${BUILD_DIR}
  fi

  echo '>> Configuring + building Rockbox (retro-handheld, normal build) ...'
  mkdir -p ${BUILD_DIR} && cd ${BUILD_DIR}
  [ -f Makefile ] || ../tools/configure --target=retro-handheld --type=n
  make -j${NPROC_CMD}

  echo '>> Assembling PanicOS-x64 pak (rhbuild -> portmaster-x64-zip) ...'
  make rhclean
  make rhbuild
  make portmaster-x64-zip
  echo '>> PanicOS-x64 zip produced:'
  ls -l ${ZIP_IN_BUILD}

  echo '>> ccache stats:'
  ccache -s
"

# --- 4. Collect the output --------------------------------------------------
SRC_ZIP="$ROOT/$BUILD_DIR/$ZIP_IN_BUILD"
if [ ! -f "$SRC_ZIP" ]; then
  echo "!! Expected zip not found at $SRC_ZIP" >&2
  exit 1
fi
cp -f "$SRC_ZIP" "$ROOT/$OUTPUT"
echo ">> Done."
echo ">> PanicOS-x64 pak: $ROOT/$OUTPUT ($(du -h "$ROOT/$OUTPUT" | cut -f1))"
