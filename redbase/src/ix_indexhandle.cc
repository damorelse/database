#include <cstring>
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
	PageNum entryPageNum;
	RC rc = rid.GetPageNum(entryPageNum);
	if (rc != OK_RC)
		return rc;
	SlotNum entrySlotNum;
	rc = rid.GetSlotNum(entrySlotNum);
	if (rc != OK_RC)
		return rc;
	// End check input.
	
	// Recursive call
	PageNum newChildPage = IX_NO_PAGE;
	char* newAttribute = new char[ixIndexHeader.attrLength];
	rc = InsertEntryHelper(ixIndexHeader.rootPage, ixIndexHeader.height, attribute, rid, newChildPage, newAttribute);
	if (rc != OK_RC)
		return rc;

	// Root node was split, need to add level to index
	if (newChildPage != IX_NO_PAGE){
		// Create new root
		PageNum pageNum;
		char *pData;
		//RC rc = CreatePage(pfFileHandle, pageNum, pData);
		PF_PageHandle pfPageHandle;
		RC rc = pfFileHandle.AllocatePage(pfPageHandle);
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
			pfFileHandle.UnpinPage(pageNum);
			PrintError(rc);
			return rc;
		}

		// Write root header info
		char* ptr = pData;
		int intTmp = 1;
		memcpy(ptr, &intTmp, sizeof(int));

		// Write data, key<old root> attribute<newAttribute> key<newChildPage>
		ptr += sizeof(int);
		memcpy(ptr, &ixIndexHeader.rootPage, sizeof(PageNum));
		ptr += sizeof(PageNum);
		memcpy(ptr, &newAttribute, ixIndexHeader.attrLength);
		ptr += ixIndexHeader.attrLength;
		memcpy(ptr, &newChildPage, sizeof(PageNum));

		// Mark page as dirty
		rc = pfFileHandle.MarkDirty(pageNum);
		if (rc != OK_RC){
			pfFileHandle.UnpinPage(pageNum);
			PrintError(rc);
			return rc;
		}

		// Clean up.
		pData = NULL;
		ptr = NULL;
		rc = pfFileHandle.UnpinPage(pageNum);
		if (rc != OK_RC){
			PrintError(rc);
			return rc;
		}

		// Modify index header info
		modified = true;
		ixIndexHeader.rootPage = pageNum;
		ixIndexHeader.height += 1;
	}

	// Clean up
	delete [] newAttribute;

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

	// Recursive delete call
	PageNum oldPage = IX_NO_PAGE;
	rc = DeleteEntryHelper(IX_NO_PAGE, ixIndexHeader.rootPage, ixIndexHeader.height, attribute, rid, oldPage);
	if (rc != OK_RC)
		return rc;

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
		// Get page data
		char *pData;
		//RC rc = GetPage(pfFileHandle, 0, pData);
		PF_PageHandle pfPageHandle = PF_PageHandle();
		RC rc = pfFileHandle.GetThisPage(0, pfPageHandle);
		if (rc != OK_RC){
			PrintError(rc);
			return rc;
		}
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
			pfFileHandle.UnpinPage(0);
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
char* IX_IndexHandle::GetEntryPtr(char* pData, const SlotNum slotNum) const
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

