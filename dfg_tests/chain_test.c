#include <stdio.h>
#include <stdlib.h>

#include "config.h"
#include "evpath.h"
#include "ev_deploy.h"
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


extern int
be_test_master(int argc, char **argv)
{
    char **nodes;
    CManager cm;
    attr_list contact_list;
    char *str_contact;
    EVdfg_stone src, last, tmp, sink;
    EVsource source_handle;
    int node_count = 3;
    int i;

    if (argc == 1) {
	sscanf(argv[0], "%d", &node_count);
    }
    nodes = malloc(sizeof(nodes[0]) * (node_count+1));
    for (i=0; i < node_count; i++) {
	nodes[i] = malloc(5);
	sprintf(nodes[i], "N%d", i);
    }
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
    EVdfg_register_node_list(test_dfg, &nodes[0]);
    src = EVdfg_create_source_stone(test_dfg, "master_source");
    EVdfg_assign_node(src, nodes[0]);

    last = src;

    for (i=1; i < node_count -1; i++) {
	tmp = EVdfg_create_stone(test_dfg, NULL);
	EVdfg_link_port(last, 0, tmp);
	EVdfg_assign_node(tmp, nodes[i]);
	last = tmp;
    }
    sink = EVdfg_create_sink_stone(test_dfg, "simple_handler");
    EVdfg_link_port(last, 0, sink);
    EVdfg_assign_node(sink, nodes[node_count-1]);

    EVdfg_realize(test_dfg);

/* We're node 0 in the DFG */
    EVdfg_join_dfg(test_dfg, nodes[0], str_contact);

/* Fork the others */
    test_fork_children(&nodes[0], str_contact);

    if (EVdfg_ready_wait(test_dfg) != 1) {
	/* dfg initialization failed! */
	exit(1);
    }

    
    {
	simple_rec rec;
	generate_simple_record(&rec);
	/* submit will be quietly ignored if source is not active */
	EVsubmit(source_handle, &rec, NULL);
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
    {
	simple_rec rec;
	generate_simple_record(&rec);
	/* submit will be quietly ignored if source is not active */
	EVsubmit(src, &rec, NULL);
    }
    return EVdfg_wait_for_shutdown(test_dfg);
}
