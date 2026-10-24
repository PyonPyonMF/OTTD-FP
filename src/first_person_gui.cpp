/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/**
 * @file first_person_gui.cpp The first-person view: walking around in the 3D view, and riding along inside vehicles.
 *
 * On foot one walks over the ground as the 3D view shows it: up and down slopes, onto the platforms of stations,
 * but not into water, through the walls of buildings as the view shows them, or through trains and road vehicles,
 * which push one aside when they come by. Next to a train or road vehicle that stands still, one can get in: into the
 * cab, which opens the cab view, or into a coach, bus or mail van, which is drawn from the inside around one. Inside,
 * one walks along the aisle and through the doors to the next coaches, sits down by a window, and gets off again
 * when the vehicle stands still.
 *
 * Nothing of this changes the game: the vehicles go on as they would, and they do not know about anyone walking.
 */

#include "stdafx.h"
#include "first_person_gui.h"
#include "cab_view_ride.h"
#include "window_gui.h"
#include "window_func.h"
#include "viewport_func.h"
#include "vehicle_base.h"
#include "vehicle_gui.h"
#include "train.h"
#include "roadveh.h"
#include "cargotype.h"
#include "town.h"
#include "map_func.h"
#include "company_func.h"
#include "strings_func.h"
#include "gfx_func.h"
#include "core/geometry_func.hpp"
#include "console_func.h"
#include "console_internal.h"
#include "network/network.h"
#include "openttd.h"
#include "debug.h"

#include "3rdparty/fmt/format.h"

#include "widgets/first_person_widget.h"

#include "table/strings.h"

#include "safeguards.h"

/** Walking speed, in world units per second; the 3D view draws a tile about as wide as ten metres, so about 2 m/s. */
static constexpr float FP_WALK_SPEED = 3.0f;
/** Running speed, while Shift is held. */
static constexpr float FP_RUN_SPEED = 9.0f;
/** Height of the eye above what one stands on, about 1.7 m. */
static constexpr float FP_EYE = 2.7f;
/** Height of the eye above the floor, sitting. */
static constexpr float FP_SEATED_EYE = 2.0f;
/** How much room one needs around oneself. */
static constexpr float FP_RADIUS = 0.5f;
/** Highest step one walks up, like onto a platform; not up the sides of foundations. */
static constexpr float FP_STEP = 1.6f;
/** Longest step made at once, so nothing is walked through between two checks. */
static constexpr float FP_MAX_STEP = 0.3f;
/** Time (in milliseconds) over which the eye follows most of a change in the height of what one stands on. */
static constexpr float FP_CLIMB_MS = 90.0f;
/** How close to a vehicle one has to be to get in. */
static constexpr float FP_REACH = 1.5f;
/** How far beside a vehicle one gets off. */
static constexpr float FP_GET_OFF_DISTANCE = 1.0f;
/** How far (in tiles) around oneself vehicles are looked at, for being in the way and for getting in. */
static constexpr int FP_VEHICLE_TILES = 3;
/** Height of the floor of trains above the rails. */
static constexpr float FP_TRAIN_FLOOR = 1.0f;
/** Height of the floor of road vehicles above the road. */
static constexpr float FP_ROAD_FLOOR = 0.5f;
/** Thickness of the walls inside vehicles, as the 3D view draws them. */
static constexpr float FP_WALL = 0.12f;
/** Half the width of the aisle between the seats, as the 3D view draws it, less some room. */
static constexpr float FP_AISLE = 0.3f;
/** Half the width of the doors between coaches, as the 3D view draws them, less some room. */
static constexpr float FP_DOOR = 0.4f;
/** How far one keeps from the end walls inside vehicles. */
static constexpr float FP_END_ROOM = 0.45f;
/** Distance between the rows of seats, as the 3D view draws them. */
static constexpr float FP_SEAT_PITCH = 2.0f;
/** Half the width of the aisle between the seats, as the 3D view draws it. */
static constexpr float FP_AISLE_HALF_WIDTH = 0.45f;
/** How far (in radians) one looks up when getting into a vehicle; a bit down, at the seats along the aisle. */
static constexpr float FP_INSIDE_PITCH = -0.2f;
/** How often (in milliseconds) the status and the buttons are brought up to date. */
static constexpr uint FP_STATUS_REFRESH_MS = 250;
/** How long (in milliseconds) a message in the status stays. */
static constexpr uint FP_MESSAGE_MS = 2500;

/**
 * Get the direction a vehicle points to, the way the 3D view draws it.
 * @param dir The direction of the vehicle.
 * @return The angle in radians; 0 points towards +x, pi/2 towards +y.
 */
static float GetHeading(Direction dir)
{
	TileIndexDiffC diff = TileIndexDiffCByDir(dir);
	return std::atan2(static_cast<float>(diff.y), static_cast<float>(diff.x));
}

/**
 * What is inside a part of a train or road vehicle, for riding in it.
 * @param v The vehicle part.
 * @return What its inside looks like; #CabViewInteriorKind::None when one cannot ride in it.
 */
static CabViewInteriorKind GetInteriorKind(const Vehicle *v)
{
	if (v->cargo_cap == 0 || !IsValidCargoType(v->cargo_type)) return CabViewInteriorKind::None;
	if (IsCargoInClass(v->cargo_type, CargoClass::Passengers)) return CabViewInteriorKind::Coach;
	if (IsCargoInClass(v->cargo_type, CargoClass::Mail)) return CabViewInteriorKind::Mail;
	return CabViewInteriorKind::None;
}

/**
 * Whether a part of a train belongs to an engine, which has a cab: also the rear head of a dual-headed engine,
 * which may be at the front of a train that drives backwards, and the other parts of an articulated engine.
 * @param t The part.
 * @return True for a part of an engine.
 */
static bool IsEnginePart(const Train *t)
{
	if (t->IsArticulatedPart()) t = t->GetFirstEnginePart();
	return t->IsEngine() || t->IsMultiheaded();
}

/** A train or road vehicle near someone walking, as the box the 3D view draws it as. */
struct NearVehicle {
	const Vehicle *v; ///< The vehicle part.
	float x; ///< X position of its middle.
	float y; ///< Y position of its middle.
	float z; ///< Height of its track or road.
	float cos; ///< Cosine of the direction it points to.
	float sin; ///< Sine of the direction it points to.
	CabViewVehicleBox box; ///< Its size.

	/**
	 * Get a point in the coordinates of the vehicle.
	 * @param px X position of the point.
	 * @param py Y position of the point.
	 * @return How far the point is along the vehicle from its middle, and across it to the left.
	 */
	std::pair<float, float> ToLocal(float px, float py) const
	{
		const float dx = px - this->x, dy = py - this->y;
		return {dx * this->cos + dy * this->sin, -dx * this->sin + dy * this->cos};
	}

