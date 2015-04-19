#include <cstdio>
#include <iostream>
#include <math.h>
#include <cstring>
#include "rm.h"

using namespace std;

RM_Manager::RM_Manager    (PF_Manager &pfm): pfm(&pfm){}
RM_Manager::~RM_Manager   ()
{
	pfm = NULL;
}

RC RM_Manager::CreateFile (const char *fileName, int recordSize)
{
	// Check input parameters
	if (fileName == NULL){
		PrintError(RM_INPUTNULL);
		return RM_INPUTNULL;
	}
	size_t nameLen = strlen(fileName);
	if (nameLen > MAXNAME || nameLen == 0){
		PrintError(RM_FILENAMELEN);
		return RM_FILENAMELEN;
	}
	if (recordSize > PF_PAGE_SIZE || recordSize <= 0){
		PrintError(RM_RECORDSIZE);
		return RM_RECORDSIZE;
	}
	// End check input parameters.

	// Create file
	RC rc = pfm->CreateFile(fileName);
	if (rc != OK_RC){
		PrintError(rc);
		return rc;
	}

	// Create header page in file
	PF_FileHandle fileHandle = PF_FileHandle();
	rc = pfm->OpenFile(fileName, fileHandle);
	if (rc != OK_RC){
		PrintError(rc);
		return rc;
	}

	PF_PageHandle pageHandle = PF_PageHandle();
	rc = fileHandle.AllocatePage(pageHandle);
	if (rc != OK_RC){
		PrintError(rc);
		return rc;
	}

	// Write info to header page
	char *pData;
	rc = pageHandle.GetData(pData);
	if (rc != OK_RC){
		PrintError(rc);
		return rc;
	}

	char* ptr = pData;
	size_t tmp = recordSize;
	memcpy(ptr, &tmp, sizeof(size_t)); // recordSize
	ptr += sizeof(size_t);
	tmp = CalculateMaxSlots(recordSize) - 1; // 0-indexing
	memcpy(ptr, &tmp, sizeof(size_t)); // maxSlot
	ptr += sizeof(size_t);
	tmp = 0;
	memcpy(ptr, &tmp, sizeof(size_t)); // maxPage
	ptr += sizeof(size_t);
	int tmp2 = RM_PAGE_LIST_END;
	memcpy(ptr, &tmp2, sizeof(int)); // firstFreeSpace
	ptr += sizeof(int);
	tmp = sizeof(int) + ceil(CalculateMaxSlots(recordSize) / 8.0);
	memcpy(ptr, &tmp, sizeof(size_t)); // pageHeaderSize
	// End write info to header page.

	// Mark header page as dirty.
	rc = fileHandle.MarkDirty(0);
	if (rc != OK_RC){
		PrintError(rc);
		return rc;
	}

	//// Clean up
	pData = NULL;
	ptr = NULL;

	rc = fileHandle.UnpinPage(0);
	if (rc != OK_RC){
		PrintError(rc);
		return rc;
	}

	rc = pfm->CloseFile(fileHandle);
	if (rc != OK_RC){
		PrintError(rc);
		return rc;
	}

	return OK_RC;
}

RC RM_Manager::DestroyFile(const char *fileName)
{
	// Check input parameters
	if (fileName == NULL){
		PrintError(RM_INPUTNULL);
		return RM_INPUTNULL;
	}
	size_t nameLen = strlen(fileName);
	if (nameLen > MAXNAME || nameLen == 0){
		PrintError(RM_FILENAMELEN);
		return RM_FILENAMELEN;
	}
	// End check input parameters.

	RC rc = pfm->DestroyFile(fileName);
	if (rc != OK_RC){
		PrintError(rc);
		return rc;
	}

	return OK_RC;
}

RC RM_Manager::OpenFile   (const char *fileName, RM_FileHandle &fileHandle)
{
	// Check input parameters
	if (fileName == NULL){
		PrintError(RM_INPUTNULL);
		return RM_INPUTNULL;
	}
	size_t nameLen = strlen(fileName);
	if (nameLen > MAXNAME || nameLen == 0){
		PrintError(RM_FILENAMELEN);
		return RM_FILENAMELEN;
	}
	// End check input parameters.

	// Open file handle
	RC rc = pfm->OpenFile(fileName, fileHandle.pfFileHandle);
	if (rc != OK_RC){
		PrintError(rc);
		return rc;
	}

	// Initialize state
	fileHandle.open = true;
	fileHandle.modified = false;

	// Copy over file header info
	// Get header page handle
	PF_PageHandle pfPageHandle = PF_PageHandle();
	rc = fileHandle.pfFileHandle.GetThisPage(0, pfPageHandle);
	if (rc != OK_RC){
		PrintError(rc);
		return rc;
	}

	// Get header page data
	char *pData;
	rc = pfPageHandle.GetData(pData);
	if (rc != OK_RC){
		fileHandle.pfFileHandle.UnpinPage(0);
		PrintError(rc);
		return rc;
	}

	// Copy over header data
	char* ptr = pData;
	memcpy(&fileHandle.rmFileHeader.recordSize, ptr, sizeof(size_t));
	ptr += sizeof(size_t);
	memcpy(&fileHandle.rmFileHeader.maxSlot, ptr, sizeof(size_t));
	ptr += sizeof(size_t);
	memcpy(&fileHandle.rmFileHeader.maxPage, ptr, sizeof(size_t));
	ptr += sizeof(size_t);
	memcpy(&fileHandle.rmFileHeader.firstFreeSpace, ptr, sizeof(int));
	ptr += sizeof(int);
	memcpy(&fileHandle.rmFileHeader.pageHeaderSize, ptr, sizeof(size_t));

    cerr << "B. " <<fileHandle.rmFileHeader.recordSize << " " << fileHandle.rmFileHeader.maxPage << endl; //TODO
	
	// Clean up
	pData = NULL;
	ptr = NULL;
	rc = fileHandle.pfFileHandle.UnpinPage(0);
	if (rc != OK_RC){
		PrintError(rc);
		return rc;
	}

	return OK_RC;
}

RC RM_Manager::CloseFile  (RM_FileHandle &fileHandle)
{
	// Flush dirty pages
	RC rc = fileHandle.ForcePages();
	if (rc != OK_RC)
		return rc;
    cerr << "D. " << fileHandle.rmFileHeader.maxPage << endl; //TODO
        
	// Close file handle.
	rc = pfm->CloseFile(fileHandle.pfFileHandle);
	if (rc != OK_RC){
		PrintError(rc);
		return rc;
	}
	fileHandle.open = false;

	return OK_RC;
}

size_t RM_Manager::CalculateMaxSlots(int recordSize){
	return floor((PF_PAGE_SIZE - sizeof(int)) / (recordSize + 1/8.0));
}
