
#include <stdio.h>
#include "config.h"

static char *EVPath_version = "EVPath Version 4.0.161 rev. 26828  -- 2017-05-09 11:06:03 -0400 (Tue, 09 May 2017)\n";

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

