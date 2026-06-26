# Milkdrop Visualizer + Obsede2 Default Theme — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add the trimpod Milkdrop/projectM visualizer and ~100 `.milk` presets to the IncognitoMan `retro-handheld` Rockbox port, toggled by a simultaneous **X+Y** press, and ship the **Obsede2** theme as the default — keeping all stock Rockbox functionality intact.

**Architecture:** GL-ify the SDL window (one shared GLES 3.0 context): normal UI draws the 320×240 LCD surface as a full-window textured quad; when `trimpod_viz_active` is set, a dedicated GL render thread runs projectM into a 1/4-res FBO upscaled to the window. Audio reaches projectM through a lock-free PCM ring tapped in `pcm-sdl.c`. projectM-4 is built from source for the Allwinner H700 / mainline-Panfrost arm64 target. X+Y in the global keymap launches the visualizer when audio is playing.

**Tech Stack:** C, SDL2, OpenGL ES 3.0 / GLES2 entry points, EGL, projectM-4 (cmake/C++17), Rockbox build system (`tools/configure`, GNU make), PortMaster pak packaging.

**Reference sources (read-only):**
- Base fork: this repo (`/home/user1/rockbox-milkdrop`, branch `milkdrop-visualizer`).
- trimpod (visualizer/preset source): `/tmp/claude-1000/-home-user1-rockbox-milkdrop/3f57f0b8-218c-4752-8d1e-7de24c2a8c2f/scratchpad/trimpod` (origin `https://github.com/tyrannotorus/c-trimui-trimpod-classic-pak`). Re-clone if the scratchpad is gone.
- Sanitized Obsede2 theme staged at `…/scratchpad/theme/.rockbox`.
- Design spec: `docs/superpowers/specs/2026-06-25-milkdrop-visualizer-design.md`.

**Conventions:**
- Commit after each task with `Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>`.
- Build the target with: `./tools/configure --target=retro-handheld` flow (see Task 2) then `make`. Where a full device build is unavailable in the dev environment, the **build must at least compile and link**; on-target behavior is checked in Task 12.
- When the plan says "copy trimpod's X and apply these changes", copy the file verbatim from the trimpod reference tree, then make ONLY the listed edits.

---

## File Structure

**Created:**
- `lib/projectm/` — vendored projectM-4 source (submodule), `build-projectm.sh`, output `lib/projectm/lib/*.a`, `lib/projectm/include/projectM-4/*.h`
- `apps/milkdrop_visualizer.c`, `apps/milkdrop_visualizer.h` — the visualizer (ported, trimpod-UI deps stripped)
- `apps/menus/visualizer_menu.c` — the Visualizer settings submenu
- `apps/milkdrop_presets/` (build-time asset dir) — the 100 `.milk` presets + `LICENSE.md`
- `packaging/retro-handheld/config.cfg` — default config applying Obsede2
- Obsede2 theme files under `wps/`, `fonts/`, `icons/`, `themes/` (bundled into the build)

**Modified:**
- `firmware/target/hosted/sdl/window-sdl.c` / `.h` — GLES present path + context bridge
- `firmware/target/hosted/sdl/pcm-sdl.c` — PCM tap
- `tools/configure` — projectM include/link, GLES
- `apps/SOURCES` — add visualizer + menu
- `apps/settings.h`, `apps/settings_list.c` — `viz_transition` setting
- `apps/actions.h` — `ACTION_STD_VIZ_TOGGLE`
- `apps/keymaps/keymap-retro-handheld.c` — X+Y binding
- A global action handler site (Task 9) — launch on X+Y
- `apps/gui/wps.c` — remove timeout auto-start (verify none present)
- `apps/menus/settings_menu.c` (or `display_menu.c`) — hook the Visualizer submenu
- `packaging/retro-handheld/retro-handheld.make`, launch scripts — presets, drop scaler shim, ship config.cfg + theme

---

## Task 1: Vendor and build projectM-4 from source

**Files:**
- Create: `lib/projectm/build-projectm.sh`
- Create: `lib/projectm/.gitignore`
- Create: `.gitmodules` entry / submodule at `lib/projectm/src`
- Output (gitignored): `lib/projectm/lib/libprojectM-4.a`, `lib/projectm/lib/libprojectM_eval.a`, `lib/projectm/include/projectM-4/*.h`

- [ ] **Step 1: Add projectM-4 as a submodule, pinned to a known-good tag**

```bash
cd /home/user1/rockbox-milkdrop
git submodule add https://github.com/projectM-visualizer/projectm.git lib/projectm/src
cd lib/projectm/src && git checkout v4.1.6 && git submodule update --init --recursive && cd -
```
(v4.1.6 matches the projectM the trimpod presets were curated against.)

- [ ] **Step 2: Write the build script**

