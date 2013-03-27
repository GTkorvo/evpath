#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "evpath.h"
#include "ev_dfg.h"
#include "test_support.h"

static int status;
static EVdfg test_dfg;

static
int
simple_handler(CManager cm, void *vevent, void *client_data, attr_list attrs)
{
    simple_rec_ptr event = vevent;
    (void)cm;
    (void)client_data;
    checksum_simple_record(event, attrs, quiet);
    EVdfg_shutdown(test_dfg, 0);
    return 0;
}

static int node_count = 3;
static EVdfg_stone src;

static void
join_handler(EVdfg dfg, char *identifier, void* available_sources, void *available_sinks)
{
    static int client_count = 1;
    int i;
    char *canon_name = malloc(20);
    EVdfg_stone last, tmp, sink;
    (void) available_sources;
    (void) available_sinks;
    if (client_count < node_count) {
	sprintf(canon_name, "client%d", client_count);
    } else {
	canon_name = strdup("terminal");
    }
    EVdfg_assign_canonical_name(dfg, identifier, canon_name);

    
    if (client_count < node_count) {
	/* increment the count and wait for the others to join */
	client_count++;
	return;
    }

    /* the last node has joined, finish the DFG */
    last = src;

    EVdfg_assign_node(src, "origin");
    for (i=1; i < node_count -1; i++) {
	char str[10];
	tmp = EVdfg_create_stone(dfg, NULL);
	EVdfg_link_port(last, 0, tmp);
	sprintf(str, "client%d", i);
	EVdfg_assign_node(tmp, str);
	last = tmp;
    }
    sink = EVdfg_create_sink_stone(dfg, "simple_handler");
    EVdfg_link_port(last, 0, sink);
    EVdfg_assign_node(sink, "terminal");

    EVdfg_realize(dfg);
}


extern int
be_test_master(int argc, char **argv)
{
    char **nodes;
    CManager cm;
    char *str_contact;
    EVsource source_handle;
    int i;

    if (argc == 1) {
	sscanf(argv[0], "%d", &node_count);
    }

    cm = CManager_create();
    CMlisten(cm);

/*
**  LOCAL DFG SUPPORT   Sources and sinks that might or might not be utilized.
*/

    source_handle = EVcreate_submit_handle(cm, -1, simple_format_list);
    EVdfg_register_source("master_source", source_handle);
    EVdfg_register_sink_handler(cm, "simple_handler", simple_format_list,
				(EVSimpleHandlerFunc) simple_handler);

/*
**  DFG CREATION
*/
    test_dfg = EVdfg_create(cm);
    str_contact = EVdfg_get_contact_list(test_dfg);
    EVdfg_node_join_handler(test_dfg, (EVdfgJoinHandlerFunc)join_handler);
    src = EVdfg_create_source_stone(test_dfg, "master_source");

/* We're node 0 in the DFG */
    EVdfg_join_dfg(test_dfg, "origin", str_contact);

/* Fork the others */
    nodes = malloc(sizeof(nodes[0])*(node_count+1));
    for (i=1; i < node_count; i++) {
	nodes[i] = strdup("client");
    }
    nodes[node_count] = NULL;
    test_fork_children(&nodes[0], str_contact);

    if (EVdfg_ready_wait(test_dfg) != 1) {
	/* dfg initialization failed! */
	exit(1);
    }

    
    if (EVdfg_active_sink_count(test_dfg) == 0) {
	EVdfg_ready_for_shutdown(test_dfg);
    }

    if (EVdfg_source_active(source_handle)) {
	simple_rec rec;
	generate_simple_record(&rec);
	/* submit would be quietly ignored if source is not active */
	EVsubmit(source_handle, &rec, NULL);
    }

    EVfree_source(source_handle);
    status = EVdfg_wait_for_shutdown(test_dfg);

    wait_for_children(nodes);
    for (i=1; i < node_count; i++) {
	free(nodes[i]);
    }
    free(nodes);
    free(str_contact);
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
    test_dfg = EVdfg_create(cm);

    src = EVcreate_submit_handle(cm, -1, simple_format_list);
    EVdfg_register_source("master_source", src);
    EVdfg_register_sink_handler(cm, "simple_handler", simple_format_list,
				(EVSimpleHandlerFunc) simple_handler);
    EVdfg_join_dfg(test_dfg, argv[1], argv[2]);
    EVdfg_ready_wait(test_dfg);

    if (EVdfg_active_sink_count(test_dfg) == 0) {
	EVdfg_ready_for_shutdown(test_dfg);
    }

    if (EVdfg_source_active(src)) {
	simple_rec rec;
	generate_simple_record(&rec);
	/* submit would be quietly ignored if source is not active */
	EVsubmit(src, &rec, NULL);
    }
    EVfree_source(src);
    return EVdfg_wait_for_shutdown(test_dfg);
}
