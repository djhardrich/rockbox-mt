# Aurora / Aurora Light Theme Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Ship a new neumorphic dark/light theme pair, **Aurora** and **Aurora Light**, as the retro-handheld PortMaster fork's default WPS+SBS theme, replacing Obsede2 (which stays installed and selectable, just no longer the default).

**Architecture:** Same mechanism Obsede2 already uses — a `.cfg` + `.wps` + `.sbs` + per-theme bitmap folder under `wps/<Name>/`, explicitly copied into the packaged pak by `packaging/retro-handheld/retro-handheld.make` (this fork doesn't use the stock `wps/WPSLIST`/`buildzip.pl` WPS-bundling path for its bundled themes). New pieces this plan adds that Obsede2 didn't need: full-screen `backdrops/*.bmp` bitmaps (drawn via the `%X()` WPS tag) for the neumorphic panel look, a Bricolage Grotesque `.fnt` set (via `tools/convttf`, TTF→`.fnt` directly), and a small Python/Pillow asset-generation script since the bitmaps are procedurally drawn, not hand-painted.

**Tech Stack:** Rockbox WPS/SBS tag language (`manual/appendix/wps_tags.tex`), `tools/convttf` (FreeType-based TTF→`.fnt`), Python 3 + Pillow for bitmap generation, `tools/checkwps` for syntax validation, the hosted SDL simulator (`docs/UISIMULATOR`) for visual verification.

**Design source:** `docs/superpowers/specs/2026-07-03-aurora-theme-design.md`. Two corrections to that spec discovered during research, both incorporated below:
1. Obsede2 has **no** `backdrops/Obsede2.*.bmp` files (it has zero backdrop bitmaps — its look comes entirely from flat `.cfg` colors + the small bitmaps in `wps/Obsede2/`). Aurora's backdrop bitmaps are new infrastructure, not a port of an existing mechanism.
2. Obsede2's `.wps` routes Track Title and Artist text through its two **CJK-labeled** font slots (`34-SFProDisplay-Bold-CJK.fnt`, `24-SFProDisplay-Regular-CJK.fnt`) even for non-CJK text, since those fonts also cover the Latin range. Aurora keeps those two exact files unchanged in those two slots (this is what "CJK fallback keeps the existing bundled CJK faces" in the spec means concretely) and only swaps Bricolage Grotesque into the slots that were plain `SF-Pro-Display-*.fnt` (no `-CJK` suffix).

**Known simplification:** only Bricolage Grotesque Regular and Bold are available on disk (`/home/user1/.gemini/skills/canvas-design/canvas-fonts/BricolageGrotesque-{Regular,Bold}.ttf`) — no Black/ExtraBold weight. Anywhere Obsede2 used `SF-Pro-Display-Black.fnt` (the SBS's big menu-title-adjacent slot), Aurora uses Bold at the same pixel size instead.

**Battery display:** the design spec calls for "percentage text only, no graphical battery bitmap." `%bl` is a plain numeric-percent WPS tag, so this plan drops `battery.bmp`/`battery_charging.bmp` entirely and uses `%bl` as text. Exact escaping for a trailing literal `%` character isn't confirmed anywhere in this codebase (no existing `.wps`/`.sbs` file in the tree does it), so Task 5/6 write the bare number (`87`, not `87%`) — safe, unambiguous, and it's checked for real in Task 9/10 rather than guessed.

---

### Task 1: Asset-generation script — palette + neumorphic panel primitives

**Files:**
- Create: `utils/theme-tools/gen_aurora_assets.py`

- [ ] **Step 1: Write the palette table and drawing primitives**

```python
#!/usr/bin/env python3
"""Generate Aurora / Aurora Light theme bitmap assets for the retro-handheld
WPS/SBS (320x240 LCD, 24-bit BMP). Run directly; see main() for the CLI.
"""
import os
import sys

from PIL import Image, ImageDraw, ImageFilter

LCD_W, LCD_H = 320, 240

PALETTES = {
    "aurora": {
        "base":           "#1c1e26",
        "panel_top":      "#20222c",
        "panel_bottom":   "#191a22",
        "inset_top":      "#171820",
        "inset_bottom":   "#242631",
        "text_primary":   "#e7e6ee",
        "text_secondary": "#8a8ca3",
        "accent_start":   "#ff8a65",
        "accent_end":     "#ffca28",
        "shadow_light":   "#2a2d3a",
        "shadow_dark":    "#121319",
    },
    "auroralight": {
        "base":           "#f4efe8",
        "panel_top":      "#ffffff",
        "panel_bottom":   "#ece6db",
        "inset_top":      "#e2dbcb",
        "inset_bottom":   "#f7f2ea",
        "text_primary":   "#23222a",
        "text_secondary": "#8d8676",
        "accent_start":   "#ff8a65",
        "accent_end":     "#ffca28",
        "shadow_light":   "#fffdf8",
        "shadow_dark":    "#cdc3ae",
    },
}

# Rockbox's TRANSPARENT_COLOR (firmware/export/lcd.h): pixels of exactly this
# colour are skipped when a bitmap is drawn with lcd_bitmap_transparent(),
# which is how %xd()-composited overlay icons blend over a backdrop instead
# of showing a rectangular patch.
TRANSPARENT_KEY = (255, 0, 255)


def hex_to_rgb(h):
    h = h.lstrip("#")
    return tuple(int(h[i:i + 2], 16) for i in (0, 2, 4))


def vertical_gradient(size, top_hex, bottom_hex):
    w, h = size
    top, bottom = hex_to_rgb(top_hex), hex_to_rgb(bottom_hex)
    col = Image.new("RGB", (1, h))
    for y in range(h):
        t = y / max(h - 1, 1)
        col.putpixel((0, y), tuple(round(top[c] + (bottom[c] - top[c]) * t) for c in range(3)))
    return col.resize((w, h))


def horizontal_gradient(size, left_hex, right_hex):
    w, h = size
    left, right = hex_to_rgb(left_hex), hex_to_rgb(right_hex)
    row = Image.new("RGB", (w, 1))
    for x in range(w):
        t = x / max(w - 1, 1)
        row.putpixel((x, 0), tuple(round(left[c] + (right[c] - left[c]) * t) for c in range(3)))
    return row.resize((w, h))


def rounded_mask(size, radius):
    mask = Image.new("L", size, 0)
    ImageDraw.Draw(mask).rounded_rectangle(
        [(0, 0), (size[0] - 1, size[1] - 1)], radius=radius, fill=255)
    return mask


def draw_panel(canvas, box, radius, palette, inset=False, offset=4, blur=6):
    """Composite a neumorphic rounded panel onto `canvas` (an RGB Image)
    within `box` = (x0, y0, x1, y1). Light source is top-left: a light-colour
    shadow offset up-left and a dark-colour shadow offset down-right, both
    blurred, read as *raised*. `inset=True` swaps which shadow goes on which
    side, reading as *pressed in* (used for bar tracks / grooves).

    Shadows are built as blurred L-mode (alpha-only) masks, then pasted as
    solid colour directly onto `canvas` through that mask -- NOT by blurring
    a colour RGBA image. Blurring straight (non-premultiplied) RGBA blends
    each shadow colour toward the surrounding fully-transparent (0,0,0,0)
    fill at the mask's tapering edge, which drags the visible composited
    colour toward black on every side and cancels out the top-left
    highlight. Blurring only the alpha mask and compositing the real colour
    against the real canvas avoids that fringing entirely."""
    x0, y0, x1, y1 = box
    w, h = x1 - x0, y1 - y0
    pad = blur + offset
    base_mask = rounded_mask((w, h), radius)

    def offset_mask(dx, dy):
        m = Image.new("L", (w + pad * 2, h + pad * 2), 0)
        m.paste(base_mask, (pad + dx, pad + dy))
        return m.filter(ImageFilter.GaussianBlur(blur))

    if not inset:
        dark_mask, light_mask = offset_mask(offset, offset), offset_mask(-offset, -offset)
    else:
        dark_mask, light_mask = offset_mask(-offset, -offset), offset_mask(offset, offset)

    region = canvas.crop((x0 - pad, y0 - pad, x1 + pad, y1 + pad))
    region.paste(Image.new("RGB", region.size, hex_to_rgb(palette["shadow_dark"])), (0, 0), dark_mask)
    region.paste(Image.new("RGB", region.size, hex_to_rgb(palette["shadow_light"])), (0, 0), light_mask)
    canvas.paste(region, (x0 - pad, y0 - pad))

    top, bottom = (palette["inset_top"], palette["inset_bottom"]) if inset else \
                  (palette["panel_top"], palette["panel_bottom"])
    canvas.paste(vertical_gradient((w, h), top, bottom), (x0, y0), base_mask)


def new_canvas(size, base_hex):
    return Image.new("RGB", size, hex_to_rgb(base_hex))


def new_transparent_canvas(size):
    return Image.new("RGB", size, TRANSPARENT_KEY)


def save_bmp(img, path):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    img.convert("RGB").save(path, "BMP")


def selftest(outdir):
    pal = PALETTES["aurora"]
    canvas = new_canvas((240, 160), pal["base"])
    draw_panel(canvas, (20, 20, 220, 140), radius=18, palette=pal)
    path = os.path.join(outdir, "selftest_aurora_panel.png")
    canvas.save(path)
    print("wrote", path)


if __name__ == "__main__":
    if len(sys.argv) == 3 and sys.argv[1] == "selftest":
        selftest(sys.argv[2])
    else:
        print("Usage: gen_aurora_assets.py selftest <outdir>", file=sys.stderr)
        sys.exit(1)
```

- [ ] **Step 2: Run the self-test and view the result**

```bash
mkdir -p /tmp/claude-1000/-home-user1-rockbox-milkdrop/16102b4b-6c00-4a21-adea-f0256c3e1b7b/scratchpad/aurora-preview
python3 utils/theme-tools/gen_aurora_assets.py selftest /tmp/claude-1000/-home-user1-rockbox-milkdrop/16102b4b-6c00-4a21-adea-f0256c3e1b7b/scratchpad/aurora-preview
```

Expected: prints `wrote .../selftest_aurora_panel.png`. Open the PNG with the Read tool and confirm it shows a rounded dark panel on a slightly darker background with a soft light highlight on its top-left edge and a soft dark shadow on its bottom-right edge (a raised "soft UI" card, not a flat rectangle and not a harsh drop-shadow). If the shadow looks too subtle or too harsh, adjust `offset`/`blur` in `draw_panel` and re-run before continuing — this is the primitive every other asset in this plan builds on.

- [ ] **Step 3: Commit**

```bash
git add utils/theme-tools/gen_aurora_assets.py
git commit -m "$(cat <<'EOF'
viz: add Aurora theme asset-generation script (palette + panel primitive)

Neumorphic panels are drawn procedurally (Pillow), not hand-painted:
a rounded-rect gradient fill plus a blurred light/dark shadow pair
offset from a top-left light source. This is the shared primitive
every Aurora/Aurora Light backdrop and icon asset builds on.

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>
EOF
)"
```

---

### Task 2: Backdrop generators (6 screens × 2 themes)

**Files:**
- Modify: `utils/theme-tools/gen_aurora_assets.py`
- Create (generated, not hand-edited): `backdrops/Aurora.lockscreen.bmp`, `backdrops/Aurora.now-playing-art.bmp`, `backdrops/Aurora.now-playing-noart.bmp`, `backdrops/Aurora.menu.bmp`, `backdrops/Aurora.quickscreen.bmp`, `backdrops/Aurora.usb.bmp`, and the same 6 with the `AuroraLight.` prefix

- [ ] **Step 1: Add the 6 backdrop generator functions + CLI**

Append to `utils/theme-tools/gen_aurora_assets.py` (replace the `if __name__` block at the bottom):

```python
def backdrop_lockscreen(palette):
    c = new_canvas((LCD_W, LCD_H), palette["base"])
    draw_panel(c, (10, 30, 310, 150), radius=20, palette=palette)   # clock card
    draw_panel(c, (15, 183, 305, 213), radius=14, palette=palette)  # now-playing strip
    return c


def backdrop_now_playing(palette, with_art):
    c = new_canvas((LCD_W, LCD_H), palette["base"])
    if with_art:
        draw_panel(c, (163, 30, 313, 180), radius=16, palette=palette)  # art frame
        draw_panel(c, (0, 33, 156, 179), radius=18, palette=palette)    # text card
    else:
        draw_panel(c, (0, 33, LCD_W, 179), radius=18, palette=palette)  # full-width text card
    draw_panel(c, (0, 213, LCD_W, LCD_H), radius=0, palette=palette, inset=True)  # bottom bar tray
    return c


def backdrop_menu(palette):
    c = new_canvas((LCD_W, LCD_H), palette["base"])
    draw_panel(c, (0, 0, LCD_W, 24), radius=0, palette=palette)          # title bar
    draw_panel(c, (4, 28, LCD_W - 4, LCD_H - 4), radius=16, palette=palette, inset=True)  # list well
    return c


def backdrop_quickscreen(palette):
    c = new_canvas((LCD_W, LCD_H), palette["base"])
    draw_panel(c, (0, 0, LCD_W, 24), radius=0, palette=palette)
    draw_panel(c, (20, 40, LCD_W - 20, LCD_H - 20), radius=20, palette=palette)
    return c


def backdrop_usb(palette):
    c = new_canvas((LCD_W, LCD_H), palette["base"])
    draw_panel(c, (0, 0, LCD_W, 98), radius=0, palette=palette)   # banner strip (matches usb.bmp slot)
    draw_panel(c, (40, 140, LCD_W - 40, 220), radius=18, palette=palette)
    return c


BACKDROPS = {
    "lockscreen":          lambda p: backdrop_lockscreen(p),
    "now-playing-art":     lambda p: backdrop_now_playing(p, with_art=True),
    "now-playing-noart":   lambda p: backdrop_now_playing(p, with_art=False),
    "menu":                lambda p: backdrop_menu(p),
    "quickscreen":         lambda p: backdrop_quickscreen(p),
    "usb-panel":           lambda p: backdrop_usb(p),
}


def gen_backdrops(theme, outdir):
    palette = PALETTES[theme]
    for tag, fn in BACKDROPS.items():
        img = fn(palette)
        assert img.size == (LCD_W, LCD_H), f"{tag}: expected {(LCD_W, LCD_H)}, got {img.size}"
        save_bmp(img, os.path.join(outdir, f"{tag}.bmp"))
        print("wrote", f"{tag}.bmp", img.size, img.mode)


if __name__ == "__main__":
    if len(sys.argv) == 3 and sys.argv[1] == "selftest":
        selftest(sys.argv[2])
    elif len(sys.argv) == 4 and sys.argv[1] == "backdrops":
        gen_backdrops(sys.argv[2], sys.argv[3])
    else:
        print("Usage: gen_aurora_assets.py selftest <outdir>\n"
              "       gen_aurora_assets.py backdrops <aurora|auroralight> <outdir>", file=sys.stderr)
        sys.exit(1)
```

**Why `wps/Aurora/` and bare filenames, not a top-level `backdrops/` directory:** Rockbox's skin engine resolves every relative bitmap path in `%xl()`/`%xd()`/`%X()` against a directory derived from the *loading skin file's own path with its extension stripped* (`apps/gui/skin_engine/skin_parser.c`, the `bmpdir` computation before `load_skin_bitmaps()`/`skin_backdrop_assign()`) — for `wps/Aurora.wps` that resolves to `wps/Aurora/`, automatically, with no prefix needed in the tag. This is exactly why the real `wps/Obsede2.wps` in this repo references its bitmaps as bare `battery.bmp` (not `Obsede2/battery.bmp`) even though the file lives at `wps/Obsede2/battery.bmp`, and why the stock `wps/cabbiev2.320x240x16.wps` references `%X(wpsbackdrop-320x240x16.bmp)` resolving to `wps/cabbiev2/wpsbackdrop-320x240x16.bmp`. A top-level `backdrops/` directory is a *different*, unrelated Rockbox feature (`BACKDROP_DIR`, `firmware/export/rbpaths.h`) with its own naming convention (see the existing `backdrops/cabbiev2.*.bmp` files) — it is not where a WPS's own per-file backdrop bitmap lives. So: output straight into `wps/Aurora/`/`wps/AuroraLight/` (the exact same directories Task 3's icons already write into) using bare filenames, matching the file naming Task 3 already established.

**Why per-viewport `%xl()`/`%xd()` draws instead of the `%X()` tag, even though `%X()` is "the backdrop tag":** `%X()` sets exactly one skin-wide backdrop image, chosen once when the skin file is parsed/loaded (`parse_image_special()` just overwrites a single file-scope `backdrop_filename` static; `wps_data->backdrop_id` is assigned exactly once, after parsing finishes). It is **not** re-evaluated at render time and has no per-viewport or per-conditional-branch scope — every `%X()` call anywhere in the file fires during the same one-time parse pass, so if a file contains more than one `%X()` call (as an earlier draft of this plan did, guarded by `%?C<...>`/`%?if(...)` conditionals), only the last one encountered during parsing wins, permanently, for every screen state. This is confirmed by the fact that every stock theme in this repo's `wps/` directory (all `cabbiev2.*` variants included) calls `%X()` exactly once, near the top of the file. Since Aurora genuinely needs a different backdrop bitmap per screen state (lockscreen vs. now-playing vs. menu vs. quickscreen vs. USB), Tasks 5/6 instead preload all 6 per-theme backdrops with `%xl()` and draw the correct one as the first tag inside each screen state's own `%Vl(name,0,0,320,240,-)` viewport block via `%xd()` — the same proven mechanism `wps/Obsede2.sbs` already uses to show either album art or its "star" fallback bitmap as a full 160×240 viewport background (`%Vl(withStar,160,0,160,240,-)` / `%xd(star)`), just at full-screen size instead of half-screen. Because viewport content tags draw in document order and a later tag paints over an earlier one, putting the `%xd(bg_*)` call first in each viewport block makes it the background for everything else drawn in that same block.

- [ ] **Step 2: Generate both themes' backdrops directly into `wps/Aurora/`/`wps/AuroraLight/`, verify dimensions**

```bash
mkdir -p wps/Aurora wps/AuroraLight
python3 utils/theme-tools/gen_aurora_assets.py backdrops aurora wps/Aurora
python3 utils/theme-tools/gen_aurora_assets.py backdrops auroralight wps/AuroraLight
```

Expected: 6 `wrote ... (320, 240) RGB` lines per theme (12 total), no `AssertionError`. Then confirm on disk:

```bash
file wps/Aurora/{lockscreen,now-playing-art,now-playing-noart,menu,quickscreen,usb-panel}.bmp \
     wps/AuroraLight/{lockscreen,now-playing-art,now-playing-noart,menu,quickscreen,usb-panel}.bmp
```

Expected: all 12 report `PC bitmap, Windows 3.x format, 320 x 240 x 24`. Note the backdrop tag is `usb-panel`, not `usb` — Task 3 already uses the plain name `usb.bmp` for its 320×98 icon banner in the same `wps/Aurora/` directory, and both files must coexist without colliding.

- [ ] **Step 3: Visually inspect the two highest-traffic screens**

```bash
convert wps/Aurora/now-playing-art.bmp /tmp/claude-1000/-home-user1-rockbox-milkdrop/16102b4b-6c00-4a21-adea-f0256c3e1b7b/scratchpad/aurora-preview/now-playing-art.png
convert wps/AuroraLight/lockscreen.bmp /tmp/claude-1000/-home-user1-rockbox-milkdrop/16102b4b-6c00-4a21-adea-f0256c3e1b7b/scratchpad/aurora-preview/light-lockscreen.png
```

Open both PNGs with the Read tool. Confirm: the dark Now Playing backdrop shows a raised art-frame panel on the right and a raised text-card panel on the left, both readable against the dark base, with an inset (pressed-in) tray along the bottom for the progress/volume bars; the light Lockscreen backdrop shows the inverted cream palette with warm (not grey/muddy) shadow tones. If a panel reads as muddy or the shadow is too weak/strong, adjust the palette's `shadow_light`/`shadow_dark` values or `draw_panel`'s `offset`/`blur` and regenerate before moving on.

- [ ] **Step 4: Commit**

```bash
git add utils/theme-tools/gen_aurora_assets.py wps/Aurora/*.bmp wps/AuroraLight/*.bmp
git commit -m "$(cat <<'EOF'
viz: generate Aurora / Aurora Light backdrop bitmaps

Six 320x240 neumorphic backdrops per theme (lockscreen, now-playing
with/without album art, menu, quickscreen, USB), procedurally drawn
by gen_aurora_assets.py. Written into wps/Aurora/ and wps/AuroraLight/
(not a top-level backdrops/ directory) using bare filenames, since
Rockbox's skin engine resolves %xl()/%xd() bitmap paths against a
directory derived from the loading .wps/.sbs file's own name -- the
same convention wps/Obsede2/ already uses. Displayed per screen
state via %xl()/%xd() viewport-local draws rather than the %X()
backdrop tag, since %X() sets one skin-wide backdrop at parse time
and cannot vary per viewport/conditional branch.

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>
EOF
)"
```

---

### Task 3: Icon/sprite bitmap generators (13 assets × 2 themes)

**Files:**
- Modify: `utils/theme-tools/gen_aurora_assets.py`
- Create (generated): `wps/Aurora/{lock,playmodes,prog,progbg,repeat_status,shuffle_status,usb,vol,vol_down,vol_up,volbg,noart,fallback}.bmp`, and the same 13 under `wps/AuroraLight/`

These match Obsede2's `wps/Obsede2/` asset list (see `wps/Obsede2.wps`/`.sbs`) minus `battery.bmp`/`battery_charging.bmp` (dropped — Aurora uses `%bl` text instead), plus two new assets: `noart.bmp` (64×64, the WPS's own "no album art" glyph — a new addition, doesn't exist in Obsede2) and `fallback.bmp` (160×240, Aurora's equivalent of Obsede2's `nocover.bmp`, used in the SBS browse screen).

- [ ] **Step 1: Add the icon generator functions**

Append to `utils/theme-tools/gen_aurora_assets.py` (insert before the `if __name__` block):

```python
def bar_fill(palette, size=(230, 4)):
    return horizontal_gradient(size, palette["accent_start"], palette["accent_end"])


def bar_track(palette, size=(230, 4)):
    img = new_canvas(size, palette["inset_top"])
    d = ImageDraw.Draw(img)
    d.line([(0, 0), (size[0] - 1, 0)], fill=hex_to_rgb(palette["shadow_dark"]))
    d.line([(0, size[1] - 1), (size[0] - 1, size[1] - 1)], fill=hex_to_rgb(palette["shadow_light"]))
    return img


def icon_lock(palette, size=(8, 10)):
    img = new_transparent_canvas(size)
    d = ImageDraw.Draw(img)
    fg = hex_to_rgb(palette["accent_end"])
    d.arc([1, 0, 6, 6], 180, 360, fill=fg)
    d.rectangle([1, 4, 6, 9], fill=fg)
    return img


def icon_vol(palette, size, filled_bars):
    """Simple speaker-cone + N sound-wave bars glyph."""
    img = new_transparent_canvas(size)
    d = ImageDraw.Draw(img)
    fg = hex_to_rgb(palette["text_primary"])
    w, h = size
    d.polygon([(0, h * 0.35), (w * 0.35, h * 0.35), (w * 0.65, 0),
               (w * 0.65, h), (w * 0.35, h * 0.65), (0, h * 0.65)], fill=fg)
    for i in range(filled_bars):
        x = w * 0.72 + i * (w * 0.12)
        d.line([(x, h * (0.5 - 0.12 * (i + 1))), (x, h * (0.5 + 0.12 * (i + 1)))], fill=fg)
    return img


def icon_usb(palette, size=(320, 98)):
    img = new_canvas(size, palette["panel_top"])
    d = ImageDraw.Draw(img)
    fg = hex_to_rgb(palette["accent_end"])
    cx, cy = size[0] // 2, size[1] // 2
    d.line([(cx, cy - 20), (cx, cy + 20)], fill=fg, width=4)
    d.line([(cx - 16, cy - 4), (cx + 16, cy - 4)], fill=fg, width=4)
    d.ellipse([cx - 24, cy + 12, cx - 16, cy + 20], outline=fg, width=2)
    d.rectangle([cx + 12, cy + 8, cx + 22, cy + 18], outline=fg, width=2)
    return img


def icon_noart(palette, size=(64, 64)):
    """Waveform glyph shown in place of album art when none is available."""
    img = new_transparent_canvas(size)
    d = ImageDraw.Draw(img)
    fg = hex_to_rgb(palette["accent_start"])
    bars = [0.3, 0.6, 0.9, 0.5, 0.8, 0.4, 0.7]
    n = len(bars)
    bw = size[0] / (n * 2)
    for i, h_frac in enumerate(bars):
        x = bw + i * bw * 2
        bar_h = size[1] * h_frac * 0.7
        y0 = (size[1] - bar_h) / 2
        d.rounded_rectangle([x, y0, x + bw * 0.8, y0 + bar_h], radius=bw * 0.4, fill=fg)
    return img


def icon_fallback(palette, size=(160, 240)):
    img = new_canvas(size, palette["base"])
    draw_panel(img, (16, 60, size[0] - 16, size[1] - 60), radius=18, palette=palette)
    glyph = icon_noart(palette, size=(80, 80))
    mask = Image.new("L", glyph.size, 0)
    px = glyph.load()
    mpx = mask.load()
    for yy in range(glyph.size[1]):
        for xx in range(glyph.size[0]):
            if px[xx, yy] != TRANSPARENT_KEY:
                mpx[xx, yy] = 255
    img.paste(glyph, ((size[0] - 80) // 2, (size[1] - 80) // 2 - 20), mask)
    return img


def sprite_strip(frames):
    w = frames[0].size[0]
    h = frames[0].size[1]
    strip = Image.new("RGB", (w, h * len(frames)))
    for i, f in enumerate(frames):
        strip.paste(f, (0, i * h))
    return strip


def icon_playmodes(palette):
    """9 frames for %mp = Stop, Play, Pause, FF, RW, Rec, RecPause, Radio, RadioMuted."""
    size = (18, 18)
    fg = hex_to_rgb(palette["text_primary"])
    frames = []

    def frame():
        img = new_transparent_canvas(size)
        return img, ImageDraw.Draw(img)

    # 0 Stop
    img, d = frame(); d.rectangle([5, 5, 12, 12], fill=fg); frames.append(img)
    # 1 Play
    img, d = frame(); d.polygon([(5, 4), (14, 9), (5, 14)], fill=fg); frames.append(img)
    # 2 Pause
    img, d = frame(); d.rectangle([5, 4, 8, 14], fill=fg); d.rectangle([10, 4, 13, 14], fill=fg); frames.append(img)
    # 3 Fast Forward
    img, d = frame(); d.polygon([(3, 4), (9, 9), (3, 14)], fill=fg); d.polygon([(9, 4), (15, 9), (9, 14)], fill=fg); frames.append(img)
    # 4 Rewind
    img, d = frame(); d.polygon([(15, 4), (9, 9), (15, 14)], fill=fg); d.polygon([(9, 4), (3, 9), (9, 14)], fill=fg); frames.append(img)
    # 5 Recording
    img, d = frame(); d.ellipse([4, 4, 14, 14], fill=hex_to_rgb(palette["accent_start"])); frames.append(img)
    # 6 Recording paused
    img, d = frame()
    d.ellipse([4, 4, 14, 14], outline=hex_to_rgb(palette["accent_start"]), width=2)
    d.rectangle([7, 7, 8, 11], fill=fg); d.rectangle([10, 7, 11, 11], fill=fg)
    frames.append(img)
    # 7 FM Radio playing
    img, d = frame()
    d.arc([2, 2, 16, 16], 300, 60, fill=fg)
    d.arc([5, 5, 13, 13], 300, 60, fill=fg)
    d.ellipse([8, 8, 10, 10], fill=fg)
    frames.append(img)
    # 8 FM Radio muted (same glyph, with a slash)
    img, d = frame()
    d.arc([2, 2, 16, 16], 300, 60, fill=fg)
    d.arc([5, 5, 13, 13], 300, 60, fill=fg)
    d.ellipse([8, 8, 10, 10], fill=fg)
    d.line([(2, 16), (16, 2)], fill=hex_to_rgb(palette["accent_start"]), width=2)
    frames.append(img)

    return sprite_strip(frames)


def icon_repeat_status(palette):
    """4 frames for repeat mode All / One / Shuffle / A-B (mm=1..4, see %xd(R,%mm,-1))."""
    size = (16, 18)
    fg = hex_to_rgb(palette["accent_end"])
    frames = []

    def frame():
        img = new_transparent_canvas(size)
        return img, ImageDraw.Draw(img)

    # All: loop arrows
    img, d = frame(); d.arc([2, 3, 14, 15], 30, 300, fill=fg, width=2); d.polygon([(12, 2), (15, 5), (11, 6)], fill=fg); frames.append(img)
    # One: loop arrows + "1"
    img, d = frame()
    d.arc([2, 3, 14, 15], 30, 300, fill=fg, width=2); d.polygon([(12, 2), (15, 5), (11, 6)], fill=fg)
    d.line([(8, 8), (8, 13)], fill=fg)
    frames.append(img)
    # Shuffle: crossed arrows
    img, d = frame()
    d.line([(2, 4), (14, 14)], fill=fg, width=2); d.line([(2, 14), (14, 4)], fill=fg, width=2)
    frames.append(img)
    # A-B: bracketed range
    img, d = frame()
    d.line([(3, 4), (3, 14)], fill=fg, width=2); d.line([(13, 4), (13, 14)], fill=fg, width=2)
    d.line([(3, 9), (13, 9)], fill=fg, width=2)
    frames.append(img)

    return sprite_strip(frames)


def icon_shuffle_status(palette, size=(16, 18)):
    img = new_transparent_canvas(size)
    d = ImageDraw.Draw(img)
    fg = hex_to_rgb(palette["accent_start"])
    d.line([(2, 5), (14, 13)], fill=fg, width=2)
    d.line([(2, 13), (14, 5)], fill=fg, width=2)
    return img


ICONS = {
    "prog":            lambda p: bar_fill(p),
    "vol":             lambda p: bar_fill(p),
    "progbg":          lambda p: bar_track(p),
    "volbg":           lambda p: bar_track(p),
    "lock":            lambda p: icon_lock(p),
    "vol_up":          lambda p: icon_vol(p, (16, 16), 3),
    "vol_down":        lambda p: icon_vol(p, (14, 14), 1),
    "usb":             lambda p: icon_usb(p),
    "noart":           lambda p: icon_noart(p),
    "fallback":        lambda p: icon_fallback(p),
    "playmodes":       lambda p: icon_playmodes(p),
    "repeat_status":   lambda p: icon_repeat_status(p),
    "shuffle_status":  lambda p: icon_shuffle_status(p),
}

EXPECTED_SIZES = {
    "prog": (230, 4), "vol": (230, 4), "progbg": (230, 4), "volbg": (230, 4),
    "lock": (8, 10), "vol_up": (16, 16), "vol_down": (14, 14),
    "usb": (320, 98), "noart": (64, 64), "fallback": (160, 240),
    "playmodes": (18, 162), "repeat_status": (16, 72), "shuffle_status": (16, 18),
}


def gen_icons(theme, outdir):
    palette = PALETTES[theme]
    for tag, fn in ICONS.items():
        img = fn(palette)
        expected = EXPECTED_SIZES[tag]
        assert img.size == expected, f"{tag}: expected {expected}, got {img.size}"
        save_bmp(img, os.path.join(outdir, f"{tag}.bmp"))
        print("wrote", tag, img.size, img.mode)
```

- [ ] **Step 2: Wire `icons` into the CLI**

Replace the `if __name__` block again:

```python
if __name__ == "__main__":
    if len(sys.argv) == 3 and sys.argv[1] == "selftest":
        selftest(sys.argv[2])
    elif len(sys.argv) == 4 and sys.argv[1] == "backdrops":
        gen_backdrops(sys.argv[2], sys.argv[3])
    elif len(sys.argv) == 4 and sys.argv[1] == "icons":
        gen_icons(sys.argv[2], sys.argv[3])
    else:
        print("Usage: gen_aurora_assets.py selftest <outdir>\n"
              "       gen_aurora_assets.py backdrops <aurora|auroralight> <outdir>\n"
              "       gen_aurora_assets.py icons <aurora|auroralight> <outdir>", file=sys.stderr)
        sys.exit(1)
```

- [ ] **Step 3: Generate both themes' icon sets, verify dimensions**

```bash
mkdir -p wps/Aurora wps/AuroraLight
python3 utils/theme-tools/gen_aurora_assets.py icons aurora wps/Aurora
python3 utils/theme-tools/gen_aurora_assets.py icons auroralight wps/AuroraLight
```

Expected: 13 `wrote ...` lines per theme (26 total), no `AssertionError`. Then:

```bash
file wps/Aurora/*.bmp wps/AuroraLight/*.bmp
```

Expected: 26 files, each `PC bitmap, Windows 3.x format` at the size in `EXPECTED_SIZES` above (e.g. `playmodes.bmp: ... 18 x 162 x 24`).

- [ ] **Step 4: Visually inspect the playmodes strip and progress bar**

```bash
convert wps/Aurora/playmodes.bmp -scale 400% /tmp/claude-1000/-home-user1-rockbox-milkdrop/16102b4b-6c00-4a21-adea-f0256c3e1b7b/scratchpad/aurora-preview/playmodes.png
convert wps/Aurora/prog.bmp -scale 300% /tmp/claude-1000/-home-user1-rockbox-milkdrop/16102b4b-6c00-4a21-adea-f0256c3e1b7b/scratchpad/aurora-preview/prog-fill.png
```

Open both with the Read tool. Confirm the playmodes strip shows 9 distinguishable stacked glyphs (magenta background is expected here — it's the transparency key, not a rendering bug) and the progress-bar fill shows a clean horizontal amber gradient. Adjust glyph coordinates in `icon_playmodes`/`bar_fill` and regenerate if anything looks wrong before continuing.

- [ ] **Step 5: Commit**

```bash
git add utils/theme-tools/gen_aurora_assets.py wps/Aurora/*.bmp wps/AuroraLight/*.bmp
git commit -m "$(cat <<'EOF'
viz: generate Aurora / Aurora Light icon and bar bitmaps

13 assets per theme (progress/volume bars, playmode + repeat/shuffle
sprite strips, lock, volume icons, USB banner, and two new assets —
noart.bmp and fallback.bmp — for the no-album-art states). Battery
bitmaps are dropped: Aurora shows %bl as text instead of a graphical
gauge. Small overlay icons use Rockbox's magenta TRANSPARENT_COLOR
key so they composite over the backdrop bitmaps from the previous
commit instead of showing an opaque patch.

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>
EOF
)"
```

---

### Task 4: Convert Bricolage Grotesque to Rockbox `.fnt` files

**Files:**
- Create: `fonts/12-Bricolage-Grotesque-Regular.fnt`, `fonts/12-Bricolage-Grotesque-Bold.fnt`, `fonts/15-Bricolage-Grotesque-Regular.fnt`, `fonts/16-Bricolage-Grotesque-Bold.fnt`, `fonts/19-Bricolage-Grotesque-Regular.fnt`, `fonts/32-Bricolage-Grotesque-Bold.fnt`, `fonts/72-Bricolage-Grotesque-Bold.fnt`

Sizes are derived directly from where each weight is used: WPS slots 5/6/8/9 (Album Title 15, Album Year 12, Lockscreen clock 72, everything else 16), SBS slots 2/3/4 (12, 32, 16), and the base UI font referenced by `themes/*.cfg`/`config.cfg` (`font:` line, 19 — matching Obsede2's own base font size). `tools/convttf` (built at `tools/convttf`, confirmed working — converts these exact TTFs directly, no BDF intermediate needed) goes straight from `.ttf` to `.fnt` at a given pixel size.

- [ ] **Step 1: Build `convttf` if not already built**

```bash
cd tools && make convttf && cd ..
ls -la tools/convttf
```

Expected: an executable `tools/convttf` (already confirmed to build cleanly with `freetype2` via pkg-config in this environment).

- [ ] **Step 2: Convert all 7 sizes**

```bash
BRI=/home/user1/.gemini/skills/canvas-design/canvas-fonts
tools/convttf -p 12 -o fonts/12-Bricolage-Grotesque-Regular.fnt $BRI/BricolageGrotesque-Regular.ttf
tools/convttf -p 12 -o fonts/12-Bricolage-Grotesque-Bold.fnt    $BRI/BricolageGrotesque-Bold.ttf
tools/convttf -p 15 -o fonts/15-Bricolage-Grotesque-Regular.fnt $BRI/BricolageGrotesque-Regular.ttf
tools/convttf -p 16 -o fonts/16-Bricolage-Grotesque-Bold.fnt    $BRI/BricolageGrotesque-Bold.ttf
tools/convttf -p 19 -o fonts/19-Bricolage-Grotesque-Regular.fnt $BRI/BricolageGrotesque-Regular.ttf
tools/convttf -p 32 -o fonts/32-Bricolage-Grotesque-Bold.fnt    $BRI/BricolageGrotesque-Bold.ttf
tools/convttf -p 72 -o fonts/72-Bricolage-Grotesque-Bold.fnt    $BRI/BricolageGrotesque-Bold.ttf
```

Expected: each prints `Converted ... done (converted N glyphs, 0 errors).` (confirmed working for the 16pt case during planning — 526 glyphs, 0 errors).

- [ ] **Step 3: Verify all 7 files exist and are non-empty**

```bash
ls -la fonts/*Bricolage*.fnt
file fonts/*Bricolage*.fnt
```

Expected: 7 files, each reported as `data` (binary `.fnt`, same as the existing `fonts/*SF-Pro*.fnt`/`fonts/*SFProDisplay*.fnt` files) and non-zero size.

- [ ] **Step 4: Also copy the OFL license alongside the source TTFs' attribution**

```bash
cp /home/user1/.gemini/skills/canvas-design/canvas-fonts/BricolageGrotesque-OFL.txt fonts/Bricolage-Grotesque-OFL.txt
```

- [ ] **Step 5: Commit**

```bash
git add fonts/*Bricolage*.fnt fonts/Bricolage-Grotesque-OFL.txt
git commit -m "$(cat <<'EOF'
fonts: add Bricolage Grotesque .fnt set for the Aurora theme

7 sizes covering every plain-Latin slot Obsede2's SF-Pro-Display
fonts filled (WPS: 15/12/72/16, SBS: 12/32/16, base UI font: 19),
converted directly from TTF via tools/convttf (no .bdf source, same
precedent as the existing SF-Pro fonts already in this tree). Only
Regular and Bold are available (no Black) — anywhere Obsede2 used
SF-Pro-Display-Black, Aurora uses Bold at the same size instead.
The two CJK-labeled slots (34/24pt SFProDisplay-*-CJK, used for
Track Title/Artist so CJK text still renders) are intentionally left
untouched; Bricolage has no CJK coverage.

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>
EOF
)"
```

---

### Task 5: Author `wps/Aurora.wps` and `wps/Aurora.sbs` (dark theme)

**Files:**
- Create: `wps/Aurora.wps`, `wps/Aurora.sbs`

Adapted tag-for-tag from `wps/Obsede2.wps`/`.sbs` (proven-working control flow, kept as-is): asset paths are bare filenames (Rockbox resolves `%xl()`/`%xd()` bitmap paths against a directory derived from the loading skin file's own name — for `wps/Aurora.wps` that's `wps/Aurora/`, automatically, exactly like `wps/Obsede2.wps` references its own bitmaps as bare `battery.bmp` even though they live at `wps/Obsede2/battery.bmp` — see Task 2's note on this), colors use the Aurora palette hex values, the graphical battery blocks are replaced with `%bl` text viewports, and a no-album-art glyph block is added (new — Obsede2's WPS has none).

**Per-screen-state backdrops use `%xl()`/`%xd()`, not `%X()`:** an earlier draft of this task used the `%X()` backdrop tag guarded by conditionals (`%?C<%X(a)|%X(b)>`) to show a different backdrop per screen state. That doesn't work: `%X()` sets one skin-wide backdrop exactly once, at parse time (see Task 2's note for the full explanation, grounded in `apps/gui/skin_engine/skin_parser.c`) — every `%X()` call in the file fires during the same parse pass regardless of which conditional branch it's textually inside, so only the last one wins, permanently. Every stock theme in this repo (`wps/cabbiev2.*`) calls `%X()` exactly once for this reason. Instead, each of the 6 backdrop bitmaps generated in Task 2 is preloaded with `%xl()` like any other image and drawn with `%xd()` as the *first* tag inside its screen state's own `%Vl(name,0,0,320,240,-)` viewport block — later tags in that same block draw on top of it. This is the exact same mechanism `wps/Obsede2.sbs` already uses correctly to show either album art or its "star" fallback bitmap as a viewport-sized background (`%Vl(withStar,160,0,160,240,-)` / `%xd(star)`), just at full-screen size instead of half-screen, and it re-evaluates any `%?` conditional correctly at render time (unlike `%X()`).

- [ ] **Step 1: Create `wps/Aurora.wps`**

```
# Aurora by this project
# Neumorphic dark theme -- see docs/superpowers/specs/2026-07-03-aurora-theme-design.md
# 2026-07-03 v1.0

# Disable Status Bar
%wd

# Preload Fonts
%Fl(3,34-SFProDisplay-Bold-CJK.fnt)
%Fl(5,15-Bricolage-Grotesque-Regular.fnt)
%Fl(6,12-Bricolage-Grotesque-Regular.fnt)
%Fl(7,24-SFProDisplay-Regular-CJK.fnt)
%Fl(8,72-Bricolage-Grotesque-Bold.fnt)
%Fl(9,16-Bricolage-Grotesque-Bold.fnt)

# Preload Images
%xl(bg_lock,lockscreen.bmp,0,0)
%xl(bg_art,now-playing-art.bmp,0,0)
%xl(bg_noart,now-playing-noart.bmp,0,0)
%xl(playmodes,playmodes.bmp,0,0,9)
%xl(noart,noart.bmp,0,0)
%xl(volbg,volbg.bmp,0,0)
%xl(vol,vol.bmp,0,0)
%xl(prog,prog.bmp,0,0)
%xl(progbg,progbg.bmp,0,0)
%xl(R,repeat_status.bmp,4)
%xl(S,shuffle_status.bmp)
%xl(vol_up,vol_up.bmp)
%xl(vol_down,vol_down.bmp)
%xl(L,lock.bmp)

# Display Viewports
%Vd(main)
%?mh<%Vd(lockscreen)|%Vd(metadata)%?C<%Vd(withAlbumArt)|%Vd(noAlbumArt)>%?mv(3)<%Vd(volume)|%Vd(prog)>%?ps<%Vd(repeatModeDark)|%?mm<%Vd(noRepeatDark)|%Vd(repeatModeDark)>>>
%Vl(main,0,0,320,240,-)

# Album Art Preload
%Cl(0,0,140,140,r,c)
%?C<%Vd(albumArt)>

#######################
##### Lockscreen ######
#######################

%Vl(lockscreen,0,0,320,240,-)
%xd(bg_lock)

# Playback Mode Icons
%Vl(lockscreen,2,3,18,18,-)
%xd(playmodes,%mp)

# Clock
%Vl(lockscreen,10,40,-10,100,8)%Vf(e7e6ee)
%ac%?cf<%cH|%cl>:%cM%?cf<| %cp>

# Day
%Vl(lockscreen,15,35,-15,16,9)%Vf(8a8ca3)
%ac%?cu<Monday|Tuesday|Wednesday|Thursday|Friday|Saturday|Sunday>, %cb %cd

# Lock Indicator
%Vl(lockscreen,156,7,8,10,-)
%?mh<%xd(L)>

# Now Playing
%Vl(lockscreen,15,193,-15,20,9)
%Vf(8a8ca3)
%?if(%mp, >, 1)<%ac%s%?ia<%ia> - %?it<%it|%fn>|>

#######################
##### Play Screen #####
#######################

%Vl(metadata,0,0,320,240,-)
%?C<%xd(bg_art)|%xd(bg_noart)>

# Album Art
%Vl(metadata,173,40,140,140,-)
%Cd

# No-Album-Art Glyph
%Vl(noAlbumArt,173,40,140,140,-)
%?C<|%xd(noart)>

# Playback Mode Icons
%Vl(metadata,2,3,18,18,-)
%xd(playmodes,%mp)

# Track Number (repeat)
%Vl(repeatModeDark,41,4,100,13,9)%Vf(e7e6ee)
%al%pp of %pe

# Track Number (no repeat)
%Vl(noRepeatDark,22,4,100,13,9)%Vf(e7e6ee)
%al%pp of %pe

# Repeat
%Vl(repeatModeDark,20,3,18,18,-)
%t(5)%?mm<%?ps<%xd(S)>|%xd(R,%mm,-1)>;%t(5)%?ps<%xd(S)|%?mm<|%xd(R,%mm,-1)>>

# Clock
%Vl(metadata,232,4,50,13,9)%Vf(e7e6ee)
%ar%?cf<%cH|%cl>:%cM%?cf<| %cp>

# Battery viewport selector (flash when low, '+' prefix when charging)
%?bc<%Vd(battCharging)|%?if(%bl,>=,15)<%Vd(batt)|%Vd(lowBatt)>>

# Battery (percentage text)
%Vl(batt,275,4,40,13,9)%Vf(e7e6ee)
%ar%bl

%Vl(lowBatt,275,4,40,13,9)%Vf(ff8a65)
%t(1)%ar%bl;%t(1) 

%Vl(battCharging,275,4,40,13,9)%Vf(ffca28)
%ar+%bl

# Track Artist or Title
%Vl(noAlbumArt,6,43,-10,30,7)%Vf(8a8ca3)
%?C<|%?mh<|%aL%s%?ia<%ia|%?it<%it|%fn>>>>

%Vl(withAlbumArt,6,43,160,30,7)%Vf(8a8ca3)
%?C<%?mh<|%aL%s%?ia<%ia|%?it<%it|%fn>>>>

# Track Title
%Vl(noAlbumArt,6,68,-10,36,3)%Vf(e7e6ee)
%?C<|%?mh<|%aL%s%?ia<%it>>>

%Vl(withAlbumArt,6,68,160,36,3)%Vf(e7e6ee)
%?C<%?mh<|%aL%s%?ia<%it>>>

# Album Title
%Vl(noAlbumArt,6,104,-10,15,5)%Vf(8a8ca3)
%?C<|%?mh<|%aL%s%?id<%id|Unknown Album>>>
%Vl(withAlbumArt,6,104,160,15,5)%Vf(8a8ca3)
%?C<%?mh<|%aL%s%?id<%id|Unknown Album>>>

# Album Year
%Vl(metadata,6,125,150,14,6)%Vf(8a8ca3)
%?iy<%ss(0,4, %iy)|>

# Volume Level Icons
%Vl(volume,289,219,16,16,-)
%xd(vol_up)

%Vl(volume,14,220,16,16,-)%Vf(ffca28)
%xd(vol_down)

# Volume Bar
%Vl(volume,45,225,230,4,-)%Vf(ffca28)
%pv(0,0,230,4,vol,backdrop,volbg,d)

# Time Elapsed
%Vl(prog,5,219,32,14,9)%Vf(e7e6ee)
%al%?if(%pc,<,600)< |>%pc

# Progress Bar
%Vl(prog,45,225,230,4,-)
%pb(0,0,230,4,prog,backdrop,progbg)

# Total Track Time
%Vl(prog,281,219,32,14,9)%Vf(e7e6ee)
%ar%pt
```

- [ ] **Step 2: Create `wps/Aurora.sbs`**

```
# Aurora by this project
# Neumorphic dark theme -- see docs/superpowers/specs/2026-07-03-aurora-theme-design.md
# 2026-07-03 v1.0

# Disable Status Bar
%wd

# Preload Font
%Fl(2,12-Bricolage-Grotesque-Bold.fnt)
%Fl(3,32-Bricolage-Grotesque-Bold.fnt)
%Fl(4,16-Bricolage-Grotesque-Bold.fnt)

# Preload Images
%xl(bg_usb,usb-panel.bmp,0,0)
%xl(bg_qs,quickscreen.bmp,0,0)
%xl(bg_menu,menu.bmp,0,0)
%xl(usb,usb.bmp,0,0)
%xl(L,lock.bmp)
%xl(star,fallback.bmp,0,0)

# Album Art Preload
%Cl(-40,0,240,240,c,c)
%?if(%cs, !=, 21)<%?C<%Vd(noStar)|%Vd(withStar)>>

%?if(%cs, =, 10)<%Vd(qsTitle)%VI(clearScreen)|%?if(%cs, !=, 21)<%?C<%Vd(noStar)|%Vd(withStar)>>>


#__USB screen
%?if(%cs, =, 21)<%VI(clearScreen)%Vd(usb)|%VI(menu)%Vd(info)%Vd(batt)>

#__Clear Screen
%Vi(clearScreen,0,0,1,1,-)

# Backdrop (USB, quickscreen, and the normal menu each use a different panel)
%Vd(bgHolder)
%Vl(bgHolder,0,0,320,240,-)
%?if(%cs, =, 21)<%xd(bg_usb)|%?if(%cs, =, 10)<%xd(bg_qs)|%xd(bg_menu)>>

# Display Viewports

# --- With Album Art ---
%Vl(noStar,160,0,160,240,-)
%Cd

# --- No Album Art ---
%Vl(withStar,160,0,160,240,-)
%xd(star)

# Menu
%Vi(menu,0,24,160,216,-)
%Vf(8a8ca3)

# Menu Title
%Vl(info,6,4,101,16,4)
%Vf(e7e6ee)
%al%s%?cs<Main Menu|%?Lt<%Lt>>

# Hold
%Vl(info,114,7,8,10,-)
%Vf(ffca28)
%?mh<%xd(L)>

# Quick Screen title viewport
%Vl(qsTitle,6,4,101,16,4)
%Vf(e7e6ee)
%alQuick Screen

# Battery viewport selector (flash when low, '+' prefix when charging)
%?bc<%Vd(battCharging)|%?if(%bl,>=,15)<%Vd(batt)|%Vd(lowBatt)>>

# Battery (percentage text)
%Vl(batt,114,6,24,13,2)%Vf(e7e6ee)
%ar%bl

%Vl(lowBatt,114,6,24,13,2)%Vf(ff8a65)
%t(1)%ar%bl;%t(1) 

%Vl(battCharging,114,6,24,13,2)%Vf(ffca28)
%ar+%bl

# USB_screen

#__USB
%Vl(usb,0,0,320,98,-)
%xd(usb)

%Vl(usb,20,160,280,-,3)%Vf(e7e6ee)
%acUSB CONNECTED

%Vl(usb,20,195,280,-,2)%Vf(8a8ca3)
%acEject Before Disconnecting
```

Note: the SBS's `usb.bmp` (via `%xl(usb,usb.bmp,0,0)`, the 320×98 icon banner from Task 3) and the new `bg_usb` backdrop (`usb-panel.bmp`, the 320×240 full-screen backdrop from Task 2, renamed from Task 2's `usb` tag to avoid colliding with Task 3's identically-named `usb.bmp` icon in the same `wps/Aurora/` directory) are two different files that must coexist — see the callout in Task 2 Step 2 about running Task 2's backdrop generation with `usb` renamed to `usb-panel` in `BACKDROPS`/`gen_backdrops`'s output filename for this reason.

- [ ] **Step 3: Commit**

```bash
git add wps/Aurora.wps wps/Aurora.sbs
git commit -m "$(cat <<'EOF'
viz: add wps/Aurora.wps and wps/Aurora.sbs

Same viewport/control-flow structure as Obsede2 (proven-working
WPS/SBS tag file), re-skinned: bare-filename asset references into
wps/Aurora/ (matching how Obsede2 resolves its own bitmaps), Aurora
palette colors via %Vf(), per-screen-state backdrop bitmaps drawn
via %xl()/%xd() viewport-local draws (not %X(), which is a
parse-time skin-wide singleton and can't vary per screen state),
%bl text in place of the graphical battery gauge, and a new
no-album-art glyph block. Syntax is validated in a later task via
tools/checkwps.

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>
EOF
)"
```

---

### Task 6: Author `wps/AuroraLight.wps` and `wps/AuroraLight.sbs` (light theme)

**Files:**
- Create: `wps/AuroraLight.wps`, `wps/AuroraLight.sbs`

Identical control flow to the corrected `wps/Aurora.wps`/`.sbs` (bare-filename asset paths resolved against `wps/AuroraLight/` automatically, `%xl()`/`%xd()` viewport-local backdrop draws instead of `%X()`, an explicit `%Vd(bgHolder)` to unhide the SBS's backdrop-holder viewport — see Task 5's notes for why each of these matters, all grounded in `apps/gui/skin_engine/skin_parser.c`/`skin_render.c`); only the asset folder (`AuroraLight/`) and the two palette-dependent text colors change (`e7e6ee`→`23222a`, `8a8ca3`→`8d8676`; the amber accent colors `ff8a65`/`ffca28` are shared between both themes per the design spec, so those stay the same).

- [ ] **Step 1: Create `wps/AuroraLight.wps`**

```
# Aurora Light by this project
# Neumorphic light theme -- see docs/superpowers/specs/2026-07-03-aurora-theme-design.md
# 2026-07-03 v1.0

# Disable Status Bar
%wd

# Preload Fonts
%Fl(3,34-SFProDisplay-Bold-CJK.fnt)
%Fl(5,15-Bricolage-Grotesque-Regular.fnt)
%Fl(6,12-Bricolage-Grotesque-Regular.fnt)
%Fl(7,24-SFProDisplay-Regular-CJK.fnt)
%Fl(8,72-Bricolage-Grotesque-Bold.fnt)
%Fl(9,16-Bricolage-Grotesque-Bold.fnt)

# Preload Images
%xl(bg_lock,lockscreen.bmp,0,0)
%xl(bg_art,now-playing-art.bmp,0,0)
%xl(bg_noart,now-playing-noart.bmp,0,0)
%xl(playmodes,playmodes.bmp,0,0,9)
%xl(noart,noart.bmp,0,0)
%xl(volbg,volbg.bmp,0,0)
%xl(vol,vol.bmp,0,0)
%xl(prog,prog.bmp,0,0)
%xl(progbg,progbg.bmp,0,0)
%xl(R,repeat_status.bmp,4)
%xl(S,shuffle_status.bmp)
%xl(vol_up,vol_up.bmp)
%xl(vol_down,vol_down.bmp)
%xl(L,lock.bmp)

# Display Viewports
%Vd(main)
%?mh<%Vd(lockscreen)|%Vd(metadata)%?C<%Vd(withAlbumArt)|%Vd(noAlbumArt)>%?mv(3)<%Vd(volume)|%Vd(prog)>%?ps<%Vd(repeatModeDark)|%?mm<%Vd(noRepeatDark)|%Vd(repeatModeDark)>>>
%Vl(main,0,0,320,240,-)

# Album Art Preload
%Cl(0,0,140,140,r,c)
%?C<%Vd(albumArt)>

#######################
##### Lockscreen ######
#######################

%Vl(lockscreen,0,0,320,240,-)
%xd(bg_lock)

# Playback Mode Icons
%Vl(lockscreen,2,3,18,18,-)
%xd(playmodes,%mp)

# Clock
%Vl(lockscreen,10,40,-10,100,8)%Vf(23222a)
%ac%?cf<%cH|%cl>:%cM%?cf<| %cp>

# Day
%Vl(lockscreen,15,35,-15,16,9)%Vf(8d8676)
%ac%?cu<Monday|Tuesday|Wednesday|Thursday|Friday|Saturday|Sunday>, %cb %cd

# Lock Indicator
%Vl(lockscreen,156,7,8,10,-)
%?mh<%xd(L)>

# Now Playing
%Vl(lockscreen,15,193,-15,20,9)
%Vf(8d8676)
%?if(%mp, >, 1)<%ac%s%?ia<%ia> - %?it<%it|%fn>|>

#######################
##### Play Screen #####
#######################

%Vl(metadata,0,0,320,240,-)
%?C<%xd(bg_art)|%xd(bg_noart)>

# Album Art
%Vl(metadata,173,40,140,140,-)
%Cd

# No-Album-Art Glyph
%Vl(noAlbumArt,173,40,140,140,-)
%?C<|%xd(noart)>

# Playback Mode Icons
%Vl(metadata,2,3,18,18,-)
%xd(playmodes,%mp)

# Track Number (repeat)
%Vl(repeatModeDark,41,4,100,13,9)%Vf(23222a)
%al%pp of %pe

# Track Number (no repeat)
%Vl(noRepeatDark,22,4,100,13,9)%Vf(23222a)
%al%pp of %pe

# Repeat
%Vl(repeatModeDark,20,3,18,18,-)
%t(5)%?mm<%?ps<%xd(S)>|%xd(R,%mm,-1)>;%t(5)%?ps<%xd(S)|%?mm<|%xd(R,%mm,-1)>>

# Clock
%Vl(metadata,232,4,50,13,9)%Vf(23222a)
%ar%?cf<%cH|%cl>:%cM%?cf<| %cp>

# Battery viewport selector (flash when low, '+' prefix when charging)
%?bc<%Vd(battCharging)|%?if(%bl,>=,15)<%Vd(batt)|%Vd(lowBatt)>>

# Battery (percentage text)
%Vl(batt,275,4,40,13,9)%Vf(23222a)
%ar%bl

%Vl(lowBatt,275,4,40,13,9)%Vf(ff8a65)
%t(1)%ar%bl;%t(1) 

%Vl(battCharging,275,4,40,13,9)%Vf(ffca28)
%ar+%bl

# Track Artist or Title
%Vl(noAlbumArt,6,43,-10,30,7)%Vf(8d8676)
%?C<|%?mh<|%aL%s%?ia<%ia|%?it<%it|%fn>>>>

%Vl(withAlbumArt,6,43,160,30,7)%Vf(8d8676)
%?C<%?mh<|%aL%s%?ia<%ia|%?it<%it|%fn>>>>

# Track Title
%Vl(noAlbumArt,6,68,-10,36,3)%Vf(23222a)
%?C<|%?mh<|%aL%s%?ia<%it>>>

%Vl(withAlbumArt,6,68,160,36,3)%Vf(23222a)
%?C<%?mh<|%aL%s%?ia<%it>>>

# Album Title
%Vl(noAlbumArt,6,104,-10,15,5)%Vf(8d8676)
%?C<|%?mh<|%aL%s%?id<%id|Unknown Album>>>
%Vl(withAlbumArt,6,104,160,15,5)%Vf(8d8676)
%?C<%?mh<|%aL%s%?id<%id|Unknown Album>>>

# Album Year
%Vl(metadata,6,125,150,14,6)%Vf(8d8676)
%?iy<%ss(0,4, %iy)|>

# Volume Level Icons
%Vl(volume,289,219,16,16,-)
%xd(vol_up)

%Vl(volume,14,220,16,16,-)%Vf(ffca28)
%xd(vol_down)

# Volume Bar
%Vl(volume,45,225,230,4,-)%Vf(ffca28)
%pv(0,0,230,4,vol,backdrop,volbg,d)

# Time Elapsed
%Vl(prog,5,219,32,14,9)%Vf(23222a)
%al%?if(%pc,<,600)< |>%pc

# Progress Bar
%Vl(prog,45,225,230,4,-)
%pb(0,0,230,4,prog,backdrop,progbg)

# Total Track Time
%Vl(prog,281,219,32,14,9)%Vf(23222a)
%ar%pt
```

- [ ] **Step 2: Create `wps/AuroraLight.sbs`**

```
# Aurora Light by this project
# Neumorphic light theme -- see docs/superpowers/specs/2026-07-03-aurora-theme-design.md
# 2026-07-03 v1.0

# Disable Status Bar
%wd

# Preload Font
%Fl(2,12-Bricolage-Grotesque-Bold.fnt)
%Fl(3,32-Bricolage-Grotesque-Bold.fnt)
%Fl(4,16-Bricolage-Grotesque-Bold.fnt)

# Preload Images
%xl(bg_usb,usb-panel.bmp,0,0)
%xl(bg_qs,quickscreen.bmp,0,0)
%xl(bg_menu,menu.bmp,0,0)
%xl(usb,usb.bmp,0,0)
%xl(L,lock.bmp)
%xl(star,fallback.bmp,0,0)

# Album Art Preload
%Cl(-40,0,240,240,c,c)
%?if(%cs, !=, 21)<%?C<%Vd(noStar)|%Vd(withStar)>>

%?if(%cs, =, 10)<%Vd(qsTitle)%VI(clearScreen)|%?if(%cs, !=, 21)<%?C<%Vd(noStar)|%Vd(withStar)>>>


#__USB screen
%?if(%cs, =, 21)<%VI(clearScreen)%Vd(usb)|%VI(menu)%Vd(info)%Vd(batt)>

#__Clear Screen
%Vi(clearScreen,0,0,1,1,-)

# Backdrop (USB, quickscreen, and the normal menu each use a different panel)
%Vd(bgHolder)
%Vl(bgHolder,0,0,320,240,-)
%?if(%cs, =, 21)<%xd(bg_usb)|%?if(%cs, =, 10)<%xd(bg_qs)|%xd(bg_menu)>>

# Display Viewports

# --- With Album Art ---
%Vl(noStar,160,0,160,240,-)
%Cd

# --- No Album Art ---
%Vl(withStar,160,0,160,240,-)
%xd(star)

# Menu
%Vi(menu,0,24,160,216,-)
%Vf(8d8676)

# Menu Title
%Vl(info,6,4,101,16,4)
%Vf(23222a)
%al%s%?cs<Main Menu|%?Lt<%Lt>>

# Hold
%Vl(info,114,7,8,10,-)
%Vf(ffca28)
%?mh<%xd(L)>

# Quick Screen title viewport
%Vl(qsTitle,6,4,101,16,4)
%Vf(23222a)
%alQuick Screen

# Battery viewport selector (flash when low, '+' prefix when charging)
%?bc<%Vd(battCharging)|%?if(%bl,>=,15)<%Vd(batt)|%Vd(lowBatt)>>

# Battery (percentage text)
%Vl(batt,114,6,24,13,2)%Vf(23222a)
%ar%bl

%Vl(lowBatt,114,6,24,13,2)%Vf(ff8a65)
%t(1)%ar%bl;%t(1) 

%Vl(battCharging,114,6,24,13,2)%Vf(ffca28)
%ar+%bl

# USB_screen

#__USB
%Vl(usb,0,0,320,98,-)
%xd(usb)

%Vl(usb,20,160,280,-,3)%Vf(23222a)
%acUSB CONNECTED

%Vl(usb,20,195,280,-,2)%Vf(8d8676)
%acEject Before Disconnecting
```

- [ ] **Step 3: Commit**

```bash
git add wps/AuroraLight.wps wps/AuroraLight.sbs
git commit -m "$(cat <<'EOF'
viz: add wps/AuroraLight.wps and wps/AuroraLight.sbs

Same corrected structure as Aurora.wps/.sbs: bare-filename asset
references resolved against wps/AuroraLight/, per-screen-state
backdrops drawn via %xl()/%xd() viewport-local draws (not %X()),
an explicit %Vd(bgHolder) unhiding the SBS's backdrop-holder
viewport, and the two palette-dependent text colors swapped for
the light palette. The amber accent (ff8a65/ffca28) is shared
between both themes per the design spec, so those %Vf() values
are unchanged.

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>
EOF
)"
```

---

### Task 7: Author `themes/Aurora.cfg` and `themes/AuroraLight.cfg`

**Files:**
- Create: `themes/Aurora.cfg`, `themes/AuroraLight.cfg`

Modeled on `themes/Obsede2.cfg`. Two corrections versus blindly copying Obsede2's file: `battery display: numeric` (Obsede2's `graphical` was actually invalid — the real choice-setting values are `graphic`/`numeric`, see `apps/settings_list.c:332`; Aurora wants the percentage-text display anyway) and `font:` points at the new `19-Bricolage-Grotesque-Regular.fnt` base UI font from Task 4 (this is the font used by ordinary lists/menus, separate from the `%Fl`-preloaded WPS/SBS fonts).

- [ ] **Step 1: Create `themes/Aurora.cfg`**

```
# Aurora by this project
# Neumorphic dark default theme
# 2026-07-03 v1.0

wps: /.rockbox/wps/Aurora.wps
sbs: /.rockbox/wps/Aurora.sbs
selector type: bar (solid colour)
line selector start color: 2a2d3a
line selector end color: 1c1e26
line selector text color: e7e6ee
filetype colours: -
foreground color: e7e6ee
background color: 1c1e26
font: /.rockbox/fonts/19-Bricolage-Grotesque-Regular.fnt
statusbar: off
scrollbar: off
battery display: numeric
show icons: on
iconset: /.rockbox/icons/icons_5px.bmp
```

- [ ] **Step 2: Create `themes/AuroraLight.cfg`**

```
# Aurora Light by this project
# Neumorphic light companion to Aurora
# 2026-07-03 v1.0

wps: /.rockbox/wps/AuroraLight.wps
sbs: /.rockbox/wps/AuroraLight.sbs
selector type: bar (solid colour)
line selector start color: ffffff
line selector end color: ece6db
line selector text color: 23222a
filetype colours: -
foreground color: 23222a
background color: f4efe8
font: /.rockbox/fonts/19-Bricolage-Grotesque-Regular.fnt
statusbar: off
scrollbar: off
battery display: numeric
show icons: on
iconset: /.rockbox/icons/icons_5px.bmp
```

- [ ] **Step 3: Commit**

```bash
git add themes/Aurora.cfg themes/AuroraLight.cfg
git commit -m "$(cat <<'EOF'
theme: add Aurora and Aurora Light .cfg files

Modeled on themes/Obsede2.cfg. Fixes battery display: Obsede2's
"graphical" isn't one of the two valid choice-setting values
(graphic/numeric, apps/settings_list.c) -- Aurora correctly uses
"numeric" for its percentage-text battery display.

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>
EOF
)"
```

---

### Task 8: Packaging — bundle Aurora and set it as the default

**Files:**
- Modify: `packaging/retro-handheld/retro-handheld.make`
- Modify: `packaging/retro-handheld/config.cfg`

Obsede2's bundling lines and its `fonts/*.fnt` glob (which already sweeps up the new Bricolage `.fnt` files with no changes needed) stay untouched — Obsede2 remains installed and selectable from the Theme menu, just no longer the boot default. Unlike Obsede2, Aurora has no separate top-level `backdrops/` directory to bundle: its 6 backdrop bitmaps per theme live inside `wps/Aurora/`/`wps/AuroraLight/` alongside the icon bitmaps (see Task 2's note on why), so the single `cp -r wps/Aurora/.`/`cp -r wps/AuroraLight/.` line already covers them.

