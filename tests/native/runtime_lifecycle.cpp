// SPDX-License-Identifier: GPL-3.0-only
#include "runtime_resources.h"
#include "steam_runtime.h"

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
	std::puts("runtime_lifecycle: passed");
}
