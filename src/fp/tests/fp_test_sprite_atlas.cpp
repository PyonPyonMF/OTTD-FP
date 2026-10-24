/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file fp_test_sprite_atlas.cpp Tests of the sprite atlas and the caches of the first-person mode. */

#include "../../stdafx.h"

#include "../../3rdparty/catch2/catch.hpp"

#include "../fp_hooks.h"
#include "../core/fp_lru_cache.h"
#include "../sprites/fp_sprite_atlas.h"
#include "../sprites/fp_sprite_bridge.h"
#include "../../core/math_func.hpp"
#include "../../table/sprites.h"

#include "../../safeguards.h"

using namespace fp;

/** A rectangle the packer handed out. */
struct PackedRect {
	uint page; ///< Its page.
	uint x; ///< Left column.
	uint y; ///< Top row.
	uint width; ///< Width.
	uint height; ///< Height.
};

TEST_CASE("Sprite atlas - packed rectangles stay on their page, aligned and apart")
{
	AtlasPacker packer;
	std::vector<PackedRect> rects;
	uint32_t state = 99;
	auto next = [&state](uint limit) { state = state * 1664525u + 1013904223u; return (state >> 8) % limit; };

	/* Sprite like sizes: many small tiles, some tall buildings, a few wide ones. */
	uint64_t area = 0;
	while (true) {
		uint width = Align(8 + next(next(4) == 0 ? 300 : 80), ATLAS_ALIGN);
		uint height = Align(8 + next(next(5) == 0 ? 260 : 48), ATLAS_ALIGN);
		auto slot = packer.Allocate(width, height);
		if (!slot.has_value()) break;
		rects.push_back({slot->page, slot->x, slot->y, width, height});
		area += static_cast<uint64_t>(width) * height;
	}

	CHECK(packer.GetPageCount() == ATLAS_MAX_PAGES);
	CHECK(packer.GetUsedArea() == area);
	/* Shelves pack sprites well; a full atlas is mostly used. */
	CHECK(area > static_cast<uint64_t>(ATLAS_MAX_PAGES) * ATLAS_PAGE_SIZE * ATLAS_PAGE_SIZE * 7 / 10);

	for (const PackedRect &r : rects) {
		CHECK(r.page < ATLAS_MAX_PAGES);
		CHECK(r.x % ATLAS_ALIGN == 0);
		CHECK(r.y % ATLAS_ALIGN == 0);
		CHECK(r.x + r.width <= ATLAS_PAGE_SIZE);
		CHECK(r.y + r.height <= ATLAS_PAGE_SIZE);
	}

	/* No two rectangles overlap: paint them into a coverage map per page. */
	std::vector<std::vector<uint8_t>> covered(ATLAS_MAX_PAGES, std::vector<uint8_t>((ATLAS_PAGE_SIZE / ATLAS_ALIGN) * (ATLAS_PAGE_SIZE / ATLAS_ALIGN)));
	size_t overlaps = 0;
	for (const PackedRect &r : rects) {
		for (uint y = r.y / ATLAS_ALIGN; y < (r.y + r.height) / ATLAS_ALIGN; y++) {
			for (uint x = r.x / ATLAS_ALIGN; x < (r.x + r.width) / ATLAS_ALIGN; x++) {
				if (covered[r.page][y * (ATLAS_PAGE_SIZE / ATLAS_ALIGN) + x]++ != 0) overlaps++;
			}
		}
	}
	CHECK(overlaps == 0);

	CHECK_FALSE(packer.Allocate(ATLAS_PAGE_SIZE + ATLAS_ALIGN, 8).has_value());
	packer.Reset();
	CHECK(packer.GetPageCount() == 0);
	auto slot = packer.Allocate(ATLAS_PAGE_SIZE, ATLAS_PAGE_SIZE);
	REQUIRE(slot.has_value());
	CHECK(slot->page == 0);
}

