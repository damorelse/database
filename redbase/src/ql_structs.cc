#include <vector>
#include <set>
#include <map>
#include <iterator>
#include <fstream>
#include <algorithm>
#include <utility>
#include "ql.h"
#include "sm.h"

using namespace std;

Node::Node(){
	numConditions = 0;
	conditions = NULL;

	memset(type, '\0', MAXNAME+1);
	child = NULL;
	otherChild = NULL;
	parent = NULL;
	memset(output, '\0', MAXNAME+1);

	numRelations = 0;
	relations = NULL;
	numRids = 0;
	rids = NULL;

	numOutAttrs = 0;
	outAttrs = NULL;
	numCountPairs = 0;
	pCounts = NULL;
	project = false;

	rc = 0;
}
Node::~Node(){
	delete [] conditions;
	conditions = NULL;
	delete [] relations;
	relations = NULL;
	delete [] rids;
	rids = NULL;
	delete [] outAttrs;
	outAttrs = NULL;
	delete [] pCounts;
	pCounts = NULL;
}
RC Node::execute(){/*nothing; should set output*/}
void Node::printType(){
	cout << type;
	if (numConditions == 0)
		return;

	cout << " (";
	for (int i = 0 ; i < numConditions; ++i){
		cout << conditions[i].lhsAttr.relName << "." << conditions[i].lhsAttr.attrName << " ";
		switch (conditions[i].op){
		case EQ_OP:
			cout << "=";
			break;
		case NE_OP:
			cout << "<>";
			break;
		case LT_OP:
			cout << "<";
			break;
		case GT_OP:
			cout << ">";
			break;
		case LE_OP:
			cout << "<=";
			break;
		case GE_OP:
			cout << ">=";
			break;
		};
		if (i + 1 < numConditions)
			cout << ", ";
	}
	cout << ")";
}
Attrcat Node::getAttrcat(const char *relName, char* attrName){
	for (int i = 0; i < numOutAttrs; ++i){
		if (strcmp(outAttrs[i].relName, relName) == 0 &&
			strcmp(outAttrs[i].attrName, attrName) == 0)
			return outAttrs[i];
	}
	return NULL;
}

