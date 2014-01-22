/***** Includes *****/
#include "config.h"
#if defined (__INTEL_COMPILER)
#  pragma warning (disable: 1418)
#endif

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <limits.h>

#include <enet/enet.h>
#include <arpa/inet.h>
#include <time.h>
#include <sys/time.h>
 
#ifdef __MACH__
#include <mach/clock.h>
#include <mach/mach.h>
#endif

#include <atl.h>
#include <cercs_env.h>
#include "evpath.h"
#include "cm_transport.h"


typedef struct func_list_item {
    select_list_func func;
    void *arg1;
    void *arg2;
} FunctionListElement;

typedef struct enet_client_data {
    CManager cm;
    char *hostname;
    int listen_port;
    CMtrans_services svc;
    ENetHost *server;
} *enet_client_data_ptr;

typedef struct enet_connection_data {
    char *remote_host;
    int remote_IP;
    int remote_contact_port;
    ENetPeer *peer;
    CMbuffer read_buffer;
    int read_buffer_len;
    ENetPacket *packet;
    enet_client_data_ptr sd;
    CMConnection conn;
} *enet_conn_data_ptr;

static atom_t CM_PEER_IP = -1;
static atom_t CM_PEER_LISTEN_PORT = -1;
static atom_t CM_NETWORK_POSTFIX = -1;
static atom_t CM_ENET_PORT = -1;
static atom_t CM_ENET_HOSTNAME = -1;
static atom_t CM_ENET_ADDR = -1;
static atom_t CM_TRANSPORT = -1;

extern attr_list
libcmenet_LTX_non_blocking_listen(CManager cm, CMtrans_services svc,
				  transport_entry trans, attr_list listen_info);

static int
check_host(char *hostname, void *sin_addr)
{
    (void)hostname; (void)sin_addr;
#ifdef HAS_STRUCT_HOSTEN
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
#endif
    printf("Check host called, unimplemented\n");
    return 0;
}

static enet_conn_data_ptr 
create_enet_conn_data(CMtrans_services svc)
{
    enet_conn_data_ptr enet_conn_data =
    svc->malloc_func(sizeof(struct enet_connection_data));
    enet_conn_data->remote_host = NULL;
    enet_conn_data->remote_contact_port = -1;
    enet_conn_data->read_buffer = NULL;
    enet_conn_data->read_buffer_len = 1;
    return enet_conn_data;
}

static void *
enet_accept_conn(enet_client_data_ptr sd, transport_entry trans, 
		 ENetAddress *address);

static void free_func(void *packet)
{
    /* Clean up the packet now that we're done using it. */
    enet_packet_destroy ((ENetPacket*)packet);
}

