#include "config.h"

#include "io.h"
#include "gen_thread.h"
#include "atl.h"
#include "evpath.h"
#include "cm_internal.h"
#ifndef MODULE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef HAVE_WINDOWS_H
#include <winsock.h>
#else
#include <netinet/in.h>
#include <arpa/inet.h>
#endif
#include <malloc.h>
#else
#include <asm/uaccess.h>
#include <linux/in.h>
#include "kernel/kcm.h"
#include "kernel/cm_kernel.h"
#include "kernel/library.h"

static char *
inet_ntoa(struct in_addr ina)
{
    static char buf[4*sizeof "123"];
    unsigned char *ucp = (unsigned char *)&ina;

    sprintf(buf, "%d.%d.%d.%d",
	ucp[0] & 0xff,
	ucp[1] & 0xff,
	ucp[2] & 0xff,
	ucp[3] & 0xff);
    return buf;
}
#endif
#include "assert.h"
#include "cercs_env.h"

/*
 * The main function there is CMpbio_get_format_rep_callback, the routine
 * that is passed to create_server_IOcontext() as the get_format_rep
 * callback value.  It's job is to get a format body representation from
 * another CM.  It works with the format ID, the host_IP and host_port
 * embedded in the format ID (but supplied separately).  Additionally, in
 * some circumstances, the app_context value will be a CMConnection.  If
 * present, that connection is the link the message containing this format
 * came in on and presumably then the link to the remote context that can
 * give us the format body.  Once we get the connection, we just send out a
 * format request message and wait for an answer. 
 *
 * The routine CM_pbio_query is the message handler for CM/PBIO messages.
 * CM/PBIO messages are not ordinary CM messages (since they are not encoded
 * with PBIO themselves (that would be a nasty recursion)) but instead are 
 * handled as foreign messages.  At this time, CM_pbio_query supports three
 * types of incoming message.  A QUERY (I.E. the format request from the
 * previous paragraph), a RESPONSE (the return message after a query), and a
 * CACHE_PUSH (an unsolicited format ID/body pair to be entered into the local
 * context).   Message encoding is network byte order for a simple structure
 * of 4-byte integers.  A magic number 'PBIO' as 4 bytes in ascii hex,
 * precedes all messages and is the foreign message tag used by CM to
 * recognize CM/PBIO messages.   
 *
 * For formats that CM knows about (I.E. not CM user formats, but CM message
 * formats), CM uses the CACHE_PUSH messages to preload needed formats
 * before a message is sent.  Which formats have been preloaded are tracked
 * on aper-connection basis.  (Code for this is in cm_formats.c, subroutine
 * CMformat_preload(). )  Generally, this means that QUERY messages are not 
 * generated for CM message formats.  However, they are necessary for CM user
 * formats, such as those that are used to encode ECho event messages.  CM is
 * generally unaware of those formats as the encoded data appears as a byte
 * array in CM messages.  So, when encoded ECho event data is presented to
 *  PBIO for decoding on the remote host, the following steps occur:
 *
 * PBIO searches for a matching in the local server_IOcontext in the
 *	destination CM.
 * If it is not found, a callback to the get_format_rep routine
 *	(CMpbio_get_format_rep_callback()) is performed.
 * CMpbio_get_format_rep_callback() finds the appropriate CM connection and
 *	sends a QUERY message to the originating CM and waits for a response
 *	on a CMcondition.  (This suspends execution of the callback 
 *	subroutine.)
 * The originating CM receives the QUERY message and looks up the requested
 *	format and its subformats.
 * The subformats are sent on the connection with CACHE_PUSH messages (they
 *	would have been requested anyway if we didn't push them).
 * The server_format_rep of the base format is sent via a RESPONSE message.
 * The destination CM receives the CACHE_PUSH messages and loads the
 *	subformats into its server_IOcontext (if not present already).
 * The destination CM receives the RESPONSE message and wakes the callback
 *	subroutine, which returns the server_format_rep.
 *
 * In order to make the above happen with reasonable locking, we have
 * separated the locking from several routines (such as CMget_conn()) to
 * create internal versions that assume the CManager is already locked.  The
 * care taken with locking also means that some ways of using CM that used
 * to work will no longer.  As far as I know, the only
 * backwards-incompatible changes are in the use of user_contexts and
 * user_formats.  Previously once you'd done a CMget_user_type_context to
 * get an IOContext, or CMregister_user_format to get an IOFormat, you could
 * pretty much use the basic PBIO routines on those items.  That is no
 * longer true.  In particular, you can't use get_format_IOcontext(),
 * set_IOconversion_IOcontext(), get_subformats_IOcontext(), or
 * get_IOformat_by_name() without CManager being locked.  Since we don't
 * export the CM locking routines, there are now CM versions of the routines
 * in the list above. 
 * 
 * At the present time, you must choose between having CM be its own format
 * server and using an external format server.  A CM that is doing one can't
 * talk with a CM that is doing the other.  For backwards compatibility, the
 * default is to use an external format server.  At some point in the
 * not-so-distant future, this will change.  You should get notice before that
 * happens.  In the meantime, you can explicitly select CM being its own
 * format server by setting the "CMSelfFormats" environment variable.
 * Alternatively, you can force the use of an external format server with
 * the "CMExternalFormats" variable.
 * 
 */

