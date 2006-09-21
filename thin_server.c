#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <unistd.h>
#include "cercs_env.h"
#include "evpath.h"
#include "cm_internal.h"

static void socket_accept_thin_client(void *cmv, void * sockv);
extern void CMget_qual_hostname(char *buf, int len);

#ifndef SOCKET_ERROR
#define SOCKET_ERROR -1
#endif

static int
get_self_ip_addr()
{
    struct hostent *host;
    char buf[256];
    char **p;
#ifdef SIOCGIFCONF
    char *ifreqs;
    struct ifreq *ifr;
    struct sockaddr_in *sai;
    struct ifconf ifaces;
    int ifrn;
    int ss;
#endif
    int rv = 0;
    char *IP_string = cercs_getenv("CERCS_IP");
    if (IP_string != NULL) {
	in_addr_t ip = inet_addr(IP_string);
	if (ip != -1) return ntohl(ip);
    }
    gethostname(buf, sizeof(buf));
    host = gethostbyname(buf);
    if (host != NULL) {
	for (p = host->h_addr_list; *p != 0; p++) {
	    struct in_addr *in = *(struct in_addr **) p;
	    if (ntohl(in->s_addr) != INADDR_LOOPBACK) {
		return (ntohl(in->s_addr));
	    }
	}
    }
    /*
     *  Since we couldn't find an IP address in some logical way, we'll open
     *  a DGRAM socket and ask it first for the list of its interfaces, and
     *  then checking for an interface we can use, and then finally asking that
     *  interface what its address is.
     */
#ifdef SIOCGIFCONF
    ss = socket(AF_INET, SOCK_DGRAM, 0);
    ifaces.ifc_len = 64 * sizeof(struct ifreq);
    ifaces.ifc_buf = ifreqs = malloc(ifaces.ifc_len);
    /*
     *  if we can't SIOCGIFCONF we're kind of dead anyway, bail.
     */
    if (ioctl(ss, SIOCGIFCONF, &ifaces) >= 0) {
	ifr = ifaces.ifc_req;
	ifrn = ifaces.ifc_len / sizeof(struct ifreq);
	for (; ifrn--; ifr++) {
	    /*
	     * Basically we'll take the first interface satisfying 
	     * the following: 
	     *   up, running, not loopback, address family is INET.
	     */
	    ioctl(ss, SIOCGIFFLAGS, ifr);
	    sai = (struct sockaddr_in *) &(ifr->ifr_addr);
	    if (ifr->ifr_flags & IFF_LOOPBACK) {
		continue;
	    }
	    if (!(ifr->ifr_flags & IFF_UP)) {
		continue;
	    }
	    if (!(ifr->ifr_flags & IFF_RUNNING)) {
		continue;
	    }
	    /*
	     * sure would be nice to test for AF_INET here but it doesn't
	     * cooperate and I've done enough for now ...
	     * if (sai->sin_addr.s.addr != AF_INET) continue;
	    */
	    if (sai->sin_addr.s_addr == INADDR_ANY)
		continue;
	    if (sai->sin_addr.s_addr == INADDR_LOOPBACK)
		continue;
	    rv = ntohl(sai->sin_addr.s_addr);
	    break;
	}
    }
    close(ss);
    free(ifreqs);
#endif
    /*
     *  Absolute last resort.  If we can't figure out anything else, look
     *  for the CM_LAST_RESORT_IP_ADDR environment variable.
     */
    if (rv == 0) {
	char *c = cercs_getenv("CM_LAST_RESORT_IP_ADDR");
    }
    /*
     *	hopefully by now we've set rv to something useful.  If not,
     *  GET A BETTER NETWORK CONFIGURATION.
     */
    return rv;
}

