/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file fp_test_tile_journal.cpp Tests of the tile journal of the first-person mode. */

#include "../../stdafx.h"

#include "../../3rdparty/catch2/catch.hpp"

#include "../sim/fp_tile_journal.h"

#include "../../safeguards.h"

using namespace fp;

/**
 * Take the dirty chunks of a journal, sorted.
 * @param journal The journal.
 * @return The dirty chunks.
 */
static std::vector<ChunkIndex> TakeSorted(TileJournal &journal)
{
	std::vector<ChunkIndex> chunks;
	journal.Take(chunks);
	std::sort(chunks.begin(), chunks.end());
	return chunks;
}

TEST_CASE("Tile journal - grid")
{
	ChunkGrid grid(64, 128);
	CHECK(grid.size_x == 4);
	CHECK(grid.size_y == 8);
	CHECK(grid.IndexOfTile(17, 33) == grid.Index(1, 2));
	CHECK(grid.X(grid.Index(3, 5)) == 3);
	CHECK(grid.Y(grid.Index(3, 5)) == 5);
}

TEST_CASE("Tile journal - a tile inside a chunk marks only that chunk")
{
	TileJournal journal;
	journal.Reset(64, 64);
	journal.MarkTile(5, 5);
	journal.MarkTile(6, 5);
	CHECK(TakeSorted(journal) == std::vector<ChunkIndex>{0});
	CHECK(TakeSorted(journal).empty());
}

TEST_CASE("Tile journal - a tile at a chunk corner marks the chunks sharing its north corner")
{
	TileJournal journal;
	journal.Reset(64, 64);
	ChunkGrid grid = journal.GetGrid();
	journal.MarkTile(16, 16);
	CHECK(TakeSorted(journal) == std::vector<ChunkIndex>{grid.Index(0, 0), grid.Index(1, 0), grid.Index(0, 1), grid.Index(1, 1)});

	journal.MarkTile(16, 20);
	CHECK(TakeSorted(journal) == std::vector<ChunkIndex>{grid.Index(0, 1), grid.Index(1, 1)});
}

TEST_CASE("Tile journal - tiles at the map edge and outside it")
{
	TileJournal journal;
	journal.Reset(64, 64);
	journal.MarkTile(0, 0);
	journal.MarkTile(63, 63);
	journal.MarkTile(64, 64);
	ChunkGrid grid = journal.GetGrid();
	CHECK(TakeSorted(journal) == std::vector<ChunkIndex>{grid.Index(0, 0), grid.Index(3, 3)});
}

TEST_CASE("Tile journal - reset forgets everything")
{
	TileJournal journal;
	journal.Reset(64, 64);
	journal.MarkTile(5, 5);
	journal.Reset(128, 128);
	CHECK(TakeSorted(journal).empty());
	CHECK(journal.GetGrid().size_x == 8);
}
