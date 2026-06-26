# Building the Rockbox PortMaster pak (Dockerized)

This repo ships a one-command, reproducible cross-build of the aarch64
PortMaster pak (projectM-4 + Rockbox), using only Docker -- no toolchain on the
host.

## Prerequisites

- **Docker** with **buildx**.
- **arm64 emulation** registered via binfmt/qemu (`qemu-aarch64`). On an x86_64
  host the build runs the aarch64 container under emulation. If
  `/proc/sys/fs/binfmt_misc/qemu-aarch64` is missing, the script attempts to
  register it with:

  ```
  docker run --privileged --rm tonistiigi/binfmt --install arm64
  ```

  (needs privileged Docker; on a native aarch64 host no emulation is required).

## Build

```
./build-portmaster.sh
```

Flags:

- `--rebuild` -- force a rebuild of the Docker image.
- `--clean`   -- wipe `build-portmaster/` before building.

## Output

The PortMaster release pak lands at the repo root as **`Rockbox.pak.zip`**
(the `release_filename` from `packaging/retro-handheld/pak.json`). The script
prints its path and size when finished.

## What it does

1. Builds a minimal native-aarch64 Debian Bookworm image
   (`Dockerfile.portmaster`) with the build deps (cmake, SDL2/GLES2/EGL dev
   packages, zip, perl, ...).
2. Runs the container as your host user, bind-mounting the repo at `/src`, and:
   - checks out the projectM submodule,
   - builds projectM-4 as native aarch64 static libs
     (`lib/projectm/build-projectm.sh`) **before** Rockbox -- `tools/configure`
     links those `.a` archives,
   - configures + builds Rockbox for the `retro-handheld` target,
   - runs `make rhbuild` then `make portmaster-zip` to lay out and pack the pak
     (which now includes the Milkdrop `.milk` presets under `rockbox/presets/`).
3. Copies the resulting zip to `Rockbox.pak.zip`.

## Notes

- The aarch64 build runs under emulation on x86_64 hosts: **expect many minutes**
  (30+ for a full clean build).
- The container uses **gcc-12**, which avoids the host **gcc-16** internal
  compiler error on `libopus/celt/vq.c` -- so no source workaround is needed.
