#include <vector>
#include <set>
#include <map>
#include <iterator>
#include <fstream>
#include <algorithm>
#include <utility>
#include <string>
#include "ql.h"
#include "sm.h"

using namespace std;

RC WriteToOutput(Node* child, Node* otherChild, int numOutAttrs, Attrcat *outAttrs, map<pair<string, string>, Attrcat> &attrcats, map<pair<string, string>, Attrcat> &otherAttrcats, RM_Record record, RM_Record otherRecord, char* outPData, RM_FileHandle &outFile);
bool CheckSelectionCondition(char* pData, Condition cond, map<pair<string, string>, Attrcat> &attrcats);
bool CheckJoinCondition(char* pData, char* otherPData, Condition cond, map<pair<string, string>, Attrcat> &attrcats, map<pair<string, string>, Attrcat> &otherAttrcats);
bool CheckCondition(CompOp op, AttrType attrType, char* leftPtr, const int leftLen, char* rightPtr, const int rightLen);
bool isJoinCondition(Condition &cond);
bool SelectionConditionApplies(Condition &cond, set<string> &myRelations);
bool JoinConditionApplies(Condition &cond, set<string> &myRelations);
Attrcat GetAttrcat(RelAttr relAttr, map<pair<string, string>, Attrcat> &attrcats, map<pair<string, string>, Attrcat> &otherAttrcats);


RelAttrCount::RelAttrCount( const RelAttr& relAttr, const int& count): first(relAttr), second(count){}
bool RelAttrCount::operator==(const RelAttrCount &other) const{
	return (first == other.first && second == other.second);
}


// Public
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
	execution = FILE;
	cost = 0;
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
CompOp Node::FlipOp(CompOp op){
	CompOp flipOp;
	switch(op){
	case LT_OP:
		flipOp = GT_OP;
		break;
	case LE_OP:
		flipOp = GE_OP;
		break;
	case GT_OP:
		flipOp = LT_OP;
		break;
	case GE_OP:
		flipOp = LE_OP;
		break;
	default:
		flipOp = op;
		break;
	}
	return flipOp;
}


