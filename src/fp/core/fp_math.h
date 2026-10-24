/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file fp_math.h Small vector and matrix types of the first-person mode. */

#ifndef FP_MATH_H
#define FP_MATH_H

#include <array>
#include <cmath>
#include <numbers>

namespace fp {

/**
 * Three component vector.
 *
 * The first-person world is right-handed with Z pointing up: X runs along
 * the map's X axis (towards the south-west in the 2D view), Y along the
 * map's Y axis (towards the south-east) and Z is the height.
 * World positions are kept in double precision; everything sent to the GPU
 * is relative to the camera and in single precision.
 * @tparam T Component type.
 */
template <typename T>
struct Vec3 {
	T x = 0; ///< X component.
	T y = 0; ///< Y component.
	T z = 0; ///< Z component.

	constexpr Vec3() = default;

	/**
	 * Create a vector from its components.
	 * @param x X component.
	 * @param y Y component.
	 * @param z Z component.
	 */
	constexpr Vec3(T x, T y, T z) : x(x), y(y), z(z) {}

	/**
	 * Convert from a vector of another component type.
	 * @param other The vector to convert.
	 */
	template <typename U>
	constexpr explicit Vec3(const Vec3<U> &other) : x(static_cast<T>(other.x)), y(static_cast<T>(other.y)), z(static_cast<T>(other.z)) {}

	constexpr Vec3 operator+(const Vec3 &other) const { return {this->x + other.x, this->y + other.y, this->z + other.z}; }
	constexpr Vec3 operator-(const Vec3 &other) const { return {this->x - other.x, this->y - other.y, this->z - other.z}; }
	constexpr Vec3 operator*(T s) const { return {this->x * s, this->y * s, this->z * s}; }
	constexpr Vec3 &operator+=(const Vec3 &other) { this->x += other.x; this->y += other.y; this->z += other.z; return *this; }
	constexpr bool operator==(const Vec3 &other) const = default;
};

using Vec3d = Vec3<double>; ///< Vector for world positions.
using Vec3f = Vec3<float>; ///< Vector for camera-relative and GPU data.

/**
 * Dot product of two vectors.
 * @param a First vector.
 * @param b Second vector.
 * @return The dot product.
 */
template <typename T>
constexpr T Dot(const Vec3<T> &a, const Vec3<T> &b)
{
	return a.x * b.x + a.y * b.y + a.z * b.z;
}

/**
 * Cross product of two vectors.
 * @param a First vector.
 * @param b Second vector.
 * @return The cross product.
 */
template <typename T>
constexpr Vec3<T> Cross(const Vec3<T> &a, const Vec3<T> &b)
{
	return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

/**
 * Length of a vector.
 * @param v The vector.
 * @return The length.
 */
template <typename T>
inline T Length(const Vec3<T> &v)
{
	return std::sqrt(Dot(v, v));
}

/**
 * Vector of unit length in the same direction.
 * @param v The vector, must not be the zero vector.
 * @return The normalised vector.
 */
template <typename T>
inline Vec3<T> Normalise(const Vec3<T> &v)
{
	return v * (T{1} / Length(v));
}

/**
 * Linear interpolation.
 * @param a Value at \a t == 0.
 * @param b Value at \a t == 1.
 * @param t Interpolation factor.
 * @return The interpolated value.
 */
template <typename T, typename F>
constexpr T Lerp(const T &a, const T &b, F t)
{
	return a + (b - a) * t;
}

/**
 * Interpolate between two angles along the shortest way round.
 * @param a Angle at \a t == 0, in radians.
 * @param b Angle at \a t == 1, in radians.
 * @param t Interpolation factor.
 * @return The interpolated angle, in radians.
 */
inline double LerpAngle(double a, double b, double t)
{
	double diff = std::remainder(b - a, 2 * std::numbers::pi);
	return a + diff * t;
}

/** 4x4 matrix of floats in column-major order, as OpenGL expects it. */
struct Mat4f {
	std::array<float, 16> m{}; ///< Elements, column after column.

	/**
	 * Access an element.
	 * @param row Row of the element.
	 * @param col Column of the element.
	 * @return Reference to the element.
	 */
	constexpr float &At(int row, int col) { return this->m[col * 4 + row]; }

	/** @copydoc At */
	constexpr float At(int row, int col) const { return this->m[col * 4 + row]; }

	/**
	 * Identity matrix.
	 * @return The identity matrix.
	 */
	static constexpr Mat4f Identity()
	{
		Mat4f r;
		for (int i = 0; i < 4; i++) r.At(i, i) = 1.0f;
		return r;
	}

	/**
	 * Matrix product.
	 * @param other Right-hand side.
	 * @return The product (this * other).
	 */
	constexpr Mat4f operator*(const Mat4f &other) const
	{
		Mat4f r;
		for (int row = 0; row < 4; row++) {
			for (int col = 0; col < 4; col++) {
				float sum = 0.0f;
				for (int k = 0; k < 4; k++) sum += this->At(row, k) * other.At(k, col);
				r.At(row, col) = sum;
			}
		}
		return r;
	}
};

/**
 * Perspective projection.
 * With \a reversed_z the far plane is at infinity and depth goes from 1 at the
 * near plane to 0 at infinity; this needs a zero-to-one clip space and a
 * floating point depth buffer, but keeps the precision uniform over distance.
 * @param fov_y Vertical field of view, in radians.
 * @param aspect Width divided by height.
 * @param z_near Distance of the near plane.
 * @param z_far Distance of the far plane, ignored for \a reversed_z.
 * @param reversed_z Whether to build an infinite reversed-Z projection.
 * @return The projection matrix.
 */
inline Mat4f MakePerspective(float fov_y, float aspect, float z_near, float z_far, bool reversed_z)
{
	float f = 1.0f / std::tan(fov_y / 2.0f);
	Mat4f r;
	r.At(0, 0) = f / aspect;
	r.At(1, 1) = f;
	r.At(3, 2) = -1.0f;
	if (reversed_z) {
		r.At(2, 3) = z_near;
	} else {
		r.At(2, 2) = (z_far + z_near) / (z_near - z_far);
		r.At(2, 3) = 2.0f * z_far * z_near / (z_near - z_far);
	}
	return r;
}

/**
 * View rotation of a camera located at the origin, with Z as world up.
 * @param forward Unit vector the camera looks along; must not be vertical.
 * @return The view matrix.
 */
inline Mat4f MakeViewRotation(const Vec3f &forward)
{
	Vec3f right = Normalise(Cross(forward, Vec3f{0, 0, 1}));
	Vec3f up = Cross(right, forward);

	Mat4f r = Mat4f::Identity();
	r.At(0, 0) = right.x;    r.At(0, 1) = right.y;    r.At(0, 2) = right.z;
	r.At(1, 0) = up.x;       r.At(1, 1) = up.y;       r.At(1, 2) = up.z;
	r.At(2, 0) = -forward.x; r.At(2, 1) = -forward.y; r.At(2, 2) = -forward.z;
	return r;
}

} // namespace fp

#endif /* FP_MATH_H */
