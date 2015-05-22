#include <cstdio>
#include <iostream>
#include <set>
#include <vector>
#include <fstream>
#include <string>
#include <sstream>
#include <algorithm>
#include "redbase.h"
#include "sm.h"
#include "ix.h"
#include "rm.h"
#include "printer.h"

using namespace std;

SM_Manager::SM_Manager(IX_Manager &ixm, RM_Manager &rmm): ixManager(&ixm), rmManager(&rmm)
{
}

SM_Manager::~SM_Manager()
{
	ixManager = NULL;
	rmManager = NULL;
}

RC SM_Manager::OpenDb(const char *dbName)
{
	// Change current working directory to database's subdirectory
    if (chdir(dbName) < 0)
        return SM_CHDIR;

	// Open relcat and attrcat relations
	RC rc;
	if (rc = rmManager->OpenFile(RELCAT, relFile))
		return rc;
	if (rc = rmManager->OpenFile(ATTRCAT, attrFile))
		return rc;

    return (0);
}

RC SM_Manager::CloseDb()
{
	// Close relcat and attrcat relations
	RC rc;
	if (rc = rmManager->CloseFile(relFile))
		return rc;
	if (rc = rmManager->CloseFile(attrFile))
		return rc;

	// Change currect working directory to up parent directory
	if (chdir("..") < 0)
        return SM_CHDIR;

    return (0);
}

RC SM_Manager::CreateTable(const char *relName,
                           int        attrCount,
                           AttrInfo   *attributes)
{
    //cout << "CreateTable\n"
    //     << "   relName     =" << relName << "\n"
    //     << "   attrCount   =" << attrCount << "\n";
    //for (int i = 0; i < attrCount; i++)
    //    cout << "   attributes[" << i << "].attrName=" << attributes[i].attrName
    //         << "   attrType="
    //         << (attributes[i].attrType == INT ? "INT" :
    //             attributes[i].attrType == FLOAT ? "FLOAT" : "STRING")
    //         << "   attrLength=" << attributes[i].attrLength << "\n";
	
	RC rc;

	// Check input
	// Check relation name
	if (rc = CheckName(relName))
		return rc;
	if (isCatalog(relName))
		return SM_INVALIDCATACTION;

	// Check relation name uniqueness
	rc = GetRelcatRecord(relName, RM_Record());
	if (rc == 0)
		return SM_EXISTS;
	if (rc != RM_EOF)
		return rc;

	// Check attribute count
	if (attrCount < 1 || attrCount > MAXATTRS)
		return SM_ATTRNUM;

	// Check each attribute
	set<string> attrSet;
	for (int i = 0; i < attrCount; i++){
		AttrInfo attrInfo = attributes[i];

		// Check attribute type
		if (attrInfo.attrType != INT && attrInfo.attrType != FLOAT && attrInfo.attrType != STRING)
			return SM_INVALIDENUM;

		// Check attribute length
		if (attrInfo.attrType == STRING && 
			(attrInfo.attrLength < 1 || attrInfo.attrLength > MAXSTRINGLEN))
			return SM_INVALIDATTRLEN;
		if (attrInfo.attrType != STRING && attrInfo.attrLength != 4)
			return SM_INVALIDATTRLEN;

		// Check attribute name
		if (rc = CheckName(attrInfo.attrName))
			return rc;

		// Check attribute name uniqueness
		if (attrSet.find((string)attrInfo.attrName) != attrSet.end())
			return SM_EXISTS;
		attrSet.insert((string)attrInfo.attrName);
	}
	// End check input.


	// Initialize
	RID rid;
	int offset = 0;

	// Update attrcat
	for (int i = 0; i < attrCount; i++){
		Attrcat attrcat = Attrcat(relName, attributes[i].attrName, offset, attributes[i].attrType, attributes[i].attrLength, SM_INVALID);
		if (rc = attrFile.InsertRec((char*)&attrcat, rid))
			return rc;
		offset += attributes[i].attrLength;
	}

	// Update relcat
	int tupleLen = offset;
	Relcat relcat = Relcat(relName, tupleLen, attrCount, 0);
	if (rc = relFile.InsertRec((char*)&relcat, rid))
		return rc;

	// Create relation file
	if (rc = rmManager->CreateFile(relName, tupleLen))
		return rc;

    return (0);
}

