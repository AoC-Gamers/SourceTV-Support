// SPDX-License-Identifier: GPL-3.0-only
// Extracted from SourceTV-Support by Evgeniy Kazakov and contributors.
#include "extension.h"
#include "wrappers.h"

#include <am-string.h>

bool SMExtension::SetupFromGameConfig(IGameConfig* gc, char* error, int maxlength)
{
	using namespace sourcetv::abi;
	// Stage the layout bindings. Publish them only after the signatures and
	// addresses below have also resolved.
	std::optional<FieldOffset> crc, frames, recorder, pool, steamId;
	std::optional<BaseAdjustment> base;
	std::optional<VtableSlot> challengeNr, challengeType, replyChallenge, fillInfo, connect;
#if !defined _WIN32
	std::optional<VtableSlot> hltvFillInfo;
#endif

	const LayoutEntry<FieldOffset> fields[] = {
		{ "CBaseServer::stringTableCRC", crc },
		{ "CHLTVServer::CClientFrameManager", frames },
		{ "CHLTVServer::m_DemoRecorder", recorder },
		{ "CFrameSnapshotManager::m_PackedEntitiesPool", pool },
		{ "CBaseClient::m_SteamID", steamId },
	};
	const LayoutEntry<BaseAdjustment> bases[] = { { "CHLTVServer::CBaseServer", base } };
	const LayoutEntry<VtableSlot> slots[] = {
		{ "CBaseServer::GetChallengeNr", challengeNr },
		{ "CBaseServer::GetChallengeType", challengeType },
		{ "CBaseServer::ReplyChallenge", replyChallenge },
		{ "CBaseServer::FillServerInfo", fillInfo },
		{ "CBaseServer::ConnectClient", connect },
#if !defined _WIN32
		{ "CHLTVServer::FillServerInfo", hltvFillInfo },
#endif
	};
	const auto lookup = [&](const char* key, int& value) { return gc->GetOffset(key, &value); };
	const char* failedKey = nullptr;
	if (!ResolveValues(fields, lookup, failedKey) ||
		!ResolveValues(bases, lookup, failedKey) ||
		!ResolveValues(slots, lookup, failedKey)) {
		ke::SafeSprintf(error, maxlength, "Missing or invalid layout binding for \"%s\" in " GAMEDATA_FILE ".txt", failedKey);
		return false;
	}

	static const struct {
		const char* key;
		void*& address;
	} s_sigs[] = {
		{ "DataTable_WriteSendTablesBuffer", pfn_DataTable_WriteSendTablesBuffer },
		{ "CBaseServer::IsExclusiveToLobbyConnections", CBaseServer::pfn_IsExclusiveToLobbyConnections },
		{ "CSteam3Server::NotifyClientDisconnect", CSteam3Server::pfn_NotifyClientDisconnect },
		{ "CHLTVServer::AddNewFrame", CHLTVServer::pfn_AddNewFrame },
		{ "CFrameSnapshotManager::LevelChanged", CFrameSnapshotManager::pfn_LevelChanged },
#if SOURCE_ENGINE == SE_LEFT4DEAD2
		{ "CBaseClient::SendFullConnectEvent", CBaseClient::pfn_SendFullConnectEvent },
#endif
		{ "CBaseAbility::ShouldTransmit", CBaseAbility::pfn_ShouldTransmit },
#ifdef TV_RELAYTEST
		{ "OpenSocketInternal", pfn_OpenSocketInternal },
#endif
	};

	for (auto&& el : s_sigs) {
		if (!gc->GetMemSig(el.key, &el.address)) {
			ke::SafeSprintf(error, maxlength, "Unable to find signature for \"%s\" from game config (file: \"" GAMEDATA_FILE ".txt\")", el.key);

			return false;
		}

		if (el.address == NULL) {
			ke::SafeSprintf(error, maxlength, "Sigscan for \"%s\" failed (game config file: \"" GAMEDATA_FILE ".txt\")", el.key);

			return false;
		}
	}

#if defined _WIN32
	ptrdiff_t relative = 0;
#endif

	const struct {
		const char* key;
		void*& address;
	} s_addresses[] = {
		{ "ForEachTerrorPlayer<HitAnnouncement>", HitAnnouncement::pfn_ForEachTerrorPlayer },
#if defined _WIN32
		{ "CTerrorPlayer::OnPouncedOnSurvivor::`relofs to ForEachTerrorPlayer<HitAnnouncement>", reinterpret_cast<void*&>(relative) },
#endif
	};

	for (auto&& el : s_addresses) {
		if (!gc->GetAddress(el.key, &el.address)) {
			ke::SafeSprintf(error, maxlength, "Failed to get address of function \"%s\" from game config (file: \"" GAMEDATA_FILE ".txt\")", el.key);

			return false;
		}

		if (el.address == NULL) {
			ke::SafeSprintf(error, maxlength, "Unable to resolve address \"%s\" (game config file: \"" GAMEDATA_FILE ".txt\")", el.key);

			return false;
		}
	}

#if defined _WIN32
	// NOTE: needs ForEachTerrorPlayer<HitAnnouncement> address set up
	HitAnnouncement::SetupFromRelativeAddress(relative);
#endif

	CBaseServer::offset_stringTableCRC = crc;
	CHLTVServer::offset_CClientFrameManager = frames;
	CHLTVServer::offset_m_DemoRecorder = recorder;
	CHLTVServer::offset_CBaseServer = base;
	CFrameSnapshotManager::offset_m_PackedEntitiesPool = pool;
	CBaseClient::offset_m_SteamID = steamId;
	CBaseServer::vtblindex_GetChallengeNr = challengeNr;
	CBaseServer::vtblindex_GetChallengeType = challengeType;
	CBaseServer::vtblindex_ReplyChallenge = replyChallenge;
	CBaseServer::vtblindex_FillServerInfo = fillInfo;
	CBaseServer::vtblindex_ConnectClient = connect;
#if !defined _WIN32
	CHLTVServer::vtblindex_FillServerInfo = hltvFillInfo;
#endif
	return true;
}