static
void
enet_service_network(CManager cm, void *void_trans)
{
    transport_entry trans = (transport_entry) void_trans;
    enet_client_data_ptr ecd = (enet_client_data_ptr) trans->trans_data;
    CMtrans_services svc = ecd->svc;
    ENetEvent event;
    
    if (!ecd->server) return;
    if (!(CM_LOCKED(svc, ecd->cm))) {
	printf("Enet service network, CManager not locked\n");
    }

    /* Wait up to 1000 milliseconds for an event. */
    while (enet_host_service (ecd->server, & event, 1) > 0) {
        switch (event.type) {
	case ENET_EVENT_TYPE_NONE:
	    break;
        case ENET_EVENT_TYPE_CONNECT: {
	    void *enet_connection_data;
	    svc->trace_out(cm, "A new client connected from %x:%u.\n", 
			   event.peer -> address.host,
			   event.peer -> address.port);

	    enet_connection_data = enet_accept_conn(ecd, trans, &event.peer->address);

            /* Store any relevant client information here. */
            event.peer -> data = enet_connection_data;
	    ((enet_conn_data_ptr)enet_connection_data)->peer = event.peer;

            break;
	}
        case ENET_EVENT_TYPE_RECEIVE: {
	    enet_conn_data_ptr econn_d = event.peer->data;
	    CMbuffer cb;
	    svc->trace_out(cm, "A packet of length %u containing %s was received on channel %u.\n",
                    (unsigned int) event.packet -> dataLength,
                    event.packet -> data,
                    (unsigned int) event.channelID);
	    econn_d->read_buffer_len = event.packet -> dataLength;
	    cb = svc->create_data_and_link_buffer(cm, 
						  event.packet->data, 
						  econn_d->read_buffer_len);
	    econn_d->read_buffer = cb;
	    cb->return_callback = free_func;
	    cb->return_callback_data = event.packet;
	    econn_d->packet = event.packet;

	    /* kick this upstairs */
	    trans->data_available(trans, econn_d->conn);
	    svc->return_data_buffer(trans->cm, cb);
	    
            break;
	}           
        case ENET_EVENT_TYPE_DISCONNECT: {
	    enet_conn_data_ptr enet_conn_data = event.peer -> data;
	    svc->trace_out(NULL, "Got a disconnect on connection %p\n",
		event.peer -> data);

            enet_conn_data = event.peer -> data;
	    enet_conn_data->read_buffer_len = -1;
        }
	}
    }
}
static
void
enet_service_network_lock(CManager cm, void *void_trans)
{
    transport_entry trans = (transport_entry) void_trans;
    enet_client_data_ptr ecd = (enet_client_data_ptr) trans->trans_data;
    CMtrans_services svc = ecd->svc;

    ACQUIRE_CM_LOCK(svc, ecd->cm);
    enet_service_network(cm, void_trans);
    DROP_CM_LOCK(svc, ecd->cm);
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

/* 
 * Accept enet connection
 */
static void *
enet_accept_conn(enet_client_data_ptr sd, transport_entry trans, 
		 ENetAddress *address)
{
    CMtrans_services svc = sd->svc;
    enet_conn_data_ptr enet_conn_data;

    CMConnection conn;
    attr_list conn_attr_list = NULL;;

    enet_conn_data = create_enet_conn_data(svc);
    enet_conn_data->sd = sd;
    conn_attr_list = create_attr_list();
    conn = svc->connection_create(trans, enet_conn_data, conn_attr_list);
    enet_conn_data->conn = conn;

    add_attr(conn_attr_list, CM_PEER_IP, Attr_Int4, (void*)(long)address->host);
    enet_conn_data->remote_IP = address->host;
    enet_conn_data->remote_contact_port = address->port;

    if (enet_conn_data->remote_host != NULL) {
	svc->trace_out(NULL, "Accepted ENET RUDP connection from host \"%s\"",
		       enet_conn_data->remote_host);
    } else {
	svc->trace_out(NULL, "Accepted ENET RUDP connection from UNKNOWN host");
    }
    add_attr(conn_attr_list, CM_PEER_LISTEN_PORT, Attr_Int4,
	     (attr_value) (long)enet_conn_data->remote_contact_port);
    svc->trace_out(NULL, "Remote host (IP %x) is listening at port %d\n",
		   enet_conn_data->remote_IP,
		   enet_conn_data->remote_contact_port);
    free_attr_list(conn_attr_list);
    return enet_conn_data;
}

extern void
libcmenet_LTX_shutdown_conn(CMtrans_services svc, enet_conn_data_ptr scd)
{
    (void) svc;
    if (scd->remote_host) free(scd->remote_host);
    free(scd);
}


static int
initiate_conn(CManager cm, CMtrans_services svc, transport_entry trans,
	      attr_list attrs, enet_conn_data_ptr enet_conn_data,
	      attr_list conn_attr_list)
{
    int int_port_num;
    enet_client_data_ptr sd = (enet_client_data_ptr) trans->trans_data;
    char *host_name;
    static int host_ip = 0;
    struct in_addr sin_addr;
    (void)conn_attr_list;

    if (!query_attr(attrs, CM_ENET_HOSTNAME, /* type pointer */ NULL,
    /* value pointer */ (attr_value *)(long) & host_name)) {
	svc->trace_out(cm, "CMEnet transport found no CM_ENET_HOSTNAME attribute");
	host_name = NULL;
    } else {
        svc->trace_out(cm, "CMEnet transport connect to host %s", host_name);
    }
    if (!query_attr(attrs, CM_ENET_ADDR, /* type pointer */ NULL,
    /* value pointer */ (attr_value *)(long) & host_ip)) {
	svc->trace_out(cm, "CMEnet transport found no CM_ENET_ADDR attribute");
	/* wasn't there */
	host_ip = 0;
    } else {
        svc->trace_out(cm, "CMEnet transport connect to host_IP %lx", host_ip);
    }
    if ((host_name == NULL) && (host_ip == 0)) {
	printf("No host no IP\n");
	return 0;
    }

    if (!query_attr(attrs, CM_ENET_PORT, /* type pointer */ NULL,
    /* value pointer */ (attr_value *)(long) & int_port_num)) {
	svc->trace_out(cm, "CMEnet transport found no CM_ENET_PORT attribute");
	return 0;
    } else {
        svc->trace_out(cm, "CMEnet transport connect to port %d", int_port_num);
    }

    /* ENET connection, host_name is the machine name */
    ENetAddress address;
    ENetEvent event;
    ENetPeer *peer;
    sin_addr.s_addr = host_ip;
    svc->trace_out(cm, "Attempting ENET RUDP connection, host=\"%s\", IP = %s, port %d",
		   host_name == 0 ? "(unknown)" : host_name, 
		   inet_ntoa(sin_addr),
		   int_port_num);

    if (host_name) {
	enet_address_set_host (& address, host_name);
    } else {
	address.host = host_ip;
    }
    address.port = (unsigned short) int_port_num;

    if (sd->server == NULL) {
	libcmenet_LTX_non_blocking_listen(cm, svc, trans, NULL);
    }

    /* Initiate the connection, allocating the two channels 0 and 1. */
    peer = enet_host_connect (sd->server, & address, 1, 0);    
    
    if (peer == NULL)
    {
       fprintf (stderr, 
                "No available peers for initiating an ENet connection.\n");
       exit (EXIT_FAILURE);
    }
    
    /* Wait up to 5 seconds for the connection attempt to succeed. */
    if (enet_host_service (sd->server, & event, 5000) > 0 &&
        event.type == ENET_EVENT_TYPE_CONNECT)
    {
	svc->trace_out(cm, "Connection to %s:%d succeeded.\n", inet_ntoa(sin_addr), address.port);

    }
    else
    {
        /* Either the 5 seconds are up or a disconnect event was */
        /* received. Reset the peer in the event the 5 seconds   */
        /* had run out without any significant event.            */
        enet_peer_reset (peer);

        printf ("Connection to %s:%d failed   type was %d.\n", inet_ntoa(sin_addr), address.port, event.type);
	return 0;
    }

    svc->trace_out(cm, "--> Connection established");
    enet_conn_data->remote_host = host_name == NULL ? NULL : strdup(host_name);
    enet_conn_data->remote_IP = host_ip;
    enet_conn_data->remote_contact_port = int_port_num;
    enet_conn_data->sd = sd;
    enet_conn_data->peer = peer;
    peer->data = enet_conn_data;
    return 1;
}

/* 
 * Initiate a ENET RUDP connection with another CM.
 */
extern CMConnection
libcmenet_LTX_initiate_conn(CManager cm, CMtrans_services svc,
			    transport_entry trans, attr_list attrs)
{
    enet_conn_data_ptr enet_conn_data = create_enet_conn_data(svc);
    attr_list conn_attr_list = create_attr_list();
    CMConnection conn;

    if (!initiate_conn(cm, svc, trans, attrs, enet_conn_data, conn_attr_list))
	return NULL;

    add_attr(conn_attr_list, CM_PEER_LISTEN_PORT, Attr_Int4,
	     (attr_value) (long)enet_conn_data->remote_contact_port);
    conn = svc->connection_create(trans, enet_conn_data, conn_attr_list);
    enet_conn_data->conn = conn;

    return conn;
}

#include "qual_hostname.c"

/* 
 * Check to see that if we were to attempt to initiate a connection as
 * indicated by the attribute list, would we be connecting to ourselves?
 * For enet, this involves checking to see if the host name is the 
 * same as ours and if the CM_ENET_PORT matches the one we are listening on.
 */
extern int
libcmenet_LTX_self_check(CManager cm, CMtrans_services svc, 
			 transport_entry trans, attr_list attrs)
{

    enet_client_data_ptr sd = trans->trans_data;
    int host_addr;
    int int_port_num;
    char *host_name;
    char my_host_name[256];
    static int IP = 0;

    if (IP == 0) {
	IP = ntohl(get_self_ip_addr(svc));
    }
    if (!query_attr(attrs, CM_ENET_HOSTNAME, /* type pointer */ NULL,
    /* value pointer */ (attr_value *)(long) & host_name)) {
	svc->trace_out(cm, "CMself check CMEnet transport found no CM_ENET_HOSTNAME attribute");
	host_name = NULL;
    }
    if (!query_attr(attrs, CM_ENET_ADDR, /* type pointer */ NULL,
    /* value pointer */ (attr_value *)(long) & host_addr)) {
	svc->trace_out(cm, "CMself check CMEnet transport found no CM_ENET_ADDR attribute");
	if (host_name == NULL) return 0;
	host_addr = 0;
    }
    if (!query_attr(attrs, CM_ENET_PORT, /* type pointer */ NULL,
    /* value pointer */ (attr_value *)(long) & int_port_num)) {
	svc->trace_out(cm, "CMself check CMEnet transport found no CM_ENET_PORT attribute");
	return 0;
    }
    //get_qual_hostname(my_host_name, sizeof(my_host_name), svc, NULL, NULL);

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
libcmenet_LTX_connection_eq(CManager cm, CMtrans_services svc,
			    transport_entry trans, attr_list attrs,
			    enet_conn_data_ptr scd)
{

    int int_port_num;
    int requested_IP = -1;
    char *host_name = NULL;

    (void) trans;
    if (!query_attr(attrs, CM_ENET_HOSTNAME, /* type pointer */ NULL,
    /* value pointer */ (attr_value *)(long) & host_name)) {
	svc->trace_out(cm, "CMEnet transport found no CM_ENET_HOST attribute");
    }
    if (!query_attr(attrs, CM_ENET_PORT, /* type pointer */ NULL,
    /* value pointer */ (attr_value *)(long) & int_port_num)) {
	svc->trace_out(cm, "Conn Eq CMenet transport found no CM_ENET_PORT attribute");
	return 0;
    }
    if (!query_attr(attrs, CM_ENET_ADDR, /* type pointer */ NULL,
    /* value pointer */ (attr_value *)(long) & requested_IP)) {
	svc->trace_out(cm, "CMENET transport found no CM_ENET_ADDR attribute");
    }
    if (requested_IP == -1) {
	check_host(host_name, (void *) &requested_IP);
	svc->trace_out(cm, "IP translation for hostname %s is %x", host_name,
		       requested_IP);
    }

    svc->trace_out(cm, "ENET Conn_eq comparing IP/ports %x/%d and %x/%d",
		   scd->remote_IP, scd->remote_contact_port,
		   requested_IP, int_port_num);
    if ((scd->remote_IP == requested_IP) &&
	(scd->remote_contact_port == int_port_num)) {
	svc->trace_out(cm, "ENET Conn_eq returning TRUE");
	return 1;
    }
    svc->trace_out(cm, "ENET Conn_eq returning FALSE");
    return 0;
}

static attr_list
build_listen_attrs(CManager cm, CMtrans_services svc, enet_client_data_ptr sd,
		   attr_list listen_info, int int_port_num)
{
    char host_name[256];
    attr_list ret_list;
    int IP = ntohl(get_self_ip_addr(svc));
    int network_added = 0;
    char *network_string;
    
    svc->trace_out(cm, "CMEnet listen succeeded on port %d",
		       int_port_num);
    ret_list = create_attr_list();
#if !NO_DYNAMIC_LINKING
    get_qual_hostname(host_name, sizeof(host_name), svc, listen_info, 
		      &network_added);
#endif 

    if (sd) {
	sd->hostname = strdup(host_name);
	sd->listen_port = int_port_num;
    }
    if ((IP != 0) && (cercs_getenv("CM_NETWORK") == NULL) &&
	(!query_attr(listen_info, CM_NETWORK_POSTFIX, NULL,
		     (attr_value *) (long)& network_string))) {
	add_attr(ret_list, CM_ENET_ADDR, Attr_Int4,
		 (attr_value) (long)IP);
    }
    if ((cercs_getenv("CMEnetsUseHostname") != NULL) || 
	(cercs_getenv("CM_NETWORK") != NULL) ||
	(query_attr(listen_info, CM_NETWORK_POSTFIX, NULL,
		    (attr_value *) (long)& network_string))) {
	add_attr(ret_list, CM_ENET_HOSTNAME, Attr_String,
		 (attr_value) strdup(host_name));
	if (network_added) {
	    if (query_attr(listen_info, CM_NETWORK_POSTFIX, NULL,
			   (attr_value *) (long)& network_string)) {
	      add_attr(ret_list, CM_NETWORK_POSTFIX, Attr_String,
		       (attr_value) strdup(network_string));
	    }
	}
    } else if (IP == 0) {
        add_int_attr(ret_list, CM_ENET_ADDR, htonl(INADDR_LOOPBACK));
    }
    add_attr(ret_list, CM_ENET_PORT, Attr_Int4,
	     (attr_value) (long)int_port_num);
    
    add_attr(ret_list, CM_TRANSPORT, Attr_String,
	     (attr_value) strdup("enet"));
    return ret_list;
}

/* 
 * Create an IP socket for connection from other CMs
 */
extern attr_list
libcmenet_LTX_non_blocking_listen(CManager cm, CMtrans_services svc,
				  transport_entry trans, attr_list listen_info)
{
    enet_client_data_ptr sd = trans->trans_data;
    ENetAddress address;
    ENetHost * server;


    int attr_port_num = 0;
    u_short port_num = 0;

    /* 
     *  Check to see if a bind to a specific port was requested
     */
    if (listen_info != NULL
	&& !query_attr(listen_info, CM_ENET_PORT,
		       NULL, (attr_value *)(long) & attr_port_num)) {
	port_num = 0;
    } else {
	if (attr_port_num > USHRT_MAX || attr_port_num < 0) {
	    fprintf(stderr, "Requested port number %d is invalid\n", attr_port_num);
	    return NULL;
	}
	port_num = attr_port_num;
    }

    svc->trace_out(cm, "CMEnet begin listen, requested port %d", attr_port_num);

    address.host = ENET_HOST_ANY;

    if (sd->server != NULL) {
	/* we're already listening */
        if (port_num == 0) {
	    /* not requesting a specific port, return what we have */
	    return build_listen_attrs(cm, svc, NULL, listen_info, address.port);
	} else {
	    printf("CMlisten_specific() requesting a specific port follows other Enet operation which initiated listen at another port.  Only one listen allowed, second listen fails.\n");
	    return NULL;
	}
    }
    if (port_num != 0) {
	/* Bind the server to the default localhost.     */
	/* A specific host address can be specified by   */
	/* enet_address_set_host (& address, "x.x.x.x"); */

	address.port = port_num;

	svc->trace_out(cm, "CMEnet trying to bind selected port %d", port_num);
	server = enet_host_create (& address /* the address to bind the server host to */, 
				   0      /* allow up to 4095 clients and/or outgoing connections */,
				   1      /* allow up to 2 channels to be used, 0 and 1 */,
				   0      /* assume any amount of incoming bandwidth */,
				   0      /* assume any amount of outgoing bandwidth */);
	if (server == NULL) {
	    fprintf (stderr, 
		     "An error occurred while trying to create an ENet server host.\n");
	    return NULL;
	}
	sd->server = server;
    } else {
	long seedval = time(NULL) + getpid();
	/* port num is free.  Constrain to range 26000 : 26100 */
	int low_bound = 26000;
	int high_bound = 26100;
	int size;
	int tries;
	srand48(seedval);
    restart:
	size = high_bound - low_bound;
	tries = 10;
	while (tries > 0) {
	    int target = low_bound + size * drand48();
	    address.port = target;
	    svc->trace_out(cm, "CMEnet trying to bind port %d", target);

	    server = enet_host_create (& address /* the address to bind the server host to */, 
				       0     /* 0 means dynamic alloc clients and/or outgoing connnections */,
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
	    high_bound += 100;
	    goto restart;
	}
	sd->server = server;
    }
    svc->fd_add_select(cm, enet_host_get_sock_fd (server), 
		       (select_list_func) enet_service_network, (void*)cm, (void*)trans);
    return build_listen_attrs(cm, svc, sd, listen_info, address.port);
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

extern void *
libcmenet_LTX_read_block_func(CMtrans_services svc,
			      enet_conn_data_ptr conn_data, int *actual_len)
{
    CMbuffer cb;

    if (conn_data->read_buffer_len == -1) return NULL;

    *actual_len = conn_data->read_buffer_len;
    cb = conn_data->read_buffer;
    conn_data->read_buffer_len = 0;
    conn_data->read_buffer = NULL;
    return cb;
}

static
void current_utc_time(struct timespec *ts)
{
#ifdef __MACH__ // OS X does not have clock_gettime, use clock_get_time
    clock_serv_t cclock;
    mach_timespec_t mts;
    host_get_clock_service(mach_host_self(), CALENDAR_CLOCK, &cclock);
    clock_get_time(cclock, &mts);
    mach_port_deallocate(mach_task_self(), cclock);
    ts->tv_sec = mts.tv_sec;
    ts->tv_nsec = mts.tv_nsec;
#else
    clock_gettime(CLOCK_REALTIME, ts);
#endif
 
}

static struct timespec time_diff(struct timespec start, struct timespec end)
{
    struct timespec temp;
    if ((end.tv_nsec-start.tv_nsec)<0) {
	temp.tv_sec = end.tv_sec-start.tv_sec-1;
	temp.tv_nsec = 1000000000+end.tv_nsec-start.tv_nsec;
    } else {
	temp.tv_sec = end.tv_sec-start.tv_sec;
	temp.tv_nsec = end.tv_nsec-start.tv_nsec;
    }
    return temp;
}

extern int
libcmenet_LTX_writev_func(CMtrans_services svc, enet_conn_data_ptr ecd,
			  struct iovec *iov, int iovcnt, attr_list attrs)
{
    int i;
    int length = 0;
    static struct timespec last_flush_call = {0,0};

    (void) attrs;
    for (i = 0; i < iovcnt; i++) {
	length += iov[i].iov_len;
    }

    svc->trace_out(ecd->sd->cm, "CMENET vector write of %d bytes on peer %p",
		   length, ecd->peer);

   /* Create a reliable packet of the right size */
    ENetPacket * packet = enet_packet_create (NULL, length, 
					      ENET_PACKET_FLAG_RELIABLE);

    length = 0;
    /* copy in the data */
    for (i = 0; i < iovcnt; i++) {
	memcpy(packet->data + length, iov[i].iov_base, iov[i].iov_len);
	length += iov[i].iov_len;
    }

    /* Send the packet to the peer over channel id 0. */
    if (enet_peer_send (ecd->peer, 0, packet) == -1) return -1;
    if (last_flush_call.tv_sec == 0) {
	enet_host_flush(ecd->sd->server);
	current_utc_time(&last_flush_call);
    } else {
	struct timespec now, diff;
	current_utc_time(&now);
	diff = time_diff(last_flush_call, now);
	if (diff.tv_sec > 0) {
	    last_flush_call = now;
	    enet_host_flush(ecd->sd->server);
	}
    }
    return iovcnt;
}


static int enet_global_init = 0;

static void
free_enet_data(CManager cm, void *sdv)
{
    enet_client_data_ptr sd = (enet_client_data_ptr) sdv;
    CMtrans_services svc = sd->svc;
    (void)cm;
    if (sd->hostname != NULL)
	svc->free_func(sd->hostname);
    svc->free_func(sd);
}

static void
shutdown_enet_thread
(CManager cm, void *sdv)
{
    enet_client_data_ptr sd = (enet_client_data_ptr) sdv;
    CMtrans_services svc = sd->svc;
    (void)cm;
    if (sd->server != NULL) {
	ENetHost * server = sd->server;
	svc->fd_remove_select(cm, enet_host_get_sock_fd (server));
	sd->server = NULL;
	enet_host_destroy(server);
    }
}

extern void *
libcmenet_LTX_initialize(CManager cm, CMtrans_services svc,
			 transport_entry trans, attr_list attrs)
{
    static int atom_init = 0;

    enet_client_data_ptr enet_data;
    (void)attrs;
    svc->trace_out(cm, "Initialize ENET reliable UDP transport built in %s",
		   EVPATH_LIBRARY_BUILD_DIR);
    if (enet_global_init == 0) {
	if (enet_initialize () != 0) {
	    fprintf (stderr, "An error occurred while initializing ENet.\n");
	    //return EXIT_FAILURE;
	}
    }
    if (atom_init == 0) {
	CM_ENET_HOSTNAME = attr_atom_from_string("CM_ENET_HOST");
	CM_ENET_PORT = attr_atom_from_string("CM_ENET_PORT");
	CM_ENET_ADDR = attr_atom_from_string("CM_ENET_ADDR");
	CM_TRANSPORT = attr_atom_from_string("CM_TRANSPORT");
	CM_PEER_IP = attr_atom_from_string("PEER_IP");
	CM_PEER_LISTEN_PORT = attr_atom_from_string("PEER_LISTEN_PORT");
	CM_NETWORK_POSTFIX = attr_atom_from_string("CM_NETWORK_POSTFIX");
	atom_init++;
    }
    enet_data = svc->malloc_func(sizeof(struct enet_client_data));
    enet_data->cm = cm;
    enet_data->hostname = NULL;
    enet_data->listen_port = -1;
    enet_data->svc = svc;
    enet_data->server = NULL;

    svc->add_shutdown_task(cm, shutdown_enet_thread, (void *) enet_data, SHUTDOWN_TASK);
    svc->add_shutdown_task(cm, free_enet_data, (void *) enet_data, FREE_TASK);
    svc->add_periodic_task(cm, 1, 0, enet_service_network_lock, (void*)trans);
    return (void *) enet_data;
}

extern transport_entry
cmenet_add_static_transport(CManager cm, CMtrans_services svc)
{
    transport_entry transport;
    transport = svc->malloc_func(sizeof(struct _transport_item));
    transport->trans_name = strdup("enet");
    transport->cm = cm;
    transport->transport_init = (CMTransport_func)libcmenet_LTX_initialize;
    transport->listen = (CMTransport_listen_func)libcmenet_LTX_non_blocking_listen;
    transport->initiate_conn = (CMConnection(*)())libcmenet_LTX_initiate_conn;
    transport->self_check = (int(*)())libcmenet_LTX_self_check;
    transport->connection_eq = (int(*)())libcmenet_LTX_connection_eq;
    transport->shutdown_conn = (CMTransport_shutdown_conn_func)libcmenet_LTX_shutdown_conn;
    transport->read_block_func = (CMTransport_read_block_func)libcmenet_LTX_read_block_func;
    transport->read_to_buffer_func = (CMTransport_read_to_buffer_func)NULL;
    transport->writev_func = (CMTransport_writev_func)libcmenet_LTX_writev_func;
    transport->get_transport_characteristics = NULL;
    if (transport->transport_init) {
	transport->trans_data = transport->transport_init(cm, svc, transport);
    }
    return transport;
}
