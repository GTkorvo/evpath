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

#define STATUS_UNDETERMINED -2
#define STATUS_NO_CONTRIBUTION -1
#define STATUS_SUCCESS 0
#define STATUS_FAILURE 1

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


struct _EVdfg {
    CManager cm;
    char *master_contact_str;
    char *master_command_contact_str;
    CMConnection master_connection;
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
};

EVdfg_stone
EVdfg_create_source_stone(EVdfg dfg, char *source_name)
{
    int len = strlen(source_name) + strlen("source:");
    char *act = malloc(len + 1);
    strcpy(stpcpy(&act[0], "source:"), source_name);
    return EVdfg_create_stone(dfg, &act[0]);
}

EVdfg_stone
EVdfg_create_sink_stone(EVdfg dfg, char *sink_name)
{
    int len = strlen(sink_name) + strlen("sink:");
    char *act = malloc(len + 1);
    strcpy(stpcpy(&act[0], "sink:"), sink_name);
    return EVdfg_create_stone(dfg, &act[0]);
}

EVdfg_stone
EVdfg_create_stone(EVdfg dfg, char *action)
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
EVdfg_enable_auto_stone(EVdfg_stone stone, int period_sec, 
			int period_usec)
{
    stone->period_secs = period_sec;
    stone->period_usecs = period_usec;
}

extern void
EVdfg_link_port(EVdfg_stone src, int port, EVdfg_stone dest)
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
EVdfg_set_attr_list(EVdfg_stone stone, attr_list attrs)
{
    if (stone->attrs != NULL) {
	fprintf(stderr, "Warning, attributes for stone %p previously set, overwriting\n", stone);
    }
    add_ref_attr_list(attrs);
    stone->attrs = attrs;
}

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

typedef struct _EVready_msg {
    int node_id;
} EVready_msg, *EVready_ptr;

FMField EVready_msg_flds[] = {
    {"node_id", "integer", sizeof(int), FMOffset(EVready_ptr, node_id)},
    {NULL, NULL, 0, 0}
};

FMStructDescRec EVdfg_ready_format_list[] = {
    {"EVdfg_ready", EVready_msg_flds, sizeof(EVready_msg), NULL},
    {NULL, NULL, 0, NULL}
};

typedef struct _EVstartup_ack_msg {
    char *node_id;
} EVstartup_ack_msg, *EVstartup_ack_ptr;

FMField EVstartup_ack_msg_flds[] = {
    {"node_id", "string", sizeof(char*), FMOffset(EVstartup_ack_ptr, node_id)},
    {NULL, NULL, 0, 0}
};

FMStructDescRec EVdfg_startup_ack_format_list[] = {
    {"EVdfg_startup_ack", EVstartup_ack_msg_flds, sizeof(EVstartup_ack_msg), NULL},
    {NULL, NULL, 0, NULL}
};

typedef struct _EVshutdown_msg {
    int value;
} EVshutdown_msg, *EVshutdown_ptr;

FMField EVshutdown_msg_flds[] = {
    {"value", "integer", sizeof(int), FMOffset(EVshutdown_ptr, value)},
    {NULL, NULL, 0, 0}
};

FMStructDescRec EVdfg_shutdown_format_list[] = {
    {"EVdfg_shutdown", EVshutdown_msg_flds, sizeof(EVshutdown_msg), NULL},
    {NULL, NULL, 0, NULL}
};

typedef struct _EVconn_shutdown_msg {
    int stone;
} EVconn_shutdown_msg, *EVconn_shutdown_ptr;

FMField EVconn_shutdown_msg_flds[] = {
    {"stone", "integer", sizeof(int), FMOffset(EVconn_shutdown_ptr, stone)},
    {NULL, NULL, 0, 0}
};

