#include <cstdio>
#include <iostream>
#include <sys/times.h>
#include <sys/types.h>
#include <cassert>
#include <unistd.h>
#include <sstream>
#include <set>
#include <vector>
#include <string>
#include <fstream>
#include <map>
#include <queue>
#include "redbase.h"
#include "ql.h"
#include "sm.h"
#include "ix.h"
#include "rm.h"
#include "printer.h"
using namespace std;

//
// QL_Manager::QL_Manager(SM_Manager &smm, IX_Manager &ixm, RM_Manager &rmm)
//
// Constructor for the QL Manager
//
QL_Manager::QL_Manager(SM_Manager &smm, IX_Manager &ixm, RM_Manager &rmm)
{
    // Can't stand unused variable warnings!
    assert (&smm && &ixm && &rmm);

	this->smm = &smm;
	this->ixm = &ixm;
	this->rmm = &rmm;
}

//
// QL_Manager::~QL_Manager()
//
// Destructor for the QL Manager
//
QL_Manager::~QL_Manager()
{
	smm = NULL;
	ixm = NULL;
	rmm = NULL;
}


//
// Handle the select clause
//
RC QL_Manager::Select(int nSelAttrs, const RelAttr selAttrs[],
                      int nRelations, const char * const relations[],
                      int nConditions, const Condition conditions[])
{
	RC rc;

	// Check input
	// Check select attributes/relation numbers
	if (nSelAttrs < 1 || nRelations < 1)
		return QL_INVALIDNUM;
	// End check input

	Node qPlan;
	if (rc = MakeSelectQueryPlan(nSelAttrs, selAttrs, nRelations, relations, nConditions, conditions, qPlan))
		return rc;
	if (rc = GetResults(qPlan)){
		smm->DropTable(qPlan.output);
		return rc;
	}
	if (bQueryPlans){
		PrintQueryPlan(qPlan);
	}

	// Start Printer
	DataAttrInfo* dataAttrs = new DataAttrInfo[nSelAttrs]; 
	for (int i = 0; i < nSelAttrs; ++i)
		dataAttrs[i] = DataAttrInfo (qPlan.outAttrs[i]);
	Printer printer(dataAttrs, nSelAttrs);
	printer.PrintHeader(cout);

	// Print
	RM_FileHandle tmpFileHandle;
	RM_FileScan tmpFileScan;
	if (rc = rmm->OpenFile(qPlan.output, tmpFileHandle)){
		smm->DropTable(qPlan.output);
		return rc;
	}
	if (rc = tmpFileScan.OpenScan(tmpFileHandle, INT, 4, 0, NO_OP, NULL)){
		smm->DropTable(qPlan.output);
		return rc;
	}
	RM_Record record;
	while (OK_RC == (rc = tmpFileScan.GetNextRec(record))){
		char* pData;
		if (rc = record.GetData(pData)){
			smm->DropTable(qPlan.output);
			return rc;
		}
		// Print 
		printer.Print(cout, pData + nRelations*sizeof(RID));
	}
	if (rc != RM_EOF){
		smm->DropTable(qPlan.output);
		return rc;
	}
	if (rc = tmpFileScan.CloseScan()){
		smm->DropTable(qPlan.output);
		return rc;
	}
	if (rc = rmm->CloseFile(tmpFileHandle)){
		smm->DropTable(qPlan.output);
		return rc;
	}

	// Finish Printer
	printer.PrintFooter(cout);


	// Clean up
	if (rc = smm->DropTable(qPlan.output))
		return rc;

	return 0;
}

