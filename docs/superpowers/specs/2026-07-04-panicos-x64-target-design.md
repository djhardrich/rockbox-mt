# PanicOS-x64 (x86_64 PortMaster pak) — Design

**Date:** 2026-07-04
**Status:** Approved

## Summary

Add a parallel, self-contained x86_64 build that reuses the existing
`retro-handheld` configure target and PortMaster packaging, producing a separate
pak `Rockbox-x64.pak.zip` for PanicOS / ROCKNIX x64 handhelds.

No changes to `tools/configure` or the C/C++ source are required: the
`retro-handheld` target's compiler setup (`simcc "sdl-app"` + `rhhconf`) is
already native and architecture-agnostic — it uses the host compiler with no
forced arch. The current pak is aarch64 only because `build-portmaster.sh` and
`Dockerfile.portmaster` pin `linux/arm64` and build projectM for arm64. Building
the same target in an amd64 container yields an x86_64 pak.

"PanicOS-x64" is therefore not a new firmware target in the Rockbox sense — it is
the **x86_64 PortMaster build**. PanicOS and ROCKNIX both expose PortMaster's
`control.txt`, so the existing launcher works unchanged.

## Background / current state

- **Target:** `retro-handheld` (target_id 122, `app_type=sdl-app`,
  `t_cpu=hosted`, `t_manufacturer=sdl`). Config in `tools/configure`
  (`simcc "sdl-app"` at line ~253, `rhhconf` at line ~880). Arch-agnostic.
- **projectM:** `lib/projectm/build-projectm.sh` uses native cmake/compiler and
  `rm -rf`s its build dir each run, so it builds for whatever arch it runs on.
  Output static libs at `lib/projectm/lib/*.a`.
- **Packaging:** `packaging/retro-handheld/retro-handheld.make`. The `portmaster`
  target lays out `pkgbuild/portmaster/` (rockbox dir + PortMaster launcher
  files); `portmaster-zip` packs it to `rockbox.zip`.
- **Launcher:** `packaging/retro-handheld/Rockbox.sh` sources PortMaster's
  `control.txt`, obtains `$GPTOKEYB2` from the host, and runs `./rockbox`. All
  shell, arch-agnostic. `firmware/*` scripts likewise.
- **gptokeyb2:** NOT bundled in the PortMaster pak (only the NextUI variant
  bundles it). The host PortMaster install provides the correct-arch binary at
  runtime — so for x64 only `rockbox` and projectM need to be x86_64.
- **Metadata:** `packaging/retro-handheld/port.json` hardcodes
  `"arch": ["aarch64"]`. `pak.json` is not shipped inside the PortMaster zip
  (NextUI/muOS only), so it needs no x64 variant.

## New files

1. **`Dockerfile.panicos-x64`** — clone of `Dockerfile.portmaster` but
   `FROM --platform=linux/amd64 debian:bookworm`. Same gcc-12, same
   build deps (`build-essential`, `cmake`, `git`, `zip`, `perl`, `pkg-config`,
   `libsdl2-dev`, `libgles2-mesa-dev`, `libegl1-mesa-dev`, `ccache`), same
   ccache compiler-symlink setup and `PATH`. Native on an x86_64 host → no qemu.

2. **`build-panicos-x64.sh`** — clone of `build-portmaster.sh` with:
   - `PLATFORM="linux/amd64"`, `IMAGE="rockbox-panicos-x64-builder"`,
     `DOCKERFILE="Dockerfile.panicos-x64"`, `BUILD_DIR="build-panicos-x64"`,
     `OUTPUT="Rockbox-x64.pak.zip"`, `ZIP_IN_BUILD="rockbox-x64.zip"`,
     separate `CCACHE_DIR` (e.g. `$HOME/.cache/rockbox-panicos-x64-ccache`).
   - Drops the qemu/binfmt registration block (native build; no emulation).
   - Same in-container steps: submodule init → `build-projectm.sh` (native
     x86_64) → `git clean -fdx -- tools/` → configure `--target=retro-handheld
     --type=n` → `make` → `make rhclean` → `make rhbuild` → **`make
     portmaster-x64-zip`**.
   - Copies `build-panicos-x64/rockbox-x64.zip` → `Rockbox-x64.pak.zip`.

3. **`packaging/retro-handheld/x64/port.json`** — copy of the existing
   `port.json` with `"arch": ["x86_64"]` (all other fields identical).

4. **`BUILDING-PANICOS-X64.md`** — build docs mirroring
   `BUILDING-PORTMASTER.md`: native amd64, no emulation, `./build-panicos-x64.sh`
   with `--rebuild` / `--clean`, output `Rockbox-x64.pak.zip`.

## Changed files

5. **`packaging/retro-handheld/retro-handheld.make`** — add two targets that
   reuse the existing `portmaster` layout and swap in the x64 metadata (no
   duplication of the `portmaster` `cp` lines):

   ```make
   portmaster-x64: portmaster
   	cp $(RH_PACK_DIR)/x64/port.json $(PM_PKG_DIR)/rockbox/port.json

   portmaster-x64-zip: portmasterclean portmaster-x64
   	(cd $(PM_PKG_DIR) && zip -q -r - .) >rockbox-x64.zip
   ```

## Data flow

`build-panicos-x64.sh` → amd64 container → projectM x86_64 `.a` → `rhhconf`
links them into the native x86_64 `rockbox` SDL app → `rhbuild` lays out
`pkgbuild/rockbox/` (identical content to arm64: Aurora / Aurora Light themes,
SF-Pro fonts, `.milk` presets, default config) → `portmaster-x64-zip` copies the
PortMaster launcher files and the x64 `port.json` → `rockbox-x64.zip` → renamed
`Rockbox-x64.pak.zip`.

At runtime on PanicOS / ROCKNIX x64, `Rockbox.sh` sources PortMaster's
`control.txt`, gets the host's x86_64 `$GPTOKEYB2`, and launches the x86_64
`rockbox` — unchanged from the ARM flow.

## Coexistence

The arm64 and x64 flows use separate Dockerfiles, images, build dirs, ccache
dirs, and output names, so they never collide. projectM's `lib/projectm/lib/*.a`
is shared scratch space, but each build rebuilds it from clean; the two flows
must not run simultaneously (each overwrites the shared `.a`s for its arch).

## Risks / notes

- **glibc floor:** building on bookworm (glibc 2.36) sets the binary's minimum
  glibc. ROCKNIX / PanicOS x64 ship a recent glibc, so this should be fine —
  matching how the arm64 bookworm build already runs on those devices. Watch for
  it on the first on-device test.
- **`-lstdc++fs`:** `rhhconf` links it; bookworm provides the stub archive on
  amd64 as it does on arm64, so linking succeeds.
- **Testing:** validated on real hardware per the PortMaster testing workflow.
  This work produces the pak; on-device launch is the final gate.

## Out of scope (YAGNI)

- No new `tools/configure` entry (reuse `retro-handheld`).
- No universal / fat pak (separate x64 pak file).
- No gptokeyb2 bundling (host provides it at runtime).
- No `pak.json` or `systems/` changes.
