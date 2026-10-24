/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file cab_view_ride.cpp Camera of the 3D view riding along with a vehicle, and the pace of drawing the 3D view. */

#include "stdafx.h"
#include "cab_view_ride.h"
#include "vehicle_base.h"
#include "train.h"
#include "roadveh.h"
#include "road_map.h"
#include "ship.h"
#include "landscape.h"
#include "bridge_map.h"
#include "rail_map.h"
#include "tunnel_map.h"
#include "map_func.h"
#include "settings_type.h"
#include "window_gui.h"
#include "gfx_func.h"
#include "debug.h"

#include "safeguards.h"

/** Height of the eye of the 3D view above the rails: the windows of the cab, well below the decks of bridges and the tops of tunnels. */
static constexpr float CAB_VIEW_3D_EYE = 4.0f;
/** Height of the eye of the 3D view above the road, at the windscreen of a bus or lorry; a bit lower than in a train. */
static constexpr float CAB_VIEW_3D_ROAD_EYE = 3.0f;
/** How far the 3D view tilts with the grade of the track: halfway, as ramps and slopes are steep; all the way, it would look into the sky at the top of a ramp. */
static constexpr float CAB_VIEW_3D_TILT = 0.5f;
/** Distance (in world coordinates) over which the 3D view turns most of the way into a new direction of travel. */
static constexpr float CAB_VIEW_3D_TURN_DIST = 6.0f;
/** Distance (in world coordinates) over which the 3D view from a road vehicle follows most of a turn; they turn more sharply than trains. */
static constexpr float CAB_VIEW_3D_ROAD_TURN_DIST = 3.0f;
/** Distance (in world coordinates) over which the 3D view follows most of a change in height, which on slopes comes in steps. */
static constexpr float CAB_VIEW_3D_CLIMB_DIST = 1.5f;
/** Height of the eye of the 3D view above the position of an aircraft; on the runway, about that of the windows of a train cab. */
static constexpr float CAB_VIEW_3D_AIRCRAFT_EYE = 3.0f;
/** How far ahead (in world coordinates) the 3D view from a flying aircraft looks at the ground: the higher it flies, the more it looks down. */
static constexpr float CAB_VIEW_3D_AIRCRAFT_LOOK_DIST = 20.0f * TILE_SIZE;
/** How much (in radians) the 3D view from a flying aircraft looks down at most, on top of the slight look down of the 3D view. */
static constexpr float CAB_VIEW_3D_AIRCRAFT_MAX_LOOK_DOWN = 0.5f;
/** How much (in radians) climbing and descending tilt the 3D view from an aircraft at most; aircraft climb and descend steeply in the game. */
static constexpr float CAB_VIEW_3D_AIRCRAFT_MAX_TILT = 0.2f;
/** Distance (in world coordinates) over which the height of the ground below an aircraft is followed, so hills below don't make the view nod. */
static constexpr float CAB_VIEW_3D_AIRCRAFT_GROUND_DIST = 2.0f * TILE_SIZE;
/** How far (in world coordinates) in front of its position the eye of the 3D view from a ship is, at the front of its bridge. */
static constexpr float CAB_VIEW_3D_SHIP_AHEAD = 3.0f;
/** Distance (in world coordinates) a ship turning on the spot or rising in a lock counts as moving each game tick, so the view follows it. */
static constexpr float CAB_VIEW_3D_SHIP_STILL_STEP = 0.2f;
/**
 * How far (in game ticks) the moment the 3D view shows stays behind the game when a game tick happens.
 * Game ticks come late when drawing takes long, so the view needs some ticks to move smoothly between.
 */
static constexpr float CAB_VIEW_3D_LAG = 1.5f;
/**
 * Part of the time between two redraws of the screen that making a picture of the 3D view may take.
 * When it takes longer, the picture is made smaller, and when even that is not enough, the view is drawn
 * only on every second (third...) redraw, so the pace stays even and the game and the rest of the interface
 * keep enough time.
 */
