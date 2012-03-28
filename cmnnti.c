/***** Includes *****/
#include "config.h"
#include <sys/types.h>

#include <stdio.h>
#include <errno.h>
#include <pthread.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#include <assert.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#ifdef HAVE_MEMORY_H
#include <memory.h>
#endif

#include <atl.h>
#include <cercs_env.h>
#include "evpath.h"
#include "cm_transport.h"
#include <nnti.h>

#if defined (__INTEL_COMPILER)
#  pragma warning (disable: 869)
#  pragma warning (disable: 310)
#  pragma warning (disable: 1418)
#  pragma warning (disable: 180)
#  pragma warning (disable: 177)
#  pragma warning (disable: 2259)
#  pragma warning (disable: 981)
#endif

extern attr_list
libcmnnti_LTX_non_blocking_listen(CManager cm, CMtrans_services svc, 
				  transport_entry trans, attr_list listen_info);

struct nnti_connection_data;

static atom_t CM_NNTI_PORT = -1;
static atom_t CM_NNTI_ADDR = -1;
static atom_t CM_IP_HOSTNAME = -1;
static atom_t CM_TRANSPORT = -1;

typedef struct nnti_transport_data {
    CManager cm;
    CMtrans_services svc;
    int socket_fd;
    int self_ip;
    int self_port;
    char *self_hostname;
    char* incoming;
    char* outbound;
    NNTI_buffer_t  mr_recvs;
    NNTI_transport_t trans_hdl;
    struct nnti_connection_data *connections;
} *nnti_transport_data_ptr;

#define MSGBUFSIZE 25600

typedef struct nnti_connection_data {
    char *peer_hostname;
    int nnti_port;
    CMbuffer read_buffer;
    int read_buf_len;
    nnti_transport_data_ptr ntd;
    CMConnection conn;
    attr_list attrs;
    struct nnti_connection_data *next;

    NNTI_peer_t peer_hdl;
    int size;
    uint64_t cksum;
    uint64_t raddr;
    int acks_offset;
    NNTI_buffer_t mr_send; // registered memory region to send a message to client
    char *send_buffer;
    NNTI_buffer_t res_addr;
    NNTI_buffer_t buf_addr;
} *nnti_conn_data_ptr;

#ifdef WSAEWOULDBLOCK
#define EWOULDBLOCK WSAEWOULDBLOCK
#define EAGAIN WSAEINPROGRESS
#define EINTR WSAEINTR
#define errno GetLastError()
#define read(fd, buf, len) recv(fd, buf, len, 0)
#define write(fd, buf, len) send(fd, buf, len, 0)
#endif

static nnti_conn_data_ptr
create_nnti_conn_data(svc)
CMtrans_services svc;
{
    nnti_conn_data_ptr nnti_conn_data =
	svc->malloc_func(sizeof(struct nnti_connection_data));
    nnti_conn_data->read_buffer = NULL;
    nnti_conn_data->nnti_port = -1;
    nnti_conn_data->peer_hostname = NULL;
    nnti_conn_data->next = NULL;
    return nnti_conn_data;
}

static void
add_connection(nnti_transport_data_ptr ntd, nnti_conn_data_ptr ncd)
{
    nnti_conn_data_ptr tmp = ntd->connections;
    ntd->connections = ncd;
    ncd->next = tmp;
}

static void
unlink_connection(nnti_transport_data_ptr ntd, nnti_conn_data_ptr ncd)
{
    if (ntd->connections == ncd) {
	ntd->connections = ncd->next;
	ncd->next = NULL;
    } else {
	nnti_conn_data_ptr tmp = ntd->connections;
	while (tmp != NULL) {
	    if (tmp->next == ncd) {
		tmp->next = ncd->next;
		ncd->next = NULL;
		return;
	    }
	}
	printf("Serious internal error, NNTI unlink_connection, connection not found\n");
    }
}


extern void
libcmnnti_LTX_shntdown_conn(svc, ncd)
CMtrans_services svc;
nnti_conn_data_ptr ncd;
{
    unlink_connection(ncd->ntd, ncd);
    free_attr_list(ncd->attrs);
    free(ncd);
}

