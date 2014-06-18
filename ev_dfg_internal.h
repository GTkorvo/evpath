typedef struct _leaf_element {
    char *name;
    char *FMtype;
} leaf_element, leaf_elemp;

/*
 * Node join is sent by clients to the master at EVdfg_join_dfg()
 */
typedef struct _EVnode_join_msg {
    char *node_name;
    char *contact_string;
    int source_count;
    int sink_count;
    leaf_element *sinks;
    leaf_element *sources;
} EVnode_join_msg, *EVnode_join_ptr;

/*
 * EVready is sent by master to the clients when the DFG should start
 */
typedef struct _EVready_msg {
    int node_id;
} EVready_msg, *EVready_ptr;

/*
 * Deploy_ack is sent by clients to master after a deploy has been completed
 */
typedef struct _EVdeploy_ack_msg {
    char *node_id;
} EVdeploy_ack_msg, *EVdeploy_ack_ptr;

/*
 * Shutdown sent by master to clients to indicate that it's time to shut down.
 */
typedef struct _EVshutdown_msg {
    int value;
} EVshutdown_msg, *EVshutdown_ptr;

/*
 * Shutdown_contribution is sent by clients to master to indicate readiness to shutdown
 */
typedef struct _EVshutdown_contribution_msg {
    int value;
} EVshutdown_contribution_msg, *EVshutdown_contribution_ptr;

/*
 * conn_shutdown is sent by clients to master to indicate a connection failure
 */
typedef struct _EVconn_shutdown_msg {
    int stone;
} EVconn_shutdown_msg, *EVconn_shutdown_ptr;

typedef struct _EVattr_stone_struct {
    long stone;
    char *attr_str;
} EVattr_stone_struct, *EVattr_stone_ptr;

/*
 * flush_attr_reconfig is sent by clients to master to flush attr values to master 
 * and/or to start voluntary reconfiguration
 */
typedef struct _EVflush_attrs_reconfig_msg {
    int reconfig;
    long count;
    EVattr_stone_struct *attr_stone_list;
} EVflush_attrs_reconfig_msg, *EVflush_attrs_reconfig_ptr;

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

/*
 * Deploy is sent by master to clients in order to deploy many stones at once.
 */
typedef struct _EVdfg_deploy_msg {
    char *canonical_name;
    int stone_count;
    deploy_msg_stone stone_list;
} EVdfg_deploy_msg, *EVdfg_deploy_ptr;

typedef enum {DFGnode_join=0, DFGdeploy_ack=1, DFGshutdown_contrib=2, DFGconn_shutdown=3, DFGflush_reconfig=4, DFGlast_msg} 
    EVmaster_msg_type;

/*
 * data structure used to maintain master's incoming message queue
 */
typedef struct _EVdfg_master_msg {
    EVmaster_msg_type msg_type;
    CMConnection conn;
    union {
	EVnode_join_msg  node_join;
	EVdeploy_ack_msg deploy_ack;
	EVshutdown_contribution_msg shutdown_contrib;
	EVconn_shutdown_msg conn_shutdown;
	EVflush_attrs_reconfig_msg flush_reconfig;
    } u;
    struct _EVdfg_master_msg *next;
} EVdfg_master_msg, *EVdfg_master_msg_ptr;


typedef struct {
    char *name;
    attr_list contact_list;
} *EVnode_list, EVnode_rec;

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
    int needs_ready;
} *EVint_node_list;

typedef enum {DFG_Joining=0, DFG_Starting=1, DFG_Running=2, DFG_Reconfiguring=3, DFG_Shutting_Down=4, DFG_Last_State=5} DFG_State;
extern char *str_state[];

#define STATUS_FAILED -3
#define STATUS_UNDETERMINED -2
#define STATUS_NO_CONTRIBUTION -1
#define STATUS_SUCCESS 0
#define STATUS_FAILURE 1
#define STATUS_FORCE 0x10000

typedef struct {
    int stone;
    int period_secs;
    int period_usecs;
} auto_stone_list;

struct _EVdfg {
    CManager cm;
    char *master_contact_str;
    char *master_command_contact_str;
    CMConnection master_connection;
    DFG_State state;
    int shutdown_value;
    int ready_condition;
    auto_stone_list *pending_auto_list;
    int *shutdown_conditions;
    int stone_count;
    EVdfg_stone *stones;
    int node_count;
    EVint_node_list nodes;
    EVdfgJoinHandlerFunc node_join_handler;
    EVdfgFailHandlerFunc node_fail_handler;
    EVdfgReconfigHandlerFunc node_reconfig_handler;
    int my_node_id;
    int realized;
    int already_shutdown;
    int active_sink_count;
    int deploy_ack_count;
    int deploy_ack_condition;
    EVdfg_master_msg_ptr queued_messages;
	
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
extern void INT_EVdfg_add_sink_action(EVdfg_stone stone, char *sink_name);
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
extern void INT_EVdfg_node_reconfig_handler ( EVdfg dfg, EVdfgReconfigHandlerFunc func );
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
extern void INT_EVdfg_register_sink_handler ( CManager cm, char *name, FMStructDescList list, EVSimpleHandlerFunc handler, void *client_data );
extern void INT_EVdfg_register_source ( char *name, EVsource src );
extern void INT_EVdfg_set_attr_list ( EVdfg_stone stone, attr_list attrs );
extern attr_list INT_EVdfg_get_attr_list ( EVdfg_stone stone );
extern int INT_EVdfg_shutdown ( EVdfg dfg, int result );
extern int INT_EVdfg_force_shutdown ( EVdfg dfg, int result );
extern int INT_EVdfg_source_active ( EVsource src );
extern int INT_EVdfg_wait_for_shutdown ( EVdfg dfg );