FMStructDescRec EVdfg_conn_shutdown_format_list[] = {
    {"EVdfg_conn_shutdown", EVconn_shutdown_msg_flds, sizeof(EVconn_shutdown_msg), NULL},
    {NULL, NULL, 0, NULL}
};

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
typedef struct _EVdfg_stones_msg {
    char *canonical_name;
    int stone_count;
    deploy_msg_stone stone_list;
} EVdfg_stones_msg, *EVdfg_stones_ptr;

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

static void
dfg_startup_ack_handler(CManager cm, CMConnection conn, void *vmsg, 
			void *client_data, attr_list attrs);
static void
dfg_ready_handler(CManager cm, CMConnection conn, void *vmsg, 
		  void *client_data, attr_list attrs)
{
    EVdfg dfg = client_data;
    EVready_ptr msg =  vmsg;
    (void) conn;
    (void) attrs;
    CMtrace_out(cm, EVerbose, "Client DFG %p is ready, signaling %d\n", dfg, dfg->ready_condition);
    dfg->my_node_id = msg->node_id;
    CMCondition_signal(cm, dfg->ready_condition);
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
    CMtrace_out(cm, EVerbose, "The master knows about a stone %d failure\n", msg->stone);
    if (dfg->node_fail_handler != NULL) {
	int i;
	int target_stone = -1;
	char *failed_node = NULL;
	char *contact_str = NULL;
	for (i=0; i< dfg->stone_count; i++) {
	    int j;
	    for (j = 0; j < dfg->stones[i]->out_count; j++) {
		if (dfg->stones[i]->out_links[j]->stone_id == msg->stone) {
		    EVdfg_stone out_stone = dfg->stones[i]->out_links[j];
		    printf("Found reporting stone as output %d of stone %d\n",
			   j, i);
		    parse_bridge_action_spec(out_stone->action, 
					     &target_stone, &contact_str);
		    printf("Dead stone is %d\n", target_stone);
		}
	    }
	}
	for (i=0; i< dfg->stone_count; i++) {
	    if (dfg->stones[i]->stone_id == target_stone) {
		int node = dfg->stones[i]->node;
		printf("Dead node is %d, name %s\n", node,
		       dfg->nodes[node].canonical_name);
		failed_node = dfg->nodes[node].canonical_name;
	    }
	}
	dfg->node_fail_handler(dfg, failed_node, target_stone);
    }
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
    if (dfg->master_connection == NULL) {
	/* I got a shutdown message and I'm the master */
	possibly_signal_shutdown(dfg, msg->value, conn);
    } else {
	/* I'm the client, all is done */
	int i = 0;
	dfg->shutdown_value = msg->value;
	dfg->already_shutdown = 1;
	CMtrace_out(cm, EVerbose, "Client %d has confirmed shutdown\n", dfg->my_node_id);
	while (dfg->shutdown_conditions && (dfg->shutdown_conditions[i] != -1)){
	    CMCondition_signal(dfg->cm, dfg->shutdown_conditions[i++]);
	}
    }
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
    /* first, freeze the stone so that we don't lose any more data */
    INT_EVfreeze_stone(cm, stone);

	int i;
	for (i=0; i < evp->stone_lookup_table_size; i++ ) {
	    if (stone == evp->stone_lookup_table[i].local_id) {
	        global_stone_id = evp->stone_lookup_table[i].global_id;
	    }
	}
	if (global_stone_id == -1) {
	    printf("Bad mojo, failed to find global stone id after stone close\n");
	}
    if (dfg->master_connection != NULL) {
	CMFormat conn_shutdown_msg = INT_CMlookup_format(dfg->cm, EVdfg_conn_shutdown_format_list);
	EVconn_shutdown_msg msg;
	msg.stone = global_stone_id;
	INT_CMwrite(dfg->master_connection, conn_shutdown_msg, &msg);
    } else {
	EVconn_shutdown_msg msg;
	msg.stone = global_stone_id;
	/* the master is detecting a close situation */
	dfg_conn_shutdown_handler(dfg->cm, NULL, &msg, dfg, NULL);
    }
}

