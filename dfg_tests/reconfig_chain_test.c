#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "evpath.h"
#include "ev_dfg.h"
#include "test_support.h"

static int status;
static EVdfg_client test_client;
const int reconfig_node_count = 1;

char *str_contact;
char **reconfig_list = NULL;

#define EVENT_COUNT 30

CManager cm;

static
int
simple_handler(CManager cm, void *vevent, void *client_data, attr_list attrs)
{
    simple_rec_ptr event = vevent;
    (void)cm;
    (void)client_data;
    static int count;
    int hop_count;
    atom_t hop_count_atom = -1;
    if (hop_count_atom == -1) {
	hop_count_atom = attr_atom_from_string("hop_count_atom");
    }
    get_int_attr(attrs, hop_count_atom, &hop_count);
    checksum_simple_record(event, attrs, quiet);
    if (++count == EVENT_COUNT) {
	if (hop_count == reconfig_node_count + 1) {
	    EVdfg_shutdown(test_client, 0);
	} else {
	    printf("Final event didn't have the required number of hops\n");
	    EVdfg_shutdown(test_client, 1);
	}
    }
    if (!quiet) {
	printf("received count = %d, last had %d hops\n", count, hop_count);
    }
    return 0;
}

static int static_node_count = 2;

/*
 pprabhu:
 */

static char *filter_func = "{\n\
int hop_count;\n\
hop_count = attr_ivalue(event_attrs, \"hop_count_atom\");\n\
hop_count++;\n\
set_int_attr(event_attrs, \"hop_count_atom\", hop_count);\n\
return 1;\n\
}\0\0";

static void
join_handler(EVdfg_master master, char *identifier, void* available_sources, void *available_sinks)
{
    static int client_count = 0;
    int i;
    char *canon_name;
    EVdfg_stone last, tmp, sink;
    static EVdfg_stone src;
    static int graph_already_realized = 0;
    (void) available_sources;
    (void) available_sinks;
    static EVdfg dfg = NULL;

    client_count++;
    if (!graph_already_realized) {
	if (strcmp(identifier, "origin") == 0) {
	    canon_name = strdup("origin");
	} else if (client_count < static_node_count) {
	    sprintf(canon_name, "client%d", client_count-1);
	} else {
	    canon_name = strdup("terminal");
	}
	EVdfg_assign_canonical_name(master, identifier, canon_name);
	free(canon_name);
    
	if (client_count < static_node_count) {
	    return;
	}

	/* the last node has joined, finish the DFG */
	dfg = EVdfg_create(master);
	src = EVdfg_create_source_stone(dfg, "master_source");
	
	last = src;

	EVdfg_assign_node(src, "origin");
	for (i=1; i < static_node_count -1; i++) {
	    char str[10];
	    char *filter;
	    filter = create_filter_action_spec(NULL, filter_func);
	    tmp = EVdfg_create_stone(dfg, filter);
	    EVdfg_link_port(last, 0, tmp);
	    sprintf(str, "client%d", i);
	    EVdfg_assign_node(tmp, str);
	    last = tmp;
	}
	sink = EVdfg_create_sink_stone(dfg, "simple_handler");
	EVdfg_link_port(last, 0, sink);
	EVdfg_assign_node(sink, "terminal");

	EVdfg_realize(dfg);
	graph_already_realized++;
    } else {
	char *filter;
	EVdfg_stone middle_stone;
	filter = create_filter_action_spec(NULL, filter_func);
	canon_name = malloc(20);
	sprintf(canon_name, "client%d", client_count-1);
	EVdfg_assign_canonical_name(master, identifier, canon_name);

	middle_stone = EVdfg_create_stone(dfg, filter);
	EVdfg_assign_node(middle_stone, canon_name);
		
	free(canon_name);
	    //      EVdfg_reconfig_link_port_to_stone(dfg, stone_index, 0, middle_stone, NULL);
	    //      EVdfg_reconfig_link_port_from_stone(dfg, middle_stone, 0, 2, NULL);
		
	printf("Doing reconfig\n");
	EVdfg_reconfig_insert_on_port(dfg, src, 0, middle_stone, NULL);
	EVdfg_realize(dfg);
    }
}