//
// Insert the values into relName
//
RC QL_Manager::Insert(const char *relName,
                      int nValues, const Value values[])
{
	RC rc;

    // Check input
	// Check relation in db
	if (smm->isCatalog(relName))
		return QL_INVALIDCATACTION;
	if (rc = CheckRelation(relName))
		return rc;

	// Check number of values matches relation attribute count
	RM_Record record;
	if (rc = smm->GetRelcatRecord(relName, record)){
		if (rc == RM_EOF)
			return QL_RELATIONDNE;
		return rc;
	}
	char* pData;
	if (rc = record.GetData(pData))
		return rc;
	Relcat relcat(pData);
	if (nValues != relcat.attrCount)
		return QL_INVALIDTUPLE;

	// Check values' types matches relation attribute order
	Attrcat* attributes = new Attrcat[nValues];
	if (rc = smm->GetAttrcats(relName, attributes)){
		delete [] attributes;
		return rc;
	}
	for (int i = 0; i < nValues; ++i){
		if (values[i].type != attributes[i].attrType){
			delete [] attributes;
			return QL_INVALIDTUPLE;
		}
	}
	// End check input
	
	// Create temp file
	char* fileName = tmpnam(NULL);
	ofstream file(fileName);
	if (!file.is_open()){
		delete [] attributes;
		remove(fileName);
		return QL_FILEERROR;
	}
	// Write tuple to temp file (and construct pData to print to screen)
	char* pData = new char[relcat.tupleLen];
	for (int i = 0; i < nValues; ++i){
		char* attribute = pData + attributes[i].offset;
		memcpy(attribute, values[i].data, attributes[i].attrLen);

		if (i > 0)
			file << ',';
		switch(values[i].type){
		case INT:
			{
				int tmp;
				memcpy(&tmp, values[i].data, 4);
				file << tmp;
				if (file.fail()){
					delete [] pData;
					delete [] attributes;
					remove(fileName);
					return QL_FILEERROR;
				}
				break;
			}
		case FLOAT:
			{
				float tmp;
				memcpy(&tmp, values[i].data, 4);
				file << tmp;
				if (file.fail()){
					delete [] pData;
					delete [] attributes;
					remove(fileName);
					return QL_FILEERROR;
				}
				break;
			}
		case STRING:
			{
				file << (string)(char*)values[i].data;
				if (file.fail()){
					delete [] pData;
					delete [] attributes;
					remove(fileName);
					return QL_FILEERROR;
				}
				break;
			}
		}
	}
	file.close();

	// Load file tuples
	if (rc = smm->Load(relName, fileName)){
		delete [] pData;
		delete [] attributes;
		remove(fileName);
		return rc;
	}
	// Destroy temp file
	if (remove(fileName)){
		delete [] pData;
		delete [] attributes;
		return QL_FILEERROR;
	}

	// Print result
	DataAttrInfo* dataAttrs = new DataAttrInfo[relcat.attrCount]; 
	for (int i = 0; i < relcat.attrCount; ++i)
		dataAttrs[i] = DataAttrInfo (attributes[i]);
	Printer printer(dataAttrs, relcat.attrCount);
	printer.PrintHeader(cout);
	printer.Print(cout, pData);
	printer.PrintFooter(cout);

	//  Clean up
	delete [] dataAttrs;
	delete [] pData;
	delete [] attributes;

    return 0;
}