static int
CMpbio_send_format_request ARGS((char *format_ID, int format_ID_length,
				 CMConnection conn, int cond));
static void CM_pbio_query ARGS((CMConnection conn, CMTransport trans,
				char *buffer, int length));

static int
request_in_pending(cm, format_ID, format_id_length)
CManager cm;
void *format_ID; 
int format_id_length;
{
    int i;
    for (i=0; i<cm->pending_request_max; i++) {
	if ((cm->pbio_requests[i].server_id != NULL) &&
	    (format_id_length = cm->pbio_requests[i].id_length) &&
	    (memcmp(cm->pbio_requests[i].server_id, format_ID,
		    format_id_length) == 0))
	    return i;
    }
    return -1;
}

static void
add_request_to_pending(cm, format_ID, format_id_length, cond)
CManager cm;
void *format_ID;
int format_id_length;
int cond;
{
    int i;
    /* tag any duplicates as no longer the most recent request */
    for (i=0; i<cm->pending_request_max; i++) {
	if ((cm->pbio_requests[i].server_id != NULL) &&
	    (format_id_length = cm->pbio_requests[i].id_length) &&
	    (memcmp(cm->pbio_requests[i].server_id, format_ID,
		    format_id_length) == 0)) {
	    cm->pbio_requests[i].top_request = 0;
	}
    }
    /* find an insertion spot */
    for (i=0; i<cm->pending_request_max; i++) {
	if (cm->pbio_requests[i].server_id == NULL) {
	    cm->pbio_requests[i].server_id = format_ID;
	    cm->pbio_requests[i].id_length = format_id_length;
	    cm->pbio_requests[i].condition = cond;
	    cm->pbio_requests[i].top_request = 1;
	    return;
	}
    }
    cm->pbio_requests = realloc(cm->pbio_requests, 
				(cm->pending_request_max + 1) * 
				sizeof(struct _pending_format_requests));
    i = cm->pending_request_max++;
    cm->pbio_requests[i].server_id = format_ID;
    cm->pbio_requests[i].id_length = format_id_length;
    cm->pbio_requests[i].condition = cond;
    cm->pbio_requests[i].top_request = 1;
    return;
}

/*
 *  This is a bit tricky as we might have multiple pending format requests
 *  at a time, even for the same format_ID.  When this happens it's possible
 *  to build up a stack of requests.  So, we're careful to signal the 
 *  most recent request only.  The prior ones get a NULL return value.  
 *  This will cause PBIO to search once again for the format, which it 
 *  should now find.
 */

