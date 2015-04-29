#include <cstring>
#include <iostream>
#include <string>
#include "ix.h"

using namespace std;

IX_IndexScan::IX_IndexScan(): ixIndexHandle(NULL), compOp(NO_OP), value(NULL), open(false), pageNum(-1), entryNum(-1), rightLeaf(-1), inBucket(false), finished(false), entrySize(0), lastEntry(NULL)
{}
IX_IndexScan::~IX_IndexScan()
{
	delete [] lastEntry;
	lastEntry = NULL;
}

// Open index scan
RC IX_IndexScan::OpenScan(const IX_IndexHandle &indexHandle,
						  CompOp compOp,
						  void *value,
						  ClientHint  pinHint)
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
	RC rc;
	if (compOp == NO_OP || compOp == LT_OP || compOp == LE_OP)
		rc = FindMinLeafNode(pageNum);
	else //(compOp == EQ_OP || compOp == GE_OP || compOp == GT_OP)
		rc = FindLeafNode(value, pageNum);
	if (rc != OK_RC)
		return rc;

	open = true;
	entryNum = -1;
	rightLeaf = IX_NO_PAGE;
	inBucket = false;
	finished = false;
	entrySize = ixIndexHandle->ixIndexHeader.attrLength + sizeof(PageNum) + sizeof(SlotNum);
	lastEntry = new char[entrySize];

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

	// CHANGES TO ACCOMODATE DELETES DURING INDEXSCAN
	// Initialize page handle and page data
	//rc = GetPage(ixIndexHandle->pfFileHandle, pageNum, pData);
	char* pData;
	PF_PageHandle pfPageHandle = PF_PageHandle();
	RC rc = ixIndexHandle->pfFileHandle.GetThisPage(pageNum, pfPageHandle);
	if (rc != OK_RC){
		PrintError(rc);
		return rc;
	}

	rc = pfPageHandle.GetData(pData);
	if (rc != OK_RC){
		ixIndexHandle->pfFileHandle.UnpinPage(pageNum);
		PrintError(rc);
		return rc;
	}

	PageNum prevPage = pageNum;

	// Determine whether to increment entry iterator
	bool increment = false;
	// Entry now empty
	if (entryNum == -1 || !ixIndexHandle->GetSlotBitValue(pData, entryNum))
		increment = true;
	// Entry still filled
	else {
		// Read previous entry
		char* charArrTmp = new char[entrySize];
		memcpy(charArrTmp, ixIndexHandle->GetEntryPtr(pData, entryNum), entrySize);

		// If previous entry is unchanged, need to increment entry iterator
		if (memcmp(charArrTmp, lastEntry, entrySize) == 0)
			increment = true;

		delete [] charArrTmp;
	}
	if (increment){
		// Increment entry
		entryNum += 1;
		// If entry now out of range
		if (entryNum > ixIndexHandle->ixIndexHeader.maxEntryIndex){
			entryNum = 0;
			// Find next page
			rc = GetNextPage(prevPage, pageNum);
			if (rc != OK_RC){
				ixIndexHandle->pfFileHandle.UnpinPage(prevPage);
				return rc;
			}			
		}
	}
	// End determine whether to increment entry iterator

	// If switched to new page, clean up and update pData
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
			// Get new page data
			//rc = GetPage(ixIndexHandle->pfFileHandle, pageNum, pData);
			PF_PageHandle pfPageHandle = PF_PageHandle();
			RC rc = ixIndexHandle->pfFileHandle.GetThisPage(pageNum, pfPageHandle);
			if (rc != OK_RC){
				PrintError(rc);
				return rc;
			}
			rc = pfPageHandle.GetData(pData);
			if (rc != OK_RC){
				ixIndexHandle->pfFileHandle.UnpinPage(pageNum);
				PrintError(rc);
				return rc;
			}
		}
		// No more entries to read, EOF
		else {
			finished = true;
			PrintError(IX_EOF);
			return IX_EOF;
		}
	}
	// END CHANGES TO ACCOMODATE DELETES DURING INDEXSCAN
	//cerr << "scan: A" << endl;
	// Iterate through pages and entries until find one that satisfies condition (or EOF)
	bool found = false;
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
		//cerr << "scan: B" << endl;
		// Entry not found and scan not finished
		// Increment entry num
		prevPage = pageNum;
		entryNum += 1;
		if (entryNum > ixIndexHandle->ixIndexHeader.maxEntryIndex){
			rc = GetNextPage(prevPage, pageNum);
			if (rc != OK_RC){
				ixIndexHandle->pfFileHandle.UnpinPage(prevPage);
				return rc;
			}
			entryNum = 0;
		}
		//cerr << "scan: C" << endl;
		// If switched to new page...clean up and update pData
		if (prevPage != pageNum){
			// Unpin last page
			rc = ixIndexHandle->pfFileHandle.UnpinPage(prevPage);
			if (rc != OK_RC){
				PrintError(rc);
				return rc;
			}
			pData = NULL;
			//cerr << "scan: C" << endl;
			// If more entries to read...
			if (pageNum != IX_NO_PAGE){
				// Get new page handle and page data
				//rc = GetPage(ixIndexHandle->pfFileHandle, pageNum, pData);
				PF_PageHandle pfPageHandle = PF_PageHandle();
				RC rc = ixIndexHandle->pfFileHandle.GetThisPage(pageNum, pfPageHandle);
				if (rc != OK_RC){
					PrintError(rc);
					return rc;
				}
				rc = pfPageHandle.GetData(pData);
				if (rc != OK_RC){
					ixIndexHandle->pfFileHandle.UnpinPage(pageNum);
					PrintError(rc);
					return rc;
				}
			}
			// No more entries to read
			else
				finished = true;
			//cerr << "scan: D" << endl;
		}
	}
	
	// After loop
	// Scan finished, EOF
	if (finished){
		//if (prevPage == pageNum){
		//cerr << "scan: E" << endl;
		if (pageNum != IX_NO_PAGE){
			rc = ixIndexHandle->pfFileHandle.UnpinPage(pageNum);
			if (rc != OK_RC){
				PrintError(rc);
				return rc;
			}
		}
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
	memcpy(lastEntry, ixIndexHandle->GetEntryPtr(pData, entryNum), entrySize);
	//cerr << "scan: F" << endl;
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
	if (!finished){
		PrintError(IX_SCANNOTFINISHED);
		return IX_SCANNOTFINISHED;
	}

	// Set state
	ixIndexHandle = NULL;
	open = false;
	delete [] lastEntry;
	lastEntry = NULL;

	return OK_RC;
}