RC SM_Manager::DropTable(const char *relName)
{
    //cout << "DropTable\n   relName=" << relName << "\n";
	RC rc;

	// Check input
	// Check relation name
	if (rc = CheckName(relName))
		return rc;
	if (isCatalog(relName))
		return SM_INVALIDCATACTION;
	// End check input.

	// Initialize
	RM_FileScan fileScan;
	RM_Record record;
	char* pData;
	RID rid;

	// Check if relation exists
	// Find relation's relcat info
	if (rc = GetRelcatRecord(relName, record)){
		if (rc == RM_EOF)
			return SM_DNE;
		return rc;
	}

	// Delete relation file (1/4)
	if (rc = rmManager->DestroyFile(relName))
		return rc;

	// Delete relcat entry (2/4)
	if (rc = record.GetRid(rid))
		return rc;
	if (rc = relFile.DeleteRec(rid))
		return rc;
	
	// Check if any indexes
	if (rc = record.GetData(pData))
		return rc;
	Relcat relcat(pData);
	if (relcat.indexCount == 0)
		return 0;

	// Find relation's indexes
	char relation[MAXNAME + 1];
	strcpy(relation, relName);
	int offset = (int)offsetof(struct Attrcat, relName);
	if (rc = fileScan.OpenScan(attrFile, STRING, MAXNAME, offset, EQ_OP, relation))
		return rc;
	while ( OK_RC == (rc = fileScan.GetNextRec(record))){
		// Read attribute's attrcat info
		if (rc = record.GetData(pData))
			return rc;
		Attrcat attrcat(pData);

		// Delete attrcat entry (3/4)
		if (rc = record.GetRid(rid))
			return rc;
		if (rc = attrFile.DeleteRec(rid))
			return rc;

		// Delete index file (if it exists) (4/4)
		if (attrcat.indexNo != SM_INVALID){
			if (rc = ixManager->DestroyIndex(relName, attrcat.indexNo))
				return rc;
		}
	}
	// Check if error occurred while scanning attrcat file
	if (rc != RM_EOF)
		return rc;

	// Clean up
	if (rc = fileScan.CloseScan())
		return rc;

    return (0);
}

RC SM_Manager::CreateIndex(const char *relName,
                           const char *attrName)
{
    //cout << "CreateIndex\n"
    //     << "   relName =" << relName << "\n"
    //     << "   attrName=" << attrName << "\n";

	RC rc;

	// Check input
	if (rc = CheckName(relName))
		return rc;
	if (rc = CheckName(attrName))
		return rc;
	// End check input.

	RM_Record record;
	char* pData;

	// Check there is already an index on the attribute
	if (rc = GetAttrcatRecord(relName, attrName, record)){
		if (rc == RM_EOF)
			return SM_DNE;
		return rc;
	}
	if (rc = record.GetData(pData))
		return rc;
	Attrcat attrcat(pData);
	if (attrcat.indexNo != SM_INVALID)
		return SM_EXISTS;

	// Index number is set to offset, which is not-negative and unique per relation-attribute
	int indexNo = attrcat.offset;

	// Update attrcat
	attrcat.indexNo = indexNo;
	memcpy(pData, &attrcat, sizeof(Attrcat));
	if (rc = attrFile.UpdateRec(record))
		return rc;

	// Update relcat
	if (rc = GetRelcatRecord(relName, record)){
		if (rc == RM_EOF)
			return SM_DNE;
		return rc;
	}
	if (rc = record.GetData(pData))
		return rc;
	Relcat relcat(pData);
	relcat.indexCount += 1;
	memcpy(pData, &relcat, sizeof(Relcat));
	if (rc = relFile.UpdateRec(record))
		return rc;

	// Create index
	if (rc = ixManager->CreateIndex(relName, indexNo, attrcat.attrType, attrcat.attrLen))
		return rc;

	// Prepare relation scan / index insertion
	IX_IndexHandle indexHandle;
	RM_FileHandle fileHandle;
	RM_FileScan fileScan;
	RID rid;
	// Open index
	if (rc = ixManager->OpenIndex(relName, indexNo, indexHandle))
		return rc;
	// Open (relation if not a catalog) and scan
	if (strcmp(relName, RELCAT) == 0){
		if (rc = fileScan.OpenScan(relFile, INT, 4, 0, NO_OP, NULL))
		return rc;
	}
	else if (strcmp(relName, ATTRCAT) == 0){
		if (rc = fileScan.OpenScan(attrFile, INT, 4, 0, NO_OP, NULL))
			return rc;
	} 
	else {
		// Open relation
		if (rc = rmManager->OpenFile(relName, fileHandle))
			return rc;
		if (rc = fileScan.OpenScan(fileHandle, INT, 4, 0, NO_OP, NULL))
			return rc;
	}

	// Insert each relation tuple into index
	while ( OK_RC == (rc = fileScan.GetNextRec(record))){
		if (rc = record.GetData(pData))
			return rc;
		void* attribute = pData + attrcat.offset;
		if (rc = record.GetRid(rid))
			return rc;
		
		if (rc = indexHandle.InsertEntry(attribute, rid))
			return rc;
	}
	// Check if error occurred while scanning
	if (rc != RM_EOF)
		return rc;

	// Clean up
	if (rc = fileScan.CloseScan())
		return rc;
	// Close relation if necessary
	if (!isCatalog(relName)){
		if (rc = rmManager->CloseFile(fileHandle))
			return rc;
	}
	if (rc = ixManager->CloseIndex(indexHandle))
		return rc;

    return (0);
}

