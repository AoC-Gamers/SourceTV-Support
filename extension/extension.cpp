#include "extension.h"
#include "wrappers.h"
#include "frame_retention.h"
#include "transmission_policy.h"
#include "steam_runtime.h"

#include "sdk/public/engine/inetsupport.h"
#include "sdk/engine/networkstringtable.h"

#include <am-string.h>
#include <checksum_crc.h>
#include <extensions/IBinTools.h>
#include <extensions/ISDKTools.h>

// Interfaces
INetworkStringTableContainer* networkStringTableContainerServer = NULL;
IHLTVDirector* hltvdirector = NULL;
IHLTVServer* g_pHLTVServer = NULL;
INetSupport* g_pNetSupport = NULL;
IPlayerInfoManager* playerinfomanager = NULL;
IServerGameEnts* gameents = NULL;
IBinTools* bintools = NULL;
ISDKTools* sdktools = NULL;

CGlobalVars* gpGlobals = NULL;

IServer* g_pGameIServer = NULL;

std::optional<sourcetv::abi::FieldOffset> CBasePlayer::sendprop_m_fFlags;
std::optional<sourcetv::abi::FieldOffset> CBaseServer::offset_stringTableCRC;
std::optional<sourcetv::abi::VtableSlot> CBaseServer::vtblindex_GetChallengeNr;
std::optional<sourcetv::abi::VtableSlot> CBaseServer::vtblindex_GetChallengeType;
std::optional<sourcetv::abi::VtableSlot> CBaseServer::vtblindex_ReplyChallenge;
std::optional<sourcetv::abi::VtableSlot> CBaseServer::vtblindex_FillServerInfo;
std::optional<sourcetv::abi::VtableSlot> CBaseServer::vtblindex_ConnectClient;
void* CBaseServer::pfn_IsExclusiveToLobbyConnections = NULL;
CDetour* CBaseServer::detour_IsExclusiveToLobbyConnections = NULL;
ICallWrapper* CBaseServer::vcall_GetChallengeNr = NULL;
ICallWrapper* CBaseServer::vcall_GetChallengeType = NULL;
std::optional<sourcetv::abi::FieldOffset> CHLTVServer::offset_m_DemoRecorder;
std::optional<sourcetv::abi::FieldOffset> CHLTVServer::offset_CClientFrameManager;
std::optional<sourcetv::abi::BaseAdjustment> CHLTVServer::offset_CBaseServer;
std::optional<sourcetv::abi::VtableSlot> CHLTVServer::vtblindex_FillServerInfo;
int CHLTVServer::shookid_ReplyChallenge = 0;
int CHLTVServer::shookid_FillServerInfo = 0;
int CHLTVServer::shookid_hltv_FillServerInfo = 0;
int CHLTVServer::shookid_ConnectClient = 0;
void* CHLTVServer::pfn_AddNewFrame = NULL;
CDetour* CHLTVServer::detour_AddNewFrame = NULL;
std::optional<sourcetv::abi::FieldOffset> CBaseClient::offset_m_SteamID;
void* CBaseClient::pfn_SendFullConnectEvent = NULL;
CDetour* CBaseClient::detour_SendFullConnectEvent = NULL;
void* CSteam3Server::pfn_NotifyClientDisconnect = NULL;
CDetour* CSteam3Server::detour_NotifyClientDisconnect = NULL;
std::optional<sourcetv::abi::FieldOffset> CFrameSnapshotManager::offset_m_PackedEntitiesPool;
void* CFrameSnapshotManager::pfn_LevelChanged = NULL;
CDetour* CFrameSnapshotManager::detour_LevelChanged = NULL;
void* CBaseAbility::pfn_ShouldTransmit = NULL;
CDetour* CBaseAbility::detour_ShouldTransmit = NULL;
void* HitAnnouncement::pfn_ForEachTerrorPlayer = NULL;
CDetour* HitAnnouncement::detour_ForEachTerrorPlayer = NULL;

int HitAnnouncement::pzMsgId = 0;

