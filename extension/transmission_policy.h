// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <cstdint>
#include <limits>
#include <optional>

enum class AbilityAudience
{
	Player,
	Spectator,
	SourceTV,
};

inline int AbilityTransmission(int original, int always, AbilityAudience audience)
{
	return audience == AbilityAudience::Player ? original : always;
}

template <typename Visit>
void VisitPlayerEdicts(const unsigned short* indices, int count, int maxClients, Visit visit)
{
	if (!indices)
		return;

	for (int i = 0; i < count; ++i) {
		const int index = indices[i];
		if (index > 0 && index <= maxClients)
			visit(index);
	}
}

struct DamageHudMessage
{
	uint8_t event;
	int16_t attacker;
	int16_t victim;
	int16_t inflictor;
	int16_t damage;

	static std::optional<DamageHudMessage> Create(int event, int attacker, int victim, int inflictor, int damage)
	{
		const auto fitsShort = [](int value) {
			return value >= (std::numeric_limits<int16_t>::min)() && value <= (std::numeric_limits<int16_t>::max)();
		};
		if (event < 0 || event > (std::numeric_limits<uint8_t>::max)() ||
			!fitsShort(attacker) || !fitsShort(victim) || !fitsShort(inflictor) || !fitsShort(damage))
			return std::nullopt;

		return DamageHudMessage{static_cast<uint8_t>(event), static_cast<int16_t>(attacker),
			static_cast<int16_t>(victim), static_cast<int16_t>(inflictor), static_cast<int16_t>(damage)};
	}

	template <typename Start, typename End>
	void Send(Start start, End end) const
	{
		auto* writer = start();
		if (!writer)
			return;

		writer->WriteByte(event);
		writer->WriteShort(attacker);
		writer->WriteShort(victim);
		writer->WriteShort(inflictor);
		writer->WriteShort(damage);
		end();
	}
};