RC IX_IndexHandle::InsertEntryHelper(PageNum currPage, int height, void* attribute, const RID &rid, PageNum &newChildPage, void* newAttribute)
{
	// Get page data
	char *pData;
	//RC rc = GetPage(pfFileHandle, currPage, pData);
	PF_PageHandle pfPageHandle = PF_PageHandle();
	RC rc = pfFileHandle.GetThisPage(currPage, pfPageHandle);
	if (rc != OK_RC){
		PrintError(rc);
		return rc;
	}
	rc = pfPageHandle.GetData(pData);
	if (rc != OK_RC){
		pfFileHandle.UnpinPage(currPage);
		PrintError(rc);
		return rc;
	}

	// If at an internal node...
	if (height != 0){
		PageNum nextPage;
		int numKeys;
		SlotNum insertKeyIndex;
		ChooseSubtree(pData, attribute, nextPage, numKeys, insertKeyIndex);

		// Recursively insert entry
		newChildPage = IX_NO_PAGE;
		rc = InsertEntryHelper(nextPage, height - 1, attribute, rid, newChildPage, newAttribute);
		if (rc != OK_RC){
			pfFileHandle.UnpinPage(currPage);
			return rc;
		}

		// Usual case, didn't split child
		if (newChildPage == IX_NO_PAGE){
			pData = NULL;
			rc = pfFileHandle.UnpinPage(currPage);
			if (rc != OK_RC){
				PrintError(rc);
				return rc;
			}

			return OK_RC;
		}

		// We split child, must insert newChildEntry in N
		// If N has space... usual case
		if (numKeys - 1 < ixIndexHeader.maxKeyIndex){
			rc = InternalInsert(currPage, newChildPage, newAttribute, insertKeyIndex);
			if (rc != OK_RC){
				pfFileHandle.UnpinPage(currPage);
				return rc;
			}

			// Set newChildPage and newAttribute to null
			newChildPage = IX_NO_PAGE;

			pData = NULL;
			rc = pfFileHandle.UnpinPage(currPage);
			if (rc != OK_RC){
				PrintError(rc);
				return rc;
			}

			return OK_RC;
		}
		// note difference wrt splitting of leaf page!
		else {
			// Split N
			// Create N2
			PageNum newPage;
			char* newPData;
			//rc = CreatePage(pfFileHandle, newPage, newPData);
			PF_PageHandle pfPageHandle;
			RC rc = pfFileHandle.AllocatePage(pfPageHandle);
			if (rc != OK_RC){
				rc = pfFileHandle.UnpinPage(currPage);
				PrintError(rc);
				return rc;
			}
			rc = pfPageHandle.GetPageNum(newPage);
			if (rc != OK_RC){
				rc = pfFileHandle.UnpinPage(currPage);
				PrintError(rc);
				return rc;
			}
			rc = pfPageHandle.GetData(newPData);
			if (rc != OK_RC){
				pfFileHandle.UnpinPage(newPage);
				rc = pfFileHandle.UnpinPage(currPage);
				PrintError(rc);
				return rc;
			}

			// Initialize state
			char* copyBack;
			int copyBackSize;
			MakeKeyCopyBack(pData, insertKeyIndex, newChildPage, newAttribute, copyBack, copyBackSize, numKeys);
			SlotNum middleKeyIndex = numKeys / 2;
			
			// Store first half of keys in N
			int keySize = ixIndexHeader.attrLength + sizeof(PageNum);
			int newNumKeys = middleKeyIndex;
			int newCopyBackSize = sizeof(PageNum) + newNumKeys * keySize;
			WriteInternalFromKeyCopyBack(pData, copyBack, newCopyBackSize, newNumKeys);

			// Copy middle attribute, updating newChildPage and newAttribute
			char* ptr = copyBack + newCopyBackSize;
			memcpy(newAttribute, ptr, ixIndexHeader.attrLength);
			newChildPage = newPage;

			// Write rest of entries to new node N2
			ptr += ixIndexHeader.attrLength;
			newNumKeys = numKeys - newNumKeys - 1;
			newCopyBackSize = (ixIndexHeader.attrLength + sizeof(PageNum) + sizeof(SlotNum)) * (newNumKeys);
			WriteLeafFromEntryCopyBack(newPData, ptr, newCopyBackSize, newNumKeys);

			
			// Mark N and N2 pages as dirty
			rc = pfFileHandle.MarkDirty(currPage);
			if (rc != OK_RC){
				pfFileHandle.UnpinPage(currPage);
				pfFileHandle.UnpinPage(newChildPage);
				PrintError(rc);
				return rc;
			}
			rc = pfFileHandle.MarkDirty(newChildPage);
			if (rc != OK_RC){
				pfFileHandle.UnpinPage(currPage);
				pfFileHandle.UnpinPage(newChildPage);
				PrintError(rc);
				return rc;
			}

			// Clean up
			pData = NULL;
			newPData = NULL;
			delete [] copyBack;
			rc = pfFileHandle.UnpinPage(currPage);
			if (rc != OK_RC){
				pfFileHandle.UnpinPage(newChildPage);
				PrintError(rc);
				return rc;
			}
			pfFileHandle.UnpinPage(newChildPage);
			if (rc != OK_RC){
				PrintError(rc);
				return rc;
			}

			return OK_RC;
		}
	}
	// If at a leaf node...
	else {
		// Get number of entries
		int numEntries;
		memcpy(&numEntries, pData, sizeof(int));

		// If L has space... usual case
		if (numEntries - 1 < ixIndexHeader.maxEntryIndex){
			rc = LeafInsert(currPage, attribute, rid);
			if (rc != OK_RC){
				pfFileHandle.UnpinPage(currPage);
				return rc;
			}

			// Set newChildPage and newAttribute to null
			newChildPage = IX_NO_PAGE;

			pData = NULL;
			rc = pfFileHandle.UnpinPage(currPage);
			if (rc != OK_RC){
				PrintError(rc);
				return rc;
			}

			return OK_RC;
		}
		// Leaf is full, special case for bucket chaining
		else if (ShouldBucket(attribute, pData)){
			PageNum lastPage = currPage;
			char* lastData = pData;
			GetLastPageInBucketChain(lastPage, lastData); // TODO: unpinning currPage pages

			// Get number of entries in last bucket page
			int lastEntries;
			memcpy(&lastEntries, lastData, sizeof(int));

			// Bucket page full
			if (lastEntries - 1 == ixIndexHeader.maxEntryIndex){
				// Create new bucket page
				PageNum newBucket;
				char *newBucketData;
				//RC rc = CreatePage(pfFileHandle, newBucket, newBucketData);
				PF_PageHandle pfPageHandle;
				RC rc = pfFileHandle.AllocatePage(pfPageHandle);
				if (rc != OK_RC){
					PrintError(rc);
					return rc;
				}
				rc = pfPageHandle.GetPageNum(newBucket);
				if (rc != OK_RC){
					PrintError(rc);
					return rc;
				}
				// Get page data
				rc = pfPageHandle.GetData(newBucketData);
				if (rc != OK_RC){
					pfFileHandle.UnpinPage(newBucket);
					PrintError(rc);
					return rc;
				}

				// Set new bucket page's header
				char* ptr = newBucketData;
				int intTmp = 0;
				memcpy(ptr, &intTmp, sizeof(int)); // numEntries

				ptr += sizeof(int);
				PageNum pageTmp = IX_NO_PAGE;
				memcpy(ptr, &pageTmp, sizeof(PageNum)); //nextBucketPage

				ptr += sizeof(PageNum);
				memcpy(ptr, &pageTmp, sizeof(PageNum)); //leftLeaf
				
				ptr += sizeof(PageNum);
				memcpy(ptr, &pageTmp, sizeof(PageNum)); //rightLeaf

				for (SlotNum i = 0; i <= ixIndexHeader.maxEntryIndex; ++i) //bitSlots
					SetSlotBitValue(newBucketData, i, false);
				// End set new bucket page's header

				// Insert entry into new leaf page
				LeafInsert(newBucket, attribute, rid);

				// Change lastPage's header nextBucketPage
				memcpy(lastData + sizeof(int), &newBucket, sizeof(PageNum));

				// Mark pages as dirty
				rc = pfFileHandle.MarkDirty(lastPage);
				if (rc != OK_RC){
					pfFileHandle.UnpinPage(lastPage);
					pfFileHandle.UnpinPage(newBucket);
					PrintError(rc);
					return rc;
				}
				rc = pfFileHandle.MarkDirty(newBucket);
				if (rc != OK_RC){
					pfFileHandle.UnpinPage(newBucket);
					PrintError(rc);
					return rc;
				}

				// Clean up
				newBucketData = NULL;
				rc = pfFileHandle.UnpinPage(newBucket);
				if (rc != OK_RC){
					pfFileHandle.UnpinPage(lastPage);
					PrintError(rc);
					return rc;
				}
			}
			// Bucket page not full
			else {
				rc = LeafInsert(lastPage, attribute, rid);
				if (rc != OK_RC){
					pfFileHandle.UnpinPage(lastPage);
					return rc;
				}
			}

			pData = NULL;
			lastData = NULL;
			rc = pfFileHandle.UnpinPage(lastPage);
			if (rc != OK_RC){
				PrintError(rc);
				return rc;
			}

			// Set newChildPage to null
			newChildPage = IX_NO_PAGE;
			return OK_RC;
		}
		// Once in a while, the leaf is full
		else
		{
			// Split L
			// Make L2 page, set newChildPage
			char *newPData;
			//rc = CreatePage(pfFileHandle, newChildPage, newPData);
			PF_PageHandle pfPageHandle;
			RC rc = pfFileHandle.AllocatePage(pfPageHandle);
			if (rc != OK_RC){
				PrintError(rc);
				return rc;
			}
			rc = pfPageHandle.GetPageNum(newChildPage);
			if (rc != OK_RC){
				PrintError(rc);
				return rc;
			}
			// Get page data
			rc = pfPageHandle.GetData(newPData);
			if (rc != OK_RC){
				pfFileHandle.UnpinPage(newChildPage);
				PrintError(rc);
				return rc;
			}

			// Initialize state
			char* copyBack;
			int copyBackSize;
			int numEntries;
			MakeEntryCopyBack(pData, attribute, rid, copyBack, copyBackSize, numEntries);

			// Determine where to split
			int newNumEntries = numEntries / 2;
			int entrySize = ixIndexHeader.attrLength + sizeof(PageNum) + sizeof(SlotNum);
			int entryItr = newNumEntries - 1;
			while (entryItr > -1 && AttributeEqualEntry(copyBack + newNumEntries * entrySize, copyBack + entryItr * entrySize))
				entryItr -= 1;
			if (entryItr == -1){
				entryItr = newNumEntries + 1;
				while (entryItr < numEntries && AttributeEqualEntry(copyBack + newNumEntries * entrySize, copyBack + entryItr * entrySize))
					entryItr += 1;
			}
			if (entryItr < newNumEntries)
				newNumEntries = entryItr + 1;
			else
				newNumEntries = entryItr;

			// Set L2's bucket page to empty, default
			PageNum bucketPage = IX_NO_PAGE;
			memcpy(newPData + sizeof(int), &bucketPage, sizeof(PageNum));
			// Check if bucket page exists
			memcpy(&bucketPage, pData + sizeof(int), sizeof(PageNum));
			if (bucketPage != IX_NO_PAGE){
				// Bucket page must be moved
				if (newNumEntries == 1){
					memcpy(newPData + sizeof(int), &bucketPage, sizeof(PageNum));
					bucketPage = IX_NO_PAGE;
					memcpy(pData + sizeof(int), &bucketPage, sizeof(PageNum));
				}
			}

			// Write first d entries to L
			int newSize = (ixIndexHeader.attrLength + sizeof(PageNum) + sizeof(SlotNum)) * (newNumEntries);
			WriteLeafFromEntryCopyBack(pData, copyBack, newSize, newNumEntries);

			// Write rest of entries to new node L2
			char* ptr = copyBack + newSize;
			newNumEntries = numEntries - newNumEntries;
			newSize = (ixIndexHeader.attrLength + sizeof(PageNum) + sizeof(SlotNum)) * (newNumEntries);
			WriteLeafFromEntryCopyBack(newPData, ptr, newSize, newNumEntries);



			// Set newAttribute
			memcpy(newAttribute, ptr, ixIndexHeader.attrLength);

			// Set sibling pointers
			rc = SetSiblingPointers(currPage, newChildPage, pData, newPData);
			if (rc != OK_RC){
				pfFileHandle.UnpinPage(currPage);
				pfFileHandle.UnpinPage(newChildPage);
				return rc;
			}

			// Mark L and L2 pages as dirty
			rc = pfFileHandle.MarkDirty(currPage);
			if (rc != OK_RC){
				pfFileHandle.UnpinPage(currPage);
				pfFileHandle.UnpinPage(newChildPage);
				PrintError(rc);
				return rc;
			}
			rc = pfFileHandle.MarkDirty(newChildPage);
			if (rc != OK_RC){
				pfFileHandle.UnpinPage(currPage);
				pfFileHandle.UnpinPage(newChildPage);
				PrintError(rc);
				return rc;
			}

			// Clean up
			pData = NULL;
			newPData = NULL;
			delete [] copyBack;
			rc = pfFileHandle.UnpinPage(currPage);
			if (rc != OK_RC){
				pfFileHandle.UnpinPage(newChildPage);
				PrintError(rc);
				return rc;
			}
			pfFileHandle.UnpinPage(newChildPage);
			if (rc != OK_RC){
				PrintError(rc);
				return rc;
			}

			return OK_RC;

		}
	}

	newChildPage = IX_NO_PAGE;
	return OK_RC;
}