RC SM_Manager::DropIndex(const char *relName,
                         const char *attrName)
{
    //cout << "DropIndex\n"
    //     << "   relName =" << relName << "\n"
    //     << "   attrName=" << attrName << "\n";

	RC rc;

	// Check input
	if (rc = CheckName(relName))
		return rc;
	if (rc = CheckName(attrName))
		return rc;
	// End check input.

	RM_Record record;
	char* pData;

	// Check there is an index on the attribute
	if (rc = GetAttrcatRecord(relName, attrName, record)){
		if (rc == RM_EOF)
			return SM_DNE;
		return rc;
	}
	if (rc = record.GetData(pData))
		return rc;
	Attrcat attrcat(pData);
	if (attrcat.indexNo == SM_INVALID)
		return SM_DNE;

	// Get index number
	int indexNo = attrcat.indexNo;

	// Update attrcat
	attrcat.indexNo = SM_INVALID;
	memcpy(pData, &attrcat, sizeof(Attrcat));
	if (rc = attrFile.UpdateRec(record))
		return rc;

	// Update relcat
	if (rc = GetRelcatRecord(relName, record)){
		if (rc == RM_EOF)
			return SM_DNE;
		return rc;
	}
	if (rc = record.GetData(pData))
		return rc;
	Relcat relcat(pData);
	relcat.indexCount -= 1;
	memcpy(pData, &relcat, sizeof(Relcat));
	if (rc = relFile.UpdateRec(record))
		return rc;

	// Delete index
	if (rc = ixManager->DestroyIndex(relName, indexNo))
		return rc;

    return (0);
}