//
// Delete from the relName all tuples that satisfy conditions
//
RC QL_Manager::Delete(const char *relName,
                      int nConditions, const Condition conditions[])
{
    RC rc;
	const char * const relations[1] = {relName};

	// Check input
	// Check relation is not catalog
	if (smm->isCatalog(relName))
		return QL_INVALIDCATACTION;
	// End check input

	Node qPlan;
	if (rc = MakeSelectQueryPlan(0, NULL, 1, relations, nConditions, conditions, qPlan))
		return rc;
	if (rc = GetResults(qPlan)){
		smm->DropTable(qPlan.output);
		return rc;
	}
	if (bQueryPlans){
		PrintQueryPlan(qPlan);
	}

	// OPEN START
	// Open relation file
	RM_FileHandle fileHandle;
	if (rc = rmm->OpenFile(relName, fileHandle)){
		smm->DropTable(qPlan.output);
		return rc;
	}
	// Open index files
	vector<pair<Attrcat, IX_IndexHandle> > indexes;
	for (int i = 0; i < qPlan.numOutAttrs; ++i){
		// If index exists, add to indexes
		if (qPlan.outAttrs[i].indexNo != SM_INVALID){
			IX_IndexHandle indexHandle;
			if (rc = ixm->OpenIndex(relName, qPlan.outAttrs[i].indexNo, indexHandle)){
				smm->DropTable(qPlan.output);
				return rc;
			}
			indexes.push_back(make_pair(qPlan.outAttrs[i], indexHandle));
		}
	}
	// Start Printer
	DataAttrInfo* dataAttrs = new DataAttrInfo[qPlan.numOutAttrs]; 
	for (int i = 0; i < qPlan.numOutAttrs; ++i)
		dataAttrs[i] = DataAttrInfo (qPlan.outAttrs[i]);
	Printer printer(dataAttrs, qPlan.numOutAttrs);
	printer.PrintHeader(cout);
	// OPEN END

	// Delete tuples
	RM_FileHandle tmpFileHandle;
	RM_FileScan tmpFileScan;
	if (rc = rmm->OpenFile(qPlan.output, tmpFileHandle)){
		delete [] dataAttrs;
		smm->DropTable(qPlan.output);
		return rc;
	}
	if (rc = tmpFileScan.OpenScan(tmpFileHandle, INT, 4, 0, NO_OP, NULL)){
		delete [] dataAttrs;
		smm->DropTable(qPlan.output);
		return rc;
	}
	RM_Record record;
	while (OK_RC == (rc = tmpFileScan.GetNextRec(record))){
		char* pData;
		if (rc = record.GetData(pData)){
			delete [] dataAttrs;
			smm->DropTable(qPlan.output);
			return rc;
		}
		// Get rid
		RID rid(pData);
		// Update relation 
		if (rc = fileHandle.DeleteRec(rid)){
			delete [] dataAttrs;
			smm->DropTable(qPlan.output);
			return rc;
		}
		// Update indexes
		for (int k = 0; k < indexes.size(); ++k){
			char* attribute = pData + sizeof(RID) + indexes[k].first.offset;
			if (rc = indexes[k].second.DeleteEntry(attribute, rid)){
				delete [] dataAttrs;
				smm->DropTable(qPlan.output);
				return rc;
			}
		}
		// Print 
		printer.Print(cout, pData + sizeof(RID));
	}
	if (rc != RM_EOF){
		delete [] dataAttrs;
		smm->DropTable(qPlan.output);
		return rc;
	}
	if (rc = tmpFileScan.CloseScan()){
		delete [] dataAttrs;
		smm->DropTable(qPlan.output);
		return rc;
	}
	if (rc = rmm->CloseFile(tmpFileHandle)){
		delete [] dataAttrs;
		smm->DropTable(qPlan.output);
		return rc;
	}

	// CLOSE START
	// Close relation file
	if (rc = rmm->CloseFile(fileHandle)){
		delete [] dataAttrs;
		smm->DropTable(qPlan.output);
		return rc;
	}
	// Close index files
	for (int i = 0; i < indexes.size(); ++i){
		if (rc = ixm->CloseIndex(indexes[i].second)){
			delete [] dataAttrs;
			smm->DropTable(qPlan.output);
			return rc;
		}
	}
	// Finish Printer
	printer.PrintFooter(cout);
	// CLOSE END

	// Clean up
	delete [] dataAttrs;
	if (rc = smm->DropTable(qPlan.output))
		return rc;

    return 0;
}


