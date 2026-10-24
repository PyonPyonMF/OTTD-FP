/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file fp_splines.cpp The spline graph of tracks, roads, tram tracks and sidewalks. */

#include "../../stdafx.h"
#include "fp_splines.h"
#include "../../direction_func.h"
#include "../../road_func.h"
#include "../../track_func.h"

#include "../../safeguards.h"

namespace fp {

/** Middle and outward normal of a tile edge, in tile units. */
struct EdgeGeometry {
	double x; ///< X coordinate of the middle of the edge within the tile.
	double y; ///< Y coordinate of the middle of the edge within the tile.
	double nx; ///< X component of the outward normal.
	double ny; ///< Y component of the outward normal.
};

/** Geometry of the four tile edges. */
static constexpr DiagDirectionIndexArray<EdgeGeometry> EDGES{{{
	{0, 8, -1, 0}, // NE
	{8, 16, 0, 1}, // SE
	{16, 8, 1, 0}, // SW
	{8, 0, 0, -1}, // NW
}}};

/** Tile edges in counter-clockwise order seen from above, i.e. by increasing angle of their normal. */
static constexpr std::array<DiagDirection, 4> EDGES_CCW = {DiagDirection::SW, DiagDirection::SE, DiagDirection::NE, DiagDirection::NW};

static constexpr uint64_t NO_PORT = UINT64_MAX; ///< Port key of a piece end without port.
static constexpr uint PORT_SLOT_MIDDLE = 0; ///< Port in the middle of a tile edge.
static constexpr uint PORT_SLOT_LOW = 1; ///< Port on the edge, before its middle.
static constexpr uint PORT_SLOT_HIGH = 2; ///< Port on the edge, after its middle.
static constexpr double OVERLAY_LIFT = 0.25; ///< Height of the debug overlay above the pieces, in metres.

/**
 * Unit normal of a tile edge, pointing out of the tile.
 * @param edge The edge.
 * @return The normal.
 */
static Vec3d EdgeNormal(DiagDirection edge)
{
	return {EDGES[edge].nx, EDGES[edge].ny, 0.0};
}

/**
 * The horizontal vector rotated by 90 degrees counter-clockwise, i.e. pointing to its left.
 * @param v The vector.
 * @return The rotated vector.
 */
static Vec3d LeftOf(const Vec3d &v)
{
	return {-v.y, v.x, 0.0};
}

/**
 * Identity of a port.
 * Each tile edge is shared by two tiles; it is named after the tile on its
 * south-west or south-east side, i.e. as the north-east or north-west edge.
 * @param x X coordinate of the tile.
 * @param y Y coordinate of the tile.
 * @param edge Edge of the tile.
 * @param slot Position along the edge, one of the PORT_SLOT_ constants.
 * @param layer The layer.
 * @return The packed key.
 */
static uint64_t MakePortKey(uint32_t x, uint32_t y, DiagDirection edge, uint slot, SplineLayer layer)
{
	uint axis = 0;
	switch (edge) {
		case DiagDirection::NE: break;
		case DiagDirection::SW: x++; break;
		case DiagDirection::NW: axis = 1; break;
		default: y++; axis = 1; break;
	}
	return static_cast<uint64_t>(y) << 40 | static_cast<uint64_t>(x) << 16 | static_cast<uint64_t>(layer) << 8 | axis << 4 | slot;
}

/**
 * Direction across the edge a port lies on.
 * @param key The packed key of the port.
 * @return +X or +Y.
 */
static Vec3d PortCanonical(uint64_t key)
{
	return ((key >> 4) & 1) == 0 ? Vec3d{1, 0, 0} : Vec3d{0, 1, 0};
}

/**
 * Slot of a point on a tile edge beside its middle.
 * @param edge The edge.
 * @param x X coordinate of the point within the tile, in tile units.
 * @param y Y coordinate of the point within the tile, in tile units.
 * @return #PORT_SLOT_LOW or #PORT_SLOT_HIGH.
 */
static uint SideSlot(DiagDirection edge, double x, double y)
{
	double along = (edge == DiagDirection::NE || edge == DiagDirection::SW) ? y : x;
	return along < TILE_SIZE / 2.0 ? PORT_SLOT_LOW : PORT_SLOT_HIGH;
}

/**
 * Segment and curve parameter at an arc length of a piece.
 * @param piece The piece.
 * @param s Horizontal arc length from the start, in metres.
 * @return The segment and the parameter on it.
 */
static std::pair<const HermiteSegment *, double> CurveParameter(const SplineGraph::Piece &piece, double s)
{
	size_t i = 0;
	while (i + 1 < piece.segments.size() && s > piece.segments[i].Length()) {
		s -= piece.segments[i].Length();
		i++;
	}
	return {&piece.segments[i], piece.segments[i].ParameterAt(s)};
}

/**
 * Forget everything and start over.
 * @param scale The metric scales to build with.
 */
void SplineGraph::Reset(const MetricScale &scale)
{
	this->scale = scale;
	this->pieces.clear();
	this->free_pieces.clear();
	this->piece_index.clear();
	this->ports.clear();
	this->free_ports.clear();
	this->port_index.clear();
	this->chunk_pieces.clear();
	this->piece_stamps.clear();
	this->port_stamps.clear();
	this->stamp = 0;
	this->statistics = {};
}

/**
 * Position of a point of a tile.
 * @param x X coordinate of the tile.
 * @param y Y coordinate of the tile.
 * @param lx X coordinate within the tile, in tile units.
 * @param ly Y coordinate within the tile, in tile units.
 * @return The horizontal position, in metres.
 */
Vec3d SplineGraph::TileToMetres(uint32_t x, uint32_t y, double lx, double ly) const
{
	return {this->scale.UnitsToMetres(x * TILE_SIZE + lx), this->scale.UnitsToMetres(y * TILE_SIZE + ly), 0.0};
}

/**
 * Mark an element as visited in the current pass.
 * @param stamps Stamps of the elements.
 * @param id The element.
 * @return True iff the element was not visited before in this pass.
 */
bool SplineGraph::Visit(std::vector<uint32_t> &stamps, uint32_t id)
{
	if (id >= stamps.size()) stamps.resize(id + 1, 0);
	if (stamps[id] == this->stamp) return false;
	stamps[id] = this->stamp;
	return true;
}

/**
 * Replace the pieces of a chunk.
 * @param index The chunk.
 * @param data Copy of the tracks and roads of the chunk.
 * @param[out] changed Chunks whose pieces changed shape are appended to this, possibly more than once.
 */
void SplineGraph::SetChunk(ChunkIndex index, const SplineChunkData &data, std::vector<ChunkIndex> &changed)
{
	this->touched_ports.clear();
	this->refit_pieces.clear();

	if (auto it = this->chunk_pieces.find(index); it != this->chunk_pieces.end()) {
		for (PieceID id : it->second) this->RemovePiece(id);
		it->second.clear();
	}
	for (const SplineTileData &tile : data.tiles) this->AddPieces(index, tile);
	if (auto it = this->chunk_pieces.find(index); it != this->chunk_pieces.end() && it->second.empty()) this->chunk_pieces.erase(it);

	changed.push_back(index);
	this->Update(changed);
}

/**
 * Remove the pieces of a chunk.
 * @param index The chunk.
 * @param[out] changed Chunks whose pieces changed shape are appended to this, possibly more than once.
 */
void SplineGraph::RemoveChunk(ChunkIndex index, std::vector<ChunkIndex> &changed)
{
	auto it = this->chunk_pieces.find(index);
	if (it == this->chunk_pieces.end()) return;

	this->touched_ports.clear();
	this->refit_pieces.clear();
	for (PieceID id : it->second) this->RemovePiece(id);
	this->chunk_pieces.erase(it);

	changed.push_back(index);
	this->Update(changed);
}

/**
 * Create the pieces of one tile.
 * @param index Chunk of the tile.
 * @param tile Copy of the tile.
 */
void SplineGraph::AddPieces(ChunkIndex index, const SplineTileData &tile)
{
	const uint32_t x = tile.x;
	const uint32_t y = tile.y;
	auto edge_point = [&](DiagDirection edge) { return this->TileToMetres(x, y, EDGES[edge].x, EDGES[edge].y); };
	const Vec3d centre = this->TileToMetres(x, y, TILE_SIZE / 2.0, TILE_SIZE / 2.0);

	/* The two edges of a track, in the order of its first trackdir. */
	auto track_edges = [](Track track) {
		Trackdir trackdir = TrackToTrackdir(track);
		return std::pair{TrackdirToExitdir(ReverseTrackdir(trackdir)), TrackdirToExitdir(trackdir)};
	};

	auto add_stub = [&](SplineLayer layer, DiagDirection edge) {
		Vec3d direction = EdgeNormal(edge) * -1.0;
		PieceShape shape = static_cast<PieceShape>(to_underlying(PieceShape::StubNE) + to_underlying(edge));
		this->AddPiece(index, {x, y, layer, shape}, tile, {edge_point(edge), centre}, {direction, direction}, {MakePortKey(x, y, edge, PORT_SLOT_MIDDLE, layer), NO_PORT});
	};

	/* Rails: the diagonal tracks are short diagonals across a corner. */
	for (Track track : tile.rail) {
		auto [from, to] = track_edges(track);
		Vec3d p0 = edge_point(from);
		Vec3d p1 = edge_point(to);
		Vec3d direction = Normalise(p1 - p0);
		this->AddPiece(index, {x, y, SplineLayer::Rail, TrackToPieceShape(track)}, tile, {p0, p1}, {direction, direction},
				{MakePortKey(x, y, from, PORT_SLOT_MIDDLE, SplineLayer::Rail), MakePortKey(x, y, to, PORT_SLOT_MIDDLE, SplineLayer::Rail)});
	}
	if (tile.rail_stub != DiagDirection::Invalid) add_stub(SplineLayer::Rail, tile.rail_stub);

	/* Roads and tram tracks: every pair of road bits is a piece, turning around the corner; a single bit ends in the middle. */
	auto add_road = [&](SplineLayer layer, RoadBits bits) {
		if (bits.None()) return;
		auto has = [bits](DiagDirection edge) { return bits.Any(DiagDirToRoadBits(edge)); };
		if (bits.Count() == 1) {
			for (DiagDirection edge : EDGES_CCW) {
				if (has(edge)) add_stub(layer, edge);
			}
			return;
		}
		for (Track track : TRACK_BIT_ALL) {
			auto [from, to] = track_edges(track);
			if (!has(from) || !has(to)) continue;
			this->AddPiece(index, {x, y, layer, TrackToPieceShape(track)}, tile, {edge_point(from), edge_point(to)}, {EdgeNormal(from) * -1.0, EdgeNormal(to)},
					{MakePortKey(x, y, from, PORT_SLOT_MIDDLE, layer), MakePortKey(x, y, to, PORT_SLOT_MIDDLE, layer)});
		}
	};
	add_road(SplineLayer::Road, tile.road);
	add_road(SplineLayer::Tram, tile.tram);

	/* Sidewalks: one between every two neighbouring roads, counter-clockwise; around the end of a dead end. */
	if (tile.sidewalks) {
		const double offset = this->scale.MetresToUnits(SIDEWALK_OFFSET);
		auto has = [&tile](DiagDirection edge) { return tile.road.Any(DiagDirToRoadBits(edge)); };
		for (size_t i = 0; i < EDGES_CCW.size(); i++) {
			DiagDirection from = EDGES_CCW[i];
			if (!has(from)) continue;
			DiagDirection to = from;
			for (size_t j = 1; j < EDGES_CCW.size(); j++) {
				if (has(EDGES_CCW[(i + j) % EDGES_CCW.size()])) {
					to = EDGES_CCW[(i + j) % EDGES_CCW.size()];
					break;
				}
			}

			const EdgeGeometry &a = EDGES[from];
			const EdgeGeometry &b = EDGES[to];
			/* Left of a road leaving through edge 'from', right of the one leaving through 'to'. */
			double x0 = a.x - a.ny * offset, y0 = a.y + a.nx * offset;
			double x1 = b.x + b.ny * offset, y1 = b.y - b.nx * offset;
			PieceShape shape = static_cast<PieceShape>(to_underlying(PieceShape::SidewalkNE) + to_underlying(from));
			PieceID id = this->AddPiece(index, {x, y, SplineLayer::Sidewalk, shape}, tile,
					{this->TileToMetres(x, y, x0, y0), this->TileToMetres(x, y, x1, y1)}, {EdgeNormal(from) * -1.0, EdgeNormal(to)},
					{MakePortKey(x, y, from, SideSlot(from, x0, y0), SplineLayer::Sidewalk), MakePortKey(x, y, to, SideSlot(to, x1, y1), SplineLayer::Sidewalk)});

			if (to == from && id != INVALID_PIECE) {
				/* Around the end of the road, which is in the middle of the tile. */
				Piece &piece = this->pieces[id];
				piece.via = true;
				piece.via_point = this->TileToMetres(x, y, TILE_SIZE / 2.0 - a.nx * offset, TILE_SIZE / 2.0 - a.ny * offset);
				piece.via_direction = LeftOf(EdgeNormal(from)) * -1.0;
			}
		}
	}

	/* The span of a bridge or tunnel, level from head to head. */
	if (tile.wormhole != DiagDirection::Invalid) {
		DiagDirection back = ReverseDiagDir(tile.wormhole);
		Vec3d p0 = edge_point(tile.wormhole);
		Vec3d p1 = this->TileToMetres(tile.wormhole_end_x, tile.wormhole_end_y, EDGES[back].x, EDGES[back].y);
		Vec3d direction = EdgeNormal(tile.wormhole);

		auto add_wormhole = [&](SplineLayer layer) {
			PieceID id = this->AddPiece(index, {x, y, layer, PieceShape::Wormhole}, tile, {p0, p1}, {direction, direction},
					{MakePortKey(x, y, tile.wormhole, PORT_SLOT_MIDDLE, layer), MakePortKey(tile.wormhole_end_x, tile.wormhole_end_y, back, PORT_SLOT_MIDDLE, layer)});
			if (id == INVALID_PIECE) return;
			this->pieces[id].level = true;
			this->pieces[id].level_z = this->scale.WorldZToMetres(tile.wormhole_z);
		};
		if (tile.rail.Any()) add_wormhole(SplineLayer::Rail);
		if (tile.road.Any()) add_wormhole(SplineLayer::Road);
		if (tile.tram.Any()) add_wormhole(SplineLayer::Tram);
	}
}

/**
 * Create a piece and attach it to its ports.
 * @param index Chunk the piece belongs to.
 * @param key Identity of the piece.
 * @param tile The tile of the piece.
 * @param points Start and end, in metres.
 * @param directions Own unit directions of travel at the start and the end.
 * @param port_keys Keys of the ports at the start and the end, or #NO_PORT.
 * @return The piece, or #INVALID_PIECE if it already exists.
 */
PieceID SplineGraph::AddPiece(ChunkIndex index, const PieceKey &key, const SplineTileData &tile, const std::array<Vec3d, 2> &points, const std::array<Vec3d, 2> &directions, const std::array<uint64_t, 2> &port_keys)
{
	uint64_t packed = key.Pack();
	if (this->piece_index.contains(packed)) return INVALID_PIECE;

	PieceID id;
	if (!this->free_pieces.empty()) {
		id = this->free_pieces.back();
		this->free_pieces.pop_back();
	} else {
		id = static_cast<PieceID>(this->pieces.size());
		this->pieces.emplace_back();
	}

	Piece &piece = this->pieces[id];
	piece = {};
	piece.key = key;
	piece.chunk = index;
	piece.alive = true;
	piece.points = points;
	piece.directions = directions;
	piece.surface = tile.surface;
	for (uint8_t end = 0; end < 2; end++) {
		if (port_keys[end] == NO_PORT) continue;
		Vec3d canonical = PortCanonical(port_keys[end]);
		piece.signs[end] = Dot(directions[end], canonical) > 0 ? 1 : -1;
		piece.ports[end] = this->AttachPort(port_keys[end], canonical, points[end], {id, end});
	}

	this->piece_index[packed] = id;
	this->chunk_pieces[index].push_back(id);
	this->refit_pieces.push_back(id);
	this->statistics.pieces[to_underlying(key.layer)]++;
	return id;
}

/**
 * Detach a piece from its ports and free it.
 * @param id The piece.
 */
void SplineGraph::RemovePiece(PieceID id)
{
	Piece &piece = this->pieces[id];
	for (uint8_t end = 0; end < 2; end++) {
		if (piece.ports[end] != INVALID_PORT) this->DetachPort(piece.ports[end], {id, end});
	}
	this->piece_index.erase(piece.key.Pack());
	this->statistics.pieces[to_underlying(piece.key.layer)]--;
	piece.alive = false;
	this->free_pieces.push_back(id);
}

/**
 * Attach a piece end to a port, creating the port if needed.
 * @param key Identity of the port.
 * @param canonical Direction across the edge of the port.
 * @param base The point on the tile edge.
 * @param end The piece end.
 * @return The port.
 */
PortID SplineGraph::AttachPort(uint64_t key, const Vec3d &canonical, const Vec3d &base, PieceEnd end)
{
	PortID id;
	if (auto it = this->port_index.find(key); it != this->port_index.end()) {
		id = it->second;
	} else {
		if (!this->free_ports.empty()) {
			id = this->free_ports.back();
			this->free_ports.pop_back();
		} else {
			id = static_cast<PortID>(this->ports.size());
			this->ports.emplace_back();
		}
		Port &port = this->ports[id];
		port = {};
		port.key = key;
		port.alive = true;
		port.canonical = canonical;
		port.base = base;
		port.position = base;
		this->port_index[key] = id;
		this->statistics.ports++;
	}

	this->ports[id].ends.push_back(end);
	this->touched_ports.push_back(id);
	return id;
}

/**
 * Detach a piece end from a port. Empty ports are freed by Update().
 * @param id The port.
 * @param end The piece end.
 */
void SplineGraph::DetachPort(PortID id, PieceEnd end)
{
	std::vector<PieceEnd> &ends = this->ports[id].ends;
	ends.erase(std::remove(ends.begin(), ends.end(), end), ends.end());
	this->touched_ports.push_back(id);
}

/**
 * Bring everything that depends on the touched ports up to date.
 *
 * Tangents depend only on the pieces at a port, so the curves of the pieces
 * at touched ports are refitted. Their lengths and heights feed the heights
 * and grades of the ports at both of their ends, and those in turn the
 * vertical profiles of all pieces there. Nothing further away is affected.
 * @param[out] changed Chunks of pieces that changed are appended to this.
 */
void SplineGraph::Update(std::vector<ChunkIndex> &changed)
{
	/* Tangents of the touched ports; free those that became empty. */
	this->stamp++;
	for (PortID id : this->touched_ports) {
		if (!this->Visit(this->port_stamps, id)) continue;
		Port &port = this->ports[id];
		if (!port.alive) continue;
		if (port.ends.empty()) {
			this->port_index.erase(port.key);
			port.alive = false;
			this->free_ports.push_back(id);
			this->statistics.ports--;
			continue;
		}
		this->UpdateTangent(port);
		for (const PieceEnd &end : port.ends) this->refit_pieces.push_back(end.piece);
	}

	/* Curves of the pieces at those ports. */
	this->stamp++;
	this->height_ports.clear();
	for (PieceID id : this->refit_pieces) {
		if (!this->Visit(this->piece_stamps, id) || !this->pieces[id].alive) continue;
		Piece &piece = this->pieces[id];
		this->FitCurve(piece);
		this->statistics.refits++;
		for (PortID port : piece.ports) {
			if (port != INVALID_PORT) this->height_ports.push_back(port);
		}
	}

	/* Heights of the ports at both ends of those pieces. */
	this->stamp++;
	this->profile_pieces.assign(this->refit_pieces.begin(), this->refit_pieces.end());
	for (PortID id : this->height_ports) {
		if (!this->Visit(this->port_stamps, id) || !this->ports[id].alive) continue;
		Port &port = this->ports[id];
		this->UpdateHeights(port);
		for (const PieceEnd &end : port.ends) this->profile_pieces.push_back(end.piece);
	}

	/* Vertical profiles of all pieces at those ports. */
	this->stamp++;
	for (PieceID id : this->profile_pieces) {
		if (!this->Visit(this->piece_stamps, id) || !this->pieces[id].alive) continue;
		this->BuildProfile(this->pieces[id]);
		changed.push_back(this->pieces[id].chunk);
	}

	this->touched_ports.clear();
	this->refit_pieces.clear();
}

/**
 * Choose the shared point of a port and the tangents of the pieces there; see the class description.
 * @param port The port.
 */
void SplineGraph::UpdateTangent(Port &port)
{
	std::vector<Arrival> &arrivals = this->arrivals;
	arrivals.clear();
	port.side_count = {0, 0};
	for (const PieceEnd &end : port.ends) {
		const Piece &piece = this->pieces[end.piece];
		int sign = piece.signs[end.end];
		int side = (end.end == 0) ? sign : -sign;
		uint index = side > 0 ? 1 : 0;
		arrivals.push_back({end.piece, end.end, index, piece.directions[end.end] * static_cast<double>(sign)});
		port.side_count[index]++;
	}

	port.position = port.base;
	port.arc = 0;
	auto set_tangent = [this](const Arrival &a, const Vec3d &tangent) {
		Piece &piece = this->pieces[a.piece];
		piece.end_tangents[a.end] = tangent * static_cast<double>(piece.signs[a.end]);
	};

	if (port.side_count[0] == 0 || port.side_count[1] == 0) {
		/* No route through the port. */
		for (const Arrival &a : arrivals) this->pieces[a.piece].end_tangents[a.end] = this->pieces[a.piece].directions[a.end];
		return;
	}

	if (port.side_count[0] == 1 && port.side_count[1] == 1) {
		/* The arc touching both pieces at the same distance from the kink, as far out as the shorter half piece allows. */
		const Arrival &a = arrivals[0].side == 0 ? arrivals[0] : arrivals[1];
		const Arrival &b = arrivals[0].side == 0 ? arrivals[1] : arrivals[0];
		double turn = std::atan2(Length(Cross(a.direction, b.direction)), Dot(a.direction, b.direction));
		if (turn > 1e-9) {
			auto half_chord = [this](PieceID id) { return Length(this->pieces[id].points[1] - this->pieces[id].points[0]) / 2.0; };
			port.arc = std::min(half_chord(a.piece), half_chord(b.piece));
			port.position = port.base + Normalise(b.direction - a.direction) * (port.arc * std::tan(turn / 4.0));
		}
		Vec3d tangent = Normalise(a.direction + b.direction);
		set_tangent(a, tangent);
		set_tangent(b, tangent);
		return;
	}

	for (const Arrival &a : arrivals) {
		uint other = 1 - a.side;
		Vec3d tangent = port.canonical;
		if (port.side_count[a.side] == 1) {
			/* The stem of a switch. */
			tangent = a.direction;
		} else if (std::any_of(arrivals.begin(), arrivals.end(), [&](const Arrival &b) { return b.side == other && Dot(a.direction, b.direction) > 1 - 1e-9; })) {
			/* Continues straight on the other side. */
			tangent = a.direction;
		} else if (port.side_count[other] == 1) {
			/* A branch of a switch, diverging from the stem. */
			tangent = std::find_if(arrivals.begin(), arrivals.end(), [&](const Arrival &b) { return b.side == other; })->direction;
		}
		set_tangent(a, tangent);
	}
}

/**
 * Height of the surface under a point of a piece.
 * @param piece The piece.
 * @param point Horizontal position, in metres.
 * @return The height, in metres.
 */
double SplineGraph::SurfaceHeight(const Piece &piece, const Vec3d &point) const
{
	if (piece.level) return piece.level_z;
	double fx = Clamp(this->scale.MetresToUnits(point.x) / TILE_SIZE - piece.key.x, 0.0, 1.0);
	double fy = Clamp(this->scale.MetresToUnits(point.y) / TILE_SIZE - piece.key.y, 0.0, 1.0);
	return this->scale.WorldZToMetres(piece.surface.Height(fx, fy));
}

/**
 * Fit the horizontal curve of a piece to the points and tangents of its ports, and sample the surface heights along it.
 * @param piece The piece.
 */
void SplineGraph::FitCurve(Piece &piece)
{
	/* Ends of the curve: the shared point and tangent of the port, or the piece's own at an end without through route. */
	std::array<Vec3d, 2> ends = piece.points;
	std::array<Vec3d, 2> tangents = piece.directions;
	for (uint8_t end = 0; end < 2; end++) {
		PortID id = piece.ports[end];
		if (id == INVALID_PORT) continue;
		const Port &port = this->ports[id];
		if (port.side_count[0] == 0 || port.side_count[1] == 0) continue;
		ends[end] = port.position;
		tangents[end] = piece.end_tangents[end];
	}

	piece.segments.clear();
	piece.length = 0;
	auto add = [&piece](const Vec3d &p0, const Vec3d &t0, const Vec3d &p1, const Vec3d &t1) {
		if (!piece.segments.empty() && Length(p1 - p0) < 1e-9) return;
		HermiteSegment &segment = piece.segments.emplace_back();
		auto [m0, m1] = GetHermiteTangentLengths(p0, t0, p1, t1);
		segment.Set(p0, t0 * m0, p1, t1 * m1);
		piece.length += segment.Length();
	};
	/* Bends become straight track and an arc; at a bending joint the arc continues on the other piece. */
	auto add_curve = [&](const Vec3d &p0, const Vec3d &t0, const Vec3d &p1, const Vec3d &t1) {
		if (auto split = GetFilletSplit(p0, t0, p1, t1); split.has_value()) {
			add(p0, t0, split->first, split->second);
			add(split->first, split->second, p1, t1);
		} else {
			add(p0, t0, p1, t1);
		}
	};

	if (piece.via) {
		add_curve(ends[0], tangents[0], piece.via_point, piece.via_direction);
		add_curve(piece.via_point, piece.via_direction, ends[1], tangents[1]);
	} else {
		add_curve(ends[0], tangents[0], ends[1], tangents[1]);
	}

	piece.end_z = {this->SurfaceHeight(piece, ends[0]), this->SurfaceHeight(piece, ends[1])};
	auto [segment, u] = CurveParameter(piece, piece.length / 2.0);
	piece.mid_z = this->SurfaceHeight(piece, segment->Position(u));
}

/**
 * Heights at a port: one height for all pieces, the grades on either side, and how long the blends are.
 * @param port The port.
 */
void SplineGraph::UpdateHeights(Port &port)
{
	double z = 0;
	for (const PieceEnd &end : port.ends) z += this->pieces[end.piece].end_z[end.end];
	port.z = z / port.ends.size();

	std::array<double, 2> grades{};
	std::array<int, 2> counts{};
	port.blend = MAX_BLEND;
	for (const PieceEnd &end : port.ends) {
		const Piece &piece = this->pieces[end.piece];
		int sign = piece.signs[end.end];
		int side = (end.end == 0) ? sign : -sign;
		uint index = side > 0 ? 1 : 0;

		/* Grade of the half of the piece at this port, as its profile will have it. */
		double half = piece.length / 2.0;
		double grade = 0;
		if (half > 0) grade = (end.end == 0) ? (piece.mid_z - port.z) / half : (port.z - piece.mid_z) / half;
		grades[index] += grade * sign;
		counts[index]++;
		port.blend = std::min(port.blend, piece.length / 4.0);
	}
	for (uint i = 0; i < 2; i++) port.side_grade[i] = counts[i] > 0 ? grades[i] / counts[i] : 0.0;
}

/**
 * Build the vertical profile of a piece from its own heights and those of its ports.
 * @param piece The piece.
 */
void SplineGraph::BuildProfile(Piece &piece)
{
	VerticalProfile &profile = piece.profile;
	profile.length = piece.length;
	profile.z_start = piece.ports[0] != INVALID_PORT ? this->ports[piece.ports[0]].z : piece.end_z[0];
	profile.z_mid = piece.mid_z;
	profile.z_end = piece.ports[1] != INVALID_PORT ? this->ports[piece.ports[1]].z : piece.end_z[1];

	/* Grade of the pieces beyond an end, along this piece, and the blend towards it. */
	auto beyond = [&](uint8_t end, double own) -> std::pair<double, double> {
		PortID id = piece.ports[end];
		if (id == INVALID_PORT) return {own, 0.0};
		const Port &port = this->ports[id];
		int sign = piece.signs[end];
		int side = (end == 0) ? sign : -sign;
		uint other = side > 0 ? 0 : 1;
		if (port.side_count[other] == 0) return {own, 0.0};
		return {port.side_grade[other] * sign, port.blend};
	};

	const double first = profile.FirstGrade();
	const double second = profile.SecondGrade();
	std::tie(profile.grade_before, profile.blend_start) = beyond(0, first);
	std::tie(profile.grade_after, profile.blend_end) = beyond(1, second);
	profile.blend_mid = std::abs(first - second) > 1e-9 ? std::min(piece.length / 4.0, MAX_BLEND) : 0.0;
}

/**
 * Find a piece by its identity.
 * @param key The identity.
 * @return The piece, or #INVALID_PIECE if there is none.
 */
PieceID SplineGraph::Find(const PieceKey &key) const
{
	auto it = this->piece_index.find(key.Pack());
	return it == this->piece_index.end() ? INVALID_PIECE : it->second;
}

/**
 * Point of a piece and its local frame.
 * @param id The piece.
 * @param s Horizontal arc length from the start, in metres; clamped to the piece.
 * @param lateral Distance to the left of the curve, in metres, e.g. for lanes and sidewalks.
 * @return The pose.
 */
SplinePose SplineGraph::Evaluate(PieceID id, double s, double lateral) const
{
	const Piece &piece = this->pieces[id];
	s = std::clamp(s, 0.0, piece.length);

	auto [segment, u] = CurveParameter(piece, s);
	Vec3d d1 = segment->Derivative(u);
	Vec3d d2 = segment->SecondDerivative(u);
	double speed = Length(d1);
	Vec3d tangent = speed > 0 ? d1 * (1.0 / speed) : piece.directions[0];

	SplinePose pose;
	pose.left = LeftOf(tangent);
	pose.curvature = speed > 0 ? (d1.x * d2.y - d1.y * d2.x) / (speed * speed * speed) : 0.0;
	pose.grade = piece.profile.Grade(s);
	pose.position = segment->Position(u) + pose.left * lateral;
	pose.position.z = piece.profile.Height(s);
	pose.tangent = Normalise(Vec3d{tangent.x, tangent.y, pose.grade});
	return pose;
}

/**
 * The piece ends a route can continue on beyond an end of a piece.
 * @param id The piece.
 * @param end 0 for the start, 1 for the end.
 * @param[out] out The ends on the other side of the port are appended to this.
 */
void SplineGraph::GetNext(PieceID id, uint8_t end, std::vector<PieceEnd> &out) const
{
	const Piece &piece = this->pieces[id];
	PortID port_id = piece.ports[end];
	if (port_id == INVALID_PORT) return;

	auto side_of = [this](const PieceEnd &e) {
		int sign = this->pieces[e.piece].signs[e.end];
		return (e.end == 0) ? sign : -sign;
	};
	int own_side = side_of({id, end});
	for (const PieceEnd &other : this->ports[port_id].ends) {
		if (side_of(other) != own_side) out.push_back(other);
	}
}

/**
 * Position on the graph of a position in the terms of the simulation.
 * The fraction along the chord maps linearly onto the arc length, so a
 * vehicle moving evenly through a tile moves evenly along the curve.
 * @param pos The position.
 * @return The position on the graph, or nothing if the piece is not in the graph.
 */
std::optional<GraphPos> SplineGraph::Locate(const SimPos &pos) const
{
	PieceID id = this->Find(pos.key);
	if (id == INVALID_PIECE) return std::nullopt;
	return GraphPos{id, std::clamp(pos.t, 0.0, 1.0) * this->pieces[id].length};
}

/**
 * Position in the terms of the simulation of a position on the graph; the inverse of Locate().
 * @param pos The position on the graph; the piece must be valid.
 * @return The position.
 */
SimPos SplineGraph::ToSimPos(const GraphPos &pos) const
{
	const Piece &piece = this->pieces[pos.piece];
	return {piece.key, piece.length > 0 ? std::clamp(pos.s / piece.length, 0.0, 1.0) : 0.0};
}

/**
 * How far along the chord of a piece a point is, e.g. the position of a vehicle.
 * @param id The piece.
 * @param point Horizontal position, in metres.
 * @return Fraction of the way from the start to the end, 0 to 1.
 */
double SplineGraph::ChordFraction(PieceID id, const Vec3d &point) const
{
	const Piece &piece = this->pieces[id];
	Vec3d chord = piece.points[1] - piece.points[0];
	chord.z = 0;
	double length_squared = Dot(chord, chord);
	if (length_squared <= 0) return 0.0;
	Vec3d offset = point - piece.points[0];
	offset.z = 0;
	return std::clamp(Dot(offset, chord) / length_squared, 0.0, 1.0);
}

/**
 * Call a function for every piece of a chunk.
 * @param index The chunk.
 * @param func The function.
 */
void SplineGraph::ForEachPiece(ChunkIndex index, const std::function<void(PieceID)> &func) const
{
	auto it = this->chunk_pieces.find(index);
	if (it == this->chunk_pieces.end()) return;
	for (PieceID id : it->second) func(id);
}

/**
 * Build the debug overlay of a chunk: a coloured ribbon along every piece.
 * @param index The chunk.
 * @param origin Position the vertices are relative to, in metres.
 * @param[out] out Receives the mesh; previous contents are discarded.
 */
void SplineGraph::BuildOverlay(ChunkIndex index, const Vec3d &origin, TerrainMesh &out) const
{
	out.vertices.clear();
	out.indices.clear();

	/** Look of the ribbon of a layer. */
	struct Style {
		float width; ///< Width, in metres.
		TerrainColour colour; ///< Colour.
	};
	static constexpr EnumIndexArray<Style, SplineLayer, SplineLayer::End> STYLES{{{
		{1.6f, {240, 112, 24}}, // Rail
		{0.7f, {250, 232, 88}}, // Road
		{1.0f, {40, 200, 232}}, // Tram
		{0.8f, {236, 236, 236}}, // Sidewalk
	}}};
	static constexpr TerrainColour WORMHOLE{220, 60, 210};
	static constexpr double STEP = 2.0;

	this->ForEachPiece(index, [&](PieceID id) {
		const Piece &piece = this->pieces[id];
		const Style &style = STYLES[piece.key.layer];
		const TerrainColour colour = piece.key.shape == PieceShape::Wormhole ? WORMHOLE : style.colour;
		const int steps = std::max(2, static_cast<int>(std::ceil(piece.length / STEP)));
		if (out.vertices.size() + 2 * (steps + 1) > UINT16_MAX) return;

		uint16_t base = static_cast<uint16_t>(out.vertices.size());
		for (int i = 0; i <= steps; i++) {
			SplinePose pose = this->Evaluate(id, piece.length * i / steps);
			Vec3d centre = pose.position - origin;
			centre.z += OVERLAY_LIFT;
			Vec3d side = pose.left * (style.width / 2.0);
			for (const Vec3d &p : {centre - side, centre + side}) {
				out.vertices.push_back({static_cast<float>(p.x), static_cast<float>(p.y), static_cast<float>(p.z), colour.r, colour.g, colour.b, 255});
			}
		}
		for (int i = 0; i < steps; i++) {
			/* Right and left vertex of this and the next sample; counter-clockwise seen from above. */
			uint16_t r0 = base + 2 * i, l0 = r0 + 1, r1 = r0 + 2, l1 = r0 + 3;
			out.indices.insert(out.indices.end(), {r0, r1, l0, r1, l1, l0});
		}
	});
}

} // namespace fp
