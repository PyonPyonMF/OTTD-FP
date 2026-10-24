/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file cab_view_3d.h Software rendered 3D view from the cab of a train, a ship or an aircraft, on foot, or from inside a vehicle. */

#ifndef CAB_VIEW_3D_H
#define CAB_VIEW_3D_H

#include "core/geometry_type.hpp"
#include "gfx_type.h"
#include "tile_type.h"
#include "vehicle_type.h"

#include <optional>

/** Smallest size of the picture of the 3D view compared to the view, when drawing at full size takes too long. */
constexpr float CAB_VIEW_3D_MIN_SCALE = 0.5f;
/** How far below the level of its tile the 3D view shows the surface of water, so canals, rivers and the sea have banks. */
constexpr int CAB_VIEW_3D_WATER_DROP = 1;
/** Height of the eye of the 3D view from a ship above the water. */
constexpr float CAB_VIEW_3D_SHIP_EYE = 4.0f;

/** What the inside of a vehicle looks like, see #CabViewInterior. */
enum class CabViewInteriorKind : uint8_t {
	None, ///< The camera is not inside a vehicle.
	Coach, ///< Seats along an aisle and windows, like in a passenger coach or a bus.
	Mail, ///< Shelves along the walls, like in a mail van.
};

/**
 * The inside of a vehicle the camera of the 3D view is in, which is drawn around it instead of the vehicle itself.
 * Its points are given along the vehicle, across it to the left and up from its track or road.
 */
struct CabViewInterior {
	CabViewInteriorKind kind = CabViewInteriorKind::None; ///< What it looks like.
	float x = 0; ///< X position of the middle of the vehicle, at the height of its track or road.
	float y = 0; ///< Y position of the middle of the vehicle.
	float z = 0; ///< Height of the track or road in the middle of the vehicle.
	float yaw = 0; ///< Direction the front of the vehicle points to, like #CabViewCamera::yaw.
	float pitch = 0; ///< How much the vehicle climbs towards its front, in radians.
	float length = 0; ///< Length of the vehicle, see #CabViewVehicleBox.
	float width = 0; ///< Width of the vehicle.
	float height = 0; ///< Height of the vehicle above its track or road.
	float floor = 0; ///< Height of the floor above the track or road.
	bool gangway_front = false; ///< Whether there is a door to the next vehicle at the front.
	bool gangway_back = false; ///< Whether there is a door to the next vehicle at the back.
	bool windscreen = false; ///< Whether there are windows at the front and the back, like in a bus, where there is no gangway.
	Colours colour = Colours::Grey; ///< Colour of the seats, that of the company.

	bool operator==(const CabViewInterior &) const = default;
};

/** Size of the box a train or road vehicle is drawn as in the 3D view. */
struct CabViewVehicleBox {
	float length = 0; ///< Length, along the vehicle.
	float width = 0; ///< Width, across the vehicle.
	float height = 0; ///< Height above its track or road.
};

/** What someone walking in the 3D view finds at a point, see #GetCabView3DWalkSurface. */
struct CabViewWalkSurface {
	float height = 0; ///< Height of what one stands on there: the ground, or a platform.
	bool walkable = false; ///< Whether one can be there: not in water, in a building or outside the map.
};

/** Where the camera of the 3D cab view is and where it looks. */
struct CabViewCamera {
	float x = 0; ///< X position of the eye, in world coordinates.
	float y = 0; ///< Y position of the eye, in world coordinates.
	float z = 0; ///< Height of the eye, in world coordinates.
	float yaw = 0; ///< Direction of view in radians; 0 looks towards +x, pi/2 towards +y.
	float pitch = 0; ///< Tilt of the view in radians, upwards positive, on top of the slight look down of the 3D view.
	float altitude = 0; ///< How high above the ground an aircraft flies; the haze then starts further away, as the ground is seen from above.
	VehicleID vehicle = VehicleID::Invalid(); ///< The vehicle the camera rides on, which is not drawn.
	bool dark = false; ///< Nothing to see, e.g. in a depot.
	TileIndex tunnel = INVALID_TILE; ///< An entrance of the tunnel the train is in, whose inside the view shows.
	CabViewInterior interior; ///< The inside of the vehicle the camera is in, if any.

	bool operator==(const CabViewCamera &) const = default;
};

void DrawCabView3D(const Rect &r, const CabViewCamera &camera, uint32_t frame_id, float budget_ms);
float GetCabView3DFrameTime(float scale);
float GetCabView3DShipWaterZ(const Vehicle *v);
void ResetCabView3D();
std::optional<CabViewVehicleBox> GetCabView3DVehicleBox(const Vehicle *v);
CabViewWalkSurface GetCabView3DWalkSurface(float x, float y, float radius);

#endif /* CAB_VIEW_3D_H */
