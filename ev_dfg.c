#include "config.h"
#define _GNU_SOURCE
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <strings.h>

#include "evpath.h"
#include "cm_internal.h"
#include "ev_deploy.h"
#include "revpath.h"
#include "ev_dfg_internal.h"
#include <assert.h>

char *str_state[] = {"DFG_Joining", "DFG_Starting", "DFG_Running", "DFG_Reconfiguring", "DFG_Shutting_Down"};

static void handle_conn_shutdown(EVdfg dfg, int stone);
static void handle_node_join(EVdfg dfg, char *node_name, char *contact_string, CMConnection conn);

static void
queue_conn_shutdown_message(EVdfg dfg, int stone)
{
    int count = 0;
    CMtrace_out(cm, EVdfgVerbose, "EVDFG queue_conn_shutdown_message -  master DFG state is %s\n", str_state[dfg->state]);
    if (dfg->queued_messages == NULL) {
	dfg->queued_messages = malloc(sizeof(dfg->queued_messages[0]) * 2);
    } else {
	while (dfg->queued_messages[count].msg != NULL) count++;
	dfg->queued_messages = realloc(dfg->queued_messages, 
				       sizeof(dfg->queued_messages[0]) * (count + 2));
    }
    CMtrace_out(dfg->cm, EVdfgVerbose, "Queueing at (position %d) connection shutdown message for stone %x\n", count, stone);
    dfg->queued_messages[count].msg_type = 1;   /* conn_shutdown */
    dfg->queued_messages[count].msg = malloc(sizeof(EVconn_shutdown_msg));
    ((EVconn_shutdown_msg*)dfg->queued_messages[count].msg)->stone = stone;
    dfg->queued_messages[count].conn = NULL;
    dfg->queued_messages[count+1].msg_type = 0;
    dfg->queued_messages[count+1].msg = NULL;
}

static void
queue_node_join_message(EVdfg dfg, char *node_name, char *contact_string, CMConnection conn)
{
    int count = 0;
    CMtrace_out(cm, EVdfgVerbose, "EVDFG queue_node_join_message -  master DFG state is %s\n", str_state[dfg->state]);
    if (dfg->queued_messages == NULL) {
	dfg->queued_messages = malloc(sizeof(dfg->queued_messages[0]) * 2);
    } else {
	while (dfg->queued_messages[count].msg != NULL) count++;
	dfg->queued_messages = realloc(dfg->queued_messages, 
				       sizeof(dfg->queued_messages[0]) * (count + 2));
    }
    CMtrace_out(dfg->cm, EVdfgVerbose, "Queueing at (position %d) node join message for node %s(%s)\n", count, 
		node_name, contact_string);
    dfg->queued_messages[count].msg_type = 2;   /* node_join */
    dfg->queued_messages[count].msg = malloc(sizeof(EVregister_msg));
    ((EVregister_msg*)dfg->queued_messages[count].msg)->node_name = strdup(node_name);
    ((EVregister_msg*)dfg->queued_messages[count].msg)->contact_string = strdup(contact_string);
    dfg->queued_messages[count].conn = conn;
    dfg->queued_messages[count+1].msg_type = 0;
    dfg->queued_messages[count+1].msg = NULL;
}

static void
handle_queued_messages(EVdfg dfg)
{
    /* SHOULD */
    /*  1 - consolidate node failure messages (likely to get several for each node) and handle these first */
    /*  2 -  handle node join messages last */
    /* FOR THE MOMENT */
    /* just do everything in order */
    /* beware the the list might change while we're running a handler */
    if (dfg->queued_messages == NULL) return;
    CMtrace_out(cm, EVdfgVerbose, "EVDFG handle_queued_messages -  master DFG state is %s\n", str_state[dfg->state]);
    if (dfg->state == DFG_Starting) {
	CMtrace_out(cm, EVdfgVerbose, "EVDFG handle_queued_messages -  returning because state is inappropriate\n");
	return;
    }
    while (dfg->queued_messages[0].msg != NULL) {
	int count;
	int msg_type = dfg->queued_messages[0].msg_type;
	CMConnection conn = dfg->queued_messages[0].conn;
	void *msg = dfg->queued_messages[0].msg;

	count = 0;
	while (dfg->queued_messages[count+1].msg != NULL) {
	    /* copy down */
	    dfg->queued_messages[count] = dfg->queued_messages[count+1];
	    count++;
	}
	dfg->queued_messages[count].msg = NULL;
	switch(msg_type) {
	case 1:
	    dfg->state = DFG_Reconfiguring;
	    CMtrace_out(cm, EVdfgVerbose, "EVDFG queued conn shutdown -  master DFG state is now %s\n", str_state[dfg->state]);
	    handle_conn_shutdown(dfg, ((EVconn_shutdown_msg*)msg)->stone);
	    break;
	case 2:
	    dfg->state = DFG_Reconfiguring;
	    CMtrace_out(cm, EVdfgVerbose, "EVDFG queued node join -  master DFG state is now %s\n", str_state[dfg->state]);
	    handle_node_join(dfg, ((EVregister_msg*)msg)->node_name, ((EVregister_msg*)msg)->contact_string, conn);
	}
	free(msg);
	CMtrace_out(cm, EVdfgVerbose, "EVDFG handle queued end loop -  master DFG state is now %s\n", str_state[dfg->state]);
    }
    CMtrace_out(cm, EVdfgVerbose, "EVDFG handle queued exiting -  master DFG state is now %s\n", str_state[dfg->state]);
}

EVdfg_stone
INT_EVdfg_create_source_stone(EVdfg dfg, char *source_name)
{
    int len = strlen(source_name) + strlen("source:");
    char *act = malloc(len + 1);
    strcpy(stpcpy(&act[0], "source:"), source_name);
    return INT_EVdfg_create_stone(dfg, &act[0]);
}

extern void 
INT_EVdfg_add_sink_action(EVdfg_stone stone, char *sink_name)
{
    int len = strlen(sink_name) + strlen("sink:");
    char *act = malloc(len + 1);
    strcpy(stpcpy(&act[0], "sink:"), sink_name);
    INT_EVdfg_add_action(stone, &act[0]);
}

EVdfg_stone
INT_EVdfg_create_sink_stone(EVdfg dfg, char *sink_name)
{
    int len = strlen(sink_name) + strlen("sink:");
    char *act = malloc(len + 1);
    strcpy(stpcpy(&act[0], "sink:"), sink_name);
    return INT_EVdfg_create_stone(dfg, &act[0]);
}

void
INT_EVdfg_add_action(EVdfg_stone stone, char *action)
{
    if (stone->action == NULL) {
	stone->action = action;
	return;
    }
    if (stone->extra_actions == NULL) {
	stone->extra_actions = malloc(sizeof(stone->extra_actions[0]));
    } else {
	stone->extra_actions = realloc(stone->extra_actions,
				       stone->action_count * sizeof(stone->extra_actions[0]));
    }
    stone->extra_actions[stone->action_count - 1] = action;
    stone->action_count++;
}

EVdfg_stone
INT_EVdfg_create_stone(EVdfg dfg, char *action)
{
    EVdfg_stone stone = malloc(sizeof(struct _EVdfg_stone));
    stone->dfg = dfg;
    stone->node = -1;
    stone->bridge_stone = 0;
    stone->stone_id = -1;
    stone->attrs = NULL;
    stone->period_secs = -1;
    stone->period_usecs = -1;
    stone->out_count = 0;
    stone->out_links = NULL;
    stone->action_count = 1;
    stone->action = action;
    stone->extra_actions = NULL;
    stone->new_out_count = 0;
    stone->invalid = 0;
    stone->frozen = 0;
    stone->bridge_target = NULL;
    stone->pending_events = NULL;
    stone->processed_pending_events = NULL;
	
    if (dfg->stone_count == 0) {
	dfg->stones = malloc(sizeof(dfg->stones[0]));
    } else {
	dfg->stones = realloc(dfg->stones, 
			      sizeof(dfg->stones[0]) * (dfg->stone_count + 1));
    }
    stone->stone_id = 0x80000000 | dfg->stone_count;
    dfg->stones[dfg->stone_count++] = stone;
    return stone;
}

extern void 
INT_EVdfg_enable_auto_stone(EVdfg_stone stone, int period_sec, 
			int period_usec)
{
    stone->period_secs = period_sec;
    stone->period_usecs = period_usec;
}


static void fdump_dfg_stone(FILE* out, EVdfg_stone s);

static void 
reconfig_link_port(EVdfg_stone src, int port, EVdfg_stone dest, EVevent_list q_events)
{
    if (src->new_out_count == 0) {
        src->new_out_links = malloc(sizeof(src->new_out_links[0]));
        memset(src->new_out_links, 0, sizeof(src->new_out_links[0]));
        src->new_out_count = 1;
	src->new_out_ports = malloc(sizeof(src->new_out_ports[0]));
    } else {
        src->new_out_links = realloc(src->new_out_links,
				     sizeof(src->new_out_links[0]) * (src->new_out_count+1));
        memset(&src->new_out_links[src->new_out_count], 0, sizeof(src->new_out_links[0]));
        ++(src->new_out_count);
	src->new_out_ports = realloc(src->new_out_ports, sizeof(src->new_out_ports[0]) * (src->new_out_count));
    }
    src->new_out_links[src->new_out_count - 1] = dest;
    src->processed_pending_events = q_events;
    src->new_out_ports[src->new_out_count - 1] = port;
}


extern void INT_EVdfg_reconfig_link_port_to_stone(EVdfg dfg, int src_stone_index, int port, EVdfg_stone target_stone, EVevent_list q_events) {
	reconfig_link_port(dfg->stones[src_stone_index], port, target_stone, q_events);
}

extern void INT_EVdfg_reconfig_link_port_from_stone(EVdfg dfg, EVdfg_stone src_stone, int port, int target_index, EVevent_list q_events) {
	reconfig_link_port(src_stone, port, dfg->stones[target_index], q_events);
}

