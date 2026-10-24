/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file fp_splines_curve.h Curve primitives of the spline graph: planar Hermite segments and vertical profiles. */

#ifndef FP_SPLINES_CURVE_H
#define FP_SPLINES_CURVE_H

#include <optional>

#include "../core/fp_math.h"

namespace fp {

/**
 * Planar cubic Hermite segment with natural (arc length) parametrisation.
 *
 * Only the X and Y components of the vectors are used; Z is ignored and
 * returned as zero. Heights along a piece are described separately by a
 * #VerticalProfile over the horizontal arc length, so that the shape of a
 * curve does not depend on the vertical scale of the Metric Bridge.
 *
 * The arc length is integrated with Gauss-Legendre quadrature on a table of
 * sub-intervals, and inverted with Newton steps, so positions at a given
 * distance are accurate to well below a millimetre on tile-sized curves.
 */
class HermiteSegment {
public:
	static constexpr int TABLE_SIZE = 8; ///< Number of sub-intervals of the arc length table.

	void Set(const Vec3d &p0, const Vec3d &m0, const Vec3d &p1, const Vec3d &m1);

	/**
	 * Length of the segment.
	 * @return The arc length.
	 */
	double Length() const { return this->lengths.back(); }

	Vec3d Position(double u) const;
	Vec3d Derivative(double u) const;
	Vec3d SecondDerivative(double u) const;
	double ParameterAt(double s) const;

private:
	Vec3d a{}; ///< Constant coefficient of the polynomial.
	Vec3d b{}; ///< Linear coefficient of the polynomial.
	Vec3d c{}; ///< Quadratic coefficient of the polynomial.
	Vec3d d{}; ///< Cubic coefficient of the polynomial.
	std::array<double, TABLE_SIZE + 1> lengths{}; ///< Arc length from the start up to the start of every sub-interval, plus the total.

	double Speed(double u) const;
	double IntegrateSpeed(double u0, double u1) const;
};

std::pair<double, double> GetHermiteTangentLengths(const Vec3d &p0, const Vec3d &d0, const Vec3d &p1, const Vec3d &d1);
std::optional<std::pair<Vec3d, Vec3d>> GetFilletSplit(const Vec3d &p0, const Vec3d &d0, const Vec3d &p1, const Vec3d &d1);

/**
 * Heights along a piece of the spline graph, as a function of the horizontal arc length.
 *
 * The raw profile is a polyline through the start, the middle and the end of
 * the piece: that is exact for everything vehicles drive on in OpenTTD, as
 * surfaces are planar on either side of the middle of a piece (foundations,
 * inclined track, bridge ramps). Every corner of the polyline is rounded off
 * by a parabolic blend, the classic vertical curve of railways, which makes
 * the height continuously differentiable. The blends at the ends use the
 * grade of the neighbouring pieces, so a train going from flat track onto a
 * ramp tilts smoothly instead of kinking.
 */
struct VerticalProfile {
	double length = 0; ///< Horizontal length of the piece.
	double z_start = 0; ///< Height of the polyline at the start.
	double z_mid = 0; ///< Height of the polyline in the middle.
	double z_end = 0; ///< Height of the polyline at the end.
	double grade_before = 0; ///< Grade beyond the start, along the piece; equal to the first grade when there is no neighbour.
	double grade_after = 0; ///< Grade beyond the end, along the piece; equal to the last grade when there is no neighbour.
	double blend_start = 0; ///< Half-length of the blend at the start; at most a quarter of #length.
	double blend_mid = 0; ///< Half-length of the blend in the middle; at most a quarter of #length.
	double blend_end = 0; ///< Half-length of the blend at the end; at most a quarter of #length.

	/**
	 * Grade of the first half of the polyline.
	 * @return Rise per horizontal distance.
	 */
	double FirstGrade() const { return this->length > 0 ? (this->z_mid - this->z_start) * 2 / this->length : 0; }

	/**
	 * Grade of the second half of the polyline.
	 * @return Rise per horizontal distance.
	 */
	double SecondGrade() const { return this->length > 0 ? (this->z_end - this->z_mid) * 2 / this->length : 0; }

	double Height(double s) const;
	double Grade(double s) const;
};

} // namespace fp

#endif /* FP_SPLINES_CURVE_H */
