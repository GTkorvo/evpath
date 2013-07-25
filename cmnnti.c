/***** Includes *****/
#include "config.h"
#include <sys/types.h>
#ifdef ENET_FOUND
#include <enet/enet.h>
#endif

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
#include <Trios_nnti.h>

#ifdef DF_SHM_FOUND
#include "df_shm.h"
#include "df_shm_queue.h"
#endif

#if defined (__INTEL_COMPILER)
#  pragma warning (disable: 869)
#  pragma warning (disable: 310)
#  pragma warning (disable: 1418)
#  pragma warning (disable: 180)
#  pragma warning (disable: 177)
#  pragma warning (disable: 2259)
#  pragma warning (disable: 981)
#endif

static void enet_free_func(void *packet);

typedef enum {MAP_EACH_MESSAGE_PULL, MAP_EACH_MESSAGE_PUSH,
	      PERSISTENT_MAPPED_BUFFER_PULL, PERSISTENT_MAPPED_BUFFER_PUSH,
	      LAST_PROTOCOL} messaging_protocols;

extern attr_list
libcmnnti_LTX_non_blocking_listen(CManager cm, CMtrans_services svc, 
				  transport_entry trans, attr_list listen_info);
struct nnti_connection_data;

static atom_t CM_PEER_IP = -1;
static atom_t CM_PEER_LISTEN_PORT = -1;
static atom_t CM_NNTI_PORT = -1;
static atom_t CM_NNTI_PARAMS = -1;
static atom_t CM_NNTI_ADDR = -1;
static atom_t CM_NNTI_ENET_CONTROL = -1;
static atom_t CM_IP_HOSTNAME = -1;
static atom_t CM_TRANSPORT = -1;
static atom_t CM_NNTI_TRANSPORT = -1;
static atom_t CM_ENET_PORT = -1;
static atom_t CM_ENET_ADDR = -1;
static atom_t CM_TRANSPORT_RELIABLE = -1;

#ifdef DF_SHM_FOUND
static atom_t CM_SHM_PID = -1;
static atom_t CM_SHM_NUM_SLOTS = -1;
static atom_t CM_SHM_MAX_PAYLOAD = -1;

/* Global default configuration parameters */
int shm_default_num_slots = 10;
int shm_default_max_payload_size = 1048576; /* 1M */
enum DF_SHM_METHOD shm_default_method = DF_SHM_METHOD_SYSV;

struct nnti_transport_data;
struct nnti_connection_data;
struct shm_connection_data;

typedef struct shm_transport_data {
    struct nnti_transport_data *ntd;
    pid_t my_pid;                  /* local process's pid */
    df_shm_method_t shm_method;    /* a handle to underlying shm method */
    pthread_t listen_thread;       /* polling thread for shm connections */
    int listen_thread_cmd;         /* 0: wait to start; 1: run; 2: stop */
    struct shm_connection_data *connections;
    pthread_mutex_t mutex;         /* protect concurrent access to connections */ 
    pthread_cond_t cond;          
} *shm_transport_data_ptr;

typedef struct shm_connection_data {
    struct nnti_connection_data *ncd;
    shm_transport_data_ptr shm_td;
    pid_t peer_pid;                /* peer process's pid */
    df_shm_region_t shm_region;    /* shm region for this connection */
    df_queue_ep_t send_ep;     /* sender endpoint handle for outgoing queue */
    df_queue_ep_t recv_ep;     /* receiver endpoint handle for incoming queue */
    struct shm_connection_data *next;
} *shm_conn_data_ptr;

#endif

char *NNTI_result_string[] = {
    "NNTI_OK",
    "NNTI_EIO",
    "NNTI_EMSGSIZE",
    "NNTI_ECANCELED",
    "NNTI_ETIMEDOUT",
    "NNTI_EINVAL",
    "NNTI_ENOMEM",
    "NNTI_ENOENT",
    "NNTI_ENOTSUP",
    "NNTI_EEXIST",
    "NNTI_EBADRPC",
    "NNTI_ENOTINIT",
    "NNTI_EPERM",
    "NNTI_EAGAIN"};
#define NNTI_ERROR_STRING(error) ((error == 0)? NNTI_result_string[error] : NNTI_result_string[error-1000])

/* requet queue for rdma pull scheduling */
struct pull_request;
struct pull_request_queue;
struct pull_sched_context;

typedef int (* nnti_pull_sched_func) (struct pull_request *, struct pull_sched_context *, void *callback_data);

typedef void (* nnti_pull_completion_func) (struct pull_request *, struct pull_sched_context *, void *callback_data);

typedef struct nnti_transport_data {
    CManager cm;
    CMtrans_services svc;
    int socket_fd;
    int self_ip;
    int self_port;
    attr_list listen_attrs;
    char *self_hostname;
    char* incoming;
    NNTI_buffer_t  mr_recvs;
    NNTI_transport_t trans_hdl;
    pthread_t listen_thread;
    int use_enet;
    int shutdown_listen_thread;
    struct nnti_connection_data *connections;
    int cache_maps;

    /* for scheduling */
    struct pull_request_queue *pull_req_queue;
    struct pull_request_queue *ongoing_req_queue;
    nnti_pull_sched_func nnti_pull_scheduler;
    void *nnti_pull_sched_data;
    nnti_pull_completion_func nnti_pull_completion;
    void *nnti_pull_completion_data;
    int buf_size;
    int buf_count;
    NNTI_buffer_t **buf_list;

#ifdef ENET_FOUND
    /* enet support */
    ENetHost *enet_server;
#endif
    int enet_listen_port;
    attr_list characteristics;

#ifdef DF_SHM_FOUND
    shm_transport_data_ptr shm_td;
#endif
} *nnti_transport_data_ptr;

typedef struct nnti_connection_data {
    char *peer_hostname;
    int nnti_port;
    char* nnti_params;
    CMbuffer read_buffer;
    int read_buf_len;
    nnti_transport_data_ptr ntd;
    CMConnection conn;
    attr_list attrs;
    struct nnti_connection_data *next;

    NNTI_peer_t peer_hdl;
    long size;
    uint64_t cksum;
    uint64_t raddr;
    int acks_offset;
    NNTI_buffer_t mr_send; // registered memory region to send a message to client
    char *send_buffer;
    NNTI_buffer_t res_addr;
    NNTI_buffer_t buf_addr;
    int piggyback_size_max;
    void *outgoing_mapped_region;
    long outgoing_mapped_size;
    NNTI_buffer_t outgoing_mapped_mr;
    NNTI_buffer_t incoming_mapped_region;
    long incoming_mapped_size;
    NNTI_buffer_t incoming_mapped_mr;

    /* enet support */
    char *remote_host;
    int remote_IP;
    int remote_contact_port;
    int use_enet;
#ifdef ENET_FOUND
    ENetPeer *peer;
    ENetPacket *packet;
#endif
    NNTI_buffer_t mr_pull;

#ifdef DF_SHM_FOUND
    shm_conn_data_ptr shm_cd;
#endif
} *nnti_conn_data_ptr;

#ifdef ENET_FOUND
extern void
enet_non_blocking_listen(CManager cm, CMtrans_services svc,
			 transport_entry trans, attr_list listen_info);
#endif

#ifdef DF_SHM_FOUND
static shm_conn_data_ptr
create_shm_conn_data(svc)
CMtrans_services svc;
{
    shm_conn_data_ptr shm_conn_data =
    svc->malloc_func(sizeof(struct shm_connection_data));
    memset(shm_conn_data, 0, sizeof(struct shm_connection_data));
    return shm_conn_data;
}

static void
shm_add_connection(shm_transport_data_ptr shm_td, shm_conn_data_ptr shm_cd)
{
    pthread_mutex_lock(&(shm_td->mutex));
    shm_conn_data_ptr tmp = shm_td->connections;
    shm_td->connections = shm_cd;
    shm_cd->next = tmp;
    pthread_cond_signal(&(shm_td->cond));
    pthread_mutex_unlock(&(shm_td->mutex));
}

static void
shm_unlink_connection(shm_transport_data_ptr shm_td, shm_conn_data_ptr shm_cd)
{
    pthread_mutex_lock(&(shm_td->mutex));
    if (shm_td->connections == shm_cd) {
        shm_td->connections = shm_cd->next;
        shm_cd->next = NULL;
    } 
    else {
        shm_conn_data_ptr tmp = shm_td->connections;
        while (tmp != NULL) {
            if (tmp->next == shm_cd) {
                tmp->next = shm_cd->next;
                shm_cd->next = NULL;
                pthread_mutex_unlock(&(shm_td->mutex));
                return;
            }
        }
        printf("Serious internal error, shm unlink_connection, connection not found\n");
    }
    pthread_cond_signal(&(shm_td->cond));
    pthread_mutex_unlock(&(shm_td->mutex));
}

/* Make a shared memory connection to a peer process at the local node. 
 * This function creates the shared memory region and initializes a queue pair in it.
 * Information of the created shared memory rregion will be shipped to peer side
 * via NNTI or ENET. The process should later call shm_wait_for_conn_ready() to wait
 * for peer process to attach to the shared memory region.
 */