extern void INT_EVdfg_reconfig_link_port(EVdfg_stone src, int port, EVdfg_stone dest, EVevent_list q_events) {
	reconfig_link_port(src, port, dest, q_events);
}

extern void INT_EVdfg_reconfig_insert(EVdfg dfg, int src_stone_index, EVdfg_stone new_stone, int dest_stone_index, EVevent_list q_events) {
    reconfig_link_port(dfg->stones[src_stone_index], 0, new_stone, q_events);
    reconfig_link_port(new_stone, 0, dfg->stones[dest_stone_index], NULL);
    CMtrace_out(dfg->cm, EVdfgVerbose, "Inside reconfig_insert, sin = %d, min = %d, din = %d : \n", dfg->stones[src_stone_index]->node, new_stone->node, dfg->stones[dest_stone_index]->node);
}

extern void INT_EVdfg_reconfig_insert_on_port(EVdfg dfg, EVdfg_stone src_stone, int port, EVdfg_stone new_stone, EVevent_list q_events)
{
    EVdfg_stone dest_stone = src_stone->out_links[port];
    (void)dfg;
    reconfig_link_port(src_stone, port, new_stone, q_events);
    /* link port on the new stone to the old destination */
    reconfig_link_port(new_stone, port, dest_stone, NULL);
    CMtrace_out(dfg->cm, EVdfgVerbose, "Inside reconfig_insert_on_port, sin = %d, min = %d, din = %d : \n", src_stone->node, new_stone->node, dest_stone->node);
}

extern void
INT_EVdfg_link_port(EVdfg_stone src, int port, EVdfg_stone dest)
{
    if (port < 0) return;
    if (src->out_count == 0) {
	src->out_links = malloc(sizeof(src->out_links[0]) * (port+1));
	memset(src->out_links, 0, sizeof(src->out_links[0]) * (port+1));
	src->out_count = port + 1;
    } else if (src->out_count < port + 1) {
	src->out_links = realloc(src->out_links,
				 sizeof(src->out_links[0]) * (port+1));
	memset(&src->out_links[src->out_count], 0, sizeof(src->out_links[0]) * (port+1-src->out_count));
	src->out_count = port + 1;
    }
    src->out_links[port] = dest;
}

extern void
INT_EVdfg_set_attr_list(EVdfg_stone stone, attr_list attrs)
{
    if (stone->attrs != NULL) {
	fprintf(stderr, "Warning, attributes for stone %p previously set, overwriting\n", stone);
    }
    add_ref_attr_list(attrs);
    stone->attrs = attrs;
}

FMField EVleaf_element_flds[] = {
    {"name", "string", sizeof(char*), FMOffset(leaf_element*, name)},
    {"FMtype", "string", sizeof(char*), FMOffset(leaf_element*, FMtype)},
    {NULL, NULL, 0, 0}
};

FMField EVregister_msg_flds[] = {
    {"node_name", "string", sizeof(char*), FMOffset(EVregister_ptr, node_name)},
    {"contact_string", "string", sizeof(char*), FMOffset(EVregister_ptr, contact_string)},
    {"source_count", "integer", sizeof(int), FMOffset(EVregister_ptr, source_count)},
    {"sink_count", "integer", sizeof(int), FMOffset(EVregister_ptr, sink_count)},
    {"sources", "source_element[source_count]", sizeof(leaf_element), FMOffset(EVregister_ptr, sources)},
    {"sinks", "sink_element[sink_count]", sizeof(leaf_element), FMOffset(EVregister_ptr, sinks)},
    {NULL, NULL, 0, 0}
};

FMStructDescRec EVdfg_register_format_list[] = {
    {"EVdfg_register", EVregister_msg_flds, sizeof(EVregister_msg), NULL},
    {"sink_element", EVleaf_element_flds, sizeof(leaf_element), NULL},
    {"source_element", EVleaf_element_flds, sizeof(leaf_element), NULL},
    {NULL, NULL, 0, NULL}
};

FMField EVready_msg_flds[] = {
    {"node_id", "integer", sizeof(int), FMOffset(EVready_ptr, node_id)},
    {NULL, NULL, 0, 0}
};

FMStructDescRec EVdfg_ready_format_list[] = {
    {"EVdfg_ready", EVready_msg_flds, sizeof(EVready_msg), NULL},
    {NULL, NULL, 0, NULL}
};

FMField EVstartup_ack_msg_flds[] = {
    {"node_id", "string", sizeof(char*), FMOffset(EVstartup_ack_ptr, node_id)},
    {NULL, NULL, 0, 0}
};

FMStructDescRec EVdfg_startup_ack_format_list[] = {
    {"EVdfg_startup_ack", EVstartup_ack_msg_flds, sizeof(EVstartup_ack_msg), NULL},
    {NULL, NULL, 0, NULL}
};

FMField EVshutdown_msg_flds[] = {
    {"value", "integer", sizeof(int), FMOffset(EVshutdown_ptr, value)},
    {NULL, NULL, 0, 0}
};

FMStructDescRec EVdfg_shutdown_format_list[] = {
    {"EVdfg_shutdown", EVshutdown_msg_flds, sizeof(EVshutdown_msg), NULL},
    {NULL, NULL, 0, NULL}
};

FMField EVconn_shutdown_msg_flds[] = {
    {"stone", "integer", sizeof(int), FMOffset(EVconn_shutdown_ptr, stone)},
    {NULL, NULL, 0, 0}
};

FMStructDescRec EVdfg_conn_shutdown_format_list[] = {
    {"EVdfg_conn_shutdown", EVconn_shutdown_msg_flds, sizeof(EVconn_shutdown_msg), NULL},
    {NULL, NULL, 0, NULL}
};

FMField EVdfg_stone_flds[] = {
    {"global_stone_id", "integer", sizeof(int), 
     FMOffset(deploy_msg_stone, global_stone_id)},
    {"attrs", "string", sizeof(char*), 
     FMOffset(deploy_msg_stone, attrs)},
    {"period_secs", "integer", sizeof(int),
     FMOffset(deploy_msg_stone, period_secs)},
    {"period_usecs", "integer", sizeof(int),
     FMOffset(deploy_msg_stone, period_usecs)},
    {"out_count", "integer", sizeof(int), 
     FMOffset(deploy_msg_stone, out_count)},
    {"out_links", "integer[out_count]", sizeof(int), 
     FMOffset(deploy_msg_stone, out_links)},
    {"action", "string", sizeof(char*), 
     FMOffset(deploy_msg_stone, action)},
    {"extra_actions", "integer", sizeof(int), 
     FMOffset(deploy_msg_stone, extra_actions)},
    {"xactions", "string[extra_actions]", sizeof(char*), 
     FMOffset(deploy_msg_stone, xactions)},
    {NULL, NULL, 0, 0}
};

FMField EVdfg_msg_flds[] = {
    {"canonical_name", "string", sizeof(char*),
     FMOffset(EVdfg_stones_ptr, canonical_name)},
    {"stone_count", "integer", sizeof(int),
     FMOffset(EVdfg_stones_ptr, stone_count)},
    {"stone_list", "EVdfg_deploy_stone[stone_count]", sizeof(struct _EVdfg_msg_stone), FMOffset(EVdfg_stones_ptr, stone_list)},
    {NULL, NULL, 0, 0}
};

FMStructDescRec EVdfg_deploy_format_list[] = {
    {"EVdfg_deploy", EVdfg_msg_flds, sizeof(EVdfg_stones_msg), NULL},
    {"EVdfg_deploy_stone", EVdfg_stone_flds, sizeof(struct _EVdfg_msg_stone), NULL},
    {NULL, NULL, 0, NULL}
};

static void check_all_nodes_registered(EVdfg dfg);
static void possibly_signal_shutdown(EVdfg dfg, int value, CMConnection conn);
static int new_shutdown_condition(EVdfg dfg, CMConnection conn);

typedef struct {
    int stone;
    int period_secs;
    int period_usecs;
} auto_stone_list;

static void
dfg_startup_ack_handler(CManager cm, CMConnection conn, void *vmsg, 
			void *client_data, attr_list attrs);

static void
enable_auto_stones(CManager cm, EVdfg dfg)
{
    int i = 0;
    auto_stone_list *auto_list;
    auto_list = (auto_stone_list *) INT_CMCondition_get_client_data(cm, dfg->ready_condition);
    CMtrace_out(cm, EVdfgVerbose, "ENABLING AUTO STONES, list is %p\n", auto_list);
    while (auto_list && auto_list[i].period_secs != -1) {
        /* everyone is ready, enable auto stones */
	INT_EVenable_auto_stone(cm, auto_list[i].stone, auto_list[i].period_secs, auto_list[i].period_usecs);
	i++;
    }
    if (auto_list) free(auto_list);
}

static void
dfg_ready_handler(CManager cm, CMConnection conn, void *vmsg, 
		  void *client_data, attr_list attrs)
{
    EVdfg dfg = client_data;
    EVready_ptr msg =  vmsg;
    (void) conn;
    (void) attrs;
    dfg->my_node_id = msg->node_id;
    CManager_lock(cm);
    enable_auto_stones(cm, dfg);
    CMtrace_out(cm, EVdfgVerbose, "Client DFG %p Node id %d is ready, signalling %d\n", dfg, dfg->my_node_id, dfg->ready_condition);
    INT_CMCondition_signal(cm, dfg->ready_condition);
    CManager_unlock(cm);
}

