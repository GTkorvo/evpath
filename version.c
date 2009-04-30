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

static char *EVPath_version = "EVPath Version 3.0.52 rev. 7322  -- 2009-04-29 17:19:49 -0400 (Wed, 29 Apr 2009))\n";

void EVprint_version(){
    printf("%s",EVPath_version);
}