//
// Update from the relName all tuples that satisfy conditions
//
RC QL_Manager::Update(const char *relName,
                      const RelAttr &updAttr,
                      const int bIsValue,
                      const RelAttr &rhsRelAttr,
                      const Value &rhsValue,
                      int nConditions, const Condition conditions[])  
{
	RC rc;
	RM_Record record;
	char* pData;

	// Check input
	// Check relation is not catalog
	if (smm->isCatalog(relName))
		return QL_INVALIDCATACTION;

	// Check update attributes valid
	const char * const relations[1] = {relName};
	Condition misleading(updAttr, EQ_OP, !bIsValue, rhsRelAttr, rhsValue);
	if (rc = CheckCondition(misleading, relations, 1))
		return rc;
	// End check input

	Node qPlan;
	if (rc = MakeSelectQueryPlan(0, NULL, 1, relations, nConditions, conditions, qPlan))
		return rc;
	if (rc = GetResults(qPlan)){
		smm->DropTable(qPlan.output);
		return rc;
	}
	if (bQueryPlans){
		PrintQueryPlan(qPlan);
	}

	// OPEN START
	// Open relation file
	RM_FileHandle fileHandle;
	if (rc = rmm->OpenFile(relName, fileHandle)){
		smm->DropTable(qPlan.output);
		return rc;
	}
	// Get left attrcat
	Attrcat leftAttrcat;
	leftAttrcat = qPlan.getAttrcat(relName, updAttr.attrName);
	// Get index handle (if necessary)
	IX_IndexHandle indexHandle;
	if (leftAttrcat.indexNo != SM_INVALID){
		if (rc = ixm->OpenIndex(relName, leftAttrcat.indexNo, indexHandle)){
			smm->DropTable(qPlan.output);
			return rc;
		}
	}
	// Get right attrcat (if necessary)
	Attrcat rightAttrcat;
	if (!bIsValue)
		rightAttrcat = qPlan.getAttrcat(relName, rhsRelAttr.attrName);
	
	// Start Printer
	DataAttrInfo* dataAttrs = new DataAttrInfo[qPlan.numOutAttrs]; 
	for (int i = 0; i < qPlan.numOutAttrs; ++i)
		dataAttrs[i] = DataAttrInfo (qPlan.outAttrs[i]);
	Printer printer(dataAttrs, qPlan.numOutAttrs);
	printer.PrintHeader(cout);
	// OPEN END

	// Update tuples
	RM_FileHandle tmpFileHandle;
	RM_FileScan tmpFileScan;
	if (rc = rmm->OpenFile(qPlan.output, tmpFileHandle)){
		delete [] dataAttrs;
		smm->DropTable(qPlan.output);
		return rc;
	}
	if (rc = tmpFileScan.OpenScan(tmpFileHandle, INT, 4, 0, NO_OP, NULL)){
		delete [] dataAttrs;
		smm->DropTable(qPlan.output);
		return rc;
	}
	while (OK_RC == (rc = tmpFileScan.GetNextRec(record))){
		char* pData;
		if (rc = record.GetData(pData)){
			delete [] dataAttrs;
			smm->DropTable(qPlan.output);
			return rc;
		}
		// Get rid
		RID rid(pData);
		char* attribute = pData + sizeof(RID) + leftAttrcat.offset;

		// If update attribute has an index, delete old entry
		if (leftAttrcat.indexNo != SM_INVALID){
			if (rc = indexHandle.DeleteEntry(attribute, rid)){
				delete [] dataAttrs;
				smm->DropTable(qPlan.output);
				return rc;
			}
		}

		// Update record
		if (bIsValue)
			memcpy(attribute, rhsValue.data, leftAttrcat.attrLen);
		else {
			char* rightAttribute = pData + sizeof(RID) + rightAttrcat.offset;
			memcpy(attribute, rightAttribute, leftAttrcat.attrLen);
		}
		// If update attribute has an index, insert new entry
		if (leftAttrcat.indexNo != SM_INVALID){
			if (rc = indexHandle.InsertEntry(attribute, rid)){
				delete [] dataAttrs;
				smm->DropTable(qPlan.output);
				return rc;
			}
		}
		// Update relation 
		if (rc = fileHandle.UpdateRec(record)){
			delete [] dataAttrs;
			smm->DropTable(qPlan.output);
			return rc;
		}

		// Print 
		printer.Print(cout, pData + sizeof(RID));
	}
	if (rc != RM_EOF){
		delete [] dataAttrs;
		smm->DropTable(qPlan.output);
		return rc;
	}
	if (rc = tmpFileScan.CloseScan()){
		delete [] dataAttrs;
		smm->DropTable(qPlan.output);
		return rc;
	}
	if (rc = rmm->CloseFile(tmpFileHandle)){
		delete [] dataAttrs;
		smm->DropTable(qPlan.output);
		return rc;
	}

	// CLOSE START
	// Close relation file
	if (rc = rmm->CloseFile(fileHandle)){
		delete [] dataAttrs;
		smm->DropTable(qPlan.output);
		return rc;
	}
	// Close index file (if necessary)
	if (leftAttrcat.indexNo != SM_INVALID){
		if (rc = ixm->CloseIndex(indexHandle)){
			delete [] dataAttrs;
			smm->DropTable(qPlan.output);
			return rc;
		}
	}
	// Finish Printer
	printer.PrintFooter(cout);
	// CLOSE END

	// Clean up
	delete [] dataAttrs;
	if (rc = smm->DropTable(qPlan.output))
		return rc;

    return 0;
}

