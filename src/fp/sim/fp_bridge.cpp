/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file fp_bridge.cpp Implementation of the Sim Bridge and of the simulation hooks. */

#include "../../stdafx.h"
#include "fp_bridge.h"
#include "../fp_hooks.h"
#include "../../aircraft.h"
#include "../../debug.h"
#include "../../direction_func.h"
#include "../../ground_vehicle.hpp"
#include "../../map_func.h"
#include "../../vehicle_base.h"

#include "../../safeguards.h"

namespace fp {

/**
 * The bridge.
 * @return The only instance.
 */
/* static */ Bridge &Bridge::Get()
{
	static Bridge instance;
	return instance;
}

/** Start using the bridge; it records changes and publishes snapshots from now on. */
void Bridge::Acquire()
{
	if (this->users++ > 0) return;

	this->StartSession();
	this->active.store(true, std::memory_order_relaxed);
	Debug(Facility::Fp, Severity::Info, "Sim bridge active, session {}", this->session);
}

/** Stop using the bridge; once nobody uses it, it stops recording. */
void Bridge::Release()
{
	assert(this->users > 0);
	if (--this->users > 0) return;

	this->active.store(false, std::memory_order_relaxed);
	Debug(Facility::Fp, Severity::Info, "Sim bridge inactive");
}

/**
 * Set the area whose vehicles end up in the snapshots.
 * @param x X coordinate of the centre, in world units.
 * @param y Y coordinate of the centre, in world units.
 * @param radius Half the side of the square area, in world units.
 */
void Bridge::SetInterest(int x, int y, int radius)
{
	this->interest_x = x;
	this->interest_y = y;
	this->interest_radius = radius;
}

/** Forget everything recorded for the previous map or session. */
void Bridge::StartSession()
{
	this->session++;
	if (this->session == 0) this->session++; // 0 means "no session".

	this->journal.Reset(Map::SizeX(), Map::SizeY());
	this->generations.clear();
}

/** A new map was allocated, e.g. for a new game or a game being loaded. */
void Bridge::OnMapAllocated()
{
	this->world_epoch++;
	if (this->IsActive()) this->StartSession();
}

/**
 * A tile changed in a way that is visible.
 * @param tile The tile.
 */
void Bridge::OnTileDirty(TileIndex tile)
{
	if (!this->IsActive()) return;

	this->journal.MarkTile(TileX(tile), TileY(tile));
	this->statistics.tiles_marked++;
}

/** The simulation finished a tick: publish the state of the vehicles in the area of interest. */
void Bridge::OnTickEnd()
{
	if (!this->IsActive()) return;

	TickSnapshot &snapshot = this->snapshots.Back();
	this->CaptureSnapshot(snapshot);
	this->snapshots.Publish();

	this->statistics.snapshots_published++;
	this->statistics.last_vehicle_count = snapshot.vehicles.size();
}

/**
 * A vehicle is about to be deleted, so its index may be reused.
 * @param index Pool index of the vehicle.
 */
void Bridge::OnVehicleDestroyed(VehicleID index)
{
	if (!this->IsActive()) return;

	if (index.base() >= this->generations.size()) this->generations.resize(index.base() + 1);
	this->generations[index.base()]++;
}

/**
 * Fill a snapshot with the current state.
 * @param[out] snapshot The snapshot to overwrite.
 */
void Bridge::CaptureSnapshot(TickSnapshot &snapshot) const
{
	snapshot.session = this->session;
	snapshot.tick = TimerGameTick::counter;
	snapshot.time = std::chrono::steady_clock::now();
	snapshot.vehicles.clear();

	for (const Vehicle *v : Vehicle::Iterate()) {
		if (v->type >= VehicleType::CompanyEnd) continue; // Effects and disasters.
		if (v->type == VehicleType::Aircraft && !Aircraft::From(v)->IsNormalAircraft()) continue; // Shadows and rotors.
		if (std::abs(v->x_pos - this->interest_x) > this->interest_radius || std::abs(v->y_pos - this->interest_y) > this->interest_radius) continue;
		if (snapshot.vehicles.size() >= MAX_VEHICLE_FRAMES) break;

		/* Progress is kept by the first vehicle of the chain; all parts make their steps together. */
		const Vehicle *first = v->First();
		Direction first_moving = first->GetMovingDirection();

		VehicleFrame &frame = snapshot.vehicles.emplace_back();
		frame.id = v->index;
		frame.generation = v->index.base() < this->generations.size() ? this->generations[v->index.base()] : 0;
		frame.x = v->x_pos;
		frame.y = v->y_pos;
		frame.z = v->z_pos;
		frame.progress = first->progress;
		/* Same as Vehicle::GetAdvanceDistance(), which is not const. */
		frame.advance = IsDiagonalDirection(first_moving) ? TILE_AXIAL_DISTANCE : TILE_CORNER_DISTANCE * 2;
		frame.length = v->IsGroundVehicle() ? v->GetGroundVehicleCache()->cached_veh_length : 0;
		frame.type = v->type;
		frame.direction = v->direction;
		frame.moving_direction = v->GetMovingDirection();
		if (first == v) frame.flags.Set(VehicleFrameFlag::Front);
		if (v->vehstatus.Test(VehState::Hidden)) frame.flags.Set(VehicleFrameFlag::Hidden);
		if (v->vehstatus.Test(VehState::Crashed)) frame.flags.Set(VehicleFrameFlag::Crashed);
		if (v->type == VehicleType::Aircraft && v->subtype == AIR_HELICOPTER) frame.flags.Set(VehicleFrameFlag::Helicopter);
	}
}

/* Hooks called by the core; see fp_hooks.h. */

void OnMapAllocated()
{
	Bridge::Get().OnMapAllocated();
}

void OnTileDirty(TileIndex tile)
{
	Bridge::Get().OnTileDirty(tile);
}

void OnTickEnd()
{
	Bridge::Get().OnTickEnd();
}

void OnVehicleDestroyed(VehicleID index)
{
	Bridge::Get().OnVehicleDestroyed(index);
}

} // namespace fp
