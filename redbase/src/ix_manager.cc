#include <cstring>
#include <sstream>
#include "ix.h"

using namespace std;

IX_Manager::IX_Manager(PF_Manager &pfm): pfManager(&pfm)
{}
IX_Manager::~IX_Manager()
{}

// Create a new Index
RC IX_Manager::CreateIndex(const char *fileName, int indexNo,
                AttrType attrType, int attrLength)
{
	// Check input
	// Check filename is not null
	if (!fileName){
		PrintError(IX_NULLINPUT);
		return IX_NULLINPUT;
	}

	// Check filename does not exceed max relation name size and is not empty
	size_t nameLen = strlen(fileName);
	if (nameLen > MAXNAME || nameLen == 0){
		PrintError(IX_FILENAMELEN);
		return IX_FILENAMELEN;
	}

	// Given can assume indexNo is unique and positive.

	// Check attribute type is one of the three allowed
	if (attrType != INT && attrType != FLOAT && attrType != STRING){
		PrintError(IX_INVALIDENUM);
		return IX_INVALIDENUM;
	}
	// Check string attribute length is greater than 0 and less than 255 bytes
	if (attrType == STRING && (attrLength > MAXSTRINGLEN || attrLength < 1)){
		PrintError(IX_STRLEN);
		return IX_STRLEN;
	}
	// Check int or float attribute length is exactly 4 bytes
	if (attrType != STRING && attrLength != 4){
		PrintError(IX_NUMLEN);
		return IX_NUMLEN;
	}
	// End check input

	// Create file
	RC rc = pfManager->CreateFile(GetIndexFileName(fileName, indexNo));
	if (rc != OK_RC){
		PrintError(rc);
		return rc;
	}

	//TODO: initialize index file, header page?

	return OK_RC;
}

// Destroy and Index
RC IX_Manager::DestroyIndex(const char *fileName, int indexNo)
{
	// Check input parameters
	// Check filename is not null
	if (fileName == NULL){
		PrintError(IX_NULLINPUT);
		return IX_NULLINPUT;
	}
	// Check filename does not exceed max relation name size and is not empty
	size_t nameLen = strlen(fileName);
	if (nameLen > MAXNAME || nameLen == 0){
		PrintError(IX_FILENAMELEN);
		return IX_FILENAMELEN;
	}
	// Given can assume indexNo is unique and positive.
	// End check input parameters.

	// Delete file
	RC rc = pfManager->DestroyFile(GetIndexFileName(fileName, indexNo));
	if (rc != OK_RC){
		PrintError(rc);
		return rc;
	}

	return OK_RC;
}

// Open an Index
RC IX_Manager::OpenIndex(const char *fileName, int indexNo,
             IX_IndexHandle &indexHandle)
{
	// Check input parameters
	// Check filename is not null
	if (fileName == NULL){
		PrintError(IX_NULLINPUT);
		return IX_NULLINPUT;
	}
	// Check filename does not exceed max relation name size and is not empty
	size_t nameLen = strlen(fileName);
	if (nameLen > MAXNAME || nameLen == 0){
		PrintError(IX_FILENAMELEN);
		return IX_FILENAMELEN;
	}
	// Given can assume indexNo is unique and positive.
	// End check input parameters.

	// Open file handle
	RC rc = pfManager->OpenFile(GetIndexFileName(fileName, indexNo), indexHandle.pfFileHandle);
	if (rc != OK_RC){
		PrintError(rc);
		return rc;
	}

	// Initialize state
	indexHandle.open = true;

	// TODO: do more? copy over header info?

	return OK_RC;
}

// Close an Index
RC IX_Manager::CloseIndex(IX_IndexHandle &indexHandle)
{
	// Flush dirty pages
	RC rc = indexHandle.ForcePages();
	if (rc != OK_RC)
		return rc;
        
	// Close file handle.
	rc = pfManager->CloseFile(indexHandle.pfFileHandle);
	if (rc != OK_RC){
		PrintError(rc);
		return rc;
	}

	// Change state
	indexHandle.open = false;

	// TODO: do more?

	return OK_RC;
}

const char* IX_Manager::GetIndexFileName(const char *fileName, int indexNo)
{
	stringstream ss;
	ss << fileName << '.' << indexNo;
	return ss.str().c_str();
}