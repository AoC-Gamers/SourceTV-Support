// SPDX-License-Identifier: GPL-3.0-only
// Extracted from SourceTV-Support by Evgeniy Kazakov and contributors.
#include "extension.h"
#include "engine/demo_writer.h"
#include "engine/wrappers.h"


void InvokeDataTable_WriteSendTablesBuffer(ServerClass* pClasses, bf_write* buf)
{
	void DataTable_WriteSendTablesBuffer(ServerClass * pClasses, bf_write * buf);
	return reinterpret_cast<decltype(DataTable_WriteSendTablesBuffer)*>(pfn_DataTable_WriteSendTablesBuffer)(pClasses, buf);
}

#if SOURCE_ENGINE == SE_LEFT4DEAD2
void* InvokeCreateInterface(const char* ver)
{
	return reinterpret_cast<decltype(SteamInternal_CreateInterface)*>(pfn_SteamInternal_CreateInterface)(ver);
}

HSteamPipe InvokeGetHSteamPipe()
{
	return reinterpret_cast<decltype(SteamGameServer_GetHSteamPipe)*>(pfn_SteamGameServer_GetHSteamPipe)();
}

HSteamUser InvokeGetHSteamUser()
{
	return reinterpret_cast<decltype(SteamGameServer_GetHSteamUser)*>(pfn_SteamGameServer_GetHSteamUser)();
}
#endif

void InvokeOpenSocketInternal(int nModule, int nSetPort, int nDefaultPort, const char* pName, bool bTryAny)
{
	void OpenSocketInternal(int nModule, int nSetPort, int nDefaultPort, const char* pName, bool bTryAny);
	return reinterpret_cast<decltype(OpenSocketInternal)*>(pfn_OpenSocketInternal)(nModule, nSetPort, nDefaultPort, pName, bTryAny);
}

CBasePlayer* UTIL_PlayerByIndex(int playerIndex)
{
	if (playerIndex > 0 && playerIndex <= playerhelpers->GetMaxClients()) {
		IGamePlayer* pPlayer = playerhelpers->GetGamePlayer(playerIndex);
		if (pPlayer != NULL) {
			return (CBasePlayer*)gameents->EdictToBaseEntity(pPlayer->GetEdict());
		}
	}

	return NULL;
}


void CBasePlayer::AddFlag(int flags)
{
	if (!sendprop_m_fFlags)
		return;

	static_assert(sizeof(int) == 4, "L4D2 stores entity flags in a 32-bit field");
	sourcetv::abi::AddPlayerFlags(this, *sendprop_m_fFlags, flags);

	if (edict_t* edict = this->edict())
		gamehelpers->SetEdictStateChanged(edict, sendprop_m_fFlags->Bytes());
}
