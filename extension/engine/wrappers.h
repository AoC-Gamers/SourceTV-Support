#ifndef _INCLUDE_WRAPPERS_H_
#define _INCLUDE_WRAPPERS_H_

#include <checksum_crc.h>
#include <extensions/IBinTools.h>
#include "engine/engine_binding.h"

#include <iserver.h>
#include <ihltvdirector.h>
#include <ihltv.h>
#include <server_class.h>
#include <game/server/iplayerinfo.h>
#include <iclient.h>
#include <igameevents.h>
#include <networkstringtabledefs.h>

extern IServerGameEnts* gameents;
extern IPlayerInfoManager* playerinfomanager;

// ref: https://partner.steamgames.com/downloads/list
// Left 4 Dead - sdk v1.06
// Left 4 Dead 2 - sdk v1.41

#if SOURCE_ENGINE == SE_LEFT4DEAD2
#include "steam/steam_gameserver.h"
#include "steam/steamclientpublic.h"
#else
#include "steam/steamclientpublic.h"
#endif

#include "sdk/engine/demo.h"
#include "sdk/engine/clientframe.h"

#define TICK_INTERVAL			(gpGlobals->interval_per_tick)
#define TIME_TO_TICKS( dt )		( (int)( 0.5f + (float)(dt) / TICK_INTERVAL ) )

class CDetour;
class CHLTVDemoRecorder;

extern int shookid_CHLTVDemoRecorder_RecordStringTables;
extern int shookid_CHLTVDemoRecorder_RecordServerClasses;

extern void* pfn_SteamGameServer_GetHSteamPipe;
extern void* pfn_SteamGameServer_GetHSteamUser;
extern void* pfn_SteamInternal_CreateInterface;
extern void* pfn_SteamInternal_GameServer_Init;
extern void* pfn_OpenSocketInternal;

extern CDetour* detour_SteamInternal_GameServer_Init;

// Steam API
#if SOURCE_ENGINE == SE_LEFT4DEAD2
void* InvokeCreateInterface(const char* ver);

HSteamPipe InvokeGetHSteamPipe();

HSteamUser InvokeGetHSteamUser();
#endif

void InvokeOpenSocketInternal(int nModule, int nSetPort, int nDefaultPort, const char* pName, bool bTryAny);

class CBaseClient :
	public IGameEventListener2,
	public IClient,
	public IClientMessageHandler
{
public:
	static std::optional<sourcetv::abi::FieldOffset> offset_m_SteamID;

	static void* pfn_SendFullConnectEvent;

	static CDetour* detour_SendFullConnectEvent;

	CSteamID& m_SteamID()
	{
		return *reinterpret_cast<CSteamID*>(reinterpret_cast<byte*>(this) + offset_m_SteamID->Bytes());
	}
};

class CBaseServer
	: public IServer
{
public:
	static std::optional<sourcetv::abi::FieldOffset> offset_stringTableCRC;
	static std::optional<sourcetv::abi::VtableSlot> vtblindex_GetChallengeNr;
	static std::optional<sourcetv::abi::VtableSlot> vtblindex_GetChallengeType;
	static std::optional<sourcetv::abi::VtableSlot> vtblindex_ReplyChallenge;
	static std::optional<sourcetv::abi::VtableSlot> vtblindex_FillServerInfo;
	static std::optional<sourcetv::abi::VtableSlot> vtblindex_ConnectClient;

	static void* pfn_IsExclusiveToLobbyConnections;

	static CDetour* detour_IsExclusiveToLobbyConnections;

	static ICallWrapper* vcall_GetChallengeNr;
	static ICallWrapper* vcall_GetChallengeType;

	static CBaseServer* FromIServer(IServer* pServer)
	{
		return reinterpret_cast<CBaseServer*>(pServer);
	}

	static CBaseServer* FromIHLTVServer(IHLTVServer* pHLTVServer)
	{
		return FromIServer(pHLTVServer->GetBaseServer());
	}

	CRC32_t& stringTableCRC()
	{
		return *reinterpret_cast<CRC32_t*>(reinterpret_cast<byte*>(this) + offset_stringTableCRC->Bytes());
	}

	INetworkStringTableContainer* m_StringTables()
	{
		return *reinterpret_cast<INetworkStringTableContainer**>(reinterpret_cast<byte*>(this) + offset_stringTableCRC->Bytes() + 4);
	}

	int GetChallengeNr(netadr_t& adr)
	{
		struct {
			CBaseServer* pServer;
			netadr_s* adr;
		} stack{ this, &adr };

		int ret;
		vcall_GetChallengeNr->Execute(&stack, &ret);

		return ret;
	}

	int GetChallengeType(netadr_t& adr)
	{
		struct {
			CBaseServer* pServer;
			netadr_s* adr;
		} stack{ this, &adr };

		int ret;
		vcall_GetChallengeType->Execute(&stack, &ret);

		return ret;
	}
};

