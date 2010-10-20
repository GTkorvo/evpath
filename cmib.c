/***** Includes *****/
#include "config.h"
#include <sys/types.h>

#ifdef HAVE_WINDOWS_H
#include <windows.h>
#include <winsock.h>
#define getpid()	_getpid()
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
#include <sys/ioctl.h>
#include <errno.h>
#endif
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
#include "cm_internal.h"

#include <infiniband/verbs.h>

#ifndef SOCKET_ERROR
#define SOCKET_ERROR -1
#endif

#define LISTSIZE 1024
#define _WITH_IB_

#ifdef _WITH_IB_


#if defined (__INTEL_COMPILER)
#  pragma warning (disable: 869)
#  pragma warning (disable: 310)
#  pragma warning (disable: 1418)
#  pragma warning (disable: 180)
#  pragma warning (disable: 2259)
#  pragma warning (disable: 177)
#endif

typedef struct func_list_item {
    select_list_func func;
    void *arg1;
    void *arg2;
} FunctionListElement;

struct request
{
    int magic;    
    uint32_t length;    
};


struct response
{
    uint64_t remote_addr;
    uint32_t rkey;
    uint32_t max_length;    
};

struct ibparam
{
    int lid;
    int psn;
    int qpn;
    int port;
    //anything else?
};

    


#define ptr_from_int64(p) (void *)(unsigned long)(p)
#define int64_from_ptr(p) (u_int64_t)(unsigned long)(p)


typedef struct ib_client_data {
    CManager cm;
    char *hostname;
    int listen_port;
    CMtrans_services svc;
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
} *ib_client_data_ptr;


typedef enum {Block, Non_Block} socket_block_state;

typedef struct ib_connection_data {
    char *remote_host;
    int remote_IP;
    int remote_contact_port;
    int fd;
    void *read_buffer;
    int read_buffer_len;
    ib_client_data_ptr sd;
    socket_block_state block_state;
    CMConnection conn;
    struct ibv_qp *dataqp;    
    struct ibv_mr *mr;
} *ib_conn_data_ptr;

static struct ibv_qp * initqp(ib_conn_data_ptr ib_conn_data,
			      ib_client_data_ptr sd);
static int connectqp(ib_conn_data_ptr ib_conn_data,
		     ib_client_data_ptr sd,
		     struct ibparam lparam,
		     struct ibparam rparam);
static struct ibv_mr ** regblocks(ib_client_data_ptr sd,
				 struct iovec *iovs, int iovcnt, int flags,
				 int *mrlen);


static struct ibv_send_wr * createwrlist(ib_conn_data_ptr conn, 
					 struct ibv_mr **mrlist,
					 struct iovec *iovlist,
					 int mrlen, int *wrlen, 
					 struct response rep);


#ifdef WSAEWOULDBLOCK
#define EWOULDBLOCK WSAEWOULDBLOCK
#define EAGAIN WSAEINPROGRESS
#define EINTR WSAEINTR
#define errno GetLastError()
#define read(fd, buf, len) recv(fd, buf, len, 0)
#define write(fd, buf, len) send(fd, buf, len, 0)
#endif

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


inline static struct ibv_device * IB_getdevice(char *name)
{
    struct ibv_device **dev_list;
    struct ibv_device *ib_dev;

    dev_list = ibv_get_device_list(NULL);
    if(!dev_list || !*(dev_list))
    {
	fprintf(stderr, "%s %d:%s - Couldn't get IB device list\n",
		__FILE__, __LINE__, __FUNCTION__);
	return NULL;
    }
	
    if(name)
    {
	for(; (ib_dev= *dev_list); ++dev_list)
	{
	    printf("device name = %s\n", 
		   ibv_get_device_name(ib_dev));
	    if(!strcmp(ibv_get_device_name(ib_dev), name))
	    {
		break;
	    }
	}
	if(!ib_dev)
	{
	    fprintf(stderr, "%s %d:%s - Couldn't get IB device of name %s\n",
		    __FILE__, __LINE__, __FUNCTION__, name);
	}
    }
    else
	ib_dev = *dev_list; //return very first device so obtained
    if(ib_dev)
      printf("device name = %s\n", 
	     ibv_get_device_name(ib_dev));

    return ib_dev; //could be null
}


static inline uint16_t get_local_lid(struct ibv_context *context, int port)
{
    struct ibv_port_attr attr;

    if (ibv_query_port(context, port, &attr))
	return 0;

    return attr.lid;
}

static int
check_host(hostname, sin_addr)
char *hostname;
void *sin_addr;
{
#ifdef HAS_STRUCT_HOSTENT
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
#else
    /* VxWorks ? */
    *((int *) sin_addr) = hostGetByName(hostname);
    return (*(int *) sin_addr) != -1;
#endif
}

static ib_conn_data_ptr 
create_ib_conn_data(svc)
CMtrans_services svc;
{
    ib_conn_data_ptr ib_conn_data =
    svc->malloc_func(sizeof(struct ib_connection_data));
    ib_conn_data->remote_host = NULL;
    ib_conn_data->remote_contact_port = -1;
    ib_conn_data->fd = 0;
    ib_conn_data->read_buffer = NULL;
    ib_conn_data->read_buffer_len = 0;
    ib_conn_data->block_state = Block;

    return ib_conn_data;
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


void
CMIB_data_available(transport_entry trans, CMConnection conn)
{
    int iget;
    ib_client_data_ptr sd = (ib_client_data_ptr) trans->trans_data;
    CMtrans_services svc = sd->svc;
    struct request req;
    struct response rep;
    struct ibv_mr *mr;
    int retval = 0;
    struct ibv_wc wc;
    
  
  

    fprintf(stderr, "CMIB data available\n");

    ib_conn_data_ptr scd = (ib_conn_data_ptr) svc->get_transport_data(conn);

    iget = read(scd->fd, (char *) &req, sizeof(struct request));

    if (iget == 0) {
	svc->connection_close(conn);
	return;
    }
    if (iget != sizeof(struct request)) {
	int lerrno = errno;
	svc->trace_out(scd->sd->cm, "CMIB iget was %d, errno is %d, returning 0 for read",
		       iget, lerrno);
    }

    if (req.magic != 0xdeadbeef) {
	printf("Dead beef panic, 0x%x\n", req.magic);
    } else {
	printf("Would start to read %d bytes of data\n", req.length);
    }

    //find the memory for it and create the resonse

    scd->read_buffer = malloc(req.length);
    memset(scd->read_buffer, 0, req.length);    
    scd->read_buffer_len = req.length;

    //register the memory
    mr = ibv_reg_mr(scd->sd->pd, scd->read_buffer, scd->read_buffer_len,
		    IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE | 
		    IBV_ACCESS_REMOTE_READ);
  
    //create response
    rep.remote_addr = int64_from_ptr(scd->read_buffer);
    rep.rkey = mr->rkey;
    rep.max_length = scd->read_buffer_len;

    //send response to other side

    retval = ibv_req_notify_cq(sd->send_cq, 0);

    write(scd->fd, &rep, sizeof(struct response));

//    sleep(10);
    
//     void *cq_context;

//     retval = ibv_get_cq_event(sd->send_channel, 
//      			      &sd->send_cq, &cq_context);
	
//     ibv_req_notify_cq(sd->send_cq, 0);
    
    
//     //now we need to poll for completion
//     while(1)
//     {
// 	memset(&wc, 0, sizeof(wc));

	
	
// 	retval = ibv_poll_cq(sd->send_cq, 1, &wc);
// 	if(retval == 0)
// 	{
// 	    fprintf(stderr, "no event\n");
// 	}
// 	else if(retval > 0 && wc.status == IBV_WC_SUCCESS)
// 	{
// 	    //I think data transfer completed?
	    
// 	    break;	    
// 	}
// 	else
// 	{
// 	    fprintf(stderr, "error in polling\n");
// //	    sleep(1);	    
// 	}	
	
//     }

    trans->data_available(trans, conn);
}

/* 
 * Accept socket connection
 */
static void
socket_accept_conn(void_trans, void_conn_sock)
void *void_trans;
void *void_conn_sock;
{
    transport_entry trans = (transport_entry) void_trans;
    int conn_sock = (int) (long) void_conn_sock;
    ib_client_data_ptr sd = (ib_client_data_ptr) trans->trans_data;
    CMtrans_services svc = sd->svc;
    ib_conn_data_ptr ib_conn_data;
    int sock;
    struct sockaddr sock_addr;
    unsigned int sock_len = sizeof(sock_addr);
    int int_port_num;
    struct linger linger_val;
    int sock_opt_val = 1;

#ifdef TCP_NODELAY
    int delay_value = 1;
#endif
    CMConnection conn;
    attr_list conn_attr_list = NULL;

    //ib stuff
    struct ibv_qp_init_attr  qp_init_attr;
    struct ibv_qp_attr qp_attr;
    int retval = 0;
    


    svc->trace_out(NULL, "Trying to accept something, socket %d\n", conn_sock);
    linger_val.l_onoff = 1;
    linger_val.l_linger = 60;
    if ((sock = accept(conn_sock, (struct sockaddr *) 0, (unsigned int *) 0)) == SOCKET_ERROR) {
	perror("Cannot accept socket connection");
	svc->fd_remove_select(sd->cm, conn_sock);
	fprintf(stderr, "failure in Cmib  removing socket connection\n");
	return;
    }
    sock_opt_val = 1;
    setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, (char *) &sock_opt_val,
	       sizeof(sock_opt_val));
    if (setsockopt(sock, SOL_SOCKET, SO_LINGER, (char *) &linger_val,
		   sizeof(struct linger)) != 0) {
	perror("set SO_LINGER");
	return;
    }
#ifdef TCP_NODELAY
    setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, (char *) &delay_value,
	       sizeof(delay_value));
