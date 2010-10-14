#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "ev_dfg.h"
#include "test_support.h"

static int status;
static EVdfg test_dfg;


typedef struct _rec_a {
    int a_field;
} rec_a, *rec_a_ptr;

typedef struct _rec_b {
    int b_field;
} rec_b, *rec_b_ptr;

typedef struct _rec_c {
    int c_field;
} rec_c, *rec_c_ptr;

static FMField a_field_list[] =
{
    {"a_field", "integer",
     sizeof(int), FMOffset(rec_a_ptr, a_field)},
    {NULL, NULL, 0, 0}
};

static FMField b_field_list[] =
{
    {"b_field", "integer",
     sizeof(int), FMOffset(rec_b_ptr, b_field)},
    {NULL, NULL, 0, 0}
};

static FMField c_field_list[] =
{
    {"c_field", "integer",
     sizeof(int), FMOffset(rec_c_ptr, c_field)},
    {NULL, NULL, 0, 0}
};

static FMStructDescRec a_format_list[] =
{
    {"a_rec", a_field_list, sizeof(rec_a), NULL},
    {NULL, NULL, 0, NULL}
};

static FMStructDescRec b_format_list[] =
{
    {"b_rec", b_field_list, sizeof(rec_b), NULL},
    {NULL, NULL, 0, NULL}
};

static FMStructDescRec c_format_list[] =
{
    {"c_rec", c_field_list, sizeof(rec_c), NULL},
    {NULL, NULL, 0, NULL}
};

static FMStructDescList queue_list[] = {a_format_list, b_format_list, c_format_list, NULL};

static
void
generate_a_record(rec_a_ptr event)
{
    /* always even */
    event->a_field = ((int) lrand48() % 50) * 2;
}

static
void
generate_b_record(rec_b_ptr event)
{
    /* always odd */
    event->b_field = ((int) lrand48() % 50) * 2 + 1;
}

static int repeat_count = 100;

static
int
output_handler(CManager cm, void *vevent, void *client_data, attr_list attrs)
{
    static int message_count = 0;
    rec_c_ptr event = vevent;
    (void)cm;
    if (event->c_field % 2 != 1) {
	printf("Received record should be odd, got %d\n", event->c_field);
    }
    if (quiet <= 0) {
	printf("In the handler, event data is :\n");
	printf("	c_field = %d\n", event->c_field);
	printf("Data was received with attributes : \n");
	if (attrs) dump_attr_list(attrs);
    }
    if (client_data != NULL) {
	int tmp = *((int *) client_data);
	*((int *) client_data) = tmp + 1;
    }
    message_count++;
    if (message_count == repeat_count/2) {
	EVdfg_shutdown(test_dfg, 0);
    }
    return 0;
}

static char *trans = "{\
    int found = 0;\
    a_rec *a;\
    b_rec *b;\
    c_rec c;\
    if (EVpresent(a_rec_ID, 0)) {\
        a = EVdata_a_rec(0); ++found;\
    }\
    if (EVpresent(b_rec_ID, 0)) {\
        b = EVdata_b_rec(0); ++found;\
    }\
    if (found == 2) {\
        c.c_field = a.a_field + b.b_field;\
        if (!EVpresent_b_rec(0))\
            printf(\"??? <1> not present (1)\\n\");\
        EVdiscard_a_rec(0);\
        if (!EVpresent_b_rec(0))\
            printf(\"??? <2> not present (1)\\n\");\
        EVdiscard_b_rec(0);\
        EVsubmit(0, c);\
    }\
}\0\0";

static void
data_free(void *event_data, void *client_data)
{
    (void) client_data;
    free(event_data);
}

