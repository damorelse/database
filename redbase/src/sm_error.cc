#include <cerrno>
#include <cstdio>
#include <iostream>
#include "sm.h"

using namespace std;

static char *SM_WarnMsg[] = {
  (char*)"invalid null parameter",
  (char*)"number of attributes is invalid (should be 0< <41)",
  (char*)"name length is invalid (should be 0< <5)",
  (char*)"invalid enumeration",
  (char*)"invalid name given",
  (char*)"relation or index already exists",
  (char*)"relation or index does not exist",
  (char*)"file did not successfully open",
  (char*)"invalid action upon a catalog"
};

static char *SM_ErrorMsg[] = {
	(char*)"change directory error",
	(char*)"invalid attribute length",
	(char*)"invalid ascii file format (includes string attributes that are too long)",
};

void SM_PrintError(RC rc)
{
    //cout << "SM_PrintError\n   rc=" << rc << "\n";

	// Check return code is within warn limits
	if (rc >= START_SM_WARN && rc <= SM_LASTWARN)
		cerr << "SM warning: " << SM_WarnMsg[rc - START_SM_WARN] << "\n";
	// Check return code is within error limits
	else if (-rc >= -START_SM_ERR && -rc <= -SM_LASTERROR)
		cerr << "SM error: " << SM_ErrorMsg[-rc + START_SM_ERR] << "\n";
	else if (rc == OK_RC)
		cerr << "SM_PrintError called with return code of 0\n";
	else
		cerr << "SM error: " << rc << " is out of bounds\n";
}