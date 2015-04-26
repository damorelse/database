#include <iostream>
#include <string>
#include "ix.h"

using namespace std;

IX_IndexScan::IX_IndexScan(): ixIndexHandle(NULL), compOp(NO_OP), value(NULL), open(false), pageNum(-1), entryNum(-1), rightLeaf(-1), inBucket(false), finished(false), lastEntry("")
{}
IX_IndexScan::~IX_IndexScan()
{
	ixIndexHandle = NULL;
}

// Open index scan
RC IX_IndexScan::OpenScan(const IX_IndexHandle &indexHandle,
						  CompOp compOp,
						  void *value,
						  ClientHint  pinHint = NO_HINT)
{
	// Check if filescan already open
	if (open){
		PrintError(IX_FILESCANREOPEN);
		return IX_FILESCANREOPEN;
	}

	// Check input parameters
	// Check compare operation is one of the six allowed
	if (compOp != NO_OP && compOp != EQ_OP &&  compOp !=GE_OP &&
		compOp !=LT_OP && compOp !=GT_OP && compOp !=LE_OP){
		PrintError(IX_INVALIDENUM);
		return IX_INVALIDENUM;
	}

	// Check if invalid scan combo
	if ((compOp == NO_OP && value) || (compOp != NO_OP && !value)){
		PrintError(IX_INVALIDSCANCOMBO);
		return IX_INVALIDSCANCOMBO;
	}
	// End check input parameters

	// Copy over scan params
	ixIndexHandle = &indexHandle;
	this->compOp = compOp;
	this->value = value;

	// Set state
	open = true;
	if (compOp == NO_OP || compOp == LT_OP || compOp == LE_OP){
		pageNum = FindMinLeafNode();
	}
	else if (compOp == EQ_OP || compOp == GE_OP || compOp == GT_OP){
		pageNum = FindLeafNode(value);
	}
	entryNum = 0;
	rightLeaf = IX_NO_PAGE;
	inBucket = false;
	finished = false;
	lastEntry = "";

	return OK_RC;
}

