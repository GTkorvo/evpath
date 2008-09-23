#include "config.h"

#include <stdio.h>
#include <atl.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <arpa/inet.h>
#include "evpath.h"
#include "gen_thread.h"
#ifdef HAVE_WINDOWS_H
#include <windows.h>
#define drand48() (((double)rand())/((double)RAND_MAX))
#define lrand48() rand()
#define srand48(x)
#else
#include <sys/wait.h>
#endif

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
    {"a_rec", a_field_list},
    {NULL, NULL}
};

static FMStructDescRec b_format_list[] =
{
    {"b_rec", b_field_list},
    {NULL, NULL}
};

static FMStructDescRec c_format_list[] =
{
    {"c_rec", c_field_list},
    {NULL, NULL}
};

static FMStructDescList queue_list[] = {a_format_list, b_format_list, c_format_list, NULL};
    

static
void 
generate_a_record(event)
rec_a_ptr event;
{
    /* always even */
    event->a_field = ((int) lrand48() % 50) * 2;
}

static
void 
generate_b_record(event)
rec_b_ptr event;
{
    /* always odd */
    event->b_field = ((int) lrand48() % 50) * 2 + 1;
}

int quiet = 1;

static
int
output_handler(cm, vevent, client_data, attrs)
CManager cm;
void *vevent;
void *client_data;
attr_list attrs;
{
    rec_c_ptr event = vevent;
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
    return 0;
}

static int do_regression_master_test();
static int regression = 1;
static int repeat_count = 100;
static atom_t CM_TRANSPORT;
static atom_t CM_NETWORK_POSTFIX;
static atom_t CM_MCAST_ADDR;
static atom_t CM_MCAST_PORT;

/*
static char *trans = "{\
    c_rec c;\n\
    if ((input_queue[0].size < 1) || \n\
        (input_queue[1].size < 1)) return;\n\
    c.c_field = input_queue[0].events[0].data.a_field + \n\
		input_queue[1].events[0].data.a_field;\n\
    event_discard(input_queue[0].events[0]);\n\
    event_discard(input_queue[1].events[0]);\n\
    EVsubmit(0, c);\n\
    return;\n\
}\0\0";
*/

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
    free(event_data);
}