#endif
    ib_conn_data = create_ib_conn_data(svc);
    ib_conn_data->sd = sd;
    ib_conn_data->fd = sock;

    //initialize the dataqp that will be used for all RC comms
    memset(&qp_init_attr, 0, sizeof(struct ibv_qp_init_attr));
    qp_init_attr.qp_context = sd->context;
    qp_init_attr.send_cq = sd->send_cq;
    qp_init_attr.recv_cq = sd->recv_cq;
    qp_init_attr.cap.max_recv_wr = LISTSIZE;
    qp_init_attr.cap.max_send_wr = LISTSIZE;
    qp_init_attr.cap.max_send_sge = 32;
    qp_init_attr.cap.max_recv_sge = 1;
    qp_init_attr.cap.max_inline_data = 32;
    qp_init_attr.qp_type = IBV_QPT_RC;
    

    ib_conn_data->dataqp = initqp(ib_conn_data, sd);    
    if(ib_conn_data->dataqp == NULL)
    {
	svc->trace_out(sd->cm, "CMIB can't create qp\n");
	return;
	
    }


    conn_attr_list = create_attr_list();
    conn = svc->connection_create(trans, ib_conn_data, conn_attr_list);
    ib_conn_data->conn = conn;

    add_attr(conn_attr_list, CM_FD, Attr_Int4,
	     (attr_value) (long)sock);

    sock_len = sizeof(sock_addr);
    memset(&sock_addr, 0, sock_len);
    getsockname(sock, (struct sockaddr *) &sock_addr, &sock_len);
    int_port_num = ntohs(((struct sockaddr_in *) &sock_addr)->sin_port);
    add_attr(conn_attr_list, CM_THIS_CONN_PORT, Attr_Int4,
	     (attr_value) (long)int_port_num);

    memset(&sock_addr, 0, sizeof(sock_addr));
    sock_len = sizeof(sock_addr);
    if (getpeername(sock, &sock_addr, &sock_len) == 0) {
	int_port_num = ntohs(((struct sockaddr_in *) &sock_addr)->sin_port);
	add_attr(conn_attr_list, CM_PEER_CONN_PORT, Attr_Int4,
		 (attr_value) (long)int_port_num);
	ib_conn_data->remote_IP = ntohl(((struct sockaddr_in *) &sock_addr)->sin_addr.s_addr);
	add_attr(conn_attr_list, CM_PEER_IP, Attr_Int4,
		 (attr_value) (long)ib_conn_data->remote_IP);
	if (sock_addr.sa_family == AF_INET) {
#ifdef HAS_STRUCT_HOSTENT
	    struct hostent *host;
	    struct sockaddr_in *in_sock = (struct sockaddr_in *) &sock_addr;
	    host = gethostbyaddr((char *) &in_sock->sin_addr,
				 sizeof(struct in_addr), AF_INET);
	    if (host != NULL) {
		ib_conn_data->remote_host = strdup(host->h_name);
		add_attr(conn_attr_list, CM_PEER_HOSTNAME, Attr_String,
			 (attr_value) strdup(host->h_name));
	    }
#endif
	}
    }
    if (ib_conn_data->remote_host != NULL) {
	svc->trace_out(NULL, "Accepted TCP/IP socket connection from host \"%s\"",
		       ib_conn_data->remote_host);
    } else {
	svc->trace_out(NULL, "Accepted TCP/IP socket connection from UNKNOWN host");
    }

    //here we read the incoming remote contact port number. 
    //in IB we'll extend this to include ib connection parameters
    struct ibparam param, remote_param;
    param.lid  = sd->lid;
    param.qpn  = ib_conn_data->dataqp->qp_num;
    param.port = sd->port;
    param.psn  = sd->psn;

    
    if (read(sock, (char *) &ib_conn_data->remote_contact_port, 4) != 4) {
	svc->trace_out(NULL, "Remote host dropped connection without data");
	return;
    }

    if (read(sock, (char *) &remote_param, sizeof(remote_param)) != sizeof(remote_param)) {
	svc->trace_out(NULL, "CMIB Remote host dropped connection without data");
	return;
    }
    
    if(write(sock, &param, sizeof(param)) != sizeof(param))
    {
	svc->trace_out(NULL, "CMIB remote side failed to send its parameters");
	return;	
    }
    
    if(connectqp(ib_conn_data, sd, param, remote_param))
    {
	svc->trace_out(NULL, "CMIB connectqp failed in accept connection");
	return;	
    }
    
    
    ib_conn_data->remote_contact_port =
	ntohs(ib_conn_data->remote_contact_port);
    add_attr(conn_attr_list, CM_PEER_LISTEN_PORT, Attr_Int4,
	     (attr_value) (long)ib_conn_data->remote_contact_port);
    svc->trace_out(NULL, "Remote host (IP %x) is listening at port %d\n",
		   ib_conn_data->remote_IP,
		   ib_conn_data->remote_contact_port);

