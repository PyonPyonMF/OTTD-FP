/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file fp_spectator.h The spectator role: a free flying camera. */

#ifndef FP_SPECTATOR_H
#define FP_SPECTATOR_H

#include "fp_role.h"

namespace fp {

/**
 * Free flying camera that cannot go below the ground.
 * It moves faster the higher it is, so it is as usable a metre above a
 * platform as when crossing the map at a few hundred metres.
 */
class Spectator : public Role {
public:
	static constexpr double EYE_HEIGHT = 1.8; ///< Lowest height of the eye above the ground, in metres.
	static constexpr double MIN_SPEED = 12.0; ///< Speed near the ground, in metres per second.
	static constexpr double MAX_SPEED = 900.0; ///< Highest speed without boost, in metres per second.
	static constexpr double BOOST_FACTOR = 4.0; ///< Speed factor while boosting.
	static constexpr double MAX_PITCH = 1.5; ///< Highest pitch up or down, in radians.
	static constexpr double MAX_HEIGHT = 6000.0; ///< Highest height above the ground, in metres.

	void Place(const CameraPose &pose);
	void SetBounds(double size_x, double size_y);

	std::string_view GetName() const override { return "spectator"; }
	void Update(const InputState &input, double dt, const GroundQuery &ground) override;
	CameraPose GetCamera() const override { return this->pose; }

	/**
	 * Height of the eye above the ground after the last update.
	 * @return The height, in metres.
	 */
	double GetHeightAboveGround() const { return this->height_above_ground; }

private:
	CameraPose pose{}; ///< The camera.
	Vec3d velocity{}; ///< Horizontal velocity, in metres per second.
	double climb = 0; ///< Vertical velocity, in metres per second.
	double size_x = 0; ///< Extent of the world along the X axis, in metres.
	double size_y = 0; ///< Extent of the world along the Y axis, in metres.
	double height_above_ground = 0; ///< Height of the eye above the ground, in metres.
};

} // namespace fp

#endif /* FP_SPECTATOR_H */
