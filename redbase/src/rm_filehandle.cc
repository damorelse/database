#include <cstdio>
#include <iostream>
#include <cstring>
#include "rm.h"

using namespace std;

RM_FileHandle::RM_FileHandle (): open(false), modified(false), pfFileHandle(PF_FileHandle()) {}

RM_FileHandle::~RM_FileHandle()
{
	// Assume will always be closed before deleted.
}

RM_FileHandle::RM_FileHandle(const RM_FileHandle &other): open(false), modified(false), pfFileHandle(PF_FileHandle())
{
	*this = other;
}
RM_FileHandle& RM_FileHandle::operator= (const RM_FileHandle &other)
{
	if (this != &other){
		open = other.open;
		pfFileHandle = other.pfFileHandle;
		modified = other.modified;
		rmFileHeader = other.rmFileHeader;
	}
	return *this;
}

// Given a RID, return the record
RC RM_FileHandle::GetRec     (const RID &rid, RM_Record &rec) const
{
	// Check RID
	PageNum pageNum;
	RC rc = rid.GetPageNum(pageNum);
	if (rc != OK_RC)
		return rc;
	
	SlotNum slotNum;
	rc = rid.GetSlotNum(slotNum);
	if (rc != OK_RC)
		return rc;

	if (pageNum > rmFileHeader.maxPage || pageNum < 1 ||
		slotNum > rmFileHeader.maxSlot || slotNum < 0){
		PrintError(RM_RECORD_DNE);
		return RM_RECORD_DNE;
	}
	// End check RID

	if (!open){
		PrintError(RM_FILENOTOPEN);
		return RM_FILENOTOPEN;
	}
	
	// Get page handle
	PF_PageHandle pfPageHandle = PF_PageHandle();
	rc = pfFileHandle.GetThisPage(pageNum, pfPageHandle);
	if (rc != OK_RC){
		PrintError(rc);
		return rc;
	}

	// Get page data
	char *pData;
	rc = pfPageHandle.GetData(pData);
	if (rc != OK_RC){
		pfFileHandle.UnpinPage(pageNum);
		PrintError(rc);
		return rc;
	}

	// Check if record exists
	if (!GetSlotBitValue(pData, slotNum)){
		pfFileHandle.UnpinPage(pageNum);
		PrintError(RM_RECORD_DNE);
		return RM_RECORD_DNE;
	}

	// Get pointer to record start
	char* ptr = GetRecordPtr(pData, slotNum);

	// Copy info to record
	rec.length = rmFileHeader.recordSize;
	rec.rid = rid;
	delete [] rec.recordCopy; // just in case
	rec.recordCopy = new char[rec.length];
	memcpy(rec.recordCopy, ptr, rec.length);

	// Clean up.
	pData = NULL;
	rc = pfFileHandle.UnpinPage(pageNum);
	if (rc != OK_RC){
		PrintError(rc);
		return rc;
	}

	return OK_RC;
}