// Get the next matching entry return IX_EOF if no more matching entries.
RC IX_IndexScan::GetNextEntry(RID &rid)
{
	// Check if scan finished
	if (finished){
		PrintError(IX_EOF);
		return IX_EOF;
	}

	PF_PageHandle pfPageHandle;
	char* pData;
	RC rc;
	PageNum prevPage = pageNum;
	bool found = false;

	// CHANGES TO ACCOMODATE DELETES DURING INDEXSCAN
	// Initialize page handle and page data
	rc = ixIndexHandle->pfFileHandle.GetThisPage(pageNum, pfPageHandle);
	if (rc != OK_RC){
		PrintError(rc);
		return rc;
	}
	rc = pfPageHandle.GetData(pData);
	if (rc != OK_RC){
		PrintError(rc);
		return rc;
	}

	// Read previous entry if entry still filled
	string strTmp = "";
	if (ixIndexHandle->GetSlotBitValue(pData, entryNum)){
		int entrySize = ixIndexHandle->ixIndexHeader.attrLength + sizeof(PageNum) + sizeof(SlotNum);
		char* charArrTmp = new char[entrySize];
		memcpy(charArrTmp, ixIndexHandle->GetEntryPtr(pData, entryNum), entrySize);
		strTmp = string(charArrTmp);
		delete charArrTmp;
	}

	// If previous entry is the same, increment entry iterator
	if (lastEntry == strTmp && strTmp.length > 0){
		// Increment entry num
		entryNum += 1;
		if (entryNum > ixIndexHandle->ixIndexHeader.maxEntryIndex){
			pageNum = GetNextPage(pageNum);
			entryNum = 0;
		}
	}

	// If switched to new page...
	if (prevPage != pageNum){
		// Unpin last page
		rc = ixIndexHandle->pfFileHandle.UnpinPage(prevPage);
		if (rc != OK_RC){
			PrintError(rc);
			return rc;
		}
		pData = NULL;

		// If more entries to read...
		if (pageNum != IX_NO_PAGE){
			// Get new page handle and page data
			rc = ixIndexHandle->pfFileHandle.GetThisPage(pageNum, pfPageHandle);
			if (rc != OK_RC){
				PrintError(rc);
				return rc;
			}
			rc = pfPageHandle.GetData(pData);
			if (rc != OK_RC){
				PrintError(rc);
				return rc;
			}
		}
		// No more entries to read
		else
			finished = true;
	}
	// END CHANGES TO ACCOMODATE DELETES DURING INDEXSCAN

	// If no more entries to read, EOF
	if (finished){
		PrintError(IX_EOF);
		return IX_EOF;
	}

	// Iterate through pages and entries until find one that satisfies condition (or EOF)
	while (!found && !finished){
		// If record exists in slot
		if (ixIndexHandle->GetSlotBitValue(pData, entryNum)){
			// If no condition, found
			if (compOp == NO_OP){
				found = true;
				break;
			}

			// Read in attribute, covert attribute and value to correct type
			char* ptr = ixIndexHandle->GetEntryPtr(pData, entryNum);
			AttrType attrType = ixIndexHandle->ixIndexHeader.attrType;
			int attrLength = ixIndexHandle->ixIndexHeader.attrLength;

			int a_i, v_i;
			float a_f, v_f;
			string a_s, v_s;
			switch(attrType) {
			case INT:
				memcpy(&a_i, ptr, attrLength);
				memcpy(&v_i, value, attrLength);
                break;
			case FLOAT:
				memcpy(&a_f, ptr, attrLength);
				memcpy(&v_f, value, attrLength);
                break;
			case STRING:
				char* tmp = new char[attrLength];
				memcpy(tmp, ptr, attrLength);
				a_s = string(tmp);
				v_s = string((char*)value);
				delete [] tmp;
                break;
			}

			// Check if record fulfills condition
			switch(compOp) {
			case EQ_OP:
				switch(attrType) {
				case INT:
					found = (a_i == v_i);
					finished = (a_i > v_i);
                    break;
				case FLOAT:
					found = (a_f == v_f);
					finished = (a_f > v_f);
                    break;
				case STRING:
					found = (a_s == v_s);
					finished = (a_s > v_s);
                    break;
				}
                break;
			case LT_OP:
				switch(attrType) {
				case INT:
					found = (a_i < v_i);
					finished = (a_i >= v_i);
                    break;
				case FLOAT:
					found = (a_f < v_f);
					finished = (a_f >= v_f);
                    break;
				case STRING:
					found = (a_s < v_s);
					finished = (a_s >= v_s);
                    break;
				}
                break;
			case GT_OP:
				switch(attrType) {
				case INT:
					found = (a_i > v_i);
                    break;
				case FLOAT:
					found = (a_f > v_f);
                    break;
				case STRING:
					found = (a_s > v_s);
                    break;
				}
                break;
			case LE_OP:
				switch(attrType) {
				case INT:
					found = (a_i <= v_i);
					finished = (a_i > v_i);
                    break;
				case FLOAT:
					found = (a_f <= v_f);
					finished = (a_f > v_f);
                    break;
				case STRING:
					found = (a_s <= v_s);
					finished = (a_s > v_s);
                    break;
				}
                break;
			case GE_OP:
				switch(attrType) {
				case INT:
					found = (a_i >= v_i);
                    break;
				case FLOAT:
					found = (a_f >= v_f);
                    break;
				case STRING:
					found = (a_s >= v_s);
                    break;
				}
                break;
			}
		}

		// Check if entry satisfied condition OR scan finished, break out of loop
		if (found || finished){
			break;
        }

		// Entry not found and scan not finished
		// Increment entry num
		prevPage = pageNum;
		entryNum += 1;
		if (entryNum > ixIndexHandle->ixIndexHeader.maxEntryIndex){
			pageNum = GetNextPage(pageNum);
			entryNum = 0;
		}

		// If switched to new page...
		if (prevPage != pageNum){
			// Unpin last page
			rc = ixIndexHandle->pfFileHandle.UnpinPage(prevPage);
			if (rc != OK_RC){
				PrintError(rc);
				return rc;
			}
			pData = NULL;

			// If more entries to read...
			if (pageNum != IX_NO_PAGE){
				// Get new page handle and page data
				rc = ixIndexHandle->pfFileHandle.GetThisPage(pageNum, pfPageHandle);
				if (rc != OK_RC){
					PrintError(rc);
					return rc;
				}
				rc = pfPageHandle.GetData(pData);
				if (rc != OK_RC){
					PrintError(rc);
					return rc;
				}
			}
			// No more entries to read
			else
				finished = true;
		}
	}
	
	// After loop
	// Scan finished, EOF
	if (finished){
		PrintError(IX_EOF);
		return IX_EOF;
	}

	// Matching entry was found, copy matching info to rid
	char* ptr = ixIndexHandle->GetEntryPtr(pData, entryNum);
	ptr += ixIndexHandle->ixIndexHeader.attrLength;
	memcpy(&rid.pageNum, ptr, sizeof(PageNum));
	ptr += sizeof(PageNum);
	memcpy(&rid.slotNum, ptr, sizeof(SlotNum));

	// Set lastEntry
	int entrySize = ixIndexHandle->ixIndexHeader.attrLength + sizeof(PageNum) + sizeof(SlotNum);
	char* charArrTmp = new char[entrySize];
	memcpy(charArrTmp, ixIndexHandle->GetEntryPtr(pData, entryNum), entrySize);
	lastEntry = string(charArrTmp);
	delete charArrTmp;

	// Clean up.
	pData = NULL;
	ptr = NULL;
	rc = ixIndexHandle->pfFileHandle.UnpinPage(pageNum);
	if (rc != OK_RC){
		PrintError(rc);
		return rc;
	}

	return OK_RC;
}

// Close index scan
RC IX_IndexScan::CloseScan()
{
	// Set state
	open = false;
	ixIndexHandle = NULL;

	return OK_RC;
}

