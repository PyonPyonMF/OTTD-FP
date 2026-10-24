/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file fp_splines_capture.h Copies of what the spline graph needs from the map: tracks, roads and the surface vehicles drive on. */

#ifndef FP_SPLINES_CAPTURE_H
#define FP_SPLINES_CAPTURE_H

#include "../../direction_type.h"
#include "../../road_type.h"
#include "../../slope_type.h"
#include "../../tile_type.h"
#include "../../track_type.h"
#include "../core/fp_chunk.h"

namespace fp {

double GetSlopeSurfaceZ(Slope slope, double fx, double fy);

/**
 * Surface that ground vehicles drive on, for one tile.
 *
 * Every tile type with tracks or roads computes the height of a vehicle as a
 * base height plus GetPartialPixelZ() on a slope, both after foundations
 * (bridge ramps substitute the slope of the ramp). Keeping that pair makes
 * the height exact and continuous at any point of the tile, rather than
 * only at the whole units GetSlopePixelZ() works with.
 */
struct TileSurface {
	int32_t base = 0; ///< Height of the lowest corner, in world z units (#TILE_HEIGHT per height level).
	Slope slope = SLOPE_FLAT; ///< Slope after foundations, possibly with a half tile part.

	/**
	 * Height at a point of the tile.
	 * @param fx Position along the X axis of the tile, 0 to 1.
	 * @param fy Position along the Y axis of the tile, 0 to 1.
	 * @return The height, in world z units.
	 */
	double Height(double fx, double fy) const { return this->base + GetSlopeSurfaceZ(this->slope, fx, fy); }

	bool operator==(const TileSurface &other) const = default;
};

/** Everything the spline graph needs to know about one tile. */
struct SplineTileData {
	uint32_t x = 0; ///< X coordinate of the tile on the map.
	uint32_t y = 0; ///< Y coordinate of the tile on the map.
	TileSurface surface{}; ///< The surface vehicles drive on.
	TrackBits rail{}; ///< Rail track pieces through the tile.
	DiagDirection rail_stub = DiagDirection::Invalid; ///< Entrance of a rail depot; the track ends in the middle of the tile.
	RoadBits road{}; ///< Road pieces, including depots, stops and tunnel or bridge heads.
	RoadBits tram{}; ///< Tram pieces, like #road.
	bool sidewalks = false; ///< Whether the road has paved sidewalks, as town roads have.
	DiagDirection wormhole = DiagDirection::Invalid; ///< Direction of the bridge or tunnel starting here; only set on the head that owns the span.
	uint32_t wormhole_end_x = 0; ///< X coordinate of the other head of the bridge or tunnel.
	uint32_t wormhole_end_y = 0; ///< Y coordinate of the other head of the bridge or tunnel.
	int32_t wormhole_z = 0; ///< Height of the bridge deck or tunnel floor, in world z units.

	bool operator==(const SplineTileData &other) const = default;
};

/** Copy of the tiles of one chunk that carry tracks or roads. */
struct SplineChunkData {
	uint chunk_x = 0; ///< X coordinate of the chunk.
	uint chunk_y = 0; ///< Y coordinate of the chunk.
	std::vector<SplineTileData> tiles; ///< Tiles with tracks or roads, ordered by position.
};

TileSurface GetVehicleSurface(TileIndex tile);
void CaptureSplineChunk(uint chunk_x, uint chunk_y, SplineChunkData &out);
uint64_t HashSplineChunk(const SplineChunkData &data);

} // namespace fp

#endif /* FP_SPLINES_CAPTURE_H */