int
initiate_shm_conn(cm, svc, trans, attrs, shm_conn_data, conn_attr_list)
CManager cm;
CMtrans_services svc;
transport_entry trans;
attr_list attrs;
shm_conn_data_ptr shm_conn_data;
attr_list conn_attr_list;
{
    int int_port_num;
    nnti_transport_data_ptr ntd = (nnti_transport_data_ptr) trans->trans_data;
    shm_transport_data_ptr shm_td = ntd->shm_td;
    uint32_t num_queue_slots;
    size_t max_payload_size;

    if (!query_attr(attrs, CM_SHM_NUM_SLOTS, /* type pointer */ NULL,
    /* value pointer */ (attr_value *) (long) &num_queue_slots)) {
        svc->trace_out(cm, "CMNNTI/SHM transport found no CM_SHM_NUM_SLOTS attribute");
        num_queue_slots = shm_default_num_slots;
    }
    svc->trace_out(cm, "CMNNTI/SHM transport: number of slots in queue %ld", num_queue_slots);

    if (!query_attr(attrs, CM_SHM_MAX_PAYLOAD, /* type pointer */ NULL,
    /* value pointer */ (attr_value *) (long) &max_payload_size)) {
        svc->trace_out(cm, "CMNNTI/SHM transport found no CM_SHM_MAX_PAYLOAD attribute");
        max_payload_size = shm_default_max_payload_size;
    }
    svc->trace_out(cm, "CMNNTI/SHM transport max payload size %ld", max_payload_size);

    /* create a shm region and two FIFO queues in the region
     * size of each shm region is calculated according to the queue size 
     */
    size_t queue_size = df_calculate_queue_size(num_queue_slots, max_payload_size);
    size_t region_size = 2 * queue_size + 3 * sizeof(uint64_t); /* meta-data at the beginning */
    if (region_size % PAGE_SIZE) {
        region_size += PAGE_SIZE - (region_size % PAGE_SIZE);
    }
    df_shm_region_t shm_region = df_create_shm_region(shm_td->shm_method, region_size, NULL);
    if (!shm_region) {
        svc->trace_out(cm, "CMNNTI/SHM: df_create_shm_region() failed");
        return -1;
    }

    /* the shm region is laid out in memory as follows:
     * starting addr:
     * creator pid (8 byte)
     * starting offset to the starting address of send queue (8 byte)
     * starting offset to the starting address of recv queue (8 byte)
     * peer pid (8 byte) (will be filled by peer process to notify connection establishment)
     * send queue (starting address cacheline aligned)
     * recv queue (starting address cacheline aligned)
     */
    char *ptr = (char *) shm_region->starting_addr; /* start of region, page-aligned */
    *((uint64_t *) ptr) = (uint64_t) shm_region->creator_id;
    ptr += sizeof(uint64_t);
    char *send_q_start = (char *) shm_region->starting_addr + 4 * sizeof(uint64_t);
    if ( (uint64_t)send_q_start % CACHE_LINE_SIZE) {
        send_q_start += CACHE_LINE_SIZE - ((uint64_t)send_q_start % CACHE_LINE_SIZE);
    }
    *((uint64_t *) ptr) = (uint64_t) (send_q_start - (char *) shm_region->starting_addr);
    ptr += sizeof(uint64_t);
    char *recv_q_start = send_q_start + queue_size;
    if ((uint64_t)recv_q_start % CACHE_LINE_SIZE) {
        recv_q_start += CACHE_LINE_SIZE - ((uint64_t)recv_q_start % CACHE_LINE_SIZE);
    }
    *((uint64_t *) ptr) = (uint64_t) (recv_q_start - (char *) shm_region->starting_addr);
    ptr += sizeof(uint64_t);
    *((uint64_t *) ptr) = 0; /* we are not goint to connect to process 0 anyway */

    /* send queue is for this process to send data to target process */
    df_queue_t send_q = df_create_queue (send_q_start, num_queue_slots, max_payload_size);
    df_queue_ep_t send_ep = df_get_queue_sender_ep(send_q);

    /* recv queue is for this process to receive data sent by target process */
    df_queue_t recv_q = df_create_queue (recv_q_start, num_queue_slots, max_payload_size);
    df_queue_ep_t recv_ep = df_get_queue_receiver_ep(recv_q);

    shm_conn_data->shm_region = shm_region;
    shm_conn_data->send_ep = send_ep;
    shm_conn_data->recv_ep = recv_ep;
    shm_conn_data->shm_td = shm_td;
    return 0;
}

/* wait for peer process to attach to shared memory region */
void shm_wait_for_conn_ready(shm_conn_data_ptr shm_cd) {
    uint64_t *peer_pid = ((uint64_t *) shm_cd->shm_region->starting_addr) + 3;
    while(*peer_pid == 0) {
        pthread_yield();
    }
    shm_cd->peer_pid = (pid_t) *peer_pid;
}

/* the process being connected to call this function to attach to shared memory
 * region and finish shared memory conneciton setup.
 */
shm_conn_data_ptr shm_passive_make_conn(ncd, shm_region_contact, contact_len)
nnti_conn_data_ptr ncd;
void *shm_region_contact;
int contact_len;
{
    shm_transport_data_ptr shm_td = (shm_transport_data_ptr) ncd->ntd->shm_td;
    shm_conn_data_ptr shm_cd;
    pid_t peer_pid = 0;

    /* locate and attach the shm region */
    df_shm_region_t shm_region = df_attach_shm_region (shm_td->shm_method, peer_pid,
        shm_region_contact, contact_len, NULL);
    if (!shm_region) {
        fprintf(stderr, "Cannot attach shm region. %s:%d\n", __FILE__, __LINE__);
        return NULL;
    }
 
    /* locate queues in shm region */
    uint64_t *sender_pid = (uint64_t *) shm_region->starting_addr;
    uint64_t *send_q_start = ((uint64_t *) shm_region->starting_addr + 1);
    uint64_t *recv_q_start = ((uint64_t *) shm_region->starting_addr + 2);
    uint64_t *peer_response = ((uint64_t *) shm_region->starting_addr + 3);

    /* setup local queue endpoints */
    /* Note: the receiver is receving end of send queue and sending end of recv queue */
    df_queue_t send_q = (df_queue_t)((char *)shm_region->starting_addr + *send_q_start);
    df_queue_ep_t recv_ep = df_get_queue_receiver_ep(send_q);
    df_queue_t recv_q = (df_queue_t)((char *)shm_region->starting_addr + *recv_q_start);
    df_queue_ep_t send_ep = df_get_queue_sender_ep(recv_q);

    shm_cd = create_shm_conn_data(shm_td->ntd->svc);
    shm_cd->peer_pid = (pid_t) *sender_pid;
    shm_cd->shm_region = shm_region;
    shm_cd->send_ep = send_ep;
    shm_cd->recv_ep = recv_ep;
    shm_cd->shm_td = shm_td;
    shm_cd->ncd = ncd;
    shm_add_connection(shm_td, shm_cd);

    /* notify the peer process that connection has been established */
    *peer_response = (uint64_t) shm_td->my_pid;

    return shm_cd;
}

void
shm_shutdown_conn(svc, shm_cd)
CMtrans_services svc;
shm_conn_data_ptr shm_cd;
{
    /* if this process is the creator of the shm region, destroy it; 
     * otherwise detach it */
    shm_unlink_connection(shm_cd->shm_td, shm_cd);
    df_destroy_shm_region(shm_cd->shm_region);
    svc->free_func(shm_cd);
}
#endif