RC SM_Manager::Load(const char *relName,
                    const char *fileName)
{
    //cout << "Load\n"
    //     << "   relName =" << relName << "\n"
    //     << "   fileName=" << fileName << "\n";

	RM_FileHandle fileHandle;
	vector<pair<Attrcat, IX_IndexHandle> > indexes;
	vector<Attrcat> attributes;
	RC rc;

	// Check input
	if (rc = CheckName(relName))
		return rc;
	if (isCatalog(relName))
		return SM_INVALIDCATACTION;
	if (!fileName)
		return SM_NULLINPUT;
	if (strlen(fileName) == 0)
		return SM_NAMELEN;
	// End check input.

	// Open relation files
	if (rc = rmManager->OpenFile(relName, fileHandle));
		return rc;

	// Open all index files
	RM_FileScan fileScan;
	int offset = (int)offsetof(struct Attrcat, relName);
	char relation[MAXNAME + 1];
	strcpy(relation, relName);
	if (rc = fileScan.OpenScan(attrFile, STRING, MAXNAME, offset, EQ_OP, relation))
		return rc;
	RM_Record record;
	// Find each attribute
	while ( OK_RC == (rc = fileScan.GetNextRec(record))){
		// Read attribute's attrcat info
		char* pData;
		if (rc = record.GetData(pData))
			return rc;
		Attrcat attrcat(pData);

		// If index exists, add to indexes
		if (attrcat.indexNo != SM_INVALID){
			IX_IndexHandle indexHandle;
			if (rc = ixManager->OpenIndex(relName, attrcat.indexNo, indexHandle))
				return rc;
			indexes.push_back(make_pair(attrcat, indexHandle));
		}

		// Add to attributes
		attributes.push_back(attrcat);
	}
	// Check if error occurred
	if (rc != RM_EOF)
		return rc;
	// Clean up
	if (rc = fileScan.CloseScan())
		return rc;

	// Sort attributes by attrNo
	sort(attributes.begin(), attributes.end(), sortAttrcats);

	// Get tupleLen
	if (rc = GetRelcatRecord(relName, record)){
		if (rc == RM_EOF)
			return SM_DNE;
		return rc;
	}
	char* pData;
	if (rc = record.GetData(pData))
		return rc;
	Relcat relcat(pData);
	int tupleLen = relcat.tupleLen;

	// Open ASCII file
	ifstream asciiFile(fileName);
	if (!asciiFile.is_open())
		return SM_FILENOTOPEN;

	// Read tuples from ASCII file
	string line;
	while (getline(asciiFile, line)){		
		// Build pData
		char* pData = new char[relcat.tupleLen];
		int i = 0;
		stringstream ss(line);
		string token;
		while(getline(ss, token, ',')){
			char* dst = pData + attributes[i].offset;
			switch(attributes[i].attrType){
			case INT:
				int tmp = stoi(token);
				memcpy(dst, &tmp, 4);
				break;
			case FLOAT:
				float tmp = stof(token);
				memcpy(dst, &tmp, 4);
				break;
			case STRING:
				memcpy(dst, token.c_str(), attributes[i].attrLen);
				break;
			}
		}

		// Insert into relation
		RID rid;
		if (rc = fileHandle.InsertRec(pData, rid))
			return rc;
		// Insert into indexes
		for (pair<Attrcat, IX_IndexHandle> pair : indexes){
			char* attribute = pData + pair.first.offset;
			if (rc = pair.second.InsertEntry(attribute, rid))
				return rc;
		}

		// Clean up pData
		delete [] pData;
	}

	// Close ASCII file
	asciiFile.close();
	// Close relation file
	if (rc = rmManager->CloseFile(fileHandle));
		return rc;
	// Close index files
	for (pair<Attrcat, IX_IndexHandle> pair : indexes){
		if (rc = ixManager->CloseIndex(pair.second))
			return rc;
	}

    return (0);
}

RC SM_Manager::Print(const char *relName)
{
	//cout << "Print\n"
 //        << "   relName=" << relName << "\n";

	RC rc;

	// Check input
	if (rc = CheckName(relName))
		return rc;
	// End check input

	// Get attributes
	RM_Record record;
	char* pData;
	if (rc = GetRelcatRecord(relName, record)){
		if (rc == RM_EOF)
			return SM_DNE;
		return rc;
	}
	if (rc = record.GetData(pData))
		return rc;
	Relcat relcat(pData);
	Attrcat* attributes = new Attrcat[relcat.attrCount];
	if (rc = GetAttrcats(relName, attributes))
		return rc;

	// Initialize printer
	DataAttrInfo* dataAttrs = new DataAttrInfo[relcat.attrCount]; 
	for (int i = 0; i < relcat.attrCount; ++i)
		dataAttrs[i] = DataAttrInfo (attributes[i]);
	Printer printer(dataAttrs, relcat.attrCount);
	printer.PrintHeader(cout);

	// Initialize scan
	RM_FileScan fileScan;
	RM_FileHandle fileHandle;
	if (strcmp(relName, RELCAT) == 0){
		if (rc = fileScan.OpenScan(relFile, INT, 4, 0, NO_OP, NULL))
		return rc;
	}
	else if (strcmp(relName, ATTRCAT) == 0){
		if (rc = fileScan.OpenScan(attrFile, INT, 4, 0, NO_OP, NULL))
			return rc;
	} 
	else{
		if (rc = rmManager->OpenFile(relName, fileHandle))
			return rc;
		if (rc = fileScan.OpenScan(fileHandle, INT, 4, 0, NO_OP, NULL))
			return rc;
	}

	// Scan and print tuples
	while ( OK_RC == (rc = fileScan.GetNextRec(record))){
		if (rc = record.GetData(pData))
			return rc;
		printer.Print(cout, pData);
	}
	if (rc != RM_EOF)
		return rc;

	// Finish printer
	printer.PrintFooter(cout);

	// Clean up
	delete [] attributes;
	delete [] dataAttrs;
	if (rc = fileScan.CloseScan())
		return rc;
	// Close relation if necessary
	if (!isCatalog(relName)){
		if (rc = rmManager->CloseFile(fileHandle))
			return rc;
	}

    return (0);
}

