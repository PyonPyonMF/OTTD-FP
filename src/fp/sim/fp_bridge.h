/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file fp_bridge.h The Sim Bridge: the boundary between the simulation and the first-person runtime. */

#ifndef FP_BRIDGE_H
#define FP_BRIDGE_H

#include "../../tile_type.h"
#include "fp_snapshot.h"
#include "fp_snapshot_ring.h"
#include "fp_tile_journal.h"

namespace fp {

/**
 * The Sim Bridge (docs/fp/architecture.md, section 5).
 *
 * It receives the hooks from the core and turns them into data the
 * first-person runtime can use without touching the simulation: a journal
 * of changed chunks and a snapshot of the vehicles after every tick.
 *
 * Nothing is recorded while no one uses the bridge, so the first-person
 * mode costs one atomic load per hook when it is not in use.
 *
 * All members are used with the game state lock held, except
 * #GetSnapshots() whose consumer side belongs to the drawing thread.
 */
class Bridge {
public:
	/** Most vehicle parts in one snapshot, to bound the work per tick. */
	static constexpr size_t MAX_VEHICLE_FRAMES = 8192;

	/** Counters for diagnostics. */
	struct Statistics {
		uint64_t snapshots_published = 0; ///< Snapshots published since the program started.
		uint64_t tiles_marked = 0; ///< Tile changes recorded since the program started.
		size_t last_vehicle_count = 0; ///< Vehicle parts in the last snapshot.
	};

	static Bridge &Get();

	void Acquire();
	void Release();

	/**
	 * Whether anyone uses the bridge.
	 * @return True iff the bridge records changes and publishes snapshots.
	 */
	bool IsActive() const { return this->active.load(std::memory_order_relaxed); }

	/**
	 * Current session. A session starts whenever the bridge becomes active or the map is replaced while it is active.
	 * @return The session number, never 0 while active.
	 */
	uint32_t GetSession() const { return this->session; }

	/**
	 * Number of times a map was allocated since the program started.
	 * @return The world epoch.
	 */
	uint32_t GetWorldEpoch() const { return this->world_epoch; }

	void SetInterest(int x, int y, int radius);

	/**
	 * Hand out the chunks that changed since the last call.
	 * @param[out] out The changed chunks are appended to this.
	 */
	void TakeDirtyChunks(std::vector<ChunkIndex> &out) { this->journal.Take(out); }

	/**
	 * Snapshots of the vehicles; the consumer side may be used without the game state lock.
	 * @return The snapshot ring.
	 */
	SnapshotRing<TickSnapshot> &GetSnapshots() { return this->snapshots; }

	/**
	 * Diagnostic counters.
	 * @return The counters.
	 */
	const Statistics &GetStatistics() const { return this->statistics; }

	void OnMapAllocated();
	void OnTileDirty(TileIndex tile);
	void OnTickEnd();
	void OnVehicleDestroyed(VehicleID index);

private:
	std::atomic<bool> active{false}; ///< Whether anyone uses the bridge.
	uint users = 0; ///< Number of users of the bridge.
	uint32_t session = 0; ///< Current session.
	uint32_t world_epoch = 0; ///< Number of maps allocated.

	int interest_x = 0; ///< X coordinate of the centre of the area of interest, in world units.
	int interest_y = 0; ///< Y coordinate of the centre of the area of interest, in world units.
	int interest_radius = 0; ///< Half the side of the (square) area of interest, in world units.

	TileJournal journal; ///< Chunks that changed.
	SnapshotRing<TickSnapshot> snapshots; ///< Vehicle snapshots for the drawing thread.
	std::vector<uint32_t> generations; ///< Per vehicle index: number of vehicles deleted with that index during this session.
	Statistics statistics; ///< Diagnostic counters.

	void StartSession();
	void CaptureSnapshot(TickSnapshot &snapshot) const;
};

} // namespace fp

#endif /* FP_BRIDGE_H */