	/**
	 * Get how far a point is from the box.
	 * @param px X position of the point.
	 * @param py Y position of the point.
	 * @return The distance; 0 inside.
	 */
	float Distance(float px, float py) const
	{
		auto [along, across] = this->ToLocal(px, py);
		return std::hypot(std::max(std::abs(along) - this->box.length / 2, 0.0f), std::max(std::abs(across) - this->box.width / 2, 0.0f));
	}
};

/** What someone in the first-person view is. */
enum class FirstPersonRole : uint8_t {
	Walker, ///< On foot.
	Passenger, ///< Inside a vehicle.
};

static constexpr std::initializer_list<NWidgetPart> _nested_first_person_widgets = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, Colours::Grey),
		NWidget(WWT_CAPTION, Colours::Grey, WID_FP_CAPTION),
		NWidget(WWT_SHADEBOX, Colours::Grey),
		NWidget(WWT_DEFSIZEBOX, Colours::Grey),
		NWidget(WWT_STICKYBOX, Colours::Grey),
	EndContainer(),
	NWidget(WWT_PANEL, Colours::Grey),
		NWidget(WWT_INSET, Colours::Grey), SetPadding(2, 2, 2, 2),
			NWidget(WWT_EMPTY, Colours::Invalid, WID_FP_VIEW), SetMinimalSize(320, 180), SetResize(1, 1), SetFill(1, 1),
		EndContainer(),
	EndContainer(),
	NWidget(WWT_PANEL, Colours::Grey, WID_FP_STATUS), SetResize(1, 0), SetFill(1, 0), EndContainer(),
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_PUSHTXTBTN, Colours::Grey, WID_FP_GET_IN), SetFill(1, 1), SetResize(1, 0), SetStringTip(STR_FP_GET_IN, STR_FP_GET_IN_TOOLTIP),
		NWidget(WWT_PUSHTXTBTN, Colours::Grey, WID_FP_SIT), SetFill(1, 1), SetResize(1, 0), SetStringTip(STR_FP_SIT, STR_FP_SIT_TOOLTIP),
		NWidget(WWT_RESIZEBOX, Colours::Grey),
	EndContainer(),
};

/** Window with the 3D view of someone walking around, or riding inside a vehicle. */
struct FirstPersonWindow : Window {
private:
	FirstPersonRole role = FirstPersonRole::Walker; ///< What one is.
	CabViewLook head; ///< Where one looks: on foot, as seen from the north; inside a vehicle, compared to the front of it.
	CabView3DPacer pacer; ///< When to make a new picture of the 3D view.

	/* On foot. */
	float x = 0; ///< X position.
	float y = 0; ///< Y position.
	float ground = 0; ///< Height of what one stands on.
	float z = 0; ///< Height of the feet, following #ground smoothly.
	std::vector<NearVehicle> near_vehicles; ///< The vehicles around, see #UpdateNearVehicles.
	VehicleID nearby = VehicleID::Invalid(); ///< The vehicle one is next to, to get in.

	/* Inside a vehicle. */
	VehicleID vehicle = VehicleID::Invalid(); ///< The vehicle part one is in.
	CabViewRide ride; ///< The frame of that vehicle, riding along with it.
	CabViewInterior interior; ///< What its inside looks like, without its position.
	float along = 0; ///< Where one is in it, along it from its middle.
	float across = 0; ///< Where one is in it, across it to the left from its middle.
	bool seated = false; ///< Whether one sits.

	bool use_key = false; ///< Whether the key to get in or off was held at the last realtime tick.
	bool use_pressed = false; ///< Whether the key to get in or off was pressed since the last realtime tick.
	uint status_timer = 0; ///< Milliseconds since the status was last brought up to date.
	StringID message = INVALID_STRING_ID; ///< A message to show in the status, like why one cannot get off.
	uint message_timer = 0; ///< Milliseconds the message is shown for yet.

	/**
	 * Whether this window gets the keys for walking: when it is in focus, or when no window is.
	 * @return True when it does.
	 */
	bool HasKeyFocus() const
	{
		return (_focused_window == nullptr || _focused_window == this) && !EditBoxInGlobalFocus();
	}

	/**
	 * Find the trains and road vehicles around, as the boxes the 3D view draws them as.
	 * @param px X position to look around.
	 * @param py Y position to look around.
	 */
	void UpdateNearVehicles(float px, float py)
	{
		this->near_vehicles.clear();
		const float reach = FP_VEHICLE_TILES * TILE_SIZE;
		for (const Vehicle *v : Vehicle::Iterate()) {
			if (v->type != VehicleType::Train && v->type != VehicleType::Road) continue;
			if (v->vehstatus.Test(VehState::Hidden)) continue;
			if (std::abs(v->x_pos - px) > reach || std::abs(v->y_pos - py) > reach) continue;
			std::optional<CabViewVehicleBox> box = GetCabView3DVehicleBox(v);
			if (!box.has_value()) continue;
			const float heading = GetHeading(v->direction);
			this->near_vehicles.push_back({v, static_cast<float>(v->x_pos), static_cast<float>(v->y_pos), static_cast<float>(v->z_pos), std::cos(heading), std::sin(heading), *box});
		}
	}

	/**
	 * Get the vehicle that is in the way at a point.
	 * @param px X position of the point.
	 * @param py Y position of the point.
	 * @param pz Height of the feet.
	 * @return The vehicle, or nullptr when none is.
	 */
	const NearVehicle *GetVehicleInTheWay(float px, float py, float pz) const
	{
		for (const NearVehicle &nv : this->near_vehicles) {
			/* Vehicles on a bridge above, or below one standing on the bridge, are not in the way. */
			if (nv.z > pz + FP_EYE || nv.z + nv.box.height < pz) continue;
			if (nv.Distance(px, py) < FP_RADIUS) return &nv;
		}
		return nullptr;
	}

	/**
	 * Whether one can step from where one is to a point nearby.
	 * Someone stuck, like after being pushed into a hedge, can step anywhere one could stand, to get out.
	 * @param px X position of the point.
	 * @param py Y position of the point.
	 * @param stuck Whether one is stuck where one is.
	 * @return True when one can.
	 */
	bool CanStepTo(float px, float py, bool stuck) const
	{
		CabViewWalkSurface surface = GetCabView3DWalkSurface(px, py, stuck ? 0.0f : FP_RADIUS);
		return surface.walkable && (stuck || (surface.height <= this->ground + FP_STEP && this->GetVehicleInTheWay(px, py, surface.height) == nullptr));
	}

