#include <string>
#include <cstring>
#include <stdio.h>
#include "redbase.h"

using namespace std;

const char* MYATTRCAT = "attrcat";

Attrcat::Attrcat(){
	memset(this, '\0', sizeof(Attrcat));
	offset = -1;
	attrType = INT;
	attrLen = 0;
	indexNo = -1;
}

Attrcat::Attrcat(char* pData){
	memcpy(this, pData, sizeof(Attrcat));
}

Attrcat::Attrcat(const char* relName, const char* attrName, int offset, AttrType attrType, int attrLen, int indexNo){
	memset(this, '\0', sizeof(Attrcat));
	strcpy(this->relName, relName);
	strcpy(this->attrName, attrName);
	this->offset = offset;
	this->attrType = attrType;
	this->attrLen = attrLen;
	this->indexNo = indexNo;
}

Attrcat& Attrcat::operator=(char* pData){
	memcpy(this, pData, sizeof(Attrcat));
	return *this;
}

Attrcat& Attrcat::operator=(const Attrcat& other){
	memcpy(this, &other, sizeof(Attrcat));
	return *this;
}