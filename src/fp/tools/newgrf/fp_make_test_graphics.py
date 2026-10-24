#!/usr/bin/env python3
"""Draw the sprites of the test NewGRF of the first-person mode: a tower and a cottage, in 8 bpp and 32 bpp."""

import sys

from PIL import Image, ImageDraw
from nml import palette

# Company colour ramp of the OpenTTD palette; random colours recolour it.
WALL_LEFT, WALL_RIGHT = 0xC8, 0xCC
ROOF = 0x07
WINDOW = 0x0F
TRANSPARENT = 0x00


def box(draw, origin_x, height, colours, roof):
    """Draw an isometric box over a full tile, origin_x being the north corner, the top edge of the image at -height."""

    def at(x, y):
        return (origin_x + x, y + height)

    left, right = colours
    draw.polygon([at(-31, 16), at(0, 32), at(0, 32 - height), at(-31, 16 - height)], fill=left)
    draw.polygon([at(0, 32), at(32, 16), at(32, 16 - height), at(0, 32 - height)], fill=right)
    draw.polygon([at(0, -height), at(32, 16 - height), at(0, 32 - height), at(-31, 16 - height)], fill=roof)
    for floor in range(8, height - 4, 12):
        for x in (-24, -14):
            y = 16 + (x + 31) // 2 - floor
            draw.rectangle([at(x, y - 4), at(x + 4, y)], fill=WINDOW)
        for x in (8, 18):
            y = 32 - x // 2 - floor
            draw.rectangle([at(x, y - 4), at(x + 4, y)], fill=WINDOW)


def main(out_8bpp, out_32bpp):
    image = Image.new("P", (160, 88), TRANSPARENT)
    image.putpalette(palette.raw_palette_data[0])
    draw = ImageDraw.Draw(image)
    box(draw, 31, 56, (WALL_LEFT, WALL_RIGHT), ROOF)
    box(draw, 80 + 31, 24, (WALL_LEFT, WALL_RIGHT), ROOF)
    image.save(out_8bpp)

    rgba = Image.new("RGBA", (64, 88), (0, 0, 0, 0))
    tower = image.crop((0, 0, 64, 88))
    colours = tower.convert("RGBA")
    for y in range(88):
        for x in range(64):
            if tower.getpixel((x, y)) == TRANSPARENT:
                continue
            r, g, b, a = colours.getpixel((x, y))
            # A warm glass facade, so the 32 bpp sprite is told apart from the 8 bpp one at a glance.
            rgba.putpixel((x, y), (min(255, r + 60), g, max(0, b - 40), 255))
    rgba.save(out_32bpp)


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
