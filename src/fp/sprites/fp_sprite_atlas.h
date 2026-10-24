/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file fp_sprite_atlas.h Atlas of sprite images for the GPU. */

#ifndef FP_SPRITE_ATLAS_H
#define FP_SPRITE_ATLAS_H

#include "../../gfx_type.h"
#include "../core/fp_metric.h"
#include "fp_sprite_image.h"

namespace fp {

static constexpr uint ATLAS_PAGE_SIZE = 2048; ///< Width and height of an atlas page, in texels; OpenGL 3.3 hardware supports far more.
static constexpr uint ATLAS_MAX_PAGES = 16; ///< Most pages the atlas uses before it starts over.
static constexpr uint ATLAS_MIP_LEVELS = 4; ///< Mipmap levels of a page, including the full one.
static constexpr uint ATLAS_ALIGN = 1U << (ATLAS_MIP_LEVELS - 1); ///< Tiles start and end on multiples of this, so no mipmap level mixes two tiles.
static constexpr uint ATLAS_GUTTER = 4; ///< Border around each image that repeats its edge, so filtering never reaches a neighbour.

/** Where a sprite image lies in the atlas. */
struct AtlasRegion : SpriteGeometry {
	uint16_t page = 0; ///< Atlas page.
	uint16_t x = 0; ///< Column of the left pixel of the image on the page, in texels.
	uint16_t y = 0; ///< Row of the top pixel of the image on the page, in texels.

	/**
	 * Texture coordinates of the image: left, top, right and bottom edge.
	 * Rows count from the top, as they are uploaded.
	 * @return The coordinates, from 0 to 1.
	 */
	std::array<float, 4> GetUV() const
	{
		constexpr float scale = 1.0f / ATLAS_PAGE_SIZE;
		return {this->x * scale, this->y * scale, (this->x + this->width) * scale, (this->y + this->height) * scale};
	}
};

/**
 * Pixels for a rectangle of an atlas page: an image with its gutter, at
 * every mipmap level. Its position and size are multiples of #ATLAS_ALIGN.
 */
struct AtlasUpload {
	uint16_t page = 0; ///< Atlas page.
	uint16_t x = 0; ///< Left column on the page, in texels of level 0.
	uint16_t y = 0; ///< Top row on the page, in texels of level 0.
	uint16_t width = 0; ///< Width, in texels of level 0.
	uint16_t height = 0; ///< Height, in texels of level 0.
	std::vector<Rgba8> pixels; ///< All levels, level 0 first; each level row by row from the top.
};

/** Changes of the atlas for the renderer, to apply in order. */
struct AtlasChanges {
	bool reset = false; ///< Whether everything on the pages became unused before #uploads.
	uint pages = 0; ///< Pages in use after the changes.
	std::vector<AtlasUpload> uploads; ///< Rectangles to write.

	void Merge(AtlasChanges &&later);
};

/**
 * Shelf packer for the pages of the atlas.
 *
 * Rectangles go side by side into horizontal shelves; a shelf takes the
 * height of the rectangle that opened it. A rectangle goes into the
 * lowest shelf it fits in, unless that would waste much of the shelf and a
 * new shelf can be opened. Sprites come in few different heights, so
 * shelves pack them tightly.
 */
class AtlasPacker {
public:
	/** Where a rectangle went. */
	struct Slot {
		uint page; ///< The page.
		uint x; ///< Left column.
		uint y; ///< Top row.
	};

	std::optional<Slot> Allocate(uint width, uint height);
	void Reset();

	/**
	 * Pages with anything on them.
	 * @return Number of pages.
	 */
	uint GetPageCount() const { return static_cast<uint>(this->page_tops.size()); }

	/**
	 * Area of all allocated rectangles.
	 * @return Area, in texels.
	 */
	uint64_t GetUsedArea() const { return this->used_area; }

private:
	/** A row of rectangles. */
	struct Shelf {
		uint page; ///< The page it is on.
		uint y; ///< Top row.
		uint height; ///< Height.
		uint used; ///< Width taken by rectangles so far.
	};

	std::vector<Shelf> shelves; ///< All shelves.
	std::vector<uint> page_tops; ///< Per page: the first row no shelf covers.
	uint64_t used_area = 0; ///< Area of all allocated rectangles.

	std::optional<Slot> OpenShelf(uint width, uint height);
};

void BuildAtlasTile(const SpriteImage &image, AtlasUpload &upload);
double SpritePixelLength(const MetricScale &scale, ZoomLevel zoom);

/**
 * The atlas of sprite images on the runtime side.
 *
 * It decides where each image goes and prepares its pixels; the renderer
 * only copies them to the GPU, see #TakeChanges(). Images stay until the
 * atlas is full or the game reloads its sprites; then the atlas starts
 * over and its epoch changes, and every region taken before is void.
 *
 * Must be used with the game state lock held.
 */
class SpriteAtlas {
public:
	/** Counters of the atlas. */
	struct Statistics {
		uint images = 0; ///< Images in the atlas.
		uint pages = 0; ///< Pages in use.
		uint64_t used_area = 0; ///< Area taken by tiles, in texels.
		uint32_t epoch = 0; ///< Times the atlas started over.
		uint64_t uploaded_bytes = 0; ///< Pixel data sent to the renderer, in bytes.
	};

	std::optional<AtlasRegion> Request(SpriteID sprite, PaletteID palette);
	void Reset();
	void TakeChanges(AtlasChanges &changes);
	Statistics GetStatistics() const;

	/**
	 * Number of the current filling of the atlas; regions of earlier epochs are void.
	 * @return The epoch.
	 */
	uint32_t GetEpoch() const { return this->epoch; }

private:
	AtlasPacker packer; ///< Layout of the pages.
	std::unordered_map<uint64_t, std::optional<AtlasRegion>> regions; ///< Region of each sprite and palette; std::nullopt if it cannot be shown.
	AtlasChanges pending{}; ///< Changes not yet taken by the renderer.
	uint32_t generation = 0; ///< Sprite generation the atlas was filled with.
	uint32_t epoch = 0; ///< Number of the current filling.
	uint64_t uploaded_bytes = 0; ///< Pixel data sent to the renderer, in bytes.
};

} // namespace fp

#endif /* FP_SPRITE_ATLAS_H */
