#!/usr/bin/env python3
#
# Builds a small NewGRF with 32bpp sprites for live tests of the first-person mode.
#
# OpenGFX, the base set found in test environments, is 8bpp only. This NewGRF
# replaces two base sprites with 32bpp ones, so the sprite bridge can be
# checked on what NewGRF authors really ship:
#
#   3981 (flat grass): 32bpp with translucent edges, drawn at 1x and at 4x;
#                      the 4x version has detail of its own, so the bridge
#                      must take it.
#   2982 (a steam engine): 32bpp at 1x, and at 2x with a recolour mask in
#                      the company colour range, so company colours apply.
#
# Usage: fp_test_grf.py <output dir>
#
#   Writes fp_test.grf and the expected 4x grass (expected_grass_4x.png) to the
#   directory. Needs NML (pip install nml), which brings Pillow along.
#   fp_live_test.sh loads the NewGRF in a new game with FP_NEWGRF=<file>.

import os
import subprocess
import sys

from PIL import Image
from nml import palette

COMPANY_COLOUR_FIRST = 198  # First of the eight palette indices that company colours remap.


def diamond(width, height, x, y):
    """Distance-like measure of a pixel from the centre of a tile diamond; 1 is the edge."""
    return abs(x + 0.5 - width / 2) / (width / 2) + abs(y + 0.5 - height / 2) / (height / 2)


def grass(scale):
    """Flat grass tile at a zoom of 'scale' times 1x: smooth colours no palette has, translucent edges."""
    width, height = 64 * scale, 31 * scale
    image = Image.new("RGBA", (width, height), (0, 0, 0, 0))
    pixels = image.load()
    for y in range(height):
        for x in range(width):
            d = diamond(width, height, x, y)
            if d > 1.0:
                continue
            alpha = 255 if d < 0.93 else 128
            r = 40 + (x * 120) // width
            g = 150 + (y * 90) // height
            b = 50
            if scale > 1 and (x + y) % 2 == 0:
                # Detail only the 4x version has: a fine checker.
                r, g = r + 20, g - 20
            pixels[x, y] = (r, g, b, alpha)
    return image


def engine(scale):
    """A simple engine: a grey body, which the mask turns into company colours, and a black chimney."""
    width, height = 22 * scale, 17 * scale
    image = Image.new("RGBA", (width, height), (0, 0, 0, 0))
    mask = Image.new("P", (width, height), 0)
    mask.putpalette(palette.raw_palette_data[0])
    pixels = image.load()
    mask_pixels = mask.load()
    for y in range(height):
        for x in range(width):
            u, v = x / scale, y / scale
            if 2 <= u < 20 and 7 <= v < 14:
                # Body: brightness falls from top to bottom, the mask picks the company colour ramp.
                level = 230 - int((v - 7) * 18)
                pixels[x, y] = (level, level, level, 255)
                mask_pixels[x, y] = COMPANY_COLOUR_FIRST + min(7, int((v - 7) * 8 / 7))
            elif 4 <= u < 7 and 3 <= v < 7:
                pixels[x, y] = (30, 30, 34, 255)
            elif 3 <= u < 19 and 14 <= v < 16:
                pixels[x, y] = (60, 50, 40, 255)
    return image, mask


def to_8bpp(image, mask=None):
    """Convert to the palette of the game, which the 8bpp sprite every NewGRF must have needs; index 0 is transparent."""
    # Only plain colours may match: index 0 is transparent and indices from 227 on are animated,
    # so they get magenta, which none of the test images comes near.
    colours = list(palette.raw_palette_data[0])
    for index in [0] + list(range(227, 256)):
        colours[index * 3:index * 3 + 3] = [255, 0, 255]
    reference = Image.new("P", (1, 1))
    reference.putpalette(colours)
    indexed = image.convert("RGB").quantize(palette=reference, dither=Image.Dither.NONE)
    indexed.putpalette(palette.raw_palette_data[0])
    pixels = indexed.load()
    source = image.load()
    mask_pixels = mask.load() if mask is not None else None
    for y in range(image.height):
        for x in range(image.width):
            if source[x, y][3] < 128:
                pixels[x, y] = 0
            elif mask_pixels is not None and mask_pixels[x, y] != 0:
                pixels[x, y] = mask_pixels[x, y]
            elif pixels[x, y] == 0:
                pixels[x, y] = 1
    return indexed


NML = """grf {
	grfid: "FP\\01\\01";
	name: string(STR_GRF_NAME);
	desc: string(STR_GRF_DESC);
	version: 1;
	min_compatible_version: 1;
}

replace fp_grass(3981, "grass_1x_8bpp.png") { [0, 0, 64, 31, -31, 0] }
alternative_sprites(fp_grass, ZOOM_LEVEL_NORMAL, BIT_DEPTH_32BPP, "grass_1x.png") { [0, 0, 64, 31, -31, 0] }
alternative_sprites(fp_grass, ZOOM_LEVEL_IN_4X, BIT_DEPTH_32BPP, "grass_4x.png") { [0, 0, 256, 124, -124, 0] }

replace fp_engine(2982, "engine_1x_8bpp.png") { [0, 0, 22, 17, -14, -9] }
alternative_sprites(fp_engine, ZOOM_LEVEL_NORMAL, BIT_DEPTH_32BPP, "engine_1x.png", "engine_1x_mask.png") { [0, 0, 22, 17, -14, -9] }
alternative_sprites(fp_engine, ZOOM_LEVEL_IN_2X, BIT_DEPTH_32BPP, "engine_2x.png", "engine_2x_mask.png") { [0, 0, 44, 34, -28, -18] }
"""

LANG = """##grflangid 0x01
STR_GRF_NAME :OTTD-FP sprite test
STR_GRF_DESC :32bpp sprites, a 4x sprite and a recolour mask for the first-person mode tests.
"""


def main():
    if len(sys.argv) != 2:
        print(f"Usage: {sys.argv[0]} <output dir>", file=sys.stderr)
        return 2

    out = os.path.abspath(sys.argv[1])
    os.makedirs(os.path.join(out, "lang"), exist_ok=True)

    grass_1x = grass(1)
    grass_1x.save(os.path.join(out, "grass_1x.png"))
    to_8bpp(grass_1x).save(os.path.join(out, "grass_1x_8bpp.png"))
    grass(4).save(os.path.join(out, "grass_4x.png"))
    grass(4).save(os.path.join(out, "expected_grass_4x.png"))
    engine_1x, mask_1x = engine(1)
    engine_1x.save(os.path.join(out, "engine_1x.png"))
    mask_1x.save(os.path.join(out, "engine_1x_mask.png"))
    to_8bpp(engine_1x, mask_1x).save(os.path.join(out, "engine_1x_8bpp.png"))
    engine_2x, mask_2x = engine(2)
    engine_2x.save(os.path.join(out, "engine_2x.png"))
    mask_2x.save(os.path.join(out, "engine_2x_mask.png"))

    with open(os.path.join(out, "fp_test.nml"), "w") as f:
        f.write(NML)
    with open(os.path.join(out, "lang", "english.lng"), "w") as f:
        f.write(LANG)

    subprocess.run(["nmlc", "--quiet", "--grf", "fp_test.grf", "fp_test.nml"], cwd=out, check=True)
    print(os.path.join(out, "fp_test.grf"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
