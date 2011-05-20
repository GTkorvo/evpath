#ifndef __CM_TRANSPORT_H__
#define __CM_TRANSPORT_H__

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

typedef struct _transport_item *transport_entry;
typedef struct _transport_item *CMTransport;

typedef struct _CMbuffer {
    void *buffer;
    int size;
    int in_use_by_cm;
    struct _CMbuffer *next;
    void (*return_callback)(void *);
    void *return_callback_data;
} *CMbuffer;

typedef void *(*CMTransport_malloc_func) ARGS((int));
typedef void *(*CMTransport_realloc_func) ARGS((void*, int));
typedef void (*CMTransport_free_func) ARGS((void*));

typedef void (*select_list_func) ARGS((void *, void*));

typedef void (*CMAddSelectFunc) ARGS((void *svcs, void *select_data, int fd,
				      select_list_func func,
				      void *param1, void *param2));

typedef void (*CMTransport_fd_add_select) ARGS((CManager cm, int fd, select_list_func handler_func,
		       void *param1, void *param2));
typedef void (*CMTransport_fd_remove_select) ARGS((CManager cm, int fd));
typedef void (*CMTransport_trace) ARGS((CManager cm, char *format, ...));
typedef CMConnection (*CMTransport_conn_create) ARGS((transport_entry trans,
						      void *transport_data,
						      attr_list conn_attrs));
typedef void (*CMTransport_add_shut_task) ARGS((CManager cm, CMPollFunc func,
						void *client_data));
typedef void (*CMTransport_add_period_task) ARGS((CManager cm, 
						  int period_sec,
						  int period_usec,
						  CMPollFunc func,
						  void *client_data));
typedef CMbuffer (*CMTransport_get_data_buffer) ARGS((CManager cm, int length));
typedef void (*CMTransport_return_data_buffer) ARGS((CMbuffer cmb));
typedef void (*CMTransport_connection_close) ARGS((CMConnection conn));
typedef void *(*CMTransport_get_transport_data) ARGS((CMConnection conn));
typedef CMbuffer (*CMTransport_create_data_buffer) ARGS((CManager cm, void *buffer, int length));


typedef struct CMtrans_services_s {
    CMTransport_malloc_func malloc_func;
    CMTransport_realloc_func realloc_func;
    CMTransport_free_func free_func;
    CMTransport_fd_add_select fd_add_select;
    CMTransport_fd_add_select fd_write_select;
    CMTransport_fd_remove_select fd_remove_select;
    CMTransport_trace trace_out;
    CMTransport_conn_create connection_create;
    CMTransport_add_shut_task add_shutdown_task;
    CMTransport_add_period_task add_periodic_task;
    CMTransport_get_data_buffer get_data_buffer;
    CMTransport_return_data_buffer return_data_buffer;
    CMTransport_connection_close connection_close;
    CMTransport_create_data_buffer create_data_buffer;
    CMTransport_create_data_buffer create_data_and_link_buffer;
    CMTransport_get_transport_data get_transport_data;
} *CMtrans_services;


typedef void *(*CMTransport_func) ARGS((CManager cm, CMtrans_services svc, transport_entry trans));
typedef attr_list (*CMTransport_listen_func) ARGS((CManager cm,
						   CMtrans_services svc,
						   transport_entry trans,
                                                   attr_list listen_info));
typedef void *(*CMTransport_read_block_func) ARGS((CMtrans_services svc,
						   void *transport_data,
						   int *actual));
typedef int (*CMTransport_read_to_buffer_func) ARGS((CMtrans_services svc,
						     void *transport_data,
						     void *buffer,
						     int len, int block_flag));
typedef int (*CMTransport_write_func) ARGS((CMtrans_services svc,
					    void *transport_data,
					    void *buffer, int len));
typedef int (*CMTransport_writev_func) ARGS((CMtrans_services svc,
					     void *transport_data,
					     void *buffer, int len));
typedef int (*CMTransport_writev_attr_func) ARGS((CMtrans_services svc,
						  void *transport_data,
						  void *buffer, int len,
						  attr_list attrs));
