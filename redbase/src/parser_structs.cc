#include "parser.h"

AttrInfo::AttrInfo(){
	memset(attrName, '\0', MAXNAME+1);
	attrType = INT;
	attrLength = 4;
}
AttrInfo::AttrInfo(Attrcat attrcat){
	memset(attrName, '\0', MAXNAME+1);
	strcpy(attrName, attrcat.attrName);

	attrType = attrcat.attrType;
	attrLength = attrcat.attrLen;
}


RelAttr::RelAttr(char* rel, char* attr){
	relName = new char[MAXNAME+1];
	memset(relName, '\0', MAXNAME+1);
	strcpy(relName, rel);

	attrName = new char[MAXNAME+1];
	memset(attrName, '\0', MAXNAME+1);
	strcpy(attrName, attr);
}
RelAttr::RelAttr(const RelAttr &other){
	relName = new char[MAXNAME+1];
	memset(relName, '\0', MAXNAME+1);
	strcpy(relName, other.relName);

	attrName = new char[MAXNAME+1];
	memset(attrName, '\0', MAXNAME+1);
	strcpy(attrName, other.attrName);
}
RelAttr::~RelAttr(){
	delete [] relName;
	relName = NULL;
	delete [] attrName;
	attrName = NULL;
}
RelAttr& RelAttr::operator=(const RelAttr &other){
	if (this != &other){
		if (!relName){
			relName = new char[MAXNAME+1];
			memset(relName, '\0', MAXNAME+1);
		}
		strcpy(relName, other.relName);

		if (!attrName){
			attrName = new char[MAXNAME+1];
			memset(attrName, '\0', MAXNAME+1);
		}
		strcpy(attrName, other.attrName);
	}
	return *this;
}

Value::Value(const Value &other){
	type = other.type;
	if (type != STRING){
		data = new char[4];
		memcpy(data, other.data, 4);
	} 
	else {
		int size = strlen((char*)other.data)+1;
		data = new char[size];
		strcpy((char*)data, (char*)other.data);
	}

}
Value::~Value(){
	delete [] data;
	data = NULL;
}

Condition::Condition(const RelAttr lhsAttr, CompOp op, const int isAttr, 
					 const RelAttr rhsAttr, const Value rhsValue)
					 : lhsAttr(lhsAttr), op(op), bRhsIsAttr(isAttr), rhsAttr(rhsAttr), rhsValue(rhsValue)
{}