bool ConditionApplies(Condition &cond){
	return false;
}
void Node::SetRelations(){
	numRelations = child->numRelations;
	if (otherChild)
		numRelations += otherChild->numRelations;
	// relations
	relations = new char[numRelations * (MAXNAME+1)];
	memcpy(relations, child->relations, child->numRelations * (MAXNAME+1));
	if (otherChild)
		memcpy(relations + child->numRelations * (MAXNAME+1), otherChild->relations, otherChild->numRelations * (MAXNAME+1));
}
void Node::SetRids(){
	numRids = numRelations;
	// rids
	rids = new Attrcat[numRids];
	if (child->numRids == 0)
		rids[0] = Attrcat(child->output, (char*)".rid", 0, STRING, sizeof(RID), -1);
	else
		memcpy(rids, child->rids, child->numRids * sizeof(Attrcat));
	if (otherChild){
		if (otherChild->numRids == 0)
			rids[child->numRelations] = Attrcat(otherChild->output, (char*)".rid", 0, STRING, sizeof(RID), -1);
		else
			memcpy(rids+child->numRelations, otherChild->rids, otherChild->numRids * sizeof(Attrcat));
	}
	// Update offsets
	for(int i = 0; i < numRids; ++i)
		rids[i].offset = i * sizeof(RID);
}
void Node::SetOutAttrs(){
	// numOutAttrs
	numOutAttrs = child->numOutAttrs;
	if (otherChild)
		numOutAttrs += otherChild->numOutAttrs;

	// outAttrs
	outAttrs = new Attrcat[numOutAttrs];
	memcpy(outAttrs, child->outAttrs, child->numOutAttrs * sizeof(Attrcat));
	if (otherChild)
		memcpy(outAttrs + child->numOutAttrs, otherChild->outAttrs, otherChild->numOutAttrs * sizeof(Attrcat));
	// Update offsets
	int offset = numRids * sizeof(RID);
	for (int i = 0; i < numOutAttrs; ++i){
		outAttrs[i].offset = offset;
		offset += outAttrs[i].attrLen;
	}
}
void Node::Project(bool calcProj, int numTotalPairs, pair<RelAttr, int> *pTotals){
	// Create projection counts map
	map<RelAttr, int> projCounts;
	// Include children counts
	if (child){
		for (int i = 0; i < child->numCountPairs; ++i)
			projCounts[child->pCounts[i].first] = child->pCounts[i].second;
		if (otherChild){
			for (int i = 0; i < otherChild->numCountPairs; ++i)
				projCounts[otherChild->pCounts[i].first] = otherChild->pCounts[i].second;
		}
	}
	// Include conditions counts
	for (int i = 0; i < numConditions; ++i){
		RelAttr tmp(conditions[i].lhsAttr.relName, conditions[i].lhsAttr.attrName);
		projCounts[tmp] += 1;
		if (conditions[i].bRhsIsAttr){
			RelAttr tmp2(conditions[i].rhsAttr.relName, conditions[i].rhsAttr.attrName);
			projCounts[tmp2] += 1;
		}
	}
	// Update numPairs / projCount
	numCountPairs = projCounts.size();
	if (numCountPairs > 0){
		vector<pair<RelAttr, int> > tmp;
		copy(projCounts.begin(), projCounts.end(), back_inserter(tmp));
		pCounts = &tmp[0];
	}

	if (calcProj){
		// Recreate projection totals map
		map<RelAttr, int> projTotals;
		for (int i = 0; i < numTotalPairs; ++i)
			projTotals[pTotals[i].first] = pTotals[i].second;

		// Determine which attributes need to be kept
		vector<Attrcat> newOutAttrs;
		int offset = numRids * sizeof(RID);
		for (int i = 0; i < numOutAttrs; ++i){
			RelAttr tmp(outAttrs[i].relName, outAttrs[i].attrName);
			if (projTotals.find(tmp) != projTotals.end() && 
				(numCountPairs == 0 || projCounts[tmp] < projTotals[tmp])){
				newOutAttrs.push_back(outAttrs[i]);
				newOutAttrs.back().offset = offset;
				offset += outAttrs[i].attrLen;
			}
		}

		// Projection needed
		if (newOutAttrs.size() < numOutAttrs){
			numOutAttrs = newOutAttrs.size();
			delete [] outAttrs;
			outAttrs = &newOutAttrs[0];
			project = true;
		}
	}
}
RC Node::CreateTmpOutput(){
	// Set output
	char* fileName = tmpnam(NULL);
	strcpy(output, fileName);
	// Create output relation
	vector<AttrInfo> attributes;
	for (int i = 0; i < numRids; ++i)
		attributes.push_back(AttrInfo(rids[i]));
	for (int i = 0; i < numOutAttrs; ++i)
		attributes.push_back(AttrInfo(outAttrs[i]));
	if (rc = smm->CreateTable(output, numOutAttrs, &attributes[0]))
		return rc;
	return 0;
}
bool Node::CheckCondition(char* pData, Condition cond){
	// TODO
}
RC Node::WriteToOutput(RM_Record record, RM_Record otherRecord, char* outPData, RM_FileHandle &outFile){
	char* pData;
	if (rc = record.GetData(pData))
		return rc;
	RID rid;
	if (rc = record.GetRid(rid))
		return rc;
	char* otherPData;
	RID otherRid;
	if (otherChild){
		if (rc = otherRecord.GetData(otherPData))
			return rc;
		if (rc = otherRecord.GetRid(otherRid))
			return rc;
	}

	// Write to output
	char* outItr = outPData;
	// RIDS
	if (child->numRids == 0){
		memcpy(outItr, &rid, sizeof(RID));
		outItr += sizeof(RID);
	}
	else {
		memcpy(outItr, pData, child->numRids *sizeof(RID));
		outItr += child->numRids *sizeof(RID);
	}
	if (otherChild){
		if (otherChild->numRids == 0){
			memcpy(outItr, &otherRid, sizeof(RID));
			outItr += sizeof(RID);
		}
		else{
			memcpy(outItr, otherPData, otherChild->numRids * sizeof(RID));
			outItr += otherChild->numRids * sizeof(RID);
		}
	}
	// Other attributes
	map<pair<char*, char*>, Attrcat> attrcats;
	for (int i = 0; i < child->numOutAttrs; ++i){
		pair<char*, char*> key = make_pair(child->outAttrs[i].relName, child->outAttrs[i].attrName);
		attrcats[key] = child->outAttrs[i];
	}
	map<pair<char*, char*>, Attrcat> otherAttrcats;
	if (otherChild){
		for (int i = 0; i < otherChild->numOutAttrs; ++i){
			pair<char*, char*> key = make_pair(otherChild->outAttrs[i].relName, otherChild->outAttrs[i].attrName);
			attrcats[key] = otherChild->outAttrs[i];
		}
	}
	for (int i = 0; i < numOutAttrs; ++i){
		pair<char*, char*> key = make_pair(outAttrs[i].relName, outAttrs[i].attrName);
		if (attrcats.find(key) != attrcats.end())
			memcpy(outPData + outAttrs[i].offset, pData + attrcats[key].offset, outAttrs[i].attrLen);
		else
			memcpy(outPData + outAttrs[i].offset, otherPData + otherAttrcats[key].offset, outAttrs[i].attrLen);
	}

	// Insert
	RID tmp;
	outFile.InsertRec(outPData, tmp);
}
RC Node::DeleteTmpInput(){
	// Delete input files (if NOT relations)
	if (child->numRids != 0){
		if (rc = smm->DropTable(child->output))
			return rc;
	}
	if (otherChild && otherChild->numRids != 0){
		if (rc = smm->DropTable(otherChild->output))
			return rc;
	}
}