/* dump_sockinfo("accept ", sock); */
    svc->fd_add_select(sd->cm, sock,
		       (void (*)(void *, void *)) CMIB_data_available,
		       (void *) trans, (void *) conn);
}

extern void
libcmib_LTX_shutdown_conn(svc, scd)
CMtrans_services svc;
ib_conn_data_ptr scd;
{
    svc->fd_remove_select(scd->sd->cm, scd->fd);
    close(scd->fd);
    free(scd->remote_host);
    free(scd->read_buffer);
    free(scd);
}


#include "qual_hostname.c"

static int
is_private_192(int IP)
{
    return ((IP & 0xffff0000) == 0xC0A80000);	/* equal 192.168.x.x */
}

static int
is_private_182(int IP)
{
    return ((IP & 0xffff0000) == 0xB6100000);	/* equal 182.16.x.x */
}

static int
is_private_10(int IP)
{
    return ((IP & 0xff000000) == 0x0A000000);	/* equal 10.x.x.x */
}

static int
initiate_conn(cm, svc, trans, attrs, ib_conn_data, conn_attr_list, no_more_redirect)
CManager cm;
CMtrans_services svc;
transport_entry trans;
attr_list attrs;
ib_conn_data_ptr ib_conn_data;
attr_list conn_attr_list;
int no_more_redirect;
{
    int sock;

#ifdef TCP_NODELAY
    int delay_value = 1;
#endif
    struct linger linger_val;
    int sock_opt_val = 1;
    int int_port_num;
    u_short port_num;
    ib_client_data_ptr sd = (ib_client_data_ptr) trans->trans_data;
    char *host_name;
    int remote_IP = -1;
    static int host_ip = 0;
    unsigned int sock_len;
    struct sockaddr sock_addr;
    struct sockaddr_in *sock_addri = (struct sockaddr_in *) &sock_addr;

    //ib stuff

    struct ibv_qp_init_attr  qp_init_attr;
    struct ibv_qp_attr qp_attr;
    int retval = 0;


    if (!query_attr(attrs, CM_IP_HOSTNAME, /* type pointer */ NULL,
    /* value pointer */ (attr_value *)(long) & host_name)) {
	svc->trace_out(cm, "TCP/IP transport found no IP_HOST attribute");
	host_name = NULL;
    } else {
        svc->trace_out(cm, "TCP/IP transport connect to host %s", host_name);
    }
    if (!query_attr(attrs, CM_IP_ADDR, /* type pointer */ NULL,
    /* value pointer */ (attr_value *)(long) & host_ip)) {
	svc->trace_out(cm, "TCP/IP transport found no IP_ADDR attribute");
	/* wasn't there */
	host_ip = 0;
    } else {
        svc->trace_out(cm, "TCP/IP transport connect to host_IP %lx", host_ip);
    }
    if ((host_name == NULL) && (host_ip == 0))
	return -1;

    if (!query_attr(attrs, CM_IP_PORT, /* type pointer */ NULL,
    /* value pointer */ (attr_value *)(long) & int_port_num)) {
	svc->trace_out(cm, "TCP/IP transport found no IP_PORT attribute");
	return -1;
    } else {
        svc->trace_out(cm, "TCP/IP transport connect to port %d", int_port_num);
    }
    port_num = int_port_num;
    linger_val.l_onoff = 1;
    linger_val.l_linger = 60;

    if (int_port_num == -1) {
#if defined(AF_UNIX) && !defined(HAVE_WINDOWS_H)
	/* unix socket connection, host_name is the file name */
	struct sockaddr_un sock_addru;
	if ((sock = socket(AF_UNIX, SOCK_STREAM, 0)) < 0) {
	    return -1;
	}
	sock_addru.sun_family = AF_UNIX;
	strcpy(sock_addru.sun_path, host_name);
	if (connect(sock, (struct sockaddr *) &sock_addru,
		    sizeof sock_addru) < 0) {
	    return -1;
	}
#else
	fprintf(stderr, "socket initiate_conn port_num parameter == -1 and unix sockets not available.\n");
	return -1;
#endif
    } else {
	/* INET socket connection, host_name is the machine name */
	char *network_string;
	if ((sock = socket(AF_INET, SOCK_STREAM, 0)) == SOCKET_ERROR) {
	    svc->trace_out(cm, " CMIB connect FAILURE --> Couldn't create socket");
	    return -1;
	}
	((struct sockaddr_in *) &sock_addr)->sin_family = AF_INET;
	if (((network_string = cercs_getenv("CM_NETWORK")) != NULL) &&
	    (host_name != NULL)) {
	    int name_len = strlen(host_name) + 2 + strlen(network_string);
	    char *new_host_name = svc->malloc_func(name_len);
	    char *first_dot = strchr(host_name, '.');
	    memset(new_host_name, 0, name_len);
	    if (first_dot == NULL) {
		strcpy(new_host_name, host_name);
		strcat(new_host_name, network_string);
	    } else {
		strncpy(new_host_name, host_name, first_dot - host_name);
		strcat(new_host_name, network_string);
		strcat(new_host_name, first_dot);
	    }
	    if (check_host(new_host_name, (void *) &sock_addri->sin_addr) == 0) {
		/* host has no NETWORK interface */
		if (check_host(host_name, (void *) &sock_addri->sin_addr) == 0) {
		    svc->trace_out(cm, "--> Host not found \"%s\"",
				   host_name);
		}
	    } else {
		svc->trace_out(cm, "--> Using non default network interface with hostname %s",
			       new_host_name);
	    }
	    svc->free_func(new_host_name);
	} else {
	    if (host_name != NULL) {
		if (check_host(host_name, (void *) &sock_addri->sin_addr) == 0) {
		    if (host_ip == 0) {
			svc->trace_out(cm, "CMIB connect FAILURE --> Host not found \"%s\", no IP addr supplied in contact list", host_name);
		    } else {
			svc->trace_out(cm, "CMIB --> Host not found \"%s\", Using supplied IP addr %x",
			     host_name == NULL ? "(unknown)" : host_name,
				       host_ip);
			sock_addri->sin_addr.s_addr = ntohl(host_ip);
		    }
		}
	    } else {
		sock_addri->sin_addr.s_addr = ntohl(host_ip);
	    }
	}
	sock_addri->sin_port = htons(port_num);
	remote_IP = ntohl(sock_addri->sin_addr.s_addr);
	if (is_private_192(remote_IP)) {
	    svc->trace_out(cm, "Target IP is on a private 192.168.x.x network");
	}
	if (is_private_182(remote_IP)) {
	    svc->trace_out(cm, "Target IP is on a private 182.16.x.x network");
	}
	if (is_private_10(remote_IP)) {
	    svc->trace_out(cm, "Target IP is on a private 10.x.x.x network");
	}
	svc->trace_out(cm, "Attempting TCP/IP socket connection, host=\"%s\", IP = %s, port %d",
		       host_name == 0 ? "(unknown)" : host_name, 
		       inet_ntoa(sock_addri->sin_addr),
		       int_port_num);
	if (connect(sock, (struct sockaddr *) &sock_addr,
		    sizeof sock_addr) == SOCKET_ERROR) {
#ifdef WSAEWOULDBLOCK
	    int err = WSAGetLastError();
	    if (err != WSAEWOULDBLOCK || err != WSAEINPROGRESS) {
#endif
		svc->trace_out(cm, "CMIB connect FAILURE --> Connect() to IP %s failed", inet_ntoa(sock_addri->sin_addr));
		close(sock);
#ifdef WSAEWOULDBLOCK
	    }
#endif
	}
    }

    sock_opt_val = 1;
    setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, (char *) &sock_opt_val,
	       sizeof(sock_opt_val));
    setsockopt(sock, SOL_SOCKET, SO_LINGER, (char *) &linger_val,
	       sizeof(struct linger));

