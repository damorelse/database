#include "rm_rid.h"

using namespace std;

RID::RID(): pageNum(-1), slotNum(-1){}

RID::RID(PageNum pageNum, SlotNum slotNum): pageNum(pageNum), slotNum(slotNum){}

RID::~RID(){}

RC RID::GetPageNum(PageNum &pageNum) const
{
	// Primitive check if page and slot number have been set
	if (!IsViable()){
		PrintError(RM_INVALIDRID);
		return RM_INVALIDRID;
	}
	pageNum = this->pageNum;
	return OK_RC;
}

RC RID::GetSlotNum(SlotNum &slotNum) const
{
	// Check if page and slot number have been set
	if (!IsViable()){
		PrintError(RM_INVALIDRID);
		return RM_INVALIDRID;
	}
	slotNum = this->slotNum;
	return OK_RC;
}

// Primitive check for page and slot number values
// Will return false if numbers have not been changed since initialization
bool RID::IsViable() const
{
	if (pageNum < 0 || slotNum < 0)
		return false;
	return true;
}