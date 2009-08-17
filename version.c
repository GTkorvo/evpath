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

static char *EVPath_version = "EVPath Version 3.0.54 rev. 7483  -- 2009-08-16 08:55:54 -0400 (Sun, 16 Aug 2009))))\n";

void EVprint_version(){
    printf("%s",EVPath_version);
}

