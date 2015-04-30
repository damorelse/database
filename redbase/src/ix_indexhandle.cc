#include <cstring>
#include <string>
#include <iostream>
#include "ix.h"

using namespace std;

IX_IndexHandle::IX_IndexHandle(): open(false), modified(false), pfFileHandle(PF_FileHandle()), ixIndexHeader(IX_IndexHeader()){}

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
			delete [] newAttribute;
			newAttribute = NULL;
			PrintError(rc);
			return rc;
		}
		rc = pfPageHandle.GetPageNum(pageNum);
		if (rc != OK_RC){
			delete [] newAttribute;
			newAttribute = NULL;
			PrintError(rc);
			return rc;
		}
		rc = pfPageHandle.GetData(pData);
		if (rc != OK_RC){
			delete [] newAttribute;
			newAttribute = NULL;
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
		memcpy(ptr, newAttribute, ixIndexHeader.attrLength);
		ptr += ixIndexHeader.attrLength;
		memcpy(ptr, &newChildPage, sizeof(PageNum));

		// Mark page as dirty
		rc = pfFileHandle.MarkDirty(pageNum);
		if (rc != OK_RC){
			delete [] newAttribute;
			newAttribute = NULL;
			pfFileHandle.UnpinPage(pageNum);
			PrintError(rc);
			return rc;
		}

		// Clean up.
		pData = NULL;
		ptr = NULL;
		rc = pfFileHandle.UnpinPage(pageNum);
		if (rc != OK_RC){
			delete [] newAttribute;
			newAttribute = NULL;
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
	newAttribute = NULL;
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
	rc = DeleteEntryHelper(ixIndexHeader.rootPage, ixIndexHeader.height, attribute, rid, oldPage);
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

RC IX_IndexHandle::InsertEntryHelper(PageNum currPage, int height, void* attribute, const RID &rid, PageNum &newChildPage, char* &newAttribute)
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

			// Write rest of keys to new node N2
			ptr += ixIndexHeader.attrLength;
			newNumKeys = numKeys - newNumKeys - 1;
			newCopyBackSize = sizeof(PageNum) + newNumKeys * keySize;
			WriteInternalFromKeyCopyBack(newPData, ptr, newCopyBackSize, newNumKeys); //GINA HERE

			
			// Mark N and N2 pages as dirty
			rc = pfFileHandle.MarkDirty(currPage);
			if (rc != OK_RC){
				delete [] copyBack;
				copyBack = NULL;
				pfFileHandle.UnpinPage(currPage);
				pfFileHandle.UnpinPage(newChildPage);
				PrintError(rc);
				return rc;
			}
			rc = pfFileHandle.MarkDirty(newChildPage);
			if (rc != OK_RC){
				delete [] copyBack;
				copyBack = NULL;
				pfFileHandle.UnpinPage(currPage);
				pfFileHandle.UnpinPage(newChildPage);
				PrintError(rc);
				return rc;
			}

			// Clean up
			pData = NULL;
			newPData = NULL;
			delete [] copyBack;
			copyBack = NULL;
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
			// Update pinned pages to last page in bucket chain
			rc = GetLastPageInBucketChain(currPage, pData);
			if (rc != OK_RC)
				return rc;

			// Get number of entries in last bucket page
			int lastEntries;
			memcpy(&lastEntries, pData, sizeof(int));

			// Bucket page full
			if (lastEntries - 1 == ixIndexHeader.maxEntryIndex){
				// Create new bucket page
				PageNum newBucket;
				char *newBucketData;
				//RC rc = CreatePage(pfFileHandle, newBucket, newBucketData);
				PF_PageHandle pfPageHandle;
				RC rc = pfFileHandle.AllocatePage(pfPageHandle);
				if (rc != OK_RC){
					pfFileHandle.UnpinPage(currPage);
					PrintError(rc);
					return rc;
				}
				rc = pfPageHandle.GetPageNum(newBucket);
				if (rc != OK_RC){
					pfFileHandle.UnpinPage(currPage);
					PrintError(rc);
					return rc;
				}
				// Get page data
				rc = pfPageHandle.GetData(newBucketData);
				if (rc != OK_RC){
					pfFileHandle.UnpinPage(currPage);
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
				memcpy(pData + sizeof(int), &newBucket, sizeof(PageNum));

				// Mark pages as dirty
				rc = pfFileHandle.MarkDirty(currPage);
				if (rc != OK_RC){
					pfFileHandle.UnpinPage(currPage);
					pfFileHandle.UnpinPage(newBucket);
					PrintError(rc);
					return rc;
				}
				rc = pfFileHandle.MarkDirty(newBucket);
				if (rc != OK_RC){
					pfFileHandle.UnpinPage(currPage);
					pfFileHandle.UnpinPage(newBucket);
					PrintError(rc);
					return rc;
				}

				// Clean up
				newBucketData = NULL;
				rc = pfFileHandle.UnpinPage(newBucket);
				if (rc != OK_RC){
					pfFileHandle.UnpinPage(currPage);
					PrintError(rc);
					return rc;
				}
			}
			// Bucket page not full
			else {
				rc = LeafInsert(currPage, attribute, rid);
				if (rc != OK_RC){
					pfFileHandle.UnpinPage(currPage);
					return rc;
				}
			}

			pData = NULL;
			rc = pfFileHandle.UnpinPage(currPage);
			if (rc != OK_RC){
				PrintError(rc);
				return rc;
			}

			// Set newChildPage to null
			newChildPage = IX_NO_PAGE;
			return OK_RC;
		}
		// Once in a while, the leaf is full
		else {
			// Split L
			// Make L2 page, set newChildPage
			char *newPData;
			//rc = CreatePage(pfFileHandle, newChildPage, newPData);
			PF_PageHandle pfPageHandle;
			RC rc = pfFileHandle.AllocatePage(pfPageHandle);
			if (rc != OK_RC){
				pfFileHandle.UnpinPage(currPage);
				PrintError(rc);
				return rc;
			}
			//cerr << "A" << endl;
			rc = pfPageHandle.GetPageNum(newChildPage);
			if (rc != OK_RC){
				pfFileHandle.UnpinPage(currPage);
				PrintError(rc);
				return rc;
			}
			// Get page data
			rc = pfPageHandle.GetData(newPData);
			if (rc != OK_RC){
				pfFileHandle.UnpinPage(currPage);
				pfFileHandle.UnpinPage(newChildPage);
				PrintError(rc);
				return rc;
			}
			//cerr << "B" << endl;
			// Initialize state
			char* copyBack;
			int copyBackSize;
			int numEntries;
			MakeEntryCopyBack(pData, attribute, rid, copyBack, copyBackSize, numEntries);
			//cerr << "C" << endl;
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
			//cerr << "C" << endl;
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
			//cerr << "D" << endl;
			// Write first d entries to L
			int newSize = (ixIndexHeader.attrLength + sizeof(PageNum) + sizeof(SlotNum)) * (newNumEntries);
			WriteLeafFromEntryCopyBack(pData, copyBack, newSize, newNumEntries);
			//cerr << "E" << endl;
			// Write rest of entries to new node L2
			char* ptr = copyBack + newSize;
			newNumEntries = numEntries - newNumEntries;
			newSize = (ixIndexHeader.attrLength + sizeof(PageNum) + sizeof(SlotNum)) * (newNumEntries);
			WriteLeafFromEntryCopyBack(newPData, ptr, newSize, newNumEntries);
			//cerr << "F" << endl;
			// Set newAttribute
			memcpy(newAttribute, ptr, ixIndexHeader.attrLength);

			// Set sibling pointers
			rc = SetSiblingPointers(currPage, newChildPage, pData, newPData);
			if (rc != OK_RC){
				delete [] copyBack;
				copyBack = NULL;
				pfFileHandle.UnpinPage(currPage);
				pfFileHandle.UnpinPage(newChildPage);
				return rc;
			}
			//cerr << "G" << endl;
			// Mark L and L2 pages as dirty
			rc = pfFileHandle.MarkDirty(currPage);
			if (rc != OK_RC){
				delete [] copyBack;
				copyBack = NULL;
				pfFileHandle.UnpinPage(currPage);
				pfFileHandle.UnpinPage(newChildPage);
				PrintError(rc);
				return rc;
			}
			rc = pfFileHandle.MarkDirty(newChildPage);
			if (rc != OK_RC){
				delete [] copyBack;
				copyBack = NULL;
				pfFileHandle.UnpinPage(currPage);
				pfFileHandle.UnpinPage(newChildPage);
				PrintError(rc);
				return rc;
			}
			//cerr << "H" << endl;
			// Clean up
			pData = NULL;
			newPData = NULL;
			delete [] copyBack;
			copyBack = NULL;
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
RC IX_IndexHandle::InternalInsert(PageNum pageNum, PageNum newChildPage, void* newAttribute, SlotNum keyNum)
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
		delete [] copyBack;
		copyBack = NULL;
		pfFileHandle.UnpinPage(pageNum);
		PrintError(rc);
		return rc;
	}

	// Clean up
	pData = NULL;
	ptr = NULL;
	delete [] copyBack;
	copyBack = NULL;
	rc = pfFileHandle.UnpinPage(pageNum);
	if (rc != OK_RC){
		PrintError(rc);
		return rc;
	}

	return OK_RC;
}

void IX_IndexHandle::MakeKeyCopyBack(char* pData, SlotNum insertIndex, PageNum newChildPage, void* newAttribute, char* &copyBack, int &copyBackSize, int &numKeys)
{
	// Initialize state
	memcpy(&numKeys, pData, sizeof(int));

	int keySize = ixIndexHeader.attrLength + sizeof(PageNum);
	copyBackSize = sizeof(PageNum) + (numKeys+1) * (keySize);
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
	//cerr << "Leaf Insert-MakeEntryCopyBack page: " << pageNum << endl;
	MakeEntryCopyBack(pData, attribute, rid, copyBack, copyBackSize, numEntries);

	WriteLeafFromEntryCopyBack(pData, copyBack, copyBackSize, numEntries);

	// Mark page as dirty
	rc = pfFileHandle.MarkDirty(pageNum);
	if (rc != OK_RC){
		delete [] copyBack;
		copyBack = NULL;
		PrintError(rc);
		return rc;
	}

	// Clean up
	pData = NULL;
	delete [] copyBack;
	copyBack = NULL;
	rc = pfFileHandle.UnpinPage(pageNum);
	if (rc != OK_RC){
		PrintError(rc);
		return rc;
	}

	return OK_RC;
}

void IX_IndexHandle::MakeEntryCopyBack(char* pData, void* attribute, const RID &rid, char*& copyBack, int &copyBackSize, int &numEntries){
	// Initialize state
	memcpy(&numEntries, pData, sizeof(int));
	numEntries += 1;
	if (numEntries <= 0){ //TODO
		cerr << "Should be greater than 0: " << numEntries << endl;
        numEntries = 1;
    }
	int entrySize = ixIndexHeader.attrLength + sizeof(PageNum) + sizeof(SlotNum);

	copyBackSize = (numEntries) * entrySize;
	copyBack = new char[copyBackSize];

	char* copyBackPtr = copyBack;
	char* ptr;
	bool inserted = false;

	// Determine where to insert new entry
	for (SlotNum readIndex = 0; readIndex <= ixIndexHeader.maxEntryIndex; ++readIndex){
		if (GetSlotBitValue(pData, readIndex)){
            ptr = GetEntryPtr(pData, readIndex);
			// if not inserted, determine if should insert now
			if(!inserted){
				// Check if attribute < attribute
				bool insertNow = AttrSatisfiesCondition(attribute, LT_OP, ptr, ixIndexHeader.attrType, ixIndexHeader.attrLength);

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
	}
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
	for(SlotNum i = 0; i <= ixIndexHeader.maxEntryIndex; ++i)
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

	// Set L3's left sibling to be L2 (if it exists)
	if(L3Page != IX_NO_PAGE){
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
		rc = pfFileHandle.MarkDirty(L3Page);
		if (rc != OK_RC){
			pfFileHandle.UnpinPage(L3Page);
			PrintError(rc);
			return rc;
		}

		rc = pfFileHandle.UnpinPage(L3Page);
		if (rc != OK_RC){
			PrintError(rc);
			return rc;
		}
	}

	return OK_RC;
}

bool IX_IndexHandle::ShouldBucket(void* attribute, char* pData){
	char* first = GetEntryPtr(pData, 0);
	char* last = GetEntryPtr(pData, ixIndexHeader.maxEntryIndex);
	bool equal = AttributeEqualEntry(first, last);
	if (!equal)
		return false;
	return AttributeEqualEntry((char*)attribute, first);
}

RC IX_IndexHandle::DeleteEntryHelper(PageNum currPage, int height, void* attribute, const RID &rid, PageNum &oldPage)
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
		DeleteEntryHelper(nextPage, height-1, attribute, rid, oldPage);

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
			InternalDelete(pData, deleteKeyIndex, numKeys);

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
		int numEntries = 0;
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
}

// Does not mark page dirty
void IX_IndexHandle::InternalDelete(char* pData, SlotNum deleteKeyIndex, int &numKeys)
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
	char* srcPtr = pData + skipOffset + keySize;

	int difference = skipOffset + keySize;
	int size = totalSize - difference;
	memcpy(destPtr, srcPtr, size);

	// Update numKeys in header
	numKeys -= 1;
	memcpy(pData, &numKeys, sizeof(int));
}

RC IX_IndexHandle::LeafDelete(PageNum currPage, char* pData, void* attribute, const RID &rid, int &numEntries)
{
	PageNum deletePage = currPage;
	SlotNum deleteSlot = 0;
	char* deleteData = pData;
	int attrLength = ixIndexHeader.attrLength;
	RC rc;

	bool found = false;
	while (!found){
		if(GetSlotBitValue(deleteData, deleteSlot)){
			char* ptr = GetEntryPtr(deleteData, deleteSlot);

			PageNum v_page;
			memcpy(&v_page, ptr + attrLength, sizeof(PageNum));
			SlotNum v_slot;
			memcpy(&v_slot, ptr + attrLength + sizeof(PageNum), sizeof(SlotNum));
			
			found = (rid.pageNum == v_page && rid.slotNum == v_slot && AttributeEqualEntry((char*)attribute, ptr));
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

	// Found matching entry. Delete
	SetSlotBitValue(deleteData, deleteSlot, false);

	// If delete in last bucket page, done.
	PageNum pageTmp;
	memcpy(&pageTmp, deleteData + sizeof(int), sizeof(PageNum));
	if (pageTmp == IX_NO_PAGE){
		numEntries -= 1;
		memcpy(deleteData, &numEntries, sizeof(int));

		if (deletePage != currPage){ // TODO: added condition
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
		}
		return OK_RC;
	}

	// Delete was not in last bucket page
	// Find page num of last and second to last bucket page
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
		pfFileHandle.UnpinPage(notLastPage);
		PrintError(rc);
		return rc;
	}
	rc = pfPageHandle.GetData(lastData);
	if (rc != OK_RC){
		pfFileHandle.UnpinPage(notLastPage);
		pfFileHandle.UnpinPage(lastPage);
		PrintError(rc);
		return rc;
	}

	memcpy(&pageTmp, lastData + sizeof(int), sizeof(PageNum));
	while(pageTmp != IX_NO_PAGE){
		rc = pfFileHandle.UnpinPage(notLastPage);
		if (rc != OK_RC){
			pfFileHandle.UnpinPage(lastPage);
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
			pfFileHandle.UnpinPage(notLastPage);
			PrintError(rc);
			return rc;
		}
		rc = pfPageHandle.GetData(lastData);
		if (rc != OK_RC){
			pfFileHandle.UnpinPage(notLastPage);
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

	// Get number of keys
	memcpy(&numKeys, pData, sizeof(int));

	// Iterate through keys until find first key greater than attribute.
	bool greaterThan = false;
	for (keyNum = 0; keyNum < numKeys; ++keyNum){
		ptr = GetKeyPtr(pData, keyNum);

		// Check if greater than
		greaterThan = AttrSatisfiesCondition(attribute, LT_OP, ptr, ixIndexHeader.attrType, ixIndexHeader.attrLength);
		
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
}

RC IX_IndexHandle::GetLastPageInBucketChain(PageNum &currPage, char*& pData)
{
	PageNum bucket;
	do {
		// Check if there is a next bucket page
		memcpy(&bucket, pData + sizeof(int), sizeof(PageNum));

		// Next bucket page exists
		if (bucket != IX_NO_PAGE){
			// Unpin last page
			RC rc = pfFileHandle.UnpinPage(currPage);
			if (rc != OK_RC){
				PrintError(rc);
				return rc;
			}

			// Update currPage
			currPage = bucket;

			// Get new page data
			//rc = GetPage(pfFileHandle, lastPage, lastData);
			PF_PageHandle pfPageHandle = PF_PageHandle();
			rc = pfFileHandle.GetThisPage(currPage, pfPageHandle);
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
		}
	} while( bucket != IX_NO_PAGE);

	return OK_RC;
}

bool IX_IndexHandle::AttributeEqualEntry(char* one, char* two){
	return AttrSatisfiesCondition(one, EQ_OP, two, ixIndexHeader.attrType, ixIndexHeader.attrLength);
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
