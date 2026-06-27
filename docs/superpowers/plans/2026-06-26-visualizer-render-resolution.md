# Visualizer Render Resolution Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a Settings > Visualizer "Render Resolution" choice (Full / Half / Quarter, default Quarter) that controls the projectM render divisor, replacing the hard-coded 1/4-res path.

**Architecture:** A new `CHOICE_SETTING` (`viz_resolution`, stored as index 0/1/2) is read at visualizer-session start. `milkdrop_visualizer.c` maps the index to a divisor (1/2/4) via a `viz_divisor()` helper, and an `apply_viz_resolution()` resizes the existing offscreen FBO texture and re-sets the projectM window size in place — never destroying the process-lifetime GL/projectM singletons. The change takes effect the next time the visualizer opens.

**Tech Stack:** Rockbox C core (settings_list / menu / lang infrastructure), OpenGL ES 2, vendored libprojectM-4, hosted SDL retro-handheld target.

**Testing note:** This target has no unit-test harness (hosted SDL + Dockerized aarch64 projectM build). Verification is a clean build + manual on-device check, the repo's established pattern (cf. commit `539d894219`). Each task below is a self-contained edit + commit; Task 6 is the build/verification gate.

---

### Task 1: Add the `viz_resolution` field to user settings

**Files:**
- Modify: `apps/settings.h` (the Milkdrop visualizer block near `viz_transition`)

- [ ] **Step 1: Add the field**

In `apps/settings.h`, find the existing block:

```c
    /* Milkdrop visualizer */
    int viz_transition;   /* seconds between Milkdrop preset switches; 0 = never */
```

Change it to:

```c
    /* Milkdrop visualizer */
    int viz_transition;   /* seconds between Milkdrop preset switches; 0 = never */
    int viz_resolution;   /* render divisor: 0=Full, 1=Half, 2=Quarter */
```

- [ ] **Step 2: Commit**

```bash
git add apps/settings.h
git commit -m "viz: add viz_resolution user setting field

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 2: Add the language phrases

**Files:**
- Modify: `apps/lang/english.lang` (append after the `LANG_VIZ_PRESETS` phrase added in `899c22f7f4`)

- [ ] **Step 1: Append the four phrases**

Append to the end of `apps/lang/english.lang` (after the closing `</phrase>` of `LANG_VIZ_PRESETS`):

```
<phrase>
  id: LANG_VIZ_RESOLUTION
  desc: Settings > Visualizer > render resolution of the Milkdrop visualizer
  user: core
  <source>
    *: "Render Resolution"
  </source>
  <dest>
    *: "Render Resolution"
  </dest>
  <voice>
    *: "Render Resolution"
  </voice>
</phrase>
<phrase>
  id: LANG_VIZ_RES_FULL
  desc: Settings > Visualizer > Render Resolution: full display resolution
  user: core
  <source>
    *: "Full"
  </source>
  <dest>
    *: "Full"
  </dest>
  <voice>
    *: "Full"
  </voice>
</phrase>
<phrase>
  id: LANG_VIZ_RES_HALF
  desc: Settings > Visualizer > Render Resolution: half display resolution
  user: core
  <source>
    *: "Half"
  </source>
  <dest>
    *: "Half"
  </dest>
  <voice>
    *: "Half"
  </voice>
</phrase>
<phrase>
  id: LANG_VIZ_RES_QUARTER
  desc: Settings > Visualizer > Render Resolution: quarter display resolution
  user: core
  <source>
    *: "Quarter"
  </source>
  <dest>
    *: "Quarter"
  </dest>
  <voice>
    *: "Quarter"
  </voice>
</phrase>
```

- [ ] **Step 2: Commit**

```bash
git add apps/lang/english.lang
git commit -m "lang: add Render Resolution visualizer strings

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 3: Register the CHOICE_SETTING

**Files:**
- Modify: `apps/settings_list.c` (immediately after the `viz_transition` `TABLE_SETTING_LIST` entry)

- [ ] **Step 1: Add the setting entry**

In `apps/settings_list.c`, find the `viz_transition` entry:

```c
    /* Milkdrop visualizer: seconds each preset shows before switching to the
     * next; 0 = "Off" (hold the current preset).  Read directly (no callback). */
    TABLE_SETTING_LIST(F_TIME_SETTING | F_ALLOW_ARBITRARY_VALS,
                viz_transition, LANG_VIZ_TRANSITION, 20, "viz transition",
                off_on, UNIT_SEC, formatter_time_unit_0_is_off,
                getlang_time_unit_0_is_off, NULL, 10, viz_transition_values),
```

