
#include <stdio.h>
#include "config.h"

static char *EVPath_version = "EVPath Version 4.2.0 -- Sat Jun 24 19:14:32 EDT 2017\n";

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

