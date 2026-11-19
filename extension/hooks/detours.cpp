// SPDX-License-Identifier: GPL-3.0-only
// Extracted from SourceTV-Support by Evgeniy Kazakov and contributors.
#include "extension.h"
#include "engine/wrappers.h"
#include "hooks/frame_retention.h"
#include "hooks/transmission_policy.h"
#include "core/steam_runtime.h"

#include <am-string.h>
#include <CDetour/detours.h>
#include <iterator>

extern SMExtension g_Extension;

void TrySendPZMsgToSourceTV(const HitAnnouncement& rMsg);

// A1m`: Visual bug. usermessage "PZDmgMsg" is not sent to the SourceTV client
DETOUR_DECL_STATIC1(Handler_ForEachTerrorPlayer__HitAnnouncement, bool, HitAnnouncement&, rMsg)
{
	bool bRetVal = DETOUR_STATIC_CALL(Handler_ForEachTerrorPlayer__HitAnnouncement)(rMsg);

	TrySendPZMsgToSourceTV(rMsg);

	return bRetVal;
}

// A1m`: Visual bug. infected players abilities are not sent, so the cooldown of abilities in versus-like modes is not visible in the HUD below.
// This is also relevant for spectators.
DETOUR_DECL_MEMBER1(Handler_CBaseAbility__ShouldTransmit, int, const CCheckTransmitInfo*, pInfo)
{
	const int original = DETOUR_MEMBER_CALL(Handler_CBaseAbility__ShouldTransmit)(pInfo);
	return AbilityTransmissionForViewer(original, FL_EDICT_ALWAYS, TEAM_SPECTATOR, pInfo,
		[](edict_t* viewer) { return playerhelpers->GetGamePlayer(viewer); });
}

// bug#X: hltv clients are sending "player_full_connect" event
// user ids of hltv clients can collide with user ids of sv
// event "player_full_connect" fires with userid of hltv client
DETOUR_DECL_MEMBER0(Handler_CBaseClient_SendFullConnectEvent, void)
{
	CBaseClient* _this = reinterpret_cast<CBaseClient*>(this);

	IServer* pServer = _this->GetServer();
	if (pServer != NULL && pServer->IsHLTV()) {
		// CBaseClient::SendFullConnectEvent for HLTV client - intercept
		return;
	}

	DETOUR_MEMBER_CALL(Handler_CBaseClient_SendFullConnectEvent)();
}

DETOUR_DECL_MEMBER0(Handler_CBaseServer_IsExclusiveToLobbyConnections, bool)
{
	CBaseServer* _this = reinterpret_cast<CBaseServer*>(this);
	if (_this->IsHLTV()) {
		return false;
	}

	return DETOUR_MEMBER_CALL(Handler_CBaseServer_IsExclusiveToLobbyConnections)();
}

DETOUR_DECL_MEMBER1(Handler_CHLTVServer_AddNewFrame, CClientFrame*, CClientFrame*, clientFrame)
{
	CHLTVServer* _this = reinterpret_cast<CHLTVServer*>(this);

	// bug##: hibernation causes to leak memory when adding new frames to hltv
	// forcefully remove oldest frames
	CClientFrame* pFrame = DETOUR_MEMBER_CALL(Handler_CHLTVServer_AddNewFrame)(clientFrame);

	// Only keep the number of packets required to satisfy tv_delay at our tv snapshot rate
	static ConVarRef tv_delay("tv_delay"), tv_snapshotrate("tv_snapshotrate");

	const int numFramesToKeep = FramesToKeep(tv_delay.GetFloat(), tv_snapshotrate.GetInt(), MAX_CLIENT_FRAMES);

	PruneFrames(_this->GetClientFrameManager(), numFramesToKeep);

	return pFrame;
}

// bug#8: ticket auth (authprotocol = 2) with hltv clients crashes server in steamclient.so on disconnect
// malformed steamid of unauthentificated hltv client passed to CSteamGameServer012::EndAuthSession
DETOUR_DECL_MEMBER1(Handler_CSteam3Server_NotifyClientDisconnect, void, CBaseClient*, client)
{
	if (!client->IsConnected() || client->IsFakeClient()) {
		return;
	}

	if (!client->m_SteamID().IsValid()) {
		return;
	}

	// rww: SendUserDisconnect

	DETOUR_MEMBER_CALL(Handler_CSteam3Server_NotifyClientDisconnect)(client);
}

#if SOURCE_ENGINE == SE_LEFT4DEAD2
DETOUR_DECL_STATIC6(Handler_SteamInternal_GameServer_Init, bool, uint32, unIP, uint16, usPort, uint16, usGamePort, uint16, usQueryPort, EServerMode, eServerMode, const char*, pchVersionString)
{
	// bug##: without overriding usQueryPort parm, it uses HLTV port (if having -hltv in launch parameters), which is already bound
	// failing SteamInternal_GameServer_Init also causes game to freeze
	static ConVarRef sv_master_share_game_socket("sv_master_share_game_socket");
	return sourcetv::InitSteamServer(usPort, sv_master_share_game_socket.GetBool(),
		MASTERSERVERUPDATERPORT_USEGAMESOCKETSHARE,
		[&](uint16 queryPort) {
			return DETOUR_STATIC_CALL(Handler_SteamInternal_GameServer_Init)(
				unIP, usPort, usGamePort, queryPort, eServerMode, pchVersionString);
		},
		[](bool initialized) { g_Extension.OnGameServerInitResult(initialized); });
}
#endif

