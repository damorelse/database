#include <vector>
#include <set>
#include "ql.h"
#include "sm.h"

using namespace std;

Node::Node(){
	memset(type, '\0', MAXNAME+1);

	numRelations = 0;
	relations = NULL;
	numOutAttrs = 0;
	outAttrs = NULL; // If root, must order to reflect select attributes
	numConditions = 0;
	conditions = NULL;

	memset(output, '\0', MAXNAME+1);
	memset(input, '\0', MAXNAME+1);
	memset(input2, '\0', MAXNAME+1);

	parent = NULL;
	child = NULL;
	otherChild = NULL;

	rc = 0;
	project = false;
}
Node::~Node(){
	delete [] relations;
	delete [] outAttrs;
	delete [] conditions;
}
RC Node::execute(){/*nothing; should set output and reduce outAttrs*/}
bool ConditionApplies(Condition &cond){
	return false;
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
RC Node::Project(char* inData, char* &outData){
	// TODO
}


Selection::Selection(Node& left, int numConds, Condition *conds, bool calcProj){
	// set parent for children
	left.parent = this;

	strcpy(type, "Select");
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

	numRelations = left.numRelations;
	// relations
	relations = new char[numRelations * (MAXNAME+1)];
	memcpy(relations, left.relations, left.numRelations * (MAXNAME+1));
	// numOutAttrs
	numOutAttrs = left.numOutAttrs;
	// outAttrs
	outAttrs = new Attrcat[numOutAttrs];
	memcpy(outAttrs, left.outAttrs, left.numOutAttrs * sizeof(Attrcat));

	// output will be set by execute
	strcpy(input, left.output);
	// input2 set by default

	// parent will be set by parent
	child = &left;
	// otherChild set by default
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

Join::Join(Node& left, Node& right, int numConds, Condition *conds, bool calcProj){
	// set parent for both children
	left.parent = this;
	right.parent = this;

	strcpy(type, "Join");
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

	// output will be set by execute
	strcpy(input, left.output);
	strcpy(input2, right.output);

	// parent will be set by parent
	child = &left;
	otherChild = &right;
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

Cross::Cross(Node &left, Node &right, bool calcProj){
	// set parent for both children
	left.parent = this;
	right.parent = this;

	strcpy(type, "X");
	// numConditions and conditions default set

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

	// output will be set by execute
	strcpy(input, left.output);
	strcpy(input2, right.output);

	// parent will be set by parent
	child = &left;
	otherChild = &right;
}
RC Cross::execute(){
	// TODO
}

Relation::Relation(const char *relName, SM_Manager *smm, bool calcProj){
	this->smm = smm;

	strcpy(type, relName);
	// numConditions/conditions set by default

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

	strcpy(output, relName);
	// input/input2 set by default

	// parent will be set by parent
	// child/otherchild set by default
}

// TODO: calculate projections too, set Attrcat offset correctly