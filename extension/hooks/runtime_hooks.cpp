// SPDX-License-Identifier: GPL-3.0-only
// Extracted from SourceTV-Support by Evgeniy Kazakov and contributors.
#include "extension.h"
#include "hooks/spectator_reply.h"
#include "engine/wrappers.h"
#include "sdk/engine/hltvdemo.h"
#include "sdk/common/netmessages.h"
#include "sdk/public/engine/inetsupport.h"
#include "sdk/engine/networkstringtable.h"

#include <am-string.h>
#include <extensions/IBinTools.h>
#include <extensions/ISDKTools.h>

extern IHLTVDirector* hltvdirector;
extern IHLTVServer* g_pHLTVServer;
extern INetSupport* g_pNetSupport;
extern IBinTools* bintools;
extern ISDKTools* sdktools;
extern IServer* g_pGameIServer;
extern CGlobalVars* gpGlobals;
extern int shookid_CServerGameEnts_CheckTransmit;

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
	const char*, const char*, int, ConnectPlayers&, bool);

namespace {

void DestroyCall(ICallWrapper* call)
{
	call->Destroy();
}

void RemoveHook(int id)
{
	SH_REMOVE_HOOK_ID(id);
}

} // namespace

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

struct SMExtension::RuntimeOperations {
	SMExtension& owner;
	char* error = nullptr;
	size_t maxlength = 0;

	bool BindSteam() { return owner.SetupFromSteamAPILibrary(error, maxlength); }
	bool CreateDetours() { return owner.CreateDetours(error, maxlength); }
	bool CreateVCalls() { return owner.CreateVCalls(error, maxlength); }
	bool BindHLTV() { return owner.BindHLTVServer(hltvdirector->GetHLTVServer(), error, maxlength); }
	bool HookSteamLogOff() { return owner.BindSteamLogOff(error, maxlength); }
	bool EnableDetours() { return owner.EnableDetours(error, maxlength); }

	bool ResolveGameServer()
	{
		g_pGameIServer = sdktools->GetIServer();
		if (g_pGameIServer)
			return true;
		ke::SafeStrcpy(error, maxlength, "Unable to retrieve the game IServer from SDKTools");
		return false;
	}

	bool HookDirector()
	{
		const int hook = SH_ADD_HOOK(IHLTVDirector, SetHLTVServer, hltvdirector,
			SH_MEMBER(&owner, &SMExtension::Handler_CHLTVDirector_SetHLTVServer), true);
		if (owner.m_Resources.Take<RemoveHook>(owner.m_DirectorHook, hook))
			return true;
		ke::SafeStrcpy(error, maxlength, "Unable to hook IHLTVDirector::SetHLTVServer");
		return false;
	}

	void RepairHLTV()
	{
		if (g_pHLTVServer)
			owner.RepairHLTVServer(g_pHLTVServer);
#ifdef TV_RELAYTEST
		static ConVarRef clientport("clientport");
		InvokeOpenSocketInternal(NS_CLIENT, clientport.GetInt(), PORT_SERVER, "client", true);
#endif
	}

	void RegisterLibrary() { sharesys->RegisterLibrary(myself, "sourcetvsupport"); }

	void DisableDetours()
	{
		g_pHLTVServer = nullptr;
		owner.DisableDetours();
	}

	void UnhookSteam() { owner.m_SteamHooks.Reset(); }
	void UnhookHLTV() { owner.m_HLTVHooks.Reset(); }
	void ReleaseResources() { owner.m_Resources.Reset(); }
	void ClearBindings()
	{
		owner.ClearDetourTrampolines();
		g_pGameIServer = nullptr;
		owner.ClearSteamBindings();
	}
};

bool SMExtension::Load(char* error, size_t maxlength)
{
	RuntimeOperations runtime{*this, error, maxlength};
	return m_Lifecycle.Start(runtime);
}

