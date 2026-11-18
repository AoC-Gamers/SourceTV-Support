// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <cstddef>
#include <cstring>

namespace sourcetv::demo {

enum class Result { Ok, Overflow, InvalidData, Unavailable };

template <class Buffer>
bool WriteString(Buffer& buffer, const char* value)
{
	const int bytes = buffer.GetNumBitsLeft() / 8;
	if (bytes <= 0 || std::strlen(value) >= static_cast<size_t>(bytes))
		return false;
	return buffer.WriteString(value);
}

template <class Source, class Buffer>
Result StringTables(Source& source, Buffer& buffer)
{
	const int tables = source.GetNumTables();
	if (tables < 0 || tables > 255)
		return Result::InvalidData;
	if (buffer.GetNumBitsLeft() < 8)
		return Result::Overflow;
	buffer.WriteByte(tables);

	for (int i = 0; i < tables; ++i) {
		auto* table = source.GetTable(i);
		if (!table)
			return Result::Unavailable;
		const char* tableName = table->GetTableName();
		const int entries = table->GetNumStrings();
		if (!tableName || entries < 0 || entries > 65535)
			return Result::InvalidData;
		if (!WriteString(buffer, tableName) || buffer.GetNumBitsLeft() < 16)
			return Result::Overflow;
		buffer.WriteWord(entries);

		for (int j = 0; j < entries; ++j) {
			const char* value = table->GetString(j);
			int length = 0;
			const void* data = table->GetStringUserData(j, &length);
			if (!value || length < 0 || length > 32767 || (length > 0 && !data))
				return Result::InvalidData;
			if (!WriteString(buffer, value) || buffer.GetNumBitsLeft() < 1 + (length ? 16 + 8 * length : 0))
				return Result::Overflow;
			buffer.WriteOneBit(length != 0);
			if (length) {
				buffer.WriteShort(length);
				buffer.WriteBytes(data, length);
			}
		}

		if (buffer.GetNumBitsLeft() < 1)
			return Result::Overflow;
		buffer.WriteOneBit(0); // No client-side items in the server.
	}
	return Result::Ok;
}

template <class Class, class Buffer, class SendTables>
Result ServerClasses(Class* first, Buffer& buffer, SendTables sendTables)
{
	int count = 0;
	for (auto* entry = first; entry; entry = entry->m_pNext) {
		// The count cap also rejects cyclic lists.
		if (++count > 32767 || entry->m_ClassID < 0 || entry->m_ClassID > 32767
			|| !entry->m_pNetworkName || !entry->m_pTable || !entry->m_pTable->GetName())
			return Result::InvalidData;
	}

	sendTables(first, buffer);
	if (buffer.IsOverflowed() || buffer.GetNumBitsLeft() < 16)
		return Result::Overflow;
	buffer.WriteShort(count);

	for (auto* entry = first; entry; entry = entry->m_pNext) {
		if (buffer.GetNumBitsLeft() < 16)
			return Result::Overflow;
		buffer.WriteShort(entry->m_ClassID);
		if (!WriteString(buffer, entry->m_pNetworkName) || !WriteString(buffer, entry->m_pTable->GetName()))
			return Result::Overflow;
	}
	return Result::Ok;
}

template <class Buffer, class Build, class Writer>
Result CommitRecord(Buffer& buffer, Build build, Writer writer)
{
	const Result result = build(buffer);
	if (result != Result::Ok)
		return result;
	if (buffer.IsOverflowed())
		return Result::Overflow;
	return writer(buffer) ? Result::Ok : Result::Unavailable;
}

} // namespace sourcetv::demo
