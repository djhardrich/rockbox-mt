# Autodetecting Sample-Rate + Output Bit-Depth Settings — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make both PortMaster paks autodetect the audio device's supported rate, prefer 48000 Hz, and expose user-facing Frequency (up to 192 kHz) and Output-bit-depth (Auto/16/24/32) settings.

**Architecture:** Enable Rockbox's multi-rate machinery for the shared `retro-handheld` target via `HW_SAMPR_CAPS`, let the SDL sink renegotiate the device's real rate with `SDL_AUDIO_ALLOW_FREQUENCY_CHANGE` (existing cvt path resamples), default the Frequency setting to 48 kHz via a new `PLAY_FREQ_DEFAULT` macro, and add a target-guarded bit-depth setting the SDL sink reads when opening the device.

**Tech Stack:** C, Rockbox firmware, SDL2 audio, cross-compiled via `build-portmaster.sh` (ARM) and `build-panicos-x64.sh` (x64). ccache is configured, so rebuilds are fast (see memory: PortMaster testing workflow).

**Testing note:** This codebase has no unit-test harness for the audio sink. Per-task verification is a successful cross-compile of the `retro-handheld` target plus targeted source inspection; final verification is the user's on-device pass. Commit after each task.

---

## File Structure

| File | Responsibility | Change |
|------|----------------|--------|
| `firmware/export/config/retro-handheld.h` | target capabilities | add `HW_SAMPR_CAPS`, `PLAY_FREQ_DEFAULT`, `HAVE_OUTPUT_BIT_DEPTH` |
| `firmware/export/config_caps.h` | derived audio caps | default `PLAY_FREQ_DEFAULT` to `0` |
| `apps/settings_list.c` | setting definitions | play_frequency default; new `output_bit_depth` CHOICE_SETTING + callback |
| `apps/settings.h` | persisted settings struct | add `output_bit_depth` field |
| `apps/menus/playback_menu.c` | playback menu tree | add bit-depth menu item |
| `apps/lang/english.lang` | UI strings | add bit-depth title + option strings |
| `firmware/target/hosted/sdl/pcm-sdl.c` | SDL PCM sink | `ALLOW_FREQUENCY_CHANGE`; honor bit depth; live-reopen hook |

---

## Task 1: Enable sample-rate capabilities and prefer-48k default (config only)

**Files:**
- Modify: `firmware/export/config/retro-handheld.h` (near the `HAVE_SDL_AUDIO` block, ~line 79-81)
- Modify: `firmware/export/config_caps.h` (after the `HAVE_PLAY_FREQ` block, ~line 164)
- Modify: `apps/settings_list.c:1133`

- [ ] **Step 1: Add capability macros to the target config**

In `firmware/export/config/retro-handheld.h`, find:

```c
/* Use SDL audio/pcm in a SDL app build */
#define HAVE_SDL_AUDIO
```

Replace with:

```c
/* Use SDL audio/pcm in a SDL app build */
#define HAVE_SDL_AUDIO

/* Advertise the full up-to-192kHz rate set so the core can output 48kHz and
 * higher; this also auto-enables HAVE_PLAY_FREQ (the Frequency menu). */
#define HW_SAMPR_CAPS       SAMPR_CAP_ALL_192

/* Prefer 48000 Hz out of the box (many USB-C DACs reject 44100). Users can
 * still choose Auto/44.1/96/192 in the Frequency menu. Expanded to SAMPR_48
 * at setting-definition time (pcm_sampr.h is included by then). */
#define PLAY_FREQ_DEFAULT   SAMPR_48

/* Expose the Output-bit-depth override (SDL<->device wire format only). */
#define HAVE_OUTPUT_BIT_DEPTH
```

- [ ] **Step 2: Provide the default for all other targets**

In `firmware/export/config_caps.h`, after the closing `#endif` of the `HAVE_PLAY_FREQ` block (the block that ends with the `#elif (HW_SAMPR_CAPS & SAMPR_CAP_48)` … `#endif` around line 164), add:

```c
/* Default value for the play_frequency setting. 0 == Auto (follow track rate).
 * A target may pre-define this (e.g. SAMPR_48) to prefer a fixed rate. */
#ifndef PLAY_FREQ_DEFAULT
#define PLAY_FREQ_DEFAULT   0
#endif
```

