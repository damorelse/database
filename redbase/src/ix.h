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
	int numKeys;
	PageNum parentPage;
};
// format: <header> ptr {key ptr} ...
#define IX_BIT_START sizeof(int) + 4*sizeof(PageNum)
struct IX_LeafHeader{
	int numEntries;
	PageNum nextBucketPage;
	PageNum parentPage;
	PageNum leftLeaf;
	PageNum rightLeaf;
	char* bitSlots;
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
	char* GetRecordPtr(char* pData, const SlotNum slotNum) const;     // Gets a pointer to a specific entry's start location
	bool GetSlotBitValue(char* pData, const SlotNum slotNum) const;   // Read a specific entry's bit value in page header
	void SetSlotBitValue(char* pData, const SlotNum slotNum, bool b); // Write a specific entry's bit value in page header

	PageNum FindLeafNode(void* attribute) const;
	PageNum FindLeafNodeHelper(PageNum currPage, int currHeight, bool findMin, void* attribute) const;
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
	bool open;
	const IX_IndexHandle* ixIndexHandle;
	CompOp compOp;
	void *value;

	PageNum FindMinLeafNode() const;
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
#define IX_LASTERROR	END_IX_ERR                   //TODO: set


#endif
