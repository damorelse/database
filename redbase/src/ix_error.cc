#include <cerrno>
#include <cstdio>
#include <iostream>
#include "ix.h"

using namespace std;

// Error tables
static char *IX_WarnMsg[] = {
  (char*)"file not yet opened",
  (char*)"entry does not exist, cannot delete",
  (char*)"file scan already opened, do not re-open",
  (char*)"end of file",
  (char*)"trying to close before scan finished",
};

static char *IX_ErrorMsg[] = {
	(char*)"invalid type given for enumeration",
	(char*)"invalid null input",
	(char*)"invalid string length, should be 0 < < 255",
	(char*)"invalid number length, should be 4",
	(char*)"invalid file name length",
	(char*)"invalid scan parameters; either value null and compOp not NO_OP, or value not null and compOp NO_OP",
	(char*)"invalid index number",
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