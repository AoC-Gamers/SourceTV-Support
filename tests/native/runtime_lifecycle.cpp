// SPDX-License-Identifier: GPL-3.0-only
#include "core/runtime_resources.h"
#include "core/runtime_lifecycle.h"
#include "core/steam_runtime.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdio>
#include <type_traits>
#include <vector>

namespace {

std::vector<int> releasedHooks;
bool providerAlive = false;

struct Call
{
	int id;
	std::vector<int>* released;
};

void RemoveHook(int hook)
{
	assert(hook != 0);
	releasedHooks.push_back(hook);
}

void DestroyCall(Call* call)
{
	assert(providerAlive);
	call->released->push_back(call->id);
}

void TestPartialHookAcquisitions()
{
	constexpr int hookCount = 7;
	for (int failAt = 0; failAt <= hookCount; ++failAt) {
		std::array<int, hookCount> hooks{};
		sourcetv::RuntimeResources<hookCount> owner;
		releasedHooks.clear();

		int i = 0;
		for (; i < hookCount; ++i) {
			if (!owner.Take<RemoveHook>(hooks[i], i == failAt ? 0 : i + 1))
				break;
		}
		assert(i == failAt);
		owner.Reset();
		owner.Reset();

		assert(releasedHooks.size() == static_cast<size_t>(failAt));
		for (int j = 0; j < failAt; ++j)
			assert(releasedHooks[j] == failAt - j);
		assert(std::all_of(hooks.begin(), hooks.end(), [](int id) { return id == 0; }));

		// The same group can bind a new engine instance after rollback.
		assert(owner.Take<RemoveHook>(hooks[0], 100));
		owner.Reset();
		assert(releasedHooks.back() == 100);
	}
}

void TestPreparationAndProviderDrop()
{
	constexpr size_t detourCount = 8;
	constexpr size_t callCount = 2;
	constexpr size_t count = detourCount + callCount;
	std::vector<int> released;
	std::array<Call, count> bindings{};
	for (size_t i = 0; i < bindings.size(); ++i)
		bindings[i] = {static_cast<int>(i + 1), &released};

	for (size_t failAt = 0; failAt <= count; ++failAt) {
		std::array<Call*, count> slots{};
		sourcetv::RuntimeResources<count> owner;
		released.clear();
		providerAlive = true;
		size_t i = 0;
		for (; i < count; ++i) {
			if (!owner.Take<DestroyCall>(slots[i], i == failAt ? nullptr : &bindings[i]))
				break;
		}
		assert(i == failAt);

		owner.Reset();
		providerAlive = false;
		owner.Reset();
		assert(std::all_of(slots.begin(), slots.end(), [](const Call* call) { return !call; }));
		assert(released.size() == failAt);
		for (size_t j = 0; j < released.size(); ++j)
			assert(released[j] == static_cast<int>(failAt - j));
	}
}

struct LifecycleProvider {
	sourcetv::RuntimeLifecycle& lifecycle;
	int failAt;
	int attempts = 0;
	int published = 0;
	int repairs = 0;
	bool providerAvailable = true;
	bool retireDuringRepair = false;

	LifecycleProvider(sourcetv::RuntimeLifecycle& state, int fail) : lifecycle(state), failAt(fail) {}

	std::vector<int> detours;
	std::vector<int> enabled;
	std::vector<int> calls;
	std::vector<int> hltvHooks;
	std::vector<int> steamHooks;
	std::vector<int> bindings;
	std::vector<int> acquired;
	std::vector<int> released;

	bool Take(std::vector<int>& resources, int id)
	{
		assert(!lifecycle.IsActive() && !published);
		if (attempts++ == failAt)
			return false;
		acquired.push_back(id);
		resources.push_back(id);
		return true;
	}

	bool TakeRange(std::vector<int>& resources, int first, int count)
	{
		for (int i = 0; i < count; ++i) {
			if (!Take(resources, first + i))
				return false;
		}
		return true;
	}

