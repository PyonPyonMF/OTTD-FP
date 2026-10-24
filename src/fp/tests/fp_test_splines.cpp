/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file fp_test_splines.cpp Tests of the spline graph of the first-person mode. */

#include "../../stdafx.h"

#include "../../3rdparty/catch2/catch.hpp"

#include "../world/fp_splines.h"
#include "../../landscape.h"
#include "../../map_func.h"
#include "../../road_func.h"
#include "../../slope_func.h"
#include "../../track_func.h"

#include "../../safeguards.h"

using namespace fp;

static const double TILE = MetricScale{}.TileLength(); ///< Length of a tile, in metres.
static constexpr double EPSILON = 1e-9; ///< Tolerance for values that must agree exactly.

/**
 * A tile with rail tracks.
 * @param x X coordinate of the tile.
 * @param y Y coordinate of the tile.
 * @param rail The tracks.
 * @param surface The surface.
 * @return The tile data.
 */
static SplineTileData RailTile(uint32_t x, uint32_t y, TrackBits rail, TileSurface surface = {})
{
	SplineTileData tile;
	tile.x = x;
	tile.y = y;
	tile.rail = rail;
	tile.surface = surface;
	return tile;
}

/**
 * A tile with a road.
 * @param x X coordinate of the tile.
 * @param y Y coordinate of the tile.
 * @param road The road bits.
 * @param sidewalks Whether the road has sidewalks.
 * @return The tile data.
 */
static SplineTileData RoadTile(uint32_t x, uint32_t y, RoadBits road, bool sidewalks = false)
{
	SplineTileData tile;
	tile.x = x;
	tile.y = y;
	tile.road = road;
	tile.sidewalks = sidewalks;
	return tile;
}

/** A spline graph built from tiles, with helpers for inspecting it. */
struct TestGraph {
	SplineGraph graph; ///< The graph.
	ChunkGrid grid{256, 256}; ///< Chunks of the imaginary map.
	std::vector<ChunkIndex> chunks; ///< Chunks that were set.
	std::vector<ChunkIndex> changed; ///< Chunks reported as changed.

	/**
	 * Build a graph.
	 * @param tiles The tiles, in any order and any chunks.
	 */
	explicit TestGraph(const std::vector<SplineTileData> &tiles = {})
	{
		this->graph.Reset(MetricScale{});
		this->Set(tiles);
	}

	/**
	 * Replace the chunks the tiles belong to.
	 * @param tiles The tiles; all tiles of a chunk must be given together.
	 */
	void Set(const std::vector<SplineTileData> &tiles)
	{
		std::map<ChunkIndex, SplineChunkData> data;
		for (const SplineTileData &tile : tiles) {
			ChunkIndex index = this->grid.IndexOfTile(tile.x, tile.y);
			data[index].chunk_x = this->grid.X(index);
			data[index].chunk_y = this->grid.Y(index);
			data[index].tiles.push_back(tile);
		}
		for (const auto &[index, chunk] : data) {
			this->graph.SetChunk(index, chunk, this->changed);
			if (std::find(this->chunks.begin(), this->chunks.end(), index) == this->chunks.end()) this->chunks.push_back(index);
		}
	}

	/**
	 * Find a piece that must exist.
	 * @param x X coordinate of the tile.
	 * @param y Y coordinate of the tile.
	 * @param layer The layer.
	 * @param shape The shape.
	 * @return The piece.
	 */
	PieceID Get(uint32_t x, uint32_t y, SplineLayer layer, PieceShape shape) const
	{
		PieceID id = this->graph.Find({x, y, layer, shape});
		REQUIRE(id != INVALID_PIECE);
		return id;
	}

	/**
	 * All pieces.
	 * @return The pieces.
	 */
	std::vector<PieceID> Pieces() const
	{
		std::vector<PieceID> result;
		for (ChunkIndex index : this->chunks) this->graph.ForEachPiece(index, [&](PieceID id) { result.push_back(id); });
		return result;
	}
};

/** Continuity of the joints between pieces. */
struct JointStats {
	uint joints = 0; ///< Number of joints checked.
	double max_gap = 0; ///< Largest distance between the ends of connected pieces.
	double max_kink = 0; ///< Largest angle between the 3D tangents of connected pieces.
};

/**
 * Check every joint of the graph where a route continues from one piece to another.
 * @param g The graph.
 * @return The worst values found.
 */
static JointStats CheckJoints(const TestGraph &g)
{
	JointStats stats;
	std::vector<PieceEnd> next;
	for (PieceID id : g.Pieces()) {
		const SplineGraph::Piece &piece = g.graph.GetPiece(id);
		for (uint8_t end = 0; end < 2; end++) {
			next.clear();
			g.graph.GetNext(id, end, next);
			SplinePose here = g.graph.Evaluate(id, end == 0 ? 0.0 : piece.length);
			Vec3d out = end == 1 ? here.tangent : here.tangent * -1.0;
			for (const PieceEnd &other : next) {
				SplinePose there = g.graph.Evaluate(other.piece, other.end == 0 ? 0.0 : g.graph.GetPiece(other.piece).length);
				Vec3d in = other.end == 0 ? there.tangent : there.tangent * -1.0;
				stats.joints++;
				stats.max_gap = std::max(stats.max_gap, Length(here.position - there.position));
				stats.max_kink = std::max(stats.max_kink, std::atan2(Length(Cross(out, in)), Dot(out, in)));
			}
		}
	}
	return stats;
}

