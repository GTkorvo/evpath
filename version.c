
#include <stdio.h>
#include "config.h"

static char *EVPath_version = "EVPath Version 4.1.1 rev. 26924  -- 2017-06-03 22:56:34 -0400 (Sat, 03 Jun 2017)\n";

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

