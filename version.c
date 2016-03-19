#if defined(FUNCPROTO) || defined(__STDC__) || defined(__cplusplus) || defined(c_plusplus)
#ifndef ARGS
#define ARGS(args) args
#endif
#else
#ifndef ARGS
#define ARGS(args) (/*args*/)
#endif
#endif

#include <stdio.h>
#include "config.h"

static char *EVPath_version = "EVPath Version 4.0.131 rev. 24682  -- 2016-03-18 22:43:59 -0400 (Fri, 18 Mar 2016)\n";

#if defined (__INTEL_COMPILER)
//  Allow extern declarations with no prior decl
#  pragma warning (disable: 1418)
#endif
void EVprint_version()
{
    printf("%s",EVPath_version);
}
void EVfprint_version(FILE*out)
{
    fprintf(out, "%s",EVPath_version);
}

