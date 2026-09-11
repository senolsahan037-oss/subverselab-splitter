"""Deterministic app icon for the Subverse Splitter desktop build.

What it replaces was a stock multicoloured "S" — orange, teal, blue and navy
ribbons on white — that belonged to no product and shared nothing with the
SubverseLab palette. In a Dock beside anything else of ours it read as a
different company's app.

The mark is the one the web Splitter and the plugin header already carry: a
solid column on the left, the same bands pulled apart on the right. One thing,
then several. It is drawn here rather than copied from a PNG because an app
icon needs a ground — a transparent mark on the Dock sits on whatever wallpaper
is behind it — and because the artwork has to be inset from the canvas edge the
way macOS expects rather than filling it.
"""

from pathlib import Path

from PIL import Image, ImageDraw

OUT = Path(__file__).resolve().parents[1] / "Resources" / "dock_icon.png"

GROUND = (3, 40, 37, 255)      # #032825, the brand ground
GOLD   = (197, 160, 89, 255)   # #C5A059
TEAL   = (42, 157, 143, 255)   # #2A9D8F

SIZE = 1024
BANDS = 4


def build() -> Image.Image:
    # Drawn at 4x and reduced, which is cheaper than antialiasing the rounded
    # corners by hand and gives the same result.
    scale = 4
    canvas = SIZE * scale
    image = Image.new("RGBA", (canvas, canvas), (0, 0, 0, 0))
    draw = ImageDraw.Draw(image)

    # macOS insets its icons: artwork filling the square reads as oversized
    # next to every system app. About a tenth on each side.
    inset = canvas * 0.10
    ground = [inset, inset, canvas - inset, canvas - inset]
    draw.rounded_rectangle(ground, radius=canvas * 0.19, fill=GROUND)

    # The mark, inside the ground with its own breathing room.
    pad = canvas * 0.26
    left, top = pad, pad
    right, bottom = canvas - pad, canvas - pad
    height = bottom - top

    column_w = (right - left) * 0.30
    gutter = (right - left) * 0.16
    stems_x = left + column_w + gutter
    stems_w = right - stems_x
    radius = canvas * 0.018

    # The mix: bands flush against one another, masked to one rounded column so
    # the joins between them do not read as gaps.
    band_h = height / BANDS
    column = Image.new("RGBA", (canvas, canvas), (0, 0, 0, 0))
    column_draw = ImageDraw.Draw(column)
    for index in range(BANDS):
        y = top + index * band_h
        column_draw.rectangle(
            [left, y, left + column_w, y + band_h],
            fill=GOLD if index % 2 == 0 else TEAL,
        )
    mask = Image.new("L", (canvas, canvas), 0)
    ImageDraw.Draw(mask).rounded_rectangle(
        [left, top, left + column_w, bottom], radius=radius, fill=255
    )
    image.paste(column, (0, 0), mask)

    # The stems: the same bands with air between them.
    stem_h = band_h * 0.62
    spare = (height - BANDS * stem_h) / (BANDS - 1)
    for index in range(BANDS):
        y = top + index * (stem_h + spare)
        draw.rounded_rectangle(
            [stems_x, y, stems_x + stems_w, y + stem_h],
            radius=radius,
            fill=GOLD if index % 2 == 0 else TEAL,
        )

    return image.resize((SIZE, SIZE), Image.LANCZOS)


if __name__ == "__main__":
    icon = build()
    icon.save(OUT)
    print(f"  {OUT.relative_to(OUT.parents[1])}  {icon.size[0]}x{icon.size[1]}")
