#include "config.h"
#include "ltdl.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <math.h>
#ifdef HAVE_NETDB_H
#include <netdb.h>
#endif
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#include <stdlib.h>
#ifdef HAVE_WINDOWS_H
#include <winsock.h>
#define __ANSI_CPP__
#else
#include <netinet/in.h>
#include <arpa/inet.h>
#endif
#include <io.h>
#include <atl.h>
#include "evpath.h"
#include "gen_thread.h"
#include "cercs_env.h"
#include "cm_internal.h"
#include "cm_transport.h"

#if defined(HAVE_GETDOMAINNAME) && !defined(GETDOMAINNAME_DEFINED)
extern int getdomainname ARGS((char *name, int namelen));
#endif

static void CMinitialize ARGS((CManager cm));

struct CMtrans_services_s CMstatic_trans_svcs = {INT_CMmalloc, INT_CMrealloc, INT_CMfree, 
					       INT_CM_fd_add_select, 
					       CM_fd_write_select, 
					       CM_fd_remove_select, 
					       CMtransport_trace,
					       CMConnection_create,
					       INT_CMadd_shutdown_task,
					       cm_get_data_buf,
					       cm_return_data_buf};
static void CMControlList_close ARGS((CMControlList cl));
static int CMcontrol_list_poll ARGS((CMControlList cl));
int CMdo_non_CM_handler ARGS((CMConnection conn, int header,
			      char *buffer, int length));
void CMdo_performance_response ARGS((CMConnection conn, int length,
					    int func, int byte_swap,
					    char *buffer));

void CMhttp_handler ARGS((CMConnection conn, char* buffer, int length));

static void
CMpoll_forever(cm)
CManager cm;
{
    /* don't hold locks while polling forever */
    CMControlList cl = cm->control_list;
    int should_exit = 0;
    CManager_lock(cm);
    CManager_unlock(cm);
    if (cl->has_thread > 0 && cl->server_thread == thr_thread_self())
	should_exit++;
    while(!cl->closed) {
	CMtrace_out(cm, CMLowLevelVerbose, "CM Poll Forever - thread %lx doing wait", (long)thr_thread_self());
	if (CMcontrol_list_wait(cl) == -1) {
	    CMtrace_out(cm, CMLowLevelVerbose, "CM Poll Forever - doing close and exit");
	    INT_CManager_close(cm);
	    exit(1);
	}
    }
    CMtrace_out(cm, CMLowLevelVerbose, "CM Poll Forever - doing close");
    CManager_lock(cm);
    INT_CManager_close(cm);
    if (should_exit != 0) thr_thread_exit(NULL);
}

extern void
INT_CMrun_network(cm)
CManager cm;
{
    if ((cm->control_list->server_thread != 0) &&
	(cm->control_list->server_thread != thr_thread_self())) {
	/* What?  We're polling, but we're not the server thread? */
	fprintf(stderr, "Warning:  CMrun_network() called when another thread may already be handling the network\n");
	fprintf(stderr, "          This situation may result in unexpected I/O blocking.\n");
	fprintf(stderr, "          Server thread set to %lx.\n", (long) thr_thread_self());
    }
    cm->control_list->server_thread = thr_thread_self();
    cm->control_list->has_thread = 1;
    CManager_unlock(cm);
    CMpoll_forever(cm);
}

static int
CM_test_thread_func()
{
    return 1;
}

int
INT_CMfork_comm_thread(cm)
CManager cm;
{
    /* if we're on a kernel-level-threads package, for the thread and 
       return 1, else return 0; */
    if (gen_thr_is_kernel()) {
	if (cm->control_list->has_thread == 0) {
	    if (cm->control_list->network_blocking_function.func) {
		thr_thread_t server_thread = 
		    thr_fork((void_arg_func)CMpoll_forever, 
			     (void*)cm);
		CMtrace_out(cm, CMLowLevelVerbose,
			    "CM - Forked comm thread %lx", server_thread);
		if (server_thread == NULL) {
		    return 0;
		}
		cm->control_list->server_thread = server_thread;
		cm->control_list->has_thread = 1;
		cm->reference_count++;
		cm->control_list->reference_count++;
		cm->control_list->free_reference_count++;
	    } else {
		/*
		 *  Can't start a server thread yet, but lets see 
		 *  if we can fork anything successfully.
		 */
		thr_thread_t test_thread = 
		    thr_fork((void_arg_func)CM_test_thread_func, 
			     (void*)cm);
		if (test_thread == NULL) {
		    /* No.  Say we can't. */
		    CMtrace_out(cm, CMLowLevelVerbose,
				"CM - Test fork failed, no comm thread");
		    return 0;
		}
		/* OK, we'll fork it later. */
		CMtrace_out(cm, CMLowLevelVerbose,
			    "CM - Will fork comm thread later");
		cm->control_list->has_thread = -1; /* should fork one */
	    }
	}
	return 1;
    }
    CMtrace_out(cm, CMLowLevelVerbose,
		"CM - No threads package for comm thread");
    return 0;
}

extern
void
CMControlList_set_blocking_func(cl, cm, bfunc, pfunc, client_data)
CMControlList cl;
CManager cm;
CMPollFunc bfunc;
CMPollFunc pfunc;
void *client_data;
{
    assert(cl->network_blocking_function.func == NULL);
    cl->network_blocking_function.func = bfunc;
    cl->network_blocking_function.client_data = client_data;
    cl->network_blocking_function.cm = NULL;
    cl->network_polling_function.func = pfunc;
    cl->network_polling_function.client_data = client_data;
    cl->network_polling_function.cm = NULL;
    if (cl->has_thread == -1) {
	thr_thread_t server_thread = 
	    thr_fork((void_arg_func)CMpoll_forever, 
		     (void*)cm);
	if (server_thread == NULL) {
	    return;
	}
	CMtrace_out(cm, CMLowLevelVerbose,
		    "CM - Forked comm thread %lx", server_thread);
	cm->control_list->server_thread = server_thread;
	cm->control_list->reference_count++;
	cm->control_list->free_reference_count++;
	cl->has_thread = 1;
	cm->reference_count++;
    }
}

extern void
INT_CMpoll_network(cm)
CManager cm;
{
    CMControlList cl = cm->control_list;
    CMtrace_out(cm, CMLowLevelVerbose, "CM Poll Network");
    cl->network_polling_function.func((void*)&CMstatic_trans_svcs,
				      cl->network_polling_function.client_data);
    CManager_lock(cm);
    CMcontrol_list_poll(cl);
    CManager_unlock(cm);
}

static void
add_contact_list(cm, attrs)
CManager cm;
attr_list attrs;
{
    int list_size = 0;
    if (cm->contact_lists == NULL) {
	cm->contact_lists = INT_CMmalloc(sizeof(attr_list) *2);
	list_size = 0;
    } else {
	while(cm->contact_lists[list_size] != NULL) list_size++;
	cm->contact_lists = INT_CMrealloc(cm->contact_lists, 
				      sizeof(attr_list) * (list_size + 2));
    }
    cm->contact_lists[list_size] = attrs;
    cm->contact_lists[list_size+1] = NULL;
}

void
INT_CM_insert_contact_info(CManager cm, attr_list attrs)
{
    attr_merge_lists(cm->contact_lists[0], attrs);
}

attr_list
INT_CMget_contact_list(cm)
CManager cm;
{
    if (cm->contact_lists == NULL) return NULL;
    CMadd_ref_attr_list(cm, cm->contact_lists[0]);
    return (cm->contact_lists[0]);
}

extern attr_list
INT_CMget_specific_contact_list(cm, attrs)
CManager cm;
attr_list attrs;
{
    char *chosen_transport = NULL, *chosen_net = NULL;
    int i = 0;

    if (attrs != NULL) {
	get_string_attr(attrs, CM_TRANSPORT, &chosen_transport);
    }
    if (attrs != NULL) {
	get_string_attr(attrs, CM_NETWORK_POSTFIX, &chosen_net);
    }
    if ((chosen_transport == NULL) && (chosen_net == NULL)) {
	CMadd_ref_attr_list(cm, cm->contact_lists[0]);
	return cm->contact_lists[0];
    }
    /* specific transport chosen */
    while (cm->contact_lists && (cm->contact_lists[i] != NULL)) {
	char *this_transport = NULL, *this_postfix = NULL;

	get_string_attr(cm->contact_lists[i], CM_TRANSPORT, &this_transport);
	get_string_attr(cm->contact_lists[i], CM_NETWORK_POSTFIX, &this_postfix);
	if (this_transport == NULL) {
	    this_transport = "sockets";
	}
	if (strcmp(this_transport, chosen_transport) == 0) {
	    if ((chosen_net != NULL) || (this_postfix != NULL)) {
		/* one is not null */
		if (chosen_net && this_postfix) {
		    if (strcmp(chosen_net, this_postfix) != 0) {
			i++;
			continue;
		    }
		} else {
		    i++;
		    continue;
		}
	    }
	    CMadd_ref_attr_list(cm, cm->contact_lists[i]);
	    return cm->contact_lists[i];
	}
	i++;
    }
    /* chosen transport not listened? */
    INT_CMlisten_specific(cm, attrs);
    /* try again */
    while (cm->contact_lists && (cm->contact_lists[i] != NULL)) {
	char *this_transport = NULL, *this_postfix = NULL;

	get_string_attr(cm->contact_lists[i], CM_TRANSPORT, &this_transport);
	get_string_attr(cm->contact_lists[i], CM_NETWORK_POSTFIX, 
			&this_postfix);
	if (this_transport == NULL) {
	    this_transport = "sockets";
	}
	if (strcmp(this_transport, chosen_transport) == 0) {
	    if ((chosen_net != NULL) || (this_postfix != NULL)) {
		/* one is not null */
		if (chosen_net && this_postfix) {
		    if (strcmp(chosen_net, this_postfix) != 0) {
			i++;
			continue;
		    }
		} else {
		    i++;
		    continue;
		}
	    }
	    CMadd_ref_attr_list(cm, cm->contact_lists[i]);
	    return cm->contact_lists[i];
	}
	i++;
    }
    /* maybe it failed to load */
    return NULL;
}

int
INT_CMlisten(cm)
     CManager cm;
{
  return INT_CMlisten_specific (cm, NULL);
}

extern int
CMinternal_listen(cm, listen_info)
CManager cm;
attr_list listen_info;
{
    int success = 0;
    transport_entry *trans_list;
    char *choosen_transport = NULL;

    if (listen_info != NULL) {
	get_string_attr(listen_info, CM_TRANSPORT, &choosen_transport);
    }
    if (choosen_transport != NULL) {
        CMtrace_out(cm, CMConnectionVerbose,
		    "CM - Listening only on transport \"%s\"",
		    choosen_transport);
	if (load_transport(cm, choosen_transport) == 0) {
	    CMtrace_out(cm, CMConnectionVerbose,
			"Failed to load transport \"%s\".  Revert to default.\n",
			choosen_transport);
	    choosen_transport = NULL;
	}
    }
    trans_list = cm->transports;
    while ((trans_list != NULL) && (*trans_list != NULL)) {
	attr_list attrs;
	if ((choosen_transport == NULL) || 
	    (strcmp((*trans_list)->trans_name, choosen_transport) == 0)) {
	    attrs = (*trans_list)->listen(cm, &CMstatic_trans_svcs,
					  *trans_list,
					  listen_info);
	    add_contact_list(cm, attrs);
	    if (CMtrace_on(cm, CMConnectionVerbose)) {
		printf("Adding contact list -> ");
		dump_attr_list(attrs);
	    }
	    if (attrs != NULL) {
		success++;
	    }
	}
	trans_list++;
    }
    return success;
}

int
INT_CMlisten_specific(cm, listen_info)
CManager cm;
attr_list listen_info;
{
    int success = 0;
    if (!cm->initialized) CMinitialize(cm);
    success = CMinternal_listen(cm, listen_info);
    return (success != 0);
}

#ifndef DONT_USE_SOCKETS
static char *CMglobal_default_transport = "sockets";
#else 
static char *CMglobal_default_transport = NULL;
#endif

static char *CMglobal_alternate_transports[] = {NULL};

static void 
CMinitialize(cm)
CManager cm;
{
    char **transport_names = CMglobal_alternate_transports;
    char *def = cercs_getenv("CMDefaultTransport");
    if (def != NULL) CMglobal_default_transport = def;
    if (CMglobal_default_transport) {
	if (load_transport(cm, CMglobal_default_transport) == 0) {
	    fprintf(stderr, "Failed to initialize default transport.  Exiting.\n");
	    exit(1);
	}
    }
    while ((transport_names != NULL) && (transport_names[0] != NULL)) {
	load_transport(cm, transport_names[0]);
	transport_names++;
    }
    cm->initialized++;
}

static int
CMcontrol_list_poll(cl)
CMControlList cl;
{
    func_entry *poll_list = cl->polling_function_list;
    while ((poll_list != NULL) && (poll_list->func != NULL)){
	int consistency_number = cl->cl_consistency_number;

	poll_list->func(poll_list->cm, poll_list->client_data);
	/* do function */
	if (consistency_number != cl->cl_consistency_number) {
	    return 1;
	}
	poll_list++;
    }
    return 1;
}

static 
void
CMControlList_add_poll(cl, cm, func, client_data)
CMControlList cl;
CManager cm;
CMPollFunc func;
void *client_data;
{
    func_entry *poll_list;
    int count = 0;
    poll_list = cl->polling_function_list;
    while ((poll_list != NULL) && (poll_list->func != NULL)) {
	count++;
    }
    if (poll_list != NULL) {
	poll_list = INT_CMrealloc(poll_list, sizeof(func_entry) * (count+2));
    } else {
	poll_list = INT_CMmalloc(sizeof(func_entry)*2);
    }
    poll_list[count].cm = cm;
    poll_list[count].func = func;
    poll_list[count].client_data = client_data;
    poll_list[count+1].func = NULL;
    cl->polling_function_list = poll_list;
}
    
extern
void
INT_CMadd_poll(cm, func, client_data)
CManager cm;
CMPollFunc func;
void *client_data;
{
    CMControlList_add_poll(cm->control_list, cm, func, client_data);
}

