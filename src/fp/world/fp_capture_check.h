/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file fp_capture_check.h Tools to inspect and verify captured tile sprites: listings, images and a pixel-exact self-check. */

#ifndef FP_CAPTURE_CHECK_H
#define FP_CAPTURE_CHECK_H

#include "../../zoom_type.h"
#include "fp_tile_sprites.h"

namespace fp {

/**
 * An off-screen image drawn with the blitter in use, the way screenshots are
 * made. It covers an area of the 2D view, in viewport coordinates.
 */
class CaptureCanvas {
public:
	CaptureCanvas(int left, int top, int right, int bottom, ZoomLevel zoom);

	void Draw(const std::function<void(const DrawPixelInfo &area)> &draw);
	size_t CountDifferences(const CaptureCanvas &other, int left, int top, int right, int bottom) const;
	bool SavePng(const std::string &path) const;

	/**
	 * Width of the image.
	 * @return The width, in pixels.
	 */
	int Width() const { return this->width; }

	/**
	 * Height of the image.
	 * @return The height, in pixels.
	 */
	int Height() const { return this->height; }

	/**
	 * Left edge of the area.
	 * @return The left edge, in viewport coordinates.
	 */
	int Left() const { return this->left; }

	/**
	 * Top edge of the area.
	 * @return The top edge, in viewport coordinates.
	 */
	int Top() const { return this->top; }

	/**
	 * Zoom level of the image.
	 * @return The zoom level.
	 */
	ZoomLevel Zoom() const { return this->zoom; }

private:
	int left; ///< Left edge of the area, in viewport coordinates.
	int top; ///< Top edge of the area, in viewport coordinates.
	int width; ///< Width of the image, in pixels.
	int height; ///< Height of the image, in pixels.
	ZoomLevel zoom; ///< Zoom level of the image.
	uint bytes_per_pixel; ///< Bytes per pixel of the blitter.
	std::vector<uint8_t> pixels; ///< The image, row by row.
};

/** Outcome of CheckTileCapture(). */
struct CaptureCheckResult {
	uint tiles = 0; ///< Number of tiles captured.
	size_t sprites = 0; ///< Number of sprites captured.
	double capture_ms = 0; ///< Time it took to capture the tiles, in milliseconds.
	int width = 0; ///< Width of the compared area of the landscape, in pixels.
	int height = 0; ///< Height of the compared area of the landscape, in pixels.
	size_t replay_differences = 0; ///< Pixels where the captured sprites, drawn again, differ from the 2D view.
	int view_width = 0; ///< Width of the main viewport, in pixels; 0 without one.
	int view_height = 0; ///< Height of the main viewport, in pixels.
	size_t view_differences = 0; ///< Pixels of the main viewport that are drawn differently after capturing than before.
	bool beyond_map = false; ///< Whether the area reaches beyond the map, where the 2D view draws black void.
	std::vector<std::string> files; ///< Images that were written.
};

/** Outcome of BenchmarkChunkCapture(); times are per chunk, in microseconds. */
struct CaptureBenchmark {
	uint chunks = 0; ///< Number of chunks captured per pass.
	size_t tiles = 0; ///< Number of tiles in these chunks.
	size_t sprites = 0; ///< Number of sprites in these chunks.
	double terrain_us = 0; ///< Capturing and hashing the terrain, as the World Mirror did before sprites.
	double first_sprites_us = 0; ///< Capturing the sprites the first time, when the sprite cache may still have to load them.
	double sprites_us = 0; ///< Capturing the sprites again.
	double max_sprites_us = 0; ///< Capturing the sprites again, the slowest chunk.
	double hash_us = 0; ///< Hashing the sprites.
	uint unstable_chunks = 0; ///< Chunks whose second capture differed from the first; must be 0.
};

std::vector<std::string> DescribeTileSprites(const TileSpriteCapture &capture, const TileSprites &tile);
CaptureBenchmark BenchmarkChunkCapture(uint chunk_x, uint chunk_y, uint radius);
CaptureCheckResult CheckTileCapture(TileIndex centre, uint radius, std::string_view name);
std::vector<std::string> SaveTileCaptureImages(TileIndex tile, std::string_view name);

} // namespace fp

#endif /* FP_CAPTURE_CHECK_H */
