#include "ix.h"

using namespace std;

IX_IndexScan::IX_IndexScan(): open(false), ixIndexHandle(NULL)
{}
IX_IndexScan::~IX_IndexScan()
{
	ixIndexHandle = NULL;
}

// Open index scan
RC IX_IndexScan::OpenScan(const IX_IndexHandle &indexHandle,
						  CompOp compOp,
						  void *value,
						  ClientHint  pinHint = NO_HINT)
{
	// Check if filescan already open
	if (open){
		PrintError(IX_FILESCANREOPEN);
		return IX_FILESCANREOPEN;
	}

	// Check input parameters
	// Check compare operation is one of the seven allowed
	if (compOp != NO_OP && compOp != EQ_OP && compOp !=NE_OP && 
		compOp !=LT_OP && compOp !=GT_OP && compOp !=LE_OP && 
		compOp !=GE_OP){
		PrintError(IX_INVALIDENUM);
		return IX_INVALIDENUM;
	}

	// Check if value is null
	if (!value){
		PrintError(IX_NULLINPUT);
		return IX_NULLINPUT;
	}
	// End check input parameters

	// Copy over scan params
	ixIndexHandle = &indexHandle;
	this->compOp = compOp;
	this->value = value;

	// Set state
	open = true;
	// TODO: store more state

	return OK_RC;
}

// Get the next matching entry return IX_EOF if no more matching entries.
RC IX_IndexScan::GetNextEntry(RID &rid)
{
	if (ixIndexHandle->ixIndexHeader.rootPage == IX_NO_PAGE){
		PrintError(IX_EOF);
		return IX_EOF;
	}

	// TODO: search B+ tree
	PageNum leafPage = ixIndexHandle->FindLeafNode(value);
}

// Close index scan
RC IX_IndexScan::CloseScan()
{
	// Set state
	open = false;
	ixIndexHandle = NULL;

	return OK_RC;
}

PageNum IX_IndexScan::FindMinLeafNode() const
{
	return ixIndexHandle->FindLeafNodeHelper(ixIndexHandle->ixIndexHeader.rootPage, ixIndexHandle->ixIndexHeader.height, true, NULL);
}
