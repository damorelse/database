#include <cstring>
#include <sstream>
#include "ix.h"

using namespace std;

IX_Manager::IX_Manager(PF_Manager &pfm): pfManager(&pfm)
{}
IX_Manager::~IX_Manager()
{
	pfManager = NULL;
}

// Create a new Index
RC IX_Manager::CreateIndex(const char *fileName, int indexNo,
                AttrType attrType, int attrLength)
{
	// Check input
	// Check filename is not null
	if (!fileName){
		PrintError(IX_NULLINPUT);
		return IX_NULLINPUT;
	}

	// Check filename does not exceed max relation name size and is not empty
	size_t nameLen = strlen(fileName);
	if (nameLen > MAXNAME || nameLen == 0){
		PrintError(IX_FILENAMELEN);
		return IX_FILENAMELEN;
	}

	// Given can assume indexNo is unique and positive.

	// Check attribute type is one of the three allowed
	if (attrType != INT && attrType != FLOAT && attrType != STRING){
		PrintError(IX_INVALIDENUM);
		return IX_INVALIDENUM;
	}
	// Check string attribute length is greater than 0 and less than 255 bytes
	if (attrType == STRING && (attrLength > MAXSTRINGLEN || attrLength < 1)){
		PrintError(IX_STRLEN);
		return IX_STRLEN;
	}
	// Check int or float attribute length is exactly 4 bytes
	if (attrType != STRING && attrLength != 4){
		PrintError(IX_NUMLEN);
		return IX_NUMLEN;
	}
	// End check input

	// Create file
	RC rc = pfManager->CreateFile(GetIndexFileName(fileName, indexNo));
	if (rc != OK_RC){
		PrintError(rc);
		return rc;
	}

	// Create header page in file
	PF_FileHandle fileHandle = PF_FileHandle();
	rc = pfManager->OpenFile(GetIndexFileName(fileName, indexNo), fileHandle);
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

	// Get header page info
	char *pData;
	rc = pageHandle.GetData(pData);
	if (rc != OK_RC){
		PrintError(rc);
		return rc;
	}

	// Write info to header page
	char* ptr = pData;
	PageNum pageNumTmp = NO_PAGE;
	memcpy(ptr, &pageNumTmp, sizeof(PageNum)); // rootPage

	ptr += sizeof(PageNum);
	int intTmp = 0;
	memcpy(ptr, &intTmp, sizeof(int)); // height

	ptr += sizeof(int);
	memcpy(ptr, &attrType, sizeof(AttrType)); // attrType

	ptr += sizeof(AttrType);
	memcpy(ptr, &attrLength, sizeof(int)); // attrLength

	ptr += sizeof(int);
	SlotNum slotNumTmp = CalculateMaxKeys(attrLength) - 1; // 0-based
	memcpy(ptr, &slotNumTmp, sizeof(SlotNum)); // maxKeyIndex

	ptr += sizeof(SlotNum);
	slotNumTmp = CalculateMaxEntries(attrLength) - 1; // 0-based
	memcpy(ptr, &slotNumTmp, sizeof(SlotNum)); // maxEntryIndex

	ptr += sizeof(SlotNum);
	intTmp = sizeof(int) + sizeof(PageNum);
	memcpy(ptr, &intTmp, sizeof(int)); // internalHeaderSize

	ptr += sizeof(int);
	intTmp = sizeof(int) + 4*sizeof(PageNum) + ceil(CalculateMaxEntries(attrLength) / 8.0);
	memcpy(ptr, &intTmp, sizeof(int)); // leafHeaderSize
	// End write info to header page.

	// Mark header page as dirty.
	rc = fileHandle.MarkDirty(0);
	if (rc != OK_RC){
		PrintError(rc);
		return rc;
	}

	// Clean up
	pData = NULL;
	ptr = NULL;

	rc = fileHandle.UnpinPage(0);
	if (rc != OK_RC){
		PrintError(rc);
		return rc;
	}

	rc = pfManager->CloseFile(fileHandle);
	if (rc != OK_RC){
		PrintError(rc);
		return rc;
	}

	return OK_RC;
}