/** A piece of a route and the direction it is driven in. */
struct RouteStep {
	PieceID piece; ///< The piece.
	bool forward; ///< Whether the route runs from the start to the end of the piece.
};

/**
 * Sample the heading and curvature along a route.
 * @param g The graph.
 * @param route The pieces of the route.
 * @param[out] headings Receives the heading at every sample, unwrapped, in radians.
 * @param[out] curvatures Receives the curvature in the direction of the route at every sample.
 */
static void SampleRoute(const TestGraph &g, const std::vector<RouteStep> &route, std::vector<double> &headings, std::vector<double> &curvatures)
{
	for (const RouteStep &step : route) {
		const double length = g.graph.GetPiece(step.piece).length;
		for (int i = 0; i <= 64; i++) {
			double s = step.forward ? length * i / 64 : length * (64 - i) / 64;
			SplinePose pose = g.graph.Evaluate(step.piece, s);
			double heading = std::atan2(pose.tangent.y, pose.tangent.x);
			if (!step.forward) heading += std::numbers::pi;
			if (!headings.empty()) heading = headings.back() + std::remainder(heading - headings.back(), 2 * std::numbers::pi);
			headings.push_back(heading);
			curvatures.push_back(step.forward ? pose.curvature : -pose.curvature);
		}
	}
}

TEST_CASE("Splines - Hermite segment has a natural parametrisation")
{
	HermiteSegment line;
	line.Set({0, 0, 0}, {10, 0, 0}, {10, 0, 0}, {10, 0, 0});
	CHECK(line.Length() == Approx(10.0));
	for (double s : {0.0, 2.5, 5.0, 7.5, 10.0}) CHECK(line.ParameterAt(s) == Approx(s / 10.0).margin(1e-12));

	/* A quarter circle of radius 20 from the tangent lengths for a circular arc. */
	const double radius = 20.0;
	Vec3d p0{radius, 0, 0}, d0{0, 1, 0}, p1{0, radius, 0}, d1{-1, 0, 0};
	auto [m0, m1] = GetHermiteTangentLengths(p0, d0, p1, d1);
	HermiteSegment arc;
	arc.Set(p0, d0 * m0, p1, d1 * m1);
	CHECK(arc.Length() == Approx(std::numbers::pi * radius / 2).epsilon(1e-3));

	for (int i = 0; i <= 32; i++) {
		double s = arc.Length() * i / 32;
		Vec3d p = arc.Position(arc.ParameterAt(s));
		CHECK(std::abs(Length(p) - radius) < 1e-3 * radius);

		/* Equal steps of arc length are equal steps along the curve. */
		Vec3d q = arc.Position(arc.ParameterAt(std::min(s + 0.01, arc.Length())));
		if (s + 0.01 <= arc.Length()) CHECK(Length(q - p) == Approx(0.01).epsilon(1e-4));
	}
}

TEST_CASE("Splines - vertical profile is continuously differentiable")
{
	VerticalProfile profile;
	profile.length = 30.0;
	profile.z_start = 0.0;
	profile.z_mid = 1.0;
	profile.z_end = 1.5;
	profile.grade_before = -0.05;
	profile.grade_after = 0.2;
	profile.blend_start = 5.0;
	profile.blend_mid = 7.5;
	profile.blend_end = 4.0;

	/* The ends are in the middle of their blends, which continue on the neighbouring pieces. */
	CHECK(profile.Grade(0.0) == Approx((profile.grade_before + profile.FirstGrade()) / 2));
	CHECK(profile.Grade(30.0) == Approx((profile.SecondGrade() + profile.grade_after) / 2));

	/* No jumps in height or grade, and the grade is the derivative of the height. */
	const double step = 1e-3;
	double max_jump = 0, max_grade_jump = 0, max_derivative_error = 0;
	double previous_z = profile.Height(0.0);
	double previous_grade = profile.Grade(0.0);
	for (int i = 1; i * step <= profile.length; i++) {
		double z = profile.Height(i * step);
		double grade = profile.Grade(i * step);
		max_jump = std::max(max_jump, std::abs(z - previous_z));
		max_grade_jump = std::max(max_grade_jump, std::abs(grade - previous_grade));
		max_derivative_error = std::max(max_derivative_error, std::abs((z - previous_z) / step - (grade + previous_grade) / 2));
		previous_z = z;
		previous_grade = grade;
	}
	CHECK(max_jump < 1e-3);
	CHECK(max_grade_jump < 1e-3);
	CHECK(max_derivative_error < 1e-4);

	/* Without changes of grade the profile is a straight line. */
	VerticalProfile line;
	line.length = 30.0;
	line.z_start = 2.0;
	line.z_mid = 3.0;
	line.z_end = 4.0;
	line.grade_before = line.grade_after = 1.0 / 15.0;
	line.blend_start = line.blend_end = 5.0;
	for (double s = 0; s <= 30.0; s += 0.5) CHECK(line.Height(s) == Approx(2.0 + s / 15.0));
}