TEST_CASE("Sprite atlas - tiles repeat the edge in their gutter and have all mipmap levels")
{
	SpriteImage image;
	image.width = 5;
	image.height = 3;
	image.pixels.assign(15, Rgba8{});
	/* An opaque red left column, a translucent green pixel, the rest transparent. */
	for (int y = 0; y < 3; y++) image.pixels[y * 5] = Rgba8{200, 10, 10, 255};
	image.pixels[1 * 5 + 3] = Rgba8{10, 200, 10, 128};

	AtlasUpload upload;
	BuildAtlasTile(image, upload);
	CHECK(upload.width == Align(5 + 2 * ATLAS_GUTTER, ATLAS_ALIGN));
	CHECK(upload.height == Align(3 + 2 * ATLAS_GUTTER, ATLAS_ALIGN));
	CHECK(upload.width % ATLAS_ALIGN == 0);
	CHECK(upload.height % ATLAS_ALIGN == 0);

	size_t expected = 0;
	for (uint level = 0; level < ATLAS_MIP_LEVELS; level++) expected += static_cast<size_t>(upload.width >> level) * (upload.height >> level);
	CHECK(upload.pixels.size() == expected);

	auto at = [&upload](uint x, uint y) { return upload.pixels[y * upload.width + x]; };
	/* The image itself is unchanged where it is visible. */
	CHECK(at(ATLAS_GUTTER, ATLAS_GUTTER) == Rgba8{200, 10, 10, 255});
	CHECK(at(ATLAS_GUTTER + 3, ATLAS_GUTTER + 1) == Rgba8{10, 200, 10, 128});
	/* The gutter repeats the edge, like clamping would. */
	CHECK(at(0, 0) == Rgba8{200, 10, 10, 255});
	CHECK(at(0, upload.height - 1) == Rgba8{200, 10, 10, 255});
	/* Transparent texels take the colour of their neighbours instead of black. */
	Rgba8 bled = at(ATLAS_GUTTER + 1, ATLAS_GUTTER);
	CHECK(bled.a == 0);
	CHECK(bled.r + bled.g > 150);
	for (const Rgba8 &p : upload.pixels) {
		if (p.a == 0) CHECK(p.r + p.g + p.b > 0);
	}
}

TEST_CASE("Sprite atlas - mipmaps of a uniform colour keep the colour")
{
	SpriteImage image;
	image.width = 16;
	image.height = 16;
	image.pixels.assign(256, Rgba8{90, 140, 210, 255});

	AtlasUpload upload;
	BuildAtlasTile(image, upload);
	for (const Rgba8 &p : upload.pixels) CHECK(p == Rgba8{90, 140, 210, 255});
}

TEST_CASE("Sprite atlas - mipmaps average in linear light, weighted by alpha")
{
	/* Black and white average to the middle grey of linear light, not to 128. */
	SpriteImage image;
	image.width = 16;
	image.height = 16;
	image.pixels.resize(256);
	for (uint i = 0; i < 256; i++) image.pixels[i] = (i % 2 == 0) ? Rgba8{0, 0, 0, 255} : Rgba8{255, 255, 255, 255};

	AtlasUpload upload;
	BuildAtlasTile(image, upload);
	const size_t level1 = static_cast<size_t>(upload.width) * upload.height;
	const Rgba8 grey = upload.pixels[level1 + (ATLAS_GUTTER / 2 + 2) * (upload.width / 2) + ATLAS_GUTTER / 2 + 2];
	CHECK(grey.r == 188);
	CHECK(grey.a == 255);

	/* A transparent texel does not darken an opaque one. */
	image.pixels.assign(256, Rgba8{});
	for (uint i = 0; i < 256; i += 2) image.pixels[i] = Rgba8{250, 40, 40, 255};
	BuildAtlasTile(image, upload);
	const Rgba8 half = upload.pixels[level1 + (ATLAS_GUTTER / 2 + 2) * (upload.width / 2) + ATLAS_GUTTER / 2 + 2];
	CHECK(half.r == 250);
	CHECK(half.a == 128);
}