// Assumes there is free space in internal
RC IX_IndexHandle::InternalInsert(PageNum pageNum, PageNum &newChildPage, void* newAttribute, SlotNum keyNum)
{
	// Get page data
	char *pData;
	//RC rc = GetPage(pfFileHandle, pageNum, pData);
	PF_PageHandle pfPageHandle = PF_PageHandle();
	RC rc = pfFileHandle.GetThisPage(pageNum, pfPageHandle);
	if (rc != OK_RC){
		PrintError(rc);
		return rc;
	}
	rc = pfPageHandle.GetData(pData);
	if (rc != OK_RC){
		pfFileHandle.UnpinPage(pageNum);
		PrintError(rc);
		return rc;
	}

	// Get number of keys
	int numKeys;
	memcpy(&numKeys, pData, sizeof(int));

	// Determine where to insert key
	char* ptr = GetKeyPtr(pData, keyNum);

	// Copy keys that needs to be shifted
	int copyBackSize = (ixIndexHeader.attrLength + sizeof(PageNum)) * (numKeys - keyNum);
	char* copyBack = new char[copyBackSize];
	memcpy(copyBack, ptr, copyBackSize);

	// Insert key
	memcpy(ptr, newAttribute, ixIndexHeader.attrLength);
	ptr += ixIndexHeader.attrLength;
	memcpy(ptr, &newChildPage, sizeof(PageNum));
			
	// Write back shifted keys
	ptr += sizeof(PageNum);
	memcpy(ptr, copyBack, copyBackSize);

	// Write back header, increase number of entries
	numKeys += 1;
	memcpy(pData, &numKeys, sizeof(int));

	// Mark page as dirty
	rc = pfFileHandle.MarkDirty(pageNum);
	if (rc != OK_RC){
		pfFileHandle.UnpinPage(pageNum);
		PrintError(rc);
		return rc;
	}

	// Clean up
	pData = NULL;
	ptr = NULL;
	delete [] copyBack;
	rc = pfFileHandle.UnpinPage(pageNum);
	if (rc != OK_RC){
		PrintError(rc);
		return rc;
	}

	return OK_RC;
}