class CHLTVServer
{
public:
	static std::optional<sourcetv::abi::FieldOffset> offset_m_DemoRecorder;
	static std::optional<sourcetv::abi::FieldOffset> offset_CClientFrameManager;
	static std::optional<sourcetv::abi::BaseAdjustment> offset_CBaseServer;

	static std::optional<sourcetv::abi::VtableSlot> vtblindex_FillServerInfo;

	static int shookid_ReplyChallenge;
	static int shookid_FillServerInfo;
	static int shookid_hltv_FillServerInfo;
	static int shookid_ConnectClient;

	static void* pfn_AddNewFrame;

	static CDetour* detour_AddNewFrame;

	CHLTVDemoRecorder& m_DemoRecorder()
	{
		return *reinterpret_cast<CHLTVDemoRecorder*>(reinterpret_cast<byte*>(this) + offset_m_DemoRecorder->Bytes());
	}

	CClientFrameManager& GetClientFrameManager()
	{
		return *reinterpret_cast<CClientFrameManager*>(reinterpret_cast<byte*>(this) + offset_CClientFrameManager->Bytes());
	}

	static CHLTVServer* FromBaseServer(CBaseServer* pServer)
	{
		return offset_CBaseServer ? static_cast<CHLTVServer*>(offset_CBaseServer->CompleteObject(pServer)) : nullptr;
	}
};

class CSteam3Server
{
public:
	static void* pfn_NotifyClientDisconnect;

	static CDetour* detour_NotifyClientDisconnect;
};

class CFrameSnapshotManager
{
public:
	static std::optional<sourcetv::abi::FieldOffset> offset_m_PackedEntitiesPool;

	static void* pfn_RecordStringTables;
	static void* pfn_RecordServerClasses;

	static void* pfn_LevelChanged;

	static CDetour* detour_LevelChanged;

	CClassMemoryPoolExt<PackedEntity>& m_PackedEntitiesPool()
	{
		return *reinterpret_cast<CClassMemoryPoolExt<PackedEntity>*>(reinterpret_cast<byte*>(this) + offset_m_PackedEntitiesPool->Bytes());
	}
};

class CBaseEntity :
	public IServerEntity
{
public:
	edict_t* edict()
	{
		return gameents->BaseEntityToEdict(this);
	}
};

class CBaseAbility :
	public CBaseEntity
{
public:
	static void* pfn_ShouldTransmit;
	static CDetour* detour_ShouldTransmit;
};

class CBasePlayer :
	public CBaseEntity
{
public:
	static std::optional<sourcetv::abi::FieldOffset> sendprop_m_fFlags;

	IGamePlayer* GetIGamePlayer()
	{
		return playerhelpers->GetGamePlayer(edict());
	}

	void AddFlag(int flags);

	bool IsHLTV()
	{
		IGamePlayer* pGamePlayer = GetIGamePlayer();
		if (pGamePlayer == NULL) {
			return false;
		}

		return pGamePlayer->IsSourceTV();
	}

	int GetUserID()
	{
		return engine->GetPlayerUserId(edict());
	}

	void ChangeTeam(int teamIndex)
	{
		IPlayerInfo* pPlayerInfo = playerinfomanager->GetPlayerInfo(edict());
		if (pPlayerInfo == NULL) {
			return;
		}

		if (pPlayerInfo->GetTeamIndex() == teamIndex) {
			return;
		}

		pPlayerInfo->ChangeTeam(teamIndex);
	}
};

class CTerrorPlayer :
	public CBasePlayer
{
public:
	//
};

class HitAnnouncement
{
public:
	static void* pfn_ForEachTerrorPlayer;

	static CDetour* detour_ForEachTerrorPlayer;

	static int pzMsgId;

#if defined _WIN32
	static void SetupFromRelativeAddress(ptrdiff_t relative)
	{
		pfn_ForEachTerrorPlayer = reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(pfn_ForEachTerrorPlayer) + relative);
	}
#endif

public:
	int m_iEventType;

	CTerrorPlayer* m_pVictim;
	CTerrorPlayer* m_pAttacker;
	CTerrorPlayer* m_pInflictor;

	int m_iDamageAmount;
	bool m_bIgnoreTeamCheck;
};

CBasePlayer* UTIL_PlayerByIndex(int playerIndex);

#endif // _INCLUDE_WRAPPERS_H_
