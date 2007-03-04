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

typedef void (*INT_CMfree_func) ARGS((void *block));

typedef enum _CMControlStyle {
    CMSingleThreaded, CMDedicatedServerThread, CMOccasionalPolling
} CMControlStyle;

typedef struct free_block_rec {
    int ref_count;
    CManager cm;
    void *block;
    INT_CMfree_func free_func;
    CManager locking_cm;
} *free_block_rec_p;

typedef void (*CMNetworkFunc) ARGS((void *svcs, void *client_data));


typedef void (*CMRemoveSelectFunc) ARGS((void *svcs, void *select_data, int fd));

typedef struct _periodic_task *periodic_task_handle;

struct _CMTaskHandle {
    CManager cm;
    periodic_task_handle task;
};

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
    char rem_header[16]; /* max 12 bytes w/o attributes, 16 bytes with */
    int rem_header_len;
    char *rem_attr_base;
    int rem_attr_len;
    IOEncodeVector vector_data;
};

typedef struct _CMCloseHandlerList {
    CMCloseHandlerFunc close_handler;
    void *close_client_data;
    struct _CMCloseHandlerList *next;
} *CMCloseHandlerList;

typedef struct _CMConnHandlerList {
    CMCloseHandlerFunc func;
    void *client_data;
} *CMConnHandlerList, CMConnHandlerListEntry;

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
    int failed;

    CMFormat *downloaded_formats;

    CMCloseHandlerList close_list;

    int write_callback_len;
    CMConnHandlerList write_callbacks;
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

