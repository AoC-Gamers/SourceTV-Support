// SPDX-License-Identifier: GPL-3.0-only
#pragma once

class ServerClass;
class bf_write;

extern void* pfn_DataTable_WriteSendTablesBuffer;
void InvokeDataTable_WriteSendTablesBuffer(ServerClass* pClasses, bf_write* buf);
