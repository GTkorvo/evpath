#include "config.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>
#ifdef HAVE_NETDB_H
#include <netdb.h>
#endif
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#include <stdlib.h>
#include <ffs.h>
#include <atl.h>
#include "evpath.h"
#include "gen_thread.h"
#include "cm_internal.h"

typedef struct _CMCondition {
    CMCondition next;
    int condition_num;
    int waiting;
    int signaled;
    int failed;
    thr_condition_t cond_condition;
    CMConnection conn;
    void *client_data;
} CMCondition_s;

static int cm_control_debug_flag = -1;

static void set_debug_flag(CManager cm)
{
    (void)cm;
    if (cm_control_debug_flag == -1) {
	if (CMtrace_on(cm, CMLowLevelVerbose)) {
	    cm_control_debug_flag = 1;
	} else {
	    cm_control_debug_flag = 0;
	}
    }
}

static CMCondition
CMCondition_find(CMControlList cl, int condition)
{
    CMCondition next = cl->condition_list;
    while (next != NULL) {
	if (next->condition_num == condition) {
	    return next;
	}
	next = next->next;
    }
    fprintf(stderr, "Serious internal error.  Use of condition %d, no longer in control list\n", condition);
    return NULL;
}


extern int
INT_CMCondition_get(CManager cm, CMConnection conn)
{
    CMControlList cl = cm->control_list;
    CMCondition cond = INT_CMmalloc(sizeof(CMCondition_s));
    set_debug_flag(cm);
    cond->next = cl->condition_list;
    cl->condition_list = cond;
    cond->condition_num = cl->next_condition_num++;
    cond->conn = conn;
    if (cl->next_condition_num >= 0xffffff) {
	/* recycle at  (16 M - 1) [ Caution on number reuse ] */
	cl->next_condition_num = 0;
    }
    cond->waiting = 0;
    cond->signaled = 0;
    cond->failed = 0;
    cond->cond_condition = NULL;
    if (gen_thr_initialized()) {
	cond->cond_condition = thr_condition_alloc();
    }
    return cond->condition_num;
}

static void
CMCondition_trigger(CMCondition cond, CMControlList cl)
{
    (void)cl;
    if (cm_control_debug_flag) {
	printf("CMLowLevel Triggering CMcondition %d\n", cond->condition_num);
    }
    if (cond->waiting) {
	if (gen_thr_initialized()) {
	    if (cm_control_debug_flag) {
		printf("CMLowLevel Triggering CMcondition %d, thr_cond %lx\n", 
		       cond->condition_num, (long)cond->cond_condition);
	    }
	    thr_condition_signal(cond->cond_condition);
	}
    }
    if (cm_control_debug_flag) {
	printf("CMLowLevel After trigger for CMcondition %d\n", 
	       cond->condition_num);
    }
}

extern void
CMconn_fail_conditions(CMConnection conn)
{
    CMControlList cl = conn->cm->control_list;
    CMCondition cond_list;
    set_debug_flag(conn->cm);
    cond_list = cl->condition_list;
    while(cond_list != NULL) {
	if (cond_list->conn == conn) {
	    cond_list->failed = 1;
	    CMCondition_trigger(cond_list, cl);
	}
	cond_list = cond_list->next;
    }
}

void
CMCondition_destroy(CMControlList cl, int condition)
{
    CMCondition cond = NULL, prev = NULL;
    CMCondition next = NULL;

    if (cl->condition_list) {
	if (cl->condition_list->condition_num == condition) {
	    cond = cl->condition_list;
	    cl->condition_list = cl->condition_list->next;
	} else {
	    prev = cl->condition_list;
	    next = cl->condition_list->next;
	    while (next != NULL) {
		if (next->condition_num == condition) {
		    cond = next;
		    prev->next = next->next;
		    break;
		}
		prev = next;
		next = next->next;
	    }
	}
    }
    if (cond == NULL) {
	fprintf(stderr, "Serious internal error.  Use of condition %d, no longer in control list\n", condition);
    } else {
	/* free internal elements */
	if (cond->cond_condition) {
	    thr_condition_free(cond->cond_condition);
	    cond->cond_condition = NULL;
	}
	INT_CMfree(cond);
    }
}

extern int
INT_CMCondition_has_signaled(CManager cm, int condition)
{
    int retval;
    CMCondition cond;
    CMControlList cl = cm->control_list;
    set_debug_flag(cm);

    cond = CMCondition_find(cl, condition);
    retval = cond->signaled;
    
    return retval;
}

extern int
INT_CMCondition_has_failed(CManager cm, int condition)
{
    int retval;
    CMCondition cond;
    CMControlList cl = cm->control_list;
    set_debug_flag(cm);

    cond = CMCondition_find(cl, condition);
    retval = cond->failed;
    
    return retval;
}

