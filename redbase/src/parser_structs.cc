#include <string>
#include <cstring>
#include <cerrno>
#include <iostream>
#include "parser.h"

using namespace std;

AttrInfo::AttrInfo(){
    del = false;
	attrName = NULL;
	attrType = INT;
	attrLength = 4;
}
AttrInfo::AttrInfo(const AttrInfo& other){
	attrName = new char[MAXNAME+1];
	memset(attrName, '\0', MAXNAME+1);
    del = true;	
	strcpy(attrName, other.attrName);
	attrType = other.attrType;
	attrLength = other.attrLength;
}
AttrInfo::~AttrInfo(){
	if (del && attrName)
		delete [] attrName;
	attrName = NULL;
    del = false;
}
AttrInfo& AttrInfo::operator=(const AttrInfo& other){
	if (this != &other){
        if (!del){
			attrName = new char[MAXNAME+1];
			memset(attrName, '\0', MAXNAME+1);
            del = true;
		}	
		strcpy(attrName, other.attrName);
		attrType = other.attrType;
		attrLength = other.attrLength;
	}
	return *this;
}
AttrInfo::AttrInfo(Attrcat attrcat){
	del = true;
	attrName = new char[MAXNAME+1];
	memset(attrName, '\0', MAXNAME+1);
	strcpy(attrName, attrcat.attrName);

	attrType = attrcat.attrType;
	attrLength = attrcat.attrLen;
}


RelAttr::RelAttr(){
	relName = NULL;
	attrName = NULL;
	delRel = false;
	delAttr = false;
}
RelAttr::RelAttr(const char* rel, const char* attr){
	relName = new char[MAXNAME+1];
	memset(relName, '\0', MAXNAME+1);
	if (rel)
		strcpy(relName, rel);

	attrName = new char[MAXNAME+1];
	memset(attrName, '\0', MAXNAME+1);
	if (attr)
		strcpy(attrName, attr);

	delRel = true;
	delAttr = true;
}
RelAttr::RelAttr(const RelAttr &other){
	relName = new char[MAXNAME+1];
	memset(relName, '\0', MAXNAME+1);
	if (other.relName)
		strcpy(relName, other.relName);

	attrName = new char[MAXNAME+1];
	memset(attrName, '\0', MAXNAME+1);
	if (other.attrName)
		strcpy(attrName, other.attrName);

	delRel = true;
	delAttr = true;
}
RelAttr::~RelAttr(){
	if (relName && delRel)
		delete [] relName;
	relName = NULL;
	if (attrName && delAttr)
		delete [] attrName;
	attrName = NULL;
}
RelAttr& RelAttr::operator=(const RelAttr &other){
	if (this != &other){
		if (!relName){
			relName = new char[MAXNAME+1];
			memset(relName, '\0', MAXNAME+1);
			delRel = true;
		}
		strcpy(relName, other.relName);
		if (!attrName){
			attrName = new char[MAXNAME+1];
			memset(attrName, '\0', MAXNAME+1);
			delAttr = true;
		}
		strcpy(attrName, other.attrName);
	}
	return *this;
}
bool RelAttr::operator==(const RelAttr &other) const{
	return strcmp(relName, other.relName) && strcmp(attrName, other.attrName);
}
bool RelAttr::operator<(const RelAttr &other) const{
	int diff = strcmp(relName, other.relName);
	if (diff == 0)
		return (strcmp(attrName, other.attrName) < 0);
	return (diff < 0);
}


Value::Value(){
	type = INT;
	data = NULL;
	del = false;
}
Value::Value(const Value &other): type(other.type), data(NULL), del(false){
	if (other.data){
		if (type != STRING){
			data = new char[4];
			memcpy(data, other.data, 4);
		} 
		else {
			//int size = strlen((char*)other.data)+1;
			cerr << sizeof((char*)other.data) << endl;
			data = new char[sizeof((char*)other.data)];
			memcpy((char*)data, (char*)other.data, sizeof((char*)other.data));
		}
		del = true;
	}
}
Value& Value::operator=(const Value &other){
	if (this != &other){
		if (data){
			delete [] (char*)data;
			data = NULL;
		}
		if (other.data){
			if (type != STRING){
				data = new char[4];
				memcpy(data, other.data, 4);
			} 
			else {
				//int size = strlen((char*)other.data)+1;
				cerr << sizeof((char*)other.data) << endl;
				data = new char[sizeof((char*)other.data)];
				memcpy((char*)data, (char*)other.data, sizeof((char*)other.data));
			}
			del = true;
		}
	}
	return *this;
}
Value::~Value(){
	if (data && del)
		delete [] data;
	data = NULL;
}
bool Value::operator==(const Value &other) const{
	if (type != other.type)
		return false;

	int a_i, v_i;
	float a_f, v_f;
	switch(type) {
	case INT:
		memcpy(&a_i, data, 4);
		memcpy(&v_i, other.data, 4);
		return a_i == v_i;
        break;
	case FLOAT:
		memcpy(&a_f, data, 4);
		memcpy(&v_f, other.data, 4);
		return a_f == v_f;
        break;
	case STRING:
		return (0 == strcmp((char*)data, (char*)other.data));
		break;
	}
}
bool Value::operator<(const Value &other) const{
	if (type != other.type)
		return type < other.type;
	int a_i, v_i;
	float a_f, v_f;
	switch(type) {
	case INT:
		memcpy(&a_i, data, 4);
		memcpy(&v_i, other.data, 4);
		return a_i < v_i;
        break;
	case FLOAT:
		memcpy(&a_f, data, 4);
		memcpy(&v_f, other.data, 4);
		return a_f < v_f;
        break;
	case STRING:
		return (strcmp((char*)data, (char*)other.data) < 0);
		break;
	}
}

Condition::Condition(): lhsAttr(RelAttr("", "")), rhsAttr(RelAttr("", "")), rhsValue(Value()){
	op = EQ_OP;
	bRhsIsAttr = true;
}
Condition::Condition(const Condition& other): lhsAttr(other.lhsAttr), rhsAttr(other.rhsAttr), rhsValue(other.rhsValue){
	op = other.op;
	bRhsIsAttr = other.bRhsIsAttr;
}
Condition::Condition(const RelAttr lhsAttr, CompOp op, const int isAttr, 
					 const RelAttr rhsAttr, const Value rhsValue)
					 : lhsAttr(lhsAttr), op(op), bRhsIsAttr(isAttr), rhsAttr(rhsAttr), rhsValue(rhsValue){}
Condition& Condition::operator=(const Condition& other){
	lhsAttr = other.lhsAttr;
	op = other.op;
	bRhsIsAttr = other.bRhsIsAttr;
	rhsAttr = other.rhsAttr;
	rhsValue = other.rhsValue;
}
bool Condition::operator==(const Condition &other) const{
	if (!(lhsAttr == other.lhsAttr) || op != other.op || bRhsIsAttr != other.bRhsIsAttr)
		return false;
	if (bRhsIsAttr)
		return rhsAttr == other.rhsAttr;
	return rhsValue == other.rhsValue;
}
bool Condition::operator<(const Condition &other) const{
	if (!((lhsAttr == other.lhsAttr)))
		return lhsAttr < other.lhsAttr;
	if (op != other.op)
		return op < other.op;
	if (bRhsIsAttr != other.bRhsIsAttr)
		return bRhsIsAttr < other.bRhsIsAttr;
	if (bRhsIsAttr)
		return rhsAttr < other.rhsAttr;
	return rhsValue < other.rhsValue;
}
