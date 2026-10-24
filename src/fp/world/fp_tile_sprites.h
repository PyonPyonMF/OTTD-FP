/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file fp_tile_sprites.h The sprites of tiles as the 2D view draws them, recorded from the tile drawing procedures. */

#ifndef FP_TILE_SPRITES_H
#define FP_TILE_SPRITES_H

#include "../../core/enum_type.hpp"
#include "../../core/geometry_type.hpp"
#include "../../gfx_type.h"
#include "../../slope_type.h"
#include "../../tile_type.h"

namespace fp {

/**
 * A sprite as the 2D view draws it.
 *
 * Positions are in pixels of the 2D view at ZoomLevel::Min, i.e. in the
 * units of RemapCoords(), relative to the reference point of the tile:
 * RemapCoords() of TileSprites::origin. The 2D view puts the origin of the
 * sprite there; the offsets stored in the sprite itself apply on top.
 */
struct CapturedSprite {
	SpriteID image = 0; ///< The sprite, including modifier bits such as recolouring.
	PaletteID pal = 0; ///< The palette or recolour sprite to draw with, or PAL_NONE.
	std::optional<SubSprite> sub; ///< Part of the sprite to draw; the whole sprite if empty.
	Point offset{}; ///< Position of the origin of the sprite, relative to the reference point of the tile.
};

/**
 * A sprite with a bounding box in the world. The 2D view sorts these by
 * their boxes and draws their child sprites right after them.
 *
 * Boxes are in world units relative to TileSprites::origin and span
 * [bb_min, bb_max): a side of length zero is a thin slice.
 */
struct CapturedParentSprite {
	CapturedSprite sprite; ///< The sprite; SPR_EMPTY_BOUNDING_BOX for a box that only takes part in sorting.
	Coord3D<int> bb_min{}; ///< Lowest corner of the bounding box.
	Coord3D<int> bb_max{}; ///< Highest corner of the bounding box, exclusive.
	uint32_t first_child = 0; ///< Index of the first child sprite in TileSpriteCapture::children.
	uint32_t child_count = 0; ///< Number of child sprites; they are drawn after this sprite, in this order.
};

/** Parts of a tile foundation that ground sprites can be drawn on. */
enum class CapturedFoundation : uint8_t {
	Normal, ///< Normal foundation, or the first part of a steep one.
	Halftile, ///< Half-tile foundation on top.
	End, ///< End marker.
};

/**
 * The sprites of one tile. The index ranges refer to the lists of the
 * TileSpriteCapture that holds the tile.
 */
struct TileSprites {
	static constexpr uint32_t NO_FOUNDATION = UINT32_MAX; ///< Value of #foundations for parts that do not exist.

	TileIndex tile = INVALID_TILE; ///< The tile.
	Coord3D<int> origin{}; ///< North corner of the tile at its base height, in world units: the reference point of all positions.
	Slope slope = SLOPE_FLAT; ///< Slope of the tile.
	int ground_z = 0; ///< Base height of the surface the ground sprites lie on, in world units; above origin.z on foundations.
	Slope ground_slope = SLOPE_FLAT; ///< Slope of the surface the ground sprites lie on.
	uint32_t first_ground = 0; ///< Index of the first ground sprite in TileSpriteCapture::ground.
	uint32_t ground_count = 0; ///< Number of ground sprites.
	uint32_t first_parent = 0; ///< Index of the first parent sprite in TileSpriteCapture::parents.
	uint32_t parent_count = 0; ///< Number of parent sprites.

	/**
	 * Per part of the foundation: index in TileSpriteCapture::parents of the
	 * foundation sprite, or #NO_FOUNDATION. Once a tile drew a foundation,
	 * its ground sprites are children of the foundation sprite instead of
	 * being in the ground list, so they are sorted together with it.
	 */
	std::array<uint32_t, to_underlying(CapturedFoundation::End)> foundations{NO_FOUNDATION, NO_FOUNDATION};
};

/**
 * The sprites of any number of tiles, as their drawing procedures produce
 * them for the 2D view. That is the same path for the base set and for
 * NewGRFs, including callbacks, animation and recolouring.
 *
 * All tiles share three flat lists, so capturing a whole chunk does not
 * allocate per tile. Within a tile the order is the drawing order: ground
 * sprites first, then the parent sprites (which the 2D view sorts by their
 * bounding boxes), each followed by its children.
 */
struct TileSpriteCapture {
	std::vector<TileSprites> tiles; ///< The tiles, in the order they were captured.
	std::vector<CapturedSprite> ground; ///< Sprites drawn flat on the ground, before everything else.
	std::vector<CapturedParentSprite> parents; ///< Sprites with a bounding box.
	std::vector<CapturedSprite> children; ///< Sprites drawn relative to a parent sprite.

	void Clear();
	std::span<const CapturedSprite> Ground(const TileSprites &tile) const;
	std::span<const CapturedParentSprite> Parents(const TileSprites &tile) const;
	std::span<const CapturedSprite> Children(const CapturedParentSprite &parent) const;

	/**
	 * Total number of sprites.
	 * @return Number of ground, parent and child sprites together.
	 */
	size_t SpriteCount() const { return this->ground.size() + this->parents.size() + this->children.size(); }
};

uint64_t HashTileSprites(const TileSpriteCapture &capture);
bool operator==(const CapturedSprite &a, const CapturedSprite &b);
bool operator==(const CapturedParentSprite &a, const CapturedParentSprite &b);

} // namespace fp

#endif /* FP_TILE_SPRITES_H */
