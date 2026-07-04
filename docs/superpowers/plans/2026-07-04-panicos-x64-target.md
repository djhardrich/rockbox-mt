# PanicOS-x64 (x86_64 PortMaster pak) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Produce an x86_64 PortMaster pak (`Rockbox-x64.pak.zip`) for PanicOS / ROCKNIX x64 handhelds by reusing the arch-agnostic `retro-handheld` target in an amd64 Docker build.

**Architecture:** No `tools/configure` or C/C++ source changes. Clone the two arm64 build files (`Dockerfile.portmaster`, `build-portmaster.sh`) into amd64 variants, add an x64 `port.json` metadata overlay, and add a `portmaster-x64-zip` make target that reuses the existing `portmaster` layout and swaps the arch metadata. projectM and Rockbox compile natively for x86_64 in the amd64 container.

**Tech Stack:** Docker (native linux/amd64, no qemu), Debian bookworm gcc-12, cmake (projectM-4), GNU make, bash, zip.

**Reference spec:** `docs/superpowers/specs/2026-07-04-panicos-x64-target-design.md`

---

### Task 1: x64 port.json metadata overlay

**Files:**
- Create: `packaging/retro-handheld/x64/port.json`

- [ ] **Step 1: Create the x64 overlay directory and port.json**

Copy `packaging/retro-handheld/port.json` verbatim, changing only the `arch`
array from `["aarch64"]` to `["x86_64"]`. Full contents:

```json
{
  "version": 4,
  "name": "rockbox.zip",
  "items": [
    "Rockbox.sh",
    "rockbox"
  ],
  "items_opt": [],
  "attr": {
    "title": "Rockbox",
    "porter": [
      "IncognitoMan"
    ],
    "desc": "Rockbox is a free, open-source firmware replacement for digital audio players (DAPs), offering an alternative to the original operating system. It's designed to be highly customizable, with features like advanced equalizers, themes, and support for various file formats.",
    "desc_md": null,
    "inst": "Ready to Run",
    "inst_md": null,
    "genres": [
      "other"
    ],
    "image": {},
    "rtr": true,
    "exp": false,
    "runtime": [],
    "store": [],
    "availability": "full",
    "reqs": [],
    "arch": [
      "x86_64"
    ],
    "min_glibc": ""
  }
}
```

- [ ] **Step 2: Verify it is valid JSON and differs only in arch**

Run:
```bash
python3 -c "import json;print(json.load(open('packaging/retro-handheld/x64/port.json'))['attr']['arch'])"
diff <(sed 's/"x86_64"/"aarch64"/' packaging/retro-handheld/x64/port.json) packaging/retro-handheld/port.json
```
Expected: prints `['x86_64']`; the `diff` produces no output (identical apart from arch string).

- [ ] **Step 3: Commit**

```bash
git add packaging/retro-handheld/x64/port.json
git commit -m "packaging: add x86_64 port.json overlay for the PanicOS-x64 pak"
```

---

### Task 2: portmaster-x64 make targets

**Files:**
- Modify: `packaging/retro-handheld/retro-handheld.make` (append after the existing `portmaster-zip` target, ~line 106)

- [ ] **Step 1: Add the two targets**

Insert immediately after the `portmaster-zip:` target block (before `rhclean:`):

```make
portmaster-x64: portmaster
	## x86_64 variant: reuse the portmaster layout, swap in the x64 arch metadata
	cp $(RH_PACK_DIR)/x64/port.json $(PM_PKG_DIR)/rockbox/port.json

portmaster-x64-zip: portmasterclean portmaster-x64
	(cd $(PM_PKG_DIR) && zip -q -r - .) >rockbox-x64.zip

```

- [ ] **Step 2: Verify make parses the new targets**

Run from a configured build dir is not required; just syntax-check the include by
listing targets from the repo root make wrapper is unavailable, so instead grep
to confirm the block is well-formed (tabs, not spaces, on recipe lines):

```bash
grep -nP '^\t' packaging/retro-handheld/retro-handheld.make | grep -E 'port.json|rockbox-x64.zip'
```
Expected: both recipe lines appear, each prefixed by a real TAB (the `^\t` match
confirms tab indentation — spaces would not match and would break make).

- [ ] **Step 3: Commit**

```bash
git add packaging/retro-handheld/retro-handheld.make
git commit -m "packaging: add portmaster-x64-zip make target for the x86_64 pak"
```

---