void IX_IndexHandle::MakeKeyCopyBack(char* pData, SlotNum insertIndex, PageNum &newChildPage, void* newAttribute, char* copyBack, int &copyBackSize, int &numKeys)
{
	// Initialize state
	memcpy(&numKeys, pData, sizeof(int));

	int keySize = ixIndexHeader.attrLength + sizeof(PageNum);
	copyBackSize = sizeof(PageNum) + numKeys * (keySize);
	copyBack = new char[copyBackSize];
	char* ptr = copyBack;

	// Copy over keys to copyBack
	memcpy(ptr, pData + sizeof(int), sizeof(PageNum));
	ptr += sizeof(PageNum);
	for (SlotNum i = 0; i < numKeys; ++i){
		// Check if should insert new key first
		if (insertIndex == i){
			memcpy(ptr, newAttribute, ixIndexHeader.attrLength);
			ptr += ixIndexHeader.attrLength;
			memcpy(ptr, &newChildPage, sizeof(PageNum));
			ptr += sizeof(PageNum);
		}
		memcpy(ptr, GetKeyPtr(pData, i), keySize);
		ptr += keySize;
	}
	// Check if new key has been inserted
	if (insertIndex == numKeys){
		memcpy(ptr, newAttribute, ixIndexHeader.attrLength);
		ptr += ixIndexHeader.attrLength;
		memcpy(ptr, &newChildPage, sizeof(PageNum));
		ptr += sizeof(PageNum);
	}

	// Increment numKeys
	numKeys += 1;
}

void IX_IndexHandle::WriteInternalFromKeyCopyBack(char* pData, char* copyBack, int copyBackSize, int numKeys)
{
	// Write copyBack back
	char* ptr = pData + ixIndexHeader.internalHeaderSize;
	memcpy(ptr, copyBack, copyBackSize);

	// Write back header
	// numEntries
	memcpy(pData, &numKeys, sizeof(int));

	// Clean up
	ptr = NULL;
}




// Assume there is free space in leaf
// Assumes leaf header is already set up
RC IX_IndexHandle::LeafInsert(PageNum pageNum, void* attribute, const RID &rid)
{
	// Get page data
	char *pData;
	//RC rc = GetPage(pfFileHandle, pageNum, pData);
	PF_PageHandle pfPageHandle = PF_PageHandle();
	RC rc = pfFileHandle.GetThisPage(pageNum, pfPageHandle);
	if (rc != OK_RC){
		PrintError(rc);
		return rc;
	}
	rc = pfPageHandle.GetData(pData);
	if (rc != OK_RC){
		pfFileHandle.UnpinPage(pageNum);
		PrintError(rc);
		return rc;
	}

	char* copyBack;
	int copyBackSize;
	int numEntries;
	MakeEntryCopyBack(pData, attribute, rid, copyBack, copyBackSize, numEntries);

	WriteLeafFromEntryCopyBack(pData, copyBack, copyBackSize, numEntries);

	// Mark page as dirty
	rc = pfFileHandle.MarkDirty(pageNum);
	if (rc != OK_RC){
		PrintError(rc);
		return rc;
	}

	// Clean up
	pData = NULL;
	delete [] copyBack;
	rc = pfFileHandle.UnpinPage(pageNum);
	if (rc != OK_RC){
		PrintError(rc);
		return rc;
	}

	return OK_RC;
}

void IX_IndexHandle::MakeEntryCopyBack(char* pData, void* attribute, const RID &rid, char* copyBack, int &copyBackSize, int &numEntries){
	// Read in attribute, covert attribute to correct type
	int a_i;
	float a_f;
	string a_s;
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

	// Initialize state
	memcpy(&numEntries, pData, sizeof(int));
	numEntries += 1;
	int entrySize = ixIndexHeader.attrLength + sizeof(PageNum) + sizeof(SlotNum);

	copyBackSize = (numEntries) * entrySize;
	copyBack = new char[copyBackSize];

	char* copyBackPtr = copyBack;
	char* ptr;
	bool inserted = false;

	// Determine where to insert new entry
	for (SlotNum readIndex = 0; readIndex < ixIndexHeader.maxEntryIndex; ++readIndex){
		if (GetSlotBitValue(pData, readIndex)){
			// if not inserted, determine if should insert now
			if(!inserted){
				bool insertNow = false;

				// Read in value, covert value to correct type
				int v_i;
				float v_f;
				string v_s;
				switch(ixIndexHeader.attrType) {
				case INT:
					memcpy(&v_i, ptr, ixIndexHeader.attrLength);
					insertNow = (a_i < v_i);
					break;
				case FLOAT:
					memcpy(&v_f, ptr, ixIndexHeader.attrLength);
					insertNow = (a_f < v_f);
					break;
				case STRING:
					char* tmp = new char[ixIndexHeader.attrLength];
					memcpy(tmp, ptr, ixIndexHeader.attrLength);
					v_s = string(tmp);
					delete [] tmp;
					insertNow = (a_s < v_s);
					break;
				}

				if (insertNow){
					// Write new entry to copyBack
					memcpy(copyBackPtr, attribute, ixIndexHeader.attrLength);
					copyBackPtr += ixIndexHeader.attrLength;
					memcpy(copyBackPtr, &rid.pageNum, sizeof(PageNum));
					copyBackPtr += sizeof(PageNum);
					memcpy(copyBackPtr, &rid.slotNum, sizeof(SlotNum));
					copyBackPtr += sizeof(SlotNum);

					// Set inserted
					inserted = true;
				}
			}

			// Write entry to copyBack
			ptr = GetEntryPtr(pData, readIndex);
			memcpy(copyBackPtr, ptr, entrySize);
			copyBackPtr += entrySize;
		}
	}

	// If still not inserted, insert new entry into copyBack
	if (!inserted){
		// Write new entry to copyBack
		memcpy(copyBackPtr, attribute, ixIndexHeader.attrLength);
		copyBackPtr += ixIndexHeader.attrLength;
		memcpy(copyBackPtr, &rid.pageNum, sizeof(PageNum));
		copyBackPtr += sizeof(PageNum);
		memcpy(copyBackPtr, &rid.slotNum, sizeof(SlotNum));
		copyBackPtr += sizeof(SlotNum);

		// Set inserted
		inserted = true;
	}

	// Clean up
	copyBackPtr = NULL;
	ptr = NULL;
}

