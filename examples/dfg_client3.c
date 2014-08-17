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

EVdfg_client test_client;

static int
simple_handler(CManager cm, void *vevent, void *client_data, attr_list attrs)
{
    simple_rec_ptr event = vevent;
    printf("I got %d\n", event->integer_field);
    EVdfg_shutdown(test_client, event->integer_field == 318);
    return 1;
}

/* this file is evpath/examples/dfg_client3.c */
int main(int argc, char **argv)
{
    CManager cm;
    char *str_contact;
    EVdfg_stone src, sink;
    EVsource source_handle;

    (void)argc; (void)argv;
    cm = CManager_create();
    CMlisten(cm);

/*
**  LOCAL DFG SUPPORT   Sources and sinks that might or might not be utilized.
*/

    source_handle = EVcreate_submit_handle(cm, -1, simple_format_list);
    EVdfg_register_source("event source", source_handle);
    EVdfg_register_sink_handler(cm, "simple_handler", simple_format_list,
				(EVSimpleHandlerFunc) simple_handler, NULL);

    /* We're node argv[1] in the process set, contact list is argv[2] */
    test_client = EVdfg_assoc_client(cm, argv[1], argv[2]);

    if (EVdfg_ready_wait(test_client) != 1) {
	/* dfg initialization failed! */
	exit(1);
    }

    
    if (EVdfg_source_active(source_handle)) {
	simple_rec rec;
	rec.integer_field = 318;
	/* submit would be quietly ignored if source is not active */
	EVsubmit(source_handle, &rec, NULL);
    }

/*! [Shutdown code] */
    if (EVdfg_active_sink_count(test_client) > 0) {
	/* if there are active sinks, the handler will call EVdfg_shutdown() */
    } else {
	if (EVdfg_source_active(source_handle)) {
	    /* we had a source and have already submitted, indicate success */
	    EVdfg_shutdown(test_client, 0 /* success */);
	} else {
	    /* we had neither a source or sink, ready to shutdown, no opinion */
	    EVdfg_ready_for_shutdown(test_client);
	}
    }

    return(EVdfg_wait_for_shutdown(test_client));
/*! [Shutdown code] */
}
