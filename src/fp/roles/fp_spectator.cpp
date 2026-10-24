/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file fp_spectator.cpp The spectator role: a free flying camera. */

#include "../../stdafx.h"
#include "fp_spectator.h"
#include "../../core/math_func.hpp"

#include "../../safeguards.h"

namespace fp {

/**
 * Put the camera somewhere and stop all motion.
 * @param pose The new camera.
 */
void Spectator::Place(const CameraPose &pose)
{
	this->pose = pose;
	this->velocity = {};
	this->climb = 0;
}

/**
 * Set the extent of the world; the camera may leave it by a small margin only.
 * @param size_x Extent along the X axis, in metres.
 * @param size_y Extent along the Y axis, in metres.
 */
void Spectator::SetBounds(double size_x, double size_y)
{
	this->size_x = size_x;
	this->size_y = size_y;
}

void Spectator::Update(const InputState &input, double dt, const GroundQuery &ground)
{
	this->pose.yaw = std::remainder(this->pose.yaw + input.look_yaw, 2 * std::numbers::pi);
	this->pose.pitch = Clamp(this->pose.pitch + input.look_pitch, -MAX_PITCH, MAX_PITCH);

	double ground_z = ground(this->pose.position.x, this->pose.position.y);
	double above = std::max(this->pose.position.z - ground_z, 0.0);
	double speed = Clamp(MIN_SPEED + above * 0.75, MIN_SPEED, MAX_SPEED) * (input.boost ? BOOST_FACTOR : 1.0);

	/* Horizontal movement relative to the heading, smoothed so the camera does not jerk. */
	Vec3d forward{std::cos(this->pose.yaw), std::sin(this->pose.yaw), 0};
	Vec3d right{std::sin(this->pose.yaw), -std::cos(this->pose.yaw), 0};
	Vec3d wish = forward * input.move_forward + right * input.move_right;
	double wish_length = Length(wish);
	if (wish_length > 1.0) wish = wish * (1.0 / wish_length);
	this->velocity = Lerp(this->velocity, wish * speed, 1.0 - std::exp(-dt * 6.0));

	/* Every impulse adds to the climb rate, which then decays. */
	this->climb += input.move_up * speed * 0.6;
	this->climb *= std::exp(-dt * 3.0);

	Vec3d &position = this->pose.position;
	position += this->velocity * dt;
	position.z += this->climb * dt;

	const double margin = 500.0;
	position.x = Clamp(position.x, -margin, this->size_x + margin);
	position.y = Clamp(position.y, -margin, this->size_y + margin);

	ground_z = ground(position.x, position.y);
	if (position.z < ground_z + EYE_HEIGHT) {
		position.z = ground_z + EYE_HEIGHT;
		this->climb = std::max(this->climb, 0.0);
	}
	if (position.z > ground_z + MAX_HEIGHT) {
		position.z = ground_z + MAX_HEIGHT;
		this->climb = std::min(this->climb, 0.0);
	}
	this->height_above_ground = position.z - ground_z;
}

} // namespace fp
