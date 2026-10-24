/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file fp_runtime.h The first-person runtime: ties the bridge, the world mirror and the roles together. */

#ifndef FP_RUNTIME_H
#define FP_RUNTIME_H

#include <utility>

#include "../roles/fp_spectator.h"
#include "../world/fp_splines_mirror.h"
#include "fp_view_handoff.h"

namespace fp {

/**
 * The FP Runtime (docs/fp/architecture.md, section 3).
 *
 * Every frame it advances the active role, brings the world mirror up to
 * date and publishes the result for the renderer. It is driven by the
 * first-person window and does nothing while no such window is open.
 *
 * All members are used with the game state lock held, except #GetHandoff().
 */
class Runtime {
public:
	static constexpr double VIEW_DISTANCE = 6000.0; ///< Radius around the camera in which the world is built, in metres.
	static constexpr std::chrono::microseconds MESH_BUDGET{3000}; ///< Time per frame that may be spent on meshing chunks.
	static constexpr std::chrono::microseconds SPLINE_BUDGET{1500}; ///< Time per frame that may be spent on capturing chunks for the spline graph.

	static Runtime &Get();

	void Activate(int x, int y);
	void Deactivate();
	void LookAt(int x, int y, double distance, double height, double yaw);

	/**
	 * Whether a first-person view is open.
	 * @return True iff the runtime is active.
	 */
	bool IsActive() const { return this->active; }

	void Update(const InputState &input, uint delta_ms, const Rect &view_rect, std::vector<Rect> &&visible);

	/**
	 * The spectator role.
	 * @return The spectator.
	 */
	const Spectator &GetSpectator() const { return this->spectator; }

	/**
	 * The world mirror.
	 * @return The world mirror.
	 */
	const WorldMirror &GetWorldMirror() const { return this->mirror; }

	/**
	 * The spline graph of tracks, roads and sidewalks, with the chunks it covers.
	 * @return The spline mirror.
	 */
	const SplineMirror &GetSplines() const { return this->splines; }

	/**
	 * Switch the debug overlay of the spline graph on or off.
	 * @param enabled Whether to draw the overlay.
	 */
	void SetDebugSplines(bool enabled) { this->splines.SetOverlay(enabled); }

	/**
	 * The metric scales in use.
	 * @return The scales.
	 */
	const MetricScale &GetScale() const { return this->scale; }

	/**
	 * Hand-over to the renderer; may be used without the game state lock.
	 * @return The hand-over.
	 */
	ViewHandoff &GetHandoff() { return this->handoff; }

private:
	bool active = false; ///< Whether a first-person view is open.
	uint32_t session = 0; ///< Bridge session the world mirror was built for.
	bool reset_pending = false; ///< Whether the renderer still has to drop its chunks.
	MetricScale scale{}; ///< The metric scales.
	Spectator spectator; ///< The spectator role.
	WorldMirror mirror; ///< The world mirror.
	SplineMirror splines; ///< The spline graph.
	ViewHandoff handoff; ///< Hand-over to the renderer.
	std::vector<ChunkIndex> dirty; ///< Scratch list of changed chunks.
	std::vector<ChunkOp> ops; ///< Scratch list of chunk changes.
	std::vector<ChunkOp> overlay_ops; ///< Scratch list of changes of the debug overlay.

	void ResetWorld();
	double GroundHeight(double x, double y) const;
};

} // namespace fp

#endif /* FP_RUNTIME_H */