Selection::Selection(SM_Manager *smm, RM_Manager *rmm, IX_Manager *ixm, Node& left, int numConds, Condition *conds, bool calcProj, int numTotalPairs, pair<RelAttr, int> *pTotals){
	// set parent for children
	left.parent = this;

	// numConditions and conditions
	vector<Condition> condVector;
	for (int i = 0; i < numConds; ++i){
		if (ConditionApplies(conds[i]))
			condVector.push_back(conds[i]);
	}
	numConditions = condVector.size();
	copy(condVector.begin(), condVector.end(), conditions);
	// Early exit
	if (numConditions == 0){
		rc = QL_SELNODE;
		return;
	}

	this->smm = smm;
	this->rmm = rmm;
	this->ixm = ixm;
	strcpy(type, "Select");
	child = &left;
	// otherChild set by default
	// parent will be set by parent
	// output will be set by execute

	SetRelations();
	SetRids();
	SetOutAttrs();
	Project(calcProj, numTotalPairs, pTotals);
}
bool compareRID(const RID& lhs, const RID& rhs)
{
  return lhs.pageNum < rhs.pageNum;
}
bool Selection::UseIndex(Condition cond){
	// TODO
	return true;
}
RC Selection::execute(){
	if (rc = CreateTmpOutput())
		return rc;

	// Open files
	RM_FileHandle outFile;
	if (rc = rmm->OpenFile(output, outFile))
		return rc;
	RM_FileHandle file;
	if(rc = rmm->OpenFile(child->output, file))
		return rc;
	
	int len = outAttrs[numOutAttrs-1].offset + outAttrs[numOutAttrs-1].attrLen;
	char* outPData = new char[len];
	memset(outPData, '\0', len);

	// If no index-merging at all...
	if (!UseIndex(conditions[0])){
		RM_FileScan scan;
		if (rc = scan.OpenScan(file, INT, 4, 0, NO_OP, NULL))
			return rc;
		RM_Record record;
		while( OK_RC == (rc = scan.GetNextRec(record))){
			char* pData;
			if (rc = record.GetData(pData))
				return rc;

			// Check rest of conditions
			bool insert = true;
			for (int k = 0; insert && k < numConditions; ++k)
				insert = CheckCondition(pData, conditions[k]);
			if (insert){
				if (rc = WriteToOutput(record, record, outPData, outFile))
					return rc;
			}
		}
		if (rc != RM_EOF)
			return rc;
	}
	// Perform index merging
	else {
		set<RID> currRids;
		int firstNonIndexed;
		for (int i = 0; i < numConditions && UseIndex(conditions[i]); ++i){
			// TODO: Perform index condition
		}
		// Get data of rids
		vector<RID> ridVector(currRids.begin(), currRids.end());
		sort(ridVector.begin(), ridVector.end(), compareRID);
		RM_Record record;
		for (int i = 0; i < ridVector.size(); ++i){
			if (rc = file.GetRec(ridVector[i], record))
				return rc;
			char* pData;
			if (rc = record.GetData(pData))
				return rc;

			// Check rest of conditions
			bool insert = true;
			for (int k = firstNonIndexed; insert && k < numConditions; ++k)
				insert = CheckCondition(pData, conditions[k]);
			if (insert){
				if (rc = WriteToOutput(record, record, outPData, outFile))
					return rc;
			}
		}
	}

	delete [] outPData;

	// Close files
	if (rc = rmm->CloseFile(file))
		return rc;
	if (rc = rmm->CloseFile(outFile))
		return rc;

	if (rc = DeleteTmpInput())
		return rc;
}
bool Selection::ConditionApplies(Condition &cond){
	if (strcmp(relations, cond.lhsAttr.relName) != 0)
		return false;
	if (cond.bRhsIsAttr && 
		strcmp(relations, cond.rhsAttr.relName) != 0)
		return false;
	return true;
}

