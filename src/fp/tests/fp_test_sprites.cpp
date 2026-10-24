/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file fp_test_sprites.cpp Tests of the sprite decoding of the first-person mode. */

#include "../../stdafx.h"

#include "../../3rdparty/catch2/catch.hpp"

#include "../sprites/fp_sprite_decode.h"
#include "../../blitter/32bpp_optimized.hpp"
#include "../../blitter/32bpp_simple.hpp"
#include "../../palette_func.h"
#include "../../spritecache.h"

#include "../../safeguards.h"

using namespace fp;

/** Small deterministic random number generator, so failures can be reproduced. */
struct TestRandom {
	uint32_t state; ///< State of the generator.

	/**
	 * Next number.
	 * @param limit Upper bound, exclusive.
	 * @return A number from 0 to \a limit - 1.
	 */
	uint32_t Next(uint32_t limit)
	{
		this->state = this->state * 1664525u + 1013904223u;
		return (this->state >> 8) % limit;
	}
};

/** A sprite for tests: the same pixels at every zoom level, like Blitter_32bppOptimized::Encode() expects. */
struct TestSprite {
	std::vector<SpriteLoader::CommonPixel> pixels; ///< The pixels.
	SpriteLoader::SpriteCollection collection{}; ///< The sprite, pointing to #pixels.

	/**
	 * Point all zoom levels to the pixels.
	 * @param width Width.
	 * @param height Height.
	 */
	void Finish(uint16_t width, uint16_t height)
	{
		for (ZoomLevel zoom = ZoomLevel::Begin; zoom != ZoomLevel::End; zoom++) {
			SpriteLoader::Sprite &sprite = this->collection[zoom];
			sprite.width = width;
			sprite.height = height;
			sprite.x_offs = -3;
			sprite.y_offs = -5;
			sprite.colours = {SpriteComponent::RGB, SpriteComponent::Alpha, SpriteComponent::Palette};
			sprite.data = this->pixels.data();
		}
	}
};

/**
 * A sprite with every kind of pixel the sprite loader produces: transparent,
 * 8bpp (palette index without RGB), 32bpp opaque and translucent, and 32bpp
 * with recolour mask, opaque and translucent.
 * @param random The random numbers.
 * @param[out] sprite Receives the sprite.
 */
static void MakeMixedSprite(TestRandom &random, TestSprite &sprite)
{
	const uint16_t width = 37;
	const uint16_t height = 23;
	sprite.pixels.resize(width * height);
	for (SpriteLoader::CommonPixel &p : sprite.pixels) {
		p = {};
		switch (random.Next(6)) {
			case 0: break; // Transparent.
			case 1: p.a = 255; p.m = static_cast<uint8_t>(1 + random.Next(255)); break; // 8bpp.
			case 2: p.r = random.Next(256); p.g = random.Next(256); p.b = random.Next(256); p.a = 255; break;
			case 3: p.r = random.Next(256); p.g = random.Next(256); p.b = random.Next(256); p.a = static_cast<uint8_t>(1 + random.Next(254)); break;
			case 4: p.r = random.Next(256); p.g = random.Next(256); p.b = random.Next(256); p.a = 255; p.m = static_cast<uint8_t>(1 + random.Next(255)); break;
			case 5: p.r = random.Next(256); p.g = random.Next(256); p.b = random.Next(256); p.a = static_cast<uint8_t>(1 + random.Next(254)); p.m = static_cast<uint8_t>(1 + random.Next(255)); break;
		}
	}
	sprite.Finish(width, height);
}

/**
 * Draw an image over a background the way the 32bpp blitters compose pixels.
 * @param image The image.
 * @param background The background colour.
 * @return The resulting pixels.
 */
static std::vector<Colour> ComposeOver(const SpriteImage &image, Colour background)
{
	std::vector<Colour> result(image.pixels.size(), background);
	for (size_t i = 0; i < image.pixels.size(); i++) {
		const Rgba8 &p = image.pixels[i];
		result[i] = Blitter_32bppBase::ComposeColourRGBA(p.r, p.g, p.b, p.a, background);
	}
	return result;
}

/**
 * Draw a sprite with a blitter over a background.
 * @param blitter The blitter.
 * @param sprite The sprite.
 * @param mode How to draw.
 * @param remap The remap table.
 * @param background The background colour.
 * @return The resulting pixels.
 */
