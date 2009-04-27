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

static char *EVPath_version = "EVPath Version 3.0.51 rev. 7312  -- 2009-04-24 11:43:07 -0400 (Fri, 24 Apr 2009)\n";

void EVprint_version(){
    printf("%s",EVPath_version);
}

