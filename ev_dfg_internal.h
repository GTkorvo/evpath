typedef struct _leaf_element {
    char *name;
    char *FMtype;
} leaf_element, leaf_elemp;

typedef struct _EVregister_msg {
    char *node_name;
    char *contact_string;
    int source_count;
    int sink_count;
    leaf_element *sinks;
    leaf_element *sources;
} EVregister_msg, *EVregister_ptr;

typedef struct _EVready_msg {
    int node_id;
} EVready_msg, *EVready_ptr;

typedef struct _EVstartup_ack_msg {
    char *node_id;
} EVstartup_ack_msg, *EVstartup_ack_ptr;

typedef struct _EVshutdown_msg {
    int value;
} EVshutdown_msg, *EVshutdown_ptr;

typedef struct _EVconn_shutdown_msg {
    int stone;
} EVconn_shutdown_msg, *EVconn_shutdown_ptr;

typedef struct _EVdfg_msg_stone {
    int global_stone_id;
    char *attrs;
    int period_secs;
    int period_usecs;
    int out_count;
    int *out_links;
    char *action;
    int extra_actions;
    char **xactions;
} *deploy_msg_stone;

typedef struct _EVdfg_stones_msg {
    char *canonical_name;
    int stone_count;
    deploy_msg_stone stone_list;
} EVdfg_stones_msg, *EVdfg_stones_ptr;

typedef struct {
    char *name;
    attr_list contact_list;
} *EVnode_list, EVnode_rec;

typedef struct {
    int msg_type;
    void *msg;
    CMConnection conn;
} queued_msg;

struct _EVdfg_stone {
    EVdfg dfg;
    int node;
    int bridge_stone;
    int stone_id;
    attr_list attrs;
    int period_secs;
    int period_usecs;
    int out_count;
    EVdfg_stone *out_links;
    int action_count;
    char *action;
    char **extra_actions;
	
    /* dynamic reconfiguration structures below */
    EVdfg_stone *new_out_links;
    EVdfg_stone bridge_target;
    EVevent_list pending_events;
    EVevent_list processed_pending_events;
    int new_out_count;
    int *new_out_ports;
    int invalid;
    int frozen;
};

typedef struct _EVint_node_rec {
    char *name;
    char *canonical_name;
    attr_list contact_list;
    char *str_contact_list;
    CMConnection conn;
    int self;
    int shutdown_status_contribution;
} *EVint_node_list;

typedef enum {DFG_Joining, DFG_Starting, DFG_Running, DFG_Reconfiguring, DFG_Shutting_Down} DFG_State;
extern char *str_state[];

#define STATUS_FAILED -3
#define STATUS_UNDETERMINED -2
#define STATUS_NO_CONTRIBUTION -1
#define STATUS_SUCCESS 0
#define STATUS_FAILURE 1

struct _EVdfg {
    CManager cm;
    char *master_contact_str;
    char *master_command_contact_str;
    CMConnection master_connection;
    DFG_State state;
    int shutdown_value;
    int ready_condition;
    int *shutdown_conditions;
    int stone_count;
    EVdfg_stone *stones;
    int node_count;
    EVint_node_list nodes;
    EVdfgJoinHandlerFunc node_join_handler;
    EVdfgFailHandlerFunc node_fail_handler;
    int my_node_id;
    int realized;
    int already_shutdown;
    int active_sink_count;
    int deploy_ack_count;
    int startup_ack_condition;
    queued_msg *queued_messages;
	
    /* reconfig data is below */
    int reconfig;
    int deployed_stone_count;
    int old_node_count;
    int sig_reconfig_bool;
    int transfer_events_count;
    int delete_count;
    int **transfer_events_list;
    int **delete_list;
	
    int no_deployment;
};

extern int INT_EVdfg_active_sink_count ( EVdfg dfg );
extern void INT_EVdfg_add_action ( EVdfg_stone stone, char *action_spec );
extern void INT_EVdfg_assign_canonical_name ( EVdfg dfg, char *given_name, char *canonical_name );
extern void INT_EVdfg_assign_node ( EVdfg_stone stone, char *node );
extern EVdfg INT_EVdfg_create ( CManager cm );
extern EVdfg_stone INT_EVdfg_create_sink_stone ( EVdfg dfg, char *handler_name );
extern EVdfg_stone INT_EVdfg_create_source_stone ( EVdfg, char *source_name );
extern EVdfg_stone INT_EVdfg_create_stone ( EVdfg dfg, char *action_spec );
extern void INT_EVdfg_enable_auto_stone ( EVdfg_stone stone, int period_sec, int period_usec );
extern void INT_EVdfg_freeze_next_bridge_stone ( EVdfg dfg, int stone_index );
extern char* INT_EVdfg_get_contact_list ( EVdfg dfg );
extern void INT_EVdfg_join_dfg ( EVdfg dfg, char *node_name, char *master_contact );
extern void INT_EVdfg_link_port ( EVdfg_stone source, int source_port, EVdfg_stone destination );
extern void INT_EVdfg_node_fail_handler ( EVdfg dfg, EVdfgFailHandlerFunc func );
extern void INT_EVdfg_node_join_handler ( EVdfg dfg, EVdfgJoinHandlerFunc func );
extern void INT_EVdfg_ready_for_shutdown ( EVdfg dfg );
extern int INT_EVdfg_ready_wait ( EVdfg dfg );
extern int INT_EVdfg_realize ( EVdfg dfg );
extern void INT_EVdfg_reconfig_delete_link ( EVdfg dfg, int src_index, int dest_index );
extern void INT_EVdfg_reconfig_insert ( EVdfg dfg, int src_stone_id, EVdfg_stone new_stone, int dest_stone_id, EVevent_list q_event );
extern void INT_EVdfg_reconfig_insert_on_port(EVdfg dfg, EVdfg_stone src_stone, int port, EVdfg_stone new_stone, EVevent_list q_events);
extern void INT_EVdfg_reconfig_link_port ( EVdfg_stone src, int port, EVdfg_stone dest, EVevent_list q_events );
extern void INT_EVdfg_reconfig_link_port_from_stone ( EVdfg dfg, EVdfg_stone src_stone, int port, int target_index, EVevent_list q_events );
extern void INT_EVdfg_reconfig_link_port_to_stone ( EVdfg dfg, int src_stone_index, int port, EVdfg_stone target_stone, EVevent_list q_events );
extern void INT_EVdfg_reconfig_transfer_events ( EVdfg dfg, int src_stone_index, int src_port, int dest_stone_index, int dest_port );
extern void INT_EVdfg_register_node_list ( EVdfg dfg, char** list );
extern void INT_EVdfg_register_raw_sink_handler ( CManager cm, char *name, EVRawHandlerFunc handler );
extern void INT_EVdfg_register_sink_handler ( CManager cm, char *name, FMStructDescList list, EVSimpleHandlerFunc handler );
extern void INT_EVdfg_register_source ( char *name, EVsource src );
extern void INT_EVdfg_set_attr_list ( EVdfg_stone stone, attr_list attrs );
extern int INT_EVdfg_shutdown ( EVdfg dfg, int result );
extern int INT_EVdfg_source_active ( EVsource src );
extern int INT_EVdfg_wait_for_shutdown ( EVdfg dfg );

