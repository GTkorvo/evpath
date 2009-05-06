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

static char *EVPath_version = "EVPath Version 3.0.53 rev. 7369  -- 2009-05-05 15:56:52 -0400 (Tue, 05 May 2009)))\n";

void EVprint_version(){
    printf("%s",EVPath_version);
}