#include "qual_hostname.c"

struct connect_message {
    short message_type;
    short port;
    uint32_t name_len;
    char name[1];
};


static int
initiate_nnti_conn(cm, svc, trans, attrs, nnti_conn_data, conn_attr_list)
CManager cm;
CMtrans_services svc;
transport_entry trans;
attr_list attrs;
nnti_conn_data_ptr nnti_conn_data;
attr_list conn_attr_list;
{
    int int_port_num;
    nnti_transport_data_ptr ntd = (nnti_transport_data_ptr) trans->trans_data;
    char *host_name;
    char server_url[256];
    struct connect_message *cmsg;

    if (!query_attr(attrs, CM_IP_HOSTNAME, /* type pointer */ NULL,
    /* value pointer */ (attr_value *)(long) & host_name)) {
	svc->trace_out(cm, "NNTI transport found no NNTI_HOST attribute");
	host_name = NULL;
    } else {
        svc->trace_out(cm, "NNTI transport connect to host %s", host_name);
    }
    if (host_name == NULL)
	return -1;

    if (!query_attr(attrs, CM_NNTI_PORT, /* type pointer */ NULL,
    /* value pointer */ (attr_value *) (long) &int_port_num)) {
	svc->trace_out(cm, "CMNNTI transport found no NNTI_PORT attribute");
	return -1;
    } else {
	svc->trace_out(cm, "CMNNTI transport connect to port %d", int_port_num);
    }

    sprintf(server_url, "ib://%s:%d/", host_name, int_port_num);

    if (ntd->self_port == -1) {
        libcmnnti_LTX_non_blocking_listen(cm, svc, trans, NULL);
    }

    int timeout = 500;
    ntd->svc->trace_out(trans->cm, "Connecting to URL \"%s\"", server_url);
    int err = NNTI_connect(&ntd->trans_hdl, server_url, timeout, 
			   &nnti_conn_data->peer_hdl);
    if (err != NNTI_OK) {
        fprintf (stderr, "Error: NNTI_connect() returned non-zero: %d\n", err);
        return 1;
    }

    /* register memory regions */
    char *req_buf = malloc (NNTI_REQUEST_BUFFER_SIZE);
    err = NNTI_register_memory(&ntd->trans_hdl, req_buf, NNTI_REQUEST_BUFFER_SIZE, 1,
                                NNTI_SEND_SRC, &nnti_conn_data->peer_hdl, &nnti_conn_data->mr_send);
    if (err != NNTI_OK) {
        fprintf (stderr, "Error: NNTI_register_memory(SEND_SRC) for client message returned non-zero: %d\n", err);
        return 1;
    }

    svc->trace_out(trans->cm, " register ACK memory...");

    if (err != NNTI_OK) {
        fprintf (stderr, "Error: NNTI_register_memory(RECV_DST) for server message returned non-zero: %d\n", err);
        return 1;
    }

    cmsg = (void*)req_buf;
    cmsg->message_type = 1;
    cmsg->port = ntd->self_port;
    cmsg->name_len = strlen(ntd->self_hostname);
    strcpy(&cmsg->name[0], ntd->self_hostname);
    err = NNTI_send(&nnti_conn_data->peer_hdl, &nnti_conn_data->mr_send, NULL);
    if (err != NNTI_OK) {
        fprintf (stderr, "Error: NNTI_send() returned non-zero: %d\n", err);
        return 1;
    }

    svc->trace_out(trans->cm, " NNTI_send() returned. Call wait... ");
    nnti_conn_data->nnti_port = int_port_num;
    nnti_conn_data->peer_hostname = strdup(host_name);
    nnti_conn_data->ntd = ntd;

    /* Wait for message to be sent */
    NNTI_status_t               status;
    err = NNTI_wait(&nnti_conn_data->mr_send, NNTI_SEND_SRC, timeout, &status);
    if (err != NNTI_OK) {
        fprintf (stderr, "Error: NNTI_wait() for sending returned non-zero: %d\n", err);
        return 1;
    }
    svc->trace_out(trans->cm, " NNTI_wait() of send request returned... ");


    svc->trace_out(cm, "--> Connection established");

    nnti_conn_data->nnti_port = int_port_num;
    nnti_conn_data->ntd = ntd;
    nnti_conn_data->send_buffer = req_buf;
    return 1;
}

