/***** Includes *****/
#include "config.h"
#include <sys/types.h>

#ifdef HAVE_WINDOWS_H
#include <windows.h>
#include <winsock.h>
#define getpid()	_getpid()
#else
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
#include "redirect.h"

#ifndef SOCKET_ERROR
#define SOCKET_ERROR -1
#endif

typedef struct func_list_item {
    select_list_func func;
    void *arg1;
    void *arg2;
} FunctionListElement;

typedef struct socket_client_data {
    CManager cm;
    char *hostname;
    int listen_port;
    CMtrans_services svc;
} *socket_client_data_ptr;

typedef enum {Block, Non_Block} socket_block_state;

typedef struct socket_connection_data {
    char *remote_host;
    int remote_IP;
    int remote_contact_port;
    int fd;
    void *read_buffer;
    int read_buffer_len;
    socket_client_data_ptr sd;
    socket_block_state block_state;
    CMConnection conn;
} *socket_conn_data_ptr;

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

static socket_conn_data_ptr 
create_socket_conn_data(svc)
CMtrans_services svc;
{
    socket_conn_data_ptr socket_conn_data =
    svc->malloc_func(sizeof(struct socket_connection_data));
    socket_conn_data->remote_host = NULL;
    socket_conn_data->remote_contact_port = -1;
    socket_conn_data->fd = 0;
    socket_conn_data->read_buffer = svc->malloc_func(1);
    socket_conn_data->read_buffer_len = 1;
    socket_conn_data->block_state = Block;
    return socket_conn_data;
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
 * Accept socket connection
 */
static void
socket_accept_conn(void_trans, void_conn_sock)
void *void_trans;
void *void_conn_sock;
{
    transport_entry trans = (transport_entry) void_trans;
    int conn_sock = (int) (long) void_conn_sock;
    socket_client_data_ptr sd = (socket_client_data_ptr) trans->trans_data;
    CMtrans_services svc = sd->svc;
    socket_conn_data_ptr socket_conn_data;
    int sock;
    struct sockaddr sock_addr;
    int sock_len = sizeof(sock_addr);
    int int_port_num;
    struct linger linger_val;
    int sock_opt_val = 1;

#ifdef TCP_NODELAY
    int delay_value = 1;
#endif
    CMConnection conn;
    attr_list conn_attr_list = NULL;;

    svc->trace_out(NULL, "Trying to accept something, socket %d\n", conn_sock);
    linger_val.l_onoff = 1;
    linger_val.l_linger = 60;
    if ((sock = accept(conn_sock, (struct sockaddr *) 0, (int *) 0)) == SOCKET_ERROR) {
	perror("Cannot accept socket connection");
	svc->fd_remove_select(sd->cm, conn_sock);
	fprintf(stderr, "failure in CMsockets  removing socket connection\n");
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
    socket_conn_data = create_socket_conn_data(svc);
    socket_conn_data->sd = sd;
    socket_conn_data->fd = sock;
    conn_attr_list = create_attr_list();
    conn = svc->connection_create(trans, socket_conn_data, conn_attr_list);
    socket_conn_data->conn = conn;

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
    if (getpeername(sock, &sock_addr,
		    &sock_len) == 0) {
	int_port_num = ntohs(((struct sockaddr_in *) &sock_addr)->sin_port);
	add_attr(conn_attr_list, CM_PEER_CONN_PORT, Attr_Int4,
		 (attr_value) (long)int_port_num);
	socket_conn_data->remote_IP = ntohl(((struct sockaddr_in *) &sock_addr)->sin_addr.s_addr);
	add_attr(conn_attr_list, CM_PEER_IP, Attr_Int4,
		 (attr_value) (long)socket_conn_data->remote_IP);
	if (sock_addr.sa_family == AF_INET) {
#ifdef HAS_STRUCT_HOSTENT
	    struct hostent *host;
	    struct sockaddr_in *in_sock = (struct sockaddr_in *) &sock_addr;
	    host = gethostbyaddr((char *) &in_sock->sin_addr,
				 sizeof(struct in_addr), AF_INET);
	    if (host != NULL) {
		socket_conn_data->remote_host = strdup(host->h_name);
		add_attr(conn_attr_list, CM_PEER_HOSTNAME, Attr_String,
			 (attr_value) strdup(host->h_name));
	    }
#endif
	}
    }
    if (socket_conn_data->remote_host != NULL) {
	svc->trace_out(NULL, "Accepted TCP/IP socket connection from host \"%s\"",
		       socket_conn_data->remote_host);
    } else {
	svc->trace_out(NULL, "Accepted TCP/IP socket connection from UNKNOWN host");
    }
    if (read(sock, (char *) &socket_conn_data->remote_contact_port, 4) != 4) {
	svc->trace_out(NULL, "Remote host dropped connection without data");
	return;
    }
    socket_conn_data->remote_contact_port =
	ntohs(socket_conn_data->remote_contact_port);
    add_attr(conn_attr_list, CM_PEER_LISTEN_PORT, Attr_Int4,
	     (attr_value) (long)socket_conn_data->remote_contact_port);
    svc->trace_out(NULL, "Remote host (IP %x) is listening at port %d\n",
		   socket_conn_data->remote_IP,
		   socket_conn_data->remote_contact_port);

/* dump_sockinfo("accept ", sock); */
    if (trans->data_available) {
        svc->fd_add_select(sd->cm, sock,
                           (void (*)(void *, void *)) trans->data_available,
                           (void *) trans, (void *) conn);
    }
}

extern void
libcmsockets_LTX_shutdown_conn(svc, scd)
CMtrans_services svc;
socket_conn_data_ptr scd;
{
    svc->fd_remove_select(scd->sd->cm, scd->fd);
    close(scd->fd);
    free(scd->remote_host);
    free(scd->read_buffer);
    free(scd);
}


#include "qual_hostname.c"

#ifndef REDIRECT_SERVER_HOST
#define REDIRECT_SERVER_HOST "redirecthost.cercs.gatech.edu";
#endif

/* Send a message to redirect server */
static int
send_msg_to_redirect_server(cm, svc, msg)
CManager cm;
CMtrans_services svc;
redirect_msg_ptr msg;
{
    int redirect_sock;
    struct sockaddr_in redirect_server_addr;
    struct sockaddr_in *redirect_server_addri = (struct sockaddr_in *) &redirect_server_addr;
    char *redirect_server_host;

    /* Create a stream socket */
    redirect_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (redirect_sock == SOCKET_ERROR) {
	fprintf(stderr, "Cannot open INET socket\n");
	return -1;
    }
    /* Bind an address to the socket */
    memset((char *) &redirect_server_addr, 0, sizeof(struct sockaddr_in));
    redirect_server_addr.sin_family = AF_INET;

    redirect_server_host = cercs_getenv("REDIRECT_SERVER_HOST");
    if (redirect_server_host == NULL) {
	redirect_server_host = REDIRECT_SERVER_HOST;	/* from configure */
    }
    if (check_host(redirect_server_host, (void *) &redirect_server_addri->sin_addr) == 0) {
	svc->trace_out(cm, "CMSocket connect FAILURE --> Redirect server host not found %s", redirect_server_host);
	return -1;
    }
    redirect_server_addr.sin_port = htons(RS_PORT);

    /* Connecting to the server */
    if (connect(redirect_sock, (struct sockaddr *) &redirect_server_addr,
		sizeof redirect_server_addr) == SOCKET_ERROR) {
#ifdef WSAEWOULDBLOCK
	int err = WSAGetLastError();
	if (err != WSAEWOULDBLOCK || err != WSAEINPROGRESS) {
#endif
	    svc->trace_out(cm, "CMSocket redirect server connect FAILURE");
	    close(redirect_sock);
	    return -1;
#ifdef WSAEWOULDBLOCK
	}
#endif
    } {
	int iget = 0;
	int len = strlen(msg->content) + 2;
	int left = len + 4;
	int hton_len = htons(len);
	char *buffer = (char *) svc->malloc_func(len + 4);
	memcpy(buffer, &hton_len, 4);
	buffer[4] = msg->type;
	strncpy(buffer + 5, msg->content, len - 1);

	/* send message out */
	while (left > 0) {
	    iget = write(redirect_sock, (char *) buffer + len + 4 - left, left);
	    if (iget == -1) {
		int lerrno = errno;
		if ((lerrno != EWOULDBLOCK) &&
		    (lerrno != EAGAIN) &&
		    (lerrno != EINTR)) {
		    /* serious error */
		    fprintf(stderr, "Send message to redirect server fail\n");
		} else {
		    iget = 0;
		}
	    }
	    left -= iget;
	}
	svc->free_func(buffer);
    }

    return redirect_sock;
}

/* Tell redirect server to let it connect to me instead */
static int
request_redirect(cm, svc, attrs)
CManager cm;
CMtrans_services svc;
attr_list attrs;
{
    int length;
    struct sockaddr_in sock_addr;
    int conn_sock;
    int int_port_num = 0;
    u_short port_num = 0;
    attr_list conn_attr_list;
    char redir_response = REDIRECTION_REQUESTED;

    conn_attr_list = create_attr_list();
    svc->trace_out(cm, "CMSocket request redirect");

    /* creat listen socket */
    conn_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (conn_sock == SOCKET_ERROR) {
	fprintf(stderr, "Cannot open INET socket\n");
	return -1;
    }
    svc->trace_out(cm, "CMSocket begin listen, requested port %d", int_port_num);
    sock_addr.sin_family = AF_INET;
    sock_addr.sin_addr.s_addr = INADDR_ANY;
    sock_addr.sin_port = htons(port_num);
    if (bind(conn_sock, (struct sockaddr *) &sock_addr,
	     sizeof sock_addr) == SOCKET_ERROR) {
	fprintf(stderr, "Cannot bind INET socket\n");
	return -1;
    }
    length = sizeof sock_addr;
    if (getsockname(conn_sock, (struct sockaddr *) &sock_addr, &length) < 0) {
	fprintf(stderr, "Cannot get socket name\n");
	return -1;
    }
    int_port_num = ntohs(sock_addr.sin_port);

    /* add local listen addr to attrs */
    {
	char host_name[256];
	int IP = get_self_ip_addr(svc);
	char *peer_host_name;
	int peer_listen_port = -1;
	int peer_ip = 0;
	int network_added = 0;

	get_qual_hostname(host_name, sizeof(host_name), svc, NULL, 
			  &network_added);

	if ((IP != 0) && (cercs_getenv("CM_NETWORK") == NULL)) {
	    add_attr(conn_attr_list, CM_IP_ADDR, Attr_Int4,
		     (attr_value) (long)IP);
	} else {
	    add_attr(conn_attr_list, CM_IP_HOSTNAME, Attr_String,
		     (attr_value) strdup(host_name));
	    if (network_added) {
		char *network_string = NULL;
		if (query_attr(attrs, CM_NETWORK_POSTFIX, NULL,
			       (attr_value *) (long)& network_string)) {
		    add_attr(conn_attr_list, CM_NETWORK_POSTFIX, Attr_String,
			     (attr_value) strdup(network_string));
		}
	    }
	}
	add_attr(conn_attr_list, CM_IP_PORT, Attr_Int4,
		 (attr_value) (long)int_port_num);

	if (!query_attr(attrs, CM_IP_HOSTNAME, /* type pointer */ NULL,
	/* value pointer */ (attr_value *) (long)& peer_host_name)) {
	    svc->trace_out(cm, "TCP/IP transport found no IP_HOST attribute");
	    peer_host_name = NULL;
	}
	if (peer_host_name != NULL)
	    add_attr(conn_attr_list, CM_PEER_HOSTNAME, Attr_String,
		     (attr_value) strdup(peer_host_name));

	if (!query_attr(attrs, CM_IP_ADDR, /* type pointer */ NULL,
	/* value pointer */ (attr_value *)(long) & peer_ip)) {
	    svc->trace_out(cm, "TCP/IP transport found no IP_ADDR attribute");
	    peer_ip = 0;
	}
	if (peer_ip != 0)
	    add_attr(conn_attr_list, CM_PEER_IP, Attr_Int4,
		     (attr_value) (long)peer_ip);
	if ((peer_host_name == NULL) && (peer_ip == 0)) {
	    svc->trace_out(cm, "No HOST_NAME and HOST_IP attribute");
	    return -1;
	}
	if (!query_attr(attrs, CM_IP_PORT, /* type pointer */ NULL,
	/* value pointer */ (attr_value *)(long) & peer_listen_port)) {
	    svc->trace_out(cm, "TCP/IP transport found no IP_PORT attribute");
	    return -1;
	}
	add_attr(conn_attr_list, CM_PEER_LISTEN_PORT, Attr_Int4,
		 (attr_value) (long)peer_listen_port);
    }

    if (listen(conn_sock, 1)) {
	fprintf(stderr, "listen failed\n");
	return -1;
    }
    svc->trace_out(cm, "CMSocket listen succeeded on port %d",
		   int_port_num);


    /* send request redirect message */
    {
	int request_sock;
	redirect_msg request_msg;
	char *attr_str = attr_list_to_string(conn_attr_list);

	request_msg.type = REQUEST;
	request_msg.content = svc->malloc_func(strlen(attr_str) + 1);
	strcpy(request_msg.content, attr_str);
	if ((request_sock = send_msg_to_redirect_server(cm, svc, &request_msg)) < 0)
	    return -1;
	svc->trace_out(cm, "Send out request redirect message");
	svc->free_func(attr_str);
	if (read(request_sock, &redir_response, 1) != 1) {
	    redir_response = REDIRECTION_IMPOSSIBLE;
	}
	close(request_sock);
    }

    if (redir_response == REDIRECTION_REQUESTED) {
	int redirect_sock;
	int client_len;
	struct sockaddr_in client;

	client_len = sizeof(client);
	if ((redirect_sock = accept(conn_sock, (struct sockaddr *) &client, &client_len)) == -1) {
	    fprintf(stderr, "Can't accept client\n");
	    return -1;
	}
	close(conn_sock);

	return redirect_sock;
    } else {
	return -1;
    }
}

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
initiate_conn(cm, svc, trans, attrs, socket_conn_data, conn_attr_list, no_more_redirect)
CManager cm;
CMtrans_services svc;
transport_entry trans;
attr_list attrs;
socket_conn_data_ptr socket_conn_data;
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
    socket_client_data_ptr sd = (socket_client_data_ptr) trans->trans_data;
    char *host_name;
    int remote_IP = -1;
    int IP = get_self_ip_addr(svc);
    static int host_ip = 0;
    int sock_len;
    struct sockaddr sock_addr;
    struct sockaddr_in *sock_addri = (struct sockaddr_in *) &sock_addr;

    int redirect_needed = 0;	/* set to true if we should try the
				 * redirect server instead of making a
				 * direct connection */

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
	    svc->trace_out(cm, " CMSocket connect FAILURE --> Couldn't create socket");
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
		    redirect_needed = 1;
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
			svc->trace_out(cm, "CMSocket connect FAILURE --> Host not found \"%s\", no IP addr supplied in contact list", host_name);
			redirect_needed = 1;
		    } else {
			svc->trace_out(cm, "CMSOCKET --> Host not found \"%s\", Using supplied IP addr %x",
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
	if ((is_private_192(remote_IP) && !is_private_192(IP)) ||
	    (is_private_182(remote_IP) && !is_private_182(IP))/* ||
	    (is_private_10(remote_IP) && !is_private_10(IP))*/) {
	    /* 
	     * if the target address is on a reserved private network and
	     * our IP address is not also on the same type of private
	     * network, then we certainly need redirection because a
	     * connection will not succeed.
	     */
	    redirect_needed = 1;
	}
	if (!redirect_needed) {
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
		    svc->trace_out(cm, "CMSocket connect FAILURE --> Connect() to IP %s failed", inet_ntoa(sock_addri->sin_addr));
		    close(sock);
		    redirect_needed = 1;
#ifdef WSAEWOULDBLOCK
		}
#endif
	    }
	}
    }

    if (redirect_needed) {
	if ((sock = request_redirect(cm, svc, attrs)) < 0) {
	    svc->trace_out(cm, "CMSocket request redirect failed");
	    return -1;
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

    if (!no_more_redirect) {
	int local_listen_port = htons(sd->listen_port);
	write(sock, &local_listen_port, 4);
    }
    svc->trace_out(cm, "--> Connection established");
    socket_conn_data->remote_host = host_name == NULL ? NULL : strdup(host_name);
    socket_conn_data->remote_IP = remote_IP;
    socket_conn_data->remote_contact_port = int_port_num;
    socket_conn_data->fd = sock;
    socket_conn_data->sd = sd;

    add_attr(conn_attr_list, CM_FD, Attr_Int4,
	     (attr_value) (long)sock);
    sock_len = sizeof(sock_addr);
    getsockname(sock, (struct sockaddr *) &sock_addr, &sock_len);
    int_port_num = ntohs(((struct sockaddr_in *) &sock_addr)->sin_port);
    add_attr(conn_attr_list, CM_THIS_CONN_PORT, Attr_Int4,
	     (attr_value) (long)int_port_num);
    add_attr(conn_attr_list, CM_PEER_IP, Attr_Int4,
	     (attr_value) (long)socket_conn_data->remote_IP);
    if (getpeername(sock, &sock_addr,
		    &sock_len) == 0) {
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
		socket_conn_data->remote_host = strdup(host->h_name);
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
libcmsockets_LTX_initiate_conn(cm, svc, trans, attrs)
CManager cm;
CMtrans_services svc;
transport_entry trans;
attr_list attrs;
{
    socket_conn_data_ptr socket_conn_data = create_socket_conn_data(svc);
    attr_list conn_attr_list = create_attr_list();
    CMConnection conn;
    int sock;

    if ((sock = initiate_conn(cm, svc, trans, attrs, socket_conn_data, conn_attr_list, 0)) < 0)
	return NULL;

    add_attr(conn_attr_list, CM_PEER_LISTEN_PORT, Attr_Int4,
	     (attr_value) (long)socket_conn_data->remote_contact_port);
    conn = svc->connection_create(trans, socket_conn_data, conn_attr_list);
    socket_conn_data->conn = conn;

    svc->trace_out(cm, "CMSockets Adding trans->data_available as action on fd %d", sock);
    if (trans->data_available) {
        svc->fd_add_select(cm, sock, (select_list_func) trans->data_available,
                           (void *) trans, (void *) conn);
    }

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
libcmsockets_LTX_self_check(cm, svc, trans, attrs)
CManager cm;
CMtrans_services svc;
transport_entry trans;
attr_list attrs;
{

    socket_client_data_ptr sd = trans->trans_data;
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
libcmsockets_LTX_connection_eq(cm, svc, trans, attrs, scd)
CManager cm;
CMtrans_services svc;
transport_entry trans;
attr_list attrs;
socket_conn_data_ptr scd;
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

static void
initiate_conn_redirect(void_trans, void_conn_sock)
void *void_trans;
void *void_conn_sock;
{
    transport_entry trans = (transport_entry) void_trans;
    int conn_sock = (int) (long) void_conn_sock;
    socket_client_data_ptr sd = (socket_client_data_ptr) trans->trans_data;
    CMtrans_services svc = sd->svc;
    CManager cm = sd->cm;

    attr_list attrs;
    redirect_msg_ptr msg = (redirect_msg_ptr) svc->malloc_func(sizeof(redirect_msg));
    char *buffer;
    int iget, msg_len, left;

    iget = read(conn_sock, (char *) &msg_len, 4);
    msg_len = ntohs(msg_len);
    if (iget == 0) {
	return;
    } else if (iget == -1) {
	int lerrno = errno;
	if ((lerrno != EWOULDBLOCK) &&
	    (lerrno != EAGAIN) &&
	    (lerrno != EINTR)) {
	    /* serious error */
	    return;
	}
    }
    buffer = (char *) malloc(msg_len);
    left = msg_len;
    while (left > 0) {
	iget = read(conn_sock, buffer + msg_len - left, left);
	left -= iget;
    }

    msg->type = buffer[0];
    msg->content = (char *) svc->malloc_func(msg_len - 1);
    strncpy(msg->content, buffer + 1, msg_len - 1);

    {
	socket_conn_data_ptr socket_conn_data;
	attr_list conn_attr_list;
	CMConnection conn;
	int sock;

	switch (msg->type) {
	case NO_NEED_REGISTER:
	    svc->fd_remove_select(cm, conn_sock);
	    svc->trace_out(cm, "CMSocket Redirect - registration unnecessary");
	    break;
	case REQUEST:
	    {
		socket_conn_data = create_socket_conn_data(svc);
		conn_attr_list = create_attr_list();
		attrs = attr_list_from_string(msg->content);

		svc->trace_out(cm, "CMSocket Redirect - Request redirect message received");
		svc->trace_out(cm, "CMSocket Redirect - attr_list_string: %s", msg->content);

		/* The following code is for simulation to test redirect
		 * service when no host is behind firewall. */
		if ((sock = initiate_conn(cm, svc, trans, attrs, socket_conn_data, conn_attr_list, 1)) < 0)
		    break;

		{
		    int left, iget;

		    iget = read(sock, (char *) &socket_conn_data->remote_contact_port, 4);
		    if (iget == 0) {
			break;
		    } else if (iget == -1) {
			int lerrno = errno;
			if ((lerrno != EWOULDBLOCK) &&
			    (lerrno != EAGAIN) &&
			    (lerrno != EINTR)) {
			    /* serious error */
			    break;
			} else {
			    iget = 0;
			}
		    }
		    left = 4 - iget;
		    while (left > 0) {
			iget = read(sock, (char *) &socket_conn_data->remote_contact_port + 4 - left,
				    left);
			if (iget == 0) {
			    break;
			} else if (iget == -1) {
			    int lerrno = errno;
			    if ((lerrno != EWOULDBLOCK) &&
				(lerrno != EAGAIN) &&
				(lerrno != EINTR)) {
				/* serious error */
				break;
			    } else {
				iget = 0;
			    }
			}
			left -= iget;
		    }
		    svc->trace_out(NULL, "Remote host (IP %x)is listening at port %d\n",
				   socket_conn_data->remote_IP,
				   socket_conn_data->remote_contact_port);
		}

		add_attr(conn_attr_list, CM_PEER_LISTEN_PORT, Attr_Int4,
		     (attr_value) (long)socket_conn_data->remote_contact_port);
		conn = svc->connection_create(trans, socket_conn_data, conn_attr_list);

		svc->trace_out(cm, "CMSockets Adding trans->data_available as action on fd %d", sock);
                if (trans->data_available) {
                    svc->fd_add_select(cm, sock, (select_list_func) trans->data_available,
                                       (void *) trans, (void *) conn);
                }
		break;
	    }
	default:
	    svc->trace_out(cm, "CMSocket Redirect - Invalid message");
	}
	svc->free_func(msg->content);
	svc->free_func(msg);
    }
}

/* initiate a connection with redirect sever redirecthost.cercs.gatech.edu, *
 * register the host name and port number */
static void
redirect_register(cm, svc, trans, attrs)
CManager cm;
CMtrans_services svc;
transport_entry trans;
attr_list attrs;
{
    int redirect_sock;
    redirect_msg register_msg;
    char *attr_str = attr_list_to_string(attrs);

    register_msg.type = REGISTER;
    register_msg.content = svc->malloc_func(strlen(attr_str) + 1);
    strcpy(register_msg.content, attr_str);
    if ((redirect_sock = send_msg_to_redirect_server(cm, svc, &register_msg)) < 0) {
	svc->trace_out(cm, "Can't register");
	svc->free_func(attr_str);
	return;
    }
    svc->trace_out(cm, "Send out register message");
    svc->free_func(attr_str);

    svc->trace_out(cm, "CMSockets Adding initiate_conn_redirect as action on fd %d", redirect_sock);
    svc->fd_add_select(cm, redirect_sock, initiate_conn_redirect,
		       (void *) trans, (void *) (long)redirect_sock);
}

/* 
 * Create an IP socket for connection from other CMs
 */
extern attr_list
libcmsockets_LTX_non_blocking_listen(cm, svc, trans, listen_info)
CManager cm;
CMtrans_services svc;
transport_entry trans;
attr_list listen_info;
{
    socket_client_data_ptr sd = trans->trans_data;
    int length;
    struct sockaddr_in sock_addr;
    int sock_opt_val = 1;
    int conn_sock;
    int int_port_num = 0;
    u_short port_num = 0;
    static int register_with_redirect_server = -1;
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
		       NULL, (attr_value *)(long) & int_port_num)) {
	port_num = 0;
    } else {
	if (int_port_num > USHRT_MAX || int_port_num < 0) {
	    fprintf(stderr, "Requested port number %d is invalid\n", int_port_num);
	    return NULL;
	}
	port_num = int_port_num;
    }

    svc->trace_out(cm, "CMSocket begin listen, requested port %d", int_port_num);
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
	if (bind(conn_sock, (struct sockaddr *) &sock_addr,
		 sizeof sock_addr) == SOCKET_ERROR) {
	    fprintf(stderr, "Cannot bind INET socket\n");
	    return NULL;
	}
    } else {
	/* port num is free.  Constrain to range 26000 : 26100 */
	srand(time(NULL));
	int low_bound = 26000;
	int high_bound = 26100;
	int size = high_bound - low_bound;
	int tries = 10;
	int result = SOCKET_ERROR;
	while (tries > 0) {
	    int target = low_bound + size * drand48();
	    sock_addr.sin_port = htons(target);
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

    svc->trace_out(cm, "CMSockets Adding socket_accept_conn as action on fd %d", conn_sock);
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

	svc->trace_out(cm, "CMSocket listen succeeded on port %d, fd %d",
		       int_port_num, conn_sock);
	ret_list = create_attr_list();
#if NO_DYNAMIC_LINKING
	get_qual_hostname(host_name, sizeof(host_name), svc, listen_info, 
			  &network_added);
#endif 

	sd->hostname = strdup(host_name);
	sd->listen_port = int_port_num;
	if ((IP != 0) && (cercs_getenv("CM_NETWORK") == NULL) &&
	    (!query_attr(listen_info, CM_NETWORK_POSTFIX, NULL,
			 (attr_value *) (long)& network_string))) {
	    add_attr(ret_list, CM_IP_ADDR, Attr_Int4,
		     (attr_value) (long)IP);
	}
	if ((cercs_getenv("CMSocketsUseHostname") != NULL) || 
	    (cercs_getenv("CM_NETWORK") != NULL) ||
	    (query_attr(listen_info, CM_NETWORK_POSTFIX, NULL,
			 (attr_value *) (long)& network_string))) {
	    add_attr(ret_list, CM_IP_HOSTNAME, Attr_String,
		     (attr_value) strdup(host_name));
	    if (network_added) {
		char *network_string = NULL;
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

	if (register_with_redirect_server == -1) {
	    if (cercs_getenv("CM_REDIR_REGISTER") == NULL) {
		register_with_redirect_server = 0;
	    } else {
		register_with_redirect_server = 1;
	    }
	}
	if (register_with_redirect_server)
	    redirect_register(cm, svc, trans, ret_list);

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
libcmsockets_LTX_set_write_notify(trans, svc, scd, enable)
transport_entry trans;
CMtrans_services svc;
socket_conn_data_ptr scd;
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
set_block_state(CMtrans_services svc, socket_conn_data_ptr scd,
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
	svc->trace_out(scd->sd->cm, "CMSocket switch fd %d to blocking",
		       scd->fd);
    } else if ((needed_block_state == Non_Block) && 
	       (scd->block_state == Block)) {
	fdflags |= O_NONBLOCK;
	if (fcntl(scd->fd, F_SETFL, fdflags) == -1) 
	    perror("fcntl nonblock");
	scd->block_state = Non_Block;
	svc->trace_out(scd->sd->cm, "CMSocket switch fd %d to nonblocking",
		       scd->fd);
    }
}

extern int
libcmsockets_LTX_read_to_buffer_func(svc, scd, buffer, requested_len, 
				     non_blocking)
CMtrans_services svc;
socket_conn_data_ptr scd;
void *buffer;
int requested_len;
int non_blocking;
{
    int left, iget;

    int fdflags = fcntl(scd->fd, F_GETFL, 0);
    if (fdflags == -1) {
	perror("getflags\n");
	return -1;
    }
    if (scd->block_state == Block) {
	svc->trace_out(scd->sd->cm, "CMSocket fd %d state block", scd->fd);
    } else {
	svc->trace_out(scd->sd->cm, "CMSocket fd %d state nonblock", scd->fd);
    }
    svc->trace_out(scd->sd->cm, "CMSocket read of %d bytes on fd %d, non_block %d", requested_len,
		   scd->fd, non_blocking);
    if (non_blocking && (scd->block_state == Block)) {
	svc->trace_out(scd->sd->cm, "CMSocket switch to non-blocking fd %d",
		       scd->fd);
	set_block_state(svc, scd, Non_Block);
    }
    iget = read(scd->fd, (char *) buffer, requested_len);
    if (iget == -1) {
	int lerrno = errno;
	if ((lerrno != EWOULDBLOCK) &&
	    (lerrno != EAGAIN) &&
	    (lerrno != EINTR)) {
	    /* serious error */
	    svc->trace_out(scd->sd->cm, "CMSocket iget was -1, errno is %d, returning 0 for read",
			   lerrno);
	    return -1;
	} else {
	    if (non_blocking) {
		svc->trace_out(scd->sd->cm, "CMSocket iget was -1, would block, errno is %d",
			   lerrno);
		return 0;
	    }
	    return -1;
	}
    } else if (iget == 0) {
	/* serious error */
	svc->trace_out(scd->sd->cm, "CMSocket iget was 0, errno is %d, returning -1 for read",
		       errno);
	return -1;
    }
    left = requested_len - iget;
    while (left > 0) {
	int lerrno;
	iget = read(scd->fd, (char *) buffer + requested_len - left,
		    left);
	lerrno = errno;
	if (iget == -1) {
	    if ((lerrno != EWOULDBLOCK) &&
		(lerrno != EAGAIN) &&
		(lerrno != EINTR)) {
		/* serious error */
		svc->trace_out(scd->sd->cm, "CMSocket iget was -1, errno is %d, returning %d for read", 
			   lerrno, requested_len - left);
		return (requested_len - left);
	    } else {
		iget = 0;
		if (!non_blocking && (scd->block_state == Non_Block)) {
		    svc->trace_out(scd->sd->cm, "CMSocket switch to blocking fd %d",
				   scd->fd);
		    set_block_state(svc, scd, Block);
		}
	    }
	} else if (iget == 0) {
	    svc->trace_out(scd->sd->cm, "CMSocket iget was 0, errno is %d, returning %d for read", 
			   lerrno, requested_len - left);
	    return requested_len - left;	/* end of file */
	}
	left -= iget;
    }
    return requested_len;
}


extern int
libcmsockets_LTX_write_func(svc, scd, buffer, length)
CMtrans_services svc;
socket_conn_data_ptr scd;
void *buffer;
int length;
{
    int left = length;
    int iget = 0;
    int fd = scd->fd;

    svc->trace_out(scd->sd->cm, "CMSocket write of %d bytes on fd %d",
		   length, fd);
    while (left > 0) {
	iget = write(fd, (char *) buffer + length - left, left);
	if (iget == -1) {
	    int lerrno = errno;
	    if ((lerrno != EWOULDBLOCK) &&
		(lerrno != EAGAIN) &&
		(lerrno != EINTR)) {
		/* serious error */
		return (length - left);
	    } else {
		if (lerrno == EWOULDBLOCK) {
		    svc->trace_out(scd->sd->cm, "CMSocket write blocked - switch to blocking fd %d",
				   scd->fd);
		    set_block_state(svc, scd, Block);
		}
		iget = 0;
	    }
	}
	left -= iget;
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
			printf("CMSockets write Would block, fd %d, length %d",
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
libcmsockets_LTX_writev_attr_func(svc, scd, iov, iovcnt, attrs)
CMtrans_services svc;
socket_conn_data_ptr scd;
struct iovec *iov;
int iovcnt;
attr_list attrs;
{
    int fd = scd->fd;
    int left = 0;
    int iget = 0;
    int iovleft, i;
    iovleft = iovcnt;

    /* sum lengths */
    for (i = 0; i < iovcnt; i++)
	left += iov[i].iov_len;

    svc->trace_out(scd->sd->cm, "CMSocket writev of %d bytes on fd %d",
		   left, fd);
    while (left > 0) {
	int write_count = iovleft;
	if (write_count > IOV_MAX)
	    write_count = IOV_MAX;
	iget = writev(fd, (struct iovec *) &iov[iovcnt - iovleft],
		      write_count);
	if (iget == -1) {
	    svc->trace_out(scd->sd->cm, "	writev failed, errno was %d", errno);
	    if ((errno != EWOULDBLOCK) && (errno != EAGAIN)) {
		/* serious error */
		return (iovcnt - iovleft);
	    } else {
		if (errno == EWOULDBLOCK) {
		    svc->trace_out(scd->sd->cm, "CMSocket writev blocked - switch to blocking fd %d",
				   scd->fd);
		    set_block_state(svc, scd, Block);
		}
		iget = 0;
	    }
	}
	if (iget == left) {
	    return iovcnt;
	}
	svc->trace_out(scd->sd->cm, "	writev partial success, %d bygtes written", iget);
	left -= iget;
	while (iget > 0) {
	    iget -= iov[iovcnt - iovleft].iov_len;
	    iovleft--;
	}

	if (iget < 0) {
	    /* 
	     * Only part of the last block was written.  Modify IO 
	     * vector to indicate the remaining block to be written.
	     */
	    /* restore iovleft and iget to cover remaining block */
	    iovleft++;
	    iget += iov[iovcnt - iovleft].iov_len;

	    /* adjust count down and base up by number of bytes written */
	    iov[iovcnt - iovleft].iov_len -= iget;
	    iov[iovcnt - iovleft].iov_base =
		(char *) (iov[iovcnt - iovleft].iov_base) + iget;
	}
    }
    return iovcnt;
}

/* non blocking version */
extern int
libcmsockets_LTX_NBwritev_attr_func(svc, scd, iov, iovcnt, attrs)
CMtrans_services svc;
socket_conn_data_ptr scd;
struct iovec *iov;
int iovcnt;
attr_list attrs;
{
    int fd = scd->fd;
    int init_bytes, left = 0;
    int iget = 0;
    int iovleft, i;
    iovleft = iovcnt;

    /* sum lengths */
    for (i = 0; i < iovcnt; i++)
	left += iov[i].iov_len;

    init_bytes = left;

    svc->trace_out(scd->sd->cm, "CMSocket Non-blocking writev of %d bytes on fd %d",
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
	    svc->trace_out(scd->sd->cm, "CMSocket writev returned -1, errno %d",
		   errno);
	    if ((errno != EWOULDBLOCK) && (errno != EAGAIN)) {
		/* serious error */
		return -1;
	    } else {
		return init_bytes - left;
	    }
	}
	svc->trace_out(scd->sd->cm, "CMSocket writev returned %d", iget);
	left -= iget;
	if (iget != this_write_bytes) {
	    /* didn't write everything, the rest would block, return */
	    svc->trace_out(scd->sd->cm, "CMSocket blocked, return %d", 
			   init_bytes -left);
	    return init_bytes - left;
	}
	iovleft -= write_count;
    }
    return init_bytes - left;
}

extern int
libcmsockets_LTX_writev_func(svc, scd, iov, iovcnt)
CMtrans_services svc;
socket_conn_data_ptr scd;
struct iovec *iov;
int iovcnt;
{
    return libcmsockets_LTX_writev_attr_func(svc, scd, iov, iovcnt, NULL);
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
    socket_client_data_ptr sd = (socket_client_data_ptr) sdv;
    CMtrans_services svc = sd->svc;
    if (sd->hostname != NULL)
	svc->free_func(sd->hostname);
    svc->free_func(sd);
}

extern void *
libcmsockets_LTX_initialize(cm, svc)
CManager cm;
CMtrans_services svc;
{
    static int atom_init = 0;

    socket_client_data_ptr socket_data;
    svc->trace_out(cm, "Initialize TCP/IP Socket transport built in %s",
		   EVPATH_LIBRARY_BUILD_DIR);
    if (socket_global_init == 0) {
#ifdef HAVE_WINDOWS_H
	int nErrorStatus;
	/* initialize the winsock package */
	nErrorStatus = WSAStartup(wVersionRequested, &wsaData);
	if (nErrorStatus != 0) {
	    fprintf(stderr, "Could not initialize windows socket library!");
	    WSACleanup();
	    exit(-1);
	}
#endif
	/* 
	 * ignore SIGPIPE's  (these pop up when ports die.  we catch the 
	 * failed writes) 
	 */
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
	atom_init++;
    }
    socket_data = svc->malloc_func(sizeof(struct socket_client_data));
    socket_data->cm = cm;
    socket_data->hostname = NULL;
    socket_data->listen_port = -1;
    socket_data->svc = svc;
    svc->add_shutdown_task(cm, free_socket_data, (void *) socket_data);
    return (void *) socket_data;
}