extern void
EVdfg_assign_canonical_name(EVdfg dfg, char *given_name, char *canonical_name)
{
    int node;
    for (node = 0; node < dfg->node_count; node++) {
	if (dfg->nodes[node].name == given_name) {
	    dfg->nodes[node].canonical_name = strdup(canonical_name);
	}
    }
}

static void
node_register_handler(CManager cm, CMConnection conn, void *vmsg, 
		      void *client_data, attr_list attrs)
{
    EVdfg dfg = client_data;
    EVregister_ptr msg =  vmsg;
    int node;
    int new_node = -1;
    (void)cm;
    (void)conn;
    (void)attrs;
    if (dfg->node_join_handler == NULL) {
	/* static node list */
	for (node = 0; node < dfg->node_count; node++) {
	    if (strcmp(dfg->nodes[node].name, msg->node_name) == 0) {
		dfg->nodes[node].conn = conn;
		dfg->nodes[node].str_contact_list = strdup(msg->contact_string);
		dfg->nodes[node].contact_list = attr_list_from_string(dfg->nodes[node].str_contact_list);
		dfg->nodes[node].shutdown_status_contribution = STATUS_UNDETERMINED;
		new_node = node;
		break;
	    }
	}
	if (new_node == -1) {
	    printf("Registering node \"%s\" not found in node list\n", 
		   msg->node_name);
	    return;
	}
    } else {
	int n = dfg->node_count++;
	dfg->nodes = realloc(dfg->nodes, (sizeof(dfg->nodes[0])*dfg->node_count));
	memset(&dfg->nodes[n], 0, sizeof(dfg->nodes[0]));
	dfg->nodes[n].name = strdup(msg->node_name);
	dfg->nodes[n].canonical_name = NULL;
	dfg->nodes[n].shutdown_status_contribution = STATUS_UNDETERMINED;
	dfg->nodes[n].self = 0;
	dfg->nodes[n].conn = conn;
	dfg->nodes[n].str_contact_list = strdup(msg->contact_string);
	dfg->nodes[n].contact_list = attr_list_from_string(dfg->nodes[n].str_contact_list);
	new_node = n;
    }
    CMtrace_out(cm, EVerbose, "Client \"%s\" has joined DFG, contact %s\n", msg->node_name, dfg->nodes[new_node].str_contact_list);

    check_all_nodes_registered(dfg);
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
    CMtrace_out(cm, EVerbose, "Client %d getting Deploy message\n", dfg->my_node_id);

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
	if (msg->stone_list[i].period_secs != -1) {
	    INT_EVenable_auto_stone(cm, local_stone, msg->stone_list[i].period_secs, msg->stone_list[i].period_usecs);
	}
	if (action_type(msg->stone_list[i].action) == Action_Terminal) {
	    dfg->active_sink_count++;
	}
    }    
    if (conn != NULL) {
	CMFormat startup_ack_msg = INT_CMlookup_format(dfg->cm, EVdfg_startup_ack_format_list);
	EVstartup_ack_msg response_msg;
	response_msg.node_id = msg->canonical_name;
	INT_CMwrite(dfg->master_connection, startup_ack_msg, &response_msg);
	CMtrace_out(cm, EVerbose, "Client %d wrote startup ack\n", dfg->my_node_id);
    } else {
      	CMtrace_out(cm, EVerbose, "Client %d no master conn\n", dfg->my_node_id);
    }
    CManager_unlock(cm);
}

