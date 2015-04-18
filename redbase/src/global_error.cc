#include <cerrno>
#include <cstdio>
#include <iostream>
#include <cstdlib>
#include "redbase.h"
#include "pf.h"
#include "rm.h"

using namespace std;

void PrintError(RC rc)
{
	RC posRc = abs(rc);

	if (posRc == OK_RC)
		cerr << "PrintError called with return code of 0\n";
	else if (posRc >= START_PF_WARN && posRc <= END_PF_WARN)
		PF_PrintError(rc);
	else if (posRc >= START_RM_WARN && posRc <= END_RM_WARN)
		RM_PrintError(rc);
	else if (posRc >= START_IX_WARN && posRc <= END_IX_WARN)
		;// TODO: replace
	else if (posRc >= START_SM_WARN && posRc <= END_SM_WARN)
		;// TODO: replace
	else if (posRc >= START_QL_WARN && posRc <= END_QL_WARN)
		;// TODO: replace
	else
		cerr << "Global PrintError error: " << rc << " is out of bounds\n";
}