/**
 * Check that the surface model matches GetPartialPixelZ() at all whole units of a tile.
 * @param slope The slope.
 * @return True iff the heights agree within the rounding of GetPartialPixelZ().
 */
static bool SurfaceMatchesPartialPixelZ(Slope slope)
{
	for (int x = 0; x < static_cast<int>(TILE_SIZE); x++) {
		for (int y = 0; y < static_cast<int>(TILE_SIZE); y++) {
			double ours = GetSlopeSurfaceZ(slope, static_cast<double>(x) / TILE_SIZE, static_cast<double>(y) / TILE_SIZE);
			int theirs = GetPartialPixelZ(x, y, slope);
			if (std::abs(ours - theirs) > 1.0) {
				UNSCOPED_INFO("slope " << static_cast<int>(slope) << " at (" << x << ", " << y << "): model " << ours << ", game " << theirs);
				return false;
			}
		}
	}
	return true;
}

TEST_CASE("Splines - surface model matches GetPartialPixelZ, including half tiles")
{
	for (uint s = SLOPE_FLAT; s < SLOPE_ELEVATED; s++) {
		CAPTURE(s);
		CHECK(SurfaceMatchesPartialPixelZ(static_cast<Slope>(s)));
		for (Corner corner : {Corner::W, Corner::S, Corner::E, Corner::N}) {
			CHECK(SurfaceMatchesPartialPixelZ(HalftileSlope(static_cast<Slope>(s), corner)));
		}
	}
	for (Slope s : {SLOPE_STEEP_N, SLOPE_STEEP_E, SLOPE_STEEP_S, SLOPE_STEEP_W}) CHECK(SurfaceMatchesPartialPixelZ(s));
}

TEST_CASE("Splines - straight track")
{
	std::vector<SplineTileData> tiles;
	for (uint32_t x = 2; x < 7; x++) tiles.push_back(RailTile(x, 3, Track::X));
	TestGraph g(tiles);

	for (PieceID id : g.Pieces()) {
		const SplineGraph::Piece &piece = g.graph.GetPiece(id);
		CHECK(piece.length == Approx(TILE));
		for (double s : {0.0, 7.0, 15.36, 30.0}) {
			SplinePose pose = g.graph.Evaluate(id, s);
			CHECK(pose.tangent.x == Approx(-1.0));
			CHECK(std::abs(pose.curvature) < 1e-12);
			CHECK(pose.position.y == Approx(3.5 * TILE));
		}
	}

	JointStats joints = CheckJoints(g);
	CHECK(joints.joints == 8);
	CHECK(joints.max_gap < EPSILON);
	CHECK(joints.max_kink < EPSILON);
}

TEST_CASE("Splines - curve is G1 and bends one way")
{
	/* Going north-east along X, turning through a diagonal to go north-west along Y. */
	TestGraph g({
		RailTile(4, 2, Track::X),
		RailTile(3, 2, Track::X),
		RailTile(2, 2, Track::Left),
		RailTile(2, 1, Track::Y),
		RailTile(2, 0, Track::Y),
	});

	JointStats joints = CheckJoints(g);
	CHECK(joints.joints == 8);
	CHECK(joints.max_gap < EPSILON);
	CHECK(joints.max_kink < EPSILON);

	std::vector<double> headings, curvatures;
	SampleRoute(g, {
		{g.Get(4, 2, SplineLayer::Rail, PieceShape::X), true},
		{g.Get(3, 2, SplineLayer::Rail, PieceShape::X), true},
		{g.Get(2, 2, SplineLayer::Rail, PieceShape::Left), false},
		{g.Get(2, 1, SplineLayer::Rail, PieceShape::Y), false},
		{g.Get(2, 0, SplineLayer::Rail, PieceShape::Y), false},
	}, headings, curvatures);

	/* From heading -X to heading -Y: a quarter turn, without wobbles. */
	CHECK(std::abs(headings.back() - headings.front()) == Approx(std::numbers::pi / 2));
	bool left = headings.back() > headings.front();
	for (size_t i = 1; i < headings.size(); i++) CHECK((left ? headings[i] - headings[i - 1] : headings[i - 1] - headings[i]) > -1e-9);
	for (double curvature : curvatures) CHECK((left ? curvature : -curvature) > -1e-9);

	/* The two 45 degree kinks become one circular arc over the diagonal, touching the X and Y tracks halfway their tiles' edges and middles. */
	const double half_diagonal = TILE / (2 * std::numbers::sqrt2);
	const double radius = half_diagonal / std::tan(std::numbers::pi / 8);
	PieceID diagonal = g.Get(2, 2, SplineLayer::Rail, PieceShape::Left);
	for (int i = 0; i <= 8; i++) CHECK(std::abs(g.graph.Evaluate(diagonal, g.graph.GetPiece(diagonal).length * i / 8).curvature) == Approx(1 / radius).epsilon(0.01));
	CHECK(g.graph.GetPiece(diagonal).length == Approx(radius * std::numbers::pi / 4).epsilon(1e-3));

	/* Further away the track stays straight. */
	SplinePose straight = g.graph.Evaluate(g.Get(3, 2, SplineLayer::Rail, PieceShape::X), 0.0);
	CHECK(std::abs(straight.curvature) < 1e-12);
}