static void
signal_requests(cm, server_rep, condition)
CManager cm;
char *server_rep;
int condition;
{
    /* 
     *  signal the most recent (top) request and give it the server rep.
     *  The others get signalled with NULL;
     */
    
    int i;
    char *format_ID = NULL;
    int format_id_length = 0;
    /* find info for this request */
    for (i=0; i<cm->pending_request_max; i++) {
	if (cm->pbio_requests[i].condition == condition) {
	    format_ID = cm->pbio_requests[i].server_id;
	    format_id_length = cm->pbio_requests[i].id_length;
	}
    }
    if (format_id_length == 0) {
	printf("CMpbio Error in signal requests\n");
	return;
    }
    /* tag any duplicates as no longer the most recent request */
    for (i=0; i<cm->pending_request_max; i++) {
	if ((cm->pbio_requests[i].server_id != NULL) &&
	    (format_id_length = cm->pbio_requests[i].id_length) &&
	    (memcmp(cm->pbio_requests[i].server_id, format_ID,
		    format_id_length) == 0)) {
	    char **server_rep_ptr;
	    server_rep_ptr = 
		CMCondition_get_client_data(cm, 
					    cm->pbio_requests[i].condition);
	    if (cm->pbio_requests[i].top_request == 1) {
		*server_rep_ptr = server_rep;
	    } else {
		*server_rep_ptr = NULL;
	    }
	    CMCondition_signal(cm, cm->pbio_requests[i].condition);
	    cm->pbio_requests[i].id_length = 0;
	    cm->pbio_requests[i].server_id = NULL;
	    cm->pbio_requests[i].top_request = 0;
	    cm->pbio_requests[i].condition = -1;
	}
    }
}

#ifndef MODULE
extern void print_server_ID ARGS((unsigned char*));
#endif 

extern void *
CMpbio_get_format_rep_callback(format_ID, format_ID_length, host_IP,
			       host_port, app_context, client_data)
void *format_ID; 
int format_ID_length;
int host_IP;
int host_port;
void *app_context;
void *client_data;
{
    CManager cm = (CManager) client_data;
    CMConnection conn = (CMConnection) app_context;
    char *server_rep;
    struct in_addr in;
    char *host_string;
    int cond;
    attr_list contact_attrs = create_attr_list();

    assert(CManager_locked(cm));

    in.s_addr = host_IP;
    host_string =  inet_ntoa(in);
    CMtrace_out(cm, CMFormatVerbose, "CMpbio request for format from host %lx, port %d", host_IP, host_port);
#ifndef MODULE
    if (CMtrace_on(cm, CMFormatVerbose)) {
	printf("CMpbio request is for format ");
	print_server_ID(format_ID);
	printf("\n");
    }
#endif

    cond = CMCondition_get(cm, conn);
    CMCondition_set_client_data(cm, cond, &server_rep);

    if (request_in_pending(cm, format_ID, format_ID_length) == -1) {
        add_request_to_pending(cm, format_ID, format_ID_length, cond);
	if ((conn == NULL) || (conn->closed)) {
	    CMtrace_out(cm, CMFormatVerbose, 
			"CMpbio connection not available, trying to reestablish, conn %lx, host %s, port %d", 
			conn, host_string, host_port);
	    add_attr(contact_attrs, CM_IP_HOSTNAME, Attr_String,
		     (attr_value) strdup(host_string));
	    add_attr(contact_attrs, CM_IP_PORT, Attr_Int4,
		     (attr_value) (long)host_port);
	    
	    conn = CMinternal_get_conn(cm, contact_attrs);
	
	    if (conn == NULL) {
		CMtrace_out(cm, CMFormatVerbose, "CMpbio failed to reestablish connection, returning NULL\n");
		return NULL;
	    }
	    CMtrace_out(cm, CMFormatVerbose, "CMpbio got connection %lx", 
			conn);
	} else {
	    conn->ref_count++;
	    CMtrace_out(cm, CMFormatVerbose, "CMpbio Request format on connection %lx",
			conn);
	}
	if (CMpbio_send_format_request(format_ID, format_ID_length, conn,
				       cond) != 1) {
	    CMtrace_out(cm, CMFormatVerbose, "CMpbio write failed");
	    return NULL;
	}
    } else {
        add_request_to_pending(cm, format_ID, format_ID_length, cond);
	CMtrace_out(cm, CMFormatVerbose, 
		    "CMpbio - add duplicate pending request");
    }
    CMtrace_out(cm, CMFormatVerbose, "CMpbio waiting on condition %d", cond);
    CManager_unlock(cm);
    if (CMCondition_wait(cm, cond) != 1) {
	CMtrace_out(cm, CMFormatVerbose, "CMpbio Connection failed %lx",
		    conn);
	return NULL;
    } else {
	CMtrace_out(cm, CMFormatVerbose, "CMpbio Request returned");
    }
    CManager_lock(cm);
    return server_rep;
}