Insert immediately after it:

```c
    /* Milkdrop visualizer render resolution: the offscreen FBO is sized to the
     * display resolution divided by 1 (Full) / 2 (Half) / 4 (Quarter).  Stored
     * as the choice index (0/1/2); read directly in milkdrop_visualizer.c. */
    CHOICE_SETTING(0, viz_resolution, LANG_VIZ_RESOLUTION, 2, "viz resolution",
                "full,half,quarter", NULL, 3,
                ID2P(LANG_VIZ_RES_FULL), ID2P(LANG_VIZ_RES_HALF),
                ID2P(LANG_VIZ_RES_QUARTER)),
```

- [ ] **Step 2: Commit**

```bash
git add apps/settings_list.c
git commit -m "viz: register viz_resolution choice setting (Full/Half/Quarter)

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 4: Add the menu item

**Files:**
- Modify: `apps/menus/visualizer_menu.c`

- [ ] **Step 1: Add the MENUITEM_SETTING and wire it into the menu**

In `apps/menus/visualizer_menu.c`, find:

```c
MENUITEM_SETTING(viz_transition_item, &global_settings.viz_transition, NULL);
MENUITEM_FUNCTION(viz_presets_item, 0, ID2P(LANG_VIZ_PRESETS),
                  milkdrop_visualizer_menu, NULL, Icon_Playback_menu);

MAKE_MENU(visualizer_settings_menu, ID2P(LANG_VISUALIZER), NULL,
          Icon_Playback_menu,
          &viz_transition_item, &viz_presets_item);
```

Replace it with:

```c
MENUITEM_SETTING(viz_transition_item, &global_settings.viz_transition, NULL);
MENUITEM_SETTING(viz_resolution_item, &global_settings.viz_resolution, NULL);
MENUITEM_FUNCTION(viz_presets_item, 0, ID2P(LANG_VIZ_PRESETS),
                  milkdrop_visualizer_menu, NULL, Icon_Playback_menu);

MAKE_MENU(visualizer_settings_menu, ID2P(LANG_VISUALIZER), NULL,
          Icon_Playback_menu,
          &viz_transition_item, &viz_resolution_item, &viz_presets_item);
```

- [ ] **Step 2: Commit**

```bash
git add apps/menus/visualizer_menu.c
git commit -m "viz: add Render Resolution to the Visualizer settings menu

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 5: Drive the render divisor from the setting

**Files:**
- Modify: `apps/milkdrop_visualizer.c` (the `VIZ_DIVISOR` define + `viz_gl_init`, plus a new helper and a new per-session apply function and its call site)

- [ ] **Step 1: Replace the compile-time divisor with a runtime helper**

Find (around line 71-79):

```c
/* projectM renders at 1/4 screen resolution into an offscreen FBO, which is then
 * nearest-neighbour upscaled to fill the window.  Our libprojectM is patched to
 * composite into the caller's bound framebuffer, so render_frame() lands in our
 * FBO. */
#define VIZ_DIVISOR     4
static GLuint viz_fbo, viz_tex, viz_prog, viz_vbo;
```

Replace with:

```c
/* projectM renders into an offscreen FBO at the display resolution divided by a
 * user-selected divisor (Render Resolution: Full=1 / Half=2 / Quarter=4), which
 * is then nearest-neighbour upscaled to fill the window.  Our libprojectM is
 * patched to composite into the caller's bound framebuffer, so render_frame()
 * lands in our FBO. */
static GLuint viz_fbo, viz_tex, viz_prog, viz_vbo;

/* Map the Render Resolution choice index (0/1/2) to the FBO downscale divisor.
 * Unknown/garbage values fall through to Quarter (the default low-res path). */
static int viz_divisor(void)
{
    switch (global_settings.viz_resolution)
    {
        case 0:  return 1;   /* Full    */
        case 1:  return 2;   /* Half    */
        default: return 4;   /* Quarter */
    }
}
```

- [ ] **Step 2: Use `viz_divisor()` for the initial sizing in `viz_gl_init`**

In `viz_gl_init()`, find:

