#!/usr/bin/env bash
# Dockerized cross-build of the Rockbox PortMaster pak (projectM-4 + Rockbox).
#
# Produces an aarch64 PortMaster zip reproducibly, using only Docker -- no
# toolchain on the host. The build runs in a native-aarch64 Debian Bookworm
# container (gcc-12), which sidesteps the host gcc-16 ICE on libopus/celt/vq.c.
# On an x86_64 host the container runs under qemu emulation: correct but slow
# (expect many minutes -- 30+ for a full clean build).
#
# Usage:
#   ./build-portmaster.sh             # build (reuses cached image + build dir)
#   ./build-portmaster.sh --rebuild   # force a rebuild of the Docker image
#   ./build-portmaster.sh --clean     # wipe build-portmaster/ before building
#   ./build-portmaster.sh --clean --rebuild
#
# Output: Rockbox.pak.zip at the repo root (the PortMaster release filename,
#         per packaging/retro-handheld/pak.json "release_filename").
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT"

IMAGE="rockbox-portmaster-builder"
DOCKERFILE="Dockerfile.portmaster"
BUILD_DIR="build-portmaster"
PLATFORM="linux/arm64"
OUTPUT="Rockbox.pak.zip"      # final pak name (pak.json release_filename)
ZIP_IN_BUILD="rockbox.zip"    # name emitted by the portmaster-zip make target

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

# --- 1. Docker availability + arm64 emulation ------------------------------
if ! command -v docker >/dev/null 2>&1; then
  echo "!! docker not found on PATH. Install Docker and retry." >&2
  exit 1
fi

echo ">> Checking arm64 (qemu-aarch64) binfmt registration ..."
if [ -e /proc/sys/fs/binfmt_misc/qemu-aarch64 ]; then
  echo ">> qemu-aarch64 already registered -- no binfmt setup needed."
else
  echo ">> qemu-aarch64 not registered; attempting to install via tonistiigi/binfmt ..."
  if docker run --privileged --rm tonistiigi/binfmt --install arm64; then
    echo ">> arm64 emulation registered."
  else
    echo "!! Could not register arm64 emulation (needs privileged Docker)." >&2
    echo "!! On an aarch64 host this is harmless. On x86_64 the build will fail" >&2
    echo "!! until qemu-aarch64 is registered (run the binfmt step with rights)." >&2
  fi
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
[ "$CLEAN" -eq 1 ] && echo ">> --clean: build dir will be wiped inside the container."

NPROC_CMD='$(nproc)'
docker run --rm \
  --platform "$PLATFORM" \
  --user "$(id -u):$(id -g)" -e HOME=/tmp \
  -v "$ROOT":/src -w /src \
  "$IMAGE" bash -euo pipefail -c "
  echo '>> Marking /src as a safe git directory ...'
  git config --global --add safe.directory /src
  git config --global --add safe.directory /src/lib/projectm/src

  echo '>> Ensuring submodules (projectM source) are present ...'
  git submodule update --init --recursive

  echo '>> Building projectM-4 (native aarch64 static libs) ...'
  ./lib/projectm/build-projectm.sh
  echo '>> projectM archives:'
  ls -l lib/projectm/lib/*.a

  # Rockbox builds its host tools (convbdf, bmp2rb, ...) in-place under tools/.
  # A prior NATIVE (e.g. x86_64) build leaves those binaries in the source tree;
  # make would see them up-to-date and execute them under aarch64 -> 'Error 127'.
  # Remove them so they are rebuilt for this container's architecture.
  echo '>> Removing any stale host-arch build tools (rebuilt for aarch64) ...'
  git clean -fdx -- tools/ || true

  if [ ${CLEAN} -eq 1 ]; then
    echo '>> --clean: wiping ${BUILD_DIR} ...'
    rm -rf ${BUILD_DIR}
  fi

  echo '>> Configuring + building Rockbox (retro-handheld, normal build) ...'
  mkdir -p ${BUILD_DIR} && cd ${BUILD_DIR}
  [ -f Makefile ] || ../tools/configure --target=retro-handheld --type=n
  make -j${NPROC_CMD}

  echo '>> Assembling PortMaster pak (rhbuild -> portmaster-zip) ...'
  # retro-handheld.make is auto-included by tools/root.make for this target,
  # so its targets/vars (ROOTDIR, RH_ROCKBOX_DIR, ...) resolve from the build
  # dir. rhbuild lays out pkgbuild/rockbox; portmaster-zip packs it -> ${ZIP_IN_BUILD}.
  # rhclean first: rhbuild does a bare 'mkdir pkgbuild' that fails if a previous
  # (incremental) run left the dir behind.
  make rhclean
  make rhbuild
  make portmaster-zip
  echo '>> PortMaster zip produced:'
  ls -l ${ZIP_IN_BUILD}
"

# --- 4. Collect the output --------------------------------------------------
SRC_ZIP="$ROOT/$BUILD_DIR/$ZIP_IN_BUILD"
if [ ! -f "$SRC_ZIP" ]; then
  echo "!! Expected zip not found at $SRC_ZIP" >&2
  exit 1
fi
cp -f "$SRC_ZIP" "$ROOT/$OUTPUT"
echo ">> Done."
echo ">> PortMaster pak: $ROOT/$OUTPUT ($(du -h "$ROOT/$OUTPUT" | cut -f1))"
