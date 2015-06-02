#include <vector>
#include <set>
#include <map>
#include <iterator>
#include <fstream>
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
	memset(input, '\0', MAXNAME+1);
	memset(otherInput, '\0', MAXNAME+1);
	memset(output, '\0', MAXNAME+1);

	numRelations = 0;
	relations = NULL;

	numCountPairs = 0;
	pCounts = NULL;
	numOutAttrs = 0;
	outAttrs = NULL;
	project = false;

	rc = 0;
}
Node::~Node(){
	delete [] conditions;
	conditions = NULL;
	delete [] relations;
	relations = NULL;
	delete [] pCounts;
	pCounts = NULL;
	delete [] outAttrs;
	outAttrs = NULL;
}
RC Node::execute(){/*nothing; should set output*/}
bool ConditionApplies(Condition &cond){
	return false;
}
void Node::Project(bool calcProj, int numTotalPairs, pair<RelAttr, int> *pTotals){
		// Projection Start
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
		int offset = 0;
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
	// Projection End
}
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


Selection::Selection(SM_Manager *smm, Node& left, int numConds, Condition *conds, bool calcProj, int numTotalPairs, pair<RelAttr, int> *pTotals){
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
	strcpy(type, "Select");
	child = &left;
	// otherChild set by default
	// parent will be set by parent
	strcpy(input, left.output);
	// otherInput set by default
	// output will be set by execute

	numRelations = left.numRelations;
	// relations
	relations = new char[numRelations * (MAXNAME+1)];
	memcpy(relations, left.relations, left.numRelations * (MAXNAME+1));

	// numOutAttrs
	numOutAttrs = left.numOutAttrs;
	// outAttrs
	outAttrs = new Attrcat[numOutAttrs];
	memcpy(outAttrs, left.outAttrs, left.numOutAttrs * sizeof(Attrcat));

	Project(calcProj, numTotalPairs, pTotals);
}
RC Selection::execute(){
	// TODO
}
bool Selection::ConditionApplies(Condition &cond){
	if (strcmp(relations, cond.lhsAttr.relName) != 0)
		return false;
	if (cond.bRhsIsAttr && 
		strcmp(relations, cond.rhsAttr.relName) != 0)
		return false;
	return true;
}

Join::Join(SM_Manager *smm, Node& left, Node& right, int numConds, Condition *conds, bool calcProj, int numTotalPairs, pair<RelAttr, int> *pTotals){
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
	strcpy(type, "Join");
	child = &left;
	otherChild = &right;
	// parent will be set by parent
	strcpy(input, left.output);
	strcpy(otherInput, right.output);
	// output will be set by execute

	numRelations = left.numRelations + right.numRelations;
	// relations
	relations = new char[numRelations * (MAXNAME+1)];
	memcpy(relations, left.relations, left.numRelations * (MAXNAME+1));
	memcpy(relations + left.numRelations * (MAXNAME+1), right.relations, right.numRelations * (MAXNAME+1));

	// numOutAttrs
	numOutAttrs = left.numOutAttrs + right.numOutAttrs;
	// outAttrs
	outAttrs = new Attrcat[numOutAttrs];
	memcpy(outAttrs, left.outAttrs, left.numOutAttrs * sizeof(Attrcat));
	memcpy(outAttrs + left.numOutAttrs * sizeof(Attrcat), right.outAttrs, right.numOutAttrs * sizeof(Attrcat));

	Project(calcProj, numTotalPairs, pTotals);
}
RC Join::execute(){
	// TODO
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

Cross::Cross(SM_Manager *smm, Node &left, Node &right, bool calcProj, int numTotalPairs, pair<RelAttr, int> *pTotals){
	// set parent for both children
	left.parent = this;
	right.parent = this;

	// numConditions and conditions default set

	this->smm = smm;
	strcpy(type, "X");
	child = &left;
	otherChild = &right;
	// parent will be set by parent
	strcpy(input, left.output);
	strcpy(otherInput, right.output);
	// output will be set by execute

	numRelations = left.numRelations + right.numRelations;
	// relations
	relations = new char[numRelations * (MAXNAME+1)];
	memcpy(relations, left.relations, left.numRelations * (MAXNAME+1));
	memcpy(relations + left.numRelations * (MAXNAME+1), right.relations, right.numRelations * (MAXNAME+1));

	// numOutAttrs
	numOutAttrs = left.numOutAttrs + right.numOutAttrs;
	// outAttrs
	outAttrs = new Attrcat[numOutAttrs];
	memcpy(outAttrs, left.outAttrs, left.numOutAttrs * sizeof(Attrcat));
	memcpy(outAttrs + left.numOutAttrs * sizeof(Attrcat), right.outAttrs, right.numOutAttrs * sizeof(Attrcat));

	Project(calcProj, numTotalPairs, pTotals);
}
RC Cross::execute(){
	// Set output
	char* fileName = tmpnam(NULL);
	strcpy(output, fileName);
	// Create relation
	vector<AttrInfo> attributes;
	// TODO: add in RID attributes
	for (int i = 0; i < numOutAttrs; ++i)
		attributes.push_back(AttrInfo(outAttrs[i]));
	if (rc = smm->CreateTable(output, numOutAttrs, &attributes[0]))
		return rc;

	// Perform filescans, write results to temp file
	RM_FileHandle file;
	RM_FileHandle otherFile;

	// Delete input files
	remove(input);
	remove(otherInput);
}

Relation::Relation(SM_Manager *smm, const char *relName, bool calcProj, int numTotalPairs, pair<RelAttr, int> *pTotals){
	// numConditions/conditions set by default

	this->smm = smm;
	strcpy(type, relName);
	// child/otherChild set by default
	// parent set by parent node
	// input/otherInput set by default
	strcpy(output, relName);

	numRelations = 1;
	relations = new char[numRelations * (MAXNAME+1)];
	strcpy(relations, relName);

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