void IX_IndexHandle::WriteLeafFromEntryCopyBack(char* pData, char* copyBack, int copyBackSize, int numEntries)
{
	// Write copyBack back
	char* ptr = GetEntryPtr(pData, 0);
	memcpy(ptr, copyBack, copyBackSize);

	// Write back header
	// numEntries
	memcpy(pData, &numEntries, sizeof(int));
	// bitSlots
	for(SlotNum i = 0; i < ixIndexHeader.maxEntryIndex; ++i)
		SetSlotBitValue(pData, i, (i < numEntries));

	// Clean up
	ptr = NULL;
}

RC IX_IndexHandle::SetSiblingPointers(PageNum L1Page, PageNum L2Page, char* L1, char* L2){
	int leftOffset = sizeof(int) + sizeof(PageNum);
	int rightOffset = leftOffset + sizeof(PageNum);

	// Set L2's right sibling to be L's old right sibling (L3)
	PageNum L3Page;
	memcpy(&L3Page, L1 + rightOffset, sizeof(PageNum));
	memcpy(L2 + rightOffset,  &L3Page, sizeof(PageNum));

	// Set L2's left sibling to be L
	memcpy(L2 + leftOffset,  &L1Page, sizeof(PageNum));

	// Set L's right sibling to be L2
	memcpy(L1 + rightOffset, &L2Page, sizeof(PageNum));

	// Set L3's left sibling to be L2
	char *L3;
	//RC rc = GetPage(pfFileHandle, L3Page, L3);
	PF_PageHandle pfPageHandle = PF_PageHandle();
	RC rc = pfFileHandle.GetThisPage(L3Page, pfPageHandle);
	if (rc != OK_RC){
		PrintError(rc);
		return rc;
	}
	rc = pfPageHandle.GetData(L3);
	if (rc != OK_RC){
		pfFileHandle.UnpinPage(L3Page);
		PrintError(rc);
		return rc;
	}

	memcpy(L3 + leftOffset, &L2Page, sizeof(PageNum));

	// Clean up
	L3 = NULL;
	rc = pfFileHandle.UnpinPage(L3Page);
	if (rc != OK_RC){
		PrintError(rc);
		return rc;
	}

	return OK_RC;
}

bool IX_IndexHandle::ShouldBucket(void* attribute, char* pData){
	char* first = GetEntryPtr(pData, 0);
	char* last = GetEntryPtr(pData, ixIndexHeader.maxEntryIndex);
	bool bucket = false;

	int a_i, f_i, l_i;
	float a_f, f_f, l_f;
	string a_s, f_s, l_s;
	switch(ixIndexHeader.attrType) {
	case INT:
		memcpy(&a_i, attribute, ixIndexHeader.attrLength);
		memcpy(&f_i, first, ixIndexHeader.attrLength);
		memcpy(&l_i, last, ixIndexHeader.attrLength);
		bucket = (a_i == f_i && a_i == l_i);
        break;
	case FLOAT:
		memcpy(&a_f, attribute, ixIndexHeader.attrLength);
		memcpy(&f_f, first, ixIndexHeader.attrLength);
		memcpy(&l_f, last, ixIndexHeader.attrLength);
		bucket = (a_f == f_f && a_f == l_f);
        break;
	case STRING:
		a_s = string((char*)attribute);
		char* tmp = new char[ixIndexHeader.attrLength];
		memcpy(tmp, first, ixIndexHeader.attrLength);
		f_s = string(tmp);
		memcpy(tmp, last, ixIndexHeader.attrLength);
		l_s = string(tmp);
		delete [] tmp;
		bucket = (a_s == f_s && a_s == l_s);
        break;
	}
	
	return bucket;
}





