/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file fp_rect.cpp Rectangle arithmetic for compositing the 3D view with the 2D interface. */

#include "../../stdafx.h"
#include "fp_rect.h"

#include "../../safeguards.h"

namespace fp {

/**
 * Check whether a rectangle covers no pixels. Rectangles are inclusive, like everywhere in OpenTTD.
 * @param r The rectangle.
 * @return True iff the rectangle is empty.
 */
bool IsEmptyArea(const Rect &r)
{
	return r.left > r.right || r.top > r.bottom;
}

/**
 * Intersection of two rectangles.
 * @param a First rectangle.
 * @param b Second rectangle.
 * @return The intersection, which may be empty.
 */
Rect IntersectRects(const Rect &a, const Rect &b)
{
	return {std::max(a.left, b.left), std::max(a.top, b.top), std::min(a.right, b.right), std::min(a.bottom, b.bottom)};
}

/**
 * Cut a hole into a rectangle.
 * The remainder is split into at most four rectangles: a full-width band
 * above and below the hole, and the parts left and right of it.
 * @param from The rectangle to cut into.
 * @param hole The part to remove.
 * @param[out] out Receives the non-empty remaining parts.
 */
void SubtractRect(const Rect &from, const Rect &hole, std::vector<Rect> &out)
{
	if (IsEmptyArea(from)) return;

	Rect cut = IntersectRects(from, hole);
	if (IsEmptyArea(cut)) {
		out.push_back(from);
		return;
	}

	if (cut.top > from.top) out.push_back({from.left, from.top, from.right, cut.top - 1});
	if (cut.bottom < from.bottom) out.push_back({from.left, cut.bottom + 1, from.right, from.bottom});
	if (cut.left > from.left) out.push_back({from.left, cut.top, cut.left - 1, cut.bottom});
	if (cut.right < from.right) out.push_back({cut.right + 1, cut.top, from.right, cut.bottom});
}

/**
 * Cut a hole into an area made of disjoint rectangles.
 * @param[in,out] area The area; stays a set of disjoint rectangles.
 * @param hole The part to remove.
 */
void SubtractRects(std::vector<Rect> &area, const Rect &hole)
{
	std::vector<Rect> result;
	result.reserve(area.size() + 3);
	for (const Rect &r : area) SubtractRect(r, hole, result);
	area = std::move(result);
}

} // namespace fp
