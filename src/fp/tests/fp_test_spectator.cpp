/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file fp_test_spectator.cpp Tests of the spectator role of the first-person mode. */

#include "../../stdafx.h"

#include "../../3rdparty/catch2/catch.hpp"

#include "../roles/fp_spectator.h"

#include "../../safeguards.h"

using namespace fp;

/**
 * Spectator above flat ground at a height of 100 m, in a world of 10 km.
 * @return The spectator.
 */
static Spectator MakeSpectator()
{
	Spectator spectator;
	spectator.SetBounds(10000, 10000);
	CameraPose pose;
	pose.position = {5000, 5000, 150};
	spectator.Place(pose);
	return spectator;
}

static const GroundQuery FLAT_GROUND = [](double, double) { return 100.0; };

TEST_CASE("Spectator - moves along its heading")
{
	Spectator spectator = MakeSpectator();
	InputState input;
	input.move_forward = 1;
	for (int i = 0; i < 60; i++) spectator.Update(input, 1.0 / 60, FLAT_GROUND);

	Vec3d position = spectator.GetCamera().position;
	CHECK(position.x > 5000);
	CHECK(position.y == Approx(5000));
	CHECK(position.z == Approx(150));
}

TEST_CASE("Spectator - turns and pitches within limits")
{
	Spectator spectator = MakeSpectator();
	InputState input;
	input.look_yaw = 0.5f;
	input.look_pitch = 10.0f;
	spectator.Update(input, 1.0 / 60, FLAT_GROUND);

	CHECK(spectator.GetCamera().yaw == Approx(0.5));
	CHECK(spectator.GetCamera().pitch == Approx(Spectator::MAX_PITCH));
}

TEST_CASE("Spectator - cannot go below the ground")
{
	Spectator spectator = MakeSpectator();
	InputState input;
	input.move_up = -20;
	for (int i = 0; i < 120; i++) {
		spectator.Update(input, 1.0 / 60, FLAT_GROUND);
		input.move_up = 0;
	}

	CHECK(spectator.GetCamera().position.z == Approx(100 + Spectator::EYE_HEIGHT));
	CHECK(spectator.GetHeightAboveGround() == Approx(Spectator::EYE_HEIGHT));
}

TEST_CASE("Spectator - stays near the world")
{
	Spectator spectator = MakeSpectator();
	InputState input;
	input.move_right = 1;
	input.boost = true;
	for (int i = 0; i < 600; i++) spectator.Update(input, 0.1, FLAT_GROUND);

	CHECK(spectator.GetCamera().position.y >= -500.0);
	CHECK(spectator.GetCamera().position.y <= 10500.0);
}