extern
int
CMcontrol_list_wait(cl)
CMControlList cl;
{
    /* associated CM should *not* be locked */
    if ((cl->server_thread != 0) &&
	(cl->server_thread != thr_thread_self())) {
	/* What?  We're polling, but we're not the server thread? */
	fprintf(stderr, "Warning:  Multiple threads calling CMnetwork_wait\n");
	fprintf(stderr, "          This situation may result in unexpected I/O blocking.\n");
	fprintf(stderr, "          Server thread set to %lx.\n", (long) thr_thread_self());
    }
    cl->server_thread = thr_thread_self();
    if (cl->network_blocking_function.func != NULL) {
	cl->network_blocking_function.func((void*)&CMstatic_trans_svcs,
					   cl->network_blocking_function.client_data);
    }
    CMcontrol_list_poll(cl);
    return 1;
}

static CMControlList CMControlList_create();

extern
CManager
INT_CManager_create()
{
    CManager cm = (CManager) INT_CMmalloc(sizeof(CManager_s));
    int atom_init = 0;

    if (cm == NULL)
	return NULL;

    if (atom_init == 0) {
	set_attr_atom_and_string("CM_TRANSPORT", CM_TRANSPORT);
	set_attr_atom_and_string("CM_NETWORK_POSTFIX", CM_NETWORK_POSTFIX);
	set_attr_atom_and_string("IP_HOST", CM_IP_HOSTNAME);
	set_attr_atom_and_string("IP_PORT", CM_IP_PORT);
	set_attr_atom_and_string("CM_REG_BW_RUN_LEN", CM_REBWM_RLEN );
	set_attr_atom_and_string("CM_REG_BW_REPEAT_CNT", CM_REBWM_REPT);
	set_attr_atom_and_string("CM_BW_MEASURE_INTERVAL",
				 CM_BW_MEASURE_INTERVAL);
	set_attr_atom_and_string("CM_BW_MEASURE_TASK",
				 CM_BW_MEASURE_TASK);
	set_attr_atom_and_string("CM_BW_MEASURED_VALUE",
				 CM_BW_MEASURED_VALUE);
	set_attr_atom_and_string("CM_BW_MEASURED_COF",
				 CM_BW_MEASURED_COF);
	set_attr_atom_and_string("CM_BW_MEASURE_SIZE",
				 CM_BW_MEASURE_SIZE);
	set_attr_atom_and_string("CM_BW_MEASURE_SIZEINC",
				 CM_BW_MEASURE_SIZEINC);
	set_attr_atom_and_string("CM_EVENT_SIZE",
                                 CM_EVENT_SIZE);
	set_attr_atom_and_string("EV_EVENT_LSUM",
                                 EV_EVENT_LSUM);
    }

    /* initialize data structs */
    cm->transports = NULL;
    cm->initialized = 0;
    cm->reference_count = 1;

    cm->control_list = CMControlList_create();
    cm->exchange_lock = thr_mutex_alloc();

    cm->locked = 0;
    cm->closed = 0;
    cm->abort_read_ahead = 0;
    CMinit_local_formats(cm);
    cm->context_lock = thr_mutex_alloc();

    cm->in_format_count = 0;
    cm->in_formats = INT_CMmalloc(1);

    cm->reg_format_count = 0;
    cm->reg_formats = INT_CMmalloc(1);

    cm->pending_request_max = 1;
    cm->pbio_requests = INT_CMmalloc(sizeof(struct _pending_format_requests));
    cm->pbio_requests[0].server_id = NULL;
    cm->pbio_requests[0].id_length = 0;
    cm->pbio_requests[0].condition = 0;
    cm->pbio_requests[0].top_request = 0;

    cm->connection_count = 0;
    cm->connections = INT_CMmalloc(1);
    cm->reg_user_format_count = 0;
    cm->reg_user_formats = INT_CMmalloc(1);
    cm->taken_buffer_list = NULL;
    cm->cm_buffer_list = NULL;

    cm->contact_lists = NULL;
    cm->shutdown_functions = NULL;
    EVPinit(cm);
    return cm;
}

extern void CMControlList_free(CMControlList cl);

static void
CManager_free(cm)
CManager cm;
{
    int i;
    CMbuffer list = NULL;

    if (cm->transports) {
	int i = 0;
	while (cm->transports[i] != NULL) {
	    INT_CMfree(cm->transports[i]);
	    i++;
	}
	INT_CMfree(cm->transports);
    }

    INT_CMfree(cm->in_formats);

    for (i=0 ; i < cm->reg_format_count; i++) {
	free_IOsubcontext(cm->reg_formats[i]->IOsubcontext);
	INT_CMfree(cm->reg_formats[i]->format_name);
	INT_CMfree(cm->reg_formats[i]);
    }
    INT_CMfree(cm->reg_formats);

    /*
     *  Applications are expected to free the user contexts that 
     *  they request.  If they do this, there will be no user formats to
     *  free at this point.  (Doing so might result in double freeing.)
     */
    INT_CMfree(cm->reg_user_formats);

    INT_CMfree(cm->pbio_requests);

    INT_CMfree(cm->connections);
    /*
     * GSE why o why
     * a bug a cannot find.  If I leave the free in place, bad things happen
     * on linux in cm/tests/take_test, SOMETIMES.
     * I.E. sometimes there's a seg fault.  If I run with MALLOC_CHECK_ or 
     * much of anything else, the problem goes away.  So, for now, simply 
     * don't do the thr_mutex_free().  Sigh.
     */
    /* thr_mutex_free(cm->exchange_lock);*/

    free_IOcontext(cm->IOcontext);
    thr_mutex_free(cm->context_lock);

    i = 0;
    if (cm->contact_lists != NULL) {
	while(cm->contact_lists[i] != NULL) {
	    INT_CMfree_attr_list(cm, cm->contact_lists[i]);
	    i++;
	}
	INT_CMfree(cm->contact_lists);
    }
    list = cm->taken_buffer_list;
    while (list != NULL) {
	CMbuffer next = list->next;
	/* don't free the taken buffers */
	INT_CMfree(list);
	list = next;
    }
    list = cm->cm_buffer_list;
    while (list != NULL) {
	CMbuffer next = list->next;
	INT_CMfree(list->buffer);
	INT_CMfree(list);
	list = next;
    }
    if (cm->shutdown_functions) INT_CMfree(cm->shutdown_functions);
    INT_CMfree(cm);
}

extern void
INT_CManager_close(cm)
CManager cm;
{
    CMControlList cl = cm->control_list;

    CMtrace_out(cm, CMFreeVerbose, "CManager %lx closing", (long) cm);
    while (cm->connection_count != 0) {
	/* connections are moved down as they are closed... */
	INT_CMConnection_close(cm->connections[0]);
    }

    CMtrace_out(cm, CMFreeVerbose, "CMControlList close CL=%lx current reference count will be %d", 
		(long) cl, cl->reference_count - 1);
    CMControlList_close(cl);
    if (cm->shutdown_functions != NULL) {
	int i = 0;
	func_entry *shutdown_functions = cm->shutdown_functions;
	cm->shutdown_functions = NULL;

	while (shutdown_functions[i].func != NULL) {
	    i++;
	}
	i--;
	for ( ; i >= 0; i--) {
	    CMtrace_out(cm, CMFreeVerbose, "CManager calling shutdown function %d, %lx", i, (long)shutdown_functions[i].func);
	    shutdown_functions[i].func(cm, shutdown_functions[i].client_data);
	    shutdown_functions[i].func = NULL;
	}
	INT_CMfree(shutdown_functions);
    }
    CMControlList_free(cl);

    cm->reference_count--;
    CMtrace_out(cm, CMFreeVerbose, "CManager %lx ref count now %d", 
		(long) cm, cm->reference_count);
    if (cm->reference_count == 0) {
	CMtrace_out(cm, CMFreeVerbose, "Freeing CManager %lx", cl);
	CManager_unlock(cm);
	CManager_free(cm);
    } else {
	CManager_unlock(cm);
    }
}

extern void
internal_add_shutdown_task(CManager cm, CMPollFunc func, void *client_data)
{
    int func_count = 0;
    if (cm->shutdown_functions == NULL) {
	cm->shutdown_functions = 
	    INT_CMmalloc(sizeof(cm->shutdown_functions[0]) * 2);
    } else {
	while (cm->shutdown_functions[func_count].func != NULL) {
	    func_count++;
	}
	cm->shutdown_functions = 
	    INT_CMrealloc(cm->shutdown_functions,
		      sizeof(cm->shutdown_functions[0]) * (func_count +2));
    }
    cm->shutdown_functions[func_count].func = func;
    cm->shutdown_functions[func_count].client_data = client_data;
    func_count++;
    cm->shutdown_functions[func_count].func = NULL;
}

extern void
INT_CMadd_shutdown_task(CManager cm, CMPollFunc func, void *client_data)
{
    internal_add_shutdown_task(cm, func, client_data);
}

static void
add_conn_to_CM(CManager cm, CMConnection conn)
{
    cm->connections = 
	INT_CMrealloc(cm->connections, 
		  (cm->connection_count + 1) * sizeof(cm->connections[0]));
    cm->connections[cm->connection_count] = conn;
    cm->connection_count++;
}

static void
remove_conn_from_CM(CManager cm, CMConnection conn)
{
    int i;
    int found = 0;
    for (i=0; i < cm->connection_count; i++) {
	if (cm->connections[i] == conn) {
	    found++;
	} else if (found) {
	    /* copy down */
	    cm->connections[i-1] = cm->connections[i];
	}
    }
    if (found == 0) {
	fprintf(stderr, "Internal error, remove_conn_from_CM.  Not found\n");
    } else {
	cm->connection_count--;
    }
}

static CMControlList
CMControlList_create()
{
    CMControlList new_list = (CMControlList) INT_CMmalloc(sizeof(CMControlList_s));
    new_list->select_initialized = 0;
    new_list->select_data = NULL;
    new_list->add_select = NULL;
    new_list->remove_select = NULL;
    new_list->server_thread = NULL;
    new_list->network_blocking_function.func = NULL;
    new_list->network_polling_function.func = NULL;
    new_list->polling_function_list = NULL;
    new_list->cl_consistency_number = 1;
    new_list->reference_count = 1;
    new_list->free_reference_count = 1;
    new_list->list_mutex = NULL;
    new_list->list_mutex = thr_mutex_alloc();
    new_list->condition_list = NULL;
    new_list->next_condition_num = 1;
    new_list->closed = 0;
    new_list->locked = 0;
    new_list->has_thread = 0;
    return new_list;
}

CMConnection
CMConnection_create(trans, transport_data, conn_attrs)
transport_entry trans;
void *transport_data;
attr_list conn_attrs;
{
    static int first = 1;
    static int non_block_default = 0;
    int blocking_on_conn;
    CMConnection conn = INT_CMmalloc(sizeof(struct _CMConnection));
    if (first) {
	char *value = cercs_getenv("CMNonBlockWrite");
	first = 0;
	if (value != NULL) {
	    sscanf(value, "%d", &non_block_default);
	    CMtrace_out(trans->cm, CMConnectionVerbose, "CM default blocking %d",
			non_block_default);
	}
    }
    conn->cm = trans->cm;
    conn->trans = trans;
    conn->transport_data = transport_data;
    conn->ref_count = 1;
    conn->write_lock = thr_mutex_alloc();
    conn->read_lock = thr_mutex_alloc();
    conn->closed = 0;
    conn->failed = 0;
    conn->downloaded_formats = NULL;
    conn->IOsubcontext = create_IOsubcontext(trans->cm->IOcontext);
    conn->foreign_data_handler = NULL;
    conn->io_out_buffer = create_IOBuffer();
    conn->close_list = NULL;
    conn->write_callback_len = 0;
    conn->write_callbacks = NULL;
    conn->attrs = conn_attrs;
    conn->attr_encode_buffer = create_AttrBuffer();

    conn->partial_buffer = NULL;
    conn->buffer_full_point = 0;
    conn->buffer_data_end = 0;

    conn->characteristics = NULL;
    conn->write_pending = 0;
    conn->do_non_blocking_write = non_block_default;
    
    if (get_int_attr(conn_attrs, CM_CONN_BLOCKING, &blocking_on_conn)) {
	conn->do_non_blocking_write = !blocking_on_conn;
    }
    add_conn_to_CM(trans->cm, conn);
    CMtrace_out(trans->cm, CMFreeVerbose, "CMConnection_create %lx ",
		(long) conn);
    return conn;
}

extern attr_list
INT_CMConnection_get_attrs(conn)
CMConnection conn;
{
    return conn->attrs;
}

typedef struct {
    int size; /*stream length to probe bw. */
    int size_inc;/*size=size+size_inc if previous size is not adequate*/
    int successful_run; /*if we have at least 5 successful runs, 
			  size is set properly*/
    int failed_run;/*After parameters are set properly, if we have 3
		     contineous runs failed, we need to set the size again 
		     for the changed network condition*/
    CMConnection conn;
    attr_list attrs;
} *bw_measure_data;

static void
do_bw_measure(CManager cm, void *client_data)
{
    double bw;
    bw_measure_data data = (bw_measure_data) client_data;
    bw=INT_CMregressive_probe_bandwidth(data->conn, data->size, data->attrs);

    /*Initialization phase*/
    if(bw<0 && data->successful_run<5){
	data->size+=data->size_inc;
	data->successful_run=0;
    }
    if(bw>=0 && data->successful_run<5) data->successful_run++; /*if measured correctly in several back-to-back measurements, increase data->successful_run up to 5. 5 indicates parameters are well tuned now*/

    /*After initialization, if network condition changed, and previously tuned parameters come to not adequate:*/
    if(bw<0 && data->successful_run>=5 && data->failed_run<5)
	data->failed_run++;
    if(bw>=0)  data->failed_run=0; /*guard for contineous failures. */
    if(data->failed_run>=5){ /*need to tune parameters again now. */
	data->successful_run=0;
	data->failed_run=0;
    }
	
    CMtrace_out(data->conn->cm, CMLowLevelVerbose,"successful run: %d, failed run: %d, size: %d, bw: %f\n", data->successful_run, data->failed_run, data->size, bw);
    return;
}    

