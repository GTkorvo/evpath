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

static char *EVPath_version = "EVPath Version 3.0.55 rev. 7487  -- 2009-08-17 16:13:36 -0400 (Mon, 17 Aug 2009)))))\n";

void EVprint_version(){
    printf("%s",EVPath_version);
}

