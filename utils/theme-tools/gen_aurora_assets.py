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