extern int
INT_CMConnection_set_character(CMConnection conn, attr_list attrs)
{
    long interval_value;
    if (attrs == NULL) return 0;
    if (get_long_attr(attrs, CM_BW_MEASURE_INTERVAL, &interval_value)) {
	bw_measure_data data;
	int previous_interval;
	CMTaskHandle task = NULL;
	if ((interval_value <= 1) || (interval_value > 60*60*8)) {
	    printf("Bad CM_BW_MEASURE_INTERVAL, %ld seconds\n",
		   interval_value);
	    return 0;
	}

	CMtrace_out(conn->cm, CMLowLevelVerbose,"CM_BW_MEASURE_INTERVAL set, interval is %d", interval_value);
	if (conn->characteristics && 
	    (get_int_attr(conn->characteristics, CM_BW_MEASURE_INTERVAL,
			  &previous_interval) != 0)) {
	    CMTaskHandle prior_task = NULL;
	    if (interval_value >= previous_interval) {
		CMtrace_out(conn->cm, CMLowLevelVerbose,"CM_BW_MEASURE_INTERVAL prior interval is %d, no action.", previous_interval);
		return 1;
	    }
	    CMtrace_out(conn->cm, CMLowLevelVerbose,"CM_BW_MEASURE_INTERVAL prior interval is %d, killing prior task.", previous_interval);
	    get_long_attr(conn->characteristics, CM_BW_MEASURE_TASK,
			  (long*)(long)&prior_task);
	    if (prior_task) INT_CMremove_task(prior_task);
	}
	data = malloc(sizeof(*data));
	data->size=data->size_inc=-1;
	
	/*Get attr about size, size_inc from attributes. */
	get_int_attr(attrs, CM_BW_MEASURE_SIZE, &(data->size));
	if(data->size<1024)
	    data->size=1024;
	get_int_attr(attrs, CM_BW_MEASURE_SIZEINC, &(data->size_inc));
	if(data->size_inc<1024)
	    data->size_inc=1024;

	data->successful_run=0;
	data->failed_run=0;

	/*app set attr about N and repeat time. store in data->attrs automically and pass to regressive_probe_bandwidth. 
	 */
	data->conn = conn;
	data->attrs = CMattr_copy_list(conn->cm, attrs);
	/* do one task almost immediately */
	(void) INT_CMadd_delayed_task(conn->cm, 0, 1000, do_bw_measure, 
				  (void*)data);
	/* schedule tasks periodically */
	task = INT_CMadd_periodic_task(conn->cm, interval_value, 0, 
				   do_bw_measure, (void*)data);
	if (conn->characteristics == NULL) {
	    conn->characteristics = CMcreate_attr_list(conn->cm);
	}
	set_int_attr(conn->characteristics, CM_BW_MEASURE_INTERVAL,
		     interval_value);
	set_long_attr(conn->characteristics, CM_BW_MEASURE_TASK, (long)task);
	
	return 1;
    }
    return 0;
}

extern CMConnection 
INT_CMget_indexed_conn(cm,i)
CManager cm;
int i;
{
    if (i>=0 && i<cm->connection_count) {
	if (cm->connections[i] != NULL) {
	    return cm->connections[i];
	} else {
	    CMtrace_out(cm, CMConnectionVerbose,
			"cm->connection[%d] is NULL. INT_CMget_indexed_conn\n", i);
	    return NULL;
	}
    } else {
	CMtrace_out(cm, CMConnectionVerbose,
		    "Invalid index. i=%d. INT_CMget_indexed_conn\n", i);
	return NULL;
    }
}

static void CMConnection_failed (CMConnection conn);

void
INT_CMConnection_dereference(conn)
CMConnection conn;
{
    conn->ref_count--;
    if (conn->ref_count > 0) {
	CMtrace_out(conn->cm, CMConnectionVerbose, "CM - Dereference connection %lx",
		    (void*)conn);
	return;
    }
    if (conn->ref_count < 0) return;   /*  BAD! */
    conn->closed = 1;
    CMtrace_out(conn->cm, CMConnectionVerbose, "CM - Shut down connection %lx\n",
		(void*)conn);
    if (conn->failed == 0) {
	CMConnection_failed(conn);
    }
    if (conn->write_callbacks) INT_CMfree(conn->write_callbacks);
    thr_mutex_free(conn->write_lock);
    thr_mutex_free(conn->read_lock);
    free_IOsubcontext(conn->IOsubcontext);
    INT_CMfree(conn->downloaded_formats);
    conn->foreign_data_handler = NULL;
    free_IOBuffer(conn->io_out_buffer);
    free_AttrBuffer(conn->attr_encode_buffer);
    INT_CMfree(conn);
}

static void
CMConnection_failed(conn)
CMConnection conn;
{
    CMTaskHandle prior_task = NULL;
    if (conn->failed) return;
    conn->failed = 1;
    CMtrace_out(conn->cm, CMFreeVerbose, "CMConnection failed conn=%lx", 
		(long) conn);
    CMconn_fail_conditions(conn);
    remove_conn_from_CM(conn->cm, conn);
    conn->trans->shutdown_conn(&CMstatic_trans_svcs, conn->transport_data);
    get_long_attr(conn->characteristics, CM_BW_MEASURE_TASK, 
		  (long*)(long)&prior_task);
    if (prior_task) INT_CMremove_task(prior_task);
    if (conn->close_list) {
	CMCloseHandlerList list = conn->close_list;
	conn->close_list = NULL;
	while (list != NULL) {
	    CMCloseHandlerList next = list->next;
	    CMtrace_out(conn->cm, CMConnectionVerbose, 
			"CM - Calling close handler %lx for connection %lx\n",
			(void*) list->close_handler, (void*)conn);
	    list->close_handler(conn->cm, conn, list->close_client_data);
	    INT_CMfree(list);
	    list = next;
	}
    }
    if (conn->closed != 0) INT_CMConnection_close(conn);
}

void
INT_CMConnection_close(conn)
CMConnection conn;
{
    CMtrace_out(conn->cm, CMFreeVerbose, "CMConnection close conn=%lx ref count will be %d", 
		(long) conn, conn->ref_count - 1);
    INT_CMConnection_dereference(conn);
}

void
INT_CMconn_register_close_handler(conn, func, client_data)
CMConnection conn;
CMCloseHandlerFunc func;
void *client_data;
{
    CMCloseHandlerList *lastp = &conn->close_list;
    CMCloseHandlerList entry = INT_CMmalloc(sizeof(*entry));
    while (*lastp != NULL) lastp = &((*lastp)->next);
    entry->close_handler = func;
    entry->close_client_data = client_data;
    entry->next = NULL;
    *lastp = entry;
}

static void
CMControlList_close(cl)
CMControlList cl;
{
    void *status;
    cl->reference_count--;
    cl->closed = 1;
    if ((cl->has_thread > 0) && (cl->server_thread != thr_thread_self())){
	    (cl->wake_select)((void*)&CMstatic_trans_svcs,
			      &cl->select_data);
    }	
    if (cl->reference_count == 0) {
        if ((cl->has_thread > 0) && (cl->server_thread != thr_thread_self())){
	    (cl->stop_select)((void*)&CMstatic_trans_svcs,
			      &cl->select_data);
	    (cl->wake_select)((void*)&CMstatic_trans_svcs,
			      &cl->select_data);
            thr_thread_join(cl->server_thread, &status);
	}
	cl->closed = 1;
    }
}

extern int trace_val[];

extern void
CMControlList_free(cl)
CMControlList cl;
{
    cl->free_reference_count--;
    if (trace_val[CMFreeVerbose]) {
	printf("CMControlList_free, %lx, ref count now %d\n", (long)cl,
	       cl->free_reference_count);
    }
    if(cl->free_reference_count == 0) {
	if (trace_val[CMFreeVerbose]) {
	    printf("CMControlList_free freeing %lx\n", (long)cl);
	}
	if (cl->polling_function_list != NULL) {
	    INT_CMfree(cl->polling_function_list);
	}
	thr_mutex_free(cl->list_mutex);
	INT_CMfree(cl);
    }
}

#ifndef GETDOMAINNAME_DEFINED
extern int getdomainname ARGS((char*name, int namelen));
#endif

#include "qual_hostname.c"

extern void
CMget_qual_hostname(char *buf, int len)
{
    get_qual_hostname(buf, len, &CMstatic_trans_svcs, NULL, NULL);
}

extern int
INT_CMget_self_ip_addr()
{
    return get_self_ip_addr(&CMstatic_trans_svcs);
}

static
CMConnection
try_conn_init(CManager cm, transport_entry trans, attr_list attrs)
{
    CMConnection conn;
    conn = trans->initiate_conn(cm, &CMstatic_trans_svcs,
				trans, attrs);
    if (conn != NULL) {
	if (CMtrace_on(conn->cm, CMConnectionVerbose)) {
	    char *attr_str = attr_list_to_string(attrs);
	    CMtrace_out(conn->cm, CMConnectionVerbose, 
			"CM - Establish connection %lx - %s", (void*)conn,
			attr_str);
	    INT_CMfree(attr_str);
	}
    }
    return conn;
}

CMConnection
CMinternal_initiate_conn(cm, attrs)
CManager cm;
attr_list attrs;
{
    transport_entry *trans_list;
    char *choosen_transport = NULL;

    assert(CManager_locked(cm));

    if (attrs != NULL) {
	get_string_attr(attrs, CM_TRANSPORT, &choosen_transport);
    }
    if (choosen_transport != NULL) {
	if (load_transport(cm, choosen_transport) == 0) {
	    CMtrace_out(cm, CMConnectionVerbose,
			"Failed to load transport \"%s\".  Revert to default.\n",
			choosen_transport);
	    choosen_transport = NULL;
	}
    }
    trans_list = cm->transports;
    if (choosen_transport == NULL) {
        CMtrace_out(cm, CMConnectionVerbose,
		    "INT_CMinitiate_conn no transport attr found");

	while ((trans_list != NULL) && (*trans_list != NULL)) {
	    CMConnection conn;
	    conn = try_conn_init(cm, *trans_list, attrs);
	    if (conn != NULL) return conn;
	    trans_list++;
	}
    } else {
        CMtrace_out(cm, CMConnectionVerbose,
		    "INT_CMinitiate_conn looking for transport \"%s\"", 
		    choosen_transport);
	while ((trans_list != NULL) && (*trans_list != NULL)) {
	    if (strcmp((*trans_list)->trans_name, choosen_transport) == 0) {
		return try_conn_init(cm, *trans_list, attrs);
	    }
	    trans_list++;
	}
        CMtrace_out(cm, CMConnectionVerbose,
		    "INT_CMinitiate_conn transport \"%s\" not found - no connection", 
		    choosen_transport);
	return NULL;
    }
	
    return NULL;
}

static void
dump_CMConnection(CMConnection conn)
{
    if (conn == NULL) {
	printf("CMConnection NULL\n");
	return;
    }
    printf("CMConnection %lx, reference count %d, closed %d\n\tattrs : ", 
	   (long) conn, conn->ref_count, conn->closed);
    dump_attr_list(conn->attrs);
    printf("\tbuffer_full_point %d, current buffer_end %d\n", 
	   conn->buffer_full_point, conn->buffer_data_end);
    printf("\twrite_pending %d\n", conn->write_pending);
}

CMConnection
INT_CMinitiate_conn(cm, attrs)
CManager cm;
attr_list attrs;
{
    CMConnection conn;
    if (!cm->initialized) CMinitialize(cm);
    conn = CMinternal_initiate_conn(cm, attrs);
    if (CMtrace_on(cm, CMConnectionVerbose)) {
	printf("CMinitiate_conn returning ");
	if (conn != NULL) {
	    dump_CMConnection(conn);
	} else {
	    printf("NULL\n");
	}
    }
    return conn;
}

void
INT_CMConnection_add_reference(conn)
CMConnection conn;
{
    conn->ref_count++;
}

CMConnection
CMinternal_get_conn(cm, attrs)
CManager cm;
attr_list attrs;
{
    int i;
    CMConnection conn = NULL;
    assert(CManager_locked(cm));
    if (CMtrace_on(cm, CMConnectionVerbose)) {
	printf("In CMinternal_get_conn, attrs ");
	if (attrs) dump_attr_list(attrs); else printf("\n");
    }
    for (i=0; i<cm->connection_count; i++) {
	CMConnection tmp = cm->connections[i];
	if (tmp->trans->connection_eq(cm, &CMstatic_trans_svcs,
				       tmp->trans, attrs,
				       tmp->transport_data)) {
	    tmp->ref_count++;
	    conn = tmp;
	}
    }
    if (conn == NULL) {
	conn = CMinternal_initiate_conn(cm, attrs);
	if (conn != NULL) {
	    conn->ref_count++;
	}
    }
    if (CMtrace_on(cm, CMConnectionVerbose)) {
	printf("CMinternal_get_conn returning ");
	if (conn != NULL) {
	    dump_CMConnection(conn);
	} else {
	    printf("NULL\n");
	}
    }
    return conn;
}

CMConnection
INT_CMget_conn(cm, attrs)
CManager cm;
attr_list attrs;
{
    CMConnection conn;
    if (!cm->initialized) CMinitialize(cm);
    conn = CMinternal_get_conn(cm, attrs);
    return conn;
}

int
INT_CMcontact_self_check(cm, attrs)
CManager cm;
attr_list attrs;
{
    transport_entry *trans_list;
    if (!cm->initialized) CMinitialize(cm);
    trans_list = cm->transports;
    while ((trans_list != NULL) && (*trans_list != NULL)) {
	int result = 0;
	result = (*trans_list)->self_check(cm, &CMstatic_trans_svcs, 
					   *trans_list, attrs);
	if (result) return result;
	trans_list++;
    }
    return 0;
}

/* alloc temporary buffer for CM use */
extern CMbuffer
cm_get_data_buf(CManager cm, int length)  
{
    CMbuffer tmp = cm->cm_buffer_list;
    while (tmp != NULL) {
	if (!tmp->in_use_by_cm) {
	    if (tmp->size < length) {
		char *t = INT_CMrealloc(tmp->buffer, length);
		if (t == NULL) {
		    return NULL;
		}
		tmp->buffer = t;
		tmp->size = length;
	    }
	    tmp->in_use_by_cm++;
	    return tmp;
	}
	tmp = tmp->next;
    }
    tmp = INT_CMmalloc(sizeof(*tmp));
    tmp->buffer = INT_CMmalloc(length);
    tmp->size = length;
    tmp->in_use_by_cm = 1;
    tmp->next = cm->cm_buffer_list;
    cm->cm_buffer_list = tmp;
    return tmp;
}

/* realloc temporary buffer for CM use */
extern CMbuffer
cm_extend_data_buf(CManager cm, CMbuffer tmp, int length)  
{
    if (tmp->size < length) {
	char *t = INT_CMrealloc(tmp->buffer, length);
	if (t == NULL) {
	    return NULL;
	}
	tmp->buffer = t;
	tmp->size = length;
    }
    return tmp;
}

/* CM says that it is done with temporary buffer */
extern void
cm_return_data_buf(CMbuffer cmb)
{
    if (cmb) cmb->in_use_by_cm = 0;
}

