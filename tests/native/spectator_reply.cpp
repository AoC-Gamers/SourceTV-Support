// SPDX-License-Identifier: GPL-3.0-only
#include "hooks/spectator_reply.h"

#include <cassert>
#include <cstdio>
#include <string>
#include <vector>

struct ReplyFields
{
	std::vector<std::string> fields;

	void WriteLong(int value) { fields.push_back("long:" + std::to_string(value)); }
	void WriteByte(int value) { fields.push_back("byte:" + std::to_string(value)); }
	void WriteShort(int value) { fields.push_back("short:" + std::to_string(value)); }
	void WriteLongLong(long long value) { fields.push_back("longlong:" + std::to_string(value)); }
	void WriteString(const char* value) { fields.push_back(std::string("string:") + value); }
};

int main()
{
	ReplyFields challenge;
	sourcetv::WriteSpectatorChallenge(challenge, 42, 3, "spectator", 10097);
	assert((challenge.fields == std::vector<std::string>{
		"long:-1", "byte:65", "long:42", "long:3", "short:1", "longlong:0",
		"byte:0", "string:spectator", "long:10097", "string:", "byte:0", "longlong:0",
	}));

	ReplyFields rejection;
	sourcetv::WriteSplitscreenRejection(rejection);
	assert((rejection.fields == std::vector<std::string>{
		"long:-1", "byte:57", "string:Splitscreen is not allowed in HLTV\n",
	}));
	std::puts("spectator_reply: challenge fields and splitscreen rejection validated");
}
