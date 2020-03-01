#ifndef VSS_DELTA_H
#define VSS_DELTA_H

#include <stdio.h>
#include <windows.h>

struct ops {
	void (*addFileHandle)(PUSN_RECORD record);
	void (*changeFileHandle)(PUSN_RECORD record);
	void (*deleteFileHandle)(PUSN_RECORD record);
};

void compareVolumeShadowCopies(HANDLE backupVolume, HANDLE modifiedVolume, struct ops *ops);
void getDeleteUsnRecord(HANDLE volume, PUSN_JOURNAL_DATA journal, void* buffer, struct ops *ops);
PUSN_JOURNAL_DATA getUsnJournalData(HANDLE volume, void* buffer, DWORD bufLen);
PUSN_RECORD getLastUsnRecord(HANDLE volume, DWORDLONG StartFileReferenceNumber, PUSN_JOURNAL_DATA journal, void* buffer, DWORD bufLen);
PUSN_RECORD getFirstUsnRecord(HANDLE volume, DWORDLONG StartFileReferenceNumber, PUSN_JOURNAL_DATA journal, void* buffer, DWORD bufLen);

#endif