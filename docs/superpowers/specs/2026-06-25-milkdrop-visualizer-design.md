# Milkdrop Visualizer + Obsede2 Default Theme for the IncognitoMan Rockbox Port

- **Date:** 2026-06-25
- **Base repo (fork target):** https://github.com/IncognitoMan/rockbox (`retro-handheld` SDL app target, PortMaster pak)
- **Visualizer/preset source:** https://github.com/tyrannotorus/c-trimui-trimpod-classic-pak (trimpod)
- **Build target:** Allwinner H700, mainline kernel + Panfrost (Mali-G31), custom OS, arm64. Full GLES 3.x available.

## Goal

Fork the IncognitoMan PortMaster Rockbox port and add the trimpod Milkdrop/projectM
visualizer plus its ~100 `.milk` presets, **keeping all stock Rockbox functionality
intact**. The visualizer is toggled on/off with a simultaneous **X + Y** button press
(replacing trimpod's idle-timeout auto-start). Also ship the **Obsede2** theme as the
default theme.

## Non-Goals

- Porting trimpod's custom UI framework (`trimpod_ui`, `trimpod_page`, `trimpod_transition`),
  its iPod-style Now Playing screen, or its spectrum widget. Stock Rockbox UI is preserved.
- Supporting the trimpod idle-timeout auto-start. It is removed; X+Y replaces it.
- Reusing trimpod's prebuilt `libprojectM` static libs (they are tied to the NextUI
  vendor toolchain). projectM-4 is built from source for the target.

## Key Decisions (confirmed)

| Decision | Choice |
|---|---|
| GLES vs software render | **GL-ify the window** (trimpod-style GLES present). Replaces `SDL_Renderer` path; preserves all UI functionality. |
| projectM-4 source | **Build from source** (cmake, `-DENABLE_GLES=ON`) for the arm64/Panfrost toolchain. |
| `libsdl2_scaler.so` shim | **Dropped.** The GLES textured-quad present upscales 320×240→panel itself. |
| Feature scope | **Full parity** with trimpod (per-preset on/off menu + `visualizers.txt`, fade-to-black, transition-time setting) minus the timeout auto-start. |
| X+Y toggle scope | **Global** (any screen), gated on audio playing. No keymap collisions exist for `BUTTON_X\|BUTTON_Y`. |
| Default theme | **Obsede2** (renamed from `Obsede' 2` to avoid shell-quoting bugs), applied via a shipped default `config.cfg`. |

## Architecture Overview

```
                    +------------------------------------------+
   audio callback   |  pcm-sdl.c                               |
   (S16 stereo) ----+--> viz_pcm_push() --> lock-free ring ----+--> pcm_sdl_viz_latest()
                    +------------------------------------------+            |
                                                                           v
  +----------------------+      shared GLES 3.0 context        +-----------------------+
  | window-sdl.c (GL)    |<----------------------------------->| milkdrop_visualizer.c |
  | - LCD -> textured    |   trimpod_viz_active gates present  | - projectM render     |
  |   quad present       |                                     | - 1/4-res FBO upscale |
  | - sdl_gl_make_current|                                     | - GL render thread    |
  | - present_lcd_fade   |                                     | - fade state machine  |
  +----------------------+                                     | - preset scan/cycle   |
            ^                                                  | - visualizers.txt     |
            |                                                  +-----------------------+
   normal UI present                                                      ^
   (lcd_update path)                                                      |
                                                          launched by X+Y action
                                                          (global keymap -> handler)
```

There is **one** shared GLES 3.0 context on the SDL window. Normal UI uploads the
320×240 LCD software surface to a GL texture and draws it as a full-window quad. When
`trimpod_viz_active` is set, normal presents short-circuit and a dedicated GL render
thread owns the context, rendering projectM into a 1/4-res FBO that is upscaled to the
window. Audio reaches projectM through a lock-free PCM tap in `pcm-sdl.c`.

## Components

### 1. GLES present path — `firmware/target/hosted/sdl/window-sdl.c` / `.h`
Port trimpod's GL backend:
- Create the window with `SDL_WINDOW_OPENGL`; request a GLES 3.0 context
  (`SDL_GL_CONTEXT_PROFILE_ES`, major 3, minor 0).
- Replace the `SDL_Renderer` / `SDL_RenderCopy` present with: upload `sim_lcd_surface`
  to a GL texture and draw a full-window quad (nearest upscale). Implement
  `gl_present_lcd_fade(surface, fade)` and route `sdl_window_render()` through it.
