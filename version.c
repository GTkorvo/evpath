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

static char *EVPath_version = "EVPath Version 3.0.56 rev. 7503  -- 2009-08-21 06:29:14 -0400 (Fri, 21 Aug 2009))))))\n";

void EVprint_version(){
    printf("%s",EVPath_version);
}