	/**
	 * Find a place nearby where one can stand.
	 * @param px X position to start looking.
	 * @param py Y position to start looking.
	 * @return The place, or std::nullopt when there is none within a few tiles.
	 */
	std::optional<std::pair<float, float>> FindRoom(float px, float py) const
	{
		for (float r = 0; r <= 4 * TILE_SIZE; r += FP_RADIUS) {
			const int points = std::max(1, static_cast<int>(2 * M_PI * r / FP_RADIUS));
			for (int i = 0; i < points; i++) {
				const float a = 2 * static_cast<float>(M_PI) * i / points;
				const float qx = px + r * std::cos(a), qy = py + r * std::sin(a);
				CabViewWalkSurface surface = GetCabView3DWalkSurface(qx, qy, FP_RADIUS);
				if (surface.walkable && this->GetVehicleInTheWay(qx, qy, surface.height) == nullptr) return std::pair{qx, qy};
			}
		}
		return std::nullopt;
	}

	/**
	 * Walk a bit on foot, sliding along walls, in small steps so nothing is walked through.
	 * @param dx Distance along the x axis.
	 * @param dy Distance along the y axis.
	 */
	void Move(float dx, float dy)
	{
		const int steps = std::max(1, static_cast<int>(std::ceil(std::hypot(dx, dy) / FP_MAX_STEP)));
		dx /= steps;
		dy /= steps;
		for (int i = 0; i < steps; i++) {
			const bool stuck = !GetCabView3DWalkSurface(this->x, this->y, FP_RADIUS).walkable;
			if (this->CanStepTo(this->x + dx, this->y + dy, stuck)) {
				this->x += dx;
				this->y += dy;
			} else if (dx != 0 && this->CanStepTo(this->x + dx, this->y, stuck)) {
				this->x += dx;
			} else if (dy != 0 && this->CanStepTo(this->x, this->y + dy, stuck)) {
				this->y += dy;
			} else {
				break;
			}
			this->ground = GetCabView3DWalkSurface(this->x, this->y, 0).height;
		}
	}

	/** Get out of the way of a vehicle that comes by: it does not know about anyone walking, so it pushes one aside. */
	void GetPushed()
	{
		for (int tries = 0; tries < 4; tries++) {
			const NearVehicle *nv = this->GetVehicleInTheWay(this->x, this->y, this->z);
			if (nv == nullptr) return;
			auto [a, c] = nv->ToLocal(this->x, this->y);
			const float out_along = nv->box.length / 2 + FP_RADIUS + 0.05f - std::abs(a);
			const float out_across = nv->box.width / 2 + FP_RADIUS + 0.05f - std::abs(c);
			if (out_across <= out_along) {
				c += c < 0 ? -out_across : out_across;
			} else {
				a += a < 0 ? -out_along : out_along;
			}
			this->x = nv->x + a * nv->cos - c * nv->sin;
			this->y = nv->y + a * nv->sin + c * nv->cos;
			this->ground = GetCabView3DWalkSurface(this->x, this->y, 0).height;
		}
	}

	/**
	 * Walk on foot for a while.
	 * @param ms How long, in milliseconds.
	 * @param forward How much to walk forward, from -1 to 1.
	 * @param right How much to walk to the right, from -1 to 1.
	 */
	void Walk(float ms, float forward, float right)
	{
		this->UpdateNearVehicles(this->x, this->y);
		this->GetPushed();
		const float length = std::hypot(forward, right);
		if (length > 0) {
			const float distance = (_shift_pressed ? FP_RUN_SPEED : FP_WALK_SPEED) * ms / 1000 / std::max(length, 1.0f);
			const float yaw = this->head.yaw;
			this->Move((std::cos(yaw) * forward + std::sin(yaw) * right) * distance, (std::sin(yaw) * forward - std::cos(yaw) * right) * distance);
		}
		this->z += (this->ground - this->z) * (1.0f - std::exp(-ms / FP_CLIMB_MS));

		/* The vehicle one could get into. */
		this->nearby = VehicleID::Invalid();
		float best = FP_REACH;
		for (const NearVehicle &nv : this->near_vehicles) {
			if (nv.z > this->z + FP_EYE || nv.z + nv.box.height < this->z) continue;
			float d = nv.Distance(this->x, this->y);
			if (d < best) {
				best = d;
				this->nearby = nv.v->index;
			}
		}
	}

	/**
	 * Walk inside the vehicle for a while: along the aisle and through the doors to the next vehicles.
	 * @param ms How long, in milliseconds.
	 * @param forward How much to walk forward, from -1 to 1.
	 * @param right How much to walk to the right, from -1 to 1.
	 */
	void WalkInside(float ms, float forward, float right)
	{
		const float length = std::hypot(forward, right);
		if (this->seated || length == 0) return;
		const float distance = (_shift_pressed ? FP_RUN_SPEED : FP_WALK_SPEED) * ms / 1000 / std::max(length, 1.0f);
		const float yaw = this->head.yaw;
		this->along += (std::cos(yaw) * forward + std::sin(yaw) * right) * distance;
		this->across += (std::sin(yaw) * forward - std::cos(yaw) * right) * distance;

		const float side = this->interior.kind == CabViewInteriorKind::Coach ? FP_AISLE : this->interior.width / 2 - FP_WALL - 0.8f;
		this->across = std::clamp(this->across, -side, side);
		const float end = this->interior.length / 2 - FP_WALL - FP_END_ROOM;
		if (std::abs(this->along) <= end) return;

		/* Through the door at the end into the next vehicle, when there is one and one is in the door. */
		const Vehicle *v = Vehicle::Get(this->vehicle);
		const bool front = this->along > 0;
		const Vehicle *next = front ? v->GetMovingPrev() : v->GetMovingNext();
		if ((front ? this->interior.gangway_front : this->interior.gangway_back) && next != nullptr && std::abs(this->across) <= FP_DOOR) {
			std::optional<CabViewVehicleBox> box = GetCabView3DVehicleBox(next);
			if (box.has_value()) {
				const float past = std::abs(this->along) - end;
				const float next_end = box->length / 2 - FP_WALL - FP_END_ROOM;
				this->Board(next, front ? -next_end + past : next_end - past, this->across, this->head.yaw);
				return;
			}
		}
		this->along = std::clamp(this->along, -end, end);
	}

	/**
	 * Update what the inside of the vehicle one is in looks like, as it may change, like when it is refitted.
	 * @param v The vehicle part one is in.
	 */
	void UpdateInterior(const Vehicle *v)
	{
		std::optional<CabViewVehicleBox> box = GetCabView3DVehicleBox(v);
		if (box.has_value()) {
			this->interior.length = box->length;
			this->interior.width = box->width;
			this->interior.height = box->height;
		}
		this->interior.kind = GetInteriorKind(v);
		if (this->interior.kind == CabViewInteriorKind::None) this->interior.kind = CabViewInteriorKind::Mail;
		this->interior.floor = v->type == VehicleType::Train ? FP_TRAIN_FLOOR : FP_ROAD_FLOOR;
		auto passage = [](const Vehicle *u) { return u != nullptr && GetInteriorKind(u) != CabViewInteriorKind::None; };
		this->interior.gangway_front = passage(v->GetMovingPrev());
		this->interior.gangway_back = passage(v->GetMovingNext());
		this->interior.windscreen = v->type == VehicleType::Road;
		this->interior.colour = _company_colours[v->owner];
	}