Create `lib/projectm/build-projectm.sh`:
```bash
#!/usr/bin/env bash
# Builds projectM-4 as static libs for the retro-handheld (arm64/GLES) target.
# Override CC/CXX/CMAKE_TOOLCHAIN_FILE in the environment for cross builds.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
SRC="$HERE/src"
BUILD="$HERE/build"
OUT_LIB="$HERE/lib"
OUT_INC="$HERE/include"

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
find "$BUILD" -name 'libprojectM_eval.a' -o -name 'libprojectM-4-playlist.a' 2>/dev/null \
  | grep -E 'libprojectM_eval\.a$' | xargs -r -I{} cp {} "$OUT_LIB/"
# Headers: prefer installed export headers from the source tree.
cp -r "$SRC/src/api/include/projectM-4" "$OUT_INC/" 2>/dev/null || \
  cp -r "$SRC/include/projectM-4" "$OUT_INC/"
echo "projectM build complete:"
ls -l "$OUT_LIB"
```
```bash
chmod +x lib/projectm/build-projectm.sh
```

- [ ] **Step 3: Add .gitignore for build artifacts**

Create `lib/projectm/.gitignore`:
```
/build/
/lib/
/include/
```

- [ ] **Step 4: Run the build and verify the static libs exist**

Run: `./lib/projectm/build-projectm.sh`
Expected: completes; `lib/projectm/lib/libprojectM-4.a` and `lib/projectm/lib/libprojectM_eval.a` exist, and `lib/projectm/include/projectM-4/projectM.h` exists.

Run: `ls lib/projectm/lib/*.a lib/projectm/include/projectM-4/projectM.h`
Expected: all three paths listed.

> If cmake reports a missing dep (GLM), it is bundled as a projectM submodule; ensure `git submodule update --init --recursive` ran. If `libprojectM_eval.a` is not produced separately (some versions fold eval into the core lib), note that and drop `-lprojectM_eval` from Task 2's link line accordingly.

- [ ] **Step 5: Commit**

```bash
git add .gitmodules lib/projectm/src lib/projectm/build-projectm.sh lib/projectm/.gitignore
git commit -m "build: vendor projectM-4 v4.1.6 source + GLES static-lib build script

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

## Task 2: Wire projectM into the build (`tools/configure`)

**Files:**
- Modify: `tools/configure` — the `rhhconf()` function and the `retro-handheld)` target block (≈ lines 3360-3384)

Reference: trimpod's `tools/configure` `rhhconf` (lines 879-893) and target block (3362-3384).

- [ ] **Step 1: Find the retro-handheld link config in this repo**

Run: `grep -n "rhhconf\|retro-handheld\|RETRO_HANDHELD" tools/configure | head`
Expected: locate `rhhconf()` (the SDL link options helper) and the `retro-handheld)` case (`target_id=122`).

- [ ] **Step 2: Add projectM libs + GLES to the link options**

In `rhhconf()`, where `LDOPTS` is assembled for the SDL app, append the projectM static libs and GL libs. Replace the existing `LDOPTS=...` SDL line so it reads (adapt to the exact existing variable text):
```sh
pmlibs="$rootdir/lib/projectm/lib/libprojectM-4.a $rootdir/lib/projectm/lib/libprojectM_eval.a -lGLESv2 -lEGL -lstdc++ -lstdc++fs"
LDOPTS="$LDOPTS $pmlibs"
```
Link order matters: projectM core → eval → GLESv2 → EGL → stdc++ → stdc++fs. If Task 1 Step 4 found eval folded into the core lib, omit `libprojectM_eval.a`.

- [ ] **Step 3: Add the projectM include path to the target block**

In the `retro-handheld)` case, add to `extradefines`:
```sh
extradefines="$extradefines -I$rootdir/lib/projectm/include"
```

- [ ] **Step 4: Configure and build the stock tree to verify nothing breaks yet**

Run:
```bash
mkdir -p build && cd build && ../tools/configure --target=retro-handheld --type=n && make -j"$(nproc)" 2>&1 | tail -20; cd ..
```
(Use the repo's documented non-interactive configure flags if different; `grep -n "advopts\|--target" tools/configure` to confirm.)
Expected: build **links successfully** (projectM symbols available even though nothing calls them yet — unused libs link clean).

> If the dev environment can't run a full device build, at minimum run `../tools/configure --target=retro-handheld` and confirm it generates a `Makefile` with the `-I.../lib/projectm/include` cflag and the projectM `.a` paths in the link line: `grep projectm build/Makefile`.

- [ ] **Step 5: Commit**

```bash
git add tools/configure
git commit -m "build: link projectM-4 + GLES into the retro-handheld target

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

## Task 3: GL-ify the SDL window (`window-sdl.c` / `.h`)

**Files:**
- Modify: `firmware/target/hosted/sdl/window-sdl.c`
- Modify: `firmware/target/hosted/sdl/window-sdl.h`

Reference: trimpod `firmware/target/hosted/sdl/window-sdl.c` (setup 303-343, `sdl_window_render` 249-269, `gl_present_lcd_fade` / `sdl_gl_present_lcd_fade` 173-177, helpers) and `window-sdl.h` (44-50). This repo's current file uses `SDL_Renderer` + `gui_texture` (`sdl_window_setup` ≈209-251, `sdl_window_render` ≈151-166, `rebuild_gui_texture` ≈92).

- [ ] **Step 1: Add the context-bridge declarations to `window-sdl.h`**

