# Autodetecting sample-rate + output bit-depth settings (retro-handheld / PortMaster paks)

**Date:** 2026-07-06
**Targets:** `retro-handheld` (shared by both the ARM PortMaster pak and the PanicOS-x64 pak)
**Status:** Approved design, ready for implementation plan

## Problem

On both paks, audio is always output at 44100 Hz. When a USB-C DAC that does not
support 44100 (only 48000) is connected, playback is distorted/gapped. The user
confirmed on-device that 44100 is the culprit by reproducing the same bad audio
at 44.1 kHz in another app.

### Root cause

`firmware/export/config/retro-handheld.h` never defines `HW_SAMPR_CAPS`.
`firmware/export/pcm_sampr.h` therefore defaults it to `SAMPR_CAP_44`, which means:

- `hw_freq_sampr[]` contains exactly one rate: 44100.
- `HAVE_PLAY_FREQ` is never enabled (`firmware/export/config_caps.h`), so there is
  no "Frequency" menu and `PLAY_SAMPR_*` are pinned to 44100.
- Rockbox's DSP resamples every track to 44100 and the SDL sink
  (`firmware/target/hosted/sdl/pcm-sdl.c`) always opens the device at 44100.

So the build has no way to ever emit anything but 44100, regardless of the DAC.

## Goals

1. Autodetect and prefer 48000 Hz; never feed the device an unsupported rate.
2. Expose a user-facing **Frequency** setting (up to 192 kHz) — mostly free, since
   Rockbox already has this menu once `HAVE_PLAY_FREQ` is enabled.
3. Expose a user-facing **Output bit depth** override (Auto / 16 / 24 / 32).
4. Keep changes in shared files guarded so other SDL app targets / simulators are
   unaffected.

### Non-goal

A true 24/32-bit audio pipeline. Rockbox's mixer (`firmware/pcm_mixer.c`) and PCM
buffers are 16-bit (S16) end-to-end. The existing `HAVE_ALSA_32BIT` path
(`firmware/target/hosted/pcm-alsa.c:331`) only shifts 16-bit samples into 32-bit
frames — no added fidelity. The bit-depth setting here changes only the
SDL↔device *wire format*, not internal precision.

## Design

### 1. Sample-rate capability (config)

In `retro-handheld.h`:

```c
#define HW_SAMPR_CAPS   SAMPR_CAP_ALL_192
```

Effects (all via existing Rockbox machinery):
- `hw_freq_sampr[]` gains 44.1 / 48 / 88.2 / 96 / 176.4 / 192 kHz.
- `HAVE_PLAY_FREQ = 192` → the **Frequency** menu appears
  (Auto / 44.1 / 48 / 88.2 / 96 / 176.4 / 192) with no new UI code
  (`apps/settings_list.c:1131`, `apps/menus/playback_menu.c`).
- `PLAY_SAMPR_MAX = SAMPR_192`; the DSP may resample up to 192.

### 2. Autodetect / adapt in the SDL sink

In `sink_set_freq_nolock()` (`pcm-sdl.c`), add `SDL_AUDIO_ALLOW_FREQUENCY_CHANGE`
to the `SDL_OpenAudioDevice()` allowed-changes mask (currently only
`SDL_AUDIO_ALLOW_SAMPLES_CHANGE`).

Result: `obtained.freq` reflects the device's *actual* supported rate instead of
being forced to the requested rate. The existing converter path already handles a
mismatch — `SDL_BuildAudioCVT(&cvt, AUDIO_S16SYS, 2, pcm_sampr, obtained.format,
obtained.channels, obtained.freq)` and `write_to_soundcard()` resample Rockbox's
stream to `obtained.freq`. So whatever rate is selected or auto-picked, the device
is only ever driven at a rate it supports — the distortion cannot occur. This is
renegotiated on every device (re)open, so it is hotplug-safe.

### 3. Default rate = 48 kHz (prefer 48000)

Introduce an overridable macro for the setting default rather than hardcoding it
in shared `settings_list.c`:

- In `config_caps.h` (or alongside `HAVE_PLAY_FREQ`): `#ifndef PLAY_FREQ_DEFAULT`
  → `#define PLAY_FREQ_DEFAULT 0` (0 = Auto, current behavior for all targets).
- In `retro-handheld.h`: `#define PLAY_FREQ_DEFAULT SAMPR_48`.
- In `settings_list.c`, change the `play_frequency` `TABLE_SETTING` default arg
  from `0` to `PLAY_FREQ_DEFAULT`.

