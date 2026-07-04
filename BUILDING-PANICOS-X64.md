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