Join::Join(SM_Manager *smm, RM_Manager *rmm, IX_Manager *ixm, Node& left, Node& right, int numConds, Condition *conds, bool calcProj, int numTotalPairs, pair<RelAttr, int> *pTotals){
	// set parent for both children
	left.parent = this;
	right.parent = this;

	// numConditions and conditions
	vector<Condition> condVector;
	for (int i = 0; i < numConds; ++i){
		if (ConditionApplies(conds[i]))
			condVector.push_back(conds[i]);
	}
	numConditions = condVector.size();
	copy(condVector.begin(), condVector.end(), conditions);
	// Early exit
	if (numConditions == 0){
		rc = QL_JOINNODE;
		return;
	}

	this->smm = smm;
	this->rmm = rmm;
	this->ixm = ixm;
	strcpy(type, "Join");
	child = &left;
	otherChild = &right;
	// parent will be set by parent
	// output will be set by execute

	SetRelations();
	SetRids();
	SetOutAttrs();
	Project(calcProj, numTotalPairs, pTotals);
}
// Assume typical case is one joining condition
RC Join::execute(){
	if (rc = CreateTmpOutput())
		return rc;
	
	// TODO

	if (rc = DeleteTmpInput())
		return rc;
}
bool Join::ConditionApplies(Condition &cond){
	if (!cond.bRhsIsAttr)
		return false;
	set<char*> leftRel(child->relations, child->relations+child->numRelations);
	set<char*> rightRel(otherChild->relations, otherChild->relations+otherChild->numRelations);
	if (leftRel.find(cond.lhsAttr.relName) != leftRel.end() &&
		rightRel.find(cond.rhsAttr.relName) != rightRel.end())
		return true;
	if (leftRel.find(cond.rhsAttr.relName) != leftRel.end() &&
		rightRel.find(cond.lhsAttr.relName) != rightRel.end())
		return true;
	return false;
}

Cross::Cross(SM_Manager *smm, RM_Manager *rmm, IX_Manager *ixm, Node &left, Node &right, bool calcProj, int numTotalPairs, pair<RelAttr, int> *pTotals){
	// set parent for both children
	left.parent = this;
	right.parent = this;

	// numConditions and conditions default set

	this->smm = smm;
	this->rmm = rmm;
	this->ixm = ixm;
	strcpy(type, "X");
	child = &left;
	otherChild = &right;
	// parent will be set by parent
	// output will be set by execute

	SetRelations();
	SetRids();
	SetOutAttrs();
	Project(calcProj, numTotalPairs, pTotals);
}
RC Cross::execute(){
	if (rc = CreateTmpOutput())
		return rc;

	// Open files and filescans
	RM_FileHandle outFile;
	if (rc = rmm->OpenFile(output, outFile))
		return rc;
	RM_FileHandle file;
	if(rc = rmm->OpenFile(child->output, file))
		return rc;
	RM_FileHandle otherFile;
	if(rc = rmm->OpenFile(otherChild->output, otherFile))
		return rc;
	RM_FileScan scan;
	if (rc = scan.OpenScan(file, INT, 4, 0, NO_OP, NULL))
		return rc;
	RM_FileScan otherScan;
	if (rc = otherScan.OpenScan(otherFile, INT, 4, 0, NO_OP, NULL))
		return rc;

	// Iterate over files
	int len = outAttrs[numOutAttrs-1].offset + outAttrs[numOutAttrs-1].attrLen;
	char* outPData = new char[len];
	memset(outPData, '\0', len);

	RM_Record record;
	while(OK_RC == (rc = scan.GetNextRec(record))){
		RM_Record otherRecord;
		while (OK_RC == (rc = otherScan.GetNextRec(otherRecord))){
			if (rc = WriteToOutput(record, otherRecord, outPData, outFile))
				return rc;
		}
		if (rc != RM_EOF)
			return rc;
	}
	if (rc != RM_EOF)
		return rc;

	delete [] outPData;

	// Close filescans and files
	if (rc = otherScan.CloseScan())
		return rc;
	if (rc = scan.CloseScan())
		return rc;
	if (rc = rmm->CloseFile(otherFile))
		return rc;
	if (rc = rmm->CloseFile(file))
		return rc;
	if (rc = rmm->CloseFile(outFile))
		return rc;

	if (rc = DeleteTmpInput())
		return rc;
}

Relation::Relation(SM_Manager *smm, const char *relName, bool calcProj, int numTotalPairs, pair<RelAttr, int> *pTotals){
	// numConditions/conditions set by default

	this->smm = smm;
	strcpy(type, relName);
	// child/otherChild set by default
	// parent set by parent node
	strcpy(output, relName);

	numRelations = 1;
	relations = new char[numRelations * (MAXNAME+1)];
	strcpy(relations, relName);
	// numRids and rids set by default (0, NULL)

	// numOutAttrs
	RM_Record record;
	char* pData;
	if (smm->GetRelcatRecord(relName, record)){
		rc = QL_RELNODE;
		return;
	}
	if (record.GetData(pData)){
		rc = QL_RELNODE;
		return;
	}
	Relcat relcat(pData);
	numOutAttrs = relcat.attrCount;
	// outAttrs
	outAttrs = new Attrcat[relcat.attrCount];
	if (smm->GetAttrcats(relName, outAttrs)){
		rc = QL_RELNODE;
		return;
	}

	Project(calcProj, numTotalPairs, pTotals);
}