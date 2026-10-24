/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file fp_test_snapshot_ring.cpp Tests of the snapshot hand-over of the first-person mode. */

#include "../../stdafx.h"

#include <thread>

#include "../../3rdparty/catch2/catch.hpp"

#include "../sim/fp_snapshot_ring.h"

#include "../../safeguards.h"

using namespace fp;

TEST_CASE("Snapshot ring - nothing to acquire before the first publish")
{
	SnapshotRing<int> ring;
	CHECK_FALSE(ring.Acquire());
	CHECK(ring.Current() == 0);
	CHECK(ring.Previous() == 0);
}

TEST_CASE("Snapshot ring - consumer keeps the last two values")
{
	SnapshotRing<int> ring;

	ring.Back() = 1;
	ring.Publish();
	REQUIRE(ring.Acquire());
	CHECK(ring.Current() == 1);
	CHECK_FALSE(ring.Acquire());

	ring.Back() = 2;
	ring.Publish();
	REQUIRE(ring.Acquire());
	CHECK(ring.Current() == 2);
	CHECK(ring.Previous() == 1);
}

TEST_CASE("Snapshot ring - a slow consumer gets the latest value")
{
	SnapshotRing<int> ring;
	for (int i = 1; i <= 10; i++) {
		ring.Back() = i;
		ring.Publish();
	}
	REQUIRE(ring.Acquire());
	CHECK(ring.Current() == 10);
}

TEST_CASE("Snapshot ring - producer and consumer on different threads")
{
	/* Each value is a vector filled with one number; a torn read would mix numbers. */
	SnapshotRing<std::vector<int>> ring;
	constexpr int COUNT = 20000;

	std::thread producer([&ring]() {
		for (int i = 1; i <= COUNT; i++) {
			std::vector<int> &back = ring.Back();
			back.assign(64, i);
			ring.Publish();
		}
	});

	int last = 0;
	bool consistent = true;
	bool monotonic = true;
	while (last < COUNT) {
		if (!ring.Acquire()) continue;
		const std::vector<int> &current = ring.Current();
		if (current.empty()) continue;
		consistent &= std::all_of(current.begin(), current.end(), [&](int v) { return v == current.front(); });
		monotonic &= current.front() > last;
		last = current.front();
	}
	producer.join();

	CHECK(consistent);
	CHECK(monotonic);
}