	/**
	 * Get into a vehicle part, or move into the next one.
	 * @param v The vehicle part.
	 * @param along Where one is in it, along it from its middle.
	 * @param across Where one is in it, across it to the left from its middle.
	 * @param yaw Where one looks, compared to the front of the vehicle.
	 */
	void Board(const Vehicle *v, float along, float across, float yaw)
	{
		this->role = FirstPersonRole::Passenger;
		this->vehicle = v->index;
		this->along = along;
		this->across = across;
		this->seated = false;
		this->head.yaw = std::remainder(yaw, 2.0f * static_cast<float>(M_PI));
		/* Standing inside, one looks a bit down, at the seats along the aisle. */
		this->head.pitch = FP_INSIDE_PITCH;
		this->UpdateInterior(v);
		this->ride = {};
		this->ride.Tick(v, CabViewSeat::Middle, true);
		this->pacer.Reset(this->GetCamera());
		this->UpdateButtons();
		this->SetDirty();
	}

	/**
	 * Whether getting into a vehicle part at a point next to it is getting into its cab: of an engine, at the ends of
	 * an engine that also carries passengers, and at the front of a road vehicle, like a lorry or the driver's seat of a bus.
	 * @param v The vehicle part.
	 * @param along Where along the vehicle the point is, from its middle, in the direction the vehicle points to.
	 * @param length The length of the vehicle.
	 * @return True for the cab.
	 */
	static bool IsCabAt(const Vehicle *v, float along, float length)
	{
		const bool seats = GetInteriorKind(v) != CabViewInteriorKind::None;
		if (v->type == VehicleType::Train) return IsEnginePart(Train::From(v)) && (!seats || std::abs(along) > length / 4);
		return !seats || (v->IsPrimaryVehicle() && along > length / 4);
	}

	/**
	 * Get into the vehicle one stands next to: into the cab, or into a coach, bus or mail van.
	 * @return Whether this window closed, as the cab view took its place.
	 */
	bool GetIn()
	{
		const Vehicle *v = Vehicle::GetIfValid(this->nearby);
		if (v == nullptr) return false;
		if (v->First()->cur_speed != 0) {
			this->ShowMessage(STR_FP_MESSAGE_WAIT_UNTIL_STANDING);
			return false;
		}
		auto it = std::ranges::find_if(this->near_vehicles, [v](const NearVehicle &nv) { return nv.v == v; });
		if (it == this->near_vehicles.end()) return false;
		auto [a, c] = it->ToLocal(this->x, this->y);
		if (IsCabAt(v, a, it->box.length)) {
			ShowCabViewWindow3D(v->First(), this);
			this->Close();
			return true;
		}
		const CabViewInteriorKind kind = GetInteriorKind(v);
		if (kind == CabViewInteriorKind::None) {
			this->ShowMessage(STR_FP_MESSAGE_NO_WAY_IN);
			return false;
		}
		/* The vehicle points one way; its frame the way it moves. */
		const bool reversed = v->GetMovingDirection() != v->direction;
		if (reversed) a = -a;
		const float end = it->box.length / 2 - FP_WALL - FP_END_ROOM;
		this->Board(v, std::clamp(a, -end, end), 0, this->head.yaw - GetHeading(v->GetMovingDirection()));
		return false;
	}

	/** Get off the vehicle one is in, next to it on the side where there is room, preferring platforms. */
	void GetOff()
	{
		const Vehicle *v = Vehicle::Get(this->vehicle);
		if (v->First()->cur_speed != 0) {
			this->ShowMessage(STR_FP_MESSAGE_WAIT_UNTIL_STANDING);
			return;
		}
		const CabViewCamera frame = this->ride.Get();
		if (!this->PlaceBeside(v, this->along)) {
			this->ShowMessage(STR_FP_MESSAGE_NO_ROOM_TO_GET_OFF);
			return;
		}
		this->head.yaw = std::remainder(frame.yaw + this->head.yaw, 2.0f * static_cast<float>(M_PI));
		this->vehicle = VehicleID::Invalid();
		this->seated = false;
		this->pacer.Reset(this->GetCamera());
		this->UpdateButtons();
		this->SetDirty();
	}

	/** Sit down on the seat nearest to where one stands, facing the window, or stand up again. */
	void SitOrStand()
	{
		if (this->role != FirstPersonRole::Passenger || this->interior.kind != CabViewInteriorKind::Coach) return;
		if (this->seated) {
			this->seated = false;
			this->across = 0;
			this->head.pitch = FP_INSIDE_PITCH;
		} else {
			/* The rows of seats, as the 3D view draws them. */
			const float la = this->interior.length / 2 - FP_WALL;
			const int rows = std::max(1, static_cast<int>((2 * la - 1.2f) / FP_SEAT_PITCH));
			const float first = -(rows - 1) * FP_SEAT_PITCH / 2;
			const int row = std::clamp(static_cast<int>(std::round((this->along - first) / FP_SEAT_PITCH)), 0, rows - 1);
			const float side = std::remainder(this->head.yaw, 2.0f * static_cast<float>(M_PI)) >= 0 ? 1.0f : -1.0f;
			const float wc = this->interior.width / 2 - FP_WALL;
			this->along = first + row * FP_SEAT_PITCH - 0.05f;
			this->across = side * (FP_AISLE_HALF_WIDTH + wc) / 2;
			/* Facing the front, a bit towards the window. */
			this->head.yaw = side * 0.6f;
			this->head.pitch = 0;
			this->seated = true;
		}
		this->UpdateButtons();
	}

	/**
	 * Show a message in the status for a while.
	 * @param string The message.
	 */
	void ShowMessage(StringID string)
	{
		this->message = string;
		this->message_timer = FP_MESSAGE_MS;
		this->SetWidgetDirty(WID_FP_STATUS);
	}

