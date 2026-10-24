/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file fp_metric.h The "Metric Bridge": conversion between OpenTTD world units and metres. */

#ifndef FP_METRIC_H
#define FP_METRIC_H

#include "../../tile_type.h"
#include "fp_math.h"

namespace fp {

/**
 * Scales that map the OpenTTD world onto metres (docs/fp/architecture.md, section 2.1).
 *
 * Horizontal and vertical distances use independent scales: copying the
 * horizontal scale to heights would turn every slope into a 50% grade.
 * Heights of objects (vehicles, buildings) are not terrain and should use
 * the horizontal scale to keep their proportions.
 */
struct MetricScale {
	/**
	 * Metres per world unit at which vehicles move as fast as their speedometer
	 * says, without slowing down time.
	 *
	 * A vehicle at 100 km/h gains 150 "progress" per tick (trains run their
	 * controller twice per tick at 75 each, road vehicles once with speeds in
	 * 0.5 km/h units). One step of 1/16 tile needs #TILE_AXIAL_DISTANCE (192),
	 * so crossing a tile takes 3072 / 150 = 20.48 ticks of 27 ms, or 0.55296 s.
	 * At 100 km/h that is 15.36 m per tile, i.e. 0.96 m per world unit.
	 */
	static constexpr double TRUE_SPEED_METRES_PER_UNIT = 0.96;
	static constexpr double DEFAULT_TIME_DILATION = 2.0; ///< Default slow-down of time in first-person mode.
	static constexpr double DEFAULT_METRES_PER_LEVEL = 5.0; ///< Default metres per terrain height level.

	double metres_per_unit = TRUE_SPEED_METRES_PER_UNIT * DEFAULT_TIME_DILATION; ///< Horizontal metres per world unit (#TILE_SIZE units per tile).
	double metres_per_level = DEFAULT_METRES_PER_LEVEL; ///< Metres per terrain height level.

	static MetricScale ForTimeDilation(double dilation, double metres_per_level = DEFAULT_METRES_PER_LEVEL);

	/**
	 * Length of a tile.
	 * @return Metres per tile.
	 */
	constexpr double TileLength() const { return this->metres_per_unit * TILE_SIZE; }

	/**
	 * Convert a horizontal distance.
	 * @param units Distance in world units.
	 * @return Distance in metres.
	 */
	constexpr double UnitsToMetres(double units) const { return units * this->metres_per_unit; }

	/**
	 * Convert a horizontal distance.
	 * @param metres Distance in metres.
	 * @return Distance in world units.
	 */
	constexpr double MetresToUnits(double metres) const { return metres / this->metres_per_unit; }

	/**
	 * Convert a terrain height.
	 * @param levels Height in height levels.
	 * @return Height in metres.
	 */
	constexpr double LevelsToMetres(double levels) const { return levels * this->metres_per_level; }

	/**
	 * Convert a world z coordinate, which has #TILE_HEIGHT units per height level.
	 * @param z The z coordinate, e.g. Vehicle::z_pos or the result of GetSlopePixelZ().
	 * @return Height in metres.
	 */
	constexpr double WorldZToMetres(double z) const { return z / TILE_HEIGHT * this->metres_per_level; }

	/**
	 * Convert a world position.
	 * @param x X coordinate in world units.
	 * @param y Y coordinate in world units.
	 * @param z Z coordinate in world units (#TILE_HEIGHT per height level).
	 * @return The position in metres.
	 */
	constexpr Vec3d WorldToMetres(double x, double y, double z) const
	{
		return {this->UnitsToMetres(x), this->UnitsToMetres(y), this->WorldZToMetres(z)};
	}
};

} // namespace fp

#endif /* FP_METRIC_H */
