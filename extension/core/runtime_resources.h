// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <array>
#include <cstddef>
#include <utility>

namespace sourcetv {

// The tracked slots outlive their owner. Explicit Reset orders teardown while
// the engine and extension providers are still available.
template <std::size_t Capacity>
class RuntimeResources
{
public:
	RuntimeResources() = default;
	RuntimeResources(const RuntimeResources&) = delete;
	RuntimeResources& operator=(const RuntimeResources&) = delete;
	~RuntimeResources() { Reset(); }

	template <auto Release, typename T>
	bool Take(T& slot, T resource)
	{
		if (!resource)
			return false;

		if (slot || m_Count == Capacity) {
			Release(resource);
			return false;
		}

		slot = resource;
		m_Entries[m_Count++] = {&slot, [](void* address) {
			auto& owned = *static_cast<T*>(address);
			Release(std::exchange(owned, T{}));
		}};
		return true;
	}

	void Reset()
	{
		while (m_Count) {
			const auto& entry = m_Entries[--m_Count];
			entry.release(entry.slot);
		}
	}

private:
	struct Entry
	{
		void* slot;
		void (*release)(void*);
	};

	std::array<Entry, Capacity> m_Entries{};
	std::size_t m_Count = 0;
};

} // namespace sourcetv
