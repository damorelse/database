#include <string>
#include <iostream>
#include "ix.h"

using namespace std;

IX_IndexHandle::IX_IndexHandle(): open(false), modified(false), pfFileHandle(PF_FileHandle()), ixIndexHeader(IX_IndexHeader())
{}
IX_IndexHandle::~IX_IndexHandle()
{
	// Assumes CloseIndex will be called before being deleted
}

// Insert a new index entry
RC IX_IndexHandle::InsertEntry(void *attribute, const RID &rid)
{
	// Check if file is open
	if (!open){
		PrintError(IX_FILENOTOPEN);
		return IX_FILENOTOPEN;
	}

	// Check input
	// Check if pData is null
	if (!attribute){
		PrintError(IX_NULLINPUT);
		return IX_NULLINPUT;
	}
	// Check RID
	PageNum pageNum;
	RC rc = rid.GetPageNum(pageNum);
	if (rc != OK_RC)
		return rc;
	SlotNum slotNum;
	rc = rid.GetSlotNum(slotNum);
	if (rc != OK_RC)
		return rc;
	// End check input.


	// If index is empty, create index
	if (ixIndexHeader.rootPage == IX_NO_PAGE){
		// TODO: Create left and right leaf pages
		// TODO: insert entry into one of the leaf pages 

		// Allocate new page, root internal page
		PF_PageHandle pfPageHandle;
		RC rc = pfFileHandle.AllocatePage(pfPageHandle);
		if (rc != OK_RC){
			PrintError(rc);
			return rc;
		}

		PageNum rootPage;
		rc = pfPageHandle.GetPageNum(rootPage);
		if (rc != OK_RC){
			PrintError(rc);
			return rc;
		}

		// Get page data, root internal page
		char *pData;
		rc = pfPageHandle.GetData(pData);
		if (rc != OK_RC){
			pfFileHandle.UnpinPage(rootPage);
			PrintError(rc);
			return rc;
		}

		// TODO: Fill in header and data, root internal page

		// Mark page as dirty, root internal page
		rc = pfFileHandle.MarkDirty(rootPage);
		if (rc != OK_RC){
			PrintError(rc);
			return rc;
		}

		// Clean up.
		pData = NULL;
		//ptr = NULL;
		rc = pfFileHandle.UnpinPage(rootPage);
		if (rc != OK_RC){
			PrintError(rc);
			return rc;
		}

		// Update index header
		modified = true;
		ixIndexHeader.rootPage = rootPage;
		ixIndexHeader.height = 1;
	}
	// Index is not empty...
	else {
		PageNum leafNode = FindLeafNode(attribute);
		// TODO: 
	}

	return OK_RC;
}

// Delete a new index entry
RC IX_IndexHandle::DeleteEntry(void *attribute, const RID &rid)
{
	// Check if file is open
	if (!open){
		PrintError(IX_FILENOTOPEN);
		return IX_FILENOTOPEN;
	}

	// Check if index is empty
	if (ixIndexHeader.rootPage == IX_NO_PAGE){
		PrintError(IX_ENTRYDNE);
		return IX_ENTRYDNE;
	}

	// Check input
	// Check if pData is null
	if (!attribute){
		PrintError(IX_NULLINPUT);
		return IX_NULLINPUT;
	}
	// Check RID
	PageNum pageNum;
	RC rc = rid.GetPageNum(pageNum);
	if (rc != OK_RC)
		return rc;
	SlotNum slotNum;
	rc = rid.GetSlotNum(slotNum);
	if (rc != OK_RC)
		return rc;
	// End check input

	// Find correct leaf node
	PageNum leafNode = FindLeafNode(attribute);
	// TODO: check page and bucket pages for {attribute, rid}. Delete if find. Return error if not.

	return OK_RC;
}