- [ ] **Step 3: Use the default in the play_frequency setting**

In `apps/settings_list.c`, find (line ~1133):

```c
                  play_frequency, LANG_FREQUENCY, 0, "playback frequency", "auto",
```

Replace with:

```c
                  play_frequency, LANG_FREQUENCY, PLAY_FREQ_DEFAULT, "playback frequency", "auto",
```

- [ ] **Step 4: Build the retro-handheld target**

Run: `./build-portmaster.sh`
Expected: build completes; no macro-redefinition or `SAMPR_48 undeclared` errors. `HAVE_PLAY_FREQ` is now defined (192), so the Frequency menu code compiles in.

- [ ] **Step 5: Sanity-check the config took effect**

Run: `grep -nE "HW_SAMPR_CAPS|PLAY_FREQ_DEFAULT|HAVE_OUTPUT_BIT_DEPTH" firmware/export/config/retro-handheld.h`
Expected: all three defines present.

- [ ] **Step 6: Commit**

```bash
git add firmware/export/config/retro-handheld.h firmware/export/config_caps.h apps/settings_list.c
git commit -m "audio: enable up-to-192kHz rates and default retro-handheld to 48kHz"
```

---

## Task 2: Autodetect the device rate in the SDL sink

**Files:**
- Modify: `firmware/target/hosted/sdl/pcm-sdl.c:131` (the `SDL_OpenAudioDevice` call in `sink_set_freq_nolock`)

- [ ] **Step 1: Allow SDL to report the device's real rate**

In `firmware/target/hosted/sdl/pcm-sdl.c`, find:

```c
    if((pcm_devid = SDL_OpenAudioDevice(audiodev, 0, &wanted_spec, &obtained, SDL_AUDIO_ALLOW_SAMPLES_CHANGE)) == 0) {
```

Replace with:

```c
    /* ALLOW_FREQUENCY_CHANGE: obtained.freq reflects the device's actual
     * supported rate instead of being forced to the requested rate. The cvt
     * path below (SDL_BuildAudioCVT + write_to_soundcard) resamples our stream
     * to obtained.freq, so the device is never driven at an unsupported rate.
     * Renegotiated on every reopen -> hotplug-safe. */
    if((pcm_devid = SDL_OpenAudioDevice(audiodev, 0, &wanted_spec, &obtained,
                        SDL_AUDIO_ALLOW_SAMPLES_CHANGE | SDL_AUDIO_ALLOW_FREQUENCY_CHANGE)) == 0) {
```

- [ ] **Step 2: Confirm the cvt path already handles freq mismatch (inspection only)**

Run: `sed -n '166,172p' firmware/target/hosted/sdl/pcm-sdl.c`
Expected: `SDL_BuildAudioCVT(&cvt, AUDIO_S16SYS, 2, pcm_sampr, obtained.format, obtained.channels, obtained.freq);` — confirms resampling from `pcm_sampr` to `obtained.freq` is already wired. No change needed.

- [ ] **Step 3: Build**

Run: `./build-portmaster.sh`
Expected: build completes cleanly.

- [ ] **Step 4: Commit**

```bash
git add firmware/target/hosted/sdl/pcm-sdl.c
git commit -m "sdl: renegotiate device sample rate (fixes 44.1 distortion on 48k-only DACs)"
```

---

## Task 3: Add the output_bit_depth setting (struct, setting, menu, lang)

**Files:**
- Modify: `apps/settings.h:897-899` (add field near `play_frequency`)
- Modify: `apps/settings_list.c` (add callback near line 769; add CHOICE_SETTING near line 1146)
- Modify: `apps/menus/playback_menu.c:176-178` and `:212-214`
- Modify: `apps/lang/english.lang` (append phrases)

- [ ] **Step 1: Add the persisted setting field**

In `apps/settings.h`, find (line ~897):

```c
#ifdef HAVE_PLAY_FREQ
    int play_frequency; /* core audio output frequency selection */
#endif
```

Replace with:

```c
#ifdef HAVE_PLAY_FREQ
    int play_frequency; /* core audio output frequency selection */
#endif
#ifdef HAVE_OUTPUT_BIT_DEPTH
    int output_bit_depth; /* SDL<->device wire format: 0=auto,1=16,2=24,3=32 */
#endif
```