#ifdef TCP_NODELAY
    setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, (char *) &delay_value,
	       sizeof(delay_value));
#endif

    //initialize the dataqp that will be used for all RC comms

    ib_conn_data->dataqp = initqp(ib_conn_data, sd);
    if(ib_conn_data->dataqp == NULL)
    {
	svc->trace_out(sd->cm, "CMIB initqp failed in initiate_conn\n");
	return -1;	
    }
    
//here we write out the connection port to the other side. 
//for sockets thats all thats required. For IB we can use this to exchange information about the 
//IB parameters for the other side

//What does no_more_redirect check?
    if (!no_more_redirect) {
	int local_listen_port = htons(sd->listen_port);
	write(sock, &local_listen_port, 4);
	
    }

    struct ibparam param, remote_param;
    param.lid  = sd->lid;
    param.qpn  = ib_conn_data->dataqp->qp_num;
    param.port = sd->port;
    param.psn  = sd->psn;
    
    retval = write(sock, &param, sizeof(param));
    if(retval <= 0)
    {
	svc->trace_out(sd->cm, "CMIB write parameter to socket failed %d\n", retval);
	return retval;
	
    }
    
    retval = read(sock, &remote_param, sizeof(param));
    if(retval <= 0)
    {
	svc->trace_out(sd->cm, "CMIB write parameter to socket failed %d\n", retval);
	return retval;
    }    
    

    retval = connectqp(ib_conn_data, sd,
		       param, remote_param);
    if(retval)
    {
	//svc->trace_out(sd->cm, "CMIB connectqp failed in initiate connection\n");
	return -1;
	
    }
    

    svc->trace_out(cm, "--> Connection established");
    ib_conn_data->remote_host = host_name == NULL ? NULL : strdup(host_name);
    ib_conn_data->remote_IP = remote_IP;
    ib_conn_data->remote_contact_port = int_port_num;
    ib_conn_data->fd = sock;
    ib_conn_data->sd = sd;

    add_attr(conn_attr_list, CM_FD, Attr_Int4,
	     (attr_value) (long)sock);
    sock_len = sizeof(sock_addr);
    getsockname(sock, (struct sockaddr *) &sock_addr, &sock_len);
    int_port_num = ntohs(((struct sockaddr_in *) &sock_addr)->sin_port);
    add_attr(conn_attr_list, CM_THIS_CONN_PORT, Attr_Int4,
	     (attr_value) (long)int_port_num);
    add_attr(conn_attr_list, CM_PEER_IP, Attr_Int4,
	     (attr_value) (long)ib_conn_data->remote_IP);
    if (getpeername(sock, &sock_addr, &sock_len) == 0) {
	int_port_num = ntohs(((struct sockaddr_in *) &sock_addr)->sin_port);
	add_attr(conn_attr_list, CM_PEER_CONN_PORT, Attr_Int4,
		 (attr_value) (long)int_port_num);
	if (sock_addr.sa_family == AF_INET) {
#ifdef HAS_STRUCT_HOSTENT
	    struct hostent *host;
	    struct sockaddr_in *in_sock = (struct sockaddr_in *) &sock_addr;
	    host = gethostbyaddr((char *) &in_sock->sin_addr,
				 sizeof(struct in_addr), AF_INET);
	    if (host != NULL) {
		ib_conn_data->remote_host = strdup(host->h_name);
		add_attr(conn_attr_list, CM_PEER_HOSTNAME, Attr_String,
			 (attr_value) strdup(host->h_name));
	    }
#endif
	}
    }

    return sock;
}

/* 
 * Initiate a socket connection with another data exchange.  If port_num is -1,
 * establish a unix socket connection (name_str stores the file name of
 * the waiting socket).  Otherwise, establish an INET socket connection
 * (name_str stores the machine name).
 */