// Destroy and Index
RC IX_Manager::DestroyIndex(const char *fileName, int indexNo)
{
	// Check input parameters
	// Check filename is not null
	if (fileName == NULL){
		PrintError(IX_NULLINPUT);
		return IX_NULLINPUT;
	}
	// Check filename does not exceed max relation name size and is not empty
	size_t nameLen = strlen(fileName);
	if (nameLen > MAXNAME || nameLen == 0){
		PrintError(IX_FILENAMELEN);
		return IX_FILENAMELEN;
	}
	// Given can assume indexNo is unique and positive.
	// End check input parameters.

	// Delete file
	RC rc = pfManager->DestroyFile(GetIndexFileName(fileName, indexNo));
	if (rc != OK_RC){
		PrintError(rc);
		return rc;
	}

	return OK_RC;
}

// Open an Index
RC IX_Manager::OpenIndex(const char *fileName, int indexNo,
             IX_IndexHandle &indexHandle)
{
	// Check input parameters
	// Check filename is not null
	if (fileName == NULL){
		PrintError(IX_NULLINPUT);
		return IX_NULLINPUT;
	}
	// Check filename does not exceed max relation name size and is not empty
	size_t nameLen = strlen(fileName);
	if (nameLen > MAXNAME || nameLen == 0){
		PrintError(IX_FILENAMELEN);
		return IX_FILENAMELEN;
	}
	// Given can assume indexNo is unique and positive.
	// End check input parameters.

	// Open file handle
	RC rc = pfManager->OpenFile(GetIndexFileName(fileName, indexNo), indexHandle.pfFileHandle);
	if (rc != OK_RC){
		PrintError(rc);
		return rc;
	}

	// Initialize state
	indexHandle.open = true;
	indexHandle.modified = false;

	// Get header page handle
	PF_PageHandle pfPageHandle = PF_PageHandle();
	rc = indexHandle.pfFileHandle.GetThisPage(0, pfPageHandle);
	if (rc != OK_RC){
		PrintError(rc);
		return rc;
	}

	// Get header page data
	char *pData;
	rc = pfPageHandle.GetData(pData);
	if (rc != OK_RC){
		indexHandle.pfFileHandle.UnpinPage(0);
		PrintError(rc);
		return rc;
	}

	// Copy over header data
	char* ptr = pData;
	memcpy(&indexHandle.ixIndexHeader.rootPage, ptr, sizeof(PageNum));

	ptr += sizeof(PageNum);
	memcpy(&indexHandle.ixIndexHeader.height, ptr, sizeof(int));

	ptr += sizeof(int);
	memcpy(&indexHandle.ixIndexHeader.attrType, ptr, sizeof(AttrType));

	ptr += sizeof(AttrType);
	memcpy(&indexHandle.ixIndexHeader.attrLength, ptr, sizeof(int));

	ptr += sizeof(int);
	memcpy(&indexHandle.ixIndexHeader.maxKeyIndex, ptr, sizeof(SlotNum));

	ptr += sizeof(SlotNum);
	memcpy(&indexHandle.ixIndexHeader.maxEntryIndex, ptr, sizeof(SlotNum));

	ptr += sizeof(SlotNum);
	memcpy(&indexHandle.ixIndexHeader.internalHeaderSize, ptr, sizeof(int));

	ptr += sizeof(int);
	memcpy(&indexHandle.ixIndexHeader.leafHeaderSize, ptr, sizeof(int));
	// End copy over header data

	// Clean up
	pData = NULL;
	ptr = NULL;
	rc = indexHandle.pfFileHandle.UnpinPage(0);
	if (rc != OK_RC){
		PrintError(rc);
		return rc;
	}

	return OK_RC;
}

// Close an Index
RC IX_Manager::CloseIndex(IX_IndexHandle &indexHandle)
{
	// Flush dirty pages
	RC rc = indexHandle.ForcePages();
	if (rc != OK_RC)
		return rc;
        
	// Close file handle.
	rc = pfManager->CloseFile(indexHandle.pfFileHandle);
	if (rc != OK_RC){
		PrintError(rc);
		return rc;
	}

	// Change state
	indexHandle.open = false;

	return OK_RC;
}

const char* IX_Manager::GetIndexFileName(const char *fileName, int indexNo)
{
	stringstream ss;
	ss << fileName << '.' << indexNo;
	return ss.str().c_str();
}

int IX_Manager::CalculateMaxKeys(int attrLength)
{
	return (PF_PAGE_SIZE - sizeof(int) - sizeof(PageNum)) / (attrLength + sizeof(PageNum));
}
int IX_Manager::CalculateMaxEntries(int attrLength)
{
	return floor((PF_PAGE_SIZE - sizeof(int) - 4*sizeof(PageNum)) / (attrLength + sizeof(PageNum) + sizeof(SlotNum) + 1/8.0));
}