/* user wants CM temporary buffer */
static int
cm_user_take_data_buf(CManager cm, void *buffer)
{
    CMbuffer tmp = cm->cm_buffer_list;
    CMbuffer last = NULL;
    while (tmp != NULL) {
	if ((tmp->buffer <= buffer) && 
	    ((char*)buffer < ((char*)tmp->buffer + tmp->size))){
	    /* remove the buffer from CM's list */
	    if (last == NULL) {
		cm->cm_buffer_list = tmp->next;
	    } else {
		last->next = tmp->next;
	    }
	    /* add the buffer to the taken list */
	    tmp->next = cm->taken_buffer_list;
	    cm->taken_buffer_list = tmp;
	    return 1;
	}
	last = tmp;
	tmp = tmp->next;
    }
    return 0;
}
    
/* user is done with CM temporary buffer */
static int
cm_user_return_data_buf(CManager cm, void *buffer)
{
    CMbuffer tmp = cm->taken_buffer_list;
    CMbuffer last = NULL;
    while (tmp != NULL) {
	if ((tmp->buffer <= buffer) && 
	    ((char*)buffer < ((char*)tmp->buffer + tmp->size))){
	    /* remove the buffer from taken list */
	    if (last == NULL) {
		cm->taken_buffer_list = tmp->next;
	    } else {
		last->next = tmp->next;
	    }
	    /* add the buffer to the CMs list */
	    tmp->next = cm->cm_buffer_list;
	    cm->cm_buffer_list = tmp;
	    return 1;
	}
	last = tmp;
	tmp = tmp->next;
    }
    return 0;
}

int (*cm_write_hook)(int) = (int (*)(int)) NULL;
int (*cm_preread_hook)(int,char*) = (int (*)(int, char*)) NULL;
void (*cm_postread_hook)(int,char*) = (void (*)(int, char*)) NULL;
void (*cm_last_postread_hook)() = (void (*)()) NULL;
static int CMact_on_data(CMConnection conn, char *buffer, int length);

extern void CMDataAvailable(trans, conn)
transport_entry trans;
CMConnection conn;
{
    CManager cm = conn->cm;
    int do_read = 1;
    int read_msg_count = 0;
    int read_byte_count = 0;
    int result;
    static int first = 1;
    static int read_ahead_msg_limit = 50;
    static int read_ahead_byte_limit = 1024*1024;
    char *buffer = NULL;
    int length;

    CManager_lock(cm);
    if (first) {
	char *tmp;
	first = 0;
	tmp = cercs_getenv("CMReadAheadMsgLimit");
	if (tmp != NULL) {
	    if (sscanf(tmp, "%d", &read_ahead_msg_limit) != 1) {
		printf("Read ahead msg limit \"%s\" not parsed\n", tmp);
	    }
	}
	tmp = cercs_getenv("CMReadAheadByteLimit");
	if (tmp != NULL) {
	    if (sscanf(tmp, "%d", &read_ahead_byte_limit) != 1) {
		printf("Read ahead byte limit \"%s\" not parsed\n", tmp);
	    }
	}
    }

 start_read:
    if (conn->closed) {
	CManager_unlock(cm);
	return;
    }
    if (conn->foreign_data_handler != NULL) {
	CManager_unlock(cm);
	((void(*)())conn->foreign_data_handler)(conn);
	return;
    }
    if ((trans->read_to_buffer_func) && (conn->partial_buffer == NULL)) {
	conn->partial_buffer = cm_get_data_buf(cm, 4);
	conn->buffer_full_point = 4;
	conn->buffer_data_end = 0;
	CMtrace_out(cm, CMLowLevelVerbose, "CMdata beginning new read, expect 4");
    } else {
	if (trans->read_to_buffer_func) {
	    CMtrace_out(cm, CMLowLevelVerbose, "CMdata continuing read, got %d, expecting %d", conn->buffer_data_end, conn->buffer_full_point);
	} else {
	    CMtrace_out(cm, CMLowLevelVerbose, "CMdata block read beginning");
	}
    }	

    /* read first 4 bytes */
    if (cm_preread_hook) {
	do_read = cm_preread_hook(conn->buffer_full_point - conn->buffer_data_end, conn->partial_buffer->buffer);
    }
    if (do_read) {
	if (trans->read_to_buffer_func) {
	    int len = conn->buffer_full_point - conn->buffer_data_end;
	    char *buf = (char*)conn->partial_buffer->buffer + conn->buffer_data_end;
	    int actual;
	    actual = trans->read_to_buffer_func(&CMstatic_trans_svcs, 
						conn->transport_data, 
						buf, len, 1);
	    if (actual == -1) {
		CMtrace_out(cm, CMLowLevelVerbose, 
			    "CMdata read failed, actual %d, failing connection %lx", actual, conn);
		CMConnection_failed(conn);
		CManager_unlock(cm);
		return;
	    }
	    conn->buffer_data_end += actual;
	    if (actual < len) {
		/* partial read */
		CMtrace_out(cm, CMLowLevelVerbose, 
			    "CMdata read partial, got %d", actual);
		CManager_unlock(cm);
		return;
	    }
	    buffer = conn->partial_buffer->buffer;
	    length = conn->buffer_data_end;
	} else {
	    buffer = trans->read_block_func(&CMstatic_trans_svcs, 
					    conn->transport_data,
					    &length);
	    if (length == 0) {
		CManager_unlock(cm);
		return;
	    }
	    if (buffer == NULL) {
		CMtrace_out(cm, CMLowLevelVerbose, "CMdata read_block failed, failing connection %lx", conn);
		CMConnection_failed(conn);
		CManager_unlock(cm);
		return;
	    }
	    CMtrace_out(cm, CMLowLevelVerbose, "CMdata read_block returned %d bytes of data", length);
	    conn->partial_buffer = NULL;
	    conn->buffer_data_end = 0;
	}
	if (cm_postread_hook) {
	    cm_postread_hook(conn->buffer_full_point - conn->buffer_data_end, 
			     conn->partial_buffer->buffer);
	}
    }
    result = CMact_on_data(conn, buffer, length);
    if (result != 0) {
	conn->buffer_full_point += result;
	cm_extend_data_buf(cm, conn->partial_buffer, conn->buffer_full_point);
	goto start_read;
    }
    read_msg_count++;
    read_byte_count += length;

    if (conn->partial_buffer) {
	cm_return_data_buf(conn->partial_buffer);
	conn->partial_buffer = NULL;
    }
    /* try read-ahead */
    if (cm->abort_read_ahead == 1) {
	cm->abort_read_ahead = 0;
	CMtrace_out(cm, CMDataVerbose, 
		    "CM - readahead not tried, aborted for condition signal");
	CManager_unlock(cm);
	return;
    }	
    if ((read_msg_count > read_ahead_msg_limit) || 
	(read_byte_count > read_ahead_byte_limit)) {
	CMtrace_out(cm, CMDataVerbose, 
		    "CM - readahead not tried, fairness, read %d msgs, %d bytes",
		    read_msg_count, read_byte_count);
	CManager_unlock(cm);
	return;
    } else {
	goto start_read;
    }
}

static int
CMact_on_data(CMConnection conn, char *buffer, int length){
    char *base = buffer;
    int byte_swap = 0;
    int get_attrs = 0;
    int padding = 0;
    int performance_msg = 0, event_msg = 0;
    int performance_func = 0;
    CMbuffer cm_decode_buf = NULL, cm_data_buf;
    attr_list attrs = NULL;
    int data_length, attr_length = 0, i, decoded_length;
    int header_len;
    int stone_id;
    char *decode_buffer = NULL, *data_buffer;
    IOFormat format;
    CManager cm = conn->cm;
    CMincoming_format_list cm_format = NULL;

    if (length < 4) {
	return 4 - length;
    }
    switch (*((int*)buffer)) {  /* assume 4-byte int */
    case 0x00444d43: /* \0DMC reversed byte order */
	byte_swap = 1;
    case 0x434d4400:  /* CMD\0 */
	break;
    case 0x00414d43: /* \0AMC reversed byte order */
	byte_swap = 1;
    case 0x434d4100:  /* CMA\0 */
	get_attrs = 1;
	break;
    case 0x00504d43: /* \0PMC reversed byte order */
	byte_swap = 1;
    case 0x434d5000:  /* CMP\0 */
	performance_msg = 1;
	break;
    case 0x004c4d43: /* \0LMC reversed byte order */
	byte_swap = 1;
    case 0x434d4c00:  /* CML\0 */
	event_msg = 1;
	get_attrs = 1;
	break;
    default:
	/*  non CM message */
	/*  lookup registered message prefixes and try to find handler */
	/*  otherwise give up */
	if (CMdo_non_CM_handler(conn, *(int*)buffer, buffer, length) == 0) {
	    printf("Unknown message on connection %lx, %x\n", (long) conn, *(int*)buffer);
	    CMConnection_failed(conn);
	}	    
	return 0;
    }

    if (get_attrs == 1) {
	if (!event_msg) {
	    header_len = 12;/* magic plus two 4-byte sizes (attrs + data) */
	    padding = 4;	/* maintain 8 byte alignment for data */
	    if (conn->buffer_data_end == 4) {
		cm_extend_data_buf(cm, conn->partial_buffer, 8);
		memcpy((char*)conn->partial_buffer->buffer + 4, 
		       conn->partial_buffer->buffer, 4); /* duplicate magic */
		conn->buffer_data_end = 8;
		conn->buffer_full_point = 8;
	    }
	} else {
	    header_len = 16;
	    padding = 0;
	}
    } else {
	header_len = 8; /* magic plus 4-byte size */
	padding = 0;	/* maintain 8 byte alignment for data */
    }

    if (length < header_len) {
	return header_len + padding - length;
    }
    base = buffer + 4 + padding; /* skip used data */
    if (byte_swap) {
	((char*)&data_length)[0] = base[3];
	((char*)&data_length)[1] = base[2];
	((char*)&data_length)[2] = base[1];
	((char*)&data_length)[3] = base[0];
	if (header_len != 8) {
	    ((char*)&attr_length)[0] = base[7];
	    ((char*)&attr_length)[1] = base[6];
	    ((char*)&attr_length)[2] = base[5];
	    ((char*)&attr_length)[3] = base[4];
	}
    } else {
	data_length = ((int *) base)[0];
	if (header_len != 8) {
	    attr_length = ((int *) base)[1];
	}
    }

    if (performance_msg) {
	performance_func = 0xff & (data_length >> 24);
	data_length &= 0xffffff;
	data_length -= 8;  /* subtract off header size */
    }
    if (event_msg) {
	if (byte_swap) {
	    ((char*)&stone_id)[0] = base[11];
	    ((char*)&stone_id)[1] = base[10];
	    ((char*)&stone_id)[2] = base[9];
	    ((char*)&stone_id)[3] = base[8];
	} else {
	    stone_id = ((int *) base)[2];
	}
    }	

    if (length < header_len + padding + data_length + attr_length) {
	return header_len + padding + data_length + attr_length - 
	    length;
    }
    base = buffer + header_len + padding;
    if (performance_msg) {
	CMdo_performance_response(conn, data_length, performance_func, byte_swap,
				  base);
        return 0;
    }
    data_buffer = base + attr_length;
    if (attr_length != 0) {
	attrs = CMdecode_attr_from_xmit(conn->cm, base);
	if (CMtrace_on(conn->cm, CMDataVerbose)) {
	    fprintf(stderr, "CM - Incoming read attributes -> ");
	    dump_attr_list(attrs);
	}
    }
    if (event_msg) {
	CMtrace_out(cm, CMDataVerbose, "CM - Receiving event message data len %d, attr len %d, stone_id %x\n",
		    data_length, attr_length, stone_id);
	if (attrs == NULL){
	    attrs = CMcreate_attr_list(cm);
	}
	set_int_attr(attrs, CM_EVENT_SIZE, data_length);

	cm_data_buf = conn->partial_buffer;
	conn->buffer_full_point = 0;
	conn->buffer_data_end = 0;
	conn->partial_buffer = NULL;
	internal_cm_network_submit(cm, cm_data_buf, attrs, conn, data_buffer,
				   data_length, stone_id);
	cm_return_data_buf(cm_data_buf);
	return 0;
    }
    format = get_format_app_IOcontext(conn->cm->IOcontext, data_buffer, conn);
    if (format == NULL) {
	fprintf(stderr, "invalid format in incoming buffer\n");
	return 0;
    }
    CMtrace_out(cm, CMDataVerbose, "CM - Receiving record of type %s", 
		name_of_IOformat(format));
    for (i=0; i< cm->in_format_count; i++) {
	if (cm->in_formats[i].format == format) {
	    cm_format = &cm->in_formats[i];
	}
    }
    if (cm_format == NULL) {
	cm_format = CMidentify_rollbackCMformat(cm, format);
	if(cm_format)
		CMcreate_conversion(cm, cm_format);
    }

    if ((cm_format == NULL) || (cm_format->handler == NULL)) {
	fprintf(stderr, "CM - No handler for incoming data of this version of format \"%s\"\n",
		name_of_IOformat(format));
	return 0;
    }
    assert(has_conversion_IOformat(format));

    if (decode_in_place_possible(format)) {
	if (!decode_in_place_IOcontext(cm->IOcontext, data_buffer, 
				       (void**) (long) &decode_buffer)) {
	    printf("Decode failed\n");
	    return 0;
	}
    } else {
	decoded_length = this_IOrecord_length(cm->IOcontext, data_buffer, data_length);
	cm_decode_buf = cm_get_data_buf(cm, decoded_length);
	decode_buffer = cm_decode_buf->buffer;
	decode_to_buffer_IOcontext(cm->IOcontext, data_buffer, decode_buffer);
	cm_return_data_buf(conn->partial_buffer);
	conn->partial_buffer = NULL;
    }
    if(cm_format->older_format) {
	if(!process_old_format_data(cm, cm_format, &decode_buffer, &cm_decode_buf)){
	    return 0;
	}
    }
    if (CMtrace_on(conn->cm, CMDataVerbose)) {
	static int dump_char_limit = 256;
	static int warned = 0;
	static int size_set = 0;
	int r;
	if (size_set == 0) {
	    char *size_str = cercs_getenv("CMDumpSize");
	    size_set++;
	    if (size_str != NULL) {
		dump_char_limit = atoi(size_str);
	    }
	}
	printf("CM - record contents are:\n  ");
	r = dump_limited_unencoded_IOrecord((IOFile)cm->IOcontext, format,
					    decode_buffer, dump_char_limit);
	if (r && !warned) {
	    printf("\n\n  ****  Warning **** CM record dump truncated\n");
	    printf("  To change size limits, set CMDumpSize environment variable.\n\n\n");
	    warned++;
	}
    }
    if (attrs == NULL) {
	attrs = CMcreate_attr_list(cm);
	CMattr_merge_lists(cm, attrs, conn->attrs);
    } else {
	CMattr_merge_lists(cm, attrs, conn->attrs);
    }

    /* 
     *  Handler may recurse, so clear these structures first
     */
    cm_data_buf = conn->partial_buffer;
    conn->buffer_full_point = 0;
    conn->buffer_data_end = 0;
    conn->partial_buffer = NULL;

    INT_CMConnection_add_reference(conn);
    CManager_unlock(cm);
    cm_format->handler(cm, conn, decode_buffer, cm_format->client_data,
		       attrs);
    CManager_lock(cm);
    INT_CMConnection_dereference(conn);
    if (cm_data_buf) {
	cm_return_data_buf(cm_data_buf);
    }
    if (cm_decode_buf) {
	cm_return_data_buf(cm_decode_buf);
	cm_decode_buf = NULL;
    }
    if (attrs) {
	INT_CMfree_attr_list(cm, attrs);
	attrs = NULL;
    }
    CMtrace_out(cm, CMDataVerbose, "CM - Finish processing - record of type %s", 
		name_of_IOformat(format));
    return 0;
}

