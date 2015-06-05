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
#include <utility>
#include <algorithm>
#include <cerrno>
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

	QueryTree qPlan;
	if (nSelAttrs == 1 && strcmp(selAttrs[0].attrName, "*") == 0){
		nSelAttrs = 0;
		selAttrs = NULL;
	}
	if (rc = MakeSelectQueryPlan(nSelAttrs, selAttrs, nRelations, relations, nConditions, conditions, qPlan))
		return rc;
	if (rc = GetResults(*qPlan.root)){
		smm->DropTable(qPlan.root->output);
		return rc;
	}
	if (bQueryPlans){
		PrintQueryPlan(*qPlan.root);
	}

	// Start Printer
	int ridsSize = nRelations * sizeof(RID);
	vector<DataAttrInfo> dataAttrs; 
	for (int i = 0; i < qPlan.root->numOutAttrs; ++i){
		dataAttrs.push_back(DataAttrInfo (qPlan.root->outAttrs[i]));
		dataAttrs.back().offset -= ridsSize;
	}
	Printer printer(&dataAttrs[0], qPlan.root->numOutAttrs);
	printer.PrintHeader(cout);

	// Print
	RM_FileHandle tmpFileHandle;
	RM_FileScan tmpFileScan;
	if (rc = rmm->OpenFile(qPlan.root->output, tmpFileHandle)){
		smm->DropTable(qPlan.root->output);
		return rc;
	}
	if (rc = tmpFileScan.OpenScan(tmpFileHandle, INT, 4, 0, NO_OP, NULL)){
		smm->DropTable(qPlan.root->output);
		return rc;
	}
	RM_Record record;
	while (OK_RC == (rc = tmpFileScan.GetNextRec(record))){
		char* pData;
		if (rc = record.GetData(pData)){
			smm->DropTable(qPlan.root->output);
			return rc;
		}
		// Print 
		printer.Print(cout, pData + ridsSize);
	}
	if (rc != RM_EOF){
		smm->DropTable(qPlan.root->output);
		return rc;
	}
	if (rc = tmpFileScan.CloseScan()){
		smm->DropTable(qPlan.root->output);
		return rc;
	}
	if (rc = rmm->CloseFile(tmpFileHandle)){
		smm->DropTable(qPlan.root->output);
		return rc;
	}

	// Finish Printer
	printer.PrintFooter(cout);


	// Clean up
	if (rc = smm->DropTable(qPlan.root->output))
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
	pData = new char[relcat.tupleLen];
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

	QueryTree qPlan;
	if (rc = MakeSelectQueryPlan(0, NULL, 1, relations, nConditions, conditions, qPlan))
		return rc;
	if (rc = GetResults(*qPlan.root)){
		smm->DropTable(qPlan.root->output);
		return rc;
	}
	if (bQueryPlans){
		PrintQueryPlan(*qPlan.root);
	}

	// OPEN START
	// Open relation file
	RM_FileHandle fileHandle;
	if (rc = rmm->OpenFile(relName, fileHandle)){
		smm->DropTable(qPlan.root->output);
		return rc;
	}
	// Open index files
	vector<pair<Attrcat, IX_IndexHandle> > indexes;
	for (int i = 0; i < qPlan.root->numOutAttrs; ++i){
		// If index exists, add to indexes
		if (qPlan.root->outAttrs[i].indexNo != SM_INVALID){
			IX_IndexHandle indexHandle;
			if (rc = ixm->OpenIndex(relName, qPlan.root->outAttrs[i].indexNo, indexHandle)){
				smm->DropTable(qPlan.root->output);
				return rc;
			}
			indexes.push_back(pair<Attrcat, IX_IndexHandle>(qPlan.root->outAttrs[i], indexHandle));
		}
	}
	// Start Printer
	int ridsSize = sizeof(RID);
	vector<DataAttrInfo> dataAttrs; 
	for (int i = 0; i < qPlan.root->numOutAttrs; ++i){
		dataAttrs.push_back(DataAttrInfo (qPlan.root->outAttrs[i]));
		dataAttrs.back().offset -= ridsSize;
	}
	Printer printer(&dataAttrs[0], qPlan.root->numOutAttrs);
	printer.PrintHeader(cout);
	// OPEN END

	// Delete tuples
	RM_FileHandle tmpFileHandle;
	RM_FileScan tmpFileScan;
	if (rc = rmm->OpenFile(qPlan.root->output, tmpFileHandle)){
		smm->DropTable(qPlan.root->output);
		return rc;
	}
	if (rc = tmpFileScan.OpenScan(tmpFileHandle, INT, 4, 0, NO_OP, NULL)){
		smm->DropTable(qPlan.root->output);
		return rc;
	}
	RM_Record record;
	while (OK_RC == (rc = tmpFileScan.GetNextRec(record))){
		char* pData;
		if (rc = record.GetData(pData)){
			smm->DropTable(qPlan.root->output);
			return rc;
		}
		// Get rid
		RID rid(pData);
		// Update relation 
		if (rc = fileHandle.DeleteRec(rid)){
			smm->DropTable(qPlan.root->output);
			return rc;
		}
		// Update indexes
		for (int k = 0; k < indexes.size(); ++k){
			char* attribute = pData + indexes[k].first.offset;
			if (rc = indexes[k].second.DeleteEntry(attribute, rid)){
				smm->DropTable(qPlan.root->output);
				return rc;
			}
		}
		// Print 
		printer.Print(cout, pData + ridsSize);
	}
	if (rc != RM_EOF){
		smm->DropTable(qPlan.root->output);
		return rc;
	}
	if (rc = tmpFileScan.CloseScan()){
		smm->DropTable(qPlan.root->output);
		return rc;
	}
	if (rc = rmm->CloseFile(tmpFileHandle)){
		smm->DropTable(qPlan.root->output);
		return rc;
	}

	// CLOSE START
	// Close relation file
	if (rc = rmm->CloseFile(fileHandle)){
		smm->DropTable(qPlan.root->output);
		return rc;
	}
	// Close index files
	for (int i = 0; i < indexes.size(); ++i){
		if (rc = ixm->CloseIndex(indexes[i].second)){
			smm->DropTable(qPlan.root->output);
			return rc;
		}
	}
	// Finish Printer
	printer.PrintFooter(cout);
	// CLOSE END

	// Clean up
	if (rc = smm->DropTable(qPlan.root->output))
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

	QueryTree qPlan;
	if (rc = MakeSelectQueryPlan(0, NULL, 1, relations, nConditions, conditions, qPlan))
		return rc;
	if (rc = GetResults(*qPlan.root)){
		smm->DropTable(qPlan.root->output);
		return rc;
	}
	if (bQueryPlans){
		PrintQueryPlan(*qPlan.root);
	}

	// OPEN START
	// Open relation file
	RM_FileHandle fileHandle;
	if (rc = rmm->OpenFile(relName, fileHandle)){
		smm->DropTable(qPlan.root->output);
		return rc;
	}
	// Get left attrcat
	Attrcat leftAttrcat;
	leftAttrcat = qPlan.root->getAttrcat(relName, updAttr.attrName);
	// Get index handle (if necessary)
	IX_IndexHandle indexHandle;
	if (leftAttrcat.indexNo != SM_INVALID){
		if (rc = ixm->OpenIndex(relName, leftAttrcat.indexNo, indexHandle)){
			smm->DropTable(qPlan.root->output);
			return rc;
		}
	}
	// Get right attrcat (if necessary)
	Attrcat rightAttrcat;
	if (!bIsValue)
		rightAttrcat = qPlan.root->getAttrcat(relName, rhsRelAttr.attrName);
	
	// Start Printer
	int ridsSize = sizeof(RID);
	vector<DataAttrInfo> dataAttrs; 
	for (int i = 0; i < qPlan.root->numOutAttrs; ++i){
		dataAttrs.push_back(DataAttrInfo (qPlan.root->outAttrs[i]));
		dataAttrs.back().offset -= ridsSize;
	}
	Printer printer(&dataAttrs[0], qPlan.root->numOutAttrs);
	printer.PrintHeader(cout);
	// OPEN END

	// Update tuples
	RM_FileHandle tmpFileHandle;
	RM_FileScan tmpFileScan;
	if (rc = rmm->OpenFile(qPlan.root->output, tmpFileHandle)){
		smm->DropTable(qPlan.root->output);
		return rc;
	}
	if (rc = tmpFileScan.OpenScan(tmpFileHandle, INT, 4, 0, NO_OP, NULL)){
		smm->DropTable(qPlan.root->output);
		return rc;
	}
	while (OK_RC == (rc = tmpFileScan.GetNextRec(record))){
		char* pData;
		if (rc = record.GetData(pData)){
			smm->DropTable(qPlan.root->output);
			return rc;
		}
		// Get rid
		RID rid(pData);
		char* attribute = pData + leftAttrcat.offset;

		// If update attribute has an index, delete old entry
		if (leftAttrcat.indexNo != SM_INVALID){
			if (rc = indexHandle.DeleteEntry(attribute, rid)){
				smm->DropTable(qPlan.root->output);
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
				smm->DropTable(qPlan.root->output);
				return rc;
			}
		}
		// Update relation 
		if (rc = fileHandle.UpdateRec(record)){
			smm->DropTable(qPlan.root->output);
			return rc;
		}

		// Print 
		printer.Print(cout, pData + ridsSize);
	}
	if (rc != RM_EOF){
		smm->DropTable(qPlan.root->output);
		return rc;
	}
	if (rc = tmpFileScan.CloseScan()){
		smm->DropTable(qPlan.root->output);
		return rc;
	}
	if (rc = rmm->CloseFile(tmpFileHandle)){
		smm->DropTable(qPlan.root->output);
		return rc;
	}

	// CLOSE START
	// Close relation file
	if (rc = rmm->CloseFile(fileHandle)){
		smm->DropTable(qPlan.root->output);
		return rc;
	}
	// Close index file (if necessary)
	if (leftAttrcat.indexNo != SM_INVALID){
		if (rc = ixm->CloseIndex(indexHandle)){
			smm->DropTable(qPlan.root->output);
			return rc;
		}
	}
	// Finish Printer
	printer.PrintFooter(cout);
	// CLOSE END

	// Clean up
	if (rc = smm->DropTable(qPlan.root->output))
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