int shookid_CHLTVDemoRecorder_RecordStringTables = 0;
int shookid_CHLTVDemoRecorder_RecordServerClasses = 0;
int shookid_CServerGameEnts_CheckTransmit = 0;

void* pfn_DataTable_WriteSendTablesBuffer = NULL;
void* pfn_SteamGameServer_GetHSteamPipe = NULL;
void* pfn_SteamGameServer_GetHSteamUser = NULL;
void* pfn_SteamInternal_CreateInterface = NULL;
void* pfn_SteamInternal_GameServer_Init = NULL;
void* pfn_OpenSocketInternal = NULL;

CDetour* detour_SteamInternal_GameServer_Init = NULL;

// SourceHook
SH_DECL_HOOK1_void(IHLTVDirector, SetHLTVServer, SH_NOATTRIB, 0, IHLTVServer*);
SH_DECL_HOOK0_void(CHLTVDemoRecorder, RecordStringTables, SH_NOATTRIB, 0);
SH_DECL_HOOK1_void(CHLTVDemoRecorder, RecordServerClasses, SH_NOATTRIB, 0, ServerClass*);
SH_DECL_MANUALHOOK2_void(CBaseServer_ReplyChallenge, 0, 0, 0, netadr_s&, bf_read&);
SH_DECL_MANUALHOOK1_void(CBaseServer_FillServerInfo, 0, 0, 0, SVC_ServerInfo&);
SH_DECL_MANUALHOOK1_void(CHLTVServer_FillServerInfo, 0, 0, 0, SVC_ServerInfo&);
#if SOURCE_ENGINE == SE_LEFT4DEAD2
SH_DECL_HOOK0_void(ISteamGameServer, LogOff, SH_NOATTRIB, 0);
#endif
SH_DECL_HOOK3_void(IServerGameEnts, CheckTransmit, SH_NOATTRIB, 0, CCheckTransmitInfo*, const unsigned short*, int);
SH_DECL_MANUALHOOK10(CHLTVServer_ConnectClient, 0, 0, 0, IClient*, netadr_t&, int, int, int, const char*,
	const char*, const char*, int, CUtlVector<NetMessageCvar_t>&, bool);

// Detours
#include <CDetour/detours.h>

SMExtension g_Extension;
SMEXT_LINK(&g_Extension);

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
	int iRetVal = DETOUR_MEMBER_CALL(Handler_CBaseAbility__ShouldTransmit)(pInfo);

	if (iRetVal == FL_EDICT_ALWAYS || !pInfo) {
		return iRetVal;
	}

	IGamePlayer* pPlayer = playerhelpers->GetGamePlayer(pInfo->m_pClientEnt);
	if (pPlayer == NULL) {
		return iRetVal;
	}

	IPlayerInfo* pPInfo = pPlayer->GetPlayerInfo();
	if (pPInfo == NULL) {
		return iRetVal;
	}

	if (pPlayer->IsSourceTV())
		return AbilityTransmission(iRetVal, FL_EDICT_ALWAYS, AbilityAudience::SourceTV);

	const auto audience = pPInfo->GetTeamIndex() == TEAM_SPECTATOR
		? AbilityAudience::Spectator : AbilityAudience::Player;
	return AbilityTransmission(iRetVal, FL_EDICT_ALWAYS, audience);
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

void DestroyCall(ICallWrapper* call)
{
	call->Destroy();
}

void RemoveHook(int id)
{
	SH_REMOVE_HOOK_ID(id);
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
	for (const auto& binding : detours) {
		auto* detour = CDetourManager::CreateDetour(binding.callback, binding.trampoline, binding.target);
		if (!m_Resources.Take<DestroyDetour>(binding.detour, detour)) {
			ke::SafeSprintf(error, maxlength, "Unable to create detour for %s", binding.name);
			return false;
		}
	}
	return true;
}

bool SMExtension::EnableDetours(char* error, size_t maxlength)
{
	for (const auto& binding : detours) {
		binding.detour->EnableDetour();
		if (!binding.detour->IsEnabled()) {
			ke::SafeSprintf(error, maxlength, "Unable to enable detour for %s", binding.name);
			return false;
		}
	}
	return true;
}

