#ifndef __I_O__
#include <io.h>
#endif
#ifndef CERCS_ENV_H
#include <cercs_env.h>
#endif

#include <ev_internal.h>

#ifndef GEN_THREAD_H
#define thr_mutex_t void*
#define thr_thread_t void *
#endif

struct _ecl_code_struct;



typedef struct _DelaySizeMtx {
/************
AveRTDelay[i] is the Average RTT to tranferring data of 
size MsgSize[i].  i=0,1,...,MsgNum. 
************/
    int MsgNum;			/* num of msgs */
    double *AveRTDelay;
    int *MsgSize;
}DelaySizeMtx;


typedef struct _CMincoming_format {
    IOFormat format;
    CMHandlerFunc handler;
    void *client_data;
    IOcompat_formats older_format;
	IOFormat local_prior_format;
	IOContext local_iocontext;
    CMFormat f2_format;
    int f1_struct_size;
    struct _ecl_code_struct *code;
} *CMincoming_format_list;

struct _CMControlList;
typedef struct _CMControlList *CMControlList;

struct _pending_format_requests {
    char *server_id;
    int id_length;
    int condition;
    int top_request;
};

typedef struct func_entry {
    CMPollFunc func;
    CManager cm;
    void *client_data;
} func_entry;

#include "cm_transport.h"

typedef struct _CManager {
    transport_entry *transports;
    int initialized;
    int reference_count;

    CMControlList control_list;	/* the control list for this DE */

    int in_format_count;
    CMincoming_format_list in_formats;
    
    int reg_format_count;
    CMFormat *reg_formats;

    int reg_user_format_count;
    CMFormat *reg_user_formats;
  
    int pending_request_max;
    struct _pending_format_requests *pbio_requests;

    int connection_count;
    CMConnection *connections;

    thr_mutex_t exchange_lock;
    int locked;
    int closed;
    int abort_read_ahead;

    IOContext IOcontext;	/* pbio context for data encoding */
    thr_mutex_t context_lock;

    CMbuffer taken_buffer_list;
    CMbuffer cm_buffer_list;

    attr_list *contact_lists;

    func_entry *shutdown_functions;

    struct _event_path_data *evp;
} CManager_s;

typedef struct _CMCondition *CMCondition;

typedef void (*CMfree_func) ARGS((void *block));

typedef enum _CMControlStyle {
    CMSingleThreaded, CMDedicatedServerThread, CMOccasionalPolling
} CMControlStyle;

typedef struct free_block_rec {
    int ref_count;
    CManager cm;
    void *block;
    CMfree_func free_func;
    CManager locking_cm;
} *free_block_rec_p;

typedef void (*CMNetworkFunc) ARGS((void *svcs, void *client_data));


typedef void (*CMRemoveSelectFunc) ARGS((void *svcs, void *select_data, int fd));

typedef struct _periodic_task *periodic_task_handle;

typedef periodic_task_handle (*CMAddPeriodicFunc) 
    ARGS((void *svcs, void *select_data, int period_sec, int period_usec,
	  select_list_func func, void *param1, void *param2));

typedef void (*CMRemovePeriodicFunc) ARGS((void *svcs, void *select_data, 
					   periodic_task_handle handle));

typedef void (*CMWakeSelectFunc) ARGS((void *svcs, void *select_data));

typedef struct _CMControlList {
    func_entry network_blocking_function;
    func_entry network_polling_function;
    func_entry *polling_function_list;
    int cl_consistency_number;

    int select_initialized;
    void *select_data;
    CMAddSelectFunc add_select;
    CMRemoveSelectFunc remove_select;
    CMAddSelectFunc write_select;
    CMAddPeriodicFunc add_periodic;
    CMAddPeriodicFunc add_delayed_task;
    CMRemovePeriodicFunc remove_periodic;
    CMWakeSelectFunc stop_select;
    CMWakeSelectFunc wake_select;
    /* 
     * CLs can be used by multiple DEs, close it when ref count reaches
     * zero 
     */
    int reference_count;
    int free_reference_count;

    CMCondition condition_list;
    int next_condition_num;

    thr_mutex_t list_mutex;
    int locked;

    int closed;
    int has_thread;
    thr_thread_t server_thread;
} CMControlList_s;

struct queued_data_rec {
    char rem_header[12];
    int rem_header_len;
    char *rem_attr_base;
    int rem_attr_len;
    IOEncodeVector vector_data;
};

struct _CMConnection {
    CManager cm;
    /* remote contact info */

    transport_entry trans;
    void *transport_data;
    int ref_count;
    thr_mutex_t write_lock;
    thr_mutex_t read_lock;
    IOBuffer io_out_buffer;
    int closed;

    CMFormat *downloaded_formats;

    CMCloseHandlerFunc close_handler;
    void *close_client_data;

    IOContext IOsubcontext;
    AttrBuffer attr_encode_buffer;
    void *foreign_data_handler;