int
main(argc, argv)
int argc;
char **argv;
{
    CManager cm;
    int regression_master = 1;

    /* XXX for testing */ setbuf(stdout, NULL);

    while (argv[1] && (argv[1][0] == '-')) {
	if (argv[1][1] == 'c') {
	    regression_master = 0;
	} else if (argv[1][1] == 's') {
	    regression_master = 0;
	} else if (argv[1][1] == 'q') {
	    quiet++;
	} else if (argv[1][1] == 'v') {
	    quiet--;
	} else if (argv[1][1] == 'n') {
	    regression = 0;
	    quiet = -1;
	}
	argv++;
	argc--;
    }
    srand48(getpid());
#ifdef USE_PTHREADS
    gen_pthread_init();
#endif
    CM_TRANSPORT = attr_atom_from_string("CM_TRANSPORT");
    CM_NETWORK_POSTFIX = attr_atom_from_string("CM_NETWORK_POSTFIX");
    CM_MCAST_PORT = attr_atom_from_string("MCAST_PORT");
    CM_MCAST_ADDR = attr_atom_from_string("MCAST_ADDR");

    if (regression && regression_master) {
	return do_regression_master_test();
    }
    cm = CManager_create();
/*    (void) CMfork_comm_thread(cm);*/

    if (argc == 1) {
	attr_list contact_list, listen_list = NULL;
	char *transport = NULL;
	char *postfix = NULL;
	char *string_list;
	char *filter;
	EVstone term, fstone;
	EVaction faction;
	if ((transport = getenv("CMTransport")) != NULL) {
	    if (listen_list == NULL) listen_list = create_attr_list();
	    add_attr(listen_list, CM_TRANSPORT, Attr_String,
		     (attr_value) strdup(transport));
	}
	if ((postfix = getenv("CMNetworkPostfix")) != NULL) {
	    if (listen_list == NULL) listen_list = create_attr_list();
	    add_attr(listen_list, CM_NETWORK_POSTFIX, Attr_String,
		     (attr_value) strdup(postfix));
	}
	CMlisten_specific(cm, listen_list);
	contact_list = CMget_contact_list(cm);
	if (contact_list) {
	    string_list = attr_list_to_string(contact_list);
	} else {
	    /* must be multicast, hardcode a contact list */
#define HELLO_PORT 12345
#define HELLO_GROUP "225.0.0.37"
	    int addr;
	    (void) inet_aton(HELLO_GROUP, (struct in_addr *)&addr);
	    contact_list = create_attr_list();
	    add_attr(contact_list, CM_MCAST_ADDR, Attr_Int4,
		     (attr_value) (long)addr);
	    add_attr(contact_list, CM_MCAST_PORT, Attr_Int4,
		     (attr_value) HELLO_PORT);
	    add_attr(contact_list, CM_TRANSPORT, Attr_String,
		     (attr_value) "multicast");
/*	    conn = CMinitiate_conn(cm, contact_list);*/
	    string_list = attr_list_to_string(contact_list);
	    free_attr_list(contact_list);
	}	
	term = EValloc_stone(cm);
	EVassoc_terminal_action(cm, term, c_format_list, output_handler, NULL);
	filter = create_multityped_action_spec(queue_list, 
						c_format_list, trans);
	
	fstone = EValloc_stone(cm);
	faction = EVassoc_multi_action(cm, fstone, filter, NULL);
	EVaction_set_output(cm, fstone, faction, 0, term);
	
	printf("Contact list \"%d:%s\"\n", fstone, string_list);
	CMsleep(cm, 120);
    } else {
	attr_list attrs;
	int remote_stone, stone = 0;
	int count, i;
	char *map;
	EVsource a_handle, b_handle;
	if (argc == 2) {
	    attr_list contact_list;
	    char *list_str;
	    sscanf(argv[1], "%d:", &remote_stone);
	    list_str = strchr(argv[1], ':') + 1;
	    contact_list = attr_list_from_string(list_str);
	    stone = EValloc_stone(cm);
	    EVassoc_output_action(cm, stone, contact_list, remote_stone);
	}
	attrs = create_attr_list();
#define CMDEMO_TEST_ATOM ATL_CHAR_CONS('C','\115','\104','t')
	set_attr_atom_and_string("CMdemo_test_atom", CMDEMO_TEST_ATOM);
	add_attr(attrs, CMDEMO_TEST_ATOM, Attr_Int4, (attr_value)45678);
	a_handle = EVcreate_submit_handle_free(cm, stone, a_format_list,
					       data_free, NULL);
	b_handle = EVcreate_submit_handle_free(cm, stone, b_format_list,
					       data_free, NULL);
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
		rec_a_ptr a = malloc(sizeof(*a));
		generate_a_record(a);
		if (quiet <=0) {printf("submitting a -> %d\n", a->a_field);}
		EVsubmit(a_handle, a, attrs);
	    } else {
		rec_b_ptr b = malloc(sizeof(*b));
		generate_b_record(b);
		if (quiet <=0) {printf("submitting b -> %d\n", b->b_field);}
		EVsubmit(b_handle, b, attrs);
	    }
	}
	CMsleep(cm, 1);
	free_attr_list(attrs);
    }
    CManager_close(cm);
    return 0;
}

static pid_t subproc_proc = 0;

static void
fail_and_die(signal)
int signal;
{
    fprintf(stderr, "EVtest failed to complete in reasonable time\n");
    if (subproc_proc != 0) {
	kill(subproc_proc, 9);
    }
    exit(1);
}

static
pid_t
run_subprocess(args)
char **args;
{
#ifdef HAVE_WINDOWS_H
    int child;
    child = _spawnv(_P_NOWAIT, "./multiq_test.exe", args);
    if (child == -1) {
	printf("failed for multiq_test\n");
	perror("spawnv");
    }
    return child;
#else
    pid_t child;
    if (quiet <=0) {printf("Forking subprocess\n");}
    child = fork();
    if (child == 0) {
	/* I'm the child */
	execv("./multiq_test", args);
    }
    return child;
#endif
}

