#include "config.h"
#include "ffs.h"
#include "gen_thread.h"
#include "atl.h"
#include "evpath.h"
#include "cm_internal.h"
#include "config.h"

#ifndef MODULE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#else
#include "kernel/kcm.h"
#include "kernel/library.h"
#endif
#include "assert.h"

extern void
IntCManager_lock(CManager cm, char *file, int line)
{
    CMtrace_out(cm, CMLowLevelVerbose, "CManager Lock at \"%s\" line %d\n",
		file, line);
    if (gen_thr_initialized()) {
	if (cm->exchange_lock == NULL) {
	    cm->exchange_lock = thr_mutex_alloc();
	}
	thr_mutex_lock(cm->exchange_lock);
    }
    cm->locked++;
    if (cm->locked != 1) {
	printf("CManager lock inconsistency, %d\n", cm->locked);
    }
}

extern void
IntCManager_unlock(CManager cm, char *file, int line)
{
    CMtrace_out(cm, CMLowLevelVerbose, "CManager Unlock at \"%s\" line %d\n",
		file, line);
    cm->locked--;
    if (cm->locked != 0) {
	printf("CManager unlock inconsistency, %d\n", cm->locked);
    }
    if (gen_thr_initialized() && cm->exchange_lock) {
	thr_mutex_unlock(cm->exchange_lock);
    }
}

extern int
CManager_locked(CManager cm)
{
    return cm->locked;
}


