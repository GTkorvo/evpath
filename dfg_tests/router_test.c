#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <time.h>

#include "config.h"
#include "ev_dfg.h"
#include "test_support.h"

static int status;
static EVdfg test_dfg;

static
int
simple_handler(CManager cm, void *vevent, void *client_data, attr_list attrs)
{
    static int handle_count = 0;
    simple_rec_ptr event = vevent;
    (void)cm;
    (void)client_data;
    handle_count++;
    if (!quiet) {
	printf("Got %d handles, waiting on %d\n", handle_count, event->integer_field);
    }
    if (event->integer_field == handle_count) {
	EVdfg_shutdown(test_dfg, 0);
    }
    return 0;
}


static char *router_function = "\
{\n\
    static int count = 0;\n\
    return (count++) % EVmax_output();\n\
}\0\0";

extern int
be_test_master(int argc, char **argv)
{
    char **nodes;
    CManager cm;
    char *str_contact;
    char *chandle;
    EVdfg_stone source, router;
    EVsource source_handle;
    int out_count, node_count, last_row_size;
    int ndig;
    int i;
    int repeat_count = 40;
    char *router_action;

    srand48(time(NULL));
    out_count = lrand48() % 4 + 2;
    if (argc == 1) {
	sscanf(argv[0], "%d", &out_count);
    }
    if (!quiet) {
	printf("Running with out_count = %d\n", out_count);
    }
    repeat_count = ((int)(repeat_count / out_count) + 1) * out_count;

    node_count = out_count + 1;

    nodes = malloc(sizeof(nodes[0]) * (node_count+1));
    for (i=0; i < node_count; i++) {
	nodes[i] = malloc(ndig+2);
	sprintf(nodes[i], "N%d", i+1);
    }
    nodes[node_count] = NULL;
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
    EVdfg_register_node_list(test_dfg, &nodes[0]);


    router_action = create_router_action_spec(simple_format_list, router_function);

    source = EVdfg_create_source_stone(test_dfg, "master_source");
    EVdfg_assign_node(source, nodes[0]);

    router = EVdfg_create_stone(test_dfg, router_action);
    EVdfg_assign_node(router, nodes[0]);

    EVdfg_link_port(source, 0, router);

    for (i=1; i < node_count; i++) {
	EVdfg_stone terminal = EVdfg_create_sink_stone(test_dfg,"simple_handler");
	EVdfg_link_port(router, i-1, terminal);
	EVdfg_assign_node(terminal, nodes[i]);
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
	int i;
	simple_rec rec;
	generate_simple_record(&rec);
	for (i=0 ; i < repeat_count; i++) {
	    /* encode shutdown in fields */
	    rec.integer_field = repeat_count / out_count;
	    EVsubmit(source_handle, &rec, NULL);
	}
    }

    status = EVdfg_wait_for_shutdown(test_dfg);
    free(str_contact);
    EVfree_source(source_handle);
    wait_for_children(nodes);

    CManager_close(cm);
    for (i=0; i < node_count; i++) {
	free(nodes[i]);
    }
    free(nodes);
    return status;
}


extern int
be_test_child(int argc, char **argv)
{
    CManager cm;
    EVsource src;
    char *chandle;

    cm = CManager_create();
    if (argc != 3) {
	printf("Child usage:  evtest  <nodename> <mastercontact>\n");
	exit(1);
    }
    test_dfg = EVdfg_create(cm);

    src = EVcreate_submit_handle(cm, -1, simple_format_list);
    EVdfg_register_source("master_source", src);
    chandle = malloc(sizeof(char)*(strlen(argv[1]) + 9));
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
	/* submit will be quietly ignored if source is not active */
	EVsubmit(src, &rec, NULL);
    }
    EVfree_source(src);
    return EVdfg_wait_for_shutdown(test_dfg);
}
