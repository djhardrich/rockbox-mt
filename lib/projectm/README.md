# projectM-4 (Milkdrop visualizer library)

## Overview

projectM-4 is vendored as a git submodule at `lib/projectm/src`.  After cloning
the repository, initialise it with:

```
git submodule update --init --recursive
```

## Building the static libraries

The compiled libraries are **not** committed (they are gitignored) and must be
built before building Rockbox:

```
./lib/projectm/build-projectm.sh
```

The script produces:

- `lib/projectm/lib/libprojectM-4.a`
- `lib/projectm/lib/libprojectM_eval.a`
- headers in `lib/projectm/include/`

## Cross-compiling (arm64 / Panfrost device)

Set `CMAKE_TOOLCHAIN_FILE` (and optionally `CC`/`CXX`) before running the script
so CMake picks up the correct sysroot and compiler:

```
export CMAKE_TOOLCHAIN_FILE=/path/to/toolchain.cmake
export CC=aarch64-linux-gnu-gcc
export CXX=aarch64-linux-gnu-g++
./lib/projectm/build-projectm.sh
```

## Linking into Rockbox

The retro-handheld build links these libraries via the `rhhconf()` function in
`tools/configure`.  `configure` will print a warning if the libraries are not
present when you run it; the link step will then fail with a missing-file error
if you proceed without building them first.