RC RM_FileHandle::InsertRec  (const char *inData, RID &rid)       // Insert a new record
{
	if (!open){
		PrintError(RM_FILENOTOPEN);
		return RM_FILENOTOPEN;
	}

	if (rmFileHeader.firstFreeSpace == RM_PAGE_LIST_END){
		// Allocate new page
		PF_PageHandle pfPageHandle;
		RC rc = pfFileHandle.AllocatePage(pfPageHandle);
		if (rc != OK_RC){
			PrintError(rc);
			return rc;
		}

		PageNum pageNum;
		rc = pfPageHandle.GetPageNum(pageNum);
		if (rc != OK_RC){
			PrintError(rc);
			return rc;
		}

		// Get page data
		char *pData;
		rc = pfPageHandle.GetData(pData);
		if (rc != OK_RC){
			pfFileHandle.UnpinPage(pageNum);
			PrintError(rc);
			return rc;
		}

		// Modify file header
		modified = true;
		// maxPage
		cerr << "CHECK" << rmFileHeader.maxPage << " " << pageNum << endl;
		rmFileHeader.maxPage = pageNum;
		// freeSpace list
		if (rmFileHeader.maxSlot > 0){
			rmFileHeader.firstFreeSpace = pageNum;
		}

		// Fill in page header
		// nextFreeSpace
		int i = RM_PAGE_LIST_END;
		memcpy(pData, &i, sizeof(int));
		// slotsBit
		SetSlotBitValue(pData, 0, true);

		// Copy pData to page
		char* ptr = GetRecordPtr(pData, 0);
		memcpy(ptr, inData, rmFileHeader.recordSize);

		// Clean up.
		pData = NULL;
		ptr = NULL;
		rc = pfFileHandle.UnpinPage(pageNum);
		if (rc != OK_RC){
			PrintError(rc);
			return rc;
		}

		// Set rid
		rid.pageNum = pageNum;
		rid.slotNum = 0;
	}
	else {
		// Get page num from free space list
		PageNum pageNum = rmFileHeader.firstFreeSpace;

		// Get page handle
		PF_PageHandle pfPageHandle = PF_PageHandle();
		RC rc = pfFileHandle.GetThisPage(pageNum, pfPageHandle);
		if (rc != OK_RC){
			PrintError(rc);
			return rc;
		}

		// Get page data
		char *pData;
		rc = pfPageHandle.GetData(pData);
		if (rc != OK_RC){
			pfFileHandle.UnpinPage(pageNum);
			PrintError(rc);
			return rc;
		}

		// Find open bitslot
		SlotNum slotNum = 0;
		for (; slotNum <= rmFileHeader.maxSlot; ++slotNum){
			if (!GetSlotBitValue(pData, slotNum)){
				// Found empty slot
				break;
			}
		}

		// Set bit slot
		SetSlotBitValue(pData, slotNum, true);

		// Copy pData to page
		char* ptr = GetRecordPtr(pData, slotNum);
		memcpy(ptr, inData, rmFileHeader.recordSize);

		// Check if page now full
		bool full = true;
		for (; full && slotNum <= rmFileHeader.maxSlot; ++slotNum){
			full = GetSlotBitValue(pData, slotNum);
		}
		// If full, remove from free space list.
		if (full){
			// Modify file header
			int i;
			memcpy(&i, pData, sizeof(int));
			modified = true;
			rmFileHeader.firstFreeSpace = i;

			// Modify page header
			i = RM_PAGE_FULL;
			memcpy(pData, &i, sizeof(int));
		}

		// Clean up.
		pData = NULL;
		ptr = NULL;
		rc = pfFileHandle.UnpinPage(pageNum);
		if (rc != OK_RC){
			PrintError(rc);
			return rc;
		}

		// Set rid
		rid.pageNum = pageNum;
		rid.slotNum = slotNum;

	}

	return OK_RC;
}

RC RM_FileHandle::DeleteRec  (const RID &rid)                    // Delete a record
{
	// Check RID
	PageNum pageNum;
	RC rc = rid.GetPageNum(pageNum);
	if (rc != OK_RC)
		return rc;
	
	SlotNum slotNum;
	rc = rid.GetSlotNum(slotNum);
	if (rc != OK_RC)
		return rc;

	if (pageNum > rmFileHeader.maxPage || slotNum > rmFileHeader.maxSlot){
		PrintError(RM_RECORD_DNE);
		return RM_RECORD_DNE;
	}
	// End check RID.

	if (!open){
		PrintError(RM_FILENOTOPEN);
		return RM_FILENOTOPEN;
	}

	// Get page handle
	PF_PageHandle pfPageHandle = PF_PageHandle();
	rc = pfFileHandle.GetThisPage(pageNum, pfPageHandle);
	if (rc != OK_RC){
		PrintError(rc);
		return rc;
	}

	// Get page data
	char *pData;
	rc = pfPageHandle.GetData(pData);
	if (rc != OK_RC){
		pfFileHandle.UnpinPage(pageNum);
		PrintError(rc);
		return rc;
	}

	// "Delete" record by clearing slot bit
	SetSlotBitValue(pData, slotNum, false);

	// Add page to freeSpace list if not already on it
	int i;
	memcpy(&i, pData, sizeof(int));
	if (i == RM_PAGE_FULL){
		modified = true;
		memcpy(pData, &rmFileHeader.firstFreeSpace, sizeof(int));
		rmFileHeader.firstFreeSpace = pageNum;
	}

	// Clean up.
	pData = NULL;
	rc = pfFileHandle.UnpinPage(pageNum);
	if (rc != OK_RC){
		PrintError(rc);
		return rc;
	}

	return OK_RC;
}

