#include <stdio.h>
#include <stdlib.h>

#include <strings.h>
#include <unistd.h>
#include "config.h"
#include "evpath.h"
#include "ev_dfg.h"
#include "test_support.h"

static int status;
static EVdfg test_dfg;

static char *filter_func = "{\n\
int hop_count;\n\
hop_count = attr_ivalue(event_attrs, \"hop_count_atom\");\n\
hop_count++;\n\
set_int_attr(event_attrs, \"hop_count_atom\", hop_count);\n\
}\0\0";

#define REPEAT_COUNT 100

#include "ev_dfg_internal.h"
void
on_failure()
{
    int i;
    printf("In failure\n");
    for (i=0; i < test_dfg->node_count; i++) {
	printf("NODE %d status is :", i);
	switch (test_dfg->nodes[i].shutdown_status_contribution) {
	case STATUS_UNDETERMINED:
	    printf("NOT READY FOR SHUTDOWN\n");
	    break;
	case STATUS_NO_CONTRIBUTION:
	    printf("READY for shutdown, no status\n");
	    break;
	case STATUS_SUCCESS:
	    printf("READY for shutdown, SUCCESS\n");
	    break;
	default:
	    printf("READY for shutdown, FAILURE %d\n",
			test_dfg->nodes[i].shutdown_status_contribution);
	    break;
	}	    
    }
}

static
int
simple_handler(CManager cm, void *vevent, void *client_data, attr_list attrs)
{
    simple_rec_ptr event = vevent;
    static int count = 0;
    (void)cm;
    (void)client_data;
    int hop_count;
    atom_t hop_count_atom = -1;
    if (hop_count_atom == -1) {
	hop_count_atom = attr_atom_from_string("hop_count_atom");
    }
    get_int_attr(attrs, hop_count_atom, &hop_count);
    checksum_simple_record(event, attrs, quiet);
    count++;
    if (count == REPEAT_COUNT) {
        EVdfg_shutdown(test_dfg, 0);
    } else {
//        printf("."); fflush(stdout);
    }
    if (!quiet) printf("\nreceived had %d hops\n", hop_count);
    return 0;
}


int node_count = 3;
char **nodes;
EVdfg_stone *stones;

extern void
fail_handler(EVdfg dfg, char *failed_node_name, int failed_stone)
{
    int i;
    int failed_node = -1;
    int dest_node;
    EVdfg_stone dest_stone;
    if (!quiet) printf("Master knows about the failure of node %s, stone %d\n", failed_node_name, failed_stone);
    for(i = 0; i < node_count; i++) {
	if (!nodes) {
	    printf("Why is NODES NULL?\n");
	}
	if (nodes[i] && (strcmp(nodes[i], failed_node_name) == 0)) {
	    failed_node = i;
	    nodes[i] = NULL;
	    stones[i] = NULL;
	    if (!quiet)printf("Failed node is %d\n", failed_node);
	}
    }
    if (failed_node == -1) return;  /* repeat notification */

    dest_node = failed_node+1;
    while ((dest_stone = stones[dest_node]) == NULL) dest_node++;
    
    EVdfg_reconfig_link_port(stones[failed_node-1], 0, dest_stone, NULL);
    if (!quiet) printf("Linking stone %p on node %d to stone on node %d\n", stones[failed_node+1], failed_node-1, dest_node);
    EVdfg_realize(dfg);
}

extern int
be_test_master(int argc, char **argv)
{
    CManager cm;
    char *str_contact;
    EVdfg_stone src, last, tmp, sink;
    EVsource source_handle;
    int i;
    char *filter;

    alarm(240);  /* reset time limit to 4 minutes */
    if (argc == 1) {
	sscanf(argv[0], "%d", &node_count);
    }
    on_exit_handler = on_failure;
    nodes = malloc(sizeof(nodes[0]) * (node_count+1));
    stones = malloc(sizeof(stones[0]) * (node_count+1));
    for (i=0; i < node_count; i++) {
	nodes[i] = malloc(5);
	if (i == ( node_count / 2 )) {
	    sprintf(nodes[i], "D%d", i);  /* this one will die early */
	} else {
	    sprintf(nodes[i], "N%d", i);
	}
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
    EVdfg_node_fail_handler(test_dfg, fail_handler);
    EVdfg_register_node_list(test_dfg, &nodes[0]);
    src = EVdfg_create_source_stone(test_dfg, "master_source");
    EVdfg_assign_node(src, nodes[0]);

    stones[0] = last = src;
    filter = create_filter_action_spec(NULL, filter_func);

    for (i=1; i < node_count -1; i++) {
	stones[i] = tmp = EVdfg_create_stone(test_dfg, filter);
	EVdfg_link_port(last, 0, tmp);
	EVdfg_assign_node(tmp, nodes[i]);
	last = tmp;
    }
    sink = EVdfg_create_sink_stone(test_dfg, "simple_handler");
    stones[node_count-1] = sink;
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

    
    if (EVdfg_active_sink_count(test_dfg) == 0) {
	EVdfg_ready_for_shutdown(test_dfg);
    }

    if (EVdfg_source_active(source_handle)) {
        int i = 0;
	for (i=0 ; i < REPEAT_COUNT; i++) {
	    simple_rec rec;
	    atom_t hop_count_atom;
	    attr_list attrs = create_attr_list();
	    hop_count_atom = attr_atom_from_string("hop_count_atom");
	    add_int_attr(attrs, hop_count_atom, 1);
	    generate_simple_record(&rec);
	    /* submit would be quietly ignored if source is not active */
	    EVsubmit(source_handle, &rec, attrs);
	    CMsleep(cm, 1);
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
    int die_early = 0;

    alarm(240);   /* reset time limit to 4 minutes */
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

    if (argv[1][0] == 'D') {
      	/* Only the good die young */
	die_early++;
    }
    if (EVdfg_source_active(src)) {
	simple_rec rec;
	generate_simple_record(&rec);
	/* submit would be quietly ignored if source is not active */
	EVsubmit(src, &rec, NULL);
    }
    if (die_early) {
      CMsleep(cm, 45);
      printf("Node %s exiting early\n", argv[1]);
	exit(0);
    } else {
	return EVdfg_wait_for_shutdown(test_dfg);
    }
    return 0;
}
