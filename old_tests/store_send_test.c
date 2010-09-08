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
#include "evpath_old.h"
#include "gen_thread.h"
#ifdef HAVE_WINDOWS_H
#include <windows.h>
#define drand48() (((double)rand())/((double)RAND_MAX))
#define lrand48() rand()
#define srand48(x)
#else
#include <sys/wait.h>
#endif

typedef struct _complex_rec {
    double r;
    double i;
} complex, *complex_ptr;

typedef struct _nested_rec {
    complex item;
} nested, *nested_ptr;

static IOField nested_field_list[] =
{
    {"item", "complex", sizeof(complex), IOOffset(nested_ptr, item)},
    {NULL, NULL, 0, 0}
};

static IOField complex_field_list[] =
{
    {"r", "double", sizeof(double), IOOffset(complex_ptr, r)},
    {"i", "double", sizeof(double), IOOffset(complex_ptr, i)},
    {NULL, NULL, 0, 0}
};

static int seq_num = 0;

typedef struct _simple_rec {
    int sequence_number;
    int integer_field;
    short short_field;
    long long_field;
    nested nested_field;
    double double_field;
    char char_field;
    int scan_sum;
} simple_rec, *simple_rec_ptr;

static IOField simple_field_list[] =
{
    {"sequence_number", "integer",
     sizeof(int), IOOffset(simple_rec_ptr, sequence_number)}, 
    {"integer_field", "integer",
     sizeof(int), IOOffset(simple_rec_ptr, integer_field)},
    {"short_field", "integer",
     sizeof(short), IOOffset(simple_rec_ptr, short_field)},
    {"long_field", "integer",
     sizeof(long), IOOffset(simple_rec_ptr, long_field)},
    {"nested_field", "nested",
     sizeof(nested), IOOffset(simple_rec_ptr, nested_field)},
    {"double_field", "float",
     sizeof(double), IOOffset(simple_rec_ptr, double_field)},
    {"char_field", "char",
     sizeof(char), IOOffset(simple_rec_ptr, char_field)},
    {"scan_sum", "integer",
     sizeof(int), IOOffset(simple_rec_ptr, scan_sum)},
    {NULL, NULL, 0, 0}
};

static CMFormatRec simple_format_list[] =
{
    {"simple", simple_field_list},
    {"complex", complex_field_list},
    {"nested", nested_field_list},
    {NULL, NULL}
};

static
void 
generate_record(simple_rec_ptr event)
{
    long sum = 0;
    event->sequence_number = seq_num++; 
    event->integer_field = (int) lrand48() % 100;
    sum += event->integer_field % 100;
    event->short_field = ((short) lrand48());
    sum += event->short_field % 100;
    event->long_field = ((long) lrand48());
    sum += event->long_field % 100;

    event->nested_field.item.r = drand48();
    sum += ((int) (event->nested_field.item.r * 100.0)) % 100;
    event->nested_field.item.i = drand48();
    sum += ((int) (event->nested_field.item.i * 100.0)) % 100;

    event->double_field = drand48();
    sum += ((int) (event->double_field * 100.0)) % 100;
    event->char_field = lrand48() % 128;
    sum += event->char_field;
    sum = sum % 100;
    event->scan_sum = (int) sum;
}

int quiet = 1;

