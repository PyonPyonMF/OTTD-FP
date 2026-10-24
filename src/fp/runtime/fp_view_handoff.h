/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file fp_view_handoff.h Hand-over of view state from the runtime update to the renderer. */

#ifndef FP_VIEW_HANDOFF_H
#define FP_VIEW_HANDOFF_H

#include <mutex>
#include <utility>

#include "../../core/geometry_type.hpp"
#include "../roles/fp_role.h"
#include "../world/fp_world_mirror.h"

namespace fp {

/** Everything the renderer needs to draw the view. */
struct ViewFrame {
	bool active = false; ///< Whether there is a view to draw at all.
	uint32_t session = 0; ///< Bridge session; snapshots of other sessions are ignored.
	CameraPose camera{}; ///< The camera.
	MetricScale scale{}; ///< The metric scales.
	double view_distance = 0; ///< Distance up to which the world is built, in metres.
	Rect view_rect{}; ///< Screen rectangle of the 3D view.
	std::vector<Rect> visible; ///< Parts of #view_rect not covered by other windows.
};

/** Everything published since the renderer last took it. */
struct ViewUpdate {
	ViewFrame frame{}; ///< The latest view state.
	bool reset_chunks = false; ///< Whether the renderer must drop all chunks before applying #ops.
	std::vector<ChunkOp> ops; ///< Chunk changes, to apply in order.
	bool reset_overlay = false; ///< Whether the renderer must drop all overlay meshes before applying #overlay_ops.
	std::vector<ChunkOp> overlay_ops; ///< Changes of the debug overlay, e.g. of the spline graph; meshes per chunk like #ops.
};

/** What the renderer reports back. */
struct RendererStatus {
	bool running = false; ///< Whether a renderer drew a frame recently.
	bool failed = false; ///< Whether the renderer could not start.
	std::string info; ///< OpenGL renderer and version, or why the renderer failed.
	uint chunks = 0; ///< Chunks on the GPU.
	uint overlay_chunks = 0; ///< Chunks of the debug overlay on the GPU.
	uint vehicles = 0; ///< Vehicles drawn in the last frame.
	float frame_ms = 0; ///< Time spent rendering the last frame, in milliseconds.
};

/**
 * Hand-over between the runtime update, which runs with the game state lock
 * held, and the renderer, which runs in the OpenGL back-end without it.
 *
 * The view state is replaced on every publish, while chunk changes queue up
 * until the renderer takes them, in order. A reset drops queued changes,
 * as they belong to the previous world.
 */
class ViewHandoff {
public:
	void Publish(const ViewFrame &frame, std::vector<ChunkOp> &&ops, bool reset_chunks);
	void PublishOverlay(std::vector<ChunkOp> &&ops, bool reset);
	void Deactivate();
	bool Take(ViewUpdate &update);

	void SetRendererStatus(const RendererStatus &status);
	RendererStatus GetRendererStatus();
	void ReportContextLost();
	bool TakeContextLost();

private:
	std::mutex lock; ///< Guards all members.
	ViewFrame frame{}; ///< Latest view state.
	std::vector<ChunkOp> ops; ///< Chunk changes not yet taken.
	bool reset_chunks = false; ///< Whether the renderer must drop all chunks before applying #ops.
	std::vector<ChunkOp> overlay_ops; ///< Overlay changes not yet taken.
	bool reset_overlay = false; ///< Whether the renderer must drop all overlay meshes before applying #overlay_ops.
	bool fresh = false; ///< Whether anything was published since the last take.
	bool context_lost = false; ///< Whether the renderer lost its chunks, so all must be sent again.
	RendererStatus status{}; ///< Latest status of the renderer.
};

} // namespace fp

#endif /* FP_VIEW_HANDOFF_H */