	/**
	 * Get the camera of the 3D view now.
	 * @return The camera.
	 */
	CabViewCamera GetCamera() const
	{
		if (this->role == FirstPersonRole::Walker) {
			CabViewCamera camera;
			camera.x = this->x;
			camera.y = this->y;
			camera.z = this->z + FP_EYE;
			camera.yaw = this->head.yaw;
			camera.pitch = this->head.pitch;
			return camera;
		}

		/* Inside a vehicle: the frame of the vehicle rides along smoothly; one is somewhere in it. */
		CabViewCamera frame = this->ride.Get();
		CabViewCamera camera = frame;
		const float c = std::cos(frame.yaw), s = std::sin(frame.yaw);
		camera.x = frame.x + this->along * c - this->across * s;
		camera.y = frame.y + this->along * s + this->across * c;
		camera.z = frame.z + this->interior.floor + (this->seated ? FP_SEATED_EYE : FP_EYE) + this->along * std::tan(frame.pitch);
		camera.yaw = frame.yaw + this->head.yaw;
		camera.pitch = frame.pitch + this->head.pitch;
		camera.interior = this->interior;
		camera.interior.x = frame.x;
		camera.interior.y = frame.y;
		camera.interior.z = frame.z;
		camera.interior.yaw = frame.yaw;
		camera.interior.pitch = frame.pitch;
		return camera;
	}

	/** Bring the texts and states of the buttons up to date. */
	void UpdateButtons()
	{
		bool can_get_in_or_off = false;
		if (this->role == FirstPersonRole::Walker) {
			can_get_in_or_off = Vehicle::IsValidID(this->nearby);
		} else {
			can_get_in_or_off = Vehicle::Get(this->vehicle)->First()->cur_speed == 0;
		}
		this->SetWidgetDisabledState(WID_FP_GET_IN, !can_get_in_or_off);
		this->SetWidgetDisabledState(WID_FP_SIT, this->role != FirstPersonRole::Passenger || this->interior.kind != CabViewInteriorKind::Coach);
		this->SetWidgetDirty(WID_FP_GET_IN);
		this->SetWidgetDirty(WID_FP_SIT);
		this->SetWidgetDirty(WID_FP_CAPTION);
	}

public:
	FirstPersonWindow(WindowDesc &desc, WindowNumber window_number) : Window(desc)
	{
		this->InitNested(window_number);
	}

	/**
	 * Stand somewhere on foot.
	 * @param px X position.
	 * @param py Y position.
	 * @param yaw Direction to look to, in radians; 0 looks towards +x, pi/2 towards +y.
	 */
	void PlaceAt(float px, float py, float yaw)
	{
		this->role = FirstPersonRole::Walker;
		this->vehicle = VehicleID::Invalid();
		this->seated = false;
		/* Not in a house or in the water. */
		this->UpdateNearVehicles(px, py);
		std::optional<std::pair<float, float>> room = this->FindRoom(px, py);
		if (room.has_value()) std::tie(px, py) = *room;
		this->x = px;
		this->y = py;
		this->ground = GetCabView3DWalkSurface(px, py, 0).height;
		this->z = this->ground;
		this->head.yaw = std::remainder(yaw, 2.0f * static_cast<float>(M_PI));
		this->head.pitch = 0;
		this->pacer.Reset(this->GetCamera());
		this->UpdateButtons();
		this->SetDirty();
	}

	/**
	 * Stand next to a vehicle part, on the side where there is room, preferring platforms.
	 * @param v The vehicle part.
	 * @param along Where along the vehicle, from its middle, in the direction it moves.
	 * @return Whether there was room.
	 */
	bool PlaceBeside(const Vehicle *v, float along)
	{
		std::optional<CabViewVehicleBox> box = GetCabView3DVehicleBox(v);
		if (!box.has_value()) return false;
		const float heading = GetHeading(v->GetMovingDirection());
		auto [vx, vy] = GetCabView3DExactPosition(v);
		const float c = std::cos(heading), s = std::sin(heading);
		this->UpdateNearVehicles(vx, vy);

		std::optional<std::pair<float, float>> best;
		float best_height = 0;
		for (float side : {1.0f, -1.0f}) {
			const float across = side * (box->width / 2 + FP_GET_OFF_DISTANCE);
			const float px = vx + along * c - across * s, py = vy + along * s + across * c;
			CabViewWalkSurface surface = GetCabView3DWalkSurface(px, py, FP_RADIUS);
			const NearVehicle *in_the_way = this->GetVehicleInTheWay(px, py, surface.height);
			Debug(Facility::Misc, Severity::Notice, "[FirstPerson] getting off vehicle {} at {:.1f}, {:.1f}: {}, height {:.1f} for a vehicle at {}{}",
				v->index.base(), px, py, surface.walkable ? "room" : "no room", surface.height, v->z_pos, in_the_way != nullptr ? ", another vehicle in the way" : "");
			if (!surface.walkable || surface.height > v->z_pos + FP_STEP + FP_TRAIN_FLOOR) continue;
			if (in_the_way != nullptr) continue;
			if (!best.has_value() || surface.height > best_height) {
				best = {px, py};
				best_height = surface.height;
			}
		}
		if (!best.has_value()) return false;
		this->role = FirstPersonRole::Walker;
		this->x = best->first;
		this->y = best->second;
		this->ground = best_height;
		this->z = best_height;
		return true;
	}

	/**
	 * Get into a vehicle part as a passenger, e.g. from the console.
	 * @param v The vehicle part.
	 * @return Whether one can ride in it.
	 */
	bool BoardVehicle(const Vehicle *v)
	{
		if ((v->type != VehicleType::Train && v->type != VehicleType::Road) || GetInteriorKind(v) == CabViewInteriorKind::None) return false;
		this->Board(v, 0, 0, 0);
		return true;
	}

	/**
	 * Turn the head, e.g. from the console.
	 * @param yaw Direction to look to, in radians; on foot as seen from the north, inside a vehicle compared to its front.
	 * @param pitch How far to look up, in radians.
	 */
	void Look(float yaw, float pitch)
	{
		this->head.yaw = yaw;
		this->head.pitch = pitch;
	}

	/** Toggle sitting, e.g. from the console. */
	void ToggleSeat()
	{
		this->SitOrStand();
	}

	/**
	 * Get where one is and what one is in, for the console.
	 * @return The description.
	 */
	std::string GetDescription() const
	{
		CabViewCamera camera = this->GetCamera();
		if (this->role == FirstPersonRole::Walker) {
			const bool free = GetCabView3DWalkSurface(this->x, this->y, FP_RADIUS).walkable;
			return fmt::format("on foot at tile {:.2f}, {:.2f}{}, eye at height {:.1f}, looking {:.0f} degrees; {}",
				camera.x / TILE_SIZE, camera.y / TILE_SIZE, free ? "" : " (stuck)", camera.z, camera.yaw * 180 / M_PI,
				Vehicle::IsValidID(this->nearby) ? fmt::format("next to vehicle {}", this->nearby.base()) : "no vehicle near");
		}
		return fmt::format("inside vehicle {} ({:.1f} along, {:.1f} across, {}), eye at tile {:.2f}, {:.2f}, height {:.1f}",
			this->vehicle.base(), this->along, this->across, this->seated ? "seated" : "standing", camera.x / TILE_SIZE, camera.y / TILE_SIZE, camera.z);
	}