- [ ] **Step 1: Add Aurora/Aurora Light bundling to `retro-handheld.make`**

Find:

```makefile
	cp $(ROOTDIR)/icons/icons_5px.bmp $(RH_ROCKBOX_DIR)/icons/
	## Bundle prebuilt SF-Pro fonts (not .bdf sources, so convbdf cannot build them)
```

Replace with:

```makefile
	cp $(ROOTDIR)/icons/icons_5px.bmp $(RH_ROCKBOX_DIR)/icons/
	## Bundle Aurora / Aurora Light theme assets (wps, sbs, bitmaps incl. backdrops, theme cfg)
	mkdir -p $(RH_ROCKBOX_DIR)/wps/Aurora $(RH_ROCKBOX_DIR)/wps/AuroraLight
	cp $(ROOTDIR)/wps/Aurora.wps $(ROOTDIR)/wps/Aurora.sbs $(RH_ROCKBOX_DIR)/wps/
	cp $(ROOTDIR)/wps/AuroraLight.wps $(ROOTDIR)/wps/AuroraLight.sbs $(RH_ROCKBOX_DIR)/wps/
	cp -r $(ROOTDIR)/wps/Aurora/. $(RH_ROCKBOX_DIR)/wps/Aurora/
	cp -r $(ROOTDIR)/wps/AuroraLight/. $(RH_ROCKBOX_DIR)/wps/AuroraLight/
	cp $(ROOTDIR)/themes/Aurora.cfg $(ROOTDIR)/themes/AuroraLight.cfg $(RH_ROCKBOX_DIR)/themes/
	## Bundle prebuilt SF-Pro fonts (not .bdf sources, so convbdf cannot build them)
```

