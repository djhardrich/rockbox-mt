# Aurora / Aurora Light — New Default Theme

- **Date:** 2026-07-03
- **Target:** `retro-handheld` PortMaster fork, 320×240 software LCD canvas
  (`firmware/export/config/retro-handheld.h`), GPU-upscaled 2x to the
  device's physical 640×480 panel. Non-touch, button-navigated (see
  `apps/keymaps/keymap-retro-handheld.c`).
- **Replaces:** Obsede2 as the shipped default theme
  (`packaging/retro-handheld/config.cfg`, `themes/Obsede2.cfg`).

## Goal

Design and ship a new default WPS/SBS theme pair — **Aurora** (dark) and
**Aurora Light** — reflecting current (2026) dark-first UI conventions:
neumorphic "soft UI" panels, a warm single accent color, and restrained,
distinctive (non-Apple-system) typography. Same screen coverage as the
theme it replaces, same `.cfg`/`.wps`/`.sbs` file conventions already used
by Obsede2, so it drops into the existing default-theme mechanism with no
firmware changes.

## Non-Goals

- No change to `LCD_WIDTH`/`LCD_HEIGHT` (stays 320×240; native 640×480
  rendering was considered and explicitly rejected — out of scope, much
  larger blast radius than "a new theme").
- No OS-level automatic dark/light switching — Rockbox has none. Aurora
  Light is a second, fully independent theme the user selects from the
  Theme menu, same as picking any other theme.
- No changes to the Milkdrop visualizer's own UI (separate subsystem).
- Not reworking Obsede2's underlying viewport/tag conventions — Aurora
  follows the same WPS/SBS structure Obsede2 already established for this
  fork, just with new assets and layout.

## Visual Language

| Element | Choice |
|---|---|
| Direction | Neumorphic Soft — extruded slate panels, soft embossed shadows (light source top-left), 14–20px rounded corners, no hard edges or drop-shadow-free flat blocks. |
| Accent | Sunset Amber, `#ff8a65 → #ffca28` gradient. Used on: progress/volume bar fill, album-art fallback glyph, selection highlight, "battery OK" state. |
| Dark palette | Base `#1c1e26`, panel gradients `#20222c → #191a22` / `#242631 → #171820`, primary text `#e7e6ee`, secondary text `#8a8ca3`. |
| Light palette (Aurora Light) | Inverted slate→cream base (e.g. base `#f4efe8`, panels `#ffffff → #ece6db`), same amber accent, shadow tone re-tuned for a light source (light mode neumorphism needs warmer/darker shadow tones than a naive color-invert to still read as "soft," not muddy). |
| Typography | Bricolage Grotesque (SIL OFL, Google Fonts) — quirky angled bowls, distinctly not system-UI. Replaces the SF Pro Display family Obsede2 uses. CJK fallback keeps the existing bundled CJK faces (Bricolage has no CJK coverage). |
| Iconography | New rounded glyph set, not a recolor of `icons_5px.bmp`. Procedurally generated (vector-drawn, rasterized to Rockbox's indexed BMP format) rather than hand pixel-art. |
| Battery | Graphical battery icon (12-frame `battery.bmp`/`battery_charging.bmp` strip, procedurally drawn to match the neumorphic style — outline + amber fill bar, bolt overlay when charging), matching Obsede2's graphical-battery convention rather than percentage text. |

## Screen Inventory

Matches Obsede2's existing coverage for this fork — same screens, new look:

1. **Lockscreen** — large clock, day-of-week, playback-mode glyph, lock
   indicator, dim "now playing" line. (Obsede2: `wps/Obsede2.wps`
   `lockscreen` viewport group.)
2. **Now Playing (WPS)** — art panel (or amber-glyph fallback when no
   album art) + neumorphic info panel (title / artist / album), amber
   progress bar (soft inset track, per the approved mockup), elapsed/
   remaining time, percentage battery, clock.
3. **Menu / status bar (SBS)** — panel-style menu list over the slate
   base, percentage battery, lock indicator. (Obsede2: `wps/Obsede2.sbs`.)
4. **Quickscreen** — same neumorphic card treatment as Now Playing.
5. **USB screen** — centered "connected" state, matching palette.

## Asset Pipeline

Rockbox's WPS/SBS tag language draws flat viewport fills and pre-rendered
bitmaps — it has no live shadow/gradient/blur rendering. The neumorphic
look is therefore **baked into backdrop bitmaps**, the same approach
Obsede2 already uses (`wps/Obsede2/*.bmp`, loaded via `%xl()`/`%xd()`):

- One 320×240 backdrop bitmap per screen state needed (e.g. Now Playing
  with/without album art, Lockscreen, Menu, Quickscreen, USB), generated
  procedurally (script-drawn soft-shadow rounded panels, not hand-painted),
  for **both** Aurora and Aurora Light — two full asset sets, not a
  color-invert of one.
- New icon bitmaps: playmode glyph strip (repeat/shuffle), lock icon,
  USB icon, no-album-art placeholder glyph, volume up/down glyphs, and the
  progress/volume bar fill+track textures (soft inset pill, amber
  gradient fill) — sized to match the existing Obsede2 slots being
  replaced (e.g. playmodes 18×162, lock 8×10, vol bars 230×4, per the
  current `wps/Obsede2/` assets) unless the new layout changes a slot's
  footprint.
- Font: Bricolage Grotesque TTF rasterized to BDF at each pixel size the
  layout needs, then compiled to Rockbox's `.fnt` format via the existing
  BDF→`.fnt` pipeline already used for the bundled `fonts/*.bdf` set.

## File Layout

Follows the existing convention exactly (see `themes/Obsede2.cfg`,
`wps/Obsede2.wps`, `wps/Obsede2.sbs`, `wps/Obsede2/*.bmp`):

```
themes/Aurora.cfg            themes/AuroraLight.cfg
wps/Aurora.wps                wps/AuroraLight.wps
wps/Aurora.sbs                wps/AuroraLight.sbs
wps/Aurora/*.bmp               wps/AuroraLight/*.bmp
  (backdrops and icons both live here, bare filenames -- not a
  separate top-level backdrops/ directory, which is an unrelated
  Rockbox feature)
fonts/*-Bricolage-Grotesque-*.fnt   (shared by both)
```

`packaging/retro-handheld/config.cfg` (the shipped default config) is
updated to point at `Aurora.wps`/`Aurora.sbs` in place of Obsede2, same as
commit `4a00a1b187` did for Obsede2.

## Open Items for the Implementation Plan

- Exact pixel sizes/weights needed from Bricolage Grotesque (title,
  body, numerals) — determined when laying out each screen.
- Exact new icon glyph list and each one's pixel footprint — derived
  1:1 from the Obsede2 slots it replaces, confirmed during layout.
- Whether the "no album art" amber glyph is a music note, waveform, or
  something else distinctive — a small open design choice, not
  architecturally significant.
