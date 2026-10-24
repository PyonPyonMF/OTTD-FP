/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file fp_role.h Interface of the roles a player can take in the first-person mode. */

#ifndef FP_ROLE_H
#define FP_ROLE_H

#include "../core/fp_math.h"
#include "../input/fp_input.h"

namespace fp {

/** Where the camera is and where it looks. */
struct CameraPose {
	Vec3d position{}; ///< Position of the eye, in metres.
	double yaw = 0; ///< Heading in radians, counter-clockwise from the X axis seen from above.
	double pitch = 0; ///< Pitch in radians, positive is up.
	double fov_y = 1.0; ///< Vertical field of view, in radians.

	/**
	 * Unit vector the camera looks along.
	 * @return The direction.
	 */
	Vec3d Forward() const
	{
		return {std::cos(this->pitch) * std::cos(this->yaw), std::cos(this->pitch) * std::sin(this->yaw), std::sin(this->pitch)};
	}
};

/**
 * Function giving the height of the ground at a position.
 * Takes the X and Y coordinate in metres, returns the height in metres.
 */
using GroundQuery = std::function<double(double, double)>;

/**
 * A role the player takes in the first-person mode (docs/fp/architecture.md, section 9):
 * spectator, walker, passenger, train driver and so on.
 */
class Role {
public:
	virtual ~Role() = default;

	/**
	 * Name of the role, for diagnostics.
	 * @return The name.
	 */
	virtual std::string_view GetName() const = 0;

	/**
	 * Advance the role by one frame.
	 * @param input The input actions of this frame.
	 * @param dt Length of the frame, in seconds.
	 * @param ground Height of the ground.
	 */
	virtual void Update(const InputState &input, double dt, const GroundQuery &ground) = 0;

	/**
	 * Camera of the role.
	 * @return The camera pose after the last update.
	 */
	virtual CameraPose GetCamera() const = 0;
};

} // namespace fp

#endif /* FP_ROLE_H */
