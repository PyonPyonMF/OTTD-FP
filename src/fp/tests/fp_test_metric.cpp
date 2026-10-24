/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file fp_test_metric.cpp Tests of the Metric Bridge of the first-person mode. */

#include "../../stdafx.h"

#include "../../3rdparty/catch2/catch.hpp"

#include "../core/fp_metric.h"
#include "../../gfx_type.h"
#include "../../vehicle_base.h"

#include "../../safeguards.h"

using namespace fp;

/**
 * Metres per world unit at which a vehicle moves as fast as its speedometer says.
 * @param progress_per_tick Progress the vehicle gains per tick at 100 km/h.
 * @return The scale.
 */
static double TrueSpeedScale(uint progress_per_tick)
{
	double ticks_per_tile = static_cast<double>(TILE_SIZE * TILE_AXIAL_DISTANCE) / progress_per_tick;
	double seconds_per_tile = ticks_per_tile * MILLISECONDS_PER_TICK / 1000.0;
	double metres_per_tile = 100.0 / 3.6 * seconds_per_tile;
	return metres_per_tile / TILE_SIZE;
}

TEST_CASE("Metric bridge - true speed scale follows from the vehicle movement code")
{
	/* Trains: speed in km/h, the controller runs twice per tick. */
	CHECK(TrueSpeedScale(2 * Vehicle::GetAdvanceSpeed(100)) == Approx(MetricScale::TRUE_SPEED_METRES_PER_UNIT));
	/* Road vehicles: speed in 0.5 km/h, the controller runs once per tick. */
	CHECK(TrueSpeedScale(Vehicle::GetAdvanceSpeed(200)) == Approx(MetricScale::TRUE_SPEED_METRES_PER_UNIT));
}

TEST_CASE("Metric bridge - time dilation")
{
	MetricScale scale = MetricScale::ForTimeDilation(2.0);
	CHECK(scale.metres_per_unit == Approx(1.92));
	CHECK(scale.TileLength() == Approx(30.72));

	/* Time cannot run faster than normal. */
	CHECK(MetricScale::ForTimeDilation(0.5).metres_per_unit == Approx(MetricScale::TRUE_SPEED_METRES_PER_UNIT));

	/* The default is the recommended preset. */
	CHECK(MetricScale{}.metres_per_unit == Approx(scale.metres_per_unit));
}

TEST_CASE("Metric bridge - conversions")
{
	MetricScale scale = MetricScale::ForTimeDilation(3.0, 4.0);
	CHECK(scale.UnitsToMetres(TILE_SIZE) == Approx(scale.TileLength()));
	CHECK(scale.MetresToUnits(scale.UnitsToMetres(123.0)) == Approx(123.0));
	CHECK(scale.LevelsToMetres(2) == Approx(8.0));
	CHECK(scale.WorldZToMetres(TILE_HEIGHT) == Approx(4.0));

	Vec3d p = scale.WorldToMetres(TILE_SIZE, 2 * TILE_SIZE, 3 * TILE_HEIGHT);
	CHECK(p.x == Approx(scale.TileLength()));
	CHECK(p.y == Approx(2 * scale.TileLength()));
	CHECK(p.z == Approx(12.0));
}