extern EVdfg
EVdfg_create(CManager cm)
{
    EVdfg dfg = malloc(sizeof(struct _EVdfg));
    attr_list contact_list;

    memset(dfg, 0, sizeof(struct _EVdfg));
    dfg->cm = cm;
    
    contact_list = CMget_contact_list(cm);
    dfg->master_contact_str = attr_list_to_string(contact_list);
    dfg->startup_ack_condition = -1;
    free_attr_list(contact_list);
    EVregister_close_handler(cm, dfg_stone_close_handler, (void*)dfg);
    CMregister_handler(CMregister_format(cm, EVdfg_register_format_list),
		       node_register_handler, dfg);
    CMregister_handler(CMregister_format(cm, EVdfg_ready_format_list),
		       dfg_ready_handler, dfg);
    CMregister_handler(CMregister_format(cm, EVdfg_startup_ack_format_list),
		       dfg_startup_ack_handler, dfg);
    CMregister_handler(CMregister_format(cm, EVdfg_deploy_format_list),
		       dfg_deploy_handler, dfg);
    CMregister_handler(CMregister_format(cm, EVdfg_shutdown_format_list),
		       dfg_shutdown_handler, dfg);
    CMregister_handler(CMregister_format(cm, EVdfg_conn_shutdown_format_list),
		       dfg_conn_shutdown_handler, dfg);
    return dfg;
}

extern char *EVdfg_get_contact_list(EVdfg dfg)
{
    attr_list listen_list, contact_list = NULL;
    atom_t CM_TRANSPORT = attr_atom_from_string("CM_TRANSPORT");
    CManager cm = dfg->cm;

    /* use enet transport if available */
    listen_list = create_attr_list();
    add_string_attr(listen_list, CM_TRANSPORT, strdup("enet"));
    contact_list = CMget_specific_contact_list(cm, listen_list);

    if (contact_list == NULL) {
	contact_list = CMget_contact_list(cm);
	if (contact_list == NULL) {
	    CMlisten(cm);
	    contact_list = CMget_contact_list(cm);
	}
    }
    dfg->master_command_contact_str = attr_list_to_string(contact_list);
    return dfg->master_command_contact_str;
}

static void
check_connectivity(EVdfg dfg)
{
    int i;
    for (i=0; i< dfg->stone_count; i++) {
	if (dfg->stones[i]->action_count == 0) {
	    printf("Warning, stone %d (assigned to node %s) has no actions registered", i, dfg->nodes[dfg->stones[i]->node].name);
	    continue;
	}
	if (dfg->stones[i]->out_count == 0) {
	    char *action_spec = dfg->stones[i]->action;
	    switch(action_type(action_spec)) {
	    case Action_Terminal:
		break;
	    default:
		printf("Warning, stone %d (assigned to node %s) has no outputs connected to other stones\n", i, dfg->nodes[dfg->stones[i]->node].name);
		printf("	This stone's first registered action is \"%s\"\n",
		       action_spec);
		break;
	    }
	}
    }
}

extern int
EVdfg_realize(EVdfg dfg)
{
    check_connectivity(dfg);
//    check_types(dfg);
    dfg->realized = 1;
    return 1;
}

extern void
EVdfg_register_node_list(EVdfg dfg, char **nodes)
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
EVdfg_assign_node(EVdfg_stone stone, char *node_name)
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
    stone->node = node;
}

extern int 
EVdfg_ready_wait(EVdfg dfg)
{
    CMtrace_out(cm, EVerbose, "DFG %p wait for ready\n", dfg);
    CMCondition_wait(dfg->cm, dfg->ready_condition);
    CMtrace_out(cm, EVerbose, "DFG %p ready wait released\n", dfg);
    return 1;
}

extern int
EVdfg_shutdown(EVdfg dfg, int result)
{
    if (dfg->already_shutdown) printf("Node %d, already shut down BAD!\n", dfg->my_node_id);
    if (dfg->master_connection != NULL) {
	/* we are a client, tell the master to shutdown */
	CMFormat shutdown_msg = CMlookup_format(dfg->cm, EVdfg_shutdown_format_list);
	EVshutdown_msg msg;
	msg.value = result;
	INT_CMwrite(dfg->master_connection, shutdown_msg, &msg);
	/* and wait until we hear back */
    } else {
	possibly_signal_shutdown(dfg, result, NULL);
    }
    if (!dfg->already_shutdown) {
	CMCondition_wait(dfg->cm, new_shutdown_condition(dfg, dfg->master_connection));
    }
    return dfg->shutdown_value;
}

