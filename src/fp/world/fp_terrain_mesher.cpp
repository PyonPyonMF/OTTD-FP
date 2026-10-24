/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file fp_terrain_mesher.cpp Triangle meshes of the bare terrain. */

#include "../../stdafx.h"
#include "fp_terrain_mesher.h"
#include "../../core/enum_type.hpp"

#include "../../safeguards.h"

namespace fp {

/**
 * Choose the diagonal that splits a tile into the same two planes as OpenTTD uses.
 *
 * GetPartialPixelZ() defines the surface: it is split along the diagonal
 * whose two corners have the same height (e.g. west and east when only the
 * north corner is raised). When both diagonals qualify, as with two raised
 * opposite corners, the split runs along the lower diagonal, forming a valley.
 * When neither does the tile is a plane and any split is fine.
 * @param north Height of the north corner.
 * @param west Height of the west corner.
 * @param east Height of the east corner.
 * @param south Height of the south corner.
 * @return The diagonal to split along.
 */
TileDiagonal GetTileDiagonal(int north, int west, int east, int south)
{
	bool west_east_level = west == east;
	bool north_south_level = north == south;

	if (west_east_level && north_south_level) return west + east <= north + south ? TileDiagonal::WestEast : TileDiagonal::NorthSouth;
	if (west_east_level) return TileDiagonal::WestEast;
	return TileDiagonal::NorthSouth;
}

/**
 * Height of the terrain at a point of a tile, using the same triangles as the mesh.
 * The north corner is at (0, 0), west at (1, 0), east at (0, 1) and south at (1, 1).
 * @param north Height of the north corner.
 * @param west Height of the west corner.
 * @param east Height of the east corner.
 * @param south Height of the south corner.
 * @param fx Position along the X axis of the tile, 0 to 1.
 * @param fy Position along the Y axis of the tile, 0 to 1.
 * @return The height, in the unit of the corner heights.
 */
double GetTerrainSurfaceHeight(int north, int west, int east, int south, double fx, double fy)
{
	if (GetTileDiagonal(north, west, east, south) == TileDiagonal::WestEast) {
		if (fx + fy <= 1.0) return north + (west - north) * fx + (east - north) * fy;
		return south + (east - south) * (1.0 - fx) + (west - south) * (1.0 - fy);
	}

	if (fx >= fy) return north + (west - north) * fx + (south - west) * fy;
	return north + (south - east) * fx + (east - north) * fy;
}

/**
 * Debug colour of a terrain class.
 * In milestone M0 the terrain has no textures; the colours make the tile types readable.
 * @param terrain_class The class.
 * @return The colour.
 */
TerrainColour GetTerrainColour(TerrainClass terrain_class)
{
	static constexpr EnumIndexArray<TerrainColour, TerrainClass, TerrainClass::End> COLOURS{{{
		{  0,   0,   0}, // Void
		{ 98, 142,  62}, // Grass
		{122, 124,  80}, // Rough
		{128, 120, 110}, // Rocks
		{176, 156,  72}, // Fields
		{222, 202, 142}, // Desert
		{236, 240, 246}, // Snow
		{ 52,  98,  44}, // Trees
		{ 38,  88, 150}, // Sea
		{ 58, 110, 160}, // Canal
		{ 48, 108, 172}, // River
		{202, 190, 140}, // Coast
		{112,  96,  86}, // Rail
		{ 72,  72,  78}, // Road
		{176,  92,  80}, // House
		{204, 172,  60}, // Station
		{132,  92, 152}, // Industry
		{152, 132, 112}, // TunnelBridge
		{100, 132, 152}, // Object
	}}};
	return COLOURS[terrain_class];
}

/**
 * Position of the north corner of a chunk in the first-person world.
 * @param chunk_x X coordinate of the chunk.
 * @param chunk_y Y coordinate of the chunk.
 * @param scale The metric scales.
 * @return The position, in metres.
 */
Vec3d GetChunkOrigin(uint chunk_x, uint chunk_y, const MetricScale &scale)
{
	double chunk_length = scale.TileLength() * CHUNK_SIZE;
	return {chunk_x * chunk_length, chunk_y * chunk_length, 0.0};
}

/**
 * Small, stable variation of brightness per tile, so neighbouring tiles of the same class remain distinguishable.
 * @param x X coordinate of the tile on the map.
 * @param y Y coordinate of the tile on the map.
 * @return Brightness offset.
 */
static int TileShade(uint x, uint y)
{
	uint32_t h = x * 0x9E3779B1U ^ y * 0x85EBCA77U;
	h ^= h >> 15;
	h *= 0x2C1B3C6DU;
	h ^= h >> 12;
	return static_cast<int>(h % 13) - 6;
}

/**
 * Build the mesh of the bare terrain of a chunk: two triangles per tile, flat
 * coloured by class. Tiles outside the map are left out.
 * @param data Copy of the tiles of the chunk.
 * @param scale The metric scales.
 * @param[out] out Receives the mesh; previous contents are discarded.
 */
void MeshTerrainChunk(const TerrainChunkData &data, const MetricScale &scale, TerrainMesh &out)
{
	out.vertices.clear();
	out.indices.clear();

	const float tile_length = static_cast<float>(scale.TileLength());
	const float level = static_cast<float>(scale.metres_per_level);

	for (uint y = 0; y < data.tiles_y; y++) {
		for (uint x = 0; x < data.tiles_x; x++) {
			TerrainClass terrain_class = data.Class(x, y);
			if (terrain_class == TerrainClass::Void) continue;

			int north = data.Height(x, y);
			int west = data.Height(x + 1, y);
			int east = data.Height(x, y + 1);
			int south = data.Height(x + 1, y + 1);

			TerrainColour colour = GetTerrainColour(terrain_class);
			int shade = TileShade(data.chunk_x * CHUNK_SIZE + x, data.chunk_y * CHUNK_SIZE + y);
			auto channel = [shade](uint8_t c) { return static_cast<uint8_t>(Clamp(c + shade, 0, 255)); };
			uint8_t r = channel(colour.r);
			uint8_t g = channel(colour.g);
			uint8_t b = channel(colour.b);

			auto corner = [&](uint cx, uint cy, int height) {
				out.vertices.push_back({cx * tile_length, cy * tile_length, height * level, r, g, b, 255});
			};

			uint16_t base = static_cast<uint16_t>(out.vertices.size());
			corner(x, y, north);
			corner(x + 1, y, west);
			corner(x, y + 1, east);
			corner(x + 1, y + 1, south);

			const uint16_t n = base, w = base + 1, e = base + 2, s = base + 3;
			if (GetTileDiagonal(north, west, east, south) == TileDiagonal::WestEast) {
				out.indices.insert(out.indices.end(), {n, w, e, w, s, e});
			} else {
				out.indices.insert(out.indices.end(), {n, w, s, n, s, e});
			}
		}
	}
}

} // namespace fp
