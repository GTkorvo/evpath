#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>

#include "config.h"
#include "evpath.h"
#include "ev_deploy.h"
#include "test_support.h"

static int status;
static EVdfg test_dfg;
static int base=2;

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
    char *chandle;
    EVdfg_stone *last, *tmp;
    EVsource source_handle;
    int level_count = 3, node_count, last_row_size;
    int ndig;
    int i,j,nbase,n;
    int fanout=1;

    if (argc == 1) {
	sscanf(argv[0], "%d", &level_count);
    }
    node_count = pow(base,level_count) -1;
    last_row_size = pow(base,level_count - 1);
    ndig = (int) (level_count*log10((double)base)) +1;

    nodes = malloc(sizeof(nodes[0]) * (node_count+1));
    for (i=0; i < node_count; i++) {
	nodes[i] = malloc(ndig+2);
	sprintf(nodes[i], "N%d", i+1);
    }
    cm = CManager_create();
    CMlisten(cm);
    contact_list = CMget_contact_list(cm);
    str_contact = attr_list_to_string(contact_list);

/*
**  LOCAL DFG SUPPORT   Sources and sinks that might or might not be utilized.
*/

    if (fanout) {
      source_handle = EVcreate_submit_handle(cm, -1, simple_format_list);
      EVdfg_register_source("master_source", source_handle);
    } else {
      EVdfg_register_sink_handler(cm, "simple_handler", simple_format_list,
				  (EVSimpleHandlerFunc) simple_handler);
    }

/*
**  DFG CREATION
*/
    test_dfg = EVdfg_create(cm);
    EVdfg_register_node_list(test_dfg, &nodes[0]);
    if (fanout) {

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
	  EVdfg_link_port(last[j/base], 0, tmp[j]);
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
	EVdfg_link_port(last[j/base],0,tmp[j]);
	EVdfg_assign_node(tmp[j], nodes[n-1]);
      }
   
      EVdfg_realize(test_dfg);

/* We're node 0 in the DFG */
      EVdfg_join_dfg(test_dfg, nodes[0], str_contact);

    }
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
    sprintf(chandle,"handler%s", argv[1]);
    EVdfg_register_sink_handler(cm,chandle, simple_format_list,
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
