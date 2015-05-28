#include <cerrno>
#include <cstdio>
#include <iostream>
#include "ql.h"

using namespace std;

// TODO

static char *QL_WarnMsg[] = {
  (char*)"",
};

static char *QL_ErrorMsg[] = {
	(char*)"", 
};

void QL_PrintError(RC rc)
{
	// Check return code is within warn limits
	if (rc >= START_QL_WARN && rc <= QL_LASTWARN)
		cerr << "QL warning: " << QL_WarnMsg[rc - START_QL_WARN] << "\n";
	// Check return code is within error limits
	else if (-rc >= -START_QL_ERR && -rc <= -QL_LASTERROR)
		cerr << "QL error: " << QL_ErrorMsg[-rc + START_QL_ERR] << "\n";
	else if (rc == OK_RC)
		cerr << "QL_PrintError called with return code of 0\n";
	else
		cerr << "QL error: " << rc << " is out of bounds\n";
}