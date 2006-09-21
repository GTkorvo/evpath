#include "../config.h"
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <unistd.h>

#include "io.h"

typedef struct _simple_rec {
    int integer_field;
    short short_field;
    long long_field;
    double double_field;
    char char_field;
    int scan_sum;
} simple_rec, *simple_rec_ptr;

static IOField simple_field_list[] =
{
    {"integer_field", "integer",
     sizeof(int), IOOffset(simple_rec_ptr, integer_field)},
    {"short_field", "integer",
     sizeof(short), IOOffset(simple_rec_ptr, short_field)},
    {"long_field", "integer",
     sizeof(long), IOOffset(simple_rec_ptr, long_field)},
    {"double_field", "float",
     sizeof(double), IOOffset(simple_rec_ptr, double_field)},
    {"char_field", "char",
     sizeof(char), IOOffset(simple_rec_ptr, char_field)},
    {"scan_sum", "integer",
     sizeof(int), IOOffset(simple_rec_ptr, scan_sum)},
    {NULL, NULL, 0, 0}
};


static int do_connection(char* host, int port);
static void generate_record (simple_rec_ptr event);

int
main(int argc, char **argv)
{
    char *remote_host;
    int remote_port;
    int stone;
    struct hostent *host_addr;
    IOFile out_connection;
    IOFormat ioformat;
    simple_rec rec;
    int conn;
    char *comment = "Stone xxxxx";

    if ((argc != 4) || (sscanf(argv[2], "%d", &remote_port) != 1)
			|| (sscanf(argv[3], "%d", &stone) != 1)) {
	printf("Usage \"thin_client remote_host remote_port\"\n");
	exit(1);
    }
    remote_host = argv[1];

    conn = do_connection(remote_host, remote_port);

    out_connection = open_IOfd(conn, "w");

    sprintf(comment, "Stone %d", stone);
    write_comment_IOfile(out_connection, comment);
    ioformat = register_IOrecord_format("thin_message", simple_field_list, 
					out_connection);
    generate_record(&rec);
    write_IOfile(out_connection, ioformat, &rec);
    close_IOfile(out_connection);
    return 0;
}

int
do_connection(char * remote_host, int port)
{
    struct hostent *host_addr;
    struct sockaddr_in sin;

    memset((char*)&sin, 0, sizeof(sin));

    int conn = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

    if (conn == -1) return -1;

    host_addr = gethostbyname(remote_host);
    if (!host_addr) {
	sin.sin_addr.s_addr = inet_addr(remote_host);
	if(host_addr == NULL) return -1;
    } else {
	memcpy((char*)&sin.sin_addr, host_addr->h_addr, host_addr->h_length);
    }
    sin.sin_port = port;
    sin.sin_family = AF_INET;
    if (connect(conn, (struct sockaddr *) &sin,
		sizeof sin) == -1) {
#ifdef WSAEWOULDBLOCK
	int err = WSAGetLastError();
	if (err != WSAEWOULDBLOCK || err != WSAEINPROGRESS) {
#endif
	    close(conn);
	    return -1;
#ifdef WSAEWOULDBLOCK
	}
#endif
    }
    return conn;
}

static
void 
generate_record(event)
simple_rec_ptr event;
{
    long sum = 0;
    event->integer_field = (int) lrand48() % 100;
    sum += event->integer_field % 100;
    event->short_field = ((short) lrand48());
    sum += event->short_field % 100;
    event->long_field = ((long) lrand48());
    sum += event->long_field % 100;

    event->double_field = drand48();
    sum += ((int) (event->double_field * 100.0)) % 100;
    event->char_field = lrand48() % 128;
    sum += event->char_field;
    sum = sum % 100;
    event->scan_sum = (int) sum;
}