void *
INT_CMtake_buffer(cm, data)
CManager cm;
void *data;
{
    if (cm_user_take_data_buf(cm, data) == 0) {
	/*
	 * someday we may get here if we have transports that allocate 
	 * their own buffers.  How to support?  Maybe do a copy and 
	 * return the copy...?
	 */
	fprintf(stderr, "Error: INT_CMtake_buffer called with record not associated with cm\n");
	return NULL;
    } else {
	return data;
    }
}

extern void
INT_CMreturn_buffer(cm, data)
CManager cm;
void *data;
{
    if (cm_user_return_data_buf(cm, data) == 0) {
	/*
	 * someday we may get here if we have transports that allocate 
	 * their own buffers.  How to support?  Maybe do a copy and 
	 * return the copy...?
	 */
	fprintf(stderr, "Error: INT_CMreturn_buffer called with record %lx not associated with cm\n", (long)data);
    }
}

extern int
INT_CMtry_return_buffer(cm, data)
CManager cm;
void *data;
{
    int ret;
    ret = cm_user_return_data_buf(cm, data);
    return ret;
}

void
INT_CMregister_handler(format, handler, client_data)
CMFormat format;
CMHandlerFunc handler;
void *client_data;
{
    format->handler = handler;
    format->client_data = client_data;
}

extern int
INT_CMwrite(conn, format, data)
CMConnection conn;
CMFormat format;
void *data;
{
    return INT_CMwrite_attr(conn, format, data, NULL);
}

extern void CMWriteQueuedData(trans, conn)
transport_entry trans;
CMConnection conn;
{
    attr_list attrs = NULL;  /* GSE fix */
    CMtrace_out(conn->cm, CMLowLevelVerbose, "CMWriteQueuedData, conn %lx", 
		(long)conn);
    CManager_lock(conn->cm);
    if (conn->queued_data.rem_header_len != 0) {
	struct _io_encode_vec tmp_vec[1];
	int actual;
	tmp_vec[0].iov_base = conn->queued_data.rem_header;
	tmp_vec[0].iov_len = conn->queued_data.rem_header_len;
	actual = trans->NBwritev_attr_func(&CMstatic_trans_svcs,
					   conn->transport_data,
					   &tmp_vec[0], 1,
					   attrs);
	if (actual < conn->queued_data.rem_header_len) {
	    conn->queued_data.rem_header_len -= actual;
	    memmove(&conn->queued_data.rem_header[0],
		    &conn->queued_data.rem_header[actual],
		    conn->queued_data.rem_header_len);
	    CManager_unlock(conn->cm);
	    return;
	}
    }
    if (conn->queued_data.rem_attr_len != 0) {
	struct _io_encode_vec tmp_vec[1];
	int actual;
	tmp_vec[0].iov_base = conn->queued_data.rem_attr_base;
	tmp_vec[0].iov_len = conn->queued_data.rem_attr_len;
	actual = trans->NBwritev_attr_func(&CMstatic_trans_svcs,
					   conn->transport_data,
					   &tmp_vec[0], 1,
					   attrs);
	if (actual < conn->queued_data.rem_attr_len) {
	    conn->queued_data.rem_attr_len -= actual;
	    conn->queued_data.rem_attr_base += actual;
	    CManager_unlock(conn->cm);
	    return;
	}
    }
    {
	int vec_count = 0;
	int length = 0;
	IOEncodeVector vec = conn->queued_data.vector_data;
	int actual = 0;
	
	while(vec[vec_count].iov_base != NULL) {
	    length += vec[vec_count].iov_len;
	    vec_count++;
	}
	actual = trans->NBwritev_attr_func(&CMstatic_trans_svcs,
					   conn->transport_data,
					   vec, vec_count,
					   attrs);
	if (actual < length) {
	    int i = 0;
	    CMtrace_out(conn->cm, CMLowLevelVerbose, "Continued partial pending write, %d bytes sent", actual);
	    while (actual > vec[i].iov_len) {
		actual -= vec[i].iov_len;
		i++;
	    }
	    vec[i].iov_len -= actual;
	    vec[i].iov_base = (char*)vec[i].iov_base + actual;
	    conn->queued_data.vector_data = &vec[i];
	    CManager_unlock(conn->cm);
	    return;
	}
    }
    conn->write_pending = 0;
    conn->trans->set_write_notify(conn->trans, &CMstatic_trans_svcs, 
				  conn->transport_data, 0);

    if(!CManager_locked(conn->cm)) {
	printf("Not LOCKED in write queued data!\n");
    }
    if (conn->write_callbacks) {
	int i = 0;
	CMtrace_out(conn->cm, CMLowLevelVerbose, "Completed pending write, doing notification");
	while (conn->write_callbacks[i].func != NULL) {
	    conn->write_callbacks[i].func(conn->cm, conn,
					     conn->write_callbacks[i].client_data);
	    conn->write_callbacks[i].func = NULL;
	    i++;
	}
    } else {
	CMtrace_out(conn->cm, CMLowLevelVerbose, "Completed pending write, No notifications");
    }
    CManager_unlock(conn->cm);
}


static void
queue_remaining_write(conn, tmp_vec, pbio_vec, vec_count, attrs, 
		      actual_bytes_written, attrs_present)
CMConnection conn;
IOEncodeVector tmp_vec;
IOEncodeVector pbio_vec;
int vec_count;
attr_list attrs;
int actual_bytes_written;
int attrs_present;
{
    int i = 0, j = 0;
    while (actual_bytes_written > tmp_vec[i].iov_len) {
	actual_bytes_written -= tmp_vec[i].iov_len;
	i++;
    }
    tmp_vec[i].iov_len -= actual_bytes_written;
    tmp_vec[i].iov_base = (char*)tmp_vec[i].iov_base + actual_bytes_written;

    if (i == 0) {
	/* didn't even write the 8 or 12 byte header */
	memcpy(&conn->queued_data.rem_header, tmp_vec[0].iov_base, 
	       tmp_vec[0].iov_len);
	conn->queued_data.rem_header_len = tmp_vec[0].iov_len;
    } else {
	conn->queued_data.rem_header_len = 0;
    }
    if (attrs_present && (i <= 1)) {
	/* got stuck in encoded attributes */
	conn->queued_data.rem_attr_base = tmp_vec[1].iov_base;
	conn->queued_data.rem_attr_len = tmp_vec[1].iov_len;
    } else {
	conn->queued_data.rem_attr_len = 0;
    }
    
    /* fixup pbio_vec */
    j = i - 1;  /* how far into the pbio vector are we? */
    if (attrs_present) j--;
    if (j >= 0) {
	pbio_vec[j].iov_len -= actual_bytes_written;
	pbio_vec[j].iov_base = (char*)pbio_vec[j].iov_base + 
	    actual_bytes_written;
    } else {
	j = 0;  /* nothing written */
    }

    /* 
     * copy application data (which had been left in place) into temporary
     * PBIO buffer as well.
     */
    conn->queued_data.vector_data = 
	copy_all_to_IOBuffer(conn->io_out_buffer, &pbio_vec[j]);
}


static void
add_pending_write_callback(CMConnection conn, CMCloseHandlerFunc handler, 
			   void* client_data)
{
    int count = 0;
    while (conn->write_callbacks && 
	   (conn->write_callbacks[count].func != NULL)) count++;
    if (count + 2 > conn->write_callback_len) {
	if (conn->write_callbacks == NULL) {
	    conn->write_callbacks = malloc(sizeof(conn->write_callbacks[0])*2);
	} else {
	    conn->write_callbacks = 
		realloc(conn->write_callbacks,
			sizeof(conn->write_callbacks[0])*(count+2));
	    conn->write_callback_len = count+1;
	}
    }
    conn->write_callbacks[count].func = handler;
    conn->write_callbacks[count].client_data = client_data;
    conn->write_callbacks[count+1].func = NULL;
}
    

static void
wake_pending_write(CManager cm, CMConnection conn, void *param)
{
    int cond = (long)param;
    INT_CMCondition_signal(cm, cond);
}

static void
wait_for_pending_write(conn)
CMConnection conn;
{
    CMControlList cl = conn->cm->control_list;
    if (!cl->has_thread) {
	/* single thread working, just poll network */
	CManager_unlock(conn->cm);
	while(conn->write_pending) {
	    CMcontrol_list_wait(cl);
	}
	CManager_lock(conn->cm);
    } else {
	/* other thread is handling the network wait for it to wake us up */
	while (conn->write_pending) {
	    int cond = INT_CMCondition_get(conn->cm, conn);
	    add_pending_write_callback(conn, wake_pending_write, 
				       (void*) (long)cond);
	    INT_CMCondition_wait(conn->cm, cond);
	}
    }	    
}
	

extern int
INT_CMwrite_attr(conn, format, data, attrs)
CMConnection conn;
CMFormat format;
void *data;
attr_list attrs;
{
    int no_attr_header[2] = {0x434d4400, 0};  /* CMD\0 in first entry */
    int attr_header[3] = {0x434d4100, 0, 0};  /* CMA\0 in first entry */
    IOEncodeVector vec;
    int length = 0, vec_count = 0, actual;
    int do_write = 1;
    void *encoded_attrs = NULL;
    int attrs_present = 0;

    /* ensure conn is open */
    if (conn->closed != 0) {
	CMtrace_out(conn->cm, CMDataVerbose, "Not writing data to closed connection");
	return 0;
    }
    if (conn->failed != 0) {
	CMtrace_out(conn->cm, CMDataVerbose, "Not writing data to failed connection");
	return 0;
    }
    if (conn->write_pending) {
	wait_for_pending_write(conn);
    }
    if (format->registration_pending) {
	CMcomplete_format_registration(format, 1);
    }
    if (format->format == NULL) {
	printf("Format registration has failed for format \"%s\" - write aborted\n",
	       format->format_name);
	return 0;
    }
    CMformat_preload(conn, format);

    if (CMtrace_on(conn->cm, CMDataVerbose)) {
	static int dump_char_limit = 256;
	static int warned = 0;
	static int size_set = 0;
	int r;
	if (size_set == 0) {
	    char *size_str = cercs_getenv("CMDumpSize");
	    size_set++;
	    if (size_str != NULL) {
		dump_char_limit = atoi(size_str);
	    }
	}
	printf("CM - Writing record of type %s\n",
	       name_of_IOformat(format->format));
	if (attrs != NULL) {
	    printf("CM - write attributes are:");
	    dump_attr_list(attrs);
	}
	printf("CM - record contents are:\n  ");
	r = dump_limited_unencoded_IOrecord((IOFile)format->IOsubcontext, 
					    format->format,
					    data, dump_char_limit);
	if (r && !warned) {
	    printf("\n\n  ****  Warning **** CM record dump truncated\n");
	    printf("  To change size limits, set CMDumpSize environment variable.\n\n\n");
	    warned++;
	}
    }

    /* encode data with CM context */
    vec = encode_IOcontext_vectorB(format->IOsubcontext, conn->io_out_buffer,
				   format->format, data);

    while(vec[vec_count].iov_base != NULL) {
	length += vec[vec_count].iov_len;
	vec_count++;
    }
    no_attr_header[1] = length;
    attr_header[1] = length;
    if (attrs != NULL) {
	attrs_present++;
	encoded_attrs = encode_attr_for_xmit(attrs, conn->attr_encode_buffer,
					     &attr_header[2]);
	attr_header[2] = (attr_header[2] +7) & -8;  /* round up to even 8 */
    }
    CMtrace_out(conn->cm, CMDataVerbose, "CM - Total write size is %d bytes data + %d bytes attrs\n", length, attr_header[2]);
    if (cm_write_hook != NULL) {
	do_write = cm_write_hook(length);
    }
    if (do_write) {
	struct _io_encode_vec static_vec[100];
	IOEncodeVector tmp_vec = &static_vec[0];
	int byte_count = length;/* sum lengths */
	if (vec_count >= sizeof(static_vec)/ sizeof(static_vec[0])) {
	    tmp_vec = INT_CMmalloc((vec_count+1) * sizeof(*tmp_vec));
	}
	if (attrs == NULL) {
	    tmp_vec[0].iov_base = &no_attr_header;
	    tmp_vec[0].iov_len = sizeof(no_attr_header);
	    memcpy(&tmp_vec[1], vec, sizeof(*tmp_vec) * vec_count);
	    vec_count++;
	    byte_count += sizeof(no_attr_header);
	    CMtrace_out(conn->cm, CMLowLevelVerbose, 
			"Writing %d vectors, total %d bytes in writev", 
			vec_count, byte_count);
	} else {
	    tmp_vec[0].iov_base = &attr_header;
	    tmp_vec[0].iov_len = sizeof(attr_header);
	    tmp_vec[1].iov_base = encoded_attrs;
	    tmp_vec[1].iov_len = attr_header[2];
	    memcpy(&tmp_vec[2], vec, sizeof(*tmp_vec) * vec_count);
	    byte_count += sizeof(attr_header) + attr_header[2];
	    vec_count += 2;
	    CMtrace_out(conn->cm, CMLowLevelVerbose, 
			"Writing %d vectors, total %d bytes (including attrs) in writev", 
			vec_count, byte_count);
	}
	if (conn->do_non_blocking_write == 1) {
	    int actual_bytes;
	    actual_bytes = 
		conn->trans->NBwritev_attr_func(&CMstatic_trans_svcs, 
						conn->transport_data, 
						tmp_vec, vec_count, attrs);
	    if (actual_bytes < byte_count) {
		/* copy remaining and send it later */
		if (actual_bytes < 0 ) actual_bytes = 0;
		queue_remaining_write(conn, tmp_vec, vec, vec_count, 
				      attrs, actual_bytes, attrs_present);
		conn->trans->set_write_notify(conn->trans, &CMstatic_trans_svcs, conn->transport_data, 1);
		conn->write_pending = 1;
		CMtrace_out(conn->cm, CMLowLevelVerbose, 
			    "Partial write, queued %d bytes",
			    byte_count - actual_bytes);
		return 1;
	    }
	    actual = vec_count;  /* set actual for success */
	} else if (conn->trans->writev_attr_func != NULL) {
	    actual = conn->trans->writev_attr_func(&CMstatic_trans_svcs, 
						   conn->transport_data, 
						   tmp_vec,
						   vec_count, attrs);
	} else {
	    actual = conn->trans->writev_func(&CMstatic_trans_svcs, 
					      conn->transport_data, 
					      tmp_vec, vec_count);
	}
	if (tmp_vec != &static_vec[0]) {
	    INT_CMfree(tmp_vec);
	}
	if (actual != vec_count) {
	    /* fail */
	    CMtrace_out(conn->cm, CMLowLevelVerbose, 
			"Writev failed, expected %d, only wrote %d", vec_count, actual);
	    return 0;
	}
    }
    CMtrace_out(conn->cm, CMLowLevelVerbose, "Writev success");
    return 1;
}

