//
// dbcreate.cc
//
// Author: Jason McHugh (mchughj@cs.stanford.edu)
//
// This shell is provided for the student.

#include <iostream>
#include <cstdio>
#include <cstring>
#include <unistd.h>
#include <vector>
#include <cstddef>
#include "rm.h"
#include "sm.h"
#include "redbase.h"

using namespace std;

//
// main
//
int main(int argc, char *argv[])
{
    char *dbname;
    char command[255] = "mkdir ";
    RC rc;

    // Look for 2 arguments. The first is always the name of the program
    // that was executed, and the second should be the name of the
    // database.
    if (argc != 2) {
        cerr << "Usage: " << argv[0] << " dbname \n";
        exit(1);
    }

    // The database name is the second argument
    dbname = argv[1];

    // Create a subdirectory for the database
    if (system (strcat(command,dbname)) != 0) {
        cerr << argv[0] << " cannot create directory: " << dbname << "\n";
        exit(1);
    }

	// Change current working directory to database's subdirectory
    if (chdir(dbname) < 0) {
        cerr << argv[0] << " chdir error to " << dbname << "\n";
        exit(1);
    }

	// initialize RedBase components
	PF_Manager pfm;
    RM_Manager rmm(pfm);

	// Create the system catalogs...
	// relcat
	if (rc = rmm.CreateFile(RELCAT, sizeof(Relcat))){
		PrintError(rc);
		return rc;
	}

	// attrcat
	if (rc = rmm.CreateFile(ATTRCAT, sizeof(Attrcat))){
		PrintError(rc);
		return rc;
	}

	// Update relcat catalog
	RID rid;
	RM_FileHandle relFile;
	if (rc = rmm.OpenFile(RELCAT, relFile))
		return rc;
	// relcat
	Relcat relRelcat(RELCAT, sizeof(Relcat), 4, 0);
	if (rc = relFile.InsertRec((char*)&relRelcat, rid))
		return rc;
	// attrcat
	Relcat attrRelcat(ATTRCAT, sizeof(Attrcat), 6, 0);
	if (rc = relFile.InsertRec((char*)&attrRelcat, rid))
		return rc;
	if (rc = rmm.CloseFile(relFile))
		return rc;

	// Update attrcat catalog
	RM_FileHandle attrFile;
	if (rc = rmm.OpenFile(ATTRCAT, attrFile))
		return rc;
	vector<Attrcat> attributes;
	// Make all the Attrcats
	// relcat
	attributes.push_back(Attrcat(RELCAT, "relName", offsetof(struct Relcat, relName), STRING, MAXNAME+1, SM_INVALID));
	attributes.push_back(Attrcat(RELCAT, "tupleLen", offsetof(struct Relcat, tupleLen), INT, sizeof(int), SM_INVALID));
	attributes.push_back(Attrcat(RELCAT, "attrCount", offsetof(struct Relcat, attrCount), INT, sizeof(int), SM_INVALID));
	attributes.push_back(Attrcat(RELCAT, "indexCount", offsetof(struct Relcat, indexCount), INT, sizeof(int), SM_INVALID));
	// attrcat
	attributes.push_back(Attrcat(ATTRCAT, "relName", offsetof(struct Attrcat, relName), STRING, MAXNAME+1, SM_INVALID));
	attributes.push_back(Attrcat(ATTRCAT, "attrName", offsetof(struct Attrcat, attrName), STRING, MAXNAME+1, SM_INVALID));
	attributes.push_back(Attrcat(ATTRCAT, "offset", offsetof(struct Attrcat, offset), INT, sizeof(int), SM_INVALID));
	attributes.push_back(Attrcat(ATTRCAT, "attrType", offsetof(struct Attrcat, attrType), INT, sizeof(AttrType), SM_INVALID));
	attributes.push_back(Attrcat(ATTRCAT, "attrLen", offsetof(struct Attrcat, attrLen), INT, sizeof(int), SM_INVALID));
	attributes.push_back(Attrcat(ATTRCAT, "indexNo", offsetof(struct Attrcat, indexNo), INT, sizeof(int), SM_INVALID));
	// Insert all the Attrcats into attrcat catalog
	for (int i = 0; i < attributes.size(); ++i){
		Attrcat attrcat = attributes.at(i);
		if (rc = attrFile.InsertRec((char*)&attrcat, rid))
			return rc;
	}
	if (rc = rmm.CloseFile(attrFile))
		return rc;

	return(0);
}
