
#include <stdio.h>
#include "config.h"

static char *EVPath_version = "EVPath Version 4.0.163 rev. 26836  -- 2017-05-15 10:04:22 -0400 (Mon, 15 May 2017)\n";

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