static std::vector<Colour> DrawWithBlitter(Blitter &blitter, const TestSprite &sprite, BlitterMode mode, const std::array<uint8_t, 256> &remap, Colour background)
{
	const SpriteLoader::Sprite &root = sprite.collection.Root();
	UniquePtrSpriteAllocator allocator;
	Sprite *encoded = blitter.Encode(SpriteType::Normal, sprite.collection, allocator);

	std::vector<Colour> result(static_cast<size_t>(root.width) * root.height, background);
	Blitter::BlitterParams bp{};
	bp.sprite = encoded->data;
	bp.remap = remap.data();
	bp.width = root.width;
	bp.height = root.height;
	bp.sprite_width = root.width;
	bp.sprite_height = root.height;
	bp.dst = result.data();
	bp.pitch = root.width;
	blitter.Draw(&bp, mode, ZoomLevel::Min);
	return result;
}

TEST_CASE("Sprite decode - colours match the 32bpp blitters for every remap mode")
{
	TestRandom random{12345};

	/* The blitters look colours up in the current palette. */
	const Palette saved_palette = _cur_palette;
	for (uint i = 0; i < 256; i++) _cur_palette.palette[i] = Colour(random.Next(256), random.Next(256), random.Next(256));
	_cur_palette.palette[0] = Colour(0, 0, 0, 0);
	std::span<const Colour, 256> palette(_cur_palette.palette);

	std::array<uint8_t, 256> table{};
	for (uint i = 1; i < table.size(); i++) table[i] = random.Next(8) == 0 ? 0 : static_cast<uint8_t>(1 + random.Next(255));

	TestSprite sprite;
	MakeMixedSprite(random, sprite);
	RawSprite raw;
	CaptureSprite(sprite.collection, raw);
	REQUIRE(raw.zoom == ZoomLevel::Min);

	const Colour background(17, 130, 210);
	const std::array<std::pair<BlitterMode, SpriteRemapMode>, 4> modes = {{
		{BlitterMode::Normal, SpriteRemapMode::None},
		{BlitterMode::ColourRemap, SpriteRemapMode::Colour},
		{BlitterMode::CrashRemap, SpriteRemapMode::Crash},
		{BlitterMode::BlackRemap, SpriteRemapMode::Black},
	}};

	Blitter_32bppOptimized optimized;
	Blitter_32bppSimple simple;
	for (Blitter *blitter : std::initializer_list<Blitter *>{&optimized, &simple}) {
		for (auto [blitter_mode, remap_mode] : modes) {
			CAPTURE(std::string(blitter->GetName()), static_cast<int>(remap_mode));

			SpriteRemap remap{remap_mode, table};
			SpriteImage image;
			ApplyPalette(raw, palette, remap, image);
			std::vector<Colour> ours = ComposeOver(image, background);
			std::vector<Colour> theirs = DrawWithBlitter(*blitter, sprite, blitter_mode, table, background);

			size_t mismatches = 0;
			for (size_t i = 0; i < ours.size(); i++) {
				if (ours[i].r == theirs[i].r && ours[i].g == theirs[i].g && ours[i].b == theirs[i].b) continue;
				if (mismatches++ == 0) {
					const SpriteLoader::CommonPixel &p = sprite.pixels[i];
					UNSCOPED_INFO("first mismatch at " << i << ": pixel rgbam " << +p.r << " " << +p.g << " " << +p.b << " " << +p.a << " " << +p.m
							<< ", ours " << +ours[i].r << " " << +ours[i].g << " " << +ours[i].b
							<< ", blitter " << +theirs[i].r << " " << +theirs[i].g << " " << +theirs[i].b);
				}
			}
			CHECK(mismatches == 0);
		}
	}

	_cur_palette = saved_palette;
}

TEST_CASE("Sprite decode - remapping to index 0 hides a pixel, 8bpp pixels keep the palette brightness")
{
	std::array<Colour, 256> palette{};
	palette[7] = Colour(10, 20, 30);
	palette[9] = Colour(200, 100, 50);

	RawSprite raw;
	raw.width = 3;
	raw.height = 1;
	raw.pixels.resize(3);
	raw.pixels[0] = {0, 0, 0, 255, 7}; // 8bpp, index 7.
	raw.pixels[1] = {0, 0, 0, 255, 9}; // 8bpp, index 9.
	raw.pixels[2] = {1, 2, 3, 128, 0}; // 32bpp, half transparent.

	SpriteImage image;
	ApplyPalette(raw, palette, {}, image);
	CHECK(image.pixels[0] == Rgba8{10, 20, 30, 255});
	CHECK(image.pixels[1] == Rgba8{200, 100, 50, 255});
	CHECK(image.pixels[2] == Rgba8{1, 2, 3, 128});

	SpriteRemap remap;
	remap.mode = SpriteRemapMode::Colour;
	remap.table[7] = 9;
	remap.table[9] = 0;
	ApplyPalette(raw, palette, remap, image);
	CHECK(image.pixels[0] == Rgba8{200, 100, 50, 255});
	CHECK(image.pixels[1].a == 0);
	CHECK(image.pixels[2] == Rgba8{1, 2, 3, 128});
}

