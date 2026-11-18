// SPDX-License-Identifier: GPL-3.0-only
#ifndef SOURCETV_RUNTIME_LIFECYCLE_H
#define SOURCETV_RUNTIME_LIFECYCLE_H

namespace sourcetv {

class RuntimeLifecycle {
public:
	bool IsActive() const { return phase_ == Phase::Active; }
	bool IsPrepared() const { return phase_ == Phase::Prepared; }

	template <class Cleanup, class... Stage>
	bool Prepare(Cleanup cleanup, Stage... stage)
	{
		if (!(stage() && ...)) {
			Stop(cleanup);
			return false;
		}
		phase_ = Phase::Prepared;
		return true;
	}

	template <class Cleanup, class... Stage>
	bool Start(Cleanup cleanup, Stage... stage)
	{
		if (!IsPrepared())
			return false;
		if (!(stage() && ...)) {
			Stop(cleanup);
			return false;
		}
		phase_ = Phase::Active;
		return true;
	}

	template <class... Cleanup>
	void Stop(Cleanup... cleanup)
	{
		phase_ = Phase::Stopped;
		(cleanup(), ...);
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
