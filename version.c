#if defined(FUNCPROTO) || defined(__STDC__) || defined(__cplusplus) || defined(c_plusplus)
#ifndef ARGS
#define ARGS(args) args
#endif
#else
#ifndef ARGS
#define ARGS(args) (/*args*/)
#endif
#endif

#include <stdio.h>
#include "config.h"

static char *EVPath_version = "EVPath Version 3.0.57 rev. 7506  -- 2009-08-24 01:23:21 -0400 (Mon, 24 Aug 2009)))))))\n";

void EVprint_version(){
    printf("%s",EVPath_version);
}

