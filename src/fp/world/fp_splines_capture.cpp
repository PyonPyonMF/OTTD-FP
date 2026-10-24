/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file fp_splines_capture.cpp Copies of what the spline graph needs from the map: tracks, roads and the surface vehicles drive on. */

#include "../../stdafx.h"
#include "fp_splines_capture.h"
#include "fp_terrain_mesher.h"
#include "../../bridge.h"
#include "../../bridge_map.h"
#include "../../landscape.h"
#include "../../map_func.h"
#include "../../rail_map.h"
#include "../../road_map.h"
#include "../../slope_func.h"
#include "../../tile_cmd.h"
#include "../../track_func.h"
#include "../../tunnelbridge_map.h"

#include "../../safeguards.h"

namespace fp {

/**
 * Height of a point of a sloped tile, without rounding.
 *
 * This is the continuous counterpart of GetPartialPixelZ(), including half
 * tile slopes and steep slopes: at whole units it agrees with it within its
 * rounding.
 * @param slope The slope, possibly with a half tile part.
 * @param fx Position along the X axis of the tile, 0 to 1.
 * @param fy Position along the Y axis of the tile, 0 to 1.
 * @return Height above the lowest corner, in world z units.
 */
double GetSlopeSurfaceZ(Slope slope, double fx, double fy)
{
	if (IsHalftileSlope(slope)) {
		/* The same half planes as GetPartialPixelZ(), which is levelled on the half at the given corner. */
		const double x = fx * TILE_SIZE;
		const double y = fy * TILE_SIZE;
		bool levelled;
		switch (GetHalftileSlopeCorner(slope)) {
			case Corner::W: levelled = x > y; break;
			case Corner::S: levelled = x + y >= TILE_SIZE; break;
			case Corner::E: levelled = x <= y; break;
			default: levelled = x + y < TILE_SIZE; break;
		}
		if (levelled) return GetSlopeMaxPixelZ(slope);
		slope = RemoveHalftileSlope(slope);
	}

	return GetTerrainSurfaceHeight(
			GetSlopeZInCorner(slope, Corner::N) * TILE_HEIGHT,
			GetSlopeZInCorner(slope, Corner::W) * TILE_HEIGHT,
			GetSlopeZInCorner(slope, Corner::E) * TILE_HEIGHT,
			GetSlopeZInCorner(slope, Corner::S) * TILE_HEIGHT,
			fx, fy);
}

/**
 * Surface that ground vehicles drive on, exactly as the get_slope_pixel_z_proc of the tile computes it.
 *
 * Tracks, roads, depots, stations and crossings all use the slope and height
 * after their foundation (see GetFoundationSlope()). Tunnel entrances keep
 * vehicles at the height of the entrance, and bridge heads put them either on
 * a ramp or one level above their foundation, see GetSlopePixelZ_TunnelBridge().
 * @param tile The tile.
 * @return The surface.
 * @pre The game state lock is held.
 */
TileSurface GetVehicleSurface(TileIndex tile)
{
	if (IsTileType(tile, TileType::TunnelBridge)) {
		auto [tileh, z] = GetTilePixelSlope(tile);
		if (IsTunnel(tile)) return {z, SLOPE_FLAT};

		DiagDirection dir = GetTunnelBridgeDirection(tile);
		z += ApplyPixelFoundationToSlope(GetBridgeFoundation(tileh, DiagDirToAxis(dir)), tileh);
		if (tileh != SLOPE_FLAT) return {z + static_cast<int>(TILE_HEIGHT), SLOPE_FLAT};

		static constexpr DiagDirectionIndexArray<Slope> RAMPS{SLOPE_NE, SLOPE_SE, SLOPE_SW, SLOPE_NW};
		return {z, RAMPS[dir]};
	}

	auto [slope, z] = GetFoundationPixelSlope(tile);
	return {z, slope};
}

/**
 * Whether the roadside of a road tile has paved sidewalks.
 * @param tile The tile.
 * @return True iff the road is drawn with sidewalks.
 */
static bool HasSidewalks(TileIndex tile)
{
	if (!IsTileType(tile, TileType::Road) || GetRoadTileType(tile) == RoadTileType::Depot) return false;
	switch (GetRoadside(tile)) {
		case Roadside::Barren:
		case Roadside::Grass:
		case Roadside::GrassRoadWorks:
			return false;
		default:
			return true;
	}
}

/**
 * Copy what the spline graph needs of one tile.
 * @param tile The tile.
 * @param[out] out Receives the data.
 * @return True iff the tile carries tracks or roads.
 * @pre The game state lock is held.
 */
static bool CaptureSplineTile(TileIndex tile, SplineTileData &out)
{
	switch (GetTileType(tile)) {
		case TileType::Railway:
		case TileType::Road:
		case TileType::Station:
		case TileType::TunnelBridge:
			break;
		default:
			return false;
	}

	out = {};
	out.x = TileX(tile);
	out.y = TileY(tile);

	if (IsTileType(tile, TileType::Railway) && IsRailDepot(tile)) {
		out.rail_stub = GetRailDepotDirection(tile);
	} else {
		out.rail = TrackdirBitsToTrackBits(GetTileTrackStatus(tile, TransportType::Rail, RoadTramType::Invalid).trackdirs);
	}
	out.road = GetAnyRoadBits(tile, RoadTramType::Road, true);
	out.tram = GetAnyRoadBits(tile, RoadTramType::Tram, true);
	out.sidewalks = out.road.Any() && HasSidewalks(tile);

	if (IsTileType(tile, TileType::TunnelBridge) && GetTunnelBridgeTransportType(tile) != TransportType::Water) {
		/* The span belongs to the head with the lower index, so it is built once. */
		TileIndex other = GetOtherTunnelBridgeEnd(tile);
		if (tile < other) {
			out.wormhole = GetTunnelBridgeDirection(tile);
			out.wormhole_end_x = TileX(other);
			out.wormhole_end_y = TileY(other);
			out.wormhole_z = IsTunnel(tile) ? GetTilePixelZ(tile) : GetBridgeHeight(tile) * TILE_HEIGHT;
		}
	}

	if (out.rail.None() && out.rail_stub == DiagDirection::Invalid && out.road.None() && out.tram.None()) return false;

	out.surface = GetVehicleSurface(tile);
	return true;
}

/**
 * Copy what the spline graph needs of one chunk.
 * @param chunk_x X coordinate of the chunk.
 * @param chunk_y Y coordinate of the chunk.
 * @param[out] out Receives the copy.
 * @pre The game state lock is held.
 */
void CaptureSplineChunk(uint chunk_x, uint chunk_y, SplineChunkData &out)
{
	out.chunk_x = chunk_x;
	out.chunk_y = chunk_y;
	out.tiles.clear();

	uint x0 = chunk_x * CHUNK_SIZE;
	uint y0 = chunk_y * CHUNK_SIZE;
	uint x1 = std::min(x0 + CHUNK_SIZE, Map::SizeX());
	uint y1 = std::min(y0 + CHUNK_SIZE, Map::SizeY());

	SplineTileData tile_data;
	for (uint y = y0; y < y1; y++) {
		for (uint x = x0; x < x1; x++) {
			if (CaptureSplineTile(TileXY(x, y), tile_data)) out.tiles.push_back(tile_data);
		}
	}
}

/**
 * Fingerprint of everything the spline graph of a chunk depends on.
 * Signals and animations mark tiles dirty all the time without changing
 * tracks or roads; comparing fingerprints avoids rebuilding for them.
 * @param data The captured chunk.
 * @return 64-bit FNV-1a hash of the data.
 */
uint64_t HashSplineChunk(const SplineChunkData &data)
{
	uint64_t hash = 0xCBF29CE484222325ULL;
	auto mix = [&hash](uint64_t value, int bytes) {
		for (int i = 0; i < bytes; i++) {
			hash ^= (value >> (i * 8)) & 0xFF;
			hash *= 0x100000001B3ULL;
		}
	};

	mix(data.tiles.size(), 4);
	for (const SplineTileData &t : data.tiles) {
		mix(t.x, 4);
		mix(t.y, 4);
		mix(static_cast<uint32_t>(t.surface.base), 4);
		mix(t.surface.slope, 1);
		mix(t.rail.base(), 1);
		mix(to_underlying(t.rail_stub), 1);
		mix(t.road.base(), 1);
		mix(t.tram.base(), 1);
		mix(t.sidewalks, 1);
		mix(to_underlying(t.wormhole), 1);
		mix(t.wormhole_end_x, 4);
		mix(t.wormhole_end_y, 4);
		mix(static_cast<uint32_t>(t.wormhole_z), 4);
	}
	return hash;
}

} // namespace fp