extern int CMpbio_get_port_callback(client_data)
void *client_data;
{
    CManager cm = (CManager) client_data;
    attr_list contact_attrs = CMget_contact_list(cm);
    int int_port_num;

    if (contact_attrs == NULL) {
	CMinternal_listen(cm, NULL);
    }
    contact_attrs = CMget_contact_list(cm);
    if (!query_attr(contact_attrs, CM_IP_PORT, /* type pointer */ NULL,
		    /* value pointer */ (attr_value *) (long) &int_port_num)) {
	CMtrace_out(cm, CMFormatVerbose, "CMpbio port callback found no IP_PORT attribute");
	return 0;
    }
    CMtrace_out(cm, CMFormatVerbose, "CMpbio port callback returning %d", int_port_num);
    return int_port_num;
}

#define MAGIC 0x5042494f
#define REVERSE_MAGIC 0x4f494250

#define PBIO_QUERY 0
#define PBIO_RESPONSE 1
#define PBIO_CACHE_PUSH 2

struct pbio_exchange_msg {
    int magic;
    int msg_len;
    int msg_type;    /* 0 is query.   1 is response */
    int cond;
    int payload1_length;
    int payload2_length;
};

extern struct CMtrans_services_s CMstatic_trans_svcs;

static int
CMpbio_send_format_request(format_ID, format_ID_length, conn, cond)
char *format_ID;
int format_ID_length;
CMConnection conn;
int cond;
{
    struct pbio_exchange_msg msg;
    struct _io_encode_vec vec[2];
    int actual;

    msg.magic = MAGIC;
    msg.msg_len = sizeof(msg) - 8 + format_ID_length;
    msg.msg_type = PBIO_QUERY;
    msg.payload1_length = format_ID_length;
    msg.payload2_length = 0;
    msg.cond = cond;
    vec[0].iov_base = &msg;
    vec[0].iov_len = sizeof(msg);
    vec[1].iov_base = format_ID;
    vec[1].iov_len = format_ID_length;
    CMtrace_out(conn->cm, CMLowLevelVerbose, "CMpbio send format request - total %d bytes in writev", format_ID_length + sizeof(msg));
    actual = conn->trans->writev_func(&CMstatic_trans_svcs, 
				      conn->transport_data, 
				      &vec[0], 2);
    if (actual != 2) {
	CMConnection_close(conn);
	return 0;
    }
    return 1;
}

static int
CMpbio_send_format_response(ioformat, conn, cond)
IOFormat ioformat;
CMConnection conn;
int cond;
{
    struct pbio_exchange_msg msg;
    struct _io_encode_vec vec[2];
    int actual;
    char *format_body_rep;
    int body_len = 0;

    format_body_rep = get_server_rep_IOformat(ioformat, &body_len);
    /*
     * msg_len is (sizeof(msg) minus 8 + whatever) because the msg struct 
     * includes magic and overall len, which is read separately.
     */
    msg.magic = MAGIC;
    msg.msg_len = sizeof(msg) - 8 + body_len;
    msg.msg_type = PBIO_RESPONSE;
    msg.payload1_length = body_len;
    msg.payload2_length = 0;
    msg.cond = cond;
    vec[0].iov_base = &msg;
    vec[0].iov_len = sizeof(msg);
    vec[1].iov_base = format_body_rep;
    vec[1].iov_len = body_len;
    CMtrace_out(conn->cm, CMLowLevelVerbose, "CMpbio send format response - total %d bytes in writev", body_len + sizeof(msg));
    actual = conn->trans->writev_func(&CMstatic_trans_svcs, 
				      conn->transport_data, 
				      &vec[0], 2);
    if (actual != 2) {
	CMConnection_close(conn);
	return 0;
    }
    return 1;
}