extern int
EVdfg_active_sink_count(EVdfg dfg)
{
    return dfg->active_sink_count;
}

extern void
EVdfg_ready_for_shutdown(EVdfg dfg)
{
    if (dfg->already_shutdown) return;
    if (dfg->master_connection != NULL) {
	/* we are a client, tell the master to shutdown */
	CMFormat shutdown_msg = CMlookup_format(dfg->cm, EVdfg_shutdown_format_list);
	EVshutdown_msg msg;
	msg.value = STATUS_NO_CONTRIBUTION;   /* no status contribution */
	INT_CMwrite(dfg->master_connection, shutdown_msg, &msg);
    } else {
	possibly_signal_shutdown(dfg, STATUS_NO_CONTRIBUTION, NULL);
    }
}

extern int 
EVdfg_wait_for_shutdown(EVdfg dfg)
{
    if (dfg->already_shutdown) return dfg->shutdown_value;
    CMCondition_wait(dfg->cm, new_shutdown_condition(dfg, dfg->master_connection));
    return dfg->shutdown_value;
}

extern int EVdfg_source_active(EVsource src)
{
    return (src->local_stone_id != -1);
}

extern void
EVdfg_register_source(char *name, EVsource src)
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
EVdfg_register_sink_handler(CManager cm, char *name, FMStructDescList list, EVSimpleHandlerFunc handler)
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
EVdfg_register_raw_sink_handler(CManager cm, char *name, EVRawHandlerFunc handler)
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
    dfg->shutdown_conditions[cur_count] = CMCondition_get(dfg->cm, conn);
    dfg->shutdown_conditions[cur_count+1] = -1;
    return dfg->shutdown_conditions[cur_count];
}

extern void
EVdfg_join_dfg(EVdfg dfg, char* node_name, char *master_contact)
{
    CManager cm = dfg->cm;
    event_path_data evp = cm->evp;
    attr_list master_attrs = attr_list_from_string(master_contact);
    dfg->master_contact_str = strdup(master_contact);
    if (CMcontact_self_check(cm, master_attrs) == 1) {
	/* we are the master */
	int node=0;
	if (dfg->node_join_handler == NULL) {
	    /* static node list */
	    for (node = 0; node < dfg->node_count; node++) {
		if (strcmp(dfg->nodes[node].name, node_name) == 0) {
		    dfg->nodes[node].self = 1;
		    dfg->my_node_id = node;
		    break;
		}
	    }
	    if (node == dfg->node_count) {
		printf("Node \"%s\" not found in node list\n", node_name);
		exit(1);
	    }
	} else {
	    dfg->node_count = 1;
	    dfg->nodes = malloc(sizeof(dfg->nodes[0]));
	    memset(dfg->nodes, 0, sizeof(dfg->nodes[0]));
	    dfg->nodes[0].name = strdup(node_name);
	    dfg->nodes[0].canonical_name = NULL;
	    dfg->nodes[0].shutdown_status_contribution = STATUS_UNDETERMINED;
	    dfg->nodes[0].self = 1;
	    dfg->my_node_id = 0;
	}
	dfg->ready_condition = CMCondition_get(cm, NULL);
	check_all_nodes_registered(dfg);
    } else {
	CMConnection conn = CMget_conn(cm, master_attrs);
	CMFormat register_msg = CMlookup_format(cm, EVdfg_register_format_list);
	EVregister_msg msg;
	attr_list contact_list = CMget_contact_list(cm);
	char *my_contact_str;
	int i;
	if (conn == NULL) {
	    fprintf(stderr, "failed to contact Master at %s\n", attr_list_to_string(master_attrs));
	    fprintf(stderr, "Join DFG failed\n");
	    return;
	}
	if (contact_list == NULL) {
	    CMlisten(cm);
	    contact_list = CMget_contact_list(cm);
	}

	my_contact_str = attr_list_to_string(contact_list);
	free_attr_list(contact_list);

	dfg->ready_condition = CMCondition_get(cm, conn);
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
	CMtrace_out(cm, EVerbose, "DFG %p node name %s\n", dfg, node_name);
    }
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
    stone = EVdfg_create_stone(dfg, action);
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
    CMFormat deploy_msg = CMlookup_format(dfg->cm, EVdfg_deploy_format_list);

    for (i=0; i< dfg->stone_count; i++) {
	if (dfg->stones[i]->node == node) {
	    stone_count++;
	}
    }
    if (stone_count == 0) {
        dfg->deploy_ack_count++;
      	return;
    }
    msg.canonical_name = dfg->nodes[node].canonical_name;
    msg.stone_count = stone_count;
    msg.stone_list = malloc(stone_count * sizeof(msg.stone_list[0]));
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
	    mstone->extra_actions = 0;
	    mstone->xactions = NULL;
	    j++;
	}
    }
    if (dfg->nodes[node].conn) {
	INT_CMwrite(dfg->nodes[node].conn, deploy_msg, &msg);
    } else {
	dfg_deploy_handler(dfg->cm, NULL, &msg, dfg, NULL);
    }
    for(i=0 ; i < msg.stone_count; i++) {
	free(msg.stone_list[i].out_links);
	if (msg.stone_list[i].attrs) free(msg.stone_list[i].attrs);
    }
    free(msg.stone_list);
}

