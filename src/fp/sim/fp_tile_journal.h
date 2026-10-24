/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file fp_tile_journal.h Journal of map chunks whose tiles changed. */

#ifndef FP_TILE_JOURNAL_H
#define FP_TILE_JOURNAL_H

#include <mutex>

#include "../core/fp_chunk.h"

namespace fp {

/**
 * Journal of the chunks whose tiles changed since the journal was last read.
 *
 * It is fed from MarkTileDirtyByTile(): every change of a tile that is
 * visible in the 2D view already passes there, so the journal sees the same
 * changes without any further hooks in the core.
 */
class TileJournal {
public:
	void Reset(uint map_size_x, uint map_size_y);
	void MarkTile(uint tile_x, uint tile_y);
	void Take(std::vector<ChunkIndex> &out);

	/**
	 * Grid the journal works on.
	 * @return The chunk grid.
	 */
	ChunkGrid GetGrid() const { return this->grid; }

private:
	std::mutex lock; ///< Guards the journal; tiles may be marked dirty from other threads than the one reading the journal.
	ChunkGrid grid{}; ///< The chunk grid of the map.
	std::vector<bool> dirty; ///< Per chunk: whether it is in #dirty_list.
	std::vector<ChunkIndex> dirty_list; ///< The dirty chunks, in the order they became dirty.

	void MarkChunk(uint cx, uint cy);
};

} // namespace fp

#endif /* FP_TILE_JOURNAL_H */