static void 
handle_conn_shutdown(EVdfg dfg, int stone)
{
    if (dfg->node_fail_handler != NULL) {
	int i;
	int target_stone = -1;
	char *failed_node = NULL;
	char *contact_str = NULL;
	CMtrace_out(cm, EVdfgVerbose, "IN CONN_SHUTDOWN_HANDLER\n");
	for (i=0; i< dfg->stone_count; i++) {
	    int j;
	    for (j = 0; j < dfg->stones[i]->out_count; j++) {
		if (dfg->stones[i]->out_links[j]->stone_id == stone) {
		    EVdfg_stone out_stone = dfg->stones[i]->out_links[j];
		    CMtrace_out(cm, EVdfgVerbose, "Found reporting stone as output %d of stone %d\n",
				j, i);
		    parse_bridge_action_spec(out_stone->action, 
					     &target_stone, &contact_str);
		    CMtrace_out(cm, EVdfgVerbose, "Dead stone is %d\n", target_stone);
		}
	    }
	}
	for (i=0; i< dfg->stone_count; i++) {
	    if (dfg->stones[i]->stone_id == target_stone) {
		int node = dfg->stones[i]->node;
		CMtrace_out(cm, EVdfgVerbose, "Dead node is %d, name %s\n", node,
			    dfg->nodes[node].canonical_name);
		failed_node = dfg->nodes[node].canonical_name;
		dfg->nodes[node].shutdown_status_contribution = STATUS_FAILED;
	    }
	}
	CManager_unlock(dfg->cm);
	dfg->node_fail_handler(dfg, failed_node, target_stone);
	CManager_lock(dfg->cm);
	dfg->reconfig = 1;
	dfg->sig_reconfig_bool = 1;
	check_all_nodes_registered(dfg);
    }
}

static void
dfg_conn_shutdown_handler(CManager cm, CMConnection conn, void *vmsg, 
			  void *client_data, attr_list attrs)
{
    EVdfg dfg = client_data;
    EVconn_shutdown_msg *msg = (EVconn_shutdown_msg *)vmsg;
    
    (void)cm;
    (void)conn;
    (void)attrs;
    CMtrace_out(cm, EVdfgVerbose, "The master knows about a stone %d failure\n", msg->stone);
    CManager_lock(cm);
    switch (dfg->state) {
    case DFG_Shutting_Down:
	/* ignore */
	break;
    case DFG_Reconfiguring:
    case DFG_Starting:
	queue_conn_shutdown_message(dfg, msg->stone);
	break;
    case DFG_Joining:
	printf("This shouldn't happen , connection shutdown during joining phase \n");
	exit(0);
	break;
    case DFG_Running:
	dfg->state = DFG_Reconfiguring;
	CMtrace_out(cm, EVdfgVerbose, "EVDFG conn_shutdown_handler -  master DFG state is now %s\n", str_state[dfg->state]);
	handle_conn_shutdown(dfg, msg->stone);
	break;
    }
    handle_queued_messages(dfg);
    CMtrace_out(cm, EVdfgVerbose, "EVDFG exit conn shutdown master DFG state is %s\n", str_state[dfg->state]);
    CManager_unlock(cm);
}

static void
dfg_shutdown_handler(CManager cm, CMConnection conn, void *vmsg, 
		  void *client_data, attr_list attrs)
{
    EVdfg dfg = client_data;
    EVshutdown_ptr msg =  vmsg;
    (void)cm;
    (void)conn;
    (void)attrs;
    CManager_lock(cm);
    if (dfg->master_connection == NULL) {
	/* I got a shutdown message and I'm the master */
	possibly_signal_shutdown(dfg, msg->value, conn);
    } else {
	/* I'm the client, all is done */
	int i = 0;
	dfg->shutdown_value = msg->value;
	dfg->already_shutdown = 1;
	CMtrace_out(cm, EVdfgVerbose, "Client %d has confirmed shutdown\n", dfg->my_node_id);
	while (dfg->shutdown_conditions && (dfg->shutdown_conditions[i] != -1)){
	    CMtrace_out(cm, EVdfgVerbose, "Client %d shutdown signalling %d\n", dfg->my_node_id, dfg->shutdown_conditions[i]);
	    INT_CMCondition_signal(dfg->cm, dfg->shutdown_conditions[i++]);
	}
    }
    CMtrace_out(cm, EVdfgVerbose, "EVDFG exit shutdown master DFG state is %s\n", str_state[dfg->state]);
    CManager_unlock(cm);
}

static void
dfg_stone_close_handler(CManager cm, CMConnection conn, int stone, 
		  void *client_data)
{
    EVdfg dfg = (EVdfg)client_data;
    event_path_data evp = cm->evp;
    int global_stone_id = -1;
    (void)cm;
    (void)conn;
    CManager_lock(cm);
    /* first, freeze the stone so that we don't lose any more data */
    INT_EVfreeze_stone(cm, stone);

    int i;
    for (i=0; i < evp->stone_lookup_table_size; i++ ) {
	if (stone == evp->stone_lookup_table[i].local_id) {
	    global_stone_id = evp->stone_lookup_table[i].global_id;
	}
    }
    if (global_stone_id == -1) {
	CMtrace_out(cm, EVdfgVerbose, "Bad mojo, failed to find global stone id after stone close of stone %d\n", stone);
	CMtrace_out(cm, EVdfgVerbose, "  If the above message occurs during shutdown, this is likely not a concern\n");
	CManager_unlock(cm);
	return;
    }
    if (dfg->master_connection != NULL) {
	CMFormat conn_shutdown_msg = INT_CMlookup_format(dfg->cm, EVdfg_conn_shutdown_format_list);
	EVconn_shutdown_msg msg;
	msg.stone = global_stone_id;
	INT_CMwrite(dfg->master_connection, conn_shutdown_msg, &msg);
	CManager_unlock(dfg->cm);
    } else {
	EVconn_shutdown_msg msg;
	msg.stone = global_stone_id;
	/* the master is detecting a close situation */
	CManager_unlock(dfg->cm);
	dfg_conn_shutdown_handler(dfg->cm, NULL, &msg, dfg, NULL);
    }
}

extern void
INT_EVdfg_assign_canonical_name(EVdfg dfg, char *given_name, char *canonical_name)
{
    int node;
    for (node = 0; node < dfg->node_count; node++) {
	if (dfg->nodes[node].name == given_name) {
	    if (dfg->realized == 1)
		CMtrace_out(dfg->cm, EVdfgVerbose, "Reconfigure canonical name assignment, node = %d\n", node);
	    dfg->nodes[node].canonical_name = strdup(canonical_name);
	}
    }
}

static void
handle_node_join(EVdfg dfg, char *node_name, char *contact_string, CMConnection conn)
{
    int node;
    int new_node = -1;

    assert(CManager_locked(dfg->cm));

    if (dfg->node_join_handler == NULL) {
	/* static node list */
	for (node = 0; node < dfg->node_count; node++) {
	    if (strcmp(dfg->nodes[node].name, node_name) == 0) {
		if (conn == NULL) {
		    /* we are the master joining as a client node */
		    dfg->nodes[node].self = 1;
		    dfg->my_node_id = node;
		} else {
		    dfg->nodes[node].conn = conn;
		    dfg->nodes[node].str_contact_list = strdup(contact_string);
		    dfg->nodes[node].contact_list = attr_list_from_string(dfg->nodes[node].str_contact_list);
		    dfg->nodes[node].shutdown_status_contribution = STATUS_UNDETERMINED;
		}
		new_node = node;
		break;
	    }
	}
	if (new_node == -1) {
	    printf("Registering node \"%s\" not found in node list\n", 
		   node_name);
	    return;
	}
    } else {
	int n;
	
	if (dfg->realized == 1 && dfg->reconfig == 0) {
	    dfg->reconfig = 1;
	    dfg->sig_reconfig_bool = 1;
	    dfg->old_node_count = dfg->node_count;
	    CMtrace_out(dfg->cm, EVdfgVerbose, "Reconfigure, contact_string = %s\n", contact_string);
	    CMtrace_out(dfg->cm, EVdfgVerbose, "node_count = %d, stone_count = %d\n", dfg->node_count, dfg->stone_count);
	}
	n = dfg->node_count++;
	dfg->nodes = realloc(dfg->nodes, (sizeof(dfg->nodes[0])*dfg->node_count));
	memset(&dfg->nodes[n], 0, sizeof(dfg->nodes[0]));
	dfg->nodes[n].name = strdup(node_name);
	dfg->nodes[n].canonical_name = NULL;
	dfg->nodes[n].shutdown_status_contribution = STATUS_UNDETERMINED;
	new_node = n;
	if (conn == NULL) {
	    dfg->nodes[n].self = 1;
	    dfg->my_node_id = n;
	} else {
	    dfg->nodes[n].self = 0;
	    dfg->nodes[n].conn = conn;
	    dfg->nodes[n].str_contact_list = strdup(contact_string);
	    dfg->nodes[n].contact_list = attr_list_from_string(dfg->nodes[n].str_contact_list);
	}
    }
    CMtrace_out(cm, EVdfgVerbose, "Client \"%s\" has joined DFG, contact %s\n", node_name, dfg->nodes[new_node].str_contact_list);
    check_all_nodes_registered(dfg);
}

static void
node_register_handler(CManager cm, CMConnection conn, void *vmsg, 
		      void *client_data, attr_list attrs)
{
    /* a client join message came in from the network */
    EVdfg dfg = client_data;
    EVregister_ptr msg =  vmsg;
    (void)cm;
    (void)attrs;
    CManager_lock(cm);
    switch (dfg->state) {
    case DFG_Shutting_Down:
	/* ignore */
	break;
    case DFG_Reconfiguring:
    case DFG_Starting:
	queue_node_join_message(dfg, msg->node_name, msg->contact_string, conn);
	break;
    case DFG_Running:
	dfg->state = DFG_Reconfiguring;
	CMtrace_out(cm, EVdfgVerbose, "EVDFG node join -  master DFG state is now %s\n", str_state[dfg->state]);
	/* fall thorough */
    case DFG_Joining:
	handle_node_join(dfg, msg->node_name, msg->contact_string, conn);
	break;
    }
    handle_queued_messages(dfg);
    CMtrace_out(cm, EVdfgVerbose, "EVDFG exit join handler -  master DFG state is %s\n", str_state[dfg->state]);
    CManager_unlock(cm);
}

