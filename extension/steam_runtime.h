// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <cstdint>

namespace sourcetv {

template <typename Original, typename Refresh>
bool InitSteamServer(uint16_t port, bool shareGameSocket, uint16_t sharedPort,
	Original original, Refresh refresh)
{
	const uint16_t queryPort = shareGameSocket ? sharedPort : static_cast<uint16_t>(port - 1);
	const bool initialized = original(queryPort);
	refresh(initialized);
	return initialized;
}

} // namespace sourcetv
