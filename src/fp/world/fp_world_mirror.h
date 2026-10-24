/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file fp_world_mirror.h The World Mirror: the 3D representation derived from the map. */

#ifndef FP_WORLD_MIRROR_H
#define FP_WORLD_MIRROR_H

#include <chrono>

#include "fp_terrain_mesher.h"

namespace fp {

/**
 * Change of the chunks held by the renderer: the removal of a chunk, or what
 * changed of it. A chunk that is built for the first time gets both its mesh
 * and its sprites; later only what really changed is sent again.
 */
struct ChunkOp {
	ChunkIndex index = 0; ///< The chunk.
	Vec3d origin{}; ///< Position of the north corner of the chunk, in metres.
	bool remove = false; ///< Whether the chunk goes away; nothing else is set then.
	std::optional<TerrainMesh> mesh; ///< The new terrain mesh, if the terrain changed.
	std::shared_ptr<const TileSpriteCapture> sprites; ///< The new sprites of its tiles as the 2D view draws them, if they changed; see CaptureChunkSprites().
};

/**
 * The World Mirror (docs/fp/architecture.md, section 6).
 *
 * Keeps track of which chunks around the camera are built and turns map
 * changes into new meshes. Chunks are built nearest first within a time
 * budget, and dropped again when the camera moves away.
 */
class WorldMirror {
public:
	/** Counters for diagnostics. */
	struct Statistics {
		uint built_chunks = 0; ///< Chunks currently built.
		uint pending_chunks = 0; ///< Chunks in view that still need to be built.
		uint64_t meshed_chunks = 0; ///< Chunks meshed since the last reset.
		uint64_t sprite_changes = 0; ///< Chunks whose sprites were sent, since the last reset.
		uint64_t unchanged_chunks = 0; ///< Dirty chunks whose terrain and sprites turned out not to have changed, since the last reset.
		uint64_t captured_chunks = 0; ///< Chunks whose sprites were captured, since the last reset.
		uint64_t captured_sprites = 0; ///< Sprites captured in these chunks.
		std::chrono::nanoseconds terrain_time{}; ///< Time spent capturing and hashing terrain, since the last reset.
		std::chrono::nanoseconds sprite_time{}; ///< Time spent capturing and hashing sprites, since the last reset.
	};

	void Reset(const ChunkGrid &grid, const MetricScale &scale);
	void MarkDirty(std::span<const ChunkIndex> chunks);
	void Update(const Vec3d &camera, double view_distance, std::chrono::microseconds budget, std::vector<ChunkOp> &ops);

	/**
	 * Diagnostic counters.
	 * @return The counters.
	 */
	const Statistics &GetStatistics() const { return this->statistics; }

	/**
	 * The chunk grid the mirror works on.
	 * @return The grid.
	 */
	const ChunkGrid &GetGrid() const { return this->grid; }

private:
	/** State of one chunk. */
	enum class ChunkState : uint8_t {
		Empty, ///< Not built.
		Built, ///< Built and up to date.
		Dirty, ///< Built, but the tiles changed since.
	};

	ChunkGrid grid{}; ///< The chunks of the map.
	MetricScale scale{}; ///< The metric scales.
	std::vector<ChunkState> states; ///< State per chunk.
	std::vector<uint64_t> hashes; ///< Per chunk: fingerprint of the terrain it was built from, see HashTerrainChunk().
	std::vector<uint64_t> sprite_hashes; ///< Per chunk: fingerprint of the sprites it was built from, see HashTileSprites().
	std::vector<ChunkIndex> built; ///< Chunks that are built (or dirty).
	std::vector<ChunkIndex> candidates; ///< Scratch list of chunks to build.
	TerrainChunkData capture; ///< Scratch copy of a chunk.
	TileSpriteCapture sprites; ///< Scratch capture of the sprites of a chunk.
	Statistics statistics; ///< Diagnostic counters.
};

} // namespace fp

#endif /* FP_WORLD_MIRROR_H */
