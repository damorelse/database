#include <cstdio>
#include <iostream>
#include "rm.h"

using namespace std;

RM_Record::RM_Record (): recordCopy(NULL), rid(RID()), length(0){}

RM_Record::~RM_Record()
{
	if (recordCopy)
		delete recordCopy;
}

RM_Record::RM_Record (const RM_Record &other): recordCopy(NULL), rid(RID()), length(0)
{
	*this = other;
}

RM_Record& RM_Record::operator=  (const RM_Record &other)
{
	if (this != &other){
		delete recordCopy;
		recordCopy = NULL;
		rid = other.rid;
		length = other.length;

		if (other.recordCopy){
			recordCopy = new char[length];
			memcpy(recordCopy, other.recordCopy, length);
		}
	}
	return *this;
}

// Return the data corresponding to the record.  Sets *pData to the record contents.
RC RM_Record::GetData(char *&pData) const
{
	if(!recordCopy){
		PrintError(RM_RECORDNOTREAD);
		return RM_RECORDNOTREAD;
	}
	pData = recordCopy;
	return OK_RC;
}

// Return the RID associated with the record
RC RM_Record::GetRid (RID &rid) const
{
	if(!recordCopy){
		PrintError(RM_RECORDNOTREAD);
		return RM_RECORDNOTREAD;
	}
	rid = this->rid;
	return OK_RC;
}