extern int
internal_write_event(conn, format, remote_path_id, path_len, event, attrs)
CMConnection conn;
CMFormat format;
void *remote_path_id;
int path_len;
event_item *event;
attr_list attrs;
{
    IOEncodeVector vec;
    struct _io_encode_vec preencoded_vec[2];
    int data_length = 0, vec_count = 0, actual, attr_len = 0;
    int do_write = 1;
    void *encoded_attrs = NULL;
    int attrs_present = 0;

    /* ensure conn is open */
    if (conn->closed != 0) {
	CMtrace_out(conn->cm, CMDataVerbose, "Not writing data to closed connection");
	return 0;
    }
    if (conn->failed != 0) {
	CMtrace_out(conn->cm, CMDataVerbose, "Not writing data to failed connection");
	return 0;
    }
    if (conn->write_pending) {
	wait_for_pending_write(conn);
    }
    if (format->registration_pending) {
	CMcomplete_format_registration(format, 1);
    }
    if (format->format == NULL) {
	printf("Format registration has failed for format \"%s\" - write aborted\n",
	       format->format_name);
	return 0;
    }
    CMformat_preload(conn, format);

    if (CMtrace_on(conn->cm, CMDataVerbose)) {
	static int dump_char_limit = 256;
	static int warned = 0;
	static int size_set = 0;
	int r;
	if (size_set == 0) {
	    char *size_str = cercs_getenv("CMDumpSize");
	    size_set++;
	    if (size_str != NULL) {
		dump_char_limit = atoi(size_str);
	    }
	}
	printf("CM - Writing record %lx of type %s\n", (long)event,
	       name_of_IOformat(format->format));
	if (attrs != NULL) {
	    printf("CM - write attributes are:");
	    dump_attr_list(attrs);
	} else {
	    printf("CM - write attrs NULL\n");
	}
	printf("CM - record contents ");
	if (event->decoded_event) {
	    printf("DECODED are:\n  ");
	    r = dump_limited_unencoded_IOrecord((IOFile)format->IOsubcontext, 
						format->format,
						event->decoded_event, dump_char_limit);
	} else {
	    printf("ENCODED are:\n  ");
	    r = dump_limited_encoded_IOrecord((IOFile)format->IOsubcontext, 
					      format->format,
					      event->encoded_event, dump_char_limit);
	}	    
	if (r && !warned) {
	    printf("\n\n  ****  Warning **** CM record dump truncated\n");
	    printf("  To change size limits, set CMDumpSize environment variable.\n\n\n");
	    warned++;
	}
    }

    if (!event->encoded_event) {
	/* encode data with CM context */
	vec = encode_IOcontext_vectorB(format->IOsubcontext, conn->io_out_buffer,
				       format->format, event->decoded_event);

	while(vec[vec_count].iov_base != NULL) {
	    data_length += vec[vec_count].iov_len;
	    vec_count++;
	}
    } else {
	vec = &preencoded_vec[0];
	preencoded_vec[0].iov_base = event->encoded_event;
	preencoded_vec[0].iov_len = event->event_len;
	preencoded_vec[1].iov_base = NULL;
	preencoded_vec[1].iov_len = 0;
	vec_count = 1;
	data_length = event->event_len;
    }
    if (attrs != NULL) {
	attrs_present++;
	encoded_attrs = encode_attr_for_xmit(attrs, conn->attr_encode_buffer,
					     &attr_len);
	attr_len = (attr_len +7) & -8;  /* round up to even 8 */
    }
    CMtrace_out(conn->cm, CMDataVerbose, "CM - Total write size is %d bytes data + %d bytes attrs\n", data_length, attr_len);
    if (cm_write_hook != NULL) {
	do_write = cm_write_hook(data_length);
    }
    if (do_write) {
	struct _io_encode_vec static_vec[100];
	IOEncodeVector tmp_vec = &static_vec[0];
	int byte_count = data_length;/* sum lengths */
	int header[4] = {0x434d4C00, 0, 0, 0};  /* CML\0 in first entry */
	if (vec_count >= sizeof(static_vec)/ sizeof(static_vec[0])) {
	    tmp_vec = INT_CMmalloc((vec_count+3) * sizeof(*tmp_vec));
	}
	header[1] = data_length;
	if (path_len != 4) {
	    header[0] = 0x434d4700;
	    header[3] = (path_len + 7) & -8;
	} else {
	    header[0] = 0x434d4C00;  /* 4 byte chan ID */
	    header[3] = *((int*)remote_path_id);
	}
	if (attrs == NULL) {
	    IOEncodeVector assign_vec = tmp_vec;
	    header[2] = 0;
	    if (path_len != 4) {
		tmp_vec[1].iov_base = remote_path_id;
		tmp_vec[1].iov_len = (path_len + 7) & -8;
		byte_count += tmp_vec[1].iov_len;
		assign_vec++;
	    }
	    tmp_vec[0].iov_base = &header;
	    tmp_vec[0].iov_len = sizeof(header);
	    memcpy(&assign_vec[1], vec, sizeof(*tmp_vec) * vec_count);
	    vec_count++;
	    byte_count += sizeof(header);
	    CMtrace_out(conn->cm, CMLowLevelVerbose, 
			"Writing %d vectors, total %d bytes in writev", 
			vec_count, byte_count);
	} else {
	    tmp_vec[0].iov_base = &header;
	    tmp_vec[0].iov_len = sizeof(header);
	    tmp_vec[1].iov_base = encoded_attrs;
	    header[2] = attr_len;
	    tmp_vec[1].iov_len = attr_len;
	    memcpy(&tmp_vec[2], vec, sizeof(*tmp_vec) * vec_count);
	    byte_count += sizeof(header) + header[2];
	    vec_count += 2;
	    CMtrace_out(conn->cm, CMLowLevelVerbose, 
			"Writing %d vectors, total %d bytes (including attrs) in writev", 
			vec_count, byte_count);
	}
	if (conn->do_non_blocking_write == 1) {
	    int actual_bytes;
	    actual_bytes = 
		conn->trans->NBwritev_attr_func(&CMstatic_trans_svcs, 
						conn->transport_data, 
						tmp_vec, vec_count, attrs);
	    if (actual_bytes < byte_count) {
		/* copy remaining and send it later */
		if (actual_bytes < 0 ) actual_bytes = 0;
		queue_remaining_write(conn, tmp_vec, vec, vec_count, 
				      attrs, actual_bytes, attrs_present);
		conn->trans->set_write_notify(conn->trans, &CMstatic_trans_svcs, conn->transport_data, 1);
		conn->write_pending = 1;
		CMtrace_out(conn->cm, CMLowLevelVerbose, 
			    "Partial write, queued %d bytes",
			    byte_count - actual_bytes);
		return 1;
	    }
	    actual = vec_count;  /* set actual for success */
	} else if (conn->trans->writev_attr_func != NULL) {
	    actual = conn->trans->writev_attr_func(&CMstatic_trans_svcs, 
						   conn->transport_data, 
						   tmp_vec,
						   vec_count, attrs);
	} else {
	    actual = conn->trans->writev_func(&CMstatic_trans_svcs, 
					      conn->transport_data, 
					      tmp_vec, vec_count);
	}
	if (tmp_vec != &static_vec[0]) {
	    INT_CMfree(tmp_vec);
	}
	if (actual != vec_count) {
	    /* fail */
	    CMtrace_out(conn->cm, CMLowLevelVerbose, 
			"Writev failed, expected %d, only wrote %d", vec_count, actual);
	    return 0;
	}
    }
    CMtrace_out(conn->cm, CMLowLevelVerbose, "Writev success");
    return 1;
}

static void
init_non_blocking_conn(CMConnection conn)
{
    /* default */
    conn->do_non_blocking_write = 0;

    if (conn->trans->NBwritev_attr_func == NULL) return;
    if (conn->trans->set_write_notify == NULL) return;

    /* only if we make it this far should we try non blocking writes */
    conn->do_non_blocking_write = 1;
}

extern int
INT_CMConnection_write_would_block(CMConnection conn)
{
    if (conn->do_non_blocking_write == -1) {
	init_non_blocking_conn(conn);
    }
    return conn->write_pending;
}

extern void
INT_CMregister_write_callback(CMConnection conn, CMWriteCallbackFunc handler,
			      void *client_data)
{
    if (conn->do_non_blocking_write == -1) {
	init_non_blocking_conn(conn);
    }
    add_pending_write_callback(conn, handler, client_data);

}

#define CMPerfProbe (unsigned int) 0xf0
#define CMPerfProbeResponse (unsigned int) 0xf1
#define CMPerfBandwidthInit (unsigned int) 0xf2
#define CMPerfBandwidthBody (unsigned int) 0xf3
#define CMPerfBandwidthEnd  (unsigned int) 0xf4
#define CMPerfBandwidthResult  (unsigned int) 0xf5

#define CMRegressivePerfBandwidthInit (unsigned int) 0xf6
#define CMRegressivePerfBandwidthBody (unsigned int) 0xf7
#define CMRegressivePerfBandwidthEnd  (unsigned int) 0xf8
#define CMRegressivePerfBandwidthResult  (unsigned int) 0xf9

void
CMdo_performance_response(conn, length, func, byte_swap, buffer)
CMConnection conn;
int length;
int func;
int byte_swap;
char *buffer;
{
    /* part of length was read already */
    length += 8;
    switch(func) {
    case CMPerfProbe:
	/* first half of latency probe arriving */
	{
	    struct _io_encode_vec tmp_vec[2];
	    int header[2];
	    int actual;
	    tmp_vec[0].iov_base = &header;
	    tmp_vec[0].iov_len = sizeof(header);
	    header[0] = 0x434d5000;  /* CMP\0 */
	    header[1] = length | (CMPerfProbeResponse << 24);
	    tmp_vec[1].iov_len = length - 8;
	    tmp_vec[1].iov_base = buffer;

	    CMtrace_out(conn->cm, CMLowLevelVerbose, "CM - responding to latency probe of %d bytes\n", length);
	    if (conn->trans->writev_attr_func != NULL) {
		actual = conn->trans->writev_attr_func(&CMstatic_trans_svcs, 
						       conn->transport_data, 
						       tmp_vec,
						       2, NULL);
	    } else {
		actual = conn->trans->writev_func(&CMstatic_trans_svcs, 
						  conn->transport_data, 
						  tmp_vec, 2);
	    }
	    if (actual != 2) {
		printf("perf write failed\n");
	    }
	}
	break;
    case CMPerfProbeResponse:
	/* last half of latency probe arriving, probe completion*/
	{
	    int cond = *(int*)buffer;  /* first entry should be condition */
	    chr_time *timer = INT_CMCondition_get_client_data(conn->cm, cond);
	    
	    CMtrace_out(conn->cm, CMLowLevelVerbose, "CM - latency probe response, condition %d\n", cond);
	    chr_timer_stop(timer);
	    INT_CMCondition_signal(conn->cm, cond);
	}
	break;
    case CMPerfBandwidthInit:
	/* initiate bandwidth measure */
	chr_timer_start(&conn->bandwidth_start_time);
	break;
    case CMPerfBandwidthBody:
	/* no activity for inner packets */
	break;
    case CMPerfBandwidthEnd:
	/* first half of latency probe arriving */
	{
	    int header[4];
	    int actual;
	    chr_timer_stop(&conn->bandwidth_start_time);

	    header[0] = 0x434d5000;  /* CMP\0 */
	    header[1] = sizeof(header) | (CMPerfBandwidthResult << 24);
	    header[2] = *(int*)buffer;  /* first entry should be condition */
	    header[3] = (int) 
		chr_time_to_microsecs(&conn->bandwidth_start_time);
	    CMtrace_out(conn->cm, CMLowLevelVerbose, "CM - Completing bandwidth probe - %d microseconds to receive\n", header[2]);

	    actual = conn->trans->write_func(&CMstatic_trans_svcs, 
					     conn->transport_data, 
					     &header, sizeof(header));
	    if (actual != sizeof(header)) {
		printf("perf write failed\n");
	    }
	}
	break;
    case CMPerfBandwidthResult:
	/* last half of latency probe arriving, probe completion*/
	{
	    int cond = *(int*)buffer;  /* first entry should be condition */
	    int time;
	    char *chr_time, tmp;
	    int *result_p = INT_CMCondition_get_client_data(conn->cm, cond);
	    
	    time = ((int*)buffer)[1];/* second entry should be condition */
	    if (byte_swap) {
		chr_time = (char*)&time;
		tmp = chr_time[0];
		chr_time[0] = chr_time[3];
		chr_time[3] = tmp;
		tmp = chr_time[1];
		chr_time[1] = chr_time[2];
		chr_time[2] = tmp;
	    }
	    *result_p = time;
	    CMtrace_out(conn->cm, CMLowLevelVerbose, "CM - bandwidth probe response, condition %d\n", cond);
	    INT_CMCondition_signal(conn->cm, cond);
	}
	break;
    case CMRegressivePerfBandwidthInit:
	/* initiate bandwidth measure */
        CMtrace_out(conn->cm, CMConnectionVerbose, "CM - received CM bw measure initiate");
	chr_timer_start(&conn->regressive_bandwidth_start_time);
	break;
    case CMRegressivePerfBandwidthBody:
	/* no activity for inner packets */
	break;
    case CMRegressivePerfBandwidthEnd:
	/* first half of latency probe arriving */
	{
	    int header[4];
	    int actual;
	    chr_timer_stop(&conn->regressive_bandwidth_start_time);

	    header[0] = 0x434d5000;  /* CMP\0 */
	    header[1] = sizeof(header) | (CMRegressivePerfBandwidthResult << 24);
	    header[2] = *(int*)buffer;  /* first entry should be condition */
	    header[3] = (int) 
		chr_time_to_microsecs(&conn->regressive_bandwidth_start_time);
            CMtrace_out(conn->cm, CMConnectionVerbose, "CM - received CM bw measure end, condition %d", *(int*)buffer);
	    CMtrace_out(conn->cm, CMLowLevelVerbose, "CM - Completing bandwidth probe - %d microseconds to receive\n", header[2]);

	    actual = conn->trans->write_func(&CMstatic_trans_svcs, 
					     conn->transport_data, 
					     &header, sizeof(header));
	    if (actual != sizeof(header)) {
		printf("perf write failed\n");
	    }
	}
	break;
    case CMRegressivePerfBandwidthResult:
	/* last half of latency probe arriving, probe completion*/
	{
	    int cond = *(int*)buffer;  /* first entry should be condition */
	    int time;
	    char *chr_time, tmp;
	    int *result_p = INT_CMCondition_get_client_data(conn->cm, cond);
	    
	    time = ((int*)buffer)[1];/* second entry should be condition */
	    if (byte_swap) {
		chr_time = (char*)&time;
		tmp = chr_time[0];
		chr_time[0] = chr_time[3];
		chr_time[3] = tmp;
		tmp = chr_time[1];
		chr_time[1] = chr_time[2];
		chr_time[2] = tmp;
	    }
	    *result_p = time;
	    CMtrace_out(conn->cm, CMLowLevelVerbose, "CM - bandwidth probe response, condition %d\n", cond);
	    INT_CMCondition_signal(conn->cm, cond);
	}
	break;
	
    }
}

