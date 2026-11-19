// SPDX-License-Identifier: GPL-3.0-only
#include "engine/engine_binding.h"

#include <sourcehook_impl.h>

#include <cassert>
#include <cstdio>
#include <type_traits>

using namespace SourceHook;

ISourceHook* g_SHPtr = nullptr;
int g_PLID = 1;

namespace {

struct Address { unsigned marker = 0x12345678; };
struct Reader { int consumed = 0; };

// Independent provider with two distinguishable slots of the same ABI shape.
struct Provider
{
	virtual void Other(Address&, Reader& reader) { reader.consumed -= 5; }
	virtual void Reply(Address& address, Reader& reader)
	{
		assert(address.marker == 0x12345678);
		reader.consumed += 7;
	}
};

SH_DECL_MANUALHOOK2_void(TestReply, 0, 0, 0, Address&, Reader&);

Provider* seenReceiver;
int hookCalls;
void OnReply(Address& address, Reader& reader)
{
	seenReceiver = META_IFACEPTR(Provider);
	assert(address.marker == 0x12345678 && reader.consumed == 0);
	++hookCalls;
	RETURN_META(MRES_IGNORED);
}

__attribute__((noinline)) void Dispatch(Provider& receiver, Address& address, Reader& reader)
{
	receiver.Reply(address, reader);
}

void CheckHook(sourcetv::abi::VtableSlot slot, bool expected)
{
	Provider receiver;
	Address address;
	Reader reader;
	hookCalls = 0;
	seenReceiver = nullptr;
	SH_MANUALHOOK_RECONFIGURE(TestReply, slot.Index(), 0, 0);
	const int id = SH_ADD_MANUALHOOK(TestReply, &receiver, SH_STATIC(OnReply), false);
	assert(id != 0);
	Dispatch(receiver, address, reader);
	assert(reader.consumed == 7);
	assert(hookCalls == (expected ? 1 : 0));
	assert(seenReceiver == (expected ? &receiver : nullptr));
	assert(SH_REMOVE_HOOK_ID(id));

	reader.consumed = 0;
	Dispatch(receiver, address, reader);
	assert(reader.consumed == 7 && hookCalls == (expected ? 1 : 0));
}

} // namespace

int main()
{
	using namespace sourcetv::abi;
	static_assert(sizeof(void*) == 4);
	static_assert(!std::is_convertible_v<FieldOffset, VtableSlot>);
	static_assert(!std::is_convertible_v<BaseAdjustment, FieldOffset>);

	SourceHook::Impl::CSourceHookImpl hook;
	g_SHPtr = &hook;
	CheckHook(*VtableSlot::FromGamedata(1), true);
	CheckHook(*VtableSlot::FromGamedata(0), false);
	std::puts("engine_binding: SourceHook x86 slot, receiver and original validated");
}