static constexpr float CAB_VIEW_3D_FRAME_BUDGET = 0.65f;
/** Redraws of the screen per picture of the 3D view at most. */
static constexpr int CAB_VIEW_3D_MAX_FRAME_DIVIDER = 4;
/** How long drawing has to be too slow or fast enough before the pace of the 3D view changes, so it does not keep changing. */
static constexpr std::chrono::milliseconds CAB_VIEW_3D_PACE_DELAY{1500};
/** How far (in radians) the head turns when the mouse moves a pixel while looking around. */
static constexpr float CAB_VIEW_LOOK_SENSITIVITY = 0.004f;
/** How far (in radians) the head turns up or down at most. */
static constexpr float CAB_VIEW_LOOK_MAX_PITCH = 1.2f;
/** Time after which the 3D view is redrawn even when the camera did not move. */
static constexpr std::chrono::milliseconds CAB_VIEW_3D_IDLE_FRAME_TIME{1000};

/**
 * Get the position of a vehicle including how far it got towards its next step.
 * Vehicles move in whole steps, a train at 90 km/h only every third game tick or so,
 * which would make the 3D view jerk forward.
 * @param v The vehicle.
 * @return X and Y position in world coordinates.
 */
std::pair<float, float> GetCabView3DExactPosition(const Vehicle *v)
{
	/* The progress collects the speed until there is enough for a step, see GroundVehicle::DoUpdateSpeed(),
	 * and for aircraft 256 make a step, see UpdateAircraftSpeed(). */
	Direction dir = v->GetMovingDirection();
	uint step = (v->type == VehicleType::Aircraft) ? 256 : IsDiagonalDirection(dir) ? TILE_AXIAL_DISTANCE : TILE_CORNER_DISTANCE * 2; // As Vehicle::GetAdvanceDistance().
	/* The progress is kept by the first vehicle of the chain, also when it drives backwards. */
	float part = std::min<float>(v->First()->progress, step) / step;
	TileIndexDiffC diff = TileIndexDiffCByDir(dir);
	return {v->x_pos + diff.x * part, v->y_pos + diff.y * part};
}

/**
 * Get the height of the track of a train or the road of a road vehicle at a point near it.
 * The height of a vehicle is that of its middle, which on slopes and ramps is lower or higher than its front.
 * @param v The vehicle.
 * @param x X coordinate of the point.
 * @param y Y coordinate of the point.
 * @return The height of the track.
 */
static float GetTrackHeight(const Vehicle *v, float x, float y)
{
	auto height = [v](int x, int y) {
		if (!IsInsideBS(x, 0, Map::SizeX() * TILE_SIZE) || !IsInsideBS(y, 0, Map::SizeY() * TILE_SIZE)) return v->z_pos;
		TileIndex tile = TileVirtXY(x, y);
		/* On a bridge, the track is as high as the bridge, up to the ramp at its other end. */
		if (IsBridgeAbove(tile)) {
			int bridge = GetBridgePixelHeight(GetSouthernBridgeEnd(tile));
			if (v->z_pos > bridge - static_cast<int>(TILE_HEIGHT)) return bridge;
		}
		/* Beyond the end of the line or the road, or the entrance of a tunnel, keep the height of the vehicle. */
		if (v->type == VehicleType::Road) {
			if (!MayHaveRoad(tile) || (!HasTileRoadType(tile, RoadTramType::Road) && !HasTileRoadType(tile, RoadTramType::Tram))) return v->z_pos;
		} else if (GetTileRailType(tile) == INVALID_RAILTYPE) {
			return v->z_pos;
		}
		return GetSlopePixelZ(x, y, true);
	};
	/* Heights are in whole units; blend those of the points around, so the eye does not climb in steps. */
	int ix = static_cast<int>(std::floor(x));
	int iy = static_cast<int>(std::floor(y));
	float fx = x - ix;
	float fy = y - iy;
	return (height(ix, iy) * (1 - fx) + height(ix + 1, iy) * fx) * (1 - fy) + (height(ix, iy + 1) * (1 - fx) + height(ix + 1, iy + 1) * fx) * fy;
}

/**
 * Place the camera of the 3D view of a train at the windows of its front, and of a road vehicle on its path;
 * or, for the seat in the middle, at its track or road in the middle of the vehicle.
 * The height eases with the distance travelled, so the view follows the track or road over slopes at any speed.
 * @param v The vehicle of the train or road vehicle the camera is on.
 * @param seat Where the camera is on the vehicle.
 * @param camera The camera, with its direction already set.
 * @param x X position of the middle of the vehicle.
 * @param y Y position of the middle of the vehicle.
 * @param moved Distance the vehicle moved since the last game tick.
 * @param jump Place the camera at once, without easing.
 */
