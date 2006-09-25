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

static char *CM_version = "CM Version 2.0.320 -- Mon Sep 25 12:42:28 EDT 2006\n";

void CMprint_version(){
    printf("%s",CM_version);
}

