#include <string>
#include <cstring>
#include <stdio.h>
#include "redbase.h"

const char* MYRELCAT = "relcat";

Relcat::Relcat(char* pData){
		memcpy(this, pData, sizeof(Relcat));
	}

Relcat::Relcat(const char* relName, int tupleLen, int attrCount, int indexCount){
	memset(this, 0, sizeof(Relcat));
	int len = (strlen(relName) > MAXNAME) ? MAXNAME : strlen(relName);
	memcpy(this->relName, relName, len+1);
	this->tupleLen = tupleLen;
	this->attrCount = attrCount;
	this->indexCount = indexCount;
}
