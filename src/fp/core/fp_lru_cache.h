/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file fp_lru_cache.h Cache that drops the least recently used entries once it exceeds a memory budget. */

#ifndef FP_LRU_CACHE_H
#define FP_LRU_CACHE_H

#include <list>
#include <memory>

namespace fp {

/**
 * Cache of shared, immutable values with a budget in bytes.
 *
 * When an insertion takes the cache over its budget, the least recently
 * used entries go until it fits again; the newest entry always stays, even
 * when it alone is larger than the budget. Values are handed out as shared
 * pointers, so a value someone still holds lives on after it left the cache.
 * @tparam Key Type of the key; must be hashable.
 * @tparam Value Type of the values.
 */
template <typename Key, typename Value>
class LruCache {
public:
	/**
	 * Create an empty cache.
	 * @param budget Memory the values may use, in bytes.
	 */
	explicit LruCache(size_t budget) : budget(budget) {}

	/**
	 * Look a value up and mark it as most recently used.
	 * @param key The key.
	 * @return The value, or nullptr if it is not in the cache.
	 */
	std::shared_ptr<const Value> Find(const Key &key)
	{
		auto it = this->index.find(key);
		if (it == this->index.end()) return nullptr;
		this->entries.splice(this->entries.begin(), this->entries, it->second);
		return it->second->value;
	}

	/**
	 * Add a value, or replace the value of a key, as most recently used.
	 * @param key The key.
	 * @param value The value.
	 * @param size Memory used by the value, in bytes.
	 */
	void Insert(const Key &key, std::shared_ptr<const Value> value, size_t size)
	{
		if (auto it = this->index.find(key); it != this->index.end()) this->Erase(it->second);

		this->entries.push_front({key, std::move(value), size});
		this->index[key] = this->entries.begin();
		this->size += size;

		while (this->size > this->budget && this->entries.size() > 1) this->Erase(std::prev(this->entries.end()));
	}

	/** Remove all values. */
	void Clear()
	{
		this->entries.clear();
		this->index.clear();
		this->size = 0;
	}

	/**
	 * Number of values in the cache.
	 * @return The count.
	 */
	size_t GetCount() const { return this->entries.size(); }

	/**
	 * Memory used by the values in the cache.
	 * @return Size in bytes.
	 */
	size_t GetSize() const { return this->size; }

	/**
	 * Memory the values may use.
	 * @return Size in bytes.
	 */
	size_t GetBudget() const { return this->budget; }

private:
	/** A value in the cache. */
	struct Entry {
		Key key; ///< Its key.
		std::shared_ptr<const Value> value; ///< The value.
		size_t size; ///< Memory used by the value, in bytes.
	};

	size_t budget; ///< Memory the values may use, in bytes.
	size_t size = 0; ///< Memory used by the values, in bytes.
	std::list<Entry> entries; ///< The values, most recently used first.
	std::unordered_map<Key, typename std::list<Entry>::iterator> index; ///< Position of each key in #entries.

	/**
	 * Remove a value.
	 * @param it Its position in #entries.
	 */
	void Erase(typename std::list<Entry>::iterator it)
	{
		this->size -= it->size;
		this->index.erase(it->key);
		this->entries.erase(it);
	}
};

} // namespace fp

#endif /* FP_LRU_CACHE_H */
