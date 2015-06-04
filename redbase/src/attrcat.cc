#include <string>
#include <cstring>
#include <stdio.h>
#include "redbase.h"

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

Attrcat::Attrcat(const char* relName, char* attrName, int offset, AttrType attrType, int attrLen, int indexNo){
	memset(this, '\0', sizeof(Attrcat));
	int len = (strlen(relName) > MAXNAME) ? MAXNAME : strlen(relName);
	memcpy(this->relName, relName, len);
	len = (strlen(attrName) > MAXNAME) ? MAXNAME : strlen(attrName);
	memcpy(this->attrName, attrName, len);
	this->offset = offset;
	this->attrType = attrType;
	this->attrLen = attrLen;
	this->indexNo = indexNo;
}

Attrcat& Attrcat::operator=(char* pData){
	memcpy(this, pData, sizeof(Attrcat));
	return *this;
}