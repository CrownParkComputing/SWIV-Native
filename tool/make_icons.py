#!/usr/bin/env python3
"""Builds the launcher icon and every size Android asks for.

Same three-part icon as the other Retro-* front ends - the Retro script cut
from the Retro Recompilation logo, the title's name under it in the
wordmark's chrome-blue gradient, and the title's own mark below that - so
SWIV sits with the rest of the family on a home screen and reads as one set.
The mark is the only thing that changes: SWIV is a vertical shooter whose
player's helicopter is the most distinctive thing in it, so the mark is a
top-down helicopter - a rotor disc, a body, a tail boom and a tail rotor -
drawn from geometry rather than traced from a sprite.

    python3 tool/make_icons.py

Run from the SWIV-Native root. Overwrites the mipmaps and writes the
adaptive-icon foreground and background colour.
"""

from __future__ import annotations

import os
from PIL import Image, ImageDraw, ImageFilter, ImageFont

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LOGO = os.path.join(HERE, "assets", "retro_recomp_logo.png")
FONT = "/usr/share/fonts/liberation/LiberationSans-Bold.ttf"

SIZE = 1024

BG_TOP = (8, 12, 26)
BG_BOTTOM = (3, 4, 9)

# The wordmark's blue, top to bottom: white highlight into deep blue.
CHROME = [
    (232, 244, 255),
    (150, 205, 250),
    (56, 120, 220),
    (26, 60, 160),
    (120, 180, 240),
]

# The mark is a helicopter, not a satellite - so it is warm, the colour of
# SWIV's bullet tracers and explosions, not the wordmark's cool blue. That
# also gives the icon contrast against the dark blue plate.
HELI = [
    (255, 246, 214),  # near-white highlight on the rotor blade
    (255, 214, 96),   # hot yellow
    (255, 142, 48),   # orange
    (214, 58, 28),    # red-orange core
    (255, 178, 86),   # amber tail
]
HELI_DARK = (56, 18, 8)


def vertical_gradient(size, colours):
    width, height = size
    grad = Image.new("RGB", (1, height))
    pixels = grad.load()
    steps = len(colours) - 1
    for y in range(height):
        position = y / max(1, height - 1) * steps
        index = min(int(position), steps - 1)
        blend = position - index
        start, end = colours[index], colours[index + 1]
        pixels[0, y] = tuple(
            int(start[c] + (end[c] - start[c]) * blend) for c in range(3)
        )
    return grad.resize((width, height))


def background():
    canvas = vertical_gradient((SIZE, SIZE), [BG_TOP, BG_BOTTOM]).convert("RGBA")
    glow = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0))
    draw = ImageDraw.Draw(glow)
    # The glow leans orange now, echoing the mark, so the whole plate warms up
    # rather than reading as a pure dark blue next to the warm helicopter.
    draw.ellipse((60, 250, SIZE - 60, SIZE - 120), fill=(180, 110, 40, 90))
    draw.ellipse((200, 520, SIZE - 200, SIZE - 60), fill=(220, 70, 40, 80))
    glow = glow.filter(ImageFilter.GaussianBlur(120))
    return Image.alpha_composite(canvas, glow)


def retro_script(width):
    """The Retro wordmark, cut out of the logo rather than redrawn.

    The brush-script "Retro" runs from x=290 to x=1080 in the logo, y=0 to
    y=115. A narrower crop (the old one, 168..578) caught only the capital
    R, which read as a brand mark but not as the word "Retro" - the family
    wants the whole word, the way the deployed Retro-Saturn and Retro-Amiga
    icons show it.
    """
    logo = Image.open(LOGO).convert("RGBA")
    script = logo.crop((290, 0, 1080, 115))
    # The bracket rules either side of the script poke into the crop. Every
    # pixel of the script is warm, so anything bluer than it is red is a rule.
    pixels = script.load()
    for y in range(script.height):
        for x in range(script.width):
            r, g, b, a = pixels[x, y]
            if a and b > r:
                pixels[x, y] = (r, g, b, 0)
    height = round(script.height * width / script.width)
    return script.resize((width, height), Image.LANCZOS)


def chrome_text(text, width, height):
    """[text] in the wordmark's blue, with the dark outline it has."""
    size = 10
    font = ImageFont.truetype(FONT, size)
    while True:
        probe = ImageFont.truetype(FONT, size + 4)
        box = probe.getbbox(text)
        if box[2] - box[0] > width or box[3] - box[1] > height:
            break
        size += 4
        font = probe

    box = font.getbbox(text)
    pad = 18
    layer = Image.new("RGBA", (box[2] - box[0] + pad * 2, box[3] - box[1] + pad * 2))
    ImageDraw.Draw(layer).text(
        (pad - box[0], pad - box[1]), text, font=font, fill=(255, 255, 255, 255)
    )

    mask = layer.split()[3]
    fill = vertical_gradient(layer.size, CHROME).convert("RGBA")
    fill.putalpha(mask)

    outline = Image.new("RGBA", layer.size, (0, 0, 0, 0))
    outline.paste((12, 20, 48, 255), (0, 0), mask.filter(ImageFilter.MaxFilter(9)))
    return Image.alpha_composite(outline, fill)


