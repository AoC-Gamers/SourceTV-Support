// SPDX-License-Identifier: GPL-3.0-only
#include "engine/engine_binding.h"

#include <cassert>
#include <cstdio>
#include <cstring>

int main()
{
	using namespace sourcetv::abi;
	assert(!VtableSlot::FromGamedata(-1));
	assert(!FieldOffset::FromGamedata(-1));
	assert(FieldOffset::FromGamedata(0)->Bytes() == 0);
	assert(!BaseAdjustment::FromGamedata(-1));
	std::byte storage[24]{};
	assert(BaseAdjustment::FromGamedata(4)->CompleteObject(storage + 8) == storage + 4);
	assert(BaseAdjustment::FromGamedata(8)->CompleteObject(storage + 8) == storage);
	assert(BaseAdjustment::FromGamedata(0)->CompleteObject(nullptr) == nullptr);

	// The fixture puts a high, unreplicated bit in an unaligned 32-bit field.
	unsigned char flagsBytes[] = {0xa5, 0x02, 0x00, 0x00, 0x40, 0x5a};
	const unsigned char expectedFlags[] = {0xa5, 0x06, 0x00, 0x00, 0x40, 0x5a};
	AddPlayerFlags(flagsBytes, *FieldOffset::FromGamedata(1), 0x04);
	assert(std::memcmp(flagsBytes, expectedFlags, sizeof(flagsBytes)) == 0);

	std::optional<VtableSlot> primary, secondary;
	const LayoutEntry<VtableSlot> entries[] = {{"primary", primary}, {"secondary", secondary}};
	const char* failed = nullptr;
	assert(!ResolveValues(entries, [](const char* key, int& value) {
		value = 1;
		return std::strcmp(key, "primary") == 0;
	}, failed));
	assert(std::strcmp(failed, "secondary") == 0 && !primary && !secondary);

	assert(ResolveValues(entries, [](const char* key, int& value) {
		value = std::strcmp(key, "primary") == 0 ? 1 : 0;
		return true;
	}, failed));
	assert(primary->Index() == 1 && secondary->Index() == 0);
	std::puts("engine_values: offsets, slot resolution and flags validated");
}
