
#include <stdio.h>
#include "config.h"

static char *EVPath_version = "EVPath Version 4.0.157 rev. 26775  -- 2017-04-30 08:36:03 -0400 (Sun, 30 Apr 2017)\n";

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