RC SM_Manager::Set(const char *paramName, const char *value)
{
    //cout << "Set\n"
    //     << "   paramName=" << paramName << "\n"
    //     << "   value    =" << value << "\n";

	// TODO: nothing for now

    return (0);
}

RC SM_Manager::Help()
{
    //cout << "Help\n";
	RC rc;
	if (rc = Print(RELCAT))
		return rc;
    return (0);
}

RC SM_Manager::Help(const char *relName)
{
    //cout << "Help\n"
    //     << "   relName=" << relName << "\n";

	RC rc;

	// Check input
	if (rc = CheckName(relName))
		return rc;
	// End check input

	// Get attributes
	RM_Record record;
	char* pData;
	if (rc = GetRelcatRecord(relName, record)){
		if (rc == RM_EOF)
			return SM_DNE;
		return rc;
	}
	if (rc = record.GetData(pData))
		return rc;
	Relcat relcat(pData);
	Attrcat* attributes = new Attrcat[relcat.attrCount];
	if (rc = GetAttrcats(relName, attributes))
		return rc;

	// Make dataAttrs based on Attrcat
	const int attrCount = 6;
	DataAttrInfo* dataAttrs = new DataAttrInfo[attrCount]; 
	dataAttrs[0] = DataAttrInfo(ATTRCAT, "relName", offsetof(struct Attrcat, relName), STRING, MAXNAME+1, SM_INVALID);
	dataAttrs[1] = DataAttrInfo(ATTRCAT, "attrName", offsetof(struct Attrcat, attrName), STRING, MAXNAME+1, SM_INVALID);
	dataAttrs[2] = DataAttrInfo(ATTRCAT, "offset", offsetof(struct Attrcat, offset), INT, sizeof(int), SM_INVALID);
	dataAttrs[3] = DataAttrInfo(ATTRCAT, "attrType", offsetof(struct Attrcat, attrType), INT, sizeof(AttrType), SM_INVALID);
	dataAttrs[4] = DataAttrInfo(ATTRCAT, "attrLen", offsetof(struct Attrcat, attrLen),INT, sizeof(int), SM_INVALID);
	dataAttrs[5] = DataAttrInfo(ATTRCAT, "indexNo", offsetof(struct Attrcat, indexNo), INT, sizeof(int), SM_INVALID);

	// Initialize printer
	Printer printer(dataAttrs, attrCount);
	printer.PrintHeader(cout);

	// Print attributes
	for (int i = 0; i < relcat.attrCount; ++i){
		printer.Print(cout,(char*)(attributes+i));
	}

	// Finish printer
	printer.PrintFooter(cout);

	// Clean up
	delete [] attributes;
	delete [] dataAttrs;
    return (0);
}

