#include <cerrno>
#include <cstdio>
#include <iostream>
#include "ix.h"

using namespace std;

// Error tables
static char *IX_WarnMsg[] = {
  (char*)"",
};

static char *IX_ErrorMsg[] = {
	(char*)"",
};

void IX_PrintError(RC rc)
{
	// Check return code is within warn limits
	if (rc >= START_IX_WARN && rc <= IX_LASTWARN)
		cerr << "IX warning: " << IX_WarnMsg[rc - START_IX_WARN] << "\n";
	// Check return code is within error limits
	else if (-rc >= -START_IX_ERR && -rc <= -IX_LASTERROR)
		cerr << "IX error: " << IX_ErrorMsg[-rc + START_IX_ERR] << "\n";
	else if (rc == OK_RC)
		cerr << "IX_PrintError called with return code of 0\n";
	else
		cerr << "IX error: " << rc << " is out of bounds\n";
}