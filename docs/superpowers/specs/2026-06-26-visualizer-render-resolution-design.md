# Visualizer Render Resolution Setting

## Summary

Add a user-facing setting that controls the render resolution of the Milkdrop/projectM
visualizer. Today the visualizer always renders into an offscreen FBO at **1/4** of the
display resolution and nearest-neighbour upscales to fill the window (`VIZ_DIVISOR 4` in
`apps/milkdrop_visualizer.c`). The new setting lets the user bypass that low-res path and
render at full (or half) display resolution for sharper visuals, at the cost of GPU load.

## Goals

- Expose render resolution as a choice in **Settings > Visualizer**.
- Offer **Full / Half / Quarter** (divisor 1 / 2 / 4).
- Default to **Quarter** so existing behavior and performance are unchanged for everyone
  who doesn't opt in.
- Take effect the next time the visualizer is opened (consistent with the existing
  "Preset Switch Interval" setting; no live mid-session change required).

## Non-goals

- No change to the projectM warp **mesh size** (`48x36`); the mesh grid is independent of
  pixel resolution.
- No FBO-bypass "render projectM straight to the screen" fast path. The fade-to-black
  transition shader (`viz_gl_present`, `u_fade`) still needs the present pass, so the FBO
  stays in the pipeline at every resolution.
- No live, mid-session resolution switching.

## Setting

`apps/settings.h` — add to `struct user_settings`, next to `viz_transition`:

```c
int viz_resolution;   /* Milkdrop render divisor: 0=Full, 1=Half, 2=Quarter */
```

`apps/settings_list.c` — a `CHOICE_SETTING` storing the choice index (0/1/2), config
key `"viz resolution"`, default index **2** (Quarter), read directly (no callback):

```c
CHOICE_SETTING(0, viz_resolution, LANG_VIZ_RESOLUTION, 2, "viz resolution",
               "full,half,quarter", NULL, 3,
               ID2P(LANG_VIZ_RES_FULL), ID2P(LANG_VIZ_RES_HALF),
               ID2P(LANG_VIZ_RES_QUARTER)),
```

`apps/lang/english.lang` — four new phrases following the block added in `899c22f7f4`:

- `LANG_VIZ_RESOLUTION` — "Render Resolution"
- `LANG_VIZ_RES_FULL` — "Full"
- `LANG_VIZ_RES_HALF` — "Half"
- `LANG_VIZ_RES_QUARTER` — "Quarter"

`apps/menus/visualizer_menu.c` — a `MENUITEM_SETTING(viz_resolution_item,
&global_settings.viz_resolution, NULL)` added to `visualizer_settings_menu`, placed
between the transition interval and the Presets entry.

## Render path changes (`apps/milkdrop_visualizer.c`)

The FBO/texture and projectM handle are deliberately **process-lifetime singletons**
(created once, never destroyed — the file's comments forbid tearing them down). So the
setting is applied by **resizing**, not rebuilding.

1. Replace the compile-time `#define VIZ_DIVISOR 4` with a runtime map from the setting
   index to a divisor:

   ```c
   /* setting index 0/1/2 -> divisor 1/2/4 */
   static int viz_divisor(void)
   {
       switch (global_settings.viz_resolution) {
           case 0:  return 1;   /* Full  */
           case 1:  return 2;   /* Half  */
           default: return 4;   /* Quarter (default) */
       }
   }
   ```

   `viz_gl_init()` uses `viz_divisor()` for its initial sizing (first launch).

2. Add `apply_viz_resolution()`, called from `visualizer_session()` after `ensure_init()`
   (alongside `apply_viz_transition()`):

   - recompute `viz_w/viz_h` from the current drawable size (`SDL_GL_GetDrawableSize`)
     divided by `viz_divisor()`;
   - if they differ from the current texture size, rebind `viz_tex` and
     `glTexImage2D(... viz_w, viz_h ...)` to resize it in place — the FBO color
     attachment remains valid, so no FBO/texture re-creation and no singleton teardown;
   - `projectm_set_window_size(pm, viz_w, viz_h)`.

   The per-frame `glViewport(0, 0, viz_w, viz_h)` (render thread) and the present quad
   already read `viz_w/viz_h`, so they adapt automatically.

3. `ensure_init()` still performs the one-time `projectm_set_window_size` for first
   launch; the per-session `apply_viz_resolution()` overrides it whenever the setting
   has changed since.

## Data flow

```
Settings > Visualizer > Render Resolution
        |  (global_settings.viz_resolution, persisted in config.cfg)
        v
visualizer_session()
        |--> ensure_init()              (first launch: build singletons at chosen res)
        |--> apply_viz_resolution()     (every session: resize FBO tex + projectM)
        v
viz_render_thread()  -> glViewport(viz_w,viz_h) -> projectm_opengl_render_frame
        v
viz_gl_present()     -> upscale FBO tex to window (1:1 at Full, fade applied)
```

## Edge cases

- **Full resolution (divisor 1):** the present pass becomes a 1:1 textured blit; the fade
  shader still applies. `GL_NEAREST` filtering is irrelevant at 1:1. Correct, just heavier.
- **Drawable size unknown before init:** `apply_viz_resolution()` runs only after
  `ensure_init()` succeeded, so the GL context and window exist.
- **Setting changed while visualizer is open:** not applied until the next open (documented
  behavior; matches Preset Switch Interval).
- **Invalid/garbage stored value:** `viz_divisor()` falls through to Quarter.

## Testing

This is a hosted SDL retro-handheld target that depends on the Dockerized aarch64 projectM
build, so there is no unit-test harness for this path. Verification follows the repo's
established pattern (clean build + manual on-device check, cf. commit `539d894219`):

1. Clean build of the PortMaster pak with projectM linked.
2. Confirm the new **Render Resolution** item appears in Settings > Visualizer with values
   Full / Half / Quarter and persists across a restart.
3. Launch the visualizer at each setting and confirm: Quarter matches current output, Half
   and Full are visibly sharper, and switching the setting then reopening the visualizer
   changes the resolution with no crash or black screen.
```