bool SMExtension::SetupFromSteamAPILibrary(char* error, int maxlength)
{
#if SOURCE_ENGINE == SE_LEFT4DEAD2
	char path[256];
	ke::path::Format(path, sizeof(path), "bin/" LIBSTEAMAPI_FILE);

	char libError[512];
	ke::RefPtr<ke::SharedLib> steam_api = ke::SharedLib::Open(path, libError, sizeof(libError));
	if (!steam_api) {
		ke::SafeSprintf(error, maxlength, "Unable to load library \"%s\" (reason: \"%s\")", path, libError);

		return false;
	}

	static const struct {
		const char* symbol;
		void*& address;
	} s_symbols[] = {
		{ "SteamInternal_CreateInterface", pfn_SteamInternal_CreateInterface },
		{ "SteamInternal_GameServer_Init", pfn_SteamInternal_GameServer_Init },
		{ "SteamGameServer_GetHSteamPipe", pfn_SteamGameServer_GetHSteamPipe },
		{ "SteamGameServer_GetHSteamUser", pfn_SteamGameServer_GetHSteamUser },
	};

	void* addresses[sizeof(s_symbols) / sizeof(s_symbols[0])]{};
	for (size_t i = 0; i < sizeof(addresses) / sizeof(addresses[0]); ++i) {
		addresses[i] = steam_api->lookup(s_symbols[i].symbol);
		if (!addresses[i]) {
			ke::SafeSprintf(error, maxlength, "Unable to find symbol \"%s\" (file: \"%s\")", s_symbols[i].symbol, path);
			return false;
		}
	}

	m_SteamModule = std::move(steam_api);
	for (size_t i = 0; i < sizeof(addresses) / sizeof(addresses[0]); ++i)
		s_symbols[i].address = addresses[i];
#endif

	return true;
}

void SMExtension::ClearSteamBindings()
{
	pfn_SteamInternal_CreateInterface = nullptr;
	pfn_SteamInternal_GameServer_Init = nullptr;
	pfn_SteamGameServer_GetHSteamPipe = nullptr;
	pfn_SteamGameServer_GetHSteamUser = nullptr;
	m_SteamModule = nullptr;
}
