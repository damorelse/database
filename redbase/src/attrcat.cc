#include <string>
#include <cstring>
#include <stdio.h>
#include "redbase.h"

const char* ATTRCAT = "attrcat";

Attrcat::Attrcat(){
	offset = -1;
	attrType = INT;
	attrLen = 0;
	indexNo = -1;
}

Attrcat::Attrcat(char* pData){
	memcpy(this, pData, sizeof(Attrcat));
}

Attrcat::Attrcat(const char* relName, char* attrName, int offset, AttrType attrType, int attrLen, int indexNo){
	int len = (strlen(relName) > MAXNAME) ? MAXNAME : strlen(relName);
	memcpy(this->relName, relName, len+1);
	len = (strlen(attrName) > MAXNAME) ? MAXNAME : strlen(attrName);
	memcpy(this->attrName, attrName, len+1);
	this->offset = offset;
	this->attrType = attrType;
	this->attrLen = attrLen;
	this->indexNo = indexNo;
}
