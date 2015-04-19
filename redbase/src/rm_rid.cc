#include "rm_rid.h"

using namespace std;

RID::RID(): pageNum(-1), slotNum(-1){}

RID::RID(PageNum pageNum, SlotNum slotNum): pageNum(pageNum), slotNum(slotNum){}

RID::~RID(){}

//use default copy constructor, and copy assignment operator

RC RID::GetPageNum(PageNum &pageNum) const
{
	if (!IsViable()){
		PrintError(RM_INVALIDRID);
		return RM_INVALIDRID;
	}
	pageNum = this->pageNum;
	return OK_RC;
}

RC RID::GetSlotNum(SlotNum &slotNum) const
{
	if (!IsViable()){
		PrintError(RM_INVALIDRID);
		return RM_INVALIDRID;
	}
	slotNum = this->slotNum;
	return OK_RC;
}

bool RID::IsViable() const
{
	if (pageNum < 0 || slotNum < 0)
		return false;
	return true;
}