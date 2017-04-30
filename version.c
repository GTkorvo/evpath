
#include <stdio.h>
#include "config.h"

static char *EVPath_version = "EVPath Version 4.0.156 rev. 26764  -- 2017-04-29 08:24:17 -0400 (Sat, 29 Apr 2017)\n";

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