TEST_CASE("Splines - S-curve is G1 and returns to the original heading")
{
	TestGraph g({
		RailTile(5, 2, Track::X),
		RailTile(4, 2, Track::X),
		RailTile(3, 2, Track::Left),
		RailTile(3, 1, Track::Right),
		RailTile(2, 1, Track::X),
		RailTile(1, 1, Track::X),
	});

	JointStats joints = CheckJoints(g);
	CHECK(joints.joints == 10);
	CHECK(joints.max_gap < EPSILON);
	CHECK(joints.max_kink < EPSILON);

	std::vector<double> headings, curvatures;
	SampleRoute(g, {
		{g.Get(5, 2, SplineLayer::Rail, PieceShape::X), true},
		{g.Get(4, 2, SplineLayer::Rail, PieceShape::X), true},
		{g.Get(3, 2, SplineLayer::Rail, PieceShape::Left), false},
		{g.Get(3, 1, SplineLayer::Rail, PieceShape::Right), false},
		{g.Get(2, 1, SplineLayer::Rail, PieceShape::X), true},
		{g.Get(1, 1, SplineLayer::Rail, PieceShape::X), true},
	}, headings, curvatures);
	CHECK(headings.back() == Approx(headings.front()).margin(1e-9));

	/* It bends one way, then the other, and never more than the 45 degrees of the diagonal. */
	auto [lowest, highest] = std::minmax_element(headings.begin(), headings.end());
	CHECK(std::max(headings.front() - *lowest, *highest - headings.front()) < std::numbers::pi / 4 + 1e-9);
}

TEST_CASE("Splines - switch keeps the straight track straight and every route G1")
{
	/* Stem along X; on tile (2, 2) the track either continues along X or diverges to the north-west. */
	TestGraph g({
		RailTile(4, 2, Track::X),
		RailTile(3, 2, Track::X),
		RailTile(2, 2, {Track::X, Track::Left}),
		RailTile(1, 2, Track::X),
		RailTile(2, 1, Track::Y),
	});

	JointStats joints = CheckJoints(g);
	CHECK(joints.max_gap < EPSILON);
	CHECK(joints.max_kink < EPSILON);

	/* The through route is a straight line. */
	for (uint32_t x : {4, 3, 2, 1}) {
		PieceID id = g.Get(x, 2, SplineLayer::Rail, PieceShape::X);
		for (double s : {0.0, 10.0, 20.0, TILE}) CHECK(std::abs(g.graph.Evaluate(id, s).curvature) < 1e-12);
	}

	/* The diverging branch leaves tangentially. */
	PieceID branch = g.Get(2, 2, SplineLayer::Rail, PieceShape::Left);
	SplinePose start = g.graph.Evaluate(branch, g.graph.GetPiece(branch).length);
	CHECK(start.tangent.x == Approx(1.0)); // Driven backwards, i.e. towards -X.

	/* From the stem a route can continue on two pieces. */
	std::vector<PieceEnd> next;
	g.graph.GetNext(g.Get(3, 2, SplineLayer::Rail, PieceShape::X), 1, next);
	CHECK(next.size() == 2);
}

/**
 * Angle between the directions of a route leaving one piece end and entering another.
 * @param g The graph.
 * @param from The piece end the route leaves.
 * @param to The piece end the route enters.
 * @return The angle, in radians.
 */
static double RouteKink(const TestGraph &g, PieceEnd from, PieceEnd to)
{
	SplinePose a = g.graph.Evaluate(from.piece, from.end == 0 ? 0.0 : g.graph.GetPiece(from.piece).length);
	SplinePose b = g.graph.Evaluate(to.piece, to.end == 0 ? 0.0 : g.graph.GetPiece(to.piece).length);
	Vec3d out = from.end == 1 ? a.tangent : a.tangent * -1.0;
	Vec3d in = to.end == 0 ? b.tangent : b.tangent * -1.0;
	return std::atan2(Length(Cross(out, in)), Dot(out, in));
}