DETOUR_DECL_MEMBER0(Handler_CFrameSnapshotManager_LevelChanged, void)
{
	CFrameSnapshotManager* _this = reinterpret_cast<CFrameSnapshotManager*>(this);

	// bug##: Underlying method CClassMemoryPool::Clear creates CUtlRBTree with insufficient iterator size (unsigned short)
	// memory object of which fails to iterate over more than 65535 of packed entities
	_this->m_PackedEntitiesPool().Clear();

	// CFrameSnapshotManager::m_PackedEntitiesPool shouldn't have elements to free from now on
	DETOUR_MEMBER_CALL(Handler_CFrameSnapshotManager_LevelChanged)();
}
//

namespace {

void DestroyDetour(CDetour* detour)
{
	detour->Destroy();
}

struct DetourBinding
{
	const char* name;
	CDetour*& detour;
	void*& target;
	void* callback;
	void** trampoline;
};

const DetourBinding detours[] = {
	{ "CBaseAbility::ShouldTransmit", CBaseAbility::detour_ShouldTransmit,
		CBaseAbility::pfn_ShouldTransmit, GET_MEMBER_CALLBACK(Handler_CBaseAbility__ShouldTransmit),
		GET_MEMBER_TRAMPOLINE(Handler_CBaseAbility__ShouldTransmit) },
	{ "CBaseServer::IsExclusiveToLobbyConnections", CBaseServer::detour_IsExclusiveToLobbyConnections,
		CBaseServer::pfn_IsExclusiveToLobbyConnections, GET_MEMBER_CALLBACK(Handler_CBaseServer_IsExclusiveToLobbyConnections),
		GET_MEMBER_TRAMPOLINE(Handler_CBaseServer_IsExclusiveToLobbyConnections) },
	{ "CSteam3Server::NotifyClientDisconnect", CSteam3Server::detour_NotifyClientDisconnect,
		CSteam3Server::pfn_NotifyClientDisconnect, GET_MEMBER_CALLBACK(Handler_CSteam3Server_NotifyClientDisconnect),
		GET_MEMBER_TRAMPOLINE(Handler_CSteam3Server_NotifyClientDisconnect) },
	{ "CHLTVServer::AddNewFrame", CHLTVServer::detour_AddNewFrame,
		CHLTVServer::pfn_AddNewFrame, GET_MEMBER_CALLBACK(Handler_CHLTVServer_AddNewFrame),
		GET_MEMBER_TRAMPOLINE(Handler_CHLTVServer_AddNewFrame) },
	{ "CFrameSnapshotManager::LevelChanged", CFrameSnapshotManager::detour_LevelChanged,
		CFrameSnapshotManager::pfn_LevelChanged, GET_MEMBER_CALLBACK(Handler_CFrameSnapshotManager_LevelChanged),
		GET_MEMBER_TRAMPOLINE(Handler_CFrameSnapshotManager_LevelChanged) },
	{ "ForEachTerrorPlayer<HitAnnouncement>", HitAnnouncement::detour_ForEachTerrorPlayer,
		HitAnnouncement::pfn_ForEachTerrorPlayer, GET_STATIC_CALLBACK(Handler_ForEachTerrorPlayer__HitAnnouncement),
		GET_STATIC_TRAMPOLINE(Handler_ForEachTerrorPlayer__HitAnnouncement) },
#if SOURCE_ENGINE == SE_LEFT4DEAD2
	{ "SteamInternal_GameServer_Init", detour_SteamInternal_GameServer_Init,
		pfn_SteamInternal_GameServer_Init, GET_STATIC_CALLBACK(Handler_SteamInternal_GameServer_Init),
		GET_STATIC_TRAMPOLINE(Handler_SteamInternal_GameServer_Init) },
	{ "CBaseClient::SendFullConnectEvent", CBaseClient::detour_SendFullConnectEvent,
		CBaseClient::pfn_SendFullConnectEvent, GET_MEMBER_CALLBACK(Handler_CBaseClient_SendFullConnectEvent),
		GET_MEMBER_TRAMPOLINE(Handler_CBaseClient_SendFullConnectEvent) },
#endif
};

} // namespace

bool SMExtension::CreateDetours(char* error, size_t maxlength)
{
	CDetourManager::Init(smutils->GetScriptingEngine(), nullptr);
	return sourcetv::ApplyRuntimeBindings(detours, [&](const DetourBinding& binding) {
		auto* detour = CDetourManager::CreateDetour(binding.callback, binding.trampoline, binding.target);
		if (!m_Resources.Take<DestroyDetour>(binding.detour, detour)) {
			ke::SafeSprintf(error, maxlength, "Unable to create detour for %s", binding.name);
			return false;
		}
		return true;
	});
}

bool SMExtension::EnableDetours(char* error, size_t maxlength)
{
	return sourcetv::ApplyRuntimeBindings(detours, [&](const DetourBinding& binding) {
		binding.detour->EnableDetour();
		if (!binding.detour->IsEnabled()) {
			ke::SafeSprintf(error, maxlength, "Unable to enable detour for %s", binding.name);
			return false;
		}
		return true;
	});
}

void SMExtension::DisableDetours()
{
	for (auto it = std::rbegin(detours); it != std::rend(detours); ++it) {
		if (it->detour && it->detour->IsEnabled())
			it->detour->DisableDetour();
	}
}

void SMExtension::ClearDetourTrampolines()
{
	for (const auto& binding : detours)
		*binding.trampoline = nullptr;
}
