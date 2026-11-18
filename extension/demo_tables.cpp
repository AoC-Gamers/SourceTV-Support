// SPDX-License-Identifier: GPL-3.0-only
// Extracted from SourceTV-Support by Evgeniy Kazakov and contributors.
#include "extension.h"
#include "wrappers.h"
#include "demo_payload.h"

#include <vector>

extern INetworkStringTableContainer* networkStringTableContainerServer;

// Need this for demofile.h to not link tier2
bool CUtlStreamBuffer::IsOpen() const
{
	return m_hFileHandle != NULL;
}

namespace demo = sourcetv::demo;

static void LogRecordFailure(const char* record, demo::Result result)
{
	if (result == demo::Result::Ok)
		return;

	const char* reason = "recorder or source unavailable";
	if (result == demo::Result::Overflow)
		reason = "payload exceeds demo buffer";
	else if (result == demo::Result::InvalidData)
		reason = "invalid table data";
	smutils->LogError(myself, "Unable to record %s: %s", record, reason);
}

void SMExtension::Handler_CHLTVDemoRecorder_RecordStringTables()
{
	CHLTVDemoRecorder* recorder = META_IFACEPTR(CHLTVDemoRecorder);
	std::vector<byte> storage(DEMO_RECORD_BUFFER_SIZE);
	bf_write buffer(storage.data(), storage.size());
	buffer.SetAssertOnOverflow(false);

	const auto result = demo::CommitRecord(buffer,
		[](bf_write& payload) {
			if (!networkStringTableContainerServer)
				return demo::Result::Unavailable;
			return demo::StringTables(*networkStringTableContainerServer, payload);
		},
		[recorder](bf_write& payload) {
			CDemoFile* file = recorder ? recorder->GetDemoFile() : nullptr;
			if (!file || !file->IsOpen())
				return false;
			file->WriteStringTables(&payload, recorder->GetRecordingTick());
			return true;
		});
	LogRecordFailure("string tables", result);
	RETURN_META(MRES_SUPERCEDE);
}

void SMExtension::Handler_CHLTVDemoRecorder_RecordServerClasses(ServerClass* classes)
{
	CHLTVDemoRecorder* recorder = META_IFACEPTR(CHLTVDemoRecorder);
	std::vector<byte> storage(DEMO_RECORD_BUFFER_SIZE);
	bf_write buffer(storage.data(), storage.size());
	buffer.SetAssertOnOverflow(false);

	const auto result = demo::CommitRecord(buffer,
		[classes](bf_write& payload) {
			if (!pfn_DataTable_WriteSendTablesBuffer)
				return demo::Result::Unavailable;
			return demo::ServerClasses(classes, payload, [](ServerClass* first, bf_write& output) {
				InvokeDataTable_WriteSendTablesBuffer(first, &output);
			});
		},
		[recorder](bf_write& payload) {
			CDemoFile* file = recorder ? recorder->GetDemoFile() : nullptr;
			if (!file || !file->IsOpen())
				return false;
			file->WriteNetworkDataTables(&payload, recorder->GetRecordingTick());
			return true;
		});
	LogRecordFailure("server classes", result);
	RETURN_META(MRES_SUPERCEDE);
}