TEST_CASE("Splines - branches of switches bend smoothly")
{
	SECTION("A branch turning back to an axis bends one way")
	{
		/* As found on the title game: a depot on (2, 2), a switch on (3, 2) whose branch turns onto Y. */
		SplineTileData depot = RailTile(2, 2, {});
		depot.rail_stub = DiagDirection::SW;
		TestGraph g({depot, RailTile(3, 2, {Track::X, Track::Upper}), RailTile(4, 2, Track::X), RailTile(3, 1, Track::Y), RailTile(3, 0, Track::Y)});

		std::vector<double> headings, curvatures;
		SampleRoute(g, {
			{g.Get(3, 0, SplineLayer::Rail, PieceShape::Y), true},
			{g.Get(3, 1, SplineLayer::Rail, PieceShape::Y), true},
			{g.Get(3, 2, SplineLayer::Rail, PieceShape::Upper), true},
			{g.Get(2, 2, SplineLayer::Rail, PieceShape::StubSW), true},
		}, headings, curvatures);
		bool left = headings.back() > headings.front();
		for (size_t i = 1; i < headings.size(); i++) CHECK((left ? headings[i] - headings[i - 1] : headings[i - 1] - headings[i]) > -1e-9);
		/* The depot leaves little room for the last part of the turn: straight track, then the largest arc that fits. */
		double sharpest = 0;
		for (double curvature : curvatures) sharpest = std::max(sharpest, std::abs(curvature));
		WARN("tightest radius of the branch: " << 1 / sharpest << " m");
		CHECK(1 / sharpest > 9.0);
		CHECK(CheckJoints(g).max_kink < EPSILON);
	}

	SECTION("A branch into a diagonal line overshoots its direction only a little")
	{
		TestGraph g({
			RailTile(5, 2, Track::X),
			RailTile(4, 2, Track::X),
			RailTile(3, 2, {Track::X, Track::Left}),
			RailTile(2, 2, Track::X),
			RailTile(3, 1, Track::Right),
			RailTile(2, 1, Track::Left),
			RailTile(2, 0, Track::Right),
		});
		CHECK(CheckJoints(g).max_kink < EPSILON);

		std::vector<double> headings, curvatures;
		SampleRoute(g, {
			{g.Get(4, 2, SplineLayer::Rail, PieceShape::X), true},
			{g.Get(3, 2, SplineLayer::Rail, PieceShape::Left), false},
			{g.Get(3, 1, SplineLayer::Rail, PieceShape::Right), false},
			{g.Get(2, 1, SplineLayer::Rail, PieceShape::Left), false},
		}, headings, curvatures);
		double turn = headings.back() - headings.front();
		CHECK(std::abs(turn) == Approx(std::numbers::pi / 4));
		double overshoot = 0;
		for (double heading : headings) overshoot = std::max(overshoot, (heading - headings.front()) / turn - 1);
		WARN("branch into a diagonal line overshoots by " << overshoot * std::abs(turn) * 180 / std::numbers::pi << " degrees");
		CHECK(overshoot * std::abs(turn) < 15 * std::numbers::pi / 180);
	}
}

TEST_CASE("Splines - crossovers keep straight lines straight and branches tangential")
{
	SECTION("A diagonal line crossing a straight line at a port")
	{
		TestGraph g({RailTile(3, 2, {Track::X, Track::Upper}), RailTile(2, 2, {Track::X, Track::Lower})});
		for (PieceID id : g.Pieces()) {
			const SplineGraph::Piece &piece = g.graph.GetPiece(id);
			for (double f : {0.0, 0.5, 1.0}) CHECK(std::abs(g.graph.Evaluate(id, piece.length * f).curvature) < 1e-12);
			CHECK(piece.length == Approx(Length(piece.points[1] - piece.points[0])));
		}
	}

	SECTION("Branches leaving and joining a straight line at the same port")
	{
		TestGraph g({RailTile(3, 2, {Track::X, Track::Right}), RailTile(2, 2, {Track::X, Track::Lower})});
		PieceID straight_a = g.Get(3, 2, SplineLayer::Rail, PieceShape::X);
		PieceID straight_b = g.Get(2, 2, SplineLayer::Rail, PieceShape::X);
		PieceID joining = g.Get(3, 2, SplineLayer::Rail, PieceShape::Right);
		PieceID leaving = g.Get(2, 2, SplineLayer::Rail, PieceShape::Lower);

		/* The straight line stays straight, and both branches meet it tangentially. */
		for (PieceID id : {straight_a, straight_b}) CHECK(std::abs(g.graph.Evaluate(id, 10.0).curvature) < 1e-12);
		CHECK(RouteKink(g, {straight_a, 1}, {straight_b, 0}) < EPSILON);
		CHECK(RouteKink(g, {straight_a, 1}, {leaving, 0}) < EPSILON);
		CHECK(RouteKink(g, {joining, 0}, {straight_b, 0}) < EPSILON);
	}
}

TEST_CASE("Splines - diagonal track is a straight diagonal line")
{
	TestGraph g({
		RailTile(6, 6, Track::X),
		RailTile(5, 6, Track::Left),
		RailTile(5, 5, Track::Right),
		RailTile(4, 5, Track::Left),
		RailTile(4, 4, Track::Right),
		RailTile(3, 4, Track::Left),
	});

	CHECK(CheckJoints(g).max_kink < EPSILON);
	for (PieceID id : {g.Get(5, 5, SplineLayer::Rail, PieceShape::Right), g.Get(4, 5, SplineLayer::Rail, PieceShape::Left), g.Get(4, 4, SplineLayer::Rail, PieceShape::Right)}) {
		const SplineGraph::Piece &piece = g.graph.GetPiece(id);
		CHECK(piece.length == Approx(TILE / std::numbers::sqrt2));
		for (double s : {0.0, 5.0, 10.0}) CHECK(std::abs(g.graph.Evaluate(id, s).curvature) < 1e-9);
	}
}