	/**
	 * The vehicle one is in is about to be removed: get off where it is.
	 * @param v The vehicle part.
	 */
	void OnVehicleGone(const Vehicle *v)
	{
		if (this->role != FirstPersonRole::Passenger || v->index != this->vehicle) return;
		const CabViewCamera camera = this->GetCamera();
		if (!this->PlaceBeside(v, this->along)) {
			this->PlaceAt(camera.x, camera.y, camera.yaw);
			return;
		}
		this->head.yaw = camera.yaw;
		this->vehicle = VehicleID::Invalid();
		this->seated = false;
		this->UpdateButtons();
		this->SetDirty();
	}

	void UpdateWidgetSize(WidgetID widget, Dimension &size, const Dimension &padding, [[maybe_unused]] Dimension &fill, [[maybe_unused]] Dimension &resize) override
	{
		switch (widget) {
			case WID_FP_STATUS:
				size.height = 2 * GetCharacterHeight(FontSize::Normal) + WidgetDimensions::scaled.vsep_normal + WidgetDimensions::scaled.frametext.Vertical() + WidgetDimensions::scaled.bevel.Vertical();
				break;

			case WID_FP_GET_IN: {
				Dimension d = maxdim(GetStringBoundingBox(STR_FP_GET_IN), GetStringBoundingBox(STR_FP_GET_OFF));
				size = maxdim(size, {d.width + padding.width, d.height + padding.height});
				break;
			}

			case WID_FP_SIT: {
				Dimension d = maxdim(GetStringBoundingBox(STR_FP_SIT), GetStringBoundingBox(STR_FP_STAND_UP));
				size = maxdim(size, {d.width + padding.width, d.height + padding.height});
				break;
			}
		}
	}

	std::string GetWidgetString(WidgetID widget, StringID stringid) const override
	{
		switch (widget) {
			case WID_FP_CAPTION:
				if (this->role == FirstPersonRole::Passenger) return GetString(STR_FP_CAPTION_PASSENGER, Vehicle::Get(this->vehicle)->First()->index);
				return GetString(STR_FP_CAPTION_ON_FOOT);

			case WID_FP_GET_IN:
				return GetString(this->role == FirstPersonRole::Passenger ? STR_FP_GET_OFF : STR_FP_GET_IN);

			case WID_FP_SIT:
				return GetString(this->seated ? STR_FP_STAND_UP : STR_FP_SIT);

			default:
				return this->Window::GetWidgetString(widget, stringid);
		}
	}

	/**
	 * Get the first line of the status: where one is.
	 * @return The text.
	 */
	std::string GetPlaceString() const
	{
		if (this->role == FirstPersonRole::Passenger) {
			const Vehicle *v = Vehicle::Get(this->vehicle);
			const Vehicle *front = v->First();
			int car = 1, cars = 0;
			for (const Vehicle *u = front; u != nullptr; u = u->Next()) {
				cars++;
				if (u == v) car = cars;
			}
			if (cars == 1) return GetString(STR_FP_STATUS_SPEED, PackVelocity(front->GetDisplaySpeed(), front->type));
			return GetString(STR_FP_STATUS_CAR, car, cars, PackVelocity(front->GetDisplaySpeed(), front->type));
		}
		const Town *t = ClosestTownFromTile(TileVirtXY(static_cast<int>(this->x), static_cast<int>(this->y)), 20);
		if (t == nullptr) return GetString(STR_FP_STATUS_COUNTRYSIDE);
		return GetString(STR_FP_STATUS_NEAR_TOWN, t->index);
	}

	/**
	 * Get the second line of the status: what one can do.
	 * @return The text.
	 */
	std::string GetHintString() const
	{
		if (this->message != INVALID_STRING_ID) return GetString(this->message);
		const StringID button = CabViewLook::IsLookButton(true) ? STR_FP_MOUSE_RIGHT : STR_FP_MOUSE_LEFT;
		if (this->role == FirstPersonRole::Passenger) {
			if (this->interior.kind != CabViewInteriorKind::Coach) return GetString(STR_FP_HINT_INSIDE_VAN, button);
			if (this->seated) return GetString(STR_FP_HINT_SEATED, button);
			return GetString(this->interior.gangway_front || this->interior.gangway_back ? STR_FP_HINT_INSIDE : STR_FP_HINT_INSIDE_BUS, button);
		}
		const Vehicle *v = Vehicle::GetIfValid(this->nearby);
		auto it = std::ranges::find_if(this->near_vehicles, [v](const NearVehicle &nv) { return nv.v == v; });
		if (v != nullptr && it != this->near_vehicles.end()) {
			if (v->First()->cur_speed != 0) return GetString(STR_FP_HINT_MOVING, v->First()->index);
			if (IsCabAt(v, it->ToLocal(this->x, this->y).first, it->box.length)) return GetString(STR_FP_HINT_GET_INTO_CAB, v->First()->index);
			if (GetInteriorKind(v) == CabViewInteriorKind::None) return GetString(STR_FP_HINT_NO_WAY_IN, v->First()->index);
			return GetString(STR_FP_HINT_GET_IN, v->First()->index);
		}
		return GetString(STR_FP_HINT_WALK, button);
	}

	void DrawWidget(const Rect &r, WidgetID widget) const override
	{
		switch (widget) {
			case WID_FP_VIEW:
				DrawCabView3D(r, this->pacer.GetDrawnCamera(), this->pacer.GetFrameId(), this->pacer.GetBudget());
				break;

			case WID_FP_STATUS: {
				Rect ir = r.Shrink(WidgetDimensions::scaled.bevel);
				GfxFillRect(ir, PC_BLACK);
				ir = ir.Shrink(WidgetDimensions::scaled.frametext);
				DrawString(ir, this->GetPlaceString(), TextColour::Silver);
				ir.top += GetCharacterHeight(FontSize::Normal) + WidgetDimensions::scaled.vsep_normal;
				DrawString(ir, this->GetHintString(), TextColour::Grey);
				break;
			}
		}
	}

	void OnClick([[maybe_unused]] Point pt, WidgetID widget, [[maybe_unused]] int click_count) override
	{
		switch (widget) {
			case WID_FP_VIEW:
				CabViewLook::Start(this, false);
				break;

			case WID_FP_GET_IN:
				if (this->role == FirstPersonRole::Walker) {
					if (this->GetIn()) return;
				} else {
					this->GetOff();
				}
				break;

			case WID_FP_SIT:
				this->SitOrStand();
				break;
		}
	}

	bool OnRightClick([[maybe_unused]] Point pt, WidgetID widget) override
	{
		return widget == WID_FP_VIEW && CabViewLook::Start(this, true);
	}

