/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file fp_test_math.cpp Tests of the vector and matrix types of the first-person mode. */

#include "../../stdafx.h"

#include "../../3rdparty/catch2/catch.hpp"

#include "../core/fp_math.h"

#include "../../safeguards.h"

using namespace fp;

/** Point in clip space after the perspective divide. */
struct Ndc {
	float x; ///< Horizontal, -1 is left.
	float y; ///< Vertical, -1 is bottom.
	float z; ///< Depth.
};

/**
 * Project a point relative to the camera.
 * @param m View-projection matrix.
 * @param p The point.
 * @return The normalised device coordinates.
 */
static Ndc Project(const Mat4f &m, const Vec3f &p)
{
	std::array<float, 4> clip{};
	for (int row = 0; row < 4; row++) clip[row] = m.At(row, 0) * p.x + m.At(row, 1) * p.y + m.At(row, 2) * p.z + m.At(row, 3);
	return {clip[0] / clip[3], clip[1] / clip[3], clip[2] / clip[3]};
}

TEST_CASE("Math - vectors")
{
	Vec3d a{1, 2, 3};
	Vec3d b{4, 5, 6};
	CHECK(Dot(a, b) == 32);
	CHECK(Cross(Vec3d{1, 0, 0}, Vec3d{0, 1, 0}) == Vec3d{0, 0, 1});
	CHECK(Length(Vec3d{3, 4, 0}) == Approx(5));
	CHECK(Lerp(a, b, 0.5) == Vec3d{2.5, 3.5, 4.5});
}

TEST_CASE("Math - angles interpolate the short way round")
{
	const double pi = std::numbers::pi;
	CHECK(LerpAngle(0.9 * pi, -0.9 * pi, 0.5) == Approx(pi));
	CHECK(LerpAngle(0.1, 0.3, 0.5) == Approx(0.2));
}

TEST_CASE("Math - view and projection")
{
	Vec3f forward{1, 0, 0};
	Mat4f view = MakeViewRotation(forward);

	for (bool reversed : {false, true}) {
		CAPTURE(reversed);
		Mat4f view_proj = MakePerspective(1.0f, 1.5f, 0.5f, 1000.0f, reversed) * view;

		/* Straight ahead is the centre of the screen. */
		Ndc centre = Project(view_proj, {100, 0, 0});
		CHECK(centre.x == Approx(0).margin(1e-6));
		CHECK(centre.y == Approx(0).margin(1e-6));

		/* Left of the camera (+Y when looking along +X) is on the left, up is up. */
		CHECK(Project(view_proj, {100, 10, 0}).x < 0);
		CHECK(Project(view_proj, {100, 0, 10}).y > 0);

		/* Depth at the near plane. */
		CHECK(Project(view_proj, {0.5f, 0, 0}).z == Approx(reversed ? 1.0f : -1.0f));
	}

	/* Reversed-Z: depth decreases with distance towards zero. */
	Mat4f reversed = MakePerspective(1.0f, 1.5f, 0.5f, 0.0f, true) * view;
	CHECK(Project(reversed, {10, 0, 0}).z > Project(reversed, {1000, 0, 0}).z);
	CHECK(Project(reversed, {1e7f, 0, 0}).z == Approx(0).margin(1e-6));
}