Out of the box, all content is emitted at 48000 (Rockbox's resampler handles 44.1
content — better quality than SDL's converter). Users who want per-track /
bit-perfect / hi-res behavior select **Auto** (or 96/192); those selections stay
device-safe via §2. Other targets keep their existing `0`/Auto default.

Tradeoff (accepted): with the 48 kHz default, a 96/192 file is downsampled to 48
until the user switches to Auto.

### 4. Output bit-depth override (setting + menu)

- New config gate, e.g. `#define HAVE_OUTPUT_BIT_DEPTH` in `retro-handheld.h`
  (only where an SDL sink can honor it).
- New `int output_bit_depth` in `global_settings` (`apps/settings.h`), with a
  `CHOICE`/`TABLE` setting in `apps/settings_list.c` and an entry in the playback
  menu, guarded by `HAVE_OUTPUT_BIT_DEPTH`. Values: **Auto / 16 / 24 / 32**,
  default **Auto**.
- The SDL sink reads `global_settings.output_bit_depth` when opening the device in
  `sink_set_freq_nolock()` and chooses `wanted_spec.format` + allowed-changes:
  - **Auto**: request `AUDIO_S16SYS`, add `SDL_AUDIO_ALLOW_FORMAT_CHANGE` → SDL
    opens the device's native format; the cvt path converts S16→device.
  - **16**: force `AUDIO_S16SYS` (no format-change flag).
  - **24 / 32**: request `AUDIO_S32SYS` (no format-change flag).

  The existing `obtained.format` switch, `pcm_channel_bytes`, and cvt code already
  cope with S8/S16/S32/F32, so no new conversion logic is required.

- **SDL 24-bit caveat (accepted):** SDL2 has no 24-bit sample type; 24-bit audio is
  carried in 32-bit containers. At the SDL layer "24" and "32" both request
  `AUDIO_S32SYS`; the ALSA driver beneath SDL decides the actual wire packing
  (S24_3LE vs S32). The menu still offers 24 and 32 for familiarity; they are
  functionally identical at this layer. No fidelity change either way (source is
  16-bit).

- **Applying live:** changing the setting fires a callback that reopens the audio
  device so the new format takes effect immediately. Implementation: a small target
  hook in `pcm-sdl.c` (exposed via a header) that locks, re-runs
  `sink_set_freq_nolock()` with the current frequency, and unlocks; the
  `HAVE_OUTPUT_BIT_DEPTH` setting callback calls it. (The existing frequency
  callback `playback_frequency_callback` → `audio_set_playback_frequency` already
  rebuilds the buffer for rate changes; bit depth only needs a device reopen.)

## Files touched

| File | Change |
|------|--------|
| `firmware/export/config/retro-handheld.h` | `HW_SAMPR_CAPS`, `PLAY_FREQ_DEFAULT`, `HAVE_OUTPUT_BIT_DEPTH` |
| `firmware/export/config_caps.h` | default `PLAY_FREQ_DEFAULT 0` |
| `firmware/target/hosted/sdl/pcm-sdl.c` | `ALLOW_FREQUENCY_CHANGE`; read bit-depth setting → format/allowed-changes; reopen hook |
| `firmware/export/sdl_codec.h` (or a sink header) | declare reopen hook |
| `apps/settings.h` | `output_bit_depth` field |
| `apps/settings_list.c` | `PLAY_FREQ_DEFAULT` for play_frequency; new bit-depth setting + callback |
| `apps/menus/playback_menu.c` | bit-depth menu entry (guarded) |
| lang | new string(s) for the bit-depth setting (e.g. `LANG_OUTPUT_BIT_DEPTH`) |

Settings-block version bump if required by the settings format.

## Testing

- Build both paks: `build-portmaster.sh` (ARM) and `build-panicos-x64.sh` (x64).
- On device (user's hardware pass per their workflow):
  1. Play 44.1 kHz content through the USB-C DAC → **no distortion/gaps**.
  2. **Frequency** and **Output bit depth** menus appear and switch live.
  3. A hi-res (96/192) file plays (with Frequency = Auto).
  4. Built-in output still works after the changes.
- Do not lose the resulting `.pak` (user reuses it for hardware testing).

## Open decisions — RESOLVED

1. Default rate: **48 kHz fixed** (Auto available as an option).
2. Bit-depth menu: **Auto / 16 / 24 / 32** (24 ≡ 32 at the SDL layer, documented).