bool SMExtension::CreateVCalls(char* error, size_t maxlength)
{
	SourceMod::PassInfo params[] = {
#if SMINTERFACE_BINTOOLS_VERSION == 4
		{ PassType_Basic, PASSFLAG_BYVAL, sizeof(int), NULL, 0 },
		{ PassType_Basic, PASSFLAG_BYVAL, sizeof(netadr_t*), NULL, 0 },
#else
		// sm1.9- support
		{ PassType_Basic, PASSFLAG_BYVAL, sizeof(int) },
		{ PassType_Basic, PASSFLAG_BYVAL, sizeof(netadr_t*) },
#endif
	};

	const struct {
		const char* name;
		int index;
		ICallWrapper*& call;
	} calls[] = {
		{ "CBaseServer::GetChallengeNr", CBaseServer::vtblindex_GetChallengeNr->Index(), CBaseServer::vcall_GetChallengeNr },
		{ "CBaseServer::GetChallengeType", CBaseServer::vtblindex_GetChallengeType->Index(), CBaseServer::vcall_GetChallengeType },
	};
	for (const auto& entry : calls) {
		auto* call = bintools->CreateVCall(entry.index, 0, 0, &params[0], &params[1], 1);
		if (!m_Resources.Take<DestroyCall>(entry.call, call)) {
			ke::SafeSprintf(error, maxlength, "Unable to create virtual call for %s", entry.name);
			return false;
		}
	}
	return true;
}

bool SMExtension::Load(char* error, size_t maxlength)
{
	g_pGameIServer = sdktools->GetIServer();
	if (!g_pGameIServer) {
		ke::SafeStrcpy(error, maxlength, "Unable to retrieve the game IServer from SDKTools");
		return false;
	}

	if (!CreateVCalls(error, maxlength))
		return false;

	const int directorHook = SH_ADD_HOOK(IHLTVDirector, SetHLTVServer, hltvdirector,
		SH_MEMBER(this, &SMExtension::Handler_CHLTVDirector_SetHLTVServer), true);
	if (!m_Resources.Take<RemoveHook>(m_DirectorHook, directorHook)) {
		ke::SafeStrcpy(error, maxlength, "Unable to hook IHLTVDirector::SetHLTVServer");
		return false;
	}

	if (!BindHLTVServer(hltvdirector->GetHLTVServer(), error, maxlength) ||
		!BindSteamLogOff(error, maxlength) || !EnableDetours(error, maxlength))
		return false;

	m_Phase = Phase::Active;
	if (g_pHLTVServer)
		RepairHLTVServer(g_pHLTVServer);

#ifdef TV_RELAYTEST
	static ConVarRef clientport("clientport");
	InvokeOpenSocketInternal(NS_CLIENT, clientport.GetInt(), PORT_SERVER, "client", true);
#endif
	if (m_Phase != Phase::Active)
		return false;

	sharesys->RegisterLibrary(myself, "sourcetvsupport");
	return true;
}

void SMExtension::Unload()
{
	m_Phase = Phase::Stopped;
	g_pHLTVServer = nullptr;
	for (size_t i = sizeof(detours) / sizeof(detours[0]); i > 0; --i) {
		auto* detour = detours[i - 1].detour;
		if (detour && detour->IsEnabled())
			detour->DisableDetour();
	}
	m_SteamHooks.Reset();
	m_HLTVHooks.Reset();
	m_Resources.Reset();
	for (const auto& binding : detours)
		*binding.trampoline = nullptr;

	g_pGameIServer = nullptr;
	ClearSteamBindings();
}

