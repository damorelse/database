#include <string>
#include "redbase.h"

Relcat::Relcat(char* pData){
		memcpy(this, pData, sizeof(Relcat));
	}

Relcat::Relcat(const char* relName, int tupleLen, int attrCount, int indexCount){
	int len = (strlen(relName) > MAXNAME) ? MAXNAME : strlen(relName);
	memcpy(this->relName, relName, len+1);
	this->tupleLen = tupleLen;
	this->attrCount = attrCount;
	this->indexCount = indexCount;
}