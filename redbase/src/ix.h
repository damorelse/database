//
// ix.h
//
//   Index Manager Component Interface
//

#ifndef IX_H
#define IX_H

// Please do not include any other files than the ones below in this file.

#include "redbase.h"  // Please don't change these lines
#include "rm_rid.h"  // Please don't change these lines
#include "pf.h"

// Internal
#define IX_NO_PAGE -1
struct IX_IndexHeader{
	PageNum rootPage;   // CHANGES
	int height;         // CHANGES
	AttrType attrType;
	int attrLength;
	SlotNum maxKeyIndex;
	SlotNum maxEntryIndex;
	int internalHeaderSize;
	int leafHeaderSize;

	IX_IndexHeader(){
		rootPage = IX_NO_PAGE;
		height = 0;
		attrType = (AttrType) -1;
		attrLength = 0;
		maxKeyIndex = -1;
		maxEntryIndex = -1;
		internalHeaderSize = 0;
		leafHeaderSize = 0;
	}
};
struct IX_InternalHeader{
	int numKeys;			// CHANGES
};
// format: <header> ptr {key ptr} ...
#define IX_BIT_START sizeof(int) + 3*sizeof(PageNum)
struct IX_LeafHeader{
	int numEntries;         // CHANGES
	PageNum nextBucketPage; // CHANGES
	PageNum leftLeaf;		// CHANGES
	PageNum rightLeaf;		// CHANGES
	char* bitSlots;			// CHANGES
};
// format: <header> {key page slot} ...
// End Internal

//
// IX_IndexHandle: IX Index File interface
//
class IX_IndexHandle {
	friend class IX_Manager;
	friend class IX_IndexScan;
public:
    IX_IndexHandle();
    ~IX_IndexHandle();

    // Insert a new index entry
    RC InsertEntry(void *attribute, const RID &rid);

    // Delete a new index entry
    RC DeleteEntry(void *attribute, const RID &rid);

    // Force index files to disk
    RC ForcePages();

private:
	bool open;
	bool modified;
	PF_FileHandle pfFileHandle;
	IX_IndexHeader ixIndexHeader;

	char* GetKeyPtr(char* pData, const SlotNum slotNum) const;        // Gets a pointer to a specific key's start location
	char* GetEntryPtr(char* pData, const SlotNum slotNum) const;     // Gets a pointer to a specific entry's start location
	bool GetSlotBitValue(char* pData, const SlotNum slotNum) const;   // Read a specific entry's bit value in page header
	void SetSlotBitValue(char* pData, const SlotNum slotNum, bool b); // Write a specific entry's bit value in page header

	// Insert helper functions
	RC InsertEntryHelper(PageNum currPage, int height, void* attribute, const RID &rid, PageNum &newChildPage, void* newAttribute);
	// Internal insert
	RC InternalInsert(PageNum pageNum, PageNum &newChildPage, void* newAttribute, SlotNum keyNum);
	void MakeKeyCopyBack(char* pData, SlotNum insertIndex, PageNum &newChildPage, void* newAttribute, char* copyBack, int &copyBackSize, int &numKeys);
	void WriteInternalFromKeyCopyBack(char* pData, char* copyBack, int copyBackSize, int numKeys);
	// Leaf insert
	RC LeafInsert(PageNum pageNum, void* attribute, const RID &rid);
	void MakeEntryCopyBack(char* pData, void* attribute, const RID &rid, char* copyBack, int &copyBackSize, int &numEntries);
	void WriteLeafFromEntryCopyBack(char* pData, char* copyBack, int copyBackSize, int numEntries);
	RC SetSiblingPointers(PageNum L1Page, PageNum L2Page, char* L1, char* L2);
	// Bucket insert
	bool ShouldBucket(void* attribute, char* pData);

	// Delete helper functions
	RC DeleteEntryHelper(PageNum parentPage, PageNum currPage, int height, void* attribute, const RID &rid, PageNum &oldPage);
	RC InternalDelete(char* pData, SlotNum deleteKeyIndex, int &numKeys);
	RC LeafDelete(PageNum currPage, char* pData, void* attribute, const RID &rid, int &numEntries);

