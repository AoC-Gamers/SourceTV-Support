// SPDX-License-Identifier: GPL-3.0-only
#ifndef SOURCETV_RUNTIME_LIFECYCLE_H
#define SOURCETV_RUNTIME_LIFECYCLE_H

namespace sourcetv {

class RuntimeLifecycle {
public:
	bool IsActive() const { return phase_ == Phase::Active; }
	bool IsPrepared() const { return phase_ == Phase::Prepared; }

	template <class Runtime>
	bool Prepare(Runtime& runtime)
	{
		if (!runtime.BindSteam() || !runtime.CreateDetours()) {
			Stop(runtime);
			return false;
		}
		phase_ = Phase::Prepared;
		return true;
	}

	template <class Runtime>
	bool Start(Runtime& runtime)
	{
		if (!IsPrepared())
			return false;
		if (!runtime.ResolveGameServer() || !runtime.CreateVCalls() ||
		    !runtime.HookDirector() || !runtime.BindHLTV() ||
		    !runtime.HookSteamLogOff() || !runtime.EnableDetours()) {
			Stop(runtime);
			return false;
		}
		phase_ = Phase::Active;
		runtime.RepairHLTV();
		if (!IsActive())
			return false;
		runtime.RegisterLibrary();
		return true;
	}

	template <class Runtime>
	void Stop(Runtime& runtime)
	{
		phase_ = Phase::Stopped;
		runtime.DisableDetours();
		runtime.UnhookSteam();
		runtime.UnhookHLTV();
		runtime.ReleaseResources();
		runtime.ClearBindings();
	}

	bool CanDropSDKTools(bool gameServerResolved) const
	{
		return IsActive() && gameServerResolved;
	}

	template <class Stop, class Clear>
	void DropBinTools(Stop stop, Clear clear)
	{
		// Wrappers must be destroyed while the provider is still alive.
		stop();
		clear();
	}

	template <class Stop, class Clear>
	void DropSDKTools(bool gameServerResolved, Stop stop, Clear clear)
	{
		clear();
		if (!CanDropSDKTools(gameServerResolved))
			stop();
	}

private:
	enum class Phase { Stopped, Prepared, Active };
	Phase phase_ = Phase::Stopped;
};

template <class Server, class Reset, class Bind>
bool BindRuntimeServer(Server*& current, Server* next, Reset reset, Bind bind)
{
	current = nullptr;
	reset();
	if (!next)
		return true;
	if (!bind(next)) {
		reset();
		return false;
	}
	current = next;
	return true;
}

template <class Bindings, class Apply>
bool ApplyRuntimeBindings(const Bindings& bindings, Apply apply)
{
	for (const auto& binding : bindings) {
		if (!apply(binding))
			return false;
	}
	return true;
}

} // namespace sourcetv
#endif