    CMbuffer partial_buffer;	/* holds data from partial reads */
    int buffer_full_point;	/* data required for buffer to be full */
    int buffer_data_end;	/* last point with valid data in buffer */

    attr_list characteristics;
    chr_time bandwidth_start_time;
    chr_time regressive_bandwidth_start_time; /*ztcai*/
    attr_list attrs;
    struct queued_data_rec queued_data;
    int write_pending;
    int do_non_blocking_write;
};

struct _CMFormat {
    CManager cm;
    char *format_name;
    IOContext IOsubcontext;
    IOFormat  format;
    void *field_list_addr;
    CMHandlerFunc handler;
    void *client_data;
    IOFieldList field_list;
    CMFormatList subformat_list;
    IOOptInfo *opt_info;
    int registration_pending;
};

#define CManager_lock(cm) IntCManager_lock(cm, __FILE__, __LINE__)
#define CManager_unlock(cm) IntCManager_unlock(cm, __FILE__, __LINE__)
extern void IntCManager_lock ARGS((CManager cm, char *file, int line));
extern void IntCManager_unlock ARGS((CManager cm, char *file, int line));
extern int CManager_locked ARGS((CManager cm));

extern void CMControlList_lock ARGS((CMControlList cl));
extern void CMControlList_unlock ARGS((CMControlList cl));
extern int CMControlList_locked ARGS((CMControlList cl));
#define CMConn_write_lock(cm) IntCMConn_write_lock(cm, __FILE__, __LINE__)
#define CMConn_write_unlock(cm) IntCMConn_write_unlock(cm, __FILE__, __LINE__)
extern void IntCMConn_write_lock ARGS((CMConnection cl, char *file, 
				       int line));
extern void IntCMConn_write_unlock ARGS((CMConnection cl, char *file,
					 int line));
extern int CMConn_write_locked ARGS((CMConnection cl));
extern void CMglobal_data_lock();
extern void CMglobal_data_unlock();
extern int CMglobal_data_locked();

typedef enum _CMTraceType {
    CMAlwaysTrace, CMControlVerbose, CMConnectionVerbose, CMLowLevelVerbose, CMDataVerbose, CMTransportVerbose, CMFormatVerbose, CMFreeVerbose, EVerbose,
    CMLastTraceType /* add before this one */
} CMTraceType;

extern void 
CMtrace_out ARGS((CManager cm, CMTraceType trace_type, char *format, ...));

extern int
CMtrace_on ARGS((CManager cm, CMTraceType trace_type));

extern void 
CMDataAvailable ARGS((transport_entry trans, CMConnection conn));

extern void 
CMWriteQueuedData ARGS((transport_entry trans, CMConnection conn));

extern CMincoming_format_list
CMidentify_CMformat ARGS((CManager cm, IOFormat format));

extern void CMtransport_trace ARGS((CManager cm, char *format, ...));

extern void
CM_fd_add_select ARGS((CManager cm, int fd, select_list_func handler_func,
		       void *param1, void *param2));

extern void
CM_fd_write_select ARGS((CManager cm, int fd, select_list_func handler_func,
			 void *param1, void *param2));

extern void CM_fd_remove_select ARGS((CManager cm, int fd));

extern CMConnection
CMConnection_create ARGS((transport_entry trans, void *transport_data,
			  attr_list conn_attrs));

extern void free_CMFormat ARGS((CMFormat format));

extern void CMcomplete_format_registration ARGS((CMFormat format, int lock));
extern int CMcontrol_list_wait ARGS((CMControlList cl));
extern int CMcontrol_list_poll ARGS((CMControlList cl));
extern int load_transport ARGS((CManager cm, const char *trans_name));

extern int CMinternal_listen ARGS((CManager cm, attr_list listen_info));
extern CMConnection CMinternal_get_conn ARGS((CManager cm, attr_list attrs));
extern void CMconn_fail_conditions ARGS((CMConnection conn));
extern int CMpbio_send_format_preload ARGS((IOFormat ioformat, CMConnection conn));
extern void CMformat_preload ARGS((CMConnection conn, CMFormat format));
extern void CMinit_local_formats ARGS((CManager cm));

extern CMbuffer cm_get_data_buf ARGS((CManager cm, int length));
extern void cm_return_data_buf ARGS((CMbuffer cmb));

extern CMincoming_format_list CMidentify_rollbackCMformat 
	ARGS((CManager cm, IOFormat format));
extern void
CMcreate_conversion ARGS((CManager cm, CMincoming_format_list cm_format));
extern int
process_old_format_data ARGS((CManager cm, CMincoming_format_list cm_format,
	   	char **decode_buff, CMbuffer *cm_decode_buffer));
extern int
internal_write_event(CMConnection conn, CMFormat format, 
		     void *remote_path_id, int path_len, event_item *event,
		     attr_list attrs);