static void
dfg_startup_ack_handler(CManager cm, CMConnection conn, void *vmsg, 
		  void *client_data, attr_list attrs)
{
    EVdfg dfg = client_data;
    EVstartup_ack_ptr msg =  vmsg;
    (void) conn;
    (void) attrs;
    dfg->deploy_ack_count++;
    CMtrace_out(cm, EVerbose, "Client %s reports deployed, count %d\n", msg->node_id, dfg->deploy_ack_count);
    if (dfg->deploy_ack_count == dfg->node_count) {
	CMtrace_out(cm, EVerbose, "That was the last one, Signalling %d\n", dfg->startup_ack_condition);
	CMCondition_signal(cm, dfg->startup_ack_condition);
    }
}

static void
perform_deployment(EVdfg dfg)
{
    int i;
    assign_stone_ids(dfg);
    add_bridge_stones(dfg);
    dfg->deploy_ack_count = 1;  /* we are number 1 */
    if (dfg->startup_ack_condition == -1) {
	dfg->startup_ack_condition = INT_CMCondition_get(dfg->cm, NULL);
    }
    for (i=0; i < dfg->node_count; i++) {
	deploy_to_node(dfg, i);
    }
}

static void
wait_for_startup_acks(EVdfg dfg)
{
    if (dfg->deploy_ack_count != dfg->node_count) {
	CMCondition_wait(dfg->cm, dfg->startup_ack_condition);
    }
}

static void
signal_ready(EVdfg dfg)
{
    int i;
    CMFormat ready_msg = CMlookup_format(dfg->cm, EVdfg_ready_format_list);
    EVready_msg msg;
    CMtrace_out(cm, EVerbose, "Master signaling DFG %p ready for operation\n",
		dfg);
    for (i=0; i < dfg->node_count; i++) {
	if (dfg->nodes[i].conn != NULL) {
	    msg.node_id = i;
	    INT_CMwrite(dfg->nodes[i].conn, ready_msg, &msg);
	    CMtrace_out(cm, EVerbose, "Master - ready sent to node \"%s\"\n",
			dfg->nodes[i].name);
	} else {
	    if (!dfg->nodes[i].self) {
		printf("Failure, no connection, not self, node %d\n", i);
		exit(1);
	    }
	}
    }
    CMtrace_out(cm, EVerbose, "Master DFG %p is ready, signaling\n", dfg);
    CMCondition_signal(dfg->cm, dfg->ready_condition);
}

