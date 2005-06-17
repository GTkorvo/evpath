#include "config.h"

#include <atl.h>
#include <evpath.h>
#include <cm_internal.h>
#include <cm_transport.h>
#ifndef MODULE
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#else
#include "kernel/kcm.h"
#include "kernel/cm_kernel.h"
#include "kernel/library.h"
/* don't pull in sys/types if MODULE is defined */
#define _SYS_TYPES_H
#endif
#include "ltdl.h"
#include "assert.h"

extern struct CMtrans_services_s CMstatic_trans_svcs;
/*const lt_dlsymlist lt_preloaded_symbols[1] = { { 0, 0 } };*/

static transport_entry *global_transports = NULL;

static transport_entry
add_transport_to_cm(cm, transport)
CManager cm;
transport_entry transport;
{
    int num_trans;
    if (cm->transports == NULL) {
	cm->transports = INT_CMmalloc(sizeof(transport_entry) * 2);
	num_trans = 0;
    } else {
	num_trans = 0;
	while(cm->transports[num_trans] != NULL) num_trans++;
	cm->transports = INT_CMrealloc(cm->transports,
				   sizeof(transport_entry) * (num_trans +2));
    }
    cm->transports[num_trans] = INT_CMmalloc(sizeof(struct _transport_item));
    *(cm->transports[num_trans]) = *transport;
    cm->transports[num_trans + 1] = NULL;
    transport = cm->transports[num_trans];
    transport->cm = cm;
    return transport;
}

int
load_transport(cm, trans_name)
CManager cm;
const char *trans_name;
{
    transport_entry *trans_list = global_transports;
    transport_entry transport;
    int i = 0;
    char *libname;
    lt_dlhandle handle;	

    while ((trans_list != NULL) && (*trans_list != NULL)) {
	if (strcmp((*trans_list)->trans_name, trans_name) == 0) {
	    transport_entry trans = add_transport_to_cm(cm, *trans_list);
	    if (trans->transport_init) {
		trans->trans_data = 
		    trans->transport_init(cm, &CMstatic_trans_svcs);
	    }
	    return 1;
	}
	trans_list++;
	i++;
    }
    if (global_transports != NULL) {
      global_transports = INT_CMrealloc(global_transports, 
				    sizeof(global_transports) * (i + 2));
    } else {
        global_transports = INT_CMmalloc(sizeof(global_transports) * (i+2));
    }
    global_transports[i] = 
	transport = INT_CMmalloc(sizeof(struct _transport_item));
    global_transports[i+1] = NULL;

    libname = INT_CMmalloc(strlen(trans_name) + strlen("libcm") + strlen(".la") 
		       + 1);
    
    strcpy(libname, "libcm");
    strcat(libname, trans_name);
    strcat(libname, ".la");

    if (lt_dlinit() != 0) {
	fprintf (stderr, "error during initialization: %s\n", lt_dlerror());
	return 0;
    }

    lt_dladdsearchdir(CM_LIBRARY_BUILD_DIR);
    lt_dladdsearchdir(CM_LIBRARY_INSTALL_DIR);
    handle = lt_dlopen(libname);
    if (!handle) {
	fprintf(stderr, "Failed to load required '%s' dll.  Error \"%s\".\n",
		trans_name, lt_dlerror());
	fprintf(stderr, "Search path includes '.', '%s', '%s' and any default search paths supported by ld.so\n", CM_LIBRARY_BUILD_DIR, 
		CM_LIBRARY_INSTALL_DIR);

    } else {
	CMtrace_out(cm, CMTransportVerbose, "Loading local or staticly linked version of \"%s\" transport",
		    trans_name);
    }
    if (!handle) {
	return 0;
    }
    INT_CMfree(libname);
    CMtrace_out(cm, CMTransportVerbose, "Loaded transport.");
    transport->trans_name = strdup(trans_name);
    transport->cm = cm;
    transport->data_available = CMDataAvailable;  /* callback pointer */
    transport->write_possible = CMWriteQueuedData;  /* callback pointer */
    transport->transport_init = (CMTransport_func)
	lt_dlsym(handle, "initialize");  
    transport->listen = (CMTransport_listen_func)
	lt_dlsym(handle, "non_blocking_listen");  
    transport->initiate_conn = (CMConnection(*)())
	lt_dlsym(handle, "initiate_conn");  
    transport->self_check = (int(*)())lt_dlsym(handle, "self_check");
    transport->connection_eq = (int(*)())lt_dlsym(handle, "connection_eq");
    transport->shutdown_conn = (CMTransport_shutdown_conn_func)
	lt_dlsym(handle, "shutdown_conn");  
    transport->read_to_buffer_func = (CMTransport_read_to_buffer_func)
	lt_dlsym(handle, "read_to_buffer_func");  
    transport->read_block_func = (CMTransport_read_block_func)
	lt_dlsym(handle, "read_block_func");  
    transport->write_func = (CMTransport_write_func)
	lt_dlsym(handle, "write_func");  
    transport->writev_func = (CMTransport_writev_func)
	lt_dlsym(handle, "writev_func");  
    transport->writev_attr_func = (CMTransport_writev_attr_func)
	lt_dlsym(handle, "writev_attr_func");  
    transport->NBwritev_attr_func = (CMTransport_writev_attr_func)
	lt_dlsym(handle, "NBwritev_attr_func");  
    transport->set_write_notify = (CMTransport_set_write_notify_func)
	lt_dlsym(handle, "set_write_notify");
    CMtrace_out(cm, CMTransportVerbose, "Listen is %lx", transport->listen);
    if (transport->transport_init) {
	transport->trans_data = 
	    transport->transport_init(cm, &CMstatic_trans_svcs);
    }
    transport = add_transport_to_cm(cm, transport);
    return 1;
}
