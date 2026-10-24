/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file fp_splines.h The spline graph of tracks, roads, tram tracks and sidewalks. */

#ifndef FP_SPLINES_H
#define FP_SPLINES_H

#include <functional>
#include <optional>
#include <unordered_map>

#include "../core/fp_metric.h"
#include "fp_splines_capture.h"
#include "fp_splines_curve.h"
#include "fp_terrain_mesher.h"

namespace fp {

/** Separate networks of the spline graph; pieces of different layers never connect. */
enum class SplineLayer : uint8_t {
	Rail, ///< Railway tracks.
	Road, ///< Centre lines of roads.
	Tram, ///< Centre lines of tram tracks.
	Sidewalk, ///< Centre lines of the paved sidewalks of town roads.
	End, ///< End marker.
};

/**
 * Shape of a piece within its tile.
 *
 * The first six values equal the #Track values: a piece between the middles
 * of two tile edges. Rails take the diagonal tracks as short diagonals across
 * a corner, as the simulation drives them, while roads and tram tracks turn
 * by 90 degrees around the corner, as road vehicles do.
 */
enum class PieceShape : uint8_t {
	X, ///< Between the north-east and south-west edges, see Track::X.
	Y, ///< Between the north-west and south-east edges, see Track::Y.
	Upper, ///< Between the north-east and north-west edges, see Track::Upper.
	Lower, ///< Between the south-west and south-east edges, see Track::Lower.
	Left, ///< Between the south-west and north-west edges, see Track::Left.
	Right, ///< Between the north-east and south-east edges, see Track::Right.
	StubNE, ///< From the north-east edge to the middle of the tile: a dead end, depot or bay stop.
	StubSE, ///< From the south-east edge to the middle of the tile.
	StubSW, ///< From the south-west edge to the middle of the tile.
	StubNW, ///< From the north-west edge to the middle of the tile.
	Wormhole, ///< Span of a bridge or tunnel, from the head on this tile to the other head.
	SidewalkNE, ///< Sidewalk from the left side of the road leaving through the north-east edge to the next road counter-clockwise.
	SidewalkSE, ///< Sidewalk from the left side of the road leaving through the south-east edge.
	SidewalkSW, ///< Sidewalk from the left side of the road leaving through the south-west edge.
	SidewalkNW, ///< Sidewalk from the left side of the road leaving through the north-west edge.
	End, ///< End marker.
};

/**
 * Shape of the piece of a track.
 * @param track The track.
 * @return The shape.
 */
constexpr PieceShape TrackToPieceShape(Track track)
{
	return static_cast<PieceShape>(track);
}

/**
 * Whether a shape is one of the six tracks between two tile edges.
 * @param shape The shape.
 * @return True iff the shape corresponds to a #Track.
 */
constexpr bool IsTrackShape(PieceShape shape)
{
	return shape < PieceShape::StubNE;
}

/** Identity of a piece that survives rebuilding the graph: tile, layer and shape. */
struct PieceKey {
	uint32_t x = 0; ///< X coordinate of the tile on the map.
	uint32_t y = 0; ///< Y coordinate of the tile on the map.
	SplineLayer layer = SplineLayer::Rail; ///< Layer of the piece.
	PieceShape shape = PieceShape::X; ///< Shape of the piece within the tile.

	/**
	 * Pack the key into a number.
	 * @return The packed key.
	 */
	constexpr uint64_t Pack() const { return static_cast<uint64_t>(this->y) << 40 | static_cast<uint64_t>(this->x) << 16 | static_cast<uint64_t>(this->layer) << 8 | static_cast<uint64_t>(this->shape); }

	constexpr bool operator==(const PieceKey &other) const = default;
};

/**
 * Position of a vehicle in the terms of the simulation: a piece of a tile, and how far along it.
 * The fraction is measured along the straight chord between the ends of the
 * piece, which is how the simulation moves vehicles through a tile.
 */
struct SimPos {
	PieceKey key{}; ///< The piece.
	double t = 0; ///< Fraction of the way from the start to the end of the piece, 0 to 1.
};

using PieceID = uint32_t; ///< Index of a piece in the graph; only valid until the graph changes.
using PortID = uint32_t; ///< Index of a port in the graph; only valid until the graph changes.
static constexpr PieceID INVALID_PIECE = UINT32_MAX; ///< No piece.
static constexpr PortID INVALID_PORT = UINT32_MAX; ///< No port: the piece ends without connection.

/** Position on the graph: a piece and the arc length from its start. */
struct GraphPos {
	PieceID piece = INVALID_PIECE; ///< The piece.
	double s = 0; ///< Horizontal arc length from the start of the piece, in metres.
};

/** One end of a piece. */
struct PieceEnd {
	PieceID piece = INVALID_PIECE; ///< The piece.
	uint8_t end = 0; ///< 0 for the start, 1 for the end.