/**
 * Fill the levels of a test sprite the way the sprite cache does: the
 * levels more detailed than \a native double its pixels.
 * @param random The random numbers.
 * @param native The zoom level the sprite really has.
 * @param[out] storage Receives the pixels of every level.
 * @param[out] collection Receives the sprite.
 */
static void MakeUpscaledSprite(TestRandom &random, ZoomLevel native, std::array<std::vector<SpriteLoader::CommonPixel>, 3> &storage, SpriteLoader::SpriteCollection &collection)
{
	SpriteLoader::Sprite &base = collection[native];
	base.width = 9;
	base.height = 5;
	base.x_offs = -4;
	base.y_offs = -2;
	std::vector<SpriteLoader::CommonPixel> &pixels = storage[to_underlying(native)];
	pixels.resize(base.width * base.height);
	for (SpriteLoader::CommonPixel &p : pixels) p = {0, 0, 0, 255, static_cast<uint8_t>(1 + random.Next(255))};
	base.data = pixels.data();

	for (ZoomLevel zoom = native; zoom > ZoomLevel::Min;) {
		const SpriteLoader::Sprite &coarse = collection[zoom];
		--zoom;
		SpriteLoader::Sprite &fine = collection[zoom];
		fine.width = coarse.width * 2;
		fine.height = coarse.height * 2;
		fine.x_offs = coarse.x_offs * 2;
		fine.y_offs = coarse.y_offs * 2;
		std::vector<SpriteLoader::CommonPixel> &fine_pixels = storage[to_underlying(zoom)];
		fine_pixels.resize(fine.width * fine.height);
		for (uint y = 0; y < fine.height; y++) {
			for (uint x = 0; x < fine.width; x++) fine_pixels[y * fine.width + x] = coarse.data[(y / 2) * coarse.width + x / 2];
		}
		fine.data = fine_pixels.data();
	}
}

TEST_CASE("Sprite decode - the most detailed zoom level with information of its own is taken")
{
	TestRandom random{777};
	for (ZoomLevel native : {ZoomLevel::Min, ZoomLevel::In2x, ZoomLevel::Normal}) {
		CAPTURE(static_cast<int>(native));
		std::array<std::vector<SpriteLoader::CommonPixel>, 3> storage;
		SpriteLoader::SpriteCollection collection{};
		MakeUpscaledSprite(random, native, storage, collection);
		CHECK(SelectDetailedZoom(collection) == native);

		RawSprite raw;
		CaptureSprite(collection, raw);
		CHECK(raw.zoom == native);
		CHECK(raw.width == 9);
		CHECK(raw.x_offs == -4);
		CHECK(raw.Scale() == ScaleByZoom(1, native));
	}

	/* A single pixel of detail at 4x is enough to keep the 4x level. */
	std::array<std::vector<SpriteLoader::CommonPixel>, 3> storage;
	SpriteLoader::SpriteCollection collection{};
	MakeUpscaledSprite(random, ZoomLevel::Normal, storage, collection);
	storage[0][5].m ^= 1;
	CHECK(SelectDetailedZoom(collection) == ZoomLevel::Min);
}

TEST_CASE("Sprite decode - layers combine like the 2D view draws them")
{
	SpriteImage bottom;
	bottom.width = 2;
	bottom.height = 2;
	bottom.x_offs = 0;
	bottom.y_offs = 0;
	bottom.zoom = ZoomLevel::Normal;
	bottom.pixels.assign(4, Rgba8{255, 0, 0, 255});

	SpriteImage top;
	top.width = 2;
	top.height = 1;
	top.x_offs = 2;
	top.y_offs = 1;
	top.zoom = ZoomLevel::Min;
	top.pixels = {Rgba8{0, 0, 255, 255}, Rgba8{0, 0, 0, 0}};

	std::array<const SpriteImage *, 2> layers = {&bottom, &top};
	SpriteImage image;
	ComposeImages(layers, image);

	/* The 1x layer is scaled up to 4x: it covers 8 by 8 pixels from (0, 0). */
	CHECK(image.zoom == ZoomLevel::Min);
	CHECK(image.x_offs == 0);
	CHECK(image.y_offs == 0);
	CHECK(image.width == 8);
	CHECK(image.height == 8);
	CHECK(image.At(0, 0) == Rgba8{255, 0, 0, 255});
	CHECK(image.At(2, 1) == Rgba8{0, 0, 255, 255});
	CHECK(image.At(3, 1) == Rgba8{255, 0, 0, 255});
}
