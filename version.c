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

static char *CM_version = "CM Version 2.0.313 -- Mon Jul 31 14:16:27 EDT 2006\n";

void CMprint_version(){
    printf("%s",CM_version);
}

