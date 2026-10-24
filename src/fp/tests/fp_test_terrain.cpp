/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file fp_test_terrain.cpp Tests of the terrain mesher of the first-person mode. */

#include "../../stdafx.h"

#include "../../3rdparty/catch2/catch.hpp"

#include "../world/fp_terrain_mesher.h"
#include "../../landscape.h"
#include "../../slope_func.h"

#include "../../safeguards.h"

using namespace fp;

/**
 * Check that the triangles of the mesh describe the same surface as GetPartialPixelZ().
 * @param slope The slope, without half tile part.
 * @return True iff the heights agree everywhere within rounding.
 */
static bool SurfaceMatchesPartialPixelZ(Slope slope)
{
	int north = GetSlopeZInCorner(slope, Corner::N);
	int west = GetSlopeZInCorner(slope, Corner::W);
	int east = GetSlopeZInCorner(slope, Corner::E);
	int south = GetSlopeZInCorner(slope, Corner::S);

	for (uint x = 0; x < TILE_SIZE; x++) {
		for (uint y = 0; y < TILE_SIZE; y++) {
			double ours = GetTerrainSurfaceHeight(north, west, east, south, static_cast<double>(x) / TILE_SIZE, static_cast<double>(y) / TILE_SIZE) * TILE_HEIGHT;
			int theirs = GetPartialPixelZ(x, y, slope);
			/* GetPartialPixelZ() rounds to whole units. */
			if (std::abs(ours - theirs) > 1.0) {
				UNSCOPED_INFO("slope " << static_cast<int>(slope) << " at (" << x << ", " << y << "): mesh " << ours << ", game " << theirs);
				return false;
			}
		}
	}
	return true;
}

TEST_CASE("Terrain mesher - surface matches GetPartialPixelZ for all normal slopes")
{
	for (uint s = SLOPE_FLAT; s <= SLOPE_ELEVATED; s++) {
		CAPTURE(s);
		CHECK(SurfaceMatchesPartialPixelZ(static_cast<Slope>(s)));
	}
}

TEST_CASE("Terrain mesher - surface matches GetPartialPixelZ for steep slopes")
{
	CHECK(SurfaceMatchesPartialPixelZ(SLOPE_STEEP_N));
	CHECK(SurfaceMatchesPartialPixelZ(SLOPE_STEEP_E));
	CHECK(SurfaceMatchesPartialPixelZ(SLOPE_STEEP_S));
	CHECK(SurfaceMatchesPartialPixelZ(SLOPE_STEEP_W));
}

TEST_CASE("Terrain mesher - diagonal choice")
{
	/* One raised corner: split between the two neighbouring corners. */
	CHECK(GetTileDiagonal(1, 0, 0, 0) == TileDiagonal::WestEast);
	CHECK(GetTileDiagonal(0, 1, 0, 0) == TileDiagonal::NorthSouth);
	/* Two opposite raised corners: a valley along the lower diagonal. */
	CHECK(GetTileDiagonal(1, 0, 0, 1) == TileDiagonal::WestEast);
	CHECK(GetTileDiagonal(0, 1, 1, 0) == TileDiagonal::NorthSouth);
}

/**
 * Chunk of 2x2 tiles inside the map with a raised north corner on the first tile.
 * @return The chunk data.
 */
static TerrainChunkData MakeSmallChunk()
{
	TerrainChunkData data;
	data.tiles_x = 2;
	data.tiles_y = 2;
	data.classes.fill(TerrainClass::Void);
	data.classes[0] = TerrainClass::Grass;
	data.classes[1] = TerrainClass::Rail;
	data.classes[CHUNK_SIZE] = TerrainClass::Sea;
	/* The fourth tile stays void and must be skipped. */
	data.heights[0] = 1;
	return data;
}

TEST_CASE("Terrain mesher - mesh layout")
{
	TerrainMesh mesh;
	MeshTerrainChunk(MakeSmallChunk(), MetricScale{}, mesh);

	CHECK(mesh.vertices.size() == 3 * 4);
	CHECK(mesh.indices.size() == 3 * 6);
	for (uint16_t index : mesh.indices) CHECK(index < mesh.vertices.size());

	/* The raised north corner of the first tile is one height level up. */
	CHECK(mesh.vertices[0].z == Approx(MetricScale{}.metres_per_level));
	CHECK(mesh.vertices[1].z == 0.0f);
}

TEST_CASE("Terrain mesher - triangles face up")
{
	TerrainChunkData data = MakeSmallChunk();
	data.classes.fill(TerrainClass::Grass);
	data.tiles_x = CHUNK_SIZE;
	data.tiles_y = CHUNK_SIZE;
	/* Some rough terrain. */
	for (uint i = 0; i < data.heights.size(); i++) data.heights[i] = static_cast<uint8_t>((i * 7) % 3);

	TerrainMesh mesh;
	MeshTerrainChunk(data, MetricScale{}, mesh);
	REQUIRE(mesh.indices.size() == CHUNK_SIZE * CHUNK_SIZE * 6);

	for (size_t i = 0; i < mesh.indices.size(); i += 3) {
		const TerrainVertex &a = mesh.vertices[mesh.indices[i]];
		const TerrainVertex &b = mesh.vertices[mesh.indices[i + 1]];
		const TerrainVertex &c = mesh.vertices[mesh.indices[i + 2]];
		Vec3f normal = Cross(Vec3f{b.x - a.x, b.y - a.y, b.z - a.z}, Vec3f{c.x - a.x, c.y - a.y, c.z - a.z});
		CHECK(normal.z > 0.0f);
	}
}

TEST_CASE("Terrain mesher - chunk origin")
{
	MetricScale scale;
	Vec3d origin = GetChunkOrigin(2, 3, scale);
	CHECK(origin.x == Approx(2 * CHUNK_SIZE * scale.TileLength()));
	CHECK(origin.y == Approx(3 * CHUNK_SIZE * scale.TileLength()));
	CHECK(origin.z == 0.0);
}

TEST_CASE("Terrain mesher - fingerprint follows the data")
{
	TerrainChunkData a = MakeSmallChunk();
	TerrainChunkData b = MakeSmallChunk();
	CHECK(HashTerrainChunk(a) == HashTerrainChunk(b));

	b.heights[5] = 2;
	CHECK(HashTerrainChunk(a) != HashTerrainChunk(b));

	b = a;
	b.classes[1] = TerrainClass::Road;
	CHECK(HashTerrainChunk(a) != HashTerrainChunk(b));
}
