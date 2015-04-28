#include <cstring>
#include <sstream>
#include <cmath>
#include <string>
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
	if (indexNo < 1){
		PrintError(IX_INVALIDNUM);
		return IX_INVALIDNUM;
	}

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
	stringstream ss;
	ss << fileName << '.' << indexNo;
	string buff= ss.str();

	const char* indexName = {ss.str().c_str()};

	RC rc = pfManager->CreateFile(buff.c_str());
	if (rc != OK_RC){
		PrintError(rc);
		return rc;
	}

	PF_FileHandle fileHandle = PF_FileHandle();
	rc = pfManager->OpenFile(buff.c_str(), fileHandle);
	if (rc != OK_RC){
		PrintError(rc);
		return rc;
	}

	// Create header page in file
	char *pData;
	//rc = CreatePage(fileHandle, headerPage, pData);
	PF_PageHandle pfPageHandle;
	rc = fileHandle.AllocatePage(pfPageHandle);
	if (rc != OK_RC){
		PrintError(rc);
		return rc;
	}
	rc = pfPageHandle.GetData(pData);
	if (rc != OK_RC){
		fileHandle.UnpinPage(0);
		PrintError(rc);
		return rc;
	}

	//Create root leaf page
	PageNum rootPage;
	rc = CreateEmptyRoot(fileHandle, attrLength, rootPage);
	if (rc != OK_RC){
		fileHandle.UnpinPage(0);
		return rc;
	}

	// Write info to header page
	char* ptr = pData;
	memcpy(ptr, &rootPage, sizeof(PageNum)); // rootPage

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
	intTmp = sizeof(int);
	memcpy(ptr, &intTmp, sizeof(int)); // internalHeaderSize

	ptr += sizeof(int);
	intTmp = sizeof(int) + 3*sizeof(PageNum) + ceil(CalculateMaxEntries(attrLength) / 8.0);
	memcpy(ptr, &intTmp, sizeof(int)); // leafHeaderSize
	// End write info to header page.

	// Mark header page as dirty.
	rc = fileHandle.MarkDirty(0);
	if (rc != OK_RC){
		fileHandle.UnpinPage(0);
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

	// Close file
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
	stringstream ss;
	ss << fileName << '.' << indexNo;
	const char* indexName = ss.str().c_str();
	RC rc = pfManager->DestroyFile(indexName);
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
	stringstream ss;
	ss << fileName << '.' << indexNo;
	const char* indexName = {ss.str().c_str()};

	RC rc = pfManager->OpenFile(indexName, indexHandle.pfFileHandle);
	if (rc != OK_RC){
		PrintError(rc);
		return rc;
	}

	// Initialize state
	indexHandle.open = true;
	indexHandle.modified = false;

	// Get header page info
	char *pData;
	//rc = GetPage(indexHandle.pfFileHandle, pageNum, pData);
	PF_PageHandle pfPageHandle = PF_PageHandle();
	rc = indexHandle.pfFileHandle.GetThisPage(0, pfPageHandle);
	if (rc != OK_RC){
		PrintError(rc);
		return rc;
	}
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
	return floor((PF_PAGE_SIZE - sizeof(int) - 3*sizeof(PageNum)) / (attrLength + sizeof(PageNum) + sizeof(SlotNum) + 1/8.0));
}

RC IX_Manager::CreateEmptyRoot(PF_FileHandle &fileHandle, int attrLength, PageNum &pageNum)
{
	SlotNum maxEntry = CalculateMaxEntries(attrLength);

	// Create page
	char *pData;
	//RC rc = CreatePage(pfFileHandle, pageNum, pData);
	PF_PageHandle pfPageHandle;
	RC rc = fileHandle.AllocatePage(pfPageHandle);
	if (rc != OK_RC){
		PrintError(rc);
		return rc;
	}
	rc = pfPageHandle.GetPageNum(pageNum);
	if (rc != OK_RC){
		PrintError(rc);
		return rc;
	}
	rc = pfPageHandle.GetData(pData);
	if (rc != OK_RC){
		fileHandle.UnpinPage(pageNum);
		PrintError(rc);
		return rc;
	}

	//Fill in header, leaf page
	char* ptr = pData;
	int intTmp = 0;
	memcpy(ptr, &intTmp, sizeof(int)); // numEntries

	ptr += sizeof(int);
	PageNum pageNumTmp = IX_NO_PAGE;
	memcpy(ptr, &pageNumTmp, sizeof(PageNum)); // nextBucketPage

	ptr += sizeof(PageNum);
	memcpy(ptr, &pageNumTmp, sizeof(PageNum)); // leftLeaf

	ptr += sizeof(PageNum);
	memcpy(ptr, &pageNumTmp, sizeof(PageNum)); // rightLeaf

	ptr += sizeof(PageNum);
	char charTmp = 0;
	for (SlotNum i = 0; i < ceil(maxEntry / 8.0); ++i){ //bitSlots
		memcpy(ptr, &charTmp, sizeof(char));
		ptr += sizeof(char);
	}

	// Mark page as dirty
	rc = fileHandle.MarkDirty(pageNum);
	if (rc != OK_RC){
		fileHandle.UnpinPage(pageNum);
		PrintError(rc);
		return rc;
	}

	// Clean up.
	pData = NULL;
	ptr = NULL;
	rc = fileHandle.UnpinPage(pageNum);
	if (rc != OK_RC){
		PrintError(rc);
		return rc;
	}

	return OK_RC;
}

/*
RC IX_Manager::CreatePage(PF_FileHandle fileHandle, PageNum &pageNum, char* pData){
	PF_PageHandle pfPageHandle;
	RC rc = fileHandle.AllocatePage(pfPageHandle);
	if (rc != OK_RC){
		PrintError(rc);
		return rc;
	}

	rc = pfPageHandle.GetPageNum(pageNum);
	if (rc != OK_RC){
		PrintError(rc);
		return rc;
	}

	// Get page data
	rc = pfPageHandle.GetData(pData);
	if (rc != OK_RC){
		fileHandle.UnpinPage(pageNum);
		PrintError(rc);
		return rc;
	}

	return OK_RC;
}

RC IX_Manager::GetPage(PF_FileHandle fileHandle, PageNum pageNum, char* pData) const{
	PF_PageHandle pfPageHandle = PF_PageHandle();
	RC rc = fileHandle.GetThisPage(pageNum, pfPageHandle);
	if (rc != OK_RC){
		PrintError(rc);
		return rc;
	}

	rc = pfPageHandle.GetData(pData);
	if (rc != OK_RC){
		fileHandle.UnpinPage(pageNum);
		PrintError(rc);
		return rc;
	}

	return OK_RC;
}
*/
