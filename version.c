
#include <stdio.h>
#include "config.h"

static char *EVPath_version = "EVPath Version 4.0.164 rev. 26874  -- 2017-05-21 20:18:22 -0400 (Sun, 21 May 2017)\n";

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