static void
dfg_deploy_handler(CManager cm, CMConnection conn, void *vmsg, 
		  void *client_data, attr_list attrs)
{
    EVdfg dfg = (EVdfg) client_data;
    event_path_data evp = cm->evp;
    (void) dfg;
    (void) conn;
    (void) attrs;
    EVdfg_stones_ptr msg =  vmsg;
    int i, base = evp->stone_lookup_table_size;
    int auto_stones = 0;
    auto_stone_list *auto_list = malloc(sizeof(auto_stone_list));

    CMtrace_out(cm, EVdfgVerbose, "Client %d getting Deploy message\n", dfg->my_node_id);

    CManager_lock(cm);
    /* add stones to local lookup table */
    if (evp->stone_lookup_table_size == 0) {
	evp->stone_lookup_table = 
	    malloc(sizeof(evp->stone_lookup_table[0]) * msg->stone_count);
    } else {
	evp->stone_lookup_table = 
	    realloc(evp->stone_lookup_table,
		    sizeof(evp->stone_lookup_table[0]) * (msg->stone_count+base));
    }
    for (i=0; i < msg->stone_count; i++) {
	evp->stone_lookup_table[base + i].global_id = msg->stone_list[i].global_stone_id;
	evp->stone_lookup_table[base + i].local_id = INT_EValloc_stone(cm);
    }
    evp->stone_lookup_table_size = base + i;
    for (i=0; i < msg->stone_count; i++) {
	int local_stone = evp->stone_lookup_table[base + i].local_id;
	int local_list[1024]; /* better be enough */
	int j;
	if (msg->stone_list[i].attrs != NULL) {
	    attr_list tmp_attrs = attr_list_from_string(msg->stone_list[i].attrs);
	    INT_EVset_attr_list(cm, local_stone, tmp_attrs);
	    free_attr_list(tmp_attrs);
	}
	for (j=0; j < msg->stone_list[i].out_count; j++) {
	    if (msg->stone_list[i].out_links[j] != -1) {
		local_list[j] = lookup_local_stone(evp, msg->stone_list[i].out_links[j]);
		if (local_list[j] == -1) {
		    printf("Didn't found global stone %d\n", msg->stone_list[i].out_links[j]);
		}
	    } else {
		local_list[j] = -1;
	    }
	}
	local_list[msg->stone_list[i].out_count] = -1;
	INT_EVassoc_general_action(cm, local_stone, msg->stone_list[i].action, 
				   &local_list[0]);
	for (j=0; j < msg->stone_list[i].extra_actions; j++) {
	    INT_EVassoc_general_action(cm, local_stone, msg->stone_list[i].xactions[j], 
				       &local_list[0]);
	}	    
	if (msg->stone_list[i].period_secs != -1) {
	    auto_list= realloc(auto_list, sizeof(auto_list[0]) * (auto_stones+2));
	    auto_list[auto_stones].stone = local_stone;
	    auto_list[auto_stones].period_secs = msg->stone_list[i].period_secs;
	    auto_list[auto_stones].period_usecs = msg->stone_list[i].period_usecs;
	    auto_stones++;
	}
	if (action_type(msg->stone_list[i].action) == Action_Terminal) {
	    dfg->active_sink_count++;
	}
    }    
    auto_list[auto_stones].period_secs = -1;
    if (conn != NULL) {
	CMFormat startup_ack_msg = INT_CMlookup_format(dfg->cm, EVdfg_startup_ack_format_list);
	EVstartup_ack_msg response_msg;
	response_msg.node_id = msg->canonical_name;
	INT_CMwrite(dfg->master_connection, startup_ack_msg, &response_msg);
	CMtrace_out(cm, EVdfgVerbose, "Client %d wrote startup ack\n", dfg->my_node_id);
    } else {
      	CMtrace_out(cm, EVdfgVerbose, "Client %d no master conn\n", dfg->my_node_id);
    }
    INT_CMCondition_set_client_data(cm, dfg->ready_condition, (void*)auto_list);
    
    CManager_unlock(cm);
}

static void
free_dfg(CManager cm, void *vdfg)
{
    EVdfg dfg = vdfg;
    int i;
    for (i=0; i < dfg->node_count; i++) {
	free(dfg->nodes[i].name);
	free(dfg->nodes[i].canonical_name);
	if (dfg->nodes[i].str_contact_list) free(dfg->nodes[i].str_contact_list);
	if (dfg->nodes[i].contact_list) free_attr_list(dfg->nodes[i].contact_list);
    }
    free(dfg->nodes);
    for (i=0; i < dfg->stone_count; i++) {
	if (dfg->stones[i]->out_links) free(dfg->stones[i]->out_links);
	if (dfg->stones[i]->action) free(dfg->stones[i]->action);
	if (dfg->stones[i]->extra_actions) {
	    int j;
	    for (j=0; j < dfg->stones[i]->action_count-1; j++) {
		free(dfg->stones[i]->extra_actions[j]);
	    }
	    free(dfg->stones[i]->extra_actions);
	}
	free(dfg->stones[i]);
    }
    if (dfg->master_contact_str) free(dfg->master_contact_str);
    if (dfg->shutdown_conditions) free(dfg->shutdown_conditions);
    free(dfg->stones);
    free(dfg);
}

extern EVdfg
INT_EVdfg_create(CManager cm)
{
    EVdfg dfg = malloc(sizeof(struct _EVdfg));
    attr_list contact_list;

    memset(dfg, 0, sizeof(struct _EVdfg));
    dfg->cm = cm;
    dfg->reconfig = 0;
    dfg->deployed_stone_count = 0;
    dfg->sig_reconfig_bool = 0;
    dfg->old_node_count = 1;
    dfg->transfer_events_count = 0;
    dfg->delete_count = 0;
    dfg->transfer_events_list = NULL;
    dfg->delete_list = NULL;
    dfg->no_deployment = 0;
    dfg->state = DFG_Joining;
    CMtrace_out(cm, EVdfgVerbose, "EVDFG initialization -  master DFG state set to %s\n", str_state[dfg->state]);
    contact_list = INT_CMget_contact_list(cm);
    dfg->master_contact_str = attr_list_to_string(contact_list);
    dfg->startup_ack_condition = -1;
    free_attr_list(contact_list);
    INT_EVregister_close_handler(cm, dfg_stone_close_handler, (void*)dfg);
    INT_CMregister_handler(INT_CMregister_format(cm, EVdfg_register_format_list),
			   node_register_handler, dfg);
    INT_CMregister_handler(INT_CMregister_format(cm, EVdfg_ready_format_list),
			   dfg_ready_handler, dfg);
    INT_CMregister_handler(INT_CMregister_format(cm, EVdfg_startup_ack_format_list),
			   dfg_startup_ack_handler, dfg);
    INT_CMregister_handler(INT_CMregister_format(cm, EVdfg_deploy_format_list),
			   dfg_deploy_handler, dfg);
    INT_CMregister_handler(INT_CMregister_format(cm, EVdfg_shutdown_format_list),
			   dfg_shutdown_handler, dfg);
    INT_CMregister_handler(INT_CMregister_format(cm, EVdfg_conn_shutdown_format_list),
			   dfg_conn_shutdown_handler, dfg);
    INT_CMadd_shutdown_task(cm, free_dfg, dfg, FREE_TASK);
    return dfg;
}

extern char *INT_EVdfg_get_contact_list(EVdfg dfg)
{
    attr_list listen_list, contact_list = NULL;
    atom_t CM_TRANSPORT = attr_atom_from_string("CM_TRANSPORT");
    CManager cm = dfg->cm;

    /* use enet transport if available */
    listen_list = create_attr_list();
    add_string_attr(listen_list, CM_TRANSPORT, strdup("enet"));
    contact_list = INT_CMget_specific_contact_list(cm, listen_list);

    free_attr_list(listen_list);
    if (contact_list == NULL) {
	contact_list = INT_CMget_contact_list(cm);
	if (contact_list == NULL) {
	    CMlisten(cm);
	    contact_list = INT_CMget_contact_list(cm);
	}
    }
    dfg->master_command_contact_str = attr_list_to_string(contact_list);
    free_attr_list(contact_list);
    return dfg->master_command_contact_str;
}

static void
check_connectivity(EVdfg dfg)
{
    int i;
    for (i=0; i< dfg->stone_count; i++) {
	CMtrace_out(dfg->cm, EVdfgVerbose, "Stone %d - assigned to node %s, action %s\n", i, dfg->nodes[dfg->stones[i]->node].canonical_name, (dfg->stones[i]->action ? dfg->stones[i]->action : "NULL"));
	if (dfg->stones[i]->node == -1) {
	    printf("Warning, stone %d has not been assigned to any node.  This stone will not be deployed.\n", i);
	    printf("    This stones particulars are:\n");
	    fdump_dfg_stone(stdout, dfg->stones[i]);
	}
	if (dfg->stones[i]->action_count == 0) {
	    printf("Warning, stone %d (assigned to node %s) has no actions registered", i, dfg->nodes[dfg->stones[i]->node].canonical_name);
	    continue;
	}
	if ((dfg->stones[i]->out_count == 0) && (dfg->stones[i]->new_out_count == 0)) {
	    char *action_spec = dfg->stones[i]->action;
	    switch(action_type(action_spec)) {
	    case Action_Terminal:
	    case Action_Bridge:
		break;
	    default:
		printf("Warning, stone %d (assigned to node %s) has no outputs connected to other stones\n", i, dfg->nodes[dfg->stones[i]->node].canonical_name);
		printf("    This stones particulars are:\n");
		fdump_dfg_stone(stdout, dfg->stones[i]);
		break;
	    }
	}
    }
}

extern int
INT_EVdfg_realize(EVdfg dfg)
{
    check_connectivity(dfg);
//    check_types(dfg);

    if (dfg->realized == 1) {
	dfg->reconfig = 0;
    }
    dfg->realized = 1;
    return 1;
}