### Task 3: Dockerfile.panicos-x64 (native amd64 build image)

**Files:**
- Create: `Dockerfile.panicos-x64`

- [ ] **Step 1: Create the Dockerfile**

Clone of `Dockerfile.portmaster` with `linux/amd64` and updated header comments.
Full contents:

```dockerfile
# Minimal native-amd64 build image for the Rockbox PanicOS-x64 PortMaster pak.
#
# Building FOR x86_64 by building ON an amd64 image means the container's own
# gcc/g++ are x86_64 -- no CROSS_COMPILE / cmake toolchain file is needed. On an
# x86_64 host this runs natively (no qemu emulation), so it is fast.
#
# Debian Bookworm ships gcc-12, matching the arm64 PortMaster image, so the
# projectM + Rockbox build behaves identically apart from the target arch.
FROM --platform=linux/amd64 debian:bookworm

# Build deps, in one layer, apt lists cleaned to keep the image lean:
#   build-essential gcc g++ make  -> Rockbox + projectM C/C++ compilation
#   cmake                         -> projectM-4 build system
#   git                           -> submodule checkout (projectM source)
#   zip perl                      -> Rockbox language/zip build + buildzip.pl
#   pkg-config                    -> locating SDL2 / GLES / EGL
#   libsdl2-dev                   -> SDL2 app target (sdl2-config, headers, libs)
#   libgles2-mesa-dev libegl1-mesa-dev -> GLES2 + EGL headers/libs (visualizer)
#   ccache                        -> cache compiled objects across builds/runs
RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential \
        gcc \
        g++ \
        make \
        cmake \
        git \
        zip \
        perl \
        pkg-config \
        libsdl2-dev \
        libgles2-mesa-dev \
        libegl1-mesa-dev \
        ccache \
    && rm -rf /var/lib/apt/lists/* \
    && mkdir -p /usr/lib/ccache/bin \
    && for c in gcc cc g++ c++; do ln -sf /usr/bin/ccache "/usr/lib/ccache/bin/$c"; done

# Compiler-name symlinks first on PATH: any build system invoking plain
# `gcc`/`g++` (cmake, Rockbox's Makefiles) is transparently routed through
# ccache with no per-project CC/CXX override needed.
ENV PATH="/usr/lib/ccache/bin:${PATH}"

WORKDIR /src
```

- [ ] **Step 2: Verify the platform line**

Run:
```bash
grep -n 'platform=linux/amd64' Dockerfile.panicos-x64
```
Expected: one match on the `FROM` line.

- [ ] **Step 3: Commit**

```bash
git add Dockerfile.panicos-x64
git commit -m "build: add native-amd64 Docker image for the PanicOS-x64 pak"
```

---

### Task 4: build-panicos-x64.sh (native amd64 build driver)

**Files:**
- Create: `build-panicos-x64.sh` (mode +x)

- [ ] **Step 1: Create the script**

Clone of `build-portmaster.sh`, retargeted to amd64. Differences from the arm64
script: no qemu/binfmt block (native build), amd64 platform, distinct image /
build dir / ccache dir / output names, and `portmaster-x64-zip` in place of
`portmaster-zip`. Full contents:

```bash
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
# Output: Rockbox-x64.pak.zip at the repo root.
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
```

- [ ] **Step 2: Make it executable and syntax-check**

Run:
```bash
chmod +x build-panicos-x64.sh
bash -n build-panicos-x64.sh && echo "syntax OK"
./build-panicos-x64.sh --help
```
Expected: `syntax OK`; `--help` prints the usage header lines (2..20).

- [ ] **Step 3: Commit**

```bash
git add build-panicos-x64.sh
git commit -m "build: add build-panicos-x64.sh driver for the x86_64 PortMaster pak"
```

---

### Task 5: BUILDING-PANICOS-X64.md docs

**Files:**
- Create: `BUILDING-PANICOS-X64.md`

- [ ] **Step 1: Create the doc**

