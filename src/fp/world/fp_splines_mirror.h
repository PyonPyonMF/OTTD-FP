/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file fp_splines_mirror.h Keeps the spline graph around the camera up to date with the map. */

#ifndef FP_SPLINES_MIRROR_H
#define FP_SPLINES_MIRROR_H

#include <chrono>
#include <utility>

#include "fp_splines.h"
#include "fp_world_mirror.h"

namespace fp {

/** Result of comparing the spline graph with the simulation, see SplineMirror::Check(). */
struct SplineCheck {
	uint pieces = 0; ///< Pieces checked.
	uint joints = 0; ///< Pairs of pieces checked where a route continues from one to the other.
	uint crossing_joints = 0; ///< Of #joints, those at ports with several pieces on both sides, where a route between branches may keep a kink.
	double max_gap = 0; ///< Largest distance between the ends of two connected pieces, in metres.
	double max_kink = 0; ///< Largest angle between the tangents of two connected pieces, in radians, except at crossings.
	double max_crossing_kink = 0; ///< Largest angle between the tangents of two connected pieces at crossings, in radians.
	double max_surface_error = 0; ///< Largest difference between the surface model and GetSlopePixelZ(), in world z units.
	std::array<double, to_underlying(SplineLayer::End)> max_chord_distance{}; ///< Per layer: largest horizontal distance between a curve and the straight chord of its piece at the same fraction, in metres.
	double min_rail_radius = std::numeric_limits<double>::infinity(); ///< Tightest radius of a rail curve, in metres.
	PieceKey tightest_rail{}; ///< Piece with the tightest rail curve.
	double max_profile_offset = 0; ///< Largest height of a vertical profile above or below the surface under it, in metres.
};

/**
 * Keeps the spline graph around the camera up to date, like the World Mirror
 * does with the terrain: chunks are captured nearest first within a time
 * budget, rebuilt when their tracks or roads change, and dropped when the
 * camera moves away.
 *
 * It can also produce a debug overlay: one mesh per chunk with a ribbon
 * along every piece, for the renderer to draw over the terrain.
 *
 * All members are used with the game state lock held.
 */
class SplineMirror {
public:
	/** Counters for diagnostics. */
	struct Statistics {
		uint built_chunks = 0; ///< Chunks currently in the graph.
		uint pending_chunks = 0; ///< Chunks in view that still need to be captured.
		uint64_t captured_chunks = 0; ///< Chunks put into the graph since the last reset.
		uint64_t unchanged_chunks = 0; ///< Dirty chunks whose tracks and roads turned out not to have changed, since the last reset.
	};

	void Reset(const ChunkGrid &grid, const MetricScale &scale);
	void MarkDirty(std::span<const ChunkIndex> chunks);
	void Update(const Vec3d &camera, double view_distance, std::chrono::microseconds budget, std::vector<ChunkOp> &overlay_ops);

	void SetOverlay(bool enabled);

	/**
	 * Whether the debug overlay is produced.
	 * @return True iff the overlay is on.
	 */
	bool HasOverlay() const { return this->overlay; }

	/**
	 * Check, and clear, whether the renderer must drop all overlay meshes before applying new ones.
	 * @return True iff the overlay must be reset.
	 */
	bool TakeOverlayReset() { return std::exchange(this->overlay_reset, false); }

	/**
	 * The spline graph.
	 * @return The graph.
	 */
	const SplineGraph &GetGraph() const { return this->graph; }

	/**
	 * Diagnostic counters.
	 * @return The counters.
	 */
	const Statistics &GetStatistics() const { return this->statistics; }

	SplineCheck Check() const;

private:
	/** State of one chunk. */
	enum class ChunkState : uint8_t {
		Empty, ///< Not in the graph.
		Built, ///< In the graph and up to date.
		Dirty, ///< In the graph, but the tiles changed since.
	};

	ChunkGrid grid{}; ///< The chunks of the map.
	MetricScale scale{}; ///< The metric scales.
	SplineGraph graph; ///< The graph.
	std::vector<ChunkState> states; ///< State per chunk.
	std::vector<uint64_t> hashes; ///< Per chunk: fingerprint of the data it was built from, see HashSplineChunk().
	std::vector<ChunkIndex> built; ///< Chunks that are in the graph.
	std::vector<ChunkIndex> candidates; ///< Scratch list of chunks to capture.
	std::vector<ChunkIndex> changed; ///< Scratch list of chunks whose pieces changed.
	SplineChunkData capture; ///< Scratch copy of a chunk.
	bool overlay = false; ///< Whether the debug overlay is produced.
	bool overlay_reset = false; ///< Whether the renderer must drop all overlay meshes.
	Statistics statistics; ///< Diagnostic counters.

	void EmitOverlay(std::vector<ChunkOp> &overlay_ops);
};

} // namespace fp

#endif /* FP_SPLINES_MIRROR_H */
