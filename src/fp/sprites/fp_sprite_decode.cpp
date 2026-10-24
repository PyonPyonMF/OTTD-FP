/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file fp_sprite_decode.cpp Turning sprites of the sprite loader into images. */

#include "../../stdafx.h"
#include "fp_sprite_decode.h"
#include "../../blitter/32bpp_base.hpp"
#include "../../palette_func.h"

#include "../../safeguards.h"

namespace fp {

/**
 * Check whether two pixels are the same.
 * @param a The one pixel.
 * @param b The other pixel.
 * @return True iff all channels match.
 */
static bool SamePixel(const SpriteLoader::CommonPixel &a, const SpriteLoader::CommonPixel &b)
{
	return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a && a.m == b.m;
}

/**
 * Check whether a zoom level is a plain doubling of the next coarser one,
 * which is how the sprite cache fills levels the sprite has no pixels for.
 * @param fine The more detailed zoom level.
 * @param coarse The zoom level with half the resolution.
 * @return True iff \a fine has no information that \a coarse lacks.
 */
static bool IsDoublingOf(const SpriteLoader::Sprite &fine, const SpriteLoader::Sprite &coarse)
{
	if (fine.data == nullptr || coarse.data == nullptr) return false;
	if (fine.width != coarse.width * 2 || fine.height != coarse.height * 2) return false;
	if (fine.x_offs != coarse.x_offs * 2 || fine.y_offs != coarse.y_offs * 2) return false;

	for (uint y = 0; y < fine.height; y++) {
		const SpriteLoader::CommonPixel *fine_row = fine.data + static_cast<size_t>(y) * fine.width;
		const SpriteLoader::CommonPixel *coarse_row = coarse.data + static_cast<size_t>(y / 2) * coarse.width;
		for (uint x = 0; x < fine.width; x++) {
			if (!SamePixel(fine_row[x], coarse_row[x / 2])) return false;
		}
	}
	return true;
}

/**
 * Choose the zoom level to take the pixels of a sprite from.
 *
 * The sprite cache fills every zoom level up to ZoomLevel::Min, the 4x
 * one, by doubling pixels of the most detailed level the sprite really has.
 * Taking such a level would cost up to sixteen times the memory without
 * adding a single detail, so the most detailed level that is not a doubling
 * of the next coarser one is taken. This never loses information.
 * @param collection The sprite at all zoom levels.
 * @return A level between ZoomLevel::Min and ZoomLevel::Normal.
 */
ZoomLevel SelectDetailedZoom(const SpriteLoader::SpriteCollection &collection)
{
	ZoomLevel zoom = ZoomLevel::Min;
	while (zoom < ZoomLevel::Normal && IsDoublingOf(collection[zoom], collection[zoom + 1])) ++zoom;
	return zoom;
}

/**
 * Copy the pixels of a sprite from the sprite loader.
 * @param collection The sprite at all zoom levels, as SpriteEncoder::Encode() gets it.
 * @param[out] raw Receives the most detailed level, see SelectDetailedZoom().
 */
void CaptureSprite(const SpriteLoader::SpriteCollection &collection, RawSprite &raw)
{
	raw.zoom = SelectDetailedZoom(collection);
	const SpriteLoader::Sprite &source = collection[raw.zoom];
	raw.width = source.width;
	raw.height = source.height;
	raw.x_offs = source.x_offs;
	raw.y_offs = source.y_offs;
	raw.colours = source.colours;

	size_t count = static_cast<size_t>(source.width) * source.height;
	if (source.data == nullptr) count = 0;
	raw.pixels.assign(source.data, source.data + count);
}

/**
 * Apply a palette and a remap to a sprite.
 *
 * This produces the colours the 32bpp blitters put on the screen when they
 * draw the sprite over a background: pixels with a palette index take
 * their colour from the palette, after the remap, with the brightness of
 * their RGB value (Blitter_32bppOptimized::Encode() and Draw()).
 * A remap to index 0 hides the pixel, as in the blitters.
 * @param raw The sprite.
 * @param palette The palette, usually a copy of _cur_palette.
 * @param remap The remap to apply.
 * @param[out] image Receives the image.
 */
void ApplyPalette(const RawSprite &raw, std::span<const Colour, 256> palette, const SpriteRemap &remap, SpriteImage &image)
{
	static_cast<SpriteGeometry &>(image) = raw;
	image.pixels.resize(raw.pixels.size());

	for (size_t i = 0; i < raw.pixels.size(); i++) {
		const SpriteLoader::CommonPixel &p = raw.pixels[i];
		Rgba8 &out = image.pixels[i];
		out = {};
		if (p.a == 0) continue;

		if (remap.mode == SpriteRemapMode::Black) {
			out = {0, 0, 0, 255};
			continue;
		}

		if (p.m == 0) {
			if (remap.mode == SpriteRemapMode::Crash) {
				uint8_t grey = Blitter_32bppBase::MakeDark(p.r, p.g, p.b);
				out = {grey, grey, grey, p.a};
			} else {
				out = {p.r, p.g, p.b, p.a};
			}
			continue;
		}

		uint8_t index = remap.mode == SpriteRemapMode::None ? p.m : remap.table[p.m];
		if (index == 0) continue;

		/* 8bpp pixels have no RGB and so keep the brightness of the palette. */
		uint8_t brightness = std::max({p.r, p.g, p.b});
		if (brightness == 0) brightness = DEFAULT_BRIGHTNESS;
		Colour colour = AdjustBrightness(palette[index], brightness);
		out = {colour.r, colour.g, colour.b, p.a};
	}
}

/**
 * Draw images on top of each other, like the 2D view draws the layers of a
 * vehicle: all share the same anchor, later ones cover earlier ones.
 * Layers of coarser zoom levels are scaled up to the most detailed one.
 * @param layers The images, bottom first.
 * @param[out] image Receives the combined image.
 */
void ComposeImages(std::span<const SpriteImage *const> layers, SpriteImage &image)
{
	image = {};
	if (layers.empty()) return;

	ZoomLevel zoom = ZoomLevel::Normal;
	for (const SpriteImage *layer : layers) zoom = std::min(zoom, layer->zoom);
	const int unit = ScaleByZoom(1, zoom);

	int left = INT_MAX, top = INT_MAX, right = INT_MIN, bottom = INT_MIN;
	for (const SpriteImage *layer : layers) {
		int scale = layer->Scale() / unit;
		left = std::min(left, layer->x_offs * scale);
		top = std::min(top, layer->y_offs * scale);
		right = std::max(right, (layer->x_offs + layer->width) * scale);
		bottom = std::max(bottom, (layer->y_offs + layer->height) * scale);
	}

	image.zoom = zoom;
	image.x_offs = static_cast<int16_t>(left);
	image.y_offs = static_cast<int16_t>(top);
	image.width = static_cast<uint16_t>(right - left);
	image.height = static_cast<uint16_t>(bottom - top);
	image.pixels.assign(static_cast<size_t>(image.width) * image.height, Rgba8{});

	for (const SpriteImage *layer : layers) {
		int scale = layer->Scale() / unit;
		for (int y = 0; y < layer->height * scale; y++) {
			for (int x = 0; x < layer->width * scale; x++) {
				const Rgba8 &src = layer->At(x / scale, y / scale);
				if (src.a == 0) continue;

				Rgba8 &dst = image.pixels[static_cast<size_t>(layer->y_offs * scale + y - top) * image.width + (layer->x_offs * scale + x - left)];
				/* Source over destination, with straight alpha. */
				uint sa = src.a;
				uint da = dst.a * (255 - sa) / 255;
				uint a = sa + da;
				dst.r = static_cast<uint8_t>((src.r * sa + dst.r * da) / a);
				dst.g = static_cast<uint8_t>((src.g * sa + dst.g * da) / a);
				dst.b = static_cast<uint8_t>((src.b * sa + dst.b * da) / a);
				dst.a = static_cast<uint8_t>(a);
			}
		}
	}
}

} // namespace fp