RC IX_IndexHandle::DeleteEntryHelper(PageNum parentPage, PageNum currPage, int height, void* attribute, const RID &rid, PageNum &oldPage)
{
	// Get page data
	char *pData;
	//RC rc = GetPage(pfFileHandle, currPage, pData);
	PF_PageHandle pfPageHandle = PF_PageHandle();
	RC rc = pfFileHandle.GetThisPage(currPage, pfPageHandle);
	if (rc != OK_RC){
		PrintError(rc);
		return rc;
	}
	rc = pfPageHandle.GetData(pData);
	if (rc != OK_RC){
		pfFileHandle.UnpinPage(currPage);
		PrintError(rc);
		return rc;
	}

	// If at an internal node...
	if (height != 0){
		// Choose subtree
		PageNum nextPage;
		int numKeys;
		SlotNum deleteKeyIndex;
		ChooseSubtree(pData, attribute, nextPage, numKeys, deleteKeyIndex);

		// Recursive delete
		DeleteEntryHelper(currPage, nextPage, height-1, attribute, rid, oldPage);

		// Usual case, child not deleted
		if (oldPage == IX_NO_PAGE){
			rc = pfFileHandle.UnpinPage(currPage);
			if (rc != OK_RC){
				PrintError(rc);
				return rc;
			}
			return OK_RC;
		}
		// We discarded child node
		else{
			rc = InternalDelete(pData, deleteKeyIndex, numKeys);

			// Node not empty, usual case
			if (numKeys != 0){
				// Mark page as dirty
				rc = pfFileHandle.MarkDirty(currPage);
				if (rc != OK_RC){
					pfFileHandle.UnpinPage(currPage);
					PrintError(rc);
					return rc;
				}

				// Delete doesn't go further
				oldPage = IX_NO_PAGE;

				// Clean up
				rc = pfFileHandle.UnpinPage(currPage);
				if (rc != OK_RC){
					PrintError(rc);
					return rc;
				}
				return OK_RC;
			}
			// Delete node
			else {
				// If deleting root
				if (currPage == ixIndexHeader.rootPage){
					// Set root to last existing page pointer
					modified = true;
					memcpy(&ixIndexHeader.rootPage, pData + ixIndexHeader.internalHeaderSize, sizeof(PageNum));
				}

				rc = pfFileHandle.UnpinPage(currPage);
				if (rc != OK_RC){
					PrintError(rc);
					return rc;
				}
				rc = pfFileHandle.DisposePage(currPage);
				if (rc != OK_RC){
					PrintError(rc);
					return rc;
				}

				oldPage = currPage;
				return OK_RC;
			}
		}
	}
	// Leaf node
	else {
		// Delete entry
		int numEntries;
		rc = LeafDelete(currPage, pData, attribute, rid, numEntries);
		if (rc != OK_RC){
			pfFileHandle.UnpinPage(currPage);
			PrintError(rc);
			return rc;
		}

		// If leaf not empty or is root page
		if (numEntries != 0 || currPage == ixIndexHeader.rootPage){
			// Mark page as dirty
			rc = pfFileHandle.MarkDirty(currPage);
			if (rc != OK_RC){
				pfFileHandle.UnpinPage(currPage);
				PrintError(rc);
				return rc;
			}

			// Delete doesn't go further
			oldPage = IX_NO_PAGE;

			// Clean up
			rc = pfFileHandle.UnpinPage(currPage);
			if (rc != OK_RC){
				PrintError(rc);
				return rc;
			}
			return OK_RC;
		}
		// Leaf now empty
		else {
			// Delete page
			rc = pfFileHandle.UnpinPage(currPage);
			if (rc != OK_RC){
				PrintError(rc);
				return rc;
			}
			rc = pfFileHandle.DisposePage(currPage);
			if (rc != OK_RC){
				PrintError(rc);
				return rc;
			}

			oldPage = currPage;
			return OK_RC;
		}
	}

	oldPage = IX_NO_PAGE;
	return OK_RC;
}

