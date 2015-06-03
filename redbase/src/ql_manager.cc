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
#include <list>
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
	if (nSelAttrs == 1 && strcmp(selAttrs[0].attrName, "*") == 0){
		nSelAttrs = 0;
		selAttrs = NULL;
	}
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
	int ridsSize = nRelations * sizeof(RID);
	vector<DataAttrInfo> dataAttrs; 
	for (int i = 0; i < qPlan.numOutAttrs; ++i){
		dataAttrs.push_back(DataAttrInfo (qPlan.outAttrs[i]));
		dataAttrs.back().offset -= ridsSize;
	}
	Printer printer(&dataAttrs[0], qPlan.numOutAttrs);
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
		printer.Print(cout, pData + ridsSize);
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
	vector<DataAttrInfo> dataAttrs; 
	for (int i = 0; i < relcat.attrCount; ++i)
		dataAttrs[i] = DataAttrInfo (attributes[i]);
	Printer printer(&dataAttrs[0], relcat.attrCount);
	printer.PrintHeader(cout);
	printer.Print(cout, pData);
	printer.PrintFooter(cout);

	//  Clean up
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
	int ridsSize = sizeof(RID);
	vector<DataAttrInfo> dataAttrs; 
	for (int i = 0; i < qPlan.numOutAttrs; ++i){
		dataAttrs.push_back(DataAttrInfo (qPlan.outAttrs[i]));
		dataAttrs.back().offset -= ridsSize;
	}
	Printer printer(&dataAttrs[0], qPlan.numOutAttrs);
	printer.PrintHeader(cout);
	// OPEN END

	// Delete tuples
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
		// Get rid
		RID rid(pData);
		// Update relation 
		if (rc = fileHandle.DeleteRec(rid)){
			smm->DropTable(qPlan.output);
			return rc;
		}
		// Update indexes
		for (int k = 0; k < indexes.size(); ++k){
			char* attribute = pData + indexes[k].first.offset;
			if (rc = indexes[k].second.DeleteEntry(attribute, rid)){
				smm->DropTable(qPlan.output);
				return rc;
			}
		}
		// Print 
		printer.Print(cout, pData + ridsSize);
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

	// CLOSE START
	// Close relation file
	if (rc = rmm->CloseFile(fileHandle)){
		smm->DropTable(qPlan.output);
		return rc;
	}
	// Close index files
	for (int i = 0; i < indexes.size(); ++i){
		if (rc = ixm->CloseIndex(indexes[i].second)){
			smm->DropTable(qPlan.output);
			return rc;
		}
	}
	// Finish Printer
	printer.PrintFooter(cout);
	// CLOSE END

	// Clean up
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
	int ridsSize = sizeof(RID);
	vector<DataAttrInfo> dataAttrs; 
	for (int i = 0; i < qPlan.numOutAttrs; ++i){
		dataAttrs.push_back(DataAttrInfo (qPlan.outAttrs[i]));
		dataAttrs.back().offset -= ridsSize;
	}
	Printer printer(&dataAttrs[0], qPlan.numOutAttrs);
	printer.PrintHeader(cout);
	// OPEN END

	// Update tuples
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
	while (OK_RC == (rc = tmpFileScan.GetNextRec(record))){
		char* pData;
		if (rc = record.GetData(pData)){
			smm->DropTable(qPlan.output);
			return rc;
		}
		// Get rid
		RID rid(pData);
		char* attribute = pData + leftAttrcat.offset;

		// If update attribute has an index, delete old entry
		if (leftAttrcat.indexNo != SM_INVALID){
			if (rc = indexHandle.DeleteEntry(attribute, rid)){
				smm->DropTable(qPlan.output);
				return rc;
			}
		}

		// Update record
		if (bIsValue)
			memcpy(attribute, rhsValue.data, leftAttrcat.attrLen);
		else {
			char* rightAttribute = pData + ridsSize + rightAttrcat.offset;
			memcpy(attribute, rightAttribute, leftAttrcat.attrLen);
		}
		// If update attribute has an index, insert new entry
		if (leftAttrcat.indexNo != SM_INVALID){
			if (rc = indexHandle.InsertEntry(attribute, rid)){
				smm->DropTable(qPlan.output);
				return rc;
			}
		}
		// Update relation 
		if (rc = fileHandle.UpdateRec(record)){
			smm->DropTable(qPlan.output);
			return rc;
		}

		// Print 
		printer.Print(cout, pData + ridsSize);
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

	// CLOSE START
	// Close relation file
	if (rc = rmm->CloseFile(fileHandle)){
		smm->DropTable(qPlan.output);
		return rc;
	}
	// Close index file (if necessary)
	if (leftAttrcat.indexNo != SM_INVALID){
		if (rc = ixm->CloseIndex(indexHandle)){
			smm->DropTable(qPlan.output);
			return rc;
		}
	}
	// Finish Printer
	printer.PrintFooter(cout);
	// CLOSE END

	// Clean up
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

list<set<char*>>::iterator GetJoinSet(list<set<char*>>joinGroups, char* relName){
	for (list<set<char*>>::iterator  it = joinGroups.begin(); it!=joinGroups.end(); ++it)
		if (it->find(relName) != it->end())
			return it;
	return joinGroups.end();
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
	set<string> myRels(relations, relations+nRelations);
	if (myRels.size() != nRelations)
		return QL_MULTIREL;

	// Check attributes valid (and make copies)
	bool calcProj = true;
	vector<RelAttr> mySelAttrs(nSelAttrs);
	vector<Condition> myConds(nConditions);
	vector<Condition> myAttrConds;
	vector<Condition> myValConds;
	if (nSelAttrs == 0)
		calcProj = false;
	else {
		for (int i = 0; i < nSelAttrs; ++i){
			mySelAttrs[i] = selAttrs[i];
			if (rc = CheckAttribute(mySelAttrs[i], relations, nRelations))
				return rc;
		}

		for (int i = 0; i < nConditions; ++i){
			if(conditions[i].bRhsIsAttr){
				myAttrConds.push_back(conditions[i]);
				rc = CheckCondition(myAttrConds.back(), relations, nRelations);
				myConds.push_back(myAttrConds.back());
			}
			else {
				myValConds.push_back(conditions[i]);
				rc = CheckCondition(myValConds.back(), relations, nRelations);
				myConds.push_back(myAttrConds.back());
			}
			if (rc)
				return rc;
		}
	}
	// End check input

	// Make projection map
	map<RelAttr, int> projMap;
	for (int i = 0; i < mySelAttrs.size(); ++i){
		projMap[mySelAttrs[i]] += 1;
	}
	for (int i = 0; i < myAttrConds.size(); ++i){
		projMap[RelAttr(myAttrConds[i].lhsAttr.relName, myAttrConds[i].lhsAttr.attrName)] += 1;
		projMap[RelAttr(myAttrConds[i].rhsAttr.relName, myAttrConds[i].rhsAttr.attrName)] += 1;
	}
	for (int i = 0; i < myValConds.size(); ++i)
		projMap[RelAttr(myValConds[i].lhsAttr.relName, myValConds[i].lhsAttr.attrName)] += 1;
	vector<pair<RelAttr, int> > projVector;
    copy(projMap.begin(), projMap.end(), back_inserter(projVector));

	// Make join lists
	map<char*, set<char*>> joinLists;
	for (int i = 0; i < myAttrConds.size(); ++i){
		if (strcmp(myAttrConds[i].lhsAttr.relName, myAttrConds[i].rhsAttr.relName) != 0){
			joinLists[myAttrConds[i].lhsAttr.relName].insert(myAttrConds[i].rhsAttr.relName);
			joinLists[myAttrConds[i].rhsAttr.relName].insert(myAttrConds[i].lhsAttr.relName);
		}
	}

	// Make join groups
	vector<set<char*>> joinGroups;
	set<char*> processed;
	while (processed.size() < nRelations){
		// Initialize, find first member of new group
		int i = 0;
		for (; i < nRelations; ++i)
			if (processed.find((char*)relations[i]) == processed.end())
				break;

		// Find rest of members
		queue<char*> toProcess;
		toProcess.push((char*)relations[i]);
		set<char*> currProcessed;
		while(!toProcess.empty()){
			currProcessed.insert(toProcess.front());
			if (joinLists.find(toProcess.front()) != joinLists.end()){
				for (set<char*>::iterator it = joinLists[toProcess.front()].begin(); 
					 it != joinLists[toProcess.front()].end(); ++it){
					if (currProcessed.find(*it) == currProcessed.end())
						toProcess.push(*it);
				}
			}
			toProcess.pop();
		}

		// Update join groups
		joinGroups.push_back(currProcessed);
		processed.insert(currProcessed.begin(), currProcessed.end());
	}

	// Create relation/selection nodes
	map<char*, Node> relSelNodes;
	for(int i = 0; i < nRelations; ++i){
		Node rel = Relation (smm, (char*)relations[i], calcProj, projVector.size(), &projVector[0]);
		if (rel.rc)
			return rel.rc;

		Node sel = Selection (smm, rmm, ixm, rel, nConditions, &myConds[0], calcProj, projVector.size(), &projVector[0]);
		if (sel.rc)
			relSelNodes[(char*)relations[i]] = rel;
		else 
			relSelNodes[(char*)relations[i]] = sel;
	}

	// SELECTION condition ordering: if indexed, by selectivity + # IOs
	for (map<char*, Node>::iterator it=relSelNodes.begin(); it != relSelNodes.end(); ++it){
		// TODO: order it->second.conditions;
	}
	for (int i = 0; i < joinGroups.size; ++i){
		// Join ordering (byte size + access costs) + Condition ordering (both or one indexed-selectivity)
		// TODO
	}
	// Cross ordering (byte size)
	// TODO
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
		 if (nextNodes.front()->otherChild){
			 count += 1;
			 nextNodes.push(nextNodes.front()->otherChild);
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
	RecursivePrint(qPlan, 0);
}
// Go down tree, print each branch one at a time, right-most branch first
void QL_Manager::RecursivePrint(Node node, int indent){
	// Print projection (if it exists)
	if (node.project){
		for (int i = 0; i < indent; ++i)
			cout << "|   ";
		cout << "Project (";
		for (int i = 0; i < node.numOutAttrs; ++i){
			cout << node.outAttrs[i].relName << "." << node.outAttrs[i].attrName;
			if (i + 1 < node.numOutAttrs)
				cout << ", ";
		}
		cout << ")" << endl;

		for (int i = 0; i < indent + 1; ++i)
			cout << "|   ";
		cout << endl;
	}

	// Print node
	for (int i = 0; i < indent; ++i)
		cout << "|   ";
	node.printType();
	cout << endl;

	for (int i = 0; i < indent; ++i)
		cout << "|   ";
	if (!node.child){
		cout << endl;
		return;
	}
	cout << "|";
	if (node.otherChild){
		cout << "___" << endl;

		for (int i = 0; i < indent+2; ++i)
			cout << "|   ";
	}
	cout << endl;

	if (node.otherChild)
		RecursivePrint(*node.otherChild, indent + 1);

	RecursivePrint(*node.child, indent);
}