static nnti_conn_data_ptr
create_nnti_conn_data(svc)
CMtrans_services svc;
{
    nnti_conn_data_ptr nnti_conn_data =
	svc->malloc_func(sizeof(struct nnti_connection_data));
    memset(nnti_conn_data, 0, sizeof(struct nnti_connection_data));
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


#include "qual_hostname.c"

enum {CMNNTI_CONNECT=1, CMNNTI_PIGGYBACK=2, CMNNTI_PULL_REQUEST=3, CMNNTI_PULL_COMPLETE=4, CMNNTI_PULL_SHM_REQUEST=5};
char *msg_type_name[] = {"NO MESSAGE", "CMNNTI_CONNECT", "CMNNTI_PIGGYBACK", "CMNNTI_PULL_REQUEST", "CMNNTI_PULL_COMPLETE", "CMNNTI_PULL_SHM_REQUEST"};

struct connect_message {
    short message_type;
    short nnti_port;
    short enet_port;
    uint32_t enet_ip;
#ifdef DF_SHM_FOUND
    uint32_t shm_contact_len;
#endif
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
    char *host_name, *nnti_transport, *params;
    char server_url[256];
    struct connect_message *cmsg;
    int use_shm = 0;

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

    if (!query_attr(attrs, CM_NNTI_PARAMS, /* type pointer */ NULL,
    /* value pointer */ (attr_value *) (long) &params)) {
	svc->trace_out(cm, "CMNNTI transport found no NNTI_PARAMS attribute");
	params = strdup("");
    } else {
	svc->trace_out(cm, "CMNNTI transport connect with params %s", params);
    }

    if (!get_string_attr(attrs, CM_NNTI_TRANSPORT, &nnti_transport)) {
	svc->trace_out(cm, "NNTI transport found no NNTI_TRANSPORT attribute");

	return -1;
    } else {
        svc->trace_out(cm, "NNTI transport connect using transport %s", nnti_transport);
    }

    sprintf(server_url, "%s://%s:%d/%s", nnti_transport, host_name, int_port_num, params);

    if (ntd->self_port == -1) {
        libcmnnti_LTX_non_blocking_listen(cm, svc, trans, NULL);
    }

#ifdef ENET_FOUND
    if (ntd->enet_server == NULL) {
	enet_non_blocking_listen(cm, svc, trans, NULL);
    }
#endif

#ifdef DF_SHM_FOUND
    int is_shm_connection = 0;
    /* make shm connection if the peer process and this process are on the same node */
    if ((use_shm) && (strcmp(host_name, ntd->self_hostname) == 0)) {
        svc->trace_out(cm, "Connecting to local peer process. \n");
   
        shm_conn_data_ptr shm_conn_data = create_shm_conn_data(svc);
        int rc = initiate_shm_conn(cm, svc, trans, attrs, shm_conn_data, conn_attr_list);
        if (rc != 0) {
            svc->trace_out(cm, "Error: cannot make shm connection. \n");
            return -1;
        } 
        nnti_conn_data->shm_cd = shm_conn_data;
        shm_conn_data->ncd = nnti_conn_data;
        is_shm_connection = 1;
    }
    else {
        nnti_conn_data->shm_cd = NULL;
    }
#endif

    int timeout = 500;
    ntd->svc->trace_out(trans->cm, "Connecting to URL \"%s\"", server_url);
    printf("Trying connetion to server_URL %s\n", server_url);
    int err = NNTI_connect(&ntd->trans_hdl, server_url, timeout, 
			   &nnti_conn_data->peer_hdl);
    if (err != NNTI_OK) {
        fprintf (stderr, "Error: NNTI_connect() returned non-zero: %d, %s\n", err, NNTI_ERROR_STRING(err));
        return 1;
    }

    /* register memory regions */
    char *req_buf = malloc (NNTI_REQUEST_BUFFER_SIZE);
    err = NNTI_register_memory(&ntd->trans_hdl, req_buf, NNTI_REQUEST_BUFFER_SIZE, 1,
                                NNTI_SEND_SRC, &nnti_conn_data->peer_hdl, &nnti_conn_data->mr_send);
    if (err != NNTI_OK) {
        fprintf (stderr, "Error: NNTI_register_memory(SEND_SRC) for client message returned non-zero: %d %s\n", err, NNTI_ERROR_STRING(err));
        return 1;
    }

    svc->trace_out(trans->cm, " register ACK memory...");

    if (err != NNTI_OK) {
        fprintf (stderr, "Error: NNTI_register_memory(RECV_DST) for server message returned non-zero: %d %s\n", err, NNTI_ERROR_STRING(err));
        return 1;
    }

    cmsg = (void*)req_buf;
    cmsg->message_type = CMNNTI_CONNECT;
    cmsg->nnti_port = ntd->self_port;
    cmsg->enet_port = ntd->enet_listen_port;
    cmsg->enet_ip = ntd->self_ip;
    cmsg->name_len = strlen(ntd->self_hostname);
    strcpy(&cmsg->name[0], ntd->self_hostname);

#ifdef DF_SHM_FOUND
    if (is_shm_connection) {
        /* append the shm region after the hostname */
        int contact_len;
        void * shm_region_contact = df_shm_region_contact_info (ntd->shm_td->shm_method,
            nnti_conn_data->shm_cd->shm_region, &contact_len);
        char *position = cmsg->name + cmsg->name_len + 2;  
        /* TODO: make sure it's within req_buf boundary */
        memcpy(position, shm_region_contact, contact_len);
        free(shm_region_contact);
        cmsg->shm_contact_len = contact_len;
    }
    else {
        cmsg->shm_contact_len = 0;
    }
#endif

    svc->trace_out(cm, "CMNNTI initiate sending connect message to remote host");
    err = NNTI_send(&nnti_conn_data->peer_hdl, &nnti_conn_data->mr_send, NULL);
    if (err != NNTI_OK) {
        fprintf (stderr, "Error: NNTI_send() returned non-zero: %d %s\n", err, NNTI_ERROR_STRING(err));
        return 1;
    }

    svc->trace_out(trans->cm, " NNTI_send() returned. Call wait... ");
    nnti_conn_data->nnti_port = int_port_num;
    nnti_conn_data->peer_hostname = strdup(host_name);
    nnti_conn_data->ntd = ntd;

    /* Wait for message to be sent */
    NNTI_status_t               status;
    timeout = 500;
 again:
    err = NNTI_wait(&nnti_conn_data->mr_send, NNTI_SEND_SRC, timeout, &status);
    if ((err == NNTI_ETIMEDOUT) || (err == NNTI_EAGAIN)) {
	if (nnti_conn_data->ntd->shutdown_listen_thread) return 0;
	timeout *=2;
	goto again;
    }
    if (err != NNTI_OK) {
        fprintf (stderr, "Error: NNTI_wait() for sending returned non-zero: %d %s\n", err, NNTI_ERROR_STRING(err));
        return 1;
    }
    svc->trace_out(trans->cm, " NNTI_wait() of send CONNECT request returned... ");

#ifdef DF_SHM_FOUND
    if (is_shm_connection) {
        /* wait for peer process to attach to shared memory region */
        shm_wait_for_conn_ready(nnti_conn_data->shm_cd);
        shm_add_connection(ntd->shm_td, nnti_conn_data->shm_cd);
    }
#endif

    svc->trace_out(cm, "--> NNTI Connection established");

    nnti_conn_data->nnti_port = int_port_num;
    nnti_conn_data->nnti_params = params;
    nnti_conn_data->ntd = ntd;
    nnti_conn_data->send_buffer = req_buf;
    return 1;
}

#ifdef ENET_FOUND
static void nnti_enet_service_network(CManager cm, void *void_trans);

extern void
enet_non_blocking_listen(CManager cm, CMtrans_services svc,
			 transport_entry trans, attr_list listen_info)
{
    nnti_transport_data_ptr ntd = (nnti_transport_data_ptr) trans->trans_data;
    ENetAddress address;
    ENetHost * server;


    u_short port_num = 0;

    svc->trace_out(cm, "CMnnti begin enet listen\n");

    address.host = ENET_HOST_ANY;

    if (ntd->enet_server != NULL) {
	/* we're already listening */
	return;
    }
    long seedval = time(NULL) + getpid();
    /* port num is free.  Constrain to range 26000 : 26100 */
    int low_bound = 26000;
    int high_bound = 26100;
    int size = high_bound - low_bound;
    int tries = 10;
    srand48(seedval);
    while (tries > 0) {
	int target = low_bound + size * drand48();
	address.port = target;
	svc->trace_out(cm, "CMnnti trying to bind enet port %d", target);
	
	server = enet_host_create (& address /* the address to bind the server host to */, 
				   0     /* allow dynamic clients (This is supported by the GaTech mod ENET only) */,
				   1      /* allow up to 2 channels to be used, 0 and 1 */,
				   0      /* assume any amount of incoming bandwidth */,
				   0      /* assume any amount of outgoing bandwidth */);
	tries--;
	if (server != NULL) tries = 0;
	if (tries == 5) {
	    /* try reseeding in case we're in sync with another process */
	    srand48(time(NULL) + getpid());
	}
    }
    if (server == NULL) {
	fprintf(stderr, "Failed after 5 attempts to bind to a random port.  Lots of undead servers on this host?\n");
	return;
    }
    ntd->enet_server = server;
    ntd->enet_listen_port = address.port;

    svc->fd_add_select(cm, enet_host_get_sock_fd (server), 
		       (select_list_func) nnti_enet_service_network, (void*)cm, (void*)trans);
    return;
}

static int
initiate_enet_link(CManager cm, CMtrans_services svc, transport_entry trans,
		   nnti_conn_data_ptr nnti_conn_data, char *host_name, int int_port_num)
{
    nnti_transport_data_ptr ntd = (nnti_transport_data_ptr) trans->trans_data;
    ENetAddress address;
    ENetEvent event;
    ENetPeer *peer;
    struct in_addr sin_addr;
    enet_address_set_host (& address, host_name);
    sin_addr.s_addr = address.host;

    svc->trace_out(cm, "Attempting ENET RUDP connection, host=\"%s\", IP = %s, port %d",
		   host_name == 0 ? "(unknown)" : host_name, 
		   inet_ntoa(sin_addr),
		   int_port_num);

    enet_address_set_host (& address, host_name);
    address.port = (unsigned short) int_port_num;

    if (ntd->enet_server == NULL) {
	enet_non_blocking_listen(cm, svc, trans, NULL);
    }

    /* Initiate the connection, allocating the two channels 0 and 1. */
    peer = enet_host_connect (ntd->enet_server, & address, 1, 0);    
    
    if (peer == NULL) {
       fprintf (stderr, 
                "No available peers for initiating an ENet connection.\n");
       exit (EXIT_FAILURE);
    }
    
    /* Wait up to 5 seconds for the connection attempt to succeed. */
    if (enet_host_service (ntd->enet_server, & event, 5000) > 0 &&
        event.type == ENET_EVENT_TYPE_CONNECT) {
	svc->trace_out(cm, "Connection to %s:%d succeeded.\n", inet_ntoa(sin_addr), address.port);

    } else {
        /* Either the 5 seconds are up or a disconnect event was */
        /* received. Reset the peer in the event the 5 seconds   */
        /* had run out without any significant event.            */
        enet_peer_reset (peer);

        printf ("Connection to %s:%d failed.", inet_ntoa(sin_addr), address.port);
	return 0;
    }

    svc->trace_out(cm, "--> Enet Connection established");
    nnti_conn_data->remote_host = host_name == NULL ? NULL : strdup(host_name);
    nnti_conn_data->remote_IP = address.host;
    nnti_conn_data->remote_contact_port = int_port_num;
    nnti_conn_data->ntd = ntd;
    nnti_conn_data->peer = peer;
    peer->data = nnti_conn_data;
    return 1;
}
#endif

#ifdef ENET_FOUND
static int
initiate_enet_conn(CManager cm, CMtrans_services svc, transport_entry trans,
	      attr_list attrs, nnti_conn_data_ptr nnti_conn_data,
	      attr_list conn_attr_list)
{
    int int_port_num;
    nnti_transport_data_ptr ntd = (nnti_transport_data_ptr) trans->trans_data;
    char *host_name;
    static int host_ip = 0;
    struct in_addr sin_addr;
    (void)conn_attr_list;

    if (!query_attr(attrs, CM_IP_HOSTNAME, /* type pointer */ NULL,
    /* value pointer */ (attr_value *)(long) & host_name)) {
	svc->trace_out(cm, "Cmnnti/Enet transport found no CM_IP_HOSTNAME attribute");
	host_name = NULL;
    } else {
        svc->trace_out(cm, "Cmnnti/Enet transport connect to host %s", host_name);
    }
    if (!query_attr(attrs, CM_ENET_ADDR, /* type pointer */ NULL,
    /* value pointer */ (attr_value *)(long) & host_ip)) {
	svc->trace_out(cm, "Cmnnti/Enet transport found no CM_ENET_ADDR attribute");
	/* wasn't there */
	host_ip = 0;
    } else {
        svc->trace_out(cm, "Cmnnti/Enet transport connect to host_IP %lx", host_ip);
    }
    if ((host_name == NULL) && (host_ip == 0))
	return -1;

    if (!query_attr(attrs, CM_ENET_PORT, /* type pointer */ NULL,
    /* value pointer */ (attr_value *)(long) & int_port_num)) {
	svc->trace_out(cm, "Cmnnti/Enet transport found no CM_ENET_PORT attribute");
	return -1;
    } else {
        svc->trace_out(cm, "Cmnnti/Enet transport connect to port %d", int_port_num);
    }

    return initiate_enet_link(cm, svc, trans, nnti_conn_data, host_name, int_port_num);
}
#endif

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
    int enet_conn_status;

    if (initiate_nnti_conn(cm, svc, trans, attrs, nnti_conn_data, conn_attr_list) != 1) {
	return NULL;
    }

#ifdef ENET_FOUND
    sleep(1);

    enet_conn_status = initiate_enet_conn(cm, svc, trans, attrs, nnti_conn_data, conn_attr_list);
    switch (enet_conn_status) {
    case -1:
      svc->trace_out(cm, "NO ENET Contact info provided.");
      nnti_conn_data->use_enet = 0;
      break;
    case 0:
      svc->trace_out(cm, "ENET Connection failed.");
      return NULL;
    default:
      nnti_conn_data->use_enet = 1;
    }
#endif

    add_attr(conn_attr_list, CM_IP_HOSTNAME, Attr_String,
	     (attr_value) strdup(nnti_conn_data->peer_hostname));
    add_attr(conn_attr_list, CM_NNTI_PORT, Attr_Int4,
	     (attr_value) (long)nnti_conn_data->nnti_port);
    add_attr(conn_attr_list, CM_NNTI_PARAMS, Attr_String,
	     (attr_value) (long)nnti_conn_data->nnti_params);

    nnti_conn_data->piggyback_size_max = NNTI_REQUEST_BUFFER_SIZE;
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


struct piggyback {
  unsigned short size;          // size of message payload
  char payload[1];
};

struct pull_request {
  unsigned long size;          // size of message to pull
  void *addr;
  NNTI_buffer_t buf_addr;
  NNTI_buffer_t res_addr;
  void *msg_info;
};

struct pull_complete_notify {
  void *msg_info;
};

struct client_message {
  short message_type;
  union {
    struct piggyback pig;
    struct pull_request pull;
    struct pull_complete_notify pull_complete;
  };
};

enum pull_request_state {
  GET_SCHED_QUEUED,             // in queue, ready to be scheduled
  GET_SCHED_GET_ISSUED,         // GET command has been issued
  GET_SCHED_COMPLETED,          // GET command has finished successfully
  GET_SCHED_ERROR,              // some error/failure has happened
};

struct pull_sched_context {
  nnti_conn_data_ptr ncd;       // from which connection
  int target_stone_id;          // local target stone
  void *context_data;           // other context informaiton such as application phases
  // TODO: add source process id and data format id
};

/* queue structure for delayed pull requests */
struct pull_request_queue {
  struct pull_request request;
  enum pull_request_state state;
  int num_errors;
  struct pull_sched_context context;
  struct pull_request_queue *next;
};

/* max number of errors for scheduling a request */
static int max_num_errors_scheduling = 10;

static void
handle_pull_request_message(nnti_conn_data_ptr ncd, CMtrans_services svc, transport_entry trans,
			    struct client_message *m);
static void
handle_pull_complete_message(nnti_conn_data_ptr ncd, CMtrans_services svc, transport_entry trans,
			     struct client_message *m);

static void
handle_control_request(nnti_conn_data_ptr ncd, CMtrans_services svc, transport_entry trans, 
		       struct client_message *m);

static int nnti_conn_count = 0;
typedef struct listen_struct {
  nnti_transport_data_ptr ntd;
  CMtrans_services svc;
  transport_entry trans;
} *listen_struct_p;

static void
handle_request_buffer_event(listen_struct_p lsp, NNTI_status_t *wait_status)
{
    nnti_transport_data_ptr ntd = lsp->ntd;
    CMtrans_services svc = lsp->svc;
    transport_entry trans = lsp->trans;
    struct connect_message *cm = (struct connect_message *)(wait_status->start+wait_status->offset);
	    
    nnti_conn_data_ptr ncd = ntd->connections;
    while (ncd != NULL) {
	if (memcmp(&wait_status->src, &ncd->peer_hdl, sizeof(wait_status->src)) == 0) {
	    ntd->svc->trace_out(trans->cm, "NNTI data available on existing connection, from host %s, type %s (%d)", 
				ncd->peer_hostname, msg_type_name[cm->message_type], cm->message_type);
	    break;
	}
	ncd = ncd->next;
    }
    switch (cm->message_type){
    case CMNNTI_CONNECT: 
    {
	int err;
	attr_list conn_attr_list = NULL;
		
	ntd->svc->trace_out(trans->cm, "  client %s:%d  (enet %d, %x) is connecting",
			    cm->name, cm->nnti_port, cm->enet_port, cm->enet_ip);
	      
	assert(ncd == NULL);
	ncd = create_nnti_conn_data(svc);
	ncd->ntd = ntd;
	ncd->peer_hdl = wait_status->src;
	ncd->remote_contact_port = cm->enet_port;
	ncd->remote_IP = cm->enet_ip;

	ncd->size = NNTI_REQUEST_BUFFER_SIZE;
	ncd->raddr = 0;
	ncd->cksum = 0;
	ncd->acks_offset=(nnti_conn_count++)*NNTI_REQUEST_BUFFER_SIZE;
	//            ncd->buf_addr = cm->buf_addr;
	//            ncd->res_addr = cm->res_addr;
		
	/* register memory region for sending acknowledgement to this client */
	ntd->svc->trace_out(trans->cm, "   ID %d register small send buffer to client %d (offset=%d)...",
			    cm->nnti_port, nnti_conn_count-1, ncd->acks_offset);
	
	conn_attr_list = create_attr_list();
	ncd->conn = svc->connection_create(trans, ncd, conn_attr_list);
	ncd->attrs = conn_attr_list;
	add_connection(ntd, ncd);
	ncd->send_buffer = malloc(NNTI_REQUEST_BUFFER_SIZE);
	err = NNTI_register_memory (&ntd->trans_hdl,
				    ncd->send_buffer,
				    NNTI_REQUEST_BUFFER_SIZE, 1, NNTI_SEND_SRC,
				    &ncd->peer_hdl, &ncd->mr_send);
	if (err != NNTI_OK) {
	    fprintf (stderr, "Error: NNTI_register_memory(NNTI_SEND_SRC) for server message returned non-zero: %d %s\n", err, NNTI_ERROR_STRING(err));
	    return;
	}
	
	ncd->piggyback_size_max = NNTI_REQUEST_BUFFER_SIZE;

#ifdef DF_SHM_FOUND
        if (cm->shm_contact_len != 0) {
            /* attach to the shared memory region to complete connection setup */
            void *shm_region_contact = (void *) (cm->name + cm->name_len + 2);
            ncd->shm_cd = shm_passive_make_conn(ncd, shm_region_contact, cm->shm_contact_len);
            if (!ncd->shm_cd) {
                fprintf(stderr, "Error: cannot make connection.\n");  
            }  
        }
        else {
            ncd->shm_cd = NULL;
        }
#endif
    }
    break;
    case CMNNTI_PIGGYBACK:
    {
	struct client_message *m = (struct client_message *)(wait_status->start+wait_status->offset);
	ncd->read_buffer = ntd->svc->get_data_buffer(trans->cm, (int)m->pig.size);
	
	memcpy(&((char*)ncd->read_buffer->buffer)[0], &(m->pig.payload[0]), m->pig.size);
	
	ncd->read_buf_len = m->pig.size;
	/* kick this upstairs */
	trans->data_available(trans, ncd->conn);
	ntd->svc->return_data_buffer(trans->cm, ncd->read_buffer);
	ncd->read_buffer = NULL;
    }
    break;
    default:
    {
	struct client_message *m = (struct client_message *)(wait_status->start+wait_status->offset);
	handle_control_request(ncd, svc, trans, m);
    }
    }
}

int
listen_thread_func(void *vlsp)
{
    int timeout = 10;
    listen_struct_p lsp = vlsp;
    nnti_transport_data_ptr ntd = lsp->ntd;
    CMtrans_services svc = lsp->svc;
    transport_entry trans = lsp->trans;
    int err;
    NNTI_status_t               wait_status;
    ntd->buf_size = 10;  // initial size
    ntd->buf_list = malloc(sizeof(ntd->buf_list[0]) * ntd->buf_size);
  
    while (1) {
        ntd->buf_count = 1;
	unsigned int which;
        ntd->buf_list[0] = &ntd->mr_recvs;

	//err = NNTI_wait(&ntd->mr_recvs, NNTI_RECV_QUEUE, timeout, &wait_status);
        err = NNTI_waitany((const NNTI_buffer_t**)ntd->buf_list, ntd->buf_count, NNTI_RECV_QUEUE, timeout, &which, &wait_status);

	if (ntd->shutdown_listen_thread) {
	    return 0;
	}
	if ((err == NNTI_ETIMEDOUT) || (err==NNTI_EAGAIN)) {
	    //ntd->svc->trace_out(trans->cm, "NNTI_wait() on receiving result %d timed out...", err);

            // TODO: check if it's time to schedule Puts and check progress of outstanding requests

	    continue;
        } else if (err != NNTI_OK) {
            fprintf (stderr, "Error: NNTI_wait() on receiving result returned non-zero: %d %s  which is %d, status is %d\n", err, NNTI_ERROR_STRING(err), which, wait_status.result);
            return 1;
        } else {
	    ntd->svc->trace_out(trans->cm, "  message arived: msg wait_status=%d size=%lu offs=%lu addr=%lu, offset was %ld",
		     wait_status.result, wait_status.length, wait_status.offset, wait_status.start+wait_status.offset, wait_status.offset);
        }

        if (wait_status.result == NNTI_OK) {
/*	  if (which == 0) { */
	    handle_request_buffer_event(lsp, &wait_status);
/*	  } */
	}

        // TODO: check if it's time to schedule Puts and check progress of outstanding requests

    }
}

#ifdef DF_SHM_FOUND
void *
shm_listen_thread_func(void *vlsp)
{
    int timeout = 10;
    listen_struct_p lsp = vlsp;
    nnti_transport_data_ptr ntd = lsp->ntd;
    shm_transport_data_ptr shm_td = ntd->shm_td;
    CMtrans_services svc = lsp->svc;
    transport_entry trans = lsp->trans;
    int err;

    while (shm_td->listen_thread_cmd == 0) {
        pthread_yield();
    }

    while (shm_td->listen_thread_cmd == 1) {
        /* poll for incoming data messages from shm queues 
         * we use a simple fair polling policy
         */
        pthread_mutex_lock(&(shm_td->mutex));
        while(shm_td->connections == NULL) {
            if(shm_td->listen_thread_cmd == 2) {
                pthread_mutex_unlock(&(shm_td->mutex));
                pthread_exit(NULL);
            }
            pthread_cond_wait(&(shm_td->cond), &(shm_td->mutex)); 
        }
        struct shm_connection_data *conn = shm_td->connections;
        while(conn) {
            void *data = NULL;
            size_t length = 0;
            int rc = df_try_dequeue(conn->recv_ep, &data, &length);
            switch(rc) {
                case 0: { /* dequeue succeeded */
                    conn->ncd->read_buffer = shm_td->ntd->svc->get_data_buffer(trans->cm, length);

                    /* copy the data from shm into a cm buffer */
                    memcpy(&((char*)conn->ncd->read_buffer->buffer)[0], data, length);

                    df_release(conn->recv_ep);

                    conn->ncd->read_buf_len = length;

                    /* kick upstairs */
                    trans->data_available(trans, conn->ncd->conn);
                    shm_td->ntd->svc->return_data_buffer(trans->cm, conn->ncd->read_buffer);
                    conn->ncd->read_buffer = NULL;
                    break;
                }
                case -1: { /* no data available */
                    break;
                }
                case 1: { /* dequeue failed */
                    svc->trace_out(shm_td->ntd->cm, "Error: dequeue returns error on shm connection.\n");
                    break;
                }
                default:
                    break;
            }
            conn = conn->next;
        }
        pthread_mutex_unlock(&(shm_td->mutex));
    }
    return NULL;
}

#endif

#ifdef ENET_FOUND
static void *
enet_accept_conn(nnti_transport_data_ptr ntd, transport_entry trans, 
		 ENetAddress *address);

static
void
nnti_enet_service_network(CManager cm, void *void_trans)
{
    transport_entry trans = (transport_entry) void_trans;
    nnti_transport_data_ptr ntd = (nnti_transport_data_ptr) trans->trans_data;
    CMtrans_services svc = ntd->svc;
    ENetEvent event;
    
    if (!ntd->enet_server) {
	printf("nnti_enet_service network returning\n");
	return;
    }

    /* Wait up to 1000 milliseconds for an event. */
    while (enet_host_service (ntd->enet_server, & event, 1) > 0) {
        switch (event.type) {
	case ENET_EVENT_TYPE_NONE:
	    break;
        case ENET_EVENT_TYPE_CONNECT: {
	    void *nnti_connection_data;
	    svc->trace_out(cm, "A new client connected from %x:%u.\n", 
			   event.peer -> address.host,
			   event.peer -> address.port);

	    nnti_connection_data = enet_accept_conn(ntd, trans, &event.peer->address);

	    ((nnti_conn_data_ptr)nnti_connection_data)->use_enet = 1;
            /* Store any relevant client information here. */
            event.peer -> data = nnti_connection_data;
	    ((nnti_conn_data_ptr)nnti_connection_data)->peer = event.peer;

            break;
	}
        case ENET_EVENT_TYPE_RECEIVE: {
	    nnti_conn_data_ptr ncd = event.peer->data;
	    struct client_message *m = (struct client_message *) event.packet->data;
	    svc->trace_out(cm, "An ENET packet of length %u was received on channel %u, message type %s(%d)",
			   (unsigned int) event.packet -> dataLength,
			   (unsigned int) event.channelID,
			   msg_type_name[m->message_type], m->message_type);
	    if (m->message_type == CMNNTI_PIGGYBACK){
		int piggyback_size;
		ncd->packet = event.packet;
	      
		ncd->read_buffer = ntd->svc->get_data_buffer(trans->cm, (int)m->pig.size);
	      
		piggyback_size = m->pig.size;
		memcpy(&((char*)ncd->read_buffer->buffer)[0], &(m->pig.payload[0]), m->pig.size);
	      

		enet_packet_destroy(event.packet);
		ncd->read_buf_len = piggyback_size;
		/* kick this upstairs */
		svc->trace_out(cm, "We received piggybacked data of size %d %x.",
			       piggyback_size, piggyback_size);
		trans->data_available(trans, ncd->conn);
		ncd->read_buffer = NULL;
	    } else {
		handle_control_request(ncd, svc, trans, m);
		enet_packet_destroy (event.packet);
	    }
            break;
	}           
        case ENET_EVENT_TYPE_DISCONNECT: {
	    nnti_conn_data_ptr nnti_conn_data = event.peer -> data;
	    svc->trace_out(NULL, "Got a disconnect on connection %p\n",
		event.peer -> data);

            nnti_conn_data = event.peer -> data;
	    nnti_conn_data->peer = NULL;
	    /*	    nnti_conn_data->read_buffer_len = -1;*/

        }
	}
    }
}
#endif

#ifdef DF_SHM_FOUND
static void
handle_pull_shm_request_message(nnti_conn_data_ptr ncd, CMtrans_services svc, transport_entry trans,
                       struct client_message *m)
{
    /* copy data from shm queue */
    shm_transport_data_ptr shm_td = ncd->shm_cd->shm_td;
    struct shm_connection_data *conn = ncd->shm_cd;
    void *data = NULL;
    size_t length = 0;
    int rc = df_try_dequeue(conn->recv_ep, &data, &length);
    switch(rc) {
        case 0: { /* dequeue succeeded */
            ncd->read_buffer = shm_td->ntd->svc->get_data_buffer(trans->cm, length);

            /* copy the data from shm into a cm buffer */
            memcpy(&((char*)conn->ncd->read_buffer->buffer)[0], data, length);

            df_release(conn->recv_ep);

            conn->ncd->read_buf_len = length;

            /* kick upstairs */
            trans->data_available(trans, conn->ncd->conn);
            shm_td->ntd->svc->return_data_buffer(trans->cm, conn->ncd->read_buffer);
            conn->ncd->read_buffer = NULL;
            break;
        }
        case -1: { /* no data available */
            break;
        }
        case 1: { /* dequeue failed */
            svc->trace_out(shm_td->ntd->cm, "Error: dequeue returns error on shm connection.\n");
            break;
        }
        default:
            break;
    } 
}
#else
static void
handle_pull_shm_request_message(nnti_conn_data_ptr ncd, CMtrans_services svc, transport_entry trans,
				struct client_message *m){}
#endif

static void
handle_control_request(nnti_conn_data_ptr ncd, CMtrans_services svc, transport_entry trans,
		       struct client_message *m)
{

  switch (m->message_type) {
  case CMNNTI_PULL_REQUEST:{ 
      handle_pull_request_message(ncd, svc, trans, m);
      break;
  }
  case CMNNTI_PULL_SHM_REQUEST:{
      handle_pull_shm_request_message(ncd, svc, trans, m);
      break;
  }
  case CMNNTI_PULL_COMPLETE:{
      handle_pull_complete_message(ncd, svc, trans, m);
      break;
  }
  }

}

#ifdef ENET_FOUND
/* 
 * Accept enet connection
 */
static void *
enet_accept_conn(nnti_transport_data_ptr ntd, transport_entry trans, 
		 ENetAddress *address)
{
    CMtrans_services svc = ntd->svc;
    nnti_conn_data_ptr ncd = ntd->connections;
    int verbose = -1;
    CMConnection conn;
    attr_list conn_attr_list = NULL;;

 restart:
    ncd = ntd->connections;
    if (verbose >=1) printf("NCD is %p\n", ncd);
    
    while (ncd && (ncd->remote_IP != address->host) && 
	   (ncd->remote_contact_port != address->port)) {
	if (verbose >=1) {
	    printf("NCD remote IP %x, address->host %x\n", ncd->remote_IP, address->host);
	    printf("NCD remote contact port %d, address->port %d\n", ncd->remote_contact_port, address->port);
	}
	ncd = ncd->next;
    }
    if (ncd == NULL) {
      printf("Waiting...\n");
      sleep(1);
      verbose++;
      goto restart;
    }

    conn_attr_list = ncd->attrs;

    add_attr(conn_attr_list, CM_PEER_IP, Attr_Int4, (void*)(long)address->host);
    ncd->remote_IP = address->host;
    ncd->remote_contact_port = address->port;

    if (ncd->remote_host != NULL) {
	svc->trace_out(NULL, "Accepted NNTI/ENET RUDP connection from host \"%s\"",
		       ncd->remote_host);
    } else {
	svc->trace_out(NULL, "Accepted NNTI/ENET RUDP connection from UNKNOWN host");
    }
    add_attr(conn_attr_list, CM_PEER_LISTEN_PORT, Attr_Int4,
	     (attr_value) (long)ncd->remote_contact_port);
    svc->trace_out(NULL, "Remote host (IP %x) is listening at port %d\n",
		   ncd->remote_IP,
		   ncd->remote_contact_port);
    return ncd;
}

static void
setup_enet_listen(CManager cm, CMtrans_services svc, transport_entry trans, attr_list listen_list)
{
    nnti_transport_data_ptr ntd = (nnti_transport_data_ptr) trans->trans_data;
    static int socket_global_init = 0;

    ENetAddress address;
    ENetHost * server;
    long seedval = time(NULL) + getpid();
    /* port num is free.  Constrain to range 26000 : 26100 */
    int low_bound = 26000;
    int high_bound = 26100;
    int size = high_bound - low_bound;
    int tries = 10;

    if (socket_global_init++ == 0) {
	if (enet_initialize () != 0) {
	    fprintf (stderr, "An error occurred while initializing ENet.\n");
	    //return EXIT_FAILURE;
	}
    }
    svc->add_periodic_task(cm, 1, 0, nnti_enet_service_network, (void*)trans);
    svc->trace_out(cm, "CMNNTI begin ENET listen\n");

    srand48(seedval);
    address.host = ENET_HOST_ANY;
    while (tries > 0) {
	int target = low_bound + size * drand48();
	address.port = target;
	svc->trace_out(cm, "Cmnnti/Enet trying to bind port %d", target);
	
	server = enet_host_create (& address /* the address to bind the server host to */, 
				   0     /* allow up to 4095 clients and/or outgoing connections */,
				   1      /* allow up to 2 channels to be used, 0 and 1 */,
				   0      /* assume any amount of incoming bandwidth */,
				   0      /* assume any amount of outgoing bandwidth */);
	tries--;
	if (server != NULL) tries = 0;
	if (tries == 5) {
	    /* try reseeding in case we're in sync with another process */
	    srand48(time(NULL) + getpid());
	}
    }
    if (server == NULL) {
	fprintf(stderr, "Failed after 5 attempts to bind to a random port.  Lots of undead servers on this host?\n");
	return;
    }
    ntd->enet_server = server;
    ntd->enet_listen_port = address.port;
    svc->trace_out(cm, "CMNNTI  ENET listen at port %d, server %p\n", address.port, server);
    svc->fd_add_select(cm, enet_host_get_sock_fd (server), 
		       (select_list_func) nnti_enet_service_network, (void*)cm, (void*)trans);
    add_attr(listen_list, CM_ENET_PORT, Attr_Int4,
	     (attr_value) (long)address.port);
}
#else
static void
setup_enet_listen(CManager cm, CMtrans_services svc, transport_entry trans, attr_list listen_list) {}
#endif

static void
setup_nnti_listen(CManager cm, CMtrans_services svc, transport_entry trans, attr_list listen_list)
{
    static NNTI_transport_t trans_hdl;
    nnti_transport_data_ptr ntd = trans->trans_data;
    static int initialized = 0;
    char url[256];
    char *last_colon, *first_colon, *last_slash;
    char *hostname;
    int incoming_size = 10;
    char *nnti_transport;
    int int_port_num = 0;
    int err;

    /* hope to eliminate this at some point */
    setenv("TRIOS_NNTI_USE_RDMA_TARGET_ACK", "FALSE", 1);

    if (!initialized) {
	NNTI_init(NNTI_DEFAULT_TRANSPORT, NULL, &trans_hdl);
	initialized++;
    }
    NNTI_get_url(&trans_hdl, url, sizeof(url));
    last_colon = rindex(url, ':');
    *last_colon = 0;
    first_colon = index(url, ':');
    *first_colon = 0;
    hostname = (first_colon + 1);
    while(hostname[0] == '/') hostname++;
    last_slash = index(last_colon+1, '/');
    *(last_slash++) = 0;
    sscanf((last_colon + 1), "%d", &int_port_num);

    add_attr(listen_list, CM_IP_HOSTNAME, Attr_String,
	     (attr_value) strdup(hostname));
    add_attr(listen_list, CM_NNTI_PORT, Attr_Int4,
	     (attr_value) (long) int_port_num);
    if (strlen(last_slash)) {
	add_string_attr(listen_list, CM_NNTI_PARAMS, strdup(last_slash));
    }
    add_attr(listen_list, CM_TRANSPORT, Attr_String,
	     (attr_value) strdup("nnti"));
    add_attr(listen_list, CM_NNTI_TRANSPORT, Attr_String,
	     (attr_value) strdup(url));

    ntd->incoming  = malloc (incoming_size * NNTI_REQUEST_BUFFER_SIZE);
    memset (ntd->incoming, 0, incoming_size * NNTI_REQUEST_BUFFER_SIZE);
    
    err = NNTI_register_memory(&trans_hdl, (char*)ntd->incoming, 
			       NNTI_REQUEST_BUFFER_SIZE, incoming_size,
			       NNTI_RECV_QUEUE, &trans_hdl.me, &ntd->mr_recvs);
    if (err != NNTI_OK) {
      fprintf (stderr, "Error: NNTI_register_memory(NNTI_RECV_DST) for client messages returned non-zero: %d %s\n", err, NNTI_ERROR_STRING(err));
      return;
    } else {
      ntd->svc->trace_out(trans->cm, "Successfully registered memory on listen side incoming %p", ntd->incoming);
    }
    
    ntd->self_port = int_port_num;
    ntd->trans_hdl = trans_hdl;
    listen_struct_p lsp = malloc(sizeof(*lsp));
    lsp->svc = svc;
    lsp->trans = trans;
    lsp->ntd = ntd;
    ntd->shutdown_listen_thread = 0;
    ntd->listen_thread = 0;
    ntd->self_hostname = strdup(hostname);
    err = pthread_create(&ntd->listen_thread, NULL, (void*(*)(void*))listen_thread_func, lsp);
}

extern attr_list
libcmnnti_LTX_non_blocking_listen(cm, svc, trans, listen_info)
CManager cm;
CMtrans_services svc;
transport_entry trans;
attr_list listen_info;
{
    attr_list listen_list;
    nnti_transport_data_ptr ntd = trans->trans_data;
    int use_enet = 0;
    char *enet = getenv("NNTI_ENET");
    int use_nnti = 1;
    if (ntd->listen_attrs != NULL) {
	return ntd->listen_attrs;
    }
    if (enet) {
      sscanf(enet, "%d", &use_enet);
    }
    if (listen_info) {
	get_int_attr(listen_info, CM_NNTI_ENET_CONTROL, &use_enet);
    }

    listen_list = create_attr_list();
    if (use_nnti) {
	setup_nnti_listen(cm, svc, trans, listen_list);
    }
    if (use_enet) {
	setup_enet_listen(cm, svc, trans, listen_list);
    }
    ntd->listen_attrs = listen_list;
    ntd->use_enet = use_enet;

#ifdef DF_SHM_FOUND
    ntd->shm_td->listen_thread_cmd = 1;
    listen_struct_p lsp = malloc(sizeof(*lsp));
    lsp->svc = svc;
    lsp->trans = trans;
    lsp->ntd = ntd;
    (void) pthread_create(&ntd->shm_td->listen_thread, NULL, (void*(*)(void*))shm_listen_thread_func, lsp);
#endif
    return listen_list;

}

#ifdef NEED_IOVEC_DEFINE
struct iovec {
    void *iov_base;
    int iov_len;
};

#endif

static void enet_free_func(void *packet)
{
#ifdef ENET_FOUND
    /* Clean up the packet now that we're done using it. */
    enet_packet_destroy ((ENetPacket*)packet);
#endif
}

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
    if (ncd->read_buf_len == -1) return NULL;

    *actual_len = ncd->read_buf_len;
    ncd->read_buf_len = 0;
            
    return ncd->read_buffer;
}

