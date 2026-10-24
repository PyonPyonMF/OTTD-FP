/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file fp_tile_capture.h Copies of map chunks, so meshing does not need the game state lock. */

#ifndef FP_TILE_CAPTURE_H
#define FP_TILE_CAPTURE_H

#include "../../tile_type.h"
#include "../core/fp_chunk.h"
#include "fp_tile_sprites.h"

namespace fp {

/** What the surface of a tile shows, as far as the bare terrain is concerned. */
enum class TerrainClass : uint8_t {
	Void, ///< Outside the playable map.
	Grass, ///< Clear land with grass.
	Rough, ///< Rough land.
	Rocks, ///< Rocky land.
	Fields, ///< Farm fields.
	Desert, ///< Desert.
	Snow, ///< Snow covered land.
	Trees, ///< Trees.
	Sea, ///< Sea.
	Canal, ///< Canal.
	River, ///< River.
	Coast, ///< Coast or other shore.
	Rail, ///< Railway, including rail depots.
	Road, ///< Road or tram, including depots and level crossings.
	House, ///< Town building.
	Station, ///< Station, stop, airport, dock or waypoint.
	Industry, ///< Industry.
	TunnelBridge, ///< Tunnel entrance or bridge head.
	Object, ///< Object, e.g. transmitter, lighthouse or owned land.
	End, ///< End marker.
};

/** Snapshot of the tiles of one chunk. */
struct TerrainChunkData {
	static constexpr uint CORNERS = CHUNK_SIZE + 1; ///< Corners along each side of a chunk.

	uint chunk_x = 0; ///< X coordinate of the chunk.
	uint chunk_y = 0; ///< Y coordinate of the chunk.
	uint tiles_x = 0; ///< Number of tiles of the chunk inside the map along the X axis.
	uint tiles_y = 0; ///< Number of tiles of the chunk inside the map along the Y axis.
	std::array<uint8_t, CORNERS * CORNERS> heights{}; ///< Height of the north corner of every tile, plus one row and column of the next chunks; index y * #CORNERS + x.
	std::array<TerrainClass, CHUNK_SIZE * CHUNK_SIZE> classes{}; ///< Class of every tile; index y * #CHUNK_SIZE + x.

	/**
	 * Height of a tile corner.
	 * @param x X coordinate of the corner within the chunk, up to #CHUNK_SIZE.
	 * @param y Y coordinate of the corner within the chunk, up to #CHUNK_SIZE.
	 * @return The height in height levels.
	 */
	uint8_t Height(uint x, uint y) const { return this->heights[y * CORNERS + x]; }

	/**
	 * Class of a tile.
	 * @param x X coordinate of the tile within the chunk.
	 * @param y Y coordinate of the tile within the chunk.
	 * @return The class.
	 */
	TerrainClass Class(uint x, uint y) const { return this->classes[y * CHUNK_SIZE + x]; }
};

TerrainClass ClassifyTile(TileIndex tile);
void CaptureTerrainChunk(uint chunk_x, uint chunk_y, TerrainChunkData &out);
uint64_t HashTerrainChunk(const TerrainChunkData &data);
void CaptureChunkSprites(uint chunk_x, uint chunk_y, TileSpriteCapture &out);

} // namespace fp

#endif /* FP_TILE_CAPTURE_H */