static void
possibly_signal_shutdown(EVdfg dfg, int value, CMConnection conn)
{
    int i;
    CMFormat shutdown_msg = CMlookup_format(dfg->cm, EVdfg_shutdown_format_list);
    EVshutdown_msg msg;
    int status = STATUS_SUCCESS;
    int shutdown = 1;
    int signal_from_client = -1;
    for (i=0; i < dfg->node_count; i++) {
	if ((conn == NULL) && dfg->nodes[i].self) {
	    /* we're the master and node i */
	    signal_from_client = i;
	} else if (conn == dfg->nodes[i].conn) {
	    signal_from_client = i;
	}
    }
	    
    CMtrace_out(cm, EVerbose, "Client %d signals %d, See if we're all ready to signal shutdown\n", signal_from_client, value);
    dfg->nodes[signal_from_client].shutdown_status_contribution = value;
    for (i=0; i < dfg->node_count; i++) {
	CMtrace_out(cm, EVerbose, "NODE %d status is :", i);
	switch (dfg->nodes[i].shutdown_status_contribution) {
	case STATUS_UNDETERMINED:
	    CMtrace_out(cm, EVerbose, "NOT READY FOR SHUTDOWN\n");
	    shutdown = 0;
	    break;
	case STATUS_NO_CONTRIBUTION:
	    CMtrace_out(cm, EVerbose, "READY for shutdown, no status\n");
	    break;
	case STATUS_SUCCESS:
	    CMtrace_out(cm, EVerbose, "READY for shutdown, SUCCESS\n");
	    break;
	default:
	    CMtrace_out(cm, EVerbose, "READY for shutdown, FAILURE %d\n",
			dfg->nodes[i].shutdown_status_contribution);
	    status |= dfg->nodes[i].shutdown_status_contribution;
	    break;
	}	    
    }
    if (!shutdown) {
	CMtrace_out(cm, EVerbose, "DFG not ready for shutdown\n");
	return;
    }
    CMtrace_out(cm, EVerbose, "DFG shutdown with value %d\n", status);
    msg.value = status;
    for (i=0; i < dfg->node_count; i++) {
	if (dfg->nodes[i].conn != NULL) {
	    INT_CMwrite(dfg->nodes[i].conn, shutdown_msg, &msg);
	    CMtrace_out(cm, EVerbose, "DFG shutdown message sent to client \"%s\"\n", dfg->nodes[i].name);
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
	CMCondition_signal(dfg->cm, dfg->shutdown_conditions[i++]);
    }
    CMtrace_out(cm, EVerbose, "Master DFG shutdown\n");
}

extern void EVdfg_node_join_handler(EVdfg dfg, EVdfgJoinHandlerFunc func)
{
    dfg->node_join_handler = func;
}

extern void EVdfg_node_fail_handler(EVdfg dfg, EVdfgFailHandlerFunc func)
{
    dfg->node_fail_handler = func;
}

static void
check_all_nodes_registered(EVdfg dfg)
{
    int i;
    if (dfg->node_join_handler != NULL) {
	EVint_node_list node = &dfg->nodes[dfg->node_count-1];
	(dfg->node_join_handler)(dfg, node->name, NULL, NULL);
	if (dfg->realized == 0) return;
    } else {
	/* must be static node list */
	for(i=0; i<dfg->node_count; i++) {
	    if (!dfg->nodes[i].self && (dfg->nodes[i].conn == NULL)) {
		return;
	    }
	}
    }
    perform_deployment(dfg);
    wait_for_startup_acks(dfg);
    signal_ready(dfg);
}

