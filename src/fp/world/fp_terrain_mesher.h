/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file fp_terrain_mesher.h Triangle meshes of the bare terrain. */

#ifndef FP_TERRAIN_MESHER_H
#define FP_TERRAIN_MESHER_H

#include "../core/fp_metric.h"
#include "fp_tile_capture.h"

namespace fp {

/** Vertex of a terrain mesh; 16 bytes. */
struct TerrainVertex {
	float x; ///< X coordinate relative to the chunk origin, in metres.
	float y; ///< Y coordinate relative to the chunk origin, in metres.
	float z; ///< Height, in metres.
	uint8_t r; ///< Red component of the colour.
	uint8_t g; ///< Green component of the colour.
	uint8_t b; ///< Blue component of the colour.
	uint8_t a; ///< Alpha component of the colour.
};
static_assert(sizeof(TerrainVertex) == 16);

/** Indexed triangle list of one chunk. */
struct TerrainMesh {
	std::vector<TerrainVertex> vertices; ///< The vertices.
	std::vector<uint16_t> indices; ///< Three indices per triangle, counter-clockwise seen from above.
};

/** The diagonal along which the surface of a tile is split into two triangles. */
enum class TileDiagonal : uint8_t {
	NorthSouth, ///< Split between the north and the south corner.
	WestEast, ///< Split between the west and the east corner.
};

/** Colour in sRGB. */
struct TerrainColour {
	uint8_t r; ///< Red.
	uint8_t g; ///< Green.
	uint8_t b; ///< Blue.
};

TileDiagonal GetTileDiagonal(int north, int west, int east, int south);
double GetTerrainSurfaceHeight(int north, int west, int east, int south, double fx, double fy);
TerrainColour GetTerrainColour(TerrainClass terrain_class);
Vec3d GetChunkOrigin(uint chunk_x, uint chunk_y, const MetricScale &scale);
void MeshTerrainChunk(const TerrainChunkData &data, const MetricScale &scale, TerrainMesh &out);

} // namespace fp

#endif /* FP_TERRAIN_MESHER_H */
