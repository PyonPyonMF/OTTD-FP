/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file fp_runtime.cpp The first-person runtime: ties the bridge, the world mirror and the roles together. */

#include "../../stdafx.h"
#include "fp_runtime.h"
#include "../sim/fp_bridge.h"
#include "../../landscape.h"
#include "../../map_func.h"

#include "../../safeguards.h"

namespace fp {

/**
 * The runtime.
 * @return The only instance.
 */
/* static */ Runtime &Runtime::Get()
{
	static Runtime instance;
	return instance;
}

/**
 * Open the first-person view. The camera starts behind and above a point,
 * looking at it towards the top of the 2D view, so the 3D view shows what
 * the 2D view showed.
 * @param x X coordinate of the point to look at, in world units.
 * @param y Y coordinate of the point to look at, in world units.
 */
void Runtime::Activate(int x, int y)
{
	if (this->active) return;

	this->active = true;
	Bridge::Get().Acquire();
	this->ResetWorld();

	const double distance = 450.0;
	const double height = 220.0;
	const double yaw = -0.75 * std::numbers::pi; // Towards the north corner of the map, i.e. up in the 2D view.

	CameraPose pose;
	pose.yaw = yaw;
	pose.pitch = -std::atan2(height, distance);
	pose.fov_y = 60.0 * std::numbers::pi / 180.0;
	pose.position.x = this->scale.UnitsToMetres(x) - std::cos(yaw) * distance;
	pose.position.y = this->scale.UnitsToMetres(y) - std::sin(yaw) * distance;
	pose.position.z = this->GroundHeight(this->scale.UnitsToMetres(x), this->scale.UnitsToMetres(y)) + height;
	this->spectator.Place(pose);
}

/** Close the first-person view and let go of everything. */
void Runtime::Deactivate()
{
	if (!this->active) return;

	this->active = false;
	Bridge::Get().Release();
	this->mirror.Reset({}, this->scale);
	this->handoff.Deactivate();
}

/** Start building the world from scratch, for the current map and bridge session. */
void Runtime::ResetWorld()
{
	this->session = Bridge::Get().GetSession();
	this->scale = MetricScale{};
	this->mirror.Reset(ChunkGrid(Map::SizeX(), Map::SizeY()), this->scale);
	this->spectator.SetBounds(this->scale.TileLength() * Map::SizeX(), this->scale.TileLength() * Map::SizeY());
	this->reset_pending = true;
}

/**
 * Height of the ground, including foundations, at a position.
 * @param x X coordinate, in metres.
 * @param y Y coordinate, in metres.
 * @return The height, in metres.
 */
double Runtime::GroundHeight(double x, double y) const
{
	int ux = static_cast<int>(std::floor(this->scale.MetresToUnits(x)));
	int uy = static_cast<int>(std::floor(this->scale.MetresToUnits(y)));
	return this->scale.WorldZToMetres(GetSlopePixelZOutsideMap(ux, uy));
}

/**
 * Advance one frame.
 * @param input The input actions of this frame.
 * @param delta_ms Time since the previous frame, in milliseconds.
 * @param view_rect Screen rectangle of the 3D view.
 * @param visible Parts of \a view_rect not covered by other windows.
 */
void Runtime::Update(const InputState &input, uint delta_ms, const Rect &view_rect, std::vector<Rect> &&visible)
{
	if (!this->active) return;

	Bridge &bridge = Bridge::Get();
	if (bridge.GetSession() != this->session || this->handoff.TakeContextLost()) this->ResetWorld();

	/* Long frames, e.g. while the game was busy loading, should not throw the camera across the map. */
	double dt = std::min(delta_ms / 1000.0, 0.1);
	this->spectator.Update(input, dt, [this](double x, double y) { return this->GroundHeight(x, y); });
	const CameraPose camera = this->spectator.GetCamera();

	bridge.SetInterest(
			static_cast<int>(this->scale.MetresToUnits(camera.position.x)),
			static_cast<int>(this->scale.MetresToUnits(camera.position.y)),
			static_cast<int>(this->scale.MetresToUnits(VIEW_DISTANCE)));

	this->dirty.clear();
	bridge.TakeDirtyChunks(this->dirty);
	this->mirror.MarkDirty(this->dirty);
	this->mirror.Update(camera.position, VIEW_DISTANCE, MESH_BUDGET, this->ops);

	ViewFrame frame;
	frame.active = true;
	frame.session = this->session;
	frame.camera = camera;
	frame.scale = this->scale;
	frame.view_distance = VIEW_DISTANCE;
	frame.view_rect = view_rect;
	frame.visible = std::move(visible);
	this->handoff.Publish(frame, std::move(this->ops), std::exchange(this->reset_pending, false));
}

} // namespace fp
