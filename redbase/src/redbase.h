//
// redbase.h
//   global declarations
//
#ifndef REDBASE_H
#define REDBASE_H

// Please DO NOT include any other files in this file.

//
// Globally-useful defines
//
#define MAXNAME       24                // maximum length of a relation
                                        // or attribute name
#define MAXSTRINGLEN  255               // maximum length of a
                                        // string-type attribute
#define MAXATTRS      40                // maximum number of attributes
                                        // in a relation

#define YY_SKIP_YYWRAP 1
#define yywrap() 1
void yyerror(const char *);

//
// Return codes
//
typedef int RC;

#define OK_RC         0    // OK_RC return code is guaranteed to always be 0

#define START_PF_ERR  (-1)
#define END_PF_ERR    (-100)
#define START_RM_ERR  (-101)
#define END_RM_ERR    (-200)
#define START_IX_ERR  (-201)
#define END_IX_ERR    (-300)
#define START_SM_ERR  (-301)
#define END_SM_ERR    (-400)
#define START_QL_ERR  (-401)
#define END_QL_ERR    (-500)

#define START_PF_WARN  1
#define END_PF_WARN    100
#define START_RM_WARN  101
#define END_RM_WARN    200
#define START_IX_WARN  201
#define END_IX_WARN    300
#define START_SM_WARN  301
#define END_SM_WARN    400
#define START_QL_WARN  401
#define END_QL_WARN    500

// ALL_PAGES is defined and used by the ForcePages method defined in RM
// and PF layers
const int ALL_PAGES = -1;

//
// Attribute types
//
enum AttrType {
    INT,
    FLOAT,
    STRING
};

//
// Comparison operators
//
enum CompOp {
    NO_OP,                                      // no comparison
    EQ_OP, NE_OP, LT_OP, GT_OP, LE_OP, GE_OP    // binary atomic operators
};

//
// Pin Strategy Hint
//
enum ClientHint {
    NO_HINT                                     // default value
};

//
// TRUE, FALSE and BOOLEAN
//
#ifndef BOOLEAN
typedef char Boolean;
#endif

#ifndef FALSE
#define FALSE 0
#endif

#ifndef TRUE
#define TRUE 1
#endif

#ifndef NULL
#define NULL 0
#endif




// Global print error
void PrintError(RC rc);

// Global structures
const char* RELCAT = "relcat";
const char* ATTRCAT = "attrcat";
// Change dbcreate.cc with
struct Relcat {
	char relName[MAXNAME+1];
	int tupleLen;
	int attrCount;
	int indexCount;

	Relcat(char* pData){
		memcpy(this, pData, sizeof(Relcat));
	}

	Relcat(const char* relName, int tupleLen, int attrCount, int indexCount){
		int len = (strlen(relName) > MAXNAME) ? MAXNAME : strlen(relName);
		memcpy(this->relName, relName, len+1);
		this->tupleLen = tupleLen;
		this->attrCount = attrCount;
		this->indexCount = indexCount;
	}
};

// Change dbcreate.cc with
// Change sm_manager.cc's Help(const char *relName) with 
struct Attrcat {
	char relName[MAXNAME+1];
	char attrName[MAXNAME+1];
	int offset;
	AttrType attrType;
	int attrLen;
	int indexNo;

	Attrcat(){
		offset = -1;
		attrType = INT;
		attrLen = 0;
		indexNo = -1;
	}

	Attrcat(char* pData){
		memcpy(this, pData, sizeof(Attrcat));
	}

	Attrcat(const char* relName, char* attrName, int offset, AttrType attrType, int attrLen, int indexNo){
		int len = (strlen(relName) > MAXNAME) ? MAXNAME : strlen(relName);
		memcpy(this->relName, relName, len+1);
		len = (strlen(attrName) > MAXNAME) ? MAXNAME : strlen(attrName);
		memcpy(this->attrName, attrName, len+1);
		this->offset = offset;
		this->attrType = attrType;
		this->attrLen = attrLen;
		this->indexNo = indexNo;
	}
};

#endif