bool SMExtension::BindSteamLogOff(char* error, size_t maxlength)
{
	m_SteamHooks.Reset();

#if SOURCE_ENGINE == SE_LEFT4DEAD2
	const HSteamPipe pipe = InvokeGetHSteamPipe();
	if (!pipe)
		return true; // Steam has not started yet. Its init detour retries.

	auto* client = static_cast<ISteamClient*>(InvokeCreateInterface(STEAMCLIENT_INTERFACE_VERSION));
	if (!client)
		return true;

	auto* server = client->GetISteamGameServer(InvokeGetHSteamUser(), pipe, STEAMGAMESERVER_INTERFACE_VERSION);
	if (!server)
		return true;

	const int hook = SH_ADD_HOOK(ISteamGameServer, LogOff, server,
		SH_MEMBER(this, &SMExtension::Handler_ISteamGameServer_LogOff), false);
	if (!m_SteamHooks.Take<RemoveHook>(m_LogOffHook, hook)) {
		ke::SafeStrcpy(error, maxlength, "Unable to hook ISteamGameServer::LogOff");
		return false;
	}
#endif
	return true;
}

void SMExtension::OnGameServerInitResult(bool initialized)
{
	if (m_Phase != Phase::Active)
		return;
	if (!initialized) {
		m_SteamHooks.Reset();
		return;
	}
	if (!BindSteamLogOff(m_LifecycleError, sizeof(m_LifecycleError)))
		FailRuntime();
}

bool SMExtension::BindHLTVServer(IHLTVServer* server, char* error, size_t maxlength)
{
	g_pHLTVServer = nullptr;
	m_HLTVHooks.Reset();
	if (!server)
		return true;

	CBaseServer* base = CBaseServer::FromIHLTVServer(server);
	if (!base) {
		ke::SafeStrcpy(error, maxlength, "Unable to obtain the HLTV base server");
		return false;
	}
	CHLTVServer* hltv = CHLTVServer::FromBaseServer(base);
	if (!hltv) {
		ke::SafeStrcpy(error, maxlength, "Unresolved CHLTVServer base adjustment");
		return false;
	}
	CHLTVDemoRecorder& recorder = hltv->m_DemoRecorder();
	const auto track = [&](int& slot, int id, const char* name) {
		if (m_HLTVHooks.Take<RemoveHook>(slot, id))
			return true;
		ke::SafeSprintf(error, maxlength, "Unable to hook %s", name);
		m_HLTVHooks.Reset();
		return false;
	};

	if (!track(CHLTVServer::shookid_ReplyChallenge, SH_ADD_MANUALHOOK(CBaseServer_ReplyChallenge,
			base, SH_MEMBER(this, &SMExtension::Handler_CHLTVServer_ReplyChallenge), false), "HLTV ReplyChallenge") ||
#if SOURCE_ENGINE == SE_LEFT4DEAD2
		!track(CHLTVServer::shookid_FillServerInfo, SH_ADD_MANUALHOOK(CBaseServer_FillServerInfo,
			base, SH_MEMBER(this, &SMExtension::Handler_CHLTVServer_FillServerInfo), true), "HLTV CBaseServer::FillServerInfo") ||
#if !defined _WIN32
		!track(CHLTVServer::shookid_hltv_FillServerInfo, SH_ADD_MANUALHOOK(CHLTVServer_FillServerInfo,
			hltv, SH_MEMBER(this, &SMExtension::Handler_CHLTVServer_FillServerInfo), true), "CHLTVServer::FillServerInfo") ||
#endif
#endif
		!track(CHLTVServer::shookid_ConnectClient, SH_ADD_MANUALHOOK(CHLTVServer_ConnectClient,
			base, SH_MEMBER(this, &SMExtension::Handler_CHLTVServer_ConnectClient), false), "HLTV ConnectClient") ||
		!track(shookid_CHLTVDemoRecorder_RecordStringTables, SH_ADD_HOOK(CHLTVDemoRecorder, RecordStringTables,
			&recorder, SH_MEMBER(this, &SMExtension::Handler_CHLTVDemoRecorder_RecordStringTables), false), "HLTV RecordStringTables") ||
		!track(shookid_CHLTVDemoRecorder_RecordServerClasses, SH_ADD_HOOK(CHLTVDemoRecorder, RecordServerClasses,
			&recorder, SH_MEMBER(this, &SMExtension::Handler_CHLTVDemoRecorder_RecordServerClasses), false), "HLTV RecordServerClasses") ||
		!track(shookid_CServerGameEnts_CheckTransmit, SH_ADD_HOOK(IServerGameEnts, CheckTransmit,
			gameents, SH_MEMBER(this, &SMExtension::Handler_CServerGameEnts_CheckTransmit), true), "CheckTransmit"))
		return false;

	g_pHLTVServer = server;
	return true;
}