def heli(width):
    """The SWIV mark: a compact side-view helicopter silhouette.

    A top-down rotor disc with a long tail boom was the obvious shape, but at
    icon scale its tail pushed the artwork stack taller than the wordmark
    above it - the helicopter then dominated and the Retro R + SWIV wordmark
    dissolved. The side view is shorter: a thin rotor disc on top, a fat body
    below, a tail boom trailing right with a tail rotor at the end. Same
    recognisable helicopter, half the height. Geometry, not a sprite trace.
    """
    height = round(width * 0.78)
    scale = 4
    w = width * scale
    h = height * scale
    layer = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    draw = ImageDraw.Draw(layer)

    cx = w / 2

    # Rotor disc, seen almost edge-on - a flat ellipse on top of the body.
    # The ellipse's vertical thickness is the rotor's apparent depth.
    rotor_w = w * 0.62
    rotor_h = h * 0.18
    rotor_cy = h * 0.26
    rotor_thick = round(rotor_h * 0.42)
    draw.ellipse(
        (cx - rotor_w / 2, rotor_cy - rotor_h / 2,
         cx + rotor_w / 2, rotor_cy + rotor_h / 2),
        outline=(255, 255, 255, 255),
        width=rotor_thick,
    )

    # Hub on top of the rotor - the mast where it joins the body.
    hub_r = rotor_thick * 0.55
    draw.ellipse(
        (cx - hub_r, rotor_cy - hub_r, cx + hub_r, rotor_cy + hub_r),
        fill=(255, 255, 255, 255),
    )

    # Body - a fat rounded shape under the rotor. Wider than tall so it reads
    # as a fuselage, not a bomb.
    body_top = rotor_cy + rotor_h / 2 - rotor_thick * 0.2
    body_bot = h * 0.72
    body_w = w * 0.30
    body_left = cx - body_w / 2 + w * 0.02  # shift slightly right so the tail fits
    body_right = body_left + body_w
    draw.ellipse(
        (body_left, body_top, body_right, body_bot),
        fill=(255, 255, 255, 255),
    )

    # Cockpit window - a thin slit cut from the body shape, dark, so the body
    # is not a featureless oval. The cockpit reads as a cockpit, not a hole.
    cockpit_top = body_top + (body_bot - body_top) * 0.18
    cockpit_bot = cockpit_top + (body_bot - body_top) * 0.30
    cockpit_left = body_left + body_w * 0.18
    cockpit_right = body_right - body_w * 0.30
    draw.rounded_rectangle(
        (cockpit_left, cockpit_top, cockpit_right, cockpit_bot),
        radius=round((cockpit_right - cockpit_left) * 0.30),
        fill=(56, 18, 8, 255),
    )

    # Tail boom - a thin rectangle trailing right from the body to where the
    # tail rotor sits. The boom's vertical centre tracks the body's centre.
    boom_cy = body_top + (body_bot - body_top) * 0.55
    boom_thick = h * 0.07
    boom_left = body_right - w * 0.02
    boom_right = w * 0.94
    draw.rectangle(
        (boom_left, boom_cy - boom_thick / 2,
         boom_right, boom_cy + boom_thick / 2),
        fill=(255, 255, 255, 255),
    )

    # Tail rotor - a small disc on top of the boom, where a real helicopter's
    # tail rotor would be. Smaller than the body, larger than the boom.
    tail_r = h * 0.13
    tail_cy = boom_cy
    draw.ellipse(
        (boom_right - tail_r * 0.55, tail_cy - tail_r,
         boom_right + tail_r * 0.55, tail_cy + tail_r),
        fill=(255, 255, 255, 255),
    )

    shape = layer.split()[3]
    fill = vertical_gradient((w, h), HELI).convert("RGBA")
    fill.putalpha(shape)

    # A dark rim under the whole mark, the same trick the wordmark uses, so
    # the helicopter keeps its edge against the warm glow behind it.
    rim = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    rim.paste(HELI_DARK + (255,), (0, 0), shape.filter(ImageFilter.MaxFilter(9)))
    stamped = Image.alpha_composite(rim, fill)
    return stamped.resize((width, height), Image.LANCZOS)


