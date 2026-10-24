/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file fp_test_tile_sprites.cpp Tests of the captured tile sprites of the first-person mode. */

#include "../../stdafx.h"

#include "../../3rdparty/catch2/catch.hpp"

#include "../world/fp_tile_sprites.h"
#include "../../table/sprites.h"

#include "../../safeguards.h"

using namespace fp;

/**
 * A sprite for tests.
 * @param image The sprite.
 * @param x X offset.
 * @param y Y offset.
 * @return The sprite.
 */
static CapturedSprite MakeSprite(SpriteID image, int x, int y)
{
	CapturedSprite sprite;
	sprite.image = image;
	sprite.pal = PAL_NONE;
	sprite.offset = {x, y};
	return sprite;
}

/**
 * Append a tile like CaptureTileSprites() does: a ground sprite, and a
 * foundation carrying a ground sprite plus a building with a child.
 * @param capture The capture to append to.
 * @param tile The tile.
 */
static void AppendTile(TileSpriteCapture &capture, TileIndex tile)
{
	TileSprites &sprites = capture.tiles.emplace_back();
	sprites.tile = tile;
	sprites.origin = {16, 32, 8};
	sprites.slope = SLOPE_N;
	sprites.ground_z = 16;
	sprites.ground_slope = SLOPE_FLAT;

	sprites.first_ground = static_cast<uint32_t>(capture.ground.size());
	capture.ground.push_back(MakeSprite(3981, 0, 0));
	sprites.ground_count = 1;

	sprites.first_parent = static_cast<uint32_t>(capture.parents.size());
	CapturedParentSprite &foundation = capture.parents.emplace_back();
	foundation.sprite = MakeSprite(990, 0, 0);
	foundation.bb_max = {16, 16, 7};
	foundation.first_child = static_cast<uint32_t>(capture.children.size());
	capture.children.push_back(MakeSprite(3981, 0, -32));
	foundation.child_count = 1;

	CapturedParentSprite &building = capture.parents.emplace_back();
	building.sprite = MakeSprite(1421, 0, -32);
	building.sprite.pal = 775;
	building.bb_min = {1, 1, 8};
	building.bb_max = {15, 15, 40};
	building.first_child = static_cast<uint32_t>(capture.children.size());
	capture.children.push_back(MakeSprite(1422, 4, -50));
	capture.children.push_back(MakeSprite(1423, 6, -52));
	building.child_count = 2;
	sprites.parent_count = 2;

	sprites.foundations[to_underlying(CapturedFoundation::Normal)] = sprites.first_parent;
}

TEST_CASE("Tile sprites - ranges of a tile")
{
	TileSpriteCapture capture;
	AppendTile(capture, TileIndex{100});
	AppendTile(capture, TileIndex{101});

	REQUIRE(capture.tiles.size() == 2);
	CHECK(capture.SpriteCount() == 2 + 4 + 6);

	const TileSprites &second = capture.tiles[1];
	REQUIRE(capture.Ground(second).size() == 1);
	CHECK(&capture.Ground(second)[0] == &capture.ground[1]);

	std::span<const CapturedParentSprite> parents = capture.Parents(second);
	REQUIRE(parents.size() == 2);
	CHECK(parents[1].sprite.image == 1421);
	std::span<const CapturedSprite> children = capture.Children(parents[1]);
	REQUIRE(children.size() == 2);
	CHECK(children[0].image == 1422);
	CHECK(children[1].image == 1423);
	CHECK(&children[0] == &capture.children[4]);

	CHECK(second.foundations[to_underlying(CapturedFoundation::Normal)] == 2);
	CHECK(second.foundations[to_underlying(CapturedFoundation::Halftile)] == TileSprites::NO_FOUNDATION);

	capture.Clear();
	CHECK(capture.tiles.empty());
	CHECK(capture.SpriteCount() == 0);
}

TEST_CASE("Tile sprites - equality")
{
	CapturedSprite a = MakeSprite(1000, 1, 2);
	CapturedSprite b = a;
	CHECK(a == b);

	b.sub = SubSprite{0, 0, 10, 10};
	CHECK_FALSE(a == b);
	a.sub = SubSprite{0, 0, 10, 10};
	CHECK(a == b);
	a.sub->bottom = 9;
	CHECK_FALSE(a == b);

	CapturedParentSprite p, q;
	p.sprite = q.sprite = MakeSprite(1000, 0, 0);
	CHECK(p == q);
	q.bb_max.z = 1;
	CHECK_FALSE(p == q);
}

TEST_CASE("Tile sprites - fingerprint")
{
	TileSpriteCapture reference;
	AppendTile(reference, TileIndex{100});
	const uint64_t hash = HashTileSprites(reference);
	CHECK(hash == HashTileSprites(reference));

	/* The same sprites at other positions in the lists, as when other data came before, have the same fingerprint. */
	TileSpriteCapture shifted;
	shifted.children.push_back(MakeSprite(1, 0, 0));
	shifted.parents.emplace_back();
	AppendTile(shifted, TileIndex{100});
	CHECK(HashTileSprites(shifted) == hash);

	/* Any change of what would be drawn changes it. */
	auto changed = [&reference](std::function<void(TileSpriteCapture &)> change) {
		TileSpriteCapture copy = reference;
		change(copy);
		return HashTileSprites(copy);
	};
	CHECK(changed([](TileSpriteCapture &c) { c.ground[0].image++; }) != hash);
	CHECK(changed([](TileSpriteCapture &c) { c.ground[0].offset.y--; }) != hash);
	CHECK(changed([](TileSpriteCapture &c) { c.parents[1].sprite.pal = PAL_NONE; }) != hash);
	CHECK(changed([](TileSpriteCapture &c) { c.parents[1].sprite.sub = SubSprite{0, 0, 1, 1}; }) != hash);
	CHECK(changed([](TileSpriteCapture &c) { c.parents[1].bb_max.z++; }) != hash);
	CHECK(changed([](TileSpriteCapture &c) { c.children[2].offset.x++; }) != hash);
	CHECK(changed([](TileSpriteCapture &c) { c.parents[1].child_count = 1; }) != hash);
	CHECK(changed([](TileSpriteCapture &c) { c.tiles[0].ground_z = 8; }) != hash);
	CHECK(changed([](TileSpriteCapture &c) { c.tiles[0].foundations[0] = TileSprites::NO_FOUNDATION; }) != hash);
	CHECK(changed([](TileSpriteCapture &c) { c.tiles[0].tile = TileIndex{101}; }) != hash);

	/* Animation: one frame of a building differs from the next. */
	CHECK(changed([](TileSpriteCapture &c) { c.children[1].image = 1424; }) != hash);
}
