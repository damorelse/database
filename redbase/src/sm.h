//
// sm.h
//   Data Manager Component Interface
//

#ifndef SM_H
#define SM_H

// Please do not include any other files than the ones below in this file.

#include <stdlib.h>
#include <string.h>
#include "redbase.h"  // Please don't change these lines
#include "parser.h"
#include "rm.h"
#include "ix.h"

#define SM_INVALID -1
//
// SM_Manager: provides data management
//
class SM_Manager {
    friend class QL_Manager;
	friend class Relation;
public:
    SM_Manager    (IX_Manager &ixm, RM_Manager &rmm);
    ~SM_Manager   ();                             // Destructor

    RC OpenDb     (const char *dbName);           // Open the database
    RC CloseDb    ();                             // close the database

    RC CreateTable(const char *relName,           // create relation relName
                   int        attrCount,          //   number of attributes
                   AttrInfo   *attributes);       //   attribute data
    RC CreateIndex(const char *relName,           // create an index for
                   const char *attrName);         //   relName.attrName
    RC DropTable  (const char *relName);          // destroy a relation

    RC DropIndex  (const char *relName,           // destroy index on
                   const char *attrName);         //   relName.attrName
    RC Load       (const char *relName,           // load relName from
                   const char *fileName);         //   fileName
    RC Help       ();                             // Print relations in db
    RC Help       (const char *relName);          // print schema of relName

    RC Print      (const char *relName);          // print relName contents

    RC Set        (const char *paramName,         // set parameter to
                   const char *value);            //   value

private:
	bool isCatalog(const char* relName);
	RC CheckName(const char* relName);
	RC GetRelcatRecord (const char* relName, RM_Record &record);
	RC GetAttrcatRecord (const char* relName, const char *attrName, RM_Record &record);
	RC GetAttrcats(const char* relName, Attrcat* attributes);
	
	IX_Manager* ixManager;
	RM_Manager* rmManager;
	RM_FileHandle relFile, attrFile;
};

//
// Print-error function
//
void SM_PrintError(RC rc);

#define SM_NULLINPUT			(START_SM_WARN + 0)
#define SM_ATTRNUM				(START_SM_WARN + 1)
#define SM_NAMELEN				(START_SM_WARN + 2)
#define SM_INVALIDENUM			(START_SM_WARN + 3)
#define SM_INVALIDNAME			(START_SM_WARN + 4)
#define SM_EXISTS				(START_SM_WARN + 5)
#define SM_DNE					(START_SM_WARN + 6)
#define SM_FILENOTOPEN			(START_SM_WARN + 7)
#define SM_INVALIDCATACTION		(START_SM_WARN + 8)
#define SM_LASTWARN		SM_INVALIDCATACTION

#define SM_CHDIR			 (START_SM_ERR - 0)
#define SM_INVALIDATTRLEN	 (START_SM_ERR - 1)
#define SM_INVALIDLOADFORMAT (START_SM_ERR - 2)
#define SM_LASTERROR	SM_INVALIDLOADFORMAT	

#endif
