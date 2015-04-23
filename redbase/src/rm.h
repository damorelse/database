//
// rm.h
//
//   Record Manager component interface
//
// This file does not include the interface for the RID class.  This is
// found in rm_rid.h
//

#ifndef RM_H
#define RM_H

// Please DO NOT include any files other than redbase.h and pf.h in this
// file.  When you submit your code, the test program will be compiled
// with your rm.h and your redbase.h, along with the standard pf.h that
// was given to you.  Your rm.h, your redbase.h, and the standard pf.h
// should therefore be self-contained (i.e., should not depend upon
// declarations in any other file).

// Do not change the following includes
#include "redbase.h"
#include "rm_rid.h"
#include "pf.h"

//
// RM_Record: RM Record interface
//
class RM_Record {
	friend class RM_FileHandle;
	friend class RM_FileScan;
	public:
		RM_Record ();
		~RM_Record();
		RM_Record (const RM_Record &other);
		RM_Record& operator=  (const RM_Record &other);

		// Return the data corresponding to the record.  Sets *pData to the
		// record contents.
		RC GetData(char *&pData) const;

		// Return the RID associated with the record
		RC GetRid (RID &rid) const;

	private: 
		char * recordCopy;
		RID rid;
		size_t length;
};


#define RM_PAGE_LIST_END  -1           // end of list of free pages
#define RM_PAGE_FULL      -2           // no free space in page
#define RM_BIT_START	  sizeof(int)  //bit slots page offset
const int RM_FILE_HDR_SIZE = PF_PAGE_SIZE;

struct RM_FileHeader {
	size_t recordSize;      // in bytes
	size_t maxSlot;
	size_t maxPage;	        // CHANGES
	int firstFreeSpace;     // page num, CHANGES
	size_t pageHeaderSize;  // in bytes

	RM_FileHeader(): recordSize(0), maxSlot(0), maxPage(0), firstFreeSpace(RM_PAGE_LIST_END), pageHeaderSize(0){}
};
struct RM_PageHeader {
	int nextFreeSpace;      // page num
	char *slotsBits;
};

//
// RM_FileHandle: RM File interface
//
class RM_FileHandle {
	friend class RM_Manager;
	friend class RM_FileScan;
	public:
		RM_FileHandle ();
		~RM_FileHandle();
		RM_FileHandle(const RM_FileHandle &other);
		RM_FileHandle& operator= (const RM_FileHandle &other);

		// Given a RID, return the record
		RC GetRec     (const RID &rid, RM_Record &rec) const;

		RC InsertRec  (const char *pData, RID &rid);       // Insert a new record

		RC DeleteRec  (const RID &rid);                    // Delete a record
		RC UpdateRec  (const RM_Record &rec);              // Update a record

		// Forces a page (along with any contents stored in this class)
		// from the buffer pool to disk.  Default value forces all pages.
		RC ForcePages (PageNum pageNum = ALL_PAGES);

private:
	bool open;
	bool modified;
	PF_FileHandle pfFileHandle;
	RM_FileHeader rmFileHeader;

	bool GetSlotBitValue(char* pData, const SlotNum slotNum) const;   // Read a specific record's bit value in page header
	void SetSlotBitValue(char* pData, const SlotNum slotNum, bool b); // Write a specific record's bit value in page header
	char* GetRecordPtr(char* pData, const SlotNum slotNum) const;     // Gets a pointer to a specific record's start location
};

//
// RM_FileScan: condition-based scan of records in the file
//
class RM_FileScan {
public:
    RM_FileScan  ();
    ~RM_FileScan ();

    RC OpenScan  (const RM_FileHandle &fileHandle,
                  AttrType   attrType,
                  int        attrLength,
                  int        attrOffset,
                  CompOp     compOp,
                  void       *value,
                  ClientHint pinHint = NO_HINT); // Initialize a file scan
    RC GetNextRec(RM_Record &rec);               // Get next matching record
    RC CloseScan ();                             // Close the scan

private:
	bool open;
	PageNum pageNum;
	SlotNum slotNum;

	const RM_FileHandle* rmFileHandle;
	AttrType attrType;
	int attrLength;
	int attrOffset;
	CompOp compOp;
	void* value;
};

//
// RM_Manager: provides RM file management
//
class RM_Manager {
public:
    RM_Manager    (PF_Manager &pfm);
    ~RM_Manager   ();

    RC CreateFile (const char *fileName, int recordSize);
    RC DestroyFile(const char *fileName);
    RC OpenFile   (const char *fileName, RM_FileHandle &fileHandle);

    RC CloseFile  (RM_FileHandle &fileHandle);
private:
	PF_Manager* pfm;

	size_t CalculateMaxSlots(int recordSize);  //Calculate max number of records that will fit in one page
};

//
// Print-error function
//
void RM_PrintError(RC rc);

#define RM_RECORDNOTREAD        (START_RM_WARN + 1)  
#define RM_FILENOTOPEN			(START_RM_WARN + 2)  
#define RM_RECORD_DNE			(START_RM_WARN + 3)  
#define RM_EOF					(START_RM_WARN + 4)
#define RM_SCANNOTOPEN			(START_RM_WARN + 5)
#define RM_INVALIDSCANCOMBO		(START_RM_WARN + 6)
#define RM_LASTWARN		RM_SCANNOTOPEN

#define RM_RECORDSIZE           (START_RM_ERR - 0)
#define RM_FILENAMELEN          (START_RM_ERR - 1) 
#define RM_INPUTNULL			(START_RM_ERR - 2)	
#define RM_STRLEN				(START_RM_ERR - 3)
#define RM_MEMVIOLATION			(START_RM_ERR - 4)
#define RM_INVALIDENUM			(START_RM_ERR - 5)
#define RM_NUMLEN			(START_RM_ERR - 6)
#define RM_LASTERROR	RM_NUMLEN

#endif