PageNum IX_IndexScan::FindLeafNode(void* attribute) const
{
	return FindLeafNodeHelper(ixIndexHandle->ixIndexHeader.rootPage, ixIndexHandle->ixIndexHeader.height, false, attribute);
}
PageNum IX_IndexScan::FindMinLeafNode() const
{
	return FindLeafNodeHelper(ixIndexHandle->ixIndexHeader.rootPage, ixIndexHandle->ixIndexHeader.height, true, NULL);
}
PageNum IX_IndexScan::FindLeafNodeHelper(PageNum currPage, int currHeight, bool findMin, void* attribute) const
{
	// At leaf page, done.
	if (currHeight == 0)
		return currPage;

	// At internal page
	// Get page handle
	PF_PageHandle pfPageHandle = PF_PageHandle();
	RC rc = ixIndexHandle->pfFileHandle.GetThisPage(currPage, pfPageHandle);
	if (rc != OK_RC){
		PrintError(rc);
		return rc;
	}

	// Get page data
	char *pData;
	rc = pfPageHandle.GetData(pData);
	if (rc != OK_RC){
		ixIndexHandle->pfFileHandle.UnpinPage(currPage);
		PrintError(rc);
		return rc;
	}

	char* ptr;
	PageNum nextPage;

	// Finding min leaf page
	if (findMin){
		// Choose left-most page pointer
		ptr = pData + ixIndexHandle->ixIndexHeader.internalHeaderSize;
		memcpy(&nextPage, ptr, sizeof(PageNum));
	}
	// Not finding min leaf page
	else {
		// Convert attribute to correct type
		int a_i, k_i;
		float a_f, k_f;
		string a_s, k_s;
		switch(ixIndexHandle->ixIndexHeader.attrType) {
		case INT:
			memcpy(&a_i, attribute, ixIndexHandle->ixIndexHeader.attrLength);
			break;
		case FLOAT:
			memcpy(&a_f, attribute, ixIndexHandle->ixIndexHeader.attrLength);
			break;
		case STRING:
			char* tmp = new char[ixIndexHandle->ixIndexHeader.attrLength];
			memcpy(tmp, attribute, ixIndexHandle->ixIndexHeader.attrLength);
			a_s = string(tmp);
			delete [] tmp;
			break;
		}

		// Get number of keys
		int numKeys;
		memcpy(&numKeys, pData, sizeof(int));

		// Iterate through keys until find first key greater than attribute.
		bool greaterThan = false;
		for (SlotNum keyNum = 0; keyNum < numKeys; ++keyNum){
			ptr = ixIndexHandle->GetKeyPtr(pData, keyNum);

			// Convert key into correct form, check if greater than
			switch(ixIndexHandle->ixIndexHeader.attrType) {
			case INT:
				memcpy(&k_i, ptr, ixIndexHandle->ixIndexHeader.attrLength);
				greaterThan = a_i < k_i;
				break;
			case FLOAT:
				memcpy(&k_f, ptr, ixIndexHandle->ixIndexHeader.attrLength);
				greaterThan = a_f < k_f;
				break;
			case STRING:
				char* tmp = new char[ixIndexHandle->ixIndexHeader.attrLength];
				memcpy(tmp, ptr, ixIndexHandle->ixIndexHeader.attrLength);
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
			ptr += ixIndexHandle->ixIndexHeader.attrLength;
			memcpy(&nextPage, ptr, sizeof(PageNum));
		}
	}

	// Clean up
	pData = NULL;
	ptr = NULL;
	rc =  ixIndexHandle->pfFileHandle.UnpinPage(currPage);
	if (rc != OK_RC){
		PrintError(rc);
		return rc;
	}

	// Recursive call to next index level
	return FindLeafNodeHelper(nextPage, currHeight - 1, findMin, attribute);
}

// Assumes at a leaf node
PageNum IX_IndexScan::GetNextPage(PageNum pageNum)
{
	// Get page handle
	PF_PageHandle pfPageHandle = PF_PageHandle();
	RC rc = ixIndexHandle->pfFileHandle.GetThisPage(pageNum, pfPageHandle);
	if (rc != OK_RC){
		PrintError(rc);
		return rc;
	}

	// Get page data
	char *pData;
	rc = pfPageHandle.GetData(pData);
	if (rc != OK_RC){
		ixIndexHandle->pfFileHandle.UnpinPage(pageNum);
		PrintError(rc);
		return rc;
	}

	PageNum nextPage;
	char* ptr = pData;

	// Set nextPage to bucket page first
	ptr += sizeof(int);
	memcpy(&nextPage, ptr, sizeof(PageNum));

	// if currently at leaf page, store right leaf
	if (!inBucket){
		ptr += 3 * sizeof(PageNum);
		memcpy(&rightLeaf, ptr, sizeof(PageNum));
	}

	// If no bucket page, go to right leaf neighbor
	if (nextPage == IX_NO_PAGE){
		nextPage = rightLeaf;
		inBucket = false;
	} 
	// Else, say in bucket
	else {
		inBucket = true;
	}

	//// Clean up.
	pData = NULL;
	ptr = NULL;
	rc = ixIndexHandle->pfFileHandle.UnpinPage(pageNum);
	if (rc != OK_RC){
		PrintError(rc);
		return rc;
	}

	return nextPage;
}