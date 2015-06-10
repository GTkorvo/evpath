/***** Includes *****/
#include "config.h"
#include <sys/types.h>

#include <inttypes.h>
#include <getopt.h>


#ifdef HAVE_WINDOWS_H
#include <windows.h>
#include <winsock.h>
#define getpid()    _getpid()
#else
#include <time.h>
#ifdef HAVE_SYS_TIME_H
#include <sys/time.h>
#endif
#ifdef HAVE_SYS_TIMES_H
#include <sys/times.h>
#endif
#include <sys/socket.h>
#ifdef HAVE_SYS_SOCKIO_H
#include <sys/sockio.h>
#endif
#ifdef HAVE_SYS_SELECT_H
#include <sys/select.h>
#endif
#ifdef HAVE_SYS_UN_H
#include <sys/un.h>
#endif
#ifdef HAVE_SYS_UIO_H
#include <sys/uio.h>
#endif
#ifdef HAVE_HOSTLIB_H
#include "hostLib.h"
#endif
#ifdef HAVE_STREAMS_UN_H
#include <streams/un.h>
#endif
#include <netinet/in.h>
#include <arpa/inet.h>
#ifdef HAVE_NETDB_H
#include <netdb.h>
#endif
#endif
#include <stdio.h>
#include <fcntl.h>
#ifndef HAVE_WINDOWS_H
#include <net/if.h>
#include <netinet/tcp.h>
#include <sys/ioctl.h>
#include <errno.h>
#endif
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#undef NDEBUG
#include <assert.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#ifdef HAVE_MEMORY_H
#include <memory.h>
#endif
#include <rdma/fabric.h>
#include <rdma/fi_endpoint.h>
#include <rdma/fi_rma.h>
#include <rdma/fi_cm.h>
#include <rdma/fi_errno.h>

#include <atl.h>
#include <cercs_env.h>
#include "evpath.h"
#include "cm_transport.h"
#include "cm_internal.h"

#include <sys/queue.h>
#include <stdlib.h>

#ifndef SOCKET_ERROR
#define SOCKET_ERROR -1
#endif

#define LISTSIZE 1024
#define _WITH_IB_
#define PIGGYBACK 1025*100

#ifdef _WITH_IB_


#if defined (__INTEL_COMPILER)
#  pragma warning (disable: 869)
#  pragma warning (disable: 310)
#  pragma warning (disable: 1418)
#  pragma warning (disable: 180)
#  pragma warning (disable: 2259)
#  pragma warning (disable: 177)
#endif


//   BEGIN from shared.h in fabtests
#include <rdma/fi_eq.h>

/* all tests should work with 1.0 API */
#define FT_FIVERSION FI_VERSION(1,0)

struct test_size_param {
	int size;
	int option;
};

enum precision {
	NANO = 1,
	MICRO = 1000,
	MILLI = 1000000,
};

/* client-server common options and option parsing */

struct cs_opts {
	int iterations;
	int transfer_size;
	char *src_port;
	char *dst_port;
	char *src_addr;
	char *dst_addr;
	int size_option;
	int user_options;
	int machr;
	int argc;
	char **argv;
};

enum {
	FT_OPT_ITER = 1 << 0,
	FT_OPT_SIZE = 1 << 1
};

void ft_parseinfo(int op, char *optarg, struct fi_info *hints);
void ft_parse_addr_opts(int op, char *optarg, struct cs_opts *opts);
void ft_parsecsopts(int op, char *optarg, struct cs_opts *opts);
void ft_usage(char *name, char *desc);
void ft_csusage(char *name, char *desc);
void ft_fill_buf(void *buf, int size);
int ft_check_buf(void *buf, int size);
#define ADDR_OPTS "b:p:s:"
#define INFO_OPTS "n:f:"
#define CS_OPTS ADDR_OPTS "I:S:m"

#define INIT_OPTS (struct cs_opts) { .iterations = 1000, \
				     .transfer_size = 1024, \
				     .src_port = "9228", \
				     .dst_port = "9228", \
				     .argc = argc, .argv = argv }

extern struct test_size_param test_size[];
const unsigned int test_cnt;
#define TEST_CNT test_cnt
#define FT_STR_LEN 32

int ft_getsrcaddr(char *node, char *service, struct fi_info *hints);
int ft_getdestaddr(char *node, char *service, struct fi_info *hints);
int ft_read_addr_opts(char **node, char **service, struct fi_info *hints, 
		uint64_t *flags, struct cs_opts *opts);
char *size_str(char str[FT_STR_LEN], long long size);
char *cnt_str(char str[FT_STR_LEN], long long cnt);
int size_to_count(int size);

void init_test(struct cs_opts *opts, char *test_name, size_t test_name_len);
int ft_finalize(struct fid_ep *tx_ep, struct fid_cq *scq, struct fid_cq *rcq,
		fi_addr_t addr);


int wait_for_data_completion(struct fid_cq *cq, int num_completions);
int wait_for_completion(struct fid_cq *cq, int num_completions);
void cq_readerr(struct fid_cq *cq, char *cq_str);

int64_t get_elapsed(const struct timespec *b, const struct timespec *a, 
		enum precision p);
void show_perf(char *name, int tsize, int iters, struct timespec *start, 
		struct timespec *end, int xfers_per_iter);
void show_perf_mr(int tsize, int iters, struct timespec *start, 
		struct timespec *end, int xfers_per_iter, int argc, char *argv[]);

#define FT_PRINTERR(call, retv) \
	do { fprintf(stderr, call "(): %d, %d (%s)\n", __LINE__, (int) retv, fi_strerror((int) -retv)); } while (0)

