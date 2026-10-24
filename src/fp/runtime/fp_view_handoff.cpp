/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file fp_view_handoff.cpp Hand-over of view state from the runtime update to the renderer. */

#include "../../stdafx.h"
#include "fp_view_handoff.h"

#include "../../safeguards.h"

namespace fp {

/**
 * Publish the state of a frame.
 * @param frame The view state; replaces the previous one.
 * @param ops Chunk changes; appended to those not yet taken.
 * @param reset_chunks Whether the renderer must drop all its chunks first.
 */
void ViewHandoff::Publish(const ViewFrame &frame, std::vector<ChunkOp> &&ops, bool reset_chunks)
{
	std::lock_guard<std::mutex> guard(this->lock);

	this->frame = frame;
	if (reset_chunks) {
		this->ops.clear();
		this->reset_chunks = true;
	}
	std::move(ops.begin(), ops.end(), std::back_inserter(this->ops));
	ops.clear();
	this->fresh = true;
}

/**
 * Publish changes of the debug overlay. The overlay is drawn over the
 * terrain, so changes only take effect together with a frame.
 * @param ops Overlay changes; appended to those not yet taken.
 * @param reset Whether the renderer must drop all overlay meshes first.
 */
void ViewHandoff::PublishOverlay(std::vector<ChunkOp> &&ops, bool reset)
{
	std::lock_guard<std::mutex> guard(this->lock);

	if (reset) {
		this->overlay_ops.clear();
		this->reset_overlay = true;
	}
	std::move(ops.begin(), ops.end(), std::back_inserter(this->overlay_ops));
	ops.clear();
}

/** There is no view anymore; the renderer should release its resources. */
void ViewHandoff::Deactivate()
{
	std::lock_guard<std::mutex> guard(this->lock);

	this->frame = {};
	this->ops.clear();
	this->reset_chunks = true;
	this->overlay_ops.clear();
	this->reset_overlay = true;
	this->fresh = true;
}

/**
 * Take what was published.
 * @param[out] update Receives the latest view state and the queued chunk changes.
 * @return True iff anything was published since the last call.
 */
bool ViewHandoff::Take(ViewUpdate &update)
{
	std::lock_guard<std::mutex> guard(this->lock);

	if (!this->fresh) return false;

	update.frame = this->frame;
	update.reset_chunks = std::exchange(this->reset_chunks, false);
	update.ops.clear();
	update.ops.swap(this->ops);
	update.reset_overlay = std::exchange(this->reset_overlay, false);
	update.overlay_ops.clear();
	update.overlay_ops.swap(this->overlay_ops);
	this->fresh = false;
	return true;
}

/**
 * Report the state of the renderer.
 * @param status The state.
 */
void ViewHandoff::SetRendererStatus(const RendererStatus &status)
{
	std::lock_guard<std::mutex> guard(this->lock);
	this->status = status;
}

/**
 * Latest state of the renderer.
 * @return The state.
 */
RendererStatus ViewHandoff::GetRendererStatus()
{
	std::lock_guard<std::mutex> guard(this->lock);
	return this->status;
}

/** The renderer lost all its chunks, e.g. because the OpenGL context went away. */
void ViewHandoff::ReportContextLost()
{
	std::lock_guard<std::mutex> guard(this->lock);
	this->context_lost = true;
}

/**
 * Check, and clear, whether the renderer lost its chunks.
 * @return True iff all chunks must be sent again.
 */
bool ViewHandoff::TakeContextLost()
{
	std::lock_guard<std::mutex> guard(this->lock);
	return std::exchange(this->context_lost, false);
}

} // namespace fp
