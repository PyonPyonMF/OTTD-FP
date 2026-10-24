/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file cab_view_ride.h Camera of the 3D view riding along with a vehicle, and the pace of drawing the 3D view. */

#ifndef CAB_VIEW_RIDE_H
#define CAB_VIEW_RIDE_H

#include "cab_view_3d.h"
#include "window_type.h"

#include <chrono>
#include <deque>

std::pair<float, float> GetCabView3DExactPosition(const Vehicle *v);

/** Where on a vehicle the camera of the 3D view rides. */
enum class CabViewSeat : uint8_t {
	Cab, ///< At the windows of the cab, at the front of the vehicle in the direction it moves.
	Middle, ///< In the middle of the vehicle, at the height of its track or road; the camera is the frame of the inside of the vehicle.
};

/**
 * The camera of the 3D view riding along with a vehicle.
 * It is placed on the vehicle every game tick; the direction and height ease with the distance travelled
 * instead of with time, so the view follows the track through curves and over slopes at any speed.
 * The moment the view shows runs evenly with the clock, a bit behind the game, between the cameras of the
 * game ticks around it, as game ticks and redraws of the screen come at different rates.
 */
class CabViewRide {
public:
	void Tick(const Vehicle *v, CabViewSeat seat, bool jump);
	void Advance();
	CabViewCamera Get() const;

	/** Whether the camera was not placed yet since it was reset. */
	bool IsEmpty() const { return this->tick_cameras.empty(); }
	/** Number of game ticks the camera was placed at. */
	uint32_t GetTickCount() const { return this->tick_count; }
	/** The moment the view shows, in game ticks, see #GetTickCount. */
	double GetViewTick() const { return this->view_tick; }

private:
	float yaw = 0; ///< Direction of view, eased towards the direction of travel.
	float height = 0; ///< Height of the vehicle, eased so steps on slopes don't make it jump; for an aircraft, of the ground below it.
	float road_yaw = 0; ///< Direction of a road vehicle along the road before it started to turn.
	float climb = 0; ///< Angle at which an aircraft climbs or descends, eased.
	float aircraft_z = 0; ///< Height of an aircraft at the last game tick.
	float last_x = 0; ///< X position of the vehicle at the last game tick, see #GetExactPosition.
	float last_y = 0; ///< Y position of the vehicle at the last game tick, see #GetExactPosition.
	VehicleID last_vehicle = VehicleID::Invalid(); ///< The vehicle the camera rode on at the last game tick.

	/** Camera of the 3D view at a game tick. */
	struct TickCamera {
		uint32_t tick; ///< Number of the game tick, counted by this ride.
		CabViewCamera camera; ///< The camera at that tick.
	};
	std::deque<TickCamera> tick_cameras; ///< Cameras of the last few game ticks, oldest first.
	uint32_t tick_count = 0; ///< Number of game ticks the camera was placed at.
	double view_tick = 0; ///< The moment the 3D view shows, in game ticks; runs evenly a bit behind the game.
	float tick_interval = MILLISECONDS_PER_TICK; ///< Average time (in milliseconds) between game ticks.
	float view_lag = 0; ///< Average of how far (in game ticks) #view_tick is behind when a game tick happens.
	std::chrono::steady_clock::time_point tick_time; ///< When the last game tick happened.
	std::chrono::steady_clock::time_point clock_time; ///< When #view_tick was last advanced.

	void PlaceGroundCamera(const Vehicle *v, CabViewSeat seat, CabViewCamera &camera, float x, float y, float moved, bool jump);
	void PlaceShipCamera(const Vehicle *v, CabViewCamera &camera, float x, float y, float moved, bool jump);
	void PlaceAircraftCamera(const Vehicle *v, CabViewCamera &camera, float moved, bool jump);
	float GetClockRate() const;
};

/**
 * Where someone in the 3D view looks, away from the direction of the camera: turning the head with the mouse.
 * The mouse button that scrolls the map turns the head, so looking around feels like moving the map.
 */
class CabViewLook {
public:
	float yaw = 0; ///< How far the head is turned to the left, in radians.
	float pitch = 0; ///< How far the head is turned up, in radians.

	static bool IsLookButton(bool right_button);
	static bool Start(Window *w, bool right_button);
	void Move(Point delta, float max_yaw);

	/** Whether the head is not turned. */
	bool IsAhead() const { return this->yaw == 0 && this->pitch == 0; }
	/** Turn the head straight ahead again. */
	void Reset() { this->yaw = this->pitch = 0; }

	/**
	 * Turn a camera with the head.
	 * @param camera The camera, looking straight ahead.
	 * @return The camera, looking where the head is turned to.
	 */
	CabViewCamera Apply(CabViewCamera camera) const
	{
		camera.yaw += this->yaw;
		camera.pitch += this->pitch;
		return camera;
	}
};

/**
 * When to make a new picture of the 3D view, and how long making it may take.
 * Pictures are made on every redraw of the screen when that can be done quickly enough, as fewer pictures
 * than the screen shows look jerky; if needed, the picture is made smaller for that. Otherwise they are made
 * on every second (third...) redraw, evenly, so the game and the rest of the interface keep enough time.
 */
class CabView3DPacer {
public:
	bool Update(const CabViewCamera &camera);
	void Reset(const CabViewCamera &camera);

	/** Time (in milliseconds) making a picture of the 3D view may take. */
	float GetBudget() const { return this->frame_budget; }
	/** Number of the last picture, see #DrawCabView3D. */
	uint32_t GetFrameId() const { return this->frame_id; }
	/** Camera of the last picture. */
	const CabViewCamera &GetDrawnCamera() const { return this->drawn_camera; }

private:
	std::chrono::steady_clock::time_point frame_time; ///< When the last picture was made.
	std::chrono::steady_clock::time_point redraw_time; ///< When the screen was last redrawn, see #Update.
	float redraw_interval = 1000.0f / 60; ///< Average time (in milliseconds) between redraws of the screen.
	int frame_divider = 1; ///< Redraws of the screen per picture of the 3D view.
	float frame_budget = 0; ///< Time (in milliseconds) making a picture of the 3D view may take.
	int redraws_since_frame = 0; ///< Redraws of the screen since the last picture of the 3D view.
	std::chrono::steady_clock::time_point pace_time; ///< When #frame_divider was last right for how long drawing takes.
	CabViewCamera drawn_camera; ///< Camera of the last picture of the 3D view.
	uint32_t frame_id = 0; ///< Number of the last picture of the 3D view.
};

#endif /* CAB_VIEW_RIDE_H */