TEST_CASE("Splines - roads turn by a quarter circle within the tile")
{
	TestGraph g({RoadTile(5, 5, {RoadBit::NE, RoadBit::SE})});
	PieceID id = g.Get(5, 5, SplineLayer::Road, PieceShape::Right);
	const double radius = TILE / 2;
	CHECK(g.graph.GetPiece(id).length == Approx(std::numbers::pi * radius / 2).epsilon(1e-3));

	/* Around the east corner of the tile. */
	const Vec3d corner{5 * TILE, 6 * TILE, 0};
	for (double f : {0.0, 0.25, 0.5, 0.75, 1.0}) {
		Vec3d p = g.graph.Evaluate(id, g.graph.GetPiece(id).length * f).position;
		p.z = 0;
		CHECK(Length(p - corner) == Approx(radius).epsilon(1e-3));
	}

	/* Lanes are offsets of the centre line. */
	SplinePose centre = g.graph.Evaluate(id, 3.0);
	SplinePose lane = g.graph.Evaluate(id, 3.0, -SplineGraph::ROAD_LANE_WIDTH / 2);
	CHECK(Length(lane.position - centre.position) == Approx(SplineGraph::ROAD_LANE_WIDTH / 2));
}

TEST_CASE("Splines - road junctions and dead ends")
{
	TestGraph g({
		RoadTile(5, 5, ROAD_ALL),
		RoadTile(4, 5, ROAD_X),
		RoadTile(6, 5, {RoadBit::NE, RoadBit::SE, RoadBit::NW}),
		RoadTile(5, 4, {RoadBit::SE}),
	});

	CHECK(g.graph.GetStatistics().pieces[to_underlying(SplineLayer::Road)] == 6 + 1 + 3 + 1);
	g.Get(5, 4, SplineLayer::Road, PieceShape::StubSE);

	JointStats joints = CheckJoints(g);
	CHECK(joints.max_gap < EPSILON);
	CHECK(joints.max_kink < EPSILON);

	/* The dead end has no port in the middle of its tile. */
	const SplineGraph::Piece &stub = g.graph.GetPiece(g.Get(5, 4, SplineLayer::Road, PieceShape::StubSE));
	CHECK(stub.ports[1] == INVALID_PORT);
	CHECK(stub.length == Approx(TILE / 2));

	/* Crossing the junction straight on, or turning: three ways on. */
	std::vector<PieceEnd> next;
	g.graph.GetNext(g.Get(4, 5, SplineLayer::Road, PieceShape::X), 0, next);
	CHECK(next.size() == 3);
}

TEST_CASE("Splines - sidewalks run along both sides of town roads")
{
	TestGraph g({
		RoadTile(3, 5, ROAD_X, true),
		RoadTile(4, 5, ROAD_X, true),
		RoadTile(5, 5, {RoadBit::NE, RoadBit::SE}, true),
		RoadTile(5, 6, ROAD_Y, true),
		RoadTile(5, 7, {RoadBit::NW}, true),
	});

	JointStats joints = CheckJoints(g);
	CHECK(joints.joints > 0);
	CHECK(joints.max_gap < EPSILON);
	CHECK(joints.max_kink < EPSILON);

	/* Along a straight road: two parallel lines at the sidewalk offset. */
	const double road_y = 5.5 * TILE;
	for (PieceShape shape : {PieceShape::SidewalkSW, PieceShape::SidewalkNE}) {
		PieceID id = g.Get(4, 5, SplineLayer::Sidewalk, shape);
		CHECK(g.graph.GetPiece(id).length == Approx(TILE));
		for (double s : {0.0, 15.0, TILE}) CHECK(std::abs(g.graph.Evaluate(id, s).position.y - road_y) == Approx(SplineGraph::SIDEWALK_OFFSET));
	}

	/* In the turn: an inner and an outer arc around the east corner. */
	const Vec3d corner{5 * TILE, 6 * TILE, 0};
	std::vector<double> radii;
	for (PieceShape shape : {PieceShape::SidewalkNE, PieceShape::SidewalkSE}) {
		PieceID id = g.Get(5, 5, SplineLayer::Sidewalk, shape);
		Vec3d p = g.graph.Evaluate(id, g.graph.GetPiece(id).length / 2).position;
		p.z = 0;
		radii.push_back(Length(p - corner));
	}
	std::sort(radii.begin(), radii.end());
	CHECK(radii[0] == Approx(TILE / 2 - SplineGraph::SIDEWALK_OFFSET).epsilon(2e-3));
	CHECK(radii[1] == Approx(TILE / 2 + SplineGraph::SIDEWALK_OFFSET).epsilon(2e-3));

	/* Around a dead end: from one side of the road to the other, passing beyond its end. */
	PieceID u_turn = g.Get(5, 7, SplineLayer::Sidewalk, PieceShape::SidewalkNW);
	const SplineGraph::Piece &piece = g.graph.GetPiece(u_turn);
	CHECK(piece.via);
	CHECK(piece.ports[0] != INVALID_PORT);
	CHECK(piece.ports[1] != INVALID_PORT);
	SplinePose apex = g.graph.Evaluate(u_turn, piece.length / 2);
	CHECK(apex.position.y == Approx(7.5 * TILE + SplineGraph::SIDEWALK_OFFSET).epsilon(1e-3));
}