#ifndef IOV_MAX
/* this is not defined in some places where it should be.  Conservative. */
#define IOV_MAX 16
#endif

typedef enum {enet, nnti} control_transport;

typedef struct _send_handle {
    control_transport t;
    int size;
#ifdef ENET_FOUND
    ENetPacket *packet;
#endif
    nnti_conn_data_ptr ncd;
} send_handle;

send_handle
get_control_message_buffer(nnti_conn_data_ptr ncd, struct client_message **mp,
			   int size)
{
    send_handle ret;
    if (ncd->use_enet) {
	ret.t = enet;
#ifdef ENET_FOUND
	/* Create a reliable packet of the right size */
	ret.packet = enet_packet_create (NULL, size,
					 ENET_PACKET_FLAG_RELIABLE);
	*mp = (struct client_message *) ret.packet->data;
	memset(ret.packet->data, 0, size);
#endif
    } else {
	assert(size < NNTI_REQUEST_BUFFER_SIZE);
	ret.t = nnti;
	*mp = (struct client_message*)ncd -> send_buffer;
    }	
    ret.ncd = ncd;
    ret.size = size;
    return ret;
}

int
send_control_message(send_handle h)
{
    CManager cm = h.ncd->ntd->cm;
    CMtrans_services svc = h.ncd->ntd->svc;
    if (h.t == enet) {
#ifdef ENET_FOUND
        svc->trace_out(cm, "CMNNTI/ENET control write of %d bytes on peer %p",
		       h.size, h.ncd->peer);
	/* Send the packet to the peer over channel id 0. */
	if (enet_peer_send (h.ncd->peer, 0, h.packet) == -1) {
	    svc->trace_out(cm, "CMNNTI/ENET control write failed.");
	    return 0;
	}
	enet_host_flush(h.ncd->ntd->enet_server);
#endif
    } else {
        svc->trace_out(cm, "CMNNTI control write of %d bytes",
		       h.size);
	NNTI_status_t               status;
	int timeout = 1000;
	int err = NNTI_send(&h.ncd->peer_hdl, &h.ncd->mr_send, NULL);
	
	if (err != NNTI_OK) {
	    fprintf (stderr, "Error: NNTI_send() returned non-zero: %d %s\n", err, NNTI_ERROR_STRING(err));
	    return 1;
	}
	svc->trace_out(cm, "    NNTI_send() returned. Call wait... ");
	
	/* Wait for message to be sent */
	timeout = 1000;
      again:
	err = NNTI_wait(&h.ncd->mr_send, NNTI_SEND_SRC, timeout, &status);
	if (err == NNTI_ETIMEDOUT) {
	  timeout *=2;
	  if (h.ncd->ntd->shutdown_listen_thread) return 0;
	  goto again;
	}
	if (err != NNTI_OK) {
	  fprintf (stderr, "Error: NNTI_wait() for sending returned non-zero: %d %s\n", err, NNTI_ERROR_STRING(err));
	  return 1;
	}
	svc->trace_out(cm, "    NNTI_wait() of send request returned... ");
	
    }
    return 1;
}

