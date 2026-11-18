// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>

namespace sourcetv::abi {

class FieldOffset
{
public:
	static std::optional<FieldOffset> FromGamedata(int bytes)
	{
		if (bytes < 0)
			return std::nullopt;
		return FieldOffset(bytes);
	}

	int Bytes() const { return m_Bytes; }

private:
	explicit FieldOffset(int bytes) : m_Bytes(bytes) {}
	int m_Bytes;
};

class VtableSlot
{
public:
	static std::optional<VtableSlot> FromGamedata(int index)
	{
		if (index < 0)
			return std::nullopt;
		return VtableSlot(index);
	}

	int Index() const { return m_Index; }

private:
	explicit VtableSlot(int index) : m_Index(index) {}
	int m_Index;
};

class BaseAdjustment
{
public:
	static std::optional<BaseAdjustment> FromGamedata(int bytes)
	{
		// This key expresses the forward displacement to an embedded base.
		if (bytes < 0)
			return std::nullopt;
		return BaseAdjustment(bytes);
	}

	void* CompleteObject(void* base) const
	{
		return base ? static_cast<std::byte*>(base) - m_Bytes : nullptr;
	}

private:
	explicit BaseAdjustment(int bytes) : m_Bytes(bytes) {}
	std::ptrdiff_t m_Bytes;
};

inline void AddPlayerFlags(void* entity, FieldOffset flagsField, int32_t flags)
{
	auto* field = static_cast<std::byte*>(entity) + flagsField.Bytes();
	int32_t current;
	std::memcpy(&current, field, sizeof(current));
	current |= flags;
	std::memcpy(field, &current, sizeof(current));
}

template <typename T>
struct LayoutEntry
{
	const char* key;
	std::optional<T>& destination;
};

template <typename T, std::size_t N, typename Lookup>
bool ResolveValues(const LayoutEntry<T> (&entries)[N], Lookup lookup, const char*& failedKey)
{
	std::optional<T> values[N];
	for (std::size_t i = 0; i < N; ++i) {
		int raw;
		if (lookup(entries[i].key, raw))
			values[i] = T::FromGamedata(raw);
		if (!values[i]) {
			failedKey = entries[i].key;
			return false;
		}
	}
	for (std::size_t i = 0; i < N; ++i)
		entries[i].destination = values[i];
	return true;
}

} // namespace sourcetv::abi