extern int
EVthin_socket_listen(CManager cm,  char **hostname_p, int *port_p)
{

    int length;
    struct sockaddr_in sock_addr;
    int sock_opt_val = 1;
    int conn_sock;
    int int_port_num = 0;
    u_short port_num = 0;
    char host_name[256];
    static int register_with_redirect_server = -1;

    conn_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (conn_sock == SOCKET_ERROR) {
	fprintf(stderr, "Cannot open INET socket\n");
	return NULL;
    }
    sock_addr.sin_family = AF_INET;
    sock_addr.sin_addr.s_addr = INADDR_ANY;
    sock_addr.sin_port = htons(port_num);
    if (setsockopt(conn_sock, SOL_SOCKET, SO_REUSEADDR, (char *) &sock_opt_val,
		   sizeof(sock_opt_val)) != 0) {
	fprintf(stderr, "Failed to set 1REUSEADDR on INET socket\n");
	return NULL;
    }
    if (bind(conn_sock, (struct sockaddr *) &sock_addr,
	     sizeof sock_addr) == SOCKET_ERROR) {
	fprintf(stderr, "Cannot bind INET socket\n");
	return NULL;
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
    
    CM_fd_add_select(cm, conn_sock, socket_accept_thin_client,
		    (void *) cm, (void *) (long)conn_sock);
    

    int_port_num = ntohs(sock_addr.sin_port);

    CMget_qual_hostname(host_name, sizeof(host_name));
	
    *hostname_p = strdup(host_name);
    *port_p = int_port_num;
}


typedef struct thin_conn {
    IOFile iofile;
    int fd;
    int target_stone;
    int format_count;
    IOFormatList format_list;
    int max_src_list;
    EVsource *src_list;
} *thin_conn_data;

static void thin_free_func(void *event_data, void *client_data)
{
}

static void
thin_data_available(void *cmv, void * conn_datav)
{
    thin_conn_data cd = conn_datav;
    CManager cm = (CManager) cmv;

    switch(next_IOrecord_type(cd->iofile)) {
    case IOend:
    case IOerror:
	close_IOfile(cd->iofile);
	free_IOfile(cd->iofile);
	CM_fd_remove_select(cm, cd->fd);
	free(cd);
	break;
    case IOformat: {
	IOFormat next_format = read_format_IOfile(cd->iofile);
	int format_num = index_of_IOformat(next_format);
	if (cd->format_count == 0) {
	    cd->format_list = malloc(sizeof(cd->format_list[0]));
	} else {
	    cd->format_list = 
		realloc(cd->format_list, 
			(cd->format_count + 1) * sizeof(cd->format_list[0]));
	}
	cd->format_list[cd->format_count].format_name = 
	    strdup(name_of_IOformat(next_format));
	cd->format_list[cd->format_count].field_list =
	    get_local_field_list(next_format);
	set_IOconversion(cd->iofile, name_of_IOformat(next_format),
			 cd->format_list[cd->format_count].field_list,
			 struct_size_field_list(cd->format_list[cd->format_count].field_list, sizeof(char*)));
	break;
    }
    case IOdata: {
	IOFormat next_format = next_IOrecord_format(cd->iofile);
	IOFormatList data_format;
	int format_index, i;
	int len = next_IOrecord_length(cd->iofile);
	int format_num = index_of_IOformat(next_format);
	void *data = malloc(len);
	read_IOfile(cd->iofile, data);
	if (cd->max_src_list < format_num) {
	    cd->src_list = realloc(cd->src_list, 
				   (format_num+1) * sizeof(cd->src_list[0]));
	    memset(&cd->src_list[cd->max_src_list], 0,
		   (format_num - cd->max_src_list) * sizeof(cd->src_list[0]));
	    cd->max_src_list = format_num;
	}
	if (cd->src_list[format_num] == NULL) {
	    data_format = malloc(sizeof(data_format[0]) * (cd->format_count + 2));
	    format_index = 0;
	    while(strcmp(name_of_IOformat(next_format), 
			 cd->format_list[format_index].format_name) != 0) format_index++;
	    for (i=0; i==format_index; i++) {
		data_format[i] = cd->format_list[format_index - i];
	    }
	    data_format[format_index+1].format_name = NULL;
	    data_format[format_index+1].field_list = NULL;
	    cd->src_list[format_num] = 
		EVcreate_submit_handle_free(cm, cd->target_stone, data_format,
					    thin_free_func, cd);
	}
	EVsubmit(cd->src_list[format_num], data, NULL);
	break;
    }
    case IOcomment: {
	char *comment = read_comment_IOfile(cd->iofile);
	if (strncmp(comment, "Stone ", 6) == 0) {
	    int tmp_stone;
	    if (sscanf(comment, "Stone %d", &tmp_stone) == 1) {
		cd->target_stone = tmp_stone;
	    }
	}
	break;
    }
    }
}

static void
socket_accept_thin_client(void *cmv, void * sockv)
{
    CManager cm = (CManager) cmv;
    int conn_sock = (int) sockv;
    int sock;
    struct sockaddr sock_addr;
    int sock_len = sizeof(sock_addr);
    int int_port_num;
    struct linger linger_val;
    int sock_opt_val = 1;
    thin_conn_data cd;

#ifdef TCP_NODELAY
    int delay_value = 1;
#endif

    linger_val.l_onoff = 1;
    linger_val.l_linger = 60;
    if ((sock = accept(conn_sock, (struct sockaddr *) 0, (int *) 0)) == SOCKET_ERROR) {
	perror("Cannot accept socket connection");
	CM_fd_remove_select(cm, conn_sock);
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

    sock_len = sizeof(sock_addr);
    memset(&sock_addr, 0, sock_len);
    getsockname(sock, (struct sockaddr *) &sock_addr, &sock_len);
    int_port_num = ntohs(((struct sockaddr_in *) &sock_addr)->sin_port);

    memset(&sock_addr, 0, sizeof(sock_addr));
    sock_len = sizeof(sock_addr);
    if (getpeername(sock, &sock_addr,
		    &sock_len) == 0) {
	int_port_num = ntohs(((struct sockaddr_in *) &sock_addr)->sin_port);
	int remote_IP = ntohl(((struct sockaddr_in *) &sock_addr)->sin_addr.s_addr);
	if (sock_addr.sa_family == AF_INET) {
#ifdef HAS_STRUCT_HOSTENT
	    struct hostent *host;
	    struct sockaddr_in *in_sock = (struct sockaddr_in *) &sock_addr;
	    host = gethostbyaddr((char *) &in_sock->sin_addr,
				 sizeof(struct in_addr), AF_INET);
	    if (host != NULL) {
		host->h_name;
	    }
#endif
	}
    }

    cd = malloc(sizeof(*cd));
    memset(cd, 0, sizeof(*cd));
    cd->iofile = open_IOfd(sock, "r");
    cd->fd = sock;
    cd->src_list = malloc(sizeof(cd->src_list[0]));
    cd->src_list[0] = NULL;
    CM_fd_add_select(cm, sock,
		     (void (*)(void *, void *)) thin_data_available,
		       (void *) cm, (void *) cd);
}
