/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file fp_tile_capture.cpp Copies of map chunks, so meshing does not need the game state lock. */

#include "../../stdafx.h"
#include "fp_tile_capture.h"
#include "../fp_hooks.h"
#include "../../clear_map.h"
#include "../../core/enum_type.hpp"
#include "../../map_func.h"
#include "../../tile_map.h"
#include "../../water_map.h"

#include "../../safeguards.h"

namespace fp {

/**
 * Determine what the surface of a tile shows.
 * @param tile The tile.
 * @return The terrain class.
 * @pre The game state lock is held.
 */
TerrainClass ClassifyTile(TileIndex tile)
{
	switch (GetTileType(tile)) {
		case TileType::Clear:
			if (IsSnowTile(tile)) return TerrainClass::Snow;
			switch (GetClearGround(tile)) {
				case ClearGround::Rough: return TerrainClass::Rough;
				case ClearGround::Rocks: return TerrainClass::Rocks;
				case ClearGround::Fields: return TerrainClass::Fields;
				case ClearGround::Desert: return TerrainClass::Desert;
				default: return TerrainClass::Grass;
			}

		case TileType::Water:
			if (IsCoast(tile)) return TerrainClass::Coast;
			switch (GetWaterClass(tile)) {
				case WaterClass::Canal: return TerrainClass::Canal;
				case WaterClass::River: return TerrainClass::River;
				default: return TerrainClass::Sea;
			}

		case TileType::Railway: return TerrainClass::Rail;
		case TileType::Road: return TerrainClass::Road;
		case TileType::House: return TerrainClass::House;
		case TileType::Trees: return TerrainClass::Trees;
		case TileType::Station: return TerrainClass::Station;
		case TileType::Industry: return TerrainClass::Industry;
		case TileType::TunnelBridge: return TerrainClass::TunnelBridge;
		case TileType::Object: return TerrainClass::Object;
		default: return TerrainClass::Void;
	}
}

/**
 * Copy what the terrain mesher needs of one chunk.
 * Corners beyond the edge of the map take the height of the nearest tile
 * inside it, exactly like GetTileSlope() does.
 * @param chunk_x X coordinate of the chunk.
 * @param chunk_y Y coordinate of the chunk.
 * @param[out] out Receives the copy.
 * @pre The game state lock is held.
 */
void CaptureTerrainChunk(uint chunk_x, uint chunk_y, TerrainChunkData &out)
{
	uint x0 = chunk_x * CHUNK_SIZE;
	uint y0 = chunk_y * CHUNK_SIZE;

	out.chunk_x = chunk_x;
	out.chunk_y = chunk_y;
	out.tiles_x = std::min(CHUNK_SIZE, Map::SizeX() - x0);
	out.tiles_y = std::min(CHUNK_SIZE, Map::SizeY() - y0);

	for (uint y = 0; y < TerrainChunkData::CORNERS; y++) {
		uint ty = std::min(y0 + y, Map::MaxY());
		for (uint x = 0; x < TerrainChunkData::CORNERS; x++) {
			uint tx = std::min(x0 + x, Map::MaxX());
			out.heights[y * TerrainChunkData::CORNERS + x] = TileHeight(TileXY(tx, ty));
		}
	}

	for (uint y = 0; y < CHUNK_SIZE; y++) {
		for (uint x = 0; x < CHUNK_SIZE; x++) {
			bool inside = x < out.tiles_x && y < out.tiles_y;
			out.classes[y * CHUNK_SIZE + x] = inside ? ClassifyTile(TileXY(x0 + x, y0 + y)) : TerrainClass::Void;
		}
	}
}

/**
 * Fingerprint of everything the terrain mesh depends on.
 * Many tiles are marked dirty without their terrain changing, e.g. animated
 * tiles or switching signals; comparing fingerprints avoids meshing them again.
 * @param data The captured chunk.
 * @return 64-bit FNV-1a hash of the data.
 */
uint64_t HashTerrainChunk(const TerrainChunkData &data)
{
	uint64_t hash = 0xCBF29CE484222325ULL;
	auto mix = [&hash](uint8_t byte) {
		hash ^= byte;
		hash *= 0x100000001B3ULL;
	};

	mix(static_cast<uint8_t>(data.tiles_x));
	mix(static_cast<uint8_t>(data.tiles_y));
	for (uint8_t height : data.heights) mix(height);
	for (TerrainClass terrain_class : data.classes) mix(to_underlying(terrain_class));
	return hash;
}

/**
 * Record the sprites the 2D view draws for the tiles of one chunk, see CaptureTileSprites().
 * Tiles are captured row by row, i.e. tile (x, y) of the chunk is tile y * width + x of the capture.
 * @param chunk_x X coordinate of the chunk.
 * @param chunk_y Y coordinate of the chunk.
 * @param[out] out Receives the sprites; what it held before is dropped.
 * @pre The game state lock is held.
 */
void CaptureChunkSprites(uint chunk_x, uint chunk_y, TileSpriteCapture &out)
{
	uint x0 = chunk_x * CHUNK_SIZE;
	uint y0 = chunk_y * CHUNK_SIZE;
	uint x1 = std::min(x0 + CHUNK_SIZE, Map::SizeX());
	uint y1 = std::min(y0 + CHUNK_SIZE, Map::SizeY());

	out.Clear();
	for (uint y = y0; y < y1; y++) {
		for (uint x = x0; x < x1; x++) CaptureTileSprites(TileXY(x, y), out);
	}
}

} // namespace fp
