// SPDX-License-Identifier: GPL-3.0-only
// Extracted from SourceTV-Support by Evgeniy Kazakov and contributors.
#include "extension.h"
#include "transmission_policy.h"
#include "wrappers.h"

#include <algorithm>

extern IHLTVServer* g_pHLTVServer;
extern CGlobalVars* gpGlobals;

void TrySendPZMsgToSourceTV(const HitAnnouncement& rMsg)
{
	if (g_pHLTVServer == NULL) {
		return;
	}

	int iSourceTVIndex = g_pHLTVServer->GetHLTVSlot() + 1;

	CBasePlayer* pSourceTV = UTIL_PlayerByIndex(iSourceTVIndex);
	if (pSourceTV == NULL || !pSourceTV->IsHLTV()) {
		return;
	}

	if (rMsg.m_pAttacker == NULL || rMsg.m_pVictim == NULL) {
		return;
	}

	const auto message = DamageHudMessage::Create(rMsg.m_iEventType, rMsg.m_pAttacker->GetUserID(),
		rMsg.m_pVictim->GetUserID(), rMsg.m_pInflictor ? rMsg.m_pInflictor->GetUserID() : 0,
		rMsg.m_iDamageAmount);
	if (message) {
		message->Send([&] {
			return usermsgs->StartBitBufMessage(HitAnnouncement::pzMsgId, &iSourceTVIndex, 1, USERMSG_RELIABLE);
		}, [] { usermsgs->EndMessage(); });
	}
}

void SMExtension::Handler_CHLTVServer_FillServerInfo(SVC_ServerInfo& serverinfo)
{
	// feature request #12 - allow addons in demos
	serverinfo.m_bIsVanilla = false;
}

void SMExtension::Handler_CServerGameEnts_CheckTransmit(CCheckTransmitInfo* pInfo, const unsigned short* pEdictIndices, int nEdicts)
{
	SET_META_RESULT(MRES_OVERRIDE);

	if (!pInfo || !gpGlobals || !gpGlobals->pEdicts || gpGlobals->maxEntities <= 1)
		return;

	IGamePlayer* recipient = playerhelpers->GetGamePlayer(pInfo->m_pClientEnt);
	if (!recipient || !recipient->IsSourceTV())
		return;

	const int maxClientEdict = (std::min)(playerhelpers->GetMaxClients(), gpGlobals->maxEntities - 1);
	VisitPlayerEdicts(pEdictIndices, nEdicts, maxClientEdict, [](int index) {
		// Refresh bot PVS data when tv_transmitall is disabled.
		if (IServerNetworkable* networkable = gpGlobals->pEdicts[index].GetNetworkable())
			networkable->AreaNum();
	});
}