void CabViewRide::PlaceGroundCamera(const Vehicle *v, CabViewSeat seat, CabViewCamera &camera, float x, float y, float moved, bool jump)
{
	/* The position of a vehicle is its middle; move the camera to the front. Road vehicles turn
	 * sharply at junctions, where the front would swing off the road; they keep to their path. */
	const bool road = v->type == VehicleType::Road;
	float half_length = (road ? RoadVehicle::From(v)->gcache.cached_veh_length : Train::From(v)->gcache.cached_veh_length) / 2.0f;
	float front = (road || seat != CabViewSeat::Cab) ? 0.0f : half_length;
	camera.x = x + std::cos(this->yaw) * front;
	camera.y = y + std::sin(this->yaw) * front;
	float track = GetTrackHeight(v, camera.x, camera.y);
	if (jump) {
		this->height = track;
	} else {
		this->height += (track - this->height) * (1.0f - std::exp(-moved / CAB_VIEW_3D_CLIMB_DIST));
	}
	camera.z = this->height;
	if (seat == CabViewSeat::Cab) camera.z += road ? CAB_VIEW_3D_ROAD_EYE : CAB_VIEW_3D_EYE;
	/* The view tilts with the track around the front, so on ramps and slopes the track ahead does not rise like a wall. */
	float ahead = GetTrackHeight(v, camera.x + std::cos(this->yaw) * half_length, camera.y + std::sin(this->yaw) * half_length);
	float behind = GetTrackHeight(v, camera.x - std::cos(this->yaw) * half_length, camera.y - std::sin(this->yaw) * half_length);
	camera.pitch = CAB_VIEW_3D_TILT * std::atan2(ahead - behind, 2 * half_length);
}

/**
 * Place the camera of the 3D view of a ship at the front of its bridge, above the water, see #GetCabView3DShipWaterZ.
 * The height eases, so the view rises and sinks smoothly in locks, where the ship goes up a unit every 8 game ticks.
 * @param v The ship.
 * @param camera The camera, with its direction already set.
 * @param x X position of the middle of the ship.
 * @param y Y position of the middle of the ship.
 * @param moved Distance the ship moved since the last game tick.
 * @param jump Place the camera at once, without easing.
 */
void CabViewRide::PlaceShipCamera(const Vehicle *v, CabViewCamera &camera, float x, float y, float moved, bool jump)
{
	camera.x = x + std::cos(this->yaw) * CAB_VIEW_3D_SHIP_AHEAD;
	camera.y = y + std::sin(this->yaw) * CAB_VIEW_3D_SHIP_AHEAD;
	float z = GetCabView3DShipWaterZ(v);
	if (jump) {
		this->height = z;
	} else {
		this->height += (z - this->height) * (1.0f - std::exp(-std::max(moved, CAB_VIEW_3D_SHIP_STILL_STEP) / CAB_VIEW_3D_CLIMB_DIST));
	}
	camera.z = this->height + CAB_VIEW_3D_SHIP_EYE;
}

/**
 * Place the camera of the 3D view of an aircraft at its windows, at the height it flies in the game.
 * The higher it flies, the more the view looks down at the ground; climbing and descending tilt it a bit.
 * @param v The aircraft.
 * @param camera The camera, with its position across the ground and direction already set.
 * @param moved Distance the aircraft moved across the ground since the last game tick.
 * @param jump Place the camera at once, without easing.
 */
void CabViewRide::PlaceAircraftCamera(const Vehicle *v, CabViewCamera &camera, float moved, bool jump)
{
	float z = static_cast<float>(v->z_pos);
	float ground = static_cast<float>(GetSlopePixelZOutsideMap(static_cast<int>(camera.x), static_cast<int>(camera.y)));
	/* Helicopters rise and land straight up and down, without moving across the ground. */
	float climb = moved > 0.5f ? std::atan2(z - this->aircraft_z, moved) : 0.0f;
	this->aircraft_z = z;
	if (jump) {
		this->height = ground;
		this->climb = 0;
	} else {
		this->height += (ground - this->height) * (1.0f - std::exp(-moved / CAB_VIEW_3D_AIRCRAFT_GROUND_DIST));
		this->climb += (climb - this->climb) * (1.0f - std::exp(-std::max(moved, 0.5f) / CAB_VIEW_3D_TURN_DIST));
	}

	camera.z = z + CAB_VIEW_3D_AIRCRAFT_EYE;
	/* On the runway the eye is as high as in a train, which does not count as flying. */
	camera.altitude = std::max(camera.z - this->height - CAB_VIEW_3D_EYE, 0.0f);
	float look_down = std::min(std::atan2(camera.altitude, CAB_VIEW_3D_AIRCRAFT_LOOK_DIST), CAB_VIEW_3D_AIRCRAFT_MAX_LOOK_DOWN);
	camera.pitch = std::clamp(CAB_VIEW_3D_TILT * this->climb, -CAB_VIEW_3D_AIRCRAFT_MAX_TILT, CAB_VIEW_3D_AIRCRAFT_MAX_TILT) - look_down;
}

