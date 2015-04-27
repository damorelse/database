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
	void* newAttribute = new char[ixIndexHeader.attrLength];
	rc = InsertEntryHelper(ixIndexHeader.rootPage, ixIndexHeader.height, attribute, rid, newChildPage, newAttribute);
	if (rc != OK_RC)
		return rc;

	// Root node was split, need to add level to index
	if (newChildPage != IX_NO_PAGE){
		// Create new root
		PageNum pageNum;
		char *pData;
		RC rc = pfFileHandle.CreatePage(pageNum, pData);
		if (rc != OK_RC){
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

	PageNum oldPage = IX_NO_PAGE;
	void* oldAttribute = new char[ixIndexHeader.attrLength];
	rc = DeleteEntryHelper(IX_NO_PAGE, ixIndexHeader.rootPage, ixIndexHeader.height, attribute, rid, oldPage, oldAttribute);
	if (rc != OK_RC)
		return rc;

	if (oldPage != IX_NO_PAGE){
		// TODO: check page and bucket pages for {attribute, rid}. Delete if find. Return error if not.

	}

	delete [] oldAttribute;
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
		RC rc = pfFileHandle.GetPage(0, pData);
		if (rc != OK_RC)
			return rc;
		
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
	RC rc = pfFileHandle.GetPage(currPage, pData);
	if (rc != OK_RC)
		return rc;

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
			rc = pfFileHandle.CreatePage(newPage, newPData);
			if (rc != OK_RC){
				rc = pfFileHandle.UnpinPage(currPage);
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
			newCopyBackSize = (ixIndexHeader.attrLength + sizeof(PageNum) + sizeof(SlotNum)) * (newNumEntries);
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
			int bucketOffset = sizeof(int);
			PageNum lastPage = currPage;
			char* lastData = pData;
			PageNum bucket;

			// Find last page in bucket chain
			do {
				// Check if there is a next bucket page
				memcpy(&bucket, lastData + bucketOffset, sizeof(PageNum));

				// Next bucket page exists
				if (bucket != IX_NO_PAGE){
					// Unpin last page
					rc = pfFileHandle.UnpinPage(lastPage);
					if (rc != OK_RC){
						PrintError(rc);
						return rc;
					}
					lastData = NULL;

					// Update lastPage
					lastPage = bucket;

					// Get new page data
					RC rc = pfFileHandle.GetPage(lastPage, lastData);
					if (rc != OK_RC)
						return rc;
				}
			} while( bucket != IX_NO_PAGE);

			// Get number of entries in last bucket page
			int lastEntries;
			memcpy(&lastEntries, lastData, sizeof(int));

			// Bucket page full
			if (lastEntries - 1 == ixIndexHeader.maxEntryIndex){
				// Create new bucket page
				PageNum newBucket;
				char *newBucketData;
				RC rc = pfFileHandle.CreatePage(newBucket, newBucketData);
				if (rc != OK_RC){
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
				memcpy(lastData + bucketOffset, &newBucket, sizeof(PageNum));

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

			// Set newChildPage and newAttribute to null
			newChildPage = IX_NO_PAGE;

			pData = NULL;
			lastData = NULL;
			rc = pfFileHandle.UnpinPage(lastPage);
			if (rc != OK_RC){
				PrintError(rc);
				return rc;
			}

			return OK_RC;
		}
		// Once in a while, the leaf is full
		else
		{
			// Split L
			// Make L2 page, set newChildPage
			char *newPData;
			rc = pfFileHandle.CreatePage(newChildPage, newPData);
			if (rc != OK_RC){
				pfFileHandle.UnpinPage(currPage);
				return rc;
			}

			// Initialize state
			char* copyBack;
			int copyBackSize;
			int numEntries;
			MakeEntryCopyBack(pData, attribute, rid, copyBack, copyBackSize, numEntries);

			// Write first d entries to L
			int newNumEntries = numEntries / 2;
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
}

// Assumes there is free space in internal
RC IX_IndexHandle::InternalInsert(PageNum pageNum, PageNum &newChildPage, void* newAttribute, SlotNum keyNum)
{
	// Get page data
	char *pData;
	RC rc = pfFileHandle.GetPage(pageNum, pData);
	if (rc != OK_RC)
		return rc;

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
	RC rc = pfFileHandle.GetPage(pageNum, pData);
	if (rc != OK_RC)
		return rc;

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
	int bucketOffset = sizeof(int);
	int leftOffset = sizeof(int) + sizeof(PageNum);
	int rightOffset = leftOffset + sizeof(PageNum);

	// Set L2's nextBucketPage to no page
	PageNum noPage = IX_NO_PAGE;
	memcpy(L2 + bucketOffset, &noPage, sizeof(PageNum));

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
	RC rc = pfFileHandle.GetPage(L3Page, L3);
	if (rc != OK_RC)
		return rc;

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





RC IX_IndexHandle::DeleteEntryHelper(PageNum parentPage, PageNum currPage, int height, void* attribute, const RID &rid, PageNum &oldPage, void* oldAttribute)
{
	// Get page data
	char *pData;
	RC rc = pfFileHandle.GetPage(currPage, pData);
	if (rc != OK_RC)
		return rc;

	// If at an internal node...
	if (height != 0){
		// Choose subtree
		PageNum nextPage;
		int numKeys;
		SlotNum insertKeyIndex;
		ChooseSubtree(pData, attribute, nextPage, numKeys, insertKeyIndex);

		// Recursive delete
		DeleteEntryHelper(currPage, nextPage, height-1, attribute, rid, oldPage, oldAttribute);

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
			int numKeys;
			rc = InternalDelete(pData, oldPage, oldAttribute, numKeys);

			// Node not empty, usual case
			if (numKeys != 0){
				// Delete doesn't go further
				oldPage = IX_NO_PAGE;
			}
			else {
				// TODO
			}
		}
	}
	// Leaf node
	else {
		// TODO

	}

	return OK_RC;
}

RC IX_IndexHandle::InternalDelete(char* pData, PageNum oldPage, void* oldAttribute, int &numKeys)
{

}
RC IX_IndexHandle::LeafDelete(char* pData, void* attribute, const RID &rid, int &numEntries)
{

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