	constexpr bool operator==(const PieceEnd &other) const = default;
};

/** Point of a piece with its local frame. */
struct SplinePose {
	Vec3d position{}; ///< Position, in metres.
	Vec3d tangent{}; ///< Unit tangent in the direction of the piece, including the grade.
	Vec3d left{}; ///< Horizontal unit vector to the left of the tangent.
	double grade = 0; ///< Rise per horizontal distance.
	double curvature = 0; ///< Horizontal curvature in 1/metres; positive when turning left.
};

/**
 * The spline graph of tracks, roads, tram tracks and sidewalks (docs/fp/architecture.md, section 6.3).
 *
 * Every piece of track or road in a tile connects two ports: points in the
 * middle of tile edges (sidewalks have their ports beside the road). Pieces
 * meeting at a port share one point there, and the tangents are chosen so
 * that every route through the graph is G1-continuous:
 *  - one piece on either side: the joint cuts the corner with a circular arc
 *    touching both pieces, as far out as the shorter half piece allows; the
 *    shared point is the middle of the arc and the tangent there is the
 *    bisector of their directions. The 45 degree kinks of OpenTTD become
 *    arcs joined by straight track, which bend one way only. A curve that had
 *    to pass through the kink itself would make the straight track before a
 *    curve swing out the other way first;
 *  - one piece on one side and several on the other (a switch): the single
 *    piece keeps its direction, so the straight track stays straight, and
 *    the diverging branches start tangentially to it, as real switches do;
 *  - several on both sides (crossovers, junctions in station throats): a
 *    piece that continues straight on the other side keeps its direction,
 *    so straight and diagonal lines crossing at the port stay straight;
 *    the others start along the axis across the edge, as diverging branches.
 *    Only a route from a diagonal line onto another branch keeps its kink.
 * Each piece is made of up to four planar cubic Hermite segments (arcs and
 * straight parts, see GetFilletSplit()) with natural parametrisation, and has
 * a vertical profile that follows the surface vehicles drive on exactly,
 * with parabolic blends at every change of grade.
 *
 * The graph is built and updated chunk by chunk. Changing a chunk only
 * refits the pieces that share a port with it: tangents depend on the
 * pieces at a port, heights on those one port further.
 *
 * The graph lives in the runtime and is used with the game state lock held.
 */
class SplineGraph {
public:
	static constexpr double ROAD_LANE_WIDTH = 3.5; ///< Visual width of one lane, in metres; independent of the lateral offset in the simulation.
	static constexpr double SIDEWALK_OFFSET = 4.75; ///< Distance of the centre line of a sidewalk from the centre of a two-lane road, in metres.
	static constexpr double MAX_BLEND = 10.0; ///< Longest half-length of the blend at a change of grade, in metres.

	/** A piece of track, road or sidewalk. */
	struct Piece {
		PieceKey key{}; ///< Identity of the piece.
		ChunkIndex chunk = 0; ///< Chunk the piece belongs to.
		bool alive = false; ///< Whether the slot holds a piece.
		std::array<PortID, 2> ports{INVALID_PORT, INVALID_PORT}; ///< Ports at the start and the end.
		std::array<Vec3d, 2> points{}; ///< Start and end of the straight path of the simulation through the tile, horizontal position in metres.
		std::array<Vec3d, 2> directions{}; ///< Own unit directions of travel at the start and the end.
		std::array<int8_t, 2> signs{}; ///< Per end: +1 if the direction of travel goes along the canonical direction of the port, else -1.
		std::array<Vec3d, 2> end_tangents{}; ///< Unit tangents at the start and the end, chosen by the ports; along the direction of travel.
		bool via = false; ///< Whether the piece consists of two segments meeting at #via_point.
		Vec3d via_point{}; ///< Point between the two segments, in metres.
		Vec3d via_direction{}; ///< Unit direction of travel at #via_point.
		std::vector<HermiteSegment> segments; ///< The horizontal curve, one to four segments: arcs and straight parts, see GetFilletSplit().
		double length = 0; ///< Horizontal length, in metres.
		TileSurface surface{}; ///< Surface of the tile, for heights.
		bool level = false; ///< Whether the piece keeps the height #level_z instead of following #surface (bridges and tunnels).
		double level_z = 0; ///< Height of a level piece, in metres.
		std::array<double, 2> end_z{}; ///< Height of the surface at the start and the end, in metres.
		double mid_z = 0; ///< Height of the surface halfway along the piece, in metres.
		VerticalProfile profile{}; ///< Heights along the piece.
	};

	/** A point where pieces meet. */
	struct Port {
		uint64_t key = 0; ///< Packed identity of the port.
		bool alive = false; ///< Whether the slot holds a port.
		Vec3d canonical{}; ///< Unit direction across the tile edge, +X or +Y.
		Vec3d base{}; ///< The point on the tile edge, horizontal position in metres.
		Vec3d position{}; ///< Shared point of the curves, horizontal position in metres; #base moved into the bend of the joint.
		double arc = 0; ///< At a joint that bends: distance from #base along both pieces at which the arc of the joint meets them, in metres.
		std::vector<PieceEnd> ends; ///< Piece ends at this port.
		std::array<uint8_t, 2> side_count{}; ///< Number of pieces on the negative and positive side.
		double z = 0; ///< Height of the port, in metres.
		double blend = 0; ///< Half-length of the blends at the port, in metres.
		std::array<double, 2> side_grade{}; ///< Average grade of the pieces on the negative and positive side, along the canonical direction.
	};

