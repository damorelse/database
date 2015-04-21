#include <cerrno>
#include <cstdio>
#include <iostream>
#include "rm.h"

using namespace std;

// Error tables
static char *RM_WarnMsg[] = {
  (char*)"rid was not set",
  (char*)"record not yet read",
  (char*)"file not yet opened",
  (char*)"record does not exist",
  (char*)"reached end of file",
  (char*)"scan not yet opened",
};

static char *RM_ErrorMsg[] = {
  (char*)"record size invalid; either too big for page or less-than-or-equal-to zero",
  (char*)"filename size invalid; either exceeds name size or empty",
  (char*)"input pointer parameter is null",
  (char*)"string length invalid, either empty or exceeds max string length",
  (char*)"record index access invalid, either negative or will exceed record size",
  (char*)"invalid input given, not one of the listed enumerations",
  (char*)"invalid length for given attribute type; should be 4 for ints and floats",
};

void RM_PrintError(RC rc)
{
	// Check return code is within warn limits
	if (rc >= START_RM_WARN && rc <= RM_LASTWARN)
		cerr << "RM warning: " << RM_WarnMsg[rc - START_RM_WARN] << "\n";
	// Check return code is within error limits
	else if (-rc >= -START_RM_ERR && -rc <= -RM_LASTERROR)
		cerr << "RM error: " << RM_ErrorMsg[-rc + START_RM_ERR] << "\n";
	else if (rc == OK_RC)
		cerr << "RM_PrintError called with return code of 0\n";
	else
		cerr << "RM error: " << rc << " is out of bounds\n";
}