/* 
 * Initiate a connection to a nnti group.
 */
extern CMConnection
libcmnnti_LTX_initiate_conn(cm, svc, trans, attrs)
CManager cm;
CMtrans_services svc;
transport_entry trans;
attr_list attrs;
{
    nnti_conn_data_ptr nnti_conn_data = create_nnti_conn_data(svc);
    attr_list conn_attr_list = create_attr_list();
    CMConnection conn;

    if (initiate_nnti_conn(cm, svc, trans, attrs, nnti_conn_data, conn_attr_list) != 1) {
	return NULL;
    }

    add_attr(conn_attr_list, CM_IP_HOSTNAME, Attr_String,
	     (attr_value) strdup(nnti_conn_data->peer_hostname));
    add_attr(conn_attr_list, CM_NNTI_PORT, Attr_Int4,
	     (attr_value) (long)nnti_conn_data->nnti_port);

    conn = svc->connection_create(trans, nnti_conn_data, conn_attr_list);
    add_connection(nnti_conn_data->ntd, nnti_conn_data);
    nnti_conn_data->conn = conn;
    nnti_conn_data->attrs = conn_attr_list;
    return conn;
}

/* 
 * Check to see that if we were to attempt to initiate a connection as
 * indicated by the attribute list, would we be connecting to ourselves?
 */
extern int
libcmnnti_LTX_self_check(cm, svc, trans, attrs)
CManager cm;
CMtrans_services svc;
transport_entry trans;
attr_list attrs;
{
    nnti_transport_data_ptr ntd = trans->trans_data;
    int int_port_num;
    char *host_name;
    char my_host_name[256];
    static int IP = 0;

    if (IP == 0) {
	IP = get_self_ip_addr(svc);
    }
    if (!query_attr(attrs, CM_IP_HOSTNAME, /* type pointer */ NULL,
    /* value pointer */ (attr_value *)(long) & host_name)) {
	svc->trace_out(cm, "CMself check NNTI transport found no IP_HOST attribute");
	host_name = NULL;
    }
    if (!query_attr(attrs, CM_NNTI_PORT, /* type pointer */ NULL,
    /* value pointer */ (attr_value *)(long) & int_port_num)) {
	svc->trace_out(cm, "CMself check NNTI transport found no NNTI_PORT attribute");
	return 0;
    }
    get_qual_hostname(my_host_name, sizeof(my_host_name), svc, NULL, NULL);

    if (host_name && (strcmp(host_name, my_host_name) != 0)) {
	svc->trace_out(cm, "CMself check - Hostnames don't match");
	return 0;
    }
    if (int_port_num != ntd->self_port) {
	svc->trace_out(cm, "CMself check - Ports don't match");
	return 0;
    }
    svc->trace_out(cm, "CMself check returning TRUE");
    return 1;
}

extern int
libcmnnti_LTX_connection_eq(cm, svc, trans, attrs, ncd)
CManager cm;
CMtrans_services svc;
transport_entry trans;
attr_list attrs;
nnti_conn_data_ptr ncd;
{

    int int_port_num;
    int requested_IP = -1;
    char *host_name = NULL;

    if (!query_attr(attrs, CM_IP_HOSTNAME, /* type pointer */ NULL,
    /* value pointer */ (attr_value *)(long) & host_name)) {
	svc->trace_out(cm, "NNTI transport found no NNTI_HOST attribute");
	host_name = NULL;
    } else {
        svc->trace_out(cm, "NNTI transport connect to host %s", host_name);
    }
    if (!query_attr(attrs, CM_NNTI_PORT, /* type pointer */ NULL,
    /* value pointer */ (attr_value *) (long) &int_port_num)) {
	svc->trace_out(cm, "Conn Eq CMNnti transport found no NNTI_PORT attribute");
	return 0;
    }
    svc->trace_out(cm, "CMNnti Conn_eq comparing host/ports %s/%d and %s/%d",
		   ncd->peer_hostname, ncd->nnti_port,
		   host_name, int_port_num);

    if ((ncd->nnti_port == int_port_num) &&
	(strcmp(ncd->peer_hostname, host_name) == 0)) {
	svc->trace_out(cm, "CMNnti Conn_eq returning TRUE");
	return 1;
    }
    svc->trace_out(cm, "CMNnti Conn_eq returning FALSE");
    return 0;
}