typedef enum _CMTraceType {
    CMAlwaysTrace, CMControlVerbose, CMConnectionVerbose, CMLowLevelVerbose, CMDataVerbose, CMTransportVerbose, CMFormatVerbose, CMFreeVerbose, CMAttrVerbose, EVerbose, EVWarning, 
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
extern void
internal_add_shutdown_task(CManager cm, CMPollFunc func, void *client_data);
extern void
internal_cm_network_submit(CManager cm, CMbuffer cm_data_buf, 
			   attr_list attrs, CMConnection conn, 
			   void *buffer, int length, int stone_id);
#define CMcreate_attr_list(cm) CMint_create_attr_list(cm, __FILE__, __LINE__)
#define INT_CMfree_attr_list(cm, l) CMint_free_attr_list(cm, l, __FILE__, __LINE__)
#define CMadd_ref_attr_list(cm, l) CMint_add_ref_attr_list(cm, l, __FILE__, __LINE__)
#define CMattr_copy_list(cm, l) CMint_attr_copy_list(cm, l, __FILE__, __LINE__)
#define CMattr_merge_lists(cm, l1, l2) CMint_attr_merge_lists(cm, l1, l2, __FILE__, __LINE__)
#define CMdecode_attr_from_xmit(cm, l) CMint_decode_attr_from_xmit(cm, l, __FILE__, __LINE__)

extern attr_list CMint_create_attr_list(CManager cm, char *file, int line);
extern void CMint_free_attr_list(CManager cm, attr_list l, char *file, int line);
extern attr_list CMint_add_ref_attr_list(CManager cm, attr_list l, char *file, int line);
extern attr_list CMint_attr_copy_list(CManager cm, attr_list l, char *file, int line);
extern void CMint_attr_merge_lists(CManager cm, attr_list l1, attr_list l2, 
					char *file, int line);
extern attr_list CMint_decode_attr_from_xmit(CManager cm, void * buf, char *file, int line);
extern void* INT_CMrealloc ARGS((void *ptr, int size));
extern void* INT_CMmalloc ARGS((int size));
extern void INT_CMfree ARGS((void *ptr));
extern void INT_CMadd_shutdown_task ARGS((CManager cm, CMPollFunc func, void *client_data));
extern void INT_CManager_close ARGS((CManager cm));
extern CManager INT_CManager_create ();
extern int INT_CMlisten_specific ARGS((CManager cm, attr_list listen_info));
extern void INT_CMConnection_close ARGS((CMConnection conn));
extern void INT_CMremove_task ARGS((CMTaskHandle handle));
extern CMTaskHandle INT_CMadd_periodic ARGS((CManager cm, long period, 
					     CMPollFunc func, void *client_data));
extern CMTaskHandle
INT_CMadd_periodic_task ARGS((CManager cm, int period_sec, int period_usec, 
			  CMPollFunc func, void *client_data));
extern double
INT_CMregressive_probe_bandwidth ARGS((CMConnection conn, int size, attr_list attrs));
extern CMTaskHandle
INT_CMadd_delayed_task ARGS((CManager cm, int secs, int usecs, CMPollFunc func,
			     void *client_data));
extern int
INT_CMwrite_attr ARGS((CMConnection conn, CMFormat format, void *data, 
		       attr_list attrs));
extern int
INT_CMwrite_evcontrol ARGS((CMConnection conn, unsigned char type, int arg));
int INT_CMCondition_get ARGS((CManager cm, CMConnection dep));
void INT_CMCondition_signal ARGS((CManager cm, int condition));
void INT_CMCondition_set_client_data ARGS((CManager cm, int condition,
				       void *client_data));
void *INT_CMCondition_get_client_data ARGS((CManager cm, int condition));
int INT_CMCondition_wait ARGS((CManager cm, int condition));
extern attr_list INT_CMget_contact_list ARGS((CManager cm));
extern void INT_CMregister_non_CM_message_handler ARGS((int header, CMNonCMHandler handler));
extern void *INT_CMtake_buffer ARGS((CManager cm, void *data));
extern void INT_CMreturn_buffer ARGS((CManager cm, void *data));
extern CMConnection INT_CMget_conn ARGS((CManager cm, attr_list contact_list));
extern CMFormat INT_CMregister_format ARGS((CManager cm, char *format_name,
					    IOFieldList field_list,
					    CMFormatList subformat_list));
extern void
INT_EVforget_connection(CManager, CMConnection);
extern void
INT_EVhandle_control_message(CManager, CMConnection, unsigned char type, int arg);

extern EVaction
INT_EVassoc_mutated_imm_action(CManager cm, EVstone stone, EVaction act_num,
			       EVImmediateHandlerFunc func, void *client_data,
			       IOFormat reference_format);
extern void
INT_EVassoc_conversion_action(CManager cm, int stone_id, int stage, IOFormat target_format,
			      IOFormat incoming_format);
extern void
INT_CMregister_handler ARGS((CMFormat format, CMHandlerFunc handler, 
			void *client_data));
extern void
INT_EVaction_remove_split_target(CManager cm, EVstone stone, EVaction action,
			  EVstone target_stone);
extern long INT_CMprobe_latency ARGS((CMConnection conn, int msg_size,
				  attr_list attrs));
extern int
INT_CMwrite ARGS((CMConnection conn, CMFormat format, void *data));
extern IOFormat
INT_CMregister_user_format ARGS((CManager cm, IOContext context, char *format_name,
			      IOFieldList field_list, CMFormatList subformat_list));
extern void
INT_CMset_conversion_IOcontext ARGS((CManager cm, IOContext context,
				 IOFormat format, IOFieldList field_list,
				 int native_struct_size));
extern CMConnection
INT_CMget_indexed_conn ARGS((CManager cm, int i));
extern EVaction
INT_EVassoc_output_action(CManager cm, EVstone stone, attr_list contact_list, 
			  EVstone remote_stone);
extern int
INT_CMcontact_self_check ARGS((CManager cm, attr_list attrs));
extern int INT_CMtry_return_buffer ARGS((CManager cm, void *data));
extern IOFormat INT_CMget_IOformat_by_name ARGS((CManager cm, IOContext context,
					     char *name));
extern CMFormat
INT_CMregister_opt_format ARGS((CManager cm, char *format_name,
		       IOFieldList field_list, CMFormatList subformat_list,
		       IOOptInfo *opt_info));
extern 
void INT_CMpoll_network ARGS((CManager cm));
extern IOFormat INT_CMlookup_user_format ARGS((CManager cm, IOFieldList field_list));
extern IOFormat *INT_CMget_subformats_IOcontext ARGS((CManager cm, 
						  IOContext context,
						  void *buffer));
extern EVaction
INT_EVassoc_terminal_action(CManager cm, EVstone stone, CMFormatList format_list, 
			EVSimpleHandlerFunc handler, void* client_data);
extern 
void INT_CMrun_network ARGS((CManager cm));
extern int
INT_EVaction_add_split_target(CManager cm, EVstone stone, EVaction action,
			  EVstone target_stone);

extern int
INT_EVaction_set_output(CManager cm, EVstone stone, EVaction action, 
		    int output_index, EVstone output_stone);
extern void*
INT_CMget_transport_data ARGS((CMConnection conn));

extern EVaction
INT_EVassoc_filter_action(CManager cm, EVstone stone, 
		      CMFormatList incoming_format_list, 
		      EVSimpleHandlerFunc handler, EVstone out_stone,
		      void* client_data);
extern int INT_CMCondition_has_failed ARGS((CManager cm, int condition));
extern int
INT_EVtake_event_buffer ARGS((CManager cm, void *event));
extern void
INT_EVPsubmit(CManager cm, int local_path_id, void *data, IOFormat format);
extern int INT_CMlisten ARGS((CManager cm));
extern char *
INT_create_filter_action_spec(CMFormatList format_list, char *function);
extern char *
INT_create_router_action_spec(CMFormatList format_list, char *function);
extern void
INT_EVenable_auto_stone(CManager cm, EVstone stone_num, int period_sec, 
		    int period_usec);
extern int INT_CMfork_comm_thread ARGS((CManager cm));
extern int
INT_CMregister_write_callback ARGS((CMConnection conn, 
				CMWriteCallbackFunc handler,
				void *client_data));
extern void
INT_CMunregister_write_callback ARGS((CMConnection conn, int id));
extern void
INT_EVsubmit_general(EVsource source, void *data, EVFreeFunction free_func,
		 attr_list attrs);
extern void
INT_EVsubmit_encoded(CManager cm, EVstone stone, void *data, int data_len, attr_list attrs);
extern EVsource
INT_EVcreate_submit_handle_free(CManager cm, EVstone stone, CMFormatList data_format,
			    EVFreeFunction free_func, void *client_data);
extern void
INT_CMadd_poll ARGS((CManager cm, CMPollFunc func, void *client_data));
extern void
INT_EVPsubmit_encoded(CManager cm, int local_path_id, void *data, int len);
extern CMFormat INT_CMlookup_format ARGS((CManager cm, IOFieldList field_list));
extern char *
INT_create_transform_action_spec(CMFormatList format_list, CMFormatList out_format_list, char *function);
extern char *
INT_create_multityped_action_spec(CMFormatList *input_format_lists, CMFormatList output_format_list, char *function);

extern int INT_CMCondition_has_signaled ARGS((CManager cm, int condition));

extern EVaction
INT_EVassoc_multi_action(CManager cm, EVstone stone, char *queue_spec, 
		      void *client_data);
extern attr_list
INT_CMget_specific_contact_list ARGS((CManager cm, attr_list attrs));

extern EVaction
INT_EVassoc_immediate_action(CManager cm, EVstone stone, char *queue_spec, 
		      void *client_data);
extern CMtrans_services
INT_CMget_static_trans_services ARGS(());

extern void INT_EVfree_stone(CManager cm, EVstone stone);
extern void INT_CMsleep ARGS((CManager cm, int secs));
extern int INT_CMget_self_ip_addr();
extern attr_list INT_CMConnection_get_attrs ARGS((CMConnection conn));
extern EVstone INT_EValloc_stone(CManager cm);
extern void * INT_CMcreate_compat_info ARGS((CMFormat format, char *xform_code,
			int *len_p));
extern IOContext INT_CMget_user_type_context ARGS((CManager cm));
extern IOFormat INT_CMget_format_app_IOcontext ARGS((CManager cm, IOContext context,
					     void *buffer, void *app_context));
extern IOFormat INT_CMget_format_IOcontext ARGS((CManager cm, IOContext context,
					     void *buffer));
extern CMConnection
INT_CMinitiate_conn ARGS((CManager cm, attr_list contact_list));
extern void
INT_CMconn_register_close_handler ARGS((CMConnection conn, 
				    CMCloseHandlerFunc func, 
				    void *client_data));
extern void
INT_EVreturn_event_buffer ARGS((CManager cm, void *event));
extern void
INT_CMConnection_add_reference ARGS((CMConnection conn));
extern int
INT_CMConnection_set_character ARGS((CMConnection conn, attr_list attrs));
extern EVaction
INT_EVassoc_split_action(CManager cm, EVstone stone, EVstone *target_list);
extern void
INT_CMremove_periodic ARGS((CMTaskHandle handle));
extern EVsource
INT_EVcreate_submit_handle(CManager cm, EVstone stone, CMFormatList data_format);
extern void INT_CMfree_user_type_context ARGS((CManager cm, IOContext context));
extern IOFormat INT_EVget_src_ref_format(EVsource source);
extern long
INT_CMprobe_bandwidth ARGS((CMConnection conn, int size, attr_list attrs));
extern int INT_CMConnection_write_would_block ARGS((CMConnection conn));
extern void INT_CMusleep ARGS((CManager cm, int usecs));
extern void INT_CM_insert_contact_info ARGS((CManager cm, attr_list attrs));
extern void INT_CM_fd_add_select ARGS((CManager cm, int fd, select_func handler_func, void *param1, void *param2));
extern int INT_EVfreeze_stone(CManager cm, EVstone stone_id);
extern int INT_EVunfreeze_stone(CManager cm, EVstone stone_id);
extern int INT_EVdrain_stone(CManager cm, EVstone stone_id);
extern EVevent_list INT_EVextract_stone_events(CManager cm, EVstone stone_id);
extern attr_list INT_EVextract_attr_list(CManager cm, EVstone stone_id);
extern void INT_EVset_attr_list(CManager cm, EVstone stone_id, attr_list list);
extern void INT_EVset_store_limit(CManager cm, EVstone stone_num, EVaction action_num, int store_limit);
extern void INT_EVstore_start_send(CManager cm, EVstone stone_num, EVaction action_num);
extern int INT_EVstore_is_sending(CManager cm, EVstone stone_num, EVaction action_num);
extern int INT_EVstore_count(CManager cm, EVstone stone_num, EVaction action_num);
extern int INT_EVdestroy_stone(CManager cm, EVstone stone_id);
extern void INT_EVfree_source(EVsource source);

extern void INT_EVsend_stored(CManager cm, EVstone stone, EVaction action);
extern void INT_EVclear_stored(CManager cm, EVstone stone, EVaction action);
extern EVaction INT_EVassoc_store_action(CManager cm, EVstone stone, EVstone out_stone, int store_limit); 