extern int
be_test_master(int argc, char **argv)
{
    char **nodes;
	//    CManager cm;
    attr_list contact_list;
	//    char *str_contact;
    EVsource source_handle;
    atom_t Hop_count_atom;
    int i;
    EVdfg_master test_master;
    EVdfg test_dfg;
	
    if (argc == 1) {
	sscanf(argv[0], "%d", &static_node_count);
    }
	
    cm = CManager_create();
    CMlisten(cm);
    contact_list = CMget_contact_list(cm);
    str_contact = attr_list_to_string(contact_list);
    free_attr_list(contact_list);
	
	/*
	 **  LOCAL DFG SUPPORT   Sources and sinks that might or might not be utilized.
	 */
	
    source_handle = EVcreate_submit_handle(cm, -1, simple_format_list);
    EVdfg_register_source("master_source", source_handle);
    EVdfg_register_sink_handler(cm, "simple_handler", simple_format_list,
				(EVSimpleHandlerFunc) simple_handler, NULL);
	
	/*
	 **  MASTER AND DFG CREATION
	 */
    test_master = EVdfg_create_master(cm);
    EVdfg_node_join_handler(test_master, (EVdfgJoinHandlerFunc)join_handler);
	
	/* pprabhu: creating list of reconfiguration node names */
    reconfig_list = malloc(sizeof(reconfig_list[0]) * (reconfig_node_count + 2));
    for (i=0; i < (reconfig_node_count + 1); i++) {
        reconfig_list[i] = strdup("client");
    }
    reconfig_list[reconfig_node_count + 1] = NULL;

	/* We're node 0 in the DFG */
    test_client = EVdfg_assoc_client_local(cm, "origin", test_master);
	
	/* Fork the others */
    nodes = malloc(sizeof(nodes[0])*(static_node_count+1));
    for (i=1; i < static_node_count; i++) {
	nodes[i] = strdup("client");
    }
    nodes[static_node_count] = NULL;
    test_fork_children(&nodes[0], str_contact);
    delayed_fork_children(cm, &reconfig_list[0], str_contact, 5);

    if (EVdfg_ready_wait(test_client) != 1) {
	/* dfg initialization failed! */
	exit(1);
    }
	
    
    if (EVdfg_active_sink_count(test_client) == 0) {
	EVdfg_ready_for_shutdown(test_client);
    }
	
    Hop_count_atom = attr_atom_from_string("hop_count_atom");
    if (EVdfg_source_active(source_handle)) {
	simple_rec rec;
	memset(&rec, 0, sizeof(rec));
        for (i = 0; i < EVENT_COUNT; ++i) {
	    attr_list attrs = create_attr_list();
	    CMsleep(cm, 1);
	    generate_simple_record(&rec);
	    add_int_attr(attrs, Hop_count_atom, 1);
	    /* submit would be quietly ignored if source is not active */
	    EVsubmit(source_handle, &rec, attrs);
	    free_attr_list(attrs);
        }
    }
    
    status = EVdfg_wait_for_shutdown(test_client);
	
    wait_for_children(nodes);
    for (i=1; i < static_node_count; i++) {
	free(nodes[i]);
    }
    free(nodes);
    for (i=0; i < (reconfig_node_count + 1); i++) {
	free(reconfig_list[i]);
    }
    free(reconfig_list);
    free(str_contact);
	
    EVfree_source(source_handle);
    CManager_close(cm);
    return status;
}


extern int
be_test_child(int argc, char **argv)
{
    CManager cm;
    EVsource src;
	
	
    cm = CManager_create();
    if (argc != 3) {
	printf("Child usage:  evtest  <nodename> <mastercontact>\n");
	exit(1);
    }
	
    src = EVcreate_submit_handle(cm, -1, simple_format_list);
    EVdfg_register_source("master_source", src);
    EVdfg_register_sink_handler(cm, "simple_handler", simple_format_list,
				(EVSimpleHandlerFunc) simple_handler, NULL);
    test_client = EVdfg_assoc_client(cm, argv[1], argv[2]);
	
    EVdfg_ready_wait(test_client);
	
    if (EVdfg_active_sink_count(test_client) == 0) {
	EVdfg_ready_for_shutdown(test_client);
    }
	
    if (EVdfg_source_active(src)) {
	simple_rec rec;
	generate_simple_record(&rec);
	/* submit would be quietly ignored if source is not active */
	EVsubmit(src, &rec, NULL);
    }
    EVfree_source(src);
    return EVdfg_wait_for_shutdown(test_client);
}