```c
    SDL_GL_GetDrawableSize(sdlWindow, &win_w, &win_h);
    viz_w = win_w / VIZ_DIVISOR;
    viz_h = win_h / VIZ_DIVISOR;
```

Replace with:

```c
    SDL_GL_GetDrawableSize(sdlWindow, &win_w, &win_h);
    int divisor = viz_divisor();
    viz_w = win_w / divisor;
    viz_h = win_h / divisor;
```

- [ ] **Step 3: Add `apply_viz_resolution()` next to `apply_viz_transition()`**

Insert this function immediately after `apply_viz_transition()` (after its closing brace, before `ensure_init`):

```c
/* Apply the "Render Resolution" setting: resize the offscreen FBO texture to the
 * current drawable size divided by the chosen divisor and tell projectM the new
 * render size.  Resizes the texture IN PLACE (the FBO colour attachment stays
 * valid) so the process-lifetime GL/projectM singletons are never destroyed.
 * No-op when the size is unchanged.  Called per session, so a setting change
 * takes effect the next time the visualizer is opened. */
static void apply_viz_resolution(void)
{
    int win_w, win_h;
    SDL_GL_GetDrawableSize(sdlWindow, &win_w, &win_h);
    int divisor = viz_divisor();
    int new_w = win_w / divisor;
    int new_h = win_h / divisor;

    if (new_w == viz_w && new_h == viz_h)
        return;

    viz_w = new_w;
    viz_h = new_h;

    glBindTexture(GL_TEXTURE_2D, viz_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, viz_w, viz_h, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, NULL);

    projectm_set_window_size(pm, viz_w, viz_h);
}
```

- [ ] **Step 4: Call `apply_viz_resolution()` in `visualizer_session()`**

In `visualizer_session()`, find:

```c
    apply_viz_transition();   /* pick up the current "Visualization Transition" setting */
```

Replace with:

```c
    apply_viz_transition();   /* pick up the current "Visualization Transition" setting */
    apply_viz_resolution();   /* pick up the current "Render Resolution" setting */
```

- [ ] **Step 5: Commit**

```bash
git add apps/milkdrop_visualizer.c
git commit -m "viz: drive FBO render resolution from the viz_resolution setting

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 6: Clean build + manual verification

**Files:** none (build/verification gate)

- [ ] **Step 1: Clean build of the PortMaster pak**

Run the repo's Dockerized aarch64 build (the same flow used by commit `539d894219` /
`dab02d278a`). Expected: build completes with the projectM-linked retro-handheld target
and produces the pak zip with no compile/link errors. In particular confirm
`apps/milkdrop_visualizer.c`, `apps/settings_list.c`, and `apps/menus/visualizer_menu.c`
compile and that `genlang` accepts the new phrases.

- [ ] **Step 2: Manual on-device / hosted check**

1. Open **Settings > Visualizer** and confirm a **Render Resolution** item is present with
   values **Full / Half / Quarter**, defaulting to **Quarter**.
2. Set it, exit settings, reopen settings — confirm the value persisted (written to
   `config.cfg` as `viz resolution: ...`).
3. Launch the visualizer (X+Y while playing) at each setting:
   - **Quarter** looks identical to current output.
   - **Half** and **Full** are visibly sharper.
   - Changing the setting and reopening the visualizer changes resolution with no crash
     and no black screen (fade-to-black transitions still work at every resolution).

- [ ] **Step 3: No code commit** (verification only). If the build surfaces issues, fix them
  in the relevant task's file and amend/add a commit there.

---

## Self-Review Notes

- **Spec coverage:** setting field (Task 1) ✓, lang phrases (Task 2) ✓, CHOICE_SETTING with
  default Quarter (Task 3) ✓, menu placement between transition and Presets (Task 4) ✓,
  `viz_divisor()` + in-place `apply_viz_resolution()` + call site + `viz_gl_init` initial
  sizing (Task 5) ✓, clean-build + manual verification (Task 6) ✓.
- **Non-goals honored:** mesh size untouched; FBO/present pass retained; no live
  mid-session switch (applied at session start only).
- **Type consistency:** field `viz_resolution` (int, index 0/1/2); helper `viz_divisor()`
  returns 1/2/4; `apply_viz_resolution()` matches the `apply_viz_transition()` call
  convention. Lang IDs `LANG_VIZ_RESOLUTION`, `LANG_VIZ_RES_FULL/HALF/QUARTER` are used
  consistently across Tasks 2-4.
