// SPDX-License-Identifier: GPL-3.0-only
#include "demo_payload.h"

#include <climits>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace demo = sourcetv::demo;

static void Check(bool condition, const char* description)
{
	if (!condition) {
		std::fprintf(stderr, "demo_payload: %s\n", description);
		std::exit(1);
	}
}

struct BitBuffer {
	explicit BitBuffer(int capacity) : capacity(capacity) {}

	int capacity;
	int used = 0;
	bool overflow = false;
	std::vector<unsigned char> bytes;

	int GetNumBitsLeft() const { return capacity - used; }
	bool IsOverflowed() const { return overflow; }

	void Put(unsigned value, int count)
	{
		if (count > GetNumBitsLeft()) {
			overflow = true;
			return;
		}
		for (int i = 0; i < count; ++i, ++used) {
			if (used % 8 == 0)
				bytes.push_back(0);
			bytes.back() |= ((value >> i) & 1U) << (used % 8);
		}
	}
	void WriteByte(int value) { Put(value, 8); }
	void WriteWord(int value) { Put(value, 16); }
	void WriteShort(int value) { Put(value, 16); }
	void WriteOneBit(int value) { Put(value, 1); }
	bool WriteString(const char* value)
	{
		for (; !overflow; ++value) {
			WriteByte(*value);
			if (!*value)
				break;
		}
		return !overflow;
	}
	void WriteBytes(const void* bytes, int count)
	{
		auto* data = static_cast<const unsigned char*>(bytes);
		for (int i = 0; i < count; ++i)
			WriteByte(data[i]);
	}
};

struct WireReader {
	const std::vector<unsigned char>& bytes;
	size_t bit = 0;
	unsigned Read(int width)
	{
		unsigned value = 0;
		unsigned power = 1;
		for (int i = 0; i < width; ++i, ++bit, power *= 2) {
			if ((bytes.at(bit / 8) & (1U << (bit % 8))) != 0)
				value += power;
		}
		return value;
	}
	std::string String()
	{
		std::string value;
		while (const unsigned ch = Read(8))
			value += static_cast<char>(ch);
		return value;
	}
};

struct StringTable {
	const char* name = "t";
	int count = 2;
	const char* values[2] = {"a", "b"};
	int length = 2;
	const unsigned char payload[2] = {0xaa, 0x55};
	const void* data = payload;

	const char* GetTableName() const { return name; }
	int GetNumStrings() const { return count; }
	const char* GetString(int n) const { return values[n]; }
	const void* GetStringUserData(int n, int* bytes) const
	{
		*bytes = n == 1 ? length : 0;
		return n == 1 ? data : nullptr;
	}
};

struct Source {
	int count = 1;
	StringTable* table;
	int GetNumTables() const { return count; }
	StringTable* GetTable(int) const { return table; }
};

struct SendTable {
	const char* name;
	const char* GetName() const { return name; }
};
struct ServerClass {
	int m_ClassID;
	const char* m_pNetworkName;
	SendTable* m_pTable;
	ServerClass* m_pNext;
};

int main()
{
	using demo::Result;
	int writes = 0;
	auto writer = [&](const BitBuffer&) { ++writes; return true; };
	StringTable table;
	Source source{1, &table};
	auto buildTables = [&](BitBuffer& buffer) { return demo::StringTables(source, buffer); };

	BitBuffer exact(107);
	Check(demo::CommitRecord(exact, buildTables, writer) == Result::Ok && writes == 1, "exact capacity commits once");
	WireReader reader{exact.bytes};
	Check(reader.Read(8) == 1 && reader.String() == "t" && reader.Read(16) == 2, "table count, name and entries");
	Check(reader.String() == "a" && !reader.Read(1), "string without userdata");
	Check(reader.String() == "b" && reader.Read(1) == 1, "string with userdata");
	Check(reader.Read(16) == 2 && reader.Read(8) == 0xaa && reader.Read(8) == 0x55, "userdata length and bytes");
	Check(!reader.Read(1) && reader.bit == 107, "client-side flag ends record");

	BitBuffer shortBuffer(106);
	Check(demo::CommitRecord(shortBuffer, buildTables, writer) == Result::Overflow && writes == 1, "overflow cannot commit");
	source.count = 256;
	Check(demo::CommitRecord(shortBuffer, buildTables, writer) == Result::InvalidData && writes == 1, "table count cannot narrow");
	source.count = 0;
	BitBuffer empty(8);
	Check(demo::CommitRecord(empty, buildTables, writer) == Result::Ok && empty.bytes == std::vector<unsigned char>{0}, "empty tables payload");
	source.count = 1;
	table.count = 65536;
	BitBuffer invalid(512);
	Check(demo::CommitRecord(invalid, buildTables, writer) == Result::InvalidData, "entry count cannot narrow");
	table.count = 2;
	table.length = 32768;
	Check(demo::CommitRecord(invalid, buildTables, writer) == Result::InvalidData, "userdata length cannot narrow");
	table.length = 2;
	table.data = nullptr;
	Check(demo::CommitRecord(invalid, buildTables, writer) == Result::InvalidData && writes == 2, "null userdata cannot commit");
	int sends = 0;
	auto send = [&](ServerClass*, BitBuffer& buffer) { ++sends; buffer.WriteByte(0x9a); };
	SendTable sendTable{"DT"};
	ServerClass entry{14, "Class", &sendTable, nullptr};
	auto buildClasses = [&](BitBuffer& buf) { return demo::ServerClasses(&entry, buf, send); };
	BitBuffer classes(112);
	Check(demo::CommitRecord(classes, buildClasses, writer) == Result::Ok && writes == 3, "class record commits once");
	WireReader classReader{classes.bytes};
	Check(classReader.Read(8) == 0x9a && classReader.Read(16) == 1 && classReader.Read(16) == 14 &&
		classReader.String() == "Class" && classReader.String() == "DT" && classReader.bit == 112,
		"sendtables precede class metadata");
	BitBuffer noCapacity(7);
	Check(demo::CommitRecord(noCapacity, buildClasses, writer) == Result::Overflow && writes == 3, "opaque sendtable overflow cannot commit");
	entry.m_ClassID = 32768;
	Check(demo::CommitRecord(noCapacity, buildClasses, writer) == Result::InvalidData && sends == 2, "class ID checked before sendtables");
	entry.m_ClassID = 14;
	entry.m_pNext = &entry;
	Check(demo::CommitRecord(noCapacity, buildClasses, writer) == Result::InvalidData && sends == 2, "cyclic class list rejected");
	BitBuffer emptyClasses(24);
	auto buildEmpty = [&](BitBuffer& buf) { return demo::ServerClasses(static_cast<ServerClass*>(nullptr), buf, send); };
	Check(demo::CommitRecord(emptyClasses, buildEmpty, writer) == Result::Ok &&
		emptyClasses.bytes == std::vector<unsigned char>({0x9a, 0, 0}), "empty class list");
	BitBuffer closed(24);
	Check(demo::CommitRecord(closed, buildEmpty, [](const BitBuffer&) { return false; }) == Result::Unavailable,
		"writer unavailable");
	std::printf("demo_payload: checks passed (%zu-bit)\n", sizeof(void*) * CHAR_BIT);
}