- [ ] **Step 2: Add the setting definition + callback**

In `apps/settings_list.c`, find the end of the play_frequency callback block (line ~769):

```c
static void playback_frequency_callback(int sample_rate_hz)
{
    if (pcm_current_sink() == PCM_SINK_BUILTIN)
        audio_set_playback_frequency(sample_rate_hz);
}
#endif /* HAVE_PLAY_FREQ */
```

Immediately after it, add:

```c
#ifdef HAVE_OUTPUT_BIT_DEPTH
/* Implemented by the SDL sink (firmware/target/hosted/sdl/pcm-sdl.c). Reopens
 * the audio device so the new wire format takes effect immediately. */
extern void pcm_sdl_reopen_device(void);
static void output_bit_depth_callback(int value)
{
    (void)value;
    pcm_sdl_reopen_device();
}
#endif /* HAVE_OUTPUT_BIT_DEPTH */
```

Then find the end of the play_frequency `TABLE_SETTING` block (line ~1146):

```c
#endif /* HAVE_PLAY_FREQ */

#ifdef HAVE_ALBUMART
```

Insert the new setting between them:

```c
#endif /* HAVE_PLAY_FREQ */

#ifdef HAVE_OUTPUT_BIT_DEPTH
    CHOICE_SETTING(F_CB_ON_SELECT_ONLY|F_CB_ONLY_IF_CHANGED, output_bit_depth,
                   LANG_OUTPUT_BIT_DEPTH, 0, "output bit depth",
                   "auto,16,24,32", output_bit_depth_callback, 4,
                   ID2P(LANG_AUTO), ID2P(LANG_BIT_DEPTH_16),
                   ID2P(LANG_BIT_DEPTH_24), ID2P(LANG_BIT_DEPTH_32)),
#endif /* HAVE_OUTPUT_BIT_DEPTH */

#ifdef HAVE_ALBUMART
```

- [ ] **Step 3: Declare the menu item and add it to the tree**

In `apps/menus/playback_menu.c`, find (line ~176):

```c
#ifdef HAVE_PLAY_FREQ
MENUITEM_SETTING(play_frequency, &global_settings.play_frequency, NULL);
#endif
```

Replace with:

```c
#ifdef HAVE_PLAY_FREQ
MENUITEM_SETTING(play_frequency, &global_settings.play_frequency, NULL);
#endif
#ifdef HAVE_OUTPUT_BIT_DEPTH
MENUITEM_SETTING(output_bit_depth, &global_settings.output_bit_depth, NULL);
#endif
```

Then find (line ~212):

```c
#ifdef HAVE_PLAY_FREQ
          ,&play_frequency
#endif
```

Replace with:

```c
#ifdef HAVE_PLAY_FREQ
          ,&play_frequency
#endif
#ifdef HAVE_OUTPUT_BIT_DEPTH
          ,&output_bit_depth
#endif
```

- [ ] **Step 4: Add the language strings**

Append to `apps/lang/english.lang` (order-independent; genlang assigns ids):

```
<phrase>
  id: LANG_OUTPUT_BIT_DEPTH
  desc: in playback settings, SDL output wire format
  user: core
  <source>
    *: "Output Bit Depth"
  </source>
  <dest>
    *: "Output Bit Depth"
  </dest>
  <voice>
    *: "Output Bit Depth"
  </voice>
</phrase>
<phrase>
  id: LANG_BIT_DEPTH_16
  desc: output bit depth option
  user: core
  <source>
    *: "16-bit"
  </source>
  <dest>
    *: "16-bit"
  </dest>
  <voice>
    *: "16 bit"
  </voice>
</phrase>
<phrase>
  id: LANG_BIT_DEPTH_24
  desc: output bit depth option
  user: core
  <source>
    *: "24-bit"
  </source>
  <dest>
    *: "24-bit"
  </dest>
  <voice>
    *: "24 bit"
  </voice>
</phrase>
<phrase>
  id: LANG_BIT_DEPTH_32
  desc: output bit depth option
  user: core
  <source>
    *: "32-bit"
  </source>
  <dest>
    *: "32-bit"
  </dest>
  <voice>
    *: "32 bit"
  </voice>
</phrase>
```