// Check input
RC QL_Manager::CheckRelation(const char * relName){
	RM_Record record;
	RC rc;
	
	if (rc = smm->GetRelcatRecord(relName, record)){
		if (rc == RM_EOF)
			return QL_RELATIONDNE;
		return rc;
	}
	return 0;
}
// Note: fills in attribute.relName if previously NULL
RC QL_Manager::CheckAttribute(RelAttr &attribute, const char * const relations[], int nRelations){
	
	RM_Record record;
	RC rc;

	if (attribute.relName){
		// Check relation in from clause
		set<string> relSet(relations, relations+nRelations);
		if (relSet.find(attribute.relName) == relSet.end())
			return QL_RELNOTINCLAUSE;

		// Check relation-attribute exists
		if( rc = smm->GetAttrcatRecord(attribute.relName, attribute.attrName, record)){
			if (rc == RM_EOF)
				return QL_ATTRDNE;
			return rc;
		}
	}
	else {
		// Check exactly one relation in from clause has attribute
		int count = 0;
		int relIndex = 0;
		for (int i = 0; i < nRelations && count < 2; ++i){
			rc = smm->GetAttrcatRecord(relations[i], attribute.attrName, record);
			if (rc == 0){
				relIndex = i;
				count += 1;
			}
			else if (rc != RM_EOF)
				return rc;
		}
		if (count == 0)
			return QL_ATTRDNE;
		if (count > 1)
			return QL_ATTRAMBIG;

		attribute.relName = new char[MAXNAME + 1];
		memset(attribute.relName, '\0', MAXNAME + 1);
		strcpy(attribute.relName, relations[relIndex]);
	}
	return 0;
}
RC QL_Manager::CheckCondition(Condition &condition, const char * const relations[], int nRelations){
	RC rc;

	// Check left hand attribute
	if (rc = CheckAttribute(condition.lhsAttr, relations, nRelations))
		return rc;

	// Check right hand attribute (if it is an attribute)
	if (condition.bRhsIsAttr){
		if (rc = CheckAttribute(condition.rhsAttr, relations, nRelations))
			return rc;
	}

	// Check type compatibility
	RM_Record record;
	char* pData;
	if (rc = smm->GetAttrcatRecord(condition.lhsAttr.relName, condition.lhsAttr.attrName, record))
		return rc;
	if (rc = record.GetData(pData))
		return rc;
	AttrType lhsType = Attrcat(pData).attrType;

	if (condition.bRhsIsAttr){
		if (rc = smm->GetAttrcatRecord(condition.rhsAttr.relName, condition.rhsAttr.attrName, record))
			return rc;
		if (rc = record.GetData(pData))
			return rc;

		AttrType rhsType = Attrcat(pData).attrType;
		if (lhsType != rhsType)
			return QL_TYPEINCOM;
	}
	else {
		AttrType rhsType = condition.rhsValue.type;
		if (lhsType != rhsType)
			return QL_TYPEINCOM;
	}

	return 0;
}


RC QL_Manager::MakeSelectQueryPlan(int nSelAttrs, const RelAttr selAttrs[],
                       int nRelations, const char * const relations[],
                       int nConditions, const Condition conditions[],
					   Node &qPlan)
{
	RC rc;
	// Check input
	// Check relations in db
	for (int i = 0; i < nRelations; ++i){
		if (rc = CheckRelation(relations[i]))
			return rc;
	}

	// Check relation uniqueness
	set<string> relSet(relations, relations+nRelations);
	if (relSet.size() != nRelations)
		return QL_MULTIREL;

	// Check attributes valid (and make copies)
	vector<RelAttr> mySelAttrs(nSelAttrs);
	for (int i = 0; i < nSelAttrs; ++i){
		mySelAttrs[i] = selAttrs[i];
		if (rc = CheckAttribute(mySelAttrs[i], relations, nRelations))
			return rc;
	}
	vector<Condition> myAttrConds;
	vector<Condition> myValConds;
	for (int i = 0; i < nConditions; ++i){
		if(conditions[i].bRhsIsAttr){
			myAttrConds.push_back(conditions[i]);
			rc = CheckCondition(myAttrConds.back(), relations, nRelations);
		}
		else {
			myValConds.push_back(conditions[i]);
			rc = CheckCondition(myValConds.back(), relations, nRelations);
		}
		if (rc)
			return rc;
	}
	// End check input

	// TODO

	// Find join groups
	// Order each join group
	// Apply conditions and projections to each join pair
		// Order conditions
	// Cross join groups
}
RC QL_Manager::GetResults(Node qPlan)
{
	 map<Node*, int> counts;
	 queue<Node*> zeroCounts;
	 
	 queue<Node*> nextNodes;
	 nextNodes.push(&qPlan);
	 while (!nextNodes.empty()){
		 int count = 0;
		 if (nextNodes.front()->child){
			 count += 1;
			 nextNodes.push(nextNodes.front()->child);
		 }
		 if (nextNodes.front()->otherchild){
			 count += 1;
			 nextNodes.push(nextNodes.front()->otherchild);
		 }

		 if (count == 0)
			 zeroCounts.push(nextNodes.front());
		 else
			 counts[nextNodes.front()] = count;

		 nextNodes.pop();
	 }

	 RC rc;
	 while (!zeroCounts.empty()){
		 if (rc = zeroCounts.front()->execute())
			 return rc;

		 if (zeroCounts.front()->parent){
			counts[zeroCounts.front()->parent] -= 1;
			if (counts[zeroCounts.front()->parent] == 0){
				zeroCounts.push(zeroCounts.front()->parent);
				counts.erase(zeroCounts.front()->parent);
			}
		 }

		 zeroCounts.pop();
	 }

	 return 0;
}
void QL_Manager::PrintQueryPlan(const Node qPlan)
{
	// TODO
	
}