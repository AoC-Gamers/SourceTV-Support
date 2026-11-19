// SPDX-License-Identifier: GPL-3.0-only
#pragma once

namespace sourcetv {

template <typename Writer>
void WriteSpectatorChallenge(Writer& message, int challenge, int authentication,
	const char* context, int engineBuild)
{
	message.WriteLong(-1); // Connectionless header.
	message.WriteByte('A');
	message.WriteLong(challenge);
	message.WriteLong(authentication);
	message.WriteShort(1);
	message.WriteLongLong(0LL);
	message.WriteByte(0);
	message.WriteString(context);
	// A spectator should enter SourceTV without joining the game's lobby.
	message.WriteLong(engineBuild);
	message.WriteString("");
	message.WriteByte(0);
	message.WriteLongLong(0LL);
}

template <typename Writer>
void WriteSplitscreenRejection(Writer& message)
{
	message.WriteLong(-1);
	message.WriteByte('9');
	message.WriteString("Splitscreen is not allowed in HLTV\n");
}

} // namespace sourcetv
