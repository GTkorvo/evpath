
#include <stdio.h>
#include "config.h"

static char *EVPath_version = "EVPath Version 4.2.3 -- Thu Nov  2 09:59:50 EDT 2017\n";

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