TEST_CASE("Sprite atlas - a sprite pixel matches the tile diamond of the 2D view")
{
	MetricScale scale;
	/* A flat tile is 64 pixels wide at 1x, and 256 at 4x; across, it spans the diagonal of the tile. */
	CHECK(SpritePixelLength(scale, ZoomLevel::Normal) * 2 * TILE_PIXELS == Approx(scale.TileLength() * std::numbers::sqrt2));
	CHECK(SpritePixelLength(scale, ZoomLevel::Min) * 4 == Approx(SpritePixelLength(scale, ZoomLevel::Normal)));
}

TEST_CASE("Sprite atlas - changes merge, a reset drops earlier uploads")
{
	AtlasChanges first;
	first.pages = 1;
	first.uploads.resize(2);

	AtlasChanges second;
	second.pages = 2;
	second.uploads.resize(1);
	first.Merge(std::move(second));
	CHECK(first.uploads.size() == 3);
	CHECK(first.pages == 2);
	CHECK_FALSE(first.reset);

	AtlasChanges third;
	third.reset = true;
	third.pages = 1;
	third.uploads.resize(1);
	first.Merge(std::move(third));
	CHECK(first.reset);
	CHECK(first.uploads.size() == 1);
	CHECK(first.pages == 1);
}

TEST_CASE("Sprite atlas - reloading the sprites of the game voids the atlas")
{
	SpriteBridge &bridge = SpriteBridge::Get();
	SpriteAtlas atlas;

	/* A sprite that does not exist cannot be shown, but the request brings the atlas up to date. */
	const SpriteID missing = SPRITE_MASK;
	CHECK_FALSE(atlas.Request(missing, PAL_NONE).has_value());
	const uint32_t generation = bridge.GetGeneration();
	const uint32_t epoch = atlas.GetEpoch();
	CHECK_FALSE(atlas.Request(missing, PAL_NONE).has_value());
	CHECK(atlas.GetEpoch() == epoch);

	OnSpritesInvalidated();
	CHECK(bridge.GetGeneration() != generation);
	CHECK_FALSE(atlas.Request(missing, PAL_NONE).has_value());
	CHECK(atlas.GetEpoch() == epoch + 1);

	/* The renderer learns that everything on the pages is void. */
	AtlasChanges changes;
	atlas.TakeChanges(changes);
	CHECK(changes.reset);
	CHECK(changes.uploads.empty());
	atlas.TakeChanges(changes);
	CHECK_FALSE(changes.reset);
}

TEST_CASE("LRU cache - least recently used values leave first, held values live on")
{
	LruCache<int, std::string> cache(10);
	cache.Insert(1, std::make_shared<std::string>("one"), 4);
	cache.Insert(2, std::make_shared<std::string>("two"), 4);
	CHECK(cache.GetSize() == 8);

	/* Using 1 makes 2 the least recently used, so 2 goes when 3 comes. */
	auto one = cache.Find(1);
	REQUIRE(one != nullptr);
	cache.Insert(3, std::make_shared<std::string>("three"), 4);
	CHECK(cache.Find(2) == nullptr);
	CHECK(cache.Find(1) != nullptr);
	CHECK(cache.Find(3) != nullptr);
	CHECK(cache.GetCount() == 2);
	CHECK(cache.GetSize() == 8);

	/* A value larger than the budget stays as the only one; values still held remain valid. */
	cache.Insert(4, std::make_shared<std::string>("four"), 50);
	CHECK(cache.GetCount() == 1);
	CHECK(cache.Find(4) != nullptr);
	CHECK(*one == "one");

	/* Replacing a value adjusts the size. */
	cache.Insert(4, std::make_shared<std::string>("FOUR"), 2);
	CHECK(cache.GetSize() == 2);
	CHECK(*cache.Find(4) == "FOUR");

	cache.Clear();
	CHECK(cache.GetCount() == 0);
	CHECK(cache.GetSize() == 0);
}