typedef void (*CMTransport_shutdown_conn_func) ARGS((CMtrans_services svc,
						     void *conn_data));

typedef CMConnection (*CMTransport_conn_func) ARGS((CManager cm, 
						    CMtrans_services svc,
						    transport_entry trans, 
						    attr_list attrs));

typedef int (*CMTransport_self_check_func) ARGS((CManager cm,
						 CMtrans_services svc,
						 transport_entry trans,
						 attr_list attrs));

typedef int (*CMTransport_connection_eq_func) ARGS((CManager cm,
						    CMtrans_services svc,
						    transport_entry trans,
						    attr_list attrs,
						    void *conn_data));

typedef int (*CMTransport_set_write_notify_func) 
    ARGS((transport_entry, CMtrans_services svc, void *transport_data, int enable));


typedef void (*DataAvailableCallback) ARGS((transport_entry trans, CMConnection conn));
typedef void (*WritePossibleCallback) ARGS((transport_entry trans, CMConnection conn));

struct _transport_item {
    char *trans_name;
    CManager cm;
    DataAvailableCallback data_available;
    WritePossibleCallback write_possible;
    CMTransport_func  transport_init;
    CMTransport_listen_func  listen;
    CMTransport_conn_func  initiate_conn;
    CMTransport_self_check_func  self_check;
    CMTransport_connection_eq_func  connection_eq;
    CMTransport_shutdown_conn_func  shutdown_conn;
    CMTransport_read_to_buffer_func read_to_buffer_func;
    CMTransport_read_block_func read_block_func;
    CMTransport_write_func write_func;
    CMTransport_writev_func writev_func;
    CMTransport_writev_attr_func writev_attr_func;
    CMTransport_writev_attr_func NBwritev_attr_func; /* non blocking */
    CMTransport_set_write_notify_func set_write_notify;
    void *trans_data;
};

#if defined(__cplusplus) || defined(c_plusplus)
}
#endif

#if defined(NO_DYNAMIC_LINKING)
struct socket_connection_data;

#if defined (__INTEL_COMPILER)
//  declaration not visible
#  pragma warning (disable: 274)
#endif
extern void libcmsockets_LTX_shutdown_conn(CMtrans_services svc, struct socket_connection_data * scd);
extern CMConnection libcmsockets_LTX_initiate_conn(CManager cm, CMtrans_services svc,
						   transport_entry trans, attr_list attrs);
extern int libcmsockets_LTX_self_check(CManager cm, CMtrans_services svc,
			    transport_entry trans, attr_list attrs);
extern int libcmsockets_LTX_connection_eq(CManager cm,
					  CMtrans_services svc, transport_entry trans,
					  attr_list attrs, struct socket_connection_data * scd);
extern attr_list libcmsockets_LTX_non_blocking_listen(CManager cm, CMtrans_services svc, 
						      transport_entry trans, attr_list listen_info);
extern void libcmsockets_LTX_set_write_notify(transport_entry trans, CMtrans_services svc,
					      struct socket_connection_data * scd, int enable);

extern int libcmsockets_LTX_read_to_buffer_func(CMtrans_services svc, struct socket_connection_data * scd, 
						void *buffer, int requested_len, int non_blocking);


extern int
libcmsockets_LTX_write_func(CMtrans_services svc, struct socket_connection_data * scd, void *buffer, int length);


extern int libcmsockets_LTX_writev_attr_func(CMtrans_services svc, struct socket_connection_data * scd, 
					     void *iov, int iovcnt, attr_list attrs);

extern int libcmsockets_LTX_NBwritev_attr_func(CMtrans_services svc, struct socket_connection_data * scd, 
					       void *iov, int iovcnt, attr_list attrs);
extern int libcmsockets_LTX_writev_func(CMtrans_services svc, struct socket_connection_data * scd, 
					void *iov, int iovcnt);


extern void *
libcmsockets_LTX_initialize(CManager cm, CMtrans_services svc);
#endif

#endif