Find the comment just above the default-config install line:

```makefile
	## Install default config (sets Obsede2 as the active theme on first boot)
```

Replace with:

```makefile
	## Install default config (sets Aurora as the active theme on first boot)
```

- [ ] **Step 2: Point `config.cfg` at Aurora**

Read the current file, then replace its contents:

```
# .cfg file created by rockbox - http://www.rockbox.org

wps: /.rockbox/wps/Aurora.wps
sbs: /.rockbox/wps/Aurora.sbs
font: /.rockbox/fonts/19-Bricolage-Grotesque-Regular.fnt
iconset: /.rockbox/icons/icons_5px.bmp
selector type: bar (solid colour)
line selector start color: 2a2d3a
line selector end color: 1c1e26
line selector text color: e7e6ee
foreground color: e7e6ee
background color: 1c1e26
statusbar: off
scrollbar: off
battery display: numeric
show icons: on
```

- [ ] **Step 3: Commit**

```bash
git add packaging/retro-handheld/retro-handheld.make packaging/retro-handheld/config.cfg
git commit -m "$(cat <<'EOF'
build: bundle Aurora/Aurora Light and make Aurora the default theme

Mirrors how Obsede2 was bundled (explicit cp lines in the rhbuild
target, not the wps/WPSLIST + buildzip.pl path). Obsede2's own
bundling is untouched, so it stays installed and selectable from the
Theme menu -- only the shipped config.cfg's default changes.

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>
EOF
)"
```