static
int
simple_handler(CManager cm, void *vevent, void *client_data, attr_list attrs)
{
    simple_rec_ptr event = vevent;
    long sum = 0, scan_sum = 0;
    (void)cm;
    if (event->sequence_number != seq_num++) {
        printf("Sequence number %d but expected %d\n", event->sequence_number,
            seq_num - 1);
    }
    sum += event->integer_field % 100;
    sum += event->short_field % 100;
    sum += event->long_field % 100;
    sum += ((int) (event->nested_field.item.r * 100.0)) % 100;
    sum += ((int) (event->nested_field.item.i * 100.0)) % 100;
    sum += ((int) (event->double_field * 100.0)) % 100;
    sum += event->char_field;
    sum = sum % 100;
    scan_sum = event->scan_sum;
    if (sum != scan_sum) {
	printf("Received record checksum does not match. expected %d, got %d\n",
	       (int) sum, (int) scan_sum);
    }
    if ((quiet <= 0) || (sum != scan_sum)) {
	printf("In the handler, event data is :\n");
	printf("	integer_field = %d\n", event->integer_field);
	printf("	short_field = %d\n", event->short_field);
	printf("	long_field = %ld\n", event->long_field);
	printf("	double_field = %g\n", event->double_field);
	printf("	char_field = %c\n", event->char_field);
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
static int repeat_count = 5000;

int
main(int argc, char **argv)
{ 
    CManager cm;
    int regression_master = 1;

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
	EVstone term, fstone;
	EVaction faction;
        printf("top branch\n");
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
	EVassoc_terminal_action(cm, term, simple_format_list, simple_handler, NULL);
	
	fstone = EValloc_stone(cm);
	faction = EVassoc_store_action(cm, fstone, term, -1);
	(void)faction;
        fprintf(stderr, "setup reciever\n");
	
	printf("Contact list \"%d:%s\"\n", fstone, string_list);
	CMsleep(cm, 120);
    } else {
	simple_rec data;
	attr_list attrs;
	int remote_stone, stone = 0;
	int count;
	EVsource source_handle;
	atom_t CMDEMO_TEST_ATOM = attr_atom_from_string("CMdemo_test_atom");
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
	/* add_attr(attrs, CMDEMO_TEST_ATOM, Attr_Int4, (attr_value)45678); */
	source_handle = EVcreate_submit_handle(cm, stone, simple_format_list);
	count = repeat_count;
	while (count != 0) {
            attr_list event_attrs;
            event_attrs = create_attr_list();
	    generate_record(&data);
            add_int_attr(event_attrs, CMDEMO_TEST_ATOM, data.sequence_number);
	    if (quiet <=0) {printf("submitting %ld\n", data.long_field);}
	    EVsubmit(source_handle, &data, event_attrs);
            count--;
	}
	CMsleep(cm, 1);
	free_attr_list(attrs);
    }
    CManager_close(cm);
    return 0;
}

static pid_t subproc_proc = 0;

static void
fail_and_die(int signal)
{
    (void) signal;
    fprintf(stderr, "EVtest failed to complete in reasonable time\n");
    if (subproc_proc != 0) {
	kill(subproc_proc, 9);
    }
    exit(1);
}

static
pid_t
run_subprocess(char **args)
{
#ifdef HAVE_WINDOWS_H
    int child;
    child = _spawnv(_P_NOWAIT, "./store_send_test.exe", args);
    if (child == -1) {
	printf("failed for store_send_test\n");
	perror("spawnv");
    }
    return child;
#else
#ifndef NOTDEF
    pid_t child;
    if (quiet <=0) {printf("Forking subprocess\n");}
    child = fork();
    if (child == 0) {
	/* I'm the child */
	execv("./store_send_test", args);
        perror("execv");
        exit(EXIT_FAILURE);
    }
    return child;
#else
    printf("nothing doing  %s %s\n", args[1], args[2]);
    sleep(20);
#endif
#endif
}

static int
do_regression_master_test()
{
    CManager cm;
    char *args[] = {"store_send_test", "-c", NULL, NULL};
    int exit_state;
    int forked = 0;
    attr_list contact_list, listen_list = NULL;
    char *string_list, *transport, *postfix;
    int message_count = 0;
    int failed = 0;
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
    EVassoc_terminal_action(cm, term, simple_format_list, simple_handler, &message_count);
    
    fstone = EValloc_stone(cm);
    faction = EVassoc_store_action(cm, fstone, term, -1);

    args[2] = malloc(10 + strlen(string_list));
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
    if (message_count != 0) {
        printf("Message count high too early\n");
    }
    EVsend_stored(cm, fstone, faction);
    free(string_list);
    CManager_close(cm);
    if (message_count != repeat_count) printf("Message count == %d\n", message_count);
    return !(!failed && message_count == repeat_count);
}