static int
do_regression_master_test()
{
    CManager cm;
    char *args[] = {"multiq_test", "-c", NULL, NULL};
    char *filter;
    int exit_state;
    int forked = 0;
    attr_list contact_list, listen_list = NULL;
    char *string_list, *transport, *postfix;
    int message_count = 0;
    EVstone term, fstone;
    EVaction faction;
#ifdef HAVE_WINDOWS_H
    SetTimer(NULL, 5, 1000, (TIMERPROC) fail_and_die);
#else
    struct sigaction sigact;
    sigact.sa_flags = 0;
    sigact.sa_handler = fail_and_die;
    sigemptyset(&sigact.sa_mask);
    sigaddset(&sigact.sa_mask, SIGALRM);
    sigaction(SIGALRM, &sigact, NULL);
    alarm(300);
#endif
    cm = CManager_create();
    forked = CMfork_comm_thread(cm);
    if ((transport = getenv("CMTransport")) != NULL) {
	listen_list = create_attr_list();
	add_attr(listen_list, CM_TRANSPORT, Attr_String,
		 (attr_value) strdup(transport));
    }
    if ((postfix = getenv("CMNetworkPostfix")) != NULL) {
	if (listen_list == NULL) listen_list = create_attr_list();
	add_attr(listen_list, CM_NETWORK_POSTFIX, Attr_String,
		 (attr_value) strdup(postfix));
    }
    CMlisten_specific(cm, listen_list);
    contact_list = CMget_contact_list(cm);
    if (contact_list) {
	string_list = attr_list_to_string(contact_list);
	free_attr_list(contact_list);
    } else {
	/* must be multicast, hardcode a contact list */
#define HELLO_PORT 12345
#define HELLO_GROUP "225.0.0.37"
	int addr;
	(void) inet_aton(HELLO_GROUP, (struct in_addr *)&addr);
	contact_list = create_attr_list();
	add_attr(contact_list, CM_MCAST_ADDR, Attr_Int4,
		 (attr_value) (long)addr);
	add_attr(contact_list, CM_MCAST_PORT, Attr_Int4,
		 (attr_value) HELLO_PORT);
	add_attr(contact_list, CM_TRANSPORT, Attr_String,
		 (attr_value) "multicast");
	(void) CMinitiate_conn(cm, contact_list);
	string_list = attr_list_to_string(contact_list);
	free_attr_list(contact_list);
    }	

    if (quiet <= 0) {
	if (forked) {
	    printf("Forked a communication thread\n");
	} else {
	    printf("Doing non-threaded communication handling\n");
	}
    }
    srand48(1);

    term = EValloc_stone(cm);
    EVassoc_terminal_action(cm, term, c_format_list, output_handler, &message_count);

    filter = create_multityped_action_spec(queue_list, 
					    c_format_list, trans);
    
    fstone = EValloc_stone(cm);
    faction = EVassoc_multi_action(cm, fstone, filter, NULL);
    EVaction_set_output(cm, fstone, faction, 0, term);

    args[2] = string_list;
    args[2] = malloc(10 + strlen(string_list) + strlen(filter));
    sprintf(args[2], "%d:%s", fstone, string_list);
    subproc_proc = run_subprocess(args);

    /* give him time to start */
    CMsleep(cm, 10);
/* stuff */
    if (quiet <= 0) {
	printf("Waiting for remote....\n");
    }
#ifdef HAVE_WINDOWS_H
    if (_cwait(&exit_state, subproc_proc, 0) == -1) {
	perror("cwait");
    }
    if (exit_state == 0) {
	if (quiet <= 0) 
	    printf("Passed single remote subproc test\n");
    } else {
	printf("Single remote subproc exit with status %d\n",
	       exit_state);
    }
#else
    if (waitpid(subproc_proc, &exit_state, 0) == -1) {
	perror("waitpid");
    }
    if (WIFEXITED(exit_state)) {
	if (WEXITSTATUS(exit_state) == 0) {
	    if (quiet <- 1) 
		printf("Passed single remote subproc test\n");
	} else {
	    printf("Single remote subproc exit with status %d\n",
		   WEXITSTATUS(exit_state));
	}
    } else if (WIFSIGNALED(exit_state)) {
	printf("Single remote subproc died with signal %d\n",
	       WTERMSIG(exit_state));
    }
#endif
    free(string_list);
    CManager_close(cm);
    if (message_count != repeat_count / 2) printf("Message count == %d\n", message_count);
    return !(message_count == repeat_count / 2);
}