Add near the other externs:
```c
extern volatile bool trimpod_viz_active;        /* visualizer owns the GL context */
void  sdl_gl_make_current(void);                /* re-bind the shared GL context to the UI thread */
void *sdl_gl_get_context(void);                 /* the SDL_GLContext, for the render thread */
void  sdl_gl_present_lcd_fade(float fade);      /* present current LCD * fade (entry fade) */
```

- [ ] **Step 2: Create the GLES context at window setup**

In `sdl_window_setup()`:
- Add `SDL_WINDOW_OPENGL` to the window-creation `flags`.
- Before `SDL_CreateWindow`, request GLES 3.0:
```c
SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
```
- After `SDL_CreateWindow`, replace `SDL_CreateRenderer(...)` with:
```c
gl_ctx = SDL_GL_CreateContext(sdlWindow);
if (!gl_ctx) { /* log SDL_GetError(); fall back / abort */ }
SDL_GL_MakeCurrent(sdlWindow, gl_ctx);
SDL_GL_SetSwapInterval(1);
gl_present_init();   /* compile the LCD-quad shader + create the LCD texture (Step 4) */
```
Add file-scope state: `static SDL_GLContext gl_ctx; volatile bool trimpod_viz_active = false;`
Keep `sim_lcd_surface` exactly as-is (the LCD software surface stays the UI's draw target).

- [ ] **Step 3: Port the LCD-quad present from trimpod**

Copy trimpod's GLES2 LCD-present helpers into this file: the shader sources, `gl_present_init()`, `gl_present_lcd_fade(SDL_Surface*, float)`, and the texture upload. These draw `sim_lcd_surface` as a full-window quad with a `u_fade` uniform (nearest filtering, 320×240 → window). Use `#include <GLES2/gl2.h>`. (This replaces the `gui_texture`/`SDL_UpdateTexture` mechanism.)

- [ ] **Step 4: Rewrite `sdl_window_render()`**

```c
void sdl_window_render(void)
{
    if (trimpod_viz_active)
        return;                       /* visualizer owns the context/window */
    sdl_gl_make_current();
    gl_present_lcd_fade(sim_lcd_surface, 1.0f);
    SDL_GL_SwapWindow(sdlWindow);
}
```
Delete the old `SDL_RenderClear/RenderCopy/RenderPresent` body and the now-unused `gui_texture`/`rebuild_gui_texture`/`sdlRenderer` declarations and their references. Grep the file and `lcd-sdl.c` for `sdlRenderer`, `gui_texture`, `SDL_UpdateTexture`, `SDL_RenderCopy` and remove/redirect each (the LCD upload now happens inside `gl_present_lcd_fade`).

- [ ] **Step 5: Implement the bridge helpers**

```c
void  sdl_gl_make_current(void)      { SDL_GL_MakeCurrent(sdlWindow, gl_ctx); }
void *sdl_gl_get_context(void)       { return gl_ctx; }
void  sdl_gl_present_lcd_fade(float f){ sdl_gl_make_current();
                                        gl_present_lcd_fade(sim_lcd_surface, f);
                                        SDL_GL_SwapWindow(sdlWindow); }
```

- [ ] **Step 6: Reconcile `lcd-sdl.c`**

In `firmware/target/hosted/sdl/lcd-sdl.c` `sdl_gui_update()`, the tail currently does `SDL_UpdateTexture(...)` + `sdl_window_render()`. Remove the `SDL_UpdateTexture(gui_texture, ...)` line (the texture upload now lives in `gl_present_lcd_fade`); keep the `SDL_BlitSurface(... sim_lcd_surface ...)` and the `sdl_window_render()` call.

- [ ] **Step 7: Build and verify the stock UI still renders via GL**

Run: `cd build && make -j"$(nproc)" 2>&1 | tail -20; cd ..`
Expected: compiles and links. (Visual confirmation of the UI happens in Task 12 on target / sim.)

- [ ] **Step 8: Commit**

```bash
git add firmware/target/hosted/sdl/window-sdl.c firmware/target/hosted/sdl/window-sdl.h firmware/target/hosted/sdl/lcd-sdl.c
git commit -m "sdl: present LCD via a GLES textured quad (shared context for the visualizer)

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

## Task 4: PCM tap for the visualizer (`pcm-sdl.c`)

**Files:**
- Modify: `firmware/target/hosted/sdl/pcm-sdl.c`

Reference: trimpod `pcm-sdl.c` (lines 253-292): lock-free SP/SC ring `viz_pcm_ring`, `VIZ_PCM_FRAMES 8192`, `viz_pcm_push()`, `pcm_sdl_viz_latest()`.

- [ ] **Step 1: Locate the SDL audio callback**

Run: `grep -n "sdl_audio_callback\|SDL_MixAudio\|static void.*callback" firmware/target/hosted/sdl/pcm-sdl.c`
Expected: find the function that fills the SDL audio buffer with S16 stereo PCM.

- [ ] **Step 2: Add the ring + producer/consumer**

Copy trimpod's ring block into `pcm-sdl.c` (above the callback): the `VIZ_PCM_FRAMES` ring storage, atomic head/tail indices, `viz_pcm_push(const int16_t *src, unsigned frames)`, and `unsigned pcm_sdl_viz_latest(int16_t *out, unsigned max_frames)`.

- [ ] **Step 3: Call `viz_pcm_push()` from the callback**

In the audio callback, after the S16 stereo frames for this buffer are known (pre-volume source, as trimpod does), call:
```c
viz_pcm_push((const int16_t *)pcm_data, frame_count);
```
(Match the exact buffer/length variables in this repo's callback.)

- [ ] **Step 4: Build to verify it compiles**

Run: `cd build && make -j"$(nproc)" 2>&1 | tail -10; cd ..`
Expected: compiles (consumer is unused until Task 5 — that is fine; mark `pcm_sdl_viz_latest` non-static so it links later).

- [ ] **Step 5: Commit**

```bash
git add firmware/target/hosted/sdl/pcm-sdl.c
git commit -m "sdl: lock-free PCM tap for the visualizer (viz_pcm_push/pcm_sdl_viz_latest)

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

## Task 5: Port the visualizer core (`apps/milkdrop_visualizer.c` / `.h`)

**Files:**
- Create: `apps/milkdrop_visualizer.c` (from trimpod `apps/trimpod_visualizer.c`)
- Create: `apps/milkdrop_visualizer.h` (from trimpod `apps/trimpod_visualizer.h`)
- Modify: `apps/SOURCES`

- [ ] **Step 1: Copy the trimpod visualizer files under the new names**

```bash
cp .../scratchpad/trimpod/apps/trimpod_visualizer.c apps/milkdrop_visualizer.c
cp .../scratchpad/trimpod/apps/trimpod_visualizer.h apps/milkdrop_visualizer.h
```

- [ ] **Step 2: Strip trimpod-UI dependencies**

Edit both files:
- Remove `#include "trimpod_page.h"` and `#include "trimpod_ui.h"`.
- Rename the public symbols: `trimpod_visualizer_run` → `milkdrop_visualizer_run`, `trimpod_visualizer_fade_to_black` → `milkdrop_visualizer_fade_to_black`, `trimpod_visualizer_menu` → `milkdrop_visualizer_menu`. Update the include guard and the `#include "trimpod_visualizer.h"` → `#include "milkdrop_visualizer.h"`.
- Keep `#include "window-sdl.h"` (provides `trimpod_viz_active`, `sdl_gl_make_current`, `sdl_gl_get_context`, `sdl_gl_present_lcd_fade`). These names are unchanged from Task 3.
- Keep the `extern unsigned pcm_sdl_viz_latest(int16_t*, unsigned);` declaration (Task 4).
- In `milkdrop_visualizer_menu()`, **delete the trimpod_ui-based list body** — it is reimplemented in Task 6. For now, stub it:
```c
int milkdrop_visualizer_menu(void) { return 0; }   /* replaced in Task 6 */
```
- Confirm path constants still reference `ROCKBOX_DIR`: `PRESET_DIR = ROCKBOX_DIR "/presets"`, `VIZ_STATE_FILE = ROCKBOX_DIR "/visualizers.txt"`. (`ROCKBOX_DIR` exists in this repo's config; `grep -rn "define ROCKBOX_DIR" firmware/`.)

- [ ] **Step 3: Confirm the preset-cycle interval reads `viz_transition`**

In `viz_render_thread`, the per-preset cycle uses `global_settings.viz_transition`. Leave the reference; the setting is added in Task 7. (Build will fail to find it until Task 7 — so Task 7 must land before the build check in this task. Reorder: do Task 7 Steps 1-2 now, then return. OR temporarily hardcode `#define VIZ_TRANSITION_S 20` and replace with the setting in Task 7. Choose the hardcode to keep tasks independent, then wire the setting in Task 7 Step 4.)
Apply the temporary hardcode:
```c
/* replaced by global_settings.viz_transition in Task 7 */
int transition_s = 20;
```

- [ ] **Step 4: Add the include header API**

Ensure `apps/milkdrop_visualizer.h` declares exactly:
```c
#ifndef MILKDROP_VISUALIZER_H
#define MILKDROP_VISUALIZER_H
#include <stdbool.h>
void milkdrop_visualizer_run(void);
bool milkdrop_visualizer_fade_to_black(void);
int  milkdrop_visualizer_menu(void);
#endif
```

- [ ] **Step 5: Register the source file**

In `apps/SOURCES`, under the SDL-app / retro-handheld condition (mirror how trimpod added it at its line 33), add:
```
milkdrop_visualizer.c
```
Guard it so it only builds for this target if SOURCES is shared across targets (use the same `#if` the keymap uses, e.g. `#if (CONFIG_PLATFORM & PLATFORM_HOSTED)` with `RETRO_HANDHELD`). Confirm by reading the surrounding lines of `apps/SOURCES`.

- [ ] **Step 6: Build**

Run: `cd build && make -j"$(nproc)" 2>&1 | tail -25; cd ..`
Expected: compiles and links against projectM. Resolve any missing-symbol/case errors by matching this repo's `action.h`, `splash.h`, `gui/list.h` signatures.

- [ ] **Step 7: Commit**

```bash
git add apps/milkdrop_visualizer.c apps/milkdrop_visualizer.h apps/SOURCES
git commit -m "viz: port the Milkdrop/projectM visualizer (trimpod-UI deps removed)

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

## Task 6: Per-preset on/off list with stock Rockbox UI

**Files:**
- Modify: `apps/milkdrop_visualizer.c` (`milkdrop_visualizer_menu`)

The visualizer already scans presets into its name table and persists DISABLED basenames to `visualizers.txt` (functions `scan_presets`, `load_enabled_state`, plus the per-preset enabled flag array — keep these). Reimplement only the interactive list using `gui_synclist` (stock).

- [ ] **Step 1: Implement the list with `gui_synclist`**

Replace the Task-5 stub with a stock simplelist that shows each preset name with an on/off marker and toggles the enabled flag on select, saving state on exit. Pattern (adapt names to the existing enabled-state API in the file):
```c
#include "gui/list.h"
#include "lang.h"
#include "settings.h"

static const char* viz_list_get_name(int selected, void *data, char *buf, size_t buflen)
{
    (void)data;
    snprintf(buf, buflen, "[%c] %s",
             viz_preset_enabled(selected) ? 'x' : ' ',
             viz_preset_name(selected));
    return buf;
}

int milkdrop_visualizer_menu(void)
{
    struct gui_synclist lists;
    int action;
    int count = viz_preset_count();
    gui_synclist_init(&lists, viz_list_get_name, NULL, false, 1, NULL);
    gui_synclist_set_nb_items(&lists, count);
    gui_synclist_draw(&lists);
    while (1) {
        action = get_action(CONTEXT_LIST, HZ/2);
        if (gui_synclist_do_button(&lists, &action, LIST_WRAP_UNLESS_HELD))
            continue;
        switch (action) {
            case ACTION_STD_OK:
                viz_preset_toggle(gui_synclist_get_sel_pos(&lists));
                gui_synclist_draw(&lists);
                break;
            case ACTION_STD_CANCEL:
                viz_save_enabled_state();   /* writes visualizers.txt */
                return 0;
        }
    }
}
```
If the file's existing helpers have different names, either add the thin wrappers (`viz_preset_count/name/enabled/toggle`, `viz_save_enabled_state`) around the existing arrays/functions, or inline the array accesses. Keep it DRY with the existing `scan_presets`/state code.

- [ ] **Step 2: Build**

Run: `cd build && make -j"$(nproc)" 2>&1 | tail -15; cd ..`
Expected: compiles and links.

- [ ] **Step 3: Commit**

```bash
git add apps/milkdrop_visualizer.c
git commit -m "viz: per-preset enable/disable list using stock gui_synclist

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

## Task 7: Visualizer setting + settings submenu

**Files:**
- Modify: `apps/settings.h` (add field to `struct user_settings`)
- Modify: `apps/settings_list.c` (declare the setting + value table)
- Create: `apps/menus/visualizer_menu.c`
- Modify: `apps/SOURCES` (add the menu source)
- Modify: `apps/menus/settings_menu.c` (hook the submenu in)
- Modify: `apps/milkdrop_visualizer.c` (use the setting)

Reference: trimpod `apps/settings_list.c:146,661-668`, `apps/settings.h:439-440`, `apps/menus/visualizer_menu.c`.

- [ ] **Step 1: Add the setting field**

In `apps/settings.h`, inside `struct user_settings` (near other playback/display ints):
```c
int viz_transition;   /* seconds between Milkdrop preset switches; 0 = never */
```

- [ ] **Step 2: Declare the setting + value table**

In `apps/settings_list.c`:
- Add the value table (copy trimpod's): 
```c
static const int viz_transition_values[] = {0,5,10,15,20,30,45,60,75,90};
```
- Add the setting entry (use the table-based int setting macro this repo uses, e.g. `TABLE_SETTING` / `STRINGCHOICE_SETTING`; match neighbours). Default `20`:
```c
TABLE_SETTING(F_ALLOW_ARBITRARY_VALS, viz_transition, LANG_VIZ_TRANSITION, 20,
              "viz transition", "0,5,10,15,20,30,45,60,75,90",
              UNIT_SEC, NULL, NULL, NULL, 10,
              0,5,10,15,20,30,45,60,75,90),
```
If adding a `LANG_*` is heavy, use a literal English string via the repo's pattern for untranslated settings (grep an existing `"viz`-style or plain-string setting). Keep it consistent with this repo.

- [ ] **Step 3: Create the submenu**

Create `apps/menus/visualizer_menu.c`:
```c
#include "config.h"
#include "settings.h"
#include "menu.h"
#include "lang.h"
#include "milkdrop_visualizer.h"

MENUITEM_SETTING(viz_transition_item, &global_settings.viz_transition, NULL);
MENUITEM_FUNCTION(viz_presets_item, 0, ID2P(LANG_VIZ_PRESETS),
                  (menu_function)milkdrop_visualizer_menu, NULL, Icon_NOICON);
MAKE_MENU(visualizer_menu, ID2P(LANG_VIZ_MENU), NULL, Icon_Playback,
          &viz_transition_item, &viz_presets_item);
```
Add `LANG_VIZ_TRANSITION`, `LANG_VIZ_PRESETS`, `LANG_VIZ_MENU` to `apps/lang/english.lang` (copy the trimpod IDs/strings), or use literal strings if this repo allows non-lang menu titles (grep `MAKE_MENU` usages).

- [ ] **Step 4: Use the setting in the visualizer**

In `apps/milkdrop_visualizer.c`, replace the Task-5 hardcode:
```c
int transition_s = global_settings.viz_transition;
```

- [ ] **Step 5: Register + hook the menu**

- `apps/SOURCES`: add `menus/visualizer_menu.c` next to the other `menus/*.c`.
- In `apps/menus/settings_menu.c`, add `extern struct menu_item_ex visualizer_menu;` and insert `&visualizer_menu` into the appropriate parent menu's item list (e.g. the General Settings or Playback menu — pick the one matching trimpod's placement / nearest stock fit).

- [ ] **Step 6: Build**

Run: `cd build && make -j"$(nproc)" 2>&1 | tail -20; cd ..`
Expected: compiles and links; the new setting persists via the standard settings file automatically.

- [ ] **Step 7: Commit**

```bash
git add apps/settings.h apps/settings_list.c apps/menus/visualizer_menu.c apps/menus/settings_menu.c apps/SOURCES apps/lang/english.lang apps/milkdrop_visualizer.c
git commit -m "viz: add Visualizer settings submenu (transition time + preset on/off)

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

## Task 8: X+Y action + keymap binding

**Files:**
- Modify: `apps/actions.h`
- Modify: `apps/keymaps/keymap-retro-handheld.c`

- [ ] **Step 1: Define the action**

In `apps/actions.h`, in the standard/global action enum (near `ACTION_STD_*`):
```c
ACTION_STD_VIZ_TOGGLE,
```

- [ ] **Step 2: Bind X+Y in the standard (global) context**

In `apps/keymaps/keymap-retro-handheld.c`, find `button_context_standard[]` (the `CONTEXT_STD` table). Add, ordered so the combo is matched before any single X/Y entry:
```c
{ ACTION_STD_VIZ_TOGGLE, BUTTON_X|BUTTON_Y, BUTTON_NONE },
```
Confirm `get_context_mapping()` returns `button_context_standard` for `CONTEXT_STD` (it does per the map). Do **not** modify `button_context_wps` single-X (ID3) / single-Y (browse) entries.

- [ ] **Step 3: Build**

Run: `cd build && make -j"$(nproc)" 2>&1 | tail -10; cd ..`
Expected: compiles. (No handler yet — that's Task 9. The action is defined and bound but currently a no-op.)

- [ ] **Step 4: Commit**

```bash
git add apps/actions.h apps/keymaps/keymap-retro-handheld.c
git commit -m "input: bind X+Y to ACTION_STD_VIZ_TOGGLE (global context)

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

## Task 9: Launch the visualizer on X+Y; remove any timeout auto-start

**Files:**
- Modify: a global action-dispatch site that sees `CONTEXT_STD` actions everywhere (see Step 1)
- Modify: `apps/gui/wps.c` (verify/remove timeout auto-start)

- [ ] **Step 1: Find the global action sink**

`ACTION_STD_VIZ_TOGGLE` arrives wherever `get_action(CONTEXT_STD, …)` bubbles up. The robust place to catch it globally is the central button/action path. Run:
```bash
grep -rn "ACTION_STD_CANCEL\|default_event_handler\|gui_synclist_do_button" apps/misc.c apps/screens.c apps/root_menu.c | head
```
Find `default_event_handler()` in `apps/misc.c` (this is the global handler Rockbox calls for unhandled actions across screens).

- [ ] **Step 2: Handle the toggle in `default_event_handler()`**

In `apps/misc.c` `default_event_handler()` (or `default_event_handler_ex`), add a case:
```c
#include "milkdrop_visualizer.h"
#include "audio.h"
...
    case ACTION_STD_VIZ_TOGGLE:
        if (audio_status() & AUDIO_STATUS_PLAY) {
            if (!milkdrop_visualizer_fade_to_black())   /* false = not cancelled */
                milkdrop_visualizer_run();              /* blocks; X+Y or Back exits */
        }
        return SYS_EVENT;   /* swallow */
```
Match the function's existing return convention (look at sibling cases). The visualizer's own loop handles exit (Back, and X+Y inside the session — confirm `milkdrop_visualizer_run` polls `get_action` and treats `BUTTON_X|BUTTON_Y` / `ACTION_STD_CANCEL` as exit; if it only checks Back, add an X+Y exit check there).

- [ ] **Step 3: Ensure X+Y also exits from inside the visualizer**

In `apps/milkdrop_visualizer.c` `milkdrop_visualizer_run()` input poll, confirm/add: exit when the action is the back/cancel action OR when `button_status()` shows `BUTTON_X|BUTTON_Y`. Keep behavior: single press of X+Y toggles off.

- [ ] **Step 4: Remove timeout auto-start (regression guard)**

This repo is stock and has no `viz_start_delay`, so there should be nothing to remove. Verify:
```bash
grep -rn "viz_start_delay\|button_last_activity_tick\|auto-start the visualizer" apps/gui/wps.c
```
Expected: no matches. If any trimpod-style timeout block was inadvertently introduced, delete it.

- [ ] **Step 5: Build**

Run: `cd build && make -j"$(nproc)" 2>&1 | tail -15; cd ..`
Expected: compiles and links.

- [ ] **Step 6: Commit**

```bash
git add apps/misc.c apps/milkdrop_visualizer.c
git commit -m "viz: launch Milkdrop on X+Y when playing; X+Y/Back exits

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

## Task 10: Ship presets + packaging changes (drop scaler shim)

**Files:**
- Create: `apps/milkdrop_presets/*.milk` (+ `LICENSE.md`)
- Modify: `tools/buildzip.pl` (or the packaging make) to install presets into the `.rockbox` tree at `presets/`
- Modify: `packaging/retro-handheld/retro-handheld.make` — remove `libsdl2_scaler.so` build
- Modify: `packaging/retro-handheld/Rockbox.sh`, `launch.sh`, `mux_launch.sh` — remove `LD_PRELOAD` of the scaler

- [ ] **Step 1: Copy the presets into the repo**

```bash
mkdir -p apps/milkdrop_presets
cp .../scratchpad/trimpod/assets/presets/*.milk apps/milkdrop_presets/
cp .../scratchpad/trimpod/assets/presets/LICENSE.md apps/milkdrop_presets/
ls apps/milkdrop_presets/*.milk | wc -l    # expect 100
```

- [ ] **Step 2: Install presets into the runtime `.rockbox/presets`**

The visualizer loads from `ROCKBOX_DIR/presets`. Make the build copy them there. In `tools/buildzip.pl`, the `@userstuff` glob already handles top-level dirs; add a `presets` copy step modeled on the existing `themes`/`wps` handling:
```perl
glob_mkdir("$temp_dir/presets");
glob_copy("$ROOT/apps/milkdrop_presets/*.milk", "$temp_dir/presets/");
glob_copy("$ROOT/apps/milkdrop_presets/LICENSE.md", "$temp_dir/presets/");
```
(Match the actual helper names in this repo's `buildzip.pl`; `grep -n "glob_copy\|glob_mkdir\|userstuff" tools/buildzip.pl`.)

- [ ] **Step 3: Remove the scaler shim from packaging**

In `packaging/retro-handheld/retro-handheld.make`, delete the `gcc -shared ... libsdl2_scaler.so ...` build line and any rule that copies it into the pak.

- [ ] **Step 4: Remove the LD_PRELOAD from launchers**

In `packaging/retro-handheld/Rockbox.sh` (and `launch.sh`, `mux_launch.sh` if they set it): remove `export LD_PRELOAD="$GAMEDIR/lib/libsdl2_scaler.so"`. Keep `SDL_DEVICE_WIDTH/HEIGHT` and the `--zoom`/window handling — the GL quad upscales, but the launcher should still create a fullscreen-sized window (verify the window size args still make sense without the shim; if the shim was the only thing forcing fullscreen, set the SDL window to the panel resolution via the existing `ZOOMVAL`/env path).

- [ ] **Step 5: Build the zip/pak**

Run the packaging target this repo uses (e.g. `cd build && make zip` or the `retro-handheld.make` `portmaster`/`nextui` target). 
Expected: produces the pak/zip; confirm `presets/` with 100 `.milk` files is inside, and `libsdl2_scaler.so` is absent.
```bash
unzip -l build/rockbox.zip | grep -c '\.milk'      # expect 100
unzip -l build/*.zip | grep -i scaler               # expect no matches
```

- [ ] **Step 6: Commit**

```bash
git add apps/milkdrop_presets tools/buildzip.pl packaging/retro-handheld/
git commit -m "pkg: ship 100 Milkdrop presets to .rockbox/presets; drop libsdl2_scaler shim

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

## Task 11: Bundle Obsede2 as the default theme

**Files:**
- Create (bundled into build): `wps/Obsede2.wps`, `wps/Obsede2.sbs`, `wps/Obsede2/*`, `fonts/*SF-Pro*`, `icons/icons_5px.bmp`, `themes/Obsede2.cfg`
- Create: `packaging/retro-handheld/config.cfg` (default settings applying the theme)
- Modify: packaging to ship `config.cfg` into the pak's `.rockbox/`

Reference: sanitized theme at `…/scratchpad/theme/.rockbox`; default-config precedent `packaging/rgnano/config.cfg`.

- [ ] **Step 1: Copy the sanitized theme into the source tree**

```bash
T=.../scratchpad/theme/.rockbox
cp "$T"/wps/Obsede2.wps "$T"/wps/Obsede2.sbs wps/
cp -r "$T"/wps/Obsede2 wps/
cp "$T"/fonts/*.fnt fonts/
cp "$T"/icons/icons_5px.bmp icons/
cp "$T"/themes/Obsede2.cfg themes/ 2>/dev/null || { mkdir -p themes; cp "$T"/themes/Obsede2.cfg themes/; }
```
Confirm these dirs are bundled by `tools/buildzip.pl` `@userstuff` (`fonts`, `icons`, `themes`, `wps` are listed) — so they ship into `.rockbox/` automatically.

- [ ] **Step 2: Verify the theme fonts cover the default**

The theme's `.cfg` uses `19-SF-Pro-Display-Regular.fnt`. Confirm it was copied. (Large CJK fonts are included; keep them — they're part of the theme.)

- [ ] **Step 3: Create the default `config.cfg`**

Create `packaging/retro-handheld/config.cfg` (model on `packaging/rgnano/config.cfg`), applying the Obsede2 theme so it's the default on first boot:
```
wps: /.rockbox/wps/Obsede2.wps
sbs: /.rockbox/wps/Obsede2.sbs
font: /.rockbox/fonts/19-SF-Pro-Display-Regular.fnt
iconset: /.rockbox/icons/icons_5px.bmp
selector type: bar (solid colour)
line selector start color: 101010
line selector end color: 000000
line selector text color: FFFFFF
foreground color: FFFFFF
background color: 000000
statusbar: off
scrollbar: off
battery display: graphical
show icons: on
```

- [ ] **Step 4: Ship `config.cfg` into `.rockbox/`**

In the packaging (mirror how `rgnano` installs its `config.cfg`; `grep -rn "config.cfg" tools/ packaging/`), add a copy of `packaging/retro-handheld/config.cfg` into the pak's `.rockbox/config.cfg`. This file is Rockbox's settings file, so its contents become the defaults on a fresh install.

- [ ] **Step 5: Build the pak and verify the theme + default config are present**

Run the packaging target.
```bash
unzip -l build/*.zip | grep -E 'Obsede2|config.cfg'
```
Expected: `wps/Obsede2.wps`, `wps/Obsede2.sbs`, `wps/Obsede2/…`, `themes/Obsede2.cfg`, fonts, `icons_5px.bmp`, and `.rockbox/config.cfg` all present.

- [ ] **Step 6: Commit**

```bash
git add wps/Obsede2.wps wps/Obsede2.sbs wps/Obsede2 fonts icons/icons_5px.bmp themes/Obsede2.cfg packaging/retro-handheld/config.cfg
git commit -m "theme: bundle Obsede2 and set it as the default via shipped config.cfg

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

## Task 12: Full build + on-target verification

**Files:** none (verification only)

- [ ] **Step 1: Clean build from scratch**

```bash
rm -rf build && mkdir build && cd build
../tools/configure --target=retro-handheld && make -j"$(nproc)" 2>&1 | tee /tmp/build.log | tail -25
cd ..
grep -iE "error:|undefined reference" /tmp/build.log || echo "CLEAN BUILD"
```
Expected: `CLEAN BUILD`, projectM linked, pak produced.

- [ ] **Step 2: Deploy to the H700 device** (manual, per the repo's deploy flow)

Install the pak; boot Rockbox.

- [ ] **Step 3: On-target behavior checklist**

- [ ] Stock UI renders correctly through the new GLES present path (menus, browser, Now Playing) — no corruption, correct upscale to panel.
- [ ] **Obsede2 is the default theme** on a fresh config (SF-Pro font, custom WPS/SBS, black background).
- [ ] Start playback. Press **X+Y** → fade-to-black → Milkdrop appears, audio-reactive.
- [ ] Presets **cycle** on the configured interval (Settings → Visualizer → transition time).
- [ ] **X+Y** again (and **Back**) exits the visualizer back to the prior screen.
- [ ] X+Y with **nothing playing** → no-op.
- [ ] Settings → Visualizer → preset list toggles individual presets; disabled ones are skipped; state **persists** across a relaunch (`visualizers.txt`).
- [ ] Audio playback is unaffected (no stutter) while the visualizer runs.
- [ ] **Regression:** single **X** still opens ID3 screen and single **Y** still opens browse in Now Playing.

- [ ] **Step 4: If the bound-FBO render path is wrong** (visualizer black or renders to the wrong target)

Apply the projectM FBO fix noted in the spec §3: either patch projectM's `projectm_opengl_render_frame` to honor the caller-bound framebuffer, or change `viz_render_thread` to render projectM to the default framebuffer at full window res and drop the 1/4-res FBO upscale. Rebuild, recheck Step 3.

- [ ] **Step 5: Final commit / tag**

```bash
git commit --allow-empty -m "test: on-target verification of Milkdrop visualizer + Obsede2 theme

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

## Self-Review Notes

- **Spec coverage:** §1 GLES present→T3; §2 PCM tap→T4; §3 visualizer→T5/T6 (+FBO detail T12 S4); §4 projectM→T1/T2; §5 settings/menu→T7; §6 X+Y→T8/T9; §7 build/pkg→T2/T10; §8 presets→T10; §9 Obsede2 theme→T11. All sections mapped.
- **Ordering dependency:** T5 references `viz_transition` (T7) — resolved by hardcoding in T5 S3 then wiring in T7 S4. T9 depends on the visualizer's exit handling (T5/T6) and the action (T8).
- **Symbol consistency:** public API `milkdrop_visualizer_run/_fade_to_black/_menu`; bridge `trimpod_viz_active`, `sdl_gl_make_current/_get_context/_present_lcd_fade`; PCM `viz_pcm_push`/`pcm_sdl_viz_latest`; setting `global_settings.viz_transition`; action `ACTION_STD_VIZ_TOGGLE` — used consistently across tasks.
- **Known non-blocking risk:** projectM bound-FBO behavior (T12 S4 fallback); launcher fullscreen sizing after dropping the shim (T10 S4).
