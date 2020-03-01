#define _CRT_SECURE_NO_WARNINGS

#include <stdio.h>
#include <wchar.h>

#include <windows.h>
#include <winbase.h>

#include <Vss.h>
#include <VsWriter.h>
#include <VsBackup.h>

#include "VssDelta.h"


#define IS_OK(str)       \
if (hr != S_OK) {		 \
	printf(str);		 \
	abort();		     \
}

#define DEBUG 

#ifdef DEBUG
#define DEBUG_PRINT(fmt, args)			\
	fprintf(stderr, "DEBUG: %s:%d:%s(): " fmt, __FILE__, __LINE__, __func__, ##args)
#else
#define DEBUG_PRINT(fmt, args)
#endif


typedef HRESULT(STDAPICALLTYPE* _CreateVssBackupComponentsInternal)(
	__out IVssBackupComponents** ppBackup
	);

typedef void (APIENTRY* _VssFreeSnapshotPropertiesInternal)(
	__in VSS_SNAPSHOT_PROP* pProp
	);

static _CreateVssBackupComponentsInternal CreateVssBackupComponentsInternal_I;
static _VssFreeSnapshotPropertiesInternal VssFreeSnapshotPropertiesInternal_I;

int initVSSmodule() {
	HMODULE vssapiBase = LoadLibrary(L"vssapi.dll");

	if (vssapiBase) {
		CreateVssBackupComponentsInternal_I = (_CreateVssBackupComponentsInternal)GetProcAddress(vssapiBase, "CreateVssBackupComponentsInternal");
		VssFreeSnapshotPropertiesInternal_I = (_VssFreeSnapshotPropertiesInternal)GetProcAddress(vssapiBase, "VssFreeSnapshotPropertiesInternal");
	}
	if (!CreateVssBackupComponentsInternal_I || !VssFreeSnapshotPropertiesInternal_I)
		abort();

	if (CoInitialize(NULL) != S_OK) {
		printf("CoInitialize failed!\n");
		return 1;
	}
	return 0;
}

WCHAR* createSnapshot(IVssBackupComponents *ppBackup) {

	HRESULT hr;
	hr = ppBackup->InitializeForBackup();
	IS_OK("Failed at InitializeForBackup Stage\n")
	
	hr = ppBackup->SetBackupState(FALSE, FALSE, VSS_BT_INCREMENTAL);
	IS_OK("Failed at SetBackup Stage\n")

	hr = ppBackup->SetContext(VSS_CTX_FILE_SHARE_BACKUP);
	IS_OK("Failed at SetContext Stage\n")

	VSS_ID snapshotSetId;
	hr = ppBackup->StartSnapshotSet(&snapshotSetId);
	IS_OK("Failed at StartSnapshotSet stage\n")

	VSS_ID snapshotId;
	WCHAR driveName[MAX_PATH] = TEXT("c:\\");
	hr = ppBackup->AddToSnapshotSet(driveName, GUID_NULL, &snapshotId);
	IS_OK("Failed at AddToSnapshotSet Stage\n")

	IVssAsync* async;
	hr = ppBackup->DoSnapshotSet(&async);
	IS_OK("Failed at DoSnapshotSet stage\n")
	
	hr = async->Wait();
	async->Release();
	IS_OK("Failed at DoSnapshotSet stage in wait\n")

	VSS_SNAPSHOT_PROP prop;
	hr = ppBackup->GetSnapshotProperties(snapshotId, &prop);
	IS_OK("Failed at GetSnapshotProperties\n")

	int cqt = wcslen(prop.m_pwszSnapshotDeviceObject);
	WCHAR* str = (WCHAR*)calloc(cqt + 1, sizeof(WCHAR));
	if (!str) printf("no mem\n");

	wcsncpy(str, prop.m_pwszSnapshotDeviceObject, cqt);
	str[cqt] = L'\0';

	VssFreeSnapshotPropertiesInternal_I(&prop);

	return str;
}

void deleteSnapshot(IVssBackupComponents *ppBackup) {
	if (ppBackup) ppBackup->Release();
}

void makeChanges(const char *str) {

	char* buffer;
	if ((buffer = (char*)calloc(1024 * 8 + 1, sizeof(char))) == NULL) {
		printf("Can not allocate memory\n");
	}
	memset(buffer, 31, 8 * 1024);
	buffer[8 * 1024] = '\0';

	FILE* fd;

	if ((fd = fopen(str, "a+")) == NULL) {
		printf("Can not open file\n");
	}
	fprintf(fd, "%s", buffer);
	fflush(fd);
	fclose(fd);
}

HANDLE openFile(WCHAR* path) {
	HANDLE volume = CreateFile(path,
		GENERIC_READ,
		FILE_SHARE_DELETE | FILE_SHARE_READ | FILE_SHARE_WRITE,
		NULL,
		OPEN_ALWAYS,
		FILE_FLAG_NO_BUFFERING,
		NULL);

	if (volume == INVALID_HANDLE_VALUE) {
		DEBUG_PRINT("CreateFile: % u\n", GetLastError());
		abort();
	}

	return volume;
}


void addfile(PUSN_RECORD record) {
	printf("==========================ADD=======================\n");
	WCHAR* filename;
	WCHAR* filenameend;

	filename = (WCHAR*)(((BYTE*)record) + record->FileNameOffset);
	filenameend = (WCHAR*)(((BYTE*)record) + record->FileNameOffset + record->FileNameLength);

	printf("FileName: %.*ls\n", filenameend - filename, filename);
}

void changefile(PUSN_RECORD record) {
	WCHAR* filename;
	WCHAR* filenameend;

	printf("===========================Changed======================================\n");

	filename = (WCHAR*)(((BYTE*)record) + record->FileNameOffset);
	filenameend = (WCHAR*)(((BYTE*)record) + record->FileNameOffset + record->FileNameLength);

	printf("FileName: %.*ls\n", filenameend - filename, filename);
}

void deletefile(PUSN_RECORD record) {
	WCHAR* filename;
	WCHAR* filenameend;

	printf("========================DELETE=========================\n");
	filename = (WCHAR*)(((BYTE*)record) + record->FileNameOffset);
	filenameend = (WCHAR*)(((BYTE*)record) + record->FileNameOffset + record->FileNameLength);
	printf("FileName: %.*ls\n", filenameend - filename, filename);
}

void overflowJournal() {
	printf("Journal overflow\n");
}


int main() {
	IVssBackupComponents* ppBackup1;
	IVssBackupComponents* ppBackup2;
	WCHAR* deviceObjName1;
	WCHAR* deviceObjName2;

	initVSSmodule();

	makeChanges("ChangeFile.c");
	makeChanges("NoChangeFile.c");
	makeChanges("deleteFile.c");

	CreateVssBackupComponentsInternal_I(&ppBackup1);
	deviceObjName1 = createSnapshot(ppBackup1);
	wprintf(L"%s\n", deviceObjName1);

	system("del deleteFile.c");
	makeChanges("ChangeFile.c");
	makeChanges("addFile.c");

	CreateVssBackupComponentsInternal_I(&ppBackup2);
	deviceObjName2 = createSnapshot(ppBackup2);
	wprintf(L"%s\n", deviceObjName2);

	system("pause");	

	HANDLE backupVolume = openFile(deviceObjName1);
	HANDLE modifiedVolume = openFile(deviceObjName2);

	struct ops *ops = (struct ops*)calloc(1, sizeof(struct ops));
	ops->addFileHandle = addfile;
	ops->changeFileHandle = changefile;
	ops->deleteFileHandle = deletefile;
	ops->overflowJournal = overflowJournal;


	compareVolumeShadowCopies(backupVolume, modifiedVolume, ops);
	system("del addFile.c");

	free(deviceObjName1);
	free(deviceObjName2);
	
	deleteSnapshot(ppBackup1);
	deleteSnapshot(ppBackup2);
	
	return 0;
}