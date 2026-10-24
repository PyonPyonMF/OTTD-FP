/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file fp_snapshot.h Copies of the simulation state that the first-person runtime may read without the game state lock. */

#ifndef FP_SNAPSHOT_H
#define FP_SNAPSHOT_H

#include <chrono>

#include "../../core/enum_type.hpp"
#include "../../direction_type.h"
#include "../../timer/timer_game_tick.h"
#include "../../vehicle_type.h"

namespace fp {

/** Properties of a vehicle in a #VehicleFrame. */
enum class VehicleFrameFlag : uint8_t {
	Front, ///< First part of its vehicle chain.
	Hidden, ///< Not visible, e.g. in a depot or tunnel.
	Crashed, ///< Crashed.
	Helicopter, ///< Aircraft that is a helicopter.
};

/** Bitset of #VehicleFrameFlag elements. */
using VehicleFrameFlags = EnumBitSet<VehicleFrameFlag, uint8_t>;

/**
 * State of one vehicle part at the end of a tick.
 *
 * Positions are kept in world units, exactly like the simulation has them;
 * converting them to metres and smoothing them is up to the presentation
 * (principle P2 in docs/fp/architecture.md).
 */
struct VehicleFrame {
	VehicleID id = VehicleID::Invalid(); ///< Pool index of the vehicle part.
	uint32_t generation = 0; ///< Incremented whenever a vehicle with this index is deleted, so reused indices are not mistaken for the old vehicle.
	int32_t x = 0; ///< X coordinate in world units.
	int32_t y = 0; ///< Y coordinate in world units.
	int32_t z = 0; ///< Z coordinate in world units (#TILE_HEIGHT per height level).
	uint16_t advance = 0; ///< Progress the vehicle chain needs for its next step.
	uint8_t progress = 0; ///< Progress the vehicle chain has made towards its next step.
	uint8_t length = 0; ///< Length in units of 1/#VEHICLE_LENGTH of a full length; 0 if not a ground vehicle.
	VehicleType type = VehicleType::Invalid; ///< Type of the vehicle.
	Direction direction = Direction::Invalid; ///< Direction the vehicle part faces.
	Direction moving_direction = Direction::Invalid; ///< Direction the vehicle part moves in.
	VehicleFrameFlags flags{}; ///< Further properties.
};

/** Everything the first-person runtime needs from one tick of the simulation. */
struct TickSnapshot {
	uint32_t session = 0; ///< Bridge session the snapshot belongs to; 0 for "none".
	TimerGameTick::TickCounter tick = 0; ///< Tick counter at the end of the tick.
	std::chrono::steady_clock::time_point time{}; ///< Real time at which the snapshot was taken.
	std::vector<VehicleFrame> vehicles; ///< Vehicles in the area of interest.
};

} // namespace fp

#endif /* FP_SNAPSHOT_H */
