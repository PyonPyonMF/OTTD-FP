/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file fp_world_mirror.cpp The World Mirror: the 3D representation derived from the map. */

#include "../../stdafx.h"
#include "fp_world_mirror.h"

#include "../../safeguards.h"

namespace fp {

/**
 * Forget all chunks and start over on a (possibly) different map.
 * @param grid The chunk grid of the map.
 * @param scale The metric scales to build with.
 */
void WorldMirror::Reset(const ChunkGrid &grid, const MetricScale &scale)
{
	this->grid = grid;
	this->scale = scale;
	this->states.assign(grid.Count(), ChunkState::Empty);
	this->hashes.assign(grid.Count(), 0);
	this->sprite_hashes.assign(grid.Count(), 0);
	this->built.clear();
	this->statistics = {};
}

/**
 * Mark chunks whose tiles changed. Chunks that are not built are not affected.
 * @param chunks The changed chunks.
 */
void WorldMirror::MarkDirty(std::span<const ChunkIndex> chunks)
{
	for (ChunkIndex index : chunks) {
		if (index < this->states.size() && this->states[index] == ChunkState::Built) this->states[index] = ChunkState::Dirty;
	}
}

/**
 * Bring the chunks around the camera up to date.
 *
 * Chunks further away than \a view_distance plus a margin are removed, so
 * moving back and forth at the edge of the view does not rebuild chunks.
 * Missing and dirty chunks within \a view_distance are built nearest first,
 * until \a budget is used up; at least one chunk is built per call.
 *
 * Terrain and sprites have separate fingerprints: a dirty chunk keeps its
 * mesh unless the terrain changed, and only sends its sprites again if they
 * changed, e.g. because of animation, growing trees or a switching signal.
 * Many tiles are marked dirty without either changing.
 * @param camera Position of the camera, in metres.
 * @param view_distance Radius around the camera that should be built, in metres.
 * @param budget Time that may be spent on meshing.
 * @param[out] ops Receives the changes for the renderer.
 * @pre The game state lock is held.
 */
void WorldMirror::Update(const Vec3d &camera, double view_distance, std::chrono::microseconds budget, std::vector<ChunkOp> &ops)
{
	if (this->grid.Count() == 0) return;

	const double chunk_length = this->scale.TileLength() * CHUNK_SIZE;
	const double keep_distance = view_distance + chunk_length * 2;

	/* Distance from the camera to the centre of a chunk, ignoring height. */
	auto distance = [&](ChunkIndex index) {
		double dx = (this->grid.X(index) + 0.5) * chunk_length - camera.x;
		double dy = (this->grid.Y(index) + 0.5) * chunk_length - camera.y;
		return std::hypot(dx, dy);
	};

	/* Drop chunks that are out of range. */
	auto out_of_range = [&](ChunkIndex index) {
		if (distance(index) <= keep_distance) return false;
		this->states[index] = ChunkState::Empty;
		ChunkOp &op = ops.emplace_back();
		op.index = index;
		op.remove = true;
		return true;
	};
	this->built.erase(std::remove_if(this->built.begin(), this->built.end(), out_of_range), this->built.end());

	/* Collect what needs building within the view distance. */
	int radius = static_cast<int>(std::ceil(view_distance / chunk_length));
	int centre_x = static_cast<int>(std::floor(camera.x / chunk_length));
	int centre_y = static_cast<int>(std::floor(camera.y / chunk_length));
	int min_x = std::max(centre_x - radius, 0);
	int min_y = std::max(centre_y - radius, 0);
	int max_x = std::min(centre_x + radius, static_cast<int>(this->grid.size_x) - 1);
	int max_y = std::min(centre_y + radius, static_cast<int>(this->grid.size_y) - 1);

	this->candidates.clear();
	for (int cy = min_y; cy <= max_y; cy++) {
		for (int cx = min_x; cx <= max_x; cx++) {
			ChunkIndex index = this->grid.Index(cx, cy);
			if (this->states[index] != ChunkState::Built && distance(index) <= view_distance) this->candidates.push_back(index);
		}
	}
	std::sort(this->candidates.begin(), this->candidates.end(), [&](ChunkIndex a, ChunkIndex b) { return distance(a) < distance(b); });

	/* Build nearest first, within the budget. */
	auto start = std::chrono::steady_clock::now();
	size_t done = 0;
	for (ChunkIndex index : this->candidates) {
		if (done > 0 && std::chrono::steady_clock::now() - start >= budget) break;

		uint cx = this->grid.X(index);
		uint cy = this->grid.Y(index);
		bool fresh = this->states[index] == ChunkState::Empty;
		done++;

		auto terrain_start = std::chrono::steady_clock::now();
		CaptureTerrainChunk(cx, cy, this->capture);
		uint64_t hash = HashTerrainChunk(this->capture);
		auto sprite_start = std::chrono::steady_clock::now();
		CaptureChunkSprites(cx, cy, this->sprites);
		uint64_t sprite_hash = HashTileSprites(this->sprites);
		auto sprite_end = std::chrono::steady_clock::now();

		this->statistics.terrain_time += sprite_start - terrain_start;
		this->statistics.sprite_time += sprite_end - sprite_start;
		this->statistics.captured_chunks++;
		this->statistics.captured_sprites += this->sprites.SpriteCount();

		bool terrain_changed = fresh || hash != this->hashes[index];
		bool sprites_changed = fresh || sprite_hash != this->sprite_hashes[index];
		this->hashes[index] = hash;
		this->sprite_hashes[index] = sprite_hash;
		if (fresh) this->built.push_back(index);
		this->states[index] = ChunkState::Built;

		if (!terrain_changed && !sprites_changed) {
			this->statistics.unchanged_chunks++;
			continue;
		}

		ChunkOp &op = ops.emplace_back();
		op.index = index;
		op.origin = GetChunkOrigin(cx, cy, this->scale);
		if (terrain_changed) {
			op.mesh.emplace();
			MeshTerrainChunk(this->capture, this->scale, *op.mesh);
			this->statistics.meshed_chunks++;
		}
		if (sprites_changed) {
			op.sprites = std::make_shared<const TileSpriteCapture>(this->sprites);
			this->statistics.sprite_changes++;
		}
	}

	this->statistics.built_chunks = static_cast<uint>(this->built.size());
	this->statistics.pending_chunks = static_cast<uint>(this->candidates.size() - done);
}

} // namespace fp