list<set<string> >::iterator GetJoinSet(list<set<string> >joinGroups, char* relName){
	for (list<set<string> >::iterator  it = joinGroups.begin(); it!=joinGroups.end(); ++it)
		if (it->find(relName) != it->end())
			return it;
	return joinGroups.end();
}
RC QL_Manager::MakeSelectQueryPlan(int nSelAttrs, const RelAttr selAttrs[],
                       int nRelations, const char * const relations[],
                       int nConditions, const Condition conditions[],
					   QueryTree &qPlan)
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
	vector<RelAttr> mySelAttrs;
	vector<Condition> myConds;
	vector<Condition> myAttrConds;
	vector<Condition> myValConds;
	if (nSelAttrs == 0)
		calcProj = false;
	else {
		for (int i = 0; i < nSelAttrs; ++i){
			mySelAttrs.push_back(selAttrs[i]);
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
	vector<RelAttrCount> projVector;
	for(map<RelAttr, int>::iterator it = projMap.begin(); it != projMap.end(); ++it)
		projVector.push_back(RelAttrCount(it->first, it->second));

	// Make join lists
	map<string, set<string> > joinLists;
	for (int i = 0; i < myAttrConds.size(); ++i){
		if (strcmp(myAttrConds[i].lhsAttr.relName, myAttrConds[i].rhsAttr.relName) != 0){
			joinLists[myAttrConds[i].lhsAttr.relName].insert(myAttrConds[i].rhsAttr.relName);
			joinLists[myAttrConds[i].rhsAttr.relName].insert(myAttrConds[i].lhsAttr.relName);
		}
	}

	// Make relation groups
	vector<set<string> > relGroups;
	set<string> processed;
	while (processed.size() < nRelations){
		// Initialize, find first member of new group
		int i = 0;
		for (; i < nRelations; ++i)
			if (processed.find(relations[i]) == processed.end())
				break;

		// Find rest of members
		queue<string> toProcess;
		toProcess.push(relations[i]);
		set<string> currProcessed;
		while(!toProcess.empty()){
			currProcessed.insert(toProcess.front());
			if (joinLists.find(toProcess.front()) != joinLists.end()){
				for (set<string>::iterator it = joinLists[toProcess.front()].begin(); 
					 it != joinLists[toProcess.front()].end(); ++it){
					if (currProcessed.find(*it) == currProcessed.end())
						toProcess.push(*it);
				}
			}
			toProcess.pop();
		}

		// Update join groups
		relGroups.push_back(currProcessed);
		processed.insert(currProcessed.begin(), currProcessed.end());
	}

	// Make condition groups
	vector<vector<Condition> > condGroups(relGroups.size());
	for (int i = 0; i < myConds.size(); ++i){
		for (int k = 0; k < relGroups.size(); ++k){
			if (relGroups[k].find(myConds[i].lhsAttr.relName) != relGroups[k].end()){
				condGroups[k].push_back(myConds[i]);
				break;
			}
		}
	}

	// Create relation/selection/join nodes
	vector<Node> groupNodes;
	if (!EXT){
		// Applies selections as deeply as possible
		// No condition ordering
		// Only does file iteration for now...

		for (int k = 0; k < relGroups.size(); ++k){
			// Initialize list, create relation/selection nodes
			list<Node> needToJoin;
			for (set<string>::iterator it = relGroups[k].begin(); it != relGroups[k].end(); ++it){
				Relation rel(smm, it->c_str(), calcProj, projVector.size(), &projVector[0]);
				if (!rel.rc)
					return rel.rc;
				Selection sel(smm, rmm, ixm, rel, condGroups[k].size(), &condGroups[k][0], calcProj, projVector.size(), &projVector[0]);
				if (!sel.rc)
					needToJoin.push_back(sel);
				else
					needToJoin.push_back(rel);
			}

			// Joins
			Node left = *needToJoin.begin();
			set<Condition> leftConds(left.conditions, left.conditions + left.numConditions);
			vector<Condition> currConds;
			for (vector<Condition>::iterator it = condGroups[k].begin(); it != condGroups[k].end(); ++it){
				if (leftConds.find(*it) == leftConds.end())
					currConds.push_back(*it);
			}
			while (needToJoin.size() > 1){
				for (list<Node>::iterator it = (++needToJoin.begin()); it != needToJoin.end(); ++it){
					Node right = *it;
					Join join(smm, rmm, ixm, left, right, currConds.size(), &currConds[0], calcProj, projVector.size(), &projVector[0]);
					if(!join.rc){
						left = join;
						needToJoin.erase(it);

						vector<Condition> newConds;
						set<Condition> tmpConds (join.conditions, join.conditions+join.numConditions);
						for (vector<Condition>::iterator it = currConds.begin(); it != currConds.end(); ++it){
							if (tmpConds.find(*it) == tmpConds.end())
								newConds.push_back(*it);
						}
						currConds = newConds;
						break;
					}
				}
			}
			groupNodes.push_back(*needToJoin.begin());
		}
	}
	else {
		// TODO
		for (int i = 0; i < relGroups.size(); ++i){
			// [set size] [condition set] . (cost | Node)
			vector<map<set<Condition>, pair<int, Node> > > tables;


			// Reset parent field in each node of min cost tree
			// set groupNodes[i] to min cost tree root
		}

		// Found min join-selection ordering
		for (int i = 0; i < groupNodes.size(); ++i)
			SetParents(groupNodes[i]);
	}

	// Create cross nodes 
	// No cross needed
	if (relGroups.size() == 1){
		qPlan = &groupNodes[0];
		return 0;
	}
	if (relGroups.size() == 2){
		Node left = groupNodes[0];
		Node right = groupNodes[1];
		qPlan = &Cross(smm, rmm, ixm, left, right, calcProj, projVector.size(), &projVector[0]);
		return 0;
	}
	if (!EXT){
		// Arbitrary crossing
		Node* left = &groupNodes[0];
		Node* right = &groupNodes[1];
		Cross last(smm, rmm, ixm, *left, *right, calcProj, projVector.size(), &projVector[0]);
		for (int i = 2; i < groupNodes.size(); ++i){
			left = &last;
			right = &groupNodes[i];
			last = Cross(smm, rmm, ixm, *left, *right, calcProj, projVector.size(), &projVector[0]);
		}
		qPlan = &last;
	}
	else {
		// Initialize tables
		vector<map<set<int>, pair<int, Node> > > tables;
		for (int i = 0; i < groupNodes.size(); ++i){
			set<int> tmp;
			tmp.insert(i);
			tables[1][tmp] = pair<int, Node>(0, groupNodes[i]);
		}
		// Dynamic alg for crosses
		for (int i = 2; i <= groupNodes.size(); ++i){
			// Generate subsets of size i
			vector<vector<int> > subsets; 
			for(map<set<int>, pair<int, Node> >::iterator it = tables[i-1].begin(); it != tables[i-1].end(); ++it){
				vector<int> tmp(it->first.begin(), it->first.end());
				for (int i = 0; i < groupNodes.size(); ++i){
					if (it->first.find(i) == it->first.end()){;
						subsets.push_back(tmp);
						subsets.back().push_back(i);
					}
				}
			}

			for (int k = 0; k < subsets.size(); ++k){
				// Get min cost of subset
				set<int> setKey(subsets[k].begin(), subsets[k].end());

				for (int divide = 1; divide < i; ++divide){
					int leftSize = divide;
					int rightSize = i - divide;
					set<int> leftSet(subsets[k].begin(), subsets[k].begin() + divide);
					set<int> rightSet(subsets[k].begin() + divide, subsets[k].end());
					int cost = (tables[leftSize][leftSet].first + 
						        tables[rightSize][rightSet].first +
								CrossCost(tables[leftSize][leftSet].second, tables[rightSize][rightSet].second));

					if (divide == 1 || cost < tables[i][setKey].first){
						Node left = tables[leftSize][leftSet].second;
						Node right = tables[rightSize][rightSet].second;

						Cross cross(smm, rmm, ixm, left, right, calcProj, projVector.size(), &projVector[0]);
						cross.cost = cost;
						cross.numTuples = left.numTuples * right.numTuples;
						cross.tupleSize = left.tupleSize + right.tupleSize;

						tables[i][setKey] = pair<int, Node>(cost, cross);
					}
				}
			}
		}

		// Found min cost cross ordering
		qPlan = &tables[groupNodes.size()].begin()->second.second;
		SetParents(*qPlan.root);

		//// Sort join groups by byte size
		//sort(groupNodes.begin(), groupNodes.end(), sortJoins);
		//// Create crosses smallest->largest 
		//Node left = groupNodes[0];
		//Node right = groupNodes[1];
		//qPlan = Cross(smm, rmm, ixm, left, right, calcProj, projVector.size(), &projVector[0]);
		//for (int i = 2; i < groupNodes.size(); ++i){
		//	left = qPlan;
		//	right = groupNodes[i];
		//	qPlan = Cross(smm, rmm, ixm, left, right, calcProj, projVector.size(), &projVector[0]);
		//}
	}
	return 0;
}
int QL_Manager::SelectCost(Node &left){
	// TODO: determine selectivity of each condition + best access path ->cost
	return 0;
}
int QL_Manager::JoinCost(Node &left, Node &right){
	// TODO: determine selectivity of each condition + best access path ->cost
	return 0;
}
int QL_Manager::CrossCost(Node &left, Node &right){
	int leftNumTuples = left.numTuples;
	int leftLen = left.tupleSize;
	int rightNumTuples = right.numTuples;
	int rightLen = right.tupleSize;

	int total = (leftNumTuples * leftLen) + (rightNumTuples * leftLen); // read
	total += (leftNumTuples * rightNumTuples) * (leftLen + rightLen); // write
	return total;
}
void QL_Manager::SetParents(Node &node){
	if (node.child){
		node.child->parent = &node;
		SetParents(*node.child);
	}
	if (node.otherChild){
		node.otherChild->parent = &node;
		SetParents(*node.otherChild);
	}
}
//bool sortJoins(const Node left, const Node right){
//	int leftSize = left.numTuples * (left.tupleSize);
//	int rightSize = right.numTuples * (right.tupleSize);
//	// right.outAttrs[right.numOutAttrs - 1].offset + right.outAttrs[right.numOutAttrs - 1].attrLen
//
//	return  leftSize < rightSize;
//}

RC QL_Manager::GetResults(Node &qPlan)
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

void QL_Manager::PrintQueryPlan(Node &qPlan)
{
	RecursivePrint(qPlan, 0);
}
// Go down tree, print each branch one at a time, right-most branch first
void QL_Manager::RecursivePrint(Node &node, int indent){
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




// Assume conditions ordered by 1. value conditions 2. has index 3. selectivity
//bool SelectionUseIndex(Condition cond, map<pair<char*, char*>, Attrcat> &attrcats){
//	// Not a value condition
//	if (cond.bRhsIsAttr)
//		return false;
//	// Not an indexed attribute
//	pair<char*, char*> key(cond.lhsAttr.relName, cond.lhsAttr.attrName);
//	if (attrcats[key].indexNo == -1)
//		return false;
//	// TODO: Remove requirement is a val condition?
//	// TODO: check if relation size/selectivity warrants index scan (est. indexscan IOs < filescan IOs)
//	return true;
//}

// Assume conditions ordered by 1. at least one attribute has index 2. selectivity
//bool JoinUseIndex(Condition cond, map<pair<char*, char*>, Attrcat> &attrcats, map<pair<char*, char*>, Attrcat> &otherAttrcats){
//	pair<char*, char*> key(cond.lhsAttr.relName, cond.lhsAttr.attrName);
//	pair<char*, char*> otherKey(cond.rhsAttr.relName, cond.rhsAttr.attrName);
//	
//	map<pair<char*, char*>, Attrcat>* keyAttrcats = &attrcats;
//	map<pair<char*, char*>, Attrcat>* otherKeyAttrcats = &otherAttrcats;
//	if (attrcats.find(key) == attrcats.end()){
//		keyAttrcats = &otherAttrcats;
//		otherKeyAttrcats = &attrcats;
//	}
//
//	// No indexed attributes
//	if (keyAttrcats->at(key).indexNo == -1 && otherKeyAttrcats->at(otherKey).indexNo == -1)
//		return false;
//
//	// TODO: check if relation sizes/most selective condition is selective enough to warrant index scan
//	// TODO: flip condition attributes/op if necessary so rhsAttr is the indexed one
//
//	return true;
//}