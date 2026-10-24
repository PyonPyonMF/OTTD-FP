/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file fp_sprite_bridge.h Pixels of any sprite of the game, base set and NewGRF alike. */

#ifndef FP_SPRITE_BRIDGE_H
#define FP_SPRITE_BRIDGE_H

#include "../../gfx_type.h"
#include "../core/fp_lru_cache.h"
#include "fp_sprite_image.h"

namespace fp {

/**
 * The sprite bridge: decoded pixels of any sprite, by (SpriteID, PaletteID).
 *
 * The pixels come from the sprite cache of OpenTTD, which holds the base
 * set and every NewGRF, through the same path as the sprites of the 2D view:
 * GetRawSprite() with an encoder of our own instead of the blitter. So
 * whatever the 2D view can draw, the bridge can decode, and recolouring
 * works like in the blitters.
 *
 * Decoding happens in two steps, each with its own cache: reading the
 * sprite, which is the expensive part, and applying a palette, which is
 * cheap. So a vehicle in sixteen company colours is read only once.
 *
 * Everything is dropped when the game reloads its sprites, e.g. for other
 * NewGRFs or another base set; #GetGeneration() tells when that happened.
 *
 * All members must be used with the game state lock held: the sprite cache
 * and the recolour sprites belong to the game.
 */
class SpriteBridge {
public:
	static constexpr size_t RAW_CACHE_BUDGET = 32 << 20; ///< Memory for sprites as read, in bytes.
	static constexpr size_t IMAGE_CACHE_BUDGET = 64 << 20; ///< Memory for sprites with a palette applied, in bytes.

	/** Counters of the work done. */
	struct Statistics {
		uint64_t requests = 0; ///< Images asked for.
		uint64_t image_hits = 0; ///< Images that were in the cache.
		uint64_t sprites_read = 0; ///< Sprites read through the sprite cache.
		uint64_t invalid_requests = 0; ///< Requests for sprites or palettes that do not exist.
		uint32_t clears = 0; ///< Times all caches were dropped because the game reloaded its sprites.
	};

	static SpriteBridge &Get();

	static std::optional<std::string> CheckRequest(SpriteID sprite, PaletteID palette);

	std::shared_ptr<const SpriteImage> GetImage(SpriteID sprite, PaletteID palette);
	std::shared_ptr<const RawSprite> GetRawSprite(SpriteID sprite);
	uint32_t GetGeneration();
	void Clear();

	/**
	 * Counters of the work done.
	 * @return The counters.
	 */
	const Statistics &GetStatistics() const { return this->statistics; }

	/**
	 * The cache of sprites as read.
	 * @return The cache.
	 */
	const LruCache<SpriteID, RawSprite> &GetRawCache() const { return this->raw_cache; }

	/**
	 * The cache of sprites with a palette applied.
	 * @return The cache.
	 */
	const LruCache<uint64_t, SpriteImage> &GetImageCache() const { return this->image_cache; }

private:
	LruCache<SpriteID, RawSprite> raw_cache{RAW_CACHE_BUDGET}; ///< Sprites as read, by sprite.
	LruCache<uint64_t, SpriteImage> image_cache{IMAGE_CACHE_BUDGET}; ///< Sprites with a palette applied, by sprite and palette.
	std::array<Colour, 256> palette{}; ///< The palette the images use; taken when the caches were last dropped.
	uint32_t generation = 0; ///< Sprite generation the caches belong to.
	bool palette_valid = false; ///< Whether #palette was taken.
	Statistics statistics{}; ///< Counters of the work done.

	void Sync();
};

void NormaliseSpriteRequest(SpriteID &sprite, PaletteID &palette);
bool ResolveSpriteRemap(PaletteID palette, SpriteRemap &remap);

} // namespace fp

#endif /* FP_SPRITE_BRIDGE_H */