extern int
CMpbio_send_format_preload(ioformat, conn)
IOFormat ioformat;
CMConnection conn;
{
    struct pbio_exchange_msg msg;
    struct _io_encode_vec vec[3];
    int actual;
    char *format_body_rep;
    char *server_ID;
    int body_len = 0;
    int id_len = 0;

    format_body_rep = get_server_rep_IOformat(ioformat, &body_len);
    server_ID = get_server_ID_IOformat(ioformat, &id_len);
    /*
     * msg_len is (sizeof(msg) minus 8 + whatever) because the msg struct 
     * includes magic and overall len, which is read separately.
     */
    msg.magic = MAGIC;
    msg.msg_len = sizeof(msg) - 8 + body_len + id_len;
    msg.msg_type = PBIO_CACHE_PUSH;
    msg.payload1_length = id_len;
    msg.payload2_length = body_len;
    msg.cond = 0;
    vec[0].iov_base = &msg;
    vec[0].iov_len = sizeof(msg);
    vec[1].iov_base = server_ID;
    vec[1].iov_len = id_len;
    vec[2].iov_base = format_body_rep;
    vec[2].iov_len = body_len;
    CMtrace_out(conn->cm, CMLowLevelVerbose, "CMpbio send format preload - total %d bytes in writev", body_len + id_len + sizeof(msg));
    actual = conn->trans->writev_func(&CMstatic_trans_svcs, 
				      conn->transport_data, 
				      &vec[0], 3);
    if (actual != 3) {
	CMConnection_close(conn);
	return 0;
    }
    return 1;
}

int CMself_hosted_formats = -1;

extern void
CMinit_local_formats(cm)
CManager cm;
{
    if (CMself_hosted_formats == -1) {
	CMself_hosted_formats = 0;
	if (cercs_getenv("CMSelfFormats") != NULL) {
	    CMself_hosted_formats = 1;
	} else if (cercs_getenv("CMExternalFormats") != NULL) {
	    CMself_hosted_formats = 0;
	}
    }
    if (CMself_hosted_formats == 1) {
	cm->IOcontext = 
	    create_server_IOcontext(CMpbio_get_format_rep_callback, 
				    CMpbio_get_port_callback, cm);
	CMtrace_out(cm, CMFormatVerbose, 
		    "\nUsing self-hosted PBIO formats\n");
    } else {
	cm->IOcontext = create_IOcontext();
	CMtrace_out(cm, CMFormatVerbose, 
		    "\nUsing external PBIO format server\n");
    }
    CMregister_non_CM_message_handler(0x5042494f, CM_pbio_query);
    CMregister_non_CM_message_handler(0x4f494250, CM_pbio_query);
}

static int
conn_read_to_buffer(conn, buffer, length)
CMConnection conn;
void *buffer;
int length;
{
    transport_entry trans = conn->trans;
    if (trans->read_to_buffer_func) {
	if (trans->read_to_buffer_func(&CMstatic_trans_svcs, 
				       conn->transport_data, buffer, length, 0)
	    != length) {
	    CMConnection_close(conn);
	    return 0;
	}
    } else {
	void *tmp_buffer;
	int actual;
	tmp_buffer = trans->read_block_func(&CMstatic_trans_svcs, 
				      conn->transport_data,
				      &actual);
	if (actual < length) {
	    CMConnection_close(conn);
	    return 0;
	}
	memcpy(buffer, tmp_buffer, length);
	free(tmp_buffer);
    }
    return 1;
}

static void
byte_swap(data, size)
char *data;
int size;
{
    int i;
    assert((size % 2) == 0);
    for (i = 0; i < size / 2; i++) {
	char tmp = data[i];
	data[i] = data[size - i - 1];
	data[size - i - 1] = tmp;
    }
}