	bool BindSteam() { return Take(bindings, 1); }
	bool CreateDetours() { return TakeRange(detours, 10, 8); }
	bool ResolveGameServer() { return Take(bindings, 2); }
	bool CreateVCalls() { return TakeRange(calls, 20, 2); }
	bool HookDirector() { return Take(calls, 22); }
	bool BindHLTV() { return TakeRange(hltvHooks, 30, 7); }
	bool HookSteamLogOff() { return Take(steamHooks, 40); }
	bool EnableDetours()
	{
		assert(detours.size() == 8 && calls.size() == 3 && hltvHooks.size() == 7);
		return TakeRange(enabled, 50, 8);
	}

	void RepairHLTV()
	{
		assert(lifecycle.IsActive() && enabled.size() == 8 && !published);
		++repairs;
		if (retireDuringRepair)
			lifecycle.Stop(*this);
	}

	void RegisterLibrary()
	{
		assert(lifecycle.IsActive() && repairs == 1 && enabled.size() == 8);
		++published;
	}

	void Release(std::vector<int>& resources)
	{
		assert(!lifecycle.IsActive());
		released.insert(released.end(), resources.rbegin(), resources.rend());
		resources.clear();
	}

	void DisableDetours() { Release(enabled); }
	void UnhookSteam() { Release(steamHooks); }
	void UnhookHLTV() { Release(hltvHooks); }
	void ReleaseResources()
	{
		assert(enabled.empty() && steamHooks.empty() && hltvHooks.empty());
		assert(calls.empty() || providerAvailable);
		Release(calls);
		Release(detours);
	}
	void ClearBindings()
	{
		assert(detours.empty() && calls.empty());
		Release(bindings);
	}
};

void TestLifecycleOrchestration()
{
	const std::vector<int> expectedAcquisition{
		1, 10, 11, 12, 13, 14, 15, 16, 17, 2, 20, 21, 22, 30, 31, 32, 33, 34, 35, 36, 40,
		50, 51, 52, 53, 54, 55, 56, 57,
	};
	const std::vector<int> expectedRelease{
		57, 56, 55, 54, 53, 52, 51, 50, 40, 36, 35, 34, 33, 32, 31, 30, 22, 21, 20,
		17, 16, 15, 14, 13, 12, 11, 10, 2, 1,
	};
	const int total = static_cast<int>(expectedAcquisition.size());
	for (int failAt = 0; failAt <= total; ++failAt) {
		sourcetv::RuntimeLifecycle lifecycle;
		LifecycleProvider runtime{lifecycle, failAt};
		assert(lifecycle.Prepare(runtime) == (failAt >= 9));
		assert(!lifecycle.IsActive() && !runtime.published);
		const bool started = lifecycle.Start(runtime);
		assert(started == (failAt == total));
		assert(lifecycle.IsActive() == started && runtime.published == started);
		assert(runtime.attempts == std::min(failAt + 1, total));
		assert(runtime.acquired == std::vector<int>(expectedAcquisition.begin(), expectedAcquisition.begin() + failAt));

		if (started) {
			auto stop = [&] { lifecycle.Stop(runtime); };
			lifecycle.DropSDKTools(true, stop, [] {});
			assert(lifecycle.IsActive());
			lifecycle.DropBinTools(stop, [&] { runtime.providerAvailable = false; });
			assert(!runtime.providerAvailable);
		}
		lifecycle.Stop(runtime);
		assert(!lifecycle.IsActive() && !lifecycle.IsPrepared());
		std::vector<int> expected;
		for (int resource : expectedRelease) {
			if (std::find(runtime.acquired.begin(), runtime.acquired.end(), resource) != runtime.acquired.end())
				expected.push_back(resource);
		}
		assert(runtime.released == expected);
	}

	sourcetv::RuntimeLifecycle lifecycle;
	LifecycleProvider runtime{lifecycle, -1};
	assert(lifecycle.Prepare(runtime));
	lifecycle.DropSDKTools(false, [&] { lifecycle.Stop(runtime); }, [] {});
	assert(!lifecycle.IsPrepared() && runtime.released.size() == 9);

	assert(lifecycle.Prepare(runtime));
	runtime.retireDuringRepair = true;
	assert(!lifecycle.Start(runtime));
	assert(!lifecycle.IsActive() && !runtime.published);
}

void TestRuntimeRebinding()
{
	constexpr int count = 7;
	for (int failAt = 0; failAt < count; ++failAt) {
		sourcetv::RuntimeResources<count> hooks;
		std::array<int, count> ids{};
		int previous = 1, replacement = 2;
		int* current = nullptr;
		const auto release = [&] {
			assert(!current);
			hooks.Reset();
		};
		releasedHooks.clear();
		assert(sourcetv::BindRuntimeServer(current, &previous, release, [&](int* server) {
			assert(!current && server == &previous);
			return hooks.Take<RemoveHook>(ids[0], 100);
		}));
		assert(current == &previous);
		assert(!sourcetv::BindRuntimeServer(current, &replacement, release, [&](int* server) {
			assert(!current && server == &replacement);
			assert(releasedHooks == std::vector<int>{100});
			for (int i = 0; i < count; ++i) {
				if (!hooks.Take<RemoveHook>(ids[i], i == failAt ? 0 : i + 1))
					return false;
			}
			return true;
		}));
		assert(!current && (ids == std::array<int, count>{}));
		assert(releasedHooks.size() == static_cast<size_t>(1 + failAt));
		for (int i = 0; i < failAt; ++i)
			assert(releasedHooks[i + 1] == failAt - i);

		assert(sourcetv::BindRuntimeServer(current, &replacement, release, [&](int*) {
			return hooks.Take<RemoveHook>(ids[0], 200);
		}));
		assert(current == &replacement);
		for (int i = 0; i < 2; ++i)
			assert(sourcetv::BindRuntimeServer(current, static_cast<int*>(nullptr), release,
				[](int*) { return false; }));
		assert(!current && releasedHooks.back() == 200);
		assert(releasedHooks.size() == static_cast<size_t>(2 + failAt));
	}
}

void TestSteamInitAndRestart()
{
	constexpr uint16_t sharedPort = 0xffff;
	struct Init {
		uint16_t port;
		bool share;
		bool success;
		uint16_t expectedQueryPort;
		int nextHook;
	};
	const Init cases[] = {
		{27016, false, true, 27015, 1},
		{65535, false, true, 65534, 2},
		{1, false, true, 0, 0}, // The original succeeds without a usable context.
		{0, false, true, 65535, 3},
		{0, true, false, sharedPort, 0},
		{27016, true, true, sharedPort, 4},
	};

	int hook = 0;
	sourcetv::RuntimeResources<1> owner;
	releasedHooks.clear();
	for (const auto& c : cases) {
		int originals = 0;
		int callbacks = 0;
		const bool result = sourcetv::InitSteamServer(c.port, c.share, sharedPort,
			[&](uint16_t queryPort) {
				assert(queryPort == c.expectedQueryPort);
				++originals;
				return c.success;
			},
			[&](bool result) {
				assert(++callbacks == 1 && originals == 1 && result == c.success);
				owner.Reset();
				if (result && c.nextHook)
					assert(owner.Take<RemoveHook>(hook, c.nextHook));
			});
		assert(result == c.success && originals == 1 && callbacks == 1);
		assert(hook == c.nextHook);
	}
	owner.Reset();
	owner.Reset();
	assert((releasedHooks == std::vector<int>{1, 2, 3, 4}));
}

} // namespace

int main()
{
	using Resources = sourcetv::RuntimeResources<1>;
	static_assert(!std::is_copy_constructible_v<Resources> && !std::is_move_constructible_v<Resources>);
	TestPartialHookAcquisitions();
	TestPreparationAndProviderDrop();
	TestSteamInitAndRestart();
	TestLifecycleOrchestration();
	TestRuntimeRebinding();
	std::puts("runtime_lifecycle: passed");
}