// Force index files to disk
RC IX_IndexHandle::ForcePages()
{
	// Check if file is open
	if (!open){
		PrintError(IX_FILENOTOPEN);
		return IX_FILENOTOPEN;
	}

	if (modified) {
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
		char* ptr = pData;
		memcpy(ptr, &ixIndexHeader.rootPage, sizeof(PageNum));

		ptr += sizeof(PageNum);
		memcpy(ptr, &ixIndexHeader.height, sizeof(int));
		// End write to header page.

		// Mark header page as dirty
		rc = pfFileHandle.MarkDirty(0);
		if (rc != OK_RC){
			PrintError(rc);
			return rc;
		}

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
	RC rc = pfFileHandle.ForcePages();
	if (rc != OK_RC){
		PrintError(rc);
		return rc;
	}

	return OK_RC;
}

char* IX_IndexHandle::GetKeyPtr(char* pData, const SlotNum slotNum) const
{
	char* ptr = pData + ixIndexHeader.internalHeaderSize;
	ptr += sizeof(PageNum);
	ptr += slotNum * (ixIndexHeader.attrLength + sizeof(PageNum));
	return ptr;
}
char* IX_IndexHandle::GetRecordPtr(char* pData, const SlotNum slotNum) const
{
	char* ptr = pData + ixIndexHeader.leafHeaderSize;
	ptr += slotNum * (ixIndexHeader.attrLength + sizeof(PageNum) + sizeof(SlotNum));
	return ptr;
}
bool IX_IndexHandle::GetSlotBitValue(char* pData, const SlotNum slotNum) const
{
	char c = *(pData + IX_BIT_START + slotNum / 8); //bits per byte
	return c & (1 << slotNum % 8);
}
void IX_IndexHandle::SetSlotBitValue(char* pData, const SlotNum slotNum, bool b)
{
	if (b)
		pData[IX_BIT_START + slotNum / 8] |= ( 1 << slotNum % 8);
	else
		pData[IX_BIT_START + slotNum / 8] &= ~( 1 << slotNum % 8);
}


// TODO: Check
PageNum IX_IndexHandle::FindLeafNode(void* attribute)
{
	return FindLeafNodeHelper(attribute, ixIndexHeader.rootPage, ixIndexHeader.height);
}
PageNum IX_IndexHandle::FindLeafNodeHelper(void* attribute, PageNum currPage, int currHeight)
{
	// At leaf page, done.
	if (currHeight == 0 || currPage == IX_NO_PAGE)
		return currPage;

	// Convert attribute into correct form
	int a_i, k_i;
	float a_f, k_f;
	string a_s, k_s;
	switch(ixIndexHeader.attrType) {
	case INT:
		memcpy(&a_i, attribute, ixIndexHeader.attrLength);
        break;
	case FLOAT:
		memcpy(&a_f, attribute, ixIndexHeader.attrLength);
        break;
	case STRING:
		char* tmp = new char[ixIndexHeader.attrLength];
		memcpy(tmp, attribute, ixIndexHeader.attrLength);
		a_s = string(tmp);
		delete [] tmp;
        break;
	}

	// At internal page
	// Get page handle
	PF_PageHandle pfPageHandle = PF_PageHandle();
	RC rc = pfFileHandle.GetThisPage(currPage, pfPageHandle);
	if (rc != OK_RC){
		PrintError(rc);
		return rc;
	}

	// Get page data
	char *pData;
	rc = pfPageHandle.GetData(pData);
	if (rc != OK_RC){
		pfFileHandle.UnpinPage(currPage);
		PrintError(rc);
		return rc;
	}

	// Get number of keys
	int numKeys;
	memcpy(&numKeys, pData, sizeof(int));

	// Iterate through keys until find first key greater than attribute.
	char* ptr;
	bool greaterThan = false;
	PageNum nextPage;
	for (SlotNum keyNum = 0; keyNum < numKeys; ++keyNum){
		ptr = GetKeyPtr(pData, keyNum);

		// Convert key into correct form, check if greater than
		switch(ixIndexHeader.attrType) {
		case INT:
			memcpy(&k_i, ptr, ixIndexHeader.attrLength);
			greaterThan = a_i < k_i;
			break;
		case FLOAT:
			memcpy(&k_f, ptr, ixIndexHeader.attrLength);
			greaterThan = a_f < k_f;
			break;
		case STRING:
			char* tmp = new char[ixIndexHeader.attrLength];
			memcpy(tmp, ptr, ixIndexHeader.attrLength);
			k_s = string(tmp);
			delete [] tmp;
			greaterThan = a_s < k_s;
			break;
		}

		// Found first greaterthan key, copy preceding page pointer
		if (greaterThan){
			ptr -= sizeof(PageNum);
			memcpy(&nextPage, ptr, sizeof(PageNum));
			break;
		}
	}

	// Did not find a greaterThan key, copy last page pointer
	if (!greaterThan){
		ptr += ixIndexHeader.attrLength;
		memcpy(&nextPage, ptr, sizeof(PageNum));
	}

	// Recursive call to next index level
	return FindLeafNodeHelper(attribute, nextPage, currHeight - 1);
}