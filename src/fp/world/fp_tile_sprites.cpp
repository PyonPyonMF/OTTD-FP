/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file fp_tile_sprites.cpp The sprites of tiles as the 2D view draws them, recorded from the tile drawing procedures. */

#include "../../stdafx.h"
#include "fp_tile_sprites.h"

#include "../../safeguards.h"

namespace fp {

/** Forget all tiles and sprites, keeping the allocated memory. */
void TileSpriteCapture::Clear()
{
	this->tiles.clear();
	this->ground.clear();
	this->parents.clear();
	this->children.clear();
}

/**
 * The ground sprites of a tile.
 * @param tile A tile of this capture.
 * @return Its ground sprites, in drawing order.
 */
std::span<const CapturedSprite> TileSpriteCapture::Ground(const TileSprites &tile) const
{
	return std::span(this->ground).subspan(tile.first_ground, tile.ground_count);
}

/**
 * The parent sprites of a tile.
 * @param tile A tile of this capture.
 * @return Its parent sprites, in the order the tile added them.
 */
std::span<const CapturedParentSprite> TileSpriteCapture::Parents(const TileSprites &tile) const
{
	return std::span(this->parents).subspan(tile.first_parent, tile.parent_count);
}

/**
 * The child sprites of a parent sprite.
 * @param parent A parent sprite of this capture.
 * @return Its child sprites, in drawing order.
 */
std::span<const CapturedSprite> TileSpriteCapture::Children(const CapturedParentSprite &parent) const
{
	return std::span(this->children).subspan(parent.first_child, parent.child_count);
}

/**
 * Compare two captured sprites.
 * @param a The first sprite.
 * @param b The second sprite.
 * @return True iff both draw the same image in the same way at the same place.
 */
bool operator==(const CapturedSprite &a, const CapturedSprite &b)
{
	if (a.image != b.image || a.pal != b.pal || a.offset != b.offset || a.sub.has_value() != b.sub.has_value()) return false;
	if (!a.sub.has_value()) return true;
	return a.sub->left == b.sub->left && a.sub->top == b.sub->top && a.sub->right == b.sub->right && a.sub->bottom == b.sub->bottom;
}

/**
 * Compare two captured parent sprites.
 * @param a The first sprite.
 * @param b The second sprite.
 * @return True iff the sprites, their boxes and their child ranges are equal.
 */
bool operator==(const CapturedParentSprite &a, const CapturedParentSprite &b)
{
	return a.sprite == b.sprite && a.bb_min == b.bb_min && a.bb_max == b.bb_max && a.first_child == b.first_child && a.child_count == b.child_count;
}

/** Incremental 64-bit FNV-1a over 32-bit words; any single changed word changes the result. */
class WordHasher {
public:
	/**
	 * Add a value.
	 * @param value The value.
	 */
	void Add(uint32_t value)
	{
		this->hash ^= value;
		this->hash *= 0x100000001B3ULL;
	}

	/**
	 * Add a signed value.
	 * @param value The value.
	 */
	void Add(int value) { this->Add(static_cast<uint32_t>(value)); }

	/**
	 * Add a sprite.
	 * @param sprite The sprite.
	 */
	void Add(const CapturedSprite &sprite)
	{
		this->Add(sprite.image);
		this->Add(sprite.pal);
		this->Add(sprite.offset.x);
		this->Add(sprite.offset.y);
		this->Add(static_cast<uint32_t>(sprite.sub.has_value()));
		if (sprite.sub.has_value()) {
			this->Add(sprite.sub->left);
			this->Add(sprite.sub->top);
			this->Add(sprite.sub->right);
			this->Add(sprite.sub->bottom);
		}
	}

	/**
	 * The hash of everything added so far.
	 * @return The hash.
	 */
	uint64_t Get() const { return this->hash; }

private:
	uint64_t hash = 0xCBF29CE484222325ULL; ///< The running hash.
};

/**
 * Fingerprint of a capture: everything the 2D view would draw differently.
 * Animation, a new graphics state of a NewGRF or a changed palette change it;
 * a tile that was only marked dirty does not.
 * @param capture The capture.
 * @return 64-bit hash of all tiles and sprites.
 */
uint64_t HashTileSprites(const TileSpriteCapture &capture)
{
	WordHasher hasher;
	for (const TileSprites &tile : capture.tiles) {
		hasher.Add(tile.tile.base());
		hasher.Add(tile.origin.x);
		hasher.Add(tile.origin.y);
		hasher.Add(tile.origin.z);
		hasher.Add(static_cast<uint32_t>(tile.slope));
		hasher.Add(tile.ground_z);
		hasher.Add(static_cast<uint32_t>(tile.ground_slope));
		hasher.Add(tile.ground_count);
		hasher.Add(tile.parent_count);
		for (uint32_t foundation : tile.foundations) hasher.Add(foundation == TileSprites::NO_FOUNDATION ? foundation : foundation - tile.first_parent);

		for (const CapturedSprite &sprite : capture.Ground(tile)) hasher.Add(sprite);
		for (const CapturedParentSprite &parent : capture.Parents(tile)) {
			hasher.Add(parent.sprite);
			hasher.Add(parent.bb_min.x);
			hasher.Add(parent.bb_min.y);
			hasher.Add(parent.bb_min.z);
			hasher.Add(parent.bb_max.x);
			hasher.Add(parent.bb_max.y);
			hasher.Add(parent.bb_max.z);
			hasher.Add(parent.child_count);
			for (const CapturedSprite &child : capture.Children(parent)) hasher.Add(child);
		}
	}
	return hasher.Get();
}

} // namespace fp