RC IX_IndexHandle::InternalDelete(char* pData, SlotNum deleteKeyIndex, int &numKeys)
{
	// Move pointer to page pointer before key
	int keySize = ixIndexHeader.attrLength + sizeof(PageNum);
	int totalSize =  sizeof(PageNum) + numKeys * keySize;
	int skipOffset = sizeof(PageNum) + deleteKeyIndex * keySize - sizeof(PageNum);

	// If deleted last page pointer
	if (deleteKeyIndex == numKeys){
		// Delete previous entry
		skipOffset -= ixIndexHeader.attrLength;
	}

	// Delete page pointer and an entry
	char* destPtr = pData + skipOffset;
	char* srcPtr = pData + skipOffset + ixIndexHeader.attrLength + sizeof(PageNum);

	int difference = skipOffset + ixIndexHeader.attrLength + sizeof(PageNum);
	int size = totalSize - difference;
	memcpy(destPtr, srcPtr, size);

	// Update numKeys in header
	numKeys -= 1;
	memcpy(pData, &numKeys, sizeof(int));

	return OK_RC;
}
RC IX_IndexHandle::LeafDelete(PageNum currPage, char* pData, void* attribute, const RID &rid, int &numEntries)
{
	PageNum deletePage = currPage;
	SlotNum deleteSlot = 0;
	char* deleteData = pData;
	bool found = false;
	RC rc;

	int attrLength = ixIndexHeader.attrLength;
	while (!found){
		if(GetSlotBitValue(deleteData, deleteSlot)){
			char* ptr = GetEntryPtr(deleteData, deleteSlot);

			PageNum v_page;
			memcpy(&v_page, ptr + attrLength, sizeof(PageNum));
			SlotNum v_slot;
			memcpy(&v_slot, ptr + attrLength + sizeof(PageNum), sizeof(SlotNum));
			
			int a_i, v_i;
			float a_f, v_f;
			string a_s, v_s;
			switch(ixIndexHeader.attrType) {
			case INT:
				memcpy(&a_i, ptr, attrLength);
				memcpy(&v_i, attribute, attrLength);
				found = (a_i == v_i && rid.pageNum == v_page && rid.slotNum == v_slot);
                break;
			case FLOAT:
				memcpy(&a_f, ptr, attrLength);
				memcpy(&v_f, attribute, attrLength);
				found = (a_f == v_f && rid.pageNum == v_page && rid.slotNum == v_slot);
                break;
			case STRING:
				char* tmp = new char[attrLength];
				memcpy(tmp, ptr, attrLength);
				a_s = string(tmp);
				v_s = string((char*)attribute);
				delete [] tmp;
				found = (a_s == v_s && rid.pageNum == v_page && rid.slotNum == v_slot);
                break;
			}
		}

		// If not found, increment slot
		if (!found){
			deleteSlot += 1;
			// If past last slot, increment page
			if (deleteSlot > ixIndexHeader.maxEntryIndex){
				// Unpin page
				if (deletePage != currPage){
					rc = pfFileHandle.UnpinPage(deletePage);
					if (rc != OK_RC){
						PrintError(rc);
						return rc;
					}
				}

				// Get next bucket page
				memcpy(&deletePage, deleteData + sizeof(int), sizeof(PageNum));
				deleteSlot = 0;

				// No next page, not found
				if (deletePage == IX_NO_PAGE){
					PrintError(IX_ENTRYDNE);
					return IX_ENTRYDNE;
				}

				// Get new page data
				//rc = GetPage(pfFileHandle, deletePage, deleteData);
				PF_PageHandle pfPageHandle = PF_PageHandle();
				rc = pfFileHandle.GetThisPage(deletePage, pfPageHandle);
				if (rc != OK_RC){
					PrintError(rc);
					return rc;
				}
				rc = pfPageHandle.GetData(deleteData);
				if (rc != OK_RC){
					pfFileHandle.UnpinPage(deletePage);
					PrintError(rc);
					return rc;
				}
			}
		}
	}

	// Not found
	if (!found){
		PrintError(IX_ENTRYDNE);
		return IX_ENTRYDNE;
	}

	// Found matching entry. Delete
	SetSlotBitValue(deleteData, deleteSlot, false);

	// If delete in last bucket page, done.
	PageNum pageTmp;
	memcpy(&pageTmp, deleteData + sizeof(int), sizeof(PageNum));
	if (pageTmp == IX_NO_PAGE){
		numEntries -= 1;
		memcpy(deleteData, &numEntries, sizeof(int));

		rc = pfFileHandle.MarkDirty(deletePage);
		if (rc != OK_RC){
			pfFileHandle.UnpinPage(deletePage);
			PrintError(rc);
			return rc;
		}

		rc = pfFileHandle.UnpinPage(deletePage);
		if (rc != OK_RC){
			PrintError(rc);
			return rc;
		}

		return OK_RC;
	}

	// Find page num of last bucket page
	PageNum notLastPage = deletePage;
	char* notLastData;
	PageNum lastPage;
	char* lastData;
	
	//rc = GetPage(pfFileHandle, notLastPage, notLastData);
	PF_PageHandle pfPageHandle = PF_PageHandle();
	rc = pfFileHandle.GetThisPage(notLastPage, pfPageHandle);
	if (rc != OK_RC){
		PrintError(rc);
		return rc;
	}
	rc = pfPageHandle.GetData(notLastData);
	if (rc != OK_RC){
		pfFileHandle.UnpinPage(notLastPage);
		PrintError(rc);
		return rc;
	}

	memcpy(&lastPage, notLastData + sizeof(int), sizeof(PageNum));
	//rc = GetPage(pfFileHandle, lastPage, lastData);
	pfPageHandle = PF_PageHandle();
	rc = pfFileHandle.GetThisPage(lastPage, pfPageHandle);
	if (rc != OK_RC){
		PrintError(rc);
		return rc;
	}
	rc = pfPageHandle.GetData(lastData);
	if (rc != OK_RC){
		pfFileHandle.UnpinPage(lastPage);
		PrintError(rc);
		return rc;
	}

	memcpy(&pageTmp, lastData + sizeof(int), sizeof(PageNum));
	while(pageTmp != IX_NO_PAGE){
		rc = pfFileHandle.UnpinPage(notLastPage);
		if (rc != OK_RC){
			PrintError(rc);
			return rc;
		}
		notLastPage = lastPage;
		notLastData = lastData;

		lastPage = pageTmp;
		//rc = GetPage(pfFileHandle, lastPage, lastData);
		PF_PageHandle pfPageHandle = PF_PageHandle();
		rc = pfFileHandle.GetThisPage(lastPage, pfPageHandle);
		if (rc != OK_RC){
			PrintError(rc);
			return rc;
		}
		rc = pfPageHandle.GetData(lastData);
		if (rc != OK_RC){
			pfFileHandle.UnpinPage(lastPage);
			PrintError(rc);
			return rc;
		}

		memcpy(&pageTmp, lastData + sizeof(int), sizeof(PageNum));
	}

	// Find an entry
	SlotNum lastSlot = 0;
	while(!GetSlotBitValue(lastData, lastSlot))
		lastSlot += 1;

	// Copy entry over
	int entrySize = attrLength + sizeof(PageNum) + sizeof(SlotNum);
	memcpy(GetEntryPtr(deleteData, deleteSlot), GetEntryPtr(lastData, lastSlot), entrySize);
	SetSlotBitValue(deleteData, deleteSlot, true);
	SetSlotBitValue(lastData, lastSlot, false);

	// Update bucket page
	int lastEntries;
	memcpy(&lastEntries, lastData, sizeof(int));
	lastEntries -= 1;
	// bucket page not empty
	if (lastEntries > 0){
		memcpy(lastData, &lastEntries, sizeof(int));

		rc = pfFileHandle.MarkDirty(lastPage);
		if (rc != OK_RC){
			pfFileHandle.UnpinPage(lastPage);
			PrintError(rc);
			return rc;
		}

		rc = pfFileHandle.UnpinPage(lastPage);
		if (rc != OK_RC){
			PrintError(rc);
			return rc;
		}

		rc = pfFileHandle.UnpinPage(notLastPage);
		if (rc != OK_RC){
			PrintError(rc);
			return rc;
		}
	}
	// If bucket page now empty, update previous bucket page's nextbucketpage
	else {
		// Delete last bucket page
		rc = pfFileHandle.UnpinPage(lastPage);
		if (rc != OK_RC){
			PrintError(rc);
			return rc;
		}
		rc = pfFileHandle.DisposePage(lastPage);
		if (rc != OK_RC){
			PrintError(rc);
			return rc;
		}

		// Update previous bucket page's nextBucketPage
		int intTmp = IX_NO_PAGE;
		memcpy(notLastData+sizeof(int), &intTmp, sizeof(PageNum));

		if (notLastPage != deletePage){
			rc = pfFileHandle.MarkDirty(notLastPage);
			if (rc != OK_RC){
				pfFileHandle.UnpinPage(deletePage);
				pfFileHandle.UnpinPage(notLastPage);
				PrintError(rc);
				return rc;
			}

			rc = pfFileHandle.UnpinPage(notLastPage);
			if (rc != OK_RC){
				pfFileHandle.UnpinPage(deletePage);
				PrintError(rc);
				return rc;
			}
		}
	}

	rc = pfFileHandle.MarkDirty(deletePage);
	if (rc != OK_RC){
		pfFileHandle.UnpinPage(deletePage);
		PrintError(rc);
		return rc;
	}

	rc = pfFileHandle.UnpinPage(deletePage);
	if (rc != OK_RC){
		PrintError(rc);
		return rc;
	}
	return OK_RC;
}