void SMExtension::RepairHLTVServer(IHLTVServer* pIHLTVServer)
{
	CBaseServer* pServer = CBaseServer::FromIHLTVServer(pIHLTVServer);
	if (auto* tables = pServer->m_StringTables()) {
		auto* rules = static_cast<CNetworkStringTable*>(tables->FindTable("GameRulesCreation"));
		if (rules)
			rules->RestoreTick(TIME_TO_TICKS(1));
	}

	// bug##: in CHLTVServer::StartMaster, bot is executing "spectate" command which does nothing and it keeps him in unassigned team (index 0)
	// bot's going to fall under CDirectorSessionManager::UpdateNewPlayers's conditions to be auto-assigned to some playable team
	// enforce team change here to spectator (index 1)
	CBasePlayer* pPlayer = UTIL_PlayerByIndex(pIHLTVServer->GetHLTVSlot() + 1);
	if (pPlayer != NULL && pPlayer->IsHLTV()) {
		pPlayer->AddFlag(FL_FAKECLIENT);
		pPlayer->ChangeTeam(TEAM_SPECTATOR);
	}

	// bug#1: stringTableCRC are not set in CHLTVServer::StartMaster
	// client doesn't allow stringTableCRC to be empty
	// CHLTVServer instance must copy property stringTableCRC from CGameServer instance
	if (m_Phase == Phase::Active && g_pHLTVServer == pIHLTVServer)
		pServer->stringTableCRC() = CBaseServer::FromIServer(g_pGameIServer)->stringTableCRC();
}

void SMExtension::Handler_CHLTVDirector_SetHLTVServer(IHLTVServer* pIHLTVServer)
{
	if (m_Phase != Phase::Active)
		RETURN_META(MRES_IGNORED);

	if (!BindHLTVServer(pIHLTVServer, m_LifecycleError, sizeof(m_LifecycleError)))
		FailRuntime();
	else if (pIHLTVServer)
		RepairHLTVServer(pIHLTVServer);

	RETURN_META(MRES_IGNORED);
}

void SMExtension::Handler_CHLTVServer_ReplyChallenge(netadr_s& adr, bf_read& inmsg)
{
	// rww: check if hooks right instance
	CBaseServer* _this = META_IFACEPTR(CBaseServer);

	char buffer[512];
	bf_write msg(buffer, sizeof(buffer));

	char context[256] = { 0 };
	inmsg.ReadString(context, sizeof(context));

	msg.WriteLong(CONNECTIONLESS_HEADER);
	msg.WriteByte(S2C_CHALLENGE);

	int challengeNr = _this->GetChallengeNr(adr);
	int authprotocol = _this->GetChallengeType(adr);

	msg.WriteLong(challengeNr);
	msg.WriteLong(authprotocol);

	msg.WriteShort(1);
	msg.WriteLongLong(0LL);
	msg.WriteByte(0);

	msg.WriteString(context);

	// bug#6: CBaseServer::ReplyChallenge called on CHLTVServer instance, on reserved server will force join client to a steam lobby
	// joining steam lobby will force a client to connect to game server, instead of HLTV one
	// replying with empty lobby id and no means for lobby requirement
	msg.WriteLong(g_pNetSupport->GetEngineBuildNumber());
	msg.WriteString("");
	msg.WriteByte(0);

	msg.WriteLongLong(0LL);

	g_pNetSupport->SendPacket(NULL, NS_HLTV, adr, msg.GetData(), msg.GetNumBytesWritten());

	RETURN_META(MRES_SUPERCEDE);
}