RC RM_FileHandle::UpdateRec  (const RM_Record &rec)              // Update a record
{
	// Check record
	RID rid;
	RC rc = rec.GetRid(rid);
	if (rc != OK_RC)
		return rc;

	char* rData;
	rc = rec.GetData(rData);
	if (rc != OK_RC)
		return rc;

	if (rec.length != rmFileHeader.recordSize){ // if not correct size, DNE
		PrintError(RM_RECORD_DNE);
		return RM_RECORD_DNE;
	}
	// End check record.

	// Check RID
	PageNum pageNum;
	rc = rid.GetPageNum(pageNum);
	if (rc != OK_RC)
		return rc;
	
	SlotNum slotNum;
	rc = rid.GetSlotNum(slotNum);
	if (rc != OK_RC)
		return rc;

	if (pageNum > rmFileHeader.maxPage || slotNum > rmFileHeader.maxSlot){
		PrintError(RM_RECORD_DNE);
		return RM_RECORD_DNE;
	}
	// End check RID.

	if (!open){
		PrintError(RM_FILENOTOPEN);
		return RM_FILENOTOPEN;
	}

	// Get page handle
	PF_PageHandle pfPageHandle = PF_PageHandle();
	rc = pfFileHandle.GetThisPage(pageNum, pfPageHandle);
	if (rc != OK_RC){
		PrintError(rc);
		return rc;
	}

	// Get page data
	char *pData;
	rc = pfPageHandle.GetData(pData);
	if (rc != OK_RC){
		pfFileHandle.UnpinPage(pageNum);
		PrintError(rc);
		return rc;
	}

	// Check if record exists
	if (!GetSlotBitValue(pData, slotNum)){
		pfFileHandle.UnpinPage(pageNum);
		PrintError(RM_RECORD_DNE);
		return RM_RECORD_DNE;
	}

	// Get pointer to record start
	char* ptr = GetRecordPtr(pData, slotNum);

	// Copy info to page
	memcpy(ptr, rData, rmFileHeader.recordSize);

	// Clean up.
	rData = NULL;
	ptr = NULL;
	rc = pfFileHandle.UnpinPage(pageNum);
	if (rc != OK_RC){
		PrintError(rc);
		return rc;
	}

	return OK_RC;
}

// Forces a page (along with any contents stored in this class)
// from the buffer pool to disk.  Default value forces all pages.
RC RM_FileHandle::ForcePages (PageNum pageNum)
{
	if (!open){
		PrintError(RM_FILENOTOPEN);
		return RM_FILENOTOPEN;
	}

	// Update header page if necessary
	if (modified && (pageNum == ALL_PAGES || pageNum == 0)){
		// Get page handle
		PF_PageHandle pfPageHandle = PF_PageHandle();
		RC rc = pfFileHandle.GetThisPage(0, pfPageHandle);
		if (rc != OK_RC){
			PrintError(rc);
			return rc;
		}

		// Get page data
		char *pData;
		rc = pfPageHandle.GetData(pData);
		if (rc != OK_RC){
			pfFileHandle.UnpinPage(0);
			PrintError(rc);
			return rc;
		}
		
		// Write to header page
		char *ptr = pData + sizeof(size_t) * 2;
		memcpy(ptr, &rmFileHeader.maxPage, sizeof(size_t));
		ptr += sizeof(size_t);
		memcpy(ptr, &rmFileHeader.firstFreeSpace, sizeof(int));

		// Clean up.
		modified = false;
		pData = NULL;
		ptr = NULL;
		rc = pfFileHandle.UnpinPage(0);
		if (rc != OK_RC){
			PrintError(rc);
			return rc;
		}
	}

	// Force pages
	RC rc = pfFileHandle.ForcePages(pageNum);
	if (rc != OK_RC){
		PrintError(rc);
		return rc;
	}

	return OK_RC;
}


bool RM_FileHandle::GetSlotBitValue(char* pData, const SlotNum slotNum) const
{
	char c = *(pData + RM_BIT_START + slotNum / 8); //bits per byte
	return c & (1 << slotNum % 8);
}

void RM_FileHandle::SetSlotBitValue(char* pData, const SlotNum slotNum, bool b)
{
	if (b)
		pData[RM_BIT_START + slotNum / 8] |= ( 1 << slotNum % 8);
	else
		pData[RM_BIT_START + slotNum / 8] &= ~( 1 << slotNum % 8);
}

char* RM_FileHandle::GetRecordPtr(char* pData, const SlotNum slotNum) const
{
	return pData + rmFileHeader.pageHeaderSize + slotNum * rmFileHeader.recordSize;
}