def artwork(width):
    layer = Image.new("RGBA", (width, width), (0, 0, 0, 0))

    # The Retro wordmark is wider than the old "R" was, so it claims more
    # of the canvas at the top. SWIV sits underneath in the same chrome
    # blue the family uses for the title; the helicopter is the smaller
    # accent at the bottom, the way the Saturn swirl sits under "SATURN".
    script = retro_script(round(width * 0.84))
    name = chrome_text("SWIV", round(width * 0.68), round(width * 0.20))
    mark = heli(round(width * 0.46))

    stack = script.height + name.height + mark.height + round(width * 0.06)
    top = max(0, (width - stack) // 2)

    layer.alpha_composite(script, ((width - script.width) // 2, top))
    top += script.height + round(width * 0.020)
    layer.alpha_composite(name, ((width - name.width) // 2, top))
    top += name.height + round(width * 0.035)
    layer.alpha_composite(mark, ((width - mark.width) // 2, top))
    return layer


def master():
    canvas = background()
    art = artwork(round(SIZE * 0.86))
    canvas.alpha_composite(art, ((SIZE - art.width) // 2, (SIZE - art.width) // 2))
    return canvas


def foreground():
    """Everything inside the middle two-thirds, where the launcher's mask
    cannot eat it."""
    layer = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0))
    art = artwork(round(SIZE * 0.62))
    layer.alpha_composite(art, ((SIZE - art.width) // 2, (SIZE - art.width) // 2))
    return layer


def rounded(image, radius_fraction=0.22):
    mask = Image.new("L", image.size, 0)
    ImageDraw.Draw(mask).rounded_rectangle(
        (0, 0, image.width - 1, image.height - 1),
        radius=round(image.width * radius_fraction),
        fill=255,
    )
    out = image.copy()
    out.putalpha(mask)
    return out


def main():
    icon = master()
    fore = foreground()
    back = background()

    res = os.path.join(HERE, "android", "app", "src", "main", "res")
    legacy = {"mdpi": 48, "hdpi": 72, "xhdpi": 96, "xxhdpi": 144, "xxxhdpi": 192}
    layers = {"mdpi": 108, "hdpi": 162, "xhdpi": 216, "xxhdpi": 324, "xxxhdpi": 432}
    for density, px in legacy.items():
        folder = os.path.join(res, f"mipmap-{density}")
        os.makedirs(folder, exist_ok=True)
        icon.resize((px, px), Image.LANCZOS).save(
            os.path.join(folder, "ic_launcher.png")
        )
        rounded(icon.resize((px, px), Image.LANCZOS), 0.5).save(
            os.path.join(folder, "ic_launcher_round.png")
        )
        fore.resize((layers[density],) * 2, Image.LANCZOS).save(
            os.path.join(folder, "ic_launcher_foreground.png")
        )
        back.convert("RGB").resize((layers[density],) * 2, Image.LANCZOS).save(
            os.path.join(folder, "ic_launcher_background.png")
        )

    # Adaptive icon descriptor - Android picks this when present and ignores
    # the legacy ic_launcher.png on the launchers that use a shape mask.
    adaptive = os.path.join(res, "mipmap-anydpi-v26")
    os.makedirs(adaptive, exist_ok=True)
    for name in ("ic_launcher.xml", "ic_launcher_round.xml"):
        with open(os.path.join(adaptive, name), "w") as handle:
            handle.write(
                '<?xml version="1.0" encoding="utf-8"?>\n'
                '<adaptive-icon xmlns:android="http://schemas.android.com/apk/res/android">\n'
                '    <background android:drawable="@mipmap/ic_launcher_background"/>\n'
                '    <foreground android:drawable="@mipmap/ic_launcher_foreground"/>\n'
                "</adaptive-icon>\n"
            )

    # The adaptive icon composes a colour resource, not the gradient layer
    # written above, so derive that colour from the same BG_TOP the icon is
    # built on. Left to drift, the masked foreground floats on a plate that
    # does not match the icon beside it.
    plate = "#%02X%02X%02X" % BG_TOP
    for bucket in ("values", "values-night"):
        path = os.path.join(res, bucket, "ic_launcher_background.xml")
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "w") as handle:
            handle.write(
                '<?xml version="1.0" encoding="utf-8"?>\n'
                "<resources>\n"
                '    <color name="ic_launcher_background">'
                + plate
                + "</color>\n"
                "</resources>\n"
            )

    # Keep a 512 px PNG at the repo root for store listings and the README.
    out512 = os.path.join(HERE, "ic_launcher-playstore.png")
    icon.convert("RGB").resize((512, 512), Image.LANCZOS).save(out512)

    print("icons written")


if __name__ == "__main__":
    main()