// bug##: "Connection to Steam servers lost." upon shutting hltv down
// CHLTVServer::Shutdown is invoking CBaseServer::Shutdown which calling SteamGameServer()->LogOff()
// without any condition on whether it was just hltv shut down
// as long as sv instance is still active - prevent ISteamGameServer::LogOff from being invoked
void SMExtension::Handler_ISteamGameServer_LogOff()
{
	if (g_pGameIServer != NULL && g_pGameIServer->IsActive()) {
		// Game server still active - intercept ISteamGameServer::LogOff
		RETURN_META(MRES_SUPERCEDE);
	}

	RETURN_META(MRES_IGNORED);
}

IClient* SMExtension::Handler_CHLTVServer_ConnectClient(netadr_t& adr, int protocol, int challenge, int authProtocol, const char* name,
	const char* password, const char* hashedCDkey, int cdKeyLen, CUtlVector<NetMessageCvar_t>& splitScreenClients, bool isClientLowViolence)
{
	if (splitScreenClients.Count() > 1)
	{
		char buffer[512];
		bf_write msg(buffer, sizeof(buffer));

		msg.WriteLong(CONNECTIONLESS_HEADER);
		msg.WriteByte(S2C_CONNREJECT);
		msg.WriteString("Splitscreen is not allowed in HLTV\n");

		g_pNetSupport->SendPacket(NULL, NS_HLTV, adr, msg.GetData(), msg.GetNumBytesWritten());

		RETURN_META_VALUE(MRES_SUPERCEDE, nullptr);
	}

	RETURN_META_VALUE(MRES_IGNORED, nullptr);
}

bool SMExtension::SDK_OnLoad(char* error, size_t maxlength, bool late)
{
	HitAnnouncement::pzMsgId = usermsgs->GetMessageIndex("PZDmgMsg");
	if (HitAnnouncement::pzMsgId == -1) {
		ke::SafeStrcpy(error, maxlength, "Unable to find usermessage \"PZDmgMsg\"!");

		return false;
	}

	sm_sendprop_info_t info;
	if (!gamehelpers->FindSendPropInfo("CBasePlayer", "m_fFlags", &info)) {
		ke::SafeStrcpy(error, maxlength, "Unable to find SendProp \"CBasePlayer::m_fFlags\"");

		return false;
	}

	CBasePlayer::sendprop_m_fFlags = sourcetv::abi::FieldOffset::FromGamedata(info.actual_offset);
	if (!CBasePlayer::sendprop_m_fFlags) {
		ke::SafeStrcpy(error, maxlength, "Invalid actual offset for CBasePlayer::m_fFlags");
		return false;
	}

	IGameConfig* gc = NULL;
	if (!gameconfs->LoadGameConfigFile(GAMEDATA_FILE, &gc, error, maxlength)) {
		ke::SafeStrcpy(error, maxlength, "Unable to load a gamedata file \"" GAMEDATA_FILE ".txt\"");

		return false;
	}

	const bool resolved = SetupFromGameConfig(gc, error, maxlength);
	gameconfs->CloseGameConfigFile(gc);
	if (!resolved)
		return false;

	SH_MANUALHOOK_RECONFIGURE(CBaseServer_ReplyChallenge, CBaseServer::vtblindex_ReplyChallenge->Index(), 0, 0);
	SH_MANUALHOOK_RECONFIGURE(CBaseServer_FillServerInfo, CBaseServer::vtblindex_FillServerInfo->Index(), 0, 0);
#if !defined _WIN32
	SH_MANUALHOOK_RECONFIGURE(CHLTVServer_FillServerInfo, CHLTVServer::vtblindex_FillServerInfo->Index(), 0, 0);
#endif
	SH_MANUALHOOK_RECONFIGURE(CHLTVServer_ConnectClient, CBaseServer::vtblindex_ConnectClient->Index(), 0, 0);

	// Retrieve addresses from steam_api shared library
	if (!SetupFromSteamAPILibrary(error, maxlength)) {
		return false;
	}

	if (!CreateDetours(error, maxlength)) {
		Unload();
		return false;
	}

	m_Phase = Phase::Prepared;
	m_LifecycleError[0] = '\0';

	sharesys->AddDependency(myself, "bintools.ext", true, true);
	sharesys->AddDependency(myself, "sdktools.ext", true, true);

	return true;
}