extern void
INT_EVdfg_register_node_list(EVdfg dfg, char **nodes)
{
    int count = 0, i = 0;
    while(nodes[count] != NULL) count++;
    dfg->node_count = count;
    dfg->nodes = malloc(sizeof(dfg->nodes[0]) * count);
    memset(dfg->nodes, 0, sizeof(dfg->nodes[0]) * count);
    for (i = 0; i < dfg->node_count; i++) {
	dfg->nodes[i].name = strdup(nodes[i]);
	dfg->nodes[i].canonical_name = strdup(nodes[i]);
	dfg->nodes[i].shutdown_status_contribution = STATUS_UNDETERMINED;
    }
}

extern void
INT_EVdfg_assign_node(EVdfg_stone stone, char *node_name)
{
    EVdfg dfg = stone->dfg;
    int i, node = -1;
    for (i = 0; i < dfg->node_count; i++) {
	EVint_node_list n = &dfg->nodes[i];
	if (n->canonical_name && (strcmp(n->canonical_name, node_name) == 0)) {
	    node = i;
	} else 	if (n->name && (strcmp(n->name, node_name) == 0)) {
	    node = i;
	}

    }
    if (node == -1) {
	printf("Node \"%s\" not found in node list\n", node_name);
    }
	
    if (dfg->realized == 1) {
	CMtrace_out(dfg->cm, EVdfgVerbose, "assign node, node# = %d\n", node);
    }
    stone->node = node;
}

extern int 
INT_EVdfg_ready_wait(EVdfg dfg)
{
    CMtrace_out(cm, EVdfgVerbose, "DFG %p wait for ready\n", dfg);
    INT_CMCondition_wait(dfg->cm, dfg->ready_condition);
    CMtrace_out(cm, EVdfgVerbose, "DFG %p ready wait released\n", dfg);
    return 1;
}

extern int
INT_EVdfg_shutdown(EVdfg dfg, int result)
{
    if (dfg->already_shutdown) printf("Node %d, already shut down BAD!\n", dfg->my_node_id);
    if (dfg->master_connection != NULL) {
	/* we are a client, tell the master to shutdown */
	CMFormat shutdown_msg = INT_CMlookup_format(dfg->cm, EVdfg_shutdown_format_list);
	EVshutdown_msg msg;
	msg.value = result;
	INT_CMwrite(dfg->master_connection, shutdown_msg, &msg);
	/* and wait until we hear back */
    } else {
	possibly_signal_shutdown(dfg, result, NULL);
    }
    if (!dfg->already_shutdown) {
	CManager_unlock(dfg->cm);
	CMCondition_wait(dfg->cm, new_shutdown_condition(dfg, dfg->master_connection));
	CManager_lock(dfg->cm);
    }
    return dfg->shutdown_value;
}

extern int
INT_EVdfg_active_sink_count(EVdfg dfg)
{
    return dfg->active_sink_count;
}

extern void
INT_EVdfg_ready_for_shutdown(EVdfg dfg)
{
    if (dfg->already_shutdown) return;
    if (dfg->master_connection != NULL) {
	/* we are a client, tell the master to shutdown */
	CMFormat shutdown_msg = INT_CMlookup_format(dfg->cm, EVdfg_shutdown_format_list);
	EVshutdown_msg msg;
	msg.value = STATUS_NO_CONTRIBUTION;   /* no status contribution */
	INT_CMwrite(dfg->master_connection, shutdown_msg, &msg);
    } else {
	possibly_signal_shutdown(dfg, STATUS_NO_CONTRIBUTION, NULL);
    }
}

extern int 
INT_EVdfg_wait_for_shutdown(EVdfg dfg)
{
    if (dfg->already_shutdown) return dfg->shutdown_value;
    INT_CMCondition_wait(dfg->cm, new_shutdown_condition(dfg, dfg->master_connection));
    return dfg->shutdown_value;
}

extern int INT_EVdfg_source_active(EVsource src)
{
    return (src->local_stone_id != -1);
}

extern void
INT_EVdfg_register_source(char *name, EVsource src)
{
    CManager cm = src->cm;
    event_path_data evp = cm->evp;
    if (evp->source_count == 0) {
	evp->sources = malloc(sizeof(evp->sources[0]));
    } else {
	evp->sources = realloc(evp->sources,
			       sizeof(evp->sources[0]) * (evp->source_count + 1));
    }
    evp->sources[evp->source_count].name = name;
    evp->sources[evp->source_count].src = src;
    evp->source_count++;
}

extern void
INT_EVdfg_register_sink_handler(CManager cm, char *name, FMStructDescList list, EVSimpleHandlerFunc handler)
{
    event_path_data evp = cm->evp;
    if (evp->sink_handler_count == 0) {
	evp->sink_handlers = malloc(sizeof(evp->sink_handlers[0]));
    } else {
	evp->sink_handlers = realloc(evp->sink_handlers,
				     sizeof(evp->sink_handlers[0]) * (evp->sink_handler_count + 1));
    }
    evp->sink_handlers[evp->sink_handler_count].name = name;
    evp->sink_handlers[evp->sink_handler_count].format_list = list;
    evp->sink_handlers[evp->sink_handler_count].handler = handler;
    evp->sink_handler_count++;
}

extern void
INT_EVdfg_register_raw_sink_handler(CManager cm, char *name, EVRawHandlerFunc handler)
{
    event_path_data evp = cm->evp;
    if (evp->sink_handler_count == 0) {
	evp->sink_handlers = malloc(sizeof(evp->sink_handlers[0]));
    } else {
	evp->sink_handlers = realloc(evp->sink_handlers,
				     sizeof(evp->sink_handlers[0]) * (evp->sink_handler_count + 1));
    }
    evp->sink_handlers[evp->sink_handler_count].name = name;
    evp->sink_handlers[evp->sink_handler_count].format_list = NULL;
    evp->sink_handlers[evp->sink_handler_count].handler = (EVSimpleHandlerFunc)handler;
    evp->sink_handler_count++;
}

static int
new_shutdown_condition(EVdfg dfg, CMConnection conn)
{
    int cur_count = 0;
    if (dfg->shutdown_conditions == NULL) {
	dfg->shutdown_conditions = malloc(2*sizeof(dfg->shutdown_conditions[0]));
    } else {
	while (dfg->shutdown_conditions[cur_count++] != -1) ; 
	cur_count--;
	dfg->shutdown_conditions = realloc(dfg->shutdown_conditions, 
					   (cur_count+2)*sizeof(dfg->shutdown_conditions[0]));
    }
    dfg->shutdown_conditions[cur_count] = INT_CMCondition_get(dfg->cm, conn);
    dfg->shutdown_conditions[cur_count+1] = -1;
    return dfg->shutdown_conditions[cur_count];
}

extern void
INT_EVdfg_join_dfg(EVdfg dfg, char* node_name, char *master_contact)
{
    CManager cm = dfg->cm;
    event_path_data evp = cm->evp;
    attr_list master_attrs = attr_list_from_string(master_contact);
    if (dfg->master_contact_str) free(dfg->master_contact_str);
    dfg->master_contact_str = strdup(master_contact);
    dfg->ready_condition = INT_CMCondition_get(cm, NULL);
    INT_CMCondition_set_client_data(cm, dfg->ready_condition, NULL);
    if (INT_CMcontact_self_check(cm, master_attrs) == 1) {
	/* we are the master */
	handle_node_join(dfg, node_name, master_contact, NULL);
    } else {
	CMConnection conn = INT_CMget_conn(cm, master_attrs);
	CMFormat register_msg = INT_CMlookup_format(cm, EVdfg_register_format_list);
	EVregister_msg msg;
	attr_list contact_list = INT_CMget_contact_list(cm);
	char *my_contact_str;
	int i;
	if (conn == NULL) {
	    fprintf(stderr, "failed to contact Master at %s\n", attr_list_to_string(master_attrs));
	    fprintf(stderr, "Join DFG failed\n");
	    return;
	}
	if (contact_list == NULL) {
	    INT_CMlisten(cm);
	    contact_list = INT_CMget_contact_list(cm);
	}

	my_contact_str = attr_list_to_string(contact_list);
	free_attr_list(contact_list);

	msg.node_name = node_name;
	msg.contact_string = my_contact_str;
	msg.source_count = evp->source_count;
	msg.sources = malloc(msg.source_count * sizeof(msg.sources[0]));
	for (i=0; i < evp->source_count; i++) {
	    msg.sources[i].name = evp->sources[i].name;
	    msg.sources[i].FMtype = NULL;
	}
	msg.sink_count = evp->sink_handler_count;
	msg.sinks = malloc(msg.sink_count * sizeof(msg.sinks[0]));
	for (i=0; i < evp->sink_handler_count; i++) {
	    msg.sinks[i].name = evp->sink_handlers[i].name;
	    msg.sinks[i].FMtype = NULL;
	}
	
	INT_CMwrite(conn, register_msg, &msg);
	free(my_contact_str);
	dfg->master_connection = conn;
	CMtrace_out(cm, EVdfgVerbose, "DFG %p node name %s\n", dfg, node_name);
    }
    free_attr_list(master_attrs);
}

static EVdfg_stone
create_bridge_stone(EVdfg dfg, EVdfg_stone target)
{
    EVdfg_stone stone = NULL;
    char *contact = dfg->nodes[target->node].str_contact_list;
    char *action;
    if (dfg->nodes[target->node].self) {
	contact = dfg->master_contact_str;
    }
    action = INT_create_bridge_action_spec(target->stone_id, contact);
    stone = INT_EVdfg_create_stone(dfg, action);
    stone->bridge_stone = 1;
    stone->bridge_target = target;
	
    return stone;
}

static void
add_bridge_stones(EVdfg dfg)
{
    int i;
    for (i=0; i< dfg->stone_count; i++) {
	int j;
	for (j = 0; j < dfg->stones[i]->out_count; j++) {
	    EVdfg_stone cur = dfg->stones[i];
	    EVdfg_stone target = cur->out_links[j];
	    if (target && (!cur->bridge_stone) && (cur->node != target->node)) {
		cur->out_links[j] = create_bridge_stone(dfg, target);
		/* put the bridge stone where the source stone is */
		cur->out_links[j]->node = cur->node;
		CMtrace_out(dfg->cm, EVdfgVerbose, "Created bridge stone %p, target node is %d, assigned to node %d\n", cur->out_links[j], target->node, cur->node);
	    }
	}
    }
}