RC IX_IndexScan::FindLeafNode(void* attribute, PageNum &resultPage) const
{
	return FindLeafNodeHelper(ixIndexHandle->ixIndexHeader.rootPage, ixIndexHandle->ixIndexHeader.height, false, attribute, resultPage);
}
RC IX_IndexScan::FindMinLeafNode(PageNum &resultPage) const
{
	return FindLeafNodeHelper(ixIndexHandle->ixIndexHeader.rootPage, ixIndexHandle->ixIndexHeader.height, true, NULL, resultPage);
}
RC IX_IndexScan::FindLeafNodeHelper(PageNum currPage, int currHeight, bool findMin, void* attribute, PageNum &resultPage) const
{
	// At leaf page, done.
	if (currHeight == 0){
		resultPage = currPage;
		return OK_RC;
	}

	// At internal page
	// Get page handle
	char *pData;
	//RC rc = GetPage(ixIndexHandle->pfFileHandle, currPage, pData);
	PF_PageHandle pfPageHandle = PF_PageHandle();
	RC rc = ixIndexHandle->pfFileHandle.GetThisPage(currPage, pfPageHandle);
	if (rc != OK_RC){
		PrintError(rc);
		return rc;
	}
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
		// Get number of keys
		int numKeys;
		memcpy(&numKeys, pData, sizeof(int));

		// Iterate through keys until find first key greater than attribute.
		bool greaterThan = false;
		for (SlotNum keyNum = 0; keyNum < numKeys; ++keyNum){
			// Get key pointer
			ptr = ixIndexHandle->GetKeyPtr(pData, keyNum);

			// Check if key is greater than attribute
			greaterThan = ixIndexHandle->AttrSatisfiesCondition(attribute, LT_OP, ptr, ixIndexHandle->ixIndexHeader.attrType, ixIndexHandle->ixIndexHeader.attrLength);

			// Found first greaterthan key, copy preceding page pointer
			if (greaterThan){
				ptr -= sizeof(PageNum);
				memcpy(&nextPage, ptr, sizeof(PageNum));
				break;
			}
		}

		// Did not find a greaterThan key, copy last page pointer
		if (!greaterThan){
			ptr = ixIndexHandle->GetKeyPtr(pData, numKeys) - sizeof(PageNum);
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
	return FindLeafNodeHelper(nextPage, currHeight - 1, findMin, attribute, resultPage);
}

// Assumes at a leaf or bucket node
RC IX_IndexScan::GetNextPage(PageNum pageNum, PageNum &resultPage)
{
	// Get page data
	char *pData;
	//RC rc = GetPage(ixIndexHandle->pfFileHandle, pageNum, pData);
	PF_PageHandle pfPageHandle = PF_PageHandle();
	RC rc = ixIndexHandle->pfFileHandle.GetThisPage(pageNum, pfPageHandle);
	if (rc != OK_RC){
		PrintError(rc);
		return rc;
	}
	rc = pfPageHandle.GetData(pData);
	if (rc != OK_RC){
		ixIndexHandle->pfFileHandle.UnpinPage(pageNum);
		PrintError(rc);
		return rc;
	}

	PageNum nextPage;

	// Set nextPage to bucket page first
	char* ptr = pData + sizeof(int);
	memcpy(&nextPage, ptr, sizeof(PageNum));

	// if currently at leaf page, store right leaf
	if (!inBucket){
		ptr += 2 * sizeof(PageNum);
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

	resultPage = nextPage;
	return OK_RC;
}
