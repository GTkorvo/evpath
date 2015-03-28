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

static char *EVPath_version = "EVPath Version 4.0.84 rev. 20868  -- 2015-03-27 16:23:15 -0400 (Fri, 27 Mar 2015)\n";

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