	void OnScroll(Point delta) override
	{
		this->head.Move(delta, static_cast<float>(M_PI));
	}

	EventState OnKeyPress([[maybe_unused]] char32_t key, uint16_t keycode) override
	{
		/* The keys for walking are followed while they are held, see OnRealtimeTick; keep them from doing other things. */
		switch (keycode & ~WKC_SHIFT) {
			case 'W': case 'A': case 'S': case 'D':
			case WKC_UP: case WKC_DOWN: case WKC_LEFT: case WKC_RIGHT:
				return EventState::Handled;

			case 'E':
				/* Also seen by the held keys, but a short press can come and go between two looks at them. */
				this->use_pressed = true;
				return EventState::Handled;

			case WKC_SPACE:
				this->SitOrStand();
				return EventState::Handled;

			default:
				return EventState::NotHandled;
		}
	}

	void OnGameTick() override
	{
		if (this->role != FirstPersonRole::Passenger) return;
		const Vehicle *v = Vehicle::Get(this->vehicle);
		this->UpdateInterior(v);
		this->ride.Tick(v, CabViewSeat::Middle, false);
	}

	void OnRealtimeTick(uint delta_ms) override
	{
		/* Long pauses, like while saving, don't make one walk far at once. */
		const float ms = static_cast<float>(std::min(delta_ms, 100U));
		float forward = 0, right = 0;
		bool use = false;
		if (this->HasKeyFocus()) {
			if (_dirkeys.Test(DirectionKey::Up) || _walkkeys.Test(WalkKey::Forward)) forward += 1;
			if (_dirkeys.Test(DirectionKey::Down) || _walkkeys.Test(WalkKey::Back)) forward -= 1;
			if (_dirkeys.Test(DirectionKey::Right) || _walkkeys.Test(WalkKey::Right)) right += 1;
			if (_dirkeys.Test(DirectionKey::Left) || _walkkeys.Test(WalkKey::Left)) right -= 1;
			use = _walkkeys.Test(WalkKey::Use);
		}

		if (this->role == FirstPersonRole::Walker) {
			this->Walk(ms, forward, right);
		} else {
			this->ride.Advance();
			this->WalkInside(ms, forward, right);
		}

		/* Get in or off when the key is pressed, not again and again while it is held. */
		const bool pressed = (use && !this->use_key) || this->use_pressed;
		this->use_pressed = false;
		if (pressed) {
			if (this->role == FirstPersonRole::Walker) {
				if (this->GetIn()) return;
			} else {
				this->GetOff();
			}
		}
		this->use_key = use;

		if (this->pacer.Update(this->GetCamera())) this->SetWidgetDirty(WID_FP_VIEW);

		if (this->message_timer > 0) {
			this->message_timer = this->message_timer > delta_ms ? this->message_timer - delta_ms : 0;
			if (this->message_timer == 0) this->message = INVALID_STRING_ID;
		}
		this->status_timer += delta_ms;
		if (this->status_timer >= FP_STATUS_REFRESH_MS) {
			this->status_timer = 0;
			this->UpdateButtons();
			this->SetWidgetDirty(WID_FP_STATUS);
		}
	}
};

static WindowDesc _first_person_desc(
	WindowPosition::Center, "first_person_view", 640, 420,
	WindowClass::FirstPersonView, WindowClass::None,
	{},
	_nested_first_person_widgets
);

/**
 * Give a window the place and size of another one, so one seems to turn into the other.
 * @param w The window.
 * @param like The other window, or nullptr to keep the window where it is.
 */
void PlaceWindowLike(Window *w, const Window *like)
{
	if (like == nullptr) return;
	w->SetDirty();
	ResizeWindow(w, like->width - w->width, like->height - w->height, true, false);
	w->left = like->left;
	w->top = like->top;
	w->SetDirty();
}

/**
 * Get the first-person window, opening it when it is not open yet.
 * @param like Window to take the place and size of when opening it, or nullptr.
 * @return The window.
 */
static FirstPersonWindow *GetFirstPersonWindow(const Window *like = nullptr)
{
	FirstPersonWindow *w = dynamic_cast<FirstPersonWindow *>(BringWindowToFrontById(WindowClass::FirstPersonView, 0));
	if (w != nullptr) return w;
	w = new FirstPersonWindow(_first_person_desc, 0);
	PlaceWindowLike(w, like);
	return w;
}

/**
 * Step out of a vehicle into the first-person view, next to it on the side where there is room.
 * @param v The vehicle part to step out of, like the one at the moving front of a train.
 * @param replace The window to take the place of, like the cab view, or nullptr.
 * @return Whether there was room to step out.
 */
bool ShowFirstPersonBeside(const Vehicle *v, const Window *replace)
{
	const bool was_open = FindWindowById(WindowClass::FirstPersonView, 0) != nullptr;
	FirstPersonWindow *w = GetFirstPersonWindow(replace);
	/* Out of the cab, at the front of the vehicle. */
	std::optional<CabViewVehicleBox> box = GetCabView3DVehicleBox(v);
	const float along = box.has_value() ? box->length / 2 - 1.0f : 0.0f;
	if (!w->PlaceBeside(v, along)) {
		if (!was_open) w->Close();
		return false;
	}
	w->Look(GetHeading(v->GetMovingDirection()), 0);
	return true;
}

/**
 * Whether someone walks in the first-person view with the direction keys, so they don't scroll the main view.
 * @return True when the first-person view gets the keys: it is open and in focus, or no window is in focus.
 */
bool FirstPersonCapturesDirectionKeys()
{
	const Window *w = FindWindowById(WindowClass::FirstPersonView, 0);
	if (w == nullptr) return false;
	return _focused_window == nullptr || _focused_window == w;
}

/**
 * A vehicle is about to be removed; whoever rides in it in the first-person view gets off.
 * @param v The vehicle.
 */
void FirstPersonVehicleGone(const Vehicle *v)
{
	FirstPersonWindow *w = dynamic_cast<FirstPersonWindow *>(FindWindowById(WindowClass::FirstPersonView, 0));
	if (w != nullptr) w->OnVehicleGone(v);
}

/**
 * Get where the middle of the main view is on the ground.
 * @return The point, in world coordinates.
 */
static Point GetMainViewCentre()
{
	const Window *w = GetMainWindow();
	if (w != nullptr && w->viewport != nullptr) {
		const Viewport &vp = *w->viewport;
		Point pt = TranslateXYToTileCoord(vp, vp.left + vp.width / 2, vp.top + vp.height / 2);
		if (pt.x != -1) return pt;
	}
	return {static_cast<int>(Map::SizeX() * TILE_SIZE / 2), static_cast<int>(Map::SizeY() * TILE_SIZE / 2)};
}

/**
 * Parse a number of the console.
 * @param arg The argument.
 * @return The number, or std::nullopt when it is none.
 */