TEST_CASE("Splines - bridges: ramps, a level deck and smooth grades")
{
	/* A bridge along X from head (6, 3) to head (2, 3), with flat approaches. */
	SplineTileData head = RailTile(2, 3, Track::X, {0, SLOPE_SW});
	SplineTileData other = RailTile(6, 3, Track::X, {0, SLOPE_NE});
	head.wormhole = DiagDirection::SW;
	head.wormhole_end_x = 6;
	head.wormhole_end_y = 3;
	head.wormhole_z = TILE_HEIGHT;
	TestGraph g({RailTile(1, 3, Track::X), head, other, RailTile(7, 3, Track::X)});

	PieceID span = g.Get(2, 3, SplineLayer::Rail, PieceShape::Wormhole);
	CHECK(g.graph.GetPiece(span).length == Approx(3 * TILE));
	const double deck = MetricScale{}.metres_per_level;
	CHECK(g.graph.Evaluate(span, 1.5 * TILE).position.z == Approx(deck));
	CHECK(g.graph.Evaluate(g.Get(2, 3, SplineLayer::Rail, PieceShape::X), TILE / 2).position.z == Approx(deck / 2));

	JointStats joints = CheckJoints(g);
	CHECK(joints.joints == 8);
	CHECK(joints.max_gap < EPSILON);
	CHECK(joints.max_kink < EPSILON);
}

TEST_CASE("Splines - grades blend smoothly between flat and inclined track")
{
	/* Flat, up one level along -X on an inclined tile, flat again. */
	TestGraph g({
		RailTile(5, 3, Track::X, {0, SLOPE_FLAT}),
		RailTile(4, 3, Track::X, {0, SLOPE_NE}),
		RailTile(3, 3, Track::X, {static_cast<int32_t>(TILE_HEIGHT), SLOPE_FLAT}),
	});

	JointStats joints = CheckJoints(g);
	CHECK(joints.max_gap < EPSILON);
	CHECK(joints.max_kink < EPSILON);

	/* Halfway up the incline the track is on the surface, and the grade is that of the incline. */
	const double level = MetricScale{}.metres_per_level;
	SplinePose middle = g.graph.Evaluate(g.Get(4, 3, SplineLayer::Rail, PieceShape::X), TILE / 2);
	CHECK(middle.position.z == Approx(level / 2));
	CHECK(middle.grade == Approx(level / TILE));
}

TEST_CASE("Splines - simulation positions map to the graph and back for all tracks")
{
	for (Track track : TRACK_BIT_ALL) {
		CAPTURE(to_underlying(track));
		TestGraph g({RailTile(8, 8, track)});
		PieceID id = g.Get(8, 8, SplineLayer::Rail, TrackToPieceShape(track));
		const SplineGraph::Piece &piece = g.graph.GetPiece(id);

		for (int i = 0; i <= 16; i++) {
			SimPos pos{piece.key, i / 16.0};
			std::optional<GraphPos> on_graph = g.graph.Locate(pos);
			REQUIRE(on_graph.has_value());
			CHECK(on_graph->s == Approx(pos.t * piece.length));
			SimPos back = g.graph.ToSimPos(*on_graph);
			CHECK(back.key == pos.key);
			CHECK(back.t == Approx(pos.t).margin(1e-12));

			/* The point on the chord at that fraction projects back onto the same fraction. */
			Vec3d chord = Lerp(piece.points[0], piece.points[1], pos.t);
			CHECK(g.graph.ChordFraction(id, chord) == Approx(pos.t).margin(1e-12));
		}

		/* The piece runs in the direction of the first trackdir of the track. */
		DiagDirection exit = TrackdirToExitdir(TrackToTrackdir(track));
		Vec3d to_exit = piece.points[1] - Vec3d{8.5 * TILE, 8.5 * TILE, 0};
		TileIndexDiffC delta = TileIndexDiffCByDiagDir(exit);
		CHECK(Dot(to_exit, Vec3d{static_cast<double>(delta.x), static_cast<double>(delta.y), 0}) > 0);
	}

	/* In a curve the smooth track stays close to the straight path of the simulation. */
	TestGraph g({RailTile(3, 2, Track::X), RailTile(2, 2, Track::Left), RailTile(2, 1, Track::Y)});
	for (PieceID id : g.Pieces()) {
		const SplineGraph::Piece &piece = g.graph.GetPiece(id);
		for (int i = 0; i <= 16; i++) {
			Vec3d chord = Lerp(piece.points[0], piece.points[1], i / 16.0);
			Vec3d curve = g.graph.Evaluate(id, piece.length * i / 16.0).position;
			CHECK(std::hypot(curve.x - chord.x, curve.y - chord.y) < 0.1 * TILE);
		}
	}
}

/**
 * Tiles of a small network across four chunks: a loop of track and a road with sidewalks.
 * @return The tiles.
 */
