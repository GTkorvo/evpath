#ifndef GEN_THREAD_H
#include "gen_thread.h"
#endif

typedef struct ev_free_block_rec {
    int ref_count;
    CManager cm;
    void *free_arg;
    void *block;
    IOFormat ioformat;
/*    EControlContext locking_context;*/
    attr_list attrs;
    struct free_block_rec *next;
} *ev_free_block_rec_p;

typedef enum { Event_App_Owned,  Event_CM_Owned } event_pkg_contents;

typedef struct _event_item {
    int ref_count;
    int event_encoded;
    event_pkg_contents contents;
    void *encoded_event;
    int event_len;
    void *decoded_event;
    IOEncodeVector encoded_eventv;
    IOFormat reference_format;
    CMFormat format;
    attr_list attrs;

    /* used for malloc/free */
    CMbuffer buffer;
    CManager cm;
    void *free_arg;
    EVFreeFunction free_func;
} event_item, *event_queue;

typedef enum { Action_Output = 0, Action_Terminal, Action_Filter, Action_Decode, Action_Split} action_value;

typedef struct output_action_struct {
    CMConnection conn;
    int remote_stone_id;
    int remote_path_len;
    char *remote_path;
    int new;
    int write_pending;
} output_action_vals;

typedef struct decode_action_struct {
    IOFormat decode_format; /* has conversion registered */
    IOFormat target_reference_format;
    IOContext context;
} decode_action_vals;

typedef struct queue_item {
    event_item *item;
    struct queue_item *next;
} queue_item;

typedef struct _action {
    action_value action_type;
    IOFormat reference_format;
    int requires_decoded;
    queue_item *queue_head;
    queue_item *queue_tail;
    union {
	output_action_vals out;
	decode_action_vals decode;
	int terminal_proto_action_number;
	int *split_stone_targets;
    }o;
} action;

struct terminal_proto_vals {
    EVSimpleHandlerFunc handler;
    void *client_data;
    int target_stone_id;
};

typedef struct _proto_action {
    action_value action_type;
    CMFormatList input_format_requirements;
    IOFormat reference_format;
    union {
	struct terminal_proto_vals term;
    }t;
} proto_action;

typedef struct _stone {
    int local_id;
    int default_action;
    int proto_action_count;
    struct _proto_action *proto_actions;
    int action_count;
    struct _action *actions;
} *stone_type;
    
typedef struct _event_path_data {
    int stone_count;
    stone_type stone_map;
    IOContext root_context;
    int output_action_count;
    action **output_actions;
    queue_item *queue_items_free_list;
    event_item *current_event_item;
    queue_item *taken_events_list;
    thr_mutex_t lock;
} *event_path_data;

struct _EVSource {
    CManager cm;
    CMFormat format;
    IOFormat reference_format;
    int local_stone_id;
};

extern void EVPinit(CManager cm);