static void
assign_stone_ids(EVdfg dfg)
{
    int i;
    for (i=0; i< dfg->stone_count; i++) {
	dfg->stones[i]->stone_id = 0x80000000 | i;
    }
}

static void
deploy_to_node(EVdfg dfg, int node)
{
    int i, j;
    int stone_count = 0;
    EVdfg_stones_msg msg;
    CMFormat deploy_msg = INT_CMlookup_format(dfg->cm, EVdfg_deploy_format_list);

    for (i=0; i< dfg->stone_count; i++) {
	if (dfg->stones[i]->node == node) {
	    stone_count++;
	}
    }
    if (stone_count == 0) {
        dfg->deploy_ack_count++;
      	return;
    }
    memset(&msg, 0, sizeof(msg));
    msg.canonical_name = dfg->nodes[node].canonical_name;
    msg.stone_count = stone_count;
    msg.stone_list = malloc(stone_count * sizeof(msg.stone_list[0]));
    memset(msg.stone_list, 0, stone_count * sizeof(msg.stone_list[0]));
    j = 0;
    for (i=0; i< dfg->stone_count; i++) {
	if (dfg->stones[i]->node == node) {
	    deploy_msg_stone mstone = &msg.stone_list[j];
	    EVdfg_stone dstone = dfg->stones[i];
	    int k;
	    mstone->global_stone_id = dstone->stone_id;
	    mstone->attrs = NULL;
	    if (dstone->attrs != NULL) {
		mstone->attrs = attr_list_to_string(dstone->attrs);
	    }
	    mstone->period_secs = dstone->period_secs;
	    mstone->period_usecs = dstone->period_usecs;
	    mstone->out_count = dstone->out_count;
	    mstone->out_links = malloc(sizeof(mstone->out_links[0])*mstone->out_count);
	    for (k=0; k< dstone->out_count; k++) {
		if (dstone->out_links[k] != NULL) {
		    mstone->out_links[k] = dstone->out_links[k]->stone_id;
		} else {
		    mstone->out_links[k] = -1;
		}
	    }
	    mstone->action = dstone->action;
	    if (dstone->action_count > 1) {
		mstone->extra_actions = dstone->action_count - 1;
		mstone->xactions = malloc(sizeof(mstone->xactions[0])*mstone->extra_actions);
		for (k=0; k < mstone->extra_actions; k++) {
		    mstone->xactions[k] = dstone->extra_actions[k];
		}
	    } else {
		mstone->extra_actions = 0;
		mstone->xactions = NULL;
	    }
	    j++;
	}
    }
    if (dfg->nodes[node].conn) {
	INT_CMwrite(dfg->nodes[node].conn, deploy_msg, &msg);
    } else {
	CManager_unlock(dfg->cm);
	dfg_deploy_handler(dfg->cm, NULL, &msg, dfg, NULL);
	CManager_lock(dfg->cm);
    }
    for(i=0 ; i < msg.stone_count; i++) {
	free(msg.stone_list[i].out_links);
	if (msg.stone_list[i].attrs) free(msg.stone_list[i].attrs);
	if (msg.stone_list[i].xactions) free(msg.stone_list[i].xactions);
    }
    free(msg.stone_list);
}


void reconfig_delete_link(EVdfg dfg, int src_index, int dest_index) {
    int i;
	
    EVdfg_stone src = dfg->stones[src_index];
    EVdfg_stone dest = dfg->stones[dest_index];
    EVdfg_stone temp_stone = NULL;
	
    for (i = 0; i < src->out_count; ++i) {
	if (src->bridge_stone == 0) {
	    if (src->out_links[i]->bridge_stone) {
		temp_stone = src->out_links[i];
		if (temp_stone->bridge_target == dest) {
		    if (src->node == 0) {
			//EVfreeze_stone(dfg->cm, temp_stone->stone_id);
			//transfer_events = EVextract_stone_events(dfg->cm, temp_stone->stone_id);
			if (src->frozen == 0) {
			    EVfreeze_stone(dfg->cm, src->stone_id);
			}
			EVstone_remove_split_target(dfg->cm, src->stone_id, temp_stone->stone_id);
			//	    EVfreeze_stone(dfg->cm, temp_stone->stone_id);
			//	    transfer_events = EVextract_stone_events(dfg->cm, temp_stone->stone_id);
			EVfree_stone(dfg->cm, temp_stone->stone_id);
		    } else {
			if (src->frozen == 0) {
			    REVfreeze_stone(dfg->nodes[src->node].conn, src->stone_id);
			}
			REVstone_remove_split_target(dfg->nodes[src->node].conn, src->stone_id, temp_stone->stone_id);
			//	    REVfreeze_stone(dfg->nodes[temp_stone->node].conn, temp_stone->stone_id);
			
			CMtrace_out(dfg->cm, EVdfgVerbose, "deleting remotely.. sounds good till here.. src->node = %d, src_index = %d\n", src->node, src_index);
			fflush(stdout);
			
			//	    transfer_events = REVextract_stone_events(dfg->nodes[temp_stone->node].conn, temp_stone->stone_id);
			
			CMtrace_out(dfg->cm, EVdfgVerbose, "exracted events in delete..\n");
			fflush(stdout);
			
			REVfree_stone(dfg->nodes[src->node].conn, temp_stone->stone_id);
			//free(temp-stone);
		    }
		    //temp_stone = NULL;
		    src->out_links[i]->invalid = 1;
		    src->frozen = 1;
		    break;
		}
	    } else {
		if(src->out_links[i] == dest && src->out_links[i]->invalid == 0) {
		    if (src->node == 0) {
			if (src->frozen == 0) {
			    EVfreeze_stone(dfg->cm, src->stone_id);
			}
			EVstone_remove_split_target(dfg->cm, src->stone_id, dest->stone_id);
		    } else {
			if (src->frozen == 0) {
			    REVfreeze_stone(dfg->nodes[src->node].conn, src->stone_id);
			}
			REVstone_remove_split_target(dfg->nodes[src->node].conn, src->stone_id, dest->stone_id);
		    }
		    src->out_links[i]->invalid = 1;
		    src->frozen = 1;
		    break;
		}
	    }
	}
    }
   
}


