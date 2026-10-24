/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file fp_sprite_bridge.cpp Pixels of any sprite of the game, base set and NewGRF alike. */

#include "../../stdafx.h"

#include <atomic>

#include "fp_sprite_bridge.h"
#include "fp_sprite_decode.h"
#include "../fp_hooks.h"
#include "../../palette_func.h"
#include "../../spritecache.h"
#include "../../table/sprites.h"

#include "../../safeguards.h"

namespace fp {

/** Incremented whenever the game drops or reloads its sprites; may change on any thread. */
static std::atomic<uint32_t> _sprite_generation{1};

/** Sprite encoder that keeps the pixels as the sprite loader delivers them, instead of encoding them for a blitter. */
class CaptureEncoder : public SpriteEncoder {
public:
	/**
	 * Create the encoder.
	 * @param target Receives the pixels.
	 */
	explicit CaptureEncoder(RawSprite &target) : target(target) {}

	bool Is32BppSupported() override { return true; }

	Sprite *Encode(SpriteType, const SpriteLoader::SpriteCollection &sprite, SpriteAllocator &) override
	{
		CaptureSprite(sprite, this->target);
		return nullptr;
	}

private:
	RawSprite &target; ///< Receives the pixels.
};

/**
 * Bring a request into the form the caches use, the way DrawSprite() reads its arguments.
 * Sprites drawn transparently in the 2D view are decoded opaque, and text
 * colour remaps do not apply to sprites of the world.
 * @param[in,out] sprite The sprite, possibly with modifier bits; receives the bare sprite.
 * @param[in,out] palette The palette; receives the palette to apply.
 */
void NormaliseSpriteRequest(SpriteID &sprite, PaletteID &palette)
{
	if (HasBit(sprite, PALETTE_MODIFIER_TRANSPARENT) || HasBit(palette, PALETTE_TEXT_RECOLOUR)) palette = PAL_NONE;
	sprite = GB(sprite, 0, SPRITE_WIDTH);
	palette = GB(palette, 0, PALETTE_WIDTH);
}

/**
 * Find out how a palette changes a sprite, like GetBlitterMode() does for the 2D view.
 * @param palette The palette, normalised by NormaliseSpriteRequest().
 * @param[out] remap Receives the remap.
 * @return False iff the palette is not a recolour sprite.
 */
bool ResolveSpriteRemap(PaletteID palette, SpriteRemap &remap)
{
	remap = {};
	switch (palette) {
		case PAL_NONE: return true;
		case PALETTE_ALL_BLACK: remap.mode = SpriteRemapMode::Black; return true;
		case PALETTE_CRASH: remap.mode = SpriteRemapMode::Crash; break;
		default: remap.mode = SpriteRemapMode::Colour; break;
	}

	if (!SpriteExists(palette) || GetSpriteType(palette) != SpriteType::Recolour) return false;
	const uint8_t *table = GetNonSprite(palette, SpriteType::Recolour) + 1;
	std::copy_n(table, remap.table.size(), remap.table.begin());
	return true;
}

/**
 * The sprite bridge.
 * @return The only instance.
 */
/* static */ SpriteBridge &SpriteBridge::Get()
{
	static SpriteBridge instance;
	return instance;
}

/**
 * Check whether a sprite can be decoded with a palette.
 * @param sprite The sprite.
 * @param palette The palette.
 * @return Why it cannot be, or std::nullopt if it can.
 */
/* static */ std::optional<std::string> SpriteBridge::CheckRequest(SpriteID sprite, PaletteID palette)
{
	const SpriteID sprite_bits = SPRITE_MASK | (1U << PALETTE_MODIFIER_TRANSPARENT) | (1U << PALETTE_MODIFIER_COLOUR);
	if ((sprite & ~sprite_bits) != 0) return fmt::format("{} is not a sprite number", sprite);
	if ((palette & ~(PALETTE_MASK | (1U << PALETTE_TEXT_RECOLOUR))) != 0) return fmt::format("{} is not a palette number", palette);

	NormaliseSpriteRequest(sprite, palette);

	if (!SpriteExists(sprite)) return fmt::format("sprite {} does not exist; the highest sprite is {}", sprite, GetMaxSpriteID() - 1);
	switch (GetSpriteType(sprite)) {
		case SpriteType::Normal: break;
		case SpriteType::Recolour: return fmt::format("sprite {} is a recolour sprite, not an image", sprite);
		case SpriteType::Font: return fmt::format("sprite {} is a glyph of a font", sprite);
		default: return fmt::format("sprite {} is not an image", sprite);
	}

	SpriteRemap remap;
	if (!ResolveSpriteRemap(palette, remap)) return fmt::format("palette {} is not a recolour sprite", palette);
	return std::nullopt;
}

/**
 * Drop the caches if the game reloaded its sprites since they were filled.
 * The palette is taken anew at the same time; animated colours keep the
 * value they had at that moment.
 */
void SpriteBridge::Sync()
{
	uint32_t current = _sprite_generation.load(std::memory_order_acquire);
	if (current == this->generation && this->palette_valid) return;

	if (this->palette_valid) this->statistics.clears++;
	this->raw_cache.Clear();
	this->image_cache.Clear();
	/* The game loop animates the palette with the game state lock held, which we hold as well. */
	std::copy(std::begin(_cur_palette.palette), std::end(_cur_palette.palette), this->palette.begin());
	this->generation = current;
	this->palette_valid = true;
}

/**
 * Generation of the sprites; it changes whenever the game reloads them, and
 * with it the meaning of sprite numbers. Everything derived from sprites
 * has to be built again then.
 * @return The generation.
 */
uint32_t SpriteBridge::GetGeneration()
{
	this->Sync();
	return this->generation;
}

/** Drop all cached sprites and take the palette anew. */
void SpriteBridge::Clear()
{
	this->raw_cache.Clear();
	this->image_cache.Clear();
	this->palette_valid = false;
}

/**
 * A sprite as the sprite loader delivers it.
 * @param sprite The sprite, without modifier bits.
 * @return The sprite, or nullptr if it is no image; see #CheckRequest().
 */
std::shared_ptr<const RawSprite> SpriteBridge::GetRawSprite(SpriteID sprite)
{
	this->Sync();

	if (auto raw = this->raw_cache.Find(sprite); raw != nullptr) return raw;
	if (!SpriteExists(sprite) || GetSpriteType(sprite) != SpriteType::Normal) {
		this->statistics.invalid_requests++;
		return nullptr;
	}

	auto raw = std::make_shared<RawSprite>();
	CaptureEncoder encoder(*raw);
	UniquePtrSpriteAllocator allocator;
	::GetRawSprite(sprite, SpriteType::Normal, &allocator, &encoder);
	this->statistics.sprites_read++;

	this->raw_cache.Insert(sprite, raw, raw->MemorySize());
	return raw;
}

/**
 * A sprite with a palette applied, as the 32bpp blitters would draw it.
 * @param sprite The sprite; modifier bits are handled like DrawSprite() does.
 * @param palette The palette: PAL_NONE, a recolour sprite, PALETTE_CRASH or PALETTE_ALL_BLACK.
 * @return The image, or nullptr if the sprite or palette is invalid; see #CheckRequest().
 */
std::shared_ptr<const SpriteImage> SpriteBridge::GetImage(SpriteID sprite, PaletteID palette)
{
	this->Sync();
	this->statistics.requests++;

	NormaliseSpriteRequest(sprite, palette);
	uint64_t key = (static_cast<uint64_t>(sprite) << 32) | palette;
	if (auto image = this->image_cache.Find(key); image != nullptr) {
		this->statistics.image_hits++;
		return image;
	}

	SpriteRemap remap;
	if (!ResolveSpriteRemap(palette, remap)) {
		this->statistics.invalid_requests++;
		return nullptr;
	}

	auto raw = this->GetRawSprite(sprite);
	if (raw == nullptr) return nullptr;

	auto image = std::make_shared<SpriteImage>();
	ApplyPalette(*raw, this->palette, remap, *image);
	this->image_cache.Insert(key, image, image->MemorySize());
	return image;
}

/* Hook called by the sprite cache; see fp_hooks.h. */

void OnSpritesInvalidated()
{
	_sprite_generation.fetch_add(1, std::memory_order_release);
}

} // namespace fp