/**
 * Place the camera of the 3D view for the current game tick: in the cab of the train, road vehicle, ship or aircraft,
 * or in the middle of a vehicle of a train or a road vehicle.
 * @param v The vehicle the camera is on; of a train or an articulated road vehicle the part, like the one at the moving front.
 * @param seat Where on the vehicle the camera is.
 * @param jump Place the camera at once, without easing or moving smoothly from where it was.
 */
void CabViewRide::Tick(const Vehicle *v, CabViewSeat seat, bool jump)
{
	auto [x, y] = GetCabView3DExactPosition(v);
	float moved = std::hypot(x - this->last_x, y - this->last_y);
	this->last_x = x;
	this->last_y = y;
	/* Moving to another vehicle, like the other end of a train that reverses, starts afresh. */
	jump |= v->index != this->last_vehicle;
	this->last_vehicle = v->index;

	/* Ships turn on the spot to a new direction bit by bit; the view follows how the ship is turned. */
	const bool ship = v->type == VehicleType::Ship;
	TileIndexDiffC diff = TileIndexDiffCByDir(ship ? Ship::From(v)->rotation : v->GetMovingDirection());
	float target = std::atan2(static_cast<float>(diff.y), static_cast<float>(diff.x));
	/* Roads run along the axes of the tiles; a road vehicle facing north, east, south or west is halfway through
	 * a turn. Look along the road it turns into, as a driver does, instead of at the corner of the block. */
	if (v->type == VehicleType::Road && seat == CabViewSeat::Cab) {
		if (IsDiagonalDirection(v->direction)) {
			this->road_yaw = target;
		} else {
			target += std::remainder(target - this->road_yaw, 2.0f * static_cast<float>(M_PI));
		}
	}
	float turn = std::remainder(target - this->yaw, 2.0f * static_cast<float>(M_PI));
	/* In a tunnel the view goes on along it; there is nothing to see from a depot or hangar. */
	const bool aircraft = v->type == VehicleType::Aircraft;
	const bool tunnel = !aircraft && IsTileType(v->tile, TileType::TunnelBridge) && IsTunnel(v->tile);
	const bool dark = v->vehstatus.Test(VehState::Hidden) && !tunnel;
	/* Jump when starting, when the vehicle reverses, or when it moved too far to follow. */
	jump |= this->tick_cameras.empty() || std::abs(turn) > 0.75f * static_cast<float>(M_PI) || moved > 2 * TILE_SIZE || dark != this->tick_cameras.back().camera.dark;
	if (jump) {
		this->yaw = target;
	} else {
		float turned = ship ? std::max(moved, 4 * CAB_VIEW_3D_SHIP_STILL_STEP) : moved;
		this->yaw += turn * (1.0f - std::exp(-turned / (v->type == VehicleType::Road ? CAB_VIEW_3D_ROAD_TURN_DIST : CAB_VIEW_3D_TURN_DIST)));
	}

	CabViewCamera camera;
	camera.dark = dark;
	camera.tunnel = tunnel ? v->tile : INVALID_TILE;
	camera.yaw = this->yaw;
	camera.vehicle = v->index;
	if (aircraft) {
		camera.x = x;
		camera.y = y;
		this->PlaceAircraftCamera(v, camera, moved, jump);
	} else if (ship) {
		this->PlaceShipCamera(v, camera, x, y, moved, jump);
	} else {
		this->PlaceGroundCamera(v, seat, camera, x, y, moved, jump);
	}

	auto now = std::chrono::steady_clock::now();
	if (jump) {
		this->tick_cameras.clear();
	} else {
		/* Ticks often come late or in pairs when drawing took long, so only the long-term average counts. Skip pauses. */
		float ms = std::chrono::duration<float, std::milli>(now - this->tick_time).count();
		if (ms < 4 * MILLISECONDS_PER_TICK) this->tick_interval += (ms - this->tick_interval) * 0.02f;
	}
	this->tick_time = now;
	this->tick_count++;
	this->tick_cameras.push_back({this->tick_count, camera});
	if (this->tick_cameras.size() > 8) this->tick_cameras.pop_front();
	if (jump) {
		this->view_tick = static_cast<double>(this->tick_count) - CAB_VIEW_3D_LAG;
		this->view_lag = CAB_VIEW_3D_LAG;
		this->clock_time = now;
	} else {
		/* How far behind the view is now; see Advance for what is done with it. */
		double view_now = this->view_tick + std::chrono::duration<double, std::milli>(now - this->clock_time).count() / this->tick_interval * this->GetClockRate();
		this->view_lag += (static_cast<float>(this->tick_count - view_now) - this->view_lag) * 0.05f;
	}
}

