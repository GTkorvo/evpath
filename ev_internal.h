#ifndef GEN_THREAD_H
#include "gen_thread.h"
#endif

typedef struct ev_free_block_rec {
    int ref_count;
/*    EControlContext ec;*/
    void *free_arg;
    void *block;
/*    EventFreeFunction free_func;*/
    IOFormat ioformat;
/*    EControlContext locking_context;*/
    IOBuffer iobuffer;
    attr_list attrs;
    struct free_block_rec *next;
} *ev_free_block_rec_p;

typedef enum { Event_Unencoded_App_Owned } event_pkg_contents;

typedef struct _event_item {
    int ref_count;
    event_pkg_contents contents;
    void *encoded_event;
    int event_len;
    void *decoded_event;
    IOEncodeVector encoded_eventv;
    IOFormat format;
    ev_free_block_rec_p block_rec;
    attr_list attrs;
} event_item, *event_queue;

typedef enum { Action_Output, Action_Terminal} action_value;

typedef struct output_action_struct {
    CMConnection conn;
    int remote_stone_id;
    int remote_path_len;
    char *remote_path;
    int new;
    int write_pending;
} output_action_vals;

typedef struct queue_item {
    event_item *item;
    struct queue_item *next;
} queue_item;

typedef struct _action {
    action_value action_type;
    queue_item *queue_head;
    queue_item *queue_tail;
    union {
	output_action_vals out;
    };
} action;

struct terminal_proto_vals {
    void *handler;
    void *client_data;
};

typedef struct _proto_action {
    action_value action_type;
    CMFormatList input_format_requirements;
    union {
	struct terminal_proto_vals term;
    };
} proto_action;

typedef struct _format_map_entry {
    IOFormat format;
    struct _action *action;
} format_map_entry;

typedef struct _stone {
    int local_id;
    int default_action;
    int proto_action_count;
    struct _proto_action *proto_actions;
    int action_count;
    struct _action *actions;
    int format_map_count;
    struct _format_map_entry *map;
} *stone_type;
    
typedef struct _event_path_data {
    int stone_count;
    stone_type stone_map;
    IOContext root_context;
    int output_action_count;
    action **output_actions;
    thr_mutex_t lock;
} *event_path_data;

struct _EVSource {
    CManager cm;
    IOFormat format;
    int local_stone_id;
};