static std::optional<float> ParseFloat(std::string_view arg)
{
	std::string text{arg};
	char *end = nullptr;
	float value = std::strtof(text.c_str(), &end);
	if (end == text.c_str() || *end != '\0') return std::nullopt;
	return value;
}

/**
 * Get a vehicle by its number in the console: the internal number, as scripts and the AI use it.
 * @param arg The argument.
 * @return The vehicle, or nullptr when there is none.
 */
static const Vehicle *ParseVehicle(std::string_view arg)
{
	std::optional<float> number = ParseFloat(arg);
	if (!number.has_value() || *number < 0) return nullptr;
	return Vehicle::GetIfValid(static_cast<VehicleID>(static_cast<uint32_t>(*number)));
}

/**
 * The first-person view: walk around, get into vehicles.
 * @param argv Arguments of the command.
 * @return True iff the command was valid.
 */
static bool ConFirstPerson(std::span<std::string_view> argv)
{
	if (argv.size() < 2 || argv[1] == "help") {
		IConsolePrint(CC_HELP, "The first-person view. Usage: 'fp <command>'. Vehicles by their internal number, as scripts use it.");
		IConsolePrint(CC_HELP, "  walk [<x> <y> [<degrees>]]: walk around, at the middle of the main view or at tile x, y (fractions allowed).");
		IConsolePrint(CC_HELP, "  cab <vehicle>: ride in the cab of a vehicle, in the 3D view.");
		IConsolePrint(CC_HELP, "  board <vehicle> [<part>]: ride inside a coach, bus or mail van; part 1 is the first one.");
		IConsolePrint(CC_HELP, "  look <degrees> [<degrees up>]: turn the head; on foot 0 looks to the south-west, 90 to the south-east, 225 to the north like the map.");
		IConsolePrint(CC_HELP, "  sit: sit down, or stand up.");
		IConsolePrint(CC_HELP, "  status [<vehicle>]: where you are, and whether a vehicle stands still.");
		return true;
	}

	const std::string_view action = argv[1];
	if (action == "walk") {
		Point centre = GetMainViewCentre();
		/* Looking north, the way the map does. */
		float x = static_cast<float>(centre.x), y = static_cast<float>(centre.y), degrees = 225;
		if (argv.size() >= 4) {
			std::optional<float> tx = ParseFloat(argv[2]), ty = ParseFloat(argv[3]);
			if (!tx.has_value() || !ty.has_value()) return false;
			x = *tx * TILE_SIZE;
			y = *ty * TILE_SIZE;
		}
		if (argv.size() >= 5) {
			std::optional<float> d = ParseFloat(argv[4]);
			if (!d.has_value()) return false;
			degrees = *d;
		}
		GetFirstPersonWindow()->PlaceAt(x, y, degrees * static_cast<float>(M_PI) / 180);
		return true;
	}
	if (action == "cab" && argv.size() >= 3) {
		const Vehicle *v = ParseVehicle(argv[2]);
		if (v == nullptr || !v->IsPrimaryVehicle()) {
			IConsolePrint(CC_ERROR, "No such vehicle.");
			return true;
		}
		ShowCabViewWindow3D(v, FindWindowById(WindowClass::FirstPersonView, 0));
		CloseWindowById(WindowClass::FirstPersonView, 0);
		return true;
	}
	if (action == "board" && argv.size() >= 3) {
		const Vehicle *v = ParseVehicle(argv[2]);
		if (v != nullptr) v = v->First();
		if (v != nullptr && argv.size() >= 4) {
			std::optional<float> part = ParseFloat(argv[3]);
			if (!part.has_value()) return false;
			for (int i = 1; v != nullptr && i < static_cast<int>(*part); i++) v = v->Next();
		}
		if (v == nullptr || !GetFirstPersonWindow()->BoardVehicle(v)) IConsolePrint(CC_ERROR, "No such coach, bus or mail van to ride in.");
		return true;
	}
	FirstPersonWindow *w = dynamic_cast<FirstPersonWindow *>(FindWindowById(WindowClass::FirstPersonView, 0));
	if (action == "look" && argv.size() >= 3) {
		std::optional<float> yaw = ParseFloat(argv[2]);
		std::optional<float> pitch = argv.size() >= 4 ? ParseFloat(argv[3]) : std::optional<float>{0.0f};
		if (!yaw.has_value() || !pitch.has_value()) return false;
		if (w != nullptr) w->Look(*yaw * static_cast<float>(M_PI) / 180, *pitch * static_cast<float>(M_PI) / 180);
		return true;
	}
	if (action == "sit") {
		if (w != nullptr) w->ToggleSeat();
		return true;
	}
	if (action == "status") {
		IConsolePrint(CC_INFO, "First-person view: {}.", w == nullptr ? "closed" : w->GetDescription());
		if (argv.size() >= 3) {
			const Vehicle *v = ParseVehicle(argv[2]);
			if (v == nullptr) {
				IConsolePrint(CC_ERROR, "No such vehicle.");
			} else {
				v = v->First();
				const char *state = v->cur_speed != 0 ? "moving" : v->current_order.IsType(OT_LOADING) ? "loading at a station" : "stands still";
				IConsolePrint(CC_INFO, "Vehicle {}: {}, {} km/h-ish, at tile {}, {}.", v->index.base(), state, v->GetDisplaySpeed(), TileX(v->tile), TileY(v->tile));
				for (const Vehicle *u = v; u != nullptr; u = u->Next()) {
					const bool engine = u->type == VehicleType::Train && IsEnginePart(Train::From(u));
					const CabViewInteriorKind kind = GetInteriorKind(u);
					IConsolePrint(CC_DEFAULT, "  part {}: {}{}, {}{}", u->index.base(), engine ? "engine" : "wagon", u->IsArticulatedPart() ? " (articulated part)" : "",
						kind == CabViewInteriorKind::Coach ? "seats" : kind == CabViewInteriorKind::Mail ? "mail" : "no room inside", u == v->GetMovingFront() ? ", moving front" : "");
				}
			}
		}
		return true;
	}
	return false;
}

/**
 * The first-person view needs a map and a user interface.
 * @param echo Whether to print why the command is not available.
 * @return Whether the command may be used.
 */
static ConsoleHookResult ConHookFirstPerson(bool echo)
{
	if (_network_dedicated || _game_mode == GameMode::Bootstrap || Map::Size() == 0) {
		if (echo) IConsolePrint(CC_ERROR, "The first-person view needs a map and a user interface.");
		return ConsoleHookResult::Disallow;
	}
	return ConsoleHookResult::Allow;
}

/** Register the console command of the first-person view. */
void RegisterFirstPersonConsoleCommands()
{
	IConsole::CmdRegister("fp", ConFirstPerson, ConHookFirstPerson);
}
