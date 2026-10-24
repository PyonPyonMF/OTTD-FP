/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file fp_test_rect.cpp Tests of the rectangle arithmetic of the first-person mode. */

#include "../../stdafx.h"

#include "../../3rdparty/catch2/catch.hpp"

#include "../core/fp_rect.h"

#include "../../safeguards.h"

using namespace fp;

/**
 * Number of pixels in a set of rectangles.
 * @param area The rectangles.
 * @return The number of pixels, counting overlaps twice.
 */
static int64_t Pixels(const std::vector<Rect> &area)
{
	int64_t sum = 0;
	for (const Rect &r : area) sum += static_cast<int64_t>(r.Width()) * r.Height();
	return sum;
}

/**
 * Check that no two rectangles overlap.
 * @param area The rectangles.
 * @return True iff they are disjoint.
 */
static bool Disjoint(const std::vector<Rect> &area)
{
	for (size_t i = 0; i < area.size(); i++) {
		for (size_t j = i + 1; j < area.size(); j++) {
			if (!IsEmptyArea(IntersectRects(area[i], area[j]))) return false;
		}
	}
	return true;
}

TEST_CASE("Rect - subtracting a hole in the middle leaves four parts")
{
	std::vector<Rect> area = {{0, 0, 99, 99}};
	SubtractRects(area, {40, 40, 59, 59});
	CHECK(area.size() == 4);
	CHECK(Pixels(area) == 100 * 100 - 20 * 20);
	CHECK(Disjoint(area));
}

TEST_CASE("Rect - subtracting something outside changes nothing")
{
	std::vector<Rect> area = {{0, 0, 99, 99}};
	SubtractRects(area, {200, 200, 300, 300});
	REQUIRE(area.size() == 1);
	CHECK(area[0].left == 0);
	CHECK(area[0].bottom == 99);
}

TEST_CASE("Rect - subtracting everything leaves nothing")
{
	std::vector<Rect> area = {{10, 10, 20, 20}};
	SubtractRects(area, {0, 0, 100, 100});
	CHECK(area.empty());
}

TEST_CASE("Rect - overlapping windows")
{
	std::vector<Rect> area = {{0, 0, 639, 479}};
	SubtractRects(area, {600, -20, 800, 100}); // Over the top right corner.
	SubtractRects(area, {-50, 400, 100, 600}); // Over the bottom left corner.
	SubtractRects(area, {300, 200, 340, 240}); // A tooltip in the middle.
	CHECK(Pixels(area) == 640 * 480 - 40 * 101 - 101 * 80 - 41 * 41);
	CHECK(Disjoint(area));
}