// Private functions
bool SM_Manager::sortAttrcats(Attrcat i, Attrcat j){
	return i.offset < j.offset;
}
bool SM_Manager::isCatalog(const char* relName){
	return (strcmp(relName, RELCAT) == 0 || strcmp(relName, ATTRCAT) == 0);
}
RC SM_Manager::CheckName(const char* name){
	if (!name)
		return SM_NULLINPUT;
	if (strlen(name) < 1 || strlen(name) > MAXNAME)
		return SM_NAMELEN;
	if (!isalpha(*name))
		return SM_INVALIDNAME;
	return 0;
}
RC SM_Manager::GetRelcatRecord(const char* relName, RM_Record &record){
	char relation[MAXNAME + 1];
	strcpy(relation, relName);
	RM_FileScan fileScan;
	RC rc;
	int offset = (int)offsetof(struct Relcat, relName);

	// Scan for an entry for relation
	if (rc = fileScan.OpenScan(relFile, STRING, MAXNAME, offset, EQ_OP, relation))
		return rc;
	if (rc = fileScan.GetNextRec(record))
		return rc;
	if (rc = fileScan.CloseScan())
		return rc;

	return 0;
}
RC SM_Manager::GetAttrcatRecord (const char *relName, const char *attrName, RM_Record &record){
	char relation[MAXNAME + 1];
	strcpy(relation, relName);
	char attribute[MAXNAME + 1];
	strcpy(attribute, attrName);

	RM_FileScan fileScan;
	RC rc;
	char* pData;
	int offset = (int)offsetof(struct Attrcat, attrName);

	// Scan for entries for attribute
	if (rc = fileScan.OpenScan(attrFile, STRING, MAXNAME, offset, EQ_OP, attribute))
		return rc;
	while ( OK_RC == (rc = fileScan.GetNextRec(record))){
		if (rc = record.GetData(pData))
			return rc;
		Attrcat attrcat(pData);

		// If found correct attribute (relation name matches)
		if (strcmp(attrcat.relName, relation) == 0){
			// Clean up.
			if (rc = fileScan.CloseScan())
				return rc;
			return 0;
		}
	}
	return rc;
}
RC SM_Manager::GetAttrcats(const char* relName, Attrcat* attributes){
	RM_FileScan fileScan;
	RM_Record record;
	char* pData;
	RC rc;

	// Open scan
	char relation[MAXNAME + 1];
	strcpy(relation, relName);
	int offset = (int)offsetof(struct Attrcat, relName);
	if (rc = fileScan.OpenScan(attrFile, STRING, MAXNAME, offset, EQ_OP, relation))
		return rc;

	// Copy each attribute
	int i = 0;
	while ( OK_RC == (rc = fileScan.GetNextRec(record))){
		if (rc = record.GetData(pData))
			return rc;
		Attrcat attrcat(pData);
		memcpy(attributes+i, pData, attrcat.attrLen);
		i += 1;
	}
	// Check if error occurred while scanning attrcat file
	if (rc != RM_EOF)
		return rc;

	// Clean up
	if (rc = fileScan.CloseScan())
		return rc;

	// Sort
	sort(attributes, attributes + i, sortAttrcats);

	return 0;
}

// Begin Print Error code
// TODO
static char *SM_WarnMsg[] = {
  (char*)"invalid null parameter",
  (char*)"number of attributes is invalid (should be 0< <41)",
  (char*)"name length is invalid (should be 0< <5)",
  (char*)"invalid enumeration",
  (char*)"invalid name given",
  (char*)"relation or index already exists",
  (char*)"relation or index does not exist",
  (char*)"file did not successfully open",
  (char*)"invalid action upon a catalog"
};

static char *SM_ErrorMsg[] = {
	(char*)"change directory error",
	(char*)"invalid attribute length",
};

void SM_PrintError(RC rc)
{
    //cout << "SM_PrintError\n   rc=" << rc << "\n";

	// Check return code is within warn limits
	if (rc >= START_SM_WARN && rc <= SM_LASTWARN)
		cerr << "SM warning: " << SM_WarnMsg[rc - START_SM_WARN] << "\n";
	// Check return code is within error limits
	else if (-rc >= -START_SM_ERR && -rc <= -SM_LASTERROR)
		cerr << "SM error: " << SM_ErrorMsg[-rc + START_SM_ERR] << "\n";
	else if (rc == OK_RC)
		cerr << "SM_PrintError called with return code of 0\n";
	else
		cerr << "SM error: " << rc << " is out of bounds\n";
}

