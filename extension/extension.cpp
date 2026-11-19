#include "extension.h"
#include "engine/demo_writer.h"
#include "engine/wrappers.h"
#include "sdk/public/engine/inetsupport.h"
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

SMExtension g_Extension;
SMEXT_LINK(&g_Extension);
