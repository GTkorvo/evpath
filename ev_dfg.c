#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <strings.h>

#include "evpath.h"
#include "cm_internal.h"
#include "ev_deploy.h"

struct _EVdfg_stone {
    EVdfg dfg;
    int node;
    int bridge_stone;
    int stone_id;
    int out_count;
    EVdfg_stone *out_links;
    int action_count;
    char *action;
    char **extra_actions;
};

typedef struct _EVint_node_rec {
    char *name;
    attr_list contact_list;
    char *str_contact_list;
    CMConnection conn;
    int self;
} *EVint_node_list;


struct _EVdfg {
    CManager cm;
    char *master_contact_str;
    CMConnection master_connection;
    int shutdown_value;
    int ready_condition;
    int shutdown_condition;
    int stone_count;
    EVdfg_stone *stones;
    int node_count;
    EVint_node_list nodes;
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
	
typedef struct _EVregister_msg {
    char *node_name;
    char *contact_string;
} EVregister_msg, *EVregister_ptr;

FMField EVregister_msg_flds[] = {
    {"node_name", "string", sizeof(char*), FMOffset(EVregister_ptr, node_name)},
    {"contact_string", "string", sizeof(char*), FMOffset(EVregister_ptr, contact_string)},
    {NULL, NULL, 0, 0}
};

FMStructDescRec EVdeploy_register_format_list[] = {
    {"EVdeploy_register", EVregister_msg_flds, sizeof(EVregister_msg), NULL},
    {NULL, NULL, 0, NULL}
};

typedef struct _EVready_msg {
    int junk;
} EVready_msg, *EVready_ptr;

FMField EVready_msg_flds[] = {
    {"junk", "integer", sizeof(int), FMOffset(EVready_ptr, junk)},
    {NULL, NULL, 0, 0}
};

FMStructDescRec EVdfg_ready_format_list[] = {
    {"EVdfg_ready", EVready_msg_flds, sizeof(EVready_msg), NULL},
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

typedef struct _EVdeploy_msg_stone {
    int global_stone_id;
    int out_count;
    int *out_links;
    char *action;
    int extra_actions;
    char **xactions;
} *deploy_msg_stone;

FMField EVdeploy_stone_flds[] = {
    {"global_stone_id", "integer", sizeof(int), 
     FMOffset(deploy_msg_stone, global_stone_id)},
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
typedef struct _EVdeploy_stones_msg {
    int stone_count;
    deploy_msg_stone stone_list;
} EVdeploy_stones_msg, *EVdeploy_stones_ptr;

FMField EVdeploy_msg_flds[] = {
    {"stone_count", "integer", sizeof(int),
     FMOffset(EVdeploy_stones_ptr, stone_count)},
    {"stone_list", "EVdfg_deploy_stone[stone_count]", sizeof(struct _EVdeploy_msg_stone), FMOffset(EVdeploy_stones_ptr, stone_list)},
    {NULL, NULL, 0, 0}
};

FMStructDescRec EVdfg_deploy_format_list[] = {
    {"EVdfg_deploy", EVdeploy_msg_flds, sizeof(EVdeploy_stones_msg), NULL},
    {"EVdfg_deploy_stone", EVdeploy_stone_flds, sizeof(struct _EVdeploy_msg_stone), NULL},
    {NULL, NULL, 0, NULL}
};

static void check_all_nodes_registered(EVdfg dfg);
static void signal_shutdown(EVdfg dfg, int value);

static void
dfg_ready_handler(CManager cm, CMConnection conn, void *vmsg, 
		  void *client_data, attr_list attrs)
{
    EVdfg dfg = client_data;
    /* EVready_ptr msg =  vmsg; */
    (void) vmsg;
    (void) conn;
    (void) attrs;
    CMtrace_out(cm, EVerbose, "Client DFG %p is ready, signaling\n", dfg);
    CMCondition_signal(cm, dfg->ready_condition);
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
	signal_shutdown(dfg, msg->value);
    } else {
	/* I'm the client, all is done */
	dfg->shutdown_value = msg->value;
	CMCondition_signal(dfg->cm, dfg->shutdown_condition);
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
    for (node = 0; node < dfg->node_count; node++) {
	if (strcmp(dfg->nodes[node].name, msg->node_name) == 0) {
	    dfg->nodes[node].conn = conn;
	    dfg->nodes[node].str_contact_list = strdup(msg->contact_string);
	    dfg->nodes[node].contact_list = attr_list_from_string(dfg->nodes[node].str_contact_list);
	    new_node = node;
	    break;
	}
    }
    if (new_node == -1) {
	printf("Registering node \"%s\" not found in node list\n", 
	       msg->node_name);
	return;
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
    EVdeploy_stones_ptr msg =  vmsg;
    int i, base = evp->stone_lookup_table_size;

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
	for (j=0; j < msg->stone_list[i].out_count; j++) {
	    local_list[j] = lookup_local_stone(evp, msg->stone_list[i].out_links[j]);
	    if (local_list[j] == -1) {
		printf("Didn't found global stone %d\n", msg->stone_list[i].out_links[j]);
	    }
	}
	local_list[j] = -1;
	INT_EVassoc_general_action(cm, local_stone, msg->stone_list[i].action, 
	    &local_list[0]);
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
    free_attr_list(contact_list);
    CMregister_handler(CMregister_format(cm, EVdeploy_register_format_list),
		       node_register_handler, dfg);
    CMregister_handler(CMregister_format(cm, EVdfg_ready_format_list),
		       dfg_ready_handler, dfg);
    CMregister_handler(CMregister_format(cm, EVdfg_deploy_format_list),
		       dfg_deploy_handler, dfg);
    CMregister_handler(CMregister_format(cm, EVdfg_shutdown_format_list),
		       dfg_shutdown_handler, dfg);
    return dfg;
}

extern int
EVdfg_realize(EVdfg dfg)
{
//    check_connectivity(dfg);
//    check_types(dfg);
    (void) dfg;
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
    }
}

extern void
EVdfg_assign_node(EVdfg_stone stone, char *node_name)
{
    EVdfg dfg = stone->dfg;
    int i, node = -1;
    for (i = 0; i < dfg->node_count; i++) {
	if (strcmp(dfg->nodes[i].name, node_name) == 0) {
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
    CMtrace_out(cm, EVerbose, "DFG wait for ready\n");
    CMCondition_wait(dfg->cm, dfg->ready_condition);
    CMtrace_out(cm, EVerbose, "DFG ready wait released\n");
    return 1;
}

extern void 
EVdfg_shutdown(EVdfg dfg, int result)
{
    if (dfg->master_connection != NULL) {
	/* we are a client, tell the master to shutdown */
	CMFormat shutdown_msg = CMlookup_format(dfg->cm, EVdfg_shutdown_format_list);
	EVshutdown_msg msg;
	msg.value = result;
	CMwrite(dfg->master_connection, shutdown_msg, &msg);
	/* and wait until we hear back */
    } else {
	signal_shutdown(dfg, result);
    }
}

extern int 
EVdfg_wait_for_shutdown(EVdfg dfg)
{
    CMCondition_wait(dfg->cm, dfg->shutdown_condition);
    return dfg->shutdown_value;
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
EVdfg_join_dfg(EVdfg dfg, char* node_name, char *master_contact)
{
    CManager cm = dfg->cm;
    attr_list master_attrs = attr_list_from_string(master_contact);
    dfg->master_contact_str = strdup(master_contact);
    if (CMcontact_self_check(cm, master_attrs) == 1) {
	/* we are the master */
	int node=0;
	for (node = 0; node < dfg->node_count; node++) {
	    if (strcmp(dfg->nodes[node].name, node_name) == 0) {
		dfg->nodes[node].self = 1;
		break;
	    }
	}
	if (node == dfg->node_count) {
	    printf("Node \"%s\" not found in node list\n", node_name);
	    exit(1);
	}
	dfg->ready_condition = CMCondition_get(cm, NULL);
	dfg->shutdown_condition = CMCondition_get(cm, NULL);
	check_all_nodes_registered(dfg);
    } else {
	CMConnection conn = CMget_conn(cm, master_attrs);
	CMFormat register_msg = CMlookup_format(cm, EVdeploy_register_format_list);
	EVregister_msg msg;
	attr_list contact_list = CMget_contact_list(cm);
	char *my_contact_str;
	if (contact_list == NULL) {
	    CMlisten(cm);
	    contact_list = CMget_contact_list(cm);
	}

	my_contact_str = attr_list_to_string(contact_list);
	free_attr_list(contact_list);

	dfg->ready_condition = CMCondition_get(cm, conn);
	dfg->shutdown_condition = CMCondition_get(cm, conn);
	msg.node_name = node_name;
	msg.contact_string = my_contact_str;
	CMwrite(conn, register_msg, &msg);
	free(my_contact_str);
	dfg->master_connection = conn;
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
	    if ((!cur->bridge_stone) && (cur->node != target->node)) {
		cur->out_links[j] = create_bridge_stone(dfg, target);
		/* put the bridget stone where the source stone is */
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
    EVdeploy_stones_msg msg;
    CMFormat deploy_msg = CMlookup_format(dfg->cm, EVdfg_deploy_format_list);

    for (i=0; i< dfg->stone_count; i++) {
	if (dfg->stones[i]->node == node) {
	    stone_count++;
	}
    }
    if (stone_count == 0) return;
    msg.stone_count = stone_count;
    msg.stone_list = malloc(stone_count * sizeof(msg.stone_list[0]));
    j = 0;
    for (i=0; i< dfg->stone_count; i++) {
	if (dfg->stones[i]->node == node) {
	    deploy_msg_stone mstone = &msg.stone_list[j];
	    EVdfg_stone dstone = dfg->stones[i];
	    int k;
	    mstone->global_stone_id = dstone->stone_id;
	    mstone->out_count = dstone->out_count;
	    mstone->out_links = malloc(sizeof(mstone->out_links[0])*mstone->out_count);
	    for (k=0; k< dstone->out_count; k++) {
		mstone->out_links[k] = dstone->out_links[k]->stone_id;
	    }
	    mstone->action = dstone->action;
	    mstone->extra_actions = 0;
	    mstone->xactions = NULL;
	    j++;
	}
    }
    if (dfg->nodes[node].conn) {
	CMwrite(dfg->nodes[node].conn, deploy_msg, &msg);
    } else {
	dfg_deploy_handler(dfg->cm, NULL, &msg, NULL, NULL);
    }
    for(i=0 ; i < msg.stone_count; i++) {
	free(msg.stone_list[i].out_links);
    }
    free(msg.stone_list);
}

static void
perform_deployment(EVdfg dfg)
{
    int i;
    assign_stone_ids(dfg);
    add_bridge_stones(dfg);
    for (i=0; i < dfg->node_count; i++) {
	deploy_to_node(dfg, i);
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
	    CMwrite(dfg->nodes[i].conn, ready_msg, &msg);
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
signal_shutdown(EVdfg dfg, int value)
{
    int i;
    CMFormat shutdown_msg = CMlookup_format(dfg->cm, EVdfg_shutdown_format_list);
    EVshutdown_msg msg;
    msg.value = value;
    CMtrace_out(cm, EVerbose, "DFG shutdown with value %d\n", value);
    for (i=0; i < dfg->node_count; i++) {
	if (dfg->nodes[i].conn != NULL) {
	    CMwrite(dfg->nodes[i].conn, shutdown_msg, &msg);
	    CMtrace_out(cm, EVerbose, "DFG shutdown message sent to client \"%s\"\n", dfg->nodes[i].name);
	} else {
	    if (!dfg->nodes[i].self) {
		printf("Failure, no connection, not self, node %d\n", i);
		exit(1);
	    }
	}
    }
    dfg->shutdown_value = value;
    CMCondition_signal(dfg->cm, dfg->shutdown_condition);
    CMtrace_out(cm, EVerbose, "Master DFG shutdown\n");
}

static void
check_all_nodes_registered(EVdfg dfg)
{
    int i;
    for(i=0; i<dfg->node_count; i++) {
	if (!dfg->nodes[i].self && (dfg->nodes[i].conn == NULL)) {
	    return;
	}
    }
    perform_deployment(dfg);
    signal_ready(dfg);
}