static void reconfig_deploy(EVdfg dfg) 
{
    int i;
    int j;
    EVstone new_bridge_stone_id = -1;
    EVdfg_stone temp_stone = NULL;
    EVdfg_stone cur = NULL;
    EVdfg_stone target = NULL;
	
	
    for (i = dfg->old_node_count; i < dfg->node_count; ++i) {
	deploy_to_node(dfg, i);
    }
	
    CManager_unlock(dfg->cm);
    for (i = 0; i < dfg->stone_count; ++i) {
	if (dfg->stones[i]->new_out_count > 0) {
	    cur = dfg->stones[i];
	    CMtrace_out(dfg->cm, EVdfgVerbose, "Reconfig_deploy, stone %d, (%x)\n", i, cur->stone_id);
	    if (cur->frozen == 0) {
		CMtrace_out(dfg->cm, EVdfgVerbose, "Freezing stone %d, (%x)\n", i, cur->stone_id);
		if (cur->node == 0) {
		    /* Master */
		    EVfreeze_stone(dfg->cm, cur->stone_id);
		} else {
		    REVfreeze_stone(dfg->nodes[cur->node].conn, cur->stone_id);
		}
		cur->frozen = 1;
	    }
	    for (j = 0; j < cur->new_out_count; ++j) {
		CMtrace_out(dfg->cm, EVdfgVerbose, " stone %d (%x) has new out link\n", i, cur->stone_id);
		if (cur->new_out_ports[j] < cur->out_count) {
		    temp_stone = cur->out_links[cur->new_out_ports[j]];
		} else {
		    temp_stone = NULL;
		}
		cur->out_links[cur->new_out_ports[j]] = cur->new_out_links[j];
		
		target = cur->out_links[cur->new_out_ports[j]];
		if (target && (cur->bridge_stone == 0) && (cur->node != target->node)) {
		    cur->out_links[cur->new_out_ports[j]] = create_bridge_stone(dfg, target);
		    CMtrace_out(dfg->cm, EVdfgVerbose, "Created bridge stone (reconfig_deploy) %p, target node %d, assigned to node %d\n", cur->out_links[j], target->node, cur->node);
		    /* put the bridge stone where the source stone is */
		    cur->out_links[cur->new_out_ports[j]]->node = cur->node;
		    cur->out_links[cur->new_out_ports[j]]->pending_events = cur->processed_pending_events;
		    if (cur->node == 0) {
			new_bridge_stone_id = EVcreate_bridge_action(dfg->cm, dfg->nodes[target->node].contact_list, target->stone_id);
			cur->out_links[cur->new_out_ports[j]]->stone_id = new_bridge_stone_id;
			EVstone_add_split_target(dfg->cm, cur->stone_id, new_bridge_stone_id);
		    } else {
			new_bridge_stone_id = REVcreate_bridge_action(dfg->nodes[cur->node].conn, dfg->nodes[target->node].contact_list, target->stone_id);
			REVstone_add_split_target(dfg->nodes[cur->node].conn, cur->stone_id, new_bridge_stone_id);
		    }
		}
		else {
		    if (cur->node == 0) {
			EVstone_add_split_target(dfg->cm, cur->stone_id, target->stone_id);
		    }
		    else {
			REVstone_add_split_target(dfg->nodes[cur->node].conn, cur->stone_id, target->stone_id);
		    }
		}
		
		if (temp_stone != NULL) {
		    if (temp_stone->invalid == 0) {
			if (temp_stone->frozen == 0) {
			    if (temp_stone->node == 0) {
				EVfreeze_stone(dfg->cm, temp_stone->stone_id);
				EVtransfer_events(dfg->cm, temp_stone->stone_id, cur->out_links[cur->new_out_ports[j]]->stone_id);
			    } else {
				REVfreeze_stone(dfg->nodes[temp_stone->node].conn, temp_stone->stone_id);
				REVtransfer_events(dfg->nodes[temp_stone->node].conn, temp_stone->stone_id, cur->out_links[cur->new_out_ports[j]]->stone_id);
			    }
			}
			if (cur->node == 0) {
			    EVstone_remove_split_target(dfg->cm, cur->stone_id, temp_stone->stone_id);
			} else {
			    REVstone_remove_split_target(dfg->nodes[cur->node].conn, cur->stone_id, temp_stone->stone_id);
			}

			if (temp_stone->bridge_stone) {
			    if (cur->node == 0) {
				CMtrace_out(dfg->cm, EVdfgVerbose, "\nreconfig_deploy: Locally freeing..\n");
				fflush(stdout);
				EVfree_stone(dfg->cm, temp_stone->stone_id);
			    } else {
				CMtrace_out(dfg->cm, EVdfgVerbose, "reconfig_deploy: Remotely freeing..\n");
				fflush(stdout);
				REVfree_stone(dfg->nodes[temp_stone->node].conn, temp_stone->stone_id);
			    }
			    //free(temp_stone);
			    //temp_stone = NULL;
			}
			temp_stone->invalid = 1;
		    }
		}
	    }
	}
    }
    
    
    /* ****** Transferring events ******
     */
    
    for (i = 0; i < dfg->transfer_events_count; ++i) {
	EVdfg_stone temp = dfg->stones[dfg->transfer_events_list[i][0]];
	EVdfg_stone src = temp->out_links[dfg->transfer_events_list[i][1]];
	EVdfg_stone dest;
	
	CMtrace_out(dfg->cm, EVdfgVerbose, " Transfering events\n");
	if (temp->node == 0) {
	    if (temp->frozen == 0) {
		EVfreeze_stone(dfg->cm, temp->stone_id);
		temp->frozen = 1;
	    }
	    if (src->frozen == 0) {
		EVfreeze_stone(dfg->cm, src->stone_id);
		src->frozen = 1;
	    }
	} else {
	    if (temp->frozen == 0) {
		REVfreeze_stone(dfg->nodes[temp->node].conn, temp->stone_id);
		temp->frozen = 1;
	    }
	    if (src->frozen == 0) {
		REVfreeze_stone(dfg->nodes[src->node].conn, src->stone_id);
		src->frozen = 1;
	    }
	}
	
	temp = dfg->stones[dfg->transfer_events_list[i][2]];
	dest = temp->out_links[dfg->transfer_events_list[i][3]];
	
	if (src->node == 0) {
	    EVtransfer_events(dfg->cm, src->stone_id, dest->stone_id);
	} else {
	    REVtransfer_events(dfg->nodes[src->node].conn, src->stone_id, dest->stone_id);
	}
    }
    
    /* ****** Deleting links ******
     */
    
    for (i = 0; i < dfg->delete_count; ++i) {
	CMtrace_out(dfg->cm, EVdfgVerbose, " Deleting a link\n");
	reconfig_delete_link(dfg, dfg->delete_list[i][0], dfg->delete_list[i][1]);
    }
    
    for (i = 0; i < dfg->stone_count; ++i) {
        cur = dfg->stones[i];
	if (cur->frozen == 1 && cur->invalid == 0) {
	    if (dfg->stones[i]->new_out_count > 0) {
		free(cur->new_out_links);
		free(cur->new_out_ports);
		cur->new_out_count = 0;
	    }
	    if (cur->node == 0) {
		//	    if (cur->pending_events != NULL) {
		//	      printf("\nResubmitting events locally! Cheers!\n");
		//	      fflush(stdout);
		//	      EVsubmit_encoded(dfg->cm, cur->stone_id, cur->pending_events->buffer, cur->pending_events->length, dfg->nodes[0].contact_list);
		//	    }
		EVunfreeze_stone(dfg->cm, cur->stone_id);
	    } else {
		//	    if (cur->pending_events != NULL) {
		//	      printf("\nResubmitting events remotely! Cheers!\n");
		//	      fflush(stdout);
		//REVsubmit_encoded(dfg->nodes[cur->node].conn, cur->stone_id, cur->pending_events->buffer, cur->pending_events->length, dfg->nodes[cur->node].contact_list);
		//            }
		REVunfreeze_stone(dfg->nodes[cur->node].conn, cur->stone_id);
	    }
	    cur->frozen = 0;
	}
    }
    
    
    CManager_lock(dfg->cm);
    if (CMtrace_on(dfg->cm, EVdfgVerbose)) {
	for (i = 0; i < dfg->stone_count; ++i) {
	    fprintf(CMTrace_file, "Stone# %d : ", i);
	    fdump_dfg_stone(CMTrace_file, dfg->stones[i]);
	}
    }
    dfg->deployed_stone_count = dfg->stone_count;

}

static void
dump_dfg_stone(EVdfg_stone s)
{
    fdump_dfg_stone(stdout, s);
}

static void
fdump_dfg_stone(FILE* out, EVdfg_stone s)
{
    int i;

    (void)dump_dfg_stone;   /* stop warning aboud dump_dfg_stone, handy to keep around for debugging */

    fprintf(out, "stone %p, node %d, stone_id %x\n", s, s->node, s->stone_id);
    if (s->bridge_stone) fprintf(out, "      bridge_stone\n");
    fprintf(out, " out_count %d : ", s->out_count);
    for (i=0; i < s->out_count; i++) {
	fprintf(out, "%p, ", s->out_links[i]);
    }
    fprintf(out, "\n action_count %d, action = \"%s\"\n", s->action_count, (s->action ? s->action : "NULL"));
    fprintf(out, "new_out_count %d : ", s->new_out_count);
    for (i=0; i < s->new_out_count; i++) {
	fprintf(out, "(port %d) -> %p, ", s->new_out_ports[i], s->new_out_links[i]);
    }
    fprintf(out, "\nbridge_target %p\n", s->bridge_target);
}

static void
dfg_startup_ack_handler(CManager cm, CMConnection conn, void *vmsg, 
		  void *client_data, attr_list attrs)
{
    EVdfg dfg = client_data;
    EVstartup_ack_ptr msg =  vmsg;
    (void) conn;
    (void) attrs;
    CManager_lock(cm);
    dfg->deploy_ack_count++;
    CMtrace_out(cm, EVdfgVerbose, "Client %s reports deployed, count %d\n", msg->node_id, dfg->deploy_ack_count);
    if ((dfg->deploy_ack_count == dfg->node_count) && (dfg->startup_ack_condition != -1)) {
	CMtrace_out(cm, EVdfgVerbose, "That was the last one, Signalling %d\n", dfg->startup_ack_condition);
	CMtrace_out(cm, EVdfgVerbose, "EVDFG exit startup ack handler -  master DFG state is %s\n", str_state[dfg->state]);
	INT_CMCondition_signal(cm, dfg->startup_ack_condition);
	dfg->startup_ack_condition = -1;
	assert(dfg->state == DFG_Starting);
	dfg->state = DFG_Running;
	CMtrace_out(cm, EVdfgVerbose, "EVDFG  -  master DFG state set to %s\n", str_state[dfg->state]);
    } else {
      if (dfg->state == DFG_Reconfiguring) {
	dfg->state = DFG_Running;
	CMtrace_out(cm, EVdfgVerbose, "EVDFG after reconfiguration -  master DFG state set to %s\n", str_state[dfg->state]);
      }
    }
    CMtrace_out(cm, EVdfgVerbose, "EVDFG exit startup ack handler -  master DFG state is %s\n", str_state[dfg->state]);
    handle_queued_messages(dfg);
    CManager_unlock(cm);
}

extern void INT_EVdfg_reconfig_transfer_events(EVdfg dfg, int src_stone_index, int src_port, int dest_stone_index, int dest_port) 
{
	
    if (dfg->transfer_events_count == 0) {
	dfg->transfer_events_list = malloc(sizeof(int *));
    } else {
	dfg->transfer_events_list = realloc(dfg->transfer_events_list, (dfg->transfer_events_count + 1) * sizeof(int *));
    }
	
    dfg->transfer_events_list[dfg->transfer_events_count] = malloc(4 * sizeof(int));
	
    dfg->transfer_events_list[dfg->transfer_events_count][0] = src_stone_index;
    dfg->transfer_events_list[dfg->transfer_events_count][1] = src_port;
    dfg->transfer_events_list[dfg->transfer_events_count][2] = dest_stone_index;
    dfg->transfer_events_list[dfg->transfer_events_count][3] = dest_port;
	
    ++dfg->transfer_events_count;
}

static void reconfig_add_bridge_stones(EVdfg dfg) 
{	
    int i;
    int j;
    int k;
    EVdfg_stone cur = NULL;
	
    for (i = dfg->deployed_stone_count; i < dfg->stone_count; ++i) {
	if (dfg->stones[i]->bridge_stone == 0) {
	    cur = dfg->stones[i];
	    for (k = dfg->old_node_count; k < dfg->node_count; ++k) {
		if (k == cur->node && cur->new_out_count !=0 ) {
		    for (j = 0; j < cur->new_out_count; ++j) {
			INT_EVdfg_link_port(cur, cur->new_out_ports[j], cur->new_out_links[j]);
		    }
		    
		    free(cur->new_out_links);
		    free(cur->new_out_ports);
		    
		    cur->new_out_links = NULL;
		    cur->new_out_ports = NULL;
		    
		    cur->new_out_count = 0;
		    for (j = 0; j < cur->out_count; ++j) {
			EVdfg_stone temp_stone, target = cur->out_links[j];
			if (target->bridge_stone) {
			    temp_stone = target;
			    target = target->bridge_target;
			}
			if (target && (!cur->bridge_stone) && (cur->node != target->node)) {
			    cur->out_links[j] = create_bridge_stone(dfg, target);
			    /* put the bridge stone where the source stone is */
			    cur->out_links[j]->node = cur->node;
			}
		    }
		    break;
		}
	    }
	}
    }
}


