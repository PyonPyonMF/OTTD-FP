/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file fp_sprite_image.h Pixels of sprites as the first-person mode uses them. */

#ifndef FP_SPRITE_IMAGE_H
#define FP_SPRITE_IMAGE_H

#include "../../spriteloader/spriteloader.hpp"
#include "../../zoom_func.h"

namespace fp {

/** A pixel of a sprite image: sRGB colour with straight, not premultiplied, alpha. */
struct Rgba8 {
	uint8_t r = 0; ///< Red.
	uint8_t g = 0; ///< Green.
	uint8_t b = 0; ///< Blue.
	uint8_t a = 0; ///< Alpha; 0 is fully transparent.

	bool operator==(const Rgba8 &other) const = default;
};
static_assert(sizeof(Rgba8) == 4);

/**
 * Size and placement of a sprite image.
 *
 * Like in the 2D view, the offsets tell where the top left pixel lies
 * relative to the anchor of the sprite, e.g. the north corner of a tile or
 * the position of a vehicle. Everything is counted in pixels of #zoom.
 */
struct SpriteGeometry {
	uint16_t width = 0; ///< Width, in pixels.
	uint16_t height = 0; ///< Height, in pixels.
	int16_t x_offs = 0; ///< Horizontal offset of the left column from the anchor, in pixels.
	int16_t y_offs = 0; ///< Vertical offset of the top row from the anchor, in pixels.
	ZoomLevel zoom = ZoomLevel::Min; ///< Zoom level the pixels belong to; ZoomLevel::Min is the most detailed one.

	/**
	 * Size of a pixel in pixels of the most detailed zoom level.
	 * @return 1 for ZoomLevel::Min, 2 for ZoomLevel::In2x and 4 for ZoomLevel::Normal.
	 */
	int Scale() const { return ScaleByZoom(1, this->zoom); }
};

/**
 * A sprite as the sprite loader delivers it, before any palette is applied.
 * Each pixel carries RGB, alpha and a palette index; 8bpp sprites only have
 * the palette index, 32bpp sprites have RGB and, for recolourable parts,
 * a palette index as well.
 */
struct RawSprite : SpriteGeometry {
	SpriteComponents colours{}; ///< Colour components the sprite was loaded with.
	std::vector<SpriteLoader::CommonPixel> pixels; ///< Pixels, row by row from the top.

	/**
	 * Memory used by the sprite.
	 * @return Size in bytes.
	 */
	size_t MemorySize() const { return sizeof(*this) + this->pixels.size() * sizeof(SpriteLoader::CommonPixel); }
};

/** A sprite with its palette applied, ready to be shown. */
struct SpriteImage : SpriteGeometry {
	std::vector<Rgba8> pixels; ///< Pixels, row by row from the top.

	/**
	 * Memory used by the image.
	 * @return Size in bytes.
	 */
	size_t MemorySize() const { return sizeof(*this) + this->pixels.size() * sizeof(Rgba8); }

	/**
	 * A pixel of the image.
	 * @param x Column.
	 * @param y Row.
	 * @return The pixel.
	 */
	const Rgba8 &At(int x, int y) const { return this->pixels[static_cast<size_t>(y) * this->width + x]; }
};

/** How the palette of a sprite changes when it is drawn; the counterpart of the remapping BlitterMode values. */
enum class SpriteRemapMode : uint8_t {
	None, ///< Drawn as it is, like with PAL_NONE.
	Colour, ///< Palette indices are replaced through a recolour sprite, e.g. for company colours.
	Crash, ///< Like #Colour, and pixels without palette index turn dark grey; PALETTE_CRASH.
	Black, ///< Every visible pixel turns black; PALETTE_ALL_BLACK.
};

/** A palette remap to apply to a sprite. */
struct SpriteRemap {
	SpriteRemapMode mode = SpriteRemapMode::None; ///< The kind of remap.
	std::array<uint8_t, 256> table{}; ///< For #SpriteRemapMode::Colour and #SpriteRemapMode::Crash: new palette index for each index; 0 hides the pixel.
};

} // namespace fp

#endif /* FP_SPRITE_IMAGE_H */