// Protected
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
void Node::Project(bool calcProj, int numTotalPairs, RelAttrCount *pTotals){
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
		vector<RelAttrCount > tmp;
		for (map<RelAttr, int>::iterator it = projCounts.begin(); it != projCounts.end(); ++it)
			tmp.push_back(RelAttrCount(it->first, it->second));
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


// Local functions
RC WriteToOutput(Node* child, Node* otherChild, int numOutAttrs, Attrcat *outAttrs, map<pair<string, string>, Attrcat> &attrcats, map<pair<string, string>, Attrcat> &otherAttrcats, RM_Record record, RM_Record otherRecord, char* outPData, RM_FileHandle &outFile){
	RC rc;
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
	
	for (int i = 0; i < numOutAttrs; ++i){
		pair<string, string> key(outAttrs[i].relName, outAttrs[i].attrName);
		if (attrcats.find(key) != attrcats.end())
			memcpy(outPData + outAttrs[i].offset, pData + attrcats[key].offset, outAttrs[i].attrLen);
		else
			memcpy(outPData + outAttrs[i].offset, otherPData + otherAttrcats[key].offset, outAttrs[i].attrLen);
	}

	// Insert
	RID tmp;
	outFile.InsertRec(outPData, tmp);
}
// Value condition OR condition's attributes from same relation
bool CheckSelectionCondition(char* pData, Condition cond, map<pair<string, string>, Attrcat> &attrcats){
	pair<string, string> leftKey(cond.lhsAttr.relName, cond.lhsAttr.attrName);

	Attrcat leftAttrcat = attrcats[leftKey];
	int leftLen = leftAttrcat.attrLen;
	char* leftPtr = pData + leftAttrcat.offset;

	char* rightPtr = (char*)cond.rhsValue.data;
	if (cond.bRhsIsAttr){
		pair<string, string> rightKey(cond.rhsAttr.relName, cond.rhsAttr.attrName);
		rightPtr = pData + attrcats[rightKey].offset;
	}

	return CheckCondition(cond.op, leftAttrcat.attrType, leftPtr, leftLen, rightPtr, -1);
}
// Condition's attributes from different relations
bool CheckJoinCondition(char* pData, char* otherPData, Condition cond, map<pair<string, string>, Attrcat> &attrcats, map<pair<string, string>, Attrcat> &otherAttrcats)
{
	pair<string, string> leftKey(cond.lhsAttr.relName, cond.lhsAttr.attrName);

	if (!isJoinCondition(cond)){
		if (attrcats.find(leftKey) != attrcats.end())
			return CheckSelectionCondition(pData, cond, attrcats);
		else
			return CheckSelectionCondition(otherPData, cond, otherAttrcats);
	}

	pair<string, string> rightKey(cond.rhsAttr.relName, cond.rhsAttr.attrName);

	int leftLen, rightLen;
	char* leftPtr; 
	char* rightPtr;
	AttrType attrType;
	if (attrcats.find(leftKey) != attrcats.end()){
		Attrcat leftAttrcat = attrcats[leftKey];
		leftLen = leftAttrcat.attrLen;
		leftPtr = pData + leftAttrcat.offset;

		Attrcat rightAttrcat = otherAttrcats[rightKey];
		rightLen = rightAttrcat.attrLen;
		rightPtr = otherPData + rightAttrcat.offset;

		attrType = leftAttrcat.attrType;
	}
	else {
		Attrcat leftAttrcat = otherAttrcats[leftKey];
		leftLen = leftAttrcat.attrLen;
		leftPtr = otherPData + leftAttrcat.offset;

		Attrcat rightAttrcat = attrcats[rightKey];
		rightLen = rightAttrcat.attrLen;
		rightPtr = pData + rightAttrcat.offset;

		attrType = leftAttrcat.attrType;
	}
	
	return CheckCondition(cond.op, attrType, leftPtr, leftLen, rightPtr, rightLen);
}
bool CheckCondition(CompOp op, AttrType attrType, char* leftPtr, const int leftLen, char* rightPtr, const int rightLen){
	int a_i, v_i;
	float a_f, v_f;
	string a_s, v_s;
	switch(attrType) {
	case INT:
		memcpy(&a_i, leftPtr, 4);
		memcpy(&v_i, rightPtr, 4);
        break;
	case FLOAT:
		memcpy(&a_f, leftPtr, 4);
		memcpy(&v_f, rightPtr, 4);
        break;
	case STRING:
		char*  tmp = new char [leftLen + 1];
		tmp[leftLen] = '\0';
		memcpy(tmp, leftPtr, leftLen);
		a_s = string(tmp);
		delete [] tmp;

		if (rightLen == -1)
			v_s = string(rightPtr);
		else 
		{
			tmp = new char [rightLen + 1];
			tmp[rightLen] = '\0';
			memcpy(tmp, rightPtr, rightLen);
			v_s = string(tmp);
			delete [] tmp;
		}
        break;
	}
	// Check if fulfills condition
	switch(op) {
	case EQ_OP:
		switch(attrType) {
		case INT:
			return a_i == v_i;
            break;
		case FLOAT:
			return a_f == v_f;
            break;
		case STRING:
			return a_s == v_s;
            break;
		}
        break;
	case LT_OP:
		switch(attrType) {
		case INT:
			return a_i < v_i;
            break;
		case FLOAT:
			return a_f < v_f;
            break;
		case STRING:
			return a_s < v_s;
            break;
		}
        break;
	case GT_OP:
		switch(attrType) {
		case INT:
			return a_i > v_i;
            break;
		case FLOAT:
			return a_f > v_f;
            break;
		case STRING:
			return a_s > v_s;
            break;
		}
        break;
	case LE_OP:
		switch(attrType) {
		case INT:
			return a_i <= v_i;
            break;
		case FLOAT:
			return a_f <= v_f;
            break;
		case STRING:
			return a_s <= v_s;
            break;
		}
        break;
	case GE_OP:
		switch(attrType) {
		case INT:
			return a_i >= v_i;
            break;
		case FLOAT:
			return a_f >= v_f;
            break;
		case STRING:
			return a_s >= v_s;
            break;
		}
        break;
	case NE_OP:
		switch(attrType) {
		case INT:
			return a_i != v_i;
            break;
		case FLOAT:
			return a_f != v_f;
            break;
		case STRING:
			return a_s != v_s;
            break;
		}
        break;
	}
}
bool isJoinCondition(Condition &cond){
	return cond.bRhsIsAttr && strcmp(cond.lhsAttr.relName, cond.rhsAttr.relName) != 0;
}

// Only selection conditions
Selection::Selection(SM_Manager *smm, RM_Manager *rmm, IX_Manager *ixm, Node& left, int numConds, Condition *conds, bool calcProj, int numTotalPairs, RelAttrCount *pTotals){
	// set parent for children
	left.parent = this;

	// numConditions and conditions
	set<string> myRelations;
	for (int i = 0; i < left.numRelations; ++i)
		myRelations.insert(left.relations + i * (MAXNAME + 1));
	vector<Condition> condVector;
	for (int i = 0; i < numConds; ++i){
		if (SelectionConditionApplies(conds[i], myRelations))
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
	
	// Make outPData
	int len = outAttrs[numOutAttrs-1].offset + outAttrs[numOutAttrs-1].attrLen;
	char* outPData = new char[len];
	memset(outPData, '\0', len);

	// Make attribute-attrcat maps
	map<pair<string, string>, Attrcat> attrcats;
	for (int i = 0; i < child->numOutAttrs; ++i){
		pair<string, string> key(child->outAttrs[i].relName, child->outAttrs[i].attrName);
		attrcats[key] = child->outAttrs[i];
	}
	
	// No index scan
	if (execution == FILE){
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
				insert = CheckSelectionCondition(pData, conditions[k], attrcats);
			if (insert){
				if (rc = WriteToOutput(child, otherChild, numOutAttrs, outAttrs, attrcats, attrcats, record, record, outPData, outFile))
					return rc;
			}
		}
		if (rc != RM_EOF)
			return rc;
		if (rc = scan.CloseScan())
			return rc;
	}
	// Use index scan (for value conditions only, with an index on lhs attribute)
	else if (execution == INDEX) {
		pair<string, string> key(conditions[0].lhsAttr.relName, conditions[0].lhsAttr.attrName);
		IX_IndexHandle index;
		if (rc = ixm->OpenIndex(attrcats[key].relName, attrcats[key].indexNo, index))
			return rc;
		IX_IndexScan indexScan;
		// Check if operation is Not Equal (which is not implemented in ix scan)
		CompOp op = conditions[0].op;
		if (conditions[0].op == NE_OP){
			op = LT_OP;
		}
		for (int i = 0; i < 1 || (conditions[0].op == NE_OP && i < 2); ++i){
			if (rc = indexScan.OpenScan(index, op, conditions[0].rhsValue.data))
				return rc;

			RID rid;
			while(OK_RC == (rc = indexScan.GetNextEntry(rid))){
				RM_Record record;
				if (rc = file.GetRec(rid, record))
					return rc;
				char* pData;
				if (rc = record.GetData(pData))
					return rc;

				// Check rest of conditions
				bool insert = true;
				for (int k = 1; insert && k < numConditions; ++k)
					insert = CheckSelectionCondition(pData, conditions[k], attrcats);
				if (insert){
					if (rc = WriteToOutput(child, otherChild, numOutAttrs, outAttrs, attrcats, attrcats, record, record, outPData, outFile))
						return rc;
				}
			}
			if (rc != IX_EOF)
				return rc;
			if (rc = indexScan.CloseScan())
				return rc;

			op = GT_OP;
		}
	}
	// Attribute conditions with indexes on both attributes
	else if (execution == INDEXES)
	{
		// TODO: index scans on both attributes
	}

	// Delete pData
	delete [] outPData;

	// Close files
	if (rc = rmm->CloseFile(file))
		return rc;
	if (rc = rmm->CloseFile(outFile))
		return rc;

	if (rc = DeleteTmpInput())
		return rc;
}
// Assumes condition not yet applied
bool SelectionConditionApplies(Condition &cond, set<string> &myRelations){
	// Check left attribute is in relations
	if (myRelations.find(cond.lhsAttr.relName)  == myRelations.end())
		return false;
	// Check is not a join condition
	if (isJoinCondition(cond))
		return false;
	return true;
}


// Both selection and join conditions
Join::Join(SM_Manager *smm, RM_Manager *rmm, IX_Manager *ixm, Node& left, Node& right, int numConds, Condition *conds, bool calcProj, int numTotalPairs, RelAttrCount *pTotals){
	// set parent for both children
	left.parent = this;
	right.parent = this;

	// numConditions and conditions
	set<string> myRelations;
	for (int i = 0; i < left.numRelations; ++i)
		myRelations.insert(left.relations + i * (MAXNAME + 1));
	for (int i = 0; i < right.numRelations; ++i)
		myRelations.insert(right.relations + i * (MAXNAME + 1));
	vector<Condition> condVector;
	for (int i = 0; i < numConds; ++i){
		if (JoinConditionApplies(conds[i], myRelations))
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
RC Join::execute(){
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
	
	// Make outPData
	int len = outAttrs[numOutAttrs-1].offset + outAttrs[numOutAttrs-1].attrLen;
	char* outPData = new char[len];
	memset(outPData, '\0', len);

	// Make attribute-attrcat maps
	map<pair<string, string>, Attrcat> attrcats;
	for (int i = 0; i < child->numOutAttrs; ++i){
		pair<string, string> key(child->outAttrs[i].relName, child->outAttrs[i].attrName);
		attrcats[key] = child->outAttrs[i];
	}
	map<pair<string, string>, Attrcat> otherAttrcats;
	for (int i = 0; i < otherChild->numOutAttrs; ++i){
		pair<string, string> key(otherChild->outAttrs[i].relName, otherChild->outAttrs[i].attrName);
		attrcats[key] = otherChild->outAttrs[i];
	}

	// No index scan
	if (execution == FILE)
	{
		RM_FileScan scan;
		if (rc = scan.OpenScan(file, INT, 4, 0, NO_OP, NULL))
			return rc;
		RM_FileScan otherScan;
		if (rc = otherScan.OpenScan(otherFile, INT, 4, 0, NO_OP, NULL))
			return rc;

		// Iterate over files
		RM_Record record;
		while(OK_RC == (rc = scan.GetNextRec(record))){
			char* pData;
			if (rc = record.GetData(pData))
				return rc;

			RM_Record otherRecord;
			while (OK_RC == (rc = otherScan.GetNextRec(otherRecord))){
				char* otherPData;
				if (rc = otherRecord.GetData(otherPData))
					return rc;

				// Check rest of conditions
				bool insert = true;
				for (int k = 0; insert && k < numConditions; ++k){
					insert = CheckJoinCondition(pData, otherPData, conditions[k], attrcats, otherAttrcats);
				}
				if (insert){
					if (rc = WriteToOutput(child, otherChild, numOutAttrs, outAttrs, attrcats, otherAttrcats, record, otherRecord, outPData, outFile))
						return rc;
				}
			}
			if (rc != RM_EOF)
				return rc;
		}
		if (rc != RM_EOF)
			return rc;

		if (rc = otherScan.CloseScan())
			return rc;
		if (rc = scan.CloseScan())
			return rc;
	}
	// Index scan of one attribute 
	// (AB join C with index on C's attribute, A join B with index on one attribute)
	else if (execution == INDEX)
	{
		// Assumes lhsAttr is the indexed one
		Attrcat left = GetAttrcat(conditions[0].lhsAttr, attrcats, otherAttrcats);
		Attrcat right = GetAttrcat(conditions[0].rhsAttr, attrcats, otherAttrcats);
		bool swap = (attrcats.find(pair<string, string>(right.relName, right.attrName)) == attrcats.end());

		RM_FileScan fileScan;
		if (!swap)
			rc = fileScan.OpenScan(file, INT, 4, 0, NO_OP, NULL);
		else
			rc = fileScan.OpenScan(otherFile, INT, 4, 0, NO_OP, NULL);
		if (rc)
			return rc;

		// Iterate over files
		RM_Record fileRecord;
		while(OK_RC == (rc = fileScan.GetNextRec(fileRecord))){
			char* fileData;
			if (rc = fileRecord.GetData(fileData))
				return rc;
			char * value = fileData + right.offset;

			IX_IndexHandle index;
			if (rc = ixm->OpenIndex(conditions[0].lhsAttr.relName, left.indexNo, index))
				return rc;
			IX_IndexScan indexScan;
			
			CompOp op = conditions[0].op;

			if (op == NE_OP){
				op = LT_OP;
			}
			for (int i = 0; i < 1 || (conditions[0].op == NE_OP && i < 2); ++i){
				if (rc = indexScan.OpenScan(index, op, value))
					return rc;

				RID rid;
				while (OK_RC == (rc = indexScan.GetNextEntry(rid))){
					RM_Record indexRecord;
					if (!swap)
						rc = otherFile.GetRec(rid,indexRecord);
					else 
						rc = file.GetRec(rid, indexRecord);
					if (rc)
						return rc;

					char* indexData;
					if (rc = indexRecord.GetData(indexData))
						return rc;

					// Check rest of conditions
					bool insert = true;
					for (int k = 1; insert && k < numConditions; ++k){
						if (!swap)
							insert = CheckJoinCondition(fileData, indexData, conditions[k], attrcats, otherAttrcats);
						else
							insert = CheckJoinCondition(indexData, fileData, conditions[k], attrcats, otherAttrcats);
					}
					if (insert){
						if (!swap)
							rc = WriteToOutput(child, otherChild, numOutAttrs, outAttrs, attrcats, otherAttrcats, fileRecord, indexRecord, outPData, outFile);
						else
							rc = WriteToOutput(child, otherChild, numOutAttrs, outAttrs, attrcats, otherAttrcats, indexRecord, fileRecord, outPData, outFile);
						if (rc)
							return rc;
					}
				}
				if (rc != RM_EOF)
					return rc;
				if (rc = indexScan.CloseScan())
				return rc;

				op = GT_OP;
			}
		}
		if (rc != RM_EOF)
			return rc;
		if (rc = fileScan.CloseScan())
			return rc;
	}
	// Index scan of both attributes (must be A join B)
	else if (execution == INDEXES) {
		// TODO
	}

	delete [] outPData;

	// Close filescans and files
	if (rc = rmm->CloseFile(otherFile))
		return rc;
	if (rc = rmm->CloseFile(file))
		return rc;
	if (rc = rmm->CloseFile(outFile))
		return rc;

	if (rc = DeleteTmpInput())
		return rc;
}
// Assumes condition not yet applied
bool JoinConditionApplies(Condition &cond, set<string> &myRelations){
	// Selection condition
	if (!isJoinCondition(cond)){
		return SelectionConditionApplies(cond, myRelations);
	}
	// Check both attributes included in relations
	if (myRelations.find(cond.lhsAttr.relName) == myRelations.end() ||
		myRelations.find(cond.rhsAttr.relName) == myRelations.end()){
		return false;
	}
	// Join condition
	return true;
}
Attrcat GetAttrcat(RelAttr relAttr, map<pair<string, string>, Attrcat> &attrcats, map<pair<string, string>, Attrcat> &otherAttrcats){
	pair<string, string> key(relAttr.relName, relAttr.attrName);
	if (attrcats.find(key) != attrcats.end())
		return attrcats[key];
	return otherAttrcats[key];
}


Cross::Cross(SM_Manager *smm, RM_Manager *rmm, IX_Manager *ixm, Node &left, Node &right, bool calcProj, int numTotalPairs, RelAttrCount *pTotals){
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

	// NO projection needed
	// Project(calcProj, numTotalPairs, pTotals);
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

	// Make outPData
	int len = outAttrs[numOutAttrs-1].offset + outAttrs[numOutAttrs-1].attrLen;
	char* outPData = new char[len];
	memset(outPData, '\0', len);

	// Make attribute-attrcat maps
	map<pair<string, string>, Attrcat> attrcats;
	for (int i = 0; i < child->numOutAttrs; ++i){
		pair<string, string> key(child->outAttrs[i].relName, child->outAttrs[i].attrName);
		attrcats[key] = child->outAttrs[i];
	}
	map<pair<string, string>, Attrcat> otherAttrcats;
	for (int i = 0; i < otherChild->numOutAttrs; ++i){
		pair<string, string> key(otherChild->outAttrs[i].relName, otherChild->outAttrs[i].attrName);
		attrcats[key] = otherChild->outAttrs[i];
	}

	// Iterate over files
	RM_Record record;
	while(OK_RC == (rc = scan.GetNextRec(record))){
		RM_Record otherRecord;
		while (OK_RC == (rc = otherScan.GetNextRec(otherRecord))){
			if (rc = WriteToOutput(child, otherChild, numOutAttrs, outAttrs, attrcats, otherAttrcats, record, otherRecord, outPData, outFile))
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


Relation::Relation(SM_Manager *smm, const char *relName, bool calcProj, int numTotalPairs, RelAttrCount *pTotals){
	// numConditions/conditions set by default

	this->smm = smm;
	// rmm and ixm set by default
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

	// NO projection since no execution function to generate new output
}