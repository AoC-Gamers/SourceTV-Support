#include "plugin_vsp.h"
#include <icvar.h>

#include <cstdio>
#include <iterator>

EXPOSE_SINGLE_INTERFACE(VSPPlugin, IServerPluginCallbacks, INTERFACEVERSION_ISERVERPLUGINCALLBACKS);

bool VSPPlugin::Load(CreateInterfaceFn interfaceFactory, CreateInterfaceFn gameServerFactory)
{
	ICvar* engineCvars = interfaceFactory
		? static_cast<ICvar*>(interfaceFactory(CVAR_INTERFACE_VERSION, nullptr))
		: nullptr;
	if (!engineCvars) {
		std::fprintf(stderr, PLUGIN_LOG_PREFIX "VSP setup failed: engine factory did not provide " CVAR_INTERFACE_VERSION ".\n");
		return false;
	}

	static const char* const cvars[] = {
		"tv_enable",
		"tv_maxclients",
		"tv_dispatchmode",
		"tv_transmitall",
		"tv_relayvoice",
		"tv_debug",
		"tv_deltacache",
		"tv_password",
		"tv_overridemaster",
		"tv_autorecord",
		"tv_name",
		"tv_title",

		"tv_autoretry",
		"tv_timeout",
		"tv_snapshotrate",

		"tv_maxrate",
		"tv_relaypassword",
		"tv_chattimelimit",
		"tv_chatgroupsize",

		"tv_allow_camera_man",
		"tv_allow_static_shots",
		"tv_delay",
		//"tv_delaymapchange",

		"sv_hibernate_when_empty",
		"sv_master_share_game_socket",
	};

	std::size_t available = 0;
	for (const char* name : cvars) {
		if (ConVar* variable = engineCvars->FindVar(name)) {
			variable->RemoveFlags(FCVAR_DEVELOPMENTONLY);
			++available;
		}
	}

	std::printf(PLUGIN_LOG_PREFIX "VSP early setup complete: %zu of %zu cvars available (%zu missing). Exiting as intended.\n",
		available, std::size(cvars), std::size(cvars) - available);
	std::fflush(stdout);
	return false;
}