typedef struct {
    int send_id;
    int size;
    NNTI_buffer_t mr;
    CMbuffer write_buffer;
} *nnti_message_info;

static int send_request = 32;

static int
copy_full_buffer_and_send_pull_request(CMtrans_services svc, nnti_conn_data_ptr ncd,
				       struct iovec *iov, int iovcnt, attr_list attrs)
{
    send_handle h;
    struct client_message *m;
    CMbuffer write_buffer;
    NNTI_buffer_t mr;
    char *data;
    int i, size = 0;
    int err;
    long register_size;
    nnti_message_info local_message_info = malloc(sizeof(*local_message_info));
    NNTI_status_t               status;

    for(i=0; i<iovcnt; i++) size+= iov[i].iov_len;

    h = get_control_message_buffer(ncd, &m, sizeof(*m));

#ifdef DF_SHM_FOUND
    if (ncd->shm_cd) {
        /* put data to shared memory send queue. this is a blocking call. */
        df_queue_ep_t send_ep = ncd->shm_cd->send_ep;
        int rc = df_enqueue_vector (send_ep, iov, iovcnt);
        if (rc != 0) {
            return -1;
        }

        m->message_type = CMNNTI_PULL_SHM_REQUEST;
        m->pull.size = size;
        m->pull.addr = NULL;
        //m->pull.buf_addr;
        m->pull.msg_info = NULL;

        svc->trace_out(ncd->ntd->cm, "CMNNTI/SHM copied %d bytes and sending pull request", size);
        if (send_control_message(h) == 0) return 0;
        return 1;
    }
#endif