static long
do_single_probe(conn, size, attrs)
CMConnection conn;
int size;
attr_list attrs;
{
    int cond;
    static int max_block_size = 0;
    static char *block = NULL;
    chr_time round_trip_time;
    int actual;

    cond = INT_CMCondition_get(conn->cm, conn);

    if (size < 12) size = 12;
    if (max_block_size == 0) {
	char *new_block = malloc(size);
	if (new_block == NULL) return -1;
	block = new_block;
	max_block_size = size;
	memset(block, 0xef, size);
    } else if (size > max_block_size) {
	char *new_block = realloc(block, size);
	if (new_block == NULL) return -1;
	block = new_block;
	max_block_size = size;
	memset(block, 0xef, size);
    }
    
    /* CMP\0 in first entry for CMPerformance message */
    ((int*)block)[0] = 0x434d5000;
    /* size in second entry, high byte gives CMPerf operation */
    ((int*)block)[1] = size | (CMPerfProbe<<24);
    ((int*)block)[2] = cond;   /* condition value in third entry */
    
    INT_CMCondition_set_client_data( conn->cm, cond, &round_trip_time);

    CMtrace_out(conn->cm, CMLowLevelVerbose, "CM - Initiating latency probe of %d bytes\n", size);
    chr_timer_start(&round_trip_time);

    actual = conn->trans->write_func(&CMstatic_trans_svcs, 
				     conn->transport_data, 
				     block, size);
    if (actual != size) return -1;

    INT_CMCondition_wait(conn->cm, cond);
    CMtrace_out(conn->cm, CMLowLevelVerbose, "CM - Completed latency probe - result %g microseconds\n", chr_time_to_microsecs(&round_trip_time));
    return (long) chr_time_to_microsecs(&round_trip_time);
}

/* return units are microseconds */
extern long
INT_CMprobe_latency(conn, size, attrs)
CMConnection conn;
int size;
attr_list attrs;
{
    int i;
    long result = 0;
    int repeat_count = 5;
    for (i=0; i < 2; i++) {
	(void) do_single_probe(conn, size, attrs);
    }
    for (i=0; i < repeat_count; i++) {
	result += do_single_probe(conn, size, attrs);
    }
    result /= repeat_count;
    return result;
}

/* return units are Kbytes/sec */
extern long
INT_CMprobe_bandwidth(conn, size, attrs)
CMConnection conn;
int size;
attr_list attrs;
{
    int i;
    int cond;
    int repeat_count = 100000/size;  /* send about 100K */
    static int max_block_size = 0;
    static char *block = NULL;
    int microsecs_to_receive;
    int actual;
    double bandwidth;

    cond = INT_CMCondition_get(conn->cm, conn);

    if (size < 16) size = 16;
    if (repeat_count == 0) repeat_count = 1;
    if (max_block_size == 0) {
	char *new_block = malloc(size);
	if (new_block == NULL) return -1;
	block = new_block;
	max_block_size = size;
	memset(block, 0xef, size);
    } else if (size > max_block_size) {
	char *new_block = realloc(block, size);
	if (new_block == NULL) return -1;
	block = new_block;
	max_block_size = size;
	memset(block, 0xef, size);
    }
    
    /* CMP\0 in first entry for CMPerformance message */
    ((int*)block)[0] = 0x434d5000;
    /* size in second entry, high byte gives CMPerf operation */
    ((int*)block)[1] = size | (CMPerfBandwidthInit<<24);
    ((int*)block)[2] = cond;   /* condition value in third entry */
    
    INT_CMCondition_set_client_data( conn->cm, cond, &microsecs_to_receive);

    CMtrace_out(conn->cm, CMLowLevelVerbose, "CM - Initiating bandwidth probe of %d bytes, %d messages\n", size, repeat_count);
    actual = conn->trans->write_func(&CMstatic_trans_svcs, 
				     conn->transport_data, 
				     block, size);
    if (actual != size) { 
	return -1;
    }

    ((int*)block)[1] = size | (CMPerfBandwidthBody<<24);
    for (i=0; i <(repeat_count-1); i++) {
	actual = conn->trans->write_func(&CMstatic_trans_svcs, 
					 conn->transport_data, 
					 block, size);
	if (actual != size) {
	    return -1;
	}
    }
    ((int*)block)[1] = size | (CMPerfBandwidthEnd <<24);
    actual = conn->trans->write_func(&CMstatic_trans_svcs, 
				     conn->transport_data, 
				     block, size);
    if (actual != size) {
	return -1;
    }

    INT_CMCondition_wait(conn->cm, cond);
    CMtrace_out(conn->cm, CMLowLevelVerbose, "CM - Completed bandwidth probe - result %d microseconds\n", microsecs_to_receive);
    bandwidth = ((double) size * (double)repeat_count * 1000.0) / 
	(double)microsecs_to_receive;
    CMtrace_out(conn->cm, CMLowLevelVerbose, "CM - Estimated bandwidth - %g Mbytes/sec\n", bandwidth / 1000.0);
    return (long) bandwidth;
}



/* matrix manipulation function for regression */

/***********************************************************
Name:     AtA
Passed:   **A, a matrix of dim m by n 
Returns:  **R, a matrix of dim m by p
***********************************************************/
static void 
AtA(double **A, int m, int n, double **R)
{
    int i, j, k;

    for (i = 0; i < n; i++)
	for (j = 0; j < n; j++) {
	    R[i][j] = 0.0;
	    for (k = 0; k < m; k++)
		R[i][j] += A[k][i] * A[k][j];
	}
}

/***********************************************************
Name:     Atf
Passed:   **A, a matrix of dim n by p
*f, a vector of dim n
Returns:  **R, a matrix of dim p by 1
***********************************************************/
static void 
Atf(double **A, double *f, int n, int p, double **R)
{
    int i, j, k;

    for (i = 0; i < p; i++)
	for (j = 0; j < 1; j++) {
	    R[i][j] = 0.0;
	    for (k = 0; k < n; k++)
		R[i][j] += A[k][i] * f[k];
	}
}
/*********************************************************************
 * dp_inv: inverse matrix A
 *              matrix A is replaced by its inverse
 *		returns ks, an integer
 *    A - square matrix of size n by n
 *    ks equals n when A is invertable
 * example usage:
	  if(dp_inv(N,u) != u){
            printf("Error inverting N\n");
            exit(-1);}
***********************************************************/
static int 
dp_inv(double **A, int n)
{
    int i, j, k, ks;

    ks = 0;
    for (k = 0; k < n; k++) {
	if (A[k][k] != 0) {
	    ks = ks + 1;
	    for (j = 0; j < n; j++) {
		if (j != k)
		    A[k][j] = A[k][j] / A[k][k];
	    }			/* end for j */
	    A[k][k] = 1.0 / A[k][k];
	}			/* end if */
	for (i = 0; i < n; i++) {
	    if (i != k) {
		for (j = 0; j < n; j++) {
		    if (j != k)
			A[i][j] = A[i][j] - A[i][k] * A[k][j];
		}		/* end for j */
		A[i][k] = -A[i][k] * A[k][k];
	    }			/* end if */
	}			/* end for i */
    }				/* end for k */
    return ks;
}				/* end MtxInverse() */


/***********************************************************
Name:     AtB
Passed:   **A, a matrix of dim m by n 
**B, a matrix of dim m by p
Returns:  **R, a matrix of dim n by p
***********************************************************/
static void 
AtB(double **A, double **B, int m, int n, int p, double **R)
{
    int i, j, k;

    for (i = 0; i < n; i++)
	for (j = 0; j < p; j++) {
	    R[i][j] = 0.0;
	    for (k = 0; k < m; k++)
		R[i][j] += A[k][i] * B[k][j];
	}
}

/************************************************************************/
/* Frees a double pointer array mtx[row][] */
/************************************************************************/
static void 
dub_dp_free(double **mtx, int row)
{
    int tmp_row;
    for (tmp_row = 0; tmp_row < row; tmp_row++)
	free(mtx[tmp_row]);
    free(mtx);
}

/************************************************************************/
/* Allocate memory for a double pointer array mtx[row][col], */
/* return double pointer  */
/************************************************************************/
static double **
dub_dp_mtxall(int row, int col)
{
    double **mtx;
    int tmp_row;

    /* Set Up Row of Pointers */
    mtx = (double **) malloc((unsigned) (row) * sizeof(double *));
    if (mtx == NULL)
	return NULL;

    /* Set Up Columns in Matrix */
    for (tmp_row = 0; tmp_row < row; tmp_row++) {
	mtx[tmp_row] = (double *) malloc((unsigned) (col) * sizeof(double));
	/* If could not Allocate All Free Memory */
	if (mtx[tmp_row] == NULL) {
	    dub_dp_free(mtx, row);
	    return NULL;
	}			/* Return Null Pointer */
    }
    return mtx;			/* Return Pointer to Matrix */
}


static double 
Regression(conn, inputmtx)
CMConnection conn; 
DelaySizeMtx *inputmtx;
{

    /* CMatrixUtils mtxutl; */
    double *Y, **X, **a;
    double **XtX, **XtY;
    int j;			/* , i, k; */
    /* char plinkid[9]; */
    int numofsizes;
    numofsizes = inputmtx->MsgNum;

    Y = (double *) malloc(sizeof(double) * inputmtx->MsgNum);
    X = dub_dp_mtxall(inputmtx->MsgNum, 2);
    a = dub_dp_mtxall(2, 1);
    XtX = dub_dp_mtxall(2, 2);
    XtY = dub_dp_mtxall(2, 1);

    /* regression estimate using least square method */
    for (j = 0; j < numofsizes; j++) {
	/* convert to one way delay in milliseconds */
	Y[j] = inputmtx->AveRTDelay[j] / 2 * 1000;
	/* convert to Kbytes */
	X[j][0] = inputmtx->MsgSize[j] / 1024;
	X[j][1] = 1;
    }
    AtA(X, numofsizes, 2, XtX);
    Atf(X, Y, numofsizes, 2, XtY);
    if (!dp_inv(XtX, 2)) {
	CMtrace_out(conn->cm, CMLowLevelVerbose, "CM - Regression()- Matrix XtX is not invertible\n");
	return -1;
    }
    AtB(XtX, XtY, 2, 2, 1, a);
    CMtrace_out(conn->cm, CMLowLevelVerbose,"CM - Regression():\nslope = %f (Bandwidth = %f Mbps), intercept = %f", a[0][0], 8 / a[0][0], a[1][0]);

    dub_dp_free(X, numofsizes);
    return 8 / a[0][0];
}

