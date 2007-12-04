// getopt.h  Version 1.2
//
// Author:  Hans Dietrich
//          hdietrich2@hotmail.com
//
// This software is released into the public domain.
// You are free to use it in any way you like.
//
// This software is provided "as is" with no expressed
// or implied warranty.  I accept no liability for any
// damage or loss of business that this software may cause.
//
///////////////////////////////////////////////////////////////////////////////

#ifndef GETOPT_H
#define GETOPT_H

extern int optind, opterr;
extern const char *optarg;

int getopt(int argc, const char *argv[], const char *optstring);

#endif //GETOPT_H