extern void INT_EVdfg_reconfig_delete_link(EVdfg dfg, int src_index, int dest_index)
{
    if (dfg->delete_count == 0) {
	dfg->delete_list = malloc(sizeof(int *));
    } else {
	dfg->delete_list = realloc(dfg->delete_list, (dfg->delete_count + 1) * sizeof(int *));
    }
	
    dfg->delete_list[dfg->delete_count] = malloc(2 * sizeof(int));
	
    dfg->delete_list[dfg->delete_count][0] = src_index;
    dfg->delete_list[dfg->delete_count][1] = dest_index;
	
    ++dfg->delete_count;
}


extern
void INT_REVdfg_freeze_next_bridge_stone(EVdfg dfg, int stone_index)
{
    REVfreeze_stone(dfg->nodes[dfg->stones[stone_index]->node].conn, dfg->stones[stone_index]->out_links[0]->stone_id);

    if (dfg->realized == 1) {
	dfg->reconfig = 0;
    }
    dfg->no_deployment = 1;
}

extern
void INT_EVdfg_freeze_next_bridge_stone(EVdfg dfg, int stone_index) 
{
    EVfreeze_stone(dfg->cm, dfg->stones[stone_index]->out_links[0]->stone_id);
	
    if (dfg->realized == 1) {
	dfg->reconfig = 0;
    }
    dfg->no_deployment = 1;
}

static void
perform_deployment(EVdfg dfg)
{
    int i;

    if (dfg->sig_reconfig_bool == 0) {
	assert(dfg->state == DFG_Joining);
	dfg->state = DFG_Starting;
	CMtrace_out(cm, EVdfgVerbose, "EVDFG check all nodes registered -  master DFG state is %s\n", str_state[dfg->state]);
	assign_stone_ids(dfg);
	add_bridge_stones(dfg);
	dfg->deploy_ack_count = 1;  /* we are number 1 */
	if (dfg->startup_ack_condition == -1) {
	    dfg->startup_ack_condition = INT_CMCondition_get(dfg->cm, NULL);
	}
	for (i=0; i < dfg->node_count; i++) {
	    deploy_to_node(dfg, i);
	}
    } else {
        CMtrace_out(cm, EVdfgVerbose, "EVDFG perform_deployment -  master DFG state set to %s\n", str_state[dfg->state]);
	assert(dfg->state == DFG_Reconfiguring);
	reconfig_add_bridge_stones(dfg);
	reconfig_deploy(dfg);
    }
}

static void
wait_for_startup_acks(EVdfg dfg)
{
    if (dfg->deploy_ack_count != dfg->node_count) {
	if (dfg->startup_ack_condition != -1)  {
	    CManager_unlock(dfg->cm);
	    CMCondition_wait(dfg->cm, dfg->startup_ack_condition);
	    CManager_lock(dfg->cm);
	}
    }
}

static void
reconfig_signal_ready(EVdfg dfg)
{
    int i;
    CMFormat ready_msg = INT_CMlookup_format(dfg->cm, EVdfg_ready_format_list);
    EVready_msg msg;
    CMtrace_out(cm, EVdfgVerbose, "Master signaling DFG %p ready for operation\n",
				dfg);
    for (i=dfg->old_node_count; i < dfg->node_count; i++) {
	if (dfg->nodes[i].conn != NULL) {
	    msg.node_id = i;
	    INT_CMwrite(dfg->nodes[i].conn, ready_msg, &msg);
	    CMtrace_out(cm, EVdfgVerbose, "Master - ready sent to node \"%s\"\n",
			dfg->nodes[i].name);
	}
    }
}

static void
signal_ready(EVdfg dfg)
{
    int i;
    CMFormat ready_msg = INT_CMlookup_format(dfg->cm, EVdfg_ready_format_list);
    EVready_msg msg;
    CMtrace_out(cm, EVdfgVerbose, "Master signaling DFG %p ready for operation\n",
		dfg);
    for (i=0; i < dfg->node_count; i++) {
	if (dfg->nodes[i].conn != NULL) {
	    msg.node_id = i;
	    INT_CMwrite(dfg->nodes[i].conn, ready_msg, &msg);
	    CMtrace_out(cm, EVdfgVerbose, "Master - ready sent to node \"%s\"\n",
			dfg->nodes[i].name);
	} else {
	    if (!dfg->nodes[i].self) {
		printf("Failure, no connection, not self, node %d\n", i);
		exit(1);
	    }
	    CManager_unlock(dfg->cm);
	    msg.node_id = i;
	    CMtrace_out(cm, EVdfgVerbose, "Master DFG %p is ready, signalling %d\n", dfg, dfg->ready_condition);
	    dfg_ready_handler(dfg->cm, NULL, &msg, dfg, NULL);
	    CManager_lock(dfg->cm);
	}
    }
}

static void
possibly_signal_shutdown(EVdfg dfg, int value, CMConnection conn)
{
    int i;
    CMFormat shutdown_msg = INT_CMlookup_format(dfg->cm, EVdfg_shutdown_format_list);
    EVshutdown_msg msg;
    int status = STATUS_SUCCESS;
    int shutdown = 1;
    int signal_from_client = -1;
    assert(CManager_locked(dfg->cm));
    for (i=0; i < dfg->node_count; i++) {
	if ((conn == NULL) && dfg->nodes[i].self) {
	    /* we're the master and node i */
	    signal_from_client = i;
	} else if (conn == dfg->nodes[i].conn) {
	    signal_from_client = i;
	}
    }
	
    CMtrace_out(cm, EVdfgVerbose, "Client %d signals %d, See if we're all ready to signal shutdown\n", signal_from_client, value);
    dfg->nodes[signal_from_client].shutdown_status_contribution = value;
    for (i=0; i < dfg->node_count; i++) {
	CMtrace_out(cm, EVdfgVerbose, "NODE %d status is :", i);
	switch (dfg->nodes[i].shutdown_status_contribution) {
	case STATUS_UNDETERMINED:
	    CMtrace_out(cm, EVdfgVerbose, "NOT READY FOR SHUTDOWN\n");
	    shutdown = 0;
	    break;
	case STATUS_NO_CONTRIBUTION:
	    CMtrace_out(cm, EVdfgVerbose, "READY for shutdown, no status\n");
	    break;
	case STATUS_SUCCESS:
	    CMtrace_out(cm, EVdfgVerbose, "READY for shutdown, SUCCESS\n");
	    break;
	case STATUS_FAILED:
	    CMtrace_out(cm, EVdfgVerbose, "ALREADY FAILED\n");
	    break;
	default:
	    CMtrace_out(cm, EVdfgVerbose, "READY for shutdown, FAILURE %d\n",
			dfg->nodes[i].shutdown_status_contribution);
	    status |= dfg->nodes[i].shutdown_status_contribution;
	    break;
	}	    
    }
    if (!shutdown) {
	CMtrace_out(cm, EVdfgVerbose, "DFG not ready for shutdown\n");
	return;
    }
    CMtrace_out(cm, EVdfgVerbose, "DFG shutdown with value %d\n", status);
    dfg->state = DFG_Shutting_Down;
    CMtrace_out(cm, EVdfgVerbose, "EVDFG possibly signal shutdown -  master DFG state is %s\n", str_state[dfg->state]);
    msg.value = status;
    for (i=0; i < dfg->node_count; i++) {
	if (dfg->nodes[i].conn != NULL) {
	    INT_CMwrite(dfg->nodes[i].conn, shutdown_msg, &msg);
	    CMtrace_out(cm, EVdfgVerbose, "DFG shutdown message sent to client \"%s\"(%d)\n", dfg->nodes[i].canonical_name, i);
	} else {
	    if (!dfg->nodes[i].self) {
		printf("Failure, no connection, not self, node %d\n", i);
		exit(1);
	    }
	}
    }
    dfg->shutdown_value = status;
    i = 0;
    dfg->already_shutdown = 1;
    while(dfg->shutdown_conditions && (dfg->shutdown_conditions[i] != -1)) {
	CMtrace_out(cm, EVdfgVerbose, "Client %d shutdown signalling %d\n", dfg->my_node_id, dfg->shutdown_conditions[i]);
	INT_CMCondition_signal(dfg->cm, dfg->shutdown_conditions[i++]);
    }
    CMtrace_out(cm, EVdfgVerbose, "Master DFG shutdown\n");
}

extern void INT_EVdfg_node_join_handler(EVdfg dfg, EVdfgJoinHandlerFunc func)
{
    dfg->node_join_handler = func;
}

extern void INT_EVdfg_node_fail_handler(EVdfg dfg, EVdfgFailHandlerFunc func)
{
    dfg->node_fail_handler = func;
}

static void
check_all_nodes_registered(EVdfg dfg)
{
    int i;
    if (dfg->node_join_handler != NULL) {
	EVint_node_list node = &dfg->nodes[dfg->node_count-1];
	CManager_unlock(dfg->cm);
	(dfg->node_join_handler)(dfg, node->name, NULL, NULL);
	CManager_lock(dfg->cm);
	if ((dfg->realized == 0) || (dfg->realized == 1 && dfg->reconfig == 1)) return;
    } else {
	/* must be static node list */
	for(i=0; i<dfg->node_count; i++) {
	    if (!dfg->nodes[i].self && (dfg->nodes[i].conn == NULL)) {
		return;
	    }
	}
    }
	
    if (dfg->no_deployment == 0) {
	perform_deployment(dfg);
	wait_for_startup_acks(dfg);
    }
    dfg->no_deployment = 0;
    if (dfg->sig_reconfig_bool == 0) {
	signal_ready(dfg);
    } else {
	reconfig_signal_ready(dfg);
    }
    dfg->deployed_stone_count = dfg->stone_count;
    dfg->old_node_count = dfg->node_count;
}

