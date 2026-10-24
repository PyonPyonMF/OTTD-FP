/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file fp_splines_mirror.cpp Keeps the spline graph around the camera up to date with the map. */

#include "../../stdafx.h"
#include "fp_splines_mirror.h"
#include "../../landscape.h"

#include "../../safeguards.h"

namespace fp {

/**
 * Forget the graph and start over on a (possibly) different map.
 * @param grid The chunk grid of the map.
 * @param scale The metric scales to build with.
 */
void SplineMirror::Reset(const ChunkGrid &grid, const MetricScale &scale)
{
	this->grid = grid;
	this->scale = scale;
	this->graph.Reset(scale);
	this->states.assign(grid.Count(), ChunkState::Empty);
	this->hashes.assign(grid.Count(), 0);
	this->built.clear();
	this->changed.clear();
	this->overlay_reset = true;
	this->statistics = {};
}

/**
 * Mark chunks whose tiles changed. Chunks that are not in the graph are not affected.
 * @param chunks The changed chunks.
 */
void SplineMirror::MarkDirty(std::span<const ChunkIndex> chunks)
{
	for (ChunkIndex index : chunks) {
		if (index < this->states.size() && this->states[index] == ChunkState::Built) this->states[index] = ChunkState::Dirty;
	}
}

/**
 * Switch the debug overlay on or off.
 * @param enabled Whether to produce the overlay.
 */
void SplineMirror::SetOverlay(bool enabled)
{
	if (enabled == this->overlay) return;
	this->overlay = enabled;
	this->overlay_reset = true;
	if (enabled) this->changed.insert(this->changed.end(), this->built.begin(), this->built.end());
}

/**
 * Bring the graph around the camera up to date; see WorldMirror::Update() for the policy.
 * @param camera Position of the camera, in metres.
 * @param view_distance Radius around the camera that should be in the graph, in metres.
 * @param budget Time that may be spent on capturing chunks.
 * @param[out] overlay_ops Receives the changes of the debug overlay, if it is on.
 * @pre The game state lock is held.
 */
void SplineMirror::Update(const Vec3d &camera, double view_distance, std::chrono::microseconds budget, std::vector<ChunkOp> &overlay_ops)
{
	if (this->grid.Count() == 0) return;

	const double chunk_length = this->scale.TileLength() * CHUNK_SIZE;
	const double keep_distance = view_distance + chunk_length * 2;

	auto distance = [&](ChunkIndex index) {
		double dx = (this->grid.X(index) + 0.5) * chunk_length - camera.x;
		double dy = (this->grid.Y(index) + 0.5) * chunk_length - camera.y;
		return std::hypot(dx, dy);
	};

	/* Drop chunks that are out of range. */
	auto out_of_range = [&](ChunkIndex index) {
		if (distance(index) <= keep_distance) return false;
		this->states[index] = ChunkState::Empty;
		this->graph.RemoveChunk(index, this->changed);
		return true;
	};
	this->built.erase(std::remove_if(this->built.begin(), this->built.end(), out_of_range), this->built.end());

	/* Collect what needs capturing within the view distance. */
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

	/* Capture nearest first, within the budget. */
	auto start = std::chrono::steady_clock::now();
	size_t done = 0;
	for (ChunkIndex index : this->candidates) {
		if (done > 0 && std::chrono::steady_clock::now() - start >= budget) break;
		done++;

		CaptureSplineChunk(this->grid.X(index), this->grid.Y(index), this->capture);
		uint64_t hash = HashSplineChunk(this->capture);
		if (this->states[index] == ChunkState::Dirty && hash == this->hashes[index]) {
			this->states[index] = ChunkState::Built;
			this->statistics.unchanged_chunks++;
			continue;
		}
		this->hashes[index] = hash;

		this->graph.SetChunk(index, this->capture, this->changed);
		if (this->states[index] == ChunkState::Empty) this->built.push_back(index);
		this->states[index] = ChunkState::Built;
		this->statistics.captured_chunks++;
	}

	this->statistics.built_chunks = static_cast<uint>(this->built.size());
	this->statistics.pending_chunks = static_cast<uint>(this->candidates.size() - done);

	this->EmitOverlay(overlay_ops);
}

/**
 * Turn the chunks whose pieces changed into changes of the debug overlay.
 * @param[out] overlay_ops Receives the changes, if the overlay is on.
 */
void SplineMirror::EmitOverlay(std::vector<ChunkOp> &overlay_ops)
{
	if (this->overlay) {
		std::sort(this->changed.begin(), this->changed.end());
		this->changed.erase(std::unique(this->changed.begin(), this->changed.end()), this->changed.end());

		for (ChunkIndex index : this->changed) {
			ChunkOp &op = overlay_ops.emplace_back();
			op.index = index;
			op.origin = GetChunkOrigin(this->grid.X(index), this->grid.Y(index), this->scale);
			if (this->states[index] == ChunkState::Empty) continue;

			op.mesh.emplace();
			this->graph.BuildOverlay(index, op.origin, *op.mesh);
			if (op.mesh->indices.empty()) op.mesh.reset();
		}
	}
	this->changed.clear();
}

/**
 * Compare the graph with the simulation it is derived from.
 *
 * Checks that routes through the graph are continuous and smooth, that the
 * surface model gives the same heights as the game, and how far the smooth
 * curves and profiles depart from the straight, kinked path of the simulation.
 * Road vehicles turn around the corner rather than along the chord, so for
 * roads the distance from the chord is expected to be larger.
 * @return The results.
 * @pre The game state lock is held.
 */
SplineCheck SplineMirror::Check() const
{
	SplineCheck result;
	std::vector<PieceEnd> next;

	for (ChunkIndex index : this->built) {
		this->graph.ForEachPiece(index, [&](PieceID id) {
			const SplineGraph::Piece &piece = this->graph.GetPiece(id);
			result.pieces++;

			/* Continuity with the pieces a route continues on. */
			for (uint8_t end = 0; end < 2; end++) {
				next.clear();
				this->graph.GetNext(id, end, next);
				if (next.empty()) continue;
				const SplineGraph::Port &port = this->graph.GetPort(piece.ports[end]);
				bool crossing = port.side_count[0] > 1 && port.side_count[1] > 1;
				SplinePose here = this->graph.Evaluate(id, end == 0 ? 0.0 : piece.length);
				Vec3d out = end == 1 ? here.tangent : here.tangent * -1.0;
				for (const PieceEnd &other : next) {
					SplinePose there = this->graph.Evaluate(other.piece, other.end == 0 ? 0.0 : this->graph.GetPiece(other.piece).length);
					Vec3d in = other.end == 0 ? there.tangent : there.tangent * -1.0;
					double kink = std::atan2(Length(Cross(out, in)), Dot(out, in));
					result.joints++;
					result.max_gap = std::max(result.max_gap, Length(here.position - there.position));
					if (crossing) {
						result.crossing_joints++;
						result.max_crossing_kink = std::max(result.max_crossing_kink, kink);
					} else {
						result.max_kink = std::max(result.max_kink, kink);
					}
				}
			}

			/* Sidewalks run beside the path of the simulation, and spans above or below the surface. */
			if (piece.key.layer == SplineLayer::Sidewalk || piece.level) return;

			const int tile_x = piece.key.x * TILE_SIZE;
			const int tile_y = piece.key.y * TILE_SIZE;
			for (int i = 1; i < 16; i++) {
				double t = i / 16.0;
				Vec3d chord = Lerp(piece.points[0], piece.points[1], t);
				SplinePose pose = this->graph.Evaluate(id, t * piece.length);
				double &chord_distance = result.max_chord_distance[to_underlying(piece.key.layer)];
				chord_distance = std::max(chord_distance, std::hypot(pose.position.x - chord.x, pose.position.y - chord.y));
				if (piece.key.layer == SplineLayer::Rail && std::abs(pose.curvature) > 0 && 1 / std::abs(pose.curvature) < result.min_rail_radius) {
					result.min_rail_radius = 1 / std::abs(pose.curvature);
					result.tightest_rail = piece.key;
				}

				int ux = std::clamp(static_cast<int>(std::floor(this->scale.MetresToUnits(chord.x))), tile_x, tile_x + static_cast<int>(TILE_SIZE) - 1);
				int uy = std::clamp(static_cast<int>(std::floor(this->scale.MetresToUnits(chord.y))), tile_y, tile_y + static_cast<int>(TILE_SIZE) - 1);
				double model = piece.surface.Height(static_cast<double>(ux - tile_x) / TILE_SIZE, static_cast<double>(uy - tile_y) / TILE_SIZE);
				result.max_surface_error = std::max(result.max_surface_error, std::abs(model - GetSlopePixelZ(ux, uy, true)));

				double fx = std::clamp(this->scale.MetresToUnits(pose.position.x) / TILE_SIZE - piece.key.x, 0.0, 1.0);
				double fy = std::clamp(this->scale.MetresToUnits(pose.position.y) / TILE_SIZE - piece.key.y, 0.0, 1.0);
				double under = this->scale.WorldZToMetres(piece.surface.Height(fx, fy));
				result.max_profile_offset = std::max(result.max_profile_offset, std::abs(pose.position.z - under));
			}
		});
	}
	return result;
}

} // namespace fp
