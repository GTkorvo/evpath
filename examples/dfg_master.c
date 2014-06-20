#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "evpath.h"
#include "ev_dfg.h"

typedef struct _simple_rec {
    int integer_field;
} simple_rec, *simple_rec_ptr;

static FMField simple_field_list[] =
{
    {"integer_field", "integer", sizeof(int), FMOffset(simple_rec_ptr, integer_field)},
    {NULL, NULL, 0, 0}
};
static FMStructDescRec simple_format_list[] =
{
    {"simple", simple_field_list, sizeof(simple_rec), NULL},
    {NULL, NULL}
};

static int
simple_handler(CManager cm, void *vevent, void *client_data, attr_list attrs)
{
    simple_rec_ptr event = vevent;
    printf("I got %d\n", event->integer_field);
    return 1;
}

/* this file is evpath/examples/net_recv.c */
int main(int argc, char **argv)
{
    char *nodes[] = {"a", "b", NULL};
    CManager cm;
    char *str_contact;
    EVdfg test_dfg;
    EVdfg_stone src, sink;
    EVsource source_handle;

    (void)argc; (void)argv;
    cm = CManager_create();
    CMlisten(cm);

    test_dfg = EVdfg_create(cm);
    str_contact = EVdfg_get_contact_list(test_dfg);
    EVdfg_register_node_list(test_dfg, &nodes[0]);

/*
**  LOCAL DFG SUPPORT   Sources and sinks that might or might not be utilized.
*/

    source_handle = EVcreate_submit_handle(cm, -1, simple_format_list);
    EVdfg_register_source("event source", source_handle);
    EVdfg_register_sink_handler(cm, "simple_handler", simple_format_list,
				(EVSimpleHandlerFunc) simple_handler);

/*
**  DFG CREATION
*/

    src = EVdfg_create_source_stone(test_dfg, "event source");
    EVdfg_assign_node(src, "b");
    sink = EVdfg_create_sink_stone(test_dfg, "simple_handler");
    EVdfg_assign_node(sink, "a");
    EVdfg_link_port(src, 0, sink);

    EVdfg_realize(test_dfg);

    /* We're node "a" in the DFG */
    EVdfg_join_dfg(test_dfg, "a", str_contact);

    printf("Contact list is \"%s\"\n", str_contact);
    if (EVdfg_ready_wait(test_dfg) != 1) {
	/* dfg initialization failed! */
	exit(1);
    }

    
    if (EVdfg_source_active(source_handle)) {
	simple_rec rec;
	rec.integer_field = 318;
	/* submit would be quietly ignored if source is not active */
	EVsubmit(source_handle, &rec, NULL);
    }

    CMrun_network(cm);
}
