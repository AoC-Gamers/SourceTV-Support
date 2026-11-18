// SPDX-License-Identifier: GPL-3.0-only
#include "transmission_policy.h"

#include <cassert>
#include <cstdio>
#include <vector>

struct Writer
{
	std::vector<int> bytes;
	void WriteByte(uint8_t value) { bytes.push_back(value); }
	void WriteShort(int16_t value)
	{
		const auto bits = static_cast<uint16_t>(value);
		bytes.push_back(bits & 255);
		bytes.push_back(bits >> 8);
	}
};

int main()
{
	assert(AbilityTransmission(3, 8, AbilityAudience::Player) == 3);
	assert(AbilityTransmission(3, 8, AbilityAudience::SourceTV) == 8);
	assert(AbilityTransmission(3, 8, AbilityAudience::Spectator) == 8);
	assert(AbilityTransmission(8, 8, AbilityAudience::Player) == 8);

	const unsigned short unsorted[] = {3, 900, 2, 0, 4, 1};
	std::vector<int> visited;
	auto collect = [&](int index) { visited.push_back(index); };
	VisitPlayerEdicts(unsorted, 6, 3, collect);
	assert((visited == std::vector<int>{3, 2, 1}));
	VisitPlayerEdicts(nullptr, 6, 3, collect);
	VisitPlayerEdicts(unsorted, -1, 3, collect);
	assert(visited.size() == 3);

	const auto message = DamageHudMessage::Create(255, 0x1234, 2, 0, -2);
	assert(message);
	assert(!DamageHudMessage::Create(256, 1, 2, 0, 3));
	assert(!DamageHudMessage::Create(1, 32768, 2, 0, 3));
	assert(!DamageHudMessage::Create(1, 1, 2, 0, -32769));

	int ended = 0;
	auto end = [&] { ++ended; };
	message->Send([]() -> Writer* { return nullptr; }, end);
	assert(ended == 0);
	Writer writer;
	message->Send([&] { return &writer; }, end);
	assert(ended == 1);
	assert((writer.bytes == std::vector<int>{255, 0x34, 0x12, 2, 0, 0, 0, 254, 255}));
	std::puts("transmission_policy: passed");
}