/* return units are Mbps */
extern double
INT_CMregressive_probe_bandwidth(conn, size, attrs)
CMConnection conn;
int size;
attr_list attrs;
{
    int i, j;
    int cond;
    int N = 9; /* send out N tcp streams with varied length*/
    int repeat_count = 3;
    static int max_block_size = 0;
    static char *block = NULL;
    int microsecs_to_receive;
    int actual;
    double bandwidth;
    int biggest_size;
    DelaySizeMtx dsm;
    double ave_delay=0.0, var_delay=0.0;
    double ave_size=0.0, var_size=0.0;
    double EXY=0.0;
    double covXY=0.0, cofXY=0.0;
    

    if (size < 16) size = 16;

    if (attrs != NULL) {
	get_int_attr(attrs, CM_REBWM_RLEN, &N);
	
	get_int_attr(attrs, CM_REBWM_REPT, &repeat_count);
	CMtrace_out(conn->cm, CMLowLevelVerbose, "INT_CMregressive_probe_bandwidth: get from attr, N: %d, repeat_count: %d\n", N, repeat_count);
	if(N<6) N=6;
	if(repeat_count<3) repeat_count=3;
    } else {
	N=9;
	repeat_count=3;
    }
    CMtrace_out(conn->cm, CMConnectionVerbose, "CM - INITIATE BW MEASURE on CONN %lx\n", conn);
    biggest_size=size*(N+1);

    if (max_block_size == 0) {
	char *new_block = malloc(biggest_size);
	if (new_block == NULL) return -1;
	block = new_block;
	max_block_size = biggest_size;
	memset(block, 0xef, biggest_size);
    } else if (biggest_size > max_block_size) {
	char *new_block = realloc(block, biggest_size);
	if (new_block == NULL) return -1;
	block = new_block;
	max_block_size = biggest_size;
	memset(block, 0xef, biggest_size);
    }

    /* CMP\0 in first entry for CMPerformance message */
    ((int*)block)[0] = 0x434d5000;
    /* size in second entry, high byte gives CMPerf operation */

    dsm.MsgNum=N;
    dsm.AveRTDelay=malloc(sizeof(double)*N);
    dsm.MsgSize=malloc(sizeof(int)*N);
    
    for(i =0; i<N; i++){
	cond = INT_CMCondition_get(conn->cm, conn);
	((int*)block)[2] = cond;   /* condition value in third entry */
	INT_CMCondition_set_client_data( conn->cm, cond, &microsecs_to_receive);

	/* size in second entry, high byte gives CMPerf operation */
	((int*)block)[1] = size | (CMRegressivePerfBandwidthInit<<24);
	((int*)block)[3] = size;

	CMtrace_out(conn->cm, CMLowLevelVerbose, "CM - Initiating bandwidth probe of %d bytes, %d messages\n", size, repeat_count);
	actual = conn->trans->write_func(&CMstatic_trans_svcs, 
					 conn->transport_data, 
					 block, size);
	if (actual != size) {
	    return -1;
	}

	((int*)block)[1] = size | (CMRegressivePerfBandwidthBody<<24);
	for (j=0; j <(repeat_count-1); j++) {
	    actual = conn->trans->write_func(&CMstatic_trans_svcs, 
					     conn->transport_data, 
					     block, size);
	    if (actual != size) {
		return -1;
	    }
	}
	((int*)block)[1] = size | (CMRegressivePerfBandwidthEnd <<24);
	actual = conn->trans->write_func(&CMstatic_trans_svcs, 
					 conn->transport_data, 
					 block, size);
	if (actual != size) {
	    return -1;
	}

	if (INT_CMCondition_wait(conn->cm, cond) == 0) {
	    return 0.0;
	}
	bandwidth = ((double) size * (double)repeat_count * 1000.0) / 
	    (double)microsecs_to_receive;

	dsm.AveRTDelay[i]=(double)microsecs_to_receive*2.0/(double)repeat_count/1000000.0;
	dsm.MsgSize[i]=size;
	/*change size for the next round of bw measurment. */
	size+=biggest_size/(N+1);
	ave_delay+=dsm.AveRTDelay[i]*1000.0;
	ave_size+=dsm.MsgSize[i];
	EXY+=dsm.AveRTDelay[i]*1000.0*dsm.MsgSize[i];

	CMtrace_out(conn->cm, CMLowLevelVerbose, "CM - Partial Estimated bandwidth- %f Mbps, size: %d, delay: %d, ave_delay+=%f\n", bandwidth*8.0 / 1000.0, size-biggest_size/(N+1), microsecs_to_receive, dsm.AveRTDelay[i]*1000.0);


    }
    bandwidth=Regression( conn, &dsm);

    ave_delay /= (double)N;
    ave_size /= (double)N;
    EXY /= (double)N;
    for(i=0;i<N; i++){
      var_delay += (dsm.AveRTDelay[i]*1000.0-ave_delay)*(dsm.AveRTDelay[i]*1000.0-ave_delay);
      var_size += (dsm.MsgSize[i]-ave_size)*(dsm.MsgSize[i]-ave_size);
    }
    var_delay /= (double)N;
    var_size /= (double)N;

    covXY=EXY-ave_delay*ave_size;
    cofXY=covXY/(sqrt(var_delay)*sqrt(var_size));
    
     CMtrace_out(conn->cm, CMLowLevelVerbose,"INT_CMregressive_probe_bandwidth: ave_delay: %f, ave_size: %f, var_delay: %f, var_size: %f, EXY: %f, covXY: %f, cofXY: %f\n", ave_delay, ave_size, var_delay, var_size, EXY, covXY, cofXY);
    
    CMtrace_out(conn->cm, CMLowLevelVerbose, "CM - Regressive Estimated bandwidth- %f Mbps, size: %d\n", bandwidth, size);

    free(dsm.AveRTDelay);
    free(dsm.MsgSize);
  
    if(cofXY<0.97 && cofXY>-0.97)
	if(bandwidth>0) bandwidth*=-1; /*if the result is not reliable, return negative bandwidth*/
  
    if (conn->attrs == NULL) conn->attrs = CMcreate_attr_list(conn->cm);

    {
	int ibandwidth, icof;
	ibandwidth = bandwidth * 1000;
	icof = cofXY * 1000;

	CMtrace_out(conn->cm, CMLowLevelVerbose, "CM - Regressive Setting measures to BW %d kbps, COF %d", ibandwidth, icof);
	set_int_attr(conn->attrs, CM_BW_MEASURED_VALUE, ibandwidth);
	set_int_attr(conn->attrs, CM_BW_MEASURED_COF, icof);
     
    }
    return  bandwidth;

}


static void CM_init_select ARGS((CMControlList cl, CManager cm));

extern void
INT_CM_fd_add_select(cm, fd, handler_func, param1, param2)
CManager cm;
int fd;
select_list_func handler_func;
void *param1;
void *param2;
{
    if (!cm->control_list->select_initialized) {
	CM_init_select(cm->control_list, cm);
    }
    cm->control_list->add_select(&CMstatic_trans_svcs,
				 &cm->control_list->select_data, fd,
				 handler_func, param1, param2);
}

extern void
CM_fd_write_select(cm, fd, handler_func, param1, param2)
CManager cm;
int fd;
select_list_func handler_func;
void *param1;
void *param2;
{
    if (!cm->control_list->select_initialized) {
	CM_init_select(cm->control_list, cm);
    }
    cm->control_list->write_select(&CMstatic_trans_svcs,
				   &cm->control_list->select_data, fd,
				   handler_func, param1, param2);
}

extern void
CM_fd_remove_select(cm, fd)
CManager cm;
int fd;
{
    if (!cm->control_list->select_initialized) {
	CM_init_select(cm->control_list, cm);
    }
    cm->control_list->remove_select(&CMstatic_trans_svcs,
				    &cm->control_list->select_data, fd);
}

extern CMTaskHandle
INT_CMadd_periodic(cm, period, func, client_data)
CManager cm;
long period;
CMPollFunc func;
void *client_data;
{
    CMTaskHandle handle = INT_CMmalloc(sizeof(*handle));
    if (!cm->control_list->select_initialized) {
	CM_init_select(cm->control_list, cm);
    }
    handle->cm = cm;
    handle->task = 
	cm->control_list->add_periodic(&CMstatic_trans_svcs,
				       &cm->control_list->select_data,
				       0, period, (select_list_func)func, 
				       (void*)cm, client_data);
    if (handle->task == NULL) {
	free(handle);
	return NULL;
    }
    return handle;
}

extern CMTaskHandle
INT_CMadd_periodic_task(cm, period_sec, period_usec, func, client_data)
CManager cm;
int period_sec;
int period_usec;
CMPollFunc func;
void *client_data;
{
    CMTaskHandle handle = INT_CMmalloc(sizeof(*handle));
    if (!cm->control_list->select_initialized) {
	CM_init_select(cm->control_list, cm);
    }
    handle->cm = cm;
    handle->task = 
	cm->control_list->add_periodic(&CMstatic_trans_svcs,
				       &cm->control_list->select_data,
				       period_sec, period_usec, 
				       (select_list_func)func, 
				       (void*)cm, client_data);
    if (handle->task == NULL) {
	free(handle);
	return NULL;
    }
    return handle;
}

extern void
INT_CMremove_periodic(handle)
CMTaskHandle handle;
{
    CManager cm = handle->cm;
    cm->control_list->remove_periodic(&CMstatic_trans_svcs,
				      &cm->control_list->select_data, 
				      handle->task);
    free(handle);
}

extern void
INT_CMremove_task(handle)
CMTaskHandle handle;
{
    CManager cm = handle->cm;
    cm->control_list->remove_periodic(&CMstatic_trans_svcs,
				      &cm->control_list->select_data, 
				      handle->task);
    free(handle);
}

extern CMTaskHandle
INT_CMadd_delayed_task(cm, delay_sec, delay_usec, func, client_data)
CManager cm;
int delay_sec;
int delay_usec;
CMPollFunc func;
void *client_data;
{
    CMTaskHandle handle = INT_CMmalloc(sizeof(*handle));
    if (!cm->control_list->select_initialized) {
	CM_init_select(cm->control_list, cm);
    }
    handle->cm = cm;
    handle->task = 
	cm->control_list->add_delayed_task(&CMstatic_trans_svcs,
					   &cm->control_list->select_data,
					   delay_sec, delay_usec,
					   (select_list_func)func, 
					   (void*)cm, client_data);
    if (handle->task == NULL) {
	free(handle);
	return NULL;
    }
    return handle;
}

typedef void (*SelectInitFunc)(CMtrans_services svc, CManager cm, void *client_data);

static void
select_shutdown(CManager cm, void *shutdown_funcv)
{
    SelectInitFunc shutdown_function = (SelectInitFunc)shutdown_funcv;
    CMtrace_out(cm, CMFreeVerbose, "calling select shutdown function");
    shutdown_function(&CMstatic_trans_svcs, cm, &cm->control_list->select_data);
}


static void
CM_init_select(cl, cm)
CMControlList cl;
CManager cm;
{
    CMPollFunc blocking_function, polling_function;
    SelectInitFunc init_function;
    SelectInitFunc shutdown_function;
    lt_dlhandle handle;	

    if (lt_dlinit() != 0) {
	fprintf (stderr, "error during initialization: %s\n", lt_dlerror());
	return;
    }
    lt_dladdsearchdir(CM_LIBRARY_BUILD_DIR);
    lt_dladdsearchdir(CM_LIBRARY_INSTALL_DIR);
    handle = lt_dlopen("libcmselect.la");
    if (!handle) {
	fprintf(stderr, "Failed to load required select dll.  Error \"%s\".\n",
		lt_dlerror());
	fprintf(stderr, "Search path includes '.', '%s', '%s' and any default search paths supported by ld.so\n", CM_LIBRARY_BUILD_DIR, 
		CM_LIBRARY_INSTALL_DIR);
	exit(1);
    }
    cl->add_select = (CMAddSelectFunc)lt_dlsym(handle, "add_select");  
    cl->remove_select = (CMRemoveSelectFunc)lt_dlsym(handle, "remove_select");  
    cl->write_select = (CMAddSelectFunc)lt_dlsym(handle, "write_select");  
    cl->add_periodic = (CMAddPeriodicFunc)lt_dlsym(handle, "add_periodic");  
    cl->add_delayed_task = 
	(CMAddPeriodicFunc)lt_dlsym(handle, "add_delayed_task");  
    cl->remove_periodic = (CMRemovePeriodicFunc)lt_dlsym(handle, "remove_periodic");  
    cl->wake_select = (CMWakeSelectFunc)lt_dlsym(handle, "wake_function");
    blocking_function = (CMPollFunc)lt_dlsym(handle, "blocking_function");
    polling_function = (CMPollFunc)lt_dlsym(handle, "polling_function");
    init_function = (SelectInitFunc)lt_dlsym(handle, "select_initialize");
    shutdown_function = (SelectInitFunc)lt_dlsym(handle, "select_shutdown");
    cl->stop_select = (CMWakeSelectFunc)lt_dlsym(handle, "select_stop");
    if ((cl->add_select == NULL) || (cl->remove_select == NULL) || 
	(blocking_function == NULL) || (cl->add_periodic == NULL) ||
	(cl->remove_periodic == NULL)) {
	printf("Select failed to load properly\n");
	exit(1);
    }
    init_function(&CMstatic_trans_svcs, cm, &cm->control_list->select_data);
    CMControlList_set_blocking_func(cl, cm, blocking_function, 
				    polling_function,
				    (void*)&(cl->select_data));
    cl->select_initialized = 1;
    CMtrace_out(cm, CMFreeVerbose, "CManager adding select shutdown function, %lx",(long)shutdown_function);
    internal_add_shutdown_task(cm, select_shutdown, (void*)shutdown_function);
}

static void
wake_function(cm, cond)
CManager cm;
void *cond;
{
    CManager_lock(cm);
    INT_CMCondition_signal(cm, (int)(long)cond);
    CManager_unlock(cm);
}

extern void
INT_CMsleep(cm, sec)
CManager cm;
int sec;
{
    int cond = INT_CMCondition_get(cm, NULL);
    CMTaskHandle handle = 
	INT_CMadd_delayed_task(cm, sec, 0, wake_function, (void*)(long)cond);
    INT_CMfree(handle);
    INT_CMCondition_wait(cm, cond);
}

extern void
INT_CMusleep(cm, usec)
CManager cm;
int usec;
{
    int cond = INT_CMCondition_get(cm, NULL);
    CMTaskHandle handle = 
	INT_CMadd_delayed_task(cm, 0, usec, wake_function, (void*)(long)cond);
    INT_CMfree(handle);
    INT_CMCondition_wait(cm, cond);
}

typedef struct foreign_handler_struct {
    int header;
    CMNonCMHandler handler;
} *handler_list;

static handler_list foreign_handler_list;
static int foreign_handler_count = 0;

extern void
INT_CMregister_non_CM_message_handler(header, handler)
int header;
CMNonCMHandler handler;
{
    if (foreign_handler_count > 0) {
	foreign_handler_list = INT_CMrealloc(foreign_handler_list, 
					 sizeof(foreign_handler_list[0]) * 
					 (foreign_handler_count + 1));
    } else {
	foreign_handler_list = INT_CMmalloc(sizeof(foreign_handler_list[0]));
    }
    foreign_handler_list[foreign_handler_count].header = header;
    foreign_handler_list[foreign_handler_count].handler = handler;
    foreign_handler_count++;
}

int
CMdo_non_CM_handler(CMConnection conn, int header, char *buffer, int length)
{
    int i = 0;
    while (i < foreign_handler_count) {
	if (foreign_handler_list[i].header == header) {
	    foreign_handler_list[i].handler(conn, conn->trans, buffer, 
					    length);
	    return 1;
	}
	i++;
    }
    return 0;
}

extern CMtrans_services
INT_CMget_static_trans_services ARGS(())
{
  return &CMstatic_trans_svcs;
}

extern void*
INT_CMget_transport_data (CMConnection conn)
{
  return conn->transport_data;
}