    write_buffer = svc->get_data_buffer(ncd->ntd->cm, size);
    data = write_buffer->buffer;

    /* 
     * register_size might be bigger than needed, map it all 'cause we 
     * might use it later 
     */
    register_size = write_buffer->size;
    if (ncd->ntd->cache_maps && (ncd->outgoing_mapped_region == data) && 
	(ncd->outgoing_mapped_size == register_size)) {
	/* no need to reregister!  We'll reuse!*/
        svc->trace_out(ncd->ntd->cm, "CMNNTI reusing already mapped region at %p, size %d",
		       ncd->outgoing_mapped_region, ncd->outgoing_mapped_size);
	mr = ncd->outgoing_mapped_mr;
    } else {
	if (ncd->outgoing_mapped_region != NULL) {
	    NNTI_unregister_memory(&ncd->outgoing_mapped_mr);
	    svc->trace_out(ncd->ntd->cm, "CMNNTI unregistering previously mapped region at %p, size %d",
			   ncd->outgoing_mapped_region, ncd->outgoing_mapped_size);
	}
        svc->trace_out(ncd->ntd->cm, "CMNNTI registering region at %p, size %d",
		       data, register_size);
	err = NNTI_register_memory(&ncd->ntd->trans_hdl, data, register_size, 1, NNTI_GET_SRC, &ncd->peer_hdl, &mr);
	if (err != NNTI_OK) {
	    printf ("  CMNNTI: NNTI_register_memory() for message returned non-zero: %d %s\n",
		    err, NNTI_ERROR_STRING(err));
	}
	ncd->outgoing_mapped_region = data;
	ncd->outgoing_mapped_size = register_size;
	ncd->outgoing_mapped_mr = mr;
    }

    m->message_type = CMNNTI_PULL_REQUEST;
    m->pull.size = size;
    m->pull.addr = data;
    m->pull.buf_addr = mr;
    m->pull.msg_info = local_message_info;
    local_message_info->send_id = send_request++;
    local_message_info->size = size;
    local_message_info->mr = mr;
    local_message_info->write_buffer = write_buffer;

    size = 0;
    for(i=0; i<iovcnt; i++) {
      memcpy(&data[size], iov[i].iov_base, iov[i].iov_len);
      size += iov[i].iov_len;
    }
    svc->trace_out(ncd->ntd->cm, "CMNNTI/ENET registered buffer %p, copied %d bytes and sending pull request", data, size);
    if (send_control_message(h) == 0) return 0;
    svc->set_pending_write(ncd->conn);

