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
    "usb":                 lambda p: backdrop_usb(p),
}


def gen_backdrops(theme, outdir):
    palette = PALETTES[theme]
    name = "Aurora" if theme == "aurora" else "AuroraLight"
    for tag, fn in BACKDROPS.items():
        img = fn(palette)
        assert img.size == (LCD_W, LCD_H), f"{tag}: expected {(LCD_W, LCD_H)}, got {img.size}"
        save_bmp(img, os.path.join(outdir, f"{name}.{tag}.bmp"))
        print("wrote", f"{name}.{tag}.bmp", img.size, img.mode)


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