- Add the context bridge declared in `window-sdl.h`:
  `extern volatile bool trimpod_viz_active;`, `sdl_gl_make_current()`,
  `sdl_gl_get_context()`, `sdl_gl_present_lcd_fade(float)`.
- `sdl_window_render()` returns early when `trimpod_viz_active` (visualizer owns context).
- Remove all `SDL_Renderer`/`gui_texture` code paths and the `libsdl2_scaler.so`
  assumptions.

### 2. PCM tap — `firmware/target/hosted/sdl/pcm-sdl.c`
Add trimpod's lock-free single-producer/single-consumer ring:
- `viz_pcm_push()` called from `sdl_audio_callback` with the pre-volume S16 stereo data.
- `unsigned pcm_sdl_viz_latest(int16_t *out, unsigned max_frames)` consumer (declared
  `extern` in the visualizer).
- Ring sized `VIZ_PCM_FRAMES 8192`.

### 3. Visualizer — `apps/milkdrop_visualizer.c` / `.h`
Port `trimpod_visualizer.c`, renamed, with trimpod-UI dependencies removed:
- **Keep:** `viz_gl_init`/`viz_gl_present` (FBO + GLES2 upscale shader, `VIZ_DIVISOR 4`),
  `ensure_init` (projectM create/config: mesh 48×36, 60 fps, aspect correction, hard-cut),
  the `viz_render_thread` (PCM feed via `projectm_pcm_add_int16`, `projectm_opengl_render_frame`,
  swap), the fade state machine (`VIZ_NORMAL/FADE_OUT/FADE_IN`), `scan_presets`,
  preset cycling on the `viz_transition` interval, and `visualizers.txt` persistence
  (`PRESET_DIR = ROCKBOX_DIR "/presets"`, `VIZ_STATE_FILE = ROCKBOX_DIR "/visualizers.txt"`).
- **Public API:**
  - `void milkdrop_visualizer_run(void);` — fullscreen session; exits on X+Y or Back.
  - `bool milkdrop_visualizer_fade_to_black(void);` — fade-through-black handoff.
  - `int milkdrop_visualizer_menu(void);` — the per-preset on/off list (see §5).
- **Rework:** the per-preset on/off list currently uses `trimpod_ui`/`trimpod_page`.
  Reimplement with stock Rockbox `gui/list.h` (simplelist) + `action.h`.
- **Do NOT port** `trimpod_transition.c`. The only cross-reference is `trimpod_viz_active`,
  which lives in `window-sdl.c`.
- **projectM render-target detail (resolve during implementation):** confirm whether the
  from-source projectM-4 build's `projectm_opengl_render_frame` honors the caller-bound
  FBO. trimpod patched projectM to composite into the bound FBO. If the stock build does
  not, either (a) apply the same small patch, or (b) render projectM directly to the
  window's default framebuffer at full res and drop the 1/4-res FBO upscale.

### 4. projectM-4 — `lib/projectm/`
- Vendor projectM-4 source (submodule or copied tree) + a build script
  (`lib/projectm/build-projectm.sh`) invoking cmake with `-DENABLE_GLES=ON`,
  `-DBUILD_SHARED_LIBS=OFF`, producing `lib/projectm/lib/libprojectM-4.a` and
  `libprojectM_eval.a`, headers under `lib/projectm/include/projectM-4/`.
- Pin a known-good projectM-4 version (trimpod used 4.1.6).

