/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file fp_tile_journal.cpp Journal of map chunks whose tiles changed. */

#include "../../stdafx.h"
#include "fp_tile_journal.h"

#include "../../safeguards.h"

namespace fp {

/**
 * Start over for a map of the given size; nothing is dirty afterwards.
 * @param map_size_x Number of tiles along the X axis.
 * @param map_size_y Number of tiles along the Y axis.
 */
void TileJournal::Reset(uint map_size_x, uint map_size_y)
{
	std::lock_guard<std::mutex> guard(this->lock);

	this->grid = ChunkGrid(map_size_x, map_size_y);
	this->dirty.assign(this->grid.Count(), false);
	this->dirty_list.clear();
}

/**
 * Record that a tile changed.
 *
 * The terrain of a tile is spanned between its own north corner and those
 * of its neighbours at +X, +Y and +X+Y. When the north corner of a tile
 * moves, the tiles at -X, -Y and -X-Y change shape as well, so their chunks
 * are marked too. Within a chunk that is a no-op.
 * @param tile_x X coordinate of the tile.
 * @param tile_y Y coordinate of the tile.
 */
void TileJournal::MarkTile(uint tile_x, uint tile_y)
{
	std::lock_guard<std::mutex> guard(this->lock);

	uint cx = tile_x / CHUNK_SIZE;
	uint cy = tile_y / CHUNK_SIZE;
	bool west_edge = tile_x % CHUNK_SIZE == 0 && cx > 0;
	bool north_edge = tile_y % CHUNK_SIZE == 0 && cy > 0;

	this->MarkChunk(cx, cy);
	if (west_edge) this->MarkChunk(cx - 1, cy);
	if (north_edge) this->MarkChunk(cx, cy - 1);
	if (west_edge && north_edge) this->MarkChunk(cx - 1, cy - 1);
}

/**
 * Hand out the dirty chunks and forget about them.
 * @param[out] out The dirty chunks are appended to this.
 */
void TileJournal::Take(std::vector<ChunkIndex> &out)
{
	std::lock_guard<std::mutex> guard(this->lock);

	for (ChunkIndex index : this->dirty_list) this->dirty[index] = false;
	out.insert(out.end(), this->dirty_list.begin(), this->dirty_list.end());
	this->dirty_list.clear();
}

/**
 * Mark a chunk dirty.
 * @param cx X coordinate of the chunk.
 * @param cy Y coordinate of the chunk.
 * @pre #lock is held.
 */
void TileJournal::MarkChunk(uint cx, uint cy)
{
	if (cx >= this->grid.size_x || cy >= this->grid.size_y) return;

	ChunkIndex index = this->grid.Index(cx, cy);
	if (this->dirty[index]) return;

	this->dirty[index] = true;
	this->dirty_list.push_back(index);
}

} // namespace fp
