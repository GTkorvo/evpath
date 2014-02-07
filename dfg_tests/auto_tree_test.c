#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <unistd.h>
#include <string.h>

#include "config.h"
#include "cod.h"
#include "ev_dfg.h"
#include "test_support.h"

static int status;
static EVdfg test_dfg;
static int base=2;

static int node_count = 0;

static
int
simple_handler(CManager cm, void *vevent, void *client_data, attr_list attrs)
{
    simple_rec_ptr event = vevent;
    (void)cm;
    (void)client_data;
    static int count = 0;
    count++;
    if (count == node_count) {
	EVdfg_shutdown(test_dfg, 0);
    }
    return 0;
}

int generate_record() {
    return getpid();
}
static cod_extern_entry externs[] = { 
    {"generate_record", (void *) (long) generate_record},
    {NULL, NULL}
};
static char extern_string[] = "int generate_record();\0\0";

char *COD_generate = "{\n\
    output.integer_field = generate_record();\n\
    return 1;\n\
}";

extern int
be_test_master(int argc, char **argv)
{
    char **nodes = NULL;
    CManager cm;
    char *str_contact;
    char *chandle = NULL;
    EVdfg_stone *last = NULL, *tmp = NULL;
    int level_count = 3, last_row_size;
    int ndig = 3;
    int i,j,nbase,n;

    if (argc == 1) {
	sscanf(argv[0], "%d", &level_count);
    }
    node_count = (int) pow(base,level_count) -1;
    last_row_size = (int)pow(base,level_count - 1);
    nodes = malloc(sizeof(nodes[0]) * (node_count+1));
    
    for (i=0; i < node_count; i++) {
	nodes[i] = malloc(ndig+2);
	sprintf(nodes[i], "N%d", i+1);
    }
    nodes[node_count] = NULL;
    cm = CManager_create();
    EVadd_standard_routines(cm, extern_string, externs);
    CMlisten(cm);
    
/*
**  LOCAL DFG SUPPORT   Sources and sinks that might or might not be utilized.
*/
    
    EVdfg_register_sink_handler(cm, "simple_handler", simple_format_list,
				(EVSimpleHandlerFunc) simple_handler);
    
/*
**  DFG CREATION
*/
    test_dfg = EVdfg_create(cm);
    str_contact = EVdfg_get_contact_list(test_dfg);
    EVdfg_register_node_list(test_dfg, &nodes[0]);
    if (0) {
	
	/* create arrays for storing tmp & last*/
	last = malloc(last_row_size*sizeof(EVdfg_stone));
	tmp = malloc(last_row_size*sizeof(EVdfg_stone));
	
	last[0] = EVdfg_create_source_stone(test_dfg, "master_source");
	EVdfg_assign_node(last[0], nodes[0]);
	nbase = base;
	
	for (i=2; i < level_count; i++) {
	    for (j=0; j<nbase; j++) {
		// nbase = pow(2,(i-1))
		n = nbase + j;
		tmp[j] = EVdfg_create_stone(test_dfg,NULL);
		EVdfg_link_port(last[j/base], j%2 , tmp[j]);
		EVdfg_assign_node(tmp[j], nodes[n-1]);
	    }
	    for (j=0; j<nbase; j++) {
		last[j] = tmp[j];
	    }
	    nbase *= base;
	}
	/* Now for the last row*/
	chandle = malloc(sizeof(char)*(ndig + 10));
	for (j=0; j<nbase; j++) {
	    n = nbase+j;
	    sprintf(chandle,"handlerN%d", n);
	    EVdfg_register_sink_handler(cm,chandle,simple_format_list, 
					(EVSimpleHandlerFunc) simple_handler);
	    tmp[j] = EVdfg_create_sink_stone(test_dfg,chandle);
	    EVdfg_link_port(last[j/base],j%2 ,tmp[j]);
	    EVdfg_assign_node(tmp[j], nodes[n-1]);
	}
    } else {
	/* fanin */
	EVdfg_stone sink;
	EVdfg_register_sink_handler(cm,"master_sink",simple_format_list, 
				    (EVSimpleHandlerFunc) simple_handler);
	sink = EVdfg_create_sink_stone(test_dfg,"master_sink");
	EVdfg_assign_node(sink, nodes[0]);
	char *action_spec = create_transform_action_spec(NULL,simple_format_list,COD_generate);
	for(i=0; i < node_count; i++) {
	    EVdfg_stone autos;
	    autos = EVdfg_create_stone(test_dfg, strdup(action_spec));
	    EVdfg_assign_node(autos, nodes[i]);
	    EVdfg_enable_auto_stone(autos, 1, 0);
	    EVdfg_link_port(autos, 0, sink); 
	}
	free(action_spec);
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

    status = EVdfg_wait_for_shutdown(test_dfg);
    free(str_contact);
    wait_for_children(nodes);

    CManager_close(cm);
    for (i=0; i < node_count; i++) {
	free(nodes[i]);
    }
    free(nodes);
    if (last) free(last);
    if (tmp) free(tmp);
    if (chandle) free(chandle);
    return status;
}


extern int
be_test_child(int argc, char **argv)
{
    CManager cm;
    EVsource src;
    char *chandle;

    cm = CManager_create();
    EVadd_standard_routines(cm, extern_string, externs);
    if (argc != 3) {
	printf("Child usage:  evtest  <nodename> <mastercontact>\n");
	exit(1);
    }
    test_dfg = EVdfg_create(cm);

    src = EVcreate_submit_handle(cm, -1, simple_format_list);
    EVdfg_register_source("master_source", src);
    chandle = malloc(sizeof(char)*(strlen(argv[1]) + 9));
    sprintf(chandle,"handler%s", argv[1]);
    EVdfg_register_sink_handler(cm,chandle, simple_format_list,
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