---

### Task 9: Validate WPS/SBS syntax with `checkwps`

**Files:** none (validation gate)

- [ ] **Step 1: Build `checkwps` for the retro-handheld target**

```bash
cd tools/checkwps
../configure --target=retro-handheld --type=C --ram=32 --lcdwidth=320 --lcdheight=240
make
ls checkwps.retro-handheld
cd ../..
```

Expected: a `checkwps.retro-handheld` binary is produced with no build errors.

- [ ] **Step 2: Validate all 4 new tag files**

```bash
tools/checkwps/checkwps.retro-handheld wps/Aurora.wps wps/Aurora.sbs wps/AuroraLight.wps wps/AuroraLight.sbs
```

Expected: no `WPS_ERROR`/parse-failure output for any of the 4 files. If any tag is rejected (e.g. an unescaped character, an unknown viewport reference), fix it directly in the affected `wps/Aurora*.wps`/`.sbs` file from Task 5/6 and re-run this step until clean, then amend that task's commit with the fix (`git commit --amend` is fine here since these are this-session-only commits not yet shared).

- [ ] **Step 3: Clean up the checkwps build tree** (it's a local tool build, not shippable output)

```bash
cd tools/checkwps && make clean && cd ../..
```

No commit for this task — it's a validation gate. If Step 2 required a fix, that fix was already committed as part of amending the relevant earlier task.

---

### Task 10: Hosted-sim visual verification

**Files:** none (verification gate)

- [ ] **Step 1: Build (or confirm) the hosted SDL simulator**

The `build/` directory in this repo is already configured for the retro-handheld hosted/SDL target (confirmed: `build/rockbox` is a native ELF binary, `build/rockbox-info.txt` reports `Target: retro-handheld` / `CPU: hosted` / `Manufacturer: sdl`). Rebuild it with the new theme files included:

```bash
cd build && make && cd ..
```

Expected: clean build, no errors (this exercises `make fullinstall`-adjacent packaging only insofar as `make` alone; the pak-specific `rhbuild` bundling from Task 8 is exercised separately if you build the full pak — for sim purposes the sim reads `wps/`, `themes/`, `backdrops/`, `fonts/` straight from the source tree's `build/rockbox.zip`/install step, so a plain `make` here is sufficient to pick up the new files).

- [ ] **Step 2: Launch and select Aurora**

Use the project's `run` skill (or `docs/UISIMULATOR`'s documented flow: `cd build && ./rockbox`) to launch the hosted app. In Settings > Theme, select **Aurora**, then navigate to: Lockscreen (lock the device), Now Playing (with a loaded track — with and without album art if a test library with mixed tagged files is available), the main Menu / file browser, Quickscreen, and (if it can be triggered in sim) the USB-connected screen.

- [ ] **Step 3: Screen-dump and inspect each screen**

The hosted sim has a built-in screen-dump binding (`firmware/target/hosted/sdl/button-sdl.c`: `SDLK_F5` / numpad-0 → `sim_trigger_screendump()` → `firmware/screendump.c` writes a numbered `dump_NNNN.bmp` into the sim's `HOME_DIR`). On each screen from Step 2, press **F5**, then:

```bash
convert build/dump_0000.bmp /tmp/claude-1000/-home-user1-rockbox-milkdrop/16102b4b-6c00-4a21-adea-f0256c3e1b7b/scratchpad/aurora-preview/sim-<screen-name>.png
```

`screen_dump()` numbers files sequentially in the working directory the sim was launched from — adjust the path (or run `find . -name 'dump_*.bmp'`) if it isn't `build/dump_0000.bmp`.

Open each resulting PNG with the Read tool. Confirm for each screen: the correct backdrop panel renders (not a black/blank screen — the classic failure mode when a `%X()` filename doesn't resolve), text is legible against its panel, the progress/volume bars show the amber gradient fill over the inset track, playback-mode/repeat/shuffle icons appear correctly positioned (not shifted, not showing a magenta rectangle — a visible magenta patch means the transparency key wasn't applied correctly and a Task 3 icon needs regenerating with `new_transparent_canvas`), and the battery reads as a plain percentage number.

- [ ] **Step 4: Repeat Steps 2-3 for Aurora Light**

Same walk-through, selecting **Aurora Light** in Settings > Theme.

- [ ] **Step 5: Fix and re-verify, or conclude**

If anything in Step 3/4 looks wrong, trace it back to its source: bitmap issues go back to Task 2/3's generator functions (regenerate, don't hand-edit the `.bmp`), tag/layout issues go back to Task 5/6's `.wps`/`.sbs` content, palette issues go back to Task 1's `PALETTES` table. Make the fix, regenerate/re-copy as needed, amend the relevant task's commit, and repeat this task's verification until both themes look correct on every screen. No commit for this task itself — it's a verification gate, same pattern as Task 9.

---

## Self-Review Notes

- **Spec coverage:** Neumorphic Soft direction + Sunset Amber accent + dark/light palettes (Task 1 `PALETTES`) ✓. Bricolage Grotesque typography, CJK fallback preserved (Task 4, and the CJK-slot-preservation decision documented in the plan header) ✓. New rounded/procedural iconography, no `icons_5px.bmp` recolor needed since Aurora's own icons replace the WPS/SBS-local bitmaps while the shared `icons/icons_5px.bmp` iconset reference is left as-is in both `.cfg` files (matching Obsede2's own use of the stock iconset — the spec's iconography note is about Aurora's own asset bitmaps, which Task 3 covers) ✓. Percentage-only battery, no graphical battery bitmap (Task 3 drops battery.bmp/battery_charging.bmp; Task 5/6 use `%bl` text; Task 7 sets `battery display: numeric`) ✓. All 5 screens from the Screen Inventory (Lockscreen, Now Playing, Menu/SBS, Quickscreen, USB) get backdrops (Task 2) and are walked through in verification (Task 10) ✓. File layout matches the spec's tree exactly (Tasks 5-8) ✓. `packaging/retro-handheld/config.cfg` updated to point at Aurora (Task 8) ✓, matching commit `4a00a1b187`'s precedent for Obsede2 as referenced in the spec.
- **Non-goals honored:** no `LCD_WIDTH`/`LCD_HEIGHT` change; no OS-level dark/light auto-switching (Aurora Light is just a second selectable theme); milkdrop visualizer untouched; Obsede2's own WPS/SBS/viewport conventions untouched (Aurora follows the same conventions, doesn't rework them).
- **Open items resolved:** font sizes derived directly from Obsede2's existing slot usage (documented per-task); icon glyph list is Obsede2's 14 minus the 2 battery bitmaps plus 2 new ones (`noart.bmp`, `fallback.bmp`), each mapped 1:1 to the slot it fills; the no-album-art glyph is a waveform (`icon_noart`), a small, non-architecturally-significant choice as the spec itself notes.
- **Type/name consistency:** `PALETTES["aurora"]`/`PALETTES["auroralight"]` keys used identically in Tasks 1-3; `gen_aurora_assets.py`'s three CLI subcommands (`selftest`, `backdrops`, `icons`) match across Tasks 1-3; backdrop filenames (`Aurora.<tag>.bmp`) match between the Python `BACKDROPS` dict keys (Task 2) and the `%X()` calls in Task 5/6's `.wps`/`.sbs`; icon filenames (`wps/Aurora/<tag>.bmp`) match between the `ICONS` dict keys (Task 3) and the `%xl()`/`%x9()` references in Task 5/6; font filenames match exactly between Task 4's `convttf -o` targets and the `%Fl()` lines in Task 5/6 and the `font:` lines in Task 7/8.
