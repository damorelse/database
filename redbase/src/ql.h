//
// ql.h
//   Query Language Component Interface
//

// This file only gives the stub for the QL component

#ifndef QL_H
#define QL_H

#include <stdlib.h>
#include <string.h>
#include "redbase.h"
#include "parser.h"
#include "rm.h"
#include "ix.h"
#include "sm.h"

#define EXT false
#define QL_FILE 0
#define QL_INDEX 1
#define QL_INDEXES 2


struct RelAttrCount {
   RelAttr first;
   int second;

   RelAttrCount();
	RelAttrCount( const RelAttr& relAttr, const int& count);
	bool operator==(const RelAttrCount &other) const;
};

class Node {
public:
	Node();
	virtual ~Node();
	Node& operator=(const Node& other);
	virtual RC execute();
	void printType();
	Attrcat getAttrcat(const char *relName, char* attrName);
	CompOp FlipOp(CompOp op);

	// Returns early in join/select if no conditions apply
	int numConditions;
	Condition *conditions;

	SM_Manager *smm;
	RM_Manager *rmm;
	IX_Manager *ixm;
	char type[MAXNAME+1];
	Node *child;
	Node *otherChild;
	Node *parent; // set by parent node
	char output[MAXNAME+1]; // set during execution
	
	int numRelations;
	char *relations;
	int numRids;
	Attrcat *rids;

	int numOutAttrs;
	Attrcat *outAttrs;
	int numCountPairs;
	RelAttrCount *pCounts;
	bool project;

	RC rc; // set optionally
	int execution; //set during query plan building
	int cost;
	int numTuples;
	int tupleSize;

protected:
	// Constructor
	void SetRelations();
	void SetRids();
	void SetOutAttrs();
	void Project(bool calcProj, int numTotalPairs, RelAttrCount *pTotals);
	// Execution
	RC CreateTmpOutput();
	RC DeleteTmpInput();
};

class QueryTree {
public:
	QueryTree();
	~QueryTree();
	QueryTree& operator=(Node& node);

	Node* root;
private:
	void RecursiveDelete(Node* node);
	Node* RecursiveClone(Node* node);
};

// Child must be join or relation
class Selection : public Node {
public:
	Selection(SM_Manager *smm, RM_Manager *rmm, IX_Manager *ixm, Node &left, int numConds, Condition *conds, bool calcProj, int numTotalPairs, RelAttrCount *pTotals);
	~Selection();
	RC execute();
}; 
// Children must be join, selection, or relation
class Join : public Node {
public:
	Join(SM_Manager *smm, RM_Manager *rmm, IX_Manager *ixm, Node &left, Node &right, int numConds, Condition *conds, bool calcProj, int numTotalPairs, RelAttrCount *pTotals);
	~Join();
	RC execute();
}; 
class Cross : public Node {
public:
	Cross(SM_Manager *smm, RM_Manager *rmm, IX_Manager *ixm, Node &left, Node &right, bool calcProj, int numTotalPairs, RelAttrCount *pTotals);
	~Cross();
	RC execute();
};
// No children
class Relation : public Node {
public:
	Relation(SM_Manager *smm, const char *relName, bool calcProj, int numTotalPairs, RelAttrCount *pTotals);
	~Relation();
};

//
// QL_Manager: query language (DML)
//
class QL_Manager {
public:
    QL_Manager (SM_Manager &smm, IX_Manager &ixm, RM_Manager &rmm);
    ~QL_Manager();                       // Destructor

    RC Select  (int nSelAttrs,           // # attrs in select clause
        const RelAttr selAttrs[],        // attrs in select clause
        int   nRelations,                // # relations in from clause
        const char * const relations[],  // relations in from clause
        int   nConditions,               // # conditions in where clause
        const Condition conditions[]);   // conditions in where clause

    RC Insert  (const char *relName,     // relation to insert into
        int   nValues,                   // # values
        const Value values[]);           // values to insert

    RC Delete  (const char *relName,     // relation to delete from
        int   nConditions,               // # conditions in where clause
        const Condition conditions[]);   // conditions in where clause

    RC Update  (const char *relName,     // relation to update
        const RelAttr &updAttr,          // attribute to update
        const int bIsValue,              // 1 if RHS is a value, 0 if attribute
        const RelAttr &rhsRelAttr,       // attr on RHS to set LHS equal to
        const Value &rhsValue,           // or value to set attr equal to
        int   nConditions,               // # conditions in where clause
        const Condition conditions[]);   // conditions in where clause

private:
	SM_Manager *smm;
	IX_Manager *ixm;
	RM_Manager *rmm;

	// Check input
	RC CheckRelation(const char * relName);
	RC CheckAttribute(RelAttr &attribute, const char * const relations[], int nRelations);
	RC CheckCondition(Condition &condition, const char * const relations[], int nRelations);

	// Create select query plan
	RC MakeSelectQueryPlan(int nSelAttrs, const RelAttr selAttrs[],
                       int nRelations, const char * const relations[],
                       int nConditions, const Condition conditions[],
					   QueryTree &qPlan);
	int SelectCost(Node &left);
	int JoinCost(Node &left, Node &right);
	int CrossCost(Node &left, Node &right);
	void SetParents(Node &node);

	RC GetResults(QueryTree &qPlan);
	void PrintQueryPlan(QueryTree &qPlan);
	void RecursivePrint(QueryTree &qPlan, int indent);
};

void QL_PrintError(RC rc);

#define QL_RELATIONDNE          (START_QL_WARN + 0) 
#define QL_INVALIDNUM			(START_QL_WARN + 1) 
#define QL_MULTIREL				(START_QL_WARN + 2)
#define QL_RELNOTINCLAUSE		(START_QL_WARN + 3)
#define QL_ATTRDNE			    (START_QL_WARN + 4)
#define QL_ATTRAMBIG			(START_QL_WARN + 5)
#define QL_TYPEINCOM			(START_QL_WARN + 6)
#define QL_INVALIDTUPLE			(START_QL_WARN + 7)
#define QL_INVALIDCATACTION		(START_QL_WARN + 8)
#define QL_RELNODE				(START_QL_WARN + 9)
#define QL_SELNODE				(START_QL_WARN + 10)
#define QL_JOINNODE				(START_QL_WARN + 11)
#define QL_LASTWARN		QL_JOINNODE

#define QL_FILEERROR           (START_QL_ERR - 0)
#define QL_LASTERROR	QL_FILEERROR

#endif