/**
 * Get how fast the moment the 3D view shows runs compared to the game: a bit faster when it got behind
 * more than intended, a bit slower when it got too close to the game.
 * @return The factor for the speed of the game.
 */
float CabViewRide::GetClockRate() const
{
	return 1.0f + std::clamp((this->view_lag - CAB_VIEW_3D_LAG) * 0.05f, -0.1f, 0.1f);
}

/**
 * Let the moment the 3D view shows run on evenly, a bit behind the game.
 * Game ticks and redraws of the screen come at different rates and ticks often come late or in pairs,
 * so following the ticks directly would make the view jerk. Instead the view runs with the clock,
 * and only its speed is adjusted, slightly, so it stays the intended distance behind the game.
 */
void CabViewRide::Advance()
{
	auto now = std::chrono::steady_clock::now();
	double ms = std::chrono::duration<double, std::milli>(now - this->clock_time).count();
	this->clock_time = now;

	this->view_tick += ms / std::max(this->tick_interval, 1.0f) * this->GetClockRate();
	/* Never run ahead of the game; catch up at once when far behind, e.g. after the game was very busy. */
	this->view_tick = std::min<double>(this->view_tick, this->tick_count);
	if (this->tick_count - this->view_tick > 4 * CAB_VIEW_3D_LAG) {
		this->view_tick = static_cast<double>(this->tick_count) - CAB_VIEW_3D_LAG;
		this->view_lag = CAB_VIEW_3D_LAG;
	}
}

/**
 * Get the camera of the 3D view at the moment it shows, between the cameras of the game ticks around it.
 * @return The camera.
 */
CabViewCamera CabViewRide::Get() const
{
	if (this->tick_cameras.empty()) return {};
	auto next = std::ranges::find_if(this->tick_cameras, [this](const TickCamera &tc) { return tc.tick > this->view_tick; });
	if (next == this->tick_cameras.begin()) return this->tick_cameras.front().camera;
	if (next == this->tick_cameras.end()) return this->tick_cameras.back().camera;

	const CabViewCamera &from = std::prev(next)->camera;
	const CabViewCamera &to = next->camera;
	float f = static_cast<float>((this->view_tick - std::prev(next)->tick) / (next->tick - std::prev(next)->tick));
	CabViewCamera camera = to;
	camera.x = from.x + (to.x - from.x) * f;
	camera.y = from.y + (to.y - from.y) * f;
	camera.z = from.z + (to.z - from.z) * f;
	camera.yaw = from.yaw + std::remainder(to.yaw - from.yaw, 2.0f * static_cast<float>(M_PI)) * f;
	camera.pitch = from.pitch + (to.pitch - from.pitch) * f;
	camera.altitude = from.altitude + (to.altitude - from.altitude) * f;
	return camera;
}

/**
 * Decide, on a redraw of the screen, whether to make a new picture of the 3D view, and how long making it may take.
 * Slow drawing delays the redraws, so the measured time between them is only trusted when it is shorter than intended.
 * @param camera The camera the 3D view would show now.
 * @return Whether to make a new picture with that camera.
 */
