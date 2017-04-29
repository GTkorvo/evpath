
#include <stdio.h>
#include "config.h"

static char *EVPath_version = "EVPath Version 4.0.155 rev. 26757  -- 2017-04-28 12:32:11 -0400 (Fri, 28 Apr 2017)\n";

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