struct client_message {
  short message_type;
  unsigned short size;          // size of message payload
  char payload[1];
};

static int nnti_conn_count = 0;
typedef struct listen_struct {
  nnti_transport_data_ptr ntd;
  CMtrans_services svc;
  transport_entry trans;
} *listen_struct_p;

static int SHUTDOWN = 0;

int
listen_thread_func(void *vlsp)
{
    int timeout = 5000;
    listen_struct_p lsp = vlsp;
    nnti_transport_data_ptr ntd = lsp->ntd;
    CMtrans_services svc = lsp->svc;
    transport_entry trans = lsp->trans;
    int err;
    NNTI_status_t               wait_status;
  
    while (1) {
        attr_list conn_attr_list = NULL;
        err = NNTI_wait(&ntd->mr_recvs, NNTI_RECV_QUEUE, timeout, &wait_status);
	if (SHUTDOWN) return 0;
	if (err == NNTI_ETIMEDOUT) {
	    ntd->svc->trace_out(trans->cm, "NNTI_wait() on receiving result %d timed out...", err);
	    continue;
        } else if (err != NNTI_OK) {
            fprintf (stderr, "Error: NNTI_wait() on receiving result returned non-zero: %d\n", err);
            return 1;
        } else {
	    ntd->svc->trace_out(trans->cm, "  message arived: msg wait_status=%d size=%lu offs=%lu addr=%lu, offset was %ld",
		     wait_status.result, wait_status.length, wait_status.offset, wait_status.start+wait_status.offset, wait_status.offset);
        }


        if (wait_status.result == NNTI_OK) {
            struct connect_message *cm = (struct connect_message *)(wait_status.start+wait_status.offset);
	    
	    nnti_conn_data_ptr ncd = ntd->connections;
	    while (ncd != NULL) {
	        if (memcmp(&wait_status.src, &ncd->peer_hdl, sizeof(wait_status.src)) == 0) {
		    ntd->svc->trace_out(trans->cm, "NNTI data available on existing connection, from host %s", 
					ncd->peer_hostname);
		    break;
		}
		ncd = ncd->next;
	    }
	    if (cm->message_type == 1) {
	      ntd->svc->trace_out(trans->cm, "  client %s:%d is connecting",
		       cm->name, cm->port);
	      
	      assert(ncd == NULL);
	      ncd = create_nnti_conn_data(svc);
	      ncd->ntd = ntd;
	      ncd->peer_hdl = wait_status.src;
	      ncd->size = NNTI_REQUEST_BUFFER_SIZE;
	      ncd->raddr = NULL;
	      ncd->cksum = 0;
	      ncd->acks_offset=(nnti_conn_count++)*NNTI_REQUEST_BUFFER_SIZE;
	      //            ncd->buf_addr = cm->buf_addr;
	      //            ncd->res_addr = cm->res_addr;
	      
	      /* register memory region for sending acknowledgement to this client */
	      ntd->svc->trace_out(trans->cm, "   ID %d register small send buffer to client %d (offset=%d)...",
				  cm->port, nnti_conn_count-1, ncd->acks_offset);
	      
	      err = NNTI_register_memory (&ntd->trans_hdl,
					  &ntd->outbound[ncd->acks_offset],
					  NNTI_REQUEST_BUFFER_SIZE, 1, NNTI_SEND_SRC,
					  &ncd->peer_hdl, &ncd->mr_send);
	      if (err != NNTI_OK) {
                fprintf (stderr, "Error: NNTI_register_memory(NNTI_SEND_SRC) for server message returned non-zero: %d\n", err);
                return 1;
	      }
	      
	      conn_attr_list = create_attr_list();
	      ncd->conn = svc->connection_create(trans, ncd, conn_attr_list);
	      add_connection(ntd, ncd);
	      ncd->send_buffer = &ntd->outbound[ncd->acks_offset];
	    } else {
	      struct client_message *m = (struct client_message *)(wait_status.start+wait_status.offset);
	      ncd->read_buffer = ntd->svc->get_data_buffer(trans->cm, (int)m->size);

	      memcpy(&((char*)ncd->read_buffer->buffer)[0], &(m->payload[0]), m->size);

	      ncd->read_buf_len = m->size;
	      /* kick this upstairs */
	      trans->data_available(trans, ncd->conn);
	      ntd->svc->return_data_buffer(trans->cm, ncd->read_buffer);
	      ncd->read_buffer = NULL;
	    }
        }
    }
}


