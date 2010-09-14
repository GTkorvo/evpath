#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "evpath.h"
#include "ev_deploy.h"
#include "test_support.h"

extern int quiet;
static int status;
static EVdfg test_dfg;
static int repeat_count = 10;

static
int
simple_handler(CManager cm, void *vevent, void *client_data, attr_list attrs)
{
    static int count = 0;
    simple_rec_ptr event = vevent;
    (void)cm;
    checksum_simple_record(event, attrs, quiet);
    count++;
    if (count == repeat_count) 
	EVdfg_shutdown(test_dfg, 0);
    return 0;
}


static FMField filter_field_list[] =
{
    {"integer_field", "integer",
     sizeof(int), FMOffset(simple_rec_ptr, integer_field)},
    {NULL, NULL, 0, 0}
};

static FMStructDescRec filter_format_list[] =
{
    {"simple", filter_field_list, sizeof(simple_rec), NULL},
    {NULL, NULL}
};

extern int
be_test_master(int argc, char **argv)
{
    char *nodes[] = {"a", "b", "c", NULL};
    CManager cm;
    attr_list contact_list;
    char *str_contact;
    EVdfg_stone src, filter, sink;
    EVsource source_handle;
    char *filter_action_spec;

    cm = CManager_create();
    CMlisten(cm);
    contact_list = CMget_contact_list(cm);
    str_contact = attr_list_to_string(contact_list);

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

    src = EVdfg_create_source_stone(test_dfg, "master_source");

    filter_action_spec = create_filter_action_spec(filter_format_list, "{int ret = input.long_field % 2;return ret;}\0\0");
    filter = EVdfg_create_stone(test_dfg, filter_action_spec);
    EVdfg_link_port(src, 0, filter);
    sink = EVdfg_create_sink_stone(test_dfg, "simple_handler");
    EVdfg_link_port(filter, 0, sink);

    if ((argc != 1) || ((argc == 1) && (strcmp(argv[0], "3") == 0))) {
	EVdfg_register_node_list(test_dfg, &nodes[0]);
	EVdfg_assign_node(src, "a");
	EVdfg_assign_node(filter, "b");
	EVdfg_assign_node(sink, "c");
    } else if (strcmp(argv[0], "2a") == 0) {
	nodes[2] = NULL;
	EVdfg_register_node_list(test_dfg, &nodes[0]);
	EVdfg_assign_node(src, "a");
	EVdfg_assign_node(filter, "a");
	EVdfg_assign_node(sink, "b");
    } else if (strcmp(argv[0], "2b") == 0) {
	nodes[2] = NULL;
	EVdfg_register_node_list(test_dfg, &nodes[0]);
	EVdfg_assign_node(src, "a");
	EVdfg_assign_node(filter, "b");
	EVdfg_assign_node(sink, "b");
    } else if (strcmp(argv[0], "1") == 0) {
	nodes[1] = NULL;
	EVdfg_register_node_list(test_dfg, &nodes[0]);
	EVdfg_assign_node(src, "a");
	EVdfg_assign_node(filter, "a");
	EVdfg_assign_node(sink, "a");
    }
	

    EVdfg_realize(test_dfg);

/* We're node 0 in the DFG */
    EVdfg_join_dfg(test_dfg, nodes[0], str_contact);

/* Fork the others */
    test_fork_children(&nodes[0], str_contact);

    if (EVdfg_ready_wait(test_dfg) != 1) {
	/* dfg initialization failed! */
	exit(1);
    }

    
    if (EVdfg_active_sink_count(test_dfg) == 0) {
	EVdfg_ready_for_shutdown(test_dfg);
    }

    if (EVdfg_source_active(source_handle)) {
	int count = repeat_count;
	while (count != 0) {
	    simple_rec rec;
	    generate_simple_record(&rec);
	    EVsubmit(source_handle, &rec, NULL);
	    if ((rec.long_field%2 == 1) && (count != -1)) {
		count--;
	    }
	}
    }
    status = EVdfg_wait_for_shutdown(test_dfg);

    wait_for_children(nodes);

    CManager_close(cm);
    return status;
}


extern int
be_test_child(int argc, char **argv)
{
    CManager cm;
    EVsource src;
    int i;

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
	for (i=0; i < 20 ; i++) {
	    simple_rec rec;
	    generate_simple_record(&rec);
	    EVsubmit(src, &rec, NULL);
	}
    }
    return EVdfg_wait_for_shutdown(test_dfg);
}
