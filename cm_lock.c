#include "config.h"
#include "io.h"
#include "gen_thread.h"
#include "atl.h"
#include "evpath.h"
#include "cm_internal.h"
#include "config.h"

#ifndef MODULE
#include <stdio.h>
#include <stdlib.h>
#else
#include "kernel/kcm.h"
#include "kernel/library.h"
#endif
#include "assert.h"

extern void
IntCManager_lock(cm, file, line)
CManager cm;
char *file;
int line;
{
    CMtrace_out(cm, CMLowLevelVerbose, "CManager Lock at \"%s\" line %d",
		file, line);
    if (gen_thr_initialized()) {
	if (cm->exchange_lock == NULL) {
	    cm->exchange_lock = thr_mutex_alloc();
	}
	thr_mutex_lock(cm->exchange_lock);
    }
    cm->locked++;
    if (cm->locked != 1) printf("CManager lock inconsistency, %d\n", cm->locked);
}

extern void
IntCManager_unlock(cm, file, line)
CManager cm;
char *file;
int line;
{
    CMtrace_out(cm, CMLowLevelVerbose, "CManager Unlock at \"%s\" line %d",
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
CManager_locked(cm)
CManager cm;
{
    return cm->locked;
}

extern void
IntCMConn_write_lock(conn, file, line)
CMConnection conn;
char *file;
int line;
{
    CMtrace_out(conn->cm, CMLowLevelVerbose, "CManager Lock at \"%s\" line %d",
		file, line);
    if (gen_thr_initialized()) {
	if (conn->write_lock == NULL) {
	    conn->write_lock = thr_mutex_alloc();
	}
	thr_mutex_lock(conn->write_lock);
    }
}

extern void
IntCMConn_write_unlock(conn, file, line)
CMConnection conn;
char *file;
int line;
{
    CMtrace_out(conn->cm, CMLowLevelVerbose, "CMConn %lx Unlock at \"%s\" line %d",
		conn, file, line);
    if (gen_thr_initialized() && conn->write_lock) {
	thr_mutex_unlock(conn->write_lock);
    }
}

extern void
CMControlList_lock(cl)
CMControlList cl;
{
    if (gen_thr_initialized()) {
	if (cl->list_mutex == NULL) {
	    cl->list_mutex = thr_mutex_alloc();
	}
	thr_mutex_lock(cl->list_mutex);
    }
    cl->locked++;
}

extern void
CMControlList_unlock(cl)
CMControlList cl;
{
    cl->locked--;
    if (cl->list_mutex && gen_thr_initialized()) {
	thr_mutex_unlock(cl->list_mutex);
    }
}

extern int
CMControlList_locked(cl)
CMControlList cl;
{
    return cl->locked;
}

static thr_mutex_t CMglobal_data_mutex = NULL;
static int CMglobal_data_lock_val = 0;

extern void
CMglobal_data_lock()
{
    if (gen_thr_initialized()) {
	if (CMglobal_data_mutex == NULL) {
	    CMglobal_data_mutex = thr_mutex_alloc();
	}
	thr_mutex_lock(CMglobal_data_mutex);
    }
    CMglobal_data_lock_val++;
}

extern void
CMglobal_data_unlock()
{
    CMglobal_data_lock_val--;
    if (CMglobal_data_mutex != NULL) {
	thr_mutex_unlock(CMglobal_data_mutex);
    }
}

extern int
CMglobal_data_locked()
{
    return CMglobal_data_lock_val;
}