void IX_IndexHandle::ChooseSubtree(char* pData, void* attribute, PageNum &nextPage, int &numKeys, SlotNum &keyNum)
{
	char* ptr;

	// Convert attribute to correct type
	int a_i;
	float a_f;
	string a_s;
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

	// Get number of keys
	memcpy(&numKeys, pData, sizeof(int));

	// Iterate through keys until find first key greater than attribute.
	bool greaterThan = false;
	keyNum = 0;
	for (; !greaterThan && keyNum < numKeys; ++keyNum){
		ptr = GetKeyPtr(pData, keyNum);

		// Convert key into correct form, check if greater than
		int k_i;
		float k_f;
		string k_s;
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
		}
	}

	// Did not find a greaterThan key, copy last page pointer
	if (!greaterThan){
		ptr += ixIndexHeader.attrLength;
		memcpy(&nextPage, ptr, sizeof(PageNum));
	}
}

RC IX_IndexHandle::GetLastPageInBucketChain(PageNum &lastPage, char* lastData)
{
	PageNum firstPage = lastPage;
	int bucketOffset = sizeof(int);
	PageNum bucket;
	RC rc;

	// Find last page in bucket chain
	do {
		// Check if there is a next bucket page
		memcpy(&bucket, lastData + bucketOffset, sizeof(PageNum));

		// Next bucket page exists
		if (bucket != IX_NO_PAGE){
			if (lastPage != firstPage){
				// Unpin last page
				rc = pfFileHandle.UnpinPage(lastPage);
				if (rc != OK_RC){
					PrintError(rc);
					return rc;
				}
				lastData = NULL;
			}

			// Update lastPage
			lastPage = bucket;

			// Get new page data
			//rc = GetPage(pfFileHandle, lastPage, lastData);
			PF_PageHandle pfPageHandle = PF_PageHandle();
			rc = pfFileHandle.GetThisPage(lastPage, pfPageHandle);
			if (rc != OK_RC){
				PrintError(rc);
				return rc;
			}
			rc = pfPageHandle.GetData(lastData);
			if (rc != OK_RC){
				pfFileHandle.UnpinPage(lastPage);
				PrintError(rc);
				return rc;
			}
		}
	} while( bucket != IX_NO_PAGE);

	return OK_RC;
}

bool IX_IndexHandle::AttributeEqualEntry(char* one, char* two){
	bool equal = false;
	int attrLength = ixIndexHeader.attrLength;

	int a_i, v_i;
	float a_f, v_f;
	string a_s, v_s;
	switch(ixIndexHeader.attrType) {
	case INT:
		memcpy(&a_i, one, attrLength);
		memcpy(&v_i, two, attrLength);
		equal = (a_i == v_i);
        break;
	case FLOAT:
		memcpy(&a_f, one, attrLength);
		memcpy(&v_f, two, attrLength);
		equal = (a_f == v_f);
        break;
	case STRING:
		char* tmp = new char[attrLength];
		memcpy(tmp, one, attrLength);
		a_s = string(tmp);
		memcpy(tmp, two, attrLength);
		v_s = string(tmp);
		delete [] tmp;
		equal = (a_s == v_s);
        break;
	}

	return equal;
}

// Assume one and two points directly to attributes
bool IX_IndexHandle::AttrSatisfiesCondition(void* one, CompOp compOp, void* two, AttrType attrType, int attrLength) const
{
	// If no condition, true
	if (compOp == NO_OP)
		return true;

	// Covert attribute and value to correct type
	int o_i, t_i;
	float o_f, t_f;
	string o_s, t_s;
	switch(attrType) {
	case INT:
		memcpy(&o_i, one, attrLength);
		memcpy(&t_i, two, attrLength);
        break;
	case FLOAT:
		memcpy(&o_f, one, attrLength);
		memcpy(&t_f, two, attrLength);
        break;
	case STRING:
		char* tmp = new char[attrLength];
		memcpy(tmp, one, attrLength);
		o_s = string(tmp);
		memcpy(tmp, two, attrLength);
		t_s = string(tmp);
		delete [] tmp;
        break;
	}

	// Check if fulfills condition
	bool result = false;
	switch(compOp) {
	case EQ_OP:
		switch(attrType) {
		case INT:
			result = (o_i == t_i);
            break;
		case FLOAT:
			result = (o_f == t_f);
            break;
		case STRING:
			result = (o_s == t_s);
            break;
		}
        break;
	case LT_OP:
		switch(attrType) {
		case INT:
			result = (o_i < t_i);
            break;
		case FLOAT:
			result = (o_f < t_f);
            break;
		case STRING:
			result = (o_s < t_s);
            break;
		}
        break;
	case GT_OP:
		switch(attrType) {
		case INT:
			result = (o_i > t_i);
            break;
		case FLOAT:
			result = (o_f > t_f);
            break;
		case STRING:
			result = (o_s > t_s);
            break;
		}
        break;
	case LE_OP:
		switch(attrType) {
		case INT:
			result = (o_i <= t_i);
            break;
		case FLOAT:
			result = (o_f <= t_f);
            break;
		case STRING:
			result = (o_s <= t_s);
            break;
		}
        break;
	case GE_OP:
		switch(attrType) {
		case INT:
			result = (o_i >= t_i);
            break;
		case FLOAT:
			result = (o_f >= t_f);
            break;
		case STRING:
			result = (o_s >= t_s);
            break;
		}
        break;
	case NE_OP:
		switch(attrType) {
		case INT:
			result = (o_i != t_i);
            break;
		case FLOAT:
			result = (o_f != t_f);
            break;
		case STRING:
			result = (o_s != t_s);
            break;
		}
        break;
	}

	return result;
}