void SMExtension::Unload()
{
	RuntimeOperations runtime{*this};
	m_Lifecycle.Stop(runtime);
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
	if (!m_Lifecycle.IsActive())
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
	return sourcetv::BindRuntimeServer(g_pHLTVServer, server, [this] { m_HLTVHooks.Reset(); },
		[&](IHLTVServer* server) {
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
				return false;
			};

			return track(CHLTVServer::shookid_ReplyChallenge, SH_ADD_MANUALHOOK(CBaseServer_ReplyChallenge,
					base, SH_MEMBER(this, &SMExtension::Handler_CHLTVServer_ReplyChallenge), false), "HLTV ReplyChallenge") &&
#if SOURCE_ENGINE == SE_LEFT4DEAD2
				track(CHLTVServer::shookid_FillServerInfo, SH_ADD_MANUALHOOK(CBaseServer_FillServerInfo,
					base, SH_MEMBER(this, &SMExtension::Handler_CHLTVServer_FillServerInfo), true), "HLTV CBaseServer::FillServerInfo") &&
#if !defined _WIN32
				track(CHLTVServer::shookid_hltv_FillServerInfo, SH_ADD_MANUALHOOK(CHLTVServer_FillServerInfo,
					hltv, SH_MEMBER(this, &SMExtension::Handler_CHLTVServer_FillServerInfo), true), "CHLTVServer::FillServerInfo") &&
#endif
#endif
				track(CHLTVServer::shookid_ConnectClient, SH_ADD_MANUALHOOK(CHLTVServer_ConnectClient,
					base, SH_MEMBER(this, &SMExtension::Handler_CHLTVServer_ConnectClient), false), "HLTV ConnectClient") &&
				track(shookid_CHLTVDemoRecorder_RecordStringTables, SH_ADD_HOOK(CHLTVDemoRecorder, RecordStringTables,
					&recorder, SH_MEMBER(this, &SMExtension::Handler_CHLTVDemoRecorder_RecordStringTables), false), "HLTV RecordStringTables") &&
				track(shookid_CHLTVDemoRecorder_RecordServerClasses, SH_ADD_HOOK(CHLTVDemoRecorder, RecordServerClasses,
					&recorder, SH_MEMBER(this, &SMExtension::Handler_CHLTVDemoRecorder_RecordServerClasses), false), "HLTV RecordServerClasses") &&
				track(shookid_CServerGameEnts_CheckTransmit, SH_ADD_HOOK(IServerGameEnts, CheckTransmit,
					gameents, SH_MEMBER(this, &SMExtension::Handler_CServerGameEnts_CheckTransmit), true), "CheckTransmit");

		});
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
	if (m_Lifecycle.IsActive() && g_pHLTVServer == pIHLTVServer)
		pServer->stringTableCRC() = CBaseServer::FromIServer(g_pGameIServer)->stringTableCRC();
}

void SMExtension::Handler_CHLTVDirector_SetHLTVServer(IHLTVServer* pIHLTVServer)
{
	if (!m_Lifecycle.IsActive())
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

	const int challenge = _this->GetChallengeNr(adr);
	const int authentication = _this->GetChallengeType(adr);
	sourcetv::WriteSpectatorChallenge(msg, challenge, authentication, context,
		g_pNetSupport->GetEngineBuildNumber());

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
	const char* password, const char* hashedCDkey, int cdKeyLen, ConnectPlayers& splitScreenClients, bool isClientLowViolence)
{
	if (splitScreenClients.Count() > 1)
	{
		char buffer[512];
		bf_write msg(buffer, sizeof(buffer));

		sourcetv::WriteSplitscreenRejection(msg);

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
	RuntimeOperations runtime{*this, error, maxlength};
	if (!m_Lifecycle.Prepare(runtime))
		return false;
	m_LifecycleError[0] = '\0';

	sharesys->AddDependency(myself, "bintools.ext", true, true);
	sharesys->AddDependency(myself, "sdktools.ext", true, true);

	return true;
}