	// Both Insert/Delete helper functions
	void ChooseSubtree(char* pData, void* attribute, PageNum &nextPage, int &numKeys, SlotNum &keyNum);
	RC GetLastPageInBucketChain(PageNum &lastPage, char* lastData);
	bool AttributeEqualEntry(char* one, char* two);
	bool AttrSatisfiesCondition(void* one, CompOp compOp, void* two, AttrType attrType, int attrLength) const;
};

//
// IX_IndexScan: condition-based scan of index entries
//
class IX_IndexScan {
public:
    IX_IndexScan();
    ~IX_IndexScan();

    // Open index scan
    RC OpenScan(const IX_IndexHandle &indexHandle,
                CompOp compOp,
                void *value,
                ClientHint  pinHint = NO_HINT);

    // Get the next matching entry return IX_EOF if no more matching
    // entries.
    RC GetNextEntry(RID &rid);

    // Close index scan
    RC CloseScan();

private:
	const IX_IndexHandle* ixIndexHandle;
	CompOp compOp;
	void *value;

	bool open;
	PageNum pageNum;
	SlotNum entryNum;
	PageNum rightLeaf;
	bool inBucket;
	bool finished;
	int entrySize;
	char* lastEntry;

	RC FindLeafNode(void* attribute, PageNum &resultPage) const;
	RC FindMinLeafNode(PageNum &resultPage) const;
	RC FindLeafNodeHelper(PageNum currPage, int currHeight, bool findMin, void* attribute, PageNum &resultPage) const;

	RC GetNextPage(PageNum pageNum, PageNum &resultPage);
};

//
// IX_Manager: provides IX index file management
//
class IX_Manager {
public:
    IX_Manager(PF_Manager &pfm);
    ~IX_Manager();

    // Create a new Index
    RC CreateIndex(const char *fileName, int indexNo,
                   AttrType attrType, int attrLength);

    // Destroy and Index
    RC DestroyIndex(const char *fileName, int indexNo);

    // Open an Index
    RC OpenIndex(const char *fileName, int indexNo,
                 IX_IndexHandle &indexHandle);

    // Close an Index
    RC CloseIndex(IX_IndexHandle &indexHandle);

private:
	PF_Manager* pfManager;

	const char* GetIndexFileName(const char *fileName, int indexNo);

	int CalculateMaxKeys(int attrLength);  //Calculate max number of entries that will fit in one page
	int CalculateMaxEntries(int attrLength);  //Calculate max number of entries that will fit in one page

	RC CreateEmptyRoot(PF_FileHandle pfFileHandle, int attrLength, PageNum &resultPage);

	// TODO: temp
	//RC CreatePage(PF_FileHandle fileHandle, PageNum &pageNum, char* pData);
    //RC GetPage(PF_FileHandle fileHandle, PageNum pageNum, char* pData) const;
};

//
// Print-error function
//
void IX_PrintError(RC rc);

#define IX_FILENOTOPEN           (START_IX_WARN + 0)  
#define IX_ENTRYDNE				 (START_IX_WARN + 1)
#define IX_FILESCANREOPEN		 (START_IX_WARN + 2)
#define IX_EOF					 (START_IX_WARN + 3)
#define IX_LASTWARN		END_IX_WARN                  //TODO: set

#define IX_INVALIDENUM           (START_IX_ERR - 0)
#define IX_NULLINPUT			 (START_IX_ERR - 1)
#define IX_STRLEN				 (START_IX_ERR - 2)
#define IX_NUMLEN				 (START_IX_ERR - 3)
#define IX_FILENAMELEN			 (START_IX_ERR - 4)
#define IX_INVALIDSCANCOMBO      (START_IX_ERR - 5)
#define IX_LASTERROR	END_IX_ERR                   //TODO: set


#endif
