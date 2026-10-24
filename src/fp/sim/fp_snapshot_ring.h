/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file fp_snapshot_ring.h Lock-free hand-over of snapshots from the game thread to the drawing thread. */

#ifndef FP_SNAPSHOT_RING_H
#define FP_SNAPSHOT_RING_H

#include <array>
#include <atomic>

namespace fp {

/**
 * Wait-free hand-over of values from one producer thread to one consumer thread.
 *
 * Unlike a classic triple buffer the consumer keeps the two most recent
 * values it acquired, so it can interpolate between them. Four buffers are
 * in use: one is written by the producer, two are held by the consumer and
 * one sits in the exchange slot. The four indices are always distinct, so
 * no buffer is ever accessed by both threads at the same time. Buffers are
 * reused, which keeps allocations out of the steady state.
 *
 * When the consumer is slower than the producer it skips values; the most
 * recent value is never lost.
 * @tparam T Type of the values.
 */
template <typename T>
class SnapshotRing {
public:
	/**
	 * Buffer the producer fills next. Only for the producer thread.
	 * @return The buffer, which still holds an old value.
	 */
	T &Back() { return this->buffers[this->write_index]; }

	/** Make the value in Back() available to the consumer. Only for the producer thread. */
	void Publish()
	{
		uint8_t old = this->exchange.exchange(this->write_index | FRESH, std::memory_order_acq_rel);
		this->write_index = old & INDEX_MASK;
	}

	/**
	 * Take the most recently published value, if it has not been taken yet.
	 * Only for the consumer thread.
	 * @return True iff Current() changed; the previous current value moved to Previous().
	 */
	bool Acquire()
	{
		if ((this->exchange.load(std::memory_order_acquire) & FRESH) == 0) return false;

		uint8_t taken = this->exchange.exchange(this->previous_index, std::memory_order_acq_rel);
		this->previous_index = this->current_index;
		this->current_index = taken & INDEX_MASK;
		return true;
	}

	/**
	 * Most recently acquired value. Only for the consumer thread.
	 * @return The value, default constructed if nothing was acquired yet.
	 */
	const T &Current() const { return this->buffers[this->current_index]; }

	/**
	 * Value acquired before Current(). Only for the consumer thread.
	 * @return The value, default constructed if fewer than two values were acquired.
	 */
	const T &Previous() const { return this->buffers[this->previous_index]; }

private:
	static constexpr uint8_t INDEX_MASK = 0x03; ///< Bits of the exchange slot holding the buffer index.
	static constexpr uint8_t FRESH = 0x04; ///< Flag in the exchange slot for "not yet acquired".

	std::array<T, 4> buffers{}; ///< The buffers.
	uint8_t write_index = 0; ///< Buffer owned by the producer.
	std::atomic<uint8_t> exchange{1}; ///< Buffer in the exchange slot, with #FRESH if published but not acquired.
	uint8_t current_index = 2; ///< Most recent buffer owned by the consumer.
	uint8_t previous_index = 3; ///< Older buffer owned by the consumer.
};

} // namespace fp

#endif /* FP_SNAPSHOT_RING_H */