static void
CM_pbio_query(CMConnection conn, CMTransport trans, char *buffer, int length)
{
    struct pbio_exchange_msg tmp_msg;
    struct pbio_exchange_msg *msg;
    int swap;

    int *incoming_length;
    int tmp_length;
    int used_length = 4;
    int header = *(int*)buffer;

    CMtrace_out(conn->cm, CMFormatVerbose, "CMPbio operation in progress");
     
    if (header == 0x5042494f) {
	swap = 0;
    } else {
	swap = 1;
    }
    if (length < used_length + 4) {
	if (trans->read_to_buffer_func) {
	    int actual = trans->read_to_buffer_func(&CMstatic_trans_svcs,
						    conn->transport_data,
						    &tmp_length, 4, 0);
	    CMtrace_out(conn->cm, CMLowLevelVerbose, "CMpbio reading 4 length bytes");
	    if (actual != 4) {
		CMtrace_out(conn->cm, CMLowLevelVerbose, 
			    "CMdata read failed, actual %d", actual);
		CMConnection_close(conn);
		return;
	    }
	    incoming_length = &tmp_length;
	    length += 4;
	} else {
	    assert(0);
	}
    } else {
	incoming_length = (int *)(buffer + used_length);
    }
    used_length += 4;
    if (swap == 1) {
	byte_swap((char*)incoming_length, (int)sizeof(int));
    }

    if (length < used_length + sizeof(tmp_msg) - 8) {
	if (trans->read_to_buffer_func) {
	    int actual = trans->read_to_buffer_func(&CMstatic_trans_svcs,
						    conn->transport_data,
						    ((char*)&tmp_msg) + 8, 
						    (int)sizeof(tmp_msg) - 8, 0);
	    CMtrace_out(conn->cm, CMLowLevelVerbose, "CMpbio reading %d msg bytes",
			sizeof(tmp_msg) - 8);
	    if (actual != (sizeof(tmp_msg) - 8)) {
		CMtrace_out(conn->cm, CMLowLevelVerbose, 
			    "CMdata read failed, actual %d", actual);
		CMConnection_close(conn);
		return;
	    }
	    msg = &tmp_msg;
	} else {
	    assert(0);
	}
    } else {
	msg = (struct pbio_exchange_msg *)(buffer);
	used_length += (sizeof(tmp_msg) - 8);
    }

    if (swap == 1) {
	byte_swap((char*)&msg->msg_type, (int)sizeof(msg->msg_type));
	byte_swap((char*)&msg->cond, (int)sizeof(msg->cond));
	byte_swap((char*)&msg->payload1_length, (int)sizeof(msg->payload1_length));
	byte_swap((char*)&msg->payload2_length, (int)sizeof(msg->payload2_length));
    }
    if (*incoming_length - sizeof(tmp_msg) + 8 != 
	(msg->payload1_length + msg->payload2_length)) {
	CMtrace_out(conn->cm, CMFormatVerbose, 
		    "CMpbio Inconsistent length information, incoming %d, pay1 %d, pay2 %d", 
		    *incoming_length, msg->payload1_length, msg->payload2_length);
	CMConnection_close(conn);
	return;
    }
    CMtrace_out(conn->cm, CMFormatVerbose, 
		"CMpbio Msg incoming length = %d, type %d, cond %d, pay1 len %d, pay2 len %d\n", 
		*incoming_length, msg->msg_type, msg->cond,
		msg->payload1_length, msg->payload2_length);
    switch (msg->msg_type) {
    case PBIO_QUERY: {
	char tmp_format_id[64];  /* oversize */
	char *format_id;
	IOFormat ioformat;
	IOFormat *subformats, *tmp_sub;
	CMtrace_out(conn->cm, CMFormatVerbose, 
		    "CMpbio Incoming Query message");
	if (msg->payload1_length > sizeof(tmp_format_id)) {
	    CMtrace_out(conn->cm, CMFormatVerbose, 
			"CMpbio Huge incoming payload on query - ignoring");
	    CMConnection_close(conn);
	}
	if (length < used_length + msg->payload1_length) {
	    CMtrace_out(conn->cm, CMLowLevelVerbose, 
			"CMpbio reading %d payload bytes",
			msg->payload1_length);
	    if (conn_read_to_buffer(conn, &tmp_format_id[0], 
				    msg->payload1_length) != 1) {
		CMtrace_out(conn->cm, CMFormatVerbose, "CMpbio Read Failed");
		CMConnection_close(conn);
	    }
	    format_id = &tmp_format_id[0];
	    length += msg->payload1_length;
	} else {
	    format_id = buffer + used_length;
	    used_length += msg->payload1_length;
	}
	ioformat = get_local_format_IOcontext(conn->cm->IOcontext, 
					      &format_id[0]);
	if (ioformat == NULL) {
	    CMtrace_out(conn->cm, CMFormatVerbose, 
			"CMpbio No matching format");
	} else {
	    CMtrace_out(conn->cm, CMFormatVerbose, 
			"CMpbio Returning format %s",
			name_of_IOformat(ioformat));
	}
	tmp_sub = subformats = get_subformats_IOformat(ioformat);

	while (*tmp_sub != NULL) {
	    if (*tmp_sub != ioformat) {
		CMtrace_out(conn->cm, CMFormatVerbose, 
			    "CMpbio Preloading subformat %s",
			    name_of_IOformat(*tmp_sub));
#ifndef MODULE
		if (CMtrace_on(conn->cm, CMFormatVerbose)) {
		    int junk;
		    printf("CMpbio Preload is format ");
		    print_server_ID((unsigned char*)get_server_ID_IOformat(*tmp_sub, &junk));
		    printf("\n");
		}
#endif
		CMpbio_send_format_preload(*tmp_sub, conn);
	    }
	    tmp_sub++;
	}
	free(subformats);
	if (CMpbio_send_format_response(ioformat, conn, msg->cond) != 1) {
	    CMtrace_out(conn->cm, CMFormatVerbose, "CMpbio - Write Failed");
	    CMConnection_close(conn);
	}
	break;
    }
    case PBIO_RESPONSE: {
	char *server_rep;
	CMtrace_out(conn->cm, CMFormatVerbose, 
		    "CMpbio - Incoming Response message");
	server_rep = malloc(msg->payload1_length);
	if (length < used_length + msg->payload1_length) {
	    CMtrace_out(conn->cm, CMLowLevelVerbose, 
			"CMpbio reading %d payload bytes",
			msg->payload1_length);
	    if (conn_read_to_buffer(conn, server_rep, 
				    msg->payload1_length) != 1) {
		CMtrace_out(conn->cm, CMFormatVerbose, "CMpbio Read Failed");
		CMConnection_close(conn);
	    }
	    length += msg->payload1_length;
	} else {
	    memcpy(server_rep, buffer + used_length, msg->payload1_length);
	    used_length += msg->payload1_length;
	}
	signal_requests(conn->cm, server_rep, msg->cond);
	break;
    }
    case PBIO_CACHE_PUSH: {
	char *server_rep;
	char *format_ID;
	CMtrace_out(conn->cm, CMFormatVerbose, 
		    "CMpbio - Incoming Cache Preload message");
	format_ID = malloc(msg->payload1_length);
	server_rep = malloc(msg->payload2_length);
	
	if (length < used_length + msg->payload1_length) {
	    CMtrace_out(conn->cm, CMLowLevelVerbose, 
			"CMpbio reading %d payload bytes",
			msg->payload1_length);
	    if (conn_read_to_buffer(conn, format_ID, 
				    msg->payload1_length) != 1) {
		CMtrace_out(conn->cm, CMFormatVerbose, "CMpbio Read Failed");
		CMConnection_close(conn);
	    }
	    length += msg->payload1_length;
	} else {
	    memcpy(format_ID, buffer + used_length, msg->payload1_length);
	    used_length += msg->payload1_length;
	}
	if (length < used_length + msg->payload2_length) {
	    CMtrace_out(conn->cm, CMLowLevelVerbose, 
			"CMpbio reading %d payload bytes",
			msg->payload2_length);
	    if (conn_read_to_buffer(conn, server_rep, 
				    msg->payload2_length) != 1) {
		CMtrace_out(conn->cm, CMFormatVerbose, "CMpbio Read Failed");
		CMConnection_close(conn);
	    }
	    length += msg->payload2_length;
	} else {
	    memcpy(server_rep, buffer + used_length, msg->payload2_length);
	    used_length += msg->payload2_length;
	}
	if (!load_external_format_IOcontext(conn->cm->IOcontext, format_ID,
					    msg->payload1_length, server_rep)) {
	    CMtrace_out(conn->cm, CMFormatVerbose, 
			"CMpbio - cache load failed");
	} else {
	    CMtrace_out(conn->cm, CMFormatVerbose, "CMpbio - loaded format");
#ifndef MODULE
	    if (CMtrace_on(conn->cm, CMFormatVerbose)) {
		printf("CMpbio Preload is format ");
		print_server_ID((unsigned char*)format_ID);
		printf("\n");
	    }
#endif
	}
	break;
    }
    default: 
        {
	    char *buffer;
	    int length = msg->payload1_length + msg->payload2_length;
	    CMtrace_out(conn->cm, CMFormatVerbose, 
			"CMpbio - Unknown incoming message type %d",
			msg->msg_type);
	    buffer = malloc(length);
	    if (conn_read_to_buffer(conn, buffer, length) != 1) {
		CMtrace_out(conn->cm, CMFormatVerbose, "CMpbio Read Failed");
		CMConnection_close(conn);
	    }
	    /* ignore message */
	    free(buffer);
	    break;
	}
    }
}