	/** Counters for diagnostics. */
	struct Statistics {
		std::array<uint, to_underlying(SplineLayer::End)> pieces{}; ///< Pieces per layer.
		uint ports = 0; ///< Ports in use.
		uint64_t refits = 0; ///< Pieces fitted since the last reset.
	};

	void Reset(const MetricScale &scale);
	void SetChunk(ChunkIndex index, const SplineChunkData &data, std::vector<ChunkIndex> &changed);
	void RemoveChunk(ChunkIndex index, std::vector<ChunkIndex> &changed);

	PieceID Find(const PieceKey &key) const;

	/**
	 * A piece.
	 * @param id The piece; must be valid.
	 * @return The piece.
	 */
	const Piece &GetPiece(PieceID id) const { return this->pieces[id]; }

	/**
	 * A port.
	 * @param id The port; must be valid.
	 * @return The port.
	 */
	const Port &GetPort(PortID id) const { return this->ports[id]; }

	SplinePose Evaluate(PieceID id, double s, double lateral = 0.0) const;
	void GetNext(PieceID id, uint8_t end, std::vector<PieceEnd> &out) const;

	std::optional<GraphPos> Locate(const SimPos &pos) const;
	SimPos ToSimPos(const GraphPos &pos) const;
	double ChordFraction(PieceID id, const Vec3d &point) const;

	void ForEachPiece(ChunkIndex index, const std::function<void(PieceID)> &func) const;
	void BuildOverlay(ChunkIndex index, const Vec3d &origin, TerrainMesh &out) const;

	/**
	 * Diagnostic counters.
	 * @return The counters.
	 */
	const Statistics &GetStatistics() const { return this->statistics; }

	/**
	 * The metric scales the graph is built with.
	 * @return The scales.
	 */
	const MetricScale &GetScale() const { return this->scale; }

private:
	MetricScale scale{}; ///< The metric scales.
	std::vector<Piece> pieces; ///< All pieces, alive or not.
	std::vector<PieceID> free_pieces; ///< Slots of #pieces that are not alive.
	std::unordered_map<uint64_t, PieceID> piece_index; ///< Pieces by packed key.
	std::vector<Port> ports; ///< All ports, alive or not.
	std::vector<PortID> free_ports; ///< Slots of #ports that are not alive.
	std::unordered_map<uint64_t, PortID> port_index; ///< Ports by packed key.
	std::unordered_map<ChunkIndex, std::vector<PieceID>> chunk_pieces; ///< Pieces per chunk.
	Statistics statistics{}; ///< Diagnostic counters.

	/* Scratch space and stamps for the update. */
	std::vector<uint32_t> piece_stamps; ///< Per piece: stamp of the last pass that visited it.
	std::vector<uint32_t> port_stamps; ///< Per port: stamp of the last pass that visited it.
	uint32_t stamp = 0; ///< Current stamp.
	std::vector<PortID> touched_ports; ///< Ports whose pieces changed.
	std::vector<PieceID> refit_pieces; ///< Pieces whose curve must be refitted.
	std::vector<PortID> height_ports; ///< Ports whose heights must be recomputed.
	std::vector<PieceID> profile_pieces; ///< Pieces whose vertical profile must be rebuilt.

	/** A piece end at a port, with its own direction along the canonical direction of the port. */
	struct Arrival {
		PieceID piece; ///< The piece.
		uint8_t end; ///< The end at the port.
		uint side; ///< 0 for the negative side of the port, 1 for the positive side.
		Vec3d direction; ///< Own direction of the piece, along the canonical direction.
	};
	std::vector<Arrival> arrivals; ///< Scratch list of the piece ends at a port.

	void AddPieces(ChunkIndex index, const SplineTileData &tile);
	PieceID AddPiece(ChunkIndex index, const PieceKey &key, const SplineTileData &tile, const std::array<Vec3d, 2> &points, const std::array<Vec3d, 2> &directions, const std::array<uint64_t, 2> &port_keys);
	void RemovePiece(PieceID id);
	PortID AttachPort(uint64_t key, const Vec3d &canonical, const Vec3d &base, PieceEnd end);
	void DetachPort(PortID id, PieceEnd end);
	void Update(std::vector<ChunkIndex> &changed);
	void UpdateTangent(Port &port);
	void FitCurve(Piece &piece);
	void UpdateHeights(Port &port);
	void BuildProfile(Piece &piece);
	double SurfaceHeight(const Piece &piece, const Vec3d &point) const;
	Vec3d TileToMetres(uint32_t x, uint32_t y, double lx, double ly) const;
	bool Visit(std::vector<uint32_t> &stamps, uint32_t id);
};

} // namespace fp

#endif /* FP_SPLINES_H */