### 5. Settings + menu — `apps/settings.h`, `apps/settings_list.c`, `apps/menus/`
- Add `viz_transition` (preset cycle seconds) to `struct user_settings` +
  `settings_list.c` (reuse trimpod's `viz_transition_values[] = {0,5,10,15,20,30,45,60,75,90}`).
- **Do NOT add** `viz_start_delay` (timeout removed).
- New `apps/menus/visualizer_menu.c`: a "Visualizer" submenu with the transition-time
  `MENUITEM_SETTING` and a `MENUITEM_FUNCTION` to `milkdrop_visualizer_menu` (per-preset
  on/off). Hook it into the stock settings menu tree (e.g. General/Display settings).

### 6. X+Y toggle — keymap + action + handler
- Define `ACTION_STD_VIZ_TOGGLE` in `apps/actions.h`.
- In `apps/keymaps/keymap-retro-handheld.c`, bind `BUTTON_X|BUTTON_Y` (0x0C) with
  `pre_button = BUTTON_NONE` in the standard/global context, ordered **before** the
  single-button X and Y entries so the combo matches first. (Single X=ID3, Y=browse in
  WPS remain intact.)
- A central handler (global action dispatch) launches `milkdrop_visualizer_run()` when
  `audio_status() & AUDIO_STATUS_PLAY`. Inside the visualizer session, X+Y (or Back)
  exits.
- **Remove** the `ACTION_NONE` idle-timeout auto-start block from `apps/gui/wps.c`.

### 7. Build + packaging — `tools/configure`, `apps/SOURCES`, `packaging/retro-handheld/`
- `tools/configure` `rhhconf()`: add `-I$rootdir/lib/projectm/include` and link
  `libprojectM-4.a libprojectM_eval.a -lGLESv2 -lEGL -lstdc++ -lstdc++fs` (correct order),
  plus `SDL_WINDOW_OPENGL` build requirements.
- `apps/SOURCES`: add `milkdrop_visualizer.c` and `menus/visualizer_menu.c`.
- Packaging: copy the ~100 `.milk` presets to the pak so they land at `ROCKBOX_DIR/presets`
  at runtime; remove the `libsdl2_scaler.so` build/preload from `retro-handheld.make` and
  the launch scripts.

### 8. Presets
- Copy `assets/presets/*.milk` (100 files) from trimpod into the fork
  (`lib/projectm/presets/` or a packaging asset dir) and ship to `ROCKBOX_DIR/presets`.
- `LICENSE.md` for the presets travels with them.

### 9. Default theme — "Obsede2"
- Source: `/home/user1/Downloads/Obsede' 2.zip` (a `.rockbox/` bundle: SF-Pro fonts,
  `icons_5px.bmp`, `wps/`, `sbs/`, `.cfg`).
- **Rename** `Obsede' 2` → `Obsede2` everywhere: the `.cfg`, `.wps`, `.sbs`, the
  `wps/Obsede' 2/` asset folder, and all internal references inside those files.
- Bundle the theme files into the shipped `.rockbox` tree (`fonts/`, `icons/`, `wps/`,
  `themes/`) via the build/packaging (buildzip already globs these dirs).
- Make it the **default on first boot** by shipping a default `config.cfg` (following the
  `packaging/rgnano/config.cfg` precedent, adapted for `retro-handheld`) that applies the
  theme: `wps`, `sbs`, `font`, `iconset`, and the selector/foreground/background colors
  from the theme `.cfg`. Core `settings_list.c` defaults remain stock.

## Data Flow

1. Audio decode → SDL audio callback → `viz_pcm_push()` mirrors S16 stereo into the ring.
2. User presses **X+Y** (audio playing) → `ACTION_STD_VIZ_TOGGLE` → handler →
   `milkdrop_visualizer_fade_to_black()` then `milkdrop_visualizer_run()`.
3. `milkdrop_visualizer_run()` sets `trimpod_viz_active`, releases the GL context, spawns
   `viz_render_thread`.
4. Render thread: `pcm_sdl_viz_latest()` → `projectm_pcm_add_int16()` →
   `projectm_opengl_render_frame()` into FBO → upscale quad → `SDL_GL_SwapWindow()`,
   cycling presets every `viz_transition` seconds (enabled presets per `visualizers.txt`).
5. **X+Y** or **Back** → thread stops, context reclaimed, `trimpod_viz_active` cleared,
   `lcd_update()` repaints the screen the user came from.

## Error Handling / Edge Cases

- projectM init failure → splash an error, clear `trimpod_viz_active`, return to UI.
- No presets found at `ROCKBOX_DIR/presets` → splash "No presets", return.
- All presets disabled in `visualizers.txt` → fall back to treating all as enabled (or
  splash + return); decide in implementation, prefer graceful fallback.
- X+Y with no audio playing → no-op (handler gate).
- GL context loss / make-current failure → bail out of the session cleanly.

## Testing

- **Build:** clean `configure` for `retro-handheld` + `make` succeeds with projectM linked.
- **projectM:** standalone link/load smoke test of the from-source `.a`.
- **On-device / target:** verify (a) stock UI renders correctly via the new GL present
  path, (b) Obsede2 is the default theme on a fresh config, (c) X+Y toggles the visualizer
  while playing and Back/X+Y exits, (d) presets cycle on the configured interval,
  (e) per-preset on/off persists across runs, (f) audio is unaffected.
- **Regression:** confirm single X (ID3) and Y (browse) WPS actions still work; confirm no
  timeout auto-start occurs.

## Open Implementation Detail (tracked, not blocking)

- projectM render-target FBO behavior (see §3) — resolve by testing the from-source build;
  fall back to full-res default-framebuffer render if the bound-FBO path isn't honored.
