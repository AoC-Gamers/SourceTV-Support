// SPDX-License-Identifier: GPL-3.0-only
// Extracted from SourceTV-Support by Evgeniy Kazakov and contributors.
#include "extension.h"
#include "wrappers.h"
#include "sdk/public/engine/inetsupport.h"

#include <extensions/ISDKTools.h>
#include <am-string.h>

extern INetworkStringTableContainer* networkStringTableContainerServer;
extern INetSupport* g_pNetSupport;
extern IHLTVDirector* hltvdirector;
extern IBinTools* bintools;
extern ISDKTools* sdktools;
extern IServer* g_pGameIServer;
extern CGlobalVars* gpGlobals;

void SMExtension::SDK_OnUnload()
{
	Unload();
	bintools = nullptr;
	sdktools = nullptr;
}

void SMExtension::FailRuntime()
{
	Unload();
	smutils->LogError(myself, "%s", m_LifecycleError);
}

void SMExtension::SDK_OnAllLoaded()
{
	if (m_Phase != Phase::Prepared)
		return;

	SM_GET_LATE_IFACE(SDKTOOLS, sdktools);
	SM_GET_LATE_IFACE(BINTOOLS, bintools);

	if (!sdktools || !bintools) {
		ke::SafeStrcpy(m_LifecycleError, sizeof(m_LifecycleError),
			!sdktools ? "SDKTools is unavailable" : "BinTools is unavailable");
		FailRuntime();
		return;
	}

	if (!Load(m_LifecycleError, sizeof(m_LifecycleError)))
		FailRuntime();
}

bool SMExtension::SDK_OnMetamodLoad(ISmmAPI* ismm, char* error, size_t maxlen, bool late)
{
	GET_V_IFACE_CURRENT(GetEngineFactory, networkStringTableContainerServer, INetworkStringTableContainer, INTERFACENAME_NETWORKSTRINGTABLESERVER);
	GET_V_IFACE_CURRENT(GetEngineFactory, g_pNetSupport, INetSupport, INETSUPPORT_VERSION_STRING);

	GET_V_IFACE_CURRENT(GetServerFactory, hltvdirector, IHLTVDirector, INTERFACEVERSION_HLTVDIRECTOR);
	GET_V_IFACE_CURRENT(GetServerFactory, playerinfomanager, IPlayerInfoManager, INTERFACEVERSION_PLAYERINFOMANAGER);
	GET_V_IFACE_CURRENT(GetServerFactory, gameents, IServerGameEnts, INTERFACEVERSION_SERVERGAMEENTS);

	gpGlobals = ismm->GetCGlobals();

	// For ConVarRef
	GET_V_IFACE_CURRENT(GetEngineFactory, g_pCVar, ICvar, CVAR_INTERFACE_VERSION);

	return true;
}

bool SMExtension::QueryInterfaceDrop(SMInterface* interface)
{
	if (bintools && bintools == interface)
		return false;

	if (sdktools && sdktools == interface)
		return m_Phase == Phase::Active && g_pGameIServer;

	return IExtensionInterface::QueryInterfaceDrop(interface);
}

void SMExtension::NotifyInterfaceDrop(SMInterface* interface)
{
	if (bintools && bintools == interface) {
		// SourceMod notifies dependents before unloading the provider module.
		// The call wrappers can still destroy themselves here.
		ke::SafeStrcpy(m_LifecycleError, sizeof(m_LifecycleError), "BinTools was unloaded");
		Unload();
		bintools = nullptr;
	} else if (sdktools && sdktools == interface) {
		sdktools = nullptr;
		// SDKTools only supplies the pointer. The engine owns the game server.
		if (m_Phase != Phase::Active || !g_pGameIServer) {
			ke::SafeStrcpy(m_LifecycleError, sizeof(m_LifecycleError), "SDKTools was unloaded before resolving IServer");
			Unload();
		}
	}
}

bool SMExtension::QueryRunning(char* error, size_t maxlength)
{
	if (m_Phase == Phase::Active)
		return true;

	if (error && maxlength) {
		ke::SafeStrcpy(error, maxlength, m_LifecycleError[0] ? m_LifecycleError :
			"SourceTV Support is not active");
	}
	return false;
}
