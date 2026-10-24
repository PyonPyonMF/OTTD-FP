/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file fp_splines_curve.cpp Curve primitives of the spline graph: planar Hermite segments and vertical profiles. */

#include "../../stdafx.h"
#include "fp_splines_curve.h"

#include "../../safeguards.h"

namespace fp {

/** Nodes and weights of the five-point Gauss-Legendre quadrature on [-1, 1]. */
static constexpr std::array<std::pair<double, double>, 5> GAUSS_LEGENDRE = {{
	{0.0, 0.5688888888888889},
	{-0.5384693101056831, 0.4786286704993665},
	{0.5384693101056831, 0.4786286704993665},
	{-0.9061798459386640, 0.2369268850561891},
	{0.9061798459386640, 0.2369268850561891},
}};

/**
 * Horizontal part of a vector.
 * @param v The vector.
 * @return The vector with Z set to zero.
 */
static Vec3d Flat(const Vec3d &v)
{
	return {v.x, v.y, 0.0};
}

/**
 * Define the segment by its end points and tangents.
 * @param p0 Start point.
 * @param m0 Tangent at the start; its length shapes the curve, see GetHermiteTangentLengths().
 * @param p1 End point.
 * @param m1 Tangent at the end.
 */
void HermiteSegment::Set(const Vec3d &p0, const Vec3d &m0, const Vec3d &p1, const Vec3d &m1)
{
	Vec3d q0 = Flat(p0), q1 = Flat(p1), t0 = Flat(m0), t1 = Flat(m1);
	this->a = q0;
	this->b = t0;
	this->c = q0 * -3.0 + t0 * -2.0 + q1 * 3.0 - t1;
	this->d = q0 * 2.0 + t0 - q1 * 2.0 + t1;

	this->lengths[0] = 0.0;
	for (int i = 0; i < TABLE_SIZE; i++) {
		this->lengths[i + 1] = this->lengths[i] + this->IntegrateSpeed(static_cast<double>(i) / TABLE_SIZE, static_cast<double>(i + 1) / TABLE_SIZE);
	}
}

/**
 * Point on the segment.
 * @param u Curve parameter, 0 to 1.
 * @return The point.
 */
Vec3d HermiteSegment::Position(double u) const
{
	return this->a + (this->b + (this->c + this->d * u) * u) * u;
}

/**
 * First derivative with respect to the curve parameter.
 * @param u Curve parameter, 0 to 1.
 * @return The derivative.
 */
Vec3d HermiteSegment::Derivative(double u) const
{
	return this->b + (this->c * 2.0 + this->d * (3.0 * u)) * u;
}

/**
 * Second derivative with respect to the curve parameter.
 * @param u Curve parameter, 0 to 1.
 * @return The derivative.
 */
Vec3d HermiteSegment::SecondDerivative(double u) const
{
	return this->c * 2.0 + this->d * (6.0 * u);
}

/**
 * Speed of the curve parameter.
 * @param u Curve parameter, 0 to 1.
 * @return Arc length per unit of the parameter.
 */
double HermiteSegment::Speed(double u) const
{
	return fp::Length(this->Derivative(u));
}

/**
 * Arc length between two parameters.
 * @param u0 Start parameter.
 * @param u1 End parameter.
 * @return The arc length; negative if \a u1 is before \a u0.
 */
double HermiteSegment::IntegrateSpeed(double u0, double u1) const
{
	double half = (u1 - u0) / 2.0;
	double mid = (u0 + u1) / 2.0;
	double sum = 0.0;
	for (const auto &[node, weight] : GAUSS_LEGENDRE) sum += weight * this->Speed(mid + half * node);
	return sum * half;
}

/**
 * Curve parameter at an arc length: the inverse of the natural parametrisation.
 * @param s Arc length from the start; clamped to the segment.
 * @return The curve parameter, 0 to 1.
 */
double HermiteSegment::ParameterAt(double s) const
{
	if (s <= 0.0) return 0.0;
	if (s >= this->Length()) return 1.0;

	int i = static_cast<int>(std::upper_bound(this->lengths.begin(), this->lengths.end(), s) - this->lengths.begin()) - 1;
	i = std::clamp(i, 0, TABLE_SIZE - 1);
	const double u_low = static_cast<double>(i) / TABLE_SIZE;
	const double u_high = static_cast<double>(i + 1) / TABLE_SIZE;
	const double span = this->lengths[i + 1] - this->lengths[i];
	if (span <= 0.0) return u_low;

	/* Newton's method from a linear guess; the speed hardly changes within a sub-interval. */
	double u = u_low + (u_high - u_low) * (s - this->lengths[i]) / span;
	for (int iteration = 0; iteration < 4; iteration++) {
		double error = this->lengths[i] + this->IntegrateSpeed(u_low, u) - s;
		double speed = this->Speed(u);
		if (speed <= 0.0) break;
		u = std::clamp(u - error / speed, u_low, u_high);
		if (std::abs(error) < 1e-9) break;
	}
	return u;
}

/** Where the tangent lines of a curve that bends one way meet. */
struct TangentCorner {
	double a; ///< Distance from the start to the corner, along the start direction.
	double b; ///< Distance from the corner to the end, along the end direction.
	double turn; ///< Angle between the directions, in radians.
};

/**
 * Find where the tangent lines from both ends of a curve meet, if the curve bends one way.
 * @param p0 Start point.
 * @param t0 Unit direction at the start, horizontal.
 * @param p1 End point.
 * @param t1 Unit direction at the end, horizontal.
 * @return The corner, or nothing if the curve is straight or has to bend both ways.
 */
static std::optional<TangentCorner> GetTangentCorner(const Vec3d &p0, const Vec3d &t0, const Vec3d &p1, const Vec3d &t1)
{
	const Vec3d chord = Flat(p1 - p0);
	const double length = fp::Length(chord);
	if (length <= 0.0) return std::nullopt;

	const Vec3d direction = chord * (1.0 / length);
	const double turn_in = Cross(t0, direction).z;
	const double turn_out = Cross(direction, t1).z;
	const bool one_way = (turn_in > 1e-9 && turn_out > 1e-9) || (turn_in < -1e-9 && turn_out < -1e-9);
	if (!one_way) return std::nullopt;

	/* p0 + a * t0 = p1 - b * t1 */
	const double cross = Cross(t0, t1).z;
	TangentCorner corner{Cross(chord, t1).z / cross, Cross(t0, chord).z / cross, std::atan2(std::abs(cross), Dot(t0, t1))};
	if (!std::isfinite(corner.a) || !std::isfinite(corner.b) || corner.a <= 0 || corner.b <= 0 || corner.turn >= std::numbers::pi - 1e-6) return std::nullopt;
	return corner;
}

/**
 * Lengths of the tangents of a Hermite segment between two points with given directions.
 *
 * When the curve bends one way, the tangent lines from both ends meet in a
 * corner, and the Bézier control points are put on the way to it, at
 * k = 4/3 tan(turn / 4) / tan(turn / 2) of the distance. The control
 * polygon is then convex, so the curve never bends the other way; for ends
 * at the same distance from the corner the curve is the best cubic
 * approximation of a circular arc (k = 0.552 for a quarter circle), see
 * GetFilletSplit(). Curves that do not bend one way, i.e. straight pieces and
 * pieces that have to bend both ways, get tangents as long as the chord:
 * that makes the curve parameter of straight pieces proportional to the arc
 * length, and keeps S-shaped pieces from swinging out further than needed.
 * @param p0 Start point.
 * @param d0 Unit direction at the start.
 * @param p1 End point.
 * @param d1 Unit direction at the end.
 * @return Lengths of the tangents at the start and at the end.
 */
std::pair<double, double> GetHermiteTangentLengths(const Vec3d &p0, const Vec3d &d0, const Vec3d &p1, const Vec3d &d1)
{
	const double length = fp::Length(Flat(p1 - p0));
	std::optional<TangentCorner> corner = GetTangentCorner(p0, Flat(d0), p1, Flat(d1));
	if (!corner.has_value()) return {length, length};

	const double k = 4.0 / 3.0 * std::tan(corner->turn / 4.0) / std::tan(corner->turn / 2.0);
	return {3.0 * k * corner->a, 3.0 * k * corner->b};
}

/**
 * Split a curve that bends one way into a straight part and a circular arc.
 *
 * Of all curves between two points with given directions, the one with the
 * gentlest sharpest bend runs straight along the longer tangent line and
 * then follows the largest circle that touches both tangent lines. This is
 * what turns the kinks of OpenTTD tracks into arcs and straight track.
 * @param p0 Start point.
 * @param d0 Unit direction at the start.
 * @param p1 End point.
 * @param d1 Unit direction at the end.
 * @return Point and direction where the straight part meets the arc, or nothing if the curve is a single arc, straight, or bends both ways.
 */
std::optional<std::pair<Vec3d, Vec3d>> GetFilletSplit(const Vec3d &p0, const Vec3d &d0, const Vec3d &p1, const Vec3d &d1)
{
	const Vec3d t0 = Flat(d0), t1 = Flat(d1);
	std::optional<TangentCorner> corner = GetTangentCorner(p0, t0, p1, t1);
	if (!corner.has_value() || std::abs(corner->a - corner->b) < 1e-9 * (corner->a + corner->b)) return std::nullopt;

	if (corner->a > corner->b) return std::pair{Flat(p0) + t0 * (corner->a - corner->b), t0};
	return std::pair{Flat(p1) - t1 * (corner->b - corner->a), t1};
}

/**
 * Parabolic blend between two grades.
 * @param s Position.
 * @param at Position of the corner.
 * @param z Height of the corner.
 * @param g1 Grade before the corner.
 * @param g2 Grade after the corner.
 * @param half Half-length of the blend.
 * @return Height and grade at \a s.
 */
static std::pair<double, double> Blend(double s, double at, double z, double g1, double g2, double half)
{
	double r = s - at + half;
	return {z + g1 * (s - at) + (g2 - g1) * r * r / (4.0 * half), g1 + (g2 - g1) * r / (2.0 * half)};
}

/**
 * Height and grade at a distance along the piece.
 * @param p The profile.
 * @param s Horizontal distance from the start, clamped to the piece.
 * @return Height and grade.
 */
static std::pair<double, double> Evaluate(const VerticalProfile &p, double s)
{
	s = std::clamp(s, 0.0, p.length);
	const double mid = p.length / 2.0;
	const double g_first = p.FirstGrade();
	const double g_second = p.SecondGrade();

	if (p.blend_start > 0 && s < p.blend_start) return Blend(s, 0.0, p.z_start, p.grade_before, g_first, p.blend_start);
	if (p.blend_end > 0 && s > p.length - p.blend_end) return Blend(s, p.length, p.z_end, g_second, p.grade_after, p.blend_end);
	if (p.blend_mid > 0 && std::abs(s - mid) < p.blend_mid) return Blend(s, mid, p.z_mid, g_first, g_second, p.blend_mid);

	if (s < mid) return {p.z_start + g_first * s, g_first};
	return {p.z_mid + g_second * (s - mid), g_second};
}

/**
 * Height at a distance along the piece.
 * @param s Horizontal distance from the start, clamped to the piece.
 * @return The height.
 */
double VerticalProfile::Height(double s) const
{
	return Evaluate(*this, s).first;
}

/**
 * Grade at a distance along the piece.
 * @param s Horizontal distance from the start, clamped to the piece.
 * @return Rise per horizontal distance.
 */
double VerticalProfile::Grade(double s) const
{
	return Evaluate(*this, s).second;
}

} // namespace fp
