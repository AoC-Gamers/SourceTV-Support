// SPDX-License-Identifier: GPL-3.0-only
#include <iserverplugin.h>
#include <icvar.h>
#include <tier1/convar.h>

#include <cassert>
#include <cstdio>
#include <cstring>
#include <dlfcn.h>
#include <initializer_list>

namespace {

ICvar* offeredCvars = nullptr;
int queries = 0;

void* EngineFactory(const char* name, int*)
{
	assert(std::strcmp(name, CVAR_INTERFACE_VERSION) == 0);
	++queries;
	return offeredCvars;
}

void* UnusedGameFactory(const char*, int*)
{
	assert(false);
	return nullptr;
}

void* Open(const char* path)
{
	void* library = dlopen(path, RTLD_NOW | RTLD_LOCAL);
	if (!library)
		std::fprintf(stderr, "%s\n", dlerror());
	assert(library);
	return library;
}

CreateInterfaceFn Factory(void* library)
{
	auto factory = reinterpret_cast<CreateInterfaceFn>(dlsym(library, "CreateInterface"));
	assert(factory);
	return factory;
}

} // namespace

int main(int argc, char** argv)
{
	static_assert(sizeof(void*) == 4);
	assert(argc == 2);

	void* pluginModule = Open(argv[1]);
	auto pluginFactory = Factory(pluginModule);
	int status = IFACE_OK;
	assert(!pluginFactory("ISERVERPLUGINCALLBACKS999", &status) && status == IFACE_FAILED);
	auto* plugin = static_cast<IServerPluginCallbacks*>(
		pluginFactory(INTERFACEVERSION_ISERVERPLUGINCALLBACKS, &status));
	assert(plugin && status == IFACE_OK);

	assert(!plugin->Load(nullptr, UnusedGameFactory));
	assert(queries == 0);
	assert(!plugin->Load(EngineFactory, UnusedGameFactory));
	assert(queries == 1);

	// Use the SDK's ICvar implementation with an independently populated registry.
	void* engineModule = Open("libvstdlib_srv.so");
	auto* engine = static_cast<ICvar*>(Factory(engineModule)(CVAR_INTERFACE_VERSION, nullptr));
	assert(engine);
	offeredCvars = engine;
	assert(!plugin->Load(EngineFactory, UnusedGameFactory));
	assert(queries == 2);

	{
		ConVar enable("tv_enable", "0", FCVAR_DEVELOPMENTONLY | FCVAR_ARCHIVE);
		ConVar rate("tv_snapshotrate", "16", FCVAR_NOTIFY);
		ConVar sharing("sv_master_share_game_socket", "1", FCVAR_DEVELOPMENTONLY | FCVAR_NOTIFY);
		ConVar unrelated("vsp_contract_unrelated", "1", FCVAR_DEVELOPMENTONLY | FCVAR_ARCHIVE);

		for (ConVar* variable : {&enable, &rate, &sharing, &unrelated}) {
			engine->RegisterConCommand(variable);
			assert(engine->FindVar(variable->GetName()) == variable);
		}

		assert(enable.GetFlags() == (FCVAR_DEVELOPMENTONLY | FCVAR_ARCHIVE));
		assert(sharing.GetFlags() == (FCVAR_DEVELOPMENTONLY | FCVAR_NOTIFY));
		assert(!engine->FindVar("tv_maxclients"));

		for (int attempt = 0; attempt < 2; ++attempt) {
			assert(!plugin->Load(EngineFactory, UnusedGameFactory));
			assert(queries == 3 + attempt);
			assert(enable.GetFlags() == FCVAR_ARCHIVE && enable.GetInt() == 0);
			assert(rate.GetFlags() == FCVAR_NOTIFY && rate.GetInt() == 16);
			assert(sharing.GetFlags() == FCVAR_NOTIFY && sharing.GetInt() == 1);
			assert(unrelated.GetFlags() == (FCVAR_DEVELOPMENTONLY | FCVAR_ARCHIVE));
		}

		offeredCvars = nullptr;
		assert(!plugin->Load(EngineFactory, UnusedGameFactory));
		assert(queries == 5 && unrelated.IsFlagSet(FCVAR_DEVELOPMENTONLY));
		for (ConVar* variable : {&enable, &rate, &sharing, &unrelated})
			engine->UnregisterConCommand(variable);
	}

	dlclose(engineModule);
	dlclose(pluginModule);
	std::puts("vsp_startup: factory and one-shot cvar setup passed");
}