bool CabView3DPacer::Update(const CabViewCamera &camera)
{
	/* The time is measured here, as the time a window gets for a realtime tick is rounded down and would make the pace uneven. */
	auto now = std::chrono::steady_clock::now();
	float ms = std::chrono::duration<float, std::milli>(now - this->redraw_time).count();
	this->redraw_time = now;
	if (ms < 100) this->redraw_interval += (ms - this->redraw_interval) * 0.05f;

	float interval = std::min(this->redraw_interval, 1000.0f / std::max<int>(_settings_client.gui.refresh_rate, 1));
	float budget = interval * CAB_VIEW_3D_FRAME_BUDGET;
	float cost = GetCabView3DFrameTime(CAB_VIEW_3D_MIN_SCALE);
	int divider = this->frame_divider;
	if (cost > budget * divider) {
		divider = std::min(divider + 1, CAB_VIEW_3D_MAX_FRAME_DIVIDER);
	} else if (cost < budget * (divider - 1) * 0.75f) {
		divider--;
	}
	if (divider == this->frame_divider) {
		this->pace_time = now;
	} else if (now - this->pace_time >= CAB_VIEW_3D_PACE_DELAY) {
		Debug(Facility::Misc, Severity::Notice, "[CabView3D] picture on every {}. redraw: drawing takes at least {:.1f} ms, redraws every {:.1f} ms", divider, cost, interval);
		this->frame_divider = divider;
		this->pace_time = now;
	}
	this->frame_budget = budget * this->frame_divider;

	/* Redraw the 3D view when the camera moved; when standing still, still now and then, as the world around may change. */
	this->redraws_since_frame++;
	if ((this->redraws_since_frame >= this->frame_divider && camera != this->drawn_camera) || now - this->frame_time >= CAB_VIEW_3D_IDLE_FRAME_TIME) {
		this->frame_time = now;
		this->redraws_since_frame = 0;
		this->drawn_camera = camera;
		this->frame_id++;
		return true;
	}
	return false;
}

/**
 * Show a camera at once, e.g. when the 3D view is switched on.
 * @param camera The camera.
 */
void CabView3DPacer::Reset(const CabViewCamera &camera)
{
	this->drawn_camera = camera;
	this->frame_id++;
}

/**
 * Whether a mouse button turns the head: the one that scrolls the map.
 * @param right_button Whether it is the right mouse button, else the left one.
 * @return True when that button turns the head.
 */
/* static */ bool CabViewLook::IsLookButton(bool right_button)
{
	return right_button != (_settings_client.gui.scroll_mode == ViewportScrollMode::MapLMB);
}

/**
 * Start turning the head while the mouse button that scrolls the map is held, see #Move.
 * The window system then calls Window::OnScroll() of the window with the movement of the mouse.
 * @param w The window with the 3D view.
 * @param right_button Whether the right mouse button was pressed, else the left one.
 * @return Whether looking around started.
 */
/* static */ bool CabViewLook::Start(Window *w, bool right_button)
{
	if (!IsLookButton(right_button) || _scrolling_viewport) return false;

	/* Right clicks do not focus windows, but looking around means the view is in use. */
	SetFocusedWindow(w);
	_scrolling_viewport = true;
	_cursor.fix_at = _settings_client.gui.scroll_mode != ViewportScrollMode::MapRMB && _settings_client.gui.scroll_mode != ViewportScrollMode::MapLMB;
	return true;
}

/**
 * Turn the head with the movement of the mouse, see Window::OnScroll().
 * @param delta The movement as the window system gives it for scrolling.
 * @param max_yaw How far the head turns to either side at most, in radians; from #M_PI on all the way round.
 */
void CabViewLook::Move(Point delta, float max_yaw)
{
	/* HandleViewportScroll() inverts the movement of the mouse except in this mode; undo that. */
	const int sign = _settings_client.gui.scroll_mode == ViewportScrollMode::ViewportRMBFixed ? 1 : -1;
	this->yaw -= sign * delta.x * CAB_VIEW_LOOK_SENSITIVITY;
	this->pitch = std::clamp(this->pitch - sign * delta.y * CAB_VIEW_LOOK_SENSITIVITY, -CAB_VIEW_LOOK_MAX_PITCH, CAB_VIEW_LOOK_MAX_PITCH);
	if (max_yaw < static_cast<float>(M_PI)) {
		this->yaw = std::clamp(this->yaw, -max_yaw, max_yaw);
	} else {
		this->yaw = std::remainder(this->yaw, 2.0f * static_cast<float>(M_PI));
	}
}
