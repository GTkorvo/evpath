#include "config.h"
#include "ltdl.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>
#ifdef HAVE_NETDB_H
#include <netdb.h>
#endif
#include <io.h>
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

static void set_debug_flag(cm)
CManager cm;
{
    if (cm_control_debug_flag == -1) {
	if (CMtrace_on(cm, CMLowLevelVerbose)) {
	    cm_control_debug_flag = 1;
	} else {
	    cm_control_debug_flag = 0;
	}
    }
}

static CMCondition
CMCondition_find(cl, condition)
CMControlList cl;
int condition;
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
CMCondition_get(cm, conn)
CManager cm;
CMConnection conn;
{
    CMControlList cl = cm->control_list;
    CMCondition cond = CMmalloc(sizeof(CMCondition_s));
    set_debug_flag(cm);
    CMControlList_lock(cl);
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
    CMControlList_unlock(cl);
    return cond->condition_num;
}

static void
CMCondition_trigger(cond, cl) 
CMCondition cond;
CMControlList cl;
{
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
CMconn_fail_conditions(conn)
CMConnection conn;
{
    CMControlList cl = conn->cm->control_list;
    CMCondition cond_list;
    set_debug_flag(conn->cm);
    CMControlList_lock(cl);
    cond_list = cl->condition_list;
    while(cond_list != NULL) {
	if (cond_list->conn == conn) {
	    cond_list->failed = 1;
	    CMCondition_trigger(cond_list, cl);
	}
	cond_list = cond_list->next;
    }
    CMControlList_unlock(cl);
}

void
CMCondition_destroy(cl, condition)
CMControlList cl;
int condition;
{
    CMCondition cond = NULL, prev = NULL;
    CMCondition next = NULL;

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
    if (cond == NULL) {
	fprintf(stderr, "Serious internal error.  Use of condition %d, no longer in control list\n", condition);
    } else {
	/* free internal elements */
	if (cond->cond_condition) {
	    thr_condition_free(cond->cond_condition);
	}
	CMfree(cond);
    }
}

extern int
CMCondition_has_signaled(cm, condition)
CManager cm;
int condition;
{
    int retval;
    CMCondition cond;
    CMControlList cl = cm->control_list;
    set_debug_flag(cm);

    CMControlList_lock(cl);
    
    cond = CMCondition_find(cl, condition);
    retval = cond->signaled;
    
    CMControlList_unlock(cl);
    return retval;
}

extern int
CMCondition_has_failed(cm, condition)
CManager cm;
int condition;
{
    int retval;
    CMCondition cond;
    CMControlList cl = cm->control_list;
    set_debug_flag(cm);

    CMControlList_lock(cl);
    
    cond = CMCondition_find(cl, condition);
    retval = cond->failed;
    
    CMControlList_unlock(cl);
    return retval;
}

extern int
CMCondition_wait(cm, condition)
CManager cm;
int condition;
{
    CMCondition cond;
    CMControlList cl = cm->control_list;
    int result;

    set_debug_flag(cm);
    if (cm_control_debug_flag) {
	printf("CMLowLevel Waiting for CMcondition %d\n", condition);
    }
    CMControlList_lock(cl);
    if (cm_control_debug_flag) {
	printf("CMLowLevel locked cl\n");
    }
    cond = CMCondition_find(cl, condition);

    if (cond->signaled) {
	if (cm_control_debug_flag) {
	    printf("CMcondition %d already signalled\n", condition);
	}
	CMControlList_unlock(cl);
	return 1;
    }
    if (cond->failed) {
	if (cm_control_debug_flag) {
	    printf("CMcondition %d already failed\n", condition);
	}
	CMControlList_unlock(cl);
	return 0;
    }
    cond->waiting++;
    if (cm_control_debug_flag) {
	printf("CMLowLevel In condition wait, server thread = %lx\n", 
	       (long)cl->server_thread);
    }
    if (!cl->has_thread) {
	if (cl->server_thread == NULL) {
	    while (!(cond->signaled || cond->failed)) {
		if (cm_control_debug_flag) {
		    printf("CMLowLevel  Polling for CMcondition %d\n", condition);
		}
		CMControlList_unlock(cl);
		CMcontrol_list_wait(cl);
		CMControlList_lock(cl);
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
	    thr_condition_wait(cond->cond_condition, cl->list_mutex);
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
	    CMControlList_unlock(cl);
	    CMcontrol_list_wait(cl);
	    CMControlList_lock(cl);
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
	thr_condition_wait(cond->cond_condition, cl->list_mutex);
	if (cm_control_debug_flag) {
	    printf("CMLowLevel After wait for CMcondition %d, thr_cond %lx\n", 
		   condition, (long)cond->cond_condition);
	}
    }
    result = cond->signaled;
    CMCondition_destroy(cl, condition);
    CMControlList_unlock(cl);
    if (cm_control_debug_flag) {
	printf("CMLowLevel Return from wait CMcondition %d\n", condition);
    }
    return result;
}

extern void
CMCondition_signal(cm, condition)
CManager cm;
int condition;
{
    CMCondition cond;
    CMControlList cl = cm->control_list;
    set_debug_flag(cm);
    CMControlList_lock(cl);
    cond = CMCondition_find(cl, condition);
    cond->signaled = 1;
    CMCondition_trigger(cond, cl);
    if (cl->has_thread == 0) cm->abort_read_ahead = 1;
    CMControlList_unlock(cl);
}

extern void
CMCondition_set_client_data(cm, condition, client_data)
CManager cm;
int condition;
void *client_data;
{
    CMCondition cond;
    CMControlList cl = cm->control_list;
    set_debug_flag(cm);
    CMControlList_lock(cl);
    cond = CMCondition_find(cl, condition);
    cond->client_data = client_data;
    CMControlList_unlock(cl);
}

extern void *
CMCondition_get_client_data(cm, condition)
CManager cm;
int condition;
{
    CMCondition cond;
    void *client_data;
    CMControlList cl = cm->control_list;
    set_debug_flag(cm);
    CMControlList_lock(cl);
    cond = CMCondition_find(cl, condition);
    client_data = cond->client_data;
    CMControlList_unlock(cl);
    return client_data;
}
