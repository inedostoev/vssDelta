#include "VssDelta.h"

#define DEBUG 

#ifdef DEBUG
#define DEBUG_PRINT(fmt, args)			\
	fprintf(stderr, "DEBUG: %s:%d:%s(): " fmt, __FILE__, __LINE__, __func__, ##args)
#else
	#define DEBUG_PRINT(fmt, args)
#endif

#define BUFLEN 8192

void compareVolumeShadowCopies(HANDLE backupVolume, HANDLE modifiedVolume, struct ops *ops) {
	if (backupVolume == INVALID_HANDLE_VALUE || modifiedVolume == INVALID_HANDLE_VALUE) {
		DEBUG_PRINT("INVALID_HANDLE_VALUE\n", 1);
		return;
	}
	if (!ops) {
		DEBUG_PRINT("ops == NULL\n", 1);
		return;
	}

	void* journalBuffer = calloc(2, sizeof(USN_JOURNAL_DATA));
	if (!journalBuffer) {
		DEBUG_PRINT("journalBuffer == NULL\n", 1);
		return;
	}

	PUSN_JOURNAL_DATA bJournal = (USN_JOURNAL_DATA*)journalBuffer;
	PUSN_JOURNAL_DATA mJournal = bJournal + 1;
	bJournal = getUsnJournalData(backupVolume, bJournal, sizeof(USN_JOURNAL_DATA));
	mJournal = getUsnJournalData(modifiedVolume, mJournal, sizeof(USN_JOURNAL_DATA));

	if (bJournal->UsnJournalID != mJournal->UsnJournalID) {
		if (ops->overflowJournal)
			ops->overflowJournal();
	}

	if (bJournal->NextUsn < mJournal->AllocationDelta) {
		if (ops->overflowJournal)
			ops->overflowJournal();
	}

	
	DWORD bytecount = 1;
	DWORDLONG nextid = 0;
	USN_RECORD* record;
	USN_RECORD* recordend;

	MFT_ENUM_DATA_V0 mft_enum_data;
	mft_enum_data.StartFileReferenceNumber = 0;
	mft_enum_data.LowUsn = bJournal->NextUsn;
	mft_enum_data.HighUsn = bJournal->MaxUsn;
	PUSN_RECORD backupRecord;
	PUSN_RECORD firstUsnRecord;

	void* bBuf = calloc(2*BUFLEN, sizeof(CHAR));
	if (!bBuf) {
		DEBUG_PRINT("journalBuffer == NULL\n", 1);
		return;
	}
	DWORD bBufLen = BUFLEN;
	void* mBuf = (CHAR*)bBuf + BUFLEN;
	DWORD mBufLen = BUFLEN;
	
	/* for each new record in modifiedVolume */
	for (;;) {
		if (!DeviceIoControl(modifiedVolume,
							 FSCTL_ENUM_USN_DATA,
							 &mft_enum_data,
							 sizeof(mft_enum_data),
							 bBuf,
							 bBufLen,
							 &bytecount,
							 NULL))
		{
			if (GetLastError() == ERROR_HANDLE_EOF) {
				break;
			}
			DEBUG_PRINT("FSCTL_ENUM_USN_DATA: %u\n", GetLastError());
			return;
		}

		nextid = *((DWORDLONG*)(CHAR*)bBuf);

		record = (USN_RECORD*)((USN*)bBuf + 1);
		recordend = (USN_RECORD*)(((BYTE*)bBuf) + bytecount);
		
		while (record < recordend) {
			
			firstUsnRecord = getFirstUsnRecord(modifiedVolume, record->FileReferenceNumber, mJournal, mBuf, mBufLen);
			/* File ADD */
			if (firstUsnRecord->Usn > bJournal->NextUsn &&
				firstUsnRecord->FileReferenceNumber == record->FileReferenceNumber) {
				if (ops->addFileHandle)
					ops->addFileHandle(record);
			}
			/* File CHANGED */
			else if (firstUsnRecord->Usn < bJournal->NextUsn && 
				firstUsnRecord->FileReferenceNumber == record->FileReferenceNumber) {
				if (ops->changeFileHandle)
					ops->changeFileHandle(record);
			}
			/* File NOT CHANGED */
			else {}

			record = (USN_RECORD*)(((BYTE*)record) + record->RecordLength);
		}
		mft_enum_data.StartFileReferenceNumber = nextid;
	}

	/* File DELETE */
	getDeleteUsnRecord(modifiedVolume, bJournal, bBuf, ops);

	free(journalBuffer);
	free(bBuf);
	return;
}

void getDeleteUsnRecord(HANDLE volume, PUSN_JOURNAL_DATA journal, void* buffer, struct ops *ops) {
	DWORD dwBytes;
	DWORD dwRetBytes;
	PUSN_RECORD UsnRecord;

	READ_USN_JOURNAL_DATA_V0 ReadData;
	ReadData.StartUsn = journal->NextUsn;
	ReadData.ReasonMask = USN_REASON_FILE_DELETE;
	ReadData.ReturnOnlyOnClose = FALSE;
	ReadData.Timeout = 0;
	ReadData.BytesToWaitFor = 0;
	ReadData.UsnJournalID = journal->UsnJournalID;

	for (;;) {

		if (!DeviceIoControl(volume,
							 FSCTL_READ_USN_JOURNAL,
							 &ReadData,
							 sizeof(ReadData),
							 buffer,
							 BUFLEN,
							 &dwBytes,
							 NULL))
		{
			if (GetLastError() == ERROR_WRITE_PROTECT) return;
			printf("Read journal failed (%d)\n", GetLastError());
		}

		dwRetBytes = dwBytes - sizeof(USN);

		UsnRecord = (PUSN_RECORD)(((PUCHAR)buffer) + sizeof(USN));

		while (dwRetBytes > 0)
		{
			if (ops->deleteFileHandle)
				ops->deleteFileHandle(UsnRecord);

			dwRetBytes -= UsnRecord->RecordLength;
			UsnRecord = (PUSN_RECORD)(((PCHAR)UsnRecord) + UsnRecord->RecordLength);
		}
		ReadData.StartUsn = *(USN*)buffer;
	}
}

PUSN_JOURNAL_DATA getUsnJournalData(HANDLE volume, void *buffer, DWORD bufLen) {
	DWORD bytecount = 1;
	USN_JOURNAL_DATA* journal;

	if (!DeviceIoControl(volume,
						 FSCTL_QUERY_USN_JOURNAL,
						 NULL,
						 0,
						 buffer,
						 bufLen,
						 &bytecount,
						 NULL))
	{
		DEBUG_PRINT("FSCTL_QUERY_USN_JOURNAL: %u\n", GetLastError());
		return NULL;
	}

	journal = (USN_JOURNAL_DATA*)buffer;

#ifdef DUMP
	printf("=============================================\n");
	printf("UsnJournalID: %lu\n", journal->UsnJournalID);
	printf("FirstUsn: %lu\n", journal->FirstUsn);
	printf("NextUsn: %lu\n", journal->NextUsn);
	printf("LowestValidUsn: %lu\n", journal->LowestValidUsn);
	printf("MaxUsn: %lu\n", journal->MaxUsn);
	printf("MaximumSize: %lu\n", journal->MaximumSize);
	printf("AllocationDelta: %lu\n", journal->AllocationDelta);
#endif

	return journal;
}
 
PUSN_RECORD getLastUsnRecord(HANDLE volume, DWORDLONG StartFileReferenceNumber, PUSN_JOURNAL_DATA journal, void* buffer, DWORD bufLen) {
	DWORD bytecount = 1;
	DWORDLONG nextid = 0;
	USN_RECORD *record;
	USN_RECORD *recordend;
	
	MFT_ENUM_DATA_V0 mft_enum_data;
	mft_enum_data.StartFileReferenceNumber = StartFileReferenceNumber;
	mft_enum_data.LowUsn = 0;
	mft_enum_data.HighUsn = journal->MaxUsn;

	if (!DeviceIoControl(volume,
						 FSCTL_ENUM_USN_DATA,
						 &mft_enum_data,
						 sizeof(mft_enum_data),
						 buffer,
					     bufLen,
						 &bytecount,
						 NULL))
	{
		DEBUG_PRINT("FSCTL_ENUM_USN_DATA: %u\n", GetLastError());
		return NULL;
	}
	record = (USN_RECORD*)((USN*)buffer + 1);

#ifdef DUMP
	WCHAR* filename;
	WCHAR* filenameend;

	printf("=================================================================\n");
	printf("RecordLength: %u\n", record->RecordLength);
	printf("MajorVersion: %u\n", (DWORD)record->MajorVersion);
	printf("MinorVersion: %u\n", (DWORD)record->MinorVersion);
	printf("FileReferenceNumber: 0x%I64x\n", record->FileReferenceNumber);
	printf("ParentFRN: %lu\n", record->ParentFileReferenceNumber);
	printf("USN: %I64x\n", record->Usn);
	printf("Timestamp: %lu\n", record->TimeStamp);
	printf("Reason: %u\n", record->Reason);
	printf("SourceInfo: %u\n", record->SourceInfo);
	printf("SecurityId: %u\n", record->SecurityId);
	printf("FileAttributes: %x\n", record->FileAttributes);
	printf("FileNameLength: %u\n", (DWORD)record->FileNameLength);

	filename = (WCHAR*)(((BYTE*)record) + record->FileNameOffset);
	filenameend = (WCHAR*)(((BYTE*)record) + record->FileNameOffset + record->FileNameLength);

	printf("FileName: %.*ls\n", filenameend - filename, filename);
#endif

	return record;
}

PUSN_RECORD getFirstUsnRecord(HANDLE volume, DWORDLONG StartFileReferenceNumber, PUSN_JOURNAL_DATA journal, void* buffer, DWORD bufLen) {
	DWORD dwBytes;
	DWORD dwRetBytes;
	PUSN_RECORD UsnRecord;
	
	READ_USN_JOURNAL_DATA_V0 ReadData;
	ReadData.StartUsn = 0;
	ReadData.ReasonMask = 0xFFFFFFFF;
	ReadData.ReturnOnlyOnClose = FALSE;
	ReadData.Timeout = 0;
	ReadData.BytesToWaitFor = 0;
	ReadData.UsnJournalID = journal->UsnJournalID;

	for (;;) {
		
		if (!DeviceIoControl(volume,
							 FSCTL_READ_USN_JOURNAL,
							 &ReadData,
							 sizeof(ReadData),
							 buffer,
							 bufLen,
							 &dwBytes,
							 NULL))
		{
			if (GetLastError() == ERROR_WRITE_PROTECT) {
				return NULL;
			} 
			else {
				printf("Read journal failed (%d)\n", GetLastError());
				return NULL;
			}
		}

		dwRetBytes = dwBytes - sizeof(USN);
		UsnRecord = (PUSN_RECORD)(((PUCHAR)buffer) + sizeof(USN));

		while (dwRetBytes > 0)
		{
			if (UsnRecord->FileReferenceNumber == StartFileReferenceNumber) {

#ifdef DUMP
				WCHAR* filename;
				WCHAR* filenameend;
				
				printf("=================================================\n");
				printf("USN: %lu\n", UsnRecord->Usn);
				printf("Reason: 0x%x\n", UsnRecord->Reason);

				filename = (WCHAR*)(((BYTE*)UsnRecord) + UsnRecord->FileNameOffset);
				filenameend = (WCHAR*)(((BYTE*)UsnRecord) + UsnRecord->FileNameOffset + UsnRecord->FileNameLength);
				printf("FileName: %.*ls\n", filenameend - filename, filename);
				printf("\n");
#endif
				return UsnRecord;
			}

			dwRetBytes -= UsnRecord->RecordLength;
			UsnRecord = (PUSN_RECORD)(((PCHAR)UsnRecord) + UsnRecord->RecordLength);
		}
		ReadData.StartUsn = *(USN*)buffer;
		if (ReadData.StartUsn == journal->NextUsn) {
			return NULL;
		}
	}
} 