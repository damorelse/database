#include <cstdio>
#include <iostream>
#include <string>
#include "rm.h"

using namespace std;

RM_FileScan::RM_FileScan  (): open(false), rmFileHandle(NULL)
{
}
RM_FileScan::~RM_FileScan ()
{
	rmFileHandle = NULL;
}

RC RM_FileScan::OpenScan  (const RM_FileHandle &fileHandle,
					   	   AttrType   attrType,
						   int        attrLength,
						   int        attrOffset,
						   CompOp     compOp,
						   void       *value,
						   ClientHint pinHint = NO_HINT) // Initialize a file scan
{
	// Check input
	if (attrType == STRING && (attrLength > MAXSTRINGLEN || attrLength < 1)){
		PrintError(RM_STRLEN);
		return RM_STRLEN;
	}
	if (attrOffset < 0){
		PrintError(RM_NEGOFFSET);
		return RM_NEGOFFSET;
	}
	// End check input

	// Copy over info
	rmFileHandle = &fileHandle;
	this->attrType = attrType;
	this->attrLength = attrLength;
	this->attrOffset = attrOffset;
	this->compOp = compOp;
	this->value = value;
	
	// Setup scan params
	open = true;
	pageNum = 1;	// Skip header page
	slotNum = -1;	// Auto-increments at GetNextRec start

	return OK_RC;
}

RC RM_FileScan::GetNextRec(RM_Record &rec)               // Get next matching record
{
	bool found = false;
	PF_PageHandle pfPageHandle;
	char* pData;
	RC rc;

	// Update pageNum/slotNum
	slotNum += 1;
	if (slotNum > rmFileHandle->rmFileHeader.maxSlot){
		pageNum += 1;
		slotNum = 0;
	}

	// Check if page out of range, EOF
	if (pageNum > rmFileHandle->rmFileHeader.maxPage){
		PrintError(RM_EOF);
		return RM_EOF;
	}

	// Initialize page handle and page data
	rc = rmFileHandle->pfFileHandle.GetThisPage(pageNum, pfPageHandle);
	if (rc != OK_RC){
		PrintError(rc);
		return rc;
	}
	rc = pfPageHandle.GetData(pData);
	if (rc != OK_RC){
		PrintError(rc);
		return rc;
	}

	// Iterate through pages and records until find one
	while (!found && pageNum <= rmFileHandle->rmFileHeader.maxPage){
		// If record exists in slot
		if (rmFileHandle->GetSlotBitValue(pData, slotNum)){
			// If no condition, found
			if (value == NULL || compOp == NO_OP){
				found = true;
				break;
			}

			// Read in attribute, covert attribute and value to correct type
			char* ptr = rmFileHandle->GetRecordPtr(pData, slotNum);
			ptr += attrOffset;

			int a_i, v_i;
			float a_f, v_f;
			string a_s, v_s;
			switch(attrType) {
			case INT:
				memcpy(&a_i, ptr, attrLength);
				memcpy(&v_i, value, attrLength);
			case FLOAT:
				memcpy(&a_f, ptr, attrLength);
				memcpy(&v_f, value, attrLength);
			case STRING:
				char* tmp = new char[attrLength];
				memcpy(tmp, ptr, attrLength);
				a_s = string(tmp);
				v_s = string((char*)value);
				delete tmp;
			}

			// Check if record fulfills condition
			switch(compOp) {
			case EQ_OP:
				switch(attrType) {
				case INT:
					found = (a_i == v_i);
				case FLOAT:
					found = (a_f == v_f);
				case STRING:
					found = (a_s == v_s);
				}
			case LT_OP:
				switch(attrType) {
				case INT:
					found = (a_i < v_i);
				case FLOAT:
					found = (a_f < v_f);
				case STRING:
					found = (a_s < v_s);
				}
			case GT_OP:
				switch(attrType) {
				case INT:
					found = (a_i > v_i);
				case FLOAT:
					found = (a_f > v_f);
				case STRING:
					found = (a_s > v_s);
				}
			case LE_OP:
				switch(attrType) {
				case INT:
					found = (a_i <= v_i);
				case FLOAT:
					found = (a_f <= v_f);
				case STRING:
					found = (a_s <= v_s);
				}
			case GE_OP:
				switch(attrType) {
				case INT:
					found = (a_i >= v_i);
				case FLOAT:
					found = (a_f >= v_f);
				case STRING:
					found = (a_s >= v_s);
				}
			case NE_OP:
				switch(attrType) {
				case INT:
					found = (a_i != v_i);
				case FLOAT:
					found = (a_f != v_f);
				case STRING:
					found = (a_s != v_s);
				}
			}
		}

		if (found)
			break;

		// Record did not exist or did not satisfy condition...
		// Update pageNum/slotNum
		slotNum += 1;
		if (slotNum > rmFileHandle->rmFileHeader.maxSlot){
			pageNum += 1;
			slotNum = 0;
		}

		// If switch to new page...
		if (slotNum == 0){
			// Unpin last page
			rc = rmFileHandle->pfFileHandle.UnpinPage(pageNum-1);
			if (rc != OK_RC){
				PrintError(rc);
				return rc;
			}
			pData = NULL;

			// If still in range...
			if (pageNum <= rmFileHandle->rmFileHeader.maxPage){
				// Get new page handle and page data
				rc = rmFileHandle->pfFileHandle.GetThisPage(pageNum, pfPageHandle);
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
		}
		
	}

	// No matching record found, EOF
	if (!found){
		PrintError(RM_EOF);
		return RM_EOF;
	}

	// Copy matching record info to rec
	rec.rid.pageNum = pageNum;
	rec.rid.slotNum = slotNum;
	delete rec.recordCopy;
	int length = rmFileHandle->rmFileHeader.recordSize;
	rec.recordCopy = new char[length];
	memcpy(rec.recordCopy, rmFileHandle->GetRecordPtr(pData, slotNum), length);

	//// Clean up.
	pData = NULL;
	rc = rmFileHandle->pfFileHandle.UnpinPage(pageNum);
	if (rc != OK_RC){
		PrintError(rc);
		return rc;
	}

	return OK_RC;
}

RC RM_FileScan::CloseScan ()                            // Close the scan
{
	open = false;
	rmFileHandle = NULL;
}