static std::vector<SplineTileData> NetworkAcrossChunks()
{
	std::vector<SplineTileData> tiles;
	for (uint32_t x = 12; x < 20; x++) tiles.push_back(RailTile(x, 15, x == 16 ? TrackBits{Track::X, Track::Lower} : TrackBits{Track::X}));
	tiles.push_back(RailTile(16, 16, Track::Y));
	tiles.push_back(RailTile(16, 17, Track::Y));
	for (uint32_t y = 12; y < 20; y++) tiles.push_back(RoadTile(14, y, ROAD_Y, true));
	return tiles;
}

/**
 * Sample every piece of a graph.
 * @param g The graph.
 * @return Packed key and pose of samples along every piece, sorted.
 */
static std::vector<std::pair<uint64_t, std::array<double, 7>>> Samples(const TestGraph &g)
{
	std::vector<std::pair<uint64_t, std::array<double, 7>>> result;
	for (PieceID id : g.Pieces()) {
		const SplineGraph::Piece &piece = g.graph.GetPiece(id);
		for (int i = 0; i <= 8; i++) {
			SplinePose pose = g.graph.Evaluate(id, piece.length * i / 8);
			result.push_back({piece.key.Pack() * 16 + i, {pose.position.x, pose.position.y, pose.position.z, pose.tangent.x, pose.tangent.y, pose.tangent.z, pose.curvature}});
		}
	}
	std::sort(result.begin(), result.end());
	return result;
}

TEST_CASE("Splines - the graph does not depend on the order chunks are updated in")
{
	std::vector<SplineTileData> tiles = NetworkAcrossChunks();
	TestGraph batch(tiles);

	/* Chunk by chunk in another order, with one chunk first built empty and one removed and built again. */
	std::map<ChunkIndex, std::vector<SplineTileData>> by_chunk;
	for (const SplineTileData &tile : tiles) by_chunk[ChunkGrid(256, 256).IndexOfTile(tile.x, tile.y)].push_back(tile);
	REQUIRE(by_chunk.size() == 4);

	TestGraph incremental;
	std::vector<ChunkIndex> order;
	for (const auto &[index, chunk_tiles] : by_chunk) order.push_back(index);
	std::reverse(order.begin(), order.end());

	SplineChunkData empty;
	empty.chunk_x = incremental.grid.X(order[1]);
	empty.chunk_y = incremental.grid.Y(order[1]);
	incremental.graph.SetChunk(order[1], empty, incremental.changed);
	for (ChunkIndex index : order) incremental.Set(by_chunk[index]);
	incremental.graph.RemoveChunk(order[2], incremental.changed);
	incremental.Set(by_chunk[order[2]]);

	auto a = Samples(batch);
	auto b = Samples(incremental);
	REQUIRE(a.size() == b.size());
	for (size_t i = 0; i < a.size(); i++) {
		REQUIRE(a[i].first == b[i].first);
		for (size_t j = 0; j < a[i].second.size(); j++) CHECK(a[i].second[j] == Approx(b[i].second[j]).margin(1e-9));
	}
	CHECK(CheckJoints(incremental).max_kink < EPSILON);
}

TEST_CASE("Splines - removing a chunk turns the pieces at its border into ends")
{
	std::vector<SplineTileData> tiles;
	for (uint32_t x = 13; x < 19; x++) tiles.push_back(RailTile(x, 8, x == 15 ? TrackBits{Track::Left} : TrackBits{Track::X}));
	TestGraph g(tiles);

	/* Tile (15, 8) bends towards (15, 7); the X track on (16, 8) bends with it. */
	PieceID border = g.Get(16, 8, SplineLayer::Rail, PieceShape::X);
	CHECK(std::abs(g.graph.Evaluate(border, TILE).tangent.y) > 0.1);

	g.changed.clear();
	g.graph.RemoveChunk(g.grid.IndexOfTile(15, 8), g.changed);
	CHECK(std::find(g.changed.begin(), g.changed.end(), g.grid.IndexOfTile(16, 8)) != g.changed.end());
	CHECK(g.graph.GetPiece(border).length == Approx(TILE));
	CHECK(g.graph.Evaluate(border, TILE).tangent.y == Approx(0.0).margin(1e-12));
}

TEST_CASE("Splines - debug overlay triangles face up")
{
	TestGraph g({RailTile(3, 2, Track::X), RailTile(2, 2, Track::Left), RailTile(2, 1, Track::Y), RoadTile(5, 5, ROAD_ALL, true)});
	TerrainMesh mesh;
	g.graph.BuildOverlay(g.chunks.front(), {0, 0, 0}, mesh);
	REQUIRE(!mesh.indices.empty());
	for (uint16_t index : mesh.indices) REQUIRE(index < mesh.vertices.size());
	for (size_t i = 0; i < mesh.indices.size(); i += 3) {
		const TerrainVertex &a = mesh.vertices[mesh.indices[i]];
		const TerrainVertex &b = mesh.vertices[mesh.indices[i + 1]];
		const TerrainVertex &c = mesh.vertices[mesh.indices[i + 2]];
		Vec3f normal = Cross(Vec3f{b.x - a.x, b.y - a.y, b.z - a.z}, Vec3f{c.x - a.x, c.y - a.y, c.z - a.z});
		CHECK(normal.z > 0.0f);
	}
}