```markdown
# Building the Rockbox PanicOS-x64 pak (Dockerized)

This repo ships a one-command, reproducible **native x86_64** build of the
PanicOS / ROCKNIX x64 PortMaster pak (projectM-4 + Rockbox), using only Docker --
no toolchain on the host.

It reuses the arch-agnostic `retro-handheld` target: the only differences from
the aarch64 `build-portmaster.sh` flow are the container platform (amd64) and the
pak metadata/name. See `docs/superpowers/specs/2026-07-04-panicos-x64-target-design.md`.

## Prerequisites

- **Docker**. On an x86_64 host the amd64 container runs **natively** -- no
  qemu/binfmt emulation is needed (unlike the aarch64 pak), so the build is fast.

## Build

```
./build-panicos-x64.sh
```

Flags:

- `--rebuild` -- force a rebuild of the Docker image.
- `--clean`   -- wipe `build-panicos-x64/` before building.

## Output

The pak lands at the repo root as **`Rockbox-x64.pak.zip`**. The script prints
its path and size when finished. Install it on a PanicOS / ROCKNIX x64 device as
a PortMaster port; the host provides the x86_64 `gptokeyb2` at launch.

## What it does

1. Builds a minimal native-amd64 Debian Bookworm image (`Dockerfile.panicos-x64`)
   with the build deps (cmake, SDL2/GLES2/EGL dev packages, zip, perl, ...).
2. Runs the container as your host user, bind-mounting the repo at `/src`, and:
   - checks out the projectM submodule,
   - builds projectM-4 as **native x86_64** static libs,
   - configures + builds Rockbox for the `retro-handheld` target,
   - runs `make rhbuild` then `make portmaster-x64-zip` to lay out and pack the
     pak (Aurora themes, fonts, Milkdrop `.milk` presets, x86_64 `port.json`).
3. Copies the resulting zip to `Rockbox-x64.pak.zip`.

## Notes

- Native amd64: **fast** compared to the emulated aarch64 pak.
- Uses **gcc-12** (Debian Bookworm), matching the aarch64 image.
- The arm64 and x64 flows share `lib/projectm/lib/*.a` scratch space and each
  rebuilds it from clean -- do not run both simultaneously.
```

- [ ] **Step 2: Verify it references the correct output name**

Run:
```bash
grep -c 'Rockbox-x64.pak.zip' BUILDING-PANICOS-X64.md
```
Expected: `2` (or more) matches.

- [ ] **Step 3: Commit**

```bash
git add BUILDING-PANICOS-X64.md
git commit -m "docs: how to build the PanicOS-x64 pak"
```

---

### Task 6: End-to-end build verification

**Files:** none (verification only)

- [ ] **Step 1: Run the full build**

Run:
```bash
./build-panicos-x64.sh --clean --rebuild
```
Expected: completes without error, ending with
`>> PanicOS-x64 pak: .../Rockbox-x64.pak.zip (<size>)`.

- [ ] **Step 2: Verify the pak contents and binary arch**

Run:
```bash
unzip -l Rockbox-x64.pak.zip | grep -E 'Rockbox.sh|rockbox/rockbox|port.json'
mkdir -p /tmp/x64check && cd /tmp/x64check && unzip -o "$OLDPWD/Rockbox-x64.pak.zip" >/dev/null
file rockbox/rockbox
grep -o '"x86_64"' rockbox/port.json
cd "$OLDPWD" && rm -rf /tmp/x64check
```
Expected: the pak lists `Rockbox.sh`, `rockbox/rockbox`, `rockbox/port.json`;
`file` reports the `rockbox` binary as `ELF 64-bit LSB ... x86-64`; the grep
prints `"x86_64"`.

- [ ] **Step 3: Confirm the arm64 pak flow is untouched**

Run:
```bash
git status --short
```
Expected: no unexpected modifications to `build-portmaster.sh`,
`Dockerfile.portmaster`, or `packaging/retro-handheld/port.json` (only the new
x64 files and the `retro-handheld.make` addition from earlier tasks).

---

## Self-Review notes

- **Spec coverage:** Task 1 → x64 `port.json` overlay; Task 2 → `portmaster-x64-zip`
  make target; Task 3 → `Dockerfile.panicos-x64`; Task 4 → `build-panicos-x64.sh`;
  Task 5 → `BUILDING-PANICOS-X64.md`; Task 6 → end-to-end verification incl.
  arch check and arm64-flow-untouched check. All spec files covered; no
  `tools/configure`/source/`pak.json` changes, matching the "out of scope" list.
- **No placeholders.**
- **Name consistency:** `portmaster-x64` / `portmaster-x64-zip`, `rockbox-x64.zip`,
  `Rockbox-x64.pak.zip`, `Dockerfile.panicos-x64`, `build-panicos-x64.sh`,
  `build-panicos-x64/`, `rockbox-panicos-x64-builder` used consistently across tasks.
```