- [ ] **Step 5: Build**

Run: `./build-portmaster.sh`
Expected: builds cleanly. The `pcm_sdl_reopen_device` symbol is still undefined at link only if the SDL sink hasn't been updated yet — it IS referenced now, so this task links against the stub added in Task 4. If Task 4 is not yet done, expect an **unresolved symbol `pcm_sdl_reopen_device`** at link — that is acceptable at this checkpoint; do Task 4 before the integration build. (Compile of each translation unit must still succeed.)

- [ ] **Step 6: Commit**

```bash
git add apps/settings.h apps/settings_list.c apps/menus/playback_menu.c apps/lang/english.lang
git commit -m "settings: add Output bit depth setting (Auto/16/24/32) for SDL targets"
```

---

## Task 4: Honor the bit-depth setting in the SDL sink + live reopen

**Files:**
- Modify: `firmware/target/hosted/sdl/pcm-sdl.c` — `sink_set_freq_nolock` (lines ~108-172), and add `pcm_sdl_reopen_device`

- [ ] **Step 1: Include settings and track the current freq index**

In `firmware/target/hosted/sdl/pcm-sdl.c`, near the other includes (after `#include "panic.h"`, line ~34), add:

```c
#ifdef HAVE_OUTPUT_BIT_DEPTH
#include "settings.h"
#endif
```

Then find the static declarations block (after line ~65, `static unsigned long pcm_sampr;`) and add:

```c
#ifdef HAVE_OUTPUT_BIT_DEPTH
/* Remember the last freq index passed to sink_set_freq_nolock so the setting
 * callback can reopen the device with the same rate but a new wire format. */
static uint16_t sdl_cur_freq_index;
#endif
```

- [ ] **Step 2: Choose the requested format from the setting**

In `sink_set_freq_nolock` (line ~108), find:

```c
static void sink_set_freq_nolock(uint16_t freq)
{
    pcm_sampr = hw_freq_sampr[freq];

    SDL_AudioSpec wanted_spec;
    wanted_spec.freq = pcm_sampr;
    wanted_spec.format = AUDIO_S16SYS;
    wanted_spec.channels = 2;
```

Replace with:

```c
static void sink_set_freq_nolock(uint16_t freq)
{
    pcm_sampr = hw_freq_sampr[freq];

    /* Base allowed-changes: SDL may resample to the device's real rate (see
     * SDL_OpenAudioDevice below) and may resize the buffer. */
    int allowed_changes = SDL_AUDIO_ALLOW_SAMPLES_CHANGE |
                          SDL_AUDIO_ALLOW_FREQUENCY_CHANGE;

    SDL_AudioSpec wanted_spec;
    wanted_spec.freq = pcm_sampr;
    wanted_spec.format = AUDIO_S16SYS;
    wanted_spec.channels = 2;

#ifdef HAVE_OUTPUT_BIT_DEPTH
    sdl_cur_freq_index = freq;
    /* Output-bit-depth override. The core is 16-bit; this only selects the
     * SDL<->device wire format. SDL2 has no 24-bit sample type, so 24 and 32
     * both request S32 (ALSA below picks the real packing). */
    switch (global_settings.output_bit_depth)
    {
    case 1: /* 16 */
        wanted_spec.format = AUDIO_S16SYS;
        break;
    case 2: /* 24 */
    case 3: /* 32 */
        wanted_spec.format = AUDIO_S32SYS;
        break;
    case 0: /* auto */
    default:
        wanted_spec.format = AUDIO_S16SYS;
        allowed_changes |= SDL_AUDIO_ALLOW_FORMAT_CHANGE;
        break;
    }
#endif
```

- [ ] **Step 3: Use `allowed_changes` in the open call**

Find the open call updated in Task 2:

```c
    if((pcm_devid = SDL_OpenAudioDevice(audiodev, 0, &wanted_spec, &obtained,
                        SDL_AUDIO_ALLOW_SAMPLES_CHANGE | SDL_AUDIO_ALLOW_FREQUENCY_CHANGE)) == 0) {
```

Replace with:

```c
    if((pcm_devid = SDL_OpenAudioDevice(audiodev, 0, &wanted_spec, &obtained,
                        allowed_changes)) == 0) {
```