extern CMConnection
libcmib_LTX_initiate_conn(cm, svc, trans, attrs)
CManager cm;
CMtrans_services svc;
transport_entry trans;
attr_list attrs;
{
    ib_conn_data_ptr ib_conn_data = create_ib_conn_data(svc);
    attr_list conn_attr_list = create_attr_list();
    CMConnection conn;
    int sock;

    if ((sock = initiate_conn(cm, svc, trans, attrs, ib_conn_data, conn_attr_list, 0)) < 0)
	return NULL;

    add_attr(conn_attr_list, CM_PEER_LISTEN_PORT, Attr_Int4,
	     (attr_value) (long)ib_conn_data->remote_contact_port);
    conn = svc->connection_create(trans, ib_conn_data, conn_attr_list);
    ib_conn_data->conn = conn;

    svc->trace_out(cm, "Cmib Adding trans->data_available as action on fd %d", sock);
    svc->fd_add_select(cm, sock, (select_list_func) CMIB_data_available,
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
libcmib_LTX_self_check(cm, svc, trans, attrs)
CManager cm;
CMtrans_services svc;
transport_entry trans;
attr_list attrs;
{

    ib_client_data_ptr sd = trans->trans_data;
    int host_addr;
    int int_port_num;
    char *host_name;
    char my_host_name[256];
    static int IP = 0;

    if (IP == 0) {
	IP = get_self_ip_addr(svc);
    }
    if (!query_attr(attrs, CM_IP_HOSTNAME, /* type pointer */ NULL,
    /* value pointer */ (attr_value *)(long) & host_name)) {
	svc->trace_out(cm, "CMself check TCP/IP transport found no IP_HOST attribute");
	host_name = NULL;
    }
    if (!query_attr(attrs, CM_IP_ADDR, /* type pointer */ NULL,
    /* value pointer */ (attr_value *)(long) & host_addr)) {
	svc->trace_out(cm, "CMself check TCP/IP transport found no IP_ADDR attribute");
	if (host_name == NULL) return 0;
	host_addr = 0;
    }
    if (!query_attr(attrs, CM_IP_PORT, /* type pointer */ NULL,
    /* value pointer */ (attr_value *)(long) & int_port_num)) {
	svc->trace_out(cm, "CMself check TCP/IP transport found no IP_PORT attribute");
	return 0;
    }
    get_qual_hostname(my_host_name, sizeof(my_host_name), svc, NULL, NULL);

    if (host_name && (strcmp(host_name, my_host_name) != 0)) {
	svc->trace_out(cm, "CMself check - Hostnames don't match");
	return 0;
    }
    if (host_addr && (IP != host_addr)) {
	svc->trace_out(cm, "CMself check - Host IP addrs don't match, %lx, %lx", IP, host_addr);
	return 0;
    }
    if (int_port_num != sd->listen_port) {
	svc->trace_out(cm, "CMself check - Ports don't match, %d, %d", int_port_num, sd->listen_port);
	return 0;
    }
    svc->trace_out(cm, "CMself check returning TRUE");
    return 1;
}

extern int
libcmib_LTX_connection_eq(cm, svc, trans, attrs, scd)
CManager cm;
CMtrans_services svc;
transport_entry trans;
attr_list attrs;
ib_conn_data_ptr scd;
{

    int int_port_num;
    int requested_IP = -1;
    char *host_name = NULL;

    if (!query_attr(attrs, CM_IP_HOSTNAME, /* type pointer */ NULL,
    /* value pointer */ (attr_value *)(long) & host_name)) {
	svc->trace_out(cm, "TCP/IP transport found no IP_HOST attribute");
    }
    if (!query_attr(attrs, CM_IP_PORT, /* type pointer */ NULL,
    /* value pointer */ (attr_value *)(long) & int_port_num)) {
	svc->trace_out(cm, "Conn Eq TCP/IP transport found no IP_PORT attribute");
	return 0;
    }
    if (!query_attr(attrs, CM_IP_ADDR, /* type pointer */ NULL,
    /* value pointer */ (attr_value *)(long) & requested_IP)) {
	svc->trace_out(cm, "TCP/IP transport found no IP_ADDR attribute");
    }
    if (requested_IP == -1) {
	check_host(host_name, (void *) &requested_IP);
	requested_IP = ntohl(requested_IP);
	svc->trace_out(cm, "IP translation for hostname %s is %x", host_name,
		       requested_IP);
    }

    svc->trace_out(cm, "Socket Conn_eq comparing IP/ports %x/%d and %x/%d",
		   scd->remote_IP, scd->remote_contact_port,
		   requested_IP, int_port_num);
    if ((scd->remote_IP == requested_IP) &&
	(scd->remote_contact_port == int_port_num)) {
	svc->trace_out(cm, "Socket Conn_eq returning TRUE");
	return 1;
    }
    svc->trace_out(cm, "Socket Conn_eq returning FALSE");
    return 0;
}


/* 
 * Create an IP socket for connection from other CMs
 */
extern attr_list
libcmib_LTX_non_blocking_listen(cm, svc, trans, listen_info)
CManager cm;
CMtrans_services svc;
transport_entry trans;
attr_list listen_info;
{
    ib_client_data_ptr sd = trans->trans_data;
    unsigned int length;
    struct sockaddr_in sock_addr;
    int sock_opt_val = 1;
    int conn_sock;
    int attr_port_num = 0;
    u_short port_num = 0;
    char *network_string;

    conn_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (conn_sock == SOCKET_ERROR) {
	fprintf(stderr, "Cannot open INET socket\n");
	return NULL;
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

    svc->trace_out(cm, "CMIB begin listen, requested port %d", attr_port_num);
    sock_addr.sin_family = AF_INET;
    sock_addr.sin_addr.s_addr = INADDR_ANY;
    sock_addr.sin_port = htons(port_num);
    if (setsockopt(conn_sock, SOL_SOCKET, SO_REUSEADDR, (char *) &sock_opt_val,
		   sizeof(sock_opt_val)) != 0) {
	fprintf(stderr, "Failed to set 1REUSEADDR on INET socket\n");
	return NULL;
    }
    if (sock_addr.sin_port != 0) {
	/* specific port requested */
	svc->trace_out(cm, "CMIB trying to bind selected port %d", port_num);
	if (bind(conn_sock, (struct sockaddr *) &sock_addr,
		 sizeof sock_addr) == SOCKET_ERROR) {
	    fprintf(stderr, "Cannot bind INET socket\n");
	    return NULL;
	}
    } else {
	/* port num is free.  Constrain to range 26000 : 26100 */
	srand48(time(NULL));
	int low_bound = 26000;
	int high_bound = 26100;
	int size = high_bound - low_bound;
	int tries = 10;
	int result = SOCKET_ERROR;
	while (tries > 0) {
	    int target = low_bound + size * drand48();
	    sock_addr.sin_port = htons(target);
	    svc->trace_out(cm, "CMIB trying to bind port %d", target);
	    result = bind(conn_sock, (struct sockaddr *) &sock_addr,
			  sizeof sock_addr);
	    tries--;
	    if (result != SOCKET_ERROR) tries = 0;
	}
	if (result == SOCKET_ERROR) {
	    fprintf(stderr, "Cannot bind INET socket\n");
	    return NULL;
	}
    }
    sock_opt_val = 1;
    if (setsockopt(conn_sock, SOL_SOCKET, SO_REUSEADDR, (char *) &sock_opt_val,
		   sizeof(sock_opt_val)) != 0) {
	perror("Failed to set 2REUSEADDR on INET socket");
	return NULL;
    }
    length = sizeof sock_addr;
    if (getsockname(conn_sock, (struct sockaddr *) &sock_addr, &length) < 0) {
	fprintf(stderr, "Cannot get socket name\n");
	return NULL;
    }
    /* begin listening for conns and set the backlog */
    if (listen(conn_sock, FD_SETSIZE)) {
	fprintf(stderr, "listen failed\n");
	return NULL;
    }
    /* set the port num as one we can be contacted at */

    svc->trace_out(cm, "Cmib Adding socket_accept_conn as action on fd %d", conn_sock);
    svc->fd_add_select(cm, conn_sock, socket_accept_conn,
		       (void *) trans, (void *) (long)conn_sock);

    /* in the event the DE is shut down, close the socket */
    /* 
     *  -- Don't do this...  Close() seems to hang on sockets after 
     *  listen() for some reason.  I haven't found anywhere that defines 
     *  this behavior, but it seems relatively uniform. 
     */
    /* DExchange_add_close(de, close_socket_fd, (void*)conn_sock, NULL); */

    {
	char host_name[256];
	int int_port_num = ntohs(sock_addr.sin_port);
	attr_list ret_list;
	int IP = get_self_ip_addr(svc);
	int network_added = 0;

	svc->trace_out(cm, "CMIB listen succeeded on port %d, fd %d",
		       int_port_num, conn_sock);
	ret_list = create_attr_list();
#if !NO_DYNAMIC_LINKING
	get_qual_hostname(host_name, sizeof(host_name), svc, listen_info, 
			  &network_added);
#endif 

	sd->hostname = strdup(host_name);
	sd->listen_port = int_port_num;
	add_attr(ret_list, CM_TRANSPORT, Attr_String,
		 (attr_value) strdup("ib"));
	if ((IP != 0) && (cercs_getenv("CM_NETWORK") == NULL) &&
	    (!query_attr(listen_info, CM_NETWORK_POSTFIX, NULL,
			 (attr_value *) (long)& network_string))) {
	    add_attr(ret_list, CM_IP_ADDR, Attr_Int4,
		     (attr_value) (long)IP);
	}
	if ((cercs_getenv("CmibUseHostname") != NULL) || 
	    (cercs_getenv("CM_NETWORK") != NULL) ||
	    (query_attr(listen_info, CM_NETWORK_POSTFIX, NULL,
			 (attr_value *) (long)& network_string))) {
	    add_attr(ret_list, CM_IP_HOSTNAME, Attr_String,
		     (attr_value) strdup(host_name));
	    if (network_added) {
		if (query_attr(listen_info, CM_NETWORK_POSTFIX, NULL,
			       (attr_value *) (long)& network_string)) {
		    add_attr(ret_list, CM_NETWORK_POSTFIX, Attr_String,
			     (attr_value) strdup(network_string));
		}
	    }
	} else if (IP == 0) {
	    add_attr(ret_list, CM_IP_ADDR, Attr_Int4, 
		     (attr_value)INADDR_LOOPBACK);
	}
	add_attr(ret_list, CM_IP_PORT, Attr_Int4,
		 (attr_value) (long)int_port_num);

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
libcmib_LTX_set_write_notify(trans, svc, scd, enable)
transport_entry trans;
CMtrans_services svc;
ib_conn_data_ptr scd;
int enable;
{
    if (enable != 0) {
	svc->fd_write_select(trans->cm, scd->fd, (select_list_func) trans->write_possible,
			     (void *)trans, (void *) scd->conn);
    } else {
	/* remove entry */
	svc->fd_write_select(trans->cm, scd->fd, NULL, NULL, NULL);
    }	
}


static void
set_block_state(CMtrans_services svc, ib_conn_data_ptr scd,
		socket_block_state needed_block_state)
{
    int fdflags = fcntl(scd->fd, F_GETFL, 0);
    if (fdflags == -1) {
	perror("getflags\n");
	return;
    }
    if ((needed_block_state == Block) && (scd->block_state == Non_Block)) {
	fdflags &= ~O_NONBLOCK;
	if (fcntl(scd->fd, F_SETFL, fdflags) == -1) 
	    perror("fcntl block");
	scd->block_state = Block;
	svc->trace_out(scd->sd->cm, "CMIB switch fd %d to blocking",
		       scd->fd);
    } else if ((needed_block_state == Non_Block) && 
	       (scd->block_state == Block)) {
	fdflags |= O_NONBLOCK;
	if (fcntl(scd->fd, F_SETFL, fdflags) == -1) 
	    perror("fcntl nonblock");
	scd->block_state = Non_Block;
	svc->trace_out(scd->sd->cm, "CMIB switch fd %d to nonblocking",
		       scd->fd);
    }
}

extern CMbuffer
libcmib_LTX_read_block_func(svc, scd, len_ptr)
CMtrans_services svc;
ib_conn_data_ptr scd;
int *len_ptr;
{
  *len_ptr = scd->read_buffer_len;
  return svc->create_data_buffer(scd->sd->cm, scd->read_buffer, scd->read_buffer_len);
}

extern int
libcmib_LTX_write_func(svc, scd, buffer, length)
CMtrans_services svc;
ib_conn_data_ptr scd;
void *buffer;
int length;
{
    //this is the main function 
    //basically we follow the steps:
    //1 register the buffer memory 
    //2 create a request structure with the info for the buffer memory
    //3 send it over the socket
    //4 wait for response on the socket
    //5 read out response structure
    //6 use response to issue the rdma write
    //7 wait for write to complete
    //8 unregister memory
    //9 return

    int left = length;
    int iget = 0;
    int fd = scd->fd;
    int retval = 0;
    struct ibv_send_wr *bad_wr;
    struct ibv_mr *mr;
    struct request *r;
    struct ibv_qp_attr qp_attr;    
    struct response *resp;
    struct ibv_wc wc;
    struct ibv_qp_init_attr qp_init;

    //1. register memory
    mr = ibv_reg_mr(scd->sd->pd, buffer, length, 
		    IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_REMOTE_READ);
    if(mr  == NULL)
    {
	svc->trace_out(scd->sd->cm, "Unable to register memory %p %d\n", buffer, length);
	return -1;	
    }

    scd->mr = mr;
    
    

    //2. create request struct
    r = (struct request*)malloc(sizeof(struct request));
//    r->remote_addr = int64_from_ptr(buffer);
//    r->rkey = mr->rkey;
//    r->size = length;
    
    svc->trace_out(scd->sd->cm, "CMIB write of %d bytes on fd %d",
		   sizeof(struct request), fd);

    //3. send it over socket
    iget = write(fd, r, sizeof(struct request));
    if(iget <= 0)
    {
	svc->trace_out(scd->sd->cm, "CMIB write failed %d\n", iget);
	return iget;	
    }
    

    resp = (struct response*)malloc(sizeof(struct response));
    
    //4 . wait for response
    iget = read(fd, resp, sizeof(struct response));
    if(iget <= 0)
    {
	svc->trace_out(scd->sd->cm, "CMIB read failed %d\n", iget);
	return iget;	
    }
    
    
    //5. use the response to set up the data qp

    //6. now issue the RDMA write call
    struct ibv_sge sg = 
	{
	    .addr = int64_from_ptr(buffer),
	    .length = length,
	    .lkey = mr->lkey
	};
    
    
    struct ibv_send_wr wr = 
	{
	    .wr_id = int64_from_ptr(scd),
	    .next = NULL,
	    .sg_list = &sg,
	    .num_sge = 1,
	    .opcode = IBV_WR_RDMA_WRITE,
	    .send_flags = IBV_SEND_FENCE | IBV_SEND_SIGNALED,
	    .imm_data = 0,
	    .wr.rdma.remote_addr = int64_from_ptr(resp->remote_addr),
	    .wr.rdma.rkey = resp->rkey
	};
    
    retval = ibv_post_send(scd->dataqp, &wr, &bad_wr);
    if(retval)
    {
	svc->trace_out(scd->sd->cm, "CMIB unable to post send %d\n", retval);
	//we can get the error from the *bad_wr
	return retval;	
    }
    
	    
    //7.poll the cq to wait for completion of the transfer
    memset(&wc, 0, sizeof(wc));
    
    while(1)
    {
	
	iget = ibv_poll_cq(scd->sd->send_cq, 1, &wc);
	if(iget > 0 && wc.status == IBV_WC_SUCCESS)
	{
	    //send completeled
	    //we can break out after derigstering the memory
	    ibv_dereg_mr(mr);
	    break;	    
	}
	else if(wc.status != IBV_WC_SUCCESS)
	{
	    fprintf(stderr, "errror\n");
	    
	}
	
    }	
    return length;
}

#ifndef IOV_MAX
/* this is not defined in some places where it should be.  Conservative. */
#define IOV_MAX 16
#endif

#ifndef HAVE_WRITEV
static
int 
writev(fd, iov, iovcnt)
int fd;
struct iovec *iov;
int iovcnt;
{
    int wrote = 0;
    int i;
    for (i = 0; i < iovcnt; i++) {
	int left = iov[i].iov_len;
	int iget = 0;

	while (left > 0) {
	    iget = write(fd, (char *) iov[i].iov_base + iov[i].iov_len - left, left);
	    if (iget == -1) {
		int lerrno = errno;
		if ((lerrno != EWOULDBLOCK) &&
		    (lerrno != EAGAIN) &&
		    (lerrno != EINTR)) {
		    /* serious error */
		    return -1;
		} else {
		    if (lerrno == EWOULDBLOCK) {
			printf("Cmib write Would block, fd %d, length %d",
				       fd, left);
		    }
		    iget = 0;
		}
	    }
	    left -= iget;
	}
	wrote += iov[i].iov_len;
    }
    return wrote;
}
#endif

extern int
libcmib_LTX_writev_attr_func(svc, scd, iovs, iovcnt, attrs)
CMtrans_services svc;
ib_conn_data_ptr scd;
void *iovs;
int iovcnt;
attr_list attrs;
{
    int fd = scd->fd;
    int left = 0;
    int iget = 0;
    int iovleft, i;
    iovleft = iovcnt;
    struct iovec * iov = (struct iovec*) iovs;
    struct ibv_mr **mrlist;
    struct ibv_send_wr *wr;
    int mrlen, wrlen;
    struct ibv_send_wr *bad_wr;
    struct ibv_wc wc;    
    int retval  = 0;
    

    for (i = 0; i < iovcnt; i++)
	left += iov[i].iov_len;

    struct request req;
    struct response rep;
    
    req.magic = 0xdeadbeef;
    req.length = left;
    

    svc->trace_out(scd->sd->cm, "CMIB writev of %d bytes on fd %d",
		   left, fd);
    
    //write out request
    write(fd, &req, sizeof(struct request));

    mrlist = regblocks(scd->sd, iov, iovcnt, IBV_ACCESS_LOCAL_WRITE, &mrlen);
    if(mrlist == NULL)
    {
	return -0x10000;	
    }

    //read back response
    read(fd, &rep, sizeof(struct response));
    
    //get the workrequests
    wr = createwrlist(scd, mrlist, iov, mrlen, &wrlen,
		      rep);
    
    if(wr == NULL)
    {
	fprintf(stderr, "failed to get work request - aborting write\n");
	return -0x01000;	
    }
    
    retval = ibv_post_send(scd->dataqp, wr, &bad_wr);
    if(retval)
    {
	svc->trace_out(scd->sd->cm, "CMIB unable to post send %d\n", retval);
	//we can get the error from the *bad_wr
	return retval;	

    }
    
    memset(&wc, 0, sizeof(wc));    
    while(1)
    {
	
	iget = ibv_poll_cq(scd->sd->send_cq, 1, &wc);
	if(iget > 0 && wc.status == IBV_WC_SUCCESS)
	{
	    //send completeled
	    //we can break out after derigstering the memory
	    fprintf(stderr, "succeeded in the post send %d\n", wc.opcode);
	    
	    for(i = 0; i < mrlen; i ++)
	    {
		ibv_dereg_mr(mrlist[i]);		
		
	    }

	    free(wr->sg_list);	    
	    free(wr);
	    
	    
	    break;	    
	}
    }	

    

    return iovcnt;
}

/* non blocking version */
extern int
libcmib_LTX_NBwritev_attr_func(svc, scd, iovs, iovcnt, attrs)
CMtrans_services svc;
ib_conn_data_ptr scd;
void *iovs;
int iovcnt;
attr_list attrs;
{
    int fd = scd->fd;
    int init_bytes, left = 0;
    int iget = 0;
    int iovleft, i;
    struct iovec * iov = (struct iovec*) iovs;
    iovleft = iovcnt;

    /* sum lengths */
    for (i = 0; i < iovcnt; i++)
	left += iov[i].iov_len;

    init_bytes = left;

    svc->trace_out(scd->sd->cm, "CMIB Non-blocking writev of %d bytes on fd %d",
		   left, fd);
    set_block_state(svc, scd, Non_Block);
    while (left > 0) {
	int write_count = iovleft;
	int this_write_bytes = 0;
	if (write_count > IOV_MAX)
	    write_count = IOV_MAX;
	for (i = 0; i < write_count; i++)
	    this_write_bytes += iov[i].iov_len;

	iget = writev(fd, (struct iovec *) &iov[iovcnt - iovleft],
		      write_count);
	if (iget == -1) {
	    svc->trace_out(scd->sd->cm, "CMIB writev returned -1, errno %d",
		   errno);
	    if ((errno != EWOULDBLOCK) && (errno != EAGAIN)) {
		/* serious error */
		return -1;
	    } else {
		return init_bytes - left;
	    }
	}
	svc->trace_out(scd->sd->cm, "CMIB writev returned %d", iget);
	left -= iget;
	if (iget != this_write_bytes) {
	    /* didn't write everything, the rest would block, return */
	    svc->trace_out(scd->sd->cm, "CMIB blocked, return %d", 
			   init_bytes -left);
	    return init_bytes - left;
	}
	iovleft -= write_count;
    }
    return init_bytes - left;
}

extern int
libcmib_LTX_writev_func(svc, scd, iov, iovcnt)
CMtrans_services svc;
ib_conn_data_ptr scd;
void *iov;
int iovcnt;
{
    return libcmib_LTX_writev_attr_func(svc, scd, iov, iovcnt, NULL);
}

int socket_global_init = 0;

#ifdef HAVE_WINDOWS_H
/* Winsock init stuff, ask for ver 1.1 */
static WORD wVersionRequested = MAKEWORD(1, 1);
static WSADATA wsaData;
#endif

static void
free_socket_data(CManager cm, void *sdv)
{
    ib_client_data_ptr sd = (ib_client_data_ptr) sdv;
    CMtrans_services svc = sd->svc;
    if (sd->hostname != NULL)
	svc->free_func(sd->hostname);
    svc->free_func(sd);
}


    
extern void *
libcmib_LTX_initialize(cm, svc)
CManager cm;
CMtrans_services svc;
{
    static int atom_init = 0;

    ib_client_data_ptr socket_data;
    svc->trace_out(cm, "Initialize CM IB transport built in %s",
		   EVPATH_LIBRARY_BUILD_DIR);
    if (socket_global_init == 0) {
#ifdef SIGPIPE
	signal(SIGPIPE, SIG_IGN);
#endif
    }
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
    socket_data = svc->malloc_func(sizeof(struct ib_client_data));
    socket_data->cm = cm;
    socket_data->hostname = NULL;
    socket_data->listen_port = -1;
    socket_data->svc = svc;
    socket_data->ibdev = IB_getdevice(NULL);
    socket_data->context = ibv_open_device(socket_data->ibdev);
    socket_data->port = 1; //need to somehow get proper port here
    socket_data->lid = get_local_lid(socket_data->context, socket_data->port);
    socket_data->pd = ibv_alloc_pd(socket_data->context);
    socket_data->recv_channel = ibv_create_comp_channel(socket_data->context);
    socket_data->send_channel = ibv_create_comp_channel(socket_data->context);
    socket_data->send_cq = ibv_create_cq(socket_data->context, 1024, 
					 (void*)socket_data, socket_data->send_channel, 0);
    socket_data->recv_cq = socket_data->send_cq;
    
    

    struct ibv_srq_init_attr srq_init_attr;
    srq_init_attr.attr.max_wr = LISTSIZE;
    srq_init_attr.attr.max_sge = 32;
    srq_init_attr.attr.srq_limit = 1;
    srq_init_attr.srq_context = (void*)socket_data;

    socket_data->srq = ibv_create_srq(socket_data->pd, &srq_init_attr);
    
    socket_data->psn = lrand48()%256;

    svc->add_shutdown_task(cm, free_socket_data, (void *) socket_data);
    return (void *) socket_data;
}

static struct ibv_qp * initqp(ib_conn_data_ptr ib_conn_data,
			      ib_client_data_ptr sd)					
{
    struct ibv_qp_init_attr qp_init_attr;
    struct ibv_qp_attr qp_attr;
    struct ibv_qp *dataqp;
    int retval = 0;

    sd->svc->trace_out(sd->cm, "CMIB initqp\n");
    
    
    memset(&qp_init_attr, 0, sizeof(struct ibv_qp_init_attr));
    qp_init_attr.qp_context = sd->context;
    qp_init_attr.send_cq = sd->send_cq;
    qp_init_attr.recv_cq = sd->recv_cq;
    qp_init_attr.cap.max_recv_wr = LISTSIZE;
    qp_init_attr.cap.max_send_wr = LISTSIZE;
    qp_init_attr.cap.max_send_sge = 32;
    qp_init_attr.cap.max_recv_sge = 1;
    qp_init_attr.cap.max_inline_data = 32;
    qp_init_attr.qp_type = IBV_QPT_RC;

    dataqp = ibv_create_qp(sd->pd, &qp_init_attr);
    if(dataqp == NULL)
    {
	sd->svc->trace_out(sd->cm, "CMIB can't create qp\n");
	return NULL;	
    }

    memset(&qp_attr, 0, sizeof(qp_attr));
    qp_attr.qp_state = IBV_QPS_INIT;
    qp_attr.pkey_index = 0;
    qp_attr.port_num = sd->port;
    qp_attr.qp_access_flags = IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ | IBV_ACCESS_REMOTE_WRITE;
    qp_attr.qkey = 0x11111111;

    retval = ibv_modify_qp(dataqp, &qp_attr, 
    			   IBV_QP_STATE |
    			   IBV_QP_PKEY_INDEX | 
    			   IBV_QP_PORT | 
    			   IBV_QP_ACCESS_FLAGS);
    if(retval)
    {
	sd->svc->trace_out(sd->cm, "CMIB unable to set qp to INIT %d\n", retval);
	ibv_destroy_qp(dataqp);	
	return NULL;
    }
    

    return dataqp;
}


static int connectqp(ib_conn_data_ptr ib_conn_data,
		     ib_client_data_ptr sd,
		     struct ibparam lparam,
		     struct ibparam rparam)
{
    struct ibv_qp_attr qp_attr;
    int retval = 0;
    
    if(ib_conn_data == NULL || ib_conn_data->dataqp == NULL)
	return -1;    

    memset(&qp_attr, 0, sizeof(struct ibv_qp_attr));

    qp_attr.qp_state = IBV_QPS_RTR;
    qp_attr.dest_qp_num = rparam.qpn;
    qp_attr.rq_psn = sd->psn;
    qp_attr.sq_psn = sd->psn;
    qp_attr.ah_attr.is_global = 0;
    qp_attr.ah_attr.dlid = rparam.lid;
    qp_attr.ah_attr.sl = 0;
    qp_attr.ah_attr.src_path_bits = 0;
    qp_attr.ah_attr.port_num = rparam.port;	
    qp_attr.path_mtu = IBV_MTU_1024;
    qp_attr.max_dest_rd_atomic = 4;
    qp_attr.min_rnr_timer = 24;
    qp_attr.timeout = 28;
    qp_attr.retry_cnt = 18;
    qp_attr.rnr_retry = 18;
    qp_attr.max_rd_atomic = 4;
    
    retval = ibv_modify_qp(ib_conn_data->dataqp, &qp_attr,
			   IBV_QP_STATE |
			   IBV_QP_AV |
			   IBV_QP_PATH_MTU |
			   IBV_QP_DEST_QPN |
			   IBV_QP_RQ_PSN |  
			   IBV_QP_MIN_RNR_TIMER | IBV_QP_MAX_DEST_RD_ATOMIC);
    if(retval)
    {
	sd->svc->trace_out(sd->cm, "CMIB unable to set qp to RTR %d\n", retval);
	return retval;	

    }
    

    
    qp_attr.qp_state = IBV_QPS_RTS;
    retval = ibv_modify_qp(ib_conn_data->dataqp, &qp_attr, IBV_QP_STATE|
			   IBV_QP_TIMEOUT|
			   IBV_QP_RETRY_CNT|
			   IBV_QP_RNR_RETRY|
			   IBV_QP_SQ_PSN| IBV_QP_MAX_QP_RD_ATOMIC |
			   IBV_QP_MAX_QP_RD_ATOMIC);

    if(retval)
    {
	sd->svc->trace_out(sd->cm, "CMIB unable to set qp to RTS %d\n", retval);
	return retval;	

    }

    return 0;    
}


static struct ibv_mr ** regblocks(ib_client_data_ptr sd,
				 struct iovec *iovs, int iovcnt, int flags, 
				 int *mrlen)				  
{
    int i =0;
    
    struct ibv_mr **mrlist;

    fprintf(stderr, "iovec count = %d\n", iovcnt);
    
    mrlist = (struct ibv_mr**) malloc(sizeof(struct ibv_mr *) * iovcnt);
    if(mrlist == NULL)
    {
	//failed to allocate memory - big issue
	return NULL;	
    }
    
    for(i = 0; i < iovcnt; i++)
    {
	
	mrlist[i] = ibv_reg_mr(sd->pd, iovs[i].iov_base, 
			       iovs[i].iov_len, 
			       flags);
	if(mrlist[i] == NULL)
	{
	    fprintf(stderr, "registeration failed \n");
	    for(; i > 0; i--)
	    {
		ibv_dereg_mr(mrlist[i-1]);		
	    }
	    free(mrlist);
	    return NULL;	    
	}	
    }
    *mrlen = iovcnt;
    
    return mrlist;    
}



static struct ibv_send_wr * createwrlist(ib_conn_data_ptr conn, 
					 struct ibv_mr **mrlist,
					 struct iovec *iovlist,
					 int mrlen, int *wrlen, 
					 struct response rep)
{
    //create an array of work requests that can be posted for the transter
    struct ibv_qp_attr attr;
    struct ibv_qp_init_attr init_attr;
    struct ibv_sge *sge;
    struct ibv_send_wr *wr;
    
    
    
    memset(&attr, 0, sizeof(attr));
    memset(&init_attr, 0, sizeof(init_attr));
    
    //query to get qp params
    ibv_query_qp(conn->dataqp, &attr, IBV_QP_CAP, &init_attr);
    
    fprintf(stderr, "maximum wr = %d\t maximum sge = %d\n",
	    attr.cap.max_send_wr, attr.cap.max_send_sge);

    fprintf(stderr, "INIT\tmaximum wr = %d\t maximum sge = %d\n",
	    init_attr.cap.max_send_wr, init_attr.cap.max_send_sge);
    
    if(mrlen > attr.cap.max_send_sge)
    {
	fprintf(stderr, "too many sge fall back to slow mode\n");
	//do the slow mode here
	//TODO still
    }
    else
	*wrlen = 1;
    

    sge = (struct ibv_sge*)malloc(sizeof(struct ibv_sge) * mrlen);
    if(sge == NULL)
    {
	fprintf(stderr, "couldn't allocate memory\n");
	return NULL;
	
    }
    
    wr=(struct ibv_send_wr*)malloc(sizeof(struct ibv_send_wr)*(*wrlen));
    if(wr == NULL)
    {
	fprintf(stderr, "malloc failed for wr\n");
	free(sge);
	return NULL;	
    }
    
    
    wr->wr_id = int64_from_ptr(conn);
    wr->next = NULL;    
    wr->sg_list = sge;    
    wr->num_sge = mrlen;    
    wr->opcode = IBV_WR_RDMA_WRITE;    
    wr->send_flags = IBV_SEND_FENCE | IBV_SEND_SIGNALED;    
    wr->imm_data = 0;    
    wr->wr.rdma.remote_addr = rep.remote_addr;    
    wr->wr.rdma.rkey = rep.rkey;

    int i = 0;
    
    for(i = 0; i <mrlen; i++)
    {
	sge[i].addr = int64_from_ptr(iovlist[i].iov_base);
	sge[i].length = iovlist[i].iov_len;
	sge[i].lkey = mrlist[i]->lkey;	
    }
    
    return wr;
}



#endif