    return 1;
}

/* a simple rate limiting scheduler */
struct rate_limit_sched_data {
    int max_ongoing_reqs;
    int ongoing_req_count;
};

/*
 * Rate limiting scheduler. it only allows fetching the request iff the outstanding RDMA Gets is within
 * the limit.
 * Return 1 for ok to issue RDMA Get; 0 for delay the Get operation; -1 for error.
 */
int nnti_rate_limit_scheduler(struct pull_request *request, 
                              struct pull_sched_context *sched_context, 
                              void *callback_data
                             )
{
    struct rate_limit_sched_data *sched_data = (struct rate_limit_sched_data *) callback_data;
    if (sched_data->ongoing_req_count == sched_data->max_ongoing_reqs) {
        return 0;
    } else if (sched_data->ongoing_req_count < sched_data->max_ongoing_reqs) {
        sched_data->ongoing_req_count ++;
        return 1;
    } else {
        return -1;
    }
}

/*
 * This callback is called when a pull request is completed successfully
 */
void nnti_rate_limit_on_completion(struct pull_request *request, 
                                   struct pull_sched_context *sched_context, 
                                   void *callback_data
                                  )
{
    struct rate_limit_sched_data *sched_data = (struct rate_limit_sched_data *) callback_data;
    sched_data->ongoing_req_count --;
}

/*
 * issue RDMA Get for the request. Return 0 for success and -1 for error.
 */
int perform_pull_request_message(nnti_conn_data_ptr ncd, CMtrans_services svc, transport_entry trans,
               struct pull_request *request)
{
    int err;
    int offset = 0;
    int pullsize = request->size;
    CMbuffer read_buffer;
    char *data;
    NNTI_status_t               status;
    chr_time recv_time;

    svc->trace_out(ncd->ntd->cm, "CMNNTI/ENET Received pull request, pulling %d bytes", request->size);

    if (ncd->ntd->cache_maps &&
        (memcmp(&request->buf_addr, &ncd->incoming_mapped_region, sizeof(NNTI_buffer_t)) == 0)) {
	/* no need to reregister!  We'll reuse!*/
        svc->trace_out(ncd->ntd->cm, "CMNNTI reusing already mapped region at %p, size %d",
		       ncd->incoming_mapped_region, ncd->incoming_mapped_size);
	read_buffer = ncd->read_buffer;
	data = read_buffer->buffer;
    } else {
	if (ncd->read_buffer != NULL) {
	    svc->return_data_buffer(trans->cm, ncd->read_buffer);
	    svc->trace_out(ncd->ntd->cm, "CMNNTI unregistering previously mapped region at %p, size %d",
			   ncd->incoming_mapped_region, ncd->incoming_mapped_size);
	    err = NNTI_unregister_memory(&ncd->mr_pull);
	    if (err != NNTI_OK) {
		printf ("  CMNNTI: NNTI_unregister_memory() for client returned non-zero: %d %s\n",
			err, NNTI_ERROR_STRING(err));
	    }
	    ncd->read_buffer = NULL;
	}

        read_buffer = svc->get_data_buffer(ncd->ntd->cm, request->size);
	data = read_buffer->buffer;
        svc->trace_out(ncd->ntd->cm, "CMNNTI registering region at %p, size %d",
		       data, read_buffer->size);
        err = NNTI_register_memory(&ncd->ntd->trans_hdl, data, request->size, 1,
			       NNTI_GET_DST, &ncd->peer_hdl, &ncd->mr_pull);
	if (err != NNTI_OK) {
	    printf ("  CMNNTI: NNTI_register_memory() for client returned non-zero: %d %s\n",
		    err, NNTI_ERROR_STRING(err));
            return -1;
	}
	ncd->incoming_mapped_size = read_buffer->size;
        memcpy(&ncd->incoming_mapped_region, &request->buf_addr, sizeof(NNTI_buffer_t));
    }

    chr_timer_start(&recv_time);
    err = NNTI_get (&request->buf_addr,
		    offset,  // get from this remote buffer+offset
		    pullsize,      // this amount of data
		    &ncd->mr_pull,
		    offset); // into this buffer+offset
    
    if (err != NNTI_OK) {
	printf ("  THREAD: Error: NNTI_get() for client returned non-zero: %d %s\n",
		err, NNTI_ERROR_STRING(err));
    //    conns[which%nc].status = 1; // failed status
        return -1;
    }
#define NO_SCHEDULER
#ifdef NO_SCHEDULER
    int timeout = 500;
    err = NNTI_ETIMEDOUT;
    while ( (err == NNTI_ETIMEDOUT ) && (timeout < 520)) {
	err = NNTI_wait(&ncd->mr_pull, NNTI_GET_DST, timeout, &status);
	timeout ++;
    }

    chr_timer_stop(&recv_time);
    printf("Receive time is %g millisecs, %g microsecs\n", chr_time_to_millisecs(&recv_time), chr_time_to_microsecs(&recv_time));
    if (err != NNTI_OK) {
	fprintf (stderr, "  THREAD: Error: pull from client failed. NNTI_wait returned: %d %s, wait_status.result = %ds\n",err, NNTI_ERROR_STRING(err), status.result);
	/* bad shit */
    } else {
	// completed a pull here
	send_handle h;
	struct client_message *r;
	h = get_control_message_buffer(ncd, &r, sizeof(*r));
	r->message_type = CMNNTI_PULL_COMPLETE;
	r->pull_complete.msg_info = request->msg_info;
	svc->trace_out(ncd->ntd->cm, "CMNNTI/ENET done with pull, returning control message, type %d, local_info %p", r->message_type, r->pull_complete.msg_info);
	if (send_control_message(h) == 0) {
	    svc->trace_out(ncd->ntd->cm, "--- control message send failed!");
	}	  
	
	ncd->read_buffer = read_buffer;
	ncd->read_buf_len = request->size;
	/* kick this upstairs */
	trans->data_available(trans, ncd->conn);
	if (!ncd->ntd->cache_maps) {
	    svc->return_data_buffer(trans->cm, ncd->read_buffer);
	    NNTI_unregister_memory(&ncd->mr_pull);
	    ncd->read_buffer = NULL;
	}
    }
    return 0;
#else

    // instead of waiting for current Get operation to finish, go on to pull for any other requests
    // add destination buffer to ntd->buf_list to wait
    if (ncd->ntd->buf_count == ncd->ntd->buf_size) {
        ncd->ntd->buf_size ++;
        ncd->ntd->buf_list = realloc(ncd->ntd->buf_list, sizeof(sizeof(ncd->ntd->buf_list[0]) * ncd->ntd->buf_size));
    }
    ncd->ntd->buf_list[ncd->ntd->buf_count-1] = &ncd->mr_pull;
    ncd->ntd->buf_count ++;
    return 0;
#endif
}


static void
handle_pull_request_message(nnti_conn_data_ptr ncd, CMtrans_services svc, transport_entry trans,
		       struct client_message *m)
{
    // put the current message into pull request queue
    struct pull_request_queue *request = (struct pull_request_queue *)
        svc->malloc_func (sizeof(struct pull_request_queue));
    if (!request) {
        svc->trace_out(ncd->ntd->cm, "CMNNTI: Error: cannot allocate memory %s:%d", __FUNCTION__, __LINE__);
        return;
    }
    request->request = m->pull;
    request->state = GET_SCHED_QUEUED;
    request->num_errors = 0;

#ifdef NO_SCHEDULER
    int rc = perform_pull_request_message(ncd, svc, trans, &request->request);
    free(request);
#else

    request->next = ncd->ntd->pull_req_queue;
    ncd->ntd->pull_req_queue = request;
    // go over the request queue: schedule and pull requests
    struct pull_request_queue *req = ncd->ntd->pull_req_queue;
    struct pull_request_queue *prev_req = NULL;
    while(req) {
        // first consult the scheduler if we should pull this request.
        int should_get = (* ncd->ntd->nnti_pull_scheduler)(&req->request, &req->context, ncd->ntd->nnti_pull_sched_data);
        if (should_get == 0) { // do not pull right now
             prev_req = req;
             req = req->next;
             continue;
	} else if (should_get == -1) { // error took place
            request->state = GET_SCHED_ERROR;
            request->num_errors ++;
            if (request->num_errors == max_num_errors_scheduling) {
                svc->trace_out(ncd->ntd->cm, "CMNNTI: Errror in scheduling pull %s:%d", __FUNCTION__, __LINE__);
                // delete the request
                if (prev_req == NULL) {
                    ncd->ntd->pull_req_queue->next = req->next;
                } else {
                    prev_req->next = req->next;
                }
                // TODO: send a notification to sender side
            }
            prev_req = req;
            req = req->next;
            continue;
        }

        // issue RDMA GET for the request
        int rc = perform_pull_request_message(ncd, svc, trans, &req->request);
        if (rc == 0) { // success
            // move the request to ongoing_req_queue
            struct pull_request_queue *temp = req->next;
            if (prev_req == NULL) {
                ncd->ntd->pull_req_queue->next = req->next;
            } else {
                prev_req->next = req->next;
            }
            req->state = GET_SCHED_GET_ISSUED;
            req->next = ncd->ntd->ongoing_req_queue;
            ncd->ntd->ongoing_req_queue = req;
            req = temp;
        } else {
            request->state = GET_SCHED_ERROR;
            request->num_errors ++;
            if (request->num_errors == max_num_errors_scheduling) {
                svc->trace_out(ncd->ntd->cm, "CMNNTI: Errror in scheduling pull %s:%d", __FUNCTION__, __LINE__);
                // delete the request
                if (prev_req == NULL) {
                    ncd->ntd->pull_req_queue->next = req->next;
                } else {
                    prev_req->next = req->next;
                }
                // TODO: send a notification to sender side
            }
            prev_req = req;
            req = req->next;
        }
    }
#endif
}

static void
handle_pull_complete_message(nnti_conn_data_ptr ncd, CMtrans_services svc, transport_entry trans,
		       struct client_message *m)
{
    nnti_message_info local_message_info = m->pull_complete.msg_info;
    svc->trace_out(ncd->ntd->cm, "CMNNTI/ENET received pull complete message, freeing resources, unblocking any pending writes");
    svc->return_data_buffer(ncd->ntd->cm, local_message_info->write_buffer);
    if (!ncd->ntd->cache_maps) {
	NNTI_unregister_memory(&local_message_info->mr);
    }
    svc->wake_any_pending_write(ncd->conn);
    free(local_message_info);
}