extern int
be_test_master(int argc, char **argv)
{
    char *nodes[] = {"a", "b", "c", "d", NULL};
    CManager cm;
    attr_list contact_list;
    char *str_contact;
    EVdfg_stone srca, srcb, multiq, sink;
    EVsource a_handle, b_handle;
    char * q_action_spec;
    int count, i;
    char *map;

    (void)argc; (void)argv;
    cm = CManager_create();
    CMlisten(cm);
    contact_list = CMget_contact_list(cm);
    str_contact = attr_list_to_string(contact_list);

/*
**  LOCAL DFG SUPPORT   Sources and sinks that might or might not be utilized.
*/

    a_handle = EVcreate_submit_handle_free(cm, -1, a_format_list,
					   data_free, NULL);
    b_handle = EVcreate_submit_handle_free(cm, -1, b_format_list,
					   data_free, NULL);
    EVdfg_register_source("a_source", a_handle);
    EVdfg_register_source("b_source", b_handle);
    EVdfg_register_sink_handler(cm, "c_output_handler", c_format_list,
				(EVSimpleHandlerFunc) output_handler);
    EVdfg_register_sink_handler(cm, "a_output_handler", a_format_list,
				(EVSimpleHandlerFunc) output_handler);

/*
**  DFG CREATION
*/
    test_dfg = EVdfg_create(cm);
    EVdfg_register_node_list(test_dfg, &nodes[0]);

    srca = EVdfg_create_source_stone(test_dfg, "a_source");
    srcb = EVdfg_create_source_stone(test_dfg, "b_source");
    sink = EVdfg_create_sink_stone(test_dfg, "c_output_handler");
    q_action_spec = create_multityped_action_spec(queue_list, trans);
    multiq = EVdfg_create_stone(test_dfg, q_action_spec);
    EVdfg_link_port(srca, 0, multiq);
    EVdfg_link_port(srcb, 0, multiq);
    EVdfg_link_port(multiq, 0, sink);

    EVdfg_assign_node(srca, "a");
    EVdfg_assign_node(srcb, "b");
    EVdfg_assign_node(multiq, "c");
    EVdfg_assign_node(sink, "d");

    EVdfg_realize(test_dfg);

/* We're node 0 in the DFG */
    EVdfg_join_dfg(test_dfg, nodes[0], str_contact);

/* Fork the others */
    test_fork_children(&nodes[0], str_contact);

    if (EVdfg_ready_wait(test_dfg) != 1) {
	/* dfg initialization failed! */
	exit(1);
    }

    
    count = repeat_count;
    map = malloc(count);
    memset(map, 0, count);
    /* setup map so that it is half ones and half zeroes */
    for (i=0; i < count / 2 ; i++) {
	int j;
	int step = lrand48() % (count - i);
	int mark = 0;
	for (j = 0; j < step; j++) {
	    mark++;
	    while (map[mark] == 1) mark++;
	}
	map[mark] = 1;
    }
    for (i=0; i < count ; i++) {
	if (map[i] == 1) {
	    if (EVdfg_source_active(a_handle)) {
		rec_a_ptr a = malloc(sizeof(*a));
		generate_a_record(a);
		if (quiet <=0) {printf("submitting a -> %d\n", a->a_field);}
		EVsubmit(a_handle, a, NULL);
		}
	} else {
	    if (EVdfg_source_active(b_handle)) {
		rec_b_ptr b = malloc(sizeof(*b));
		generate_b_record(b);
		if (quiet <=0) {printf("submitting b -> %d\n", b->b_field);}
		EVsubmit(b_handle, b, NULL);
	    }
	}
    }
    CMsleep(cm, 1);

    if (EVdfg_active_sink_count(test_dfg) == 0) {
	EVdfg_ready_for_shutdown(test_dfg);
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
    EVsource a_handle, b_handle;
    int count, i;
    char *map;

    cm = CManager_create();
    if (argc != 3) {
	printf("Child usage:  evtest  <nodename> <mastercontact>\n");
	exit(1);
    }
    test_dfg = EVdfg_create(cm);

    EVdfg_register_sink_handler(cm, "c_output_handler", c_format_list,
				(EVSimpleHandlerFunc) output_handler);
    a_handle = EVcreate_submit_handle_free(cm, -1, a_format_list,
					   data_free, NULL);
    b_handle = EVcreate_submit_handle_free(cm, -1, b_format_list,
					   data_free, NULL);
    EVdfg_register_source("a_source", a_handle);
    EVdfg_register_source("b_source", b_handle);

    EVdfg_join_dfg(test_dfg, argv[1], argv[2]);
    EVdfg_ready_wait(test_dfg);

    if (EVdfg_active_sink_count(test_dfg) == 0) {
	EVdfg_ready_for_shutdown(test_dfg);
    }

    count = repeat_count;
    map = malloc(count);
    memset(map, 0, count);
    /* setup map so that it is half ones and half zeroes */
    for (i=0; i < count / 2 ; i++) {
	int j;
	int step = lrand48() % (count - i);
	int mark = 0;
	for (j = 0; j < step; j++) {
	    mark++;
	    while (map[mark] == 1) mark++;
	}
	map[mark] = 1;
    }
    for (i=0; i < count ; i++) {
	if (map[i] == 1) {
	    if (EVdfg_source_active(a_handle)) {
		rec_a_ptr a = malloc(sizeof(*a));
		generate_a_record(a);
		if (quiet <=0) {printf("submitting a -> %d\n", a->a_field);}
		EVsubmit(a_handle, a, NULL);
		}
	} else {
	    if (EVdfg_source_active(b_handle)) {
		rec_b_ptr b = malloc(sizeof(*b));
		generate_b_record(b);
		if (quiet <=0) {printf("submitting b -> %d\n", b->b_field);}
		EVsubmit(b_handle, b, NULL);
	    }
	}
    }
    return EVdfg_wait_for_shutdown(test_dfg);
}