extern int
INT_CMCondition_wait(CManager cm, int condition)
{
    CMCondition cond;
    CMControlList cl = cm->control_list;
    int result;

    assert(CManager_locked(cm));
    set_debug_flag(cm);
    if (cm_control_debug_flag) {
	printf("CMLowLevel Waiting for CMcondition %d\n", condition);
    }
    if (cm_control_debug_flag) {
	printf("CMLowLevel locked cl\n");
    }
    cond = CMCondition_find(cl, condition);

    if (cond == NULL) return -1;
    if (cond->signaled) {
	if (cm_control_debug_flag) {
	    printf("CMcondition %d already signalled\n", condition);
	}
	return 1;
    }
    if (cond->failed) {
	if (cm_control_debug_flag) {
	    printf("CMcondition %d already failed\n", condition);
	}
	return 0;
    }
    cond->waiting++;
    if (cm_control_debug_flag) {
	printf("CMLowLevel In condition wait, server thread = %lx\n", 
	       (long)cl->server_thread);
    }
    if (!cl->has_thread) {
	if ((cl->server_thread == NULL) || (cl->server_thread == thr_thread_self())) {
	    while (!(cond->signaled || cond->failed)) {
		if (cm_control_debug_flag) {
		    printf("CMLowLevel  Polling for CMcondition %d\n", condition);
		}
		CManager_unlock(cm);
		CMcontrol_list_wait(cl);
		CManager_lock(cm);
	    }
	    if (cm_control_debug_flag) {
		printf("CMLowLevel  after Polling for CMcondition %d\n", condition);
	    }
	    /* the poll and handle will set cl->server_thread, restore it */
	    cl->server_thread = NULL;
	    if (cm_control_debug_flag) {
		printf("CMLowLevel  In condition wait, reset server thread = %lx\n", 
		       (long)cl->server_thread);
	    }
	} else {
	    /* some other thread is servicing the network here 
	       hopefully they'll keep doing it */
	    /* some other thread is the server thread */
	    if (!gen_thr_initialized()) {
		fprintf(stderr, "Gen_Thread library not initialized.\n");
		return 0;
	    }
	    if (cm_control_debug_flag) {
		printf("CMLowLevel Waiting for CMcondition %d, thr_cond %lx\n", 
		       condition, (long)cond->cond_condition);
	    }
	    cm->locked--;
	    thr_condition_wait(cond->cond_condition, cm->exchange_lock);
	    cm->locked++;
	    if (cm_control_debug_flag) {
		printf("CMLowLevel After wait for CMcondition %d, thr_cond %lx\n", 
		       condition, (long)cond->cond_condition);
	    }
	}
    } else if (thr_thread_self() == cl->server_thread) {
	/* we're the server thread */
	while (!(cond->signaled || cond->failed)) {
	    if (cm_control_debug_flag) {
		printf("CMLowLevel polling for CMcondition %d\n", condition);
	    }
	    CManager_unlock(cm);
	    CMcontrol_list_wait(cl);
	    CManager_lock(cm);
	}
    } else {
	/* some other thread is the server thread */
	if (!gen_thr_initialized()) {
	    fprintf(stderr, "Gen_Thread library not initialized.\n");
	    return 0;
	}
	if (cm_control_debug_flag) {
	    printf("CMLowLevel Waiting for CMcondition %d, thr_cond %lx\n", 
		   condition, (long)cond->cond_condition);
	}
	cm->locked--;
	thr_condition_wait(cond->cond_condition, cm->exchange_lock);
	cm->locked++;
	if (cm_control_debug_flag) {
	    printf("CMLowLevel After wait for CMcondition %d, thr_cond %lx\n", 
		   condition, (long)cond->cond_condition);
	}
    }
    result = cond->signaled;
    CMCondition_destroy(cl, condition);
    if (cm_control_debug_flag) {
	printf("CMLowLevel Return from wait CMcondition %d\n", condition);
    }
    return result;
}

extern void
INT_CMCondition_signal(CManager cm, int condition)
{
    CMCondition cond;
    CMControlList cl = cm->control_list;
    if(!CManager_locked(cm)) {
	printf("Not LOCKED!\n");
    }
    set_debug_flag(cm);
    cond = CMCondition_find(cl, condition);
    cond->signaled = 1;
    CMCondition_trigger(cond, cl);
    if (cl->has_thread == 0) cm->abort_read_ahead = 1;
}

extern void
INT_CMCondition_set_client_data(CManager cm, int condition, void *client_data)
{
    CMCondition cond;
    CMControlList cl = cm->control_list;
    set_debug_flag(cm);
    cond = CMCondition_find(cl, condition);
    cond->client_data = client_data;
}

extern void *
INT_CMCondition_get_client_data(CManager cm, int condition)
{
    CMCondition cond;
    void *client_data;
    CMControlList cl = cm->control_list;
    set_debug_flag(cm);
    cond = CMCondition_find(cl, condition);
    client_data = cond->client_data;
    return client_data;
}