extern attr_list
libcmnnti_LTX_non_blocking_listen(cm, svc, trans, listen_info)
CManager cm;
CMtrans_services svc;
transport_entry trans;
attr_list listen_info;
{
    char url[256];
    char *hostname;
    NNTI_transport_t trans_hdl;
    nnti_transport_data_ptr ntd = trans->trans_data;
    int int_port_num = 0;
    attr_list listen_list;
    int nc = 100;
    int nclients = 100;
    char *last_colon;
    int err;

    NNTI_init(NNTI_TRANSPORT_IB, NULL, &trans_hdl);
    NNTI_get_url(&trans_hdl, url, sizeof(url));
    last_colon = rindex(url, ':');
    *last_colon = 0;
    hostname = (url + 3);
    while(hostname[0] == '/') hostname++;

    sscanf((last_colon + 1), "%d/", &int_port_num);

    listen_list = create_attr_list();
    add_attr(listen_list, CM_IP_HOSTNAME, Attr_String,
	     (attr_value) strdup(hostname));
    add_attr(listen_list, CM_NNTI_PORT, Attr_Int4,
	     (attr_value) (long) int_port_num);
    add_attr(listen_list, CM_TRANSPORT, Attr_String,
	     (attr_value) strdup("nnti"));

    /* at most 100 clients (REALLY NEED TO FIX) */
    nc = (nclients < 100 ? 100 : nclients);
    ntd->incoming  = malloc (nc * NNTI_REQUEST_BUFFER_SIZE);
    ntd->outbound  = malloc (nc * NNTI_REQUEST_BUFFER_SIZE);
    memset (ntd->incoming, 0, nc * NNTI_REQUEST_BUFFER_SIZE);
    memset (ntd->outbound, 0, nc * NNTI_REQUEST_BUFFER_SIZE);

    err = NNTI_register_memory(&trans_hdl, (char*)ntd->incoming, NNTI_REQUEST_BUFFER_SIZE, nc,
            NNTI_RECV_QUEUE, &trans_hdl.me, &ntd->mr_recvs);
    if (err != NNTI_OK) {
        fprintf (stderr, "Error: NNTI_register_memory(NNTI_RECV_DST) for client messages returned non-zero: %d\n", err);
        return NULL;
    } else {
	ntd->svc->trace_out(trans->cm, "Successfully registered memory on listen side incoming %p", ntd->incoming);
    }

    ntd->self_port = int_port_num;
    ntd->trans_hdl = trans_hdl;
    ntd->self_hostname = strdup(hostname);
    pthread_t new_thread = 0;
    listen_struct_p lsp = malloc(sizeof(*lsp));
    lsp->svc = svc;
    lsp->trans = trans;
    lsp->ntd = ntd;

    err = pthread_create(&new_thread, NULL, (void*(*)(void*))listen_thread_func, lsp);

    return listen_list;

}

#ifdef NEED_IOVEC_DEFINE
struct iovec {
    void *iov_base;
    int iov_len;
};

#endif

/* 
 *  This function will not be used unless there is no read_to_buffer function
 *  in the transport.  It is an example, meant to be copied in transports 
 *  that are more efficient if they allocate their own buffer space.
 */
extern void *
libcmnnti_LTX_read_block_func(svc, ncd, actual_len)
CMtrans_services svc;
nnti_conn_data_ptr ncd;
int *actual_len;
{
    *actual_len = ncd->read_buf_len;
    ncd->read_buf_len = 0;
    return ncd->read_buffer;
}