#define FT_ERR(fmt, ...) \
	do { fprintf(stderr, "%s:%d: " fmt, __FILE__, __LINE__, ##__VA_ARGS__); } while (0)

#define FT_PRINT_OPTS_USAGE(opt, desc) fprintf(stderr, " %-20s %s\n", opt, desc)

#define MIN(a,b) (((a)<(b))?(a):(b))
#define MAX(a,b) (((a)>(b))?(a):(b))
#define ARRAY_SIZE(A) (sizeof(A)/sizeof(*A))

/* for RMA tests --- we want to be able to select fi_writedata, but there is no
 * constant in libfabric for this */
enum ft_rma_opcodes {
	FT_RMA_READ = 1,
	FT_RMA_WRITE,
	FT_RMA_WRITEDATA,
};
//   END from shared.h in fabtests


//all the message types have a queue associated with them
static char *msg_string[] = {"Request", "Response", "Piggyback"};
enum {msg_request = 0, msg_response = 1, msg_piggyback = 2} msg_type;

struct request
{
    int magic;    
    uint32_t length;    
    uint64_t request_ID;
};


struct response
{
    uint64_t remote_addr;
    uint32_t rkey;
    uint32_t max_length;    
    uint64_t request_ID;
};

struct piggyback
{
    uint32_t total_length;
    uint32_t padding;
    char  body[4];
};


struct control_message
{
    int type;
    union {
	struct request req;
	struct response resp;
	struct piggyback pb;
    } u;
};

struct ibparam
{
	int lid;
	int psn;
	int qpn;
	int port;
	/*anything else? */
};

int page_size = 0;


#define ptr_from_int64(p) (void *)(unsigned long)(p)
#define int64_from_ptr(p) (u_int64_t)(unsigned long)(p)

typedef struct fabric_client_data {
    CManager cm;
    CMtrans_services svc;
    struct cs_opts opts;
    struct fi_info *hints;

    struct fid_fabric *fab;
    struct fid_pep *listen_ep;
    struct fid_domain *dom;
    struct fid_eq *cmeq;

	char *hostname;
	int listen_port;
	int lid;
	int qpn;
	int psn;
	int port;
	struct ibv_device *ibdev;
	struct ibv_context *context;
	struct ibv_comp_channel *send_channel;
	struct ibv_comp_channel *recv_channel;
	struct ibv_pd *pd;
	struct ibv_cq *recv_cq;
	struct ibv_cq *send_cq;
	struct ibv_srq *srq;
	int max_sge;    
} *fabric_client_data_ptr;


typedef struct notification
{
	int done;
}notify;

typedef struct remote_info
{
    int mrlen;
    notify isDone;
    struct ibv_mr **mrlist;
    struct ibv_send_wr *wr; 
    CMcompletion_notify_func notify_func;
    void *notify_client_data;
}rinfo;


typedef struct fabric_connection_data {
    fabric_client_data_ptr fabd;
    struct fid_cq *rcq, *scq;
    struct fid_mr *mr;
    struct fid_ep *conn_ep;
    size_t buffer_size;
    char *send_buf;
    CMbuffer read_buf;
    int max_credits;
    void *read_buffer;
    int read_buffer_len;
    int read_offset;

	char *remote_host;
	int remote_IP;
	int remote_contact_port;
	int fd;
	struct tbuffer_ *tb;    
	CMConnection conn;
	struct ibv_qp *dataqp;    
	notify isDone;
	int infocount;
	rinfo infolist[10];
	int max_imm_data;   
} *fabric_conn_data_ptr;

typedef struct tbuffer_
{
	CMbuffer buf;
	struct ibv_mr *mr;
	fabric_conn_data_ptr fcd;
	uint64_t size;
	uint64_t offset;
	struct tbuffer_ *parent;
	int childcount;    
        int inuse;
	LIST_ENTRY(tbuffer_) entries;    
}tbuffer;

LIST_HEAD(tblist, tbuffer_) memlist;
LIST_HEAD(inuselist, tbuffer_) uselist;


char *ibv_wc_opcode_str[] = {
	"IBV_WC_SEND",
	"IBV_WC_RDMA_WRITE",
	"IBV_WC_RDMA_READ",
	"IBV_WC_COMP_SWAP",
	"IBV_WC_FETCH_ADD",
	"IBV_WC_BIND_MW",
	"IBV_WC_RECV_RDMA_WITH_IMM"
};

static struct ibv_mr ** regblocks(fabric_client_data_ptr sd,
                                  struct iovec *iovs, int iovcnt, int flags,
                                  int *mrlen);


static struct ibv_send_wr * createwrlist(fabric_conn_data_ptr conn, 
                                         struct ibv_mr **mrlist,
                                         struct iovec *iovlist,
                                         int mrlen, int *wrlen); 

static int waitoncq(fabric_conn_data_ptr fcd,
                    fabric_client_data_ptr sd,
                    CMtrans_services svc, struct ibv_cq *cq);

static inline int msg_offset()
{
	return ((int) (((char *) (&(((struct control_message *)NULL)->u.pb.body[0]))) - ((char *) NULL)));
}

static int alloc_cm_res(fabric_client_data_ptr fabd);
static int alloc_ep_res(fabric_conn_data_ptr fcd, struct fi_info *fi);
static int bind_ep_res(fabric_conn_data_ptr fcd);
static void free_ep_res(fabric_conn_data_ptr fcd);


static atom_t CM_FD = -1;
static atom_t CM_THIS_CONN_PORT = -1;
static atom_t CM_PEER_CONN_PORT = -1;
static atom_t CM_PEER_IP = -1;
static atom_t CM_PEER_HOSTNAME = -1;
static atom_t CM_PEER_LISTEN_PORT = -1;
static atom_t CM_NETWORK_POSTFIX = -1;
static atom_t CM_IP_PORT = -1;
static atom_t CM_IP_HOSTNAME = -1;
static atom_t CM_IP_ADDR = -1;
static atom_t CM_TRANSPORT = -1;

static double getlocaltime()
{
	struct timeval t;
	double dt;
	gettimeofday(&t, NULL);
	dt = (double) t.tv_usec / 1e6 + t.tv_sec;
	return dt;
}

static void free_func(void *cbd)
{
	tbuffer *tb = (tbuffer*)cbd;
	tbuffer *temp;
    
	tb->inuse = 0;
	if(tb->childcount == 0)
	{
		//we can merge upwards
		temp = tb->parent;
		if(!temp)
		{
			//is the top level - at the top it means we don't need to merge or do anything other than drop offset to 0
			tb->offset = 0;     
		}
		else
		{
			//is a child of someone - we merge up
			temp->size += tb->size;
			temp->childcount --;
        
			//we can free the CMbuffer now
			free(tb->buf);
        
			//now remove tb from list and free it
			LIST_REMOVE(tb, entries);
			free(tb);       
		}
	}
    
    
    
}


static int
check_host(hostname, sin_addr)
	char *hostname;
void *sin_addr;
{
	struct hostent *host_addr;
	host_addr = gethostbyname(hostname);
	if (host_addr == NULL) {
		struct in_addr addr;
		if (inet_aton(hostname, &addr) == 0) {
			/* 
			 *  not translatable as a hostname or 
			 * as a dot-style string IP address
			 */
			return 0;
		}
		assert(sizeof(int) == sizeof(struct in_addr));
		*((int *) sin_addr) = *((int*) &addr);
	} else {
		memcpy(sin_addr, host_addr->h_addr, host_addr->h_length);
	}
	return 1;
}

static fabric_conn_data_ptr 
create_fabric_conn_data(CMtrans_services svc)
{
    fabric_conn_data_ptr fabric_conn_data = svc->malloc_func(sizeof(struct fabric_connection_data));
    memset(fabric_conn_data, 0, sizeof(struct fabric_connection_data));
    fabric_conn_data->remote_host = NULL;
    fabric_conn_data->remote_contact_port = -1;
    fabric_conn_data->fd = 0;
    fabric_conn_data->read_buffer = NULL;
    fabric_conn_data->read_buffer_len = 0;
    
    return fabric_conn_data;
}

#ifdef NOTDEF
static
void 
dump_sockaddr(who, sa)
	char *who;
struct sockaddr_in *sa;
{
	unsigned char *addr;

	addr = (unsigned char *) &(sa->sin_addr.s_addr);

	printf("%s: family=%d port=%d addr=%d.%d.%d.%d\n",
	       who,
	       ntohs(sa->sin_family),
	       ntohs(sa->sin_port),
	       addr[0], addr[1], addr[2], addr[3]);
}

static
void 
dump_sockinfo(msg, fd)
	char *msg;
int fd;
{
	int nl;
	struct sockaddr_in peer, me;

	printf("Dumping sockinfo for fd=%d: %s\n", fd, msg);

	nl = sizeof(me);
	getsockname(fd, (struct sockaddr *) &me, &nl);
	dump_sockaddr("Me", &me);

	nl = sizeof(peer);
	getpeername(fd, (struct sockaddr *) &peer, &nl);
	dump_sockaddr("Peer", &peer);
}

#endif

static int internal_write_piggyback(CMtrans_services svc,
                                    fabric_conn_data_ptr fcd,
                                    int length, struct iovec *iov, int iovcnt)
{
    //this function is only called if length < piggyback
    struct control_message *msg;
    char *point;
    int offset = msg_offset();
    int i;
	
    if(length >= PIGGYBACK)
    {
	//should never happen
	return -1;
    }

    msg = malloc(offset + length);
    memset(msg, 0, offset+length);
    msg->type = msg_piggyback;
    msg->u.pb.total_length = length + offset;
    point = &msg->u.pb.body[0];
    svc->trace_out(fcd->fabd->cm, "CMFABRIC sending piggyback msg of length %d,", 
		   length);

	
    for (i = 0; i < iovcnt; i++)
    {
	memcpy(point, iov[i].iov_base, iov[i].iov_len);
	point += iov[i].iov_len;
    }
    
    {
	
	memcpy(fcd->send_buf, msg, msg->u.pb.total_length);
	int ret;
	
	ret = fi_send(fcd->conn_ep, fcd->send_buf, msg->u.pb.total_length, fi_mr_desc(fcd->mr), 0, fcd->send_buf);
	if (ret) {
	    FT_PRINTERR("fi_send", ret);
	    return ret;
	}
	
	/* Read send queue */
	do {
	    struct fi_cq_entry comp;
	    ret = fi_cq_read(fcd->scq, &comp, 1);
	    if (ret < 0 && ret != -FI_EAGAIN) {
		FT_PRINTERR("fi_cq_read", ret);
		return ret;
	    }
	} while (ret == -FI_EAGAIN);
    }
    
    free(msg);
    return 0;
}


static int internal_write_response(CMtrans_services svc,
                                   fabric_conn_data_ptr fcd,
                                   tbuffer *tb,
                                   int length,
				   int64_t request_ID)
{
	struct control_message msg;
	int iget = 0;

	msg.type = msg_response;
	if(tb != NULL)
	{
		msg.u.resp.remote_addr = int64_from_ptr(tb->buf->buffer);
		msg.u.resp.max_length = length;
		msg.u.resp.request_ID = request_ID;
	}
	else
	{
		msg.u.resp.remote_addr = 0;
		msg.u.resp.rkey = 0;
		msg.u.resp.max_length = 0;
		msg.u.resp.request_ID = request_ID;
	}

	iget = write(fcd->fd, &msg, sizeof(struct control_message));
	if(iget != sizeof(struct control_message))
	{
		svc->trace_out(fcd->fabd->cm, "CMFABRIC error in writing response to %d", fcd->fd);
		return -1;
	}

	return 0;
}

static int internal_write_request(CMtrans_services svc,
                                  fabric_conn_data_ptr fcd,
                                  int length,
    				  void *request_ID)
{
	struct control_message msg;
	int iget = 0;

	msg.type = msg_request;
	msg.u.req.magic = 0xdeadbeef;
	msg.u.req.length = length;
	msg.u.req.request_ID = int64_from_ptr(request_ID);

	svc->trace_out(fcd->fabd->cm, "Doing internal write request, writing %d bytes", sizeof(struct control_message));
	iget = write(fcd->fd, &msg, sizeof(struct control_message));
	if(iget != sizeof(struct control_message))
	{
		svc->trace_out(fcd->fabd->cm, "CMFABRIC error in writing request to %d", fcd->fd);
		return -1;
	}

	return 0;
}
	
static double write_t = 0;
static double da_t = 0;

static int handle_response(CMtrans_services svc,
                           fabric_conn_data_ptr fcd,
                           struct control_message *msg)
{

    //read back response
    int iget = 0;
    struct ibv_send_wr *bad_wr;    
    int retval = 0;
    int i = 0;
    struct response *rep;
    rinfo *write_request;
    int free_data_elements;
    
    rep = &msg->u.resp;
    write_request = ptr_from_int64(rep->request_ID);
    
    free_data_elements = (write_request->notify_func == NULL);
    if (write_request == NULL) {
	fprintf(stderr, "Failed to get work request - aborting write\n");
	return -0x01000;
    }

    if (write_request->notify_func) {
	(write_request->notify_func)( write_request->notify_client_data);
    }
    svc->wake_any_pending_write(fcd->conn);
    return 0;   
    
}

static void handle_request(CMtrans_services svc,
                           fabric_conn_data_ptr fcd,
                           struct control_message *msg)
{
	//handling the request message

	//first read the request message from the socket
	struct ibv_mr *mr;
	tbuffer *tb;
	int retval = 0;
	struct request *req;

	req = &msg->u.req;
	
	tb = NULL;
	if(tb == NULL)
	{
		svc->trace_out(fcd->fabd->cm, "Failed to get memory\n");
		internal_write_response(svc, fcd, NULL, 0, 0);
		goto wait_on_q; 
	}

	fcd->tb = tb;
	mr = tb->mr;

	svc->set_pending_write(fcd->conn);
	internal_write_response(svc, fcd, tb, req->length, req->request_ID);

wait_on_q:
	//now the sender will start the data transfer. When it finishes he will 
	//issue a notification after which the data is in memory
	retval = waitoncq(fcd, fcd->fabd, svc, fcd->fabd->recv_cq);    
	if(retval)
	{
		svc->trace_out(fcd->fabd->cm, "Error while waiting\n");
		return;     
	}

	//now we start polling the cq

	do
	{
		svc->trace_out(fcd->fabd->cm, "CMFABRIC poll cq -start");    
		/* retval = ibv_poll_cq(fcd->fabdrecv_cq, 1, &wc); */
		/* svc->trace_out(fcd->fabd->cm, "CMFABRIC poll cq -end");     */
		/* if(retval > 0 && wc.status == IBV_WC_SUCCESS && wc.opcode == IBV_WC_RECV) */
		/* { */
		/* 	//cool beans - send completed we can go on with our life */
        
		/* 	//issue a reccieve so we don't run out */
		/* 	retval = ibv_post_recv(fcd->dataqp, &fcd->isDone.rwr,  */
		/* 	                       &fcd->isDone.badrwr); */
		/* 	if(retval) */
		/* 	{ */
		/* 		fcd->fabdsvc->trace_out(fcd->fabd->cm, "Cmfabric unable to post recv %d\n", retval); */
		/* 	} */
    
		/* 	break;       */
		/* } */
		/* else */
		/* { */
		/* 	svc->trace_out(fcd->fabd->cm, "Error polling for write completion\n"); */
		/* 	break;       */
		/* } */

		/* svc->trace_out(fcd->fabd->cm, "CMFABRIC poll cq looping");     */
    
	}while(1);

	fcd->read_buffer = tb->buf;
	svc->trace_out(fcd->fabd->cm, "FIrst 16 bytes of receive buffer (len %d) are %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x \n", req->length, ((unsigned char*)tb->buf->buffer)[0], ((unsigned char*)tb->buf->buffer)[1], ((unsigned char*)tb->buf->buffer)[2], ((unsigned char*)tb->buf->buffer)[3], ((unsigned char*)tb->buf->buffer)[4], ((unsigned char*)tb->buf->buffer)[5], ((unsigned char*)tb->buf->buffer)[6], ((unsigned char*)tb->buf->buffer)[7], ((unsigned char*)tb->buf->buffer)[8], ((unsigned char*)tb->buf->buffer)[9], ((unsigned char*)tb->buf->buffer)[10], ((unsigned char*)tb->buf->buffer)[11], ((unsigned char*)tb->buf->buffer)[12], ((unsigned char*)tb->buf->buffer)[13], ((unsigned char*)tb->buf->buffer)[14], ((unsigned char*)tb->buf->buffer)[15]);
	fcd->read_buffer_len = req->length;
	svc->trace_out(fcd->fabd->cm, "CMFABRIC handle_request completed");    
	svc->wake_any_pending_write(fcd->conn);
}

int handle_piggyback(CMtrans_services svc,
                      fabric_conn_data_ptr fcd,
                      struct control_message *msg)
{
	// read rest and deliver up
	int offset = msg_offset();
	int iget = 0;
	
	fcd->read_buffer_len = msg->u.pb.total_length - offset;
	svc->trace_out(fcd->fabd->cm, "CMFABRIC received piggyback msg of length %d, added to read_buffer", 
		       fcd->read_buffer_len);

	fcd->read_buffer = fcd->read_buf;
	fcd->read_offset = offset;
	return 0;
   
}


void cq_readerr(struct fid_cq *cq, char *cq_str)
{ 
	struct fi_cq_err_entry cq_err;
	const char *err_str;
	int ret;

	ret = fi_cq_readerr(cq, &cq_err, 0);
	if (ret < 0)
		FT_PRINTERR("fi_cq_readerr", ret);

	err_str = fi_cq_strerror(cq, cq_err.prov_errno, cq_err.err_data, NULL, 0);
	fprintf(stderr, "%s %s (%d)\n", cq_str, err_str, cq_err.prov_errno);
}

void
CMFABRIC_data_available(transport_entry trans, CMConnection conn)
{    
	int iget;
	fabric_client_data_ptr sd = (fabric_client_data_ptr) trans->trans_data;
	CMtrans_services svc = sd->svc;
	struct control_message *msg;
	double start =0;
	fabric_conn_data_ptr fcd;
	int ret;
    
	da_t = getlocaltime();
    
	fcd = (fabric_conn_data_ptr) svc->get_transport_data(conn);

	start = getlocaltime();    

	do {
	    struct fi_cq_entry comp;
		ret = fi_cq_read(fcd->rcq, &comp, 1);
		if (ret < 0 && ret != -FI_EAGAIN) {
			if (ret == -FI_EAVAIL) {
				cq_readerr(fcd->rcq, "rcq");
			} else {
				FT_PRINTERR("fi_cq_read", ret);
				return;
			}
		}
	} while (ret == -FI_EAGAIN);

	fcd = (fabric_conn_data_ptr) svc->get_transport_data(conn);
	msg = (struct control_message *) fcd->read_buf->buffer;
	svc->trace_out(fcd->fabd->cm, "CMFABRIC data available type = %s(%d)", 
		       msg_string[msg->type], msg->type);

	switch(msg->type) {
	case msg_piggyback:
		iget = handle_piggyback(svc, fcd, msg);
		if(iget == 0)
		{
		    CMbuffer tmp = fcd->read_buf;
		    trans->data_available(trans, conn);
		    svc->return_data_buffer(trans->cm, tmp);
		    fcd->read_buf = fcd->fabd->svc->get_data_buffer(trans->cm, MAX(fcd->buffer_size, sizeof(uint64_t)));
		}
		break;
	case msg_response:
		handle_response(svc, fcd, msg);
		break;
	case msg_request:
		handle_request(svc, fcd, msg);
		if(fcd->isDone.done == 0)
		{
			trans->data_available(trans, conn);
		}
		else
		{
			svc->trace_out(fcd->fabd->cm, "Cmfabric data available error in the protocol");
		}
		break;
	default:
		printf("Bad message type %d\n", msg->type);
	}

	//returning control to CM
	ret = fi_recv(fcd->conn_ep, fcd->read_buf->buffer, fcd->buffer_size, fi_mr_desc(fcd->mr), 0, fcd->read_buf->buffer);
	if (ret)
		FT_PRINTERR("fi_recv", ret);

	svc->trace_out(fcd->fabd->cm, "CMFABRIC data_available returning");
}

static int server_connect(fabric_conn_data_ptr fcd);

/* 
 * Accept socket connection
 */
static void
fabric_accept_conn(void *void_trans, void *void_conn_sock)
{
    transport_entry trans = (transport_entry) void_trans;
    int conn_sock = (int) (long) void_conn_sock;
    fabric_client_data_ptr fabd = (fabric_client_data_ptr) trans->trans_data;
    CMtrans_services svc = fabd->svc;
    fabric_conn_data_ptr fcd;
    int fd, ret;
    struct sockaddr sock_addr;
    unsigned int sock_len = sizeof(sock_addr);
    int int_port_num;
    struct linger linger_val;
    int sock_opt_val = 1;

    int delay_value = 1;
    CMConnection conn;
    attr_list conn_attr_list = NULL;
    struct ibparam param, remote_param;

    //ib stuff
    fcd = create_fabric_conn_data(svc);
    fcd->fabd = fabd;

    server_connect(fcd);
    //initialize the dataqp that will be used for all RC comms

    conn_attr_list = create_attr_list();
    conn = svc->connection_create(trans, fcd, conn_attr_list);
    fcd->conn = conn;

    sock_len = sizeof(sock_addr);
    memset(&sock_addr, 0, sock_len);
//    getsockname(sock, (struct sockaddr *) &sock_addr, &sock_len);
//    int_port_num = ntohs(((struct sockaddr_in *) &sock_addr)->sin_port);
    add_attr(conn_attr_list, CM_THIS_CONN_PORT, Attr_Int4,
	     (attr_value) (long)int_port_num);

    memset(&sock_addr, 0, sizeof(sock_addr));
    sock_len = sizeof(sock_addr);
    /* if (getpeername(sock, &sock_addr, &sock_len) == 0) { */
    /* 	int_port_num = ntohs(((struct sockaddr_in *) &sock_addr)->sin_port); */
    /* 	add_attr(conn_attr_list, CM_PEER_CONN_PORT, Attr_Int4, */
    /* 		 (attr_value) (long)int_port_num); */
    /* 	fcd->remote_IP = ntohl(((struct sockaddr_in *) &sock_addr)->sin_addr.s_addr); */
    /* 	add_attr(conn_attr_list, CM_PEER_IP, Attr_Int4, */
    /* 		 (attr_value) (long)fcd->remote_IP); */
    /* 	if (sock_addr.sa_family == AF_INET) { */
    /* 	    struct hostent *host; */
    /* 	    struct sockaddr_in *in_sock = (struct sockaddr_in *) &sock_addr; */
    /* 	    host = gethostbyaddr((char *) &in_sock->sin_addr, */
    /* 				 sizeof(struct in_addr), AF_INET); */
    /* 	    if (host != NULL) { */
    /* 		fcd->remote_host = strdup(host->h_name); */
    /* 		add_attr(conn_attr_list, CM_PEER_HOSTNAME, Attr_String, */
    /* 			 (attr_value) strdup(host->h_name)); */
    /* 	    } */
    /* 	} */
    /* } */
    if (fcd->remote_host != NULL) {
	svc->trace_out(fabd->cm, "Accepted CMFABRIC socket connection from host \"%s\"",
		       fcd->remote_host);
    } else {
	svc->trace_out(fabd->cm, "Accepted CMFABRIC socket connection from UNKNOWN host");
    }

    //here we read the incoming remote contact port number. 
    //in IB we'll extend this to include ib connection parameters
    param.lid  = fabd->lid;
    param.port = fabd->port;
    param.psn  = fabd->psn;
    
    
    if ((ret = fi_control (&fcd->rcq->fid, FI_GETWAIT, (void *) &fd))) {
	FT_PRINTERR("fi_control(FI_GETWAIT)", ret);
    }
    add_attr(conn_attr_list, CM_FD, Attr_Int4,
	     (attr_value) (long)fd);

    svc->trace_out(fabd->cm, "Cmfabric Adding trans->data_available as action on fd %d", fd);
    svc->fd_add_select(fabd->cm, fd, (select_list_func) CMFABRIC_data_available,
		       (void *) trans, (void *) conn);

    svc->trace_out(fabd->cm, "Falling out of accept conn\n");
    free_attr_list(conn_attr_list);
}

/* 
 * incoming event on CM eq
 */
static void
fabric_service_incoming(void *void_trans, void *void_eq)
{
    transport_entry trans = (transport_entry) void_trans;
    fabric_client_data_ptr fabd = (fabric_client_data_ptr) trans->trans_data;
    struct fi_eq_cm_entry entry;
    uint32_t event;
    struct fi_info *info = NULL;
    ssize_t rd;
    int ret;

    rd = fi_eq_sread(fabd->cmeq, &event, &entry, sizeof entry, -1, FI_PEEK);
    if (rd != sizeof entry) {
	FT_PRINTERR("fi_eq_sread", rd);
	return;
    }
    
    info = entry.info;
    if (event == FI_CONNREQ) {
	fabric_accept_conn(void_trans, void_eq);
    } else {
	printf("Unexpected event %d\n", event);
    }
}

extern void
libcmfabric_LTX_shutdown_conn(svc, fcd)
	CMtrans_services svc;
fabric_conn_data_ptr fcd;
{
	svc->trace_out(fcd->fabd->cm, "CMFABRIC shutdown_conn, removing select %d\n",
	               fcd->fd);
	svc->fd_remove_select(fcd->fabd->cm, fcd->fd);
	close(fcd->fd);
	//free(fcd->remote_host);
	//free(fcd->read_buffer);
	free(fcd);
}


static int
is_private_192(int IP)
{
	return ((IP & 0xffff0000) == 0xC0A80000);   /* equal 192.168.x.x */
}

static int
is_private_182(int IP)
{
	return ((IP & 0xffff0000) == 0xB6100000);   /* equal 182.16.x.x */
}

static int
is_private_10(int IP)
{
	return ((IP & 0xff000000) == 0x0A000000);   /* equal 10.x.x.x */
}

static int client_connect(CManager cm, CMtrans_services svc, transport_entry trans, attr_list attrs, fabric_conn_data_ptr fcd)
{
    fabric_client_data_ptr fabd = fcd->fabd;
    struct fi_eq_cm_entry entry;
    uint32_t event;
    struct fi_info *fi;
    ssize_t rd;
    int ret, int_port_num;

    /* Get fabric info */
    fabd->opts.dst_addr = "localhost";
    if (!query_attr(attrs, CM_IP_PORT, /* type pointer */ NULL,
		    /* value pointer */ (attr_value *)(long) & int_port_num)) {
	svc->trace_out(cm, "CMFABRIC transport found no IP_PORT attribute");
    } else {
	fabd->opts.dst_port = malloc(10);
	sprintf(fabd->opts.dst_port, "%d", int_port_num);
    }
//    printf("Connecting to addr, %s, port %s\n", fabd->opts.dst_addr, fabd->opts.dst_port);
    ret = fi_getinfo(FT_FIVERSION, fabd->opts.dst_addr, fabd->opts.dst_port, 0, fabd->hints, &fi);
    if (ret) {
	FT_PRINTERR("fi_getinfo", ret);
	goto err0;
    }

    /* Open fabric */
    ret = fi_fabric(fi->fabric_attr, &fabd->fab, NULL);
    if (ret) {
	FT_PRINTERR("fi_fabric", ret);
	goto err1;
    }

    /* Open domain */
    ret = fi_domain(fabd->fab, fi, &fabd->dom, NULL);
    if (ret) {
	FT_PRINTERR("fi_domain", ret);
	goto err2;
    }
	
    ret = alloc_cm_res(fabd);
    if (ret)
	goto err4;

    ret = alloc_ep_res(fcd, fi);
    if (ret)
	goto err5;

    ret = bind_ep_res(fcd);
    if (ret)
	goto err6;

    /* Connect to server */
    ret = fi_connect(fcd->conn_ep, fi->dest_addr, NULL, 0);
    if (ret) {
	FT_PRINTERR("fi_connect", ret);
	goto err6;
    }
    
    /* Wait for the connection to be established */
    rd = fi_eq_sread(fabd->cmeq, &event, &entry, sizeof entry, -1, 0);
    if (rd != sizeof entry) {
	FT_PRINTERR("fi_eq_sread", rd);
	return (int) rd;
    }

    if (event != FI_CONNECTED || entry.fid != &fcd->conn_ep->fid) {
	FT_ERR("Unexpected CM event %d fid %p (ep %p)\n", event, entry.fid, fcd->conn_ep);
	ret = -FI_EOTHER;
	goto err6;
    }

    fi_freeinfo(fi);
    return 0;

err6:
    free_ep_res(fcd);
err5:
    fi_close(&fabd->cmeq->fid);
err4:
    fi_close(&fabd->dom->fid);
err2:
    fi_close(&fabd->fab->fid);
err1:
    fi_freeinfo(fi);
err0:
    return ret;
}

static int
initiate_conn(cm, svc, trans, attrs, fcd, conn_attr_list, no_more_redirect)
	CManager cm;
CMtrans_services svc;
transport_entry trans;
attr_list attrs;
fabric_conn_data_ptr fcd;
attr_list conn_attr_list;
int no_more_redirect;
{
	int delay_value = 1;
	struct linger linger_val;
	int sock_opt_val = 1;
	int int_port_num;
	u_short port_num;
	fabric_client_data_ptr fabd = (fabric_client_data_ptr) trans->trans_data;
	char *host_name;
	int remote_IP = -1;
	static int host_ip = 0;
	unsigned int sock_len;
	union {
	    struct sockaddr s;
	    struct sockaddr_in s_I4;
	    struct sockaddr_in6 s_l6;
	} sock_addr;
	struct ibparam param, remote_param;

	//fabric stuff

	int retval = 0;
    
	if (!query_attr(attrs, CM_IP_HOSTNAME, /* type pointer */ NULL,
	                /* value pointer */ (attr_value *)(long) & host_name)) {
		svc->trace_out(cm, "CMFABRIC transport found no IP_HOST attribute");
		host_name = NULL;
	} else {
		svc->trace_out(cm, "CMFABRIC transport connect to host %s", host_name);
	}
	if (!query_attr(attrs, CM_IP_ADDR, /* type pointer */ NULL,
	                /* value pointer */ (attr_value *)(long) & host_ip)) {
		svc->trace_out(cm, "CMFABRIC transport found no IP_ADDR attribute");
		/* wasn't there */
		host_ip = 0;
	} else {
		svc->trace_out(cm, "CMFABRIC transport connect to host_IP %lx", host_ip);
	}
	/* if ((host_name == NULL) && (host_ip == 0)) */
	/* 	return -1; */

	if (!query_attr(attrs, CM_IP_PORT, /* type pointer */ NULL,
	                /* value pointer */ (attr_value *)(long) & int_port_num)) {
		svc->trace_out(cm, "CMFABRIC transport found no IP_PORT attribute");
//		return -1;
	} else {
		svc->trace_out(cm, "CMFABRIC transport connect to port %d", int_port_num);
	}

	client_connect(cm, svc, trans, attrs, fcd);
/* 	/\* INET socket connection, host_name is the machine name *\/ */
/* 	char *network_string; */
/* 	if ((sock = socket(AF_INET, SOCK_STREAM, 0)) == SOCKET_ERROR) { */
/* 	    svc->trace_out(cm, " CMFABRIC connect FAILURE --> Couldn't create socket"); */
/* 	    return -1; */
/* 	} */
/* 	sock_addr.s.sa_family = AF_INET; */
/* 	if (host_name != NULL) { */
/* 	    if (check_host(host_name, (void *) &sock_addr.s_I4.sin_addr) == 0) { */
/* 		if (host_ip == 0) { */
/* 		    svc->trace_out(cm, "CMFABRIC connect FAILURE --> Host not found \"%s\", no IP addr supplied in contact list", host_name); */
/* 		} else { */
/* 		    svc->trace_out(cm, "CMFABRIC --> Host not found \"%s\", Using supplied IP addr %x", */
/* 				   host_name == NULL ? "(unknown)" : host_name, */
/* 				   host_ip); */
/* 		    ((struct sockaddr_in *) &sock_addr)->sin_addr.s_addr = ntohl(host_ip); */
/* 		} */
/* 	    } */
/* 	} else { */
/* 	    sock_addr.s_I4.sin_addr.s_addr = ntohl(host_ip); */
/* 	} */
/* 	sock_addr.s_I4.sin_port = htons(port_num); */
/* 	remote_IP = ntohl(sock_addr.s_I4.sin_addr.s_addr); */
/* 	if (is_private_192(remote_IP)) { */
/* 	    svc->trace_out(cm, "Target IP is on a private 192.168.x.x network"); */
/* 	} */
/* 	if (is_private_182(remote_IP)) { */
/* 	    svc->trace_out(cm, "Target IP is on a private 182.16.x.x network"); */
/* 	} */
/* 	if (is_private_10(remote_IP)) { */
/* 	    svc->trace_out(cm, "Target IP is on a private 10.x.x.x network"); */
/* 	} */
/* 	svc->trace_out(cm, "Attempting CMFABRIC socket connection, host=\"%s\", IP = %s, port %d", */
/* 		       host_name == 0 ? "(unknown)" : host_name,  */
/* 		       inet_ntoa(sock_addr.s_I4.sin_addr), */
/* 		       int_port_num); */
/* 	if (connect(sock, (struct sockaddr *) &sock_addr, */
/* 		    sizeof sock_addr) == SOCKET_ERROR) { */
/* #ifdef WSAEWOULDBLOCK */
/* 	    int err = WSAGetLastError(); */
/* 	    if (err != WSAEWOULDBLOCK || err != WSAEINPROGRESS) { */
/* #endif */
/* 		svc->trace_out(cm, "CMFABRIC connect FAILURE --> Connect() to IP %s failed", inet_ntoa(sock_addr.s_I4.sin_addr)); */
/* 		close(sock); */
/* #ifdef WSAEWOULDBLOCK */
/* 	    } */
/* #endif */
/* 	} */


//here we write out the connection port to the other side. 
//for sockets thats all thats required. For IB we can use this to exchange information about the 
//IB parameters for the other side

	svc->trace_out(cm, "--> Connection established");
	fcd->remote_host = host_name == NULL ? NULL : strdup(host_name);
	fcd->remote_IP = remote_IP;
	fcd->remote_contact_port = int_port_num;
	fcd->fd = 0;
	fcd->fabd = fabd;

	//fixed sized right now, but we will change it to a list/table
	memset(fcd->infolist, 0, sizeof(rinfo)*10);
	fcd->infocount = 0;

    

	add_attr(conn_attr_list, CM_THIS_CONN_PORT, Attr_Int4,
	         (attr_value) (long)int_port_num);
	add_attr(conn_attr_list, CM_PEER_IP, Attr_Int4,
	         (attr_value) (long)fcd->remote_IP);
/*	if (getpeername(sock, &sock_addr.s, &sock_len) == 0) {
		int_port_num = ntohs(((struct sockaddr_in *) &sock_addr)->sin_port);
		add_attr(conn_attr_list, CM_PEER_CONN_PORT, Attr_Int4,
		         (attr_value) (long)int_port_num);
		if (sock_addr.s.sa_family == AF_INET) {
			struct hostent *host;
			struct sockaddr_in *in_sock = (struct sockaddr_in *) &sock_addr;
			host = gethostbyaddr((char *) &in_sock->sin_addr,
			                     sizeof(struct in_addr), AF_INET);
			if (host != NULL) {
				fcd->remote_host = strdup(host->h_name);
				add_attr(conn_attr_list, CM_PEER_HOSTNAME, Attr_String,
				         (attr_value) strdup(host->h_name));
			}
		}
	}
*/
	svc->trace_out(fabd->cm, "Falling out of init conn\n");
	return 1;
}

/* 
 * Initiate a socket connection with another data exchange.  If port_num is -1,
 * establish a unix socket connection (name_str stores the file name of
 * the waiting socket).  Otherwise, establish an INET socket connection
 * (name_str stores the machine name).
 */
extern CMConnection
libcmfabric_LTX_initiate_conn(cm, svc, trans, attrs)
	CManager cm;
CMtrans_services svc;
transport_entry trans;
attr_list attrs;
{
    fabric_conn_data_ptr fcd = create_fabric_conn_data(svc);
    attr_list conn_attr_list = create_attr_list();
    CMConnection conn;
    int fd, ret;

    fcd->fabd = trans->trans_data;

    if (initiate_conn(cm, svc, trans, attrs, fcd, conn_attr_list, 0) < 0)
	return NULL;

    add_attr(conn_attr_list, CM_PEER_LISTEN_PORT, Attr_Int4,
	     (attr_value) (long)fcd->remote_contact_port);
    conn = svc->connection_create(trans, fcd, conn_attr_list);
    fcd->conn = conn;

    if (fi_control (&fcd->rcq->fid, FI_GETWAIT, (void *) &fd)) {
	FT_PRINTERR("fi_control(FI_GETWAIT)", ret);
    }
    svc->trace_out(cm, "Cmfabric Adding trans->data_available as action on fd %d", fd);
    svc->fd_add_select(cm, fd, (select_list_func) CMFABRIC_data_available,
		       (void *) trans, (void *) conn);

/* dump_sockinfo("initiate ", sock); */
    return conn;
}

/* 
 * Check to see that if we were to attempt to initiate a connection as
 * indicated by the attribute list, would we be connecting to ourselves?
 * For sockets, this involves checking to see if the host name is the 
 * same as ours and if the IP_PORT matches the one we are listening on.
 */
extern int
libcmfabric_LTX_self_check(CManager cm, CMtrans_services svc, transport_entry trans, attr_list attrs)
{

    fabric_client_data_ptr fd = trans->trans_data;
    int host_addr;
    int int_port_num;
    char *host_name;
    char my_host_name[256];
    static int IP = 0;

    get_IP_config(my_host_name, sizeof(host_name), &IP, NULL, NULL, NULL,
		  NULL, svc->trace_out, (void *)cm);

    if (IP == 0) {
	if (IP == 0) IP = INADDR_LOOPBACK;
    }
    if (!query_attr(attrs, CM_IP_HOSTNAME, /* type pointer */ NULL,
		    /* value pointer */ (attr_value *)(long) & host_name)) {
	svc->trace_out(cm, "CMself check CMFABRIC transport found no IP_HOST attribute");
	host_name = NULL;
    }
    if (!query_attr(attrs, CM_IP_ADDR, /* type pointer */ NULL,
		    /* value pointer */ (attr_value *)(long) & host_addr)) {
	svc->trace_out(cm, "CMself check CMFABRIC transport found no IP_ADDR attribute");
	if (host_name == NULL) return 0;
	host_addr = 0;
    }
    if (!query_attr(attrs, CM_IP_PORT, /* type pointer */ NULL,
		    /* value pointer */ (attr_value *)(long) & int_port_num)) {
	svc->trace_out(cm, "CMself check CMFABRIC transport found no IP_PORT attribute");
	return 0;
    }
    if (host_name && (strcmp(host_name, my_host_name) != 0)) {
	svc->trace_out(cm, "CMself check - Hostnames don't match");
	return 0;
    }
    if (host_addr && (IP != host_addr)) {
	svc->trace_out(cm, "CMself check - Host IP addrs don't match, %lx, %lx", IP, host_addr);
	return 0;
    }
    if (int_port_num != fd->listen_port) {
	svc->trace_out(cm, "CMself check - Ports don't match, %d, %d", int_port_num, fd->listen_port);
	return 0;
    }
    svc->trace_out(cm, "CMself check returning TRUE");
    return 1;
}

extern int
libcmfabric_LTX_connection_eq(cm, svc, trans, attrs, fcd)
	CManager cm;
CMtrans_services svc;
transport_entry trans;
attr_list attrs;
fabric_conn_data_ptr fcd;
{

	int int_port_num;
	int requested_IP = -1;
	char *host_name = NULL;

	if (!query_attr(attrs, CM_IP_HOSTNAME, /* type pointer */ NULL,
	                /* value pointer */ (attr_value *)(long) & host_name)) {
		svc->trace_out(cm, "CMFABRIC transport found no IP_HOST attribute");
	}
	if (!query_attr(attrs, CM_IP_PORT, /* type pointer */ NULL,
	                /* value pointer */ (attr_value *)(long) & int_port_num)) {
		svc->trace_out(cm, "Conn Eq CMFABRIC transport found no IP_PORT attribute");
		return 0;
	}
	if (!query_attr(attrs, CM_IP_ADDR, /* type pointer */ NULL,
	                /* value pointer */ (attr_value *)(long) & requested_IP)) {
		svc->trace_out(cm, "CMFABRIC transport found no IP_ADDR attribute");
	}
	if (requested_IP == -1) {
		check_host(host_name, (void *) &requested_IP);
		requested_IP = ntohl(requested_IP);
		svc->trace_out(cm, "IP translation for hostname %s is %x", host_name,
		               requested_IP);
	}

	svc->trace_out(cm, "Socket Conn_eq comparing IP/ports %x/%d and %x/%d",
	               fcd->remote_IP, fcd->remote_contact_port,
	               requested_IP, int_port_num);
	if ((fcd->remote_IP == requested_IP) &&
	    (fcd->remote_contact_port == int_port_num)) {
		svc->trace_out(cm, "Socket Conn_eq returning TRUE");
		return 1;
	}
	svc->trace_out(cm, "Socket Conn_eq returning FALSE");
	return 0;
}


static void free_lres(fabric_client_data_ptr fd)
{
	fi_close(&fd->cmeq->fid);
}

static int alloc_cm_res(fabric_client_data_ptr fd)
{
	struct fi_eq_attr cm_attr;
	int ret;

	memset(&cm_attr, 0, sizeof cm_attr);
	cm_attr.wait_obj = FI_WAIT_FD;
	ret = fi_eq_open(fd->fab, &cm_attr, &fd->cmeq, NULL);
	if (ret)
		FT_PRINTERR("fi_eq_open", ret);

	return ret;
}

static void free_ep_res(fabric_conn_data_ptr fcd)
{
	fi_close(&fcd->conn_ep->fid);
	fi_close(&fcd->mr->fid);
	fi_close(&fcd->rcq->fid);
	fi_close(&fcd->scq->fid);
	free(fcd->read_buf);
}

static int alloc_ep_res(fabric_conn_data_ptr fcd, struct fi_info *fi)
{
	fabric_client_data_ptr fabd = fcd->fabd;
	struct fi_cq_attr cq_attr;
	uint64_t access_mode;
	int ret;

	fcd->buffer_size = fabd->opts.user_options & FT_OPT_SIZE ?
	    fabd->opts.transfer_size : PIGGYBACK;
	
	fcd->read_buf = fabd->svc->get_data_buffer(fabd->cm, MAX(fcd->buffer_size, sizeof(uint64_t)));
	if (!fcd->read_buf) {
		perror("malloc");
		return -1;
	}
	fcd->send_buf = malloc(MAX(fcd->buffer_size, sizeof(uint64_t)));
	if (!fcd->send_buf) {
		perror("malloc");
		return -1;
	}
	fcd->max_credits = 512;
	memset(&cq_attr, 0, sizeof cq_attr);
	cq_attr.format = FI_CQ_FORMAT_DATA;
	cq_attr.wait_obj = FI_WAIT_NONE;
	cq_attr.size = fcd->max_credits << 1;
	ret = fi_cq_open(fabd->dom, &cq_attr, &fcd->scq, NULL);
	if (ret) {
		FT_PRINTERR("fi_cq_open", ret);
		goto err1;
	}

	ret = fi_cq_open(fabd->dom, &cq_attr, &fcd->rcq, NULL);
	if (ret) {
		FT_PRINTERR("fi_cq_open", ret);
		goto err2;
	}
	
//	switch (op_type) {
//	case FT_RMA_READ:
		access_mode = FI_REMOTE_READ;
//		break;
//	case FT_RMA_WRITE:
//	case FT_RMA_WRITEDATA:
//		access_mode = FI_REMOTE_WRITE;
//		break;
//	default:
//		assert(0);
//		ret = -FI_EINVAL;
//		goto err3;
//	}
	ret = fi_mr_reg(fabd->dom, fcd->read_buf, MAX(fcd->buffer_size, sizeof(uint64_t)), 
			access_mode, 0, 0, 0, &fcd->mr, NULL);
	if (ret) {
		FT_PRINTERR("fi_mr_reg", ret);
		goto err3;
	}

	ret = fi_endpoint(fabd->dom, fi, &fcd->conn_ep, NULL);
	if (ret) {
		FT_PRINTERR("fi_endpoint", ret);
		goto err4;
	}

	if (!fabd->cmeq) {
		ret = alloc_cm_res(fabd);
		if (ret)
			goto err4;
	}

	return 0;

err4:
	fi_close(&fcd->mr->fid);
err3:
	fi_close(&fcd->rcq->fid);
err2:
	fi_close(&fcd->scq->fid);
err1:
	free(fcd->read_buf);
	free(fcd->send_buf);
	return ret;
}

static int bind_ep_res(fabric_conn_data_ptr fcd)
{
	int ret;

	ret = fi_ep_bind(fcd->conn_ep, &fcd->fabd->cmeq->fid, 0);
	if (ret) {
		FT_PRINTERR("fi_ep_bind", ret);
		return ret;
	}

	ret = fi_ep_bind(fcd->conn_ep, &fcd->scq->fid, FI_SEND);
	if (ret) {
		FT_PRINTERR("fi_ep_bind", ret);
		return ret;
	}

	ret = fi_ep_bind(fcd->conn_ep, &fcd->rcq->fid, FI_RECV);
	if (ret) {
		FT_PRINTERR("fi_ep_bind", ret);
		return ret;
	}

	ret = fi_enable(fcd->conn_ep);
	if (ret) {
		FT_PRINTERR("fi_enable", ret);
		return ret;
	}
	
	/* Post the first recv buffer */
	ret = fi_recv(fcd->conn_ep, fcd->read_buf->buffer, fcd->buffer_size, fi_mr_desc(fcd->mr), 0, fcd->read_buf->buffer);
	if (ret)
		FT_PRINTERR("fi_recv", ret);

	return ret;
}

static int server_listen(fabric_client_data_ptr fd)
{
	struct fi_info *fi;
	int ret;

	ret = fi_getinfo(FT_FIVERSION, fd->opts.src_addr, fd->opts.src_port, FI_SOURCE,
			 fd->hints, &fi);
	if (ret) {
		FT_PRINTERR("fi_getinfo", ret);
		return ret;
	}

	ret = fi_fabric(fi->fabric_attr, &fd->fab, NULL);
	if (ret) {
		FT_PRINTERR("fi_fabric", ret);
		goto err0;
	}

	ret = fi_passive_ep(fd->fab, fi, &fd->listen_ep, NULL);
	if (ret) {
		FT_PRINTERR("fi_passive_ep", ret);
		goto err1;
	}

	ret = alloc_cm_res(fd);
	if (ret)
		goto err2;

	ret = fi_pep_bind(fd->listen_ep, &fd->cmeq->fid, 0);
	if (ret) {
		FT_PRINTERR("fi_pep_bind", ret);
		goto err3;
	}

	ret = fi_listen(fd->listen_ep);
	if (ret) {
		FT_PRINTERR("fi_listen", ret);
		goto err3;
	}

	fi_freeinfo(fi);
	return 0;
err3:
	free_lres(fd);
err2:
	fi_close(&fd->listen_ep->fid);
err1:
	fi_close(&fd->fab->fid);
err0:
	fi_freeinfo(fi);
	return ret;
}

static int server_connect(fabric_conn_data_ptr fcd)
{
	struct fi_eq_cm_entry entry;
	uint32_t event;
	struct fi_info *info = NULL;
	ssize_t rd;
	int ret;
	fabric_client_data_ptr fabd = fcd->fabd;

/* connection attributes, temporarily here */
	enum fi_mr_mode mr_mode;

	rd = fi_eq_sread(fabd->cmeq, &event, &entry, sizeof entry, -1, 0);
	if (rd != sizeof entry) {
		FT_PRINTERR("fi_eq_sread", rd);
		return (int) rd;
	}

	info = entry.info;
	if (event != FI_CONNREQ) {
		fprintf(stderr, "Unexpected CM event %d\n", event);
		ret = -FI_EOTHER;
		goto err1;
	}

	mr_mode = info->domain_attr->mr_mode;
	ret = fi_domain(fabd->fab, info, &fabd->dom, NULL);
	if (ret) {
		FT_PRINTERR("fi_domain", ret);
		goto err1;
	}


	ret = fi_endpoint(fabd->dom, info, &fcd->conn_ep, NULL);
	if (ret) {
		FT_PRINTERR("fi_endpoint", -ret);
		goto err1;
	}

	ret = alloc_ep_res(fcd, info);
	if (ret)
		 goto err1;

	ret = bind_ep_res(fcd);
	if (ret)
		goto err3;

	ret = fi_accept(fcd->conn_ep, NULL, 0);
	if (ret) {
		FT_PRINTERR("fi_accept", ret);
		goto err3;
	}

	rd = fi_eq_sread(fabd->cmeq, &event, &entry, sizeof entry, -1, 0);
 	if (rd != sizeof entry) {
		FT_PRINTERR("fi_eq_sread", rd);
		goto err3;
 	}

	if (event != FI_CONNECTED || entry.fid != &fcd->conn_ep->fid) {
		fprintf(stderr, "Unexpected CM event %d fid %p (ep %p)\n",
			event, entry.fid, fcd->conn_ep);
 		ret = -FI_EOTHER;
 		goto err3;
 	}
 
 	fi_freeinfo(info);
 	return 0;

err3:
	free_ep_res(fcd);
err1:
 	fi_reject(fabd->listen_ep, info->handle, NULL, 0);
 	fi_freeinfo(info);
 	return ret;
}

/* 
 * Create an passive endpoint to listen for connections
 */
extern attr_list
libcmfabric_LTX_non_blocking_listen(CManager cm, CMtrans_services svc, transport_entry trans, attr_list listen_info)
{
    fabric_client_data_ptr fd = trans->trans_data;
    unsigned int length;
    struct sockaddr_in sock_addr;
    int sock_opt_val = 1;
    int wait_sock;
    int attr_port_num = 0;
    u_short port_num = 0;
    int port_range_low, port_range_high;
    int use_hostname = 0;
    int IP, ret;
    char host_name[256];
    size_t addrlen;
    char *local_addr = NULL;

    if (cm) {
	/* assert CM is locked */
	assert(CM_LOCKED(svc, cm));
    }
    /* 
     *  Check to see if a bind to a specific port was requested
     */
    if (listen_info != NULL
	&& !query_attr(listen_info, CM_IP_PORT,
		       NULL, (attr_value *)(long) & attr_port_num)) {
	port_num = 0;
    } else {
	if (attr_port_num > USHRT_MAX || attr_port_num < 0) {
	    fprintf(stderr, "Requested port number %d is invalid\n", attr_port_num);
	    return NULL;
	}
	port_num = attr_port_num;
    }

    svc->trace_out(cm, "CMFabric begin listen, requested port %d", attr_port_num);
    get_IP_config(host_name, sizeof(host_name), &IP, &port_range_low, &port_range_high, 
		  &use_hostname, listen_info, svc->trace_out, (void *)cm);

    if (port_num != 0) {
	char *port_str = malloc(10);
	sprintf(port_str, "%d", port_num);
	fd->opts.src_port = port_str;

	/* specific port requested */
	if (server_listen(fd) != 0) {
	    free(port_str);
	    fprintf(stderr, "Cannot bind INET socket\n");
	    return NULL;
	}
    } else {
	/* port num is free.  Constrain to range 26000 : 26100 */
	int low_bound = 26000;
	int high_bound = 26100;
	int size = high_bound - low_bound;
	int tries = 10;
	int result = SOCKET_ERROR;
	srand48(time(NULL)+getpid());
	while (tries > 0) {
	    int target = low_bound + size * drand48();
	    char *port_str = malloc(10);
	    sprintf(port_str, "%d", target);
	    fd->opts.src_port = port_str;
	    port_num = target;
	    svc->trace_out(cm, "CMFABRIC trying to bind port %d", target);
	    result = server_listen(fd);
	    tries--;
	    if (result == 0) tries = 0;
	}
	if (result != 0) {
	    fprintf(stderr, "Cannot bind INET socket\n");
	    return NULL;
	}
    }

    addrlen = 0;
    ret = fi_getname(&fd->listen_ep->fid, local_addr, &addrlen);
    if (ret != -FI_ETOOSMALL) {
	FT_PRINTERR("fi_getname", ret);
	return NULL;
    }

    local_addr = malloc(addrlen);
    ret = fi_getname(&fd->listen_ep->fid, local_addr, &addrlen);
    if (ret) {
	FT_PRINTERR("fi_getname", ret);
	return NULL;
    }

//    printf("name len is %d\n", (int)addrlen);
//    for(ret = 0; ret < addrlen; ret++) {
//	printf("%02x", (unsigned char) local_addr[ret]);
//    }

    ret = fi_control (&fd->cmeq->fid, FI_GETWAIT, (void *) &wait_sock);
    if (ret) {
	FT_PRINTERR("fi_control(FI_GETWAIT)", ret);
    }

    svc->trace_out(cm, "Cmfabric Adding fabric_service_incoming as action on fd %d", wait_sock);
    svc->fd_add_select(cm, wait_sock, fabric_service_incoming,
		       (void *) trans, (void *) fd->listen_ep);
    {
	attr_list ret_list;
	
	svc->trace_out(cm, "CMFABRIC listen succeeded on port %d, fd %d",
		       port_num, wait_sock);
	ret_list = create_attr_list();
	
	fd->hostname = strdup(host_name);
	fd->listen_port = port_num;
	add_attr(ret_list, CM_TRANSPORT, Attr_String,
		 (attr_value) strdup("fabric"));
	if ((cercs_getenv("CMFabricUseHostname") != NULL) || 
	    (cercs_getenv("CM_NETWORK") != NULL)) {
	    add_attr(ret_list, CM_IP_HOSTNAME, Attr_String,
		     (attr_value) strdup(host_name));
	} else if (IP == 0) {
	    add_attr(ret_list, CM_IP_ADDR, Attr_Int4, 
		     (attr_value)INADDR_LOOPBACK);
	}
	add_attr(ret_list, CM_IP_PORT, Attr_Int4,
		 (attr_value) (long)port_num);
	
	return ret_list;
    }
}

#if defined(HAVE_WINDOWS_H) && !defined(NEED_IOVEC_DEFINE)
#define NEED_IOVEC_DEFINE
#endif

#ifdef NEED_IOVEC_DEFINE
struct iovec {
	void *iov_base;
	int iov_len;
};

#endif

extern void
libcmfabric_LTX_set_write_notify(trans, svc, fcd, enable)
	transport_entry trans;
CMtrans_services svc;
fabric_conn_data_ptr fcd;
int enable;
{
	if (enable != 0) {
		svc->fd_write_select(trans->cm, fcd->fd, (select_list_func) trans->write_possible,
		                     (void *)trans, (void *) fcd->conn);
	} else {
		/* remove entry */
		svc->fd_write_select(trans->cm, fcd->fd, NULL, NULL, NULL);
	}   
}


extern CMbuffer
libcmfabric_LTX_read_block_func(CMtrans_services svc, fabric_conn_data_ptr fcd, int *len_ptr, int *offset_ptr)
{
    *len_ptr = fcd->read_buffer_len;
    if (fcd->read_buffer) {
	CMbuffer tmp = fcd->read_buffer;
	fcd->read_buffer = NULL;
	fcd->read_buffer_len = 0;
	if (offset_ptr) *offset_ptr = fcd->read_offset;
	return tmp;
    }
    if(fcd->tb)
	return fcd->tb->buf;
    return NULL;  
}


#ifndef IOV_MAX
/* this is not defined in some places where it should be.  Conservative. */
#define IOV_MAX 16
#endif

static double reg_t = 0;

static double writev_t = 0;


extern int
libcmfabric_LTX_writev_complete_notify_func(CMtrans_services svc, 
					fabric_conn_data_ptr fcd,
					void *iovs,
					int iovcnt,
					attr_list attrs,
					CMcompletion_notify_func notify_func,
					void *notify_client_data)
{
    	int fd = fcd->fd;
	int left = 0;
	int iget = 0;
	int i;
	struct iovec * iov = (struct iovec*) iovs;
	struct iovec * tmp_iov;
	int wrlen = 0;
	double start = 0, end = 0;
	struct control_message msg; 
	int can_reuse_mapping = 0;
	rinfo *last_write_request = &fcd->infolist[fcd->infocount];

	writev_t = getlocaltime();
    
	start = getlocaltime();
    
	for (i = 0; i < iovcnt; i++) {
	    left += iov[i].iov_len;
	}

	svc->trace_out(fcd->fabd->cm, "CMFABRIC writev of %d bytes on fd %d",
	               left, fd);
	
	if (left < PIGGYBACK)
	{
		//total size is less than the piggyback size
		iget = internal_write_piggyback(svc, fcd, left, iov, iovcnt);
		if (notify_func) {
		    (notify_func)(notify_client_data);
		}
		if(iget < 0)
		{
			svc->trace_out(fcd->fabd->cm, "CMFABRIC error in writing piggyback");
			return -1;
		}
		if(iget == 0)
		{
			return iovcnt;
		}
		return -1;
	}

	svc->set_pending_write(fcd->conn);

	/* if (notify_func) { */
	/*     can_reuse_mapping = 1; */
	/*     /\* OK, we're not going to copy the data *\/ */
	/*     if (last_write_request->mrlen == iovcnt) { */
	/* 	int i; */
	/* 	for(i=0; i < last_write_request->mrlen; i++) { */
	/* 	    if ((iov[i].iov_len != last_write_request->wr->sg_list[i].length) || */
	/* 		(int64_from_ptr(iov[i].iov_base) != last_write_request->wr->sg_list[i].addr)) { */
	/* 		can_reuse_mapping = 0; */
	/* 		svc->trace_out(fcd->fabd->cm, "CMFABRIC already mapped data, doesn't match write, buf %d, %p vs. %p, %d vs. %d", */
	/* 			       i, iov[i].iov_base, last_write_request->wr->sg_list[i].addr, iov[i].iov_len, last_write_request->wr->sg_list[i].length); */
	/* 	    break; */
	/* 	    } */
	/* 	} */
	/*     } else { */
	/* 	svc->trace_out(fcd->fabd->cm, "CMFABRIC either no already mapped data, or wrong buffer count"); */
	/* 	can_reuse_mapping = 0; */
	/*     } */
	/* } else { */
	/*     svc->trace_out(fcd->fabd->cm, "CMFABRIC User-owned data with no notify, so no reuse\n"); */
	/* } */
	iget = internal_write_request(svc, fcd, left, &fcd->infolist[fcd->infocount]);
	if(iget < 0)
	{
		svc->trace_out(fcd->fabd->cm, "CMFABRIC error in writing request");
		return -1;
	}

	if (notify_func == NULL) {
	    /* it was our tmp_iov */
	    free(tmp_iov);
	}

	return iovcnt;
}

extern int
libcmfabric_LTX_writev_func(svc, fcd, iovs, iovcnt, attrs)
CMtrans_services svc;
fabric_conn_data_ptr fcd;
void *iovs;
int iovcnt;
attr_list attrs;
{
    return libcmfabric_LTX_writev_complete_notify_func(svc, fcd, iovs, iovcnt, 
						   attrs, NULL, NULL);
}

static int socket_global_init = 0;

static void
free_fabric_data(CManager cm, void *fdv)
{
	fabric_client_data_ptr fd = (fabric_client_data_ptr) fdv;
	CMtrans_services svc = fd->svc;
	if (fd->hostname != NULL)
		svc->free_func(fd->hostname);
	svc->free_func(fd);
}

extern void *
libcmfabric_LTX_initialize(CManager cm, CMtrans_services svc)
{
	static int atom_init = 0;

	fabric_client_data_ptr fabd;
	svc->trace_out(cm, "Initialize CM fabric transport built in %s\n",
	               EVPATH_LIBRARY_BUILD_DIR);
	if (atom_init == 0) {
		CM_IP_HOSTNAME = attr_atom_from_string("IP_HOST");
		CM_IP_PORT = attr_atom_from_string("IP_PORT");
		CM_IP_ADDR = attr_atom_from_string("IP_ADDR");
		CM_FD = attr_atom_from_string("CONNECTION_FILE_DESCRIPTOR");
		CM_THIS_CONN_PORT = attr_atom_from_string("THIS_CONN_PORT");
		CM_PEER_CONN_PORT = attr_atom_from_string("PEER_CONN_PORT");
		CM_PEER_IP = attr_atom_from_string("PEER_IP");
		CM_PEER_HOSTNAME = attr_atom_from_string("PEER_HOSTNAME");
		CM_PEER_LISTEN_PORT = attr_atom_from_string("PEER_LISTEN_PORT");
		CM_NETWORK_POSTFIX = attr_atom_from_string("CM_NETWORK_POSTFIX");
		CM_TRANSPORT = attr_atom_from_string("CM_TRANSPORT");
		atom_init++;
	}
	fabd = svc->malloc_func(sizeof(struct fabric_client_data));
	fabd->svc = svc;
	memset(fabd, 0, sizeof(struct fabric_client_data));
	fabd->cm = cm;
	fabd->hostname = NULL;
	fabd->listen_port = -1;
	fabd->svc = svc;
	fabd->port = 1; //need to somehow get proper port here
    
	fabd->psn = lrand48()%256;

	fabd->hints = fi_allocinfo();
	fabd->opts.src_port = "9228",

	fabd->hints->ep_attr->type	= FI_EP_MSG;
	fabd->hints->caps		= FI_MSG;
	fabd->hints->mode		= FI_LOCAL_MR;
	fabd->hints->addr_format	= FI_SOCKADDR;

	svc->add_shutdown_task(cm, free_fabric_data, (void *) fabd, FREE_TASK);

	//here we will add the first 4MB memory buffer
	LIST_INIT(&memlist);
	LIST_INIT(&uselist);

	return (void *) fabd;
}


static struct ibv_mr ** regblocks(fabric_client_data_ptr fabd,
                                  struct iovec *iovs, int iovcnt, int flags, 
                                  int *mrlen)                 
{
	int i =0;
    
	struct ibv_mr **mrlist;

    
	mrlist = (struct ibv_mr**) malloc(sizeof(struct ibv_mr *) * iovcnt);
	if(mrlist == NULL)
	{
		//failed to allocate memory - big issue
		return NULL;    
	}
    
	for(i = 0; i < iovcnt; i++)
	{
    
	}
	*mrlen = iovcnt;
    
	return mrlist;    
}



static struct ibv_send_wr * createwrlist(fabric_conn_data_ptr conn, 
                                         struct ibv_mr **mrlist,
                                         struct iovec *iovlist,
                                         int mrlen, int *wrlen)
{
	//create an array of work requests that can be posted for the transter
	int retval = 0;
	fabric_client_data_ptr fabd = conn->fabd;
	struct ibv_send_wr *wr;
    
    
	return wr;
}

static int waitoncq(fabric_conn_data_ptr fcd,
                    fabric_client_data_ptr fabd,
                    CMtrans_services svc, struct ibv_cq *cq)
{


    
	return 0;    
}



extern transport_entry
cmfabric_add_static_transport(CManager cm, CMtrans_services svc)
{
    transport_entry transport;
    transport = svc->malloc_func(sizeof(struct _transport_item));
    memset(transport, 0, sizeof(*transport));
    transport->trans_name = strdup("fabric");
    transport->cm = cm;
    transport->transport_init = (CMTransport_func)libcmfabric_LTX_initialize;
    transport->listen = (CMTransport_listen_func)libcmfabric_LTX_non_blocking_listen;
    transport->initiate_conn = (CMConnection(*)())libcmfabric_LTX_initiate_conn;
    transport->self_check = (int(*)())libcmfabric_LTX_self_check;
    transport->connection_eq = (int(*)())libcmfabric_LTX_connection_eq;
    transport->shutdown_conn = (CMTransport_shutdown_conn_func)libcmfabric_LTX_shutdown_conn;
    transport->read_block_func = (CMTransport_read_block_func)libcmfabric_LTX_read_block_func;
    transport->read_to_buffer_func = (CMTransport_read_to_buffer_func)NULL;
    transport->writev_func = (CMTransport_writev_func)libcmfabric_LTX_writev_func;
    transport->writev_complete_notify_func = (CMTransport_writev_complete_notify_func)libcmfabric_LTX_writev_complete_notify_func;
    transport->get_transport_characteristics = NULL;
    if (transport->transport_init) {
	transport->trans_data = transport->transport_init(cm, svc, transport);
    }
    return transport;
}

#endif