extern int
libcmnnti_LTX_writev_func(svc, ncd, iov, iovcnt, attrs)
CMtrans_services svc;
nnti_conn_data_ptr ncd;
struct iovec *iov;
int iovcnt;
attr_list attrs;
{
    int size= 0, i= 0;
    int client_header_size = ((int) (((char *) (&(((struct client_message *)NULL)->pig.payload[0]))) - ((char *) NULL)));
    for(i=0; i<iovcnt; i++) size+= iov[i].iov_len;

#ifdef DF_SHM_FOUND
    if (ncd->shm_cd) {
        svc->trace_out(ncd->ntd->cm, "CMNNTI/SHM writev of %d vectors", iovcnt);

        /* send out small message directly */
        if(size <= ncd->piggyback_size_max) {
            send_handle h;
            struct client_message *m;
            h = get_control_message_buffer(ncd, &m, size + client_header_size);
            m->message_type = CMNNTI_PIGGYBACK;
            m->pig.size = size;
            size = 0;
            for(i=0; i<iovcnt; i++) {
                memcpy(&m->pig.payload[size], iov[i].iov_base, iov[i].iov_len);
                size += iov[i].iov_len;
            }
            svc->trace_out(ncd->ntd->cm, "CMNNTI/ENET outbound piggybacking %d bytes of data in control message", size);
            if (send_control_message(h) == 0) return 0;
        }
        else {
            /* put data to shared memory send queue. this is a blocking call. */
            df_queue_ep_t send_ep = ncd->shm_cd->send_ep;
            int rc = df_enqueue_vector (send_ep, iov, iovcnt);
            if (rc != 0) {
                return -1;
            }

            /* send out a control message */
            if (copy_full_buffer_and_send_pull_request(svc, ncd, iov, iovcnt, attrs) == 0)
            return 0;
        }
        return iovcnt;
    }
#endif

    if (size <= ncd->piggyback_size_max) {
        send_handle h;
	struct client_message *m;
	h = get_control_message_buffer(ncd, &m, size + client_header_size);
	m->message_type = CMNNTI_PIGGYBACK;
	m->pig.size = size;
	size = 0;
	for(i=0; i<iovcnt; i++) {
	  memcpy(&m->pig.payload[size], iov[i].iov_base, iov[i].iov_len);
	  size += iov[i].iov_len;
	}
        svc->trace_out(ncd->ntd->cm, "CMNNTI/ENET outbound piggybacking %d bytes of data in control message", size);
	if (send_control_message(h) == 0) return 0;
    } else {
        if (copy_full_buffer_and_send_pull_request(svc, ncd, iov, iovcnt, attrs) == 0)
	    return 0;
    }
    return iovcnt;
}

static void
free_nnti_data(CManager cm, void *ntdv)
{
    nnti_transport_data_ptr ntd = (nnti_transport_data_ptr) ntdv;
    CMtrans_services svc = ntd->svc;
    ntd->shutdown_listen_thread = 1;
    pthread_join(ntd->listen_thread, NULL);
    ntd->shutdown_listen_thread = 0;
    NNTI_unregister_memory(&ntd->mr_recvs);
    svc->free_func(ntd->nnti_pull_sched_data);
    if (ntd->nnti_pull_sched_data != ntd->nnti_pull_completion_data) {
        svc->free_func(ntd->nnti_pull_completion_data);
    }
#ifdef DF_SHM_FOUND
    ntd->shm_td->listen_thread_cmd = 2;
    pthread_cond_signal(&(ntd->shm_td->cond));
    pthread_join(ntd->shm_td->listen_thread, NULL);
    pthread_cond_destroy(&(ntd->shm_td->cond));
    pthread_mutex_destroy(&(ntd->shm_td->mutex));

    /* destory shm method handle and cleanup */
    df_shm_finalize(ntd->shm_td->shm_method);
    svc->free_func(ntd->shm_td);
#endif
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
    if (getenv("NNTI_LOGGING")) {   
        extern int logger_init(const int debug_level, const char *file);
        char nnti_log_filename[256];
	sprintf(nnti_log_filename, "nnti_log_%x", getpid());
        logger_init(4, nnti_log_filename);
    }
    if (atom_init == 0) {
	CM_NNTI_PORT = attr_atom_from_string("NNTI_PORT");
	CM_NNTI_ADDR = attr_atom_from_string("NNTI_ADDR");
	CM_NNTI_PARAMS = attr_atom_from_string("NNTI_PARAMS");
	CM_NNTI_ENET_CONTROL = attr_atom_from_string("NNTI_ENET_CONTROL");
	CM_IP_HOSTNAME = attr_atom_from_string("IP_HOST");
	CM_ENET_ADDR = attr_atom_from_string("CM_ENET_ADDR");
	CM_ENET_PORT = attr_atom_from_string("CM_ENET_PORT");
	CM_TRANSPORT = attr_atom_from_string("CM_TRANSPORT");
	CM_NNTI_TRANSPORT = attr_atom_from_string("CM_NNTI_TRANSPORT");
	CM_PEER_IP = attr_atom_from_string("PEER_IP");
	CM_TRANSPORT_RELIABLE = attr_atom_from_string("CM_TRANSPORT_RELIABLE");
#ifdef DF_SHM_FOUND
        CM_SHM_NUM_SLOTS = attr_atom_from_string("CM_SHM_NUM_SLOTS");
        CM_SHM_MAX_PAYLOAD = attr_atom_from_string("CM_SHM_MAX_PAYLOAD");
#endif
	atom_init++;
    }
    nnti_data = svc->malloc_func(sizeof(struct nnti_transport_data));
    memset(nnti_data, 0, sizeof(struct nnti_transport_data));
    nnti_data->cm = cm;
    nnti_data->svc = svc;
    nnti_data->socket_fd = -1;
    nnti_data->self_ip = 0;
    nnti_data->self_port = -1;
    nnti_data->connections = NULL;
    nnti_data->listen_attrs = NULL;
    nnti_data->enet_listen_port = -1;
    nnti_data->characteristics = create_attr_list();
    nnti_data->cache_maps = 1;
    add_int_attr(nnti_data->characteristics, CM_TRANSPORT_RELIABLE, 1);
    svc->add_shutdown_task(cm, free_nnti_data, (void *) nnti_data, FREE_TASK);

    nnti_data->pull_req_queue = NULL;
    nnti_data->ongoing_req_queue = NULL;

    /* hardcode to use rate limiting scheduler */
    struct rate_limit_sched_data *sched_data = (struct rate_limit_sched_data *)
        svc->malloc_func(sizeof(struct rate_limit_sched_data));
    sched_data->max_ongoing_reqs = 5; /* get this from outside environement */
    sched_data->ongoing_req_count = 0;
    nnti_data->nnti_pull_scheduler = nnti_rate_limit_scheduler;
    nnti_data->nnti_pull_sched_data = sched_data;
    nnti_data->nnti_pull_completion = nnti_rate_limit_on_completion;
    nnti_data->nnti_pull_completion_data = sched_data;

#ifdef DF_SHM_FOUND
    shm_transport_data_ptr shm_data;
    shm_data = svc->malloc_func(sizeof(struct shm_transport_data));
    shm_data->my_pid = getpid();
    shm_data->connections = NULL;
    pthread_mutex_init(&(shm_data->mutex), NULL);   
    pthread_cond_init(&(shm_data->cond), NULL);   

    /* choose the underlying shm method to use
     * pass this choice from envrioment variable CMSHM_METHOD
     */
    enum DF_SHM_METHOD shm_method;
    char *temp_str = cercs_getenv("CMNNTI_SHM_METHOD");
    if (!temp_str || strcasecmp(temp_str, "SYSV")) {    
        shm_method = DF_SHM_METHOD_SYSV;
    }
    else if (strcasecmp(temp_str, "MMAP")) {
        shm_method = DF_SHM_METHOD_MMAP;
    }
    else if (strcasecmp(temp_str, "POSIXSHM")) {
        shm_method = DF_SHM_METHOD_POSIX_SHM;
    }
    else {
        fprintf(stderr, "Error: invalid CMSNNTI_HM_METHOD value: %s\n"
                        "Valide options are: SYSV|MMAP|POSIXSHM\n"
                        "Use SYSV by default\n", temp_str);
        shm_method = shm_default_method;
    }

    shm_data->shm_method = df_shm_init(shm_method, NULL);
    if (!shm_data->shm_method) {
        svc->trace_out(cm, "Error in Initialize CMNNTI/SHM transport: cannot initialize shm method.");
        svc->free_func(shm_data);
        return NULL;
    }
    nnti_data->shm_td = shm_data;
    shm_data->ntd = nnti_data;
#endif

    return (void *) nnti_data;
}

extern void
libcmnnti_LTX_shutdown_conn(svc, ncd)
CMtrans_services svc;
nnti_conn_data_ptr ncd;
{
#ifdef DF_SHM_FOUND 
    if (ncd->shm_cd) {
        shm_shutdown_conn(svc, ncd->shm_cd);
    }
#endif
    unlink_connection(ncd->ntd, ncd);
    NNTI_unregister_memory(&ncd->mr_send);
    free(ncd->peer_hostname);
    //    free_attr_list(ncd->attrs);
    free(ncd);
}

extern attr_list
libcmnnti_LTX_get_transport_characteristics(transport_entry trans, CMtrans_services svc,
					       void* ntdv)
{
    nnti_transport_data_ptr ntd = (nnti_transport_data_ptr) ntdv;
    return ntd->characteristics;
}


extern transport_entry
cmnnti_add_static_transport(CManager cm, CMtrans_services svc)
{
    transport_entry transport;
    transport = svc->malloc_func(sizeof(struct _transport_item));
    transport->trans_name = strdup("nnti");
    transport->cm = cm;
    transport->transport_init = (CMTransport_func)libcmnnti_LTX_initialize;
    transport->listen = (CMTransport_listen_func)libcmnnti_LTX_non_blocking_listen;
    transport->initiate_conn = (CMConnection(*)())libcmnnti_LTX_initiate_conn;
    transport->self_check = (int(*)())libcmnnti_LTX_self_check;
    transport->connection_eq = (int(*)())libcmnnti_LTX_connection_eq;
    transport->shutdown_conn = (CMTransport_shutdown_conn_func)libcmnnti_LTX_shutdown_conn;
    transport->read_block_func = (CMTransport_read_block_func)libcmnnti_LTX_read_block_func;
    transport->read_to_buffer_func = (CMTransport_read_to_buffer_func)NULL;
    transport->writev_func = (CMTransport_writev_func)libcmnnti_LTX_writev_func;
    transport->get_transport_characteristics = (CMTransport_get_transport_characteristics) libcmnnti_LTX_get_transport_characteristics;
    if (transport->transport_init) {
	transport->trans_data = transport->transport_init(cm, svc, transport);
    }
    return transport;
}
