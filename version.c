
#include <stdio.h>
#include "config.h"

static char *EVPath_version = "EVPath Version 4.0.159 rev. 26801  -- 2017-05-03 20:03:31 -0400 (Wed, 03 May 2017)\n";

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