- [ ] **Step 4: Add the reopen hook**

Immediately after `sink_set_freq` (the locked wrapper, line ~174-179):

```c
static void sink_set_freq(uint16_t freq)
{
    sink_lock();
    sink_set_freq_nolock(freq);
    sink_unlock();
}
```

Add:

```c
#ifdef HAVE_OUTPUT_BIT_DEPTH
/* Called from the Output-bit-depth setting callback. Reopens the device with
 * the current rate and the newly-selected wire format. Uses the same
 * lock+reopen pattern as sink_set_freq, which is already exercised during
 * normal rate changes. */
void pcm_sdl_reopen_device(void)
{
    sink_lock();
    sink_set_freq_nolock(sdl_cur_freq_index);
    sink_unlock();
}
#endif
```

- [ ] **Step 5: Build both the failing-link check and the full target**

Run: `./build-portmaster.sh`
Expected: builds and links cleanly — `pcm_sdl_reopen_device` (referenced from `settings_list.c` in Task 3) now resolves.

- [ ] **Step 6: Commit**

```bash
git add firmware/target/hosted/sdl/pcm-sdl.c
git commit -m "sdl: honor Output bit depth setting and reopen device on change"
```

---

## Task 5: Integration build of both paks + on-device verification

**Files:** none (build + manual verification)

- [ ] **Step 1: Build the ARM PortMaster pak**

Run: `./build-portmaster.sh`
Expected: produces the ARM `.pak` (do not delete it — user reuses it, see memory: PortMaster testing workflow).

- [ ] **Step 2: Build the PanicOS-x64 pak**

Run: `./build-panicos-x64.sh`
Expected: produces the x64 `.pak`.

- [ ] **Step 3: Static confirmation of the wiring**

Run:
```bash
grep -n "SDL_AUDIO_ALLOW_FREQUENCY_CHANGE" firmware/target/hosted/sdl/pcm-sdl.c
grep -n "pcm_sdl_reopen_device" firmware/target/hosted/sdl/pcm-sdl.c apps/settings_list.c
grep -n "output_bit_depth" apps/settings.h apps/settings_list.c apps/menus/playback_menu.c
```
Expected: the frequency-change flag present; `pcm_sdl_reopen_device` defined once and referenced once; `output_bit_depth` present in all three apps files.

- [ ] **Step 4: On-device verification (user's hardware pass)**

Checklist (Settings → Playback Settings):
1. Connect the USB-C DAC, play 44.1 kHz content → **no distortion/gaps** (previously bad).
2. **Frequency** menu present with Auto / 44.1 / 48 / 88.2 / 96 / 176.4 / 192; default is 48 kHz.
3. **Output Bit Depth** menu present (Auto / 16-bit / 24-bit / 32-bit); switching it re-applies without a reboot.
4. Play a 96/192 kHz file with Frequency = Auto → plays without distortion.
5. Built-in speaker output still works.

- [ ] **Step 5: Final commit / branch is ready for the pak**

No code change; ensure the working tree is clean:
```bash
git status
```
Expected: clean (paks and `Rockbox-x64.pak.zip` are build artifacts, not committed).

---

## Self-Review

- **Spec coverage:** §1 root cause → Task 1 (HW_SAMPR_CAPS). §2 autodetect → Task 2. §3 default 48k → Task 1 (PLAY_FREQ_DEFAULT). §4 bit-depth setting + SDL format + live reopen → Tasks 3–4. Testing → Task 5. All covered.
- **Placeholder scan:** none — every step shows exact code/commands.
- **Type consistency:** `output_bit_depth` (int, 0..3) consistent across settings.h / settings_list.c / playback_menu.c. `pcm_sdl_reopen_device(void)` declared `extern` in settings_list.c and defined in pcm-sdl.c with matching signature. `PLAY_FREQ_DEFAULT` defined in retro-handheld.h (SAMPR_48) and defaulted in config_caps.h (0), used in settings_list.c. `sdl_cur_freq_index` (uint16_t) matches `sink_set_freq_nolock(uint16_t)`.
- **Cross-task ordering note:** Task 3 references `pcm_sdl_reopen_device` which Task 4 defines; the integration link must not run until Task 4 is complete (called out in Task 3 Step 5).
