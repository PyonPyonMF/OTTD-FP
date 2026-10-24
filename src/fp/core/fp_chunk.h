/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file fp_chunk.h Partitioning of the map into chunks of tiles. */

#ifndef FP_CHUNK_H
#define FP_CHUNK_H

#include "../../core/math_func.hpp"

namespace fp {

static constexpr uint CHUNK_SIZE = 16; ///< Number of tiles along each side of a chunk.

using ChunkIndex = uint32_t; ///< Index of a chunk in a #ChunkGrid.

/** The chunks covering a map. */
struct ChunkGrid {
	uint size_x = 0; ///< Number of chunks along the X axis.
	uint size_y = 0; ///< Number of chunks along the Y axis.

	constexpr ChunkGrid() = default;

	/**
	 * Create the grid covering a map.
	 * @param map_size_x Number of tiles along the X axis.
	 * @param map_size_y Number of tiles along the Y axis.
	 */
	constexpr ChunkGrid(uint map_size_x, uint map_size_y) : size_x(CeilDiv(map_size_x, CHUNK_SIZE)), size_y(CeilDiv(map_size_y, CHUNK_SIZE)) {}

	/**
	 * Number of chunks.
	 * @return Total number of chunks in the grid.
	 */
	constexpr uint Count() const { return this->size_x * this->size_y; }

	/**
	 * Index of a chunk.
	 * @param cx X coordinate of the chunk.
	 * @param cy Y coordinate of the chunk.
	 * @return The index.
	 */
	constexpr ChunkIndex Index(uint cx, uint cy) const { return cy * this->size_x + cx; }

	/**
	 * Index of the chunk holding a tile.
	 * @param tile_x X coordinate of the tile.
	 * @param tile_y Y coordinate of the tile.
	 * @return The index.
	 */
	constexpr ChunkIndex IndexOfTile(uint tile_x, uint tile_y) const { return this->Index(tile_x / CHUNK_SIZE, tile_y / CHUNK_SIZE); }

	/**
	 * X coordinate of a chunk.
	 * @param index Index of the chunk.
	 * @return The X coordinate.
	 */
	constexpr uint X(ChunkIndex index) const { return index % this->size_x; }

	/**
	 * Y coordinate of a chunk.
	 * @param index Index of the chunk.
	 * @return The Y coordinate.
	 */
	constexpr uint Y(ChunkIndex index) const { return index / this->size_x; }

	constexpr bool operator==(const ChunkGrid &other) const = default;
};

} // namespace fp

#endif /* FP_CHUNK_H */