#ifndef IOV_MAX
/* this is not defined in some places where it should be.  Conservative. */
#define IOV_MAX 16
#endif

extern int
libcmnnti_LTX_writev_func(svc, ncd, iov, iovcnt, attrs)
CMtrans_services svc;
nnti_conn_data_ptr ncd;
struct iovec *iov;
int iovcnt;
attr_list attrs;
{
    int size= 0, i= 0;
    int err;
    int timeout = 1000;
    for(i=0; i<iovcnt; i++) size+= iov[i].iov_len;
    if (size + sizeof(struct client_message) < NNTI_REQUEST_BUFFER_SIZE) {
        struct client_message *cm = (void*)ncd -> send_buffer;
	cm->message_type = 2;
	cm->size = size;
	size = 0;
	for(i=0; i<iovcnt; i++) {
	    switch (i) {
	    case 0:
	      memcpy(&cm->payload[size], iov[i].iov_base, iov[i].iov_len);
	      break;
	    case 1:
	      break;
	      memcpy(&cm->payload[size], iov[i].iov_base, iov[i].iov_len);
	      break;
	    case 2:
	      memcpy(&cm->payload[size], iov[i].iov_base, iov[i].iov_len);
	      break;
	    case 3:
	      memcpy(&cm->payload[size], iov[i].iov_base, iov[i].iov_len);
	      break;
	    default:
	      memcpy(&cm->payload[size], iov[i].iov_base, iov[i].iov_len);
	      break;
	    }
	  size += iov[i].iov_len;
	}
    }
    err = NNTI_send(&ncd->peer_hdl, &ncd->mr_send, NULL);
    if (err != NNTI_OK) {
        fprintf (stderr, "Error: NNTI_send() returned non-zero: %d\n", err);
        return 1;
    }
    svc->trace_out(ncd->ntd->cm, "    NNTI_send() returned. Call wait... ");

    /* Wait for message to be sent */
    NNTI_status_t               status;
    err = NNTI_wait(&ncd->mr_send, NNTI_SEND_SRC, timeout, &status);
    //    if (err != NNTI_OK) {
    //        fprintf (stderr, "Error: NNTI_wait() for sending returned non-zero: %d\n", err);
    //        return 1;
    //    }
    svc->trace_out(ncd->ntd->cm, "    NNTI_wait() of send request returned... ");
	
    return iovcnt;
}

static void
free_nnti_data(CManager cm, void *ntdv)
{
    nnti_transport_data_ptr ntd = (nnti_transport_data_ptr) ntdv;
    CMtrans_services svc = ntd->svc;
    printf("NNTI SHUTDOWN\n");
    SHUTDOWN=1;
    svc->free_func(ntd);
}

extern void *
libcmnnti_LTX_initialize(cm, svc)
CManager cm;
CMtrans_services svc;
{
    static int atom_init = 0;

    nnti_transport_data_ptr nnti_data;
    svc->trace_out(cm, "Initialize CMNnti transport");
    if (atom_init == 0) {
	CM_NNTI_PORT = attr_atom_from_string("NNTI_PORT");
	CM_NNTI_ADDR = attr_atom_from_string("NNTI_ADDR");
	CM_IP_HOSTNAME = attr_atom_from_string("IP_HOST");
	CM_TRANSPORT = attr_atom_from_string("CM_TRANSPORT");
	atom_init++;
    }
    nnti_data = svc->malloc_func(sizeof(struct nnti_transport_data));
    nnti_data->cm = cm;
    nnti_data->svc = svc;
    nnti_data->socket_fd = -1;
    nnti_data->self_ip = 0;
    nnti_data->self_port = -1;
    nnti_data->connections = NULL;
    svc->add_shutdown_task(cm, free_nnti_data, (void *) nnti_data);
    return (void *) nnti_data;
}

extern void
libcmnnti_LTX_shutdown_conn(svc, ncd)
CMtrans_services svc;
struct nnti_connection_data* ncd;
{
    free(ncd->peer_hostname);